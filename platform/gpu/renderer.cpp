// The GPU renderer (see renderer.hpp).

#include "renderer.hpp"

#include "shaders/generated/primitives.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace kompoz::gpu {
namespace {

// shaders/primitives.slang's kinds.
enum Kind { FILL = 0, STROKE = 1, LINE = 2, ARC = 3, SHADOW = 4, GLYPH = 5, IMAGE = 6, LAYER = 7 };

constexpr int kMaxLayer = 4096;  // px: a bigger layer is drawn straight into its target

const ShaderCode kVertex{shaders::primitives_vs_spirv, shaders::primitives_vs_dxil, shaders::primitives_vs_msl,
                         shaders::primitives_vs_glsl, shaders::primitives_vs_wgsl, "vs"};
const ShaderCode kFragment{shaders::primitives_fs_spirv, shaders::primitives_fs_dxil, shaders::primitives_fs_msl,
                           shaders::primitives_fs_glsl, shaders::primitives_fs_wgsl, "fs"};

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

Pipeline& Renderer::pipeline(Format target, bool opaque) {
    for (auto& [key, p] : pipelines_)
        if (key == std::pair{target, opaque}) return *p;
    PipelineDesc desc;
    desc.vertex = &kVertex;
    desc.fragment = &kFragment;
    for (uint32_t i = 0; i < 8; ++i) desc.instanceAttributes.push_back({i, VertexFormat::Float4, i * 16});
    desc.instanceStride = sizeof(Prim);
    desc.textures = 1;
    desc.blend.enabled = !opaque;
    desc.target = target;
    pipelines_.emplace_back(std::pair{target, opaque}, device_.createPipeline(desc));
    return *pipelines_.back().second;
}

// --- Targets -------------------------------------------------------------------------------------

void Renderer::beginTarget(Texture& target, const std::optional<ClearColor>& clear) {
    targets_.push_back({passes_.size(), ox_, oy_, std::move(clips_), std::move(translations_)});
    passes_.push_back({&target, clear, {}, {}});
    ox_ = oy_ = 0;
    clips_.clear();
    translations_.clear();
}

void Renderer::endTarget() {
    Target t = std::move(targets_.back());
    targets_.pop_back();
    order_.push_back(t.pass);
    ox_ = t.ox, oy_ = t.oy;
    clips_ = std::move(t.clips);
    translations_ = std::move(t.translations);
}

IRect Renderer::scissor() const {
    if (targets_.empty()) return {};
    const Texture& t = *passes_[targets_.back().pass].target;
    IRect all{0, 0, t.width(), t.height()};
    return clips_.empty() ? all : intersect(clips_.back(), all);
}

void Renderer::add(int kind, const Prim& prim, Texture* texture, bool opaque) {
    if (targets_.empty()) return;  // (drawn outside beginTarget/endTarget)
    Pass& pass = passes_[targets_.back().pass];
    IRect clip = scissor();
    if (clip.empty()) return;
    Prim p = prim;
    p.extra[2] = float(kind);
    if (!texture) texture = white_.get();
    if (!pass.batches.empty()) {
        Batch& last = pass.batches.back();
        if (last.texture == texture && last.opaque == opaque && last.scissor == clip) {
            pass.prims.push_back(p);
            ++last.count;
            return;
        }
    }
    pass.batches.push_back({texture, opaque, clip, uint32_t(pass.prims.size()), 1});
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
    for (size_t index : order_) execute(passes_[index]);
    order_.clear();
    // Passes still being drawn into keep their places; the others go.
    if (targets_.empty()) passes_.clear();
}

void Renderer::execute(Pass& pass) {
    std::vector<Texture*> reads;
    for (const Batch& b : pass.batches)
        if (std::find(reads.begin(), reads.end(), b.texture) == reads.end()) reads.push_back(b.texture);
    device_.beginPass(*pass.target, pass.clear, reads);
    if (!pass.prims.empty()) {
        BufferRef ref;
        void* memory = device_.allocate(pass.prims.size() * sizeof(Prim), 16, &ref);
        std::memcpy(memory, pass.prims.data(), pass.prims.size() * sizeof(Prim));
        float w = float(pass.target->width()), h = float(pass.target->height());
        // Pixels (y down) to clip space.
        float toClip[4] = {2 / w, device_.clipYDown() ? 2 / h : -2 / h, -1, device_.clipYDown() ? -1.f : 1.f};
        Pipeline* bound = nullptr;
        for (const Batch& b : pass.batches) {
            Pipeline& p = pipeline(pass.target->format(), b.opaque);
            if (&p != bound) {
                device_.setPipeline(p);
                device_.setUniforms(toClip, sizeof toClip);
                bound = &p;
            }
            device_.setScissor(b.scissor);
            device_.setTexture(0, *b.texture, Sampler{});
            device_.draw({ref.buffer, ref.offset + uint64_t(b.first) * sizeof(Prim)}, 6, b.count);
        }
    }
    device_.endPass();
    pass.prims.clear();
    pass.batches.clear();
}

void Renderer::endFrame() {
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

void Renderer::glyphs(Color color) {
    constexpr float kPage = float(AtlasPage::kSize);
    for (const GlyphQuad& g : glyphs_) {
        Prim p;
        set(p.bounds, g.x0, g.y0, g.x1, g.y1);
        set(p.uv, float(g.u) / kPage, float(g.v) / kPage, float(g.u + g.width) / kPage, float(g.v + g.height) / kPage);
        premultiplied(p.color, color);
        p.params[0] = g.color ? 1.f : 0.f;
        add(GLYPH, p, pageTexture(g.page));
    }
}

void Renderer::drawText(std::string_view text, Offset baseline, const Font& font, Color color, bool rtl) {
    if (color.a <= 0) return;
    glyphs_.clear();
    text_.layout(text, baseline.x * scale - ox_, baseline.y * scale - oy_, font, rtl, glyphs_);
    glyphs(color);
}

void Renderer::drawTextBrush(std::string_view text, Offset baseline, const Font& font, const Brush& brush, const Rect& area, bool rtl) {
    // Each glyph in the brush's color at its middle (until brushes are drawn per pixel).
    glyphs_.clear();
    text_.layout(text, baseline.x * scale - ox_, baseline.y * scale - oy_, font, rtl, glyphs_);
    std::vector<GlyphQuad> all = std::move(glyphs_);
    for (const GlyphQuad& g : all) {
        glyphs_ = {g};
        glyphs(brush.colorAt({((g.x0 + g.x1) / 2 + ox_) / scale, ((g.y0 + g.y1) / 2 + oy_) / scale}, area));
    }
    glyphs_.clear();
}

void Renderer::drawTextShadow(std::string_view text, Offset baseline, const Font& font, Color color, float blurRadius, bool rtl) {
    if (color.a <= 0) return;
    glyphs_.clear();
    text_.layout(text, baseline.x * scale - ox_, baseline.y * scale - oy_, font, rtl, glyphs_, true,
                 std::max(0, int(std::lround(blurRadius * scale))));
    glyphs(color);
}

void Renderer::drawVector(const ImageVector& vector, const Rect& b, Color color) {
    if (color.a <= 0 || b.width <= 0 || b.height <= 0) return;
    glyphs_.resize(1);
    if (text_.vectorQuad(vector, b.x * scale - ox_, b.y * scale - oy_, b.width * scale, b.height * scale, glyphs_[0])) glyphs(color);
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
    float iw = float(image.width()), ih = float(image.height());
    set(p.uv, src.x / iw, src.y / ih, src.right() / iw, src.bottom() / ih);
    p.params[0] = alpha;
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

void Renderer::drawPath(const Path& path, Offset origin, const Brush& brush, const Rect& area, const PathStroke& stroke) {
    // Paths come with the path renderer.
    (void)path, (void)origin, (void)brush, (void)area, (void)stroke;
}

void Renderer::pushClip(const Rect& r) {
    // Edges go to the nearest pixel (rounding outwards would show a row of what scrolled past).
    int x0 = int(std::lround(r.x * scale - ox_)), y0 = int(std::lround(r.y * scale - oy_));
    int x1 = int(std::lround(r.right() * scale - ox_)), y1 = int(std::lround(r.bottom() * scale - oy_));
    IRect rect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
    if (!clips_.empty()) rect = intersect(rect, clips_.back());
    clips_.push_back(rect);
}

void Renderer::popClip() {
    if (!clips_.empty()) clips_.pop_back();
}

void Renderer::pushTranslation(Offset by) {
    translations_.push_back({ox_, oy_});
    ox_ -= by.x * scale;
    oy_ -= by.y * scale;
}

void Renderer::popTranslation() {
    if (translations_.empty()) return;
    ox_ = translations_.back().x;
    oy_ = translations_.back().y;
    translations_.pop_back();
}

bool Renderer::quickReject(const Rect& r) const {
    IRect visible = scissor();
    float x0 = r.x * scale - ox_, y0 = r.y * scale - oy_, x1 = r.right() * scale - ox_, y1 = r.bottom() * scale - oy_;
    return x1 <= float(visible.x) || y1 <= float(visible.y) || x0 >= float(visible.x + visible.width) ||
           y0 >= float(visible.y + visible.height);
}

void Renderer::pushLayer(const Rect& bounds, const CornerRadii& radii, float alpha, const LayerTransform& transform) {
    ++layerCount;
    int x0 = int(std::floor(bounds.x * scale - ox_)), y0 = int(std::floor(bounds.y * scale - oy_));
    int x1 = int(std::ceil(bounds.right() * scale - ox_)), y1 = int(std::ceil(bounds.bottom() * scale - oy_));
    Layer layer{nullptr, {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)},
                {bounds.x * scale - ox_, bounds.y * scale - oy_, bounds.width * scale, bounds.height * scale}, radii.scaled(scale), alpha,
                transform};
    if (layer.rect.width <= kMaxLayer && layer.rect.height <= kMaxLayer) layer.texture = acquire(layer.rect.width, layer.rect.height);
    // Too big: drawn straight into the target (no rounding, alpha or transform), in its clips.
    if (layer.texture) {
        beginTarget(*layer.texture, ClearColor{});
        ox_ = targets_.back().ox + float(x0);
        oy_ = targets_.back().oy + float(y0);
    }
    layers_.push_back(layer);
}

void Renderer::popLayer() {
    Layer layer = layers_.back();
    layers_.pop_back();
    if (!layer.texture) return;
    endTarget();
    Texture& t = *layer.texture;
    const Rect& b = layer.shape;
    float half = std::min(b.width, b.height) / 2;
    auto clamped = [half](float r) { return std::clamp(r, 0.f, half); };
    CornerRadii r{clamped(layer.radii.topLeft), clamped(layer.radii.topRight), clamped(layer.radii.bottomRight), clamped(layer.radii.bottomLeft)};
    const LayerTransform& tf = layer.transform;
    bool square = r.max() < 0.5f;
    float rx0 = float(layer.rect.x), ry0 = float(layer.rect.y), rx1 = rx0 + float(layer.rect.width), ry1 = ry0 + float(layer.rect.height);
    Prim p;
    // Square: all of its pixels. Rounded: its exact bounds, edges anti-aliased.
    if (square) {
        set(p.bounds, rx0, ry0, rx1, ry1);
        set(p.shape, rx0, ry0, rx1, ry1);
        if (!tf.identity()) set(p.bounds, rx0 - 1, ry0 - 1, rx1 + 1, ry1 + 1);
    } else {
        set(p.bounds, b.x - 1, b.y - 1, b.right() + 1, b.bottom() + 1);
        set(p.shape, b.x, b.y, b.right(), b.bottom());
        set(p.radii, r.topLeft, r.topRight, r.bottomRight, r.bottomLeft);
    }
    set(p.params, rx0, ry0, rx1, ry1);
    set(p.uv, 0, 0, 1 / float(t.width()), 1 / float(t.height()));
    set(p.color, 0, 0, 0, layer.alpha);
    // Scaled and turned about the origin, then moved: x' = M (x - o) + o + t.
    float ox = b.x + b.width * tf.origin.x, oy = b.y + b.height * tf.origin.y;
    float rad = tf.rotationZ * std::numbers::pi_v<float> / 180, cs = std::cos(rad), sn = std::sin(rad);
    float a = cs * tf.scaleX, bb = sn * tf.scaleX, c = -sn * tf.scaleY, d = cs * tf.scaleY;
    set(p.xform, a, bb, c, d);
    p.extra[0] = ox - (a * ox + c * oy) + tf.translationX * scale;
    p.extra[1] = oy - (bb * ox + d * oy) + tf.translationY * scale;
    add(LAYER, p, &t);
}

} // namespace kompoz::gpu
