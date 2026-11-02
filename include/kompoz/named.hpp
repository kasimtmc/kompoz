#pragma once

// Named arguments, as Kotlin's, and blocks:
//
//   Text("Merhaba", fontSize = 22, color = colors::Red);
//   Column(modifier = Modifier().padding(16), verticalArrangement = Arrangement::spacedBy(8), [=] {
//       Text("İlk");
//   });
//
// A name (kompoz::fontSize, ...) is a constant; `name = value` makes a Named<Tag, V>. A composable
// takes named arguments in any order after its positional ones, each at most once and only the names
// it knows (anything else is a compile error), and a block last. Its block runs in a scope of its
// own: when only what the block read changes, the block alone runs again, so it captures by value
// ([=]), as a Kotlin lambda does; what it captures by reference must outlive the composable.
//
// A composable is a constant of Composable<Impl>: its calls take the call site (a default argument
// can't follow a parameter pack, so there is an overload for each count of arguments) and give it to
// Impl::call(site, args...). <kompoz/blocks.hpp> adds the block form Compose writes:
// Column(modifier = m) { … };

#include "runtime.hpp"

#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace kompoz {

template <class Tag, class V>
struct Named {
    V value;
};

template <class Tag>
struct Param {
    template <class V>
    constexpr Named<Tag, std::decay_t<V>> operator=(V&& value) const {
        return {std::forward<V>(value)};
    }
};

namespace detail {

template <class A>
struct NamedTag {
    using type = void;  // not a named argument
};
template <class Tag, class V>
struct NamedTag<Named<Tag, V>> {
    using type = Tag;
};
template <class A>
using TagOf = typename NamedTag<std::remove_cvref_t<A>>::type;
template <class A>
constexpr bool isNamed = !std::is_void_v<TagOf<A>>;
template <class T, class... L>
constexpr bool oneOf = (std::is_same_v<T, L> || ...);
template <class T, class... A>
constexpr int countOf = (int(std::is_same_v<T, TagOf<A>>) + ... + 0);

// A block: a callable given without a name.
template <class A>
constexpr bool isBlock = !isNamed<A> && std::is_invocable_v<std::remove_cvref_t<A>&>;

template <class... A>
constexpr bool lastIsBlock() {
    if constexpr (sizeof...(A) == 0) return false;
    else return isBlock<std::tuple_element_t<sizeof...(A) - 1, std::tuple<A...>>>;
}

// Whether a call starts with a callable given without a name and still has a block after it: a
// Card(onClick, …, block) that can be clicked.
template <class... A>
constexpr bool startsWithClick() {
    if constexpr (sizeof...(A) < 2) return false;
    else return isBlock<std::tuple_element_t<0, std::tuple<A...>>> && lastIsBlock<A...>();
}

// What a parameter list ends with when its composable takes a block last.
struct Block {};

template <class T, class... L>
constexpr size_t indexOf = [] {
    size_t i = 0;
    bool found = false;
    ((found = found || std::is_same_v<T, L>, i += found ? 0 : 1), ...);
    return i;
}();

// A composable's parameters, in order (Block last if it takes one), as Kotlin's: the arguments given
// without a name fill them in that order, then come named ones in any order, each at most once and
// only names it has, then the block. Anything else is a compile error.
template <class... P>
struct Params {
    static constexpr bool takesBlock = oneOf<Block, P...>;
    static constexpr size_t count = sizeof...(P) - (takesBlock ? 1 : 0);  // (Block isn't one)

    template <class... A>
    static constexpr bool blockGiven() {
        return takesBlock && lastIsBlock<A...>();
    }
    // How many arguments are given without a name (before the first named one).
    template <class... A>
    static constexpr size_t positional() {
        constexpr bool named[] = {isNamed<A>..., true};
        size_t end = sizeof...(A) - (blockGiven<A...>() ? 1 : 0), i = 0;
        while (i < end && !named[i]) ++i;
        return i;
    }
    template <class... A>
    static constexpr bool check() {
        constexpr size_t n = sizeof...(A), named = (size_t(isNamed<A>) + ... + 0), pos = positional<A...>();
        static_assert(pos + named + (blockGiven<A...>() ? 1 : 0) == n,
                      "kompoz: arguments without a name first, then named ones (color = …), then a block");
        static_assert(pos <= count, "kompoz: more arguments without a name than the composable takes");
        static_assert(((!isNamed<A> || oneOf<TagOf<A>, P...>) && ...), "kompoz: a named argument this composable doesn't take");
        static_assert(((!isNamed<A> || countOf<TagOf<A>, A...> == 1) && ...), "kompoz: a named argument given twice");
        static_assert(((!isNamed<A> || indexOf<TagOf<A>, P...> >= pos) && ...), "kompoz: a named argument also given without its name");
        return true;
    }
    // The argument for `Tag` (by its name, or by its place), as a T; `fallback` if not given.
    template <class Tag, class T, class... A>
    static T get(T fallback, A&&... args) {
        constexpr size_t k = indexOf<Tag, P...>, pos = positional<A...>();
        auto all = std::forward_as_tuple(std::forward<A>(args)...);
        T out = std::move(fallback);
        [&]<size_t... I>(std::index_sequence<I...>) {
            (
                [&] {
                    using Ai = std::tuple_element_t<I, std::tuple<A...>>;
                    if constexpr (std::is_same_v<TagOf<Ai>, Tag>) {
                        static_assert(std::is_convertible_v<decltype((std::get<I>(all).value)), T>,
                                      "kompoz: a named argument of a type this composable can't take");
                        out = T(std::forward<Ai>(std::get<I>(all)).value);
                    } else if constexpr (I < pos && I == k) {
                        static_assert(std::is_convertible_v<Ai, T>, "kompoz: an argument without a name of a type its place can't take");
                        out = T(std::forward<Ai>(std::get<I>(all)));
                    }
                }(),
                ...);
        }(std::index_sequence_for<A...>{});
        return out;
    }
    template <class Tag, class... A>
    static constexpr bool given() {
        return countOf<Tag, A...> > 0 || indexOf<Tag, P...> < positional<A...>();
    }
    // `field` set from the argument for `Tag`, if one was given (an options struct filled so).
    template <class Tag, class T, class... A>
    static void set(T& field, A&&... args) {
        if constexpr (given<Tag, A...>()) field = get<Tag>(T(field), args...);
    }
    // The block given last, in a scope of its own; empty if none was.
    template <class... A>
    static Content block(A&&... args);
};

// The block given last, if it was: its content, run in a scope of its own.
Content scopedBlock(Content block);
template <class... A>
Content blockOf(A&&... args) {
    if constexpr (lastIsBlock<A...>()) {
        auto&& last = std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...));
        return scopedBlock(Content(std::forward<decltype(last)>(last)));
    } else {
        return {};
    }
}

template <class... P>
template <class... A>
Content Params<P...>::block(A&&... args) {
    if constexpr (blockGiven<A...>()) return blockOf(std::forward<A>(args)...);
    else return {};
}

// A call made before its block: <kompoz/blocks.hpp>'s Column(…) { … } gives it the block with +.
template <class Impl, class... A>
struct BlockCall {
    CallSite site;
    std::tuple<A&&...> args;
    template <class F>
    decltype(auto) operator+(F&& block) && {
        return std::apply([&](A&&... a) -> decltype(auto) { return Impl::call(site, std::forward<A>(a)..., std::forward<F>(block)); },
                          std::move(args));
    }
};

} // namespace detail

// The names, as Compose's parameters are named (a composable takes those it lists).
namespace detail::names {
struct Modifier {};
struct VerticalArrangement {};
struct HorizontalAlignment {};
struct HorizontalArrangement {};
struct VerticalAlignment {};
struct ContentAlignment {};
struct Color {};
struct FontSize {};
struct FontStyle {};
struct FontWeight {};
struct FontFamily {};
struct LetterSpacing {};
struct TextDecoration {};
struct TextAlign {};
struct LineHeight {};
struct Overflow {};
struct SoftWrap {};
struct MaxLines {};
struct MinLines {};
struct InlineContent {};
struct OnTextLayout {};
struct Style {};
struct AutoSize {};
struct Enabled {};
struct ContainerColor {};
struct ContentColor {};
struct InteractionSource {};
struct Shapes {};
struct ContentPadding {};
struct CheckedContainerColor {};
struct CheckedContentColor {};
struct Tint {};
struct Alignment {};
struct ContentScale {};
struct Alpha {};
struct ColorFilter {};
struct ThumbContent {};
struct OnValueChangeFinished {};
struct SliderColors {};
struct Steps {};
struct ValueFrom {};
struct ValueTo {};
struct Thumb {};
struct Track {};
struct StartThumb {};
struct EndThumb {};
struct RangeTrack {};
struct EndInteractionSource {};
struct ReverseDirection {};
} // namespace detail::names
inline namespace args {
inline constexpr Param<detail::names::Modifier> modifier;
inline constexpr Param<detail::names::VerticalArrangement> verticalArrangement;
inline constexpr Param<detail::names::HorizontalAlignment> horizontalAlignment;
inline constexpr Param<detail::names::HorizontalArrangement> horizontalArrangement;
inline constexpr Param<detail::names::VerticalAlignment> verticalAlignment;
inline constexpr Param<detail::names::ContentAlignment> contentAlignment;
inline constexpr Param<detail::names::Color> color;
inline constexpr Param<detail::names::FontSize> fontSize;
inline constexpr Param<detail::names::FontStyle> fontStyle;
inline constexpr Param<detail::names::FontWeight> fontWeight;
inline constexpr Param<detail::names::FontFamily> fontFamily;
inline constexpr Param<detail::names::LetterSpacing> letterSpacing;
inline constexpr Param<detail::names::TextDecoration> textDecoration;
inline constexpr Param<detail::names::TextAlign> textAlign;
inline constexpr Param<detail::names::LineHeight> lineHeight;
inline constexpr Param<detail::names::Overflow> overflow;
inline constexpr Param<detail::names::SoftWrap> softWrap;
inline constexpr Param<detail::names::MaxLines> maxLines;
inline constexpr Param<detail::names::MinLines> minLines;
inline constexpr Param<detail::names::InlineContent> inlineContent;
inline constexpr Param<detail::names::OnTextLayout> onTextLayout;
inline constexpr Param<detail::names::Style> style;
inline constexpr Param<detail::names::AutoSize> autoSize;
inline constexpr Param<detail::names::Enabled> enabled;
inline constexpr Param<detail::names::ContainerColor> containerColor;
inline constexpr Param<detail::names::ContentColor> contentColor;
inline constexpr Param<detail::names::InteractionSource> interactionSource;
inline constexpr Param<detail::names::Shapes> shapes;
inline constexpr Param<detail::names::ContentPadding> contentPadding;
inline constexpr Param<detail::names::CheckedContainerColor> checkedContainerColor;
inline constexpr Param<detail::names::CheckedContentColor> checkedContentColor;
inline constexpr Param<detail::names::Tint> tint;
inline constexpr Param<detail::names::Alignment> alignment;
inline constexpr Param<detail::names::ContentScale> contentScale;
inline constexpr Param<detail::names::Alpha> alpha;
inline constexpr Param<detail::names::ColorFilter> colorFilter;
inline constexpr Param<detail::names::ThumbContent> thumbContent;
inline constexpr Param<detail::names::OnValueChangeFinished> onValueChangeFinished;
inline constexpr Param<detail::names::SliderColors> sliderColors;
inline constexpr Param<detail::names::Steps> steps;
inline constexpr Param<detail::names::ValueFrom> valueFrom;
inline constexpr Param<detail::names::ValueTo> valueTo;
inline constexpr Param<detail::names::Thumb> thumb;
inline constexpr Param<detail::names::Track> track;
inline constexpr Param<detail::names::StartThumb> startThumb;
inline constexpr Param<detail::names::EndThumb> endThumb;
inline constexpr Param<detail::names::RangeTrack> rangeTrack;
inline constexpr Param<detail::names::EndInteractionSource> endInteractionSource;
inline constexpr Param<detail::names::ReverseDirection> reverseDirection;
} // namespace args

// A composable taking named arguments: each call's arguments and call site go to Impl::call.
template <class Impl>
struct Composable {
    // (One overload for each count of arguments, up to twelve: a default argument can't follow a pack.)
    decltype(auto) operator()(CallSite site = CallSite::current()) const { return Impl::call(site); }
    template <class A1>
    decltype(auto) operator()(A1&& a1, CallSite site = CallSite::current()) const { return Impl::call(site, std::forward<A1>(a1)); }
    template <class A1, class A2>
    decltype(auto) operator()(A1&& a1, A2&& a2, CallSite site = CallSite::current()) const { return Impl::call(site, std::forward<A1>(a1), std::forward<A2>(a2)); }
    template <class A1, class A2, class A3>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, CallSite site = CallSite::current()) const { return Impl::call(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3)); }
    template <class A1, class A2, class A3, class A4>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, CallSite site = CallSite::current()) const { return Impl::call(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4)); }
    template <class A1, class A2, class A3, class A4, class A5>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, CallSite site = CallSite::current()) const { return Impl::call(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, CallSite site = CallSite::current()) const { return Impl::call(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, CallSite site = CallSite::current()) const { return Impl::call(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, CallSite site = CallSite::current()) const { return Impl::call(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, CallSite site = CallSite::current()) const { return Impl::call(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, CallSite site = CallSite::current()) const { return Impl::call(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10, class A11>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, A11&& a11, CallSite site = CallSite::current()) const { return Impl::call(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10), std::forward<A11>(a11)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10, class A11, class A12>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, A11&& a11, A12&& a12, CallSite site = CallSite::current()) const { return Impl::call(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10), std::forward<A11>(a11), std::forward<A12>(a12)); }

    // The call without its block yet, for <kompoz/blocks.hpp>: the block comes after it with +.
    detail::BlockCall<Impl> withBlock(CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple()}; }
    template <class A1>
    detail::BlockCall<Impl, A1> withBlock(A1&& a1, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1))}; }
    template <class A1, class A2>
    detail::BlockCall<Impl, A1, A2> withBlock(A1&& a1, A2&& a2, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2))}; }
    template <class A1, class A2, class A3>
    detail::BlockCall<Impl, A1, A2, A3> withBlock(A1&& a1, A2&& a2, A3&& a3, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3))}; }
    template <class A1, class A2, class A3, class A4>
    detail::BlockCall<Impl, A1, A2, A3, A4> withBlock(A1&& a1, A2&& a2, A3&& a3, A4&& a4, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4))}; }
    template <class A1, class A2, class A3, class A4, class A5>
    detail::BlockCall<Impl, A1, A2, A3, A4, A5> withBlock(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5))}; }
    template <class A1, class A2, class A3, class A4, class A5, class A6>
    detail::BlockCall<Impl, A1, A2, A3, A4, A5, A6> withBlock(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6))}; }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7>
    detail::BlockCall<Impl, A1, A2, A3, A4, A5, A6, A7> withBlock(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7))}; }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8>
    detail::BlockCall<Impl, A1, A2, A3, A4, A5, A6, A7, A8> withBlock(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8))}; }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9>
    detail::BlockCall<Impl, A1, A2, A3, A4, A5, A6, A7, A8, A9> withBlock(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9))}; }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10>
    detail::BlockCall<Impl, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10> withBlock(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10))}; }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10, class A11>
    detail::BlockCall<Impl, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11> withBlock(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, A11&& a11, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10), std::forward<A11>(a11))}; }
};

} // namespace kompoz
