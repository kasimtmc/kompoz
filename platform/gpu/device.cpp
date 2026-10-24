// What every backend shares: picking one, and where messages go.

#include "device.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace kompoz::gpu {

std::unique_ptr<Device> createVulkanDevice(const Surface& surface, std::string* error);
std::unique_ptr<Device> createGlesDevice(const Surface& surface, std::string* error);
std::unique_ptr<Device> createMetalDevice(const Surface& surface, std::string* error);
std::unique_ptr<Device> createWebGPUDevice(const Surface& surface, std::string* error);

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

namespace {
// A cache file: this, the data's size and FNV-1a hash (8 bytes each, little-endian), the data.
constexpr char kCacheMagic[8] = {'K', 'P', 'Z', 'P', 'I', 'P', 'E', '1'};

void putU64(std::vector<uint8_t>& out, uint64_t value) {
    for (int i = 0; i < 8; ++i) out.push_back(uint8_t(value >> (8 * i)));
}

uint64_t getU64(const uint8_t* in) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= uint64_t(in[i]) << (8 * i);
    return value;
}

std::filesystem::path utf8Path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
} // namespace

uint64_t fnv1a(const void* data, size_t size, uint64_t hash) {
    auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 0x100000001b3;
    return hash;
}

std::vector<uint8_t> readPipelineCache(const std::string& path) {
    std::ifstream in(utf8Path(path), std::ios::binary);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    constexpr size_t header = sizeof kCacheMagic + 16;
    if (file.size() < header || std::memcmp(file.data(), kCacheMagic, sizeof kCacheMagic) != 0) return {};
    uint64_t size = getU64(file.data() + sizeof kCacheMagic);
    if (size != file.size() - header || getU64(file.data() + sizeof kCacheMagic + 8) != fnv1a(file.data() + header, size)) return {};
    return {file.begin() + header, file.end()};
}

void writePipelineCache(const std::string& path, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> file(kCacheMagic, kCacheMagic + sizeof kCacheMagic);
    putU64(file, data.size());
    putU64(file, fnv1a(data.data(), data.size()));
    file.insert(file.end(), data.begin(), data.end());
    // (Another run of the app writing it too writes a file of its own.)
    auto stamp = uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::path target = utf8Path(path), part = utf8Path(path + "." + std::to_string(stamp % 1000000007) + ".tmp");
    std::error_code ec;
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
        if (!out.flush()) {
            out.close();
            std::filesystem::remove(part, ec);
            log("the pipeline cache couldn't be written: " + path);
            return;
        }
    }
    std::filesystem::rename(part, target, ec);
    if (ec) {  // where renaming doesn't replace
        std::error_code e;
        std::filesystem::remove(target, e);
        std::filesystem::rename(part, target, ec);
    }
    if (ec) {
        std::filesystem::remove(part, ec);
        log("the pipeline cache couldn't be written: " + path);
    }
}

std::string passLabel(int width, int height, Format format, bool window) {
    if (window) return "window";
    std::string label = std::to_string(width) + "x" + std::to_string(height);
    switch (format) {
    case Format::RGBA8: break;
    case Format::BGRA8: label += " BGRA8"; break;
    case Format::R16F: label += " R16F"; break;
    case Format::RGBA32F: label += " RGBA32F"; break;
    case Format::RGBA16F: label += " RGBA16F"; break;
    }
    return label;
}

const char* backendName(Backend backend) {
    switch (backend) {
    case Backend::Vulkan: return "vulkan";
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
#if KOMPOZ_GPU_GLES
    case Backend::GLES: return createGlesDevice(surface, error);
#endif
#if KOMPOZ_GPU_METAL
    case Backend::Metal: return createMetalDevice(surface, error);
#endif
#if KOMPOZ_GPU_WEBGPU
    case Backend::WebGPU: return createWebGPUDevice(surface, error);
#endif
    default:
        if (error) *error = std::string("Kompoz was built without its ") + backendName(backend) + " backend";
        return nullptr;
    }
}

} // namespace kompoz::gpu
