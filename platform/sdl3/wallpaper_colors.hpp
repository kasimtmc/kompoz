#pragma once

// The wallpaper's own colors on desktops, the way Android 12 takes them from the wallpaper: the
// wallpaper found (Linux: KDE Plasma's, GNOME's and the desktops' that keep its settings, Cinnamon's,
// MATE's, Xfce's; Windows'), decoded small and its palette picked (TonalPalette::fromImage) on a
// thread of its own, and again whenever it changes. Each palette comes to the main loop as an SDL
// event. KOMPOZ_NO_WALLPAPER_COLORS=1 leaves it out (screenshots the same on every machine).

#include <kompoz/layout.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kompoz::sdl {

class WallpaperColors {
public:
    explicit WallpaperColors(bool darkTheme);  // starts looking (nothing comes where none is found)
    ~WallpaperColors();
    WallpaperColors(const WallpaperColors&) = delete;
    WallpaperColors& operator=(const WallpaperColors&) = delete;

    // The SDL event type sent when the palette is learned or changes (0: none will come).
    uint32_t eventType() const;
    // The latest palette; none: no wallpaper found (or it couldn't be read).
    std::optional<TonalPalette> palette() const;
    // Some wallpapers have a variant for the dark theme (KDE's packages, GNOME's picture-uri-dark).
    void setDarkTheme(bool dark);

    struct State;

private:
    std::shared_ptr<State> state_;
};

// Where the wallpaper is, as the desktop says, for the dark theme or the light one; empty if it
// doesn't say. (Exposed for the KDE parser's sake: config is the text of plasma's appletsrc,
// activity the current activity's id or empty, readDir lists a directory's files.)
std::string kdeWallpaperFromConfig(const std::string& config, const std::string& activity, bool dark,
                                   const std::function<std::vector<std::string>(const std::string&)>& readDir);
std::string findWallpaper(bool dark);

} // namespace kompoz::sdl
