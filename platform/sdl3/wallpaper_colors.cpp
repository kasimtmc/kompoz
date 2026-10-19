#include "wallpaper_colors.hpp"

#include "image_loader.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace kompoz::sdl {

namespace {

namespace fs = std::filesystem;

// A path from UTF-8 (what the desktop's settings and the Windows API, converted, give).
fs::path pathOf(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A path from a file:// URI (or a path as it is), %XX decoded.
std::string pathFromUri(std::string_view s) {
    if (s.starts_with("file://")) s.remove_prefix(7);
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += char(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

#ifndef _WIN32
fs::path configHome() {
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return x;
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : "") / ".config";
}

// A command's first line of output, trimmed; empty if it couldn't run.
std::string commandLine(const std::string& command) {
    std::string out;
    if (FILE* p = popen((command + " 2>/dev/null").c_str(), "r")) {
        char buf[4096];
        while (fgets(buf, sizeof buf, p)) out += buf;
        pclose(p);
    }
    return out;
}

std::string gsetting(const char* schema, const char* key) {
    std::string v = trim(commandLine(std::string("gsettings get ") + schema + " " + key));
    if (v.size() >= 2 && v.front() == '\'' && v.back() == '\'') v = v.substr(1, v.size() - 2);
    return v.empty() ? v : pathFromUri(v);
}

std::string xfceWallpaper() {
    std::istringstream props(commandLine("xfconf-query -c xfce4-desktop -l"));
    for (std::string line; std::getline(props, line);)
        if (line.ends_with("/last-image")) return trim(commandLine("xfconf-query -c xfce4-desktop -p " + line));
    return {};
}
#endif

// A KDE wallpaper package's picture (/usr/share/wallpapers/Next/): the biggest in contents/images
// (images_dark for the dark theme, where it has one), by the WxH its name says, else by size.
std::string packageImage(const std::string& dir, bool dark,
                         const std::function<std::vector<std::string>(const std::string&)>& readDir) {
    for (const char* sub : dark ? std::vector{"/contents/images_dark", "/contents/images"} : std::vector{"/contents/images"}) {
        std::vector<std::string> files = readDir(dir + sub);
        std::string best;
        long long bestArea = -1;
        for (const std::string& f : files) {
            std::string name = fs::path(f).stem().string();
            long long w = 0, h = 0;
            char x = 0;
            std::istringstream in(name);
            long long area = (in >> w >> x >> h && x == 'x') ? w * h : 0;
            if (area > bestArea) bestArea = area, best = f;
        }
        if (!best.empty()) return dir + sub + "/" + best;
    }
    return {};
}

std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec)) out.push_back(e.path().filename().string());
    std::ranges::sort(out);
    return out;
}

} // namespace

std::string kdeWallpaperFromConfig(const std::string& config, const std::string& activity, bool dark,
                                   const std::function<std::vector<std::string>(const std::string&)>& readDir) {
    // [Containments][N] has the desktop's activity, screen, location and wallpaper plugin;
    // [Containments][N][Wallpaper][org.kde.image][General] its Image.
    struct Containment {
        std::map<std::string, std::string> keys;
        std::string image;
    };
    std::map<std::string, Containment> containments;
    Containment* section = nullptr;
    bool imageSection = false;
    std::istringstream in(config);
    for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (line.starts_with('[')) {
            section = nullptr;
            imageSection = false;
            if (!line.starts_with("[Containments][")) continue;
            size_t close = line.find(']', 15);
            if (close == std::string::npos) continue;
            std::string id = line.substr(15, close - 15), rest = line.substr(close + 1);
            if (rest.empty()) section = &containments[id];
            else if (rest == "[Wallpaper][org.kde.image][General]") section = &containments[id], imageSection = true;
            continue;
        }
        size_t eq = line.find('=');
        if (!section || eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (imageSection) {
            if (key == "Image") section->image = value;
        } else {
            section->keys[key] = value;
        }
    }
    // The desktop (not a panel) of the first screen, in the current activity, showing a picture.
    std::string best;
    int bestScore = -1;
    for (auto& [id, c] : containments) {
        if (c.image.empty() || c.keys["wallpaperplugin"] != "org.kde.image") continue;
        if (!c.keys["location"].empty() && c.keys["location"] != "0") continue;  // a panel's
        int score = (c.keys["lastScreen"] == "0" ? 2 : 0) + (!activity.empty() && c.keys["activityId"] == activity ? 1 : 0);
        if (score > bestScore) bestScore = score, best = c.image;
    }
    if (best.empty()) return {};
    std::string path = pathFromUri(best);
    if (!path.empty() && path.back() == '/') path.pop_back();
    std::string image = packageImage(path, dark, readDir);
    return image.empty() ? path : image;
}

std::string findWallpaper(bool dark) {
#ifdef _WIN32
    (void)dark;
    wchar_t buf[MAX_PATH] = {};
    if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, buf, 0) || !buf[0]) return {};
    std::u8string u8 = fs::path(buf).u8string();
    return std::string(u8.begin(), u8.end());
#else
    std::string desktop = lower(std::getenv("XDG_CURRENT_DESKTOP") ? std::getenv("XDG_CURRENT_DESKTOP") : "");
    auto kde = [&]() -> std::string {
        std::string config = readText(configHome() / "plasma-org.kde.plasma.desktop-appletsrc");
        if (config.empty()) return {};
        std::string activity;
        std::istringstream act(readText(configHome() / "kactivitymanagerdrc"));
        for (std::string line; std::getline(act, line);)
            if (line.starts_with("currentActivity=")) activity = trim(line.substr(16));
        std::string image = kdeWallpaperFromConfig(config, activity, dark, listDir);
        if (image.empty() && fs::is_directory("/usr/share/wallpapers/Next"))  // Plasma's default, never changed
            image = packageImage("/usr/share/wallpapers/Next", dark, listDir);
        return image;
    };
    auto gnome = [&] {
        std::string image = dark ? gsetting("org.gnome.desktop.background", "picture-uri-dark") : std::string();
        return image.empty() ? gsetting("org.gnome.desktop.background", "picture-uri") : image;
    };
    if (desktop.find("kde") != std::string::npos) return kde();
    if (desktop.find("cinnamon") != std::string::npos) return gsetting("org.cinnamon.desktop.background", "picture-uri");
    if (desktop.find("mate") != std::string::npos) return gsetting("org.mate.background", "picture-filename");
    if (desktop.find("xfce") != std::string::npos) return xfceWallpaper();
    if (desktop.find("gnome") != std::string::npos || desktop.find("unity") != std::string::npos ||
        desktop.find("budgie") != std::string::npos || desktop.find("pantheon") != std::string::npos)
        return gnome();
    return {};
#endif
}

struct WallpaperColors::State {
    uint32_t eventType = 0;
    std::atomic<bool> running{true}, dark{false};
    std::mutex mutex;
    std::condition_variable wake;
    bool woken = false;
    std::optional<TonalPalette> palette;
    std::thread thread;
#ifndef _WIN32
    int inotify = -1;
#endif

    void tell(std::optional<TonalPalette> value) {
        {
            std::lock_guard lock(mutex);
            if (palette == value) return;
            palette = std::move(value);
        }
        SDL_Event e{};
        e.type = eventType;
        SDL_PushEvent(&e);
    }

    // The wallpaper's palette, if it is a picture that can be read.
    static std::optional<TonalPalette> paletteOf(const std::string& path) {
        if (path.empty()) return std::nullopt;
        std::ifstream in(pathOf(path), std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.empty()) return std::nullopt;
        // As big as a screen, as the wallpaper Android takes its colors from is (decoders scale down as they go).
        ImageBitmap image = decodeImage(bytes, {1920, 1080, true});
        if (image.empty()) return std::nullopt;
        return TonalPalette::fromImage(image);
    }

#ifndef _WIN32
    // Whether the files read were among those just written (other files in ~/.config are not).
    bool settingsWritten() {
        bool ours = false;
        alignas(inotify_event) char buf[4096];
        for (ssize_t n; (n = read(inotify, buf, sizeof buf)) > 0;)
            for (ssize_t at = 0; at < n;) {
                auto* e = reinterpret_cast<const inotify_event*>(buf + at);
                std::string_view name = e->len ? std::string_view(e->name) : std::string_view();
                if (name == "plasma-org.kde.plasma.desktop-appletsrc" || name == "kactivitymanagerdrc" || name == "user" ||
                    name == "xfce4-desktop.xml")
                    ours = true;
                at += ssize_t(sizeof(inotify_event) + e->len);
            }
        return ours;
    }
#endif

    // Waits for a change: a settings file written (Linux), a theme change, or a while (the
    // picture may be replaced in place, as Windows' TranscodedWallpaper is).
    void waitForChange() {
#ifndef _WIN32
        if (inotify >= 0) {
            pollfd fd{inotify, POLLIN, 0};
            for (int waited = 0; running && waited < 30000; waited += 250) {
                if (poll(&fd, 1, 250) > 0 && settingsWritten()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // the rest of the change
                    settingsWritten();
                    return;
                }
                std::lock_guard lock(mutex);
                if (std::exchange(woken, false)) return;
            }
            return;
        }
#endif
        std::unique_lock lock(mutex);
        wake.wait_for(lock, std::chrono::seconds(3), [this] { return woken || !running; });
        woken = false;
    }

    void run() {
        std::string last;
        while (running) {
            std::string path = findWallpaper(dark);
            std::error_code ec;
            auto stamp = path.empty() ? fs::file_time_type() : fs::last_write_time(pathOf(path), ec);
            std::string key = path + '|' + std::to_string(stamp.time_since_epoch().count());
            if (key != last) {
                last = key;
                tell(paletteOf(path));
            }
            waitForChange();
        }
    }
};

WallpaperColors::WallpaperColors(bool darkTheme) : state_(std::make_shared<State>()) {
    State& s = *state_;
    if (SDL_getenv("KOMPOZ_NO_WALLPAPER_COLORS")) return;
    s.dark = darkTheme;
    s.eventType = SDL_RegisterEvents(1);
    if (s.eventType == 0) return;
#ifndef _WIN32
    s.inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (s.inotify >= 0) {
        fs::path config = configHome();
        for (const fs::path& dir : {config, config / "dconf", config / "xfce4" / "xfconf" / "xfce-perchannel-xml"})
            inotify_add_watch(s.inotify, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    }
#endif
    s.thread = std::thread([&s] {
        s.run();
        SDL_CleanupTLS();  // what SDL keeps for a thread it didn't start
    });
}

WallpaperColors::~WallpaperColors() {
    State& s = *state_;
    {
        std::lock_guard lock(s.mutex);
        s.running = false;
        s.woken = true;
    }
    s.wake.notify_all();
    if (s.thread.joinable()) s.thread.join();
#ifndef _WIN32
    if (s.inotify >= 0) close(s.inotify);
#endif
}

uint32_t WallpaperColors::eventType() const { return state_->eventType; }

std::optional<TonalPalette> WallpaperColors::palette() const {
    std::lock_guard lock(state_->mutex);
    return state_->palette;
}

void WallpaperColors::setDarkTheme(bool dark) {
    State& s = *state_;
    if (s.dark.exchange(dark) == dark) return;
    {
        std::lock_guard lock(s.mutex);
        s.woken = true;
    }
    s.wake.notify_all();
}

} // namespace kompoz::sdl
