#pragma once

#include "runtime.hpp"

#include <cstdint>
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
};

// Implemented by the application. The platform backend owns the real entry point
// (main, SDL_main on Android/iOS, the browser loop on the web) and calls this once.
Application createApplication(int argc, char** argv);

} // namespace kompoz
