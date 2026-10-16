#pragma once

// LazyColumn / LazyRow: lists that compose and lay out only the items in view.
//
//   LazyColumn(Modifier().fillMaxSize(), {.spacing = 8}, [&](LazyListScope& list) {
//       list.item([&] { Text("Başlık"); });
//       list.items(todos.get(), [](const Todo& t) { return t.id; }, [&](const Todo& t) { TodoRow(t); });
//   });
//
// The item lambdas run during composition only, so they may capture by reference like a
// Column's content. Which items are composed is decided from the scroll position and the
// item sizes measured so far; when scrolling needs items that aren't composed, the list
// recomposes (in the same frame) with the new window.

#include "animation.hpp"
#include "layout.hpp"
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

class LazyListScope {
public:
    void item(const Content& content);
    template <class K>
    void item(const K& key, const Content& content) {
        add(1, [k = explicitKey(key)](int) { return k; }, [content](int) { content(); });
    }

    void items(int count, const std::function<void(int)>& content);
    void items(int count, const std::function<size_t(int)>& key, const std::function<void(int)>& content);
    // With a contentType per item (Compose's): told back in layoutInfo(); items aren't reused
    // between types here, so it changes nothing else.
    void items(int count, const std::function<size_t(int)>& key, const std::function<size_t(int)>& contentType,
               const std::function<void(int)>& content);
    template <class K, class C>
    void item(const K& key, const C& contentType, const Content& content) {
        add(1, [k = explicitKey(key)](int) { return k; }, [content](int) { content(); }, false,
            [t = typeHash(contentType)](int) { return t; });
    }
    template <class T, class KeyFn, class TypeFn, class F>
        requires std::invocable<F&, const T&>
    void items(const std::vector<T>& list, KeyFn&& key, TypeFn&& contentType, F&& content) {
        const std::vector<T>* l = &list;
        add(int(list.size()), [l, key](int i) { return explicitKey(key((*l)[size_t(i)])); },
            [l, content](int i) { content((*l)[size_t(i)]); }, false,
            [l, contentType](int i) { return typeHash(contentType((*l)[size_t(i)])); });
    }

    // items(list, [](const T& t) { ... })
    template <class T, class F>
        requires std::invocable<F&, const T&>
    void items(const std::vector<T>& list, F&& content) {
        const std::vector<T>* l = &list;
        int base = size();
        add(int(list.size()), [base](int i) { return defaultKey(base + i); },
            [l, content](int i) { content((*l)[size_t(i)]); });
    }

    // items(list, key, content): the key keeps an item's state when the list changes.
    template <class T, class KeyFn, class F>
        requires std::invocable<F&, const T&>
    void items(const std::vector<T>& list, KeyFn&& key, F&& content) {
        const std::vector<T>* l = &list;
        add(int(list.size()), [l, key](int i) { return explicitKey(key((*l)[size_t(i)])); },
            [l, content](int i) { content((*l)[size_t(i)]); });
    }

    template <class T, class F>
    void itemsIndexed(const std::vector<T>& list, F&& content) {
        const std::vector<T>* l = &list;
        int base = size();
        add(int(list.size()), [base](int i) { return defaultKey(base + i); },
            [l, content](int i) { content(i, (*l)[size_t(i)]); });
    }

    // A header that stays at the top of the list while the items after it scroll under it,
    // until the next header pushes it out (LazyColumn and LazyRow).
    void stickyHeader(const Content& content);
    template <class K>
    void stickyHeader(const K& key, const Content& content) {
        add(1, [k = explicitKey(key)](int) { return k; }, [content](int) { content(); }, true);
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

void LazyColumn(const Modifier& modifier, const LazyListContent& content, CallSite site = CallSite::current());
void LazyColumn(const Modifier& modifier, const LazyListOptions& options, const LazyListContent& content,
                CallSite site = CallSite::current());
void LazyRow(const Modifier& modifier, const LazyListContent& content, CallSite site = CallSite::current());
void LazyRow(const Modifier& modifier, const LazyListOptions& options, const LazyListContent& content,
             CallSite site = CallSite::current());

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
    void items(Args&&... args) {
        items_.items(std::forward<Args>(args)...);
    }

    int size() const { return items_.size(); }
    size_t keyAt(int index) const { return items_.keyAt(index); }
    void composeAt(int index) const { items_.composeAt(index); }
    int spanAt(int index, int columns) const;

private:
    struct Span {
        int start, count, span;
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
//   LazyVerticalGrid(GridCells::Fixed(3), Modifier(), {.state = grid}, ...);
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
void LazyVerticalGrid(GridCells columns, const Modifier& modifier, const LazyGridOptions& options,
                      const std::function<void(LazyGridScope&)>& content, CallSite site = CallSite::current());
// The same sideways: `rows` divide the grid's height, columns come into view like LazyRow
// items (a span covers rows). Give it a height.
void LazyHorizontalGrid(GridCells rows, const Modifier& modifier, const LazyGridOptions& options,
                        const std::function<void(LazyGridScope&)>& content, CallSite site = CallSite::current());

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
void LazyVerticalStaggeredGrid(StaggeredGridCells columns, const Modifier& modifier, const LazyStaggeredGridOptions& options,
                               const std::function<void(LazyGridScope&)>& content, CallSite site = CallSite::current());
// The same sideways: items of their own widths in rows, each after the row that is shortest so
// far; it scrolls horizontally (from the right in a right-to-left layout). The state's places,
// offsets and layout info go along x.
void LazyHorizontalStaggeredGrid(StaggeredGridCells rows, const Modifier& modifier, const LazyStaggeredGridOptions& options,
                                 const std::function<void(LazyGridScope&)>& content, CallSite site = CallSite::current());

} // namespace kompoz
