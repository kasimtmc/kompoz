// The OpenGL ES 3 backend: the hardware layer over OpenGL ES 3.0 (and WebGL 2), where there is no
// Vulkan, Metal or WebGPU. The context is the platform's (made current on this thread); its
// functions are looked up through Surface::glGetProcAddress.
//
// It draws as the Vulkan backend does: what it renders lands in a texture top row first, as what
// is uploaded does (clip space taken as y down: the projection flipped, so the top of an image is
// a texture's first row), so a rendered texture, an uploaded one, a copy and a read back all agree;
// scissors, copies and reads take rows from the top. The window's image is drawn into a texture
// of its own and blitted, upside down, into the default framebuffer as the frame ends.

#include "device.hpp"
#include "gles_api.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// The GPU's name where the browser tells it (WebGL's own RENDERER is a placeholder), into `out`.
EM_JS(void, kompoz_webgl_renderer, (char* out, int size), {
    var info = GLctx.getExtension("WEBGL_debug_renderer_info");
    stringToUTF8(info ? String(GLctx.getParameter(info.UNMASKED_RENDERER_WEBGL)) : "", out, size);
});
#endif

namespace kompoz::gpu {

namespace {

GLenum glWrap(Wrap w) {
    switch (w) {
    case Wrap::Clamp: return GL_CLAMP_TO_EDGE;
    case Wrap::Repeat: return GL_REPEAT;
    case Wrap::Mirror: return GL_MIRRORED_REPEAT;
    }
    return GL_CLAMP_TO_EDGE;
}

GLenum glFactor(BlendFactor f) {
    switch (f) {
    case BlendFactor::Zero: return GL_ZERO;
    case BlendFactor::One: return GL_ONE;
    case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha: return GL_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
    case BlendFactor::SrcColor: return GL_SRC_COLOR;
    case BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
    case BlendFactor::DstColor: return GL_DST_COLOR;
    case BlendFactor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
    }
    return GL_ONE;
}

GLenum glOp(BlendOp op) {
    switch (op) {
    case BlendOp::Add: return GL_FUNC_ADD;
    case BlendOp::Subtract: return GL_FUNC_SUBTRACT;
    case BlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
    case BlendOp::Min: return GL_MIN;
    case BlendOp::Max: return GL_MAX;
    }
    return GL_FUNC_ADD;
}

// A format's storage, and the format and type its texels are given and read in.
struct GlFormat {
    GLenum internal, format, type;
};
GlFormat glFormat(Format f) {
    switch (f) {
    case Format::RGBA8:
    case Format::BGRA8: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};  // (BGRA8's texels swapped as they go in and out)
    case Format::R16F: return {GL_R16F, GL_RED, GL_HALF_FLOAT};
    case Format::RGBA32F: return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
    case Format::RGBA16F: return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
    }
    return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
}

// A half float read back as a float (which holds it exactly) as its 16 bits.
uint16_t toHalf(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    uint32_t sign = (bits >> 16) & 0x8000, exponent = (bits >> 23) & 0xff, mantissa = bits & 0x7fffff;
    if (exponent == 0xff) return uint16_t(sign | 0x7c00 | (mantissa ? 0x200 : 0));
    int e = int(exponent) - 127 + 15;
    if (e <= 0) return uint16_t(sign | (e < -10 ? 0 : (mantissa | 0x800000) >> (14 - e)));  // subnormal, or zero
    return uint16_t(sign | uint32_t(e) << 10 | mantissa >> 13);
}

class GlesTexture final : public Texture {
public:
    GlesTexture(Gl& gl, int width, int height, Format format, GLuint name, int levels)
        : Texture(width, height, format), gl(gl), name(name), levels(levels) {}
    ~GlesTexture() override {
        if (framebuffer) gl.DeleteFramebuffers(1, &framebuffer);
        if (name) gl.DeleteTextures(1, &name);
    }
    Gl& gl;
    GLuint name = 0, framebuffer = 0;
    int levels = 1;
};

class GlesPipeline final : public Pipeline {
public:
    GlesPipeline(Gl& gl, GLuint program) : gl(gl), program(program) {}
    ~GlesPipeline() override {
        if (program) gl.DeleteProgram(program);
    }
    Gl& gl;
    GLuint program = 0;
    std::vector<VertexAttribute> attributes;
    uint32_t stride = 0;
    BlendState blend;
};

constexpr size_t kStreamSize = 4 << 20;  // the instance buffer's size each frame starts with

class GlesDevice final : public Device {
public:
    Gl gl;
    std::function<void()> swap;
    std::function<void(int*, int*)> drawableSize;
    std::string renderer;
    bool floatTargets = false;  // half- and full-float textures drawn into (EXT_color_buffer_float or _half_float)
    int errors = 0;

    // Per frame: memory handed out (allocate) on the CPU, copied into the instance buffer as each
    // draw takes it; the uniforms' buffer.
    std::vector<std::vector<uint8_t>> chunks;
    std::vector<size_t> used;
    GLuint stream = 0, uniformBuffer = 0, vertexArray = 0;
    size_t streamSize = 0, streamUsed = 0;

    std::unique_ptr<GlesTexture> window;  // the window's image this frame
    bool inFrame = false, haveWindow = false;
    GlesTexture* target = nullptr;
    GlesPipeline* pipeline = nullptr;
    std::vector<std::pair<Sampler, GLuint>> samplers;
    std::vector<uint32_t> enabledAttributes;
    BlendState blend{};
    bool blendSet = false;

    bool init(const Surface& s, std::string* error) {
        if (!s.glGetProcAddress) {
            if (error) *error = "no OpenGL ES context";
            return false;
        }
        std::string missing;
        if (!gl.load(s.glGetProcAddress, &missing)) {
            if (error) *error = "the OpenGL ES 3.0 function " + missing + " is missing";
            return false;
        }
        const char* version = reinterpret_cast<const char*>(gl.GetString(GL_VERSION));
        if (!version || !std::strstr(version, "OpenGL ES") || std::atoi(std::strstr(version, "OpenGL ES") + 10) < 3) {
            if (error) *error = std::string("not an OpenGL ES 3 context: ") + (version ? version : "?");
            return false;
        }
        int major = std::atoi(std::strstr(version, "OpenGL ES") + 10), minor = 0;
        if (const char* dot = std::strchr(std::strstr(version, "OpenGL ES") + 10, '.')) minor = std::atoi(dot + 1);
        renderer = reinterpret_cast<const char*>(gl.GetString(GL_RENDERER));
#ifdef __EMSCRIPTEN__
        char name[256];
        kompoz_webgl_renderer(name, sizeof name);
        if (name[0]) renderer = name;
#endif
        GLint count = 0;
        gl.GetIntegerv(GL_NUM_EXTENSIONS, &count);
        auto has = [&](const char* name) {
            for (GLint i = 0; i < count; ++i)
                if (std::strcmp(reinterpret_cast<const char*>(gl.GetStringi(GL_EXTENSIONS, GLuint(i))), name) == 0) return true;
            return false;
        };
        bool es32 = major > 3 || (major == 3 && minor >= 2);
        floatTargets = es32 || has("GL_EXT_color_buffer_float") || has("GL_EXT_color_buffer_half_float");
        if (!floatTargets) {
            if (error) *error = "OpenGL ES can't draw into half-float textures here (no EXT_color_buffer_float or _half_float)";
            return false;
        }
        const char* validation = std::getenv("KOMPOZ_GPU_VALIDATION");
        auto debugCallback = has("GL_KHR_debug") && gl.DebugMessageCallbackKHR ? gl.DebugMessageCallbackKHR : es32 ? gl.DebugMessageCallback : nullptr;
        if (validation && std::string(validation) == "1" && debugCallback) {
            gl.Enable(GL_DEBUG_OUTPUT);
            gl.Enable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            debugCallback(debugMessage, this);
        }
        swap = s.glSwap;
        drawableSize = s.drawableSize;
        gl.GenVertexArrays(1, &vertexArray);
        gl.BindVertexArray(vertexArray);
        gl.GenBuffers(1, &stream);
        gl.GenBuffers(1, &uniformBuffer);
        gl.BindBuffer(GL_UNIFORM_BUFFER, uniformBuffer);
        gl.BufferData(GL_UNIFORM_BUFFER, kMaxUniforms, nullptr, GL_DYNAMIC_DRAW);
        gl.BindBufferBase(GL_UNIFORM_BUFFER, 0, uniformBuffer);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl.Disable(GL_DITHER);
        return true;
    }

    // The API's errors (counted, as Vulkan's validation errors are) and what else is severe.
    static void KOMPOZ_GL_API debugMessage(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message, const void* self) {
        if (type == GL_DEBUG_TYPE_ERROR) ++static_cast<GlesDevice*>(const_cast<void*>(self))->errors;
        if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH) log(std::string("OpenGL ES: ") + message);
    }

    ~GlesDevice() override {
        window.reset();
        for (auto& [desc, name] : samplers) gl.DeleteSamplers(1, &name);
        if (stream) gl.DeleteBuffers(1, &stream);
        if (uniformBuffer) gl.DeleteBuffers(1, &uniformBuffer);
        if (vertexArray) gl.DeleteVertexArrays(1, &vertexArray);
    }

    Backend backend() const override { return Backend::GLES; }
    std::string deviceName() const override { return renderer; }
    int validationErrors() const override { return errors; }
    bool clipYDown() const override { return true; }  // (an image's top in a texture's first row: see above)
    Format swapchainFormat() const override { return Format::RGBA8; }

    // --- Resources ---------------------------------------------------------------------------

    std::unique_ptr<Texture> createTexture(int width, int height, Format format, uint8_t usage) override {
        GlFormat f = glFormat(format);
        int levels = usage & TextureUsage::Mipmapped ? int(std::floor(std::log2(std::max(width, height)))) + 1 : 1;
        GLuint name = 0;
        while (gl.GetError() != GL_NO_ERROR) {}  // (an earlier call's error, which isn't this texture's)
        gl.GenTextures(1, &name);
        gl.BindTexture(GL_TEXTURE_2D, name);
        gl.TexStorage2D(GL_TEXTURE_2D, levels, f.internal, width, height);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
        if (gl.GetError() != GL_NO_ERROR) {
            gl.DeleteTextures(1, &name);
            return nullptr;
        }
        return std::make_unique<GlesTexture>(gl, width, height, format, name, levels);
    }

    GLuint shader(GLenum stage, const ShaderCode& code) {
        GLuint s = gl.CreateShader(stage);
        const char* source = reinterpret_cast<const char*>(code.glsl.data());
        gl.ShaderSource(s, 1, &source, nullptr);
        gl.CompileShader(s);
        GLint ok = 0;
        gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLint length = 0;
            gl.GetShaderiv(s, GL_INFO_LOG_LENGTH, &length);
            std::string message(size_t(std::max(length, 1)), '\0');
            gl.GetShaderInfoLog(s, length, nullptr, message.data());
            log("OpenGL ES: a shader didn't compile: " + message);
            ++errors;
            gl.DeleteShader(s);
            return 0;
        }
        return s;
    }

    std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override {
        GLuint vs = shader(GL_VERTEX_SHADER, *desc.vertex), fs = shader(GL_FRAGMENT_SHADER, *desc.fragment);
        if (!vs || !fs) {
            if (vs) gl.DeleteShader(vs);
            if (fs) gl.DeleteShader(fs);
            return nullptr;
        }
        GLuint program = gl.CreateProgram();
        gl.AttachShader(program, vs);
        gl.AttachShader(program, fs);
        gl.LinkProgram(program);
        gl.DeleteShader(vs);
        gl.DeleteShader(fs);
        GLint ok = 0;
        gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            GLint length = 0;
            gl.GetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string message(size_t(std::max(length, 1)), '\0');
            gl.GetProgramInfoLog(program, length, nullptr, message.data());
            log("OpenGL ES: a program didn't link: " + message);
            ++errors;
            gl.DeleteProgram(program);
            return nullptr;
        }
        // Each sampler from the unit it is named after. (Its uniform block is at binding 0, where
        // linking puts every block.)
        gl.UseProgram(program);
        for (int unit = 0; unit < kMaxTextures; ++unit) {
            std::string name = "kompozTexture" + std::to_string(unit);
            GLint location = gl.GetUniformLocation(program, name.c_str());
            if (location >= 0) gl.Uniform1i(location, unit);
        }
        gl.UseProgram(pipeline ? pipeline->program : 0);
        auto p = std::make_unique<GlesPipeline>(gl, program);
        p->attributes = desc.instanceAttributes;
        p->stride = desc.instanceStride;
        p->blend = desc.blend;
        return p;
    }

    GLuint sampler(const Sampler& s) {
        for (auto& [desc, name] : samplers)
            if (desc == s) return name;
        GLuint name = 0;
        gl.GenSamplers(1, &name);
        bool linear = s.filter == Filter::Linear;
        // (Mipmaps: the nearest one, as Skia's FilterQuality::Medium and the Vulkan backend take.)
        gl.SamplerParameteri(name, GL_TEXTURE_MIN_FILTER,
                             s.mipmaps ? (linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST) : (linear ? GL_LINEAR : GL_NEAREST));
        gl.SamplerParameteri(name, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        gl.SamplerParameteri(name, GL_TEXTURE_WRAP_S, GLint(glWrap(s.wrapU)));
        gl.SamplerParameteri(name, GL_TEXTURE_WRAP_T, GLint(glWrap(s.wrapV)));
        samplers.emplace_back(s, name);
        return name;
    }

    // A texture's framebuffer (made the first time it is drawn into, read from or copied).
    GLuint framebuffer(GlesTexture& t) {
        if (!t.framebuffer) {
            gl.GenFramebuffers(1, &t.framebuffer);
            gl.BindFramebuffer(GL_FRAMEBUFFER, t.framebuffer);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.name, 0);
            if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                log("OpenGL ES: a texture can't be drawn into");
                ++errors;
            }
        }
        return t.framebuffer;
    }

    // --- Frames ------------------------------------------------------------------------------

    void beginFrame() override {
        inFrame = true;
        haveWindow = false;
        for (size_t& u : used) u = 0;
        // A new instance buffer each frame (the driver keeps the old one while the GPU reads it).
        streamSize = std::max(streamSize, kStreamSize);
        gl.BindBuffer(GL_ARRAY_BUFFER, stream);
        gl.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(streamSize), nullptr, GL_STREAM_DRAW);
        streamUsed = 0;
    }

    Texture* swapchainTexture() override {
        if (haveWindow) return window.get();
        int w = 0, h = 0;
        if (drawableSize) drawableSize(&w, &h);
        if (w <= 0 || h <= 0) return nullptr;  // minimized
        if (!window || window->width() != w || window->height() != h) {
            auto t = createTexture(w, h, Format::RGBA8, TextureUsage::Sampled | TextureUsage::RenderTarget);
            window.reset(static_cast<GlesTexture*>(t.release()));
        }
        haveWindow = window != nullptr;
        return window.get();
    }

    void* allocate(size_t size, size_t alignment, BufferRef* ref) override {
        alignment = std::max<size_t>(alignment, 16);
        for (size_t i = 0; i < chunks.size(); ++i) {
            size_t start = (used[i] + alignment - 1) / alignment * alignment;
            if (start + size <= chunks[i].size()) {
                used[i] = start + size;
                *ref = {chunks[i].data(), start};
                return chunks[i].data() + start;
            }
        }
        chunks.emplace_back(std::max(kStreamSize, size));
        used.push_back(size);
        *ref = {chunks.back().data(), 0};
        return chunks.back().data();
    }

    void upload(Texture& texture, const IRect& region, const void* pixels, size_t rowBytes) override {
        auto& t = static_cast<GlesTexture&>(texture);
        GlFormat f = glFormat(t.format());
        size_t texel = bytesPerTexel(t.format());
        std::vector<uint8_t> swapped;
        if (t.format() == Format::BGRA8) {  // (stored as RGBA8)
            swapped.resize(size_t(region.width) * size_t(region.height) * 4);
            for (int y = 0; y < region.height; ++y) {
                const auto* src = static_cast<const uint8_t*>(pixels) + size_t(y) * rowBytes;
                uint8_t* out = swapped.data() + size_t(y) * size_t(region.width) * 4;
                for (int x = 0; x < region.width; ++x)
                    out[x * 4] = src[x * 4 + 2], out[x * 4 + 1] = src[x * 4 + 1], out[x * 4 + 2] = src[x * 4], out[x * 4 + 3] = src[x * 4 + 3];
            }
            pixels = swapped.data();
            rowBytes = size_t(region.width) * 4;
        }
        gl.BindTexture(GL_TEXTURE_2D, t.name);
        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, GLint(rowBytes / texel));
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, region.x, region.y, region.width, region.height, f.format, f.type, pixels);
        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    void generateMipmaps(Texture& texture) override {
        auto& t = static_cast<GlesTexture&>(texture);
        if (t.levels <= 1) return;
        gl.BindTexture(GL_TEXTURE_2D, t.name);
        gl.GenerateMipmap(GL_TEXTURE_2D);
    }

    void beginPass(Texture& texture, const std::optional<ClearColor>& clear, std::span<Texture* const>) override {
        auto& t = static_cast<GlesTexture&>(texture);
        gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer(t));
        gl.Viewport(0, 0, t.width(), t.height());
        target = &t;
        if (clear) {  // (all of it, as a render pass clears: not cut by a scissor)
            gl.Disable(GL_SCISSOR_TEST);
            gl.ClearColor(clear->r, clear->g, clear->b, clear->a);
            gl.Clear(GL_COLOR_BUFFER_BIT);
        }
        gl.Enable(GL_SCISSOR_TEST);
        setScissor({0, 0, t.width(), t.height()});
    }

    void setPipeline(Pipeline& p) override {
        auto& gp = static_cast<GlesPipeline&>(p);
        if (&gp != pipeline) {
            gl.UseProgram(gp.program);
            pipeline = &gp;
        }
        if (!blendSet || !(gp.blend == blend)) {
            const BlendState& b = gp.blend;
            if (b.enabled) {
                gl.Enable(GL_BLEND);
                gl.BlendFuncSeparate(glFactor(b.srcColor), glFactor(b.dstColor), glFactor(b.srcAlpha), glFactor(b.dstAlpha));
                gl.BlendEquationSeparate(glOp(b.colorOp), glOp(b.alphaOp));
            } else {
                gl.Disable(GL_BLEND);
            }
            blend = b;
            blendSet = true;
        }
    }

    void setScissor(const IRect& r) override {
        int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
        int x1 = target ? std::min(r.x + r.width, target->width()) : r.x + r.width;
        int y1 = target ? std::min(r.y + r.height, target->height()) : r.y + r.height;
        gl.Scissor(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));  // (rows from the top: see above)
    }

    void setUniforms(const void* data, size_t size) override {
        gl.BindBuffer(GL_UNIFORM_BUFFER, uniformBuffer);
        gl.BufferSubData(GL_UNIFORM_BUFFER, 0, GLsizeiptr(std::min(size, kMaxUniforms)), data);
    }

    void setTexture(int slot, Texture& texture, const Sampler& s) override {
        auto& t = static_cast<GlesTexture&>(texture);
        gl.ActiveTexture(GLenum(GL_TEXTURE0 + slot));
        gl.BindTexture(GL_TEXTURE_2D, t.name);
        gl.BindSampler(GLuint(slot), sampler(s));
    }

    void draw(const BufferRef& instances, uint32_t vertexCount, uint32_t instanceCount) override {
        if (instanceCount == 0 || !pipeline) return;
        std::vector<uint32_t> enable;
        if (pipeline->stride && instances.buffer) {
            // The instances into the frame's instance buffer, after those drawn before.
            size_t size = size_t(pipeline->stride) * instanceCount;
            if (streamUsed + size > streamSize) {
                streamSize = std::max(streamSize * 2, size);
                gl.BindBuffer(GL_ARRAY_BUFFER, stream);
                gl.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(streamSize), nullptr, GL_STREAM_DRAW);
                streamUsed = 0;
            }
            gl.BindBuffer(GL_ARRAY_BUFFER, stream);
            gl.BufferSubData(GL_ARRAY_BUFFER, GLintptr(streamUsed), GLsizeiptr(size), static_cast<const uint8_t*>(instances.buffer) + instances.offset);
            for (const VertexAttribute& a : pipeline->attributes) {
                const void* at = reinterpret_cast<const void*>(uintptr_t(streamUsed + a.offset));
                GLsizei stride = GLsizei(pipeline->stride);
                switch (a.format) {
                case VertexFormat::Float: gl.VertexAttribPointer(a.location, 1, GL_FLOAT, GL_FALSE, stride, at); break;
                case VertexFormat::Float2: gl.VertexAttribPointer(a.location, 2, GL_FLOAT, GL_FALSE, stride, at); break;
                case VertexFormat::Float3: gl.VertexAttribPointer(a.location, 3, GL_FLOAT, GL_FALSE, stride, at); break;
                case VertexFormat::Float4: gl.VertexAttribPointer(a.location, 4, GL_FLOAT, GL_FALSE, stride, at); break;
                case VertexFormat::UInt4: gl.VertexAttribIPointer(a.location, 4, GL_UNSIGNED_INT, stride, at); break;
                }
                gl.VertexAttribDivisor(a.location, 1);
                enable.push_back(a.location);
            }
            streamUsed += (size + 255) / 256 * 256;
        }
        for (uint32_t l : enabledAttributes)
            if (std::find(enable.begin(), enable.end(), l) == enable.end()) gl.DisableVertexAttribArray(l);
        for (uint32_t l : enable)
            if (std::find(enabledAttributes.begin(), enabledAttributes.end(), l) == enabledAttributes.end()) gl.EnableVertexAttribArray(l);
        enabledAttributes = std::move(enable);
        gl.DrawArraysInstanced(GL_TRIANGLES, 0, GLsizei(vertexCount), GLsizei(instanceCount));
    }

    void endPass() override { target = nullptr; }

    void copyTexture(Texture& source, const IRect& region, Texture& destination, int x, int y) override {
        auto& src = static_cast<GlesTexture&>(source);
        auto& dst = static_cast<GlesTexture&>(destination);
        GLuint read = framebuffer(src), draw = framebuffer(dst);
        gl.BindFramebuffer(GL_READ_FRAMEBUFFER, read);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
        gl.Disable(GL_SCISSOR_TEST);
        gl.BlitFramebuffer(region.x, region.y, region.x + region.width, region.y + region.height, x, y, x + region.width, y + region.height,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    std::vector<uint8_t> readPixels(Texture& texture, const IRect& region) override {
        auto& t = static_cast<GlesTexture&>(texture);
        gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer(t));
        size_t n = size_t(region.width) * size_t(region.height);
        std::vector<uint8_t> out(n * bytesPerTexel(t.format()));
        if (t.format() == Format::RGBA8 || t.format() == Format::BGRA8) {
            gl.ReadPixels(region.x, region.y, region.width, region.height, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
            if (t.format() == Format::BGRA8)
                for (size_t i = 0; i < out.size(); i += 4) std::swap(out[i], out[i + 2]);
        } else {  // (floats: RGBA and FLOAT are what every float target can be read as)
            std::vector<float> rgba(n * 4);
            gl.ReadPixels(region.x, region.y, region.width, region.height, GL_RGBA, GL_FLOAT, rgba.data());
            if (t.format() == Format::RGBA32F) {
                std::memcpy(out.data(), rgba.data(), out.size());
            } else {
                int channels = t.format() == Format::R16F ? 1 : 4;
                auto* halves = reinterpret_cast<uint16_t*>(out.data());
                for (size_t i = 0; i < n; ++i)
                    for (int c = 0; c < channels; ++c) halves[i * size_t(channels) + size_t(c)] = toHalf(rgba[i * 4 + size_t(c)]);
            }
        }
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return out;
    }

    void endFrame(bool present) override {
        if (!inFrame) return;
        if (present && haveWindow && window) {
            // The window's image into the default framebuffer, upside down (its first row is the top).
            gl.BindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer(*window));
            gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            gl.Disable(GL_SCISSOR_TEST);
            int w = window->width(), h = window->height();
            gl.BlitFramebuffer(0, 0, w, h, 0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
            if (swap) swap();
        } else {
            gl.Flush();
        }
        inFrame = false;
        haveWindow = false;
    }

    void waitIdle() override { gl.Finish(); }
};

} // namespace

std::unique_ptr<Device> createGlesDevice(const Surface& surface, std::string* error) {
    auto device = std::make_unique<GlesDevice>();
    if (!device->init(surface, error)) return nullptr;
    return device;
}

} // namespace kompoz::gpu
