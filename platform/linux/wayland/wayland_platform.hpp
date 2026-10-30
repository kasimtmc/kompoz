#pragma once

// Kompoz's platform on Wayland (app/platform.hpp): its window an xdg_toplevel the compositor
// decorates (xdg-decoration), drawn into with Vulkan (VK_KHR_wayland_surface) or OpenGL ES (a
// wl_egl_window), at the compositor's scale (fractional-scale and viewporter, else an integer
// buffer scale), its frames on the compositor's frame callbacks; the pointer, touch and the
// keyboard (xkbcommon, keys repeated as the compositor says) from the seat, typing through
// text-input-v3 too, the clipboard through the data device, the cursor's shape through
// cursor-shape, the screen kept on through idle-inhibit. libwayland-client, libwayland-egl and
// libxkbcommon are loaded at run time (wayland_dynamic.hpp).

namespace kompoz::platform {

// The app on Wayland until it quits (what main returns), or -1 where it can't run there: no
// Wayland display, its libraries or a global it needs missing (a compositor that decorates no
// windows, until Kompoz draws its own title bar), the window not made. KOMPOZ_PLATFORM=sdl keeps
// to SDL's platform.
int runWayland(int argc, char* argv[]);

} // namespace kompoz::platform
