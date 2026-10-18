// Tests of the platform code that runs without a window: the wallpaper's colors (Linux). They read
// a KDE config of their own, in a scratch XDG_CONFIG_HOME, and change it as KConfig does.

#include "wallpaper_colors.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

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

} // namespace

int main() {
    SDL_Init(SDL_INIT_EVENTS);
    kdeConfigIsRead();
    wallpaperChangesAreFollowed();
    SDL_Quit();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
