#include "desktop_services.hpp"

#include "../common/desktop_accent.hpp"
#include "../common/system.hpp"
#include "atspi_bridge.hpp"
#include "libdbus.hpp"
#ifdef KOMPOZ_WALLPAPER_COLORS
#include "../common/wallpaper_colors.hpp"
#endif

#include <xkbcommon/xkbcommon-keysyms.h>

#include <dlfcn.h>
#include <csignal>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>

#include <cctype>
#include <ctime>
#include <thread>

extern char** environ;

namespace kompoz::platform {

namespace {
volatile std::sig_atomic_t gQuitSignalled = 0;
int gSignalWakeFd = -1;
} // namespace

Key keyOf(uint32_t sym) {
    if (sym >= 'a' && sym <= 'z') return Key(int(Key::A) + int(sym - 'a'));
    if (sym >= 'A' && sym <= 'Z') return Key(int(Key::A) + int(sym - 'A'));
    if (sym >= '0' && sym <= '9') return Key(int(Key::Zero) + int(sym - '0'));
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) return Key(int(Key::F1) + int(sym - XKB_KEY_F1));
    switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter: return Key::Enter;
    case XKB_KEY_Escape: return Key::Escape;
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab: return Key::Tab;
    case XKB_KEY_BackSpace: return Key::Backspace;
    case XKB_KEY_Delete: return Key::Delete;
    case XKB_KEY_space: return Key::Space;
    case XKB_KEY_Left: return Key::Left;
    case XKB_KEY_Right: return Key::Right;
    case XKB_KEY_Up: return Key::Up;
    case XKB_KEY_Down: return Key::Down;
    case XKB_KEY_Home: return Key::Home;
    case XKB_KEY_End: return Key::End;
    case XKB_KEY_Page_Up: return Key::PageUp;
    case XKB_KEY_Page_Down: return Key::PageDown;
    default: return Key::Unknown;
    }
}

void openUri(const std::string& uri) {
    pid_t pid = 0;
    const char* argv[] = {"xdg-open", uri.c_str(), nullptr};
    if (posix_spawnp(&pid, "xdg-open", nullptr, nullptr, const_cast<char* const*>(argv), environ) != 0) {
        log("kompoz: can't open " + uri);
        return;
    }
    std::thread([pid] { waitpid(pid, nullptr, 0); }).detach();
}

int64_t monotonicNow() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return int64_t(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
}

std::vector<std::string> filesInUriList(const std::string& list) {
    std::vector<std::string> files;
    size_t start = 0;
    while (start < list.size()) {
        size_t end = list.find('\n', start);
        std::string line = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? list.size() : end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.starts_with("file://")) continue;
        line.erase(0, 7);
        if (size_t slash = line.find('/'); slash != 0) line.erase(0, slash);  // (a host before the path)
        std::string path;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '%' && i + 2 < line.size() && std::isxdigit(uint8_t(line[i + 1])) && std::isxdigit(uint8_t(line[i + 2]))) {
                path += char(std::stoi(line.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else {
                path += line[i];
            }
        }
        if (!path.empty()) files.push_back(path);
    }
    return files;
}

void quitOnSignals(int wakeFd) {
    gSignalWakeFd = wakeFd;
    struct sigaction quit{};
    quit.sa_handler = [](int) {
        gQuitSignalled = 1;
        uint64_t one = 1;
        if (gSignalWakeFd >= 0) (void)!write(gSignalWakeFd, &one, sizeof one);
    };
    sigemptyset(&quit.sa_mask);
    quit.sa_flags = SA_RESTART;
    sigaction(SIGINT, &quit, nullptr);
    sigaction(SIGTERM, &quit, nullptr);
}

int withDbus(const std::function<int()>& run) {
    void* dbus = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    int result = run();
    if (dbus) {
        if (auto shutdown = reinterpret_cast<void (*)()>(dlsym(dbus, "dbus_shutdown")); shutdown && result >= 0) shutdown();
        dlclose(dbus);
    }
    return result;
}

DesktopServices::DesktopServices() = default;
DesktopServices::~DesktopServices() = default;

void DesktopServices::start(const std::string& title, std::function<void()> wake) {
    auto notifier = [wake](std::atomic<bool>& flag) {
        return Notifier{.notify = [wake, &flag] {
            flag = true;
            wake();
        }};
    };
    accent_ = std::make_unique<DesktopAccent>(notifier(accentChanged_));
#ifdef KOMPOZ_WALLPAPER_COLORS
    wallpaper_ = std::make_unique<WallpaperColors>(false, notifier(wallpaperChanged_));
#endif
    a11y_ = std::make_unique<AtspiBridge>(title, notifier(a11yRequested_));
}

void DesktopServices::takeNotifications(std::deque<Event>& events) {
    if (accentChanged_.exchange(false)) {
        events.push_back({.type = EventType::AccentChanged});
        // (The same reader tells the light/dark preference and the title bar's buttons.)
        if (accent_ && (accent_->darkScheme() != dark_ || accent_->buttonLayout() != buttonLayout_)) {
            dark_ = accent_->darkScheme(), buttonLayout_ = accent_->buttonLayout();
            events.push_back({.type = EventType::MetricsChanged});
        }
    }
    if (wallpaperChanged_.exchange(false)) events.push_back({.type = EventType::WallpaperChanged});
    if (a11yRequested_.exchange(false)) events.push_back({.type = EventType::AccessibilityRequest});
    if (gQuitSignalled) {
        gQuitSignalled = 0;
        events.push_back({.type = EventType::Quit});
    }
}

bool DesktopServices::darkTheme() const { return accent_ && accent_->darkScheme().value_or(false); }

std::optional<std::string> DesktopServices::buttonLayout() const { return accent_ ? accent_->buttonLayout() : std::nullopt; }

void DesktopServices::metricsUpdated() {
#ifdef KOMPOZ_WALLPAPER_COLORS
    if (wallpaper_) wallpaper_->setDarkTheme(darkTheme());
#endif
}

void DesktopServices::handled(ComposeHost& host, const Event& e) {
    switch (e.type) {
    case EventType::AccentChanged:
        if (accent_) {
            std::optional<Color> color = accent_->accent();
            host.setDynamicTonalPalette(color ? std::optional(TonalPalette::fromSeed(*color)) : std::nullopt);
        }
        break;
#ifdef KOMPOZ_WALLPAPER_COLORS
    case EventType::WallpaperChanged:
        if (wallpaper_) host.setWallpaperTonalPalette(wallpaper_->palette());
        break;
#endif
    case EventType::AccessibilityRequest:
        if (a11y_)
            for (const auto& r : a11y_->takeRequests()) {
                if (r.action == SemanticsAction::SetText) host.performSemanticsAction(r.id, r.action, r.text);
                else host.performSemanticsAction(r.id, r.action, r.argument);
            }
        break;
    case EventType::FocusGained:
    case EventType::FocusLost:
        focused_ = e.type == EventType::FocusGained;
        semanticsDirty_ = true;
        break;
    default: break;
    }
}

void DesktopServices::composed(ComposeHost& host, bool redraw, int64_t frameTime, const Rect& window, float dpToPoints,
                               const std::string& title) {
    // Screen readers (AT-SPI) answer from the tree last published: once asked for, after frames
    // that changed the screen.
    bool treeRequested = a11y_ && a11y_->takeTreeRequest();
    semanticsDirty_ = semanticsDirty_ || redraw || treeRequested;
    if (a11y_ && a11y_->active() && semanticsDirty_ &&
        (treeRequested || frameTime - lastSemantics_ > (host.hasPendingWork() ? 500'000'000 : 50'000'000))) {
        lastSemantics_ = frameTime;
        semanticsDirty_ = false;
        a11y_->publish(host.semanticsTree(), {.x = window.x, .y = window.y, .width = window.width, .height = window.height,
                                              .dpToPoints = dpToPoints, .focused = focused_, .title = title});
    }
}

void DesktopServices::stop() {
    keepScreenOn(false, {});
    a11y_.reset();
    wallpaper_.reset();
    accent_.reset();
}

void DesktopServices::keepScreenOn(bool on, const std::string& app) {
    static dbus::Lib bus;
    static const bool loaded = bus.load();
    if (!loaded || on == (screenSaverBus_ != nullptr)) return;
    if (!on) {  // (closing the connection lets the screen saver go)
        bus.connection_close(screenSaverBus_);
        bus.connection_unref(std::exchange(screenSaverBus_, nullptr));
        return;
    }
    dbus::Error error;
    bus.error_init(&error);
    dbus::Connection* connection = bus.bus_get_private(dbus::kSessionBus, &error);
    bus.error_free(&error);
    if (!connection) return;
    bus.connection_set_exit_on_disconnect(connection, 0);
    dbus::Message* call =
        bus.message_new_method_call("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", "Inhibit");
    const char* name = app.c_str();
    const char* reason = "keepScreenOn";
    dbus::Message* reply = nullptr;
    if (call) {
        bus.message_append_args(call, dbus::kTypeString, &name, dbus::kTypeString, &reason, dbus::kTypeInvalid);
        bus.error_init(&error);
        reply = bus.connection_send_with_reply_and_block(connection, call, 1000, &error);
        bus.error_free(&error);
        bus.message_unref(call);
    }
    if (!reply) {
        bus.connection_close(connection);
        bus.connection_unref(connection);
        return;
    }
    bus.message_unref(reply);
    screenSaverBus_ = connection;
}

bool DesktopServices::wantsFrames() const { return a11y_ && a11y_->active() && semanticsDirty_; }  // one more frame publishes it

} // namespace kompoz::platform
