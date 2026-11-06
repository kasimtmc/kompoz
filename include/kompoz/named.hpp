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

// Whether a call's first argument is a T (a composable's other form: DropdownMenuItem(checked, …)).
template <class T, class... A>
constexpr bool startsWith() {
    if constexpr (sizeof...(A) == 0) return false;
    else return std::is_same_v<std::remove_cvref_t<std::tuple_element_t<0, std::tuple<A...>>>, T>;
}

// What a parameter list ends with when its composable takes a block last: one called with Args
// (Scaffold's gets its padding), or with nothing.
template <class... Args>
struct BlockWith {};
using Block = BlockWith<>;
template <class P>
constexpr bool isBlockParam = false;
template <class... Args>
constexpr bool isBlockParam<BlockWith<Args...>> = true;
// Whether a call's last argument is a block of `B`'s: a callable given without a name, taking B's Args.
template <class B, class... A>
constexpr bool endsWithBlockOf = false;
template <class... Args, class... A>
constexpr bool endsWithBlockOf<BlockWith<Args...>, A...> = [] {
    if constexpr (sizeof...(A) == 0) return false;
    else {
        using L = std::tuple_element_t<sizeof...(A) - 1, std::tuple<A...>>;
        return !isNamed<L> && std::is_invocable_v<std::remove_cvref_t<L>&, Args...>;
    }
}();

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
    static constexpr bool takesBlock = (isBlockParam<P> || ...);
    static constexpr size_t count = sizeof...(P) - (takesBlock ? 1 : 0);  // (the block isn't one)
    using BlockParam = std::tuple_element_t<sizeof...(P), std::tuple<void, P...>>;  // (when it takes one: last)

    template <class... A>
    static constexpr bool blockGiven() {
        if constexpr (takesBlock) return endsWithBlockOf<BlockParam, A...>;
        else return false;
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
    // Where the argument for `Tag` is: its index among the arguments (by its name, or by its place).
    template <class Tag, class... A>
    static constexpr size_t argIndex() {
        constexpr bool byName[] = {std::is_same_v<TagOf<A>, Tag>..., false};
        for (size_t i = 0; i < sizeof...(A); ++i)
            if (byName[i]) return i;
        return indexOf<Tag, P...>;  // (given by its place: given() says it was)
    }
    // The argument for `Tag`, one the composable needs (onClick, value, ...): as it was given, by its
    // name or by its place.
    template <class Tag, class... A>
    static decltype(auto) arg(A&&... args) {
        static_assert(given<Tag, A...>(), "kompoz: an argument the composable needs is missing");
        constexpr size_t i = argIndex<Tag, A...>();
        using Ai = std::tuple_element_t<i, std::tuple<A...>>;
        auto&& a = std::get<i>(std::forward_as_tuple(std::forward<A>(args)...));
        if constexpr (isNamed<Ai>) return (std::forward<Ai>(a).value);
        else return std::forward<Ai>(a);
    }
    // Whether the call is the form whose first parameter is `Tag`, a T: Tag given by name, or a
    // T given first without one (Icon(painter = p) or Icon(p) for the Painter form).
    template <class Tag, class T, class... A>
    static constexpr bool form() {
        if constexpr (countOf<Tag, A...> > 0) return true;
        else if constexpr (positional<A...>() == 0) return false;
        else return std::is_convertible_v<std::tuple_element_t<0, std::tuple<A...>>, T>;
    }
    // `field` set from the argument for `Tag`, if one was given (an options struct filled so).
    template <class Tag, class T, class... A>
    static void set(T& field, A&&... args) {
        if constexpr (given<Tag, A...>()) field = get<Tag>(T(field), args...);
    }
    // The block given last, in a scope of its own; empty if none was.
    template <class... A>
    static auto block(A&&... args);
    // The block given last as it is, run in its caller's scope (MaterialTheme's content, which
    // C++ callers give capturing by reference): empty if none was.
    template <class... A>
    static Content plainBlock(A&&... args) {
        if constexpr (blockGiven<A...>()) return Content(std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...)));
        else return Content();
    }
};

// Whether a Params lists Tag.
template <class Tag, class Q>
constexpr bool oneOfParams = false;
template <class Tag, class... P>
constexpr bool oneOfParams<Tag, Params<P...>> = oneOf<Tag, P...>;

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

// A block called with arguments: each call in a scope of its own, as a plain block's.
template <class... Args>
struct ScopedBlockWith;
template <class... Args>
struct ScopedBlockWith<BlockWith<Args...>> {
    using type = std::function<void(Args...)>;
    template <class F>
    static std::function<void(Args...)> of(F&& f) {
        return [f = std::function<void(Args...)>(std::forward<F>(f))](Args... a) {
            scopedBlock([f, a...] { f(a...); })();
        };
    }
};

template <class... P>
template <class... A>
auto Params<P...>::block(A&&... args) {
    if constexpr (!takesBlock) {
        return Content();
    } else if constexpr (std::is_same_v<BlockParam, Block>) {
        if constexpr (blockGiven<A...>()) return blockOf(std::forward<A>(args)...);
        else return Content();
    } else {
        using Fn = typename ScopedBlockWith<BlockParam>::type;
        if constexpr (blockGiven<A...>())
            return ScopedBlockWith<BlockParam>::of(std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...)));
        else return Fn();
    }
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
struct ReadOnly {};
struct TextStyle {};
struct Label {};
struct Placeholder {};
struct LeadingIcon {};
struct TrailingIcon {};
struct Prefix {};
struct Suffix {};
struct SupportingText {};
struct IsError {};
struct VisualTransformation {};
struct KeyboardOptions {};
struct KeyboardActions {};
struct SingleLine {};
struct Shape {};
struct TextFieldColors {};
struct LineLimits {};
struct OnSubmit {};
struct InputTransformation {};
struct OutputTransformation {};
struct LabelPosition {};
struct Decorator {};
struct TextObfuscationMode {};
struct TextObfuscationCharacter {};
struct TopBar {};
struct BottomBar {};
struct SnackbarHost {};
struct FloatingActionButton {};
struct FloatingActionButtonPosition {};
struct ContentWindowInsets {};
struct Title {};
struct Subtitle {};
struct NavigationIcon {};
struct Actions {};
struct ExpandedHeight {};
struct CollapsedHeight {};
struct WindowInsets {};
struct ScrolledContainerColor {};
struct NavigationIconContentColor {};
struct TitleContentColor {};
struct ActionIconContentColor {};
struct SubtitleContentColor {};
struct ScrollBehavior {};
struct TitleHorizontalAlignment {};
struct Expanded {};
struct FloatingToolbarColors {};
struct LeadingContent {};
struct HeadlineContent {};
struct OverlineContent {};
struct SupportingContent {};
struct OnLongClick {};
struct TrailingContent {};
struct ShadowElevation {};
struct Action {};
struct DismissAction {};
struct ActionOnNewLine {};
struct ActionColor {};
struct ActionContentColor {};
struct DismissActionContentColor {};
struct Header {};
struct State {};
struct CollapsedWidth {};
struct ExpandedWidth {};
struct Arrangement {};
struct Icon {};
struct RailExpanded {};
struct IconPosition {};
struct HideOnCollapse {};
struct Properties {};
struct ConfirmButton {};
struct DismissButton {};
struct Text {};
struct IconContentColor {};
struct TextContentColor {};
struct CheckedLeadingIcon {};
struct OnClick {};
struct SheetState {};
struct SheetContent {};
struct ScaffoldState {};
struct SheetPeekHeight {};
struct SheetContainerColor {};
struct SheetShadowElevation {};
struct SheetDragHandle {};
struct SheetSwipeEnabled {};
struct DrawerContent {};
struct DrawerState {};
struct GesturesEnabled {};
struct Badge {};
struct Selected {};
struct ReverseLayout {};
struct FlingBehavior {};
struct UserScrollEnabled {};
struct OverscrollEffect {};
struct VerticalItemSpacing {};
struct HorizontalItemSpacing {};
struct PageSize {};
struct BeyondViewportPageCount {};
struct PageSpacing {};
struct PageKey {};
struct SnapPosition {};
struct Checked {};
struct Columns {};
struct Rows {};
struct OnCheckedChange {};
struct OnDismissRequest {};
struct OnExpandedChange {};
struct OnValueChange {};
struct Value {};
struct ImageVector {};
struct Painter {};
struct Bitmap {};
struct ContentDescription {};
struct Data {};
struct Colors {};
struct Saver {};
struct Key {};
struct ContentType {};
struct ColorScheme {};
struct MotionScheme {};
struct Start {};
struct Top {};
struct End {};
struct Bottom {};
struct Horizontal {};
struct Vertical {};
struct All {};
struct Width {};
struct Height {};
struct X {};
struct Y {};
struct Brush {};
struct Border {};
struct Red {};
struct Green {};
struct Blue {};
struct InitialValue {};
struct TargetValue {};
struct AnimationSpec {};
struct Content {};
struct ScaleX {};
struct ScaleY {};
struct TranslationX {};
struct TranslationY {};
struct RotationX {};
struct RotationY {};
struct RotationZ {};
struct CameraDistance {};
struct TransformOrigin {};
struct Clip {};
struct RenderEffect {};
struct AmbientShadowColor {};
struct SpotShadowColor {};
struct CompositingStrategy {};
struct BlendMode {};
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
inline constexpr Param<detail::names::ReadOnly> readOnly;
inline constexpr Param<detail::names::TextStyle> textStyle;
inline constexpr Param<detail::names::Label> label;
inline constexpr Param<detail::names::Placeholder> placeholder;
inline constexpr Param<detail::names::LeadingIcon> leadingIcon;
inline constexpr Param<detail::names::TrailingIcon> trailingIcon;
inline constexpr Param<detail::names::Prefix> prefix;
inline constexpr Param<detail::names::Suffix> suffix;
inline constexpr Param<detail::names::SupportingText> supportingText;
inline constexpr Param<detail::names::IsError> isError;
inline constexpr Param<detail::names::VisualTransformation> visualTransformation;
inline constexpr Param<detail::names::KeyboardOptions> keyboardOptions;
inline constexpr Param<detail::names::KeyboardActions> keyboardActions;
inline constexpr Param<detail::names::SingleLine> singleLine;
inline constexpr Param<detail::names::Shape> shape;
inline constexpr Param<detail::names::TextFieldColors> textFieldColors;
inline constexpr Param<detail::names::LineLimits> lineLimits;
inline constexpr Param<detail::names::OnSubmit> onSubmit;
inline constexpr Param<detail::names::InputTransformation> inputTransformation;
inline constexpr Param<detail::names::OutputTransformation> outputTransformation;
inline constexpr Param<detail::names::LabelPosition> labelPosition;
inline constexpr Param<detail::names::Decorator> decorator;
inline constexpr Param<detail::names::TextObfuscationMode> textObfuscationMode;
inline constexpr Param<detail::names::TextObfuscationCharacter> textObfuscationCharacter;
inline constexpr Param<detail::names::TopBar> topBar;
inline constexpr Param<detail::names::BottomBar> bottomBar;
inline constexpr Param<detail::names::SnackbarHost> snackbarHost;
inline constexpr Param<detail::names::FloatingActionButton> floatingActionButton;
inline constexpr Param<detail::names::FloatingActionButtonPosition> floatingActionButtonPosition;
inline constexpr Param<detail::names::ContentWindowInsets> contentWindowInsets;
inline constexpr Param<detail::names::Title> title;
inline constexpr Param<detail::names::Subtitle> subtitle;
inline constexpr Param<detail::names::NavigationIcon> navigationIcon;
inline constexpr Param<detail::names::Actions> actions;
inline constexpr Param<detail::names::ExpandedHeight> expandedHeight;
inline constexpr Param<detail::names::CollapsedHeight> collapsedHeight;
inline constexpr Param<detail::names::WindowInsets> windowInsets;
inline constexpr Param<detail::names::ScrolledContainerColor> scrolledContainerColor;
inline constexpr Param<detail::names::NavigationIconContentColor> navigationIconContentColor;
inline constexpr Param<detail::names::TitleContentColor> titleContentColor;
inline constexpr Param<detail::names::ActionIconContentColor> actionIconContentColor;
inline constexpr Param<detail::names::SubtitleContentColor> subtitleContentColor;
inline constexpr Param<detail::names::ScrollBehavior> scrollBehavior;
inline constexpr Param<detail::names::TitleHorizontalAlignment> titleHorizontalAlignment;
inline constexpr Param<detail::names::Expanded> expanded;
inline constexpr Param<detail::names::FloatingToolbarColors> floatingToolbarColors;
inline constexpr Param<detail::names::LeadingContent> leadingContent;
inline constexpr Param<detail::names::TrailingContent> trailingContent;
inline constexpr Param<detail::names::ShadowElevation> shadowElevation;
inline constexpr Param<detail::names::Action> action;
inline constexpr Param<detail::names::DismissAction> dismissAction;
inline constexpr Param<detail::names::ActionOnNewLine> actionOnNewLine;
inline constexpr Param<detail::names::ActionColor> actionColor;
inline constexpr Param<detail::names::ActionContentColor> actionContentColor;
inline constexpr Param<detail::names::DismissActionContentColor> dismissActionContentColor;
inline constexpr Param<detail::names::Header> header;
inline constexpr Param<detail::names::State> state;
inline constexpr Param<detail::names::CollapsedWidth> collapsedWidth;
inline constexpr Param<detail::names::ExpandedWidth> expandedWidth;
inline constexpr Param<detail::names::Arrangement> arrangement;
inline constexpr Param<detail::names::Icon> icon;
inline constexpr Param<detail::names::RailExpanded> railExpanded;
inline constexpr Param<detail::names::IconPosition> iconPosition;
inline constexpr Param<detail::names::HideOnCollapse> hideOnCollapse;
inline constexpr Param<detail::names::Properties> properties;
inline constexpr Param<detail::names::ConfirmButton> confirmButton;
inline constexpr Param<detail::names::DismissButton> dismissButton;
inline constexpr Param<detail::names::Text> text;
inline constexpr Param<detail::names::IconContentColor> iconContentColor;
inline constexpr Param<detail::names::TextContentColor> textContentColor;
inline constexpr Param<detail::names::CheckedLeadingIcon> checkedLeadingIcon;
inline constexpr Param<detail::names::OnClick> onClick;
inline constexpr Param<detail::names::SheetState> sheetState;
inline constexpr Param<detail::names::SheetContent> sheetContent;
inline constexpr Param<detail::names::ScaffoldState> scaffoldState;
inline constexpr Param<detail::names::SheetPeekHeight> sheetPeekHeight;
inline constexpr Param<detail::names::SheetContainerColor> sheetContainerColor;
inline constexpr Param<detail::names::SheetShadowElevation> sheetShadowElevation;
inline constexpr Param<detail::names::SheetDragHandle> sheetDragHandle;
inline constexpr Param<detail::names::SheetSwipeEnabled> sheetSwipeEnabled;
inline constexpr Param<detail::names::DrawerContent> drawerContent;
inline constexpr Param<detail::names::DrawerState> drawerState;
inline constexpr Param<detail::names::GesturesEnabled> gesturesEnabled;
inline constexpr Param<detail::names::Badge> badge;
inline constexpr Param<detail::names::Selected> selected;
inline constexpr Param<detail::names::ReverseLayout> reverseLayout;
inline constexpr Param<detail::names::FlingBehavior> flingBehavior;
inline constexpr Param<detail::names::UserScrollEnabled> userScrollEnabled;
inline constexpr Param<detail::names::OverscrollEffect> overscrollEffect;
inline constexpr Param<detail::names::VerticalItemSpacing> verticalItemSpacing;
inline constexpr Param<detail::names::HorizontalItemSpacing> horizontalItemSpacing;
inline constexpr Param<detail::names::PageSize> pageSize;
inline constexpr Param<detail::names::BeyondViewportPageCount> beyondViewportPageCount;
inline constexpr Param<detail::names::PageSpacing> pageSpacing;
inline constexpr Param<detail::names::PageKey> pageKey;
inline constexpr Param<detail::names::SnapPosition> snapPosition;
inline constexpr Param<detail::names::HeadlineContent> headlineContent;
inline constexpr Param<detail::names::OverlineContent> overlineContent;
inline constexpr Param<detail::names::SupportingContent> supportingContent;
inline constexpr Param<detail::names::OnLongClick> onLongClick;
inline constexpr Param<detail::names::Checked> checked;
inline constexpr Param<detail::names::Columns> columns;
inline constexpr Param<detail::names::Rows> rows;
inline constexpr Param<detail::names::OnCheckedChange> onCheckedChange;
inline constexpr Param<detail::names::OnDismissRequest> onDismissRequest;
inline constexpr Param<detail::names::OnExpandedChange> onExpandedChange;
inline constexpr Param<detail::names::OnValueChange> onValueChange;
inline constexpr Param<detail::names::Value> value;
inline constexpr Param<detail::names::ImageVector> imageVector;
inline constexpr Param<detail::names::Painter> painter;
inline constexpr Param<detail::names::Bitmap> bitmap;
inline constexpr Param<detail::names::ContentDescription> contentDescription;
inline constexpr Param<detail::names::Data> data;
} // namespace args
// The names as kompozc writes them (arg::colors = …): all of args', and those that are something
// else of Kompoz's too (colors: the colors' namespace).
namespace arg {
using namespace args;
inline constexpr Param<detail::names::Colors> colors;
inline constexpr Param<detail::names::Saver> saver;
// (kompoz::key(id) { … } is the composable that gives an identity: these are a lazy item's)
inline constexpr Param<detail::names::Key> key;
inline constexpr Param<detail::names::ContentType> contentType;
// (kompoz::colorScheme() and motionScheme() read the theme's: these are MaterialTheme's names)
inline constexpr Param<detail::names::ColorScheme> colorScheme;
inline constexpr Param<detail::names::MotionScheme> motionScheme;
// Members' (Modifier's, Color's, InfiniteTransition's): words too common to be names of all of kompoz's (end, x, width).
inline constexpr Param<detail::names::Start> start;
inline constexpr Param<detail::names::Top> top;
inline constexpr Param<detail::names::End> end;
inline constexpr Param<detail::names::Bottom> bottom;
inline constexpr Param<detail::names::Horizontal> horizontal;
inline constexpr Param<detail::names::Vertical> vertical;
inline constexpr Param<detail::names::All> all;
inline constexpr Param<detail::names::Width> width;
inline constexpr Param<detail::names::Height> height;
inline constexpr Param<detail::names::X> x;
inline constexpr Param<detail::names::Y> y;
inline constexpr Param<detail::names::Brush> brush;
inline constexpr Param<detail::names::Border> border;
inline constexpr Param<detail::names::Red> red;
inline constexpr Param<detail::names::Green> green;
inline constexpr Param<detail::names::Blue> blue;
inline constexpr Param<detail::names::InitialValue> initialValue;
inline constexpr Param<detail::names::TargetValue> targetValue;
inline constexpr Param<detail::names::AnimationSpec> animationSpec;
// (a composable's block, given by name as Compose's content = … : kompoz::Content is its type)
inline constexpr Param<detail::names::Content> content;
inline constexpr Param<detail::names::ScaleX> scaleX;
inline constexpr Param<detail::names::ScaleY> scaleY;
inline constexpr Param<detail::names::TranslationX> translationX;
inline constexpr Param<detail::names::TranslationY> translationY;
inline constexpr Param<detail::names::RotationX> rotationX;
inline constexpr Param<detail::names::RotationY> rotationY;
inline constexpr Param<detail::names::RotationZ> rotationZ;
inline constexpr Param<detail::names::CameraDistance> cameraDistance;
inline constexpr Param<detail::names::TransformOrigin> transformOrigin;
inline constexpr Param<detail::names::Clip> clip;
inline constexpr Param<detail::names::RenderEffect> renderEffect;
inline constexpr Param<detail::names::AmbientShadowColor> ambientShadowColor;
inline constexpr Param<detail::names::SpotShadowColor> spotShadowColor;
inline constexpr Param<detail::names::CompositingStrategy> compositingStrategy;
inline constexpr Param<detail::names::BlendMode> blendMode;
} // namespace arg

// CompositionLocalProvider(LocalX provides x, content = content), with one to three values or a
// CompositionLocalContext.
template <class F>
void CompositionLocalProvider(const detail::Provided& a, const Named<detail::names::Content, F>& content,
                              CallSite site = CallSite::current()) {
    CompositionLocalProvider({a}, Content(content.value), site);
}
template <class F>
void CompositionLocalProvider(const detail::Provided& a, const detail::Provided& b, const Named<detail::names::Content, F>& content,
                              CallSite site = CallSite::current()) {
    CompositionLocalProvider({a, b}, Content(content.value), site);
}
template <class F>
void CompositionLocalProvider(const detail::Provided& a, const detail::Provided& b, const detail::Provided& c,
                              const Named<detail::names::Content, F>& content, CallSite site = CallSite::current()) {
    CompositionLocalProvider({a, b, c}, Content(content.value), site);
}
template <class F>
void CompositionLocalProvider(const CompositionLocalContext& context, const Named<detail::names::Content, F>& content,
                              CallSite site = CallSite::current()) {
    CompositionLocalProvider(context, Content(content.value), site);
}

// Compose's rememberSaveable(saver = …) { … } and rememberSaveable(inputs, saver = …) { … }.
template <class S, class F>
auto rememberSaveable(const Named<detail::names::Saver, CustomSaver<S>>& saver, F&& init, CallSite site = CallSite::current())
    -> std::decay_t<std::invoke_result_t<F&>>& {
    return rememberSaveable(saver.value, std::forward<F>(init), site);
}
template <class K, class S, class F>
auto rememberSaveable(const K& inputs, const Named<detail::names::Saver, CustomSaver<S>>& saver, F&& init,
                      CallSite site = CallSite::current()) -> std::decay_t<std::invoke_result_t<F&>>& {
    return rememberSaveable(inputs, saver.value, std::forward<F>(init), site);
}

namespace detail {
// content = { … } given by name (Compose's content parameter, often passed on, as in
// MaterialTheme(colorScheme = c, content = content)): the block, last, as the composable takes it.
template <class Impl, class... A>
decltype(auto) callImpl(CallSite site, A&&... args) {
    if constexpr (countOf<names::Content, A...> == 0) {
        return Impl::call(site, std::forward<A>(args)...);
    } else {
        static_assert(countOf<names::Content, A...> == 1, "kompoz: content given twice");
        constexpr size_t at = indexOf<names::Content, TagOf<A>...>;
        auto all = std::forward_as_tuple(std::forward<A>(args)...);
        return [&]<size_t... I>(std::index_sequence<I...>) -> decltype(auto) {
            return Impl::call(site, std::get<(I < at ? I : I + 1)>(std::move(all))..., std::get<at>(std::move(all)).value);
        }(std::make_index_sequence<sizeof...(A) - 1>{});
    }
}
} // namespace detail

// A composable taking named arguments: each call's arguments and call site go to Impl::call.
template <class Impl>
struct Composable {
    // (One overload for each count of arguments, up to sixteen: a default argument can't follow a pack.)
    decltype(auto) operator()(CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site); }
    template <class A1>
    decltype(auto) operator()(A1&& a1, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1)); }
    template <class A1, class A2>
    decltype(auto) operator()(A1&& a1, A2&& a2, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2)); }
    template <class A1, class A2, class A3>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3)); }
    template <class A1, class A2, class A3, class A4>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4)); }
    template <class A1, class A2, class A3, class A4, class A5>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10, class A11>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, A11&& a11, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10), std::forward<A11>(a11)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10, class A11, class A12>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, A11&& a11, A12&& a12, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10), std::forward<A11>(a11), std::forward<A12>(a12)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10, class A11, class A12, class A13>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, A11&& a11, A12&& a12, A13&& a13, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10), std::forward<A11>(a11), std::forward<A12>(a12), std::forward<A13>(a13)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10, class A11, class A12, class A13, class A14>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, A11&& a11, A12&& a12, A13&& a13, A14&& a14, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10), std::forward<A11>(a11), std::forward<A12>(a12), std::forward<A13>(a13), std::forward<A14>(a14)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10, class A11, class A12, class A13, class A14, class A15>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, A11&& a11, A12&& a12, A13&& a13, A14&& a14, A15&& a15, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10), std::forward<A11>(a11), std::forward<A12>(a12), std::forward<A13>(a13), std::forward<A14>(a14), std::forward<A15>(a15)); }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10, class A11, class A12, class A13, class A14, class A15, class A16>
    decltype(auto) operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, A11&& a11, A12&& a12, A13&& a13, A14&& a14, A15&& a15, A16&& a16, CallSite site = CallSite::current()) const { return detail::callImpl<Impl>(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10), std::forward<A11>(a11), std::forward<A12>(a12), std::forward<A13>(a13), std::forward<A14>(a14), std::forward<A15>(a15), std::forward<A16>(a16)); }

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
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10, class A11, class A12>
    detail::BlockCall<Impl, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12> withBlock(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, A11&& a11, A12&& a12, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10), std::forward<A11>(a11), std::forward<A12>(a12))}; }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10, class A11, class A12, class A13>
    detail::BlockCall<Impl, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13> withBlock(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, A11&& a11, A12&& a12, A13&& a13, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10), std::forward<A11>(a11), std::forward<A12>(a12), std::forward<A13>(a13))}; }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10, class A11, class A12, class A13, class A14>
    detail::BlockCall<Impl, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, A14> withBlock(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, A11&& a11, A12&& a12, A13&& a13, A14&& a14, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10), std::forward<A11>(a11), std::forward<A12>(a12), std::forward<A13>(a13), std::forward<A14>(a14))}; }
    template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10, class A11, class A12, class A13, class A14, class A15>
    detail::BlockCall<Impl, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, A14, A15> withBlock(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6, A7&& a7, A8&& a8, A9&& a9, A10&& a10, A11&& a11, A12&& a12, A13&& a13, A14&& a14, A15&& a15, CallSite site = CallSite::current()) const { return {site, std::forward_as_tuple(std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5), std::forward<A6>(a6), std::forward<A7>(a7), std::forward<A8>(a8), std::forward<A9>(a9), std::forward<A10>(a10), std::forward<A11>(a11), std::forward<A12>(a12), std::forward<A13>(a13), std::forward<A14>(a14), std::forward<A15>(a15))}; }
};

} // namespace kompoz
