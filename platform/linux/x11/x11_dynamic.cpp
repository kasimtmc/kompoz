#include "x11_dynamic.hpp"

#include <dlfcn.h>

namespace kompoz::platform::x11 {

Functions fn;

namespace {

// Each function by its name (#name: the name as written, the macros above not applied to it).
template <class T> bool find(void* library, T& pointer, const char* name) {
    pointer = library ? reinterpret_cast<T>(dlsym(library, name)) : nullptr;
    return pointer != nullptr;
}

bool loadAll() {
    void* x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    void* xi = dlopen("libXi.so.6", RTLD_NOW | RTLD_LOCAL);
    void* xcursor = dlopen("libXcursor.so.1", RTLD_NOW | RTLD_LOCAL);
    void* xrandr = dlopen("libXrandr.so.2", RTLD_NOW | RTLD_LOCAL);
    bool ok = true;
#define KOMPOZ_X11_LOAD(name) ok = find(x11, fn.p_##name, #name) && ok;
#define KOMPOZ_XI_LOAD(name) ok = find(xi, fn.p_##name, #name) && ok;
#define KOMPOZ_XCURSOR_LOAD(name) fn.xcursor = find(xcursor, fn.p_##name, #name) && fn.xcursor;
#define KOMPOZ_XRANDR_LOAD(name) fn.xrandr = find(xrandr, fn.p_##name, #name) && fn.xrandr;
    KOMPOZ_X11_FUNCTIONS(KOMPOZ_X11_LOAD)
    KOMPOZ_XI_FUNCTIONS(KOMPOZ_XI_LOAD)
    fn.xcursor = fn.xrandr = true;
    KOMPOZ_XCURSOR_FUNCTIONS(KOMPOZ_XCURSOR_LOAD)
    KOMPOZ_XRANDR_FUNCTIONS(KOMPOZ_XRANDR_LOAD)
#undef KOMPOZ_X11_LOAD
#undef KOMPOZ_XI_LOAD
#undef KOMPOZ_XCURSOR_LOAD
#undef KOMPOZ_XRANDR_LOAD
    return ok;  // (the libraries stay loaded for the process)
}

} // namespace

bool load() {
    static const bool loaded = loadAll();
    return loaded;
}

} // namespace kompoz::platform::x11
