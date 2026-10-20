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
        DrawScope scope(g, bounds, &node.textMeasurer());
        onDraw(scope);
        drawContent();
    }
};

struct DrawWithContentElement final : ModifierElement {
    std::function<void(ContentDrawScope&)> onDraw;
    void draw(GraphicsContext& g, const Rect& bounds, const LayoutNode& node,
              const std::function<void()>& drawContent) const override {
        ContentDrawScope scope(g, bounds, &node.textMeasurer(), drawContent);
        onDraw(scope);
    }
};

// The cache lives in the element: a recomposition (new captured values) or a new size rebuilds it.
struct DrawWithCacheElement final : ModifierElement {
    std::function<DrawResult(CacheDrawScope&)> build;
    struct Cache {
        bool valid = false;
        Size size;
        DrawResult result;
    };
    std::shared_ptr<Cache> cache = std::make_shared<Cache>();

    void draw(GraphicsContext& g, const Rect& bounds, const LayoutNode& node,
              const std::function<void()>& drawContent) const override {
        Size size{bounds.width, bounds.height};
        if (!cache->valid || !(cache->size == size)) {
            CacheDrawScope scope(size);
            cache->result = build(scope);
            cache->size = size;
            cache->valid = true;
        }
        const DrawResult& r = cache->result;
        if (r.withContent) {
            ContentDrawScope scope(g, bounds, &node.textMeasurer(), drawContent);
            r.withContent(scope);
            return;
        }
        if (r.behind) {
            DrawScope scope(g, bounds, &node.textMeasurer());
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
    if (!moved && !offscreen && !modulated && !l.clip) return content();
    if (moved) g.pushTransform(m);
    Rect reach{l.bounds.x - l.outsets.left, l.bounds.y - l.outsets.top, l.bounds.width + l.outsets.left + l.outsets.right,
               l.bounds.height + l.outsets.top + l.outsets.bottom};
    // Nothing of it can show (the transform taken into account): not drawn at all.
    if (!g.quickReject(l.renderEffect ? l.renderEffect->outputBounds(reach) : reach)) {
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
        d.clip = l.clip;
        if (l.clip) d.outline = l.shape.createOutline(l.size, node.rtl ? LayoutDirection::Rtl : LayoutDirection::Ltr);
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

void DrawScope::drawRect(Color color, Offset topLeft, std::optional<Size> size, DrawStyle style, BlendMode blendMode) {
    drawRoundRect(color, 0, topLeft, size, style, blendMode);
}

void DrawScope::drawRoundRect(Color color, float cornerRadius, Offset topLeft, std::optional<Size> size,
                              DrawStyle style, BlendMode blendMode) {
    Rect r = local(topLeft, size);
    if (style.isStroke() && style.pathEffect) {  // as a path, for the effect to work on
        Path outline;
        outline.addRoundRect({r.x - bounds_.x, r.y - bounds_.y, r.width, r.height}, cornerRadius);
        return drawPath(outline, color, style, blendMode);
    }
    Blending blending(*context_, blendMode);
    if (style.isStroke()) {
        // On the outline, half of it each side (as Compose's); strokeRoundRect strokes inside.
        float half = style.strokeWidth / 2;
        context_->strokeRoundRect({r.x - half, r.y - half, r.width + 2 * half, r.height + 2 * half}, cornerRadius > 0 ? cornerRadius + half : 0,
                                  style.strokeWidth, color);
    } else
        context_->fillRoundRect(r, cornerRadius, color);
}

void DrawScope::drawCircle(Color color, std::optional<float> radius, std::optional<Offset> center, DrawStyle style,
                           BlendMode blendMode) {
    float r = radius.value_or(std::min(bounds_.width, bounds_.height) / 2);
    Offset c = center.value_or(this->center());
    drawRoundRect(color, r, {c.x - r, c.y - r}, Size{2 * r, 2 * r}, style, blendMode);
}

void DrawScope::drawRect(const Brush& brush, Offset topLeft, std::optional<Size> size, BlendMode blendMode) {
    drawRoundRect(brush, 0, topLeft, size, blendMode);
}

void DrawScope::drawRoundRect(const Brush& brush, float cornerRadius, Offset topLeft, std::optional<Size> size, BlendMode blendMode) {
    Blending blending(*context_, blendMode);
    context_->fillGradient(local(topLeft, size), cornerRadius, brush, bounds_);
}

void DrawScope::drawCircle(const Brush& brush, std::optional<float> radius, std::optional<Offset> center, BlendMode blendMode) {
    float r = radius.value_or(std::min(bounds_.width, bounds_.height) / 2);
    Offset c = center.value_or(this->center());
    drawRoundRect(brush, r, {c.x - r, c.y - r}, Size{2 * r, 2 * r}, blendMode);
}

void DrawScope::drawPath(const Path& path, Color color, DrawStyle style, BlendMode blendMode) {
    drawPath(path, Brush::solid(color), style, blendMode);
}

void DrawScope::drawPath(const Path& path, const Brush& brush, DrawStyle style, BlendMode blendMode) {
    if (path.isEmpty()) return;
    Blending blending(*context_, blendMode);
    if (style.isStroke() && style.pathEffect) {
        Path effected = style.pathEffect->apply(path);
        if (effected.isEmpty()) return;
        PathStroke stroke{style.strokeWidth, style.cap, style.join, 4};
        if (style.pathEffect->fills()) stroke.width = 0;  // stamped shapes are filled
        context_->drawPath(effected, {bounds_.x, bounds_.y}, brush, bounds_, stroke);
        return;
    }
    context_->drawPath(path, {bounds_.x, bounds_.y}, brush, bounds_,
                       {style.strokeWidth, style.cap, style.join, 4});
}

void DrawScope::drawLine(Color color, Offset start, Offset end, float strokeWidth) {
    context_->drawLine(abs(start), abs(end), strokeWidth, color);
}

void DrawScope::drawLine(Color color, Offset start, Offset end, float strokeWidth, StrokeCap cap, std::optional<PathEffect> pathEffect) {
    if (cap == StrokeCap::Butt && !pathEffect) return drawLine(color, start, end, strokeWidth);
    Path line;
    line.moveTo(start.x, start.y);
    line.lineTo(end.x, end.y);
    drawPath(line, color, Stroke(strokeWidth, cap, StrokeJoin::Miter, pathEffect));
}

void DrawScope::drawPoints(const std::vector<Offset>& points, PointMode mode, Color color, float strokeWidth, StrokeCap cap,
                           std::optional<PathEffect> pathEffect) {
    if (points.empty() || strokeWidth <= 0) return;
    Path path;
    if (mode == PointMode::Points) {  // dots as wide as the stroke, filled
        float r = strokeWidth / 2;
        for (Offset p : points) {
            Rect dot{p.x - r, p.y - r, strokeWidth, strokeWidth};
            cap == StrokeCap::Round ? path.addOval(dot) : path.addRect(dot);
        }
        return drawPath(path, color, Fill);
    }
    for (size_t i = 0; i < points.size(); ++i) {
        bool start = mode == PointMode::Polygon ? i == 0 : i % 2 == 0;
        if (mode == PointMode::Lines && start && i + 1 == points.size()) break;  // an odd one out has no partner
        start ? path.moveTo(points[i].x, points[i].y) : path.lineTo(points[i].x, points[i].y);
    }
    drawPath(path, color, Stroke(strokeWidth, cap, StrokeJoin::Miter, pathEffect));
}

void DrawScope::drawArc(Color color, float startAngle, float sweepAngle, Offset topLeft, std::optional<Size> size,
                        DrawStyle style) {
    Rect oval = local(topLeft, size);
    if (style.isStroke() && style.pathEffect) {  // the arc as a path (a point every degree or so)
        Path arc;
        int steps = std::max(2, int(std::ceil(std::abs(sweepAngle))));
        float cx = oval.x - bounds_.x + oval.width / 2, cy = oval.y - bounds_.y + oval.height / 2;
        for (int i = 0; i <= steps; ++i) {
            float a = (startAngle + sweepAngle * float(i) / float(steps)) * 3.14159265f / 180.f;
            float x = cx + std::cos(a) * oval.width / 2, y = cy + std::sin(a) * oval.height / 2;
            i == 0 ? arc.moveTo(x, y) : arc.lineTo(x, y);
        }
        return drawPath(arc, color, style);
    }
    if (style.isStroke()) {
        context_->drawArc(oval, startAngle, sweepAngle, style.strokeWidth, color, style.cap == StrokeCap::Round);
        return;
    }
    // A pie slice is an arc stroked at half the radius, as wide as the radius.
    float r = std::min(oval.width, oval.height) / 2;
    Offset c = oval.center();
    context_->drawArc({c.x - r / 2, c.y - r / 2, r, r}, startAngle, sweepAngle, r, color, false);
}

Size DrawScope::measureText(std::string_view text, Font font) const {
    if (!measurer_) return {};
    return {measurer_->measureWidth(text, font), measurer_->metrics(font).lineHeight};
}

void DrawScope::drawText(std::string_view text, Offset topLeft, Font font, Color color) {
    if (!measurer_) return;
    FontMetrics m = measurer_->metrics(font);
    context_->drawText(text, abs({topLeft.x, topLeft.y + m.ascent}), font, color, bidi::firstStrongIsRtl(text));
}

void DrawScope::drawText(const TextLayoutResult& layout, Offset topLeft, std::optional<Color> color) {
    if (!layout.data_) return;
    const TextLayoutResult::Data& d = *layout.data_;
    Rect at{bounds_.x + topLeft.x, bounds_.y + topLeft.y, d.width, d.layout.height};
    if (!color) return detail::drawStyledText(*context_, d.layout, at, d.align, d.overflow);
    detail::StyledTextLayout painted = d.layout;
    for (auto& run : painted.runs) run.color = *color, run.brush.reset();
    detail::drawStyledText(*context_, painted, at, d.align, d.overflow);
}

void DrawScope::drawText(const TextLayoutMeasurer& measurer, const std::string& text, Offset topLeft, const TextStyle& style) {
    drawText(measurer.measure(text, style, bounds_.width - topLeft.x, bounds_.height - topLeft.y), topLeft);
}

void DrawScope::drawImage(const ImageBitmap& image, Offset topLeft, std::optional<Size> size, float alpha,
                          std::optional<ColorFilter> colorFilter) {
    if (image.empty()) return;
    Rect dst = local(topLeft, size.value_or(Size{float(image.width()), float(image.height())}));
    detail::Filtering filtering(*context_, colorFilter);
    context_->drawImage(image, {0, 0, float(image.width()), float(image.height())}, dst, alpha);
}

void DrawScope::drawVector(const ImageVector& vector, Color color, Offset topLeft, std::optional<Size> size) {
    detail::drawImageVector(*context_, vector, local(topLeft, size), color);
}

void DrawScope::translate(float left, float top, const std::function<void(DrawScope&)>& block) {
    context_->pushTranslation({left, top});  // the content too (drawContent() inside it)
    block(*this);
    context_->popTranslation();
}

void DrawScope::inset(float left, float top, float right, float bottom, const std::function<void(DrawScope&)>& block) {
    DrawScope inner(*context_,
                    {bounds_.x + left, bounds_.y + top, std::max(0.f, bounds_.width - left - right),
                     std::max(0.f, bounds_.height - top - bottom)},
                    measurer_);
    block(inner);
}

void DrawScope::drawOval(Color color, Offset topLeft, std::optional<Size> size, DrawStyle style, BlendMode blendMode) {
    drawOval(Brush::solid(color), topLeft, size, style, blendMode);
}

void DrawScope::drawOval(const Brush& brush, Offset topLeft, std::optional<Size> size, DrawStyle style, BlendMode blendMode) {
    Size s = size.value_or(Size{bounds_.width - topLeft.x, bounds_.height - topLeft.y});
    Path oval;
    oval.addOval({topLeft.x, topLeft.y, s.width, s.height});
    drawPath(oval, brush, style, blendMode);
}

void DrawScope::clipPath(const Path& path, const std::function<void(DrawScope&)>& block, ClipOp op) {
    context_->pushClipPath(path, {bounds_.x, bounds_.y}, op);
    block(*this);
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
    if (op == ClipOp::Intersect) context_->pushClip(local(topLeft, size));
    else context_->pushClipRoundRect(local(topLeft, size), CornerRadii(), op);
    block(*this);
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
    block(*this);
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
    void drawVector(const ImageVector&, const Rect&, Color) override {}
    void drawArc(const Rect&, float, float, float, Color, bool) override {}
    void drawImage(const ImageBitmap&, const Rect&, const Rect&, float) override {}
    void drawPath(const Path&, Offset, const Brush&, const Rect&, const PathStroke&) override {}
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
        else if (!radii && !(b.blurRadiusSpec == BlurRadiusSpec::uniform(0))) d.renderEffect = b.blurRadiusSpec.createRenderEffect(b.size, edges);
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
    DrawScope scope(*d.list, {bounds_.x, bounds_.y, d.size.width, d.size.height}, measurer);
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
    enum class Kind { Bitmap, Vector, Fill, Gradient, Custom };
    Kind kind = Kind::Custom;
    ImageBitmap bitmap;
    ImageVector vector;
    Color color;
    std::optional<Brush> brush;
    std::function<void(DrawScope&)> onDraw;
    std::optional<Size> intrinsic;
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
Painter ColorPainter(Color color) { return painterOf({.kind = Painter::Data::Kind::Fill, .color = color}); }
Painter BrushPainter(Brush brush) { return painterOf({.kind = Painter::Data::Kind::Gradient, .brush = std::move(brush)}); }

bool Painter::hasIntrinsicSize() const { return data_ && data_->intrinsic; }
Size Painter::intrinsicSize() const { return hasIntrinsicSize() ? *data_->intrinsic : Size{}; }

void Painter::draw(GraphicsContext& canvas, const Rect& b, float alpha, const std::optional<ColorFilter>& colorFilter, bool rtl) const {
    if (!data_ || alpha <= 0 || b.width <= 0 || b.height <= 0) return;
    const Data& d = *data_;
    using Kind = Data::Kind;
    detail::Filtering filtering(canvas, colorFilter);  // what it draws, through the filter
    switch (d.kind) {
    case Kind::Bitmap:
        if (!d.bitmap.empty()) canvas.drawImage(d.bitmap, {0, 0, float(d.bitmap.width()), float(d.bitmap.height())}, b, alpha);
        break;
    case Kind::Vector:  // in its own paints (a one-color one black, as Compose's icons are)
        if (!d.vector.empty()) detail::drawImageVector(canvas, d.vector.autoMirror() && rtl ? d.vector.mirrored() : d.vector, b, std::nullopt, alpha);
        break;
    case Kind::Fill: canvas.fillRoundRect(b, CornerRadii(), d.color.copy(d.color.a * alpha)); break;
    case Kind::Gradient:
    case Kind::Custom: {
        bool layer = alpha < 1;
        if (layer) canvas.pushLayer(b, CornerRadii(), alpha);
        DrawScope scope(canvas, b);
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
    case Painter::Data::Kind::Custom: return false;
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
        painter.draw(canvas, {b.x + at.x, b.y + at.y, scaled.width, scaled.height}, alpha, colorFilter, node.rtl);
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
