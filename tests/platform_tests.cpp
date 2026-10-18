// Tests of the platform code that runs without a window (Linux): the wallpaper's colors, read from
// a KDE config of their own in a scratch XDG_CONFIG_HOME and changed as KConfig does; the AT-SPI
// bridge, on a D-Bus of their own (a dbus-daemon started for them), where they play the AT-SPI
// registry, the accessibility bus's launcher and a screen reader.

#include "atspi_bridge.hpp"
#include "libdbus.hpp"
#include "wallpaper_colors.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

using namespace kompoz;
namespace fs = std::filesystem;

namespace {

int checks = 0, failures = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        ++checks;                                                      \
        if (!(c)) {                                                    \
            ++failures;                                                \
            std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
        }                                                              \
    } while (0)

// A 24-bit BMP of one color, which SDL decodes.
void writeBmp(const fs::path& path, uint32_t rgb, int w = 64, int h = 48) {
    int row = (w * 3 + 3) / 4 * 4;
    uint32_t size = 54 + uint32_t(row * h);
    std::vector<uint8_t> b(size);
    auto put = [&](size_t at, uint32_t v, int n) { for (int i = 0; i < n; ++i) b[at + size_t(i)] = uint8_t(v >> (8 * i)); };
    b[0] = 'B', b[1] = 'M';
    put(2, size, 4), put(10, 54, 4), put(14, 40, 4), put(18, uint32_t(w), 4), put(22, uint32_t(h), 4), put(26, 1, 2), put(28, 24, 2);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) put(54 + size_t(y * row + x * 3), rgb, 3);  // little endian: B, G, R
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

void kdeConfigIsRead() {
    auto dirs = [](const std::string& d) -> std::vector<std::string> {
        if (d == "/usr/share/wallpapers/Next/contents/images") return {"1920x1080.png", "5120x2880.png", "800x600.jpg", "screenshot.png"};
        if (d == "/usr/share/wallpapers/Next/contents/images_dark") return {"3840x2160.png"};
        return {};
    };
    std::string config = R"(
[Containments][1]
activityId=
formfactor=2
lastScreen=0
location=4
plugin=org.kde.panel
wallpaperplugin=org.kde.image

[Containments][1][Wallpaper][org.kde.image][General]
Image=/panel.png

[Containments][2]
activityId=other
lastScreen=0
location=0
plugin=org.kde.plasma.folder
wallpaperplugin=org.kde.image

[Containments][2][Wallpaper][org.kde.image][General]
Image=file:///home/me/Other%20Pics/a.jpg

[Containments][3]
activityId=current
lastScreen=1
location=0
wallpaperplugin=org.kde.image

[Containments][3][Wallpaper][org.kde.image][General]
Image=/second-screen.jpg

[Containments][4]
activityId=current
lastScreen=0
location=0
wallpaperplugin=org.kde.image

[Containments][4][Wallpaper][org.kde.image][General]
Image=file:///usr/share/wallpapers/Next/
PreviewImage=/ignored.png
)";
    // The first screen's desktop in the current activity; a package: its biggest picture, images_dark when dark.
    CHECK(sdl::kdeWallpaperFromConfig(config, "current", false, dirs) == "/usr/share/wallpapers/Next/contents/images/5120x2880.png");
    CHECK(sdl::kdeWallpaperFromConfig(config, "current", true, dirs) == "/usr/share/wallpapers/Next/contents/images_dark/3840x2160.png");
    // Another activity's own (a file:// URI, %20 decoded); panels never.
    CHECK(sdl::kdeWallpaperFromConfig(config, "other", false, dirs) == "/home/me/Other Pics/a.jpg");
    std::string noFolder = config.substr(0, config.find("[Containments][4]"));
    CHECK(sdl::kdeWallpaperFromConfig(noFolder, "current", false, dirs) == "/home/me/Other Pics/a.jpg");  // screen 0 beats activity
    CHECK(sdl::kdeWallpaperFromConfig("[Containments][6]\nactivityId=current\nlastScreen=1\nwallpaperplugin=org.kde.image\n"
                                      "[Containments][6][Wallpaper][org.kde.image][General]\nImage=/one.jpg\n"
                                      "[Containments][7]\nactivityId=other\nlastScreen=0\nwallpaperplugin=org.kde.image\n"
                                      "[Containments][7][Wallpaper][org.kde.image][General]\nImage=/zero.jpg\n",
                                      "current", false, dirs) == "/zero.jpg");  // whatever the order they come in
    CHECK(sdl::kdeWallpaperFromConfig("[Containments][1]\nlocation=4\nwallpaperplugin=org.kde.image\n"
                                      "[Containments][1][Wallpaper][org.kde.image][General]\nImage=/p.png\n", "", false, dirs).empty());
    CHECK(sdl::kdeWallpaperFromConfig("", "", false, dirs).empty());
    // A slideshow (another plugin) isn't read.
    CHECK(sdl::kdeWallpaperFromConfig("[Containments][5]\nlastScreen=0\nwallpaperplugin=org.kde.slideshow\n"
                                      "[Containments][5][Wallpaper][org.kde.image][General]\nImage=/x.png\n", "", false, dirs).empty());
}

// The wallpaper changed while the app runs: told by inotify, once the change settled.
void wallpaperChangesAreFollowed() {
    fs::path home = fs::temp_directory_path() / ("kompoz-platform-tests-" + std::to_string(getpid()));
    fs::remove_all(home);
    fs::create_directories(home);
    setenv("XDG_CONFIG_HOME", home.c_str(), 1);
    setenv("XDG_CURRENT_DESKTOP", "KDE", 1);
    unsetenv("KOMPOZ_NO_WALLPAPER_COLORS");
    fs::path red = home / "red.bmp", blue = home / "blue.bmp", blue2 = home / "blue2.bmp";
    writeBmp(red, 0xD32F2F), writeBmp(blue, 0x1976D2), writeBmp(blue2, 0x1976D2);
    auto writeConfig = [&](const std::string& image) {
        fs::path tmp = home / "tmp-appletsrc";
        std::ofstream(tmp) << "[Containments][9]\nlastScreen=0\nlocation=0\nwallpaperplugin=org.kde.image\n\n"
                              "[Containments][9][Wallpaper][org.kde.image][General]\nImage="
                           << image << "\n";
        fs::rename(tmp, home / "plasma-org.kde.plasma.desktop-appletsrc");  // as KConfig writes: a rename
    };
    writeConfig(red.string());
    sdl::WallpaperColors colors(false);
    CHECK(colors.eventType() != 0);
    auto waitFor = [&](const std::function<bool()>& done) {
        for (int i = 0; i < 100; ++i) {
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    };
    auto hueNear = [&](uint32_t rgb) {
        float hue = Hct::fromColor(TonalPalette::fromSeed(Color::hex(rgb)).accent1[8]).hue;
        return [&colors, hue] {
            auto p = colors.palette();
            return p && std::abs(Hct::fromColor(p->accent1[8]).hue - hue) < 8;
        };
    };
    CHECK(waitFor(hueNear(0xD32F2F)));
    auto t0 = std::chrono::steady_clock::now();
    writeConfig("file://" + blue.string());
    CHECK(waitFor(hueNear(0x1976D2)));
    long seen = long(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    CHECK(seen >= 250 && seen < 3000);  // by inotify, after the change settled (not by polling)
    // Another file with the same colors: no event, the palette being the same.
    SDL_PumpEvents();
    SDL_FlushEvent(colors.eventType());
    writeConfig(blue2.string());
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    SDL_PumpEvents();
    SDL_Event e;
    CHECK(SDL_PeepEvents(&e, 1, SDL_PEEKEVENT, colors.eventType(), colors.eventType()) == 0);
    // A picture that isn't there: none.
    writeConfig((home / "no-such-picture.png").string());
    CHECK(waitFor([&] { return !colors.palette(); }));
    fs::remove_all(home);
}

// ---- AT-SPI ----

namespace dbus = sdl::dbus;

constexpr const char* kA11y = "/org/a11y/atspi/accessible/";

// The other end of the bridge: a connection to the test bus that answers what the bridge asks
// (`answer`: true if it replied) and keeps the signals it hears, as
// "Member detail detail1 detail2 object data".
struct Peer {
    dbus::Lib& d;
    dbus::Connection* c = nullptr;
    std::string bridge;  // the bridge's unique name, once it called
    std::function<bool(dbus::Message*)> answer;
    std::vector<std::string> heard;

    Peer(dbus::Lib& lib, const std::string& address) : d(lib) {
        for (int i = 0; i < 100 && !c; ++i) {  // until the daemon listens
            dbus::Error e;
            d.error_init(&e);
            c = d.connection_open_private(address.c_str(), &e);
            d.error_free(&e);
            if (!c) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!c) return;
        d.connection_set_exit_on_disconnect(c, 0);
        dbus::Error e;
        d.error_init(&e);
        d.bus_register(c, &e);
        for (const char* rule : {"type='signal',interface='org.a11y.atspi.Event.Object'", "type='signal',interface='org.a11y.atspi.Event.Window'",
                                 "type='signal',interface='org.a11y.atspi.Event.Focus'"})
            d.bus_add_match(c, rule, &e);
        d.error_free(&e);
    }
    ~Peer() {
        if (!c) return;
        d.connection_close(c);
        d.connection_unref(c);
    }
    bool own(const char* name) {
        dbus::Error e;
        d.error_init(&e);
        int r = d.bus_request_name(c, name, 4, &e);  // DBUS_NAME_FLAG_DO_NOT_QUEUE
        d.error_free(&e);
        return r == 1;  // the primary owner
    }
    void take(dbus::Message* m) {
        int type = d.message_get_type(m);
        if (type == dbus::kMessageMethodCall) {
            if (!answer || !answer(m)) {
                dbus::Message* error = d.message_new_error(m, "org.freedesktop.DBus.Error.UnknownMethod", "no");
                d.connection_send(c, error, nullptr);
                d.message_unref(error);
            }
        } else if (type == dbus::kMessageSignal && std::string_view(d.message_get_interface(m)).starts_with("org.a11y.atspi.Event")) {
            dbus::Reader r(d, m);
            std::string path = d.message_get_path(m);
            std::string line = std::string(d.message_get_member(m)) + " " + r.str();
            line += " " + std::to_string(r.i32());
            line += " " + std::to_string(r.i32());
            line += " " + path.substr(path.rfind('/') + 1);
            dbus::Reader v = r.enter();
            if (v.type() == dbus::kTypeString) line += " " + v.str();
            else if (v.type() == dbus::kTypeDouble) line += " " + std::to_string(int(v.dbl()));
            else if (v.type() == dbus::kTypeStruct) {
                dbus::Reader ref = v.enter();
                ref.str();
                std::string p = ref.path();
                line += " " + p.substr(p.rfind('/') + 1);
            }
            heard.push_back(line);
        }
        d.connection_flush(c);
    }
    // Messages for `ms` milliseconds, or until `done`.
    bool serve(int ms, const std::function<bool()>& done = {}) {
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            if (done && done()) return true;
            d.connection_read_write(c, 10);
            while (dbus::Message* m = d.connection_pop_message(c)) {
                take(m);
                d.message_unref(m);
            }
        }
        return done && done();
    }
    bool heardLine(const std::string& line) const { return std::find(heard.begin(), heard.end(), line) != heard.end(); }

    // A call to the bridge; its reply (or error), or nullptr.
    dbus::Message* call(const std::string& path, const char* interface, const char* method,
                        const std::function<void(dbus::Writer&)>& args = {}) {
        dbus::Message* m = d.message_new_method_call(bridge.c_str(), path.c_str(), interface, method);
        if (args) {
            dbus::Writer w(d, m);
            args(w);
        }
        dbus::Error e;
        d.error_init(&e);
        dbus::Message* reply = d.connection_send_with_reply_and_block(c, m, 2000, &e);
        if (!reply && e.name) {  // an error reply: kept as its name
            lastError = e.name;
        }
        d.error_free(&e);
        d.message_unref(m);
        return reply;
    }
    std::string lastError;
    bool succeeds(const std::string& path, const char* interface, const char* method, const std::function<void(dbus::Writer&)>& args = {}) {
        dbus::Message* reply = call(path, interface, method, args);
        if (reply) d.message_unref(reply);
        return reply != nullptr;
    }
    template <class Read>
    auto ask(const std::string& path, const char* interface, const char* method, Read&& read,
             const std::function<void(dbus::Writer&)>& args = {}) {
        dbus::Message* reply = call(path, interface, method, args);
        dbus::Reader r = reply ? dbus::Reader(d, reply) : dbus::Reader(d);
        auto value = read(r);
        if (reply) d.message_unref(reply);
        return value;
    }
    std::string property(const std::string& path, const char* interface, const char* name) {
        return ask(path, "org.freedesktop.DBus.Properties", "Get", [](dbus::Reader& r) {
            dbus::Reader v = r.enter();
            switch (v.type()) {
            case dbus::kTypeString: return v.str();
            case dbus::kTypeInt32: return std::to_string(v.i32());
            case dbus::kTypeDouble: return std::to_string(v.dbl()).substr(0, 4);
            case dbus::kTypeStruct: {
                dbus::Reader s = v.enter();
                std::string name = s.str();
                return name + " " + s.path();
            }
            default: return std::string("?");
            }
        }, [&](dbus::Writer& w) { w.str(interface), w.str(name); });
    }
    std::vector<std::string> children(const std::string& path) {
        return ask(path, "org.a11y.atspi.Accessible", "GetChildren", [](dbus::Reader& r) {
            std::vector<std::string> out;
            dbus::Reader a = r.enter();
            while (a.type() == dbus::kTypeStruct) {
                dbus::Reader s = a.enter();
                s.str();
                std::string p = s.path();
                out.push_back(p.substr(p.rfind('/') + 1));
            }
            return out;
        });
    }
    uint32_t role(const std::string& path) {
        return ask(path, "org.a11y.atspi.Accessible", "GetRole", [](dbus::Reader& r) { return r.u32(); });
    }
    bool state(const std::string& path, int which) {
        return ask(path, "org.a11y.atspi.Accessible", "GetState", [&](dbus::Reader& r) {
            dbus::Reader a = r.enter();
            uint32_t bits[2] = {a.u32(), a.u32()};
            return (bits[which / 32] >> (which % 32) & 1) != 0;
        });
    }
    std::string extents(const std::string& path, uint32_t coordType) {
        return ask(path, "org.a11y.atspi.Component", "GetExtents", [](dbus::Reader& r) {
            dbus::Reader s = r.enter();
            std::string out;
            for (int i = 0; i < 4; ++i) out += (i ? " " : "") + std::to_string(s.i32());
            return out;
        }, [&](dbus::Writer& w) { w.u32(coordType); });
    }
    std::string text(const std::string& path, const char* interface, const char* method, const std::function<void(dbus::Writer&)>& args) {
        return ask(path, interface, method, [](dbus::Reader& r) {
            std::string out = r.str();
            while (r.type() == dbus::kTypeInt32) out += " " + std::to_string(r.i32());
            return out;
        }, args);
    }
    bool boolean(const std::string& path, const char* interface, const char* method, const std::function<void(dbus::Writer&)>& args) {
        return ask(path, interface, method, [](dbus::Reader& r) { return r.boolean(); }, args);
    }
};

// A dbus-daemon of the tests' own, on an abstract socket: nothing of the desktop's is touched.
struct TestBus {
    pid_t pid = 0;
    std::string address;
    fs::path dir;
    TestBus() {
        std::string name = "kompoz-platform-tests-" + std::to_string(getpid());
        address = "unix:abstract=" + name;
        dir = fs::temp_directory_path() / name;
        fs::create_directories(dir);
        std::ofstream(dir / "bus.conf") << "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN\" "
                                           "\"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                                           "<busconfig><type>session</type><listen>" << address << "</listen><auth>EXTERNAL</auth>"
                                           "<policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/>"
                                           "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>\n";
        std::string config = "--config-file=" + (dir / "bus.conf").string();
        const char* argv[] = {"dbus-daemon", config.c_str(), "--nofork", "--nopidfile", nullptr};
        posix_spawn_file_actions_t quiet;  // (nor holding the tests' output open)
        posix_spawn_file_actions_init(&quiet);
        posix_spawn_file_actions_addopen(&quiet, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&quiet, 2, "/dev/null", O_WRONLY, 0);
        if (posix_spawnp(&pid, "dbus-daemon", &quiet, nullptr, const_cast<char**>(argv), environ) != 0) pid = 0;
        posix_spawn_file_actions_destroy(&quiet);
    }
    ~TestBus() {
        if (pid) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
        }
        fs::remove_all(dir);
    }
};

std::vector<SemanticsNode> testTree() {
    std::vector<SemanticsNode> nodes(7);
    nodes[0] = {.id = 1, .bounds = {10, 20, 30, 40}, .label = "OK", .clickable = true};
    nodes[1] = {.id = 2, .bounds = {0, 70, 24, 24}, .role = SemanticsRole::Checkbox, .clickable = true, .checked = false};
    nodes[2] = {.id = 3, .bounds = {0, 100, 200, 56}, .label = "Name", .value = "héllo wörld", .editable = true};
    nodes[3] = {.id = 4, .bounds = {0, 160, 200, 40}, .stateDescription = "3", .role = SemanticsRole::Slider,
                .range = ProgressBarRangeInfo{3, 0, 10, 4}, .settable = true};
    nodes[4] = {.id = 5, .bounds = {0, 200, 200, 100}, .scrollable = true, .customActions = {"Archive"}};
    nodes[5] = {.id = 6, .parent = 5, .bounds = {0, 210, 200, 20}, .label = "Item"};
    nodes[6] = {.id = 7, .bounds = {0, 300, 200, 56}, .value = "abc", .editable = true, .password = true};
    return nodes;
}

void atspiBridgeServesTheTree() {
    TestBus bus;
    dbus::Lib d;
    if (!bus.pid || !d.load()) {
        std::printf("  (no dbus-daemon or libdbus: the AT-SPI bridge isn't tested)\n");
        return;
    }
    setenv("DBUS_SESSION_BUS_ADDRESS", bus.address.c_str(), 1);
    setenv("AT_SPI_BUS_ADDRESS", bus.address.c_str(), 1);
    setenv("KOMPOZ_ATSPI", "1", 1);
    Peer peer(d, bus.address);
    CHECK(peer.c && peer.own("org.a11y.atspi.Registry"));
    if (!peer.c) return;
    std::string embedded;
    peer.answer = [&](dbus::Message* m) {
        if (std::string_view(d.message_get_member(m)) != "Embed") return false;
        dbus::Reader r(d, m);
        dbus::Reader ref = r.enter();
        std::string name = ref.str();
        embedded = name + " " + ref.path();
        peer.bridge = name;
        dbus::Message* reply = d.message_new_method_return(m);
        dbus::Writer(d, reply).open(dbus::kTypeStruct, nullptr, [&](dbus::Writer& s) {
            s.str("org.a11y.atspi.Registry");
            s.path("/org/a11y/atspi/accessible/root");
        });
        d.connection_send(peer.c, reply, nullptr);
        d.message_unref(reply);
        return true;
    };

    sdl::AtspiBridge bridge("Test app");
    CHECK(bridge.eventType() != 0);
    CHECK(peer.serve(3000, [&] { return bridge.active(); }));  // connected, and into the registry's desktop
    if (peer.bridge.empty()) return;
    CHECK(embedded == peer.bridge + " /org/a11y/atspi/accessible/root");
    CHECK(bridge.takeTreeRequest() && !bridge.takeTreeRequest());
    bridge.publish(testTree(), {.x = 100, .y = 50, .width = 400, .height = 300, .dpToPoints = 2, .focused = true, .title = "Main"});
    peer.serve(300);
    CHECK(peer.heardLine("Activate  0 0 window Main"));
    CHECK(peer.heardLine("StateChanged active 1 0 window"));

    std::string root = "/org/a11y/atspi/accessible/root", window = "/org/a11y/atspi/accessible/window";
    auto node = [](int id) { return kA11y + std::to_string(id); };
    // The application, its window, the nodes in it.
    CHECK(peer.role(root) == 75 && peer.role(window) == 23);
    CHECK(peer.property(root, "org.a11y.atspi.Accessible", "Name") == "Test app");
    CHECK(peer.property(root, "org.a11y.atspi.Accessible", "Parent") == "org.a11y.atspi.Registry /org/a11y/atspi/accessible/root");
    CHECK(peer.property(root, "org.a11y.atspi.Application", "ToolkitName") == "Kompoz");
    CHECK(peer.children(root) == std::vector<std::string>{"window"});
    CHECK(peer.property(window, "org.a11y.atspi.Accessible", "Name") == "Main");
    CHECK(peer.property(window, "org.a11y.atspi.Accessible", "Parent") == peer.bridge + " " + root);
    CHECK((peer.children(window) == std::vector<std::string>{"1", "2", "3", "4", "5", "7"}));
    CHECK(peer.state(window, 1));  // active
    CHECK(peer.extents(window, 0) == "100 50 400 300");
    // A button: where it is (dp to points, from the screen, the window, its parent), what it does.
    CHECK(peer.role(node(1)) == 43);
    CHECK(peer.property(node(1), "org.a11y.atspi.Accessible", "Name") == "OK");
    CHECK(peer.property(node(1), "org.a11y.atspi.Accessible", "Parent") == peer.bridge + " " + window);
    CHECK(peer.extents(node(1), 0) == "120 90 60 80");
    CHECK(peer.extents(node(1), 1) == "20 40 60 80");
    CHECK(peer.extents(node(6), 2) == "0 20 400 40");
    CHECK(peer.state(node(1), 8) && peer.state(node(1), 24) && peer.state(node(1), 11) && peer.state(node(1), 30));
    CHECK(!peer.state(node(1), 12) && !peer.state(node(1), 7));
    CHECK(peer.text(node(1), "org.a11y.atspi.Action", "GetName", [](dbus::Writer& w) { w.i32(0); }) == "click");
    CHECK(peer.boolean(node(1), "org.a11y.atspi.Action", "DoAction", [](dbus::Writer& w) { w.i32(0); }));
    CHECK(!peer.boolean(node(1), "org.a11y.atspi.Action", "DoAction", [](dbus::Writer& w) { w.i32(1); }));
    SDL_PumpEvents();
    CHECK(SDL_PeepEvents(nullptr, 0, SDL_PEEKEVENT, bridge.eventType(), bridge.eventType()) > 0);  // the main loop woken
    auto requests = bridge.takeRequests();
    CHECK(requests.size() == 1 && requests[0].id == 1 && requests[0].action == SemanticsAction::Click);
    CHECK(bridge.takeRequests().empty());
    CHECK(!peer.succeeds(node(1), "org.a11y.atspi.Text", "GetText", [](dbus::Writer& w) { w.i32(0), w.i32(-1); }));
    CHECK(peer.lastError == "org.freedesktop.DBus.Error.UnknownMethod");  // a button has no text
    // A checkbox.
    CHECK(peer.role(node(2)) == 7);
    CHECK(peer.state(node(2), 41) && !peer.state(node(2), 4));
    // A text field: offsets in characters, words, what is set.
    CHECK(peer.role(node(3)) == 79);
    CHECK(peer.state(node(3), 7) && peer.state(node(3), 11));
    CHECK(peer.property(node(3), "org.a11y.atspi.Text", "CharacterCount") == "11");
    CHECK(peer.text(node(3), "org.a11y.atspi.Text", "GetText", [](dbus::Writer& w) { w.i32(1), w.i32(3); }) == "él");
    CHECK(peer.text(node(3), "org.a11y.atspi.Text", "GetText", [](dbus::Writer& w) { w.i32(6), w.i32(-1); }) == "wörld");
    CHECK(peer.text(node(3), "org.a11y.atspi.Text", "GetStringAtOffset", [](dbus::Writer& w) { w.i32(7), w.u32(1); }) == "wörld 6 11");
    CHECK(peer.text(node(3), "org.a11y.atspi.Text", "GetStringAtOffset", [](dbus::Writer& w) { w.i32(5), w.u32(1); }) == "héllo  0 6");
    CHECK(peer.text(node(3), "org.a11y.atspi.Text", "GetStringAtOffset", [](dbus::Writer& w) { w.i32(1), w.u32(0); }) == "é 1 2");
    CHECK(peer.text(node(3), "org.a11y.atspi.Text", "GetTextAfterOffset", [](dbus::Writer& w) { w.i32(1), w.u32(1); }) == "wörld 6 11");
    CHECK(peer.text(node(3), "org.a11y.atspi.Text", "GetTextBeforeOffset", [](dbus::Writer& w) { w.i32(7), w.u32(1); }) == "héllo  0 6");
    CHECK(peer.text(node(3), "org.a11y.atspi.Text", "GetTextAtOffset", [](dbus::Writer& w) { w.i32(3), w.u32(5); }) == "héllo wörld 0 11");
    CHECK(peer.ask(node(3), "org.a11y.atspi.Text", "GetCharacterAtOffset", [](dbus::Reader& r) { return r.i32(); },
                   [](dbus::Writer& w) { w.i32(1); }) == 0xE9);
    CHECK(peer.boolean(node(3), "org.a11y.atspi.EditableText", "SetTextContents", [](dbus::Writer& w) { w.str("x"); }));
    CHECK(peer.boolean(node(3), "org.a11y.atspi.EditableText", "DeleteText", [](dbus::Writer& w) { w.i32(0), w.i32(6); }));
    CHECK(peer.boolean(node(3), "org.a11y.atspi.EditableText", "InsertText", [](dbus::Writer& w) { w.i32(5), w.str("!?"), w.i32(1); }));
    CHECK(peer.boolean(node(3), "org.a11y.atspi.Component", "GrabFocus", {}));
    CHECK(!peer.boolean(node(1), "org.a11y.atspi.Component", "GrabFocus", {}));
    requests = bridge.takeRequests();
    CHECK(requests.size() == 4);
    if (requests.size() == 4) {
        CHECK(requests[0].action == SemanticsAction::SetText && requests[0].text == "x" && requests[0].id == 3);
        CHECK(requests[1].text == "wörld");
        CHECK(requests[2].text == "héllo! wörld");
        CHECK(requests[3].action == SemanticsAction::Focus && requests[3].id == 3);
    }
    // A password field: what is typed isn't said.
    CHECK(peer.role(node(7)) == 40);
    CHECK(peer.text(node(7), "org.a11y.atspi.Text", "GetText", [](dbus::Writer& w) { w.i32(0), w.i32(-1); }) == "•••");
    // A slider.
    CHECK(peer.role(node(4)) == 51);
    CHECK(peer.property(node(4), "org.a11y.atspi.Value", "CurrentValue") == "3.00");
    CHECK(peer.property(node(4), "org.a11y.atspi.Value", "MaximumValue") == "10.0");
    CHECK(peer.property(node(4), "org.a11y.atspi.Value", "MinimumIncrement") == "2.00");
    CHECK(peer.property(node(4), "org.a11y.atspi.Value", "Text") == "3");
    CHECK(peer.property(node(4), "org.a11y.atspi.Accessible", "Description") == "3");
    CHECK(peer.succeeds(node(4), "org.freedesktop.DBus.Properties", "Set", [](dbus::Writer& w) {
        w.str("org.a11y.atspi.Value"), w.str("CurrentValue");
        w.open(dbus::kTypeVariant, "d", [](dbus::Writer& v) { v.dbl(7); });
    }));
    requests = bridge.takeRequests();
    CHECK(requests.size() == 1 && requests[0].action == SemanticsAction::SetProgress && requests[0].argument == 7);
    // A scrolling list: its actions (its own too), the node in it.
    CHECK(peer.role(node(5)) == 49 && peer.role(node(6)) == 29);
    CHECK(peer.children(node(5)) == std::vector<std::string>{"6"});
    CHECK(peer.property(node(5), "org.a11y.atspi.Action", "NActions") == "3");
    CHECK(peer.text(node(5), "org.a11y.atspi.Action", "GetName", [](dbus::Writer& w) { w.i32(2); }) == "Archive");
    CHECK(peer.boolean(node(5), "org.a11y.atspi.Action", "DoAction", [](dbus::Writer& w) { w.i32(1); }));
    CHECK(peer.boolean(node(5), "org.a11y.atspi.Action", "DoAction", [](dbus::Writer& w) { w.i32(2); }));
    requests = bridge.takeRequests();
    CHECK(requests.size() == 2 && requests[0].action == SemanticsAction::ScrollBackward && requests[1].action == SemanticsAction::CustomAction &&
          requests[1].argument == 0);
    auto interfaces = [&](const std::string& path) {
        return peer.ask(path, "org.a11y.atspi.Accessible", "GetInterfaces", [](dbus::Reader& r) {
            std::string out;
            dbus::Reader a = r.enter();
            while (a.type() == dbus::kTypeString) out += a.str() + " ";
            return out;
        });
    };
    CHECK(interfaces(node(6)) == "org.a11y.atspi.Accessible org.a11y.atspi.Component ");  // a label does nothing
    CHECK(interfaces(node(1)).find("org.a11y.atspi.Action") != std::string::npos);
    CHECK(peer.ask(node(6), "org.a11y.atspi.Accessible", "GetIndexInParent", [](dbus::Reader& r) { return r.i32(); }) == 0);
    CHECK(peer.ask(node(3), "org.a11y.atspi.Accessible", "GetIndexInParent", [](dbus::Reader& r) { return r.i32(); }) == 2);
    auto childAt = [&](const std::string& path, int i) {
        return peer.ask(path, "org.a11y.atspi.Accessible", "GetChildAtIndex", [](dbus::Reader& r) {
            dbus::Reader s = r.enter();
            s.str();
            return s.path();
        }, [&](dbus::Writer& w) { w.i32(i); });
    };
    CHECK(childAt(window, 2) == node(3) && childAt(window, 6) == "/org/a11y/atspi/null");
    CHECK(peer.ask(node(5), "org.a11y.atspi.Component", "GetAccessibleAtPoint", [](dbus::Reader& r) {
        dbus::Reader s = r.enter();
        s.str();
        return s.path();
    }, [](dbus::Writer& w) { w.i32(150), w.i32(475), w.u32(0); }) == node(6));
    CHECK(peer.ask(node(5), "org.a11y.atspi.Component", "Contains", [](dbus::Reader& r) { return r.boolean(); },
                   [](dbus::Writer& w) { w.i32(150), w.i32(800), w.u32(0); }) == false);
    // What isn't there.
    CHECK(!peer.succeeds(node(99), "org.a11y.atspi.Accessible", "GetRole"));
    CHECK(peer.lastError == "org.freedesktop.DBus.Error.UnknownObject");
    CHECK(!peer.succeeds(node(1), "org.a11y.atspi.Accessible", "Frobnicate"));
    CHECK(peer.lastError == "org.freedesktop.DBus.Error.UnknownMethod");
    CHECK(peer.succeeds("/org/a11y/atspi/cache", "org.a11y.atspi.Cache", "GetItems"));

    // What changed is told.
    auto next = testTree();
    next[0].label = "Done", next[0].liveRegion = LiveRegionMode::Polite;
    next[1].checked = true;
    next[2].value = "héllo, wörld", next[2].focused = true;
    next[3].range->current = 5;
    next.erase(next.begin() + 5);  // the list's item
    next.push_back({.id = 8, .label = "New"});
    peer.heard.clear();
    bridge.publish(next, {.x = 100, .y = 50, .width = 400, .height = 300, .dpToPoints = 2, .focused = false, .title = "Main"});
    peer.serve(300);
    for (const char* line : {"PropertyChange accessible-name 0 0 1 Done", "Announcement  1 0 1 Done", "StateChanged checked 1 0 2",
                             "TextChanged insert 5 1 3 ,", "PropertyChange accessible-value 0 0 4 5", "ChildrenChanged remove 0 0 5 6",
                             "ChildrenChanged add 6 0 window 8", "Deactivate  0 0 window Main", "StateChanged active 0 0 window",
                             "StateChanged focused 1 0 3", "Focus  0 0 3"}) {
        CHECK(peer.heardLine(line));
        if (!peer.heardLine(line)) std::printf("    not heard: %s\n", line);
    }
    CHECK(peer.heard.size() == 11);
    // Typed away, and focus going elsewhere.
    auto after = next;
    after[2].value = "hé  wörld", after[2].focused = false;
    after[0].liveRegion = LiveRegionMode::Assertive, after[0].stateDescription = "now";
    after[1].enabled = false;
    after.push_back({.id = 9, .parent = 8, .label = "Inside"});  // a label with something in it: a panel
    peer.heard.clear();
    bridge.publish(after, {.x = 100, .y = 50, .width = 400, .height = 300, .dpToPoints = 2, .focused = false, .title = "Other"});
    peer.serve(300);
    for (const char* line : {"TextChanged delete 2 4 3 llo,", "TextChanged insert 2 1 3  ", "StateChanged focused 0 0 3",
                             "Announcement  2 0 1 Done, now", "PropertyChange accessible-description 0 0 1 now",
                             "PropertyChange accessible-name 0 0 window Other", "StateChanged enabled 0 0 2",
                             "StateChanged sensitive 0 0 2", "ChildrenChanged add 0 0 8 9"}) {
        CHECK(peer.heardLine(line));
        if (!peer.heardLine(line)) std::printf("    not heard: %s\n", line);
    }
    CHECK(peer.heard.size() == 9);
    CHECK(peer.role(node(8)) == 39 && peer.role(node(9)) == 29);
    CHECK(!peer.state(node(2), 8) && !peer.state(node(2), 24));
    // In the spaces after a word: that word's.
    CHECK(peer.text(node(3), "org.a11y.atspi.Text", "GetStringAtOffset", [](dbus::Writer& w) { w.i32(3), w.u32(1); }) == "hé   0 4");
}

// Without KOMPOZ_ATSPI it waits for the desktop's accessibility to be turned on (org.a11y.Status),
// then finds the accessibility bus as the launcher says.
void atspiBridgeFollowsTheDesktop() {
    TestBus bus;
    dbus::Lib d;
    if (!bus.pid || !d.load()) return;
    setenv("DBUS_SESSION_BUS_ADDRESS", bus.address.c_str(), 1);
    unsetenv("AT_SPI_BUS_ADDRESS");
    unsetenv("KOMPOZ_ATSPI");
    Peer launcher(d, bus.address);
    CHECK(launcher.c && launcher.own("org.a11y.Bus") && launcher.own("org.a11y.atspi.Registry"));
    if (!launcher.c) return;
    bool enabled = false;
    int asked = 0;
    launcher.answer = [&](dbus::Message* m) {
        std::string_view member = d.message_get_member(m);
        dbus::Message* reply = d.message_new_method_return(m);
        dbus::Writer w(d, reply);
        if (member == "Get") {
            dbus::Reader r(d, m);
            std::string interface = r.str(), name = r.str();
            ++asked;
            w.open(dbus::kTypeVariant, "b", [&](dbus::Writer& v) { v.boolean(enabled && name == "ScreenReaderEnabled"); });
        } else if (member == "GetAddress") {
            w.str(bus.address);
        } else if (member == "Embed") {
            w.open(dbus::kTypeStruct, nullptr, [&](dbus::Writer& s) {
                s.str("org.a11y.atspi.Registry");
                s.path("/org/a11y/atspi/accessible/root");
            });
        }
        d.connection_send(launcher.c, reply, nullptr);
        d.message_unref(reply);
        return true;
    };
    sdl::AtspiBridge bridge("Test app");
    CHECK(launcher.serve(1000, [&] { return asked >= 2; }));  // IsEnabled, ScreenReaderEnabled: both off
    launcher.serve(200);
    CHECK(!bridge.active());
    enabled = true;  // a screen reader started
    dbus::Message* changed = d.message_new_signal("/org/a11y/bus", "org.freedesktop.DBus.Properties", "PropertiesChanged");
    {
        dbus::Writer w(d, changed);
        w.str("org.a11y.Status");
        w.open(dbus::kTypeArray, "{sv}", [](dbus::Writer& a) {
            a.open(dbus::kTypeDictEntry, nullptr, [](dbus::Writer& e) {
                e.str("ScreenReaderEnabled");
                e.open(dbus::kTypeVariant, "b", [](dbus::Writer& v) { v.boolean(true); });
            });
        });
        w.open(dbus::kTypeArray, "s", [](dbus::Writer&) {});
    }
    d.connection_send(launcher.c, changed, nullptr);
    d.message_unref(changed);
    CHECK(launcher.serve(3000, [&] { return bridge.active(); }));
    SDL_PumpEvents();
    CHECK(SDL_PeepEvents(nullptr, 0, SDL_PEEKEVENT, bridge.eventType(), bridge.eventType()) > 0);  // to publish the tree
    CHECK(bridge.takeTreeRequest());

    // KOMPOZ_ATSPI=0: never.
    setenv("KOMPOZ_ATSPI", "0", 1);
    sdl::AtspiBridge off("Test app");
    CHECK(off.eventType() == 0 && !off.active());
    unsetenv("KOMPOZ_ATSPI");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);  // what failed seen before a crash
    SDL_Init(SDL_INIT_EVENTS);
    kdeConfigIsRead();
    wallpaperChangesAreFollowed();
    atspiBridgeServesTheTree();
    atspiBridgeFollowsTheDesktop();
    SDL_Quit();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
