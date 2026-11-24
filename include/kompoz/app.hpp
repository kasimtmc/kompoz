#pragma once

#include "runtime.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace kompoz {

struct Application {
    std::string title = "Kompoz";
    int width = 480, height = 800;  // initial window size in dp (desktop only)
    Content content;
    std::string fontPath, boldFontPath, mediumFontPath;  // optional; otherwise bundled/system fonts are used
    // Images AsyncImage downloaded are kept in the app's cache directory up to this size (the
    // least recently used go first), for the next time and the next run; 0: not kept. Not on
    // the web, where the browser caches.
    uint64_t imageDiskCacheBytes = uint64_t(64) << 20;
    // The user closing the app's window (its title bar's ×, Alt+F4): none, the app ends; else this,
    // which may hide the window instead (setAppWindowVisible(false), a Tray to show it again) or end
    // it (exitApplication). A signal (Ctrl+C, SIGTERM) ends the app either way. Desktops only.
    std::function<void()> onCloseRequest;
};

// Implemented by the application. The platform backend owns the real entry point
// (main on the desktops and the web, the activity's native methods on Android) and calls this once.
Application createApplication(int argc, char** argv);

} // namespace kompoz
