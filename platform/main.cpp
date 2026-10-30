// The program's entry point on the desktops and the web: the app on the platform it runs on.
// KOMPOZ_HEADLESS=1 runs it without a window (headless.hpp); else a Wayland session's platform,
// then X11's (Linux, BSD); Windows', macOS's or the page's. (Android's entry points are its
// activity's native methods, android_platform.cpp.)

#include "common/system.hpp"
#ifdef KOMPOZ_HEADLESS
#include "headless/headless.hpp"
#endif
#ifdef KOMPOZ_WAYLAND
#include "linux/wayland/wayland_platform.hpp"
#endif
#ifdef KOMPOZ_X11
#include "linux/x11/x11_platform.hpp"
#endif
#ifdef KOMPOZ_WIN32
#include "windows/win32_platform.hpp"
#endif
#ifdef KOMPOZ_MACOS
#include "macos/macos_platform.hpp"
#endif
#ifdef KOMPOZ_WEB
#include "web/web_platform.hpp"
#endif

int main(int argc, char* argv[]) {
    using namespace kompoz::platform;
#ifdef KOMPOZ_HEADLESS
    if (headlessRequested()) return runHeadless(argc, argv);
#endif
    int result = -1;  // (-1: the platform can't run here, the next is tried)
#ifdef KOMPOZ_WAYLAND
    if (result < 0) result = runWayland(argc, argv);
#endif
#ifdef KOMPOZ_X11
    if (result < 0) result = runX11(argc, argv);
#endif
#ifdef KOMPOZ_WIN32
    if (result < 0) result = runWin32(argc, argv);
#endif
#ifdef KOMPOZ_MACOS
    if (result < 0) result = runMacOS(argc, argv);
#endif
#ifdef KOMPOZ_WEB
    if (result < 0) result = runWeb(argc, argv);  // (it goes on in the page after main returns)
#endif
    if (result < 0) log("kompoz: no display to run on (a Wayland session, an X11 display, or KOMPOZ_HEADLESS=1)");
    return result < 0 ? 1 : result;
}
