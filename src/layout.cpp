#include <kompoz/animation.hpp>
#include <kompoz/coroutines.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/host.hpp>
#include <kompoz/layout.hpp>
#include <kompoz/strings.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <utility>

namespace kompoz {

void Arrangement::arrange(float total, const std::vector<float>& sizes, std::vector<float>& positions) const {
    size_t n = sizes.size();
    positions.resize(n);
    if (n == 0) return;
    float space = detail::toWholePx(spacing);  // (in whole px, as Compose's spacedBy)
    float used = std::accumulate(sizes.begin(), sizes.end(), 0.f) + space * float(n - 1);
    float free = std::max(0.f, total - used);
    float start = 0, gap = space;
    switch (kind) {
    case Kind::Start: break;
    case Kind::End: start = free; break;
    case Kind::Center: start = free / 2; break;
    case Kind::SpaceBetween:
        if (n > 1) gap += free / float(n - 1);
        break;
    case Kind::SpaceAround:
        start = free / float(n) / 2;
        gap += free / float(n);
        break;
    case Kind::SpaceEvenly:
        start = free / float(n + 1);
        gap += start;
        break;
    }
    float pos = start;
    for (size_t i = 0; i < n; ++i) {
        positions[i] = pos;
        pos += sizes[i] + gap;
    }
}

namespace {
thread_local int gProbeDepth = 0;
constexpr float kOverscrollStretch = 0.35f;  // how much longer content gets, fully pulled
} // namespace

detail::ProbeMeasure::ProbeMeasure() { ++gProbeDepth; }
detail::ProbeMeasure::~ProbeMeasure() { --gProbeDepth; }
bool detail::isProbeMeasure() { return gProbeDepth > 0; }
namespace {
thread_local int gMeasureEverything = 0;  // MeasureEverything
thread_local int gMinIntrinsicDepth = 0;
}
detail::MinIntrinsicWidth::MinIntrinsicWidth() { ++gMinIntrinsicDepth; }
detail::MinIntrinsicWidth::~MinIntrinsicWidth() { --gMinIntrinsicDepth; }
bool detail::isMinIntrinsicWidth() { return gMinIntrinsicDepth > 0; }

namespace {

bool sameFloat(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }

template <class T>
const T* as(const ModifierElement& e) {
    return dynamic_cast<const T*>(&e);
}

struct PaddingElement final : ModifierElement {
    PaddingValues p;
    bool absolute = false;  // start/end are left/right
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        using detail::toWholePx;  // each side in whole px, as Compose's
        float start = toWholePx(p.start), top = toWholePx(p.top), h = start + toWholePx(p.end), v = top + toWholePx(p.bottom);
        Size s = next(c.deflate(h, v));
        contentOffset = {start, top};
        return c.constrain({s.width + h, s.height + v});
    }
    bool mirrorsInRtl() const override { return !absolute; }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<PaddingElement>(other);
        return o && o->p == p && o->absolute == absolute;
    }
};

// size(), width(), heightIn(), ... Unspecified bounds are NaN and keep the incoming constraint.
struct SizeElement final : ModifierElement {
    float minWidth = NAN, maxWidth = NAN, minHeight = NAN, maxHeight = NAN;
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        Constraints t = c;
        if (!std::isnan(minWidth)) t.minWidth = std::clamp(minWidth, c.minWidth, c.maxWidth);
        if (!std::isnan(maxWidth)) t.maxWidth = std::clamp(maxWidth, c.minWidth, c.maxWidth);
        if (!std::isnan(minHeight)) t.minHeight = std::clamp(minHeight, c.minHeight, c.maxHeight);
        if (!std::isnan(maxHeight)) t.maxHeight = std::clamp(maxHeight, c.minHeight, c.maxHeight);
        t.minWidth = std::min(t.minWidth, t.maxWidth);
        t.minHeight = std::min(t.minHeight, t.maxHeight);
        return t.constrain(next(t));
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<SizeElement>(other);
        return o && sameFloat(o->minWidth, minWidth) && sameFloat(o->maxWidth, maxWidth) &&
               sameFloat(o->minHeight, minHeight) && sameFloat(o->maxHeight, maxHeight);
    }
};

struct DefaultMinSizeElement final : ModifierElement {
    float minWidth = 0, minHeight = 0;
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        Constraints t = c;
        if (c.minWidth == 0) t.minWidth = std::min(minWidth, c.maxWidth);
        if (c.minHeight == 0) t.minHeight = std::min(minHeight, c.maxHeight);
        return t.constrain(next(t));
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<DefaultMinSizeElement>(other);
        return o && o->minWidth == minWidth && o->minHeight == minHeight;
    }
};

struct FillElement final : ModifierElement {
    float widthFraction = NAN, heightFraction = NAN;
    bool lazyParent = false;  // fillParentMax...: of the lazy list measuring it, when there is one
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        Constraints t = c;
        Size parent = lazyParent ? detail::lazyParentSize() : Size{NAN, NAN};
        float maxW = !std::isnan(parent.width) ? parent.width : c.hasBoundedWidth() ? c.maxWidth : NAN;
        float maxH = !std::isnan(parent.height) ? parent.height : c.hasBoundedHeight() ? c.maxHeight : NAN;
        if (!std::isnan(widthFraction) && !std::isnan(maxW))
            t.minWidth = t.maxWidth = std::clamp(maxW * widthFraction, c.minWidth, std::max(c.minWidth, c.maxWidth));
        if (!std::isnan(heightFraction) && !std::isnan(maxH))
            t.minHeight = t.maxHeight = std::clamp(maxH * heightFraction, c.minHeight, std::max(c.minHeight, c.maxHeight));
        return t.constrain(next(t));
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<FillElement>(other);
        return o && sameFloat(o->widthFraction, widthFraction) && sameFloat(o->heightFraction, heightFraction) && o->lazyParent == lazyParent;
    }
};

thread_local Size gLazyParent{NAN, NAN};

struct OffsetElement final : ModifierElement {
    Offset offset;
    std::function<Offset()> provider;  // offset { ... }: read during layout
    bool absolute = false;
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = provider ? provider() : offset;
        return next(c);
    }
    bool mirrorsInRtl() const override { return !absolute; }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<OffsetElement>(other);
        return o && !provider && !o->provider && o->offset.x == offset.x && o->offset.y == offset.y && o->absolute == absolute;
    }
};

// Tries the largest size of the right ratio that satisfies the constraints, like Compose's
// AspectRatioModifier (width first).
struct AspectRatioElement final : ModifierElement {
    float ratio = 1;
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        auto fits = [&](Size s) {
            return s.width >= c.minWidth && s.width <= c.maxWidth && s.height >= c.minHeight && s.height <= c.maxHeight;
        };
        auto byWidth = [&](float w) { return Size{w, w / ratio}; };
        auto byHeight = [&](float h) { return Size{h * ratio, h}; };
        std::optional<Size> size;
        for (bool enforce : {true, false}) {
            Size candidates[4] = {byWidth(c.maxWidth), byHeight(c.maxHeight), byWidth(c.minWidth), byHeight(c.minHeight)};
            bool usable[4] = {c.hasBoundedWidth(), c.hasBoundedHeight(), c.minWidth > 0, c.minHeight > 0};
            for (int i = 0; i < 4 && !size; ++i)
                if (usable[i] && std::isfinite(candidates[i].width) && std::isfinite(candidates[i].height) &&
                    (!enforce || fits(candidates[i])))
                    size = c.constrain(candidates[i]);
            if (size) break;
        }
        if (!size) return next(c);  // nothing bounded: let the content decide
        next(Constraints::fixed(size->width, size->height));
        return *size;
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<AspectRatioElement>(other);
        return o && o->ratio == ratio;
    }
};

// wrapContentSize(): drops the minimum constraints of the chosen axes and aligns the content.
struct WrapContentElement final : ModifierElement {
    bool horizontal = true, vertical = true;
    Alignment align = Alignment::Center;
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        Constraints inner = c;
        if (horizontal) inner.minWidth = 0;
        if (vertical) inner.minHeight = 0;
        Size content = next(inner);
        Size own = c.constrain(content);
        contentOffset = align.align(content, own);
        return own;
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<WrapContentElement>(other);
        return o && o->horizontal == horizontal && o->vertical == vertical && o->align == align;
    }
};

// requiredSize(): the content gets exactly this size whatever the constraints; the layout
// reports a size within them and centers the content (it may overflow).
struct RequiredSizeElement final : ModifierElement {
    float width = NAN, height = NAN;
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        Constraints inner = c;
        if (!std::isnan(width)) inner.minWidth = inner.maxWidth = width;
        if (!std::isnan(height)) inner.minHeight = inner.maxHeight = height;
        Size content = next(inner);
        Size own = c.constrain(content);
        contentOffset = {(own.width - content.width) / 2, (own.height - content.height) / 2};
        return own;
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<RequiredSizeElement>(other);
        return o && sameFloat(o->width, width) && sameFloat(o->height, height);
    }
};

struct RequiredSizeInElement final : ModifierElement {
    float minWidth = NAN, maxWidth = NAN, minHeight = NAN, maxHeight = NAN;  // NAN: an axis it leaves alone
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        auto range = [](float min, float max, float inMin, float inMax) {
            if (std::isnan(min)) return std::pair{inMin, inMax};
            float hi = std::isinf(max) ? kInfinity : std::max(0.f, max);
            float lo = std::clamp(min, 0.f, hi);
            return std::pair{lo, std::isinf(hi) ? std::max(inMax, lo) : hi};  // no max: the incoming one, at least its min
        };
        auto [minW, maxW] = range(minWidth, maxWidth, c.minWidth, c.maxWidth);
        auto [minH, maxH] = range(minHeight, maxHeight, c.minHeight, c.maxHeight);
        Size content = next({minW, maxW, minH, maxH});
        Size own = c.constrain(content);
        contentOffset = {(own.width - content.width) / 2, (own.height - content.height) / 2};
        return own;
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<RequiredSizeInElement>(other);
        return o && sameFloat(o->minWidth, minWidth) && sameFloat(o->maxWidth, maxWidth) && sameFloat(o->minHeight, minHeight) &&
               sameFloat(o->maxHeight, maxHeight);
    }
};

struct BackgroundElement final : ModifierElement {
    Color color;
    std::optional<Brush> brush;
    Shape shape;
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode& node, const std::function<void()>& drawContent) const override {
        if (brush)
            canvas.fillShape(bounds, shape, node.rtl, *brush, bounds);
        else
            canvas.fillShape(bounds, shape, node.rtl, color);
        drawContent();
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<BackgroundElement>(other);
        return o && o->color == color && o->brush == brush && o->shape == shape;
    }
};

struct BorderElement final : ModifierElement {
    float width = 1;
    Color color;
    std::optional<Brush> brush;  // a BorderStroke's gradient
    Shape shape;
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode& node, const std::function<void()>& drawContent) const override {
        drawContent();
        if (brush) canvas.strokeShape(bounds, shape, node.rtl, width, *brush, bounds);  // painted by the brush over the bounds
        else canvas.strokeShape(bounds, shape, node.rtl, width, color);
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<BorderElement>(other);
        return o && o->width == width && o->color == color && o->brush == brush && o->shape == shape;
    }
};

// clip(shape): content is drawn inside the shape's outline, through the context's clip (no layer).
struct ShapeClipElement final : ModifierElement {
    Shape shape;
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode& node, const std::function<void()>& drawContent) const override {
        canvas.pushClipShape(bounds, shape, node.rtl);
        drawContent();
        canvas.popClip();
    }
    bool clipsContent() const override { return true; }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<ShapeClipElement>(other);
        return o && o->shape == shape;
    }
};

struct AlphaElement final : ModifierElement {
    float alpha = 1;
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode&, const std::function<void()>& drawContent) const override {
        if (alpha >= 1) return drawContent();
        if (alpha <= 0 || canvas.quickReject(bounds)) return;  // the layer shows nothing outside its bounds
        canvas.pushLayer(bounds, 0, alpha);
        drawContent();
        canvas.popLayer();
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<AlphaElement>(other);
        return o && o->alpha == alpha;
    }
};

struct ShapeShadowElement final : ModifierElement {
    Shape shape;
    Shadow shadow;
    bool inner = false;
    void draw(GraphicsContext& canvas, const Rect& b, const LayoutNode& node, const std::function<void()>& drawContent) const override {
        canvas.drawShapeShadow(b, shape, node.rtl, shadow, inner);
        drawContent();
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<ShapeShadowElement>(other);
        return o && o->shape == shape && o->shadow == shadow && o->inner == inner;
    }
};

struct ClipElement final : ModifierElement {
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode&, const std::function<void()>& drawContent) const override {
        canvas.pushClip(bounds);
        drawContent();
        canvas.popClip();
    }
    bool clipsContent() const override { return true; }
    bool equals(const ModifierElement& other) const override { return as<ClipElement>(other) != nullptr; }
};

struct IntrinsicElement final : ModifierElement {
    bool vertical = true;
    bool minimum = false;  // IntrinsicSize::Min across: texts as wide as their widest word
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        if (vertical) {
            float natural;
            {
                detail::ProbeMeasure probe;
                natural = next({c.minWidth, c.maxWidth, 0, kInfinity}).height;
            }
            float h = std::clamp(natural, c.minHeight, c.maxHeight);
            return c.constrain(next({c.minWidth, c.maxWidth, h, h}));
        }
        float natural;
        {
            detail::ProbeMeasure probe;
            std::optional<detail::MinIntrinsicWidth> narrowest;
            if (minimum) narrowest.emplace();
            natural = next({0, kInfinity, c.minHeight, c.maxHeight}).width;
        }
        float w = std::clamp(natural, c.minWidth, c.maxWidth);
        return c.constrain(next({w, w, c.minHeight, c.maxHeight}));
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<IntrinsicElement>(other);
        return o && o->vertical == vertical;
    }
};

struct LayoutElement final : ModifierElement {
    std::function<Size(const MeasureNext&, const Constraints&, Offset&)> fn;
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        return fn(next, c, contentOffset);
    }
};

struct OnSizeChangedElement final : ModifierElement {
    std::function<void(Size)> callback;
    mutable std::optional<Size> last;
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        Size s = next(c);
        if (!detail::isProbeMeasure() && (!last || !(*last == s))) {
            last = s;
            if (callback) callback(s);
        }
        return s;
    }
};

struct RecomposeHighlighterElement final : ModifierElement {
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode& node,
              const std::function<void()>& drawContent) const override {
        drawContent();
        uint32_t n = node.compositions;
        if (n == 0 || bounds.width <= 0 || bounds.height <= 0) return;
        Color color = n == 1 ? Color::hex(0x0000FF) : n == 2 ? Color::hex(0x00FF00)
                             : lerp(Color::hex(0xFFFF00, 0.8f), Color::hex(0xFF0000, 0.5f),
                                    std::min(1.f, float(n - 1) / 100.f));
        float width = n == 1 ? 0.5f : std::min({float(n), 6.f, std::min(bounds.width, bounds.height) / 2});
        canvas.strokeRoundRect(bounds, 0, width, color);
    }
    // Always equal: observing recompositions must not cause any.
    bool equals(const ModifierElement& other) const override {
        return as<RecomposeHighlighterElement>(other) != nullptr;
    }
};

template <class T, class F>
Modifier with(const Modifier& m, F&& configure) {
    auto e = std::make_shared<T>();
    configure(*e);
    return m.then(std::move(e));
}

} // namespace

namespace {
// An unbounded indication with a radius shows hover and focus in a circle that wide around the
// middle, like its ripple (the others over the shape).
std::optional<Rect> stateCircle(const Indication& indication, const Rect& bounds) {
    if (indication.bounded || indication.radius <= 0) return std::nullopt;
    Offset o = bounds.center();
    float r = indication.radius;
    return Rect{o.x - r, o.y - r, 2 * r, 2 * r};
}

// Material's state layer at `alpha` (hover, focus, a press without a ripple), then the ripples,
// clipped to the shape when bounded.
void drawStateLayer(GraphicsContext& canvas, const Rect& bounds, const LayoutNode& node, const Indication& indication,
                    const Shape& shape, Color color, float alpha, const std::vector<ComposeHost::RippleDraw>& ripples) {
    std::optional<Rect> circle = stateCircle(indication, bounds);
    if (alpha > 0 && circle) canvas.fillRoundRect(*circle, circle->width / 2, color.copy(alpha));
    else if (alpha > 0) canvas.fillShape(bounds, shape, node.rtl, color.copy(alpha));
    if (ripples.empty()) return;
    bool clip = indication.bounded;
    if (clip) canvas.pushClipShape(bounds, shape, node.rtl);
    for (const auto& r : ripples)
        canvas.fillRoundRect({r.center.x - r.radius, r.center.y - r.radius, 2 * r.radius, 2 * r.radius}, r.radius, color.copy(r.alpha));
    if (clip) canvas.popClip();
}
} // namespace

void ClickableElement::draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode& node,
                            const std::function<void()>& drawContent) const {
    if (indication.kind == Indication::Kind::Custom) {  // its own node draws around the content
        if (!enabled || !node.indicationNode) return drawContent();
        ContentDrawScope scope(canvas, bounds, node, drawContent);
        node.indicationNode->draw(scope);
        return;
    }
    drawContent();
    if (!enabled || indication.kind == Indication::Kind::None) return;
    // Material's state layer for hover and focus (and, without a ripple, the press). Keyboard
    // focus shows only so, as in Compose's Material 3 (its focus indicator tokens are unused).
    bool flat = indication.kind == Indication::Kind::StateLayer;
    float alpha = flat && node.pressed ? 0.16f : node.focused ? 0.1f : node.hovered ? 0.08f : 0.f;
    std::vector<ComposeHost::RippleDraw> ripples;
    if (!flat && node.host()) ripples = node.host()->ripples(node, bounds, indication);
    drawStateLayer(canvas, bounds, node, indication, shape, indication.color.value_or(indicationColor), alpha, ripples);
}

Size ScrollElement::measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const {
    if (axis == Orientation::Vertical) {
        Size content = next({c.minWidth, c.maxWidth, 0, kInfinity});
        Size own = c.constrain({content.width, std::min(content.height, c.maxHeight)});
        state.setViewportSize(own.height);
        state.setMaxValue(content.height - own.height);
        contentOffset = {0, -(reverse ? state.maxValue() - state.value() : state.value())};
        return own;
    }
    Size content = next({0, kInfinity, c.minHeight, c.maxHeight});
    Size own = c.constrain({std::min(content.width, c.maxWidth), content.height});
    state.setViewportSize(own.width);
    state.setMaxValue(content.width - own.width);
    contentOffset = {-(reverse ? state.maxValue() - state.value() : state.value()), 0};
    return own;
}

void ScrollElement::draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode&,
                         const std::function<void()>& drawContent) const {
    canvas.pushClip(clipBounds(bounds));  // like Compose, no scrollbar by default
    drawContent();
    canvas.popClip();
}

void ScrollableModifierElement::draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode&,
                                     const std::function<void()>& drawContent) const {
    if (!area) return drawContent();  // a bare scrollable() clips nothing
    canvas.pushClip(clipBounds(bounds));
    drawContent();
    canvas.popClip();
}

Modifier Modifier::padding(float all) const { return padding({all, all, all, all}); }
Modifier Modifier::padding(float horizontal, float vertical) const {
    return padding({horizontal, vertical, horizontal, vertical});
}
Modifier Modifier::padding(float start, float top, float end, float bottom) const {
    return padding(PaddingValues{start, top, end, bottom});
}
Modifier Modifier::padding(PaddingValues values) const {
    return with<PaddingElement>(*this, [&](auto& e) { e.p = values; });
}
Modifier Modifier::absolutePadding(float left, float top, float right, float bottom) const {
    return with<PaddingElement>(*this, [&](auto& e) {
        e.p = {left, top, right, bottom};
        e.absolute = true;
    });
}
namespace {

thread_local LayoutNode* gMeasuring = nullptr;  // the node whose modifiers are measuring
thread_local float gLayoutDensity = 1;           // the px a dp of the host measuring now

// windowInsetsPadding() / consumeWindowInsets(): pads by the insets the enclosing ones haven't
// consumed yet, and consumes all of `insets` for what is inside.
// The insets taken around `self` (an element of the node being measured): by the node's
// elements before it, then by its ancestors' (up to a popup's root: it has a window of its own).
WindowInsets consumedInsetsAbove(const ModifierElement* self);

struct InsetsElement final : ModifierElement {
    WindowInsets insets;
    bool pad = true;
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        WindowInsets p = pad ? insets.exclude(consumedInsetsAbove(this)) : WindowInsets{};
        float h = p.left + p.right, v = p.top + p.bottom;
        Size s = next(c.deflate(h, v));
        contentOffset = {p.left, p.top};
        return c.constrain({s.width + h, s.height + v});
    }
    bool mirrorsInRtl() const override { return false; }  // the screen's left and right
    bool equals(const ModifierElement& other) const override {
        auto* o = as<InsetsElement>(other);
        return o && o->insets == insets && o->pad == pad;
    }
};

WindowInsets consumedInsetsAbove(const ModifierElement* self) {
    WindowInsets consumed;
    const LayoutNode* node = gMeasuring;
    if (!node) return consumed;
    auto add = [&](const ModifierElement* e) {
        if (auto* i = dynamic_cast<const InsetsElement*>(e)) consumed = consumed.unionWith(i->insets);
    };
    for (const auto& e : node->modifier.elements()) {
        if (e.get() == self) break;
        add(e.get());
    }
    for (const LayoutNode* n = node->overlay ? nullptr : node->parent; n; n = n->overlay ? nullptr : n->parent)
        for (const auto& e : n->modifier.elements()) add(e.get());
    return consumed;
}

struct OnConsumedInsetsElement final : ModifierElement {
    std::function<void(const WindowInsets&)> callback;
    mutable std::optional<WindowInsets> last;
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        WindowInsets consumed = consumedInsetsAbove(this);
        if (!detail::isProbeMeasure() && (!last || !(*last == consumed))) {
            last = consumed;
            if (callback) callback(consumed);
        }
        return next(c);
    }
};

// windowInsetsTopHeight & co: one side's inset (what is left of it) as the height or width.
struct InsetsSizeElement final : ModifierElement {
    WindowInsets insets;
    WindowInsetsSides side = WindowInsetsSides::Top;  // Top, Bottom, or Start / End (Left / Right)
    bool end = false;                                 // for the width: the end (mirrored right to left)
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        WindowInsets left = insets.exclude(consumedInsetsAbove(this));
        if (side == WindowInsetsSides::Top || side == WindowInsetsSides::Bottom) {
            float h = std::clamp(side == WindowInsetsSides::Top ? left.top : left.bottom, c.minHeight, c.maxHeight);
            return next({c.minWidth, c.maxWidth, h, h});
        }
        bool rtl = gMeasuring && gMeasuring->rtl;
        float w = std::clamp(end != rtl ? left.right : left.left, c.minWidth, c.maxWidth);
        return next({w, w, c.minHeight, c.maxHeight});
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<InsetsSizeElement>(other);
        return o && o->insets == insets && o->side == side && o->end == end;
    }
};

WindowInsets consumedBy(const LayoutNode& node) {
    WindowInsets consumed;
    for (const LayoutNode* n = &node; n; n = n->overlay ? nullptr : n->parent)
        for (const auto& e : n->modifier.elements())
            if (auto* i = dynamic_cast<const InsetsElement*>(e.get())) consumed = consumed.unionWith(i->insets);
    return consumed;
}

PaddingValues sideMax(PaddingValues a, PaddingValues b) {
    return {std::max(a.start, b.start), std::max(a.top, b.top), std::max(a.end, b.end), std::max(a.bottom, b.bottom)};
}

} // namespace

WindowInsets detail::unconsumedInsets(const LayoutNode& node, const WindowInsets& insets) { return insets.exclude(consumedBy(node)); }

WindowInsets WindowInsets::statusBars() { return of(LocalSystemInsets.current().statusBars); }
WindowInsets WindowInsets::navigationBars() { return of(LocalSystemInsets.current().navigationBars); }
WindowInsets WindowInsets::systemBars() {
    const SystemInsets& i = LocalSystemInsets.current();
    return of(sideMax(sideMax(i.statusBars, i.navigationBars), i.captionBar));
}
WindowInsets WindowInsets::captionBar() { return of(LocalSystemInsets.current().captionBar); }
WindowInsets WindowInsets::mandatorySystemGestures() { return of(LocalSystemInsets.current().mandatorySystemGestures); }
WindowInsets WindowInsets::tappableElement() { return of(LocalSystemInsets.current().tappableElement); }
WindowInsets WindowInsets::waterfall() { return of(LocalSystemInsets.current().waterfall); }
WindowInsets WindowInsets::safeGestures() {
    const SystemInsets& i = LocalSystemInsets.current();
    return of(sideMax(sideMax(i.systemGestures, i.mandatorySystemGestures), sideMax(i.tappableElement, i.waterfall)));
}
WindowInsets WindowInsets::ime() { return of(LocalSystemInsets.current().ime); }
WindowInsets WindowInsets::imeAnimationSource() {
    const SystemInsets& i = LocalSystemInsets.current();
    return of(i.imeAnimating ? i.imeAnimationSource : i.ime);
}
WindowInsets WindowInsets::imeAnimationTarget() {
    const SystemInsets& i = LocalSystemInsets.current();
    return of(i.imeAnimating ? i.imeAnimationTarget : i.ime);
}
bool WindowInsets::isImeVisible() { return LocalSystemInsets.current().imeVisible; }
WindowInsets WindowInsets::displayCutout() { return of(LocalSystemInsets.current().displayCutout); }
WindowInsets detail::systemBarsForVisualComponents() { return WindowInsets::systemBars().unionWith(WindowInsets::displayCutout()); }
WindowInsets WindowInsets::systemGestures() { return of(LocalSystemInsets.current().systemGestures); }
WindowInsets WindowInsets::safeDrawing() {
    const SystemInsets& i = LocalSystemInsets.current();
    return systemBars().unionWith(of(sideMax(i.ime, i.displayCutout)));
}
WindowInsets WindowInsets::safeContent() { return safeDrawing().unionWith(safeGestures()); }

WindowInsets WindowInsets::unionWith(const WindowInsets& o) const {
    return {std::max(left, o.left), std::max(top, o.top), std::max(right, o.right), std::max(bottom, o.bottom)};
}
WindowInsets WindowInsets::exclude(const WindowInsets& o) const {
    return {std::max(0.f, left - o.left), std::max(0.f, top - o.top), std::max(0.f, right - o.right),
            std::max(0.f, bottom - o.bottom)};
}
WindowInsets WindowInsets::add(const WindowInsets& o) const {
    return {left + o.left, top + o.top, right + o.right, bottom + o.bottom};
}
WindowInsets WindowInsets::only(WindowInsetsSides sides) const {
    auto has = [&](WindowInsetsSides s) { return (uint8_t(sides) & uint8_t(s)) != 0; };
    return {has(WindowInsetsSides::Left) ? left : 0, has(WindowInsetsSides::Top) ? top : 0,
            has(WindowInsetsSides::Right) ? right : 0, has(WindowInsetsSides::Bottom) ? bottom : 0};
}

Modifier Modifier::windowInsetsPadding(const WindowInsets& insets) const {
    return with<InsetsElement>(*this, [&](auto& e) { e.insets = insets; });
}
Modifier Modifier::onLayoutRectChanged(std::function<void(const RelativeLayoutBounds&)> callback, int64_t throttleMillis,
                                       int64_t debounceMillis) const {
    return with<LayoutRectElement>(*this, [&](auto& e) {
        e.callback = std::move(callback);
        e.throttleMillis = throttleMillis;
        e.debounceMillis = debounceMillis;
    });
}
Modifier Modifier::onVisibilityChanged(std::function<void(bool)> callback, VisibilityOptions options) const {
    return with<VisibilityElement>(*this, [&](auto& e) {
        e.onChanged = std::move(callback);
        e.options = std::move(options);
    });
}
Modifier Modifier::onFirstVisible(std::function<void()> callback, VisibilityOptions options) const {
    return with<VisibilityElement>(*this, [&](auto& e) {
        e.onFirst = std::move(callback);
        e.options = std::move(options);
    });
}
Modifier Modifier::layoutBounds(const LayoutBoundsHolder& holder) const {
    return with<LayoutBoundsElement>(*this, [&](auto& e) { e.holder = holder; });
}
Modifier Modifier::preferredFrameRate(float frameRate) const {
    return with<FrameRateElement>(*this, [&](auto& e) { e.rate = frameRate; });
}
Modifier Modifier::preferredFrameRate(FrameRateCategory category) const {
    return with<FrameRateElement>(*this, [&](auto& e) { e.category = category; });
}
Modifier Modifier::sensitiveContent(bool isContentSensitive) const {
    return with<SensitiveContentElement>(*this, [&](auto& e) { e.sensitive = isContentSensitive; });
}
Modifier Modifier::clipScrollableContainer(Orientation orientation) const {
    return with<ScrollableContainerClipElement>(*this, [&](auto& e) { e.axis = orientation; });
}
Modifier Modifier::keepScreenOn() const {
    return with<KeepScreenOnElement>(*this, [](auto&) {});
}
Modifier Modifier::systemGestureExclusion() const {
    return with<SystemGestureExclusionElement>(*this, [](auto&) {});
}
Modifier Modifier::systemGestureExclusion(std::function<Rect(const LayoutCoordinates&)> exclusion) const {
    return with<SystemGestureExclusionElement>(*this, [&](auto& e) { e.exclusion = std::move(exclusion); });
}
Modifier Modifier::windowInsetsTopHeight(const WindowInsets& insets) const {
    return with<InsetsSizeElement>(*this, [&](auto& e) { e.insets = insets, e.side = WindowInsetsSides::Top; });
}
Modifier Modifier::windowInsetsBottomHeight(const WindowInsets& insets) const {
    return with<InsetsSizeElement>(*this, [&](auto& e) { e.insets = insets, e.side = WindowInsetsSides::Bottom; });
}
Modifier Modifier::windowInsetsStartWidth(const WindowInsets& insets) const {
    return with<InsetsSizeElement>(*this, [&](auto& e) { e.insets = insets, e.side = WindowInsetsSides::Horizontal; });
}
Modifier Modifier::windowInsetsEndWidth(const WindowInsets& insets) const {
    return with<InsetsSizeElement>(*this, [&](auto& e) { e.insets = insets, e.side = WindowInsetsSides::Horizontal, e.end = true; });
}
Modifier Modifier::safeDrawingPadding() const { return windowInsetsPadding(WindowInsets::safeDrawing()); }
Modifier Modifier::safeContentPadding() const { return windowInsetsPadding(WindowInsets::safeContent()); }
Modifier Modifier::systemBarsPadding() const { return windowInsetsPadding(WindowInsets::systemBars()); }
Modifier Modifier::statusBarsPadding() const { return windowInsetsPadding(WindowInsets::statusBars()); }
Modifier Modifier::navigationBarsPadding() const { return windowInsetsPadding(WindowInsets::navigationBars()); }
Modifier Modifier::imePadding() const { return windowInsetsPadding(WindowInsets::ime()); }
Modifier Modifier::displayCutoutPadding() const { return windowInsetsPadding(WindowInsets::displayCutout()); }
Modifier Modifier::captionBarPadding() const { return windowInsetsPadding(WindowInsets::captionBar()); }
Modifier Modifier::waterfallPadding() const { return windowInsetsPadding(WindowInsets::waterfall()); }
Modifier Modifier::systemGesturesPadding() const { return windowInsetsPadding(WindowInsets::systemGestures()); }
Modifier Modifier::mandatorySystemGesturesPadding() const { return windowInsetsPadding(WindowInsets::mandatorySystemGestures()); }
Modifier Modifier::safeGesturesPadding() const { return windowInsetsPadding(WindowInsets::safeGestures()); }
Modifier Modifier::onConsumedWindowInsetsChanged(std::function<void(const WindowInsets&)> block) const {
    return with<OnConsumedInsetsElement>(*this, [&](auto& e) { e.callback = std::move(block); });
}
Modifier Modifier::consumeWindowInsets(const WindowInsets& insets) const {
    return with<InsetsElement>(*this, [&](auto& e) {
        e.insets = insets;
        e.pad = false;
    });
}
Modifier Modifier::consumeWindowInsets(PaddingValues padding) const {
    return consumeWindowInsets(WindowInsets::of(padding));
}
Modifier Modifier::size(float s) const { return size(s, s); }
Modifier Modifier::size(float w, float h) const {
    return with<SizeElement>(*this, [&](auto& e) {
        e.minWidth = e.maxWidth = w;
        e.minHeight = e.maxHeight = h;
    });
}
Modifier Modifier::width(float w) const {
    return with<SizeElement>(*this, [&](auto& e) { e.minWidth = e.maxWidth = w; });
}
Modifier Modifier::height(float h) const {
    return with<SizeElement>(*this, [&](auto& e) { e.minHeight = e.maxHeight = h; });
}
Modifier Modifier::widthIn(float min, float max) const {
    return with<SizeElement>(*this, [&](auto& e) {
        e.minWidth = min;
        e.maxWidth = max;
    });
}
Modifier Modifier::heightIn(float min, float max) const {
    return with<SizeElement>(*this, [&](auto& e) {
        e.minHeight = min;
        e.maxHeight = max;
    });
}
Modifier Modifier::sizeIn(float minWidth, float minHeight, float maxWidth, float maxHeight) const {
    return with<SizeElement>(*this, [&](auto& e) {
        e.minWidth = minWidth;
        e.maxWidth = maxWidth;
        e.minHeight = minHeight;
        e.maxHeight = maxHeight;
    });
}
Modifier Modifier::defaultMinSize(float minWidth, float minHeight) const {
    return with<DefaultMinSizeElement>(*this, [&](auto& e) {
        e.minWidth = minWidth;
        e.minHeight = minHeight;
    });
}
Modifier Modifier::fillMaxWidth(float fraction) const {
    return with<FillElement>(*this, [&](auto& e) { e.widthFraction = fraction; });
}
Modifier Modifier::fillMaxHeight(float fraction) const {
    return with<FillElement>(*this, [&](auto& e) { e.heightFraction = fraction; });
}
Modifier Modifier::fillMaxSize(float fraction) const {
    return with<FillElement>(*this, [&](auto& e) { e.widthFraction = e.heightFraction = fraction; });
}
Modifier Modifier::offset(float x, float y) const {
    return with<OffsetElement>(*this, [&](auto& e) { e.offset = {x, y}; });
}
Modifier Modifier::offset(std::function<Offset()> offset) const {
    return with<OffsetElement>(*this, [&](auto& e) { e.provider = std::move(offset); });
}
Modifier Modifier::absoluteOffset(float x, float y) const {
    return with<OffsetElement>(*this, [&](auto& e) {
        e.offset = {x, y};
        e.absolute = true;
    });
}
Modifier Modifier::absoluteOffset(std::function<Offset()> offset) const {
    return with<OffsetElement>(*this, [&](auto& e) {
        e.provider = std::move(offset);
        e.absolute = true;
    });
}
Modifier Modifier::onGloballyPositioned(std::function<void(const LayoutCoordinates&)> onGloballyPositioned) const {
    return with<PositionedElement>(*this, [&](auto& e) { e.onPositioned = std::move(onGloballyPositioned); });
}
Modifier Modifier::onPlaced(std::function<void(const LayoutCoordinates&)> onPlaced) const {
    return with<PositionedElement>(*this, [&](auto& e) {
        e.onPositioned = std::move(onPlaced);
        e.inParentOnly = true;
    });
}
Modifier Modifier::aspectRatio(float ratio) const {
    if (!(ratio > 0)) return *this;
    return with<AspectRatioElement>(*this, [&](auto& e) { e.ratio = ratio; });
}
Modifier Modifier::wrapContentSize(Alignment align) const {
    return with<WrapContentElement>(*this, [&](auto& e) { e.align = align; });
}
Modifier Modifier::wrapContentWidth(HorizontalAlignment align) const {
    return with<WrapContentElement>(*this, [&](auto& e) {
        e.vertical = false;
        e.align = {align.bias, -1};
    });
}
Modifier Modifier::wrapContentHeight(VerticalAlignment align) const {
    return with<WrapContentElement>(*this, [&](auto& e) {
        e.horizontal = false;
        e.align = {-1, align.bias};
    });
}
Modifier Modifier::requiredSize(float s) const { return requiredSize(s, s); }
Modifier Modifier::requiredSize(float w, float h) const {
    return with<RequiredSizeElement>(*this, [&](auto& e) {
        e.width = w;
        e.height = h;
    });
}
Modifier Modifier::requiredWidth(float w) const {
    return with<RequiredSizeElement>(*this, [&](auto& e) { e.width = w; });
}
Modifier Modifier::requiredHeight(float h) const {
    return with<RequiredSizeElement>(*this, [&](auto& e) { e.height = h; });
}
Modifier Modifier::requiredWidthIn(float min, float max) const {
    return with<RequiredSizeInElement>(*this, [&](auto& e) {
        e.minWidth = min;
        e.maxWidth = max;
    });
}
Modifier Modifier::requiredHeightIn(float min, float max) const {
    return with<RequiredSizeInElement>(*this, [&](auto& e) {
        e.minHeight = min;
        e.maxHeight = max;
    });
}
Modifier Modifier::requiredSizeIn(float minWidth, float minHeight, float maxWidth, float maxHeight) const {
    return with<RequiredSizeInElement>(*this, [&](auto& e) {
        e.minWidth = minWidth;
        e.maxWidth = maxWidth;
        e.minHeight = minHeight;
        e.maxHeight = maxHeight;
    });
}
Modifier Modifier::background(Color color, float cornerRadius) const {
    return background(color, cornerRadius > 0 ? RoundedCornerShape(cornerRadius) : RectangleShape);
}
Modifier Modifier::background(Color color, Shape shape) const {
    return with<BackgroundElement>(*this, [&](auto& e) {
        e.color = color;
        e.shape = shape;
    });
}
Modifier Modifier::background(const Brush& brush, Shape shape) const {
    if (brush.isSolid()) return background(brush.stops().front().color, shape);
    return with<BackgroundElement>(*this, [&](auto& e) {
        e.brush = brush;
        e.shape = shape;
    });
}
Modifier Modifier::border(float width, Color color, float cornerRadius) const {
    return border(width, color, cornerRadius > 0 ? RoundedCornerShape(cornerRadius) : RectangleShape);
}
Modifier Modifier::border(float width, Color color, Shape shape) const {
    return with<BorderElement>(*this, [&](auto& e) {
        e.width = width;
        e.color = color;
        e.shape = shape;
    });
}
Modifier Modifier::border(const BorderStroke& border, Shape shape) const {
    return with<BorderElement>(*this, [&](auto& e) {
        e.width = border.width;
        e.color = border.color;
        e.brush = border.brush;
        e.shape = shape;
    });
}
Modifier Modifier::clip(Shape shape) const {
    if (shape.kind == Shape::Kind::Rectangle) return clipToBounds();
    return with<ShapeClipElement>(*this, [&](auto& e) { e.shape = shape; });
}
Modifier Modifier::alpha(float alpha) const {
    return with<AlphaElement>(*this, [&](auto& e) { e.alpha = std::clamp(alpha, 0.f, 1.f); });
}
Modifier Modifier::shadow(float elevation, Shape shape, std::optional<bool> clip, Color ambientColor, Color spotColor) const {
    bool clipped = clip.value_or(elevation > 0);
    if (elevation <= 0 && !clipped) return *this;
    GraphicsLayerScope layer;
    layer.shadowElevation = std::max(0.f, elevation);
    layer.shape = shape;
    layer.clip = clipped;
    layer.ambientShadowColor = ambientColor, layer.spotShadowColor = spotColor;
    return graphicsLayer(layer);
}
Modifier Modifier::dropShadow(Shape shape, Shadow shadow) const {
    return with<ShapeShadowElement>(*this, [&](auto& e) {
        e.shape = shape;
        e.shadow = shadow;
    });
}
Modifier Modifier::innerShadow(Shape shape, Shadow shadow) const {
    return with<ShapeShadowElement>(*this, [&](auto& e) {
        e.shape = shape;
        e.shadow = shadow;
        e.inner = true;
    });
}
namespace {
struct MinimumInteractiveElement final : ModifierElement {
    float size = 48;
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        Size content = next(c);
        Size s{std::max(content.width, size), std::max(content.height, size)};
        s = c.constrain(s);
        contentOffset = {(s.width - content.width) / 2, (s.height - content.height) / 2};
        return s;
    }
    bool expandsTouchArea() const override { return true; }
    bool equals(const ModifierElement& other) const override {
        auto* o = as<MinimumInteractiveElement>(other);
        return o && o->size == size;
    }
};
} // namespace

Modifier Modifier::minimumInteractiveComponentSize() const {
    float size = LocalMinimumInteractiveComponentSize.current();
    if (size <= 0) return *this;
    return with<MinimumInteractiveElement>(*this, [&](auto& e) { e.size = size; });
}

Modifier Modifier::clipToBounds() const {
    return with<ClipElement>(*this, [](auto&) {});
}
Modifier Modifier::clickable(std::function<void()> onClick, bool enabled) const {
    return clickable(std::move(onClick), ClickableOptions{.enabled = enabled});
}
Modifier Modifier::combinedClickable(std::function<void()> onClick, std::function<void()> onLongClick,
                                     std::function<void()> onDoubleClick, bool enabled) const {
    return clickable(std::move(onClick), {.enabled = enabled, .onLongClick = std::move(onLongClick), .onDoubleClick = std::move(onDoubleClick)});
}
Modifier Modifier::clickable(std::function<void()> onClick, const ClickableOptions& options) const {
    Shape shape;
    for (auto it = elements_.rbegin(); it != elements_.rend(); ++it) {
        if (auto* bg = dynamic_cast<const BackgroundElement*>(it->get())) { shape = bg->shape; break; }
        if (auto* border = dynamic_cast<const BorderElement*>(it->get())) { shape = border->shape; break; }
        if (auto* clip = dynamic_cast<const ShapeClipElement*>(it->get())) { shape = clip->shape; break; }
    }
    return with<ClickableElement>(*this, [&](auto& e) {
        e.onClick = std::move(onClick);
        e.onLongClick = options.onLongClick;
        e.onDoubleClick = options.onDoubleClick;
        e.enabled = options.enabled;
        e.shape = shape;
        e.indicationColor = LocalContentColor.current();
        e.indication = options.indication.value_or(LocalIndication.current());
        e.interactionSource = options.interactionSource;
    });
}
bool detail::overscrollWanted() { return !Composer::current() || LocalOverscrollFactory.current().has_value(); }
namespace {
thread_local detail::LayoutPass gLayoutPass = detail::LayoutPass::Plain;

// skipToLookaheadSize(): its content at the constraints it had in the lookahead pass.
struct SkipToLookaheadSizeElement final : ModifierElement {
    mutable std::optional<Constraints> lookahead;
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        switch (detail::currentLayoutPass()) {
        case detail::LayoutPass::Lookahead:
            lookahead = c;
            return next(c);
        case detail::LayoutPass::Approach:
            if (lookahead) return c.constrain(next(*lookahead));  // laid out where it goes, sized along the way
            return next(c);
        case detail::LayoutPass::Plain: break;
        }
        return next(c);
    }
    bool equals(const ModifierElement& other) const override { return dynamic_cast<const SkipToLookaheadSizeElement*>(&other) != nullptr; }
};
} // namespace

detail::InLayoutPass::InLayoutPass(LayoutPass pass) : previous(gLayoutPass) { gLayoutPass = pass; }
detail::InLayoutPass::~InLayoutPass() { gLayoutPass = previous; }
detail::LayoutPass detail::currentLayoutPass() { return gLayoutPass; }
Modifier Modifier::skipToLookaheadSize() const { return then(std::make_shared<SkipToLookaheadSizeElement>()); }

namespace {
struct ApproachLayoutElement final : ModifierElement {
    std::function<bool(Size)> inProgress;
    std::function<Size(const ApproachMeasureScope&, const MeasureNext&, const Constraints&, Offset&)> approach;
    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        Size lookahead;
        {
            detail::ProbeMeasure probe;  // where it is going
            detail::InLayoutPass pass(detail::LayoutPass::Lookahead);
            lookahead = c.constrain(next(c));
        }
        if (detail::isProbeMeasure()) return lookahead;
        if (!inProgress(lookahead)) return c.constrain(next(c));  // there: as the lookahead
        detail::InLayoutPass pass(detail::LayoutPass::Approach);
        return approach({lookahead, c}, next, c, contentOffset);  // (its node keeps it to the constraints)
    }
};
} // namespace

Modifier Modifier::approachLayout(std::function<bool(Size)> isMeasurementApproachInProgress,
                                  std::function<Size(const ApproachMeasureScope&, const MeasureNext&, const Constraints&, Offset&)> approachMeasure) const {
    auto e = std::make_shared<ApproachLayoutElement>();
    e->inProgress = std::move(isMeasurementApproachInProgress);
    e->approach = std::move(approachMeasure);
    return then(std::move(e));
}

BringIntoViewSpec detail::currentBringIntoViewSpec() { return Composer::current() ? LocalBringIntoViewSpec.current() : BringIntoViewSpec{}; }

void OverscrollEffect::draw(ContentDrawScope& scope) { scope.drawContent(); }

void IndicationNode::draw(ContentDrawScope& scope) { scope.drawContent(); }

namespace {
constexpr float kOverscrollPull = 0.5f;     // of the finger's leftover, less as it stretches further
constexpr float kOverscrollAbsorb = 0.35f;  // of a fling's leftover velocity
constexpr float kOverscrollSpring = 18;     // rad/s, critically damped
float along(int axis, Offset o) { return axis == 0 ? o.y : o.x; }
float along(int axis, Velocity v) { return axis == 0 ? v.y : v.x; }
} // namespace

Offset detail::StretchOverscroll::applyToScroll(Offset delta, NestedScrollSource source, const std::function<Offset(Offset)>& performScroll) {
    if (source == NestedScrollSource::UserInput) held_ = true;
    // A stretch goes back first: the finger takes it back before scrolling.
    float rest[2] = {delta.y, delta.x};
    for (int axis : {0, 1}) {
        float &f = amount[axis], &d = rest[axis], size = length(axis);
        if (f == 0 || d == 0 || (f > 0) == (d > 0) || size <= 0) continue;
        float take = std::min(std::abs(d), std::abs(f) * size);  // dp of the stretch the finger takes back
        f = f > 0 ? std::max(0.f, f - take / size) : std::min(0.f, f + take / size);
        velocity[axis] = 0;
        d = d > 0 ? d - take : d + take;
        detail::requestDraw();
    }
    Offset used = performScroll({rest[1], rest[0]});
    // What nobody used stretches it.
    if (source == NestedScrollSource::UserInput)
        for (int axis : {0, 1}) {
            float left = rest[axis] - along(axis, used), size = length(axis);
            if (std::abs(left) < 1e-3f || size <= 0) continue;
            float& f = amount[axis];
            f = std::clamp(f + left / size * kOverscrollPull * (1 - std::abs(f)), -1.f, 1.f);
            velocity[axis] = 0;
            detail::requestDraw();
        }
    return {delta.x - rest[1] + used.x, delta.y - rest[0] + used.y};
}

Task detail::StretchOverscroll::applyToFling(Velocity given, std::function<TaskOf<Velocity>(Velocity)> performFling) {
    held_ = false;
    int release = ++releases_;
    // Let go while stretched: it springs back (a pager still settles on a page) instead of flinging.
    Velocity fling = amount[0] != 0 || amount[1] != 0 ? Velocity{} : given;
    Velocity used = co_await performFling(fling);
    for (int axis : {0, 1}) {  // what the fling had left when it hit an end bumps it
        float left = along(axis, fling) - along(axis, used), size = length(axis);
        if (left != 0 && size > 0) velocity[axis] += left / size * kOverscrollAbsorb;
    }
    // Springs back, in small steps, until touched again.
    constexpr float k = kOverscrollSpring * kOverscrollSpring, c = 2 * kOverscrollSpring;
    int64_t last = (co_await detail::currentJob())->dispatcher->now();
    while (isInProgress()) {
        int64_t now = co_await awaitFrame();
        if (release != releases_ || held_) co_return;
        float dt = std::clamp(float(now - last) / 1e9f, 0.f, 0.05f);
        last = now;
        for (int axis : {0, 1}) {
            float &f = amount[axis], &v = velocity[axis];
            for (float t = 0; t < dt; t += 0.004f) {
                float h = std::min(0.004f, dt - t);
                v += (-k * f - c * v) * h;
                f += v * h;
            }
            if (std::abs(f) < 1e-4f && std::abs(v) < 1e-3f) f = v = 0;
        }
        detail::requestDraw();
    }
}

bool detail::StretchOverscroll::isInProgress() const { return amount[0] != 0 || amount[1] != 0 || velocity[0] != 0 || velocity[1] != 0; }

void detail::StretchOverscroll::draw(ContentDrawScope& scope) {
    if (amount[0] == 0 && amount[1] == 0) {
        scope.drawContent();
        return;
    }
    // Stretched along each axis from the edge pulled past (Android 12's overscroll).
    LayerTransform t;
    t.origin = {0.5f, 0.5f};
    if (float f = amount[0]; f != 0) t.scaleY = 1 + kOverscrollStretch * std::min(1.f, std::abs(f)), t.origin.y = f > 0 ? 0.f : 1.f;
    if (float f = amount[1]; f != 0) t.scaleX = 1 + kOverscrollStretch * std::min(1.f, std::abs(f)), t.origin.x = f > 0 ? 0.f : 1.f;
    scope.context().pushLayer(scope.bounds(), 0, 1, t);
    scope.drawContent();
    scope.context().popLayer();
}

std::shared_ptr<OverscrollEffect> rememberOverscrollEffect(CallSite site) {
    auto& effect = remember([] { return std::shared_ptr<OverscrollEffect>(std::make_shared<detail::StretchOverscroll>()); }, site);
    return LocalOverscrollFactory.current() ? effect : nullptr;
}

namespace {
// withoutVisualEffect / withoutEventHandling: the effect with one side taken off.
struct PartialOverscroll final : OverscrollEffect {
    std::shared_ptr<OverscrollEffect> effect;
    bool events = true, visual = true;
    Offset applyToScroll(Offset delta, NestedScrollSource source, const std::function<Offset(Offset)>& performScroll) override {
        return events ? effect->applyToScroll(delta, source, performScroll) : performScroll(delta);
    }
    Task applyToFling(Velocity velocity, std::function<TaskOf<Velocity>(Velocity)> performFling) override {
        if (events)
            co_await effect->applyToFling(velocity, std::move(performFling));
        else
            co_await performFling(velocity);
    }
    bool isInProgress() const override { return effect->isInProgress(); }
    void draw(ContentDrawScope& scope) override {
        if (visual)
            effect->draw(scope);
        else
            scope.drawContent();
    }
    void onRemeasured(Size size) override {
        if (visual) effect->onRemeasured(size);
    }
};
} // namespace

std::shared_ptr<OverscrollEffect> withoutVisualEffect(std::shared_ptr<OverscrollEffect> effect) {
    if (!effect) return nullptr;
    auto partial = std::make_shared<PartialOverscroll>();
    partial->effect = std::move(effect);
    partial->visual = false;
    return partial;
}
std::shared_ptr<OverscrollEffect> withoutEventHandling(std::shared_ptr<OverscrollEffect> effect) {
    if (!effect) return nullptr;
    auto partial = std::make_shared<PartialOverscroll>();
    partial->effect = std::move(effect);
    partial->events = false;
    return partial;
}

void OverscrollElement::draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode& node, const std::function<void()>& drawContent) const {
    if (!effect) return drawContent();
    ContentDrawScope scope(canvas, bounds, node, drawContent);
    effect->draw(scope);
}
Modifier Modifier::overscroll(std::shared_ptr<OverscrollEffect> overscrollEffect) const {
    return with<OverscrollElement>(*this, [&](auto& e) { e.effect = std::move(overscrollEffect); });
}

namespace {
// verticalScroll and horizontalScroll; scrollable and scrollableArea: with the overscroll effect
// given, or (none) the platform's if it wants one.
Modifier scrollModifier(const Modifier& m, const ScrollState& state, Orientation axis, bool enabled, bool reverse,
                        std::optional<std::shared_ptr<OverscrollEffect>> effect) {
    return with<ScrollElement>(m, [&](auto& e) {
        e.state = state;
        e.axis = axis;
        e.reverse = reverse;
        e.userScrollEnabled = enabled;
        e.overscroll = !effect && detail::overscrollWanted();
        if (effect) e.overscrollEffect = std::move(*effect);
        e.bringIntoViewSpec = detail::currentBringIntoViewSpec();
    });
}
Modifier scrollableModifier(const Modifier& m, const ScrollableState& state, Orientation axis, bool area, bool enabled, bool reverse,
                            std::optional<MutableInteractionSource> source, std::optional<std::shared_ptr<OverscrollEffect>> effect) {
    return with<ScrollableModifierElement>(m, [&](auto& e) {
        e.state = state.data_;
        e.axis = axis;
        e.reverse = reverse;
        e.area = area;
        e.userScrollEnabled = enabled;
        e.source = std::move(source);
        e.overscroll = !effect && detail::overscrollWanted();
        if (effect) e.overscrollEffect = std::move(*effect);
        e.bringIntoViewSpec = detail::currentBringIntoViewSpec();
    });
}
} // namespace

Modifier Modifier::verticalScroll(const ScrollState& state, bool enabled, bool reverseScrolling) const {
    return scrollModifier(*this, state, Orientation::Vertical, enabled, reverseScrolling, std::nullopt);
}
Modifier Modifier::verticalScroll(const ScrollState& state, std::shared_ptr<OverscrollEffect> overscrollEffect, bool enabled,
                                  bool reverseScrolling) const {
    return scrollModifier(*this, state, Orientation::Vertical, enabled, reverseScrolling, std::move(overscrollEffect));
}
Modifier Modifier::horizontalScroll(const ScrollState& state, bool enabled, bool reverseScrolling) const {
    return scrollModifier(*this, state, Orientation::Horizontal, enabled, reverseScrolling, std::nullopt);
}
Modifier Modifier::horizontalScroll(const ScrollState& state, std::shared_ptr<OverscrollEffect> overscrollEffect, bool enabled,
                                    bool reverseScrolling) const {
    return scrollModifier(*this, state, Orientation::Horizontal, enabled, reverseScrolling, std::move(overscrollEffect));
}
Modifier Modifier::scrollable(const ScrollableState& state, Orientation orientation, bool enabled, bool reverseDirection,
                              std::optional<MutableInteractionSource> interactionSource) const {
    if (!enabled) return *this;
    return scrollableModifier(*this, state, orientation, false, true, reverseDirection, std::move(interactionSource), std::nullopt);
}
Modifier Modifier::scrollable(const ScrollableState& state, Orientation orientation, std::shared_ptr<OverscrollEffect> overscrollEffect,
                              bool enabled, bool reverseDirection, std::optional<MutableInteractionSource> interactionSource) const {
    if (!enabled) return *this;
    return scrollableModifier(*this, state, orientation, false, true, reverseDirection, std::move(interactionSource),
                              std::move(overscrollEffect));
}
Modifier Modifier::scrollableArea(const ScrollableState& state, Orientation orientation, bool enabled, bool reverseScrolling,
                                  std::optional<MutableInteractionSource> interactionSource) const {
    return scrollableModifier(*this, state, orientation, true, enabled, reverseScrolling, std::move(interactionSource), std::nullopt);
}
Modifier Modifier::scrollableArea(const ScrollableState& state, Orientation orientation, std::shared_ptr<OverscrollEffect> overscrollEffect,
                                  bool enabled, bool reverseScrolling, std::optional<MutableInteractionSource> interactionSource) const {
    return scrollableModifier(*this, state, orientation, true, enabled, reverseScrolling, std::move(interactionSource),
                              std::move(overscrollEffect));
}
Modifier Modifier::scale(float scale) const { return graphicsLayer({.scaleX = scale, .scaleY = scale}); }
Modifier Modifier::scale(float scaleX, float scaleY) const { return graphicsLayer({.scaleX = scaleX, .scaleY = scaleY}); }
Modifier Modifier::rotate(float degrees) const { return graphicsLayer({.rotationZ = degrees}); }
Modifier Modifier::flex(const FlexConfig& config) const {
    return with<ParentDataElement>(*this, [&](auto& e) { e.data.flex = config; });
}
Modifier Modifier::gridItem(int row, int column, int rowSpan, int columnSpan, Alignment alignment) const {
    GridItemConfig item{std::clamp(row, -kGridMaxIndex, kGridMaxIndex), std::clamp(column, -kGridMaxIndex, kGridMaxIndex),
                        std::clamp(rowSpan, 1, kGridMaxIndex), std::clamp(columnSpan, 1, kGridMaxIndex), alignment};
    return with<ParentDataElement>(*this, [&](auto& e) { e.data.grid = item; });
}
Modifier Modifier::gridItem(GridRange rows, GridRange columns, Alignment alignment) const {
    return gridItem(rows.first, columns.first, rows.last - rows.first + 1, columns.last - columns.first + 1, alignment);
}
Modifier Modifier::gridItem(std::string area, Alignment alignment) const {
    GridItemConfig item{.alignment = alignment, .area = std::move(area)};
    return with<ParentDataElement>(*this, [&](auto& e) { e.data.grid = item; });
}
Modifier Modifier::weight(float weight, bool fill) const {
    return with<ParentDataElement>(*this, [&](auto& e) {
        e.hasWeight = true;
        e.data.weight = weight;
        e.data.fill = fill;
    });
}
void IndicationElement::draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode& node,
                             const std::function<void()>& drawContent) const {
    if (indication.kind == Indication::Kind::Custom) {
        if (!node.indicationNode) return drawContent();
        ContentDrawScope scope(canvas, bounds, node, drawContent);
        node.indicationNode->draw(scope);
        return;
    }
    drawContent();
    if (indication.kind == Indication::Kind::None) return;
    bool flat = indication.kind == Indication::Kind::StateLayer;
    float alpha = flat && source.collectIsPressedAsState() ? 0.16f
                  : source.collectIsFocusedAsState()       ? 0.1f
                  : source.collectIsHoveredAsState()       ? 0.08f
                                                           : 0.f;
    std::vector<ComposeHost::RippleDraw> ripples;
    if (!flat && node.host()) {
        int64_t now = node.host()->frameTime();
        auto tracks = detail::followPresses(source, now, [now](const detail::PressTrack& t) { return ComposeHost::rippleDone(t.start, t.released, now); });
        bool moving = false;
        for (const auto& t : tracks) {
            moving = moving || ComposeHost::rippleMoving(t.start, t.released, now);
            if (auto r = ComposeHost::rippleAt(bounds, indication, t.origin, t.start, t.released, now)) ripples.push_back(*r);
        }
        if (moving) node.host()->requestRedraw();  // the next frame draws it on (nothing to lay out)
    }
    drawStateLayer(canvas, bounds, node, indication, shape, indication.color.value_or(color), alpha, ripples);
}

Modifier Modifier::indication(const MutableInteractionSource& interactionSource, const Indication& indication) const {
    Shape shape;
    for (auto it = elements_.rbegin(); it != elements_.rend(); ++it) {
        if (auto* bg = dynamic_cast<const BackgroundElement*>(it->get())) { shape = bg->shape; break; }
        if (auto* border = dynamic_cast<const BorderElement*>(it->get())) { shape = border->shape; break; }
        if (auto* clip = dynamic_cast<const ShapeClipElement*>(it->get())) { shape = clip->shape; break; }
    }
    return with<IndicationElement>(*this, [&](auto& e) {
        e.source = interactionSource;
        e.indication = indication;
        e.shape = shape;
        e.color = LocalContentColor.current();
    });
}

Modifier Modifier::pointerHoverIcon(Cursor icon, bool overrideDescendants) const {
    return with<PointerHoverIconElement>(*this, [&](auto& e) {
        e.icon = icon;
        e.overrideDescendants = overrideDescendants;
    });
}
Modifier Modifier::matchParentSize() const {
    return with<ParentDataElement>(*this, [](auto& e) { e.data.matchParentSize = true; });
}
Modifier Modifier::fillParentMaxWidth(float fraction) const {
    return with<FillElement>(*this, [&](auto& e) {
        e.widthFraction = fraction;
        e.lazyParent = true;
    });
}
Modifier Modifier::fillParentMaxHeight(float fraction) const {
    return with<FillElement>(*this, [&](auto& e) {
        e.heightFraction = fraction;
        e.lazyParent = true;
    });
}
Modifier Modifier::fillParentMaxSize(float fraction) const {
    return with<FillElement>(*this, [&](auto& e) {
        e.widthFraction = e.heightFraction = fraction;
        e.lazyParent = true;
    });
}
Modifier Modifier::selectable(bool selected, std::function<void()> onClick, SemanticsRole role, const ClickableOptions& options) const {
    return semantics({.role = role, .checked = selected}).clickable(std::move(onClick), options);
}
Modifier Modifier::toggleable(bool value, std::function<void(bool)> onValueChange, SemanticsRole role, const ClickableOptions& options) const {
    return semantics({.role = role, .checked = value})
        .clickable([value, onValueChange = std::move(onValueChange)] {
            if (onValueChange) onValueChange(!value);
        }, options);
}
Modifier Modifier::triStateToggleable(ToggleableState state, std::function<void()> onClick, SemanticsRole role, const ClickableOptions& options) const {
    return semantics({.role = role,
                      .checked = state == ToggleableState::Indeterminate ? std::nullopt : std::optional(state == ToggleableState::On),
                      .stateDescription = state == ToggleableState::Indeterminate ? LocalStrings.current().partiallyChecked : ""})
        .clickable(std::move(onClick), options);
}
Modifier Modifier::selectableGroup() const { return semantics({.selectableGroup = true}); }

Size detail::lazyParentSize() { return gLazyParent; }
detail::LazyParentScope::LazyParentScope(Size size) : previous_(gLazyParent) { gLazyParent = size; }
detail::LazyParentScope::~LazyParentScope() { gLazyParent = previous_; }

Modifier Modifier::align(Alignment alignment) const {
    return with<ParentDataElement>(*this, [&](auto& e) { e.data.boxAlign = alignment; });
}
Modifier Modifier::align(HorizontalAlignment alignment) const {
    return with<ParentDataElement>(*this, [&](auto& e) { e.data.crossAxisBias = alignment.bias; });
}
Modifier Modifier::align(VerticalAlignment alignment) const {
    return with<ParentDataElement>(*this, [&](auto& e) { e.data.crossAxisBias = alignment.bias; });
}
Modifier Modifier::alignBy(AlignmentLine line) const {
    return with<ParentDataElement>(*this, [&](auto& e) { e.data.alignBy = line; });
}
Modifier Modifier::paddingFrom(AlignmentLine line, float before, float after) const {
    return with<AlignmentLinePaddingElement>(*this, [&](auto& e) {
        e.line = line;
        e.before = before;
        e.after = after;
    });
}
Modifier Modifier::paddingFromBaseline(float top, float bottom) const {
    return paddingFrom(FirstBaseline, top).paddingFrom(LastBaseline, std::numeric_limits<float>::quiet_NaN(), bottom);
}
Modifier Modifier::height(IntrinsicSize) const {
    return with<IntrinsicElement>(*this, [](auto& e) { e.vertical = true; });
}
Modifier Modifier::width(IntrinsicSize size) const {
    return with<IntrinsicElement>(*this, [&](auto& e) {
        e.vertical = false;
        e.minimum = size == IntrinsicSize::Min;
    });
}
Modifier Modifier::layout(std::function<Size(const MeasureNext&, const Constraints&, Offset&)> fn) const {
    return with<LayoutElement>(*this, [&](auto& e) { e.fn = std::move(fn); });
}
Modifier Modifier::layoutId(std::string id) const {
    return with<LayoutIdElement>(*this, [&](auto& e) { e.id = std::move(id); });
}
Modifier Modifier::onSizeChanged(std::function<void(Size)> onSizeChanged) const {
    return with<OnSizeChangedElement>(*this, [&](auto& e) { e.callback = std::move(onSizeChanged); });
}
Modifier Modifier::dragAndDropSource(std::function<std::optional<DragAndDropTransferData>(Offset)> transferData) const {
    return with<DragAndDropSourceElement>(*this, [&](auto& e) { e.transferData = std::move(transferData); });
}
Modifier Modifier::dragAndDropTarget(std::function<bool(const DragAndDropEvent&)> shouldStartDragAndDrop, DragAndDropTarget target) const {
    return with<DragAndDropTargetElement>(*this, [&](auto& e) {
        e.shouldStart = std::move(shouldStartDragAndDrop);
        e.target = std::move(target);
    });
}
Modifier Modifier::zIndex(float zIndex) const {
    return with<ZIndexElement>(*this, [&](auto& e) { e.zIndex = zIndex; });
}
Modifier Modifier::testTag(std::string tag) const {
    return with<TestTagElement>(*this, [&](auto& e) { e.tag = std::move(tag); });
}
Modifier Modifier::semantics(std::string contentDescription) const {
    return semantics(SemanticsProperties{.contentDescription = std::move(contentDescription)});
}
Modifier Modifier::semantics(SemanticsProperties properties) const {
    return with<SemanticsElement>(*this, [&](auto& e) {
        e.description = properties.contentDescription;
        e.properties = std::move(properties);
    });
}
bool isRtlLanguage(std::string_view tag) {
    std::string language;
    for (char c : tag) {
        if (c == '-' || c == '_') break;
        language += char(std::tolower(static_cast<unsigned char>(c)));
    }
    static constexpr std::string_view rtl[] = {"ar", "ckb", "dv", "fa", "he", "iw", "ks", "ps", "sd", "ug", "ur", "yi"};
    return std::ranges::find(rtl, language) != std::end(rtl);
}

Modifier Modifier::clearAndSetSemantics(SemanticsProperties properties) const {
    properties.clearDescendants = true;
    return semantics(std::move(properties));
}
Modifier Modifier::hideFromAccessibility() const { return semantics(SemanticsProperties{.hidden = true}); }

std::optional<SemanticsProperties> detail::semanticsOf(const Modifier& modifier) {
    std::optional<SemanticsProperties> out;
    for (const auto& element : modifier.elements()) {
        auto* s = dynamic_cast<const SemanticsElement*>(element.get());
        if (!s) continue;
        const SemanticsProperties& p = s->properties;
        if (!out) {
            out = p;
            continue;
        }
        SemanticsProperties& o = *out;
        if (!p.contentDescription.empty()) o.contentDescription += (o.contentDescription.empty() ? "" : " ") + p.contentDescription;
        if (o.role == SemanticsRole::None) o.role = p.role;
        if (!o.checked) o.checked = p.checked;
        if (o.stateDescription.empty()) o.stateDescription = p.stateDescription;
        o.heading |= p.heading;
        o.selectableGroup |= p.selectableGroup;
        o.mergeDescendants |= p.mergeDescendants;
        o.clearDescendants |= p.clearDescendants;
        o.hidden |= p.hidden;
        if (!o.progressBarRangeInfo) o.progressBarRangeInfo = p.progressBarRangeInfo;
        if (!o.setProgress) o.setProgress = p.setProgress;
        if (o.liveRegion == LiveRegionMode::None) o.liveRegion = p.liveRegion;
        o.customActions.insert(o.customActions.end(), p.customActions.begin(), p.customActions.end());
        if (o.traversalIndex == 0) o.traversalIndex = p.traversalIndex;
        if (!o.contentType) o.contentType = p.contentType;
    }
    return out;
}
Modifier Modifier::onFocusChanged(std::function<void(FocusState)> onFocusChanged) const {
    return with<FocusChangedElement>(*this, [&](auto& e) { e.onFocusChanged = std::move(onFocusChanged); });
}
Modifier Modifier::onFocusEvent(std::function<void(FocusState)> onFocusEvent) const {
    return with<FocusChangedElement>(*this, [&](auto& e) {
        e.onFocusChanged = std::move(onFocusEvent);
        e.everyEvent = true;
    });
}
Modifier Modifier::progressSemantics(float value, float valueFrom, float valueTo, int steps) const {
    float low = std::min(valueFrom, valueTo), high = std::max(valueFrom, valueTo);
    return semantics({.progressBarRangeInfo = ProgressBarRangeInfo{std::clamp(value, low, high), valueFrom, valueTo, steps}});
}
Modifier Modifier::progressSemantics() const { return semantics({.progressBarRangeInfo = ProgressBarRangeInfo::Indeterminate}); }
Modifier Modifier::focusable(bool enabled, std::optional<MutableInteractionSource> interactionSource) const {
    return enabled ? with<FocusableElement>(*this, [&](auto& e) { e.interactionSource = std::move(interactionSource); }) : *this;
}
Modifier Modifier::hoverable(const MutableInteractionSource& interactionSource, bool enabled) const {
    return enabled ? with<HoverableElement>(*this, [&](auto& e) { e.source = interactionSource; }) : *this;
}
Modifier Modifier::onKeyEvent(std::function<bool(const KeyEvent&)> onKeyEvent) const {
    return with<KeyEventElement>(*this, [&](auto& e) { e.handler = std::move(onKeyEvent); });
}
Modifier Modifier::onPreviewKeyEvent(std::function<bool(const KeyEvent&)> onPreviewKeyEvent) const {
    return with<KeyEventElement>(*this, [&](auto& e) {
        e.handler = std::move(onPreviewKeyEvent);
        e.preview = true;
    });
}
Modifier Modifier::focusRequester(const FocusRequester& requester) const {
    if (auto* c = detail::Composer::current()) requester.data_->host = &c->host();
    return with<FocusRequesterElement>(*this, [&](auto& e) { e.requester = requester; });
}

Modifier Modifier::bringIntoViewRequester(const BringIntoViewRequester& requester) const {
    return with<BringIntoViewRequesterElement>(*this, [&](auto& e) { e.requester = requester; });
}

BringIntoViewRequester::BringIntoViewRequester() : data_(std::make_shared<Data>()) {}

void BringIntoViewRequesterElement::attach(LayoutNode& node) const { requester.data_->node = node.handle(); }

Task BringIntoViewRequester::bringIntoView(std::optional<Rect> rect) const {
    auto data = data_;
    // Each frame what is left of the way, by the spring's progress: it gets there even if the
    // content moves meanwhile (Compose's BringIntoViewSpec: a spring, medium-low stiffness).
    AnimationSpec spec = spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow);
    int64_t start = -1;
    float done = 0;
    while (true) {
        int64_t now = co_await awaitFrame();
        auto handle = data->node.lock();
        LayoutNode* node = handle ? *handle : nullptr;
        if (!node || !node->isPlaced()) co_return;
        if (start < 0) start = now;
        bool finished = false;
        float p = std::min(1.f, spec.progress(float(now - start) / 1e6f, finished));
        float fraction = finished || p >= 1 || done >= 1 ? 1.f : (p - done) / (1 - done);
        Rect b = node->bounds();
        std::optional<Rect> target;
        if (rect) target = Rect{b.x + rect->x, b.y + rect->y, rect->width, rect->height};
        node->host()->bringIntoView(*node, target ? target : std::optional<Rect>(b), fraction);
        done = p;
        if (finished || p >= 1) co_return;
    }
}

FocusRequester::FocusRequester() : data_(std::make_shared<Data>()) {}
FocusRequester FocusRequester::cancel() {
    static const FocusRequester sentinel;
    return sentinel;
}
Modifier Modifier::focusProperties(FocusProperties properties) const {
    return with<FocusPropertiesElement>(*this, [&](auto& e) { e.properties = std::move(properties); });
}
Modifier Modifier::focusGroup() const { return then(std::make_shared<FocusGroupElement>()); }
Modifier Modifier::focusRestorer(std::optional<FocusRequester> fallback) const {
    return with<FocusRestorerElement>(*this, [&](auto& e) { e.fallback = std::move(fallback); });
}
Modifier Modifier::magnifier(std::function<Offset()> sourceCenter, MagnifierOptions options) const {
    auto e = std::make_shared<MagnifierElement>();
    e->sourceCenter = std::move(sourceCenter);
    e->options = std::move(options);
    return then(std::move(e));
}
void FocusRequester::requestFocus() const {
    if (data_->host) data_->host->requestFocus(*this);
}
void FocusRequester::freeFocus() const {
    if (data_->host) data_->host->freeFocus(*this);
}

void FocusManager::clearFocus() const {
    if (host_) host_->clearFocus();
}
bool FocusManager::moveFocus(FocusDirection direction) const { return host_ && host_->moveFocus(direction); }
FocusManager focusManager() { return FocusManager(&detail::Composer::require().host()); }

Modifier Modifier::recomposeHighlighter() const {
    return with<RecomposeHighlighterElement>(*this, [](auto&) {});
}
Modifier Modifier::then(const Modifier& other) const {
    Modifier m = *this;
    m.elements_.insert(m.elements_.end(), other.elements_.begin(), other.elements_.end());
    return m;
}
Modifier Modifier::then(std::shared_ptr<const ModifierElement> element) const {
    Modifier m = *this;
    m.elements_.push_back(std::move(element));
    return m;
}

LayoutNode::~LayoutNode() {
    if (!overlays.empty()) --detail::nodesWithPopups();
    if (handle_) *handle_ = nullptr;
    if (host_) host_->nodeRemoved(this);
}

std::weak_ptr<LayoutNode*> LayoutNode::handle() {
    if (!handle_) handle_ = std::make_shared<LayoutNode*>(this);
    return handle_;
}

TextMeasurer& LayoutNode::textMeasurer() const { return host_->textMeasurer(); }

namespace detail {

Size stackPolicy(LayoutNode& node, const Constraints& c) {
    Size size{c.minWidth, c.minHeight};
    for (auto* child : node.children) {
        Size s = child->measure(c);
        child->place(0, 0);
        size = {std::max(size.width, s.width), std::max(size.height, s.height)};
    }
    return size;
}

void stackSlot(const Content& content) {
    Composer::require().emit(
        keyOf(CallSite::current()),
        [](LayoutNode& node) {
            node.modifier = {};
            node.measurePolicy = stackPolicy;
            node.drawContent = {};
        },
        content);
}

} // namespace detail

Size LayoutNode::measure(const Constraints& given) {
    float outerDensity = std::exchange(gLayoutDensity, host_ ? host_->density().density : gLayoutDensity);
    struct Restore {
        float d;
        ~Restore() { gLayoutDensity = d; }
    } restore{outerDensity};
    Constraints constraints = detail::toWholePx(given);  // (whole px, as Compose's)
    bool probe = detail::isProbeMeasure();
    detail::LayoutPass pass = detail::currentLayoutPass();
    if (!needsMeasure_ && !probe && measuredWith_ == constraints && measuredIn_ == pass && !gMeasureEverything) return size_;  // as it was
    if (host_) host_->forgetLayoutReads(*this);  // what it reads this time counts
    if (!pointerInputs.empty()) detail::syncPointerInputs(*this);  // drop blocks whose modifier is gone
    for (const auto& e : modifier.elements()) e->attach(*this);
    checkReported();  // (what composing it gave it comes to be measured)
    layers_.resize(modifier.elements().size());
    LayoutNode* outer = std::exchange(gMeasuring, this);
    size_ = measureLayer(0, constraints);
    gMeasuring = outer;
    relaidOut_ = true;
    ++detail::relaidOutCount();
    if (probe) {  // its size and its children's places are the probe's now
        measuredWith_.reset();
    } else {
        needsMeasure_ = false;
        measuredWith_ = constraints;
        measuredIn_ = pass;
    }
    return size_;
}

void LayoutNode::invalidateMeasure() {
    for (LayoutNode* n = this; n && !n->needsMeasure_; n = n->parent) n->needsMeasure_ = true;
}

void LayoutNode::setReports(bool on) {
    if (on == reports_) return;
    reports_ = on;
    for (LayoutNode* n = this; n; n = n->parent) n->reportedBelow_ += on ? 1 : -1;
}

void LayoutNode::recountReported() {
    int count = reports_ ? 1 : 0;
    for (const auto* list : {&children, &overlays})
        for (const LayoutNode* child : *list) count += child->reportedBelow_;
    int change = count - reportedBelow_;
    for (LayoutNode* n = this; n && change; n = n->parent) n->reportedBelow_ += change;
}

LayoutNode* detail::measuringNode() { return gMeasuring; }

float detail::layoutDensity() { return gLayoutDensity; }
float detail::toWholePx(float dp) {
    if (!std::isfinite(dp)) return dp;
    float d = gLayoutDensity;
    // (a half up, a thousandth of a px early: a length summed from whole px a dp at a fractional
    // density can come out a hair under its half)
    return std::floor(dp * d + 0.5f + 1.f / 1024) / d;
}
Size detail::toWholePx(Size s) { return {toWholePx(s.width), toWholePx(s.height)}; }
float detail::wholePxAbove(float dp, float density) {
    if (!std::isfinite(dp)) return dp;
    return std::ceil(dp * density - 1.f / 1024) / density;  // (a hair over a whole px, from summing at a fractional density, isn't the next)
}
Size detail::wholePxAbove(Size s) { return {wholePxAbove(s.width, gLayoutDensity), wholePxAbove(s.height, gLayoutDensity)}; }
Constraints detail::toWholePx(const Constraints& c) {
    return {toWholePx(c.minWidth), toWholePx(c.maxWidth), toWholePx(c.minHeight), toWholePx(c.maxHeight)};
}

void LayoutNode::place(float x, float y, float zIndex) {
    float d = host_ ? host_->density().density : gLayoutDensity;
    float outer = std::exchange(gLayoutDensity, d);
    x = detail::toWholePx(x), y = detail::toWholePx(y);
    gLayoutDensity = outer;
    if (position_.x != x || position_.y != y || placedZIndex_ != zIndex) relaidOut_ = true, ++detail::relaidOutCount();
    position_ = {x, y};
    placedZIndex_ = zIndex;
    placedNow_ = true;
}
uint64_t& detail::relaidOutCount() {
    thread_local uint64_t count = 0;
    return count;
}
int& detail::nodesWithPopups() {
    thread_local int count = 0;
    return count;
}

detail::MeasureEverything::MeasureEverything() { ++gMeasureEverything; }
detail::MeasureEverything::~MeasureEverything() { --gMeasureEverything; }

Size LayoutNode::measureLayer(size_t index, const Constraints& c) {
    const auto& elements = modifier.elements();
    if (index == elements.size()) {
        Size s;
        for (auto* child : children) child->placedNow_ = false;
        if (measurePolicy) {
            s = measurePolicy(*this, c);
        } else {  // no policy: stack children like a Box
            for (auto* child : children) {
                Size cs = child->measure(c.loosen());
                child->place(0, 0);
                s = {std::max(s.width, cs.width), std::max(s.height, cs.height)};
            }
        }
        contentSize_ = c.constrain(detail::toWholePx(s));
        if (rtl)  // right to left: what was placed from the start is placed from the end
            for (auto* child : children)
                if (child->placedNow_) child->position_.x = contentSize_.width - child->position_.x - child->size_.width;
        return contentSize_;
    }
    if (auto* pad = dynamic_cast<const AlignmentLinePaddingElement*>(elements[index].get())) {
        // Measured from no minimum height, then padded so that the line lands where asked (a
        // content without the line counts it at its top, as Compose).
        Size inner = measureLayer(index + 1, {c.minWidth, c.maxWidth, 0, c.maxHeight});
        float line = alignmentLineInside(index + 1, pad->line);
        if (std::isnan(line)) line = 0;
        float before = std::isnan(pad->before) ? 0 : std::max(0.f, detail::toWholePx(pad->before) - line);
        float after = std::isnan(pad->after) ? 0 : std::max(0.f, detail::toWholePx(pad->after) - (inner.height - line));
        Size s = c.constrain({inner.width, before + inner.height + after});
        float y = std::isnan(pad->before) ? s.height - after - inner.height : before;
        layers_[index] = {s, {0, y}, {}};
        return s;
    }
    Offset offset;
    Size s = elements[index]->measure(
        c, [&](const Constraints& inner) { return measureLayer(index + 1, detail::toWholePx(inner)); }, offset);
    s = c.constrain(detail::toWholePx(s));
    offset = {detail::toWholePx(offset.x), detail::toWholePx(offset.y)};
    if (rtl && elements[index]->mirrorsInRtl()) {  // the content's offset from the other side
        Size inner = index + 1 == elements.size() ? contentSize_ : layers_[index + 1].size;
        offset.x = s.width - inner.width - offset.x;
    }
    layers_[index] = {s, offset, {}};
    return s;
}

float LayoutNode::contentAlignmentLine(AlignmentLine line) const {
    float own = textBaselines[line == AlignmentLine::FirstBaseline ? 0 : 1];
    if (!std::isnan(own)) return own;
    float merged = std::numeric_limits<float>::quiet_NaN();  // the children's: the topmost first, the bottommost last
    for (auto* child : children) {
        if (!child->placedNow_) continue;
        float at = child->alignmentLine(line);
        if (std::isnan(at)) continue;
        at += child->position_.y;
        merged = std::isnan(merged) ? at : line == AlignmentLine::FirstBaseline ? std::min(merged, at) : std::max(merged, at);
    }
    return merged;
}

float LayoutNode::alignmentLineInside(size_t layer, AlignmentLine line) const {
    float content = contentAlignmentLine(line);
    if (std::isnan(content)) return content;
    for (size_t i = layer; i < layers_.size(); ++i) content += layers_[i].contentOffset.y;
    return content;
}

float LayoutNode::alignmentLine(AlignmentLine line) const { return alignmentLineInside(0, line); }

void LayoutNode::updateAbsolutePosition(Offset parentContentOrigin) {
    Offset o{parentContentOrigin.x + position_.x, parentContentOrigin.y + position_.y};
    // Nothing in it measured or moved, and it is where it was: so is everything in it.
    if (!relaidOut_ && o.x == bounds_.x && o.y == bounds_.y) return;
    relaidOut_ = false;
    bounds_ = {o.x, o.y, size_.width, size_.height};
    for (auto& layer : layers_) {
        layer.bounds = {o.x, o.y, layer.size.width, layer.size.height};
        o.x += layer.contentOffset.x;
        o.y += layer.contentOffset.y;
    }
    contentBounds_ = {o.x, o.y, contentSize_.width, contentSize_.height};
    zIndex_ = placedZIndex_;
    for (const auto& e : modifier.elements())
        if (auto* z = dynamic_cast<const ZIndexElement*>(e.get())) zIndex_ += z->zIndex;
    zOrdered_ = false;
    for (auto* child : children) {
        child->updateAbsolutePosition(o);
        zOrdered_ = zOrdered_ || child->zIndex_ != 0;
    }
    if (zOrdered_) {  // drawn (and hit) in this order
        zOrder_ = children;
        std::stable_sort(zOrder_.begin(), zOrder_.end(), [](const LayoutNode* a, const LayoutNode* b) { return a->zIndex_ < b->zIndex_; });
    } else {
        zOrder_.clear();
    }
}

ParentData LayoutNode::parentData() const {
    ParentData result;
    for (const auto& e : modifier.elements()) {
        auto* pd = dynamic_cast<const ParentDataElement*>(e.get());
        if (!pd) continue;
        if (pd->hasWeight) {
            result.weight = pd->data.weight;
            result.fill = pd->data.fill;
        }
        if (pd->data.boxAlign) result.boxAlign = pd->data.boxAlign;
        if (pd->data.crossAxisBias) result.crossAxisBias = pd->data.crossAxisBias;
        if (pd->data.alignBy) result.alignBy = pd->data.alignBy;
        if (pd->data.matchParentSize) result.matchParentSize = true;
        if (pd->data.flex) result.flex = pd->data.flex;
        if (pd->data.grid) result.grid = pd->data.grid;
    }
    return result;
}

void LayoutNode::draw(GraphicsContext& canvas) {
    canvas.beginNode(this);
    drawLayer(canvas, 0);
    canvas.endNode();
}

void LayoutNode::drawAfter(GraphicsContext& canvas, const ModifierElement* element) {
    const auto& elements = modifier.elements();
    for (size_t i = 0; i < elements.size(); ++i)
        if (elements[i].get() == element) {
            canvas.beginNode(this);
            drawLayer(canvas, i + 1);
            canvas.endNode();
            return;
        }
}

std::optional<Rect> LayoutNode::boundsOf(const ModifierElement* element) const {
    const auto& elements = modifier.elements();
    for (size_t i = 0; i < elements.size(); ++i)
        if (elements[i].get() == element) return layerBounds(i);
    return std::nullopt;
}

GraphicsContext*& detail::recordingInto() {
    thread_local GraphicsContext* into = nullptr;
    return into;
}

detail::DrawPart& detail::drawPart() {
    thread_local DrawPart part;
    return part;
}

void LayoutNode::drawLayer(GraphicsContext& canvas, size_t index) {
    const auto& elements = modifier.elements();
    // A scroll container draws its overscroll effect around its content, in its viewport.
    if (index < elements.size() && index < layers_.size())
        if (auto* s = dynamic_cast<const ScrollableElement*>(elements[index].get()); s && s->drawsOverscroll())
            if (OverscrollEffect* effect = overscrollOf(*s)) {
                Rect view = layers_[index].bounds;
                if (canvas.quickReject(s->clipBounds(view))) return;
                elements[index]->draw(canvas, view, *this, [&] {
                    GraphicsContext& into = detail::recordingInto() ? *detail::recordingInto() : canvas;
                    std::function<void()> content = [&] { drawLayer(into, index + 1); };
                    ContentDrawScope scope(into, view, *this, content);
                    effect->draw(scope);
                });
                return;
            }
    if (index < elements.size() && index < layers_.size()) {
        // What clips itself to its bounds and lies outside the visible area isn't drawn: a
        // scrolled-away card with rounded corners costs no offscreen layer.
        if (elements[index]->clipsContent() && canvas.quickReject(elements[index]->clipBounds(layers_[index].bounds))) return;
        elements[index]->draw(canvas, layers_[index].bounds, *this,
                              [&] { drawLayer(detail::recordingInto() ? *detail::recordingInto() : canvas, index + 1); });
        return;
    }
    detail::DrawPart& part = detail::drawPart();
    if (part.node == this) {  // one part (the children draw whole)
        detail::DrawPart drawing = std::exchange(part, {});
        if (drawing.own && drawContent) drawContent(canvas, contentBounds_, *this);
        if (!drawing.own) {
            GraphicsContext* placing = std::exchange(detail::recordingInto(), drawing.recording);
            for (auto* child : childrenInDrawOrder())
                if (child->isPlaced()) child->draw(*drawing.children);
            detail::recordingInto() = placing;
        }
        part = drawing;
        return;
    }
    if (drawContent) drawContent(canvas, contentBounds_, *this);
    for (auto* child : childrenInDrawOrder())
        if (child->isPlaced()) child->draw(canvas);
}

} // namespace kompoz
