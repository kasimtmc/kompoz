#pragma once

// What Kompoz's own platforms on a Linux desktop (Wayland, X11) take from the desktop alike: its
// keys' symbols as Kompoz's keys, links opened with xdg-open, and its services, each on a thread
// of its own and told to the platform's loop through a wake: the accent color, the light or dark
// preference and GNOME's window buttons (the settings portal, desktop_accent.hpp), the
// wallpaper's colors (wallpaper_colors.hpp), the screen readers (AT-SPI, atspi_bridge.hpp).

#include "../app/platform.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kompoz::platform {

class DesktopAccent;
class WallpaperColors;
class AtspiBridge;

// An X11 keysym (xkbcommon's are the same) as Kompoz's key.
Key keyOf(uint32_t keysym);
// The browser, or the app for the scheme (xdg-open), its process waited for on a thread.
void openUri(const std::string& uri);
int64_t monotonicNow();  // ns
// The local files a text/uri-list names (file:// addresses, percent-decoded; comments and other
// schemes left out): what a drop of files holds.
std::vector<std::string> filesInUriList(const std::string& list);
// `run` (a platform's app, what main returns, -1: it couldn't run) with libdbus kept loaded
// until what it keeps for the process is freed after it, its users all done, as SDL_Quit does.
int withDbus(const std::function<int()>& run);

class DesktopServices {
public:
    DesktopServices();
    ~DesktopServices();
    DesktopServices(const DesktopServices&) = delete;
    DesktopServices& operator=(const DesktopServices&) = delete;

    // Started as the host is made (Platform::attach); `wake` wakes the platform's loop.
    void start(const std::string& title, std::function<void()> wake);
    // What they told since, as events: AccentChanged, WallpaperChanged, AccessibilityRequest, and
    // MetricsChanged when the light or dark preference or the window buttons changed.
    void takeNotifications(std::deque<Event>& events);
    bool darkTheme() const;
    std::optional<std::string> buttonLayout() const;  // GNOME's (DesktopAccent::buttonLayout)
    // Platform's hooks, as the platform passes them on.
    void metricsUpdated();
    void handled(ComposeHost& host, const Event& event);
    // After a frame's composition: the semantics tree published to the screen readers, as they
    // ask for it, `window` where the window is (its points) and `title` its title.
    void composed(ComposeHost& host, bool redraw, int64_t frameTime, const Rect& window, float dpToPoints, const std::string& title);
    bool wantsFrames() const;
    // Their threads ended, before the loop they wake goes.
    void stop();
    // The screen saver held off (org.freedesktop.ScreenSaver's Inhibit, for as long as the
    // connection that asked is open), for the app named `app`; where the window system can't.
    void keepScreenOn(bool on, const std::string& app);

private:
    std::atomic<bool> accentChanged_{false}, wallpaperChanged_{false}, a11yRequested_{false};
    std::unique_ptr<DesktopAccent> accent_;
    std::optional<bool> dark_;
    std::optional<std::string> buttonLayout_;
    std::unique_ptr<WallpaperColors> wallpaper_;
    std::unique_ptr<AtspiBridge> a11y_;
    bool focused_ = false, semanticsDirty_ = true;
    void* screenSaverBus_ = nullptr;  // the D-Bus connection holding the screen saver off
    int64_t lastSemantics_ = 0;
};

} // namespace kompoz::platform
