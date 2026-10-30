#pragma once

// Kompoz's platform on X11 (app/platform.hpp): its window an X window the window manager
// decorates, drawn into with Vulkan (VK_KHR_xlib_surface) or OpenGL ES (EGL's X11 platform), at
// the desktop's scale (Xft.dpi), its frames on the display's beat (the swap waits for it); the
// pointer, its wheel (smooth, as XInput 2 tells it), touch and touchpad pinches through XInput 2,
// the keyboard with an input method (XIM), the clipboard and drops as selections (ICCCM, XDND),
// the cursors of the desktop's theme (libXcursor), the refresh rate from XRandR. libX11, libXi,
// libXcursor and libXrandr are loaded at run time (x11_dynamic.hpp).

namespace kompoz::platform {

// The app on X11 until it quits (what main returns), or -1 where it can't run there: no X
// display, its libraries or XInput 2.2 missing, the window not made.
int runX11(int argc, char* argv[]);

} // namespace kompoz::platform
