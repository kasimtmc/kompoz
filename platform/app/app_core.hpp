// A Kompoz app on a platform: the application made and composed (ComposeHost), drawn by Kompoz's
// GPU renderer into a kept frame where only what changed is drawn again, shown in the platform's
// window. The same on every platform; the platform (Platform) gives it its window and events and
// says when frames are due.
#pragma once

#include "platform.hpp"

#include <cstdint>
#include <memory>

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
    // A frame: a script's next step, composition, what changed drawn and shown. `displayTime`: when
    // it will be shown, where the platform knows (a vsync's time); else the core puts it on the
    // display's beat.
    Next frame(int64_t displayTime = -1);
    // Before the platform goes: KOMPOZ_STATE_FILE written.
    void quit();
    ComposeHost& host();

private:
    std::unique_ptr<AppState> app_;
};

} // namespace kompoz::platform
