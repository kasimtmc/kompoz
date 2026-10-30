#pragma once

// Kompoz's platform on Windows (app/platform.hpp): its window a Win32 window, aware of each
// monitor's DPI (per-monitor v2), its title bar dark with the system's dark mode, drawn into with
// Vulkan (VK_KHR_win32_surface) or OpenGL ES (ANGLE's beside the program, else the driver's
// through WGL), its frames on the display's beat (the swap waits for it) and drawn on while the
// window is resized or moved; the mouse and its wheel, touch (WM_POINTER), the keyboard and the
// input method (IMM32), the clipboard, drops (OLE), the system's cursors, the screen kept on, the
// accent color and the wallpaper's colors, and screen readers (UI Automation).

namespace kompoz::platform {

// The app on Windows until it quits (what main returns), or -1 where it can't run there: the
// window class or the window not made.
int runWin32(int argc, char* argv[]);

} // namespace kompoz::platform
