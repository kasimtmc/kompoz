// What a Kompoz app asks of the platform it runs on, and what the platform asks of the app core
// (AppCore). Each platform has its own: its window (one), its events turned into platform::Event,
// its frames on the display's beat, and what it adds to ComposeHost (text input, the clipboard,
// cursors, a phone's insets and back gestures, screen readers).
#pragma once

#include "../gpu/device.hpp"
#include "platform_event.hpp"

#include <kompoz/app.hpp>
#include <kompoz/host.hpp>
#include <kompoz/media_query.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kompoz::platform {

enum class WindowButton { Minimize, Maximize, Close };

// The title bar Kompoz draws above the app where the platform leaves the window undecorated
// (Wayland without the compositor's decorations, as on GNOME): its buttons on each side, in
// order, those the window manager has (the desktop's layout: GNOME's has only Close).
struct TitleBar {
    std::vector<WindowButton> start, end;
    bool maximized = false;
    bool operator==(const TitleBar&) const = default;
};

// What the title bar asks of the window.
enum class WindowAction {
    Move,            // dragged by the pointer or finger last pressed on it
    Minimize, ToggleMaximize, Close,
    Menu,            // the window manager's menu, at (x, y)
};

// The window's measures, as the app lays out and draws by them.
struct Metrics {
    int pixelWidth = 1, pixelHeight = 1;  // the drawable's
    int pointWidth = 1, pointHeight = 1;  // the window's, in the points its events' positions are in
    float scale = 1;                      // px per dp
    float pointToDp = 1;                  // a point in dp
    float fontScale = 1;                  // the system's text size
    float refreshRate = 60;               // the display's, a second
    // Where the system leaves the window's content (a desktop's or an iPhone's safe area), in
    // points; none where the insets come otherwise (Android's, by ComposeHost::setSystemInsets).
    std::optional<Rect> safeArea;
    bool darkTheme = false;  // the system's
    std::string language;    // the preferred one ("tr", "en")
    std::optional<TitleBar> titleBar;  // drawn by Kompoz at the top of the window; none: the system's, or none at all
};

class Platform {
public:
    virtual ~Platform() = default;

    // A setting from outside the app (KOMPOZ_PROFILE, KOMPOZ_GPU): an environment variable, or
    // where an app has none (Android) a file named as it in lower case in the app's files
    // ("kompoz-profile"), what it holds. None: not set.
    virtual std::optional<std::string> option(std::string_view name) const = 0;
    virtual void log(const std::string& message) const = 0;
    virtual int64_t now() const = 0;  // ns, on the clock the events' times are on
    // The GPU backends KOMPOZ_GPU=auto tries here, the best first.
    virtual std::vector<gpu::Backend> backends() const = 0;
    // The window, made to be drawn into with `backend` (what the device needs of it into
    // `surface`); false (and `error`) if it can't be. Once only, or after closeWindow.
    virtual bool openWindow(const Application& app, gpu::Backend backend, gpu::Surface& surface, std::string* error) = 0;
    virtual void closeWindow() = 0;               // (after the device drawing into it is gone)
    virtual void showError(const std::string& message) = 0;  // what ended the app, to the user
    virtual Metrics metrics() const = 0;
    virtual void pixelSize(int* width, int* height) const = 0;  // the drawable's, as metrics() has it (cheaper: each frame)
    virtual void setWindowSize(int width, int height) = 0;  // in points
    // What Kompoz's title bar asks (Metrics::titleBar); (x, y): where, in points.
    virtual void windowAction(WindowAction action, float x, float y) { (void)action, (void)x, (void)y; }
    // The pointer and the keyboard; with `probe` (after the first frame: what tells may take a
    // few tens of milliseconds to start) a microphone and a camera looked for too, kept after.
    virtual MediaInfo media(bool probe) = 0;
    // The host made: what the platform gives it (text input, the clipboard, cursors, links,
    // haptics; on a phone its keyboard, insets, back gestures, autofill) into `callbacks`, and
    // its own handlers of what the system tells (screen readers' requests, the accent color).
    virtual void attach(ComposeHost& host, ComposeHost::Callbacks& callbacks) = 0;
    // The host given its callbacks (and a state file's state): what the platform starts on it
    // (a phone's saved state back, its keyboard's and insets' handlers, a deep link it was given).
    virtual void started(ComposeHost& host) { (void)host; }
    // The metrics read again: what the platform sets on the host besides (a phone's palettes).
    virtual void metricsUpdated(ComposeHost& host) { (void)host; }
    // An event the core handled: what the platform does after it (Android keeps the saved state
    // as it goes to the background).
    virtual void handled(ComposeHost& host, const Event& event) { (void)host, (void)event; }
    // After a frame's composition: what the platform takes from the host (the semantics tree for
    // screen readers, the back callback, the frame rate, the screen kept on).
    virtual void composed(ComposeHost& host, bool redraw, int64_t frameTime) = 0;
    // A frame more wanted for the platform's own sake (a semantics tree still to publish).
    virtual bool wantsFrames() const { return false; }
    // A frame's own work (its waits left out), for a system that wants it (Android's ADPF).
    virtual void frameWork(int64_t ns) { (void)ns; }
    // Back went unused: the system's own (Android leaves the app).
    virtual void leaveApp() {}
};

} // namespace kompoz::platform
