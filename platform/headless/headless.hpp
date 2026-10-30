#pragma once

// Kompoz's platform without a screen (app/platform.hpp), on the desktops: tests, scripted runs and
// screenshots, CI. KOMPOZ_HEADLESS=1 runs an app on it: its window a size and nothing more, the GPU
// renderer drawing only into its kept frame (platform/headless/headless_gpu.hpp), no input but a
// script's (KOMPOZ_SCRIPT), the loop and its timers its own.

namespace kompoz::platform {

bool headlessRequested();  // KOMPOZ_HEADLESS=1
int runHeadless(int argc, char* argv[]);  // the app, until it quits: what main returns

} // namespace kompoz::platform
