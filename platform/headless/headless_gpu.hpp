#pragma once

// What a GPU device needs without a window, for the headless platform and the GPU tests: Vulkan
// from the system's loader, and Metal, drawing only into textures; OpenGL ES 3 through EGL (on
// Windows ANGLE's, beside the program), its context current on a small pbuffer.

#include "../gpu/device.hpp"

#include <string>

namespace kompoz::platform {

class HeadlessGpu {
public:
    HeadlessGpu() = default;
    HeadlessGpu(const HeadlessGpu&) = delete;
    HeadlessGpu& operator=(const HeadlessGpu&) = delete;
    ~HeadlessGpu() { close(); }

    // What a device for `backend` needs, into `surface` (its drawable size is the caller's); false,
    // and why in `error`, if it can't be had here. `debug`: an OpenGL ES context that reports
    // errors (KOMPOZ_GPU_VALIDATION). One at a time.
    bool open(gpu::Backend backend, bool debug, gpu::Surface& surface, std::string* error);
    void close();  // (after the device made on it is gone)

private:
    void* vulkan_ = nullptr;  // the loader
    void* egl_ = nullptr;
    void* display_ = nullptr;
    void* context_ = nullptr;
    void* pbuffer_ = nullptr;
};

} // namespace kompoz::platform
