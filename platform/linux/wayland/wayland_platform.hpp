#pragma once

// Kompoz's platform on Wayland (app/platform.hpp): its window an xdg_toplevel the compositor
// decorates (xdg-decoration), else Kompoz (as on GNOME, or with KOMPOZ_DECORATIONS=kompoz: the
// title bar the app core draws, platform/app/title_bar.hpp, and around the window shadows to
// resize it by, subsurfaces of its own), drawn into with Vulkan (VK_KHR_wayland_surface) or
// OpenGL ES (a wl_egl_window), at the compositor's scale (fractional-scale and viewporter, else
// an integer buffer scale), its frames on the compositor's frame callbacks; the pointer, touch,
// touchpad pinches (pointer-gestures) and the keyboard (xkbcommon, keys repeated as the
// compositor says) from the seat, typing through text-input-v3 too, the clipboard and drops
// through the data device, the cursor's shape through cursor-shape, the screen kept on through
// idle-inhibit. libwayland-client, libwayland-egl and libxkbcommon are loaded at run time
// (wayland_dynamic.hpp).

namespace kompoz::platform {

// The app on Wayland until it quits (what main returns), or -1 where it can't run there: no
// Wayland display, its libraries or a global it needs missing, the window not made.
// KOMPOZ_PLATFORM=x11 keeps to X11's (XWayland's).
int runWayland(int argc, char* argv[]);

} // namespace kompoz::platform
