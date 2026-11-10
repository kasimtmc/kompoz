#pragma once

// Compose's named arguments for Modifier's, Color's and InfiniteTransition's members, as kompozc writes them:
//   Modifier().padding(arg::horizontal = 16, arg::vertical = 8).border(arg::width = 1, arg::color = c, arg::shape = s)
//   color.copy(arg::alpha = 0.5f)
//   transition.animateFloat(arg::initialValue = 0, arg::targetValue = 1, arg::animationSpec = tween(600))
// The names are arg's (kompoz::arg::width, ...): end, x or width as names of all of kompoz's would
// take those words from code that says `using namespace kompoz`.

#include "gestures.hpp"
#include "layout.hpp"
#include "named.hpp"
#include "shared_transition.hpp"

namespace kompoz {

template <class... A>
    requires(detail::anyNamedArgument<A...>)
Color Color::copy(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::Alpha, n::Red, n::Green, n::Blue>;
    static_assert(P::check<A...>());
    float a = P::get<n::Alpha>(alpha(), args...);
    if constexpr (P::given<n::Red, A...>() || P::given<n::Green, A...>() || P::given<n::Blue, A...>())
        return copy(a, P::get<n::Red>(red(), args...), P::get<n::Green>(green(), args...), P::get<n::Blue>(blue(), args...));
    else return copy(a);
}

// padding(all), padding(horizontal, vertical) or padding(start, top, end, bottom): the form of the
// names given.
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::padding(A&&... args) const {
    namespace n = detail::names;
    if constexpr (detail::countOf<n::All, A...> > 0) {
        using P = detail::Params<n::All>;
        static_assert(P::check<A...>());
        return padding(P::get<n::All>(0.f, args...));
    } else if constexpr (detail::countOf<n::Horizontal, A...> + detail::countOf<n::Vertical, A...> > 0) {
        using P = detail::Params<n::Horizontal, n::Vertical>;
        static_assert(P::check<A...>());
        return padding(P::get<n::Horizontal>(0.f, args...), P::get<n::Vertical>(0.f, args...));
    } else {
        using P = detail::Params<n::Start, n::Top, n::End, n::Bottom>;
        static_assert(P::check<A...>());
        return padding(P::get<n::Start>(0.f, args...), P::get<n::Top>(0.f, args...), P::get<n::End>(0.f, args...),
                       P::get<n::Bottom>(0.f, args...));
    }
}

// background(color, shape) or background(brush, shape).
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::background(A&&... args) const {
    namespace n = detail::names;
    using OfBrush = detail::Params<n::Brush, n::Shape>;
    if constexpr (OfBrush::form<n::Brush, const Brush&, A...>()) {
        using P = OfBrush;
        static_assert(P::check<A...>());
        const Brush& brush = P::arg<n::Brush>(args...);
        return background(brush, P::get<n::Shape>(Shape(RectangleShape), args...));
    } else {
        using P = detail::Params<n::Color, n::Shape>;
        static_assert(P::check<A...>());
        return background(Color(P::arg<n::Color>(args...)), P::get<n::Shape>(Shape(RectangleShape), args...));
    }
}

// border(width, color, shape) or border(border = a BorderStroke, shape).
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::border(A&&... args) const {
    namespace n = detail::names;
    using OfStroke = detail::Params<n::Border, n::Shape>;
    if constexpr (OfStroke::form<n::Border, const BorderStroke&, A...>()) {
        using P = OfStroke;
        static_assert(P::check<A...>());
        const BorderStroke& stroke = P::arg<n::Border>(args...);
        return border(stroke, P::get<n::Shape>(Shape(RectangleShape), args...));
    } else {
        using P = detail::Params<n::Width, n::Color, n::Shape>;
        static_assert(P::check<A...>());
        return border(float(P::arg<n::Width>(args...)), Color(P::arg<n::Color>(args...)), P::get<n::Shape>(Shape(RectangleShape), args...));
    }
}

template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::size(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::Width, n::Height>;
    static_assert(P::check<A...>());
    return size(float(P::arg<n::Width>(args...)), float(P::arg<n::Height>(args...)));
}

template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::offset(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::X, n::Y>;
    static_assert(P::check<A...>());
    return offset(P::get<n::X>(0.f, args...), P::get<n::Y>(0.f, args...));
}

// graphicsLayer(scaleX, scaleY, alpha, translationX, translationY, shadowElevation, rotationX, rotationY,
// rotationZ, cameraDistance, transformOrigin, shape, clip, renderEffect, ambientShadowColor,
// spotShadowColor, compositingStrategy, blendMode, colorFilter), as Compose's: a layer of those values,
// the others GraphicsLayerScope's own, read in the composition.
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::graphicsLayer(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::ScaleX, n::ScaleY, n::Alpha, n::TranslationX, n::TranslationY, n::ShadowElevation, n::RotationX,
                             n::RotationY, n::RotationZ, n::CameraDistance, n::TransformOrigin, n::Shape, n::Clip, n::RenderEffect,
                             n::AmbientShadowColor, n::SpotShadowColor, n::CompositingStrategy, n::BlendMode, n::ColorFilter>;
    static_assert(P::check<A...>());
    GraphicsLayerScope l;
    P::set<n::ScaleX>(l.scaleX, args...);
    P::set<n::ScaleY>(l.scaleY, args...);
    P::set<n::Alpha>(l.alpha, args...);
    P::set<n::TranslationX>(l.translationX, args...);
    P::set<n::TranslationY>(l.translationY, args...);
    P::set<n::ShadowElevation>(l.shadowElevation, args...);
    P::set<n::RotationX>(l.rotationX, args...);
    P::set<n::RotationY>(l.rotationY, args...);
    P::set<n::RotationZ>(l.rotationZ, args...);
    P::set<n::CameraDistance>(l.cameraDistance, args...);
    P::set<n::TransformOrigin>(l.transformOrigin, args...);
    P::set<n::Shape>(l.shape, args...);
    P::set<n::Clip>(l.clip, args...);
    P::set<n::RenderEffect>(l.renderEffect, args...);
    P::set<n::AmbientShadowColor>(l.ambientShadowColor, args...);
    P::set<n::SpotShadowColor>(l.spotShadowColor, args...);
    P::set<n::CompositingStrategy>(l.compositingStrategy, args...);
    P::set<n::BlendMode>(l.blendMode, args...);
    P::set<n::ColorFilter>(l.colorFilter, args...);
    return graphicsLayer(l);
}

// semantics(mergeDescendants = true) { … }: what is inside said as one, with what its block sets.
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::semantics(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::MergeDescendants, detail::BlockWith<SemanticsPropertyReceiver&>>;
    static_assert(P::check<A...>());
    SemanticsProperties p;
    p.mergeDescendants = P::get<n::MergeDescendants>(false, args...);
    if constexpr (P::blockGiven<A...>()) {
        SemanticsPropertyReceiver receiver(p);
        std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...))(receiver);
    }
    return semantics(std::move(p));
}

// gridItem(row = 1, column = 2, rowSpan = 1, columnSpan = 2, alignment = …), as Compose's.
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::gridItem(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::Row, n::Column, n::RowSpan, n::ColumnSpan, n::Alignment>;
    static_assert(P::check<A...>());
    return gridItem(P::get<n::Row>(0, args...), P::get<n::Column>(0, args...), P::get<n::RowSpan>(1, args...),
                    P::get<n::ColumnSpan>(1, args...), P::get<n::Alignment>(Alignment{Alignment::TopStart}, args...));
}

// Constraints::copy(minWidth = 0, minHeight = 0): the others as they were.
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Constraints Constraints::copy(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::MinWidth, n::MaxWidth, n::MinHeight, n::MaxHeight>;
    static_assert(P::check<A...>());
    return {P::get<n::MinWidth>(minWidth, args...), P::get<n::MaxWidth>(maxWidth, args...), P::get<n::MinHeight>(minHeight, args...),
            P::get<n::MaxHeight>(maxHeight, args...)};
}

template <class... A>
    requires(detail::anyNamedArgument<A...>)
void LayoutModifierPlaceable::placeRelative(A&&... args) const {
    using P = detail::Params<detail::names::X, detail::names::Y>;
    static_assert(P::check<A...>());
    placeRelative(float(P::arg<detail::names::X>(args...)), float(P::arg<detail::names::Y>(args...)));
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
void LayoutModifierPlaceable::place(A&&... args) const {
    using P = detail::Params<detail::names::X, detail::names::Y>;
    static_assert(P::check<A...>());
    place(float(P::arg<detail::names::X>(args...)), float(P::arg<detail::names::Y>(args...)));
}

template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::toggleable(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::Value, n::OnValueChange, n::Role, n::Enabled, n::InteractionSource>;
    static_assert(P::check<A...>());
    ClickableOptions options;
    P::set<n::Enabled>(options.enabled, args...);
    P::set<n::InteractionSource>(options.interactionSource, args...);
    return toggleable(bool(P::arg<n::Value>(args...)), std::function<void(bool)>(P::arg<n::OnValueChange>(args...)),
                      P::get<n::Role>(SemanticsRole::None, args...), options);
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::selectable(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::Selected, n::OnClick, n::Role, n::Enabled, n::InteractionSource>;
    static_assert(P::check<A...>());
    ClickableOptions options;
    P::set<n::Enabled>(options.enabled, args...);
    P::set<n::InteractionSource>(options.interactionSource, args...);
    return selectable(bool(P::arg<n::Selected>(args...)), std::function<void()>(P::arg<n::OnClick>(args...)),
                      P::get<n::Role>(SemanticsRole::None, args...), options);
}

// Font::copy(fontFamily = lora, fontWeight = FontWeight::Bold, fontSize = 20.sp, …): a size is a TextUnit
// (its sp) or a number of sp; a family is a FontFamily (its id) or an id.
template <class... A>
    requires(detail::anyNamedArgument<A...>)
auto Font::copy(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::FontFamily, n::FontWeight, n::FontSize, n::FontStyle, n::LetterSpacing, n::LineHeight, n::Brush, n::Shadow,
                             n::LineHeightStyle, n::TextIndent, n::FontFeatureSettings, n::LineBreak, n::DrawStyle, n::TextGeometricTransform, n::Color>;
    static_assert(P::check<A...>());
    // Compose's typography.titleLarge.copy(brush = …, shadow = …): a TextStyle of this font with those (a Font has none of them).
    constexpr bool styled = P::template given<n::Brush, A...>() || P::template given<n::Shadow, A...>() || P::template given<n::LineHeightStyle, A...>() ||
                            P::template given<n::TextIndent, A...>() || P::template given<n::FontFeatureSettings, A...>() || P::template given<n::LineBreak, A...>() ||
                            P::template given<n::DrawStyle, A...>() || P::template given<n::TextGeometricTransform, A...>() || P::template given<n::Color, A...>();
    Font f = *this;
    auto sp = [](auto&& v) {
        if constexpr (requires { v.value; }) return float(v.value);
        else return float(v);
    };
    if constexpr (P::given<n::FontFamily, A...>()) {
        auto&& family = P::arg<n::FontFamily>(args...);
        if constexpr (requires { family.id; }) f.family = family.id;
        else f.family = uint16_t(family);
    }
    P::set<n::FontWeight>(f.weight, args...);
    P::set<n::FontStyle>(f.style, args...);
    if constexpr (P::given<n::FontSize, A...>()) f.size = sp(P::arg<n::FontSize>(args...));
    if constexpr (P::given<n::LetterSpacing, A...>()) f.letterSpacing = sp(P::arg<n::LetterSpacing>(args...));
    if constexpr (P::given<n::LineHeight, A...>()) f.lineHeight = sp(P::arg<n::LineHeight>(args...));
    if constexpr (styled) {
        TextStyle style;
        style.font = f;
        P::set<n::Brush>(style.brush, args...);
        P::set<n::Shadow>(style.shadow, args...);
        P::set<n::LineHeightStyle>(style.lineHeightStyle, args...);
        P::set<n::TextIndent>(style.textIndent, args...);
        P::set<n::FontFeatureSettings>(style.fontFeatureSettings, args...);
        P::set<n::LineBreak>(style.lineBreak, args...);
        P::set<n::DrawStyle>(style.drawStyle, args...);
        P::set<n::TextGeometricTransform>(style.textGeometricTransform, args...);
        P::set<n::Color>(style.color, args...);
        return style;
    } else {
        return f;
    }
}

// basicMarquee(iterations = Int.MAX_VALUE, animationMode = …, repeatDelayMillis = …, initialDelayMillis = …, spacing = …, velocity = 60.dp):
// Compose's names for the MarqueeOptions.
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::basicMarquee(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::Iterations, n::AnimationMode, n::RepeatDelayMillis, n::InitialDelayMillis, n::Spacing, n::Velocity>;
    static_assert(P::check<A...>());
    MarqueeOptions options;
    P::set<n::Iterations>(options.iterations, args...);
    P::set<n::AnimationMode>(options.animationMode, args...);
    P::set<n::RepeatDelayMillis>(options.repeatDelayMillis, args...);
    P::set<n::InitialDelayMillis>(options.initialDelayMillis, args...);
    P::set<n::Spacing>(options.spacing, args...);
    P::set<n::Velocity>(options.velocity, args...);
    return basicMarquee(options);
}

template <class... A>
    requires(detail::anyNamedArgument<A...>)
void DrawScope::drawText(const TextLayoutResult& layout, A&&... args) {
    namespace n = detail::names;
    using P = detail::Params<n::TopLeft, n::Color, n::Alpha>;
    static_assert(P::check<A...>());
    drawText(layout, P::get<n::Color>(std::optional<Color>(), args...), P::get<n::TopLeft>(Offset{}, args...), P::get<n::Alpha>(std::optional<float>(), args...));
}

// ---- DrawScope's drawing calls, as Compose names their arguments ----
namespace detail {
// The arguments every one of them takes, after its own.
template <class P, class... A>
float drawAlpha(A&&... args) { return P::template get<names::Alpha>(1.f, args...); }
template <class P, class... A>
DrawStyle drawStyle(A&&... args) { return P::template get<names::Style>(Fill, args...); }
template <class P, class... A>
std::optional<ColorFilter> drawColorFilter(A&&... args) { return P::template get<names::ColorFilter>(std::optional<ColorFilter>(), args...); }
template <class P, class... A>
BlendMode drawBlendMode(A&&... args) { return P::template get<names::BlendMode>(BlendMode::SrcOver, args...); }
// The block, last.
template <class... A>
std::function<void(DrawScope&)> drawBlock(A&&... args) {
    return std::function<void(DrawScope&)>(std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...)));
}
using LineParams = Params<names::Start, names::End, names::StrokeWidth, names::Cap, names::PathEffect, names::Alpha, names::ColorFilter, names::BlendMode>;
using RectParams = Params<names::TopLeft, names::Size, names::Alpha, names::Style, names::ColorFilter, names::BlendMode>;
using RoundRectParams = Params<names::TopLeft, names::Size, names::CornerRadius, names::Alpha, names::Style, names::ColorFilter, names::BlendMode>;
using CircleParams = Params<names::Radius, names::Center, names::Alpha, names::Style, names::ColorFilter, names::BlendMode>;
using ArcParams = Params<names::StartAngle, names::SweepAngle, names::UseCenter, names::TopLeft, names::Size, names::Alpha, names::Style, names::ColorFilter, names::BlendMode>;
using PathParams = Params<names::Alpha, names::Style, names::ColorFilter, names::BlendMode>;
using PointsParams = Params<names::StrokeWidth, names::Cap, names::PathEffect, names::Alpha, names::ColorFilter, names::BlendMode>;
template <class Paint, class... A>
void drawLineBy(DrawScope& d, const Paint& paint, A&&... args) {
    using P = LineParams;
    static_assert(P::check<A...>());
    d.drawLine(paint, Offset(P::arg<names::Start>(args...)), Offset(P::arg<names::End>(args...)), P::get<names::StrokeWidth>(0.f, args...),
               P::get<names::Cap>(StrokeCap::Butt, args...), P::get<names::PathEffect>(std::optional<PathEffect>(), args...), drawAlpha<P>(args...),
               drawColorFilter<P>(args...), drawBlendMode<P>(args...));
}
template <class Paint, class... A>
void drawRectBy(DrawScope& d, const Paint& paint, A&&... args) {
    using P = RectParams;
    static_assert(P::check<A...>());
    d.drawRect(paint, P::get<names::TopLeft>(Offset{}, args...), P::get<names::Size>(std::optional<Size>(), args...), drawAlpha<P>(args...),
               drawStyle<P>(args...), drawColorFilter<P>(args...), drawBlendMode<P>(args...));
}
template <class Paint, class... A>
void drawRoundRectBy(DrawScope& d, const Paint& paint, A&&... args) {
    using P = RoundRectParams;
    static_assert(P::check<A...>());
    if constexpr (std::is_same_v<Paint, Color>)  // (its style before its alpha, as Compose's)
        d.drawRoundRect(paint, P::get<names::TopLeft>(Offset{}, args...), P::get<names::Size>(std::optional<Size>(), args...),
                        P::get<names::CornerRadius>(CornerRadius{}, args...), drawStyle<P>(args...), drawAlpha<P>(args...), drawColorFilter<P>(args...),
                        drawBlendMode<P>(args...));
    else
        d.drawRoundRect(paint, P::get<names::TopLeft>(Offset{}, args...), P::get<names::Size>(std::optional<Size>(), args...),
                        P::get<names::CornerRadius>(CornerRadius{}, args...), drawAlpha<P>(args...), drawStyle<P>(args...), drawColorFilter<P>(args...),
                        drawBlendMode<P>(args...));
}
template <class Paint, class... A>
void drawCircleBy(DrawScope& d, const Paint& paint, A&&... args) {
    using P = CircleParams;
    static_assert(P::check<A...>());
    d.drawCircle(paint, P::get<names::Radius>(std::optional<float>(), args...), P::get<names::Center>(std::optional<Offset>(), args...),
                 drawAlpha<P>(args...), drawStyle<P>(args...), drawColorFilter<P>(args...), drawBlendMode<P>(args...));
}
template <class Paint, class... A>
void drawOvalBy(DrawScope& d, const Paint& paint, A&&... args) {
    using P = RectParams;
    static_assert(P::check<A...>());
    d.drawOval(paint, P::get<names::TopLeft>(Offset{}, args...), P::get<names::Size>(std::optional<Size>(), args...), drawAlpha<P>(args...),
               drawStyle<P>(args...), drawColorFilter<P>(args...), drawBlendMode<P>(args...));
}
template <class Paint, class... A>
void drawArcBy(DrawScope& d, const Paint& paint, A&&... args) {
    using P = ArcParams;
    static_assert(P::check<A...>());
    d.drawArc(paint, float(P::arg<names::StartAngle>(args...)), float(P::arg<names::SweepAngle>(args...)), bool(P::arg<names::UseCenter>(args...)),
              P::get<names::TopLeft>(Offset{}, args...), P::get<names::Size>(std::optional<Size>(), args...), drawAlpha<P>(args...), drawStyle<P>(args...),
              drawColorFilter<P>(args...), drawBlendMode<P>(args...));
}
template <class Paint, class... A>
void drawPathBy(DrawScope& d, const Path& path, const Paint& paint, A&&... args) {
    using P = PathParams;
    static_assert(P::check<A...>());
    d.drawPath(path, paint, drawAlpha<P>(args...), drawStyle<P>(args...), drawColorFilter<P>(args...), drawBlendMode<P>(args...));
}
template <class Paint, class... A>
void drawPointsBy(DrawScope& d, const std::vector<Offset>& points, PointMode pointMode, const Paint& paint, A&&... args) {
    using P = PointsParams;
    static_assert(P::check<A...>());
    d.drawPoints(points, pointMode, paint, P::get<names::StrokeWidth>(0.f, args...), P::get<names::Cap>(StrokeCap::Butt, args...),
                 P::get<names::PathEffect>(std::optional<PathEffect>(), args...), drawAlpha<P>(args...), drawColorFilter<P>(args...), drawBlendMode<P>(args...));
}
} // namespace detail

#define KOMPOZ_DRAW_BY_NAME(method, helper, ...)                                                   \
    template <class... A>                                                                          \
        requires(detail::anyNamedArgument<A...>)                                                   \
    void DrawScope::method(__VA_ARGS__ Color color, A&&... args) {                                 \
        detail::helper(*this, KOMPOZ_DRAW_FIRST color, std::forward<A>(args)...);                 \
    }                                                                                              \
    template <class... A>                                                                          \
        requires(detail::anyNamedArgument<A...>)                                                   \
    void DrawScope::method(__VA_ARGS__ const Brush& brush, A&&... args) {                          \
        detail::helper(*this, KOMPOZ_DRAW_FIRST brush, std::forward<A>(args)...);                 \
    }
#define KOMPOZ_DRAW_FIRST
KOMPOZ_DRAW_BY_NAME(drawLine, drawLineBy)
KOMPOZ_DRAW_BY_NAME(drawRect, drawRectBy)
KOMPOZ_DRAW_BY_NAME(drawRoundRect, drawRoundRectBy)
KOMPOZ_DRAW_BY_NAME(drawCircle, drawCircleBy)
KOMPOZ_DRAW_BY_NAME(drawOval, drawOvalBy)
KOMPOZ_DRAW_BY_NAME(drawArc, drawArcBy)
#undef KOMPOZ_DRAW_FIRST
#define KOMPOZ_DRAW_FIRST path,
KOMPOZ_DRAW_BY_NAME(drawPath, drawPathBy, const Path& path,)
#undef KOMPOZ_DRAW_FIRST
#define KOMPOZ_DRAW_FIRST points, pointMode,
KOMPOZ_DRAW_BY_NAME(drawPoints, drawPointsBy, const std::vector<Offset>& points, PointMode pointMode,)
#undef KOMPOZ_DRAW_FIRST
#undef KOMPOZ_DRAW_BY_NAME

// The transforms with a block, their arguments by name, the block last.
template <class... A>
    requires(detail::anyNamedArgument<A...>)
void DrawScope::translate(A&&... args) {
    namespace n = detail::names;
    using P = detail::Params<n::Left, n::Top, detail::BlockWith<DrawScope&>>;
    static_assert(P::check<A...>());
    translate(P::get<n::Left>(0.f, args...), P::get<n::Top>(0.f, args...), detail::drawBlock(args...));
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
void DrawScope::inset(A&&... args) {
    namespace n = detail::names;
    using P = detail::Params<n::Left, n::Top, n::Right, n::Bottom, n::Horizontal, n::Vertical, detail::BlockWith<DrawScope&>>;
    static_assert(P::check<A...>());
    float horizontal = P::get<n::Horizontal>(0.f, args...), vertical = P::get<n::Vertical>(0.f, args...);
    inset(P::get<n::Left>(horizontal, args...), P::get<n::Top>(vertical, args...), P::get<n::Right>(horizontal, args...), P::get<n::Bottom>(vertical, args...),
          detail::drawBlock(args...));
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
void DrawScope::clipRect(A&&... args) {
    namespace n = detail::names;
    using P = detail::Params<n::Left, n::Top, n::Right, n::Bottom, n::ClipOp, detail::BlockWith<DrawScope&>>;
    static_assert(P::check<A...>());
    float left = P::get<n::Left>(0.f, args...), top = P::get<n::Top>(0.f, args...);
    float right = P::get<n::Right>(size().width, args...), bottom = P::get<n::Bottom>(size().height, args...);
    clipRect(Offset{left, top}, Size{right - left, bottom - top}, detail::drawBlock(args...), P::get<n::ClipOp>(ClipOp::Intersect, args...));
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
void DrawScope::rotate(float degrees, A&&... args) {
    namespace n = detail::names;
    using P = detail::Params<n::Pivot, detail::BlockWith<DrawScope&>>;
    static_assert(P::check<A...>());
    rotate(degrees, detail::drawBlock(args...), P::get<n::Pivot>(std::optional<Offset>(), args...));
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
void DrawScope::rotateRad(float radians, A&&... args) {
    namespace n = detail::names;
    using P = detail::Params<n::Pivot, detail::BlockWith<DrawScope&>>;
    static_assert(P::check<A...>());
    rotateRad(radians, detail::drawBlock(args...), P::get<n::Pivot>(std::optional<Offset>(), args...));
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
void DrawScope::scale(float scaleX, float scaleY, A&&... args) {
    namespace n = detail::names;
    using P = detail::Params<n::Pivot, detail::BlockWith<DrawScope&>>;
    static_assert(P::check<A...>());
    scale(scaleX, scaleY, detail::drawBlock(args...), P::get<n::Pivot>(std::optional<Offset>(), args...));
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
void DrawScope::scale(float scale, A&&... args) {
    this->scale(scale, scale, std::forward<A>(args)...);
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
void DrawScope::clipPath(const Path& path, A&&... args) {
    namespace n = detail::names;
    using P = detail::Params<n::ClipOp, detail::BlockWith<DrawScope&>>;
    static_assert(P::check<A...>());
    clipPath(path, detail::drawBlock(args...), P::get<n::ClipOp>(ClipOp::Intersect, args...));
}

// Stroke(width = …, miter = …, cap = …, join = …, pathEffect = …).
template <class... A>
    requires(detail::anyNamedArgument<A...>)
DrawStyle Stroke(A&&... args) {
    namespace n = detail::names;
    using P = detail::Params<n::Width, n::Miter, n::Cap, n::Join, n::PathEffect>;
    static_assert(P::check<A...>());
    return Stroke(P::get<n::Width>(0.f, args...), P::get<n::Miter>(4.f, args...), P::get<n::Cap>(StrokeCap::Butt, args...),
                  P::get<n::Join>(StrokeJoin::Miter, args...), P::get<n::PathEffect>(std::optional<PathEffect>(), args...));
}

// animateFloat(initialValue, targetValue, animationSpec, label) and animateColor's: the label is
// Compose's name for its tools (none here).
template <class... A>
    requires(detail::anyNamedArgument<A...>)
InfiniteState<float> InfiniteTransition::animateFloat(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::InitialValue, n::TargetValue, n::AnimationSpec, n::Label>;
    static_assert(P::check<A...>());
    P::get<n::Label>(std::string(), args...);
    return animateFloat(float(P::arg<n::InitialValue>(args...)), float(P::arg<n::TargetValue>(args...)),
                        AnimationSpec(P::arg<n::AnimationSpec>(args...)));
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
InfiniteState<Color> InfiniteTransition::animateColor(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::InitialValue, n::TargetValue, n::AnimationSpec, n::Label>;
    static_assert(P::check<A...>());
    P::get<n::Label>(std::string(), args...);
    return animateColor(Color(P::arg<n::InitialValue>(args...)), Color(P::arg<n::TargetValue>(args...)),
                        AnimationSpec(P::arg<n::AnimationSpec>(args...)));
}

// ---- Gestures, as Compose names their arguments ----
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::draggable(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::State, n::Orientation, n::Enabled, n::InteractionSource>;
    static_assert(P::check<A...>());
    return draggable(P::arg<n::State>(args...), P::arg<n::Orientation>(args...), P::get<n::Enabled>(true, args...),
                     P::get<n::InteractionSource>(std::optional<MutableInteractionSource>(), args...));
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::dragAndDropTarget(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::ShouldStartDragAndDrop, n::Target>;
    static_assert(P::check<A...>());
    return dragAndDropTarget(std::function<bool(const DragAndDropEvent&)>(P::arg<n::ShouldStartDragAndDrop>(args...)),
                             DragAndDropTarget(P::arg<n::Target>(args...)));
}

template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::combinedClickable(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::OnClick, n::OnLongClick, n::OnDoubleClick, n::Enabled>;
    static_assert(P::check<A...>());
    return combinedClickable(std::function<void()>(P::arg<n::OnClick>(args...)), P::get<n::OnLongClick>(std::function<void()>(), args...),
                             P::get<n::OnDoubleClick>(std::function<void()>(), args...), P::get<n::Enabled>(true, args...));
}

// ---- Shared elements, as Compose names their arguments ----
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::sharedElement(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::SharedContentState, n::AnimatedVisibilityScope, n::BoundsTransform, n::RenderInOverlayDuringTransition, n::ZIndexInOverlay>;
    static_assert(P::check<A...>());
    return sharedElement(P::arg<n::SharedContentState>(args...), P::arg<n::AnimatedVisibilityScope>(args...),
                         P::get<n::BoundsTransform>(defaultBoundsTransform(), args...), P::get<n::RenderInOverlayDuringTransition>(true, args...),
                         P::get<n::ZIndexInOverlay>(0.f, args...));
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::sharedBounds(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::SharedContentState, n::AnimatedVisibilityScope, n::BoundsTransform, n::ResizeMode, n::RenderInOverlayDuringTransition,
                             n::ZIndexInOverlay>;
    static_assert(P::check<A...>());
    return sharedBounds(P::arg<n::SharedContentState>(args...), P::arg<n::AnimatedVisibilityScope>(args...),
                        P::get<n::BoundsTransform>(defaultBoundsTransform(), args...), P::get<n::ResizeMode>(SharedResizeMode{}, args...),
                        P::get<n::RenderInOverlayDuringTransition>(true, args...), P::get<n::ZIndexInOverlay>(0.f, args...));
}

// clickable(interactionSource = source, indication = null) { … }, clickable(enabled = …, onClick = …): Compose's names;
// indication = null is none (Compose's), as Kompoz's indication = false.
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::clickable(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::OnClick, n::Enabled, n::InteractionSource, n::Indication, detail::Block>;
    static_assert(P::check<A...>());
    ClickableOptions options;
    P::set<n::Enabled>(options.enabled, args...);
    if constexpr (P::given<n::InteractionSource, A...>()) {  // (interactionSource = null: none, as Compose's)
        auto&& source = P::arg<n::InteractionSource>(args...);
        if constexpr (!requires { std::remove_cvref_t<decltype(source)>::kompozNull; }) options.interactionSource = source;
    }
    if constexpr (P::given<n::Indication, A...>()) {
        auto&& given = P::arg<n::Indication>(args...);
        if constexpr (requires { std::remove_cvref_t<decltype(given)>::kompozNull; }) options.indication = Indication(false);
        else options.indication = Indication(given);
    }
    std::function<void()> onClick;
    if constexpr (P::given<n::OnClick, A...>()) onClick = P::arg<n::OnClick>(args...);
    else onClick = std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...));  // (a click, not content: as it is)
    return clickable(std::move(onClick), options);
}

// onClick(enabled = …, matcher = …, keyboardModifiers = { … }, onLongClick = …, onDoubleClick = …) { }: the click last.
template <class... A>
Modifier Modifier::onClickBy(CallSite site, A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::Enabled, n::Matcher, n::KeyboardModifiers, n::OnLongClick, n::OnDoubleClick, n::OnClick, detail::Block>;
    static_assert(P::check<A...>());
    OnClickOptions options;
    P::set<n::Enabled>(options.enabled, args...);
    P::set<n::Matcher>(options.matcher, args...);
    P::set<n::KeyboardModifiers>(options.keyboardModifiers, args...);
    P::set<n::OnLongClick>(options.onLongClick, args...);
    P::set<n::OnDoubleClick>(options.onDoubleClick, args...);
    std::function<void()> onClick;
    if constexpr (P::given<n::OnClick, A...>()) onClick = P::arg<n::OnClick>(args...);
    else onClick = std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...));
    return this->onClick(std::move(onClick), std::move(options), site);
}

// onFirstVisible(minDurationMs = …, minFractionVisible = …, viewportBounds = …) { }: the callback last.
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::onFirstVisible(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::MinDurationMs, n::MinFractionVisible, n::ViewportBounds, detail::Block>;
    static_assert(P::check<A...>());
    VisibilityOptions options;
    P::set<n::MinDurationMs>(options.minDurationMs, args...);
    P::set<n::MinFractionVisible>(options.minFractionVisible, args...);
    P::set<n::ViewportBounds>(options.viewport, args...);
    return onFirstVisible(std::function<void()>(std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...))), options);
}

// approachLayout(isMeasurementApproachInProgress = { … }, approachMeasure = { … }).
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::approachLayout(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::IsMeasurementApproachInProgress, n::ApproachMeasure>;
    static_assert(P::check<A...>());
    return approachLayout(std::function<bool(Size)>(P::arg<n::IsMeasurementApproachInProgress>(args...)),
                          std::function<Size(const ApproachMeasureScope&, const MeasureNext&, const Constraints&, Offset&)>(P::arg<n::ApproachMeasure>(args...)));
}

// pullToRefresh(isRefreshing = …, state = …, onRefresh = …, enabled = …, threshold = …).
template <class... A>
Modifier Modifier::pullToRefreshBy(CallSite site, A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::IsRefreshing, n::State, n::OnRefresh, n::Enabled, n::Threshold>;
    static_assert(P::check<A...>());
    return pullToRefresh(bool(P::arg<n::IsRefreshing>(args...)), P::arg<n::State>(args...), std::function<void()>(P::arg<n::OnRefresh>(args...)),
                         P::get<n::Enabled>(true, args...), P::get<n::Threshold>(80.f, args...), site);
}

// transformable(state, canPan = { … }, lockRotationOnZoomPan = …, enabled = …).
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::transformable(const TransformableState& state, A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::CanPan, n::LockRotationOnZoomPan, n::Enabled>;
    static_assert(P::check<A...>());
    return transformable(state, P::get<n::CanPan>(std::function<bool(Offset)>(), args...), P::get<n::LockRotationOnZoomPan>(false, args...),
                         P::get<n::Enabled>(true, args...));
}

template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::verticalScroll(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::State, n::Enabled, n::ReverseScrolling>;
    static_assert(P::check<A...>());
    return verticalScroll(P::arg<n::State>(args...), P::get<n::Enabled>(true, args...), P::get<n::ReverseScrolling>(false, args...));
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::horizontalScroll(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::State, n::Enabled, n::ReverseScrolling>;
    static_assert(P::check<A...>());
    return horizontalScroll(P::arg<n::State>(args...), P::get<n::Enabled>(true, args...), P::get<n::ReverseScrolling>(false, args...));
}

// onVisibilityChanged(minDurationMs = …, minFractionVisible = …, viewportBounds = …) { visible -> }: the callback last.
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::onVisibilityChanged(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::MinDurationMs, n::MinFractionVisible, n::ViewportBounds, detail::BlockWith<bool>>;
    static_assert(P::check<A...>());
    VisibilityOptions options;
    P::set<n::MinDurationMs>(options.minDurationMs, args...);
    P::set<n::MinFractionVisible>(options.minFractionVisible, args...);
    P::set<n::ViewportBounds>(options.viewport, args...);
    return onVisibilityChanged(std::function<void(bool)>(std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...))), options);
}

// draggable2D(state, startDragImmediately = …, …): Compose's names over Draggable2DOptions.
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Modifier Modifier::draggable2D(const Draggable2DState& state, A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::Enabled, n::InteractionSource, n::StartDragImmediately, n::OnDragStarted, n::OnDragStopped, n::ReverseDirection>;
    static_assert(P::check<A...>());
    Draggable2DOptions options;
    P::set<n::Enabled>(options.enabled, args...);
    P::set<n::InteractionSource>(options.interactionSource, args...);
    P::set<n::StartDragImmediately>(options.startDragImmediately, args...);
    P::set<n::OnDragStarted>(options.onDragStarted, args...);
    P::set<n::OnDragStopped>(options.onDragStopped, args...);
    P::set<n::ReverseDirection>(options.reverseDirection, args...);
    return draggable2D(state, options);
}

// drawText(measurer, text, topLeft = …, style = …): the style a TextStyle, or a typography style (a Font) as the style's font.
template <class S, class... A>
    requires(detail::anyNamedArgument<A...>)
void DrawScope::drawText(const TextLayoutMeasurer& measurer, const S& text, A&&... args) {
    namespace n = detail::names;
    using P = detail::Params<n::TopLeft, n::Style, n::Overflow, n::SoftWrap, n::MaxLines, n::Size, n::BlendMode>;
    static_assert(P::check<A...>());
    TextStyle style;
    if constexpr (P::given<n::Style, A...>()) {
        auto&& given = P::arg<n::Style>(args...);
        if constexpr (std::is_convertible_v<decltype(given), Font>) style.font = Font(given);
        else style = TextStyle(given);
    }
    drawText(measurer, text, P::get<n::TopLeft>(Offset{}, args...), style, P::get<n::Overflow>(std::optional<TextOverflow>(), args...),
             P::get<n::SoftWrap>(std::optional<bool>(), args...), P::get<n::MaxLines>(std::optional<int>(), args...),
             P::get<n::Size>(std::optional<Size>(), args...), P::get<n::BlendMode>(BlendMode::SrcOver, args...));
}

// record(layer, size = …) { … }: the recording's block last.
template <class... A>
    requires(detail::anyNamedArgument<A...>)
void DrawScope::record(const GraphicsLayer& layer, A&&... args) {
    namespace n = detail::names;
    using P = detail::Params<n::Size, detail::BlockWith<DrawScope&>>;
    static_assert(P::check<A...>());
    record(layer, detail::drawBlock(std::forward<A>(args)...), P::get<n::Size>(std::optional<Size>(), args...));
}

} // namespace kompoz
