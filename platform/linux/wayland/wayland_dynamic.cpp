#include "wayland_dynamic.hpp"

#include <dlfcn.h>

namespace kompoz::platform::wayland {

Functions fn;

namespace {

// Each function by its name (#name: the name as written, the macros above not applied to it).
template <class T> bool find(void* library, T& pointer, const char* name) {
    pointer = library ? reinterpret_cast<T>(dlsym(library, name)) : nullptr;
    return pointer != nullptr;
}

bool loadAll() {
    void* client = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_LOCAL);
    void* xkb = dlopen("libxkbcommon.so.0", RTLD_NOW | RTLD_LOCAL);
    void* egl = dlopen("libwayland-egl.so.1", RTLD_NOW | RTLD_LOCAL);
    bool ok = true;
#define KOMPOZ_WAYLAND_FIND(lib) [&](auto& pointer, const char* name) { ok = find(lib, pointer, name) && ok; }
    auto inClient = KOMPOZ_WAYLAND_FIND(client);
    auto inXkb = KOMPOZ_WAYLAND_FIND(xkb);
#undef KOMPOZ_WAYLAND_FIND
#define KOMPOZ_WAYLAND_LOAD_CLIENT(name) inClient(fn.p_##name, #name);
#define KOMPOZ_WAYLAND_LOAD_XKB(name) inXkb(fn.p_##name, #name);
    KOMPOZ_WAYLAND_CLIENT_FUNCTIONS(KOMPOZ_WAYLAND_LOAD_CLIENT)
    KOMPOZ_XKB_FUNCTIONS(KOMPOZ_WAYLAND_LOAD_XKB)
    fn.egl = true;
#define KOMPOZ_WAYLAND_LOAD_EGL(name) fn.egl = find(egl, fn.p_##name, #name) && fn.egl;
    KOMPOZ_WAYLAND_EGL_FUNCTIONS(KOMPOZ_WAYLAND_LOAD_EGL)
#undef KOMPOZ_WAYLAND_LOAD_CLIENT
#undef KOMPOZ_WAYLAND_LOAD_XKB
#undef KOMPOZ_WAYLAND_LOAD_EGL
    return ok;  // (the libraries stay loaded for the process)
}

} // namespace

bool load() {
    static const bool loaded = loadAll();
    return loaded;
}

} // namespace kompoz::platform::wayland
