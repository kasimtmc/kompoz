#pragma once

// The GPU renderer's hardware layer: the little of a graphics API the renderer needs, implemented
// once for each native API: Vulkan (Linux, Windows, Android), Metal (Apple), WebGPU (the web), and
// OpenGL ES 3 where there is none of those (old GPUs; WebGL 2; ANGLE on Windows). It is
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

enum class Backend { Vulkan, Metal, GLES, WebGPU };
const char* backendName(Backend backend);

// 8 bits a channel, not sRGB-encoded: colors are blended as they are stored, as Android and Skia
// do (and as Kompoz always has). R16F: one 16-bit float channel, drawn into with additive blending
// (the path renderer's coverage). RGBA32F: four 32-bit floats, uploaded and read with nearest
// filtering (gradients' stops). RGBA16F: four 16-bit floats, drawn into and blended as RGBA8 is
// (a progressive blur's mask, finer than 8 bits).
enum class Format : uint8_t { RGBA8, BGRA8, R16F, RGBA32F, RGBA16F };
// A texel's size in bytes.
constexpr size_t bytesPerTexel(Format f) {
    return f == Format::RGBA32F ? 16 : f == Format::RGBA16F ? 8 : f == Format::R16F ? 2 : 4;
}

// Mipmapped: copies halved and halved again down to a texel, made by generateMipmaps.
struct TextureUsage {
    enum : uint8_t { Sampled = 1, RenderTarget = 2, Mipmapped = 4 };
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
// Beyond the edges: the edge texel, the texture again, or mirrored. (Transparent past them, which not
// every API's samplers can do, is the shaders'.)
enum class Wrap : uint8_t { Clamp, Repeat, Mirror };
// `mipmaps`: from a mipmapped texture's copy nearest the size it is drawn at (else its full size).
struct Sampler {
    Filter filter = Filter::Linear;
    Wrap wrapU = Wrap::Clamp, wrapV = Wrap::Clamp;
    bool mipmaps = false;
    bool operator==(const Sampler&) const = default;
};

// An entry point's code for each backend (from scripts/shaders.py): SPIR-V, MSL source, GLSL ES
// 3.00 source, WGSL source (the sources end in a NUL).
struct ShaderCode {
    std::span<const unsigned char> spirv, msl, glsl, wgsl;
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
constexpr int kMaxTextures = 6;
struct PipelineDesc {
    const ShaderCode* vertex = nullptr;
    const ShaderCode* fragment = nullptr;
    std::vector<VertexAttribute> instanceAttributes;
    uint32_t instanceStride = 0;
    int textures = 0;
    // The slots (bits) whose textures are read texel by texel, with nearest samplers: they may be
    // RGBA32F, which not every GPU filters (WebGPU binds them so).
    uint8_t texelTextures = 0;
    BlendState blend;
    Format target = Format::RGBA8;
    // The fragment shader's specialization constants 0, 1, ... (its [vk::constant_id] ones, all
    // ints): what this pipeline's shader is made for. Those not given keep their defaults.
    std::vector<int32_t> constants;
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
    // The native window that surface is made for, where the system can give the window another
    // (Android's ANativeWindow, a new one each time the app comes back from the background; none
    // while it is away): the surface made again for a new one. Unset: the same for good.
    std::function<void*()> vulkanNativeWindow;
    // OpenGL ES: a function of the context (made current on this thread) by name, and showing
    // what was drawn into the default framebuffer.
    void* (*glGetProcAddress)(const char* name) = nullptr;
    std::function<void()> glSwap;
    // Metal: the layer the window shows (a CAMetalLayer); none to draw only into textures.
    void* metalLayer = nullptr;
    // WebGPU on the web: the canvas it draws into (a CSS selector: "#canvas"); none to draw
    // only into textures.
    std::string webgpuCanvas;
    // The drawable's size in pixels.
    std::function<void(int* width, int* height)> drawableSize;
    // A file where the pipelines' compiled shaders are kept between runs, read when the device is
    // made and written when it has made more (Vulkan's pipeline cache, OpenGL ES's program
    // binaries); none to compile them every run. Metal and the browser keep theirs themselves.
    std::string pipelineCache;
};

// Where the hardware layer's messages go (stderr until the platform says).
void setLogger(void (*logger)(const char* message));
void log(const std::string& message);

// A pipeline cache's file: what was written into it, nothing if there is none or it isn't whole
// (a write cut short, another program's file).
std::vector<uint8_t> readPipelineCache(const std::string& path);
// Writes it whole or not at all (a file beside it, renamed), making its directory.
void writePipelineCache(const std::string& path, const std::vector<uint8_t>& data);
// FNV-1a, 64 bits: what the caches check and key what they keep by.
uint64_t fnv1a(const void* data, size_t size, uint64_t hash = 0xcbf29ce484222325);

// The GPU's time for a frame (Device::setGpuTiming): what it was busy with, its passes' times
// together (not the waits between its submits), and each pass's, named by what it draws into ("window", "1080x2400", "2048x2048 R16F"), or "copy WxH",
// "mipmaps WxH" (OpenGL ES's own copy of the window into its framebuffer: "copy to screen"); on
// Metal the frame alone (its command buffers' times together).
struct GpuPassTime {
    std::string label;
    double ms = 0;
};
struct GpuFrameTime {
    double ms = 0;
    std::vector<GpuPassTime> passes;
};
// A pass's name in a GpuFrameTime.
std::string passLabel(int width, int height, Format format, bool window);

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
    // In clip space y goes up (Metal, OpenGL, WebGPU) or down (Vulkan): the renderer
    // flips its projection for the second.
    virtual bool clipYDown() const = 0;
    // A render target's rows are stored bottom up when sampled (OpenGL): the renderer flips
    // texture coordinates of what it rendered.
    virtual bool renderTargetsBottomUp() const { return false; }

    virtual std::unique_ptr<Texture> createTexture(int width, int height, Format format, uint8_t usage) = 0;
    virtual std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) = 0;
    // Whether createPipeline may be called from another thread while this one goes on (Vulkan:
    // Renderer::warmPipelines makes them there).
    virtual bool makesPipelinesOnAnyThread() const { return false; }
    // The format the swapchain's images have (pipelines drawing into them are made for it).
    virtual Format swapchainFormat() const = 0;

    // Starts a frame (waiting for the GPU to be done with the one before the last). Calls below
    // need a frame begun.
    virtual void beginFrame() = 0;
    // The image presented at the end of the frame, at the drawable's size (turned: see
    // swapchainTurns); nothing if there is nothing to present to now (minimized, the surface lost).
    virtual Texture* swapchainTexture() = 0;
    // How many quarter turns (clockwise) the window's content is drawn turned by into the
    // swapchain's images, which are then the display's own way up (as wide as the drawable is high
    // for an odd number): where the system leaves turning to the app, so that the compositor
    // doesn't turn every frame (Android's Vulkan, the screen turned from its natural way up).
    virtual int swapchainTurns() const { return 0; }
    // Its rows go from the bottom of the window up (OpenGL's default framebuffer): what is drawn
    // into it is drawn upside down, to show the right way up.
    virtual bool swapchainFlipped() const { return false; }
    // How many frames ago the swapchain image swapchainTexture() gave was last presented, holding
    // what was drawn into it then (1: the frame before; drawn over where things have changed
    // since, as Android's renderer does with a buffer's age); 0 if what it holds isn't known (a
    // new one, or a platform that doesn't keep it): all of it drawn over.
    virtual int swapchainAge() const { return 0; }
    // The window's native window went away or is another (Surface::vulkanNativeWindow tells
    // which), between frames: what was made for the old one is let go of now, as the platform
    // must before the system takes it (Android's surfaceDestroyed), not at the next frame.
    virtual void surfaceChanged() {}
    // `size` bytes of this frame's memory (aligned to `alignment`), to write now.
    virtual void* allocate(size_t size, size_t alignment, BufferRef* ref) = 0;
    // Texels (in the texture's format, `rowBytes` apart) into part of a texture; outside passes.
    virtual void upload(Texture& texture, const IRect& region, const void* pixels, size_t rowBytes) = 0;
    // Draws into `target` (cleared first if `clear`), sampling `reads` (each read before by
    // what was drawn into it). A pass starts with nothing bound: its pipeline, uniforms and
    // textures are set in it (as Metal's and WebGPU's encoders have them). With `area`, it draws
    // only there (its scissor stays within it): the rest of the target is kept as it was, and a
    // clear may leave it so; a tiling GPU then loads and stores only that part (Vulkan's render
    // area; OpenGL ES clears only there).
    virtual void beginPass(Texture& target, const std::optional<ClearColor>& clear, std::span<Texture* const> reads,
                           const std::optional<IRect>& area = std::nullopt) = 0;
    virtual void setPipeline(Pipeline& pipeline) = 0;
    virtual void setScissor(const IRect& rect) = 0;
    virtual void setUniforms(const void* data, size_t size) = 0;
    virtual void setTexture(int slot, Texture& texture, const Sampler& sampler) = 0;
    // `instanceCount` instances of `vertexCount` vertices (triangles), their attributes from `instances`.
    virtual void draw(const BufferRef& instances, uint32_t vertexCount, uint32_t instanceCount) = 0;
    virtual void endPass() = 0;
    // A mipmapped texture's smaller copies, each from the one before (filtered), after its
    // pixels are uploaded; outside passes.
    virtual void generateMipmaps(Texture& texture) = 0;
    // What `texture` holds no longer matters: its next pass that doesn't clear needn't load it, a
    // tiling GPU's cheapest start (Vulkan's and Metal's "don't care", OpenGL ES's invalidation;
    // WebGPU, which has none, clears). For a pass that draws over all of it; outside passes.
    virtual void discard(Texture& texture) { (void)texture; }
    // Part of one texture into another of the same format, at (x, y); outside passes.
    virtual void copyTexture(Texture& source, const IRect& region, Texture& destination, int x, int y) = 0;
    // A texture's pixels (RGBA8, or its own format's texels: half floats for RGBA16F; top row
    // first), waiting for what was drawn into it so far.
    virtual std::vector<uint8_t> readPixels(Texture& texture, const IRect& region) = 0;
    // Submits the frame, and shows the swapchain image if `present` (and it was asked for).
    virtual void endFrame(bool present) = 0;
    // Times the GPU's work from the next frame on (or no longer): where the API can (Vulkan's and
    // WebGPU's timestamps, OpenGL ES's timer queries, Metal's command buffers); false if it can't.
    virtual bool setGpuTiming(bool on) {
        (void)on;
        return false;
    }
    // The frames timed (that drew something) whose GPU work is done since the last call, oldest
    // first: a frame or two after their endFrame. A pass the driver timed nonsensically (as
    // Adreno's OpenGL ES ends some before they begin) is left out.
    virtual std::vector<GpuFrameTime> takeGpuTimes() { return {}; }
    // Waits for the GPU to finish everything (before resources it uses go).
    virtual void waitIdle() = 0;
};

} // namespace kompoz::gpu
