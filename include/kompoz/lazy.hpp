#pragma once

// LazyColumn / LazyRow: lists that compose and lay out only the items in view.
//
//   LazyColumn(Modifier().fillMaxSize(), verticalArrangement = Arrangement::spacedBy(8), [&] {
//       item([&] { Text("Başlık"); });
//       items(todos.get(), [](const Todo& t) { return t.id; }, [&](const Todo& t) { TodoRow(t); });
//   });
//
// The item lambdas run during composition only, so they may capture by reference like a
// Column's content. Which items are composed is decided from the scroll position and the
// item sizes measured so far; when scrolling needs items that aren't composed, the list
// recomposes (in the same frame) with the new window.

#include "animation.hpp"
#include "layout.hpp"
#include "named.hpp"
#include "runtime.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kompoz {

// What the last layout showed (Compose's LazyListItemInfo / LazyListLayoutInfo).
struct LazyListItemInfo {
    int index = 0;
    size_t key = 0;          // the item's key, hashed as LazyListScope keeps it
    float offset = 0;        // dp along the list from where its content starts (past contentPadding); negative: partly scrolled out
    float size = 0;          // dp along the list
    size_t contentType = 0;  // the contentType it was given (hashed); 0: none
    friend bool operator==(const LazyListItemInfo&, const LazyListItemInfo&) = default;
};
struct LazyListLayoutInfo {
    std::vector<LazyListItemInfo> visibleItemsInfo;  // by index (a pinned sticky header first)
    // The part of the content in view, in the items' offsets: from -beforeContentPadding (the
    // padding shows too) to the viewport's size less that.
    float viewportStartOffset = 0, viewportEndOffset = 0;
    int totalItemsCount = 0;
    Size viewportSize;
    Orientation orientation = Orientation::Vertical;
    bool reverseLayout = false;  // then offsets go from the bottom (a LazyRow's right end) up
    float beforeContentPadding = 0, afterContentPadding = 0, mainAxisItemSpacing = 0;
    friend bool operator==(const LazyListLayoutInfo&, const LazyListLayoutInfo&) = default;
};

class LazyListState {
public:
    LazyListState();

    // Observable: reading it in a composable recomposes it when the first visible item changes.
    int firstVisibleItemIndex() const;
    // How far (dp) the first visible item is scrolled out of view. Not observable.
    float firstVisibleItemScrollOffset() const;
    int totalItemsCount() const;
    bool canScrollBackward() const;
    bool canScrollForward() const;

    void scrollToItem(int index, float offset = 0) const;
    // Scrolls `delta` dp towards the end; returns what was used.
    float scrollBy(float delta) const;
    // Animated, in a coroutine (Compose's animateScrollToItem): a far item is jumped close to
    // first, then the list glides there by a spring, heading for where the item really is once it
    // is laid out. Near the end it stops where the list ends. A drag, a fling or another
    // animated scroll stops it.
    Task animateScrollToItem(int index, float offset = 0) const;
    Task animateScrollBy(float delta) const;
    Task animateScrollBy(float delta, const AnimationSpec& spec) const;
    void stopScroll() const;  // stops a running animated scroll
    // Observable: dragged or flung by the user, or an animated scroll running (Compose's).
    bool isScrollInProgress() const;
    // Hears the user's drags of the list (collectIsDraggedAsState()).
    const MutableInteractionSource& interactionSource() const;
    // Observable, after each layout: the items in view, where and how big. It changes as the
    // list scrolls: read it in derivedStateOf or snapshotFlow to recompose only on what you need
    // ("is the last item in view": load more).
    LazyListLayoutInfo layoutInfo() const;

    friend bool operator==(const LazyListState& a, const LazyListState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};

template <>
struct Saver<LazyListState> {
    static SavedValue save(const LazyListState& s) {
        return SavedValue::List{s.data_ ? int64_t(s.firstVisibleItemIndex()) : 0, double(s.firstVisibleItemScrollOffset())};
    }
    static std::optional<LazyListState> restore(const SavedValue& v) {
        if (!v.list() || v.list()->size() != 2) return std::nullopt;
        auto index = (*v.list())[0].integer();
        auto offset = (*v.list())[1].number();
        if (!index || !offset) return std::nullopt;
        LazyListState s;
        s.scrollToItem(int(*index), float(*offset));
        return s;
    }
};

LazyListState rememberLazyListState(CallSite site = CallSite::current());
// Opening at an item, as Compose's rememberLazyListState(initialFirstVisibleItemIndex).
LazyListState rememberLazyListState(int initialFirstVisibleItemIndex, float initialFirstVisibleItemScrollOffset = 0,
                                    CallSite site = CallSite::current());

namespace detail {
template <class L>
constexpr bool isVectorOf = false;
template <class T, class A>
constexpr bool isVectorOf<std::vector<T, A>> = true;
template <class L>
concept isVector = isVectorOf<std::remove_cvref_t<L>>;
// A SnapshotStateList (Compose's mutableStateListOf): its items() are the vector read.
template <class L>
concept isSnapshotList = requires(const std::remove_cvref_t<L>& l) { requires isVectorOf<std::remove_cvref_t<decltype(l.items())>>; };
template <class L>
concept isList = isVector<L> || isSnapshotList<L>;
template <class L>
using ElementOf = std::remove_cvref_t<decltype(*std::begin(std::declval<const std::remove_cvref_t<L>&>()))>;
template <class F>
struct SpanArg;
template <class A>
constexpr bool isSpanArg = false;
template <class F>
constexpr bool isSpanArg<SpanArg<F>> = true;
template <class... Args>
constexpr bool lastIsSpanArg() {
    if constexpr (sizeof...(Args) == 0) return false;
    else return isSpanArg<std::remove_cvref_t<std::tuple_element_t<sizeof...(Args) - 1, std::tuple<Args...>>>>;
}
} // namespace detail
class LazyListScope {
public:
    void item(const Content& content);
    template <class K>
    void item(const K& key, const Content& content) {
        add(1, [k = explicitKey(key)](int) { return k; }, [content](int) { content(); });
    }

    // items(count) { index -> … }: the count's own forms, tried only for a count (a list's content,
    // a generic lambda, isn't made into a function of an int to see if this one fits: compilers
    // that build its body for that would fail on it).
    template <class N, class F>
        requires std::integral<N> && std::is_constructible_v<std::function<void(int)>, F>
    void items(N count, F&& content) {
        countItems(int(count), std::function<void(int)>(std::forward<F>(content)));
    }
    template <class N, class K, class F>
        requires std::integral<N> && std::is_constructible_v<std::function<size_t(int)>, K> &&
                 std::is_constructible_v<std::function<void(int)>, F>
    void items(N count, K&& key, F&& content) {
        countItems(int(count), std::function<size_t(int)>(std::forward<K>(key)), std::function<void(int)>(std::forward<F>(content)));
    }
    // With a contentType per item (Compose's): told back in layoutInfo(); items aren't reused
    // between types here, so it changes nothing else.
    template <class N, class K, class T, class F>
        requires std::integral<N> && std::is_constructible_v<std::function<size_t(int)>, K> &&
                 std::is_constructible_v<std::function<size_t(int)>, T> && std::is_constructible_v<std::function<void(int)>, F>
    void items(N count, K&& key, T&& contentType, F&& content) {
        countItems(int(count), std::function<size_t(int)>(std::forward<K>(key)), std::function<size_t(int)>(std::forward<T>(contentType)),
                   std::function<void(int)>(std::forward<F>(content)));
    }
    template <class K, class C>
    void item(const K& key, const C& contentType, const Content& content) {
        add(1, [k = explicitKey(key)](int) { return k; }, [content](int) { content(); }, false,
            [t = typeHash(contentType)](int) { return t; });
    }
    template <class L, class KeyFn, class TypeFn, class F>
        requires detail::isList<L> && std::invocable<F&, const detail::ElementOf<L>&>
    void items(L&& list, KeyFn&& key, TypeFn&& contentType, F&& content) {
        auto l = held(std::forward<L>(list));
        add(int(l->size()), [l, key](int i) { return explicitKey(key((*l)[size_t(i)])); },
            [l, content](int i) { content((*l)[size_t(i)]); }, false,
            [l, contentType](int i) { return typeHash(contentType((*l)[size_t(i)])); });
    }

    // items(list, [](const T& t) { ... })
    template <class L, class F>
        requires detail::isList<L> && std::invocable<F&, const detail::ElementOf<L>&>
    void items(L&& list, F&& content) {
        auto l = held(std::forward<L>(list));
        int base = size();
        add(int(l->size()), [base](int i) { return defaultKey(base + i); },
            [l, content](int i) { content((*l)[size_t(i)]); });
    }

    // items(list, key, content): the key keeps an item's state when the list changes.
    template <class L, class KeyFn, class F>
        requires detail::isList<L> && std::invocable<F&, const detail::ElementOf<L>&>
    void items(L&& list, KeyFn&& key, F&& content) {
        auto l = held(std::forward<L>(list));
        add(int(l->size()), [l, key](int i) { return explicitKey(key((*l)[size_t(i)])); },
            [l, content](int i) { content((*l)[size_t(i)]); });
    }

    template <class L, class F>
        requires detail::isList<L>
    void itemsIndexed(L&& list, F&& content) {
        auto l = held(std::forward<L>(list));
        int base = size();
        add(int(l->size()), [base](int i) { return defaultKey(base + i); },
            [l, content](int i) { content(i, (*l)[size_t(i)]); });
    }

    // A header that stays at the top of the list while the items after it scroll under it,
    // until the next header pushes it out (LazyColumn and LazyRow).
    void stickyHeader(const Content& content);
    template <class K>
    void stickyHeader(const K& key, const Content& content) {
        add(1, [k = explicitKey(key)](int) { return k; }, [content](int) { content(); }, true);
    }

    void countItems(int count, const std::function<void(int)>& content);
    void countItems(int count, const std::function<size_t(int)>& key, const std::function<void(int)>& content);
    void countItems(int count, const std::function<size_t(int)>& key, const std::function<size_t(int)>& contentType,
                    const std::function<void(int)>& content);

    // The list the items read later: one given by name is read where it is (it lives on: a
    // parameter, a state's value); a temporary (items(listOf("a", "b"))) is kept here.
    template <class L>
    static auto held(L&& list) {
        using V = std::remove_cvref_t<L>;
        if constexpr (detail::isSnapshotList<L>) {  // (its items as they are now: read here, so the list recomposes when they change)
            using Items = std::remove_cvref_t<decltype(list.items())>;
            return std::make_shared<const Items>(list.items());
        } else if constexpr (std::is_lvalue_reference_v<L>) {
            return std::shared_ptr<const V>(std::shared_ptr<const V>(), &list);
        } else {
            return std::make_shared<const V>(std::move(list));
        }
    }

    int size() const;
    size_t keyAt(int index) const;
    size_t contentTypeAt(int index) const;
    template <class C>
    static size_t typeHash(const C& contentType) {
        if constexpr (std::is_convertible_v<const C&, std::string_view>) return std::hash<std::string_view>{}(std::string_view(contentType));
        else return std::hash<C>{}(contentType);
    }
    void composeAt(int index) const;
    std::vector<int> stickyIndices() const;

private:
    struct Interval {
        int start, count;
        std::function<size_t(int)> key;
        std::function<void(int)> content;
        bool sticky = false;
        std::function<size_t(int)> contentType;
    };
    void add(int count, std::function<size_t(int)> key, std::function<void(int)> content, bool sticky = false,
             std::function<size_t(int)> contentType = {});
    static size_t defaultKey(int index);
    template <class K>
    static size_t explicitKey(const K& key) {
        return detail::mixKey(0x5eed5eedULL, typeHash(key));  // "başlık" as a key: its text
    }
    std::vector<Interval> intervals_;
};

class LazyGridScope;
namespace detail {
// The scope the running lazy layout's block adds to.
struct LazyReceiver {
    LazyListScope* list = nullptr;
    LazyGridScope* grid = nullptr;
};
LazyReceiver& lazyReceiver();
[[noreturn]] void outsideLazyBlock(const char* function);
// While a block runs: its scope the current one (and the one before it back after).
class LazyReceiverScope {
public:
    explicit LazyReceiverScope(LazyListScope& list) : saved_(lazyReceiver()) { lazyReceiver() = {&list, nullptr}; }
    explicit LazyReceiverScope(LazyGridScope& grid) : saved_(lazyReceiver()) { lazyReceiver() = {nullptr, &grid}; }
    ~LazyReceiverScope() { lazyReceiver() = saved_; }
    LazyReceiverScope(const LazyReceiverScope&) = delete;
    LazyReceiverScope& operator=(const LazyReceiverScope&) = delete;

private:
    LazyReceiver saved_;
};
template <class Scope, class Block>
std::function<void(Scope&)> lazyContent(Block&& block) {
    return [block = std::forward<Block>(block)](Scope& scope) {
        LazyReceiverScope current(scope);
        block();
    };
}
} // namespace detail

// How a fling of a list ends (LazyListOptions::flingBehavior): freely, or settling with an item
// lined up — the one nearest to where the fling would have stopped (rememberSnapFlingBehavior).
struct FlingBehavior {
    bool snap = false;
    SnapPosition snapPosition = SnapPosition::Center;
    DecayAnimationSpec decay = splineBasedDecay();  // how far a fling would go
};
// Compose's rememberSnapFlingBehavior(lazyListState, snapPosition): a LazyRow of cards that
// settles on a card.
inline FlingBehavior rememberSnapFlingBehavior(const LazyListState& = {}, SnapPosition snapPosition = SnapPosition::Center) {
    return {true, snapPosition, splineBasedDecay()};
}

struct LazyListOptions {
    std::optional<LazyListState> state;
    PaddingValues contentPadding;
    float spacing = 0;           // between items, like Arrangement.spacedBy
    float crossAxisBias = -1;    // start (-1), center (0) or end (1) for items narrower than the list
    // Item 0 at the bottom (a LazyRow's right end), the others above it, as in a chat; scrolling
    // goes the other way too. Indices and the state stay as they are (firstVisibleItemIndex 0:
    // the bottom item); contentPadding stays where it is on the screen.
    bool reverseLayout = false;
    std::optional<FlingBehavior> flingBehavior;
    // false: scrolled only in code (scrollToItem, animateScrollToItem) and to bring something into
    // view, not by the user (Compose's userScrollEnabled).
    bool userScrollEnabled = true;
    // Along the list (Compose's verticalArrangement / horizontalArrangement), in place of
    // `spacing`: its spacing between items and, when they all fit, where they sit on the screen
    // (Arrangement::Bottom, Center, SpaceBetween, spacedBy(8, Kind::Center)…).
    std::optional<Arrangement> arrangement;
    // Its overscroll effect (Compose's overscrollEffect): unset, the stretch
    // (rememberOverscrollEffect()); nullptr, none; or your own.
    std::optional<std::shared_ptr<OverscrollEffect>> overscrollEffect;
};

using LazyListContent = std::function<void(LazyListScope&)>;


namespace detail {
void LazyColumn(const Modifier& modifier, const LazyListContent& content, CallSite site = CallSite::current());
void LazyColumn(const Modifier& modifier, const LazyListOptions& options, const LazyListContent& content,
                CallSite site = CallSite::current());
struct LazyColumnImpl {
    using P = Params<names::Modifier, names::State, names::ContentPadding, names::ReverseLayout, names::VerticalArrangement, names::HorizontalAlignment, names::FlingBehavior, names::UserScrollEnabled, names::OverscrollEffect, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: LazyColumn takes a block: its items (item(…), items(…))");
        LazyListOptions options;
        P::set<names::State>(options.state, args...);
        P::set<names::ContentPadding>(options.contentPadding, args...);
        P::set<names::ReverseLayout>(options.reverseLayout, args...);
        P::set<names::FlingBehavior>(options.flingBehavior, args...);
        P::set<names::UserScrollEnabled>(options.userScrollEnabled, args...);
        setOverscroll<P>(options.overscrollEffect, args...);
        P::set<names::VerticalArrangement>(options.arrangement, args...);
        if constexpr (P::given<names::HorizontalAlignment, A...>()) options.crossAxisBias = P::get<names::HorizontalAlignment>(HorizontalAlignment{}, args...).bias;
        LazyColumn(P::get<names::Modifier>(Modifier{}, args...), options, lazyContent<LazyListScope>(std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...))), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::LazyColumnImpl> LazyColumn;
namespace detail {
void LazyRow(const Modifier& modifier, const LazyListContent& content, CallSite site = CallSite::current());
void LazyRow(const Modifier& modifier, const LazyListOptions& options, const LazyListContent& content,
             CallSite site = CallSite::current());
struct LazyRowImpl {
    using P = Params<names::Modifier, names::State, names::ContentPadding, names::ReverseLayout, names::HorizontalArrangement, names::VerticalAlignment, names::FlingBehavior, names::UserScrollEnabled, names::OverscrollEffect, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: LazyRow takes a block: its items (item(…), items(…))");
        LazyListOptions options;
        P::set<names::State>(options.state, args...);
        P::set<names::ContentPadding>(options.contentPadding, args...);
        P::set<names::ReverseLayout>(options.reverseLayout, args...);
        P::set<names::FlingBehavior>(options.flingBehavior, args...);
        P::set<names::UserScrollEnabled>(options.userScrollEnabled, args...);
        setOverscroll<P>(options.overscrollEffect, args...);
        P::set<names::HorizontalArrangement>(options.arrangement, args...);
        if constexpr (P::given<names::VerticalAlignment, A...>()) options.crossAxisBias = P::get<names::VerticalAlignment>(VerticalAlignment{}, args...).bias;
        LazyRow(P::get<names::Modifier>(Modifier{}, args...), options, lazyContent<LazyListScope>(std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...))), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::LazyRowImpl> LazyRow;

// ---- LazyVerticalGrid ---------------------------------------------------------------------

// Fixed(3): three columns; Adaptive(96): as many columns of at least 96 dp as fit.
struct GridCells {
    enum class Kind { Fixed, Adaptive };
    Kind kind = Kind::Fixed;
    float value = 2;
    static GridCells Fixed(int count) { return {Kind::Fixed, float(count)}; }
    static GridCells Adaptive(float minSize) { return {Kind::Adaptive, minSize}; }
    int count(float width, float spacing) const;
};

// Compose's GridItemSpan: how many cells of its line an item takes; and what a span lambda sees
// (item(span = { GridItemSpan(maxLineSpan) }): a header across the grid).
struct GridItemSpan {
    int currentLineSpan = 1;
};
struct LazyGridItemSpanScope {
    int maxLineSpan = 1;  // the line's cells
};
// Compose's StaggeredGridItemSpan: a staggered grid's item in one lane (SingleLane) or across all
// of them (FullLine, a header): item(span = StaggeredGridItemSpan.FullLine) { … }.
struct StaggeredGridItemSpan {
    int lanes = 1;
    static const StaggeredGridItemSpan SingleLane, FullLine;
};
inline const StaggeredGridItemSpan StaggeredGridItemSpan::SingleLane{1};
inline const StaggeredGridItemSpan StaggeredGridItemSpan::FullLine{1 << 20};
namespace detail {
// A span given by name to item or items, passed on to the grid marked as one: a lambda on the
// span scope (and the item), or a span itself.
template <class F>
struct SpanArg {
    F span;
};
inline int spanCells(GridItemSpan s) { return s.currentLineSpan; }
inline int spanCells(StaggeredGridItemSpan s) { return s.lanes; }
inline int spanCells(int s) { return s; }
template <class F, class... I>
int spanCells(F& f, LazyGridItemSpanScope& scope, I&&... item) {
    if constexpr (std::is_invocable_v<F&, LazyGridItemSpanScope&, I...>) return spanCells(f(scope, std::forward<I>(item)...));
    else if constexpr (std::is_invocable_v<F&, LazyGridItemSpanScope&>) return spanCells(f(scope));  // (the item unused: { GridItemSpan(2) })
    else return spanCells(f);
}
} // namespace detail
class LazyGridScope {
public:
    static constexpr int FullLine = 1 << 20;  // a span as wide as the grid (headers)

    void item(const Content& content, int span = 1) {
        spans_.push_back({items_.size(), 1, span});
        items_.item(content);
    }
    template <class K>
    void item(const K& key, const Content& content, int span = 1) {
        spans_.push_back({items_.size(), 1, span});
        items_.item(key, content);
    }
    template <class... Args>
        requires(detail::lastIsSpanArg<Args...>() || requires(LazyListScope& l, Args&&... a) { l.items(std::forward<Args>(a)...); })
    void items(Args&&... args) {
        if constexpr (detail::lastIsSpanArg<Args...>()) {
            detail::splitLast([&](auto&& span, auto&&... rest) { itemsSpanned(std::move(span.span), std::forward<decltype(rest)>(rest)...); },
                              std::forward<Args>(args)...);
        } else {
            items_.items(std::forward<Args>(args)...);
        }
    }
    // item(content, span) and item(key, content, span) with Compose's span lambda.
    template <class F>
    void item(const Content& content, detail::SpanArg<F> span) {
        spanned(items_.size(), 1, [f = std::move(span.span)](int, int columns) mutable {
            LazyGridItemSpanScope scope{columns};
            return detail::spanCells(f, scope);
        });
        items_.item(content);
    }
    template <class K, class F>
    void item(const K& key, const Content& content, detail::SpanArg<F> span) {
        spanned(items_.size(), 1, [f = std::move(span.span)](int, int columns) mutable {
            LazyGridItemSpanScope scope{columns};
            return detail::spanCells(f, scope);
        });
        items_.item(key, content);
    }

    int size() const { return items_.size(); }
    size_t keyAt(int index) const { return items_.keyAt(index); }
    size_t contentTypeAt(int index) const { return items_.contentTypeAt(index); }
    void composeAt(int index) const { items_.composeAt(index); }
    int spanAt(int index, int columns) const;

private:
    // items(count or list, …, span = { item -> GridItemSpan(…) }): the span of each, by its index or
    // its element (the list kept for it).
    template <class F, class First, class... Rest>
    void itemsSpanned(F span, First&& first, Rest&&... rest) {
        int start = items_.size();
        if constexpr (std::is_integral_v<std::remove_cvref_t<First>>) {
            spanned(start, int(first), [span](int i, int columns) mutable {
                LazyGridItemSpanScope scope{columns};
                return detail::spanCells(span, scope, i);
            });
        } else {
            std::vector<std::remove_cvref_t<decltype(*std::begin(first))>> list(std::begin(first), std::end(first));
            spanned(start, int(list.size()), [span, list](int i, int columns) mutable {
                LazyGridItemSpanScope scope{columns};
                return detail::spanCells(span, scope, list[size_t(i)]);
            });
        }
        items_.items(std::forward<First>(first), std::forward<Rest>(rest)...);
    }
    void spanned(int start, int count, std::function<int(int index, int columns)> span) { spans_.push_back({start, count, 1, std::move(span)}); }
    struct Span {
        int start, count, span;
        std::function<int(int index, int columns)> spanOf;  // (Compose's span lambda: from the item and the line)
    };
    LazyListScope items_;
    std::vector<Span> spans_;
};

// An item in view (Compose's LazyGridItemInfo): its line and cell, where it is and its size.
struct LazyGridItemInfo {
    int index = 0;
    size_t key = 0;
    int row = 0, column = 0;  // a vertical grid's lines are rows, a horizontal one's columns
    Offset offset;            // dp from where the content starts (past contentPadding)
    Size size;
    friend bool operator==(const LazyGridItemInfo& a, const LazyGridItemInfo& b) {
        return a.index == b.index && a.key == b.key && a.row == b.row && a.column == b.column && a.offset.x == b.offset.x &&
               a.offset.y == b.offset.y && a.size == b.size;
    }
};
struct LazyGridLayoutInfo {
    std::vector<LazyGridItemInfo> visibleItemsInfo;  // by index
    float viewportStartOffset = 0, viewportEndOffset = 0;  // along the lines, as LazyListLayoutInfo's
    int totalItemsCount = 0;
    Size viewportSize;
    Orientation orientation = Orientation::Vertical;
    float beforeContentPadding = 0, afterContentPadding = 0, mainAxisItemSpacing = 0;
    int maxSpan = 1;  // cells across a line (the columns of a vertical grid)
    friend bool operator==(const LazyGridLayoutInfo&, const LazyGridLayoutInfo&) = default;
};

// A lazy grid's scroll position by item (Compose's LazyGridState): the first visible item is
// the first of the first line in view, and a scroll to an item goes to its line.
//   auto grid = rememberLazyGridState();
//   LazyVerticalGrid(GridCells::Fixed(3), Modifier(), state = grid, ...);
//   scope.launch([grid]() -> Task { co_await grid.animateScrollToItem(0); });
class LazyGridState {
public:
    LazyGridState();
    int firstVisibleItemIndex() const;  // observable
    float firstVisibleItemScrollOffset() const;
    bool canScrollBackward() const;
    bool canScrollForward() const;
    // Before the grid has laid its items out, it goes there when it does.
    void scrollToItem(int index, float offset = 0) const;
    float scrollBy(float delta) const;
    Task animateScrollToItem(int index, float offset = 0) const;
    Task animateScrollBy(float delta) const;
    void stopScroll() const;
    bool isScrollInProgress() const { return lines().isScrollInProgress(); }  // observable
    const MutableInteractionSource& interactionSource() const;
    LazyGridLayoutInfo layoutInfo() const;  // observable, from the last layout
    const LazyListState& lines() const;     // its lines' (a LazyListState by line, not item)
    friend bool operator==(const LazyGridState& a, const LazyGridState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};

template <>
struct Saver<LazyGridState> {
    static SavedValue save(const LazyGridState& s) {
        return SavedValue::List{int64_t(s.firstVisibleItemIndex()), double(s.firstVisibleItemScrollOffset())};
    }
    static std::optional<LazyGridState> restore(const SavedValue& v) {
        if (!v.list() || v.list()->size() != 2) return std::nullopt;
        auto index = (*v.list())[0].integer();
        auto offset = (*v.list())[1].number();
        if (!index || !offset) return std::nullopt;
        LazyGridState s;
        s.scrollToItem(int(*index), float(*offset));
        return s;
    }
};

LazyGridState rememberLazyGridState(int initialFirstVisibleItemIndex = 0, float initialFirstVisibleItemScrollOffset = 0,
                                    CallSite site = CallSite::current());

struct LazyGridOptions {
    std::optional<LazyGridState> state;
    PaddingValues contentPadding;
    float verticalSpacing = 0, horizontalSpacing = 0;
    // Snapping (rememberSnapFlingBehavior(gridState)): a fling settles with a line of items
    // lined up — a row of a vertical grid, a column of a horizontal one.
    std::optional<FlingBehavior> flingBehavior;
    bool userScrollEnabled = true;  // as LazyListOptions'
    std::optional<std::shared_ptr<OverscrollEffect>> overscrollEffect;  // as LazyListOptions'
};
inline FlingBehavior rememberSnapFlingBehavior(const LazyGridState&, SnapPosition snapPosition = SnapPosition::Center) {
    return {true, snapPosition, splineBasedDecay()};
}

// A lazy grid: rows are composed as they come into view, like LazyColumn items.
namespace detail {
void LazyVerticalGrid(GridCells columns, const Modifier& modifier, const LazyGridOptions& options,
                      const std::function<void(LazyGridScope&)>& content, CallSite site = CallSite::current());
struct LazyVerticalGridImpl {
    using P = Params<names::Columns, names::Modifier, names::State, names::ContentPadding, names::VerticalArrangement, names::HorizontalArrangement, names::FlingBehavior, names::UserScrollEnabled, names::OverscrollEffect, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        GridCells columns = P::arg<names::Columns>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: LazyVerticalGrid takes a block: its items (item(…), items(…))");
        LazyGridOptions options;
        P::set<names::State>(options.state, args...);
        P::set<names::ContentPadding>(options.contentPadding, args...);
        P::set<names::FlingBehavior>(options.flingBehavior, args...);
        P::set<names::UserScrollEnabled>(options.userScrollEnabled, args...);
        setOverscroll<P>(options.overscrollEffect, args...);
        if constexpr (P::given<names::VerticalArrangement, A...>()) options.verticalSpacing = P::get<names::VerticalArrangement>(Arrangement{}, args...).spacing;
        if constexpr (P::given<names::HorizontalArrangement, A...>()) options.horizontalSpacing = P::get<names::HorizontalArrangement>(Arrangement{}, args...).spacing;
        LazyVerticalGrid(columns, P::get<names::Modifier>(Modifier{}, args...), options, lazyContent<LazyGridScope>(std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...))), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::LazyVerticalGridImpl> LazyVerticalGrid;
// The same sideways: `rows` divide the grid's height, columns come into view like LazyRow
// items (a span covers rows). Give it a height.
namespace detail {
void LazyHorizontalGrid(GridCells rows, const Modifier& modifier, const LazyGridOptions& options,
                        const std::function<void(LazyGridScope&)>& content, CallSite site = CallSite::current());
struct LazyHorizontalGridImpl {
    using P = Params<names::Rows, names::Modifier, names::State, names::ContentPadding, names::HorizontalArrangement, names::VerticalArrangement, names::FlingBehavior, names::UserScrollEnabled, names::OverscrollEffect, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        GridCells rows = P::arg<names::Rows>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: LazyHorizontalGrid takes a block: its items (item(…), items(…))");
        LazyGridOptions options;
        P::set<names::State>(options.state, args...);
        P::set<names::ContentPadding>(options.contentPadding, args...);
        P::set<names::FlingBehavior>(options.flingBehavior, args...);
        P::set<names::UserScrollEnabled>(options.userScrollEnabled, args...);
        setOverscroll<P>(options.overscrollEffect, args...);
        if constexpr (P::given<names::VerticalArrangement, A...>()) options.verticalSpacing = P::get<names::VerticalArrangement>(Arrangement{}, args...).spacing;
        if constexpr (P::given<names::HorizontalArrangement, A...>()) options.horizontalSpacing = P::get<names::HorizontalArrangement>(Arrangement{}, args...).spacing;
        LazyHorizontalGrid(rows, P::get<names::Modifier>(Modifier{}, args...), options, lazyContent<LazyGridScope>(std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...))), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::LazyHorizontalGridImpl> LazyHorizontalGrid;

// ---- LazyVerticalStaggeredGrid, LazyHorizontalStaggeredGrid ----------------------------------

using StaggeredGridCells = GridCells;  // Fixed(2): two lanes; Adaptive(160): lanes of at least 160 dp

// An item in view (Compose's LazyStaggeredGridItemInfo): its lane, where it is and its size.
struct LazyStaggeredGridItemInfo {
    int index = 0;
    size_t key = 0;
    int lane = 0;           // a full-line item's is 0
    bool fullLine = false;  // across all lanes
    Offset offset;          // dp from where the content starts (past contentPadding), scrolled
    Size size;
    friend bool operator==(const LazyStaggeredGridItemInfo& a, const LazyStaggeredGridItemInfo& b) {
        return a.index == b.index && a.key == b.key && a.lane == b.lane && a.fullLine == b.fullLine && a.offset.x == b.offset.x &&
               a.offset.y == b.offset.y && a.size == b.size;
    }
};
struct LazyStaggeredGridLayoutInfo {
    std::vector<LazyStaggeredGridItemInfo> visibleItemsInfo;  // by index
    float viewportStartOffset = 0, viewportEndOffset = 0;
    int totalItemsCount = 0;
    Size viewportSize;
    float beforeContentPadding = 0, afterContentPadding = 0, mainAxisItemSpacing = 0;
    int lanes = 1;
    friend bool operator==(const LazyStaggeredGridLayoutInfo&, const LazyStaggeredGridLayoutInfo&) = default;
};

// A staggered grid's scroll position by item (Compose's LazyStaggeredGridState): the first
// visible item is the lowest index in view; a scroll to an item puts its top at the top (items
// not measured yet are placed by the measured ones' average height).
class LazyStaggeredGridState {
public:
    LazyStaggeredGridState();
    int firstVisibleItemIndex() const;  // observable
    float firstVisibleItemScrollOffset() const;
    bool canScrollBackward() const;
    bool canScrollForward() const;
    // Before the grid has laid its items out, it goes there when it does.
    void scrollToItem(int index, float offset = 0) const;
    Task animateScrollToItem(int index, float offset = 0) const;
    // Observable: dragged or flung by the user (its own animateScrollToItem not counted).
    bool isScrollInProgress() const { return scrollState().isScrollInProgress(); }
    LazyStaggeredGridLayoutInfo layoutInfo() const;  // observable, from the last layout
    const ScrollState& scrollState() const;          // in dp
    friend bool operator==(const LazyStaggeredGridState& a, const LazyStaggeredGridState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};

template <>
struct Saver<LazyStaggeredGridState> {
    static SavedValue save(const LazyStaggeredGridState& s) {
        return SavedValue::List{int64_t(s.firstVisibleItemIndex()), double(s.firstVisibleItemScrollOffset())};
    }
    static std::optional<LazyStaggeredGridState> restore(const SavedValue& v) {
        if (!v.list() || v.list()->size() != 2) return std::nullopt;
        auto index = (*v.list())[0].integer();
        auto offset = (*v.list())[1].number();
        if (!index || !offset) return std::nullopt;
        LazyStaggeredGridState s;
        s.scrollToItem(int(*index), float(*offset));
        return s;
    }
};

LazyStaggeredGridState rememberLazyStaggeredGridState(int initialFirstVisibleItemIndex = 0, float initialFirstVisibleItemScrollOffset = 0,
                                                      CallSite site = CallSite::current());

struct LazyStaggeredGridOptions {
    std::optional<LazyStaggeredGridState> state;
    PaddingValues contentPadding;
    float verticalItemSpacing = 0, horizontalSpacing = 0;  // a vertical grid's: between items, between lanes
    float horizontalItemSpacing = 0, verticalSpacing = 0;  // a horizontal grid's
    bool userScrollEnabled = true;  // as LazyListOptions'
    std::optional<std::shared_ptr<OverscrollEffect>> overscrollEffect;  // as LazyListOptions'
};

// Items of their own heights in lanes, each going under the lane that is shortest so far
// (Pinterest-like); they are composed as they come into view. An item with a span of more
// than one (LazyGridScope::FullLine) crosses all lanes, below the longest one, like a header.
namespace detail {
void LazyVerticalStaggeredGrid(StaggeredGridCells columns, const Modifier& modifier, const LazyStaggeredGridOptions& options,
                               const std::function<void(LazyGridScope&)>& content, CallSite site = CallSite::current());
struct LazyVerticalStaggeredGridImpl {
    using P = Params<names::Columns, names::Modifier, names::State, names::ContentPadding, names::VerticalItemSpacing, names::HorizontalArrangement, names::UserScrollEnabled, names::OverscrollEffect, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        StaggeredGridCells columns = P::arg<names::Columns>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: LazyVerticalStaggeredGrid takes a block: its items (item(…), items(…))");
        LazyStaggeredGridOptions options;
        P::set<names::State>(options.state, args...);
        P::set<names::ContentPadding>(options.contentPadding, args...);
        P::set<names::UserScrollEnabled>(options.userScrollEnabled, args...);
        setOverscroll<P>(options.overscrollEffect, args...);
        P::set<names::VerticalItemSpacing>(options.verticalItemSpacing, args...);
        if constexpr (P::given<names::HorizontalArrangement, A...>()) options.horizontalSpacing = P::get<names::HorizontalArrangement>(Arrangement{}, args...).spacing;
        LazyVerticalStaggeredGrid(columns, P::get<names::Modifier>(Modifier{}, args...), options, lazyContent<LazyGridScope>(std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...))), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::LazyVerticalStaggeredGridImpl> LazyVerticalStaggeredGrid;
// The same sideways: items of their own widths in rows, each after the row that is shortest so
// far; it scrolls horizontally (from the right in a right-to-left layout). The state's places,
// offsets and layout info go along x.
namespace detail {
void LazyHorizontalStaggeredGrid(StaggeredGridCells rows, const Modifier& modifier, const LazyStaggeredGridOptions& options,
                                 const std::function<void(LazyGridScope&)>& content, CallSite site = CallSite::current());
struct LazyHorizontalStaggeredGridImpl {
    using P = Params<names::Rows, names::Modifier, names::State, names::ContentPadding, names::VerticalArrangement, names::HorizontalItemSpacing, names::UserScrollEnabled, names::OverscrollEffect, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        StaggeredGridCells rows = P::arg<names::Rows>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: LazyHorizontalStaggeredGrid takes a block: its items (item(…), items(…))");
        LazyStaggeredGridOptions options;
        P::set<names::State>(options.state, args...);
        P::set<names::ContentPadding>(options.contentPadding, args...);
        P::set<names::UserScrollEnabled>(options.userScrollEnabled, args...);
        setOverscroll<P>(options.overscrollEffect, args...);
        P::set<names::HorizontalItemSpacing>(options.horizontalItemSpacing, args...);
        if constexpr (P::given<names::VerticalArrangement, A...>()) options.verticalSpacing = P::get<names::VerticalArrangement>(Arrangement{}, args...).spacing;
        LazyHorizontalStaggeredGrid(rows, P::get<names::Modifier>(Modifier{}, args...), options, lazyContent<LazyGridScope>(std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...))), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::LazyHorizontalStaggeredGridImpl> LazyHorizontalStaggeredGrid;


// ---- A lazy layout's block: its items (Compose's LazyListScope / LazyGridScope receivers) ----
//
// (With <kompoz/blocks.hpp>; without it, the block is the last argument: LazyColumn(m, [&] { … }).)
//
//   LazyColumn(Modifier().fillMaxSize()) {
//       item([=] { Text("Bugün", style = typography().titleSmall); });
//       items(mails, [=](const Mail& m) { MailRow(m); });
//   };
//
// The block runs right away, in the call, adding the items to the layout being called (they are
// composed as they come into view): item, items, itemsIndexed and stickyHeader add to it as its
// scope's functions do. A variable of yours called `items` hides the function: write
// kompoz::items(items, …) there.


namespace detail {
template <class Tag, class A>
auto namedOf(A&& a) {
    if constexpr (std::is_same_v<TagOf<A>, Tag>) return std::forward_as_tuple(std::forward<A>(a).value);
    else return std::tuple<>{};
}
template <class A>
auto unnamedOf(A&& a) {
    if constexpr (!isNamed<A>) return std::forward_as_tuple(std::forward<A>(a));
    else return std::tuple<>{};
}
// Compose's items(list, key = { … }, contentType = { … }) { … }: the named key and content type
// put in their places, after what comes first (the list, the count) and before the content.
template <class F, class... A>
void withLazyNames(F&& f, A&&... args) {
    static_assert(((!isNamed<A> || std::is_same_v<TagOf<A>, names::Key> || std::is_same_v<TagOf<A>, names::ContentType> ||
                    std::is_same_v<TagOf<A>, names::Span>) && ...),
                  "kompoz: a lazy layout's items take key = …, contentType = … and (a grid's) span = … by name");
    auto unnamed = std::tuple_cat(unnamedOf(std::forward<A>(args))...);
    constexpr size_t n = std::tuple_size_v<decltype(unnamed)>;
    static_assert(n >= 1, "kompoz: a lazy layout's items take their content last");
    auto key = std::tuple_cat(namedOf<names::Key>(std::forward<A>(args))...);
    auto type = std::tuple_cat(namedOf<names::ContentType>(std::forward<A>(args))...);
    auto span = std::apply([](auto&&... s) { return std::tuple<SpanArg<std::decay_t<decltype(s)>>...>{SpanArg<std::decay_t<decltype(s)>>{s}...}; },
                           std::tuple_cat(namedOf<names::Span>(std::forward<A>(args))...));
    auto head = [&]<size_t... I>(std::index_sequence<I...>) { return std::forward_as_tuple(std::get<I>(std::move(unnamed))...); }(
        std::make_index_sequence<n - 1>{});
    std::apply(std::forward<F>(f), std::tuple_cat(std::move(head), std::move(key), std::move(type),
                                                  std::forward_as_tuple(std::get<n - 1>(std::move(unnamed))), std::move(span)));
}

template <class... A>
void itemPositional(A&&... args) {
    LazyReceiver& r = lazyReceiver();
    if constexpr (requires(LazyGridScope& g) { g.item(std::forward<A>(args)...); })
        if (r.grid) return r.grid->item(std::forward<A>(args)...);
    if constexpr (requires(LazyListScope& l) { l.item(std::forward<A>(args)...); })
        if (r.list) return r.list->item(std::forward<A>(args)...);
    outsideLazyBlock("item");
}
template <class... A>
void itemsPositional(A&&... args) {
    LazyReceiver& r = lazyReceiver();
    if constexpr (requires(LazyGridScope& g) { g.items(std::forward<A>(args)...); })
        if (r.grid) return r.grid->items(std::forward<A>(args)...);
    if constexpr (requires(LazyListScope& l) { l.items(std::forward<A>(args)...); })
        if (r.list) return r.list->items(std::forward<A>(args)...);
    outsideLazyBlock("items");
}
template <class... A>
void itemsIndexedPositional(A&&... args) {
    if (LazyListScope* l = lazyReceiver().list) return l->itemsIndexed(std::forward<A>(args)...);
    outsideLazyBlock("itemsIndexed");
}
template <class... A>
void stickyHeaderPositional(A&&... args) {
    if (LazyListScope* l = lazyReceiver().list) return l->stickyHeader(std::forward<A>(args)...);
    outsideLazyBlock("stickyHeader");
}

} // namespace detail

template <class... A>
void item(A&&... args) {
    detail::withLazyNames([](auto&&... a) { detail::itemPositional(std::forward<decltype(a)>(a)...); }, std::forward<A>(args)...);
}
template <class... A>
void items(A&&... args) {
    detail::withLazyNames([](auto&&... a) { detail::itemsPositional(std::forward<decltype(a)>(a)...); }, std::forward<A>(args)...);
}
template <class... A>
void itemsIndexed(A&&... args) {
    detail::withLazyNames([](auto&&... a) { detail::itemsIndexedPositional(std::forward<decltype(a)>(a)...); }, std::forward<A>(args)...);
}
template <class... A>
void stickyHeader(A&&... args) {
    detail::withLazyNames([](auto&&... a) { detail::stickyHeaderPositional(std::forward<decltype(a)>(a)...); }, std::forward<A>(args)...);
}

} // namespace kompoz
