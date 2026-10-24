// The hardware layer on Metal (macOS, iOS). Metal's clip space has y up and its textures their
// top row first, as uploads and the renderer have them: nothing is flipped. Each frame records
// one command buffer: uploads, copies and mipmaps in blit encoders, each pass in a render encoder
// (Metal tracks what each reads after what wrote it); readPixels commits what is recorded and
// waits. Per-frame memory is shared buffers the CPU writes, used again once the GPU is done with
// the frame (kFramesInFlight). Shaders are compiled from their MSL source, without fast math (the
// same arithmetic as the other backends). Objective-C++ with ARC: Metal's objects are released as
// the C++ ones holding them go.

#include "device.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace kompoz::gpu {

namespace {

constexpr int kFramesInFlight = 2;
constexpr size_t kChunkSize = 4 << 20;  // a frame's memory comes in pieces this big (or as big as asked)

MTLPixelFormat mtlFormat(Format f) {
    switch (f) {
    case Format::RGBA8: return MTLPixelFormatRGBA8Unorm;
    case Format::BGRA8: return MTLPixelFormatBGRA8Unorm;
    case Format::R16F: return MTLPixelFormatR16Float;
    case Format::RGBA32F: return MTLPixelFormatRGBA32Float;
    case Format::RGBA16F: return MTLPixelFormatRGBA16Float;
    }
    return MTLPixelFormatRGBA8Unorm;
}

MTLVertexFormat mtlVertexFormat(VertexFormat f) {
    switch (f) {
    case VertexFormat::Float: return MTLVertexFormatFloat;
    case VertexFormat::Float2: return MTLVertexFormatFloat2;
    case VertexFormat::Float3: return MTLVertexFormatFloat3;
    case VertexFormat::Float4: return MTLVertexFormatFloat4;
    case VertexFormat::UInt4: return MTLVertexFormatUInt4;
    }
    return MTLVertexFormatFloat4;
}

MTLBlendFactor mtlFactor(BlendFactor f) {
    switch (f) {
    case BlendFactor::Zero: return MTLBlendFactorZero;
    case BlendFactor::One: return MTLBlendFactorOne;
    case BlendFactor::SrcAlpha: return MTLBlendFactorSourceAlpha;
    case BlendFactor::OneMinusSrcAlpha: return MTLBlendFactorOneMinusSourceAlpha;
    case BlendFactor::DstAlpha: return MTLBlendFactorDestinationAlpha;
    case BlendFactor::OneMinusDstAlpha: return MTLBlendFactorOneMinusDestinationAlpha;
    case BlendFactor::SrcColor: return MTLBlendFactorSourceColor;
    case BlendFactor::OneMinusSrcColor: return MTLBlendFactorOneMinusSourceColor;
    case BlendFactor::DstColor: return MTLBlendFactorDestinationColor;
    case BlendFactor::OneMinusDstColor: return MTLBlendFactorOneMinusDestinationColor;
    }
    return MTLBlendFactorOne;
}

MTLBlendOperation mtlOp(BlendOp o) {
    switch (o) {
    case BlendOp::Add: return MTLBlendOperationAdd;
    case BlendOp::Subtract: return MTLBlendOperationSubtract;
    case BlendOp::ReverseSubtract: return MTLBlendOperationReverseSubtract;
    case BlendOp::Min: return MTLBlendOperationMin;
    case BlendOp::Max: return MTLBlendOperationMax;
    }
    return MTLBlendOperationAdd;
}

MTLSamplerAddressMode mtlWrap(Wrap w) {
    switch (w) {
    case Wrap::Clamp: return MTLSamplerAddressModeClampToEdge;
    case Wrap::Repeat: return MTLSamplerAddressModeRepeat;
    case Wrap::Mirror: return MTLSamplerAddressModeMirrorRepeat;
    }
    return MTLSamplerAddressModeClampToEdge;
}

class MetalTexture final : public Texture {
public:
    id<MTLTexture> texture;
    MetalTexture(int width, int height, Format format, id<MTLTexture> t) : Texture(width, height, format), texture(t) {}
};

class MetalPipeline final : public Pipeline {
public:
    id<MTLRenderPipelineState> state;
};

class MetalDevice final : public Device {
public:
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    CAMetalLayer* layer = nil;
    std::function<void(int*, int*)> drawableSize;
    int errors = 0;

    // Per frame: the command buffer committed last (waited for before its memory is written
    // again), and the shared buffers handed out (allocate).
    struct Chunk {
        id<MTLBuffer> buffer;
        size_t used = 0;
    };
    struct Frame {
        id<MTLCommandBuffer> submitted;
        std::vector<Chunk> chunks;
        std::vector<id<MTLCommandBuffer>> timedBuffers;  // all of the frame's, if it was timed and drew something
    };
    // Timed (setGpuTiming): the frame's command buffers so far, and whether it drew something;
    // the frames done since takeGpuTimes. (Its passes aren't timed: Metal's counters would.)
    bool timing = false, drewSomething = false;
    std::vector<id<MTLCommandBuffer>> frameBuffers;
    std::vector<GpuFrameTime> timed;
    Frame frames[kFramesInFlight];
    int frameIndex = 0;
    bool inFrame = false;

    id<MTLCommandBuffer> commands;
    id<MTLRenderCommandEncoder> pass;
    id<MTLBlitCommandEncoder> blit;
    MetalTexture* target = nullptr;
    bool scissorEmpty = false;
    id<CAMetalDrawable> drawable;
    std::unique_ptr<MetalTexture> window;  // the drawable's texture this frame
    struct Bound {
        MetalTexture* texture = nullptr;
        Sampler sampler;
    } bound[kMaxTextures];

    std::vector<std::pair<const unsigned char*, id<MTLLibrary>>> libraries;
    std::vector<std::pair<Sampler, id<MTLSamplerState>>> samplers;

    bool init(const Surface& s, std::string* error) {
        const char* validation = std::getenv("KOMPOZ_GPU_VALIDATION");
        if (validation && std::string(validation) == "1") setenv("MTL_DEBUG_LAYER", "1", 0);  // (read as the device is made)
        const char* wanted = std::getenv("KOMPOZ_GPU_DEVICE");
#if TARGET_OS_OSX
        if (wanted && *wanted)
            for (id<MTLDevice> d in MTLCopyAllDevices())
                if (std::string(d.name.UTF8String).find(wanted) != std::string::npos) device = d;
#endif
        if (!device) device = MTLCreateSystemDefaultDevice();
        if (!device) {
            if (error) *error = "no Metal device";
            return false;
        }
        queue = [device newCommandQueue];
        layer = (__bridge CAMetalLayer*)s.metalLayer;
        drawableSize = s.drawableSize;
        if (layer) {
            layer.device = device;
            layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            layer.framebufferOnly = NO;  // (the renderer copies from it: blend modes read what is below)
        }
        return true;
    }

    ~MetalDevice() override { waitIdle(); }  // (their completion handlers count into this)

    Backend backend() const override { return Backend::Metal; }
    std::string deviceName() const override { return device.name.UTF8String; }
    int validationErrors() const override { return errors; }
    bool clipYDown() const override { return false; }
    Format swapchainFormat() const override { return Format::BGRA8; }

    // --- Resources ---------------------------------------------------------------------------

    std::unique_ptr<Texture> createTexture(int width, int height, Format format, uint8_t usage) override {
        bool mipmapped = usage & TextureUsage::Mipmapped;
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtlFormat(format)
                                                                                     width:NSUInteger(width)
                                                                                    height:NSUInteger(height)
                                                                                 mipmapped:mipmapped ? YES : NO];
        d.storageMode = MTLStorageModePrivate;
        d.usage = MTLTextureUsageShaderRead | (usage & TextureUsage::RenderTarget ? MTLTextureUsageRenderTarget : 0);
        id<MTLTexture> t = [device newTextureWithDescriptor:d];
        if (!t) return nullptr;
        return std::make_unique<MetalTexture>(width, height, format, t);
    }

    // The shader's function, from its library (made once), specialized by `constants` (its
    // function constants 0, 1, ...).
    id<MTLFunction> function(const ShaderCode& code, const std::vector<int32_t>& constants = {}) {
        id<MTLLibrary> library = nil;
        for (auto& [source, made] : libraries)
            if (source == code.msl.data()) library = made;
        if (!library) {
            MTLCompileOptions* options = [MTLCompileOptions new];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            if (@available(macOS 15.0, iOS 18.0, *))
                options.mathMode = MTLMathModeSafe;
            else
                options.fastMathEnabled = NO;
#pragma clang diagnostic pop
            NSError* failure = nil;
            NSString* source = [[NSString alloc] initWithBytes:code.msl.data() length:code.msl.size() - 1 encoding:NSUTF8StringEncoding];
            library = [device newLibraryWithSource:source options:options error:&failure];
            if (!library) {
                log(std::string("Metal: a shader didn't compile: ") + (failure ? failure.localizedDescription.UTF8String : "?"));
                ++errors;
                return nil;
            }
            libraries.emplace_back(code.msl.data(), library);
        }
        if (constants.empty()) return [library newFunctionWithName:@(code.entry)];
        MTLFunctionConstantValues* values = [MTLFunctionConstantValues new];
        for (size_t i = 0; i < constants.size(); ++i) [values setConstantValue:&constants[i] type:MTLDataTypeInt atIndex:i];
        NSError* failure = nil;
        id<MTLFunction> made = [library newFunctionWithName:@(code.entry) constantValues:values error:&failure];
        if (!made) {
            log(std::string("Metal: a shader wasn't specialized: ") + (failure ? failure.localizedDescription.UTF8String : "?"));
            ++errors;
        }
        return made;
    }

    std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override {
        id<MTLFunction> vertex = function(*desc.vertex), fragment = function(*desc.fragment, desc.constants);
        if (!vertex || !fragment) return nullptr;
        MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = vertex;
        d.fragmentFunction = fragment;
        // The instances' attributes from buffer 1 (buffer 0: the uniforms), one step an instance.
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        for (const VertexAttribute& a : desc.instanceAttributes) {
            vd.attributes[a.location].format = mtlVertexFormat(a.format);
            vd.attributes[a.location].offset = a.offset;
            vd.attributes[a.location].bufferIndex = 1;
        }
        vd.layouts[1].stride = desc.instanceStride;
        vd.layouts[1].stepFunction = MTLVertexStepFunctionPerInstance;
        d.vertexDescriptor = vd;
        MTLRenderPipelineColorAttachmentDescriptor* color = d.colorAttachments[0];
        color.pixelFormat = mtlFormat(desc.target);
        color.blendingEnabled = desc.blend.enabled ? YES : NO;
        color.sourceRGBBlendFactor = mtlFactor(desc.blend.srcColor);
        color.destinationRGBBlendFactor = mtlFactor(desc.blend.dstColor);
        color.sourceAlphaBlendFactor = mtlFactor(desc.blend.srcAlpha);
        color.destinationAlphaBlendFactor = mtlFactor(desc.blend.dstAlpha);
        color.rgbBlendOperation = mtlOp(desc.blend.colorOp);
        color.alphaBlendOperation = mtlOp(desc.blend.alphaOp);
        NSError* failure = nil;
        id<MTLRenderPipelineState> state = [device newRenderPipelineStateWithDescriptor:d error:&failure];
        if (!state) {
            log(std::string("Metal: a pipeline wasn't made: ") + (failure ? failure.localizedDescription.UTF8String : "?"));
            ++errors;
            return nullptr;
        }
        auto p = std::make_unique<MetalPipeline>();
        p->state = state;
        return p;
    }

    id<MTLSamplerState> sampler(const Sampler& s) {
        for (auto& [desc, made] : samplers)
            if (desc == s) return made;
        MTLSamplerDescriptor* d = [MTLSamplerDescriptor new];
        MTLSamplerMinMagFilter filter = s.filter == Filter::Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.minFilter = d.magFilter = filter;
        // (Mipmaps: the nearest one, as Skia's FilterQuality::Medium and the other backends take.)
        d.mipFilter = s.mipmaps ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterNotMipmapped;
        d.sAddressMode = mtlWrap(s.wrapU);
        d.tAddressMode = mtlWrap(s.wrapV);
        id<MTLSamplerState> made = [device newSamplerStateWithDescriptor:d];
        samplers.emplace_back(s, made);
        return made;
    }

    // --- Frames ------------------------------------------------------------------------------

    void startCommands() {
        commands = [queue commandBuffer];
        MetalDevice* self = this;
        [commands addCompletedHandler:^(id<MTLCommandBuffer> done) {
          if (done.status == MTLCommandBufferStatusError) {
              log(std::string("Metal: a command buffer failed: ") + (done.error ? done.error.localizedDescription.UTF8String : "?"));
              ++self->errors;
          }
        }];
    }

    void endBlit() {
        if (blit) [blit endEncoding];
        blit = nil;
    }

    id<MTLBlitCommandEncoder> blitEncoder() {
        if (!blit) blit = [commands blitCommandEncoder];
        return blit;
    }

    void beginFrame() override {
        frameIndex = (frameIndex + 1) % kFramesInFlight;
        Frame& f = frames[frameIndex];
        if (f.submitted) [f.submitted waitUntilCompleted];
        f.submitted = nil;
        if (!f.timedBuffers.empty()) {  // (done in order: the last done, all are)
            CFTimeInterval start = f.timedBuffers.front().GPUStartTime, end = f.timedBuffers.back().GPUEndTime;
            if (end > start) timed.push_back({(end - start) * 1000, {}});
            f.timedBuffers.clear();
        }
        drewSomething = false;
        for (Chunk& c : f.chunks) c.used = 0;
        startCommands();
        drawable = nil;
        window.reset();
        inFrame = true;
    }

    Texture* swapchainTexture() override {
        if (!layer) return nullptr;
        if (!drawable) {
            int w = 0, h = 0;
            if (drawableSize) drawableSize(&w, &h);
            if (w <= 0 || h <= 0) return nullptr;  // minimized
            layer.drawableSize = CGSizeMake(w, h);
            drawable = [layer nextDrawable];
            if (!drawable) return nullptr;
            window = std::make_unique<MetalTexture>(int(drawable.texture.width), int(drawable.texture.height), Format::BGRA8, drawable.texture);
        }
        return window.get();
    }

    void* allocate(size_t size, size_t alignment, BufferRef* ref) override {
        Frame& f = frames[frameIndex];
        alignment = std::max<size_t>(alignment, 16);
        for (Chunk& c : f.chunks) {
            size_t start = (c.used + alignment - 1) / alignment * alignment;
            if (start + size <= c.buffer.length) {
                c.used = start + size;
                *ref = {(__bridge const void*)c.buffer, start};
                return static_cast<uint8_t*>(c.buffer.contents) + start;
            }
        }
        Chunk c;
        c.buffer = [device newBufferWithLength:std::max(kChunkSize, size) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        if (!c.buffer) return nullptr;
        c.used = size;
        f.chunks.push_back(c);
        *ref = {(__bridge const void*)c.buffer, 0};
        return c.buffer.contents;
    }

    void upload(Texture& texture, const IRect& region, const void* pixels, size_t rowBytes) override {
        auto& t = static_cast<MetalTexture&>(texture);
        size_t row = size_t(region.width) * bytesPerTexel(t.format()), size = row * size_t(region.height);
        BufferRef ref;
        auto* dst = static_cast<uint8_t*>(allocate(size, 16, &ref));
        if (!dst) return;
        for (int y = 0; y < region.height; ++y) std::memcpy(dst + size_t(y) * row, static_cast<const uint8_t*>(pixels) + size_t(y) * rowBytes, row);
        [blitEncoder() copyFromBuffer:(__bridge id<MTLBuffer>)ref.buffer
                         sourceOffset:ref.offset
                    sourceBytesPerRow:row
                  sourceBytesPerImage:size
                           sourceSize:MTLSizeMake(NSUInteger(region.width), NSUInteger(region.height), 1)
                            toTexture:t.texture
                     destinationSlice:0
                     destinationLevel:0
                    destinationOrigin:MTLOriginMake(NSUInteger(region.x), NSUInteger(region.y), 0)];
    }

    void generateMipmaps(Texture& texture) override {
        auto& t = static_cast<MetalTexture&>(texture);
        if (t.texture.mipmapLevelCount > 1) [blitEncoder() generateMipmapsForTexture:t.texture];
    }

    void copyTexture(Texture& source, const IRect& region, Texture& destination, int x, int y) override {
        [blitEncoder() copyFromTexture:static_cast<MetalTexture&>(source).texture
                           sourceSlice:0
                           sourceLevel:0
                          sourceOrigin:MTLOriginMake(NSUInteger(region.x), NSUInteger(region.y), 0)
                            sourceSize:MTLSizeMake(NSUInteger(region.width), NSUInteger(region.height), 1)
                             toTexture:static_cast<MetalTexture&>(destination).texture
                      destinationSlice:0
                      destinationLevel:0
                     destinationOrigin:MTLOriginMake(NSUInteger(x), NSUInteger(y), 0)];
    }

    void beginPass(Texture& texture, const std::optional<ClearColor>& clear, std::span<Texture* const>) override {
        endBlit();
        drewSomething = true;
        auto& t = static_cast<MetalTexture&>(texture);
        MTLRenderPassDescriptor* d = [MTLRenderPassDescriptor renderPassDescriptor];
        d.colorAttachments[0].texture = t.texture;
        d.colorAttachments[0].loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
        if (clear) d.colorAttachments[0].clearColor = MTLClearColorMake(clear->r, clear->g, clear->b, clear->a);
        d.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass = [commands renderCommandEncoderWithDescriptor:d];
        target = &t;
        for (Bound& b : bound) b = Bound{};
        setScissor({0, 0, t.width(), t.height()});
    }

    void setPipeline(Pipeline& pipeline) override { [pass setRenderPipelineState:static_cast<MetalPipeline&>(pipeline).state]; }

    void setScissor(const IRect& r) override {
        int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
        int x1 = std::min(r.x + r.width, target->width()), y1 = std::min(r.y + r.height, target->height());
        scissorEmpty = x1 <= x0 || y1 <= y0;  // (Metal takes no empty scissor: nothing is drawn instead)
        if (!scissorEmpty) [pass setScissorRect:MTLScissorRect{NSUInteger(x0), NSUInteger(y0), NSUInteger(x1 - x0), NSUInteger(y1 - y0)}];
    }

    void setUniforms(const void* data, size_t size) override {
        size = std::min(size, kMaxUniforms);
        [pass setVertexBytes:data length:size atIndex:0];
        [pass setFragmentBytes:data length:size atIndex:0];
    }

    void setTexture(int slot, Texture& texture, const Sampler& s) override {
        Bound& b = bound[slot];
        if (b.texture == &texture && b.sampler == s) return;
        b = {static_cast<MetalTexture*>(&texture), s};
        [pass setFragmentTexture:b.texture->texture atIndex:NSUInteger(slot)];
        [pass setFragmentSamplerState:sampler(s) atIndex:NSUInteger(slot)];
    }

    void draw(const BufferRef& instances, uint32_t vertexCount, uint32_t instanceCount) override {
        if (instanceCount == 0 || scissorEmpty) return;
        if (instances.buffer) [pass setVertexBuffer:(__bridge id<MTLBuffer>)instances.buffer offset:instances.offset atIndex:1];
        [pass drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertexCount instanceCount:instanceCount];
    }

    void endPass() override {
        [pass endEncoding];
        pass = nil;
        target = nullptr;
    }

    std::vector<uint8_t> readPixels(Texture& texture, const IRect& region) override {
        auto& t = static_cast<MetalTexture&>(texture);
        size_t row = size_t(region.width) * bytesPerTexel(t.format()), size = row * size_t(region.height);
        id<MTLBuffer> buffer = [device newBufferWithLength:std::max<size_t>(size, 1) options:MTLResourceStorageModeShared];
        [blitEncoder() copyFromTexture:t.texture
                           sourceSlice:0
                           sourceLevel:0
                          sourceOrigin:MTLOriginMake(NSUInteger(region.x), NSUInteger(region.y), 0)
                            sourceSize:MTLSizeMake(NSUInteger(region.width), NSUInteger(region.height), 1)
                              toBuffer:buffer
                     destinationOffset:0
                destinationBytesPerRow:row
              destinationBytesPerImage:size];
        endBlit();
        if (timing) frameBuffers.push_back(commands);
        [commands commit];
        [commands waitUntilCompleted];
        std::vector<uint8_t> out(size);
        std::memcpy(out.data(), buffer.contents, size);
        startCommands();  // the rest of the frame
        return out;
    }

    void endFrame(bool present) override {
        if (!inFrame) return;
        endBlit();
        if (present && drawable) [commands presentDrawable:drawable];
        if (timing) frameBuffers.push_back(commands);
        [commands commit];
        frames[frameIndex].submitted = commands;
        if (drewSomething) frames[frameIndex].timedBuffers = std::move(frameBuffers);
        frameBuffers.clear();
        commands = nil;
        drawable = nil;
        window.reset();
        inFrame = false;
    }

    void waitIdle() override {
        for (Frame& f : frames)
            if (f.submitted) [f.submitted waitUntilCompleted];
    }

    bool setGpuTiming(bool on) override {
        timing = on;
        return true;
    }

    std::vector<GpuFrameTime> takeGpuTimes() override { return std::exchange(timed, {}); }
};

} // namespace

std::unique_ptr<Device> createMetalDevice(const Surface& surface, std::string* error) {
    auto device = std::make_unique<MetalDevice>();
    if (!device->init(surface, error)) return nullptr;
    return device;
}

} // namespace kompoz::gpu
