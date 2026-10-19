#pragma once

// The GPU renderer's hardware layer: the little of a graphics API the renderer needs, implemented
// once for each native API (Vulkan, Direct3D 12, Metal, OpenGL ES 3 / WebGL 2, WebGPU). It is
// explicit like those APIs and small: textures, pipelines made up front, render passes that say
// which textures they read, and per-frame memory for what the CPU hands the GPU once. The
// renderer (renderer.hpp) does the rest.
//
//   device->beginFrame();
//   device->upload(atlas, region, pixels, rowBytes);        // outside passes
//   device->beginPass(*device->swapchainTexture(), clear, {&atlas});
//   device->setPipeline(*pipeline);
//   device->setTexture(0, atlas, sampler);
//   device->draw(instances, 6, count);                      // instanced quads, made in the vertex shader
//   device->endPass();
//   device->endFrame(/*present=*/true);

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kompoz::gpu {

enum class Backend { Vulkan, D3D12, Metal, GLES, WebGPU };
const char* backendName(Backend backend);

// 8 bits a channel, not sRGB-encoded: colors are blended as they are stored, as Android and Skia
// do (and as Kompoz always has). R16F: one 16-bit float channel, drawn into with additive blending
// (the path renderer's coverage). RGBA32F: four 32-bit floats, uploaded and read with nearest
// filtering (gradients' stops).
enum class Format : uint8_t { RGBA8, BGRA8, R16F, RGBA32F };
// A texel's size in bytes.
constexpr size_t bytesPerTexel(Format f) { return f == Format::RGBA32F ? 16 : f == Format::R16F ? 2 : 4; }

struct TextureUsage {
    enum : uint8_t { Sampled = 1, RenderTarget = 2 };
};

class Texture {
public:
    virtual ~Texture() = default;
    int width() const { return width_; }
    int height() const { return height_; }
    Format format() const { return format_; }

protected:
    Texture(int width, int height, Format format) : width_(width), height_(height), format_(format) {}

private:
    int width_, height_;
    Format format_;
};

enum class Filter : uint8_t { Nearest, Linear };
// Beyond the edges: the edge texel, the texture again, mirrored, or transparent.
enum class Wrap : uint8_t { Clamp, Repeat, Mirror, Transparent };
struct Sampler {
    Filter filter = Filter::Linear;
    Wrap wrapU = Wrap::Clamp, wrapV = Wrap::Clamp;
    bool operator==(const Sampler&) const = default;
};

// An entry point's code for each backend (from scripts/shaders.py): SPIR-V, DXIL, MSL source,
// GLSL ES 3.00 source, WGSL source (the sources end in a NUL).
struct ShaderCode {
    std::span<const unsigned char> spirv, dxil, msl, glsl, wgsl;
    const char* entry = "main";
};

enum class VertexFormat : uint8_t { Float, Float2, Float3, Float4, UInt4 };
struct VertexAttribute {
    uint32_t location;
    VertexFormat format;
    uint32_t offset;
};

enum class BlendFactor : uint8_t { Zero, One, SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor };
enum class BlendOp : uint8_t { Add, Subtract, ReverseSubtract, Min, Max };
// Off: the color written as it is. The default: premultiplied source over.
struct BlendState {
    bool enabled = true;
    BlendFactor srcColor = BlendFactor::One, dstColor = BlendFactor::OneMinusSrcAlpha;
    BlendFactor srcAlpha = BlendFactor::One, dstAlpha = BlendFactor::OneMinusSrcAlpha;
    BlendOp colorOp = BlendOp::Add, alphaOp = BlendOp::Add;
    bool operator==(const BlendState&) const = default;
};

// Every pipeline binds the same way: up to kMaxUniforms bytes of uniforms both stages see
// (setUniforms), `textures` textures each with its own sampler (bindings 1, 2, ... in the
// shaders), and one buffer of per-instance attributes (no per-vertex ones: the vertex shader
// makes each instance's vertices from the vertex index).
constexpr size_t kMaxUniforms = 128;
constexpr int kMaxTextures = 4;
struct PipelineDesc {
    const ShaderCode* vertex = nullptr;
    const ShaderCode* fragment = nullptr;
    std::vector<VertexAttribute> instanceAttributes;
    uint32_t instanceStride = 0;
    int textures = 0;
    BlendState blend;
    Format target = Format::RGBA8;
};

class Pipeline {
public:
    virtual ~Pipeline() = default;
};

// Memory the CPU writes this frame and the GPU reads once: where it is, for draw().
struct BufferRef {
    const void* buffer = nullptr;  // the backend's
    uint64_t offset = 0;
};

struct IRect {
    int x = 0, y = 0, width = 0, height = 0;
    bool empty() const { return width <= 0 || height <= 0; }
    bool operator==(const IRect&) const = default;
};

struct ClearColor {
    float r = 0, g = 0, b = 0, a = 0;
};

// What the window system gives a backend to present to (filled by the platform layer).
struct Surface {
    // Vulkan: the instance extensions presenting needs, vkGetInstanceProcAddr, and the surface
    // (a VkSurfaceKHR) made for an instance; false if it can't be.
    std::vector<const char*> vulkanInstanceExtensions;
    void* (*vulkanGetInstanceProcAddr)(void* instance, const char* name) = nullptr;
    std::function<bool(void* instance, uint64_t* surface)> createVulkanSurface;
    // The drawable's size in pixels.
    std::function<void(int* width, int* height)> drawableSize;
};

// Where the hardware layer's messages go (stderr until the platform says).
void setLogger(void (*logger)(const char* message));
void log(const std::string& message);

class Device {
public:
    virtual ~Device() = default;

    // A device for `backend` presenting to `surface`; nothing (and *error says why) if this
    // system can't have one. KOMPOZ_GPU_VALIDATION=1 turns the API's validation on where it has
    // one (Vulkan's layers), KOMPOZ_GPU_DEVICE=<part of a name> picks the GPU.
    static std::unique_ptr<Device> create(Backend backend, const Surface& surface, std::string* error);

    virtual Backend backend() const = 0;
    virtual std::string deviceName() const = 0;
    // Errors the API's validation reported so far (with KOMPOZ_GPU_VALIDATION=1; tests want none).
    virtual int validationErrors() const { return 0; }
    // In clip space y goes up (Direct3D, Metal, OpenGL, WebGPU) or down (Vulkan): the renderer
    // flips its projection for the second.
    virtual bool clipYDown() const = 0;
    // A render target's rows are stored bottom up when sampled (OpenGL): the renderer flips
    // texture coordinates of what it rendered.
    virtual bool renderTargetsBottomUp() const { return false; }

    virtual std::unique_ptr<Texture> createTexture(int width, int height, Format format, uint8_t usage) = 0;
    virtual std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) = 0;
    // The format the swapchain's images have (pipelines drawing into them are made for it).
    virtual Format swapchainFormat() const = 0;

    // Starts a frame (waiting for the GPU to be done with the one before the last). Calls below
    // need a frame begun.
    virtual void beginFrame() = 0;
    // The image presented at the end of the frame, at the drawable's size; nothing if there is
    // nothing to present to now (minimized, the surface lost).
    virtual Texture* swapchainTexture() = 0;
    // `size` bytes of this frame's memory (aligned to `alignment`), to write now.
    virtual void* allocate(size_t size, size_t alignment, BufferRef* ref) = 0;
    // Texels (in the texture's format, `rowBytes` apart) into part of a texture; outside passes.
    virtual void upload(Texture& texture, const IRect& region, const void* pixels, size_t rowBytes) = 0;
    // Draws into `target` (cleared first if `clear`), sampling `reads` (each read before by
    // what was drawn into it).
    virtual void beginPass(Texture& target, const std::optional<ClearColor>& clear, std::span<Texture* const> reads) = 0;
    virtual void setPipeline(Pipeline& pipeline) = 0;
    virtual void setScissor(const IRect& rect) = 0;
    virtual void setUniforms(const void* data, size_t size) = 0;
    virtual void setTexture(int slot, Texture& texture, const Sampler& sampler) = 0;
    // `instanceCount` instances of `vertexCount` vertices (triangles), their attributes from `instances`.
    virtual void draw(const BufferRef& instances, uint32_t vertexCount, uint32_t instanceCount) = 0;
    virtual void endPass() = 0;
    // Part of one texture into another of the same format, at (x, y); outside passes.
    virtual void copyTexture(Texture& source, const IRect& region, Texture& destination, int x, int y) = 0;
    // A texture's pixels (RGBA8, top row first), waiting for what was drawn into it so far.
    virtual std::vector<uint8_t> readPixels(Texture& texture, const IRect& region) = 0;
    // Submits the frame, and shows the swapchain image if `present` (and it was asked for).
    virtual void endFrame(bool present) = 0;
    // Waits for the GPU to finish everything (before resources it uses go).
    virtual void waitIdle() = 0;
};

} // namespace kompoz::gpu
