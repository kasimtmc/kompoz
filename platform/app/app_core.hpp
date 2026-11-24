// A Kompoz app on a platform: the application made and composed (KompozHost), drawn by Kompoz's
// GPU renderer into a kept frame where only what changed is drawn again, shown in the platform's
// window. The same on every platform; the platform (Platform) gives it its window and events and
// says when frames are due.
#pragma once

#include "platform.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kompoz::platform {

struct AppState;  // (app_core.cpp's)

// What the platform's frame loop does after a frame.
struct Next {
    bool busy = false;    // the next frame right away (something animates, recomposes, or a script runs): don't wait for events
    int64_t wakeAt = -1;  // else a Wake then (a coroutine's delay), on the platform's clock; -1: none
    bool quit = false;    // the app is done
};

class AppCore {
public:
    explicit AppCore(Platform& platform);
    // A window of its own the app's composition asked for (Window), on a platform window of its own:
    // its request's content composed, its title and WindowState kept (syncWindow), the user closing
    // it its onCloseRequest. `appTitle`: the app's (its cache directory's name).
    AppCore(Platform& platform, std::shared_ptr<kompoz::detail::WindowRequest> window, std::string appTitle);
    ~AppCore();
    AppCore(const AppCore&) = delete;
    AppCore& operator=(const AppCore&) = delete;

    // The application made (createApplication), its window opened and the GPU renderer started
    // on the first backend that does; false if the app can't run (logged, and shown when not
    // scripted).
    bool start(int argc, char** argv);
    void handle(const Event& event);
    // When the next frame is due on the platform's clock, for a platform pacing its frames itself
    // (one a display beat, never a second for the beat of the last); -1: now. The time until then
    // is the host's for work between frames (idle).
    int64_t frameDue() const;
    void idle(int64_t budget);
    // A frame: a script's next step, composition, what changed drawn and shown. `displayTime`: the
    // frame's time, where the platform knows it (the vsync it starts on); else the core puts it on
    // the display's beat.
    Next frame(int64_t displayTime = -1);
    // The window's native surface went away or is another (between frames): what the GPU device
    // made for the old one let go of now (gpu::Device::surfaceChanged).
    void surfaceChanged();
    // Whether the app is done (its window closed with no onCloseRequest, a signal, exitApplication):
    // the platform's loop ends, whether or not a frame is due (its window may be hidden).
    bool quitting() const;
    // Before the platform goes: KOMPOZ_STATE_FILE written.
    void quit();
    KompozHost& host();
    const std::string& appTitle() const;
    // A window's: its title, size, minimized or not and toFront, as its request has them now, given to
    // the platform where they changed (between frames).
    void syncWindow();
    // A script's "@Title: command": the app's windows by title (AppWindows'), and a command for one.
    void setWindowFinder(std::function<AppCore*(std::string_view title)> find);
    void runScript(std::string command);
    std::string_view title() const;

private:
    std::unique_ptr<AppState> app_;
};

// The windows of their own the app's composition asks for (Window), where the platform opens more
// than one: each a platform window (made by `make`) with a core of its own, opened as its request
// comes (and is shown), kept up to date, closed as it goes; those its windows' compositions ask for
// too. Closed before the app's core goes (the platform's loop owns this after it).
class AppWindows {
public:
    using MakeWindow = std::function<std::unique_ptr<Platform>()>;
    AppWindows(AppCore& app, MakeWindow make);
    ~AppWindows();
    // Between frames: the requests as they are now (after the app's or a window's frame).
    void sync();
    struct Open {
        std::shared_ptr<kompoz::detail::WindowRequest> request;
        std::unique_ptr<Platform> platform;
        std::unique_ptr<AppCore> core;
    };
    std::vector<Open>& open() { return open_; }
    // The one whose platform window this is, or nullptr.
    AppCore* coreOf(const Platform* platform);

private:
    void close(size_t index);
    AppCore& app_;
    MakeWindow make_;
    std::vector<Open> open_;
    std::vector<const kompoz::detail::WindowRequest*> failed_;  // (not tried again while they last)
};

} // namespace kompoz::platform
