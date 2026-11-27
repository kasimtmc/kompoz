#pragma once

// Lists the user reorders by dragging their items. Compose has none; this is the API of the
// library Compose apps use for it (Reorderable, sh.calvin.reorderable):
//
//   val songs = remember { mutableStateListOf("Bir", "İki", "Üç") }
//   val listState = rememberLazyListState()
//   val reorderState = rememberReorderableLazyListState(listState) { from, to ->
//       songs.add(to.index, songs.removeAt(from.index))
//   }
//   LazyColumn(state = listState) {
//       items(songs, key = { it }) { song ->
//           ReorderableItem(reorderState, key = song) { isDragging ->
//               Row {
//                   Text(song, modifier = Modifier.weight(1f))
//                   Icon(Icons.Default.DragHandle, "Sırala", modifier = Modifier.draggableHandle())
//               }
//           }
//       }
//   }
//
// The dragged item follows the finger (over the others: drawn above them), and each time it
// passes over another reorderable item onMove(from, to) is called with the two items as the list
// last showed them: move yours there, and the others glide to their new places (animateItem).
// Dragged near an end of the list, the list scrolls (scrollThreshold, past
// scrollThresholdPadding); let go, the item settles into its place.
//
// Modifier.draggableHandle() starts a drag where it is pressed and moved (the whole item, if you
// put it there); Modifier.longPressDraggableHandle() after a long press, so that the list still
// scrolls under a finger (the whole item, as a launcher's icons). Only ReorderableItem's content
// has them.
//
// For a short list that isn't lazy, ReorderableColumn and ReorderableRow lay their items out as
// a Column or a Row; the order changes on the screen while dragging and onSettle(fromIndex,
// toIndex) gets the move once the item is let go.
//
// For accessibility services (and the keyboard), give the items custom actions that move them:
//   Modifier.semantics { customActions = listOf(CustomAccessibilityAction("Yukarı taşı") { … ; true }) }

#include "coroutines.hpp"
#include "gestures.hpp"
#include "interaction.hpp"
#include "lazy.hpp"
#include "layout.hpp"
#include "named.hpp"
#include "runtime.hpp"
#include "state.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace kompoz {

// How a handle starts its drag (Reorderable's DragGestureDetector): pressed and moved past the
// touch slop, or held still for the long press timeout first.
enum class DragGestureDetector { Press, LongPress };

// The defaults (Reorderable's ReorderableLazyCollectionDefaults).
struct ReorderableLazyCollectionDefaults {
    static constexpr float ScrollThreshold = 48;  // dp from an end of the list where dragging scrolls it
};

namespace detail {

// An item as a reorderable collection sees it: where the layout last showed it, along the
// collection's offsets (from where its content starts, past the content padding).
struct ReorderablePlace {
    int index = 0;
    size_t key = 0;
    Offset offset;
    Size size;
};

// What a dragged item does, shared by the lazy lists and grids it reorders.
struct ReorderableData : std::enable_shared_from_this<ReorderableData> {
    // The layout's, set by the state (lazy list, grid or staggered grid).
    std::function<std::vector<ReorderablePlace>()> places;  // in view, from the last layout (observable)
    std::function<std::pair<float, float>()> viewport;                  // the offsets in view along the scroll axis
    std::function<float(float)> scrollBy;                               // dp towards the end; what was used
    bool vertical = true;   // the scroll axis
    bool reverse = false;   // offsets go up (a reversed list)
    bool mainAxisOnly = true;  // a list's item moves along it only; a grid's anywhere
    bool rtl = false;       // (set from the layout direction its items are composed in)
    // The user's onMove: started for the dragged item (from) and the one it is over (to); a
    // coroutine (Kotlin's suspend lambda) gives its Task, moves wait for it.
    std::function<std::optional<Task>(const ReorderablePlace& from, const ReorderablePlace& to)> move;
    PaddingValues scrollThresholdPadding;
    float scrollThreshold = ReorderableLazyCollectionDefaults::ScrollThreshold;
    std::optional<CoroutineScope> scope;

    MutableState<std::optional<size_t>> dragging{std::nullopt};  // the key of the item dragged
    MutableState<Offset> dragged{Offset{}};                       // how far, in the layout's offsets
    Offset start;                                                 // where it was when the drag began
    MutableState<std::optional<size_t>> settling{std::nullopt};   // let go, gliding into its place
    MutableState<float> settle{0.f};                              // how much of the way it still has: 1 → 0
    Offset settleAt;                                              // where it was let go
    Job settler;
    std::unordered_set<size_t> reorderable;                       // the keys of the ReorderableItems composed, enabled
    bool waiting = false;  // a move made: none other until the layout has shown it
    Job moving, scroller;
    uint64_t drags = 0;    // (a drag's number: what a late callback of an older one leaves alone)

    void begin(size_t key);
    void drag(Offset screenDelta);
    void end();
    void cancel(size_t key);  // its item left the composition while dragged
    // How far the item is drawn from where the layout put it, in dp on the screen (observable).
    Offset translationOf(size_t key) const;
    // Whether the dragged item is over another one to move to, and the scroll near an end.
    void check();

private:
    Offset toLayout(Offset screen) const;
    Offset toScreen(Offset layout) const;
    std::optional<ReorderablePlace> placeOf(size_t key) const;
    float autoScrollSpeed(const ReorderablePlace& item) const;  // dp/s, negative: backwards
    void startScroller();
};

// A ReorderableItem's, for the handles in its content.
struct ReorderableItemState {
    std::shared_ptr<ReorderableData> collection;           // a lazy layout's; none for ReorderableColumn's
    std::shared_ptr<struct ReorderableLinearData> linear;  // a ReorderableColumn's or Row's
    size_t key = 0;   // (a lazy layout's item's)
    int index = -1;   // (a ReorderableColumn's item's: which child it is, from its last layout)
    bool enabled = true;
    std::function<void(Offset)> onDragStarted;  // the handle's, as last composed
    std::function<void()> onDragStopped;
    const void* dragHandle = nullptr;  // the handle dragging it (of two, the one that started first)
};
inline const KompozitionLocal<std::shared_ptr<ReorderableItemState>> LocalReorderableItem{nullptr};

// The handles' gesture.
Modifier reorderableHandle(const Modifier& modifier, bool enabled, std::optional<MutableInteractionSource> interactionSource,
                           std::function<void(Offset)> onDragStarted, std::function<void()> onDragStopped,
                           DragGestureDetector detector);

// onMove as given: (from, to) -> Unit, or a coroutine (-> Task).
template <class Info, class F>
std::function<std::optional<Task>(const Info&, const Info&)> moveFunction(F f) {
    return [f = std::move(f)](const Info& from, const Info& to) mutable -> std::optional<Task> {
        if constexpr (std::is_same_v<std::invoke_result_t<F&, const Info&, const Info&>, Task>) return f(from, to);
        else {
            f(from, to);
            return std::nullopt;
        }
    };
}

} // namespace detail

// A reorderable lazy layout's state: what is being dragged. Made by
// rememberReorderableLazyListState and the grids' versions.
class ReorderableLazyCollectionState {
public:
    // Observable: an item is being dragged (not when it is settling after it was let go).
    bool isAnyItemDragging() const { return data_->dragging.get().has_value(); }
    friend bool operator==(const ReorderableLazyCollectionState& a, const ReorderableLazyCollectionState& b) { return a.data_ == b.data_; }

    std::shared_ptr<detail::ReorderableData> data_;
};
class ReorderableLazyListState : public ReorderableLazyCollectionState {};
class ReorderableLazyGridState : public ReorderableLazyCollectionState {};
class ReorderableLazyStaggeredGridState : public ReorderableLazyCollectionState {};

namespace detail {
ReorderableLazyListState rememberReorderableLazyListState(const LazyListState& lazyListState,
                                                          std::function<std::optional<Task>(const LazyListItemInfo&, const LazyListItemInfo&)> onMove,
                                                          PaddingValues scrollThresholdPadding, float scrollThreshold, CallSite site);
ReorderableLazyGridState rememberReorderableLazyGridState(const LazyGridState& lazyGridState,
                                                          std::function<std::optional<Task>(const LazyGridItemInfo&, const LazyGridItemInfo&)> onMove,
                                                          PaddingValues scrollThresholdPadding, float scrollThreshold, CallSite site);
ReorderableLazyStaggeredGridState rememberReorderableLazyStaggeredGridState(
    const LazyStaggeredGridState& lazyStaggeredGridState,
    std::function<std::optional<Task>(const LazyStaggeredGridItemInfo&, const LazyStaggeredGridItemInfo&)> onMove,
    PaddingValues scrollThresholdPadding, float scrollThreshold, CallSite site);

// rememberReorderableLazyListState(lazyListState, scrollThresholdPadding = …, scrollThreshold = …) { from, to -> … }
// (onMove last, or by name), and the grids' the same.
template <class StateName, class Info, auto Make>
struct RememberReorderableImpl {
    using P = Params<StateName, names::ScrollThresholdPadding, names::ScrollThreshold, names::OnMove, BlockWith<const Info&, const Info&>>;
    template <class... A>
    static auto call(CallSite site, A&&... args) {
        static_assert(P::template check<A...>());
        static_assert(P::template blockGiven<A...>() || P::template given<names::OnMove, A...>(),
                      "kompoz: a reorderable state takes onMove = { from, to -> … }, or it last");
        auto onMove = [&]() -> decltype(auto) {
            if constexpr (P::template blockGiven<A...>()) return std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
            else return P::template arg<names::OnMove>(args...);
        };
        return Make(P::template arg<StateName>(args...), moveFunction<Info>(onMove()),
                    P::template get<names::ScrollThresholdPadding>(PaddingValues{}, args...),
                    P::template get<names::ScrollThreshold>(ReorderableLazyCollectionDefaults::ScrollThreshold, args...), site);
    }
};
inline ReorderableLazyListState makeReorderableList(const LazyListState& s, std::function<std::optional<Task>(const LazyListItemInfo&, const LazyListItemInfo&)> m,
                                                    PaddingValues p, float t, CallSite site) {
    return rememberReorderableLazyListState(s, std::move(m), p, t, site);
}
inline ReorderableLazyGridState makeReorderableGrid(const LazyGridState& s, std::function<std::optional<Task>(const LazyGridItemInfo&, const LazyGridItemInfo&)> m,
                                                    PaddingValues p, float t, CallSite site) {
    return rememberReorderableLazyGridState(s, std::move(m), p, t, site);
}
inline ReorderableLazyStaggeredGridState makeReorderableStaggeredGrid(
    const LazyStaggeredGridState& s, std::function<std::optional<Task>(const LazyStaggeredGridItemInfo&, const LazyStaggeredGridItemInfo&)> m,
    PaddingValues p, float t, CallSite site) {
    return rememberReorderableLazyStaggeredGridState(s, std::move(m), p, t, site);
}

void ReorderableItem(const ReorderableLazyCollectionState& state, size_t key, const Modifier& modifier, bool enabled,
                     const std::optional<Modifier>& animateItemModifier, const std::function<void(bool)>& content, CallSite site);
// ReorderableColumn's and ReorderableRow's: ReorderableItem(modifier = …, enabled = …) { isDragging -> … }.
void ReorderableLinearItem(const Modifier& modifier, bool enabled, const std::function<void(bool)>& content, CallSite site);

// ReorderableItem(state, key = …, modifier = …, enabled = …, animateItemModifier = …) { isDragging -> … } in a
// reorderable lazy layout's item (its key the item's); ReorderableItem(modifier = …, enabled = …) { … } in a
// ReorderableColumn's or ReorderableRow's content.
struct ReorderableItemImpl {
    template <class B>
    static std::function<void(bool)> contentOf(B&& block) {  // (copied: Kotlin's lambdas are kept)
        return [block = std::decay_t<B>(std::forward<B>(block))](bool isDragging) mutable {
            if constexpr (std::is_invocable_v<std::decay_t<B>&, bool>) block(isDragging);
            else block();
        };
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {
        constexpr bool lazy = [] {
            if constexpr (sizeof...(A) == 0) return false;
            else {
                using First = std::remove_cvref_t<std::tuple_element_t<0, std::tuple<A...>>>;
                return std::is_base_of_v<ReorderableLazyCollectionState, First> || (std::is_same_v<TagOf<A>, names::State> || ...);
            }
        }();
        auto&& block = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
        // (its content takes isDragging, or nothing)
        using B = std::conditional_t<std::is_invocable_v<std::remove_cvref_t<decltype(block)>&, bool>, BlockWith<bool>, Block>;
        if constexpr (lazy) {
            using P = Params<names::State, names::Key, names::Modifier, names::Enabled, names::AnimateItemModifier, B>;
            static_assert(P::template check<A...>());
            static_assert(P::template blockGiven<A...>(), "kompoz: ReorderableItem takes a block: its content, { isDragging -> … }");
            const ReorderableLazyCollectionState& state = P::template arg<names::State>(args...);
            size_t key = LazyListScope::hashedKey(P::template arg<names::Key>(args...));
            std::optional<Modifier> animate;
            if constexpr (P::template given<names::AnimateItemModifier, A...>()) animate = P::template arg<names::AnimateItemModifier>(args...);
            ReorderableItem(state, key, P::template get<names::Modifier>(Modifier{}, args...), P::template get<names::Enabled>(true, args...), animate,
                            contentOf(block), site);
        } else {
            using P = Params<names::Modifier, names::Enabled, B>;
            static_assert(P::template check<A...>());
            static_assert(P::template blockGiven<A...>(), "kompoz: ReorderableItem takes a block: its content, { isDragging -> … }");
            ReorderableLinearItem(P::template get<names::Modifier>(Modifier{}, args...), P::template get<names::Enabled>(true, args...), contentOf(block), site);
        }
    }
};

} // namespace detail

inline constexpr Composable<detail::RememberReorderableImpl<detail::names::LazyListState, LazyListItemInfo, &detail::makeReorderableList>>
    rememberReorderableLazyListState;
inline constexpr Composable<detail::RememberReorderableImpl<detail::names::LazyGridState, LazyGridItemInfo, &detail::makeReorderableGrid>>
    rememberReorderableLazyGridState;
inline constexpr Composable<
    detail::RememberReorderableImpl<detail::names::LazyStaggeredGridState, LazyStaggeredGridItemInfo, &detail::makeReorderableStaggeredGrid>>
    rememberReorderableLazyStaggeredGridState;
// An item of a reorderable lazy layout: its content, dragged by a handle in it.
inline constexpr Composable<detail::ReorderableItemImpl> ReorderableItem;

// ---- ReorderableColumn, ReorderableRow ------------------------------------------------------

namespace detail {
void reorderableLinear(bool vertical, size_t count, std::function<void(int fromIndex, int toIndex)> onSettle, const Modifier& modifier,
                       std::function<void()> onMove, const Arrangement& arrangement, float crossAxisBias,
                       const std::function<void(size_t index, bool isDragging)>& content, CallSite site);

template <bool Vertical>
struct ReorderableLinearImpl {
    template <class... A>
    static void call(CallSite site, A&&... args) {
        using Arr = std::conditional_t<Vertical, names::VerticalArrangement, names::HorizontalArrangement>;
        using Align = std::conditional_t<Vertical, names::HorizontalAlignment, names::VerticalAlignment>;
        using First = Params<names::List>;
        using L = std::remove_cvref_t<decltype(First::arg<names::List>(args...))>;
        using T = ElementOf<L>;
        using P = Params<names::List, names::OnSettle, names::Modifier, names::OnMove, Arr, Align, BlockWith<int, const T&, bool>>;
        static_assert(P::template check<A...>());
        static_assert(P::template blockGiven<A...>(), "kompoz: ReorderableColumn takes a block: { index, item, isDragging -> … }");
        auto held = LazyListScope::held(First::arg<names::List>(args...));
        auto&& block = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
        std::function<void(size_t, bool)> content = [block = std::decay_t<decltype(block)>(block), held](size_t i, bool isDragging) mutable {
            block(int(i), (*held)[i], isDragging);
        };
        std::function<void(int, int)> onSettle = P::template arg<names::OnSettle>(args...);
        std::function<void()> onMove = P::template get<names::OnMove>(std::function<void()>{}, args...);
        Arrangement arrangement = P::template get<Arr>(Arrangement{}, args...);
        float bias = -1;
        if constexpr (P::template given<Align, A...>()) bias = P::template arg<Align>(args...).bias;
        reorderableLinear(Vertical, held->size(), std::move(onSettle), P::template get<names::Modifier>(Modifier{}, args...), std::move(onMove),
                          arrangement, bias, content, site);
    }
};
} // namespace detail

// A Column whose items the user reorders by dragging (Reorderable's ReorderableColumn): the
// content is called for each element of `list` and puts one ReorderableItem there, in a key of
// the element's (so that its state goes with it when the list changes).
//   ReorderableColumn(list = cities, onSettle = { from, to -> val moved = cities.toMutableList(); moved.add(to, moved.removeAt(from)); cities = moved },
//                     verticalArrangement = Arrangement.spacedBy(8.dp)) { index, city, isDragging ->
//       key(city) {
//           ReorderableItem { Card(modifier = Modifier.draggableHandle()) { Text(city) } }
//       }
//   }
// While dragging, the others move out of its way on the screen (onMove is called at each such
// step: a haptic tick); the list changes when it is let go, in onSettle.
inline constexpr Composable<detail::ReorderableLinearImpl<true>> ReorderableColumn;
inline constexpr Composable<detail::ReorderableLinearImpl<false>> ReorderableRow;

// ---- The handles (Reorderable's draggableHandle, longPressDraggableHandle) --------------------

template <class... A>
Modifier Modifier::draggableHandle(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::Enabled, n::InteractionSource, n::OnDragStarted, n::OnDragStopped, n::DragGestureDetector>;
    static_assert(P::check<A...>());
    std::optional<MutableInteractionSource> source;
    if constexpr (P::given<n::InteractionSource, A...>()) source = P::arg<n::InteractionSource>(args...);
    std::function<void(Offset)> started;
    if constexpr (P::given<n::OnDragStarted, A...>()) {
        auto&& f = P::arg<n::OnDragStarted>(args...);
        if constexpr (std::is_invocable_v<decltype(f)&, Offset>) started = f;
        else started = [f](Offset) mutable { f(); };
    }
    std::function<void()> stopped = P::get<n::OnDragStopped>(std::function<void()>{}, args...);
    return detail::reorderableHandle(*this, P::get<n::Enabled>(true, args...), source, std::move(started), std::move(stopped),
                                     P::get<n::DragGestureDetector>(DragGestureDetector::Press, args...));
}
template <class... A>
Modifier Modifier::longPressDraggableHandle(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::Enabled, n::InteractionSource, n::OnDragStarted, n::OnDragStopped>;
    static_assert(P::check<A...>());
    return draggableHandle(std::forward<A>(args)..., kompoz::dragGestureDetector = DragGestureDetector::LongPress);
}

} // namespace kompoz
