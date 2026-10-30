#pragma once

// libwayland-client, libwayland-egl and libxkbcommon, loaded at run time (where they aren't, as on
// an X11 desktop, the app runs on another platform): each function through a pointer that its own
// name stands for, as SDL does it, so that the generated protocol headers' inline functions call
// them too. Include this, not wayland-client-core.h, before the protocol headers (generated/).

#include <wayland-client-core.h>
#include <wayland-egl-core.h>
#include <xkbcommon/xkbcommon.h>

namespace kompoz::platform::wayland {

#define KOMPOZ_WAYLAND_CLIENT_FUNCTIONS(X)                                                                                \
    X(wl_display_connect) X(wl_display_disconnect) X(wl_display_get_fd) X(wl_display_dispatch_pending)                    \
    X(wl_display_roundtrip) X(wl_display_flush) X(wl_display_prepare_read) X(wl_display_read_events)                       \
    X(wl_display_cancel_read) X(wl_display_get_error) X(wl_proxy_marshal_flags) X(wl_proxy_add_listener)                  \
    X(wl_proxy_destroy) X(wl_proxy_get_version) X(wl_proxy_set_user_data) X(wl_proxy_get_user_data)
#define KOMPOZ_WAYLAND_EGL_FUNCTIONS(X) X(wl_egl_window_create) X(wl_egl_window_resize) X(wl_egl_window_destroy)
#define KOMPOZ_XKB_FUNCTIONS(X)                                                                                           \
    X(xkb_context_new) X(xkb_context_unref) X(xkb_keymap_new_from_string) X(xkb_keymap_unref) X(xkb_keymap_key_repeats)   \
    X(xkb_state_new) X(xkb_state_unref) X(xkb_state_update_mask) X(xkb_state_key_get_one_sym) X(xkb_state_key_get_utf8)   \
    X(xkb_state_mod_name_is_active)

// The functions, by their real declarations' types.
struct Functions {
#define KOMPOZ_WAYLAND_POINTER(name) decltype(&::name) p_##name = nullptr;
    KOMPOZ_WAYLAND_CLIENT_FUNCTIONS(KOMPOZ_WAYLAND_POINTER)
    KOMPOZ_WAYLAND_EGL_FUNCTIONS(KOMPOZ_WAYLAND_POINTER)
    KOMPOZ_XKB_FUNCTIONS(KOMPOZ_WAYLAND_POINTER)
#undef KOMPOZ_WAYLAND_POINTER
    bool egl = false;  // libwayland-egl's (OpenGL ES on Wayland)
};
extern Functions fn;

// The libraries loaded, once; false if libwayland-client or libxkbcommon isn't there.
bool load();

} // namespace kompoz::platform::wayland

#define KOMPOZ_WAYLAND_RENAME(name) (*kompoz::platform::wayland::fn.p_##name)
#define wl_display_connect KOMPOZ_WAYLAND_RENAME(wl_display_connect)
#define wl_display_disconnect KOMPOZ_WAYLAND_RENAME(wl_display_disconnect)
#define wl_display_get_fd KOMPOZ_WAYLAND_RENAME(wl_display_get_fd)
#define wl_display_dispatch_pending KOMPOZ_WAYLAND_RENAME(wl_display_dispatch_pending)
#define wl_display_roundtrip KOMPOZ_WAYLAND_RENAME(wl_display_roundtrip)
#define wl_display_flush KOMPOZ_WAYLAND_RENAME(wl_display_flush)
#define wl_display_prepare_read KOMPOZ_WAYLAND_RENAME(wl_display_prepare_read)
#define wl_display_read_events KOMPOZ_WAYLAND_RENAME(wl_display_read_events)
#define wl_display_cancel_read KOMPOZ_WAYLAND_RENAME(wl_display_cancel_read)
#define wl_display_get_error KOMPOZ_WAYLAND_RENAME(wl_display_get_error)
#define wl_proxy_marshal_flags KOMPOZ_WAYLAND_RENAME(wl_proxy_marshal_flags)
#define wl_proxy_add_listener KOMPOZ_WAYLAND_RENAME(wl_proxy_add_listener)
#define wl_proxy_destroy KOMPOZ_WAYLAND_RENAME(wl_proxy_destroy)
#define wl_proxy_get_version KOMPOZ_WAYLAND_RENAME(wl_proxy_get_version)
#define wl_proxy_set_user_data KOMPOZ_WAYLAND_RENAME(wl_proxy_set_user_data)
#define wl_proxy_get_user_data KOMPOZ_WAYLAND_RENAME(wl_proxy_get_user_data)
#define wl_egl_window_create KOMPOZ_WAYLAND_RENAME(wl_egl_window_create)
#define wl_egl_window_resize KOMPOZ_WAYLAND_RENAME(wl_egl_window_resize)
#define wl_egl_window_destroy KOMPOZ_WAYLAND_RENAME(wl_egl_window_destroy)
#define xkb_context_new KOMPOZ_WAYLAND_RENAME(xkb_context_new)
#define xkb_context_unref KOMPOZ_WAYLAND_RENAME(xkb_context_unref)
#define xkb_keymap_new_from_string KOMPOZ_WAYLAND_RENAME(xkb_keymap_new_from_string)
#define xkb_keymap_unref KOMPOZ_WAYLAND_RENAME(xkb_keymap_unref)
#define xkb_keymap_key_repeats KOMPOZ_WAYLAND_RENAME(xkb_keymap_key_repeats)
#define xkb_state_new KOMPOZ_WAYLAND_RENAME(xkb_state_new)
#define xkb_state_unref KOMPOZ_WAYLAND_RENAME(xkb_state_unref)
#define xkb_state_update_mask KOMPOZ_WAYLAND_RENAME(xkb_state_update_mask)
#define xkb_state_key_get_one_sym KOMPOZ_WAYLAND_RENAME(xkb_state_key_get_one_sym)
#define xkb_state_key_get_utf8 KOMPOZ_WAYLAND_RENAME(xkb_state_key_get_utf8)
#define xkb_state_mod_name_is_active KOMPOZ_WAYLAND_RENAME(xkb_state_mod_name_is_active)
