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
