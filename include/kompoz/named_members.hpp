#pragma once

// Compose's named arguments for Modifier's and Color's members, as kompozc writes them:
//   Modifier().padding(arg::horizontal = 16, arg::vertical = 8).border(arg::width = 1, arg::color = c, arg::shape = s)
//   color.copy(arg::alpha = 0.5f)
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

} // namespace kompoz
