#include <kompoz/host.hpp>
#include <kompoz/display_list.hpp>
#include "text_layout.hpp"

#include <kompoz/bidi.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/text_measure.hpp>
#include <kompoz/widgets.hpp>

#include <cmath>

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

struct GraphicsLayerElement final : ModifierElement {
    GraphicsLayerScope layer;
    std::function<void(GraphicsLayerScope&)> block;  // evaluated at draw time when set

    void draw(GraphicsContext& g, const Rect& bounds, const LayoutNode& node,
              const std::function<void()>& drawContent) const override {
        GraphicsLayerScope l = layer;
        if (block) block(l);
        LayerTransform t{l.scaleX, l.scaleY, l.rotationZ, l.translationX, l.translationY, l.transformOrigin};
        float alpha = std::clamp(l.alpha, 0.f, 1.f);
        if (alpha <= 0 || l.scaleX == 0 || l.scaleY == 0) return;
        bool offscreen = l.compositingStrategy == CompositingStrategy::Offscreen;
        if (t.identity() && alpha >= 1 && !l.clip && !offscreen) return drawContent();
        if (t.identity() && alpha >= 1 && l.shape.kind == Shape::Kind::Rectangle && !offscreen) {  // a plain clip is cheaper
            g.pushClip(bounds);
            drawContent();
            g.popClip();
            return;
        }
        // Nothing shows outside the layer's bounds moved by its transform (a circle around them
        // when it turns): offscreen, it isn't drawn at all.
        Offset o{bounds.x + bounds.width * t.origin.x, bounds.y + bounds.height * t.origin.y};
        float hx = std::max(std::abs(bounds.x - o.x), std::abs(bounds.right() - o.x)) * std::abs(t.scaleX);
        float hy = std::max(std::abs(bounds.y - o.y), std::abs(bounds.bottom() - o.y)) * std::abs(t.scaleY);
        if (t.rotationZ != 0) hx = hy = std::hypot(hx, hy);
        if (g.quickReject({o.x + t.translationX - hx, o.y + t.translationY - hy, 2 * hx, 2 * hy})) return;
        if (l.clip)
            g.pushShapeLayer(bounds, l.shape, node.rtl, alpha, t);
        else
            g.pushLayer(bounds, CornerRadii(), alpha, t);
        drawContent();
        g.popLayer();
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
    context_->drawImage(colorFilter ? colorFilter->apply(image) : image, {0, 0, float(image.width()), float(image.height())}, dst, alpha);
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
    if (bounds_.width <= 0 || bounds_.height <= 0) return;
    context_->pushLayer(bounds_, 0, 1, {});
    block(*this);
    // What goes is erased (DstOut): outside the path, or inside it. (A DstIn mask would leave
    // alone whatever the path's drawing doesn't cover at all.)
    if (op == ClipOp::Intersect) {
        Path area;
        area.addRect({0, 0, bounds_.width, bounds_.height});
        drawPath(Path::combine(PathOperation::Difference, area, path), colors::Black, Fill, BlendMode::DstOut);
    } else {
        drawPath(path, colors::Black, Fill, BlendMode::DstOut);
    }
    context_->popLayer();
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
void DrawTransform::scale(float scaleX, float scaleY, std::optional<Offset> pivot) {
    steps_.push_back([=](DrawScope& d, const std::function<void(DrawScope&)>& next) { d.scale(scaleX, scaleY, next, pivot); });
}
void DrawTransform::inset(float left, float top, float right, float bottom) {
    steps_.push_back([=](DrawScope& d, const std::function<void(DrawScope&)>& next) { d.inset(left, top, right, bottom, next); });
}
void DrawTransform::clipRect(Offset topLeft, Size size) {
    steps_.push_back([=](DrawScope& d, const std::function<void(DrawScope&)>& next) { d.clipRect(topLeft, size, next); });
}
void DrawTransform::clipPath(const Path& path, ClipOp op) {
    steps_.push_back([=](DrawScope& d, const std::function<void(DrawScope&)>& next) { d.clipPath(path, next, op); });
}

void DrawScope::clipRect(Offset topLeft, Size size, const std::function<void(DrawScope&)>& block) {
    context_->pushClip(local(topLeft, size));
    block(*this);
    context_->popClip();
}

void DrawScope::transformed(const LayerTransform& t, const std::function<void(DrawScope&)>& block) {
    if (bounds_.width <= 0 || bounds_.height <= 0) return;
    context_->pushLayer(bounds_, 0, 1, t);
    block(*this);
    context_->popLayer();
}

void DrawScope::rotate(float degrees, const std::function<void(DrawScope&)>& block, std::optional<Offset> pivot) {
    Offset p = pivot.value_or(center());
    LayerTransform t;
    t.rotationZ = degrees;
    t.origin = {bounds_.width > 0 ? p.x / bounds_.width : 0.5f, bounds_.height > 0 ? p.y / bounds_.height : 0.5f};
    transformed(t, block);
}

void DrawScope::scale(float scaleX, float scaleY, const std::function<void(DrawScope&)>& block,
                      std::optional<Offset> pivot) {
    Offset p = pivot.value_or(center());
    LayerTransform t;
    t.scaleX = scaleX;
    t.scaleY = scaleY;
    t.origin = {bounds_.width > 0 ? p.x / bounds_.width : 0.5f, bounds_.height > 0 ? p.y / bounds_.height : 0.5f};
    transformed(t, block);
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
struct BlurElement final : ModifierElement {
    float radiusX = 0, radiusY = 0;
    BlurredEdgeTreatment edges = BlurredEdgeTreatment::Rectangle;
    void draw(GraphicsContext& g, const Rect& bounds, const LayoutNode&, const std::function<void()>& drawContent) const override {
        if (radiusX <= 0 && radiusY <= 0) return drawContent();
        // Unbounded: room for the blur to spread into (about three times its radius).
        Rect area = bounds;
        if (edges == BlurredEdgeTreatment::Unbounded)
            area = {bounds.x - 3 * radiusX, bounds.y - 3 * radiusY, bounds.width + 6 * radiusX, bounds.height + 6 * radiusY};
        if (g.quickReject(area)) return;
        g.pushBlurLayer(area, radiusX, radiusY);
        drawContent();
        g.popLayer();
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const BlurElement*>(&other);
        return o && o->radiusX == radiusX && o->radiusY == radiusY && o->edges == edges;
    }
};
} // namespace

Modifier Modifier::blur(float radiusX, float radiusY, BlurredEdgeTreatment edgeTreatment) const {
    auto e = std::make_shared<BlurElement>();
    e->radiusX = std::max(0.f, radiusX);
    e->radiusY = std::max(0.f, radiusY);
    e->edges = edgeTreatment;
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
    LayerTransform t{p.scaleX, p.scaleY, p.rotationZ, p.translationX, p.translationY, p.pivot};
    bool layered = p.alpha < 1 || !t.identity();  // through an offscreen layer of its size
    if (layered) context_->pushLayer(at, CornerRadii(), p.alpha, t);
    context_->pushTranslation({at.x, at.y});
    d.list->replay(*context_);
    context_->popTranslation();
    if (layered) context_->popLayer();
}

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
    switch (d.kind) {
    case Kind::Bitmap:
        if (!d.bitmap.empty())
            canvas.drawImage(colorFilter ? colorFilter->apply(d.bitmap) : d.bitmap, {0, 0, float(d.bitmap.width()), float(d.bitmap.height())}, b,
                             alpha);
        break;
    case Kind::Vector: {  // in its own paints (a one-color one black, as Compose's icons are), unless a filter tints it
        std::optional<Color> tint;
        if (colorFilter) tint = colorFilter->apply(colors::Black);
        if (!d.vector.empty()) detail::drawImageVector(canvas, d.vector.autoMirror() && rtl ? d.vector.mirrored() : d.vector, b, tint, alpha);
        break;
    }
    case Kind::Fill: {
        Color c = colorFilter ? colorFilter->apply(d.color) : d.color;
        canvas.fillRoundRect(b, CornerRadii(), c.copy(c.a * alpha));
        break;
    }
    case Kind::Gradient:
    case Kind::Custom: {
        bool layer = alpha < 1;
        if (layer) canvas.pushLayer(b, CornerRadii(), alpha);
        DrawScope scope(canvas, b);
        if (d.kind == Kind::Gradient) scope.drawRect(colorFilter ? colorFilter->apply(*d.brush) : *d.brush);
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
