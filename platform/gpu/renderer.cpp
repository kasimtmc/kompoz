// The GPU renderer (see renderer.hpp).

#include "renderer.hpp"

#include "shaders/generated/effects.h"
#include "shaders/generated/path.h"
#include "shaders/generated/primitives.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace kompoz::gpu {
namespace {

// shaders/primitives.slang's kinds.
enum Kind { FILL = 0, STROKE = 1, LINE = 2, ARC = 3, SHADOW = 4, GLYPH = 5, IMAGE = 6, LAYER = 7, PATH = 8, ELEVATION = 9, MESH = 10 };

constexpr int kMaxLayer = 4096;  // px: a bigger layer is drawn straight into its target

const ShaderCode kVertex{shaders::primitives_vs_spirv, shaders::primitives_vs_msl,
                         shaders::primitives_vs_glsl, shaders::primitives_vs_wgsl, "vs"};
const ShaderCode kFragment{shaders::primitives_fs_spirv, shaders::primitives_fs_msl,
                           shaders::primitives_fs_glsl, shaders::primitives_fs_wgsl, "fs"};
const ShaderCode kMeshVertex{shaders::primitives_vsMesh_spirv, shaders::primitives_vsMesh_msl,
                             shaders::primitives_vsMesh_glsl, shaders::primitives_vsMesh_wgsl, "vsMesh"};
const ShaderCode kCoverageVertex{shaders::path_vs_spirv, shaders::path_vs_msl, shaders::path_vs_glsl,
                                 shaders::path_vs_wgsl, "vs"};
const ShaderCode kCoverageFragment{shaders::path_fs_spirv, shaders::path_fs_msl, shaders::path_fs_glsl,
                                   shaders::path_fs_wgsl, "fs"};
const ShaderCode kEffectVertex{shaders::effects_vs_spirv, shaders::effects_vs_msl, shaders::effects_vs_glsl,
                               shaders::effects_vs_wgsl, "vs"};
const ShaderCode kEffectFragment{shaders::effects_fs_spirv, shaders::effects_fs_msl, shaders::effects_fs_glsl,
                                 shaders::effects_fs_wgsl, "fs"};

constexpr int kMaskSize = 2048;  // a generation's mask is at least this big each way (px), and at least its target's size

void set(float (&out)[4], float a, float b, float c, float d) {
    out[0] = a, out[1] = b, out[2] = c, out[3] = d;
}
void premultiplied(float (&out)[4], Color c) { set(out, c.r * c.a, c.g * c.a, c.b * c.a, c.a); }

IRect intersect(const IRect& a, const IRect& b) {
    int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    int x1 = std::min(a.x + a.width, b.x + b.width), y1 = std::min(a.y + a.height, b.y + b.height);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

} // namespace

Renderer::Renderer(Device& device, TextSource& text) : device_(device), text_(text) {
    white_ = device_.createTexture(1, 1, Format::RGBA8, TextureUsage::Sampled);
}

Renderer::~Renderer() { device_.waitIdle(); }

Pipeline& Renderer::pipeline(Format target, const BlendState& blend, bool mesh) {
    PipelineKey key{target, blend, mesh};
    for (auto& [k, p] : pipelines_)
        if (k == key) return *p;
    PipelineDesc desc;
    desc.vertex = mesh ? &kMeshVertex : &kVertex;
    desc.fragment = &kFragment;
    for (uint32_t i = 0; i < 12; ++i) desc.instanceAttributes.push_back({i, VertexFormat::Float4, i * 16});
    desc.instanceStride = sizeof(Prim);
    desc.textures = 6;
    desc.blend = blend;
    desc.target = target;
    pipelines_.emplace_back(key, device_.createPipeline(desc));
    return *pipelines_.back().second;
}

namespace {

// How a blend mode is drawn: by the blend unit (`state`), the shader putting the coverage in as
// `how` says (see shaders/primitives.slang's finish), or worked out in the shader from a copy of
// the target (`readsTarget`, the blend unit off).
struct BlendPlan {
    BlendState state;
    int how = 0;
    bool readsTarget = false;
};
BlendPlan planFor(BlendMode mode, bool opaque) {
    using F = BlendFactor;
    auto unit = [](F src, F dst, F srcA, F dstA, int how = 0) { return BlendPlan{{true, src, dst, srcA, dstA}, how}; };
    if (opaque) return {{false}};
    switch (mode) {
    // The coverage multiplied in mixes these with what is there exactly.
    case BlendMode::SrcOver: return unit(F::One, F::OneMinusSrcAlpha, F::One, F::OneMinusSrcAlpha);
    case BlendMode::DstOver: return unit(F::OneMinusDstAlpha, F::One, F::OneMinusDstAlpha, F::One);
    case BlendMode::SrcAtop: return unit(F::DstAlpha, F::OneMinusSrcAlpha, F::DstAlpha, F::OneMinusSrcAlpha);
    case BlendMode::DstOut: return unit(F::Zero, F::OneMinusSrcAlpha, F::Zero, F::OneMinusSrcAlpha);
    case BlendMode::Xor: return unit(F::OneMinusDstAlpha, F::OneMinusSrcAlpha, F::OneMinusDstAlpha, F::OneMinusSrcAlpha);
    case BlendMode::Plus: return unit(F::One, F::One, F::One, F::One);
    case BlendMode::Screen: return unit(F::One, F::OneMinusSrcColor, F::One, F::OneMinusSrcAlpha);
    // These mix towards a source that changes nothing; Clear by the coverage alone.
    case BlendMode::Modulate: return unit(F::Zero, F::SrcColor, F::Zero, F::SrcAlpha, 1);
    case BlendMode::DstIn: return unit(F::Zero, F::SrcAlpha, F::Zero, F::SrcAlpha, 2);
    case BlendMode::Clear: return unit(F::Zero, F::OneMinusSrcAlpha, F::Zero, F::OneMinusSrcAlpha, 3);
    default: return {{false}, 4, true};  // Src, SrcIn, SrcOut, DstAtop and the separable and non-separable modes
    }
}

} // namespace

// --- Targets -------------------------------------------------------------------------------------

void Renderer::beginTarget(Texture& target, const std::optional<ClearColor>& clear) {
    targets_.push_back({passes_.size(), ox_, oy_, transform_, windowFromTarget_, std::move(clips_), std::move(placements_), std::move(blends_),
                        std::move(filters_)});
    passes_.push_back({&target, clear, {}, {}});
    ox_ = oy_ = 0;
    transform_ = {};
    windowFromTarget_ = {};  // (a layer's: set by pushLayer)
    clips_.clear();
    placements_.clear();
    blends_.clear();  // (what is drawn into it starts plain: its composite gets the modes and filters outside)
    filters_.clear();
}

void Renderer::endTarget() {
    Target t = std::move(targets_.back());
    targets_.pop_back();
    order_.push_back(t.pass);
    ox_ = t.ox, oy_ = t.oy;
    transform_ = t.transform;
    windowFromTarget_ = t.windowFromTarget;
    clips_ = std::move(t.clips);
    placements_ = std::move(t.placements);
    blends_ = std::move(t.blends);
    filters_ = std::move(t.filters);
}

void Renderer::pushBlendMode(BlendMode mode) { blends_.push_back(mode); }
void Renderer::popBlendMode() {
    if (!blends_.empty()) blends_.pop_back();
}
void Renderer::pushColorFilter(const ColorFilter& filter) { filters_.push_back(filter); }
void Renderer::popColorFilter() {
    if (!filters_.empty()) filters_.pop_back();
}

IRect Renderer::scissor() const {
    if (targets_.empty()) return {};
    const Texture& t = *passes_[targets_.back().pass].target;
    IRect all{0, 0, t.width(), t.height()};
    return clips_.empty() ? all : intersect(clips_.back().scissor, all);
}

float Renderer::rasterScale(float x, float y) const {
    if (transform_.identity()) return 1;
    float s = transform_.maxScale(x, y);
    if (!(s > 0) || std::abs(s - 1) < 1e-3f) return 1;
    return std::clamp(std::exp2(std::ceil(std::log2(s) * 8 - 1e-3f) / 8), 1.f / 16, 16.f);
}

std::optional<Rect> Renderer::mapped(float x0, float y0, float x1, float y1) const {
    float l = 1e30f, t = 1e30f, r = -1e30f, b = -1e30f;
    for (int c = 0; c < 4; ++c) {
        float w;
        Offset q = transform_.apply(c & 1 ? x1 : x0, c & 2 ? y1 : y0, &w);
        if (w <= 0) return std::nullopt;
        l = std::min(l, q.x), t = std::min(t, q.y), r = std::max(r, q.x), b = std::max(b, q.y);
    }
    return Rect{l, t, r - l, b - t};
}

int Renderer::clipRow(int chain) {
    ClipChain& c = clipChains_[size_t(chain)];
    MaskShelf& shelf = passes_[targets_.back().pass].shelf;
    if (c.row >= 0 && (!c.paths || (c.generation == shelf.generation && c.pass == targets_.back().pass))) return c.row;
    // Its paths in this generation's coverage mask, all of them (one that starts a new generation
    // starts them all over in it).
    std::vector<Offset> origins(c.elements.size());
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint32_t generation = shelf.generation;
        for (size_t i = 0; i < c.elements.size(); ++i)
            if (c.elements[i].polygons) origins[i] = rasterize(*c.elements[i].polygons, c.elements[i].region).value_or(Offset{});
        if (shelf.generation == generation) break;
    }
    c.generation = shelf.generation;
    c.pass = targets_.back().pass;
    c.row = int(stops_.size() / (kStopsWidth * 4));
    size_t at = stops_.size();
    stops_.resize(at + kStopsWidth * 4, 0.f);
    for (size_t i = 0; i < c.elements.size() && i < kStopsWidth / 6; ++i) {  // six texels each (see clipCoverage)
        const ClipElement& e = c.elements[i];
        float* t = &stops_[at + i * 24];
        t[0] = e.polygons ? 1.f : 0.f;
        t[1] = e.outside ? 1.f : 0.f;
        t[2] = e.evenOdd ? 1.f : 0.f;
        if (e.polygons) {
            t[4] = origins[i].x, t[5] = origins[i].y;
            const IRect& r = e.region;
            t[8] = float(r.x), t[9] = float(r.y), t[10] = float(r.x + r.width), t[11] = float(r.y + r.height);
        } else {
            std::copy(std::begin(e.rect), std::end(e.rect), t + 4);
            std::copy(std::begin(e.radii), std::end(e.radii), t + 8);
            for (int r = 0; r < 3; ++r) std::copy(e.toLocal.m + r * 3, e.toLocal.m + r * 3 + 3, t + 12 + r * 4);
        }
    }
    return c.row;
}

std::optional<Offset> Renderer::rasterizeInClips(const std::vector<Contour>& contours, IRect region) {
    int chain = clips_.empty() ? -1 : clips_.back().chain;
    MaskShelf& shelf = passes_[targets_.back().pass].shelf;
    std::optional<Offset> origin;
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (chain >= 0) clipRow(chain);
        uint32_t generation = shelf.generation;
        origin = rasterize(contours, region);
        if (shelf.generation == generation) break;  // (else in a new one: the clips' paths go there too)
    }
    return origin;
}

void Renderer::add(int kind, const Prim& prim, Texture* texture, bool opaque, Mask mask, Texture* texture1, bool inTargetPixels, Sampler sampler) {
    if (targets_.empty()) return;  // (drawn outside beginTarget/endTarget)
    Pass& pass = passes_[targets_.back().pass];
    IRect clip = scissor();
    if (clip.empty()) return;
    BlendMode blend = blends_.empty() || opaque ? BlendMode::SrcOver : blends_.back();
    if (blend == BlendMode::Dst) return;  // (changes nothing)
    Prim p = prim;
    if (kind != MESH) p.extra[2] = float(kind);  // (a mesh's extra is a vertex's color: vsMesh knows what it is)
    if (kind != PATH && kind != MESH && !opaque && !inTargetPixels && !transform_.identity()) {
        // Its own transform, then the current one.
        Projective own{{p.xform[0], p.xform[2], p.extra[0], p.xform[1], p.xform[3], p.extra[1], p.projection[0], p.projection[1], p.projection[2]}};
        Projective t = transform_ * own;
        set(p.xform, t.m[0], t.m[3], t.m[1], t.m[4]);
        p.extra[0] = t.m[2], p.extra[1] = t.m[5];
        set(p.projection, t.m[6], t.m[7], t.m[8], 0);
        // The quad reaches a device pixel past the shape where that is smallest (it reaches one
        // of its own pixels past it already); glyphs and images turned get anti-aliased edges.
        float least = 0;
        for (int c = 0; c < 4; ++c) {
            float x = p.bounds[c & 1 ? 2 : 0], y = p.bounds[c & 2 ? 3 : 1], w;
            t.apply(x, y, &w);
            float m = w > 0 ? t.minScale(x, y) : 0;
            if (m > 0) least = least > 0 ? std::min(least, m) : m;
        }
        float pad = least > 0 ? std::min(1 / least, 1e4f) : 1;
        int flags = int(p.extra[3]);
        auto grow = [&](float by) { p.bounds[0] -= by, p.bounds[1] -= by, p.bounds[2] += by, p.bounds[3] += by; };
        if (kind == GLYPH || kind == IMAGE) {
            if (!t.axisAligned()) {
                p.extra[3] = float(flags | 2);
                grow(pad);
            }
        } else if (!(kind == LAYER && (flags & 1)) && pad > 1) {
            grow(pad - 1);
        }
    }
    if (!clips_.empty() && clips_.back().chain >= 0 && !opaque) {
        int chain = clips_.back().chain;
        p.filter[2] = float(std::min<size_t>(clipChains_[size_t(chain)].elements.size(), kStopsWidth / 6));
        p.filter[3] = float(clipRow(chain));
    }
    if (!filters_.empty() && !opaque) {
        // The filters in effect, the innermost first, in a row of the table (once a frame for each chain).
        std::vector<ColorFilter> chain(filters_.rbegin(), filters_.rend());
        int row = -1;
        for (size_t i = 0; i < filterChains_.size() && row < 0; ++i)
            if (filterChains_[i] == chain) row = filterRows_[i];
        if (row < 0) {
            row = int(stops_.size() / (kStopsWidth * 4));
            size_t at = stops_.size();
            stops_.resize(at + kStopsWidth * 4, 0.f);
            for (size_t i = 0; i < chain.size() && i < kStopsWidth / 6; ++i) {
                const ColorFilter& f = chain[i];
                float* t = &stops_[at + i * 24];  // six texels
                t[0] = f.kind() == ColorFilter::Kind::Matrix ? 1.f : f.kind() == ColorFilter::Kind::Lighting ? 2.f : 0.f;
                t[1] = float(int(f.blendMode()));
                if (f.kind() == ColorFilter::Kind::Matrix) {
                    const auto& m = f.matrix().values;
                    for (int r = 0; r < 4; ++r) {
                        for (int k = 0; k < 4; ++k) t[4 + r * 4 + k] = m[size_t(r * 5 + k)];
                        t[20 + r] = m[size_t(r * 5 + 4)];
                    }
                } else {
                    Color c = f.color(), add = f.add();
                    t[4] = c.r, t[5] = c.g, t[6] = c.b, t[7] = c.a;
                    t[8] = add.r, t[9] = add.g, t[10] = add.b, t[11] = add.a;
                }
            }
            filterChains_.push_back(chain);
            filterRows_.push_back(row);
        }
        p.filter[0] = float(std::min<size_t>(chain.size(), kStopsWidth / 6));
        p.filter[1] = float(row);
    }
    Sampler sampler1, sampler5;
    Texture* texture5 = nullptr;
    int paintKind = int(p.paint[0] + 0.5f);
    auto image = [&](float index, Texture*& t, Sampler& s) {  // an image paint's texture
        if (index < 0 || size_t(index) >= paintTextures_.size()) return;
        t = paintTextures_[size_t(index)].texture, s = paintTextures_[size_t(index)].sampler;
    };
    if (paintKind == 4 && !texture1) image(p.paint[2], texture1, sampler1);
    if (paintKind == 5) {  // (a composite's: its destination's, its source's)
        if (!texture1) image(p.paint[1], texture1, sampler1);
        image(p.paint[2], texture5, sampler5);
    }
    if (!texture5) texture5 = white_.get();
    if (!texture) texture = white_.get();
    if (!texture1) texture1 = white_.get();
    uint32_t generation = pass.shelf.generation;
    if (!pass.batches.empty()) {
        Batch& last = pass.batches.back();
        if (last.texture == texture && last.texture1 == texture1 && last.mask == mask && last.opaque == opaque && last.blend == blend &&
            last.scissor == clip && last.generation == generation && last.sampler == sampler && last.sampler1 == sampler1 &&
            last.mesh == (kind == MESH) && last.texture5 == texture5 && last.sampler5 == sampler5) {
            pass.prims.push_back(p);
            ++last.count;
            return;
        }
    }
    pass.batches.push_back({texture, texture1, mask, opaque, blend, clip, generation, uint32_t(pass.prims.size()), 1, sampler, sampler1,
                            kind == MESH, texture5, sampler5});
    pass.prims.push_back(p);
}

void Renderer::fillOpaque(const IRect& rect, Color color) {
    Prim p;
    set(p.bounds, float(rect.x), float(rect.y), float(rect.x + rect.width), float(rect.y + rect.height));
    std::copy(std::begin(p.bounds), std::end(p.bounds), p.shape);
    set(p.color, color.r, color.g, color.b, color.a);  // as it is
    add(FILL, p, nullptr, true);
}

void Renderer::blit(Texture& texture) {
    const Texture& t = *passes_[targets_.back().pass].target;
    Prim p;
    set(p.bounds, 0, 0, float(t.width()), float(t.height()));
    set(p.uv, 0, 0, 1, 1);
    p.params[0] = 1;
    add(IMAGE, p, &texture, true);
}

// --- Submitting ----------------------------------------------------------------------------------

Texture* Renderer::pageTexture(int index) {
    if (size_t(index) >= pages_.size()) pages_.resize(size_t(index) + 1);
    auto& t = pages_[size_t(index)];
    if (!t) {
        t = device_.createTexture(AtlasPage::kSize, AtlasPage::kSize, Format::RGBA8, TextureUsage::Sampled);
        text_.atlas().page(index).markAllDirty();
    }
    return t.get();
}

void Renderer::flush() {
    if (order_.empty()) return;
    // What changed on the atlas since the last flush, then the passes.
    GlyphAtlas& atlas = text_.atlas();
    for (int i = 0; i < atlas.pageCount(); ++i) {
        Texture* t = pageTexture(i);
        AtlasPage& page = atlas.page(i);
        DirtyRect d = page.takeDirty();
        if (!d.empty())
            device_.upload(*t, {d.x0, d.y0, d.x1 - d.x0, d.y1 - d.y0}, page.pixels() + (size_t(d.y0) * AtlasPage::kSize + size_t(d.x0)) * 4,
                           size_t(AtlasPage::kSize) * 4);
    }
    if (!whiteUploaded_) {
        const uint8_t white[4] = {255, 255, 255, 255};
        device_.upload(*white_, {0, 0, 1, 1}, white, 4);
        whiteUploaded_ = true;
    }
    // The gradients' stops recorded since the last flush (all of them into a bigger table).
    int rows = int(stops_.size() / (kStopsWidth * 4));
    if (rows > stopsUploaded_) {
        if (!stopsTexture_ || stopsTexture_->height() < rows) {
            stopsTexture_ = device_.createTexture(kStopsWidth, std::max(64, (rows + 63) / 64 * 64), Format::RGBA32F, TextureUsage::Sampled);
            stopsUploaded_ = 0;
        }
        device_.upload(*stopsTexture_, {0, stopsUploaded_, kStopsWidth, rows - stopsUploaded_},
                       stops_.data() + size_t(stopsUploaded_) * kStopsWidth * 4, kStopsWidth * 16);
        stopsUploaded_ = rows;
    }
    for (size_t index : order_) execute(passes_[index]);
    order_.clear();
    // Passes still being drawn into keep their places; the others go.
    if (targets_.empty()) passes_.clear();
}

void Renderer::execute(Pass& pass) {
    if (pass.copy) {
        device_.copyTexture(*pass.copy->first, pass.copy->second, *pass.target, 0, 0);
        return;
    }
    if (pass.effect) {
        const EffectDraw& e = *pass.effect;
        if (!effectPipeline_) {
            PipelineDesc desc;
            desc.vertex = &kEffectVertex;
            desc.fragment = &kEffectFragment;
            desc.instanceAttributes = {{0, VertexFormat::Float4, 0}};
            desc.instanceStride = 16;
            desc.textures = 2;
            desc.blend = {false};
            desc.target = pass.target->format();
            effectPipeline_ = device_.createPipeline(desc);
        }
        BufferRef rect;
        float* r = static_cast<float*>(device_.allocate(16, 16, &rect));
        r[0] = float(e.rect.x), r[1] = float(e.rect.y), r[2] = float(e.rect.x + e.rect.width), r[3] = float(e.rect.y + e.rect.height);
        float uniforms[32];
        std::copy(std::begin(e.uniforms), std::end(e.uniforms), uniforms);
        float w = float(pass.target->width()), h = float(pass.target->height());
        bool yDown = device_.clipYDown();
        uniforms[0] = 2 / w, uniforms[1] = yDown ? 2 / h : -2 / h, uniforms[2] = -1, uniforms[3] = yDown ? -1.f : 1.f;
        Texture* table = e.mask ? e.mask : e.table && stopsTexture_ ? stopsTexture_.get() : white_.get();
        uniforms[30] = 1 / float(table->width()), uniforms[31] = 1 / float(table->height());
        Texture* reads[] = {e.source, table};
        device_.beginPass(*pass.target, ClearColor{}, reads);
        device_.setPipeline(*effectPipeline_);
        device_.setUniforms(uniforms, sizeof uniforms);
        device_.setTexture(0, *e.source, Sampler{e.linear ? Filter::Linear : Filter::Nearest});
        device_.setTexture(1, *table, Sampler{Filter::Nearest});
        device_.setScissor(e.rect);
        device_.draw(rect, 6, 1);
        device_.endPass();
        return;
    }
    // The instances, all at once; then each mask generation: its paths' coverage, then the
    // batches that read it (the target cleared only before the first).
    BufferRef prims;
    if (!pass.prims.empty()) {
        void* memory = device_.allocate(pass.prims.size() * sizeof(Prim), 16, &prims);
        std::memcpy(memory, pass.prims.data(), pass.prims.size() * sizeof(Prim));
    }
    float w = float(pass.target->width()), h = float(pass.target->height());
    bool yDown = device_.clipYDown();
    uint32_t generations = pass.batches.empty() ? 1 : pass.batches.back().generation + 1;
    bool masked = !pass.edges.empty();
    if (masked && (!mask_ || mask_->width() < pass.shelf.width || mask_->height() < pass.shelf.height)) {
        int mw = std::max(pass.shelf.width, mask_ ? mask_->width() : 0), mh = std::max(pass.shelf.height, mask_ ? mask_->height() : 0);
        mask_ = device_.createTexture(mw, mh, Format::R16F, TextureUsage::Sampled | TextureUsage::RenderTarget);
    }
    size_t edge = 0, batch = 0;
    for (uint32_t g = 0; g < generations; ++g) {
        size_t firstEdge = edge;
        while (edge < pass.edges.size() && pass.edgeGenerations[edge] == g) ++edge;
        if (edge > firstEdge) {
            if (!coveragePipeline_) {
                PipelineDesc desc;
                desc.vertex = &kCoverageVertex;
                desc.fragment = &kCoverageFragment;
                desc.instanceAttributes = {{0, VertexFormat::Float4, 0}, {1, VertexFormat::Float4, 16}};
                desc.instanceStride = sizeof(EdgeInstance);
                desc.blend = {true, BlendFactor::One, BlendFactor::One, BlendFactor::One, BlendFactor::One};
                desc.target = Format::R16F;
                coveragePipeline_ = device_.createPipeline(desc);
            }
            BufferRef edges;
            void* memory = device_.allocate((edge - firstEdge) * sizeof(EdgeInstance), 16, &edges);
            std::memcpy(memory, pass.edges.data() + firstEdge, (edge - firstEdge) * sizeof(EdgeInstance));
            float mw = float(mask_->width()), mh = float(mask_->height());
            float toMask[8] = {2 / mw, yDown ? 2 / mh : -2 / mh, -1, yDown ? -1.f : 1.f, 0, 0, 0, 0};
            device_.beginPass(*mask_, ClearColor{}, {});
            device_.setPipeline(*coveragePipeline_);
            device_.setUniforms(toMask, sizeof toMask);
            device_.setScissor({0, 0, mask_->width(), mask_->height()});
            device_.draw(edges, 6, uint32_t(edge - firstEdge));
            device_.endPass();
        }
        std::vector<Texture*> reads;
        size_t firstBatch = batch;
        while (batch < pass.batches.size() && pass.batches[batch].generation == g) ++batch;
        bool readsTarget = false;
        for (size_t i = firstBatch; i < batch; ++i) {
            const Batch& b = pass.batches[i];
            for (Texture* t : {b.mask == Mask::Texture0 ? mask_.get() : b.texture, b.mask == Mask::Texture1 ? mask_.get() : b.texture1, b.texture5})
                if (std::find(reads.begin(), reads.end(), t) == reads.end()) reads.push_back(t);
            readsTarget = readsTarget || planFor(b.blend, b.opaque).readsTarget;
        }
        if (stopsTexture_) reads.push_back(stopsTexture_.get());
        // (The clips' paths are read from the coverage mask by any primitive in them.)
        if (masked && std::find(reads.begin(), reads.end(), mask_.get()) == reads.end()) reads.push_back(mask_.get());
        if (readsTarget) {
            if (!dstCopy_ || dstCopy_->width() < pass.target->width() || dstCopy_->height() < pass.target->height() ||
                dstCopy_->format() != pass.target->format())
                dstCopy_ = device_.createTexture(std::max(pass.target->width(), dstCopy_ ? dstCopy_->width() : 0),
                                                 std::max(pass.target->height(), dstCopy_ ? dstCopy_->height() : 0), pass.target->format(),
                                                 TextureUsage::Sampled);
            reads.push_back(dstCopy_.get());
        }
        device_.beginPass(*pass.target, g == 0 ? pass.clear : std::nullopt, reads);
        float uniforms[12] = {2 / w, yDown ? 2 / h : -2 / h, -1, yDown ? -1.f : 1.f,
                              mask_ ? 1 / float(mask_->width()) : 0.f, mask_ ? 1 / float(mask_->height()) : 0.f,
                              stopsTexture_ ? 1 / float(stopsTexture_->width()) : 0.f, stopsTexture_ ? 1 / float(stopsTexture_->height()) : 0.f,
                              0, 0,
                              // A texel of the target's copy (which may be bigger than the target: one copy for all).
                              readsTarget ? 1 / float(dstCopy_->width()) : 0.f, readsTarget ? 1 / float(dstCopy_->height()) : 0.f};
        const Sampler nearest{Filter::Nearest};
        IRect all{0, 0, pass.target->width(), pass.target->height()};
        for (size_t i = firstBatch; i < batch; ++i) {
            const Batch& b = pass.batches[i];
            BlendPlan plan = planFor(b.blend, b.opaque);
            uniforms[8] = float(int(b.blend));
            uniforms[9] = float(plan.how);
            device_.setPipeline(pipeline(pass.target->format(), plan.state, b.mesh));
            device_.setUniforms(uniforms, sizeof uniforms);
            uint32_t corners = b.mesh ? 3 : 6;
            if (b.mask == Mask::Texture0)
                device_.setTexture(0, *mask_, nearest);
            else
                device_.setTexture(0, *b.texture, b.sampler);
            if (b.mask == Mask::Texture1)  // (a layer's cut, which may be turned: filtered)
                device_.setTexture(1, *mask_, Sampler{});
            else
                device_.setTexture(1, *b.texture1, b.sampler1);
            device_.setTexture(2, stopsTexture_ ? *stopsTexture_ : *white_, nearest);
            device_.setTexture(3, readsTarget ? *dstCopy_ : *white_, nearest);
            device_.setTexture(4, masked ? *mask_ : *white_, nearest);
            device_.setTexture(5, *b.texture5, b.sampler5);
            if (!plan.readsTarget) {
                device_.setScissor(b.scissor);
                device_.draw({prims.buffer, prims.offset + uint64_t(b.first) * sizeof(Prim)}, corners, b.count);
                continue;
            }
            // Each one on what is there just before it: the pass ends, the pixels under it are copied,
            // the pass goes on.
            for (uint32_t k = b.first; k < b.first + b.count; ++k) {
                const Prim& q = pass.prims[k];
                Projective place{{q.xform[0], q.xform[2], q.extra[0], q.xform[1], q.xform[3], q.extra[1], q.projection[0], q.projection[1],
                                  q.projection[2]}};
                float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
                bool behind = false;  // part of it behind the eye: all of the scissor
                for (int c = 0; c < (b.mesh ? 3 : 4); ++c) {
                    float w;
                    Offset d;
                    if (b.mesh) {  // (a triangle's vertices, where they are: x, y and w)
                        const float xyw[3][3] = {{q.bounds[0], q.bounds[1], q.bounds[2]}, {q.bounds[3], q.shape[0], q.shape[1]},
                                                 {q.shape[2], q.shape[3], q.radii[0]}};
                        w = xyw[c][2];
                        d = {xyw[c][0] / w, xyw[c][1] / w};
                    } else {
                        d = place.apply(q.bounds[c & 1 ? 2 : 0], q.bounds[c & 2 ? 3 : 1], &w);
                    }
                    behind = behind || w <= 0;
                    x0 = std::min(x0, d.x), y0 = std::min(y0, d.y), x1 = std::max(x1, d.x), y1 = std::max(y1, d.y);
                }
                IRect under = behind ? b.scissor
                                     : IRect{int(std::floor(x0)), int(std::floor(y0)), int(std::ceil(x1)) - int(std::floor(x0)),
                                             int(std::ceil(y1)) - int(std::floor(y0))};
                under = intersect(intersect(under, b.scissor), all);
                if (under.empty()) continue;
                device_.endPass();
                device_.copyTexture(*pass.target, under, *dstCopy_, under.x, under.y);
                device_.beginPass(*pass.target, std::nullopt, reads);
                device_.setUniforms(uniforms, sizeof uniforms);
                device_.setScissor(under);
                device_.draw({prims.buffer, prims.offset + uint64_t(k) * sizeof(Prim)}, corners, 1);
            }
        }
        device_.endPass();
    }
    pass.prims.clear();
    pass.batches.clear();
    pass.edges.clear();
    pass.edgeGenerations.clear();
}

void Renderer::endFrame() {
    stops_.clear();
    stopBrushes_.clear();
    stopRows_.clear();
    paintTextures_.clear();
    filterChains_.clear();
    filterRows_.clear();
    clipChains_.clear();
    stopsUploaded_ = 0;
    for (auto it = images_.begin(); it != images_.end();)
        it = it->second.token.expired() ? images_.erase(it) : std::next(it);
    std::erase_if(pool_, [](PooledTexture& p) {  // kept for a second unused: layers scroll back in
        p.idleFrames = p.inUse ? 0 : p.idleFrames + 1;
        p.inUse = false;
        return p.idleFrames > 60;
    });
}

// A layer target (or a target's copy, in its format) at least w x h, not used yet this frame (one that is would still be read by the
// pass that composites it, which runs later). Sizes go up in 32 px steps, and a free texture up
// to 64 px bigger is reused, so a layer moving by fractions doesn't make a texture each frame.
Texture* Renderer::acquire(int width, int height, Format format) {
    PooledTexture* best = nullptr;
    for (auto& p : pool_) {
        int w = p.texture->width(), h = p.texture->height();
        if (!p.inUse && p.texture->format() == format && w >= width && h >= height && w <= width + 64 && h <= height + 64 &&
            (!best || w * h < best->texture->width() * best->texture->height()))
            best = &p;
    }
    if (!best) {
        auto t = device_.createTexture((width + 31) / 32 * 32, (height + 31) / 32 * 32, format,
                                       TextureUsage::Sampled | TextureUsage::RenderTarget);
        if (!t) return nullptr;
        pool_.push_back({std::move(t)});
        best = &pool_.back();
    }
    best->inUse = true;
    return best->texture.get();
}

// Premultiplied, so that what filtering mixes is weighed by how much of it there is (as Skia
// samples): a transparent pixel's color doesn't bleed into its neighbors.
Texture* Renderer::imageTexture(const ImageBitmap& image, bool mipmapped) {
    if (image.empty()) return nullptr;
    auto it = images_.find(image.id());
    if (it != images_.end() && (it->second.mipmapped || !mipmapped)) return it->second.texture.get();
    int w = image.width(), h = image.height();
    auto t = device_.createTexture(w, h, Format::RGBA8, uint8_t(TextureUsage::Sampled | (mipmapped ? TextureUsage::Mipmapped : 0)));
    if (!t) return nullptr;
    std::vector<uint8_t> premultiplied(image.pixels(), image.pixels() + size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < premultiplied.size(); i += 4) {
        int a = premultiplied[i + 3];
        for (size_t k = 0; k < 3; ++k) premultiplied[i + k] = uint8_t((premultiplied[i + k] * a + 127) / 255);
    }
    device_.upload(*t, {0, 0, w, h}, premultiplied.data(), size_t(w) * 4);
    if (mipmapped) device_.generateMipmaps(*t);
    Texture* raw = t.get();
    images_[image.id()] = ImageTexture{std::move(t), image.token(), mipmapped};  // (a mipmapped one in place of one without)
    return raw;
}

// --- GraphicsContext -----------------------------------------------------------------------------

void Renderer::fillRoundRect(const Rect& rect, const CornerRadii& radii, Color color) {
    if (color.a <= 0 || rect.width <= 0 || rect.height <= 0) return;
    float x = rect.x * scale - ox_, y = rect.y * scale - oy_, w = rect.width * scale, h = rect.height * scale;
    float half = std::min(w, h) / 2;
    auto px = [&](float r) { return std::clamp(r * scale, 0.f, half); };
    Prim p;
    set(p.bounds, x - 1, y - 1, x + w + 1, y + h + 1);
    set(p.shape, x, y, x + w, y + h);
    set(p.radii, px(radii.topLeft), px(radii.topRight), px(radii.bottomRight), px(radii.bottomLeft));
    premultiplied(p.color, color);
    add(FILL, p);
}

void Renderer::strokeRoundRect(const Rect& rect, const CornerRadii& radii, float width, Color color) {
    if (color.a <= 0 || width <= 0 || rect.width <= 0 || rect.height <= 0) return;
    float x = rect.x * scale - ox_, y = rect.y * scale - oy_, w = rect.width * scale, h = rect.height * scale;
    float half = std::min(w, h) / 2;
    auto px = [&](float r) { return std::clamp(r * scale, 0.f, half); };
    Prim p;
    set(p.bounds, x - 1, y - 1, x + w + 1, y + h + 1);
    set(p.shape, x, y, x + w, y + h);
    set(p.radii, px(radii.topLeft), px(radii.topRight), px(radii.bottomRight), px(radii.bottomLeft));
    p.params[0] = std::min(width * scale, half);
    premultiplied(p.color, color);
    add(STROKE, p);
}

void Renderer::drawLine(Offset from, Offset to, float width, Color color) {
    float x0 = from.x * scale - ox_, y0 = from.y * scale - oy_, x1 = to.x * scale - ox_, y1 = to.y * scale - oy_;
    if (std::hypot(x1 - x0, y1 - y0) < 1e-3f || color.a <= 0) return;
    float h = width * scale / 2, pad = h + 1;
    Prim p;
    set(p.bounds, std::min(x0, x1) - pad, std::min(y0, y1) - pad, std::max(x0, x1) + pad, std::max(y0, y1) + pad);
    set(p.shape, x0, y0, x1, y1);
    p.params[0] = h;
    premultiplied(p.color, color);
    add(LINE, p);
}

// A row of the table: `stops` (if any) from its start, then, in its last three texels, the rows of
// `map` (from the target's px to where the shader is evaluated).
int Renderer::shaderRow(const std::vector<ColorStop>* stops, const Projective& map) {
    int row = int(stops_.size() / (kStopsWidth * 4));
    size_t at = stops_.size();
    stops_.resize(at + kStopsWidth * 4, 0.f);
    if (stops)
        for (size_t i = 0; i < stops->size() && i < kStopsWidth / 2 - 2; ++i) {
            const ColorStop& stop = (*stops)[i];
            float* texel = &stops_[at + i * 8];
            texel[0] = stop.color.r, texel[1] = stop.color.g, texel[2] = stop.color.b, texel[3] = stop.color.a;
            texel[4] = stop.offset;
        }
    for (int r = 0; r < 3; ++r) {
        float* texel = &stops_[at + size_t(kStopsWidth - 3 + r) * 4];
        texel[0] = map.m[r * 3], texel[1] = map.m[r * 3 + 1], texel[2] = map.m[r * 3 + 2];
    }
    return row;
}

// A shader's paint: where each pixel is in it (its dp from the area's top left, through its
// transform taken back) worked out per pixel from the map in its row; a gradient's stops before
// that, an image in texture1 (premultiplied, sampled bilinearly, tiled as it says); a composite's
// two the same, blended where the pixel is (a composite of composites baked first: bakedShader).
Renderer::Prim Renderer::paintedShader(const Shader& shader, const Rect& area, float alpha, float enlarged, const Projective* before) {
    Prim p;
    if (!shader.data_) return p;  // (transparent)
    float k = enlarged * scale;
    Projective map{{1 / k, 0, ox_ / scale - area.x, 0, 1 / k, oy_ / scale - area.y, 0, 0, 1}};
    if (const Matrix* t = shader.transform()) {
        Matrix back = *t;
        back.invert();
        map = Projective::of(back) * map;
    }
    if (before) map = map * *before;
    set(p.color, 1, 1, 1, alpha);
    switch (shader.kind()) {
    case Shader::Kind::Composite: {
        // Worked out where each pixel is (paintAt): the two through the composite's own transform
        // (after theirs), each a paint (one color a stop of it), in a row of the table; a composite
        // in either baked.
        Shader dst = shader.dst(), src = shader.src();
        if (dst.kind() == Shader::Kind::Composite || src.kind() == Shader::Kind::Composite)
            return bakedShader(shader, area, alpha, enlarged, before);
        if (const Matrix* t = shader.transform()) dst = dst.transformed(*t), src = src.transformed(*t);
        auto child = [&](const Shader& one) {
            Prim c = paintedShader(one, area, 1, enlarged, before);
            if (int(c.paint[0] + 0.5f) == 0) {  // one color: a gradient of it
                std::vector<ColorStop> stop{{0, one.stops().empty() ? colors::Transparent : one.stops().front().color}};
                set(c.paint, 1, 0, 1, float(shaderRow(&stop, Projective{})));
                set(c.gradient, 0, 0, 0, 0);
            }
            return c;
        };
        Prim d = child(dst), sp = child(src);
        int row = int(stops_.size() / (kStopsWidth * 4));
        size_t at = stops_.size();
        stops_.resize(at + kStopsWidth * 4, 0.f);
        std::copy_n(d.paint, 4, &stops_[at]);
        std::copy_n(d.gradient, 4, &stops_[at + 4]);
        std::copy_n(sp.paint, 4, &stops_[at + 8]);
        std::copy_n(sp.gradient, 4, &stops_[at + 12]);
        stops_[at + 16] = float(int(shader.blendMode()));
        auto imageIndex = [](const Prim& c) { return int(c.paint[0] + 0.5f) == 4 ? c.paint[2] : -1.f; };
        set(p.paint, 5, imageIndex(d), imageIndex(sp), float(row));
        return p;
    }
    case Shader::Kind::Image: {
        Texture* t = imageTexture(shader.image());
        if (!t) return Prim{};
        auto wrap = [](TileMode m) {
            return m == TileMode::Repeated ? Wrap::Repeat : m == TileMode::Mirror ? Wrap::Mirror : m == TileMode::Decal ? Wrap::Transparent : Wrap::Clamp;
        };
        map = Projective::scaling(1 / float(shader.image().width()), 1 / float(shader.image().height())) * map;  // to its uv
        paintTextures_.push_back({t, Sampler{Filter::Linear, wrap(shader.tileMode()), wrap(shader.tileModeY())}});
        set(p.paint, 4, 8, float(paintTextures_.size() - 1), float(shaderRow(nullptr, map)));
        return p;
    }
    default: {
        int kind = shader.kind() == Shader::Kind::Linear ? 1 : shader.kind() == Shader::Kind::Radial ? 2 : 3;
        const auto& stops = shader.stops();
        if (stops.size() < 2) {  // one color (or none)
            Color c = stops.empty() ? colors::Transparent : stops.front().color;
            premultiplied(p.color, c.copy(c.a * alpha));
            return p;
        }
        set(p.paint, float(kind), float(int(shader.tileMode()) | 8), float(std::min<size_t>(stops.size(), kStopsWidth / 2 - 2)), float(shaderRow(&stops, map)));
        set(p.gradient, shader.start().x, shader.start().y, kind == 1 ? shader.end().x : shader.radius(), kind == 1 ? shader.end().y : 0);
        return p;
    }
    }
}

// A composite of composites baked: what the area covers in this target's px (at most kMaxLayer
// across, lower in resolution if it would be more) drawn into a texture of its own (in half floats
// if the target is), the second shader onto the first by its blend mode (the renderer's own
// blending, each a paint of its own), then painted from that texture.
Renderer::Prim Renderer::bakedShader(const Shader& shader, const Rect& area, float alpha, float enlarged, const Projective* before) {
    float k = enlarged * scale;
    float x0 = (area.x * scale - ox_) * enlarged, y0 = (area.y * scale - oy_) * enlarged, w = area.width * k, h = area.height * k;
    float r = std::min(1.f, float(kMaxLayer) / std::max({w, h, 1.f}));
    int W = std::max(1, int(std::ceil(w * r))), H = std::max(1, int(std::ceil(h * r)));
    bool fine = !targets_.empty() && passes_[targets_.back().pass].target->format() == Format::RGBA16F;
    Texture* bake = acquire(W, H, fine ? Format::RGBA16F : Format::RGBA8);
    if (!bake) return Prim{};
    // Its children, through its own transform (after theirs).
    Shader dst = shader.dst(), src = shader.src();
    if (const Matrix* t = shader.transform()) dst = dst.transformed(*t), src = src.transformed(*t);
    float s0 = scale, ox0 = ox_, oy0 = oy_;
    beginTarget(*bake, ClearColor{});
    // The bake's px: the area's px from its top left, r times as many.
    scale = s0 * enlarged * r;
    ox_ = (ox0 * enlarged + x0) * r, oy_ = (oy0 * enlarged + y0) * r;
    auto fill = [&](const Shader& s) {
        Prim q = paintedShader(s, area, 1, 1);
        set(q.bounds, -1, -1, float(W) + 1, float(H) + 1);
        set(q.shape, 0, 0, float(W), float(H));
        add(FILL, q);
    };
    fill(dst);
    blends_.push_back(shader.blendMode());
    fill(src);
    blends_.pop_back();
    endTarget();
    scale = s0;
    // This target's px to the bake's uv.
    Projective map{{r / float(bake->width()), 0, -x0 * r / float(bake->width()), 0, r / float(bake->height()), -y0 * r / float(bake->height()), 0, 0, 1}};
    if (before) map = map * *before;
    paintTextures_.push_back({bake, Sampler{}});
    Prim p;
    set(p.color, 1, 1, 1, alpha);
    set(p.paint, 4, 8, float(paintTextures_.size() - 1), float(shaderRow(nullptr, map)));
    return p;
}

// A mesh: each triangle one primitive (vsMesh), its vertices where this target's pixels are
// (x, y and w, through the transform: the rasterizer clips what is behind the eye and works the
// texture coordinates and colors out in perspective). The paint: the brush's color opaque, or its
// shader where the texture coordinates are (dp, through `before` to this target's px and the
// shader's own map from there).
void Renderer::drawVertices(const Vertices& vertices, Offset origin, BlendMode blendMode, const Brush& brush, const Rect& brushArea,
                            float alpha) {
    std::vector<std::array<int, 3>> triangles = vertices.triangles();
    if (triangles.empty() || alpha <= 0) return;
    const std::vector<Offset>& at = vertices.positions();
    const std::vector<Offset>& tex = vertices.textureCoordinates();
    const std::vector<Color>& colors = vertices.colors();
    auto sampled = [&](int i) {
        Offset t = tex.empty() ? at[size_t(i)] : tex[size_t(i)];
        return Offset{origin.x + t.x, origin.y + t.y};
    };
    Prim paint;
    if (brush.isSolid() || (brush.kind() != Brush::Kind::Shader && !brush.transform() && brush.stops().size() < 2)) {
        Color c = brush.stops().empty() ? colors::Transparent : brush.stops().front().color;
        premultiplied(paint.color, c.copy(1));
    } else {
        Shader shader = brush.createShader({brushArea.width, brushArea.height});
        Rect area = brushArea;
        float enlarged = 1;
        bool nested = shader.kind() == Shader::Kind::Composite &&
                      (shader.dst().kind() == Shader::Kind::Composite || shader.src().kind() == Shader::Kind::Composite);
        if (nested) {
            // A composite of composites is baked (see paintedShader): over the texture
            // coordinates' bounds (its points from the area's top left still), as finely as the
            // mesh spreads them over the screen (as many texels as px, up to 8 a px of this
            // target's), sampled between its texels.
            float l = 1e30f, t = 1e30f, r = -1e30f, b = -1e30f;
            for (const auto& tri : triangles) {
                Offset s[3], u[3];
                bool seen = true;
                for (int k = 0; k < 3; ++k) {
                    int i = tri[size_t(k)];
                    u[k] = sampled(i);
                    l = std::min(l, u[k].x), t = std::min(t, u[k].y), r = std::max(r, u[k].x), b = std::max(b, u[k].y);
                    float w;
                    s[k] = transform_.apply((origin.x + at[size_t(i)].x) * scale - ox_, (origin.y + at[size_t(i)].y) * scale - oy_, &w);
                    seen = seen && w > 0;
                }
                // How far a dp of texture coordinate goes on the screen: the map between them's
                // size (its Frobenius norm, at least its largest stretch).
                float a1 = u[1].x - u[0].x, b1 = u[2].x - u[0].x, c1 = u[1].y - u[0].y, d1 = u[2].y - u[0].y, det = a1 * d1 - b1 * c1;
                if (!seen || std::abs(det) < 1e-12f) continue;
                float sx1 = s[1].x - s[0].x, sx2 = s[2].x - s[0].x, sy1 = s[1].y - s[0].y, sy2 = s[2].y - s[0].y;
                float j00 = (sx1 * d1 - sx2 * c1) / det, j01 = (sx2 * a1 - sx1 * b1) / det;
                float j10 = (sy1 * d1 - sy2 * c1) / det, j11 = (sy2 * a1 - sy1 * b1) / det;
                enlarged = std::max(enlarged, std::sqrt(j00 * j00 + j01 * j01 + j10 * j10 + j11 * j11) / scale);
            }
            enlarged = std::min(enlarged, 8.f);
            area = {l, t, r - l, b - t};
            Matrix shift;
            shift.translate(brushArea.x - area.x, brushArea.y - area.y);
            shader = shader.transformed(shift);
        }
        float k = scale * enlarged;  // (the point the paint is asked for: dp to this target's px, enlarged)
        Projective toPixels{{k, 0, -ox_ * enlarged, 0, k, -oy_ * enlarged, 0, 0, 1}};
        paint = paintedShader(shader, area, 1, enlarged, &toPixels);
    }
    set(paint.params, float(int(blendMode)), alpha, colors.empty() ? 0.f : 1.f, 0);
    const float* m = transform_.m;
    for (const auto& tri : triangles) {
        Prim p = paint;
        float xyw[3][3], uv[3][2], rgba[3][4] = {};
        for (int k = 0; k < 3; ++k) {
            size_t i = size_t(tri[size_t(k)]);
            float x = (origin.x + at[i].x) * scale - ox_, y = (origin.y + at[i].y) * scale - oy_;
            xyw[k][0] = m[0] * x + m[1] * y + m[2], xyw[k][1] = m[3] * x + m[4] * y + m[5], xyw[k][2] = m[6] * x + m[7] * y + m[8];
            Offset q = sampled(tri[size_t(k)]);
            uv[k][0] = q.x, uv[k][1] = q.y;
            if (!colors.empty()) premultiplied(rgba[k], colors[i]);
        }
        set(p.bounds, xyw[0][0], xyw[0][1], xyw[0][2], xyw[1][0]);
        set(p.shape, xyw[1][1], xyw[1][2], xyw[2][0], xyw[2][1]);
        set(p.radii, xyw[2][2], uv[0][0], uv[0][1], uv[1][0]);
        set(p.uv, uv[1][1], uv[2][0], uv[2][1], 0);
        set(p.xform, rgba[0][0], rgba[0][1], rgba[0][2], rgba[0][3]);
        set(p.extra, rgba[1][0], rgba[1][1], rgba[1][2], rgba[1][3]);
        set(p.projection, rgba[2][0], rgba[2][1], rgba[2][2], rgba[2][3]);
        add(MESH, p, nullptr, false, Mask::None, nullptr, true);
    }
}

Renderer::Prim Renderer::painted(const Brush& brush, const Rect& area, float alpha, float enlarged) {
    if (brush.kind() == Brush::Kind::Shader || brush.transform())
        return paintedShader(brush.createShader({area.width, area.height}), area, alpha * brush.shaderAlpha(), enlarged);
    Prim p;
    if (brush.isSolid() || brush.stops().size() < 2) {
        Color c = brush.stops().empty() ? colors::Transparent : brush.stops().front().color;
        premultiplied(p.color, c.copy(c.a * alpha));
        return p;
    }
    // Its stops in this frame's table (once for equal brushes), its geometry in the target's pixels.
    int row = -1;
    for (size_t i = 0; i < stopBrushes_.size() && row < 0; ++i)
        if (stopBrushes_[i] == brush) row = stopRows_[i];
    if (row < 0) {
        row = int(stops_.size() / (kStopsWidth * 4));
        stopBrushes_.push_back(brush);
        stopRows_.push_back(row);
        size_t at = stops_.size();
        stops_.resize(at + kStopsWidth * 4, 0.f);
        const auto& stops = brush.stops();
        for (size_t i = 0; i < stops.size() && i < kStopsWidth / 2; ++i) {
            const ColorStop& stop = stops[i];
            float* texel = &stops_[at + i * 8];
            texel[0] = stop.color.r, texel[1] = stop.color.g, texel[2] = stop.color.b, texel[3] = stop.color.a;
            texel[4] = stop.offset;
        }
    }
    Brush::Geometry g = brush.geometry(area);
    auto px = [&](Offset o) { return Offset{(o.x * scale - ox_) * enlarged, (o.y * scale - oy_) * enlarged}; };
    Offset start = px(g.start), end = px(g.end);
    int kind = brush.kind() == Brush::Kind::Linear ? 1 : brush.kind() == Brush::Kind::Radial ? 2 : 3;
    set(p.paint, float(kind), float(int(brush.tileMode())), float(std::min<size_t>(brush.stops().size(), kStopsWidth / 2)), float(row));
    set(p.gradient, start.x, start.y, kind == 1 ? end.x : g.radius * scale * enlarged, kind == 1 ? end.y : 0);
    set(p.color, 1, 1, 1, alpha);
    return p;
}

void Renderer::glyphs(const Prim& paint, float enlarged) {
    constexpr float kPage = float(AtlasPage::kSize);
    for (const GlyphQuad& g : glyphs_) {
        Prim p = paint;
        set(p.bounds, g.x0, g.y0, g.x1, g.y1);
        std::copy(std::begin(p.bounds), std::end(p.bounds), p.shape);
        set(p.uv, float(g.u) / kPage, float(g.v) / kPage, float(g.u + g.width) / kPage, float(g.v + g.height) / kPage);
        set(p.params, g.color ? 1.f : 0.f, 0, 0.5f / kPage, 0.5f / kPage);
        if (enlarged != 1) set(p.xform, 1 / enlarged, 0, 0, 1 / enlarged);  // laid out that many times bigger
        add(GLYPH, p, pageTexture(g.page));
    }
}

namespace {
// How many times bigger than its pixels text is laid out under a transform that enlarges it by
// `enlarged`: as much, unless its glyphs would no longer fit on the atlas (then they are enlarged
// after, a little soft).
float textScale(float enlarged, const Font& font, float scale) {
    return std::min(enlarged, std::max(1.f, 384 / std::max(1.f, font.size * scale)));
}
} // namespace

void Renderer::drawText(std::string_view text, Offset baseline, const Font& font, Color color, bool rtl) {
    if (color.a <= 0) return;
    float x = baseline.x * scale - ox_, y = baseline.y * scale - oy_, k = textScale(rasterScale(x, y), font, scale);
    glyphs_.clear();
    text_.layout(text, x * k, y * k, font, rtl, glyphs_, false, 0, scale * k);
    glyphs(painted(Brush::solid(color), {}), k);
}

void Renderer::drawTextBrush(std::string_view text, Offset baseline, const Font& font, const Brush& brush, const Rect& area, bool rtl) {
    float x = baseline.x * scale - ox_, y = baseline.y * scale - oy_, k = textScale(rasterScale(x, y), font, scale);
    glyphs_.clear();
    text_.layout(text, x * k, y * k, font, rtl, glyphs_, false, 0, scale * k);
    glyphs(painted(brush, area, 1, k), k);
}

void Renderer::drawTextShadow(std::string_view text, Offset baseline, const Font& font, Color color, float blurRadius, bool rtl) {
    if (color.a <= 0) return;
    float x = baseline.x * scale - ox_, y = baseline.y * scale - oy_, k = textScale(rasterScale(x, y), font, scale);
    glyphs_.clear();
    text_.layout(text, x * k, y * k, font, rtl, glyphs_, true, std::max(0, int(std::lround(blurRadius * scale * k))), scale * k);
    glyphs(painted(Brush::solid(color), {}), k);
}

void Renderer::drawVector(const ImageVector& vector, const Rect& b, Color color) {
    if (color.a <= 0 || b.width <= 0 || b.height <= 0) return;
    float x = b.x * scale - ox_, y = b.y * scale - oy_, k = rasterScale(x + b.width * scale / 2, y + b.height * scale / 2);
    glyphs_.resize(1);
    if (text_.vectorQuad(vector, x * k, y * k, b.width * scale * k, b.height * scale * k, glyphs_[0])) glyphs(painted(Brush::solid(color), {}), k);
}

void Renderer::drawArc(const Rect& oval, float start, float sweep, float width, Color color, bool roundCap) {
    if (color.a <= 0 || width <= 0 || sweep == 0) return;
    float cx = (oval.x + oval.width / 2) * scale - ox_, cy = (oval.y + oval.height / 2) * scale - oy_;
    float r = std::min(oval.width, oval.height) / 2 * scale, h = width * scale / 2, pad = r + h + 1;
    constexpr float kRadians = std::numbers::pi_v<float> / 180;
    Prim p;
    set(p.bounds, cx - pad, cy - pad, cx + pad, cy + pad);
    set(p.shape, cx, cy, r, 0);
    set(p.params, h, start * kRadians, sweep * kRadians, roundCap ? 1.f : 0.f);
    premultiplied(p.color, color);
    add(ARC, p);
}

// Sampled as `quality` says: the nearest texel, bilinearly, bilinearly from the mipmap level
// nearest the size it is drawn at (Skia's Medium), or a Mitchell cubic of the sixteen nearest
// (Skia's High, in the shader: each texel at its center).
void Renderer::drawImage(const ImageBitmap& image, const Rect& src, const Rect& dst, float alpha, FilterQuality quality) {
    Texture* t = imageTexture(image, quality == FilterQuality::Medium);
    if (!t || alpha <= 0) return;
    float x = dst.x * scale - ox_, y = dst.y * scale - oy_;
    Prim p;
    set(p.bounds, x, y, x + dst.width * scale, y + dst.height * scale);
    std::copy(std::begin(p.bounds), std::end(p.bounds), p.shape);
    float iw = float(image.width()), ih = float(image.height());
    set(p.uv, src.x / iw, src.y / ih, src.right() / iw, src.bottom() / ih);
    bool cubic = quality == FilterQuality::High;
    set(p.params, alpha, cubic ? 1.f : 0.f, 0.5f / iw, 0.5f / ih);
    Sampler sampler{quality == FilterQuality::None || cubic ? Filter::Nearest : Filter::Linear};
    sampler.mipmaps = quality == FilterQuality::Medium;
    add(IMAGE, p, t, false, Mask::None, nullptr, false, sampler);
}

void Renderer::drawShadow(const Rect& rect, const CornerRadii& radii, const Shadow& shadow, bool inner) {
    if (shadow.color.a <= 0 || rect.width <= 0 || rect.height <= 0) return;
    float blur = std::max(0.f, shadow.blurRadius) * scale;
    float sigma = blur > 0 ? 0.57735f * blur + 0.5f : 0;  // Skia's for the radius
    float grow = (inner ? -shadow.spread : shadow.spread) * scale;
    float x = rect.x * scale - ox_, y = rect.y * scale - oy_, w = rect.width * scale, h = rect.height * scale;
    CornerRadii r = radii.scaled(scale);
    // The shadow's shape: the rect spread (in for an inner one's hole) and moved.
    float sx = x - grow + shadow.offset.x * scale, sy = y - grow + shadow.offset.y * scale;
    float sw = w + 2 * grow, sh = h + 2 * grow;
    if (sw <= 0 || sh <= 0) {
        if (!inner) return;
        sw = sh = 0;  // no hole: the whole rect shadowed
    }
    CornerRadii shaped = r.grown(grow);
    auto clampTo = [](float v, float halfSide) { return std::clamp(v, 0.f, std::max(0.f, halfSide)); };
    Prim p;
    set(p.shape, sx, sy, sx + sw, sy + sh);
    float sh2 = std::min(sw, sh) / 2;
    set(p.radii, clampTo(shaped.topLeft, sh2), clampTo(shaped.topRight, sh2), clampTo(shaped.bottomRight, sh2),
        clampTo(shaped.bottomLeft, sh2));
    set(p.params, sigma, inner ? 1.f : 0.f, 0, 0);
    premultiplied(p.color, shadow.color);
    if (inner) {
        set(p.bounds, x, y, x + w, y + h);
        float half = std::min(w, h) / 2;
        set(p.uv, clampTo(r.topLeft, half), clampTo(r.topRight, half), clampTo(r.bottomRight, half), clampTo(r.bottomLeft, half));
    } else {
        float pad = 3 * sigma + 1;
        set(p.bounds, sx - pad, sy - pad, sx + sw + pad, sy + sh + pad);
    }
    add(SHADOW, p);
}

std::optional<Offset> Renderer::rasterize(const std::vector<Contour>& contours, IRect region) {
    if (region.empty() || targets_.empty()) return std::nullopt;
    Pass& pass = passes_[targets_.back().pass];
    MaskShelf& shelf = pass.shelf;
    // As big as the target, or as a region bigger than it (a layer reaching past it).
    shelf.width = std::max({shelf.width, kMaskSize, pass.target->width() + 2, region.width + 1});
    shelf.height = std::max({shelf.height, kMaskSize, pass.target->height() + 2, region.height + 1});
    // A spot on the shelves (a texel apart), in this generation's mask or the next one's.
    int w = region.width, h = region.height;
    if (shelf.x + w + 1 > shelf.width) {
        shelf.x = 0;
        shelf.y += shelf.row;
        shelf.row = 0;
    }
    if (shelf.y + h + 1 > shelf.height) {
        ++shelf.generation;
        shelf.x = shelf.y = shelf.row = 0;
    }
    int mx = shelf.x, my = shelf.y;
    shelf.x += w + 1;
    shelf.row = std::max(shelf.row, h + 1);
    edgeScratch_.clear();
    appendEdges(contours, edgeScratch_);
    float dx = float(mx - region.x), dy = float(my - region.y);
    float r[4] = {float(mx), float(my), float(mx + w), float(my + h)};
    for (const Edge& e : edgeScratch_) {
        pass.edges.push_back({{e.x0 + dx, e.y0 + dy, e.x1 + dx, e.y1 + dy}, {r[0], r[1], r[2], r[3]}});
        pass.edgeGenerations.push_back(shelf.generation);
    }
    return Offset{float(region.x - mx), float(region.y - my)};
}

void Renderer::fillContours(const std::vector<Contour>& contours, bool evenOdd, const Prim& paint) {
    if (targets_.empty() || (paint.paint[0] == 0 && paint.color[3] <= 0)) return;
    // The pixels the polygons touch, within the clip.
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (const Contour& c : contours)
        for (Offset q : c.points) x0 = std::min(x0, q.x), y0 = std::min(y0, q.y), x1 = std::max(x1, q.x), y1 = std::max(y1, q.y);
    if (x0 > x1) return;
    IRect bounds{int(std::floor(x0)), int(std::floor(y0)), int(std::ceil(x1)) - int(std::floor(x0)), int(std::ceil(y1)) - int(std::floor(y0))};
    IRect region = intersect(bounds, scissor());
    std::optional<Offset> origin = rasterizeInClips(contours, region);
    if (!origin) return;
    Prim p = paint;
    set(p.bounds, float(region.x), float(region.y), float(region.x + region.width), float(region.y + region.height));
    set(p.params, origin->x, origin->y, evenOdd ? 1.f : 0.f, 0);
    add(PATH, p, nullptr, false, Mask::Texture0);
}

std::vector<Contour> Renderer::polygonsOf(const Path& path, Offset origin, const PathStroke& stroke) {
    PathPlacement place{origin, scale, {ox_, oy_}};
    bool transformed = !transform_.identity();
    if (transformed) {
        // Cut finely enough for where the transform enlarges it most (its stroke included).
        Rect b = path.bounds();
        float reach = stroke.width * scale, most = 0;
        float x0 = (b.x + origin.x) * scale - ox_ - reach, y0 = (b.y + origin.y) * scale - oy_ - reach;
        float x1 = (b.right() + origin.x) * scale - ox_ + reach, y1 = (b.bottom() + origin.y) * scale - oy_ + reach;
        for (int c = 0; c < 4; ++c) most = std::max(most, transform_.maxScale(c & 1 ? x1 : x0, c & 2 ? y1 : y0));
        place.tolerance = kTolerance / std::clamp(most, 1.f, 64.f);
    }
    std::vector<Contour> contours = flattenPath(path.commands(), place);
    // A hairline: a pixel wide where it is drawn (through the transform first), its corners
    // round (Skia's hairlines join nothing: one line per segment).
    if (stroke.hairline) {
        if (transformed) contours = project(contours, transform_);
        return strokeOutline(contours, 1, stroke.cap, StrokeJoin::Round, stroke.miter, place.tolerance);
    }
    if (stroke.width > 0) contours = strokeOutline(contours, stroke.width * scale, stroke.cap, stroke.join, stroke.miter, place.tolerance);
    return transformed ? project(contours, transform_) : contours;
}

void Renderer::drawPath(const Path& path, Offset origin, const Brush& brush, const Rect& area, const PathStroke& stroke) {
    if (path.isEmpty()) return;
    std::vector<Contour> contours = polygonsOf(path, origin, stroke);
    bool evenOdd = !stroke.strokes() && path.fillType == Path::FillType::EvenOdd;
    Prim paint = painted(brush, area);
    if (!transform_.identity()) {
        if (paint.paint[0] != 0) {  // a gradient: worked out where each pixel is in the path's space
            Projective back = transform_.inverse();
            set(paint.radii, back.m[0], back.m[1], back.m[2], 0);
            set(paint.uv, back.m[3], back.m[4], back.m[5], 0);
            set(paint.shape, back.m[6], back.m[7], back.m[8], 0);
            paint.extra[3] = 4;
        }
    }
    fillContours(contours, evenOdd, paint);
}

void Renderer::fillGradient(const Rect& rect, const CornerRadii& radii, const Brush& brush, const Rect& area) {
    if (rect.width <= 0 || rect.height <= 0) return;
    float x = rect.x * scale - ox_, y = rect.y * scale - oy_, w = rect.width * scale, h = rect.height * scale;
    float half = std::min(w, h) / 2;
    auto px = [&](float r) { return std::clamp(r * scale, 0.f, half); };
    Prim p = painted(brush, area);
    set(p.bounds, x - 1, y - 1, x + w + 1, y + h + 1);
    set(p.shape, x, y, x + w, y + h);
    set(p.radii, px(radii.topLeft), px(radii.topRight), px(radii.bottomRight), px(radii.bottomLeft));
    add(FILL, p);
}

void Renderer::pushOutlineLayer(const Rect& bounds, const Path& outline, float alpha, const LayerTransform& transform) {
    pushLayer(bounds, CornerRadii(), alpha, transform);
    if (layers_.back().texture) layers_.back().outline = outline;
}

void Renderer::pushClip(const Rect& r) {
    float x0 = r.x * scale - ox_, y0 = r.y * scale - oy_, x1 = r.right() * scale - ox_, y1 = r.bottom() * scale - oy_;
    Clip clip{{}, clips_.empty() ? -1 : clips_.back().chain};
    std::optional<Rect> on = mapped(x0, y0, x1, y1);
    if (transform_.axisAligned() && on) {
        // Edges go to the nearest pixel (rounding outwards would show a row of what scrolled past).
        int l = int(std::lround(on->x)), t = int(std::lround(on->y)), rr = int(std::lround(on->right())), b = int(std::lround(on->bottom()));
        clip.scissor = {l, t, std::max(0, rr - l), std::max(0, b - t)};
    } else {
        // Turned (or in perspective): the pixels it can reach, and the rect itself in the chain,
        // its edges anti-aliased.
        ClipChain chain;
        if (clip.chain >= 0) chain.elements = clipChains_[size_t(clip.chain)].elements;
        chain.elements.push_back({{x0, y0, x1, y1}, {0, 0, 0, 0}, transform_.inverse(), false});
        clip.chain = int(clipChains_.size());
        clipChains_.push_back(std::move(chain));
        clip.scissor = on ? IRect{int(std::floor(on->x)), int(std::floor(on->y)), int(std::ceil(on->right())) - int(std::floor(on->x)),
                                  int(std::ceil(on->bottom())) - int(std::floor(on->y))}
                          : scissor();
    }
    if (!clips_.empty()) clip.scissor = intersect(clip.scissor, clips_.back().scissor);
    clips_.push_back(clip);
}

void Renderer::pushChain(ClipElement element, const IRect& scissor) {
    ClipChain chain;
    int outer = clips_.empty() ? -1 : clips_.back().chain;
    if (outer >= 0) chain.elements = clipChains_[size_t(outer)].elements, chain.paths = clipChains_[size_t(outer)].paths;
    chain.paths = chain.paths || element.polygons;
    chain.elements.push_back(std::move(element));
    clipChains_.push_back(std::move(chain));
    clips_.push_back({clips_.empty() ? scissor : intersect(scissor, clips_.back().scissor), int(clipChains_.size()) - 1});
}

void Renderer::pushClipRoundRect(const Rect& r, const CornerRadii& radii, ClipOp op) {
    if (op == ClipOp::Intersect && radii.max() <= 0) return pushClip(r);
    float x0 = r.x * scale - ox_, y0 = r.y * scale - oy_, x1 = r.right() * scale - ox_, y1 = r.bottom() * scale - oy_;
    float half = std::max(0.f, std::min(x1 - x0, y1 - y0) / 2);
    auto px = [&](float v) { return std::clamp(v * scale, 0.f, half); };
    ClipElement e;
    set(e.rect, x0, y0, x1, y1);
    set(e.radii, px(radii.topLeft), px(radii.topRight), px(radii.bottomRight), px(radii.bottomLeft));
    e.toLocal = transform_.inverse();
    e.outside = op == ClipOp::Difference;
    // Inside: the pixels it reaches; outside: all the clips around it leave.
    IRect reach = scissor();
    if (op == ClipOp::Intersect)
        if (std::optional<Rect> on = mapped(x0, y0, x1, y1))
            reach = {int(std::floor(on->x)), int(std::floor(on->y)), int(std::ceil(on->right())) - int(std::floor(on->x)),
                     int(std::ceil(on->bottom())) - int(std::floor(on->y))};
    pushChain(std::move(e), reach);
}

void Renderer::pushClipPath(const Path& path, Offset origin, ClipOp op) {
    auto polygons = std::make_shared<std::vector<Contour>>(polygonsOf(path, origin, {}));
    float l = 1e30f, t = 1e30f, r = -1e30f, b = -1e30f;
    for (const Contour& c : *polygons)
        for (Offset q : c.points) l = std::min(l, q.x), t = std::min(t, q.y), r = std::max(r, q.x), b = std::max(b, q.y);
    IRect around = scissor();
    IRect region = l > r ? IRect{} : intersect({int(std::floor(l)), int(std::floor(t)), int(std::ceil(r)) - int(std::floor(l)),
                                                 int(std::ceil(b)) - int(std::floor(t))}, around);
    ClipElement e;
    e.polygons = std::move(polygons);
    e.evenOdd = path.fillType == Path::FillType::EvenOdd;
    e.region = region;
    e.outside = op == ClipOp::Difference;
    pushChain(std::move(e), op == ClipOp::Intersect ? region : around);
}

void Renderer::popClip() {
    if (!clips_.empty()) clips_.pop_back();
}

void Renderer::pushTranslation(Offset by) {
    placements_.push_back({ox_, oy_, transform_});
    ox_ -= by.x * scale;
    oy_ -= by.y * scale;
}

void Renderer::popTranslation() {
    if (placements_.empty()) return;
    ox_ = placements_.back().ox;
    oy_ = placements_.back().oy;
    transform_ = placements_.back().transform;
    placements_.pop_back();
}

void Renderer::pushTransform(const Matrix& matrix) {
    placements_.push_back({ox_, oy_, transform_});
    Projective t = Projective::of(matrix);
    if (t.affine() && t.m[0] == 1 && t.m[1] == 0 && t.m[3] == 0 && t.m[4] == 1 && t.m[8] == 1) {  // only moved
        ox_ -= t.m[2] * scale;
        oy_ -= t.m[5] * scale;
        return;
    }
    // In the target's pixels: the map from dp (x * scale - origin), the matrix, and back.
    Projective dp{{scale, 0, -ox_, 0, scale, -oy_, 0, 0, 1}};
    transform_ = transform_ * dp * t * dp.inverse();
}

void Renderer::popTransform() { popTranslation(); }

bool Renderer::quickReject(const Rect& r) const {
    IRect visible = scissor();
    std::optional<Rect> on = mapped(r.x * scale - ox_, r.y * scale - oy_, r.right() * scale - ox_, r.bottom() * scale - oy_);
    if (!on) return false;  // partly behind the eye: whatever shows of it may be anywhere
    return on->right() <= float(visible.x) || on->bottom() <= float(visible.y) || on->x >= float(visible.x + visible.width) ||
           on->y >= float(visible.y + visible.height);
}

void Renderer::pushLayer(const Rect& bounds, const CornerRadii& radii, float alpha, const LayerTransform& transform) {
    pushLayer(bounds, radii, alpha, transform, std::nullopt);
}

void Renderer::pushLayer(const Rect& bounds, const CornerRadii& radii, float alpha, const LayerTransform& transform,
                         const std::optional<Rect>& within) {
    ++layerCount;
    float lx = bounds.x * scale - ox_, ly = bounds.y * scale - oy_, lw = bounds.width * scale, lh = bounds.height * scale;
    // As many texels for each of its pixels as the transform enlarges it by at most (no more than
    // fit a texture).
    float r = 1;
    for (int c = 0; c < 4; ++c) r = std::max(r, rasterScale(c & 1 ? lx + lw : lx, c & 2 ? ly + lh : ly));
    if (r > 1 && std::max(lw, lh) * r > float(kMaxLayer)) r = std::max(1.f, float(kMaxLayer) / std::max(lw, lh));
    int x0 = int(std::floor(lx * r)), y0 = int(std::floor(ly * r));
    int x1 = int(std::ceil((lx + lw) * r)), y1 = int(std::ceil((ly + lh) * r));
    if (within && (x1 - x0 > kMaxLayer || y1 - y0 > kMaxLayer)) {
        x0 = std::max(x0, int(std::floor(within->x))), y0 = std::max(y0, int(std::floor(within->y)));
        x1 = std::min(x1, int(std::ceil(within->right()))), y1 = std::min(y1, int(std::ceil(within->bottom())));
    }
    Layer layer{nullptr, {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)}, {lx * r, ly * r, lw * r, lh * r}, radii.scaled(scale * r),
                alpha, transform, r, scale};
    if (layer.rect.width <= kMaxLayer && layer.rect.height <= kMaxLayer) layer.texture = acquire(layer.rect.width, layer.rect.height);
    // Too big: drawn straight into the target (no rounding, alpha or transform), in its clips.
    if (layer.texture) {
        beginTarget(*layer.texture, ClearColor{});
        scale *= r;
        ox_ = targets_.back().ox * r + float(x0);
        oy_ = targets_.back().oy * r + float(y0);
        // Its pixels in the parent's before its transform (x / r + the rect's corner), through the
        // layer's own transform (scaled and turned about its origin, moved), then the parent's.
        const Target& parent = targets_.back();
        float ox = (layer.shape.x + layer.shape.width * transform.origin.x) / r, oy = (layer.shape.y + layer.shape.height * transform.origin.y) / r;
        float rad = transform.rotationZ * std::numbers::pi_v<float> / 180, cs = std::cos(rad), sn = std::sin(rad);
        float a = cs * transform.scaleX, b = sn * transform.scaleX, c = -sn * transform.scaleY, d = cs * transform.scaleY;
        Projective own{{a, c, ox - (a * ox + c * oy) + transform.translationX * layer.parentScale, b, d, oy - (b * ox + d * oy) + transform.translationY * layer.parentScale,
                        0, 0, 1}};
        Projective fromTexels{{1 / r, 0, float(x0) / r, 0, 1 / r, float(y0) / r, 0, 0, 1}};
        windowFromTarget_ = parent.windowFromTarget * parent.transform * own * fromTexels;
    }
    layers_.push_back(layer);
}

// Too big for a texture: the part around what can show, as far as the effect brings what is
// there into it (r is 1 then).
void Renderer::pushEffectLayer(const Rect& bounds, const RenderEffect& effect, float alpha) {
    std::optional<Rect> within;
    IRect visible = scissor();
    Projective back = transform_.inverse();
    float l = 1e30f, t = 1e30f, r = -1e30f, b = -1e30f;
    bool seen = true;
    for (int c = 0; c < 4 && seen; ++c) {
        float w;
        Offset q = back.apply(float(c & 1 ? visible.x + visible.width : visible.x), float(c & 2 ? visible.y + visible.height : visible.y), &w);
        seen = w > 0;
        l = std::min(l, q.x), t = std::min(t, q.y), r = std::max(r, q.x), b = std::max(b, q.y);
    }
    if (seen) {
        Rect out = effect.outputBounds(bounds);  // (how far it takes what is drawn, either way)
        float reachX = std::max({bounds.x - out.x, out.right() - bounds.right(), 0.f}) * scale + 1;
        float reachY = std::max({bounds.y - out.y, out.bottom() - bounds.bottom(), 0.f}) * scale + 1;
        within = Rect{l - reachX, t - reachY, r - l + 2 * reachX, b - t + 2 * reachY};
    }
    pushLayer(bounds, CornerRadii(), alpha, {}, within);
    if (layers_.back().texture) layers_.back().effect = effect;
}

// The pass drawn into the target so far runs first (ended here: what follows goes into a pass of its
// own after), then what it reads is copied out, goes through the effect as a layer's texture would
// (in the target's pixels: at the px per dp, whatever the transform), and is drawn back where the
// effect takes it in place of what is there (Src, mixed by the alpha), in the clips (which cut it
// to its shape).
void Renderer::drawBackdrop(const Rect& bounds, const RenderEffect& effect, float alpha) {
    if (targets_.empty() || alpha <= 0) return;
    Rect reads = effect.outputBounds(bounds);
    Target& t = targets_.back();
    Texture* target = passes_[t.pass].target;
    IRect all{0, 0, target->width(), target->height()};
    auto pixels = [&](const Rect& r) {  // the px it covers through the transform (all of them if behind the eye)
        std::optional<Rect> on = mapped(r.x * scale - ox_, r.y * scale - oy_, r.right() * scale - ox_, r.bottom() * scale - oy_);
        return on ? *on : Rect{0, 0, float(all.width), float(all.height)};
    };
    Rect on = pixels(reads), shape = pixels(bounds);
    IRect region = intersect({int(std::floor(on.x)), int(std::floor(on.y)), int(std::ceil(on.right())) - int(std::floor(on.x)),
                              int(std::ceil(on.bottom())) - int(std::floor(on.y))}, all);
    if (region.empty()) return;
    Texture* copy = acquire(region.width, region.height, target->format());
    if (!copy) return;
    order_.push_back(t.pass);
    Pass copying{copy, std::nullopt, {}, {}};
    copying.copy = std::pair{target, region};
    order_.push_back(passes_.size());
    passes_.push_back(std::move(copying));
    t.pass = passes_.size();
    passes_.push_back({target, std::nullopt, {}, {}});
    Layer layer{copy, region, shape, CornerRadii(), alpha, {}, 1, scale};
    layer.effect = effect;
    EffectImage image = applyEffect(layer);
    const Texture& from = *image.texture;
    float x0 = float(image.shown.x) + image.offsetX, y0 = float(image.shown.y) + image.offsetY;
    float x1 = x0 + float(image.shown.width), y1 = y0 + float(image.shown.height);
    float tx = float(image.originX) + image.offsetX, ty = float(image.originY) + image.offsetY;
    Prim p;
    set(p.bounds, x0 - 1, y0 - 1, x1 + 1, y1 + 1);
    set(p.shape, x0, y0, x1, y1);
    set(p.params, tx, ty, tx + float(image.texelsX * image.stepX), ty + float(image.texelsY * image.stepY));
    set(p.uv, 0, 0, 1 / float(from.width() * image.stepX), 1 / float(from.height() * image.stepY));
    set(p.color, 0, 0, 0, alpha);
    p.extra[3] = 8;  // (in place of what is there, mixed with it by the alpha)
    blends_.push_back(BlendMode::Src);
    add(LAYER, p, image.texture, false, Mask::None, nullptr, true);
    blends_.pop_back();
}

namespace {

// Skia's SkDrawShadowMetrics::GetSpotParams: a caster `z` above the plane under the light (x, y, lz)
// of `radius` casts its spot shadow scaled by `scale` about the light's foot and moved by
// `offset`, blurred by `blur` (all in the same px).
struct SpotParams {
    float blur, scale;
    Offset offset;
};
SpotParams spotParams(float z, float lx, float ly, float lz, float radius) {
    float ratio = lz - z > 0 ? std::clamp(z / (lz - z), 0.f, 0.95f) : 0.95f;
    float scale = lz - z > 0 ? std::clamp(lz / (lz - z), 1.f, 1.95f) : 1.95f;
    return {radius * ratio, scale, {-ratio * lx, -ratio * ly}};
}

// Skia's SkDrawShadowMetrics::GetSpotShadowTransform: the map from the caster's coordinates to the
// target's px of its spot shadow (cast from the light through its corners onto the plane under
// it), and its blur; nothing where the light isn't above it.
std::optional<std::pair<Projective, float>> spotShadowTransform(Offset light, float lz, float radius, const Projective& ctm,
                                                               const std::array<float, 3>& plane, const Rect& bounds) {
    auto height = [&](float x, float y) { return plane[0] * x + plane[1] * y + plane[2]; };
    Offset c = bounds.center();
    float z = height(c.x, c.y);
    if (ctm.affine()) {
        SpotParams p = spotParams(z, light.x, light.y, lz, radius);
        return std::pair{Projective{{p.scale, 0, p.offset.x, 0, p.scale, p.offset.y, 0, 0, 1}} * ctm, p.blur};
    }
    if (bounds.width < 1e-6f || bounds.height < 1e-6f) return std::nullopt;
    // The bounds' corners in 3D, cast from the light onto z = 0.
    std::array<std::array<double, 3>, 4> q;
    const Offset corners[4] = {{bounds.x, bounds.y}, {bounds.right(), bounds.y}, {bounds.right(), bounds.bottom()}, {bounds.x, bounds.bottom()}};
    for (int i = 0; i < 4; ++i) {
        float w;
        Offset d = ctm.apply(corners[i].x, corners[i].y, &w);
        double h = height(corners[i].x, corners[i].y), dz = lz - h;
        if (dz <= 1e-6) return std::nullopt;
        double ratio = h / dz;
        q[size_t(i)] = {d.x - (light.x - d.x) * ratio, d.y - (light.y - d.y) * ratio, 1};
    }
    auto cross = [](const std::array<double, 3>& a, const std::array<double, 3>& b) {
        return std::array<double, 3>{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    // The square [-1, 1]^2 onto the cast quad: its edges' crossings the new axes, its diagonals' the origin.
    auto h0 = cross(cross(q[1], q[0]), cross(q[2], q[3]));
    auto h1 = cross(cross(q[0], q[3]), cross(q[1], q[2]));
    auto h2 = cross(cross(q[0], q[2]), cross(q[1], q[3]));
    if (std::abs(h2[2]) < 1e-12) return std::nullopt;
    // Which way round: (-1, -1) to the first corner, as Skia means it to (its own check, which
    // takes the crossing points undivided by their w, leaves the shadow mirrored in some
    // perspectives: a triangle's, say). Turning an axis round takes it to another of the square's.
    auto off = [&](double x, double y) {
        double w = h0[2] * x + h1[2] * y + h2[2];
        return std::hypot((h0[0] * x + h1[0] * y + h2[0]) / w - q[0][0], (h0[1] * x + h1[1] * y + h2[1]) / w - q[0][1]);
    };
    double best = 1e300;
    int signs = 0;
    for (int s = 0; s < 4; ++s) {
        double e = off(s & 1 ? 1 : -1, s & 2 ? 1 : -1);
        if (e < best) best = e, signs = s;
    }
    if (signs & 1) h0 = {-h0[0], -h0[1], -h0[2]};
    if (signs & 2) h1 = {-h1[0], -h1[1], -h1[2]};
    Projective toQuad{{float(h0[0] / h2[2]), float(h1[0] / h2[2]), float(h2[0] / h2[2]), float(h0[1] / h2[2]), float(h1[1] / h2[2]),
                       float(h2[1] / h2[2]), float(h0[2] / h2[2]), float(h1[2] / h2[2]), 1}};
    float sx = 2 / bounds.width, sy = 2 / bounds.height;
    Projective toSquare{{sx, 0, -sx * bounds.x - 1, 0, sy, -sy * bounds.y - 1, 0, 0, 1}};
    float ratio = lz - z > 0 ? std::clamp(z / (lz - z), 0.f, 0.95f) : 0.95f;
    return std::pair{toQuad * toSquare, radius * ratio};
}

Matrix matrixOf(const Projective& p) {
    Matrix m;
    m.values[Matrix::ScaleX] = p.m[0], m.values[Matrix::SkewX] = p.m[1], m.values[Matrix::TranslateX] = p.m[2];
    m.values[Matrix::SkewY] = p.m[3], m.values[Matrix::ScaleY] = p.m[4], m.values[Matrix::TranslateY] = p.m[5];
    m.values[Matrix::Perspective0] = p.m[6], m.values[Matrix::Perspective1] = p.m[7], m.values[Matrix::Perspective2] = p.m[8];
    return m;
}

} // namespace

// As Android draws it (HWUI, then Skia's SkShadowUtils on Ganesh), in this target's px (a layer's
// resolution makes them more than Android's: its lengths, the light's too, taken up with them), the
// light mapped into them from the window's. A rounded rect of one radius (or a rect, a circle),
// flat, drawn through a similarity: analytically, as drawFastShadow and ShadowRRectOp do it (an
// ambient shadow the outline spread by r = z / 2 px, up to 150, its falloff b = r (1 + z / 128)
// in from there; a spot one the outline cast from the light, spread by its blur and falling off
// over twice that; each left out under the caster where it is opaque). Otherwise as Skia blurs
// when it can do nothing better (see GraphicsContext::drawElevationShadow), in the target's px.
void Renderer::drawElevationShadow(const Outline& outline, Offset origin, const ElevationShadow& shadow) {
    float alpha = std::clamp(shadow.alpha, 0.f, 1.f);
    Color ambient = shadow.ambientColor.copy(shadow.ambientColor.a * light.ambientAlpha * alpha);
    Color spot = shadow.spotColor.copy(shadow.spotColor.a * light.spotAlpha * alpha);
    if (targets_.empty() || (ambient.a <= 0 && spot.a <= 0)) return;
    bool transparent = alpha < 1;
    float k = light.density, u = scale / k;  // the window's px a dp; this target's px an Android px
    float w = 0;
    Offset lightPx = windowFromTarget_.inverse().apply(light.center.x * k, light.center.y * k, &w);
    float lz = light.z * k * u, lr = light.radius * k * u;
    Rect b = outline.bounds();
    b.x += origin.x, b.y += origin.y;
    if (b.width <= 0 || b.height <= 0) return;
    bool flat = std::abs(shadow.plane[0]) < 1e-6f && std::abs(shadow.plane[1]) < 1e-6f;
    std::optional<float> radius;
    if (outline.kind == Outline::Kind::Rectangle) radius = 0;
    else if (outline.kind == Outline::Kind::Rounded && outline.roundRect.circular()) {
        CornerRadii r = outline.roundRect.radii();
        if (std::abs(r.topLeft - r.topRight) < 1e-3f && std::abs(r.topLeft - r.bottomRight) < 1e-3f && std::abs(r.topLeft - r.bottomLeft) < 1e-3f)
            radius = std::min(r.topLeft, std::min(b.width, b.height) / 2);
    }
    const Projective& t = transform_;
    float a = t.m[0], c = t.m[1], bb = t.m[3], d = t.m[4];
    float len0 = std::hypot(a, bb), len1 = std::hypot(c, d);
    bool similar = t.affine() && std::abs(t.m[8] - 1) < 1e-6f && len0 > 0 && std::abs(len0 - len1) < 1e-4f * len0 && std::abs(a * c + bb * d) < 1e-4f * len0 * len0;
    if (flat && radius && similar && w > 0) {
        float toLocal = 1 / len0;  // the target's px in the prim's
        Rect r{b.x * scale - ox_, b.y * scale - oy_, b.width * scale, b.height * scale};
        float rr = *radius * scale;
        auto draw = [&](Rect at, float corner, float blur, float hole, Color color) {
            Prim p;
            set(p.shape, at.x, at.y, at.right(), at.bottom());
            set(p.bounds, at.x - 1, at.y - 1, at.right() + 1, at.bottom() + 1);
            set(p.radii, corner, corner, corner, corner);
            set(p.params, blur, hole, 0, 0);
            premultiplied(p.color, color);
            add(ELEVATION, p);
        };
        float zA = shadow.plane[2] * k;  // Android's px
        if (ambient.a > 0 && zA > 0) {
            float inset = std::min(zA / 2, 150.f) * u, blur = inset * (1 + zA / 128);
            float out = inset * toLocal;
            draw({r.x - out, r.y - out, r.width + 2 * out, r.height + 2 * out}, rr + out, blur * toLocal, transparent ? -1 : out, ambient);
        }
        if (spot.a > 0 && zA > 0) {
            SpotParams sp = spotParams(zA * u, lightPx.x, lightPx.y, lz, lr);
            Offset off{sp.offset.x + sp.scale * t.m[2], sp.offset.y + sp.scale * t.m[5]};
            Offset local = t.inverse().apply(off.x, off.y);  // (an affine map's inverse, as a point)
            Rect cast{r.x * sp.scale + local.x, r.y * sp.scale + local.y, r.width * sp.scale, r.height * sp.scale};
            float castRadius = rr * sp.scale, blur = sp.blur * toLocal;
            float maxOffset;
            if (rr <= 0) {
                maxOffset = std::max({std::abs(cast.x - r.x), std::abs(cast.y - r.y), std::abs(cast.right() - r.right()), std::abs(cast.bottom() - r.bottom())});
            } else {
                float dr = castRadius - rr;
                Offset upperLeft{cast.x - r.x + dr, cast.y - r.y + dr}, lowerRight{cast.right() - r.right() - dr, cast.bottom() - r.bottom() - dr};
                maxOffset = std::sqrt(std::max(upperLeft.x * upperLeft.x + upperLeft.y * upperLeft.y, lowerRight.x * lowerRight.x + lowerRight.y * lowerRight.y)) + dr;
            }
            float inset = blur + std::max(blur, maxOffset);
            draw({cast.x - blur, cast.y - blur, cast.width + 2 * blur, cast.height + 2 * blur}, castRadius + blur, 2 * sp.blur * toLocal,
                 transparent ? -1 : inset, spot);
        }
        return;
    }
    // Blurred, in the target's px: the outline there, and the map from dp to there.
    Path path = outline.asPath();
    std::vector<Contour> device = polygonsOf(path, origin, {});
    Projective ctm = t * Projective{{scale, 0, -ox_, 0, scale, -oy_, 0, 0, 1}};
    float s0 = scale, x0 = ox_, y0 = oy_;
    Projective t0 = transform_;
    scale = 1, ox_ = 0, oy_ = 0, transform_ = {};
    Path flatOutline;  // (its px, through the transform: what Skia strokes)
    float l = 1e30f, top = 1e30f, rgt = -1e30f, bot = -1e30f;
    for (const Contour& cn : device) {
        for (size_t i = 0; i < cn.points.size(); ++i) {
            Offset q = cn.points[i];
            if (i == 0) flatOutline.moveTo(q.x, q.y);
            else flatOutline.lineTo(q.x, q.y);
            l = std::min(l, q.x), top = std::min(top, q.y), rgt = std::max(rgt, q.x), bot = std::max(bot, q.y);
        }
        flatOutline.close();
    }
    float zA = (shadow.plane[0] * origin.x + shadow.plane[1] * origin.y + shadow.plane[2]) * k;  // (at the outline's origin, as Skia's)
    if (ambient.a > 0 && zA > 0 && l <= rgt) {
        float outset = std::min(zA / 2, 150.f) * u, oneOverA = 1 + zA / 128, blur = 0.5f * outset * oneOverA;
        float stroke = 0.5f * (outset - blur);
        float grow = std::max(0.f, stroke) * 2;  // (mitered: a little more)
        Rect at{l - grow, top - grow, rgt - l + 2 * grow, bot - top + 2 * grow};
        pushEffectLayer(at, BlurEffect(blur, blur, TileMode::Decal), ambient.a);
        drawPath(flatOutline, {0, 0}, Brush::solid(ambient.copy(1)), at, {});
        if (stroke > 0) drawPath(flatOutline, {0, 0}, Brush::solid(ambient.copy(1)), at, PathStroke{stroke});
        popLayer();
    }
    if (spot.a > 0) {
        std::array<float, 3> plane{shadow.plane[0] * k * u, shadow.plane[1] * k * u, shadow.plane[2] * k * u};  // (z in the target's px)
        Rect pb = path.bounds();
        pb.x += origin.x, pb.y += origin.y;
        if (auto cast = spotShadowTransform(lightPx, lz, lr, ctm, plane, pb); cast && w > 0) {
            Projective m = cast->first;
            float cl = 1e30f, ct = 1e30f, cr = -1e30f, cb = -1e30f;
            bool seen = true;
            for (int i = 0; i < 4 && seen; ++i) {
                float cw;
                Offset q = m.apply(i & 1 ? pb.right() : pb.x, i & 2 ? pb.bottom() : pb.y, &cw);
                seen = cw > 0;
                cl = std::min(cl, q.x), ct = std::min(ct, q.y), cr = std::max(cr, q.x), cb = std::max(cb, q.y);
            }
            if (seen) {
                Rect at{cl - 1, ct - 1, cr - cl + 2, cb - ct + 2};
                pushEffectLayer(at, BlurEffect(cast->second, cast->second, TileMode::Decal), spot.a);
                pushTransform(matrixOf(m));
                drawPath(path, origin, Brush::solid(spot.copy(1)), pb, {});
                popTransform();
                popLayer();
            }
        }
    }
    scale = s0, ox_ = x0, oy_ = y0, transform_ = t0;
}

void Renderer::effectPass(Texture& target, Texture& source, const IRect& rect, const float (&uniforms)[32], bool linear, bool table,
                          Texture* mask) {
    Pass pass{&target, std::nullopt, {}, {}};
    pass.effect = EffectDraw{&source, rect, {}, linear, table, mask};
    std::copy(std::begin(uniforms), std::end(uniforms), pass.effect->uniforms);
    order_.push_back(passes_.size());
    passes_.push_back(std::move(pass));
}

// Each blur: its first pass makes the pixels past what it blurs as its tile mode says, within
// what the blur reads, in a texture scaled down by powers of two until the sigma left is at most
// 4 texels (as Skia's does for a wide one: no more than 25 taps); then a Gaussian across and
// down. The variance the scaling adds (the mean of each block, and the bilinear filtering that
// scales the last one back up) is taken off the sigma. Offsets are the composite's.
int Renderer::tableRow(const std::vector<std::pair<float, float>>& stops) {
    int row = int(stops_.size() / (kStopsWidth * 4));
    size_t at = stops_.size();
    stops_.resize(at + kStopsWidth * 4, 0.f);
    for (size_t i = 0; i < stops.size() && i < size_t(kStopsWidth); ++i) stops_[at + i * 4] = stops[i].first, stops_[at + i * 4 + 1] = stops[i].second;
    return row;
}

// As Compose's: the radius at each pixel the widest one (no more than 150 px) times where it lies
// between the stops (their radii over the widest); the layer's pixels cut to its bounds first, as
// what lies past them is none of it; the bounds (its pixels, the layer's own) are where its taps
// count. What it makes reaches as far as its widest radius past them. A shader's (a mask) is drawn
// first into a texture of what the blur makes, in half floats (8 bits would move where a radius
// reaches a whole pixel), each texel its alpha where it lies (its dp from the layer's top left):
// the radius there the widest times that.
Renderer::EffectImage Renderer::progressiveBlur(const Layer& layer, const RenderEffect::Step& step) {
    const RenderEffect::Progressive& p = *step.progressive;
    float res = layer.resolution, most = 0;
    for (const auto& [at, radius] : p.stops) most = std::max(most, radius);
    float widest = std::min(most * scale, 150.f);  // px
    EffectImage image{layer.texture, layer.rect.x, layer.rect.y, 1, 1, layer.rect.width, layer.rect.height, layer.rect};
    if (!(widest > 0)) return image;
    int ext = int(std::floor(widest) * res) + 2;
    const IRect& crop = layer.rect;
    IRect region{crop.x - ext, crop.y - ext, crop.width + 2 * ext, crop.height + 2 * ext};
    Texture* cut = acquire(region.width, region.height);
    if (!cut) return image;
    float c[32] = {0, 0, 0, 0, 1, 1, float(region.x - crop.x), float(region.y - crop.y), 0, 0, float(crop.width), float(crop.height),
                   0, float(int(TileMode::Decal)), 0, 0, 0, 0, 1 / float(layer.texture->width()), 1 / float(layer.texture->height())};
    effectPass(*cut, *layer.texture, {0, 0, region.width, region.height}, c);
    std::vector<std::pair<float, float>> stops;
    for (const auto& [at, radius] : p.stops) stops.emplace_back(at, radius / most);
    int row = tableRow(stops);
    float k = scale * res;  // texels a dp
    float ox = layer.shape.x - float(region.x), oy = layer.shape.y - float(region.y);
    Texture* mask = nullptr;
    if (p.shader) {
        mask = acquire(region.width, region.height, Format::RGBA16F);
        if (!mask) return image;
        float s0 = scale;
        beginTarget(*mask, ClearColor{});
        // (The region in the layer's dp, its top left where the shader is taken from: a
        // composite's bake covers all of it.)
        scale = k, ox_ = -ox, oy_ = -oy;
        Matrix shift;
        shift.translate(ox / k, oy / k);
        Prim q = paintedShader(p.shader->transformed(shift), {-ox / k, -oy / k, float(region.width) / k, float(region.height) / k}, 1, 1);
        set(q.bounds, -1, -1, float(region.width) + 1, float(region.height) + 1);
        set(q.shape, 0, 0, float(region.width), float(region.height));
        add(FILL, q);
        endTarget();
        scale = s0;
    }
    Texture* from = cut;
    for (int axis = 0; axis < 2; ++axis) {
        Texture* to = acquire(region.width, region.height);
        if (!to) return image;
        float u[32] = {0, 0, 0, 0, 1, 1, 0, 0,
                       float(crop.x - region.x), float(crop.y - region.y), float(crop.x + crop.width - region.x), float(crop.y + crop.height - region.y),
                       2, float(int(step.edgeTreatment)), float(axis), widest, res, 0, 1 / float(from->width()), 1 / float(from->height()),
                       mask ? 3.f : p.radial ? 2.f : 1.f, float(row), float(std::min<size_t>(stops.size(), kStopsWidth)), 0,
                       p.start.x * k, p.start.y * k, p.radial ? p.radius * k : p.end.x * k, p.radial ? 0 : p.end.y * k, ox, oy, 0, 0};
        effectPass(*to, *from, {0, 0, region.width, region.height}, u, true, true, mask);
        from = to;
    }
    return {from, region.x, region.y, 1, 1, region.width, region.height, region};
}

Renderer::EffectImage Renderer::applyEffect(const Layer& layer) {
    float res = layer.resolution;
    EffectImage image{layer.texture, layer.rect.x, layer.rect.y, 1, 1, layer.rect.width, layer.rect.height, layer.rect};
    const std::vector<RenderEffect::Step>& steps = layer.effect->steps();
    size_t first = 0;
    if (!steps.empty() && steps[0].progressive) image = progressiveBlur(layer, steps[first++]);
    struct Blur {
        float sigmaX, sigmaY;
        int stepX, stepY;
        TileMode tile;
    };
    std::vector<Blur> blurs;
    int stepX = 1, stepY = 1;
    auto level = [](float sigma, int at) {
        while (sigma / float(at) > 4) at *= 2;
        return at;
    };
    for (size_t i = first; i < steps.size(); ++i) {
        const RenderEffect::Step& s = steps[i];
        if (s.kind == RenderEffect::Step::Kind::ProgressiveBlur) continue;  // (only ever the first)
        if (s.kind == RenderEffect::Step::Kind::Offset) {
            image.offsetX += s.offset.x * scale * res, image.offsetY += s.offset.y * scale * res;
            continue;
        }
        // Android's sigma for the radius in the parent's pixels (no more than Skia's 532), in the layer's.
        float sx = s.radiusX > 0 ? std::min(0.57735f * s.radiusX * scale + 0.5f, 532.f) * res : 0;
        float sy = s.radiusY > 0 ? std::min(0.57735f * s.radiusY * scale + 0.5f, 532.f) * res : 0;
        if (sx <= 0 && sy <= 0) continue;
        stepX = level(sx, stepX), stepY = level(sy, stepY);
        blurs.push_back({sx, sy, stepX, stepY, s.edgeTreatment});
    }
    // What is left of each sigma at its level: its variance less what scaling down to it adds,
    // and the last ones less what scaling back up adds (as much of that as each has, the last first).
    std::vector<float> left(blurs.size() * 2);
    for (int axis = 0; axis < 2; ++axis) {
        int last = axis ? stepY : stepX;
        float up = float(last * last - 1) / 6;
        for (size_t k = blurs.size(); k-- > 0;) {
            float sigma = axis ? blurs[k].sigmaY : blurs[k].sigmaX;
            int step = axis ? blurs[k].stepY : blurs[k].stepX, before = k ? (axis ? blurs[k - 1].stepY : blurs[k - 1].stepX) : 1;
            float variance = std::max(0.f, sigma * sigma - float(step * step - before * before) / 12);
            float taken = sigma > 0 ? std::min(variance, up) : 0;
            up -= taken;
            left[k * 2 + size_t(axis)] = sigma > 0 ? std::sqrt(variance - taken) / float(step) : 0;
        }
    }
    for (size_t k = 0; k < blurs.size(); ++k) {
        const Blur& blur = blurs[k];
        float gx = left[k * 2], gy = left[k * 2 + 1];
        // What it shows: three sigmas past what it blurs (whole pixels, as Skia's).
        int ex = blur.sigmaX > 0 ? int(std::ceil(3 * blur.sigmaX)) : 0, ey = blur.sigmaY > 0 ? int(std::ceil(3 * blur.sigmaY)) : 0;
        IRect shown{image.shown.x - ex, image.shown.y - ey, image.shown.width + 2 * ex, image.shown.height + 2 * ey};
        // What it works out: the texels the filtering scaling it back up reads (one past what
        // shows) and as far past them as the kernel reaches, on the image's texels.
        int mx = (int(std::ceil(3 * gx)) + 1) * blur.stepX, my = (int(std::ceil(3 * gy)) + 1) * blur.stepY;
        int ox = image.originX + int(std::floor(float(shown.x - mx - image.originX) / float(image.stepX))) * image.stepX;
        int oy = image.originY + int(std::floor(float(shown.y - my - image.originY) / float(image.stepY))) * image.stepY;
        int nx = (shown.x + shown.width + mx - ox + blur.stepX - 1) / blur.stepX, ny = (shown.y + shown.height + my - oy + blur.stepY - 1) / blur.stepY;
        // The image's own texels (whole ones of it).
        float vx0 = std::floor(float(image.shown.x - image.originX) / float(image.stepX));
        float vy0 = std::floor(float(image.shown.y - image.originY) / float(image.stepY));
        float vx1 = std::ceil(float(image.shown.x + image.shown.width - image.originX) / float(image.stepX));
        float vy1 = std::ceil(float(image.shown.y + image.shown.height - image.originY) / float(image.stepY));
        // Scaled down at most 8 times each way in a pass, the first one tiling.
        Texture* from = image.texture;
        int fx = blur.stepX / image.stepX, fy = blur.stepY / image.stepY, atX = image.stepX, atY = image.stepY;
        float mapX = float((ox - image.originX) / image.stepX), mapY = float((oy - image.originY) / image.stepY);
        float tile = float(int(blur.tile));
        bool first = true;
        while (first || fx > 1 || fy > 1) {
            int px = std::min(fx, 8), py = std::min(fy, 8);
            fx /= px, fy /= py, atX *= px, atY *= py;
            int w = (shown.x + shown.width + mx - ox + atX - 1) / atX, h = (shown.y + shown.height + my - oy + atY - 1) / atY;
            Texture* to = acquire(w, h);
            if (!to) return image;
            float u[32] = {0, 0, 0, 0, float(px), float(py), mapX, mapY, vx0, vy0, vx1, vy1, 0, tile, 0, 0,
                           0, 0, 1 / float(from->width()), 1 / float(from->height())};
            effectPass(*to, *from, {0, 0, w, h}, u);
            // The next pass reads all of this one's, nothing past it.
            from = to, mapX = mapY = 0, vx0 = vy0 = 0, vx1 = float(w), vy1 = float(h), tile = float(int(TileMode::Decal));
            first = false;
        }
        for (int axis = 0; axis < 2; ++axis) {
            float g = axis ? gy : gx;
            if (g <= 0) continue;
            Texture* to = acquire(nx, ny);
            if (!to) return image;
            float u[32] = {0, 0, 0, 0, 1, 1, 0, 0, 0, 0, float(nx), float(ny), 1, float(int(TileMode::Decal)), float(axis), g,
                           std::ceil(3 * g), 0, 1 / float(from->width()), 1 / float(from->height())};
            effectPass(*to, *from, {0, 0, nx, ny}, u);
            from = to;
        }
        image = {from, ox, oy, blur.stepX, blur.stepY, nx, ny, shown, image.offsetX, image.offsetY};
    }
    return image;
}

void Renderer::popLayer() {
    Layer layer = layers_.back();
    layers_.pop_back();
    if (!layer.texture) return;
    endTarget();
    scale = layer.parentScale;
    float res = layer.resolution;
    if (layer.effect) {
        // What the effect made, its pixels where they show, back in the parent's pixels.
        EffectImage image = applyEffect(layer);
        const Texture& t = *image.texture;
        float x0 = float(image.shown.x) + image.offsetX, y0 = float(image.shown.y) + image.offsetY;
        float x1 = x0 + float(image.shown.width), y1 = y0 + float(image.shown.height);
        float tx = float(image.originX) + image.offsetX, ty = float(image.originY) + image.offsetY;
        Prim p;
        set(p.bounds, x0 - 1, y0 - 1, x1 + 1, y1 + 1);
        set(p.shape, x0, y0, x1, y1);
        set(p.params, tx, ty, tx + float(image.texelsX * image.stepX), ty + float(image.texelsY * image.stepY));
        set(p.uv, 0, 0, 1 / float(t.width() * image.stepX), 1 / float(t.height() * image.stepY));
        set(p.color, 0, 0, 0, layer.alpha);
        set(p.xform, 1 / res, 0, 0, 1 / res);
        add(LAYER, p, image.texture);
        return;
    }
    Texture& t = *layer.texture;
    const Rect& b = layer.shape;
    float half = std::min(b.width, b.height) / 2;
    auto clamped = [half](float r) { return std::clamp(r, 0.f, half); };
    CornerRadii r{clamped(layer.radii.topLeft), clamped(layer.radii.topRight), clamped(layer.radii.bottomRight), clamped(layer.radii.bottomLeft)};
    const LayerTransform& tf = layer.transform;
    bool square = r.max() < 0.5f;
    float rx0 = float(layer.rect.x), ry0 = float(layer.rect.y), rx1 = rx0 + float(layer.rect.width), ry1 = ry0 + float(layer.rect.height);
    Prim p;
    // Cut to an outline: its coverage, rasterized in the layer's pixels before its transform.
    std::optional<Offset> outline;
    if (layer.outline) {
        std::vector<Contour> contours = flattenPath(layer.outline->commands(), {{0, 0}, scale * res, {-b.x, -b.y}});
        outline = rasterizeInClips(contours, layer.rect);
        if (!outline) return;
        square = true;  // (its pixels, cut by the mask)
    }
    // Square: all of its pixels. Rounded: its exact bounds, edges anti-aliased.
    if (square) {
        set(p.bounds, rx0, ry0, rx1, ry1);
        set(p.shape, rx0, ry0, rx1, ry1);
        if (!tf.identity() || res != 1 || !transform_.axisAligned()) set(p.bounds, rx0 - 1, ry0 - 1, rx1 + 1, ry1 + 1);
    } else {
        set(p.bounds, b.x - 1, b.y - 1, b.right() + 1, b.bottom() + 1);
        set(p.shape, b.x, b.y, b.right(), b.bottom());
        set(p.radii, r.topLeft, r.topRight, r.bottomRight, r.bottomLeft);
    }
    set(p.params, rx0, ry0, rx1, ry1);
    set(p.uv, 0, 0, 1 / float(t.width()), 1 / float(t.height()));
    set(p.color, 0, 0, 0, layer.alpha);
    // Back to the parent's pixels (the texture's are `res` times as many), scaled and turned about
    // the origin, then moved: x' = M (x / res - o) + o + t.
    float ox = (b.x + b.width * tf.origin.x) / res, oy = (b.y + b.height * tf.origin.y) / res;
    float rad = tf.rotationZ * std::numbers::pi_v<float> / 180, cs = std::cos(rad), sn = std::sin(rad);
    float a = cs * tf.scaleX, bb = sn * tf.scaleX, c = -sn * tf.scaleY, d = cs * tf.scaleY;
    set(p.xform, a / res, bb / res, c / res, d / res);
    p.extra[0] = ox - (a * ox + c * oy) + tf.translationX * scale;
    p.extra[1] = oy - (bb * ox + d * oy) + tf.translationY * scale;
    if (outline) {
        p.radii[0] = outline->x, p.radii[1] = outline->y;
        p.extra[3] = 1;
        set(p.bounds, rx0, ry0, rx1, ry1);  // (no further: the mask goes no further)
        add(LAYER, p, &t, false, Mask::Texture1);
        return;
    }
    add(LAYER, p, &t);
}

} // namespace kompoz::gpu
