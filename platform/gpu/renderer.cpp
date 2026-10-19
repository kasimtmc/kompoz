// The GPU renderer (see renderer.hpp).

#include "renderer.hpp"

#include "shaders/generated/path.h"
#include "shaders/generated/primitives.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace kompoz::gpu {
namespace {

// shaders/primitives.slang's kinds.
enum Kind { FILL = 0, STROKE = 1, LINE = 2, ARC = 3, SHADOW = 4, GLYPH = 5, IMAGE = 6, LAYER = 7, PATH = 8 };

constexpr int kMaxLayer = 4096;  // px: a bigger layer is drawn straight into its target

const ShaderCode kVertex{shaders::primitives_vs_spirv, shaders::primitives_vs_msl,
                         shaders::primitives_vs_glsl, shaders::primitives_vs_wgsl, "vs"};
const ShaderCode kFragment{shaders::primitives_fs_spirv, shaders::primitives_fs_msl,
                           shaders::primitives_fs_glsl, shaders::primitives_fs_wgsl, "fs"};
const ShaderCode kCoverageVertex{shaders::path_vs_spirv, shaders::path_vs_msl, shaders::path_vs_glsl,
                                 shaders::path_vs_wgsl, "vs"};
const ShaderCode kCoverageFragment{shaders::path_fs_spirv, shaders::path_fs_msl, shaders::path_fs_glsl,
                                   shaders::path_fs_wgsl, "fs"};

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

Pipeline& Renderer::pipeline(Format target, const BlendState& blend) {
    PipelineKey key{target, blend};
    for (auto& [k, p] : pipelines_)
        if (k == key) return *p;
    PipelineDesc desc;
    desc.vertex = &kVertex;
    desc.fragment = &kFragment;
    for (uint32_t i = 0; i < 12; ++i) desc.instanceAttributes.push_back({i, VertexFormat::Float4, i * 16});
    desc.instanceStride = sizeof(Prim);
    desc.textures = 5;
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
    targets_.push_back({passes_.size(), ox_, oy_, transform_, std::move(clips_), std::move(placements_), std::move(blends_), std::move(filters_)});
    passes_.push_back({&target, clear, {}, {}});
    ox_ = oy_ = 0;
    transform_ = {};
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
    if (c.row >= 0 && (!c.paths || c.generation == shelf.generation)) return c.row;
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

void Renderer::add(int kind, const Prim& prim, Texture* texture, bool opaque, Mask mask, Texture* texture1) {
    if (targets_.empty()) return;  // (drawn outside beginTarget/endTarget)
    Pass& pass = passes_[targets_.back().pass];
    IRect clip = scissor();
    if (clip.empty()) return;
    BlendMode blend = blends_.empty() || opaque ? BlendMode::SrcOver : blends_.back();
    if (blend == BlendMode::Dst) return;  // (changes nothing)
    Prim p = prim;
    p.extra[2] = float(kind);
    if (kind != PATH && !opaque && !transform_.identity()) {
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
    if (!texture) texture = white_.get();
    if (!texture1) texture1 = white_.get();
    uint32_t generation = pass.shelf.generation;
    if (!pass.batches.empty()) {
        Batch& last = pass.batches.back();
        if (last.texture == texture && last.texture1 == texture1 && last.mask == mask && last.opaque == opaque && last.blend == blend &&
            last.scissor == clip && last.generation == generation) {
            pass.prims.push_back(p);
            ++last.count;
            return;
        }
    }
    pass.batches.push_back({texture, texture1, mask, opaque, blend, clip, generation, uint32_t(pass.prims.size()), 1});
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
            for (Texture* t : {b.mask == Mask::Texture0 ? mask_.get() : b.texture, b.mask == Mask::Texture1 ? mask_.get() : b.texture1})
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
                              0, 0, 0, 0};
        const Sampler nearest{Filter::Nearest};
        IRect all{0, 0, pass.target->width(), pass.target->height()};
        for (size_t i = firstBatch; i < batch; ++i) {
            const Batch& b = pass.batches[i];
            BlendPlan plan = planFor(b.blend, b.opaque);
            uniforms[8] = float(int(b.blend));
            uniforms[9] = float(plan.how);
            device_.setPipeline(pipeline(pass.target->format(), plan.state));
            device_.setUniforms(uniforms, sizeof uniforms);
            if (b.mask == Mask::Texture0)
                device_.setTexture(0, *mask_, nearest);
            else
                device_.setTexture(0, *b.texture, Sampler{});
            if (b.mask == Mask::Texture1)  // (a layer's cut, which may be turned: filtered)
                device_.setTexture(1, *mask_, Sampler{});
            else
                device_.setTexture(1, *b.texture1, Sampler{});
            device_.setTexture(2, stopsTexture_ ? *stopsTexture_ : *white_, nearest);
            device_.setTexture(3, readsTarget ? *dstCopy_ : *white_, nearest);
            device_.setTexture(4, masked ? *mask_ : *white_, nearest);
            if (!plan.readsTarget) {
                device_.setScissor(b.scissor);
                device_.draw({prims.buffer, prims.offset + uint64_t(b.first) * sizeof(Prim)}, 6, b.count);
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
                for (int c = 0; c < 4; ++c) {
                    float w;
                    Offset d = place.apply(q.bounds[c & 1 ? 2 : 0], q.bounds[c & 2 ? 3 : 1], &w);
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
                device_.draw({prims.buffer, prims.offset + uint64_t(k) * sizeof(Prim)}, 6, 1);
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

// A layer target at least w x h, not used yet this frame (one that is would still be read by the
// pass that composites it, which runs later). Sizes go up in 32 px steps, and a free texture up
// to 64 px bigger is reused, so a layer moving by fractions doesn't make a texture each frame.
Texture* Renderer::acquire(int width, int height) {
    PooledTexture* best = nullptr;
    for (auto& p : pool_) {
        int w = p.texture->width(), h = p.texture->height();
        if (!p.inUse && w >= width && h >= height && w <= width + 64 && h <= height + 64 &&
            (!best || w * h < best->texture->width() * best->texture->height()))
            best = &p;
    }
    if (!best) {
        auto t = device_.createTexture((width + 31) / 32 * 32, (height + 31) / 32 * 32, Format::RGBA8,
                                       TextureUsage::Sampled | TextureUsage::RenderTarget);
        if (!t) return nullptr;
        pool_.push_back({std::move(t)});
        best = &pool_.back();
    }
    best->inUse = true;
    return best->texture.get();
}

Texture* Renderer::imageTexture(const ImageBitmap& image) {
    if (image.empty()) return nullptr;
    if (auto it = images_.find(image.id()); it != images_.end()) return it->second.texture.get();
    auto t = device_.createTexture(image.width(), image.height(), Format::RGBA8, TextureUsage::Sampled);
    if (!t) return nullptr;
    device_.upload(*t, {0, 0, image.width(), image.height()}, image.pixels(), size_t(image.width()) * 4);
    Texture* raw = t.get();
    images_.emplace(image.id(), ImageTexture{std::move(t), image.token()});
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

Renderer::Prim Renderer::painted(const Brush& brush, const Rect& area, float alpha, float enlarged) {
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

void Renderer::drawImage(const ImageBitmap& image, const Rect& src, const Rect& dst, float alpha) {
    Texture* t = imageTexture(image);
    if (!t || alpha <= 0) return;
    float x = dst.x * scale - ox_, y = dst.y * scale - oy_;
    Prim p;
    set(p.bounds, x, y, x + dst.width * scale, y + dst.height * scale);
    std::copy(std::begin(p.bounds), std::end(p.bounds), p.shape);
    float iw = float(image.width()), ih = float(image.height());
    set(p.uv, src.x / iw, src.y / ih, src.right() / iw, src.bottom() / ih);
    set(p.params, alpha, 0, 0.5f / iw, 0.5f / ih);
    add(IMAGE, p, t);
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
    if (stroke.width > 0) contours = strokeOutline(contours, stroke.width * scale, stroke.cap, stroke.join, stroke.miter, place.tolerance);
    return transformed ? project(contours, transform_) : contours;
}

void Renderer::drawPath(const Path& path, Offset origin, const Brush& brush, const Rect& area, const PathStroke& stroke) {
    if (path.isEmpty()) return;
    std::vector<Contour> contours = polygonsOf(path, origin, stroke);
    bool evenOdd = stroke.width <= 0 && path.fillType == Path::FillType::EvenOdd;
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
    ++layerCount;
    float lx = bounds.x * scale - ox_, ly = bounds.y * scale - oy_, lw = bounds.width * scale, lh = bounds.height * scale;
    // As many texels for each of its pixels as the transform enlarges it by at most (no more than
    // fit a texture).
    float r = 1;
    for (int c = 0; c < 4; ++c) r = std::max(r, rasterScale(c & 1 ? lx + lw : lx, c & 2 ? ly + lh : ly));
    if (r > 1 && std::max(lw, lh) * r > float(kMaxLayer)) r = std::max(1.f, float(kMaxLayer) / std::max(lw, lh));
    int x0 = int(std::floor(lx * r)), y0 = int(std::floor(ly * r));
    int x1 = int(std::ceil((lx + lw) * r)), y1 = int(std::ceil((ly + lh) * r));
    Layer layer{nullptr, {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)}, {lx * r, ly * r, lw * r, lh * r}, radii.scaled(scale * r),
                alpha, transform, r, scale};
    if (layer.rect.width <= kMaxLayer && layer.rect.height <= kMaxLayer) layer.texture = acquire(layer.rect.width, layer.rect.height);
    // Too big: drawn straight into the target (no rounding, alpha or transform), in its clips.
    if (layer.texture) {
        beginTarget(*layer.texture, ClearColor{});
        scale *= r;
        ox_ = targets_.back().ox * r + float(x0);
        oy_ = targets_.back().oy * r + float(y0);
    }
    layers_.push_back(layer);
}

void Renderer::popLayer() {
    Layer layer = layers_.back();
    layers_.pop_back();
    if (!layer.texture) return;
    endTarget();
    scale = layer.parentScale;
    float res = layer.resolution;
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
