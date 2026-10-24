// The hardware layer on WebGPU (webgpu.h: Dawn's emdawnwebgpu on the web). Its clip space has y up
// and its textures their top row first, as uploads and the renderer have them: nothing is
// flipped. Each frame records one command encoder: uploads (staged in the frame's memory, rows
// 256 bytes apart as WebGPU wants) and copies between passes, each pass a render pass. The frame's
// memory is CPU memory mirrored by GPU buffers, written to them (queue writes, which come before
// the commands submitted after them) just before the commands are submitted. Uniforms live there
// too, each draw's at its own dynamic offset of the bind group. Mipmaps are drawn, a level from
// the one before. readPixels submits and waits for the copy to be mapped (wgpuInstanceWaitAny: on
// the web through ASYNCIFY). WebGPU validates everything: its errors are counted.

#include "device.hpp"

#include <webgpu/webgpu.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace kompoz::gpu {

namespace {

constexpr size_t kChunkSize = 4 << 20;   // a frame's memory comes in pieces this big (or as big as asked)
constexpr uint64_t kUniformAlign = 256;  // (WebGPU's least minUniformBufferOffsetAlignment)
constexpr uint32_t kRowAlign = 256;      // a buffer's rows in copies to and from textures

WGPUStringView view(std::string_view s) { return {s.data(), s.size()}; }
std::string text(WGPUStringView s) { return s.data ? std::string(s.data, s.length == WGPU_STRLEN ? std::strlen(s.data) : s.length) : std::string(); }

WGPUTextureFormat wgpuFormat(Format f) {
    switch (f) {
    case Format::RGBA8: return WGPUTextureFormat_RGBA8Unorm;
    case Format::BGRA8: return WGPUTextureFormat_BGRA8Unorm;
    case Format::R16F: return WGPUTextureFormat_R16Float;
    case Format::RGBA32F: return WGPUTextureFormat_RGBA32Float;
    case Format::RGBA16F: return WGPUTextureFormat_RGBA16Float;
    }
    return WGPUTextureFormat_RGBA8Unorm;
}

WGPUVertexFormat wgpuVertexFormat(VertexFormat f) {
    switch (f) {
    case VertexFormat::Float: return WGPUVertexFormat_Float32;
    case VertexFormat::Float2: return WGPUVertexFormat_Float32x2;
    case VertexFormat::Float3: return WGPUVertexFormat_Float32x3;
    case VertexFormat::Float4: return WGPUVertexFormat_Float32x4;
    case VertexFormat::UInt4: return WGPUVertexFormat_Uint32x4;
    }
    return WGPUVertexFormat_Float32x4;
}

WGPUBlendFactor wgpuFactor(BlendFactor f) {
    switch (f) {
    case BlendFactor::Zero: return WGPUBlendFactor_Zero;
    case BlendFactor::One: return WGPUBlendFactor_One;
    case BlendFactor::SrcAlpha: return WGPUBlendFactor_SrcAlpha;
    case BlendFactor::OneMinusSrcAlpha: return WGPUBlendFactor_OneMinusSrcAlpha;
    case BlendFactor::DstAlpha: return WGPUBlendFactor_DstAlpha;
    case BlendFactor::OneMinusDstAlpha: return WGPUBlendFactor_OneMinusDstAlpha;
    case BlendFactor::SrcColor: return WGPUBlendFactor_Src;
    case BlendFactor::OneMinusSrcColor: return WGPUBlendFactor_OneMinusSrc;
    case BlendFactor::DstColor: return WGPUBlendFactor_Dst;
    case BlendFactor::OneMinusDstColor: return WGPUBlendFactor_OneMinusDst;
    }
    return WGPUBlendFactor_One;
}

WGPUBlendOperation wgpuOp(BlendOp o) {
    switch (o) {
    case BlendOp::Add: return WGPUBlendOperation_Add;
    case BlendOp::Subtract: return WGPUBlendOperation_Subtract;
    case BlendOp::ReverseSubtract: return WGPUBlendOperation_ReverseSubtract;
    case BlendOp::Min: return WGPUBlendOperation_Min;
    case BlendOp::Max: return WGPUBlendOperation_Max;
    }
    return WGPUBlendOperation_Add;
}

WGPUAddressMode wgpuWrap(Wrap w) {
    switch (w) {
    case Wrap::Clamp: return WGPUAddressMode_ClampToEdge;
    case Wrap::Repeat: return WGPUAddressMode_Repeat;
    case Wrap::Mirror: return WGPUAddressMode_MirrorRepeat;
    }
    return WGPUAddressMode_ClampToEdge;
}

// Mipmaps: each level drawn from the one before, its texels the four below it averaged (a
// triangle over the target, sampled bilinearly at each texel's middle).
constexpr const char* kMipmapShader = R"(
@group(0) @binding(0) var source : texture_2d<f32>;
@group(0) @binding(1) var bilinear : sampler;
struct Varyings { @builtin(position) position : vec4f, @location(0) uv : vec2f };
@vertex fn vs(@builtin(vertex_index) i : u32) -> Varyings {
    let corner = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    var out : Varyings;
    out.position = vec4f(corner.x * 2 - 1, 1 - corner.y * 2, 0, 1);
    out.uv = corner;
    return out;
}
@fragment fn fs(v : Varyings) -> @location(0) vec4f { return textureSampleLevel(source, bilinear, v.uv, 0); }
)";

class WebGPUTexture final : public Texture {
public:
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    bool owned = true;  // (the surface's texture isn't: it is the surface's)
    uint64_t serial;    // which texture it is (one made after another is gone may be where it was)
    WebGPUTexture(int width, int height, Format format, WGPUTexture t, bool own) : Texture(width, height, format), texture(t), owned(own) {
        static uint64_t made = 0;
        serial = ++made;
        view = wgpuTextureCreateView(t, nullptr);
    }
    ~WebGPUTexture() override {
        wgpuTextureViewRelease(view);
        if (owned) wgpuTextureRelease(texture);
    }
};

class WebGPUPipeline final : public Pipeline {
public:
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroupLayout layout = nullptr;
    int textures = 0;
    ~WebGPUPipeline() override {
        wgpuRenderPipelineRelease(pipeline);
        wgpuBindGroupLayoutRelease(layout);
    }
};

class WebGPUDevice final : public Device {
public:
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUSurface surface = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
    int surfaceWidth = 0, surfaceHeight = 0;
    std::function<void(int*, int*)> drawableSize;
    std::string name;
    int errors = 0;
    // Timed (setGpuTiming, where the adapter has timestamp queries): a frame's passes' starts and
    // ends written into a query set, resolved into a buffer and read back once done; a few frames'
    // sets in turn (a frame goes untimed while all are being read). Copies aren't timed: WebGPU
    // times passes only.
    struct TimedFrame {
        WGPUQuerySet queries = nullptr;
        WGPUBuffer resolved = nullptr, read = nullptr;
        std::vector<std::string> labels;  // pass i's: queries 2i and 2i + 1
        enum State { Free, Writing, Mapping, Mapped, Failed } state = Free;
    };
    static constexpr uint32_t kTimedPasses = 256;
    bool timestamps = false, timing = false;
    std::array<TimedFrame, 4> timedFrames;
    TimedFrame* timedNow = nullptr;
    WGPUPassTimestampWrites passTimes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    std::vector<GpuFrameTime> timed;

    // The frame's memory: CPU pieces, each mirrored by a GPU buffer written up to where it is used
    // (flush) before commands are submitted.
    struct Chunk {
        WGPUBuffer buffer = nullptr;
        std::vector<uint8_t> memory;
        size_t used = 0, flushed = 0;
    };
    std::vector<Chunk> chunks;

    WGPUCommandEncoder encoder = nullptr;
    WGPURenderPassEncoder pass = nullptr;
    WebGPUTexture* target = nullptr;
    std::unique_ptr<WebGPUTexture> window;  // the surface's texture this frame
    bool inFrame = false;

    // What the next draw binds: the pipeline, the uniforms (a frame memory buffer, at an offset),
    // the textures and samplers; the bind group for them made once a frame (bindGroup).
    WebGPUPipeline* pipeline = nullptr;
    WGPUBuffer uniformBuffer = nullptr;
    uint32_t uniformOffset = 0;
    struct Bound {
        WebGPUTexture* texture = nullptr;
        Sampler sampler;
    } bound[kMaxTextures];
    struct Group {
        WGPUBindGroupLayout layout;
        WGPUBuffer uniforms;
        uint64_t textures[kMaxTextures];  // (their serials)
        Sampler samplers[kMaxTextures];
        WGPUBindGroup group;
    };
    std::vector<Group> groups;

    std::vector<std::pair<const unsigned char*, WGPUShaderModule>> modules;
    std::vector<std::pair<Sampler, WGPUSampler>> samplers;
    WGPUShaderModule mipmapModule = nullptr;
    WGPUBindGroupLayout mipmapLayout = nullptr;
    std::vector<std::pair<WGPUTextureFormat, WGPURenderPipeline>> mipmapPipelines;
    WGPUSampler bilinear = nullptr;

    // Waits for a future (adapter, device, mapping) to complete.
    void wait(WGPUFuture future) {
        WGPUFutureWaitInfo info{future, false};
        wgpuInstanceWaitAny(instance, 1, &info, UINT64_MAX);
    }

    bool init(const Surface& s, std::string* error) {
        WGPUInstanceFeatureName timedWait = WGPUInstanceFeatureName_TimedWaitAny;
        WGPUInstanceDescriptor id = WGPU_INSTANCE_DESCRIPTOR_INIT;
        id.requiredFeatureCount = 1;
        id.requiredFeatures = &timedWait;
        instance = wgpuCreateInstance(&id);
        if (!instance) {
            if (error) *error = "no WebGPU";
            return false;
        }
        drawableSize = s.drawableSize;

        WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        WGPURequestAdapterCallbackInfo adapterCallback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
        adapterCallback.mode = WGPUCallbackMode_WaitAnyOnly;
        adapterCallback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter a, WGPUStringView message, void* self, void*) {
            if (status == WGPURequestAdapterStatus_Success)
                static_cast<WebGPUDevice*>(self)->adapter = a;
            else
                log("WebGPU: no adapter: " + text(message));
        };
        adapterCallback.userdata1 = this;
        wait(wgpuInstanceRequestAdapter(instance, &options, adapterCallback));
        if (!adapter) {
            if (error) *error = "no WebGPU adapter";
            return false;
        }
        WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
        if (wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success) {
            name = text(info.description);
            if (name.empty()) name = text(info.vendor) + (info.architecture.length ? " " + text(info.architecture) : "") +
                                     (info.device.length ? " " + text(info.device) : "");
            wgpuAdapterInfoFreeMembers(info);
        }

        WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
        WGPUFeatureName timestampQuery = WGPUFeatureName_TimestampQuery;
        timestamps = wgpuAdapterHasFeature(adapter, timestampQuery);
        if (timestamps) {
            dd.requiredFeatureCount = 1;
            dd.requiredFeatures = &timestampQuery;
        }
        dd.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType, WGPUStringView message, void* self, void*) {
            ++static_cast<WebGPUDevice*>(self)->errors;
            log("WebGPU: " + text(message));
        };
        dd.uncapturedErrorCallbackInfo.userdata1 = this;
        WGPURequestDeviceCallbackInfo deviceCallback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
        deviceCallback.mode = WGPUCallbackMode_WaitAnyOnly;
        deviceCallback.callback = [](WGPURequestDeviceStatus status, WGPUDevice d, WGPUStringView message, void* self, void*) {
            if (status == WGPURequestDeviceStatus_Success)
                static_cast<WebGPUDevice*>(self)->device = d;
            else
                log("WebGPU: no device: " + text(message));
        };
        deviceCallback.userdata1 = this;
        wait(wgpuAdapterRequestDevice(adapter, &dd, deviceCallback));
        if (!device) {
            if (error) *error = "no WebGPU device";
            return false;
        }
        queue = wgpuDeviceGetQueue(device);
        // The canvas's surface only now that there is a device: taken by WebGPU, a canvas can't
        // be WebGL's any more (where a backend after this one would draw).
#ifdef __EMSCRIPTEN__
        if (!s.webgpuCanvas.empty()) {
            WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
            canvas.selector = view(s.webgpuCanvas);
            WGPUSurfaceDescriptor sd = WGPU_SURFACE_DESCRIPTOR_INIT;
            sd.nextInChain = &canvas.chain;
            surface = wgpuInstanceCreateSurface(instance, &sd);
        }
#endif
        if (surface) {
            WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
            if (wgpuSurfaceGetCapabilities(surface, adapter, &caps) == WGPUStatus_Success && caps.formatCount > 0)
                for (size_t i = caps.formatCount; i-- > 0;)  // (the first BGRA8 or RGBA8 it prefers)
                    if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm || caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) surfaceFormat = caps.formats[i];
            wgpuSurfaceCapabilitiesFreeMembers(caps);
        }
        return true;
    }

    ~WebGPUDevice() override {
        window.reset();
        for (TimedFrame& f : timedFrames) {
            if (f.state == TimedFrame::Mapped) wgpuBufferUnmap(f.read);
            if (f.queries) wgpuQuerySetRelease(f.queries);
            if (f.resolved) wgpuBufferRelease(f.resolved);
            if (f.read) wgpuBufferRelease(f.read);
        }
        for (Group& g : groups) wgpuBindGroupRelease(g.group);
        for (auto& [code, m] : modules) wgpuShaderModuleRelease(m);
        for (auto& [desc, s] : samplers) wgpuSamplerRelease(s);
        for (auto& [format, p] : mipmapPipelines) wgpuRenderPipelineRelease(p);
        if (mipmapLayout) wgpuBindGroupLayoutRelease(mipmapLayout);
        if (mipmapModule) wgpuShaderModuleRelease(mipmapModule);
        if (bilinear) wgpuSamplerRelease(bilinear);
        for (Chunk& c : chunks) wgpuBufferRelease(c.buffer);
        if (encoder) wgpuCommandEncoderRelease(encoder);
        if (surface) wgpuSurfaceRelease(surface);
        if (queue) wgpuQueueRelease(queue);
        if (device) wgpuDeviceRelease(device);
        if (adapter) wgpuAdapterRelease(adapter);
        if (instance) wgpuInstanceRelease(instance);
    }

    Backend backend() const override { return Backend::WebGPU; }
    std::string deviceName() const override { return name; }
    int validationErrors() const override { return errors; }
    bool clipYDown() const override { return false; }
    Format swapchainFormat() const override { return surfaceFormat == WGPUTextureFormat_RGBA8Unorm ? Format::RGBA8 : Format::BGRA8; }

    // --- Resources ---------------------------------------------------------------------------

    std::unique_ptr<Texture> createTexture(int width, int height, Format format, uint8_t usage) override {
        WGPUTextureDescriptor d = WGPU_TEXTURE_DESCRIPTOR_INIT;
        d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
        if (usage & (TextureUsage::RenderTarget | TextureUsage::Mipmapped)) d.usage |= WGPUTextureUsage_RenderAttachment;  // (mipmaps are drawn)
        d.size = {uint32_t(width), uint32_t(height), 1};
        d.format = wgpuFormat(format);
        d.mipLevelCount = usage & TextureUsage::Mipmapped ? uint32_t(std::bit_width(unsigned(std::max(width, height)))) : 1;
        WGPUTexture t = wgpuDeviceCreateTexture(device, &d);
        if (!t) return nullptr;
        return std::make_unique<WebGPUTexture>(width, height, format, t, true);
    }

    WGPUShaderModule module(const ShaderCode& code) {
        for (auto& [source, m] : modules)
            if (source == code.wgsl.data()) return m;
        WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
        wgsl.code = view(std::string_view(reinterpret_cast<const char*>(code.wgsl.data()), code.wgsl.size() - 1));
        WGPUShaderModuleDescriptor d = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        d.nextInChain = &wgsl.chain;
        WGPUShaderModule m = wgpuDeviceCreateShaderModule(device, &d);
        modules.emplace_back(code.wgsl.data(), m);
        return m;
    }

    // The bind group's layout: the uniforms (binding 0, at a dynamic offset), then each texture
    // slot's texture and sampler (bindings 2i + 1 and 2i + 2, as scripts/shaders.py binds them).
    WGPUBindGroupLayout bindGroupLayout(int textures, uint8_t texel) {
        std::vector<WGPUBindGroupLayoutEntry> entries;
        WGPUBindGroupLayoutEntry u = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        u.binding = 0;
        u.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        u.buffer.type = WGPUBufferBindingType_Uniform;
        u.buffer.hasDynamicOffset = true;
        u.buffer.minBindingSize = kMaxUniforms;
        entries.push_back(u);
        for (int i = 0; i < textures; ++i) {
            bool byTexel = texel & (1 << i);
            WGPUBindGroupLayoutEntry t = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            t.binding = uint32_t(2 * i + 1);
            t.visibility = WGPUShaderStage_Fragment;
            t.texture.sampleType = byTexel ? WGPUTextureSampleType_UnfilterableFloat : WGPUTextureSampleType_Float;
            t.texture.viewDimension = WGPUTextureViewDimension_2D;
            entries.push_back(t);
            WGPUBindGroupLayoutEntry s = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            s.binding = uint32_t(2 * i + 2);
            s.visibility = WGPUShaderStage_Fragment;
            s.sampler.type = byTexel ? WGPUSamplerBindingType_NonFiltering : WGPUSamplerBindingType_Filtering;
            entries.push_back(s);
        }
        WGPUBindGroupLayoutDescriptor d = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        d.entryCount = entries.size();
        d.entries = entries.data();
        return wgpuDeviceCreateBindGroupLayout(device, &d);
    }

    std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override {
        auto p = std::make_unique<WebGPUPipeline>();
        p->textures = desc.textures;
        p->layout = bindGroupLayout(desc.textures, desc.texelTextures);
        WGPUPipelineLayoutDescriptor ld = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        ld.bindGroupLayoutCount = 1;
        ld.bindGroupLayouts = &p->layout;
        WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &ld);

        std::vector<WGPUVertexAttribute> attributes;
        for (const VertexAttribute& a : desc.instanceAttributes) {
            WGPUVertexAttribute va = WGPU_VERTEX_ATTRIBUTE_INIT;
            va.format = wgpuVertexFormat(a.format);
            va.offset = a.offset;
            va.shaderLocation = a.location;
            attributes.push_back(va);
        }
        WGPUVertexBufferLayout instances = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        instances.stepMode = WGPUVertexStepMode_Instance;
        instances.arrayStride = desc.instanceStride;
        instances.attributeCount = attributes.size();
        instances.attributes = attributes.data();

        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        blend.color = {wgpuOp(desc.blend.colorOp), wgpuFactor(desc.blend.srcColor), wgpuFactor(desc.blend.dstColor)};
        blend.alpha = {wgpuOp(desc.blend.alphaOp), wgpuFactor(desc.blend.srcAlpha), wgpuFactor(desc.blend.dstAlpha)};
        WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
        color.format = wgpuFormat(desc.target);
        color.blend = desc.blend.enabled ? &blend : nullptr;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = module(*desc.fragment);
        fragment.entryPoint = view(desc.fragment->entry);
        fragment.targetCount = 1;
        fragment.targets = &color;
        // (WGSL's overrides, named by their @id.)
        std::vector<std::string> keys;
        for (size_t i = 0; i < desc.constants.size(); ++i) keys.push_back(std::to_string(i));
        std::vector<WGPUConstantEntry> constants(desc.constants.size(), WGPU_CONSTANT_ENTRY_INIT);
        for (size_t i = 0; i < constants.size(); ++i) constants[i].key = view(keys[i].c_str()), constants[i].value = desc.constants[i];
        fragment.constantCount = constants.size();
        fragment.constants = constants.data();

        WGPURenderPipelineDescriptor d = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        d.layout = layout;
        d.vertex.module = module(*desc.vertex);
        d.vertex.entryPoint = view(desc.vertex->entry);
        d.vertex.bufferCount = desc.instanceStride ? 1 : 0;
        d.vertex.buffers = &instances;
        d.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        d.fragment = &fragment;
        p->pipeline = wgpuDeviceCreateRenderPipeline(device, &d);
        wgpuPipelineLayoutRelease(layout);
        if (!p->pipeline) return nullptr;
        return p;
    }

    WGPUSampler sampler(const Sampler& s) {
        for (auto& [desc, made] : samplers)
            if (desc == s) return made;
        WGPUSamplerDescriptor d = WGPU_SAMPLER_DESCRIPTOR_INIT;
        d.addressModeU = wgpuWrap(s.wrapU);
        d.addressModeV = wgpuWrap(s.wrapV);
        d.magFilter = d.minFilter = s.filter == Filter::Linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
        // (Mipmaps: the nearest one, as Skia's FilterQuality::Medium and the other backends take.)
        d.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        d.lodMaxClamp = s.mipmaps ? 32 : 0;
        WGPUSampler made = wgpuDeviceCreateSampler(device, &d);
        samplers.emplace_back(s, made);
        return made;
    }

    // --- Frames ------------------------------------------------------------------------------

    void startCommands() { encoder = wgpuDeviceCreateCommandEncoder(device, nullptr); }

    // The frame's memory written to its buffers, the commands so far submitted.
    void submit() {
        for (Chunk& c : chunks) {
            if (c.used > c.flushed) {
                size_t from = c.flushed & ~size_t(3), to = (c.used + 3) & ~size_t(3);  // (writes are 4-byte aligned)
                wgpuQueueWriteBuffer(queue, c.buffer, from, c.memory.data() + from, to - from);
            }
            c.flushed = c.used;
        }
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuCommandEncoderRelease(encoder);
        encoder = nullptr;
        wgpuQueueSubmit(queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
    }

    void beginFrame() override {
        collectTimes();
        timedNow = nullptr;
        if (timing)
            for (TimedFrame& f : timedFrames)
                if (f.state == TimedFrame::Free) {
                    f.state = TimedFrame::Writing;
                    f.labels.clear();
                    timedNow = &f;
                    break;
                }
        for (Chunk& c : chunks) c.used = c.flushed = 0;
        for (Group& g : groups) wgpuBindGroupRelease(g.group);
        groups.clear();
        window.reset();
        startCommands();
        inFrame = true;
    }

    Texture* swapchainTexture() override {
        if (!surface) return nullptr;
        if (!window) {
            int w = 0, h = 0;
            if (drawableSize) drawableSize(&w, &h);
            if (w <= 0 || h <= 0) return nullptr;  // minimized
            if (w != surfaceWidth || h != surfaceHeight) {
                WGPUSurfaceConfiguration c = WGPU_SURFACE_CONFIGURATION_INIT;
                c.device = device;
                c.format = surfaceFormat;
                c.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;  // (the renderer copies from it)
                c.width = uint32_t(w);
                c.height = uint32_t(h);
                c.alphaMode = WGPUCompositeAlphaMode_Opaque;
                c.presentMode = WGPUPresentMode_Fifo;
                wgpuSurfaceConfigure(surface, &c);
                surfaceWidth = w, surfaceHeight = h;
            }
            WGPUSurfaceTexture st = WGPU_SURFACE_TEXTURE_INIT;
            wgpuSurfaceGetCurrentTexture(surface, &st);
            if (!st.texture || (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
                                st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)) {
                surfaceWidth = 0;  // (configured again next frame)
                return nullptr;
            }
            window = std::make_unique<WebGPUTexture>(int(wgpuTextureGetWidth(st.texture)), int(wgpuTextureGetHeight(st.texture)),
                                                     swapchainFormat(), st.texture, false);
        }
        return window.get();
    }

    void* allocate(size_t size, size_t alignment, BufferRef* ref) override {
        alignment = std::max<size_t>(alignment, 16);
        for (Chunk& c : chunks) {
            size_t start = (c.used + alignment - 1) / alignment * alignment;
            if (start + size <= c.memory.size()) {
                c.used = start + size;
                *ref = {c.buffer, start};
                return c.memory.data() + start;
            }
        }
        Chunk c;
        c.memory.resize((std::max(kChunkSize, size) + 3) & ~size_t(3));
        WGPUBufferDescriptor d = WGPU_BUFFER_DESCRIPTOR_INIT;
        d.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_Uniform | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        d.size = c.memory.size();
        c.buffer = wgpuDeviceCreateBuffer(device, &d);
        if (!c.buffer) return nullptr;
        c.used = size;
        chunks.push_back(std::move(c));
        *ref = {chunks.back().buffer, 0};
        return chunks.back().memory.data();
    }

    void upload(Texture& texture, const IRect& region, const void* pixels, size_t rowBytes) override {
        auto& t = static_cast<WebGPUTexture&>(texture);
        size_t row = size_t(region.width) * bytesPerTexel(t.format());
        size_t stride = (row + kRowAlign - 1) / kRowAlign * kRowAlign;
        BufferRef ref;
        auto* dst = static_cast<uint8_t*>(allocate(stride * size_t(region.height), kRowAlign, &ref));
        if (!dst) return;
        for (int y = 0; y < region.height; ++y) std::memcpy(dst + size_t(y) * stride, static_cast<const uint8_t*>(pixels) + size_t(y) * rowBytes, row);
        WGPUTexelCopyBufferInfo source = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        source.buffer = static_cast<WGPUBuffer>(const_cast<void*>(ref.buffer));
        source.layout.offset = ref.offset;
        source.layout.bytesPerRow = uint32_t(stride);
        source.layout.rowsPerImage = uint32_t(region.height);
        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = t.texture;
        destination.origin = {uint32_t(region.x), uint32_t(region.y), 0};
        WGPUExtent3D size{uint32_t(region.width), uint32_t(region.height), 1};
        wgpuCommandEncoderCopyBufferToTexture(encoder, &source, &destination, &size);
    }

    void generateMipmaps(Texture& texture) override {
        auto& t = static_cast<WebGPUTexture&>(texture);
        uint32_t levels = wgpuTextureGetMipLevelCount(t.texture);
        if (levels <= 1) return;
        WGPUTextureFormat format = wgpuFormat(t.format());
        WGPURenderPipeline drawn = nullptr;
        for (auto& [f, p] : mipmapPipelines)
            if (f == format) drawn = p;
        if (!drawn) {
            if (!mipmapModule) {
                WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
                wgsl.code = view(kMipmapShader);
                WGPUShaderModuleDescriptor md = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
                md.nextInChain = &wgsl.chain;
                mipmapModule = wgpuDeviceCreateShaderModule(device, &md);
                WGPUBindGroupLayoutEntry entries[2] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
                entries[0].binding = 0;
                entries[0].visibility = WGPUShaderStage_Fragment;
                entries[0].texture.sampleType = WGPUTextureSampleType_Float;
                entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
                entries[1].binding = 1;
                entries[1].visibility = WGPUShaderStage_Fragment;
                entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
                WGPUBindGroupLayoutDescriptor ld = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
                ld.entryCount = 2;
                ld.entries = entries;
                mipmapLayout = wgpuDeviceCreateBindGroupLayout(device, &ld);
                bilinear = sampler(Sampler{Filter::Linear});
                wgpuSamplerAddRef(bilinear);  // (released on its own, as the cache's are)
            }
            WGPUPipelineLayoutDescriptor pld = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
            pld.bindGroupLayoutCount = 1;
            pld.bindGroupLayouts = &mipmapLayout;
            WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &pld);
            WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
            color.format = format;
            WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
            fragment.module = mipmapModule;
            fragment.entryPoint = view("fs");
            fragment.targetCount = 1;
            fragment.targets = &color;
            WGPURenderPipelineDescriptor d = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            d.layout = layout;
            d.vertex.module = mipmapModule;
            d.vertex.entryPoint = view("vs");
            d.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            d.fragment = &fragment;
            drawn = wgpuDeviceCreateRenderPipeline(device, &d);
            wgpuPipelineLayoutRelease(layout);
            mipmapPipelines.emplace_back(format, drawn);
        }
        for (uint32_t level = 1; level < levels; ++level) {
            WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            vd.baseMipLevel = level - 1;
            vd.mipLevelCount = 1;
            WGPUTextureView from = wgpuTextureCreateView(t.texture, &vd);
            vd.baseMipLevel = level;
            WGPUTextureView to = wgpuTextureCreateView(t.texture, &vd);
            WGPUBindGroupEntry entries[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
            entries[0].binding = 0;
            entries[0].textureView = from;
            entries[1].binding = 1;
            entries[1].sampler = bilinear;
            WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bd.layout = mipmapLayout;
            bd.entryCount = 2;
            bd.entries = entries;
            WGPUBindGroup group = wgpuDeviceCreateBindGroup(device, &bd);
            WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            attachment.view = to;
            attachment.loadOp = WGPULoadOp_Clear;
            attachment.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor pd = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            pd.colorAttachmentCount = 1;
            pd.colorAttachments = &attachment;
            pd.timestampWrites = timePass("mipmaps " + std::to_string(t.width()) + "x" + std::to_string(t.height()));
            WGPURenderPassEncoder p = wgpuCommandEncoderBeginRenderPass(encoder, &pd);
            wgpuRenderPassEncoderSetPipeline(p, drawn);
            wgpuRenderPassEncoderSetBindGroup(p, 0, group, 0, nullptr);
            wgpuRenderPassEncoderDraw(p, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(p);
            wgpuRenderPassEncoderRelease(p);
            wgpuBindGroupRelease(group);
            wgpuTextureViewRelease(from);
            wgpuTextureViewRelease(to);
        }
    }

    void copyTexture(Texture& source, const IRect& region, Texture& destination, int x, int y) override {
        WGPUTexelCopyTextureInfo from = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT, to = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        from.texture = static_cast<WebGPUTexture&>(source).texture;
        from.origin = {uint32_t(region.x), uint32_t(region.y), 0};
        to.texture = static_cast<WebGPUTexture&>(destination).texture;
        to.origin = {uint32_t(x), uint32_t(y), 0};
        WGPUExtent3D size{uint32_t(region.width), uint32_t(region.height), 1};
        wgpuCommandEncoderCopyTextureToTexture(encoder, &from, &to, &size);
    }

    void beginPass(Texture& texture, const std::optional<ClearColor>& clear, std::span<Texture* const>) override {
        auto& t = static_cast<WebGPUTexture&>(texture);
        WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        attachment.view = t.view;
        attachment.loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
        attachment.storeOp = WGPUStoreOp_Store;
        if (clear) attachment.clearValue = {clear->r, clear->g, clear->b, clear->a};
        WGPURenderPassDescriptor d = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        d.colorAttachmentCount = 1;
        d.colorAttachments = &attachment;
        d.timestampWrites = timePass(passLabel(t.width(), t.height(), t.format(), &t == window.get()));
        pass = wgpuCommandEncoderBeginRenderPass(encoder, &d);
        target = &t;
        pipeline = nullptr;
        uniformBuffer = nullptr;
        for (Bound& b : bound) b = Bound{};
        setScissor({0, 0, t.width(), t.height()});
    }

    void setPipeline(Pipeline& p) override {
        auto* next = static_cast<WebGPUPipeline*>(&p);
        if (next == pipeline) return;
        pipeline = next;
        wgpuRenderPassEncoderSetPipeline(pass, pipeline->pipeline);
    }

    void setScissor(const IRect& r) override {
        int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
        int x1 = std::max(x0, std::min(r.x + r.width, target->width())), y1 = std::max(y0, std::min(r.y + r.height, target->height()));
        wgpuRenderPassEncoderSetScissorRect(pass, uint32_t(x0), uint32_t(y0), uint32_t(x1 - x0), uint32_t(y1 - y0));
    }

    void setUniforms(const void* data, size_t size) override {
        BufferRef ref;
        auto* dst = static_cast<uint8_t*>(allocate(kMaxUniforms, kUniformAlign, &ref));
        if (!dst) return;
        std::memset(dst, 0, kMaxUniforms);
        std::memcpy(dst, data, std::min(size, kMaxUniforms));
        uniformBuffer = static_cast<WGPUBuffer>(const_cast<void*>(ref.buffer));
        uniformOffset = uint32_t(ref.offset);
    }

    void setTexture(int slot, Texture& texture, const Sampler& s) override {
        bound[slot] = {static_cast<WebGPUTexture*>(&texture), s};
    }

    // The bind group for what is bound now: one made earlier this frame for the same, or a new one.
    WGPUBindGroup bindGroup() {
        Bound textures[kMaxTextures];
        Group key{pipeline->layout, uniformBuffer, {}, {}, nullptr};
        for (int i = 0; i < pipeline->textures; ++i) {
            textures[i] = bound[i].texture ? bound[i] : bound[0];  // (unused slots: any texture)
            if (!textures[i].texture) return nullptr;
            key.textures[i] = textures[i].texture->serial;
            key.samplers[i] = textures[i].sampler;
        }
        for (const Group& g : groups)
            if (g.layout == key.layout && g.uniforms == key.uniforms && std::equal(g.textures, g.textures + pipeline->textures, key.textures) &&
                std::equal(g.samplers, g.samplers + pipeline->textures, key.samplers))
                return g.group;
        std::vector<WGPUBindGroupEntry> entries;
        WGPUBindGroupEntry u = WGPU_BIND_GROUP_ENTRY_INIT;
        u.binding = 0;
        u.buffer = uniformBuffer;
        u.size = kMaxUniforms;
        entries.push_back(u);
        for (int i = 0; i < pipeline->textures; ++i) {
            WGPUBindGroupEntry t = WGPU_BIND_GROUP_ENTRY_INIT;
            t.binding = uint32_t(2 * i + 1);
            t.textureView = textures[i].texture->view;
            entries.push_back(t);
            WGPUBindGroupEntry s = WGPU_BIND_GROUP_ENTRY_INIT;
            s.binding = uint32_t(2 * i + 2);
            s.sampler = sampler(textures[i].sampler);
            entries.push_back(s);
        }
        WGPUBindGroupDescriptor d = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        d.layout = pipeline->layout;
        d.entryCount = entries.size();
        d.entries = entries.data();
        key.group = wgpuDeviceCreateBindGroup(device, &d);
        groups.push_back(key);
        return key.group;
    }

    void draw(const BufferRef& instances, uint32_t vertexCount, uint32_t instanceCount) override {
        if (instanceCount == 0 || !pipeline || !uniformBuffer) return;
        WGPUBindGroup group = bindGroup();
        if (!group) return;
        wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &uniformOffset);
        if (instances.buffer)
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, static_cast<WGPUBuffer>(const_cast<void*>(instances.buffer)), instances.offset, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDraw(pass, vertexCount, instanceCount, 0, 0);
    }

    void endPass() override {
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        pass = nullptr;
        target = nullptr;
    }

    std::vector<uint8_t> readPixels(Texture& texture, const IRect& region) override {
        auto& t = static_cast<WebGPUTexture&>(texture);
        size_t row = size_t(region.width) * bytesPerTexel(t.format());
        size_t stride = (row + kRowAlign - 1) / kRowAlign * kRowAlign;
        WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
        bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        bd.size = std::max<size_t>(stride * size_t(region.height), 4);
        WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &bd);
        WGPUTexelCopyTextureInfo from = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        from.texture = t.texture;
        from.origin = {uint32_t(region.x), uint32_t(region.y), 0};
        WGPUTexelCopyBufferInfo to = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        to.buffer = buffer;
        to.layout.bytesPerRow = uint32_t(stride);
        to.layout.rowsPerImage = uint32_t(region.height);
        WGPUExtent3D size{uint32_t(region.width), uint32_t(region.height), 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &from, &to, &size);
        submit();
        bool mapped = false;
        WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_WaitAnyOnly;
        callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView message, void* done, void*) {
            *static_cast<bool*>(done) = status == WGPUMapAsyncStatus_Success;
            if (status != WGPUMapAsyncStatus_Success) log("WebGPU: a readback wasn't mapped: " + text(message));
        };
        callback.userdata1 = &mapped;
        wait(wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, size_t(bd.size), callback));
        std::vector<uint8_t> out(row * size_t(region.height));
        if (mapped) {
            auto* src = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(buffer, 0, size_t(bd.size)));
            for (int y = 0; y < region.height; ++y) std::memcpy(out.data() + size_t(y) * row, src + size_t(y) * stride, row);
            wgpuBufferUnmap(buffer);
        }
        wgpuBufferRelease(buffer);
        startCommands();  // the rest of the frame
        return out;
    }

    void endFrame(bool present) override {
        if (!inFrame) return;
        uint32_t times = timedNow ? uint32_t(timedNow->labels.size()) * 2 : 0;
        if (times) {
            wgpuCommandEncoderResolveQuerySet(encoder, timedNow->queries, 0, times, timedNow->resolved, 0);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, timedNow->resolved, 0, timedNow->read, 0, uint64_t(times) * 8);
        }
        submit();
        if (times) {
            timedNow->state = TimedFrame::Mapping;
            WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
            callback.mode = WGPUCallbackMode_AllowProcessEvents;
            callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* frame, void*) {
                static_cast<TimedFrame*>(frame)->state = status == WGPUMapAsyncStatus_Success ? TimedFrame::Mapped : TimedFrame::Failed;
            };
            callback.userdata1 = timedNow;
            wgpuBufferMapAsync(timedNow->read, WGPUMapMode_Read, 0, size_t(times) * 8, callback);
        } else if (timedNow) {
            timedNow->state = TimedFrame::Free;
        }
        timedNow = nullptr;
#ifndef __EMSCRIPTEN__  // (on the web the canvas shows what was drawn when the frame's callback returns)
        if (present && window) wgpuSurfacePresent(surface);
#else
        (void)present;
#endif
        window.reset();
        inFrame = false;
    }

    bool setGpuTiming(bool on) override {
        if (on && !timestamps) return false;
        timing = on;
        for (TimedFrame& f : timedFrames)
            if (on && !f.queries) {
                WGPUQuerySetDescriptor qd = WGPU_QUERY_SET_DESCRIPTOR_INIT;
                qd.type = WGPUQueryType_Timestamp;
                qd.count = kTimedPasses * 2;
                f.queries = wgpuDeviceCreateQuerySet(device, &qd);
                WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
                bd.size = uint64_t(kTimedPasses) * 16;
                bd.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
                f.resolved = wgpuDeviceCreateBuffer(device, &bd);
                bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
                f.read = wgpuDeviceCreateBuffer(device, &bd);
            }
        return true;
    }

    std::vector<GpuFrameTime> takeGpuTimes() override { return std::exchange(timed, {}); }

    // The timestamps for a pass of this frame's, if it is timed (and has room for them).
    const WGPUPassTimestampWrites* timePass(std::string label) {
        if (!timedNow || timedNow->labels.size() >= kTimedPasses) return nullptr;
        passTimes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        passTimes.querySet = timedNow->queries;
        passTimes.beginningOfPassWriteIndex = uint32_t(timedNow->labels.size()) * 2;
        passTimes.endOfPassWriteIndex = passTimes.beginningOfPassWriteIndex + 1;
        timedNow->labels.push_back(std::move(label));
        return &passTimes;
    }

    // The frames read back since: each pass from its start to its end, the frame from its first
    // pass's start to its last's end (nanoseconds; a browser may round them).
    void collectTimes() {
        if (std::any_of(timedFrames.begin(), timedFrames.end(), [](const TimedFrame& f) { return f.state == TimedFrame::Mapping; }))
            wgpuInstanceProcessEvents(instance);
        for (TimedFrame& f : timedFrames) {
            if (f.state == TimedFrame::Failed) f.state = TimedFrame::Free;
            if (f.state != TimedFrame::Mapped) continue;
            size_t n = f.labels.size() * 2;
            auto* t = static_cast<const uint64_t*>(wgpuBufferGetConstMappedRange(f.read, 0, n * 8));
            if (t) {
                GpuFrameTime frame;
                uint64_t first = UINT64_MAX, last = 0;
                for (size_t i = 0; i < f.labels.size(); ++i) {
                    uint64_t a = t[2 * i], b = std::max(t[2 * i + 1], a);
                    frame.passes.push_back({f.labels[i], double(b - a) / 1e6});
                    first = std::min(first, a), last = std::max(last, b);
                }
                frame.ms = double(last - first) / 1e6;
                timed.push_back(std::move(frame));
            }
            wgpuBufferUnmap(f.read);
            f.state = TimedFrame::Free;
        }
    }

    void waitIdle() override {
        if (!queue) return;
        WGPUQueueWorkDoneCallbackInfo callback = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_WaitAnyOnly;
        callback.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void*, void*) {};
        wait(wgpuQueueOnSubmittedWorkDone(queue, callback));
    }
};

} // namespace

std::unique_ptr<Device> createWebGPUDevice(const Surface& surface, std::string* error) {
    auto device = std::make_unique<WebGPUDevice>();
    if (!device->init(surface, error)) return nullptr;
    return device;
}

} // namespace kompoz::gpu
