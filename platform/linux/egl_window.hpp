#pragma once

// OpenGL ES 3 on a native window through EGL, for Kompoz's own platforms on Linux (Wayland's
// wl_egl_window, an X11 window): libEGL and libGLESv2 loaded at run time, the display on the
// window system's EGL platform, a context and the window's surface, current on the thread.

#include "../gpu/device.hpp"

#include <string>

namespace kompoz::platform {

class EglWindow {
public:
    EglWindow() = default;
    ~EglWindow() { close(); }
    EglWindow(const EglWindow&) = delete;
    EglWindow& operator=(const EglWindow&) = delete;

    struct Options {
        unsigned platform = 0;          // EGL_PLATFORM_WAYLAND_KHR, EGL_PLATFORM_X11_KHR
        void* display = nullptr;        // the window system's
        void* window = nullptr;         // what eglCreatePlatformWindowSurface takes for it
        int visual = -1;                // the window's visual, a configuration must have it (X11); -1: any
        bool alpha = true;              // an alpha channel in the configuration
        int swapInterval = 1;           // 0: the platform paces the frames itself
        bool debug = false;             // a debug context (KOMPOZ_GPU_VALIDATION)
    };
    // Made, with what the GPU device needs into `surface` (its functions, the swap); false (and
    // `error`) if it can't be.
    bool open(const Options& options, gpu::Surface& surface, std::string* error);
    void close();

private:
    void* display_ = nullptr;  // EGLDisplay, EGLContext, EGLSurface
    void* context_ = nullptr;
    void* surface_ = nullptr;
};

} // namespace kompoz::platform
