#include "egl_window.hpp"

#define EGL_NO_X11 1
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <dlfcn.h>

#include <type_traits>
#include <vector>

namespace kompoz::platform {

namespace {

// EGL's functions, by their declarations' types.
struct Egl {
    decltype(&eglGetPlatformDisplay) getPlatformDisplay = nullptr;
    decltype(&eglInitialize) initialize = nullptr;
    decltype(&eglBindAPI) bindApi = nullptr;
    decltype(&eglChooseConfig) chooseConfig = nullptr;
    decltype(&eglGetConfigAttrib) getConfigAttrib = nullptr;
    decltype(&eglCreateContext) createContext = nullptr;
    decltype(&eglCreatePlatformWindowSurface) createPlatformWindowSurface = nullptr;
    decltype(&eglMakeCurrent) makeCurrent = nullptr;
    decltype(&eglSwapBuffers) swapBuffers = nullptr;
    decltype(&eglSwapInterval) swapInterval = nullptr;
    decltype(&eglDestroySurface) destroySurface = nullptr;
    decltype(&eglDestroyContext) destroyContext = nullptr;
    decltype(&eglTerminate) terminate = nullptr;
    decltype(&eglGetProcAddress) getProcAddress = nullptr;
    void* library = nullptr;

    bool load() {
        if (!library) library = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!library) return false;
        auto get = [this](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(library, name));
            return fn != nullptr;
        };
        return get(getPlatformDisplay, "eglGetPlatformDisplay") && get(initialize, "eglInitialize") && get(bindApi, "eglBindAPI") &&
               get(chooseConfig, "eglChooseConfig") && get(getConfigAttrib, "eglGetConfigAttrib") && get(createContext, "eglCreateContext") &&
               get(createPlatformWindowSurface, "eglCreatePlatformWindowSurface") && get(makeCurrent, "eglMakeCurrent") &&
               get(swapBuffers, "eglSwapBuffers") && get(swapInterval, "eglSwapInterval") && get(destroySurface, "eglDestroySurface") &&
               get(destroyContext, "eglDestroyContext") && get(terminate, "eglTerminate") && get(getProcAddress, "eglGetProcAddress");
    }
};
Egl gEgl;
void* gGles = nullptr;

} // namespace

bool EglWindow::open(const Options& o, gpu::Surface& surface, std::string* error) {
    if (!gEgl.load()) {
        *error = "no EGL";
        return false;
    }
    EGLDisplay display = gEgl.getPlatformDisplay(o.platform, o.display, nullptr);
    if (display == EGL_NO_DISPLAY || !gEgl.initialize(display, nullptr, nullptr)) {
        *error = "no EGL display";
        return false;
    }
    display_ = display;
    const EGLint attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                 EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, o.alpha ? 8 : 0, EGL_NONE};
    std::vector<EGLConfig> configs(64);
    EGLint count = 0;
    gEgl.chooseConfig(display, attributes, configs.data(), EGLint(configs.size()), &count);
    EGLConfig config = nullptr;
    for (EGLint i = 0; i < count && !config; ++i) {
        EGLint visual = 0;
        if (o.visual < 0 || (gEgl.getConfigAttrib(display, configs[size_t(i)], EGL_NATIVE_VISUAL_ID, &visual) && visual == o.visual))
            config = configs[size_t(i)];
    }
    if (!config) {
        *error = "no OpenGL ES 3 configuration";
        return false;
    }
    gEgl.bindApi(EGL_OPENGL_ES_API);
    const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0,
                                        EGL_CONTEXT_OPENGL_DEBUG, o.debug ? EGL_TRUE : EGL_FALSE, EGL_NONE};
    context_ = gEgl.createContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    surface_ = context_ != EGL_NO_CONTEXT ? gEgl.createPlatformWindowSurface(display, config, o.window, nullptr) : EGL_NO_SURFACE;
    if (surface_ == EGL_NO_SURFACE || !gEgl.makeCurrent(display, surface_, surface_, context_)) {
        *error = "no OpenGL ES 3 context on the window";
        return false;
    }
    gEgl.swapInterval(display, o.swapInterval);
    if (!gGles) gGles = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    surface.glGetProcAddress = [](const char* name) -> void* {
        if (void* f = gGles ? dlsym(gGles, name) : nullptr) return f;
        return reinterpret_cast<void*>(gEgl.getProcAddress(name));
    };
    surface.glSwap = [this] { gEgl.swapBuffers(display_, surface_); };
    return true;
}

void EglWindow::close() {
    if (display_ == EGL_NO_DISPLAY) return;
    gEgl.makeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface_ != EGL_NO_SURFACE) gEgl.destroySurface(display_, surface_);
    if (context_ != EGL_NO_CONTEXT) gEgl.destroyContext(display_, context_);
    gEgl.terminate(display_);
    display_ = EGL_NO_DISPLAY, surface_ = EGL_NO_SURFACE, context_ = EGL_NO_CONTEXT;
}

} // namespace kompoz::platform
