#pragma once

// OpenGL ES 3 on a native window through EGL, for Kompoz's own desktop platforms (Wayland's
// wl_egl_window, an X11 window, a Win32 window through ANGLE): libEGL and its OpenGL ES library
// loaded at run time (ANGLE's libEGL.dll and libGLESv2.dll beside the program on Windows), the
// display on the window system's EGL platform, a context and the window's surface, current on the
// thread. No EGL header is needed to build it.

#include "../gpu/device.hpp"

#include <string>

namespace kompoz::platform {

class EglWindow {
public:
    // EGL's platforms (EGL_PLATFORM_*_KHR) for Options::platform; 0: the default display.
    static constexpr unsigned kWayland = 0x31D8, kX11 = 0x31D5;

    EglWindow() = default;
    ~EglWindow() { close(); }
    EglWindow(const EglWindow&) = delete;
    EglWindow& operator=(const EglWindow&) = delete;

    struct Options {
        unsigned platform = 0;          // kWayland, kX11; 0: eglGetDisplay's (ANGLE's on Windows)
        void* display = nullptr;        // the window system's (with platform 0: EGLNativeDisplayType)
        void* window = nullptr;         // what eglCreatePlatformWindowSurface takes for it (with platform 0: the HWND)
        int visual = -1;                // the window's visual, a configuration must have it (X11); -1: any
        bool alpha = true;              // an alpha channel in the configuration
        int swapInterval = 1;           // 0: the platform paces the frames itself
        bool debug = false;             // a debug context (KOMPOZ_GPU_VALIDATION)
    };
    // Whether EGL is there (on Windows: ANGLE beside the program).
    static bool available();
    // Why it isn't though its library is there (it couldn't be loaded); empty otherwise.
    static std::string unavailable();
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
