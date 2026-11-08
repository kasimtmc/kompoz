#pragma once

// Compose's named arguments for Modifier's, Color's and InfiniteTransition's members, as kompozc writes them:
//   Modifier().padding(arg::horizontal = 16, arg::vertical = 8).border(arg::width = 1, arg::color = c, arg::shape = s)
//   color.copy(arg::alpha = 0.5f)
//   transition.animateFloat(arg::initialValue = 0, arg::targetValue = 1, arg::animationSpec = tween(600))
// The names are arg's (kompoz::arg::width, ...): end, x or width as names of all of kompoz's would
// take those words from code that says `using namespace kompoz`.

#include "layout.hpp"
#include "named.hpp"

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
Font Font::copy(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::FontFamily, n::FontWeight, n::FontSize, n::FontStyle, n::LetterSpacing, n::LineHeight>;
    static_assert(P::check<A...>());
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
    return f;
}

template <class... A>
    requires(detail::anyNamedArgument<A...>)
void DrawScope::drawText(const TextLayoutResult& layout, A&&... args) {
    namespace n = detail::names;
    using P = detail::Params<n::TopLeft, n::Color, n::Alpha>;
    static_assert(P::check<A...>());
    drawText(layout, P::get<n::Color>(std::optional<Color>(), args...), P::get<n::TopLeft>(Offset{}, args...), P::get<n::Alpha>(std::optional<float>(), args...));
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

} // namespace kompoz
