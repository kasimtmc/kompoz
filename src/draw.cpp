#include <kompoz/host.hpp>
#include <kompoz/display_list.hpp>
#include "text_layout.hpp"

#include <kompoz/bidi.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/text_measure.hpp>
#include <kompoz/widgets.hpp>

#include <cmath>
#include <numbers>

namespace kompoz {

namespace {

struct DrawBehindElement final : ModifierElement {
    std::function<void(DrawScope&)> onDraw;
    void draw(GraphicsContext& g, const Rect& bounds, const LayoutNode& node,
              const std::function<void()>& drawContent) const override {
        DrawScope scope(g, bounds, node);
        onDraw(scope);
        drawContent();
    }
};

struct DrawWithContentElement final : ModifierElement {
    std::function<void(ContentDrawScope&)> onDraw;
    void draw(GraphicsContext& g, const Rect& bounds, const LayoutNode& node,
              const std::function<void()>& drawContent) const override {
        ContentDrawScope scope(g, bounds, node, drawContent);
        onDraw(scope);
    }
};

// The cache lives in the element: a recomposition (new captured values) or a new size rebuilds it.
struct DrawWithCacheElement final : ModifierElement {
    std::function<DrawResult(CacheDrawScope&)> build;
    struct Cache {
        bool valid = false;
        Size size;
        Density density;
        bool rtl = false;
        DrawResult result;
    };
    std::shared_ptr<Cache> cache = std::make_shared<Cache>();

    void draw(GraphicsContext& g, const Rect& bounds, const LayoutNode& node,
              const std::function<void()>& drawContent) const override {
        Size size{bounds.width, bounds.height};
        Density density = node.host() ? node.host()->density() : Density{};
        // Built again for another size, density or layout direction (as Compose's).
        if (!cache->valid || !(cache->size == size) || !(cache->density == density) || cache->rtl != node.rtl) {
            CacheDrawScope scope(size, density, node.rtl ? LayoutDirection::Rtl : LayoutDirection::Ltr);
            cache->result = build(scope);
            cache->size = size, cache->density = density, cache->rtl = node.rtl;
            cache->valid = true;
        }
        const DrawResult& r = cache->result;
        if (r.withContent) {
            ContentDrawScope scope(g, bounds, node, drawContent);
            r.withContent(scope);
            return;
        }
        if (r.behind) {
            DrawScope scope(g, bounds, node);
            r.behind(scope);
        }
        drawContent();
    }
};

// The 3x3 of a Matrix's plane part, row by row, and back (Compose's index names).
using Plane = std::array<float, 9>;
Plane mul(const Plane& a, const Plane& b) {  // b, then a
    Plane out{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) out[size_t(r * 3 + c)] = a[size_t(r * 3)] * b[size_t(c)] + a[size_t(r * 3 + 1)] * b[size_t(3 + c)] + a[size_t(r * 3 + 2)] * b[size_t(6 + c)];
    return out;
}
Plane moving(float x, float y) { return {1, 0, x, 0, 1, y, 0, 0, 1}; }
Matrix matrixOf(const Plane& p) {
    Matrix m;
    m.values[Matrix::ScaleX] = p[0], m.values[Matrix::SkewX] = p[1], m.values[Matrix::TranslateX] = p[2];
    m.values[Matrix::SkewY] = p[3], m.values[Matrix::ScaleY] = p[4], m.values[Matrix::TranslateY] = p[5];
    m.values[Matrix::Perspective0] = p[6], m.values[Matrix::Perspective1] = p[7], m.values[Matrix::Perspective2] = p[8];
    return m;
}

// A rotation about a unit axis (Skia's SkM44::Rotate), 3x3.
Plane rotation(float x, float y, float z, float degrees) {
    double r = double(degrees) * std::numbers::pi / 180;
    float c = float(std::cos(r)), s = float(std::sin(r)), t = 1 - c;
    return {t * x * x + c, t * x * y - s * z, t * x * z + s * y, t * x * y + s * z, t * y * y + c, t * y * z - s * x,
            t * x * z - s * y, t * y * z + s * x, t * z * z + c};
}

// A clip to `o` (from the top left of `bounds`): a rect, a rounded rect whose corners are circles
// as such, any other outline as a path.
void pushOutlineClip(GraphicsContext& g, const Outline& o, const Rect& bounds) {
    Rect ob = o.bounds();
    Rect at{bounds.x + ob.x, bounds.y + ob.y, ob.width, ob.height};
    bool rounded = o.kind == Outline::Kind::Rectangle || (o.kind == Outline::Kind::Rounded && o.roundRect.circular());
    CornerRadii radii = o.kind == Outline::Kind::Rounded ? o.roundRect.radii() : CornerRadii();
    if (rounded && radii.max() <= 0) g.pushClip(at);
    else if (rounded) g.pushClipRoundRect(at, radii);
    else g.pushClipPath(o.asPath(), {bounds.x, bounds.y});
}

} // namespace

namespace detail {

Matrix layerMatrix(const LayerGeometry& g) {
    float px = g.pivot.x, py = g.pivot.y;
    Plane scale = mul(moving(px, py), mul(Plane{g.scaleX, 0, 0, 0, g.scaleY, 0, 0, 0, 1}, moving(-px, -py)));
    if (g.rotationX == 0 && g.rotationY == 0) {  // turned about z in the plane, then moved (RenderProperties::updateMatrix)
        double r = double(g.rotationZ) * std::numbers::pi / 180;
        float c = float(std::cos(r)), s = float(std::sin(r));
        Plane turn = mul(moving(px, py), mul(Plane{c, -s, 0, s, c, 0, 0, 0, 1}, moving(-px, -py)));
        return matrixOf(mul(moving(g.translationX, g.translationY), mul(turn, scale)));
    }
    // Sk3DView: turned about x, then (0, -1, 0), then z the other way (applied z first), the
    // drawing's plane (x across, y up: its patch) seen from (0, 0, -72 * cameraDistance) px.
    Plane m = mul(mul(rotation(1, 0, 0, g.rotationX), rotation(0, -1, 0, g.rotationY)), rotation(0, 0, 1, -g.rotationZ));
    float u[3] = {m[0], m[3], m[6]}, v[3] = {-m[1], -m[4], -m[7]};  // where (1, 0, 0) and (0, -1, 0) go
    // The camera's distance in px (Android takes cameraDistance as inches of 72 px), the
    // perspective in dp.
    float k = g.density / (72 * std::max(g.cameraDistance, 1e-3f));
    Plane camera{u[0], v[0], 0, -u[1], -v[1], 0, u[2] * k, v[2] * k, 1};
    Plane placed = mul(moving(px + g.translationX, py + g.translationY), mul(camera, moving(-px, -py)));
    return matrixOf(mul(placed, scale));
}

void drawThroughLayer(GraphicsContext& g, const LayerDraw& l, const std::function<void()>& content) {
    float alpha = std::clamp(l.alpha, 0.f, 1.f);
    if (alpha <= 0 || l.geometry.scaleX == 0 || l.geometry.scaleY == 0) return;
    Matrix m = layerMatrix(l.geometry);
    bool moved = !m.isIdentity();
    bool offscreen = l.strategy == CompositingStrategy::Offscreen || (alpha < 1 && l.strategy == CompositingStrategy::Auto) ||
                     l.blendMode != BlendMode::SrcOver || l.colorFilter.has_value() || l.renderEffect.has_value();
    bool modulated = !offscreen && alpha < 1;  // ModulateAlpha: each draw faded
    bool shadowed = l.shadowElevation > 0;
    if (!moved && !offscreen && !modulated && !l.clip && !shadowed) return content();
    if (moved) g.pushTransform(m);
    Rect reach{l.bounds.x - l.outsets.left, l.bounds.y - l.outsets.top, l.bounds.width + l.outsets.left + l.outsets.right,
               l.bounds.height + l.outsets.top + l.outsets.bottom};
    // Its shadow: how high each point is, as Android works it out (RenderNode's true 3D transform:
    // turned about x, y and z and scaled about the pivot, raised by the elevation).
    ElevationShadow shadow;
    Rect shows = l.renderEffect ? l.renderEffect->outputBounds(reach) : reach;
    if (shadowed) {
        const LayerGeometry& q = l.geometry;
        Plane r = mul(mul(rotation(1, 0, 0, q.rotationX), rotation(0, 1, 0, q.rotationY)), rotation(0, 0, 1, q.rotationZ));
        float a = r[6] * q.scaleX, b = r[7] * q.scaleY;  // (the z row, times the scale)
        shadow.plane = {a, b, l.shadowElevation - a * q.pivot.x - b * q.pivot.y};
        shadow.ambientColor = l.ambientShadowColor, shadow.spotColor = l.spotShadowColor;
        shadow.alpha = alpha;
        Rect caster = l.outline.bounds();
        caster.x += l.bounds.x, caster.y += l.bounds.y;
        Rect s = shadow.reach(caster, g.light, g.lightCenter());
        float x0 = std::min(shows.x, s.x), y0 = std::min(shows.y, s.y);
        shows = {x0, y0, std::max(shows.right(), s.right()) - x0, std::max(shows.bottom(), s.bottom()) - y0};
    }
    // Nothing of it can show (the transform taken into account): not drawn at all.
    if (!g.quickReject(shows)) {
        if (shadowed) g.drawElevationShadow(l.outline, {l.bounds.x, l.bounds.y}, shadow);
        const Outline& o = l.outline;
        Rect ob = o.bounds();
        Rect at{l.bounds.x + ob.x, l.bounds.y + ob.y, ob.width, ob.height};
        bool rounded = o.kind == Outline::Kind::Rectangle || (o.kind == Outline::Kind::Rounded && o.roundRect.circular());
        CornerRadii radii = o.kind == Outline::Kind::Rounded ? o.roundRect.radii() : CornerRadii();
        // Cut to its outline where it goes, as what it draws there is.
        auto pushClip = [&] { pushOutlineClip(g, o, l.bounds); };
        if (offscreen) {
            std::optional<detail::Filtering> filtering;
            if (l.colorFilter) filtering.emplace(g, l.colorFilter);
            if (l.blendMode != BlendMode::SrcOver) g.pushBlendMode(l.blendMode);
            // Clipped: with an effect, by a clip around it (what the effect makes is cut, not what
            // it is made of); else by the layer itself, to a rounded rect of its bounds or its outline.
            bool clipped = l.clip && l.renderEffect;
            if (clipped) pushClip();
            if (l.renderEffect) g.pushEffectLayer(reach, *l.renderEffect, alpha);
            else if (l.clip && rounded && at == l.bounds && l.outsets == LayerOutsets{}) g.pushLayer(l.bounds, radii, alpha);
            else if (l.clip) {
                Path p = o.asPath();  // (from the layer's top left, which the outsets move)
                p.translate({l.bounds.x - reach.x, l.bounds.y - reach.y});
                g.pushOutlineLayer(reach, p, alpha);
            } else {
                g.pushLayer(reach, CornerRadii(), alpha);
            }
            content();
            g.popLayer();
            if (clipped) g.popClip();
            if (l.blendMode != BlendMode::SrcOver) g.popBlendMode();
        } else {
            std::optional<detail::Filtering> fading;
            if (modulated) {
                ColorMatrix fade;
                fade.setToScale(1, 1, 1, alpha);
                fading.emplace(g, ColorFilter::colorMatrix(fade));
            }
            if (l.clip) pushClip();
            content();
            if (l.clip) g.popClip();
        }
    }
    if (moved) g.popTransform();
}

} // namespace detail

namespace {

struct GraphicsLayerElement final : ModifierElement {
    GraphicsLayerScope layer;
    std::function<void(GraphicsLayerScope&)> block;  // evaluated at draw time when set

    void draw(GraphicsContext& g, const Rect& bounds, const LayoutNode& node,
              const std::function<void()>& drawContent) const override {
        GraphicsLayerScope l = layer;
        l.size = {bounds.width, bounds.height};
        if (block) block(l);
        detail::LayerDraw d;
        d.bounds = bounds;
        d.geometry = {l.scaleX, l.scaleY, l.translationX, l.translationY, l.rotationX, l.rotationY, l.rotationZ, l.cameraDistance,
                      {bounds.x + bounds.width * l.transformOrigin.x, bounds.y + bounds.height * l.transformOrigin.y},
                      node.host() ? node.host()->density().density : 1.f};
        d.alpha = l.alpha;
        d.shadowElevation = l.shadowElevation;
        d.ambientShadowColor = l.ambientShadowColor, d.spotShadowColor = l.spotShadowColor;
        d.clip = l.clip;
        if (l.clip || l.shadowElevation > 0) d.outline = l.shape.createOutline(l.size, node.rtl ? LayoutDirection::Rtl : LayoutDirection::Ltr);
        d.renderEffect = l.renderEffect;
        d.blendMode = l.blendMode;
        d.colorFilter = l.colorFilter;
        d.strategy = l.compositingStrategy;
        d.outsets = l.outsets;
        detail::drawThroughLayer(g, d, drawContent);
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const GraphicsLayerElement*>(&other);
        return o && !block && !o->block && o->layer == layer;
    }
};

// A blend mode other than SrcOver around one drawing.
// Anti-aliasing off while it is in scope, for a paint that says so (isAntiAlias false).
struct Aliasing {
    GraphicsContext& g;
    bool on;
    Aliasing(GraphicsContext& context, const Paint& paint) : g(context), on(!paint.isAntiAlias) {
        if (on) g.pushAntiAlias(false);
    }
    ~Aliasing() {
        if (on) g.popAntiAlias();
    }
    Aliasing(const Aliasing&) = delete;
    Aliasing& operator=(const Aliasing&) = delete;
};

struct Blending {
    GraphicsContext& g;
    bool on;
    Blending(GraphicsContext& context, BlendMode mode) : g(context), on(mode != BlendMode::SrcOver) {
        if (on) g.pushBlendMode(mode);
    }
    ~Blending() {
        if (on) g.popBlendMode();
    }
};

} // namespace

namespace {

// An image: as it is, or an Alpha8 one as a mask (as Android draws one): `brush` (its paint's
// color or shader; DrawScope's: black, as Compose's) where it is, as opaque as it is.
void paintImage(GraphicsContext& g, const ImageBitmap& image, const Rect& src, const Rect& dst, float alpha, FilterQuality quality,
                const Brush& brush, const Rect& brushArea) {
    if (image.config() != ImageBitmapConfig::Alpha8) return g.drawImage(image, src, dst, alpha, quality);
    g.drawImageMask(image, src, dst, brush, brushArea, alpha, quality);
}

} // namespace

void GraphicsContext::drawImageMask(const ImageBitmap& image, const Rect& src, const Rect& dst, const Brush& brush, const Rect& brushArea,
                                    float alpha, FilterQuality quality) {
    pushLayer(dst, CornerRadii(), alpha);  // the brush, kept where the image is
    if (brush.isSolid()) fillRoundRect(dst, CornerRadii(), brush.stops().empty() ? colors::Transparent : brush.stops().front().color);
    else fillGradient(dst, CornerRadii(), brush, brushArea);
    pushBlendMode(BlendMode::DstIn);
    drawImage(image, src, dst, 1, quality);
    popBlendMode();
    popLayer();
}

DrawScope::DrawScope(GraphicsContext& context, Rect bounds, const LayoutNode& node)
    : DrawScope(context, bounds, &node.textMeasurer(), node.host() ? node.host()->density() : Density{},
                node.rtl ? LayoutDirection::Rtl : LayoutDirection::Ltr) {}

DrawScope::DrawScope(graphics::Canvas& canvas, Size size, TextMeasurer* measurer, Density density, LayoutDirection layoutDirection)
    : DrawScope(canvas.context(), {canvas.origin().x, canvas.origin().y, size.width, size.height}, measurer, density, layoutDirection) {
    canvas_ = std::shared_ptr<graphics::Canvas>(std::shared_ptr<graphics::Canvas>(), &canvas);  // (not its own: left as it is)
}

graphics::Canvas& DrawScope::canvas() {
    if (!canvas_) {
        canvas_ = std::make_shared<graphics::Canvas>(*context_, Offset{bounds_.x, bounds_.y});
        canvas_->density_ = density_;
    }
    return *canvas_;
}

void DrawScope::within(const std::function<void(DrawScope&)>& block) {
    int depth = canvas_ ? canvas_->depth() : 0;
    block(*this);
    if (canvas_) canvas_->restoreTo(depth);
}

// --- Canvas ---------------------------------------------------------------------------------------

graphics::Canvas::Canvas(const ImageBitmap& image) : context_(nullptr), image_(image) {
    recording_ = std::make_shared<DisplayList>(detail::bitmapMeasurer());
    recording_->begin({float(image.width()), float(image.height())}, 1);
    context_ = recording_.get();
}

graphics::Canvas::~Canvas() {
    restoreTo(0);
    if (!recording_) return;
    recording_->end();
    if (recording_->size() > 0) detail::drawIntoBitmap(image_, recording_);
}

void graphics::Canvas::save() { stack_.push_back({Saved::Kind::Save}); }

void graphics::Canvas::restore() {
    for (size_t i = stack_.size(); i-- > 0;)
        if (stack_[i].kind == Saved::Kind::Save || stack_[i].kind == Saved::Kind::Layer) return restoreTo(int(i));
}

void graphics::Canvas::restoreTo(int depth) {
    while (int(stack_.size()) > std::max(depth, 0)) {
        Saved s = stack_.back();
        stack_.pop_back();
        switch (s.kind) {
        case Saved::Kind::Save: break;
        case Saved::Kind::Transform: context_->popTransform(); break;
        case Saved::Kind::Clip: context_->popClip(); break;
        case Saved::Kind::Layer:
            context_->popLayer();
            if (s.blended) context_->popBlendMode();
            if (s.filtered) context_->popColorFilter();
            break;
        }
    }
}

void graphics::Canvas::saveLayer(const Rect& bounds, const Paint& paint) {
    Saved s{Saved::Kind::Layer, paint.colorFilter.has_value(), paint.blendMode != BlendMode::SrcOver};
    if (s.filtered) context_->pushColorFilter(*paint.colorFilter);
    if (s.blended) context_->pushBlendMode(paint.blendMode);
    Rect r = bounds;
    context_->pushLayer(at({std::min(r.x, r.right()), std::min(r.y, r.bottom()), std::abs(r.width), std::abs(r.height)}), CornerRadii(),
                        paint.alpha());
    stack_.push_back(s);
}

void graphics::Canvas::withSave(const std::function<void()>& block) {
    int depth = this->depth();
    save();
    block();
    restoreTo(depth);
}

void graphics::Canvas::withSaveLayer(const Rect& bounds, const Paint& paint, const std::function<void()>& block) {
    int depth = this->depth();
    saveLayer(bounds, paint);
    block();
    restoreTo(depth);
}

// `matrix` (from what is drawn next to this canvas's coordinates) in the context's: from there to
// this canvas's, through it, and back.
void graphics::Canvas::push(const Matrix& matrix) {
    Matrix m;
    m.translate(-origin_.x, -origin_.y);
    m *= matrix;
    Matrix back;
    back.translate(origin_.x, origin_.y);
    m *= back;
    context_->pushTransform(m);
    stack_.push_back({Saved::Kind::Transform});
}

void graphics::Canvas::translate(float dx, float dy) {
    Matrix m;
    m.translate(dx, dy);
    push(m);
}

void graphics::Canvas::scale(float sx, float sy) {
    Matrix m;
    m.scale(sx, sy);
    push(m);
}

void graphics::Canvas::rotate(float degrees) {
    Matrix m;
    m.rotateZ(degrees);
    push(m);
}

void graphics::Canvas::skew(float sx, float sy) {
    Matrix m;
    m(1, 0) = sx, m(0, 1) = sy;
    push(m);
}

void graphics::Canvas::skewRad(float sxRad, float syRad) {
    skew(sxRad * 180 / std::numbers::pi_v<float>, syRad * 180 / std::numbers::pi_v<float>);
}

void graphics::Canvas::concat(const Matrix& matrix) {
    Matrix m;  // (x, y and w: Android's 3x3)
    for (int r : {0, 1, 3})
        for (int c : {0, 1, 3}) m(r, c) = matrix(r, c);
    push(m);
}

void graphics::Canvas::rotate(float degrees, float pivotX, float pivotY) {
    if (degrees == 0) return;  // (as Compose's)
    translate(pivotX, pivotY);
    rotate(degrees);
    translate(-pivotX, -pivotY);
}

void graphics::Canvas::rotateRad(float radians, float pivotX, float pivotY) { rotate(radians * 180 / std::numbers::pi_v<float>, pivotX, pivotY); }

void graphics::Canvas::scale(float sx, float sy, float pivotX, float pivotY) {
    if (sx == 1 && sy == 1) return;  // (as Compose's)
    translate(pivotX, pivotY);
    scale(sx, sy);
    translate(-pivotX, -pivotY);
}

void graphics::Canvas::clipRect(const Rect& rect, ClipOp op) {
    Rect r = at({std::min(rect.x, rect.right()), std::min(rect.y, rect.bottom()), std::abs(rect.width), std::abs(rect.height)});
    if (op == ClipOp::Intersect) context_->pushClip(r);  // (as DrawScope.clipRect)
    else context_->pushClipRoundRect(r, CornerRadii(), op);
    stack_.push_back({Saved::Kind::Clip});
}

void graphics::Canvas::clipRect(float left, float top, float right, float bottom, ClipOp op) { clipRect({left, top, right - left, bottom - top}, op); }

void graphics::Canvas::clipPath(const Path& path, ClipOp op) {
    context_->pushClipPath(path, origin_, op);
    stack_.push_back({Saved::Kind::Clip});
}

DrawScope graphics::Canvas::scope() const { return DrawScope(*context_, {origin_.x, origin_.y, 0, 0}, nullptr, density_); }

namespace {

// What a paint paints with: its shader (in the canvas's coordinates: a scope's at the canvas's
// origin) or its color, and how opaque.
Brush brushOf(const Paint& paint) { return paint.shader ? Brush::shader(*paint.shader) : Brush::solid(paint.color.copy(1)); }

DrawStyle styleOf(const Paint& paint) {
    if (paint.style == PaintingStyle::Stroke)
        return Stroke(paint.strokeWidth, paint.strokeMiterLimit, paint.strokeCap, paint.strokeJoin, paint.pathEffect);
    DrawStyle fill = Fill;
    fill.pathEffect = paint.pathEffect;
    return fill;
}

} // namespace

void graphics::Canvas::drawLine(Offset p1, Offset p2, const Paint& paint) {
    Aliasing aliasing(*context_, paint);
    scope().drawLine(brushOf(paint), p1, p2, paint.strokeWidth, paint.strokeCap, paint.pathEffect, paint.alpha(), paint.colorFilter,
                     paint.blendMode);
}

void graphics::Canvas::drawRect(const Rect& rect, const Paint& paint) {
    Aliasing aliasing(*context_, paint);
    scope().drawRect(brushOf(paint), {rect.x, rect.y}, Size{rect.width, rect.height}, paint.alpha(), styleOf(paint), paint.colorFilter,
                     paint.blendMode);
}

void graphics::Canvas::drawRect(float left, float top, float right, float bottom, const Paint& paint) {
    drawRect({left, top, right - left, bottom - top}, paint);
}

void graphics::Canvas::drawRoundRect(float left, float top, float right, float bottom, float radiusX, float radiusY, const Paint& paint) {
    Aliasing aliasing(*context_, paint);
    scope().drawRoundRect(brushOf(paint), {left, top}, Size{right - left, bottom - top}, CornerRadius(radiusX, radiusY), paint.alpha(),
                          styleOf(paint), paint.colorFilter, paint.blendMode);
}

void graphics::Canvas::drawOval(const Rect& rect, const Paint& paint) {
    Aliasing aliasing(*context_, paint);
    scope().drawOval(brushOf(paint), {rect.x, rect.y}, Size{rect.width, rect.height}, paint.alpha(), styleOf(paint), paint.colorFilter,
                     paint.blendMode);
}

void graphics::Canvas::drawOval(float left, float top, float right, float bottom, const Paint& paint) {
    drawOval({left, top, right - left, bottom - top}, paint);
}

void graphics::Canvas::drawCircle(Offset center, float radius, const Paint& paint) {
    Aliasing aliasing(*context_, paint);
    scope().drawCircle(brushOf(paint), radius, center, paint.alpha(), styleOf(paint), paint.colorFilter, paint.blendMode);
}

void graphics::Canvas::drawArc(const Rect& rect, float startAngle, float sweepAngle, bool useCenter, const Paint& paint) {
    Aliasing aliasing(*context_, paint);
    scope().drawArc(brushOf(paint), startAngle, sweepAngle, useCenter, {rect.x, rect.y}, Size{rect.width, rect.height}, paint.alpha(),
                    styleOf(paint), paint.colorFilter, paint.blendMode);
}

void graphics::Canvas::drawArc(float left, float top, float right, float bottom, float startAngle, float sweepAngle, bool useCenter, const Paint& paint) {
    drawArc({left, top, right - left, bottom - top}, startAngle, sweepAngle, useCenter, paint);
}

void graphics::Canvas::drawArcRad(const Rect& rect, float startAngleRad, float sweepAngleRad, bool useCenter, const Paint& paint) {
    const float degrees = 180 / std::numbers::pi_v<float>;
    drawArc(rect, startAngleRad * degrees, sweepAngleRad * degrees, useCenter, paint);
}

void graphics::Canvas::drawPath(const Path& path, const Paint& paint) {
    Aliasing aliasing(*context_, paint);
    scope().drawPath(path, brushOf(paint), paint.alpha(), styleOf(paint), paint.colorFilter, paint.blendMode);
}

void graphics::Canvas::drawImage(const ImageBitmap& image, Offset topLeftOffset, const Paint& paint) {
    if (image.empty()) return;
    drawImage(image, {0, 0, float(image.width()), float(image.height())},
              {topLeftOffset.x, topLeftOffset.y, float(image.width()), float(image.height())}, paint);
}

void graphics::Canvas::drawImageRect(const ImageBitmap& image, IntOffset srcOffset, IntSize srcSize, IntOffset dstOffset, IntSize dstSize,
                           const Paint& paint) {
    drawImage(image, {float(srcOffset.x), float(srcOffset.y), float(srcSize.width), float(srcSize.height)},
              {float(dstOffset.x), float(dstOffset.y), float(dstSize.width), float(dstSize.height)}, paint);
}

// An image (its paint's alpha; an Alpha8 one in the paint's color or shader), `dst` in this
// canvas's coordinates.
void graphics::Canvas::drawImage(const ImageBitmap& image, const Rect& src, const Rect& dst, const Paint& paint) {
    if (image.empty() || paint.alpha() <= 0) return;
    Aliasing aliasing(*context_, paint);
    Blending blending(*context_, paint.blendMode);
    detail::Filtering filtering(*context_, paint.colorFilter);
    paintImage(*context_, image, src, at(dst), paint.alpha(), paint.filterQuality, brushOf(paint), {origin_.x, origin_.y, 0, 0});
}

void graphics::Canvas::drawImageRect(const ImageBitmap& image, const Paint& paint) {
    if (image.empty()) return;
    IntSize all{image.width(), image.height()};
    drawImageRect(image, {}, all, {}, all, paint);
}

void graphics::Canvas::drawPoints(PointMode pointMode, const std::vector<Offset>& points, const Paint& paint) {
    Aliasing aliasing(*context_, paint);
    scope().drawPoints(points, pointMode, brushOf(paint), paint.strokeWidth, paint.strokeCap, paint.pathEffect, paint.alpha(),
                       paint.colorFilter, paint.blendMode);
}

void graphics::Canvas::drawVertices(const Vertices& vertices, BlendMode blendMode, const Paint& paint) {
    if (paint.alpha() <= 0) return;
    Blending blending(*context_, paint.blendMode);
    detail::Filtering filtering(*context_, paint.colorFilter);
    context_->drawVertices(vertices, origin_, blendMode, brushOf(paint), {origin_.x, origin_.y, 0, 0}, paint.alpha());
}

void graphics::Canvas::drawRawPoints(PointMode pointMode, const std::vector<float>& points, const Paint& paint) {
    std::vector<Offset> pairs;
    for (size_t i = 0; i + 1 < points.size(); i += 2) pairs.push_back({points[i], points[i + 1]});
    drawPoints(pointMode, pairs, paint);
}

namespace {

// A color faded by a paint's alpha.
Color faded(Color c, float alpha) { return alpha < 1 ? c.copy(c.alpha() * std::max(alpha, 0.f)) : c; }

// The path Skia draws for an arc (SkPathPriv::CreateDrawArcPath): a whole oval for a filled
// sweep of a turn or more; else through the center first if useCenter (and back to it), the
// sweep in half turns while it is a turn or more (arcTo goes round only once).
Path arcPath(const Rect& oval, float start, float sweep, bool useCenter, bool fillNoEffect) {
    Path p;
    if (fillNoEffect && std::abs(sweep) >= 360) {
        p.addOval(oval, Path::Direction::Clockwise);
        return p;
    }
    if (useCenter) p.moveTo(oval.x + oval.width / 2, oval.y + oval.height / 2);
    bool forceMoveTo = !useCenter;
    for (float half : {-180.f, 180.f})
        while (half < 0 ? sweep <= -360 : sweep >= 360) {
            p.arcTo(oval, start, half, forceMoveTo);
            start += half;
            p.arcTo(oval, start, half, false);
            start += half;
            forceMoveTo = false;
            sweep -= 2 * half;
        }
    p.arcTo(oval, start, sweep, forceMoveTo);
    if (useCenter) p.close();
    return p;
}

} // namespace

// A path (this scope's coordinates) in a brush, faded, filled or stroked (through the style's
// path effect first), its colors filtered, blended.
void DrawScope::paint(const Path& path, const Brush& brush, float alpha, const DrawStyle& style, const std::optional<ColorFilter>& colorFilter,
                      BlendMode blendMode) {
    if (path.isEmpty() || alpha <= 0) return;
    Blending blending(*context_, blendMode);
    detail::Filtering filtering(*context_, colorFilter);
    Brush b = brush.faded(alpha);
    // (Its path effect goes with it, applied where it is drawn, at the pixels it is drawn at.)
    context_->drawPath(path, {bounds_.x, bounds_.y}, b, bounds_, style.pathStroke());
}

// A rounded rect (this scope's; `oval`: a circle), as the backend draws one where it can: filled,
// or stroked along it (half each side) where its corners are circles and the stroke's joins can't
// show (curved corners, or square ones mitered); else as the path Android's canvas makes of it
// (Skia's defaults: clockwise, an oval from its right, a rect from its top left, a round rect
// from its sixth point).
void DrawScope::paintRoundRect(const Rect& r, CornerRadius corner, const Brush& brush, float alpha, const DrawStyle& style,
                               const std::optional<ColorFilter>& colorFilter, BlendMode blendMode, bool oval) {
    if (alpha <= 0) return;
    bool circular = corner.x == corner.y;
    float radius = std::max(0.f, corner.x);
    bool plain = !style.pathEffect && (!style.isStroke() || (style.width > 0 && (radius > 0 || style.join == StrokeJoin::Miter)));
    if (!circular || !plain || (style.isStroke() && !brush.isSolid())) {
        Path outline;
        Rect at{r.x - bounds_.x, r.y - bounds_.y, r.width, r.height};
        if (oval) outline.addOval(at, Path::Direction::Clockwise);
        else if (corner.x <= 0 || corner.y <= 0) outline.addRect(at, Path::Direction::Clockwise);
        else outline.addRoundRect(RoundRect{at, corner, corner, corner, corner}, Path::Direction::Clockwise);
        return paint(outline, brush, alpha, style, colorFilter, blendMode);
    }
    Blending blending(*context_, blendMode);
    detail::Filtering filtering(*context_, colorFilter);
    if (style.isStroke()) {
        float half = style.width / 2;
        context_->strokeRoundRect({r.x - half, r.y - half, r.width + 2 * half, r.height + 2 * half}, radius > 0 ? radius + half : 0, style.width,
                                  faded(brush.stops().front().color, alpha));
    } else if (brush.isSolid()) {
        context_->fillRoundRect(r, radius, faded(brush.stops().front().color, alpha));
    } else {
        context_->fillGradient(r, radius, brush.faded(alpha), bounds_);
    }
}

void DrawScope::drawLine(const Brush& brush, Offset start, Offset end, float strokeWidth, StrokeCap cap, std::optional<PathEffect> pathEffect,
                         float alpha, std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    // A square-capped line in a color: as the backend draws one (GraphicsContext::drawLine's
    // ends are square); else (butt or round ends) the line's path stroked.
    if (brush.isSolid() && strokeWidth > 0 && cap == StrokeCap::Square && !pathEffect) {
        if (alpha <= 0) return;
        Blending blending(*context_, blendMode);
        detail::Filtering filtering(*context_, colorFilter);
        context_->drawLine(abs(start), abs(end), strokeWidth, faded(brush.stops().front().color, alpha));
        return;
    }
    Path line;
    line.moveTo(start.x, start.y);
    line.lineTo(end.x, end.y);
    paint(line, brush, alpha, Stroke(strokeWidth, 4, cap, StrokeJoin::Miter, pathEffect), colorFilter, blendMode);
}

void DrawScope::drawLine(Color color, Offset start, Offset end, float strokeWidth, StrokeCap cap, std::optional<PathEffect> pathEffect,
                         float alpha, std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    drawLine(Brush::solid(color), start, end, strokeWidth, cap, pathEffect, alpha, colorFilter, blendMode);
}

void DrawScope::drawRect(const Brush& brush, Offset topLeft, std::optional<Size> size, float alpha, const DrawStyle& style,
                         std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    paintRoundRect(sorted(topLeft, size), {}, brush, alpha, style, colorFilter, blendMode);
}

void DrawScope::drawRect(Color color, Offset topLeft, std::optional<Size> size, float alpha, const DrawStyle& style,
                         std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    paintRoundRect(sorted(topLeft, size), {}, Brush::solid(color), alpha, style, colorFilter, blendMode);
}

void DrawScope::drawImage(const ImageBitmap& image, Offset topLeft, float alpha, const DrawStyle& style, std::optional<ColorFilter> colorFilter,
                          BlendMode blendMode) {
    drawImage(image, IntOffset{}, std::nullopt, topLeft, std::nullopt, alpha, style, colorFilter, blendMode);
}

void DrawScope::drawImage(const ImageBitmap& image, IntOffset srcOffset, std::optional<IntSize> srcSize, Offset dstOffset,
                          std::optional<Size> dstSize, float alpha, const DrawStyle& style, std::optional<ColorFilter> colorFilter,
                          BlendMode blendMode, FilterQuality filterQuality) {
    (void)style;  // (an image is drawn the same either way, as on Android)
    if (image.empty() || alpha <= 0) return;
    IntSize from = srcSize.value_or(IntSize{image.width() - srcOffset.x, image.height() - srcOffset.y});
    Rect src{float(srcOffset.x), float(srcOffset.y), float(from.width), float(from.height)};
    Blending blending(*context_, blendMode);
    detail::Filtering filtering(*context_, colorFilter);
    paintImage(*context_, image, src, local(dstOffset, dstSize.value_or(Size(from))), std::min(alpha, 1.f), filterQuality, Brush::solid(colors::Black),
               bounds_);
}

void DrawScope::drawRoundRect(const Brush& brush, Offset topLeft, std::optional<Size> size, CornerRadius cornerRadius, float alpha,
                              const DrawStyle& style, std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    paintRoundRect(sorted(topLeft, size), cornerRadius, brush, alpha, style, colorFilter, blendMode);
}

void DrawScope::drawRoundRect(Color color, Offset topLeft, std::optional<Size> size, CornerRadius cornerRadius, const DrawStyle& style,
                              float alpha, std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    paintRoundRect(sorted(topLeft, size), cornerRadius, Brush::solid(color), alpha, style, colorFilter, blendMode);
}

void DrawScope::drawCircle(const Brush& brush, std::optional<float> radius, std::optional<Offset> center, float alpha, const DrawStyle& style,
                           std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    float r = radius.value_or(std::min(bounds_.width, bounds_.height) / 2);
    Offset c = center.value_or(this->center());
    paintRoundRect(local({c.x - r, c.y - r}, Size{2 * r, 2 * r}), r, brush, alpha, style, colorFilter, blendMode, true);
}

void DrawScope::drawCircle(Color color, std::optional<float> radius, std::optional<Offset> center, float alpha, const DrawStyle& style,
                           std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    drawCircle(Brush::solid(color), radius, center, alpha, style, colorFilter, blendMode);
}

void DrawScope::drawOval(const Brush& brush, Offset topLeft, std::optional<Size> size, float alpha, const DrawStyle& style,
                         std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    Rect r = sorted(topLeft, size);
    if (r.width == r.height) return paintRoundRect(r, r.width / 2, brush, alpha, style, colorFilter, blendMode, true);
    Path oval;
    oval.addOval({r.x - bounds_.x, r.y - bounds_.y, r.width, r.height}, Path::Direction::Clockwise);
    paint(oval, brush, alpha, style, colorFilter, blendMode);
}

void DrawScope::drawOval(Color color, Offset topLeft, std::optional<Size> size, float alpha, const DrawStyle& style,
                         std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    drawOval(Brush::solid(color), topLeft, size, alpha, style, colorFilter, blendMode);
}

void DrawScope::drawArc(const Brush& brush, float startAngle, float sweepAngle, bool useCenter, Offset topLeft, std::optional<Size> size,
                        float alpha, const DrawStyle& style, std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    Rect oval = sorted(topLeft, size);
    // An arc of a circle, stroked butt or round, in a color: as the backend draws one.
    if (style.isStroke() && !useCenter && style.width > 0 && !style.pathEffect && style.cap != StrokeCap::Square && brush.isSolid() &&
        oval.width == oval.height && std::abs(sweepAngle) < 360) {
        if (alpha <= 0) return;
        Blending blending(*context_, blendMode);
        detail::Filtering filtering(*context_, colorFilter);
        context_->drawArc(oval, startAngle, sweepAngle, style.width, faded(brush.stops().front().color, alpha), style.cap == StrokeCap::Round);
        return;
    }
    Rect at{oval.x - bounds_.x, oval.y - bounds_.y, oval.width, oval.height};
    paint(arcPath(at, startAngle, sweepAngle, useCenter, !style.isStroke() && !style.pathEffect), brush, alpha, style, colorFilter, blendMode);
}

void DrawScope::drawArc(Color color, float startAngle, float sweepAngle, bool useCenter, Offset topLeft, std::optional<Size> size, float alpha,
                        const DrawStyle& style, std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    drawArc(Brush::solid(color), startAngle, sweepAngle, useCenter, topLeft, size, alpha, style, colorFilter, blendMode);
}

void DrawScope::drawPath(const Path& path, Color color, float alpha, const DrawStyle& style, std::optional<ColorFilter> colorFilter,
                         BlendMode blendMode) {
    paint(path, Brush::solid(color), alpha, style, colorFilter, blendMode);
}

void DrawScope::drawPath(const Path& path, const Brush& brush, float alpha, const DrawStyle& style, std::optional<ColorFilter> colorFilter,
                         BlendMode blendMode) {
    paint(path, brush, alpha, style, colorFilter, blendMode);
}

// As Skia's drawPoints: dots as wide as the stroke (a hairline's: a pixel), round with a round
// cap, square otherwise; lines between each two; one line through them all.
void DrawScope::drawPoints(const std::vector<Offset>& points, PointMode pointMode, const Brush& brush, float strokeWidth, StrokeCap cap,
                           std::optional<PathEffect> pathEffect, float alpha, std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    if (points.empty()) return;
    Path path;
    if (pointMode == PointMode::Points) {
        float w = strokeWidth > 0 ? strokeWidth : 1 / density_.density, r = w / 2;
        for (Offset p : points) {
            Rect dot{p.x - r, p.y - r, w, w};
            cap == StrokeCap::Round ? path.addOval(dot) : path.addRect(dot);
        }
        return paint(path, brush, alpha, Fill, colorFilter, blendMode);
    }
    for (size_t i = 0; i < points.size(); ++i) {
        bool start = pointMode == PointMode::Polygon ? i == 0 : i % 2 == 0;
        if (pointMode == PointMode::Lines && start && i + 1 == points.size()) break;  // an odd one out has no partner
        start ? path.moveTo(points[i].x, points[i].y) : path.lineTo(points[i].x, points[i].y);
    }
    paint(path, brush, alpha, Stroke(strokeWidth, 4, cap, StrokeJoin::Miter, pathEffect), colorFilter, blendMode);
}

void DrawScope::drawPoints(const std::vector<Offset>& points, PointMode pointMode, Color color, float strokeWidth, StrokeCap cap,
                           std::optional<PathEffect> pathEffect, float alpha, std::optional<ColorFilter> colorFilter, BlendMode blendMode) {
    drawPoints(points, pointMode, Brush::solid(color), strokeWidth, cap, pathEffect, alpha, colorFilter, blendMode);
}

void DrawScope::drawText(const TextLayoutMeasurer& measurer, const std::string& text, Offset topLeft) {
    drawText(measurer, AnnotatedString(text), topLeft, TextStyle{});
}

void DrawScope::drawText(const TextLayoutMeasurer& measurer, const AnnotatedString& text, Offset topLeft) {
    drawText(measurer, text, topLeft, TextStyle{});
}

void DrawScope::drawText(const TextLayoutMeasurer& measurer, const std::string& text, Offset topLeft, const TextStyle& style,
                         std::optional<TextOverflow> overflow, std::optional<bool> softWrap, std::optional<int> maxLines,
                         std::optional<Size> size, BlendMode blendMode) {
    drawText(measurer, AnnotatedString(text), topLeft, style, overflow, softWrap, maxLines, size, blendMode);
}

void DrawScope::drawText(const TextLayoutMeasurer& measurer, const AnnotatedString& text, Offset topLeft, const TextStyle& style,
                         std::optional<TextOverflow> overflow, std::optional<bool> softWrap, std::optional<int> maxLines,
                         std::optional<Size> size, BlendMode blendMode) {
    TextStyle s = style;
    if (overflow) s.overflow = *overflow;
    if (softWrap) s.softWrap = *softWrap;
    if (maxLines) s.maxLines = *maxLines == std::numeric_limits<int>::max() ? 0 : *maxLines;
    // In a size: its width the lines align in (as Compose's fixed constraints); else as far as the area goes.
    float maxWidth = size ? size->width : std::max(0.f, bounds_.width - topLeft.x);
    float maxHeight = size ? size->height : std::max(0.f, bounds_.height - topLeft.y);
    TextLayoutResult layout = measurer.measure(text, s, maxWidth, maxHeight);
    if (size && layout.data_) {
        const TextLayoutResult::Data& d = *layout.data_;
        layout = detail::makeTextLayoutResult(d.layout, d.text, *d.measurer, d.align, d.overflow, size->width, d.maxWidth, d.maxHeight, d.density);
    }
    drawText(layout, std::nullopt, topLeft, std::nullopt, std::nullopt, std::nullopt, std::nullopt, blendMode);
}

namespace {

// `runs` as drawText paints them: what the text's own style gave them (not a span or a link)
// in `paint` (a color, or a brush, faded by `alpha`), with the shadow, decoration and draw style
// given.
void repaintRuns(std::vector<detail::TextRun>& runs, const std::function<void(detail::TextRun&)>& paint, const std::optional<Shadow>& shadow,
                 const std::optional<TextDecoration>& decoration, const std::optional<DrawStyle>& drawStyle) {
    using detail::TextRun;
    for (TextRun& run : runs) {
        if (!(run.own & TextRun::OwnPaint)) paint(run);
        if (shadow && !(run.own & TextRun::OwnShadow)) run.shadow = shadow;
        if (decoration && !(run.own & TextRun::OwnDecoration)) run.decoration = *decoration;
        if (drawStyle && !(run.own & TextRun::OwnDrawStyle)) detail::styleGlyphs(run.font, *drawStyle);
    }
}

} // namespace

// As Compose's: a color (unset: the text's own) faded by alpha where it isn't 1 (unset: as it
// is); unset over a brushed text, its brush at alpha (unset: as it is).
void DrawScope::drawText(const TextLayoutResult& layout, std::optional<Color> color, Offset topLeft, std::optional<float> alpha,
                         std::optional<Shadow> shadow, std::optional<TextDecoration> textDecoration, std::optional<DrawStyle> drawStyle,
                         BlendMode blendMode) {
    if (!layout.data_) return;
    const TextLayoutResult::Data& d = *layout.data_;
    Rect at{bounds_.x + topLeft.x, bounds_.y + topLeft.y, d.width, d.layout.height};
    Blending blending(*context_, blendMode);
    if (!color && !alpha && !shadow && !textDecoration && !drawStyle) return detail::drawStyledText(*context_, d.layout, at, d.align, d.overflow);
    detail::StyledTextLayout painted = d.layout;
    repaintRuns(
        painted.runs,
        [&](detail::TextRun& run) {
            if (run.brush && !color) {
                if (alpha) run.brush = run.brush->faded(*alpha);
                return;
            }
            run.color = faded(color.value_or(run.color), alpha.value_or(1)), run.brush.reset();
        },
        shadow, textDecoration, drawStyle);
    detail::drawStyledText(*context_, painted, at, d.align, d.overflow);
}

void DrawScope::drawText(const TextLayoutResult& layout, const Brush& brush, Offset topLeft, std::optional<float> alpha,
                         std::optional<Shadow> shadow, std::optional<TextDecoration> textDecoration, std::optional<DrawStyle> drawStyle,
                         BlendMode blendMode) {
    if (!layout.data_) return;
    const TextLayoutResult::Data& d = *layout.data_;
    Rect at{bounds_.x + topLeft.x, bounds_.y + topLeft.y, d.width, d.layout.height};
    Blending blending(*context_, blendMode);
    detail::StyledTextLayout painted = d.layout;
    Brush b = brush.faded(alpha.value_or(1));
    repaintRuns(painted.runs, [&](detail::TextRun& run) { run.brush = b; }, shadow, textDecoration, drawStyle);
    detail::drawStyledText(*context_, painted, at, d.align, d.overflow);
}

void DrawScope::drawText(std::string_view text, Offset topLeft, Font font, Color color) {
    if (!measurer_) return;
    FontMetrics m = measurer_->metrics(font);
    context_->drawText(text, abs({topLeft.x, topLeft.y + m.ascent}), font, color, bidi::firstStrongIsRtl(text));
}

Size DrawScope::measureText(std::string_view text, Font font) const {
    if (!measurer_) return {};
    return {measurer_->measureWidth(text, font), measurer_->metrics(font).lineHeight};
}


void DrawScope::translate(float left, float top, const std::function<void(DrawScope&)>& block) {
    context_->pushTranslation({left, top});  // the content too (drawContent() inside it)
    within(block);
    context_->popTranslation();
}

void DrawScope::inset(float left, float top, float right, float bottom, const std::function<void(DrawScope&)>& block) {
    DrawScope inner(*context_,
                    {bounds_.x + left, bounds_.y + top, std::max(0.f, bounds_.width - left - right),
                     std::max(0.f, bounds_.height - top - bottom)},
                    measurer_, density_, layoutDirection_);
    block(inner);
}

void DrawScope::clipPath(const Path& path, const std::function<void(DrawScope&)>& block, ClipOp op) {
    context_->pushClipPath(path, {bounds_.x, bounds_.y}, op);
    within(block);
    context_->popClip();
}

void DrawScope::withTransform(const std::function<void(DrawTransform&)>& transform, const std::function<void(DrawScope&)>& block) {
    DrawTransform t;
    transform(t);
    // Each step inside the one before: the first outermost.
    std::function<void(DrawScope&, size_t)> from = [&](DrawScope& d, size_t i) {
        if (i == t.steps_.size()) return block(d);
        t.steps_[i](d, [&](DrawScope& inner) { from(inner, i + 1); });
    };
    from(*this, 0);
}

void DrawTransform::translate(float left, float top) {
    steps_.push_back([=](DrawScope& d, const std::function<void(DrawScope&)>& next) { d.translate(left, top, next); });
}
void DrawTransform::rotate(float degrees, std::optional<Offset> pivot) {
    steps_.push_back([=](DrawScope& d, const std::function<void(DrawScope&)>& next) { d.rotate(degrees, next, pivot); });
}
void DrawTransform::rotateRad(float radians, std::optional<Offset> pivot) {
    steps_.push_back([=](DrawScope& d, const std::function<void(DrawScope&)>& next) { d.rotateRad(radians, next, pivot); });
}
void DrawTransform::scale(float scaleX, float scaleY, std::optional<Offset> pivot) {
    steps_.push_back([=](DrawScope& d, const std::function<void(DrawScope&)>& next) { d.scale(scaleX, scaleY, next, pivot); });
}
void DrawTransform::transform(const Matrix& matrix) {
    steps_.push_back([=](DrawScope& d, const std::function<void(DrawScope&)>& next) { d.transformed(matrix, next); });
}
void DrawTransform::inset(float left, float top, float right, float bottom) {
    steps_.push_back([=](DrawScope& d, const std::function<void(DrawScope&)>& next) { d.inset(left, top, right, bottom, next); });
}
void DrawTransform::clipRect(Offset topLeft, Size size, ClipOp op) {
    steps_.push_back([=](DrawScope& d, const std::function<void(DrawScope&)>& next) { d.clipRect(topLeft, size, next, op); });
}
void DrawTransform::clipPath(const Path& path, ClipOp op) {
    steps_.push_back([=](DrawScope& d, const std::function<void(DrawScope&)>& next) { d.clipPath(path, next, op); });
}

void DrawScope::clipRect(Offset topLeft, Size size, const std::function<void(DrawScope&)>& block, ClipOp op) {
    if (op == ClipOp::Intersect) context_->pushClip(sorted(topLeft, size));
    else context_->pushClipRoundRect(sorted(topLeft, size), CornerRadii(), op);
    within(block);
    context_->popClip();
}

void DrawScope::transformed(const Matrix& matrix, const std::function<void(DrawScope&)>& block) {
    // The context's coordinates are absolute: from them to this scope's, the matrix, and back.
    Matrix m;
    m.translate(-bounds_.x, -bounds_.y);
    m *= matrix;
    Matrix back;
    back.translate(bounds_.x, bounds_.y);
    m *= back;
    context_->pushTransform(m);
    within(block);
    context_->popTransform();
}

void DrawScope::rotate(float degrees, const std::function<void(DrawScope&)>& block, std::optional<Offset> pivot) {
    Offset p = pivot.value_or(center());
    Matrix m;  // (each step before the one above it)
    m.translate(p.x, p.y);
    m.rotateZ(degrees);
    m.translate(-p.x, -p.y);
    transformed(m, block);
}

void DrawScope::rotateRad(float radians, const std::function<void(DrawScope&)>& block, std::optional<Offset> pivot) {
    rotate(radians * 180 / std::numbers::pi_v<float>, block, pivot);
}

void DrawScope::scale(float scaleX, float scaleY, const std::function<void(DrawScope&)>& block,
                      std::optional<Offset> pivot) {
    Offset p = pivot.value_or(center());
    Matrix m;
    m.translate(p.x, p.y);
    m.scale(scaleX, scaleY);
    m.translate(-p.x, -p.y);
    transformed(m, block);
}

Modifier Modifier::drawBehind(std::function<void(DrawScope&)> onDraw) const {
    auto e = std::make_shared<DrawBehindElement>();
    e->onDraw = std::move(onDraw);
    return then(std::move(e));
}

Modifier Modifier::drawWithContent(std::function<void(ContentDrawScope&)> onDraw) const {
    auto e = std::make_shared<DrawWithContentElement>();
    e->onDraw = std::move(onDraw);
    return then(std::move(e));
}

Modifier Modifier::drawWithCache(std::function<DrawResult(CacheDrawScope&)> build) const {
    auto e = std::make_shared<DrawWithCacheElement>();
    e->build = std::move(build);
    return then(std::move(e));
}

namespace {
// Passes on to `to` only what changes how things are drawn (clips, transforms, layers, blend
// modes, filters), nothing drawn: what the elements between a Self blur and the node's children
// do to them.
class StateOnly final : public GraphicsContext {
public:
    explicit StateOnly(GraphicsContext& to) : to_(to) {}
    void fillRoundRect(const Rect&, const CornerRadii&, Color) override {}
    void strokeRoundRect(const Rect&, const CornerRadii&, float, Color) override {}
    void drawLine(Offset, Offset, float, Color) override {}
    void drawText(std::string_view, Offset, const Font&, Color, bool) override {}
    void drawArc(const Rect&, float, float, float, Color, bool) override {}
    void drawImage(const ImageBitmap&, const Rect&, const Rect&, float, FilterQuality) override {}
    void drawImageMask(const ImageBitmap&, const Rect&, const Rect&, const Brush&, const Rect&, float, FilterQuality) override {}
    void drawPath(const Path&, Offset, const Brush&, const Rect&, const PathStroke&) override {}
    void drawVertices(const Vertices&, Offset, BlendMode, const Brush&, const Rect&, float) override {}
    void fillGradient(const Rect&, const CornerRadii&, const Brush&, const Rect&) override {}
    void drawShadow(const Rect&, const CornerRadii&, const Shadow&, bool) override {}
    void drawShadowPath(const Path&, Offset, const Shadow&, bool) override {}
    void drawBackdrop(const Rect&, const RenderEffect&, float) override {}
    void pushLayer(const Rect& bounds, const CornerRadii& radii, float alpha, const LayerTransform& transform) override {
        to_.pushLayer(bounds, radii, alpha, transform);
    }
    void pushOutlineLayer(const Rect& bounds, const Path& outline, float alpha, const LayerTransform& transform) override {
        to_.pushOutlineLayer(bounds, outline, alpha, transform);
    }
    void pushEffectLayer(const Rect& bounds, const RenderEffect& effect, float alpha) override { to_.pushEffectLayer(bounds, effect, alpha); }
    void popLayer() override { to_.popLayer(); }
    void pushClip(const Rect& rect) override { to_.pushClip(rect); }
    void pushClipRoundRect(const Rect& rect, const CornerRadii& radii, ClipOp op) override { to_.pushClipRoundRect(rect, radii, op); }
    void pushClipPath(const Path& path, Offset origin, ClipOp op) override { to_.pushClipPath(path, origin, op); }
    void popClip() override { to_.popClip(); }
    void pushTranslation(Offset by) override { to_.pushTranslation(by); }
    void popTranslation() override { to_.popTranslation(); }
    void pushTransform(const Matrix& matrix) override { to_.pushTransform(matrix); }
    void popTransform() override { to_.popTransform(); }
    void pushBlendMode(BlendMode mode) override { to_.pushBlendMode(mode); }
    void popBlendMode() override { to_.popBlendMode(); }
    void pushColorFilter(const ColorFilter& filter) override { to_.pushColorFilter(filter); }
    void popColorFilter() override { to_.popColorFilter(); }
    bool quickReject(const Rect& rect) const override { return to_.quickReject(rect); }
    void beginNode(const void* node) override { to_.beginNode(node); }
    void endNode() override { to_.endNode(); }

private:
    GraphicsContext& to_;
};

// Modifier::blur's (but Family with radii, a graphicsLayer): its effect (a BlurEffect of the radii,
// blurred only when both are, or the spec's for the node's size), Clamp at the edges when it is
// clipped to a shape, Decal when it spreads out; drawn as its type says.
struct BlurElement final : ModifierElement {
    std::optional<std::pair<float, float>> radii;  // else scope's spec
    BlurScope scope;
    std::function<void(BlurScope&)> block;  // evaluated at draw time when set, from the defaults

    void draw(GraphicsContext& g, const Rect& bounds, const LayoutNode& node,
              const std::function<void()>& drawContent) const override {
        BlurScope b = block ? BlurScope{} : scope;
        b.size = {bounds.width, bounds.height};
        if (block) block(b);
        bool clip = b.edgeTreatment.shape.has_value();
        TileMode edges = clip ? TileMode::Clamp : TileMode::Decal;
        detail::LayerDraw d;
        d.bounds = bounds;
        d.geometry.pivot = bounds.center();
        if (radii && radii->first > 0 && radii->second > 0) d.renderEffect = BlurEffect(radii->first, radii->second, edges);
        else if (!radii && !(b.blurRadiusSpec == BlurRadiusSpec::uniform(0))) d.renderEffect = b.blurRadiusSpec.createRenderEffect(
                b.size, edges, node.host() ? node.host()->density() : Density{});
        d.alpha = b.alpha;
        d.clip = clip;
        d.outline = clip ? b.edgeTreatment.shape->createOutline(b.size, node.rtl ? LayoutDirection::Rtl : LayoutDirection::Ltr)
                         : Outline::Rectangle({0, 0, bounds.width, bounds.height});
        detail::DrawPart& part = detail::drawPart();
        switch (b.type) {
        case BlurType::Family: return detail::drawThroughLayer(g, d, drawContent);
        case BlurType::Self: {
            // What the node draws itself through the layer; then its children over it, sharp, placed
            // as the elements in between place them.
            detail::DrawPart outer = std::exchange(part, {&node, true, nullptr});
            detail::drawThroughLayer(g, d, drawContent);
            StateOnly placing(g);
            part = {&node, false, &g, detail::recordingInto()};
            GraphicsContext* into = std::exchange(detail::recordingInto(), &placing);
            drawContent();
            detail::recordingInto() = into;
            part = outer;
            return;
        }
        case BlurType::Morph: {
            // What is behind, blurred, within its shape; the node over it, sharp.
            float alpha = std::clamp(b.alpha, 0.f, 1.f);
            if (d.renderEffect && alpha > 0 && !g.quickReject(d.renderEffect->outputBounds(bounds))) {
                if (clip) pushOutlineClip(g, d.outline, bounds);
                g.drawBackdrop(bounds, *d.renderEffect, alpha);
                if (clip) g.popClip();
            }
            return drawContent();
        }
        }
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const BlurElement*>(&other);
        return o && !block && !o->block && o->radii == radii && o->scope == scope;
    }
};
} // namespace

// Family is Compose's: a graphicsLayer, blurred only when both radii are (a BlurEffect's), Clamp at
// the edges when it is clipped to a shape, Decal when it spreads out.
Modifier Modifier::blur(float radiusX, float radiusY, const BlurredEdgeTreatment& edgeTreatment, BlurType type) const {
    bool clip = edgeTreatment.shape.has_value();
    if (!((radiusX > 0 && radiusY > 0) || clip)) return *this;
    if (type != BlurType::Family) {
        auto e = std::make_shared<BlurElement>();
        e->radii = std::pair{radiusX, radiusY};
        e->scope.edgeTreatment = edgeTreatment;
        e->scope.type = type;
        return then(std::move(e));
    }
    GraphicsLayerScope layer;
    if (radiusX > 0 && radiusY > 0) layer.renderEffect = BlurEffect(radiusX, radiusY, clip ? TileMode::Clamp : TileMode::Decal);
    layer.shape = edgeTreatment.shape.value_or(RectangleShape);
    layer.clip = clip;
    return graphicsLayer(layer);
}

// As Compose's BlurNode (Family): always a layer, its alpha.
Modifier Modifier::blur(const BlurRadiusSpec& blurRadiusSpec, const BlurredEdgeTreatment& edgeTreatment, float alpha, BlurType type) const {
    auto e = std::make_shared<BlurElement>();
    e->scope.blurRadiusSpec = blurRadiusSpec;
    e->scope.edgeTreatment = edgeTreatment;
    e->scope.alpha = alpha;
    e->scope.type = type;
    return then(std::move(e));
}

Modifier Modifier::blur(std::function<void(BlurScope&)> block) const {
    auto e = std::make_shared<BlurElement>();
    e->block = std::move(block);
    return then(std::move(e));
}

Modifier Modifier::graphicsLayer(const GraphicsLayerScope& layer) const {
    auto e = std::make_shared<GraphicsLayerElement>();
    e->layer = layer;
    return then(std::move(e));
}

Modifier Modifier::graphicsLayer(std::function<void(GraphicsLayerScope&)> block) const {
    auto e = std::make_shared<GraphicsLayerElement>();
    e->block = std::move(block);
    return then(std::move(e));
}

// ---- GraphicsLayer -------------------------------------------------------------------------

struct GraphicsLayer::Data {
    ComposeHost* host = nullptr;
    std::unique_ptr<DisplayList> list;  // the recording, from its top left
    Size size;
    Properties properties;
};

GraphicsLayer::GraphicsLayer() : data_(std::make_shared<Data>()) {}
GraphicsLayer::Properties& GraphicsLayer::properties() const { return data_->properties; }
Size GraphicsLayer::size() const { return data_->size; }
bool GraphicsLayer::empty() const { return !data_->list; }

ImageBitmap GraphicsLayer::toImageBitmap() const {
    const Data& d = *data_;
    if (!d.list || !d.host || d.size.width <= 0 || d.size.height <= 0) return {};
    const DisplayList& list = *d.list;
    return d.host->renderImage(d.size, [&list](GraphicsContext& canvas) { list.replay(canvas); });
}

GraphicsLayer rememberGraphicsLayer(CallSite site) {
    ComposeHost& host = detail::Composer::require().host();
    return remember(
        [&] {
            GraphicsLayer layer;
            layer.data_->host = &host;
            return layer;
        },
        site);
}

void DrawScope::record(const GraphicsLayer& layer, const std::function<void(DrawScope&)>& block, std::optional<Size> size) {
    GraphicsLayer::Data& d = *layer.data_;
    TextMeasurer* measurer = measurer_ ? measurer_ : d.host ? &d.host->textMeasurer() : nullptr;
    if (!measurer) return;
    if (!d.list) d.list = std::make_unique<DisplayList>(*measurer);
    d.size = size.value_or(this->size());
    d.list->begin(d.size, 1);
    d.list->pushTranslation({-bounds_.x, -bounds_.y});  // drawn where it is, kept from its top left
    GraphicsContext* outer = std::exchange(detail::recordingInto(), d.list.get());
    DrawScope scope(*d.list, {bounds_.x, bounds_.y, d.size.width, d.size.height}, measurer, density_, layoutDirection_);
    block(scope);
    detail::recordingInto() = outer;
    d.list->popTranslation();
    d.list->end();
}

void DrawScope::drawLayer(const GraphicsLayer& layer) {
    const GraphicsLayer::Data& d = *layer.data_;
    if (!d.list) return;
    const GraphicsLayer::Properties& p = d.properties;
    Rect at{bounds_.x + p.topLeft.x, bounds_.y + p.topLeft.y, d.size.width, d.size.height};
    Offset pivot = p.pivotOffset.value_or(Offset{d.size.width / 2, d.size.height / 2});
    detail::LayerDraw l;
    l.bounds = at;
    l.geometry = {p.scaleX, p.scaleY, p.translationX, p.translationY, p.rotationX, p.rotationY, p.rotationZ, p.cameraDistance,
                  {at.x + pivot.x, at.y + pivot.y}, d.host ? d.host->density().density : 1.f};
    l.alpha = p.alpha;
    l.shadowElevation = p.shadowElevation;
    l.ambientShadowColor = p.ambientShadowColor, l.spotShadowColor = p.spotShadowColor;
    l.clip = p.clip;
    l.outline = p.outline.value_or(Outline::Rectangle({0, 0, d.size.width, d.size.height}));
    l.renderEffect = p.renderEffect;
    l.blendMode = p.blendMode;
    l.colorFilter = p.colorFilter;
    l.strategy = p.compositingStrategy;
    detail::drawThroughLayer(*context_, l, [&] {
        context_->pushTranslation({at.x, at.y});
        d.list->replay(*context_);
        context_->popTranslation();
    });
}

void GraphicsLayer::setRectOutline(Offset topLeft, Size size) const {
    data_->properties.outline = Outline::Rectangle({topLeft.x, topLeft.y, size.width, size.height});
}
void GraphicsLayer::setRoundRectOutline(Offset topLeft, Size size, float cornerRadius) const {
    data_->properties.outline = Outline::Rounded({{topLeft.x, topLeft.y, size.width, size.height}, cornerRadius, cornerRadius, cornerRadius, cornerRadius});
}
void GraphicsLayer::setPathOutline(const Path& path) const { data_->properties.outline = Outline::Generic(path); }

// ---- Painter -------------------------------------------------------------------------------

struct Painter::Data {
    enum class Kind { Bitmap, Vector, Fill, Gradient, Custom, ChangingVector };
    Kind kind = Kind::Custom;
    ImageBitmap bitmap;
    ImageVector vector;
    Color color;
    std::optional<Brush> brush;
    std::function<void(DrawScope&)> onDraw;
    std::optional<Size> intrinsic;
    std::function<ImageVector()> vectorNow;   // Kind::ChangingVector: what to draw now...
    std::shared_ptr<ImageVector> lastVector;  // ...and what it last drew (kept while the same)
};

namespace {
Painter painterOf(Painter::Data data) {
    Painter p;
    p.data_ = std::make_shared<const Painter::Data>(std::move(data));
    return p;
}
} // namespace

Painter Painter::custom(std::function<void(DrawScope&)> onDraw, std::optional<Size> intrinsicSize) {
    return painterOf({.kind = Data::Kind::Custom, .onDraw = std::move(onDraw), .intrinsic = intrinsicSize});
}
Painter BitmapPainter(ImageBitmap image) {
    std::optional<Size> size;
    if (!image.empty()) size = Size{float(image.width()), float(image.height())};
    return painterOf({.kind = Painter::Data::Kind::Bitmap, .bitmap = std::move(image), .intrinsic = size});
}
Painter VectorPainter(ImageVector image) {
    std::optional<Size> size;
    if (!image.empty()) size = Size{image.defaultWidth(), image.defaultHeight()};
    return painterOf({.kind = Painter::Data::Kind::Vector, .vector = std::move(image), .intrinsic = size});
}
Painter detail::changingVectorPainter(std::function<ImageVector()> vectorNow, ImageVector first) {
    std::optional<Size> size;
    if (!first.empty()) size = Size{first.defaultWidth(), first.defaultHeight()};
    return painterOf({.kind = Painter::Data::Kind::ChangingVector, .intrinsic = size, .vectorNow = std::move(vectorNow),
                      .lastVector = std::make_shared<ImageVector>(std::move(first))});
}
Painter ColorPainter(Color color) { return painterOf({.kind = Painter::Data::Kind::Fill, .color = color}); }
Painter BrushPainter(Brush brush) { return painterOf({.kind = Painter::Data::Kind::Gradient, .brush = std::move(brush)}); }

bool Painter::hasIntrinsicSize() const { return data_ && data_->intrinsic; }
Size Painter::intrinsicSize() const { return hasIntrinsicSize() ? *data_->intrinsic : Size{}; }

void Painter::draw(GraphicsContext& canvas, const Rect& b, float alpha, const std::optional<ColorFilter>& colorFilter, bool rtl,
                   Density density) const {
    if (!data_ || alpha <= 0 || b.width <= 0 || b.height <= 0) return;
    const Data& d = *data_;
    using Kind = Data::Kind;
    LayoutDirection direction = rtl ? LayoutDirection::Rtl : LayoutDirection::Ltr;
    if (d.kind == Kind::Vector) {  // (the filter its own way: before its tint)
        DrawScope scope(canvas, b, nullptr, density, direction);
        return detail::drawVector(scope, d.vector, alpha, colorFilter);
    }
    if (d.kind == Kind::ChangingVector) {  // (as a vector)
        if (ImageVector now = d.vectorNow(); !(*d.lastVector == now)) *d.lastVector = std::move(now);
        DrawScope scope(canvas, b, nullptr, density, direction);
        return detail::drawVector(scope, *d.lastVector, alpha, colorFilter);
    }
    detail::Filtering filtering(canvas, colorFilter);  // what it draws, through the filter
    switch (d.kind) {
    case Kind::Bitmap:
        if (!d.bitmap.empty()) canvas.drawImage(d.bitmap, {0, 0, float(d.bitmap.width()), float(d.bitmap.height())}, b, alpha);
        break;
    case Kind::Vector:
    case Kind::ChangingVector: break;
    case Kind::Fill: canvas.fillRoundRect(b, CornerRadii(), d.color.copy(d.color.alpha() * alpha)); break;
    case Kind::Gradient:
    case Kind::Custom: {
        bool layer = alpha < 1;
        if (layer) canvas.pushLayer(b, CornerRadii(), alpha);
        DrawScope scope(canvas, b, nullptr, density, direction);
        if (d.kind == Kind::Gradient) scope.drawRect(*d.brush);
        else if (d.onDraw) d.onDraw(scope);
        if (layer) canvas.popLayer();
        break;
    }
    }
}

bool operator==(const Painter& a, const Painter& b) {
    if (a.data_ == b.data_) return true;
    if (!a.data_ || !b.data_ || a.data_->kind != b.data_->kind) return false;
    const Painter::Data &x = *a.data_, &y = *b.data_;
    switch (x.kind) {
    case Painter::Data::Kind::Bitmap: return x.bitmap == y.bitmap;
    case Painter::Data::Kind::Vector: return x.vector == y.vector;
    case Painter::Data::Kind::Fill: return x.color == y.color;
    case Painter::Data::Kind::Gradient: return x.brush == y.brush;
    case Painter::Data::Kind::Custom:
    case Painter::Data::Kind::ChangingVector: return false;
    }
    return false;
}

namespace {

// How much a source is scaled to fill a destination (Compose's ContentScale.computeScaleFactor).
std::pair<float, float> scaleFactor(Size src, Size dst, ContentScale scale) {
    float fit = std::min(dst.width / src.width, dst.height / src.height);
    switch (scale) {
    case ContentScale::Fit: return {fit, fit};
    case ContentScale::Crop: {
        float crop = std::max(dst.width / src.width, dst.height / src.height);
        return {crop, crop};
    }
    case ContentScale::FillBounds: return {dst.width / src.width, dst.height / src.height};
    case ContentScale::Inside: return src.width <= dst.width && src.height <= dst.height ? std::pair{1.f, 1.f} : std::pair{fit, fit};
    case ContentScale::None: return {1, 1};
    case ContentScale::FillWidth: return {dst.width / src.width, dst.width / src.width};
    case ContentScale::FillHeight: return {dst.height / src.height, dst.height / src.height};
    }
    return {1, 1};
}

// Modifier.paint (Compose's PainterNode): sizes the node by the painter's intrinsic size and
// draws the painter, scaled and aligned, behind the content.
struct PainterElement final : ModifierElement {
    Painter painter;
    bool sizeToIntrinsics = true;
    Alignment alignment = Alignment::Center;
    ContentScale contentScale = ContentScale::Inside;
    float alpha = 1;
    std::optional<ColorFilter> colorFilter;

    bool useIntrinsicSize() const { return sizeToIntrinsics && painter.hasIntrinsicSize(); }
    // The painter's size drawn in `dst`: its own scaled by the content scale (a dimension it
    // doesn't have is the destination's).
    Size scaledSize(Size dst) const {
        if (dst.width <= 0 || dst.height <= 0) return {};
        Size own = painter.intrinsicSize();
        Size src{painter.hasIntrinsicSize() && own.width > 0 ? own.width : dst.width, painter.hasIntrinsicSize() && own.height > 0 ? own.height : dst.height};
        auto [sx, sy] = scaleFactor(src, dst, contentScale);
        return {src.width * sx, src.height * sy};
    }
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        bool bounded = c.hasBoundedWidth() && c.hasBoundedHeight();
        bool fixed = c.minWidth == c.maxWidth && c.minHeight == c.maxHeight;
        if ((!useIntrinsicSize() && bounded) || fixed) return next({c.maxWidth, c.maxWidth, c.maxHeight, c.maxHeight});
        Size own = painter.intrinsicSize();
        float w = std::clamp(painter.hasIntrinsicSize() ? own.width : c.minWidth, c.minWidth, c.maxWidth);
        float h = std::clamp(painter.hasIntrinsicSize() ? own.height : c.minHeight, c.minHeight, c.maxHeight);
        // Not past the most it may be (a cropped painter is bigger than its node).
        Size scaled = useIntrinsicSize() ? scaledSize({w, h}) : Size{w, h};
        return next({std::clamp(scaled.width, c.minWidth, c.maxWidth), c.maxWidth, std::clamp(scaled.height, c.minHeight, c.maxHeight),
                     c.maxHeight});
    }
    void draw(GraphicsContext& canvas, const Rect& b, const LayoutNode& node, const std::function<void()>& drawContent) const override {
        Size scaled = scaledSize({b.width, b.height});
        Alignment a = alignment;
        if (node.rtl) a.horizontal = -a.horizontal;  // start is on the right
        Offset at = a.align(scaled, {b.width, b.height});
        painter.draw(canvas, {b.x + at.x, b.y + at.y, scaled.width, scaled.height}, alpha, colorFilter, node.rtl,
                     node.host() ? node.host()->density() : Density{});
        drawContent();
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const PainterElement*>(&other);
        return o && o->painter == painter && o->sizeToIntrinsics == sizeToIntrinsics && o->alignment == alignment &&
               o->contentScale == contentScale && o->alpha == alpha && o->colorFilter == colorFilter;
    }
};

} // namespace

Modifier Modifier::paint(const Painter& painter, bool sizeToIntrinsics, Alignment alignment, ContentScale contentScale, float alpha,
                         std::optional<ColorFilter> colorFilter) const {
    auto e = std::make_shared<PainterElement>();
    e->painter = painter;
    e->sizeToIntrinsics = sizeToIntrinsics;
    e->alignment = alignment;
    e->contentScale = contentScale;
    e->alpha = alpha;
    e->colorFilter = std::move(colorFilter);
    return then(std::move(e));
}

void Canvas(const Modifier& modifier, std::function<void(DrawScope&)> onDraw, CallSite site) {
    Spacer(modifier.drawBehind(std::move(onDraw)), site);
}

} // namespace kompoz
