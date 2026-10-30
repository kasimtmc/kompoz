#include "egl_window.hpp"

#include "system.hpp"

#include <cstdint>
#include <filesystem>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace kompoz::platform {

namespace {

#if defined(_WIN32)
// (A UTF-8 path: the program's directory in any letters.)
void* openLibrary(const std::string& name) { return LoadLibraryW(std::filesystem::path(std::u8string(name.begin(), name.end())).c_str()); }
void* symbol(void* library, const char* name) { return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name)); }
std::string eglLibrary() { return programDirectory() + "libEGL.dll"; }  // ANGLE's
std::string glesLibrary() { return programDirectory() + "libGLESv2.dll"; }
#else
void* openLibrary(const std::string& name) { return dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL); }
void* symbol(void* library, const char* name) { return dlsym(library, name); }
std::string eglLibrary() { return "libEGL.so.1"; }
std::string glesLibrary() { return "libGLESv2.so.2"; }
#endif

// EGL's, as its header has them.
using EGLint = int32_t;
using EGLBoolean = unsigned;
constexpr EGLint kNone = 0x3038, kRenderableType = 0x3040, kOpenGLES3Bit = 0x40, kSurfaceType = 0x3033, kWindowBit = 0x4, kRedSize = 0x3024,
                 kGreenSize = 0x3023, kBlueSize = 0x3022, kAlphaSize = 0x3021, kNativeVisualId = 0x302E, kContextMajorVersion = 0x3098,
                 kContextMinorVersion = 0x30FB, kContextOpenGLDebug = 0x31B0;
constexpr unsigned kOpenGLESApi = 0x30A0;

// EGL's functions (EGL 1.5's platform ones where the platform is named).
struct Egl {
    void* (*getPlatformDisplay)(unsigned, void*, const intptr_t*) = nullptr;
    void* (*getDisplay)(void*) = nullptr;
    EGLBoolean (*initialize)(void*, EGLint*, EGLint*) = nullptr;
    EGLBoolean (*bindApi)(unsigned) = nullptr;
    EGLBoolean (*chooseConfig)(void*, const EGLint*, void**, EGLint, EGLint*) = nullptr;
    EGLBoolean (*getConfigAttrib)(void*, void*, EGLint, EGLint*) = nullptr;
    void* (*createContext)(void*, void*, void*, const EGLint*) = nullptr;
    void* (*createPlatformWindowSurface)(void*, void*, void*, const intptr_t*) = nullptr;
    void* (*createWindowSurface)(void*, void*, void*, const EGLint*) = nullptr;
    EGLBoolean (*makeCurrent)(void*, void*, void*, void*) = nullptr;
    EGLBoolean (*swapBuffers)(void*, void*) = nullptr;
    EGLBoolean (*swapInterval)(void*, EGLint) = nullptr;
    EGLBoolean (*destroySurface)(void*, void*) = nullptr;
    EGLBoolean (*destroyContext)(void*, void*) = nullptr;
    EGLBoolean (*terminate)(void*) = nullptr;
    void* (*getProcAddress)(const char*) = nullptr;
    void* library = nullptr;
    void* gles = nullptr;

    bool load() {
        if (!library) library = openLibrary(eglLibrary());
        if (!library) return false;
        if (!gles) gles = openLibrary(glesLibrary());
        auto get = [this](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(symbol(library, name));
            return fn != nullptr;
        };
        get(getPlatformDisplay, "eglGetPlatformDisplay"), get(createPlatformWindowSurface, "eglCreatePlatformWindowSurface");
        return get(getDisplay, "eglGetDisplay") && get(initialize, "eglInitialize") && get(bindApi, "eglBindAPI") &&
               get(chooseConfig, "eglChooseConfig") && get(getConfigAttrib, "eglGetConfigAttrib") && get(createContext, "eglCreateContext") &&
               get(createWindowSurface, "eglCreateWindowSurface") && get(makeCurrent, "eglMakeCurrent") && get(swapBuffers, "eglSwapBuffers") &&
               get(swapInterval, "eglSwapInterval") && get(destroySurface, "eglDestroySurface") && get(destroyContext, "eglDestroyContext") &&
               get(terminate, "eglTerminate") && get(getProcAddress, "eglGetProcAddress");
    }
};
Egl gEgl;

} // namespace

bool EglWindow::available() { return gEgl.load(); }

bool EglWindow::open(const Options& o, gpu::Surface& surface, std::string* error) {
    if (!gEgl.load() || (o.platform && (!gEgl.getPlatformDisplay || !gEgl.createPlatformWindowSurface))) {
        *error = "no EGL";
        return false;
    }
    void* display = o.platform ? gEgl.getPlatformDisplay(o.platform, o.display, nullptr) : gEgl.getDisplay(o.display);
    if (!display || !gEgl.initialize(display, nullptr, nullptr)) {
        *error = "no EGL display";
        return false;
    }
    display_ = display;
    const EGLint attributes[] = {kRenderableType, kOpenGLES3Bit, kSurfaceType, kWindowBit, kRedSize, 8, kGreenSize, 8,
                                 kBlueSize, 8, kAlphaSize, o.alpha ? 8 : 0, kNone};
    std::vector<void*> configs(64);
    EGLint count = 0;
    gEgl.chooseConfig(display, attributes, configs.data(), EGLint(configs.size()), &count);
    void* config = nullptr;
    for (EGLint i = 0; i < count && !config; ++i) {
        EGLint visual = 0;
        if (o.visual < 0 || (gEgl.getConfigAttrib(display, configs[size_t(i)], kNativeVisualId, &visual) && visual == o.visual))
            config = configs[size_t(i)];
    }
    if (!config) {
        *error = "no OpenGL ES 3 configuration";
        return false;
    }
    gEgl.bindApi(kOpenGLESApi);
    const EGLint contextAttributes[] = {kContextMajorVersion, 3, kContextMinorVersion, 0, kContextOpenGLDebug, o.debug ? 1 : 0, kNone};
    context_ = gEgl.createContext(display, config, nullptr, contextAttributes);
    if (context_) surface_ = o.platform ? gEgl.createPlatformWindowSurface(display, config, o.window, nullptr) : gEgl.createWindowSurface(display, config, o.window, nullptr);
    if (!surface_ || !gEgl.makeCurrent(display, surface_, surface_, context_)) {
        *error = "no OpenGL ES 3 context on the window";
        return false;
    }
    gEgl.swapInterval(display, o.swapInterval);
    surface.glGetProcAddress = [](const char* name) -> void* {
        if (void* f = gEgl.gles ? symbol(gEgl.gles, name) : nullptr) return f;
        return gEgl.getProcAddress(name);
    };
    surface.glSwap = [this] { gEgl.swapBuffers(display_, surface_); };
    return true;
}

void EglWindow::close() {
    if (!display_) return;
    gEgl.makeCurrent(display_, nullptr, nullptr, nullptr);
    if (surface_) gEgl.destroySurface(display_, surface_);
    if (context_) gEgl.destroyContext(display_, context_);
    gEgl.terminate(display_);
    display_ = context_ = surface_ = nullptr;
}

} // namespace kompoz::platform
