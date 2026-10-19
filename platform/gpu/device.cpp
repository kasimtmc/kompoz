// What every backend shares: picking one, and where messages go.

#include "device.hpp"

#include <cstdio>

namespace kompoz::gpu {

std::unique_ptr<Device> createVulkanDevice(const Surface& surface, std::string* error);

namespace {
void (*gLogger)(const char*) = nullptr;
}

void setLogger(void (*logger)(const char* message)) { gLogger = logger; }

void log(const std::string& message) {
    if (gLogger)
        gLogger(message.c_str());
    else
        std::fprintf(stderr, "%s\n", message.c_str());
}

const char* backendName(Backend backend) {
    switch (backend) {
    case Backend::Vulkan: return "vulkan";
    case Backend::D3D12: return "d3d12";
    case Backend::Metal: return "metal";
    case Backend::GLES: return "gles";
    case Backend::WebGPU: return "webgpu";
    }
    return "?";
}

std::unique_ptr<Device> Device::create(Backend backend, const Surface& surface, std::string* error) {
    switch (backend) {
#if KOMPOZ_GPU_VULKAN
    case Backend::Vulkan: return createVulkanDevice(surface, error);
#endif
    default:
        if (error) *error = std::string("Kompoz was built without its ") + backendName(backend) + " backend";
        return nullptr;
    }
}

} // namespace kompoz::gpu
