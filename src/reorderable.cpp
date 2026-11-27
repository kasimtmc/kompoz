#include <kompoz/host.hpp>
#include <kompoz/reorderable.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace kompoz {

namespace {

float mainOf(bool vertical, Offset o) { return vertical ? o.y : o.x; }
float mainOf(bool vertical, Size s) { return vertical ? s.height : s.width; }

// How an item settles into its place when let go, and the others glide out of a dragged one's way.
AnimationSpec settleSpec() { return spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow); }

// The speed the list scrolls at when the dragged item is all the way into the threshold.
constexpr float MaxScrollSpeed = 1200;  // dp/s

} // namespace

// ---- The lazy layouts' ------------------------------------------------------------------------

Offset detail::ReorderableData::toLayout(Offset screen) const {
    // The layout's offsets go from its start: up in a reversed list, leftwards right to left.
    float x = rtl ? -screen.x : screen.x, y = screen.y;
    if (reverse) (vertical ? y : x) *= -1;
    if (mainAxisOnly) (vertical ? x : y) = 0;
    return {x, y};
}

Offset detail::ReorderableData::toScreen(Offset layout) const {
    float x = layout.x, y = layout.y;
    if (reverse) (vertical ? y : x) *= -1;
    if (rtl) x = -x;
    return {x, y};
}

std::optional<detail::ReorderablePlace> detail::ReorderableData::placeOf(size_t key) const {
    if (!places) return std::nullopt;
    for (const ReorderablePlace& p : places())
        if (p.key == key) return p;
    return std::nullopt;
}

void detail::ReorderableData::begin(size_t key) {
    auto at = placeOf(key);
    if (!at) return;
    ++drags;
    settler.cancel();
    settling.set(std::nullopt);
    start = at->offset;
    dragged.set(Offset{});
    dragging.set(key);
}

void detail::ReorderableData::drag(Offset screenDelta) {
    if (!dragging.peek()) return;
    Offset d = toLayout(screenDelta), was = dragged.peek();
    dragged.set(Offset{was.x + d.x, was.y + d.y});
    check();
}

void detail::ReorderableData::end() {
    auto key = dragging.peek();
    if (!key) return;
    scroller.cancel();
    Offset moved = dragged.peek();
    settleAt = {start.x + moved.x, start.y + moved.y};
    settle.set(1);
    settling.set(*key);
    dragging.set(std::nullopt);
    if (!scope) return settling.set(std::nullopt);
    std::weak_ptr<ReorderableData> weak = weak_from_this();
    settler = scope->launch([weak]() -> Task {
        AnimationSpec spec = settleSpec();
        int64_t first = co_await awaitFrame();
        while (true) {
            int64_t now = co_await awaitFrame();
            auto d = weak.lock();
            if (!d) co_return;
            bool finished = false;
            float p = spec.progress(float(now - first) / 1e6f, finished);
            d->settle.set(finished ? 0.f : 1 - p);
            if (finished) {
                d->settling.set(std::nullopt);
                co_return;
            }
        }
    });
}

void detail::ReorderableData::cancel(size_t key) {
    if (dragging.peek() == key) {
        scroller.cancel();
        dragging.set(std::nullopt);
    }
    if (settling.peek() == key) {
        settler.cancel();
        settling.set(std::nullopt);
    }
}

Offset detail::ReorderableData::translationOf(size_t key) const {
    bool dragged_ = dragging.get() == key, settling_ = !dragged_ && settling.get() == key;
    if (!dragged_ && !settling_) return {};
    auto at = placeOf(key);  // (read where it is drawn: drawn again when the layout moves it)
    if (!at) return {};
    Offset to = dragged_ ? Offset{start.x + dragged.get().x, start.y + dragged.get().y} : settleAt;
    float k = dragged_ ? 1.f : settle.get();
    return toScreen({(to.x - at->offset.x) * k, (to.y - at->offset.y) * k});
}

float detail::ReorderableData::autoScrollSpeed(const ReorderablePlace& item) const {
    if (!viewport || scrollThreshold <= 0) return 0;
    auto [viewStart, viewEnd] = viewport();
    Offset moved = dragged.peek();
    float at = mainOf(vertical, start) + mainOf(vertical, moved), size = mainOf(vertical, item.size);
    PaddingValues p = paddingNow(scrollThresholdPadding);
    float padStart = vertical ? p.top : p.start, padEnd = vertical ? p.bottom : p.end;
    if (reverse) std::swap(padStart, padEnd);
    float zoneStart = viewStart + padStart + scrollThreshold, zoneEnd = viewEnd - padEnd - scrollThreshold;
    float way = mainOf(vertical, moved);
    if (way > 0 && at + size > zoneEnd) return MaxScrollSpeed * std::min(1.f, (at + size - zoneEnd) / scrollThreshold);
    if (way < 0 && at < zoneStart) return -MaxScrollSpeed * std::min(1.f, (zoneStart - at) / scrollThreshold);
    return 0;
}

void detail::ReorderableData::startScroller() {
    if (scroller.isActive() || !scope || !scrollBy) return;
    std::weak_ptr<ReorderableData> weak = weak_from_this();
    scroller = scope->launch([weak]() -> Task {
        int64_t last = co_await awaitFrame();
        while (true) {
            int64_t now = co_await awaitFrame();
            auto d = weak.lock();
            if (!d || !d->dragging.peek()) co_return;
            auto item = d->placeOf(*d->dragging.peek());
            float speed = item ? d->autoScrollSpeed(*item) : 0.f;
            if (speed == 0) co_return;
            float used = d->scrollBy(speed * float(now - last) / 1e9f);
            last = now;
            d->check();
            if (used == 0) co_return;  // at the end
        }
    });
}

void detail::ReorderableData::check() {
    auto key = dragging.peek();
    if (!key) return;
    auto all = places ? places() : std::vector<ReorderablePlace>{};
    auto cur = std::find_if(all.begin(), all.end(), [&](const ReorderablePlace& p) { return p.key == *key; });
    if (cur == all.end()) return;
    if (autoScrollSpeed(*cur) != 0) startScroller();
    if (waiting || moving.isActive() || !move) return;

    Offset moved = dragged.peek();
    Offset at{start.x + moved.x, start.y + moved.y};
    const ReorderablePlace* target = nullptr;
    auto movable = [&](const ReorderablePlace& p) { return p.key != *key && reorderable.count(p.key); };
    if (mainAxisOnly) {
        // Along a list: past the middle of the next item the way it goes (the farthest one passed).
        float lead = mainOf(vertical, at), size = mainOf(vertical, cur->size);
        float way = lead - mainOf(vertical, cur->offset);
        for (const ReorderablePlace& p : all) {
            if (!movable(p)) continue;
            float middle = mainOf(vertical, p.offset) + mainOf(vertical, p.size) / 2;
            if (way > 0 && p.index > cur->index && lead + size > middle && (!target || p.index > target->index)) target = &p;
            if (way < 0 && p.index < cur->index && lead < middle && (!target || p.index < target->index)) target = &p;
        }
    } else {
        // In a grid: the item under its middle.
        Offset middle{at.x + cur->size.width / 2, at.y + cur->size.height / 2};
        for (const ReorderablePlace& p : all)
            if (movable(p) && middle.x >= p.offset.x && middle.x < p.offset.x + p.size.width && middle.y >= p.offset.y &&
                middle.y < p.offset.y + p.size.height)
                target = &p;
    }
    if (!target) return;
    // One move at a time: the next is looked for once the layout has shown this one (a frame
    // later), or once the user's coroutine has done it.
    waiting = true;
    std::optional<Task> work = move(*cur, *target);
    if (!scope) {
        waiting = false;
        return;
    }
    std::weak_ptr<ReorderableData> weak = weak_from_this();
    auto pending = std::make_shared<std::optional<Task>>(std::move(work));
    uint64_t drag = drags;
    moving = scope->launch([weak, pending, drag]() -> Task {
        if (*pending) co_await std::move(**pending);
        co_await awaitFrame();
        auto d = weak.lock();
        if (!d) co_return;
        d->waiting = false;
        if (d->drags == drag) d->check();
    });
}

namespace {

std::shared_ptr<detail::ReorderableData> rememberData(CallSite site) {
    auto data = remember([] { return std::make_shared<detail::ReorderableData>(); }, site);
    data->scope = rememberCoroutineScope();
    return data;
}

template <class Info>
const Info* infoOf(const std::vector<Info>& infos, size_t key) {
    for (const Info& i : infos)
        if (i.key == key) return &i;
    return nullptr;
}

} // namespace

ReorderableLazyListState detail::rememberReorderableLazyListState(
    const LazyListState& lazyListState, std::function<std::optional<Task>(const LazyListItemInfo&, const LazyListItemInfo&)> onMove,
    PaddingValues scrollThresholdPadding, float scrollThreshold, CallSite site) {
    KompozableScope scope(site);
    auto data = rememberData(CallSite::current());
    LazyListState list = lazyListState;
    data->places = [list, d = data.get()] {
        LazyListLayoutInfo info = list.layoutInfo();
        d->vertical = info.orientation == Orientation::Vertical;
        d->reverse = info.reverseLayout;
        std::vector<ReorderablePlace> out;
        for (const LazyListItemInfo& i : info.visibleItemsInfo) {
            Offset at = d->vertical ? Offset{0, i.offset} : Offset{i.offset, 0};
            Size size = d->vertical ? Size{info.viewportSize.width, i.size} : Size{i.size, info.viewportSize.height};
            out.push_back({i.index, i.key, at, size});
        }
        return out;
    };
    data->viewport = [list] {
        LazyListLayoutInfo info = list.layoutInfo();
        return std::pair{info.viewportStartOffset, info.viewportEndOffset};
    };
    data->scrollBy = [list, d = data.get()](float delta) { return list.scrollBy(d->reverse ? -delta : delta) * (d->reverse ? -1 : 1); };
    data->mainAxisOnly = true;
    data->move = [list, onMove = std::move(onMove)](const ReorderablePlace& from, const ReorderablePlace& to) -> std::optional<Task> {
        LazyListLayoutInfo info = list.layoutInfo();
        const LazyListItemInfo *a = infoOf(info.visibleItemsInfo, from.key), *b = infoOf(info.visibleItemsInfo, to.key);
        if (!a || !b) return std::nullopt;
        return onMove(*a, *b);
    };
    data->scrollThresholdPadding = scrollThresholdPadding;
    data->scrollThreshold = scrollThreshold;
    ReorderableLazyListState state;
    state.data_ = data;
    return state;
}

ReorderableLazyGridState detail::rememberReorderableLazyGridState(
    const LazyGridState& lazyGridState, std::function<std::optional<Task>(const LazyGridItemInfo&, const LazyGridItemInfo&)> onMove,
    PaddingValues scrollThresholdPadding, float scrollThreshold, CallSite site) {
    KompozableScope scope(site);
    auto data = rememberData(CallSite::current());
    LazyGridState grid = lazyGridState;
    data->places = [grid, d = data.get()] {
        LazyGridLayoutInfo info = grid.layoutInfo();
        d->vertical = info.orientation == Orientation::Vertical;
        std::vector<ReorderablePlace> out;
        for (const LazyGridItemInfo& i : info.visibleItemsInfo) out.push_back({i.index, i.key, i.offset, i.size});
        return out;
    };
    data->viewport = [grid] {
        LazyGridLayoutInfo info = grid.layoutInfo();
        return std::pair{info.viewportStartOffset, info.viewportEndOffset};
    };
    data->scrollBy = [grid](float delta) { return grid.scrollBy(delta); };
    data->mainAxisOnly = false;
    data->move = [grid, onMove = std::move(onMove)](const ReorderablePlace& from, const ReorderablePlace& to) -> std::optional<Task> {
        LazyGridLayoutInfo info = grid.layoutInfo();
        const LazyGridItemInfo *a = infoOf(info.visibleItemsInfo, from.key), *b = infoOf(info.visibleItemsInfo, to.key);
        if (!a || !b) return std::nullopt;
        return onMove(*a, *b);
    };
    data->scrollThresholdPadding = scrollThresholdPadding;
    data->scrollThreshold = scrollThreshold;
    ReorderableLazyGridState state;
    state.data_ = data;
    return state;
}

ReorderableLazyStaggeredGridState detail::rememberReorderableLazyStaggeredGridState(
    const LazyStaggeredGridState& lazyStaggeredGridState,
    std::function<std::optional<Task>(const LazyStaggeredGridItemInfo&, const LazyStaggeredGridItemInfo&)> onMove,
    PaddingValues scrollThresholdPadding, float scrollThreshold, CallSite site) {
    KompozableScope scope(site);
    auto data = rememberData(CallSite::current());
    LazyStaggeredGridState grid = lazyStaggeredGridState;
    data->places = [grid, d = data.get()] {
        LazyStaggeredGridLayoutInfo info = grid.layoutInfo();
        d->vertical = info.orientation == Orientation::Vertical;
        std::vector<ReorderablePlace> out;
        for (const LazyStaggeredGridItemInfo& i : info.visibleItemsInfo) out.push_back({i.index, i.key, i.offset, i.size});
        return out;
    };
    data->viewport = [grid] {
        LazyStaggeredGridLayoutInfo info = grid.layoutInfo();
        return std::pair{info.viewportStartOffset, info.viewportEndOffset};
    };
    data->scrollBy = [grid](float delta) { return grid.scrollState().scrollBy(delta); };
    data->mainAxisOnly = false;
    data->move = [grid, onMove = std::move(onMove)](const ReorderablePlace& from, const ReorderablePlace& to) -> std::optional<Task> {
        LazyStaggeredGridLayoutInfo info = grid.layoutInfo();
        const LazyStaggeredGridItemInfo *a = infoOf(info.visibleItemsInfo, from.key), *b = infoOf(info.visibleItemsInfo, to.key);
        if (!a || !b) return std::nullopt;
        return onMove(*a, *b);
    };
    data->scrollThresholdPadding = scrollThresholdPadding;
    data->scrollThreshold = scrollThreshold;
    ReorderableLazyStaggeredGridState state;
    state.data_ = data;
    return state;
}

void detail::ReorderableItem(const ReorderableLazyCollectionState& state, size_t key, const Modifier& modifier, bool enabled,
                             const std::optional<Modifier>& animateItemModifier, const std::function<void(bool)>& content, CallSite site) {
    KompozableScope scope(site);
    std::shared_ptr<ReorderableData> data = state.data_;
    data->rtl = LocalLayoutDirection.current() == LayoutDirection::Rtl;
    auto item = remember([] { return std::make_shared<ReorderableItemState>(); });
    item->collection = data;
    item->key = key;
    item->enabled = enabled;
    if (data->dragging.peek() != key) item->dragHandle = nullptr;  // (one whose gesture was cancelled)
    DisposableEffect(std::tuple<size_t, bool, const void*>(key, enabled, data.get()), [data, key, enabled]() -> std::function<void()> {
        if (enabled) data->reorderable.insert(key);
        return [data, key] {
            data->reorderable.erase(key);
            data->cancel(key);  // (the drag's gesture went with it)
        };
    });
    auto dragged = remember(key, data, [&] { return derivedStateOf([data, key] { return data->dragging.get() == key; }); });
    auto settling = remember(key, data, [&] { return derivedStateOf([data, key] { return data->settling.get() == key; }); });
    bool isDragging = dragged.get();
    Modifier m;
    if (isDragging || settling.get()) {
        // Over the others, where the finger has it (drawn there: the layout keeps its place).
        m = Modifier().zIndex(1).graphicsLayer([data, key](GraphicsLayerScope& layer) {
            Offset t = data->translationOf(key);
            layer.translationX = t.x;
            layer.translationY = t.y;
        });
    } else {
        m = animateItemModifier ? *animateItemModifier : Modifier().animateItem();
    }
    KompozitionLocalProvider({LocalReorderableItem.provides(item)}, [&] { Box(m.then(modifier), [content, isDragging] { content(isDragging); }); });
}

// ---- The handles -------------------------------------------------------------------------------

namespace {

void outsideItem() {
    std::fprintf(stderr, "kompoz: draggableHandle() outside a ReorderableItem's content (ReorderableItem(state, key) { … })\n");
    std::abort();
}

} // namespace

namespace detail {

// A ReorderableColumn's or ReorderableRow's state, while one of its items is dragged.
struct ReorderableLinearData : std::enable_shared_from_this<ReorderableLinearData> {
    bool vertical = true, rtl = false;
    std::function<void(int, int)> onSettle;
    std::function<void()> onMove;
    std::optional<CoroutineScope> scope;

    MutableState<int> dragging{-1};  // the child dragged
    MutableState<float> dragged{0};  // how far along the layout's axis (towards its end)
    MutableState<int> target{-1};    // where among the others it is shown
    float start = 0;                 // where it was when the drag began
    MutableState<int> settling{-1};  // let go: gliding to its place shown
    MutableState<float> settle{0};   // how much of the way it still has: 1 → 0
    float settleFrom = 0;
    bool reset = false;              // settled: the order shown goes back to the list's (now changed) at the next composition
    MutableState<int> generation{0};
    Job settler;
    uint64_t drags = 0;

    // From the last layout: the children's sizes along the axis, the space they were arranged in.
    std::vector<float> sizes;
    float total = 0;
    Arrangement arrangement;

    // Where each child goes along the axis with `from` shown at `to`.
    std::vector<float> positions(int from, int to) const {
        size_t n = sizes.size();
        std::vector<size_t> order(n);
        for (size_t i = 0; i < n; ++i) order[i] = i;
        if (from >= 0 && size_t(from) < n && to >= 0 && size_t(to) < n) {
            order.erase(order.begin() + from);
            order.insert(order.begin() + to, size_t(from));
        }
        std::vector<float> shownSizes(n), at(n, 0.f), out(n, 0.f);
        for (size_t k = 0; k < n; ++k) shownSizes[k] = sizes[order[k]];
        arrangement.arrange(total, shownSizes, at);
        for (size_t k = 0; k < n; ++k) out[order[k]] = at[k];
        return out;
    }

    float toLayout(float screen) const { return !vertical && rtl ? -screen : screen; }

    void begin(int index) {
        if (index < 0 || size_t(index) >= sizes.size() || settling.peek() >= 0) return;
        ++drags;
        start = positions(-1, -1)[size_t(index)];
        dragged.set(0);
        target.set(index);
        dragging.set(index);
    }
    void drag(float screenDelta) {
        int d = dragging.peek();
        if (d < 0) return;
        float at = std::clamp(start + dragged.peek() + toLayout(screenDelta), 0.f, std::max(0.f, total - sizes[size_t(d)]));
        dragged.set(at - start);
        // Past the middle of the next one the way it goes: shown there instead.
        int t = target.peek(), n = int(sizes.size());
        while (true) {
            std::vector<float> pos = positions(d, t);
            int next = -1;
            if (at > pos[size_t(d)]) {
                for (int i = 0; i < n; ++i)  // the one shown right after it
                    if (i != d && pos[size_t(i)] > pos[size_t(d)] && (next < 0 || pos[size_t(i)] < pos[size_t(next)])) next = i;
                if (next >= 0 && at + sizes[size_t(d)] > pos[size_t(next)] + sizes[size_t(next)] / 2) {
                    ++t;
                    continue;
                }
            } else if (at < pos[size_t(d)]) {
                for (int i = 0; i < n; ++i)  // the one shown right before it
                    if (i != d && pos[size_t(i)] < pos[size_t(d)] && (next < 0 || pos[size_t(i)] > pos[size_t(next)])) next = i;
                if (next >= 0 && at < pos[size_t(next)] + sizes[size_t(next)] / 2) {
                    --t;
                    continue;
                }
            }
            break;
        }
        if (t != target.peek()) {
            target.set(t);
            if (onMove) onMove();
        }
    }
    void end() {
        int d = dragging.peek();
        if (d < 0) return;
        settleFrom = start + dragged.peek();
        settle.set(1);
        settling.set(d);
        dragging.set(-1);
        std::weak_ptr<ReorderableLinearData> weak = weak_from_this();
        auto finish = [weak] {
            auto s = weak.lock();
            if (!s) return;
            int from = s->settling.peek(), to = s->target.peek();
            s->reset = true;
            s->generation.set(s->generation.peek() + 1);  // (composed again, with the list as onSettle left it)
            if (from >= 0 && to >= 0 && from != to && s->onSettle) s->onSettle(from, to);
        };
        if (!scope) return finish();
        settler = scope->launch([weak, finish]() -> Task {
            AnimationSpec spec = settleSpec();
            int64_t first = co_await awaitFrame();
            while (true) {
                int64_t now = co_await awaitFrame();
                auto s = weak.lock();
                if (!s) co_return;
                bool finished = false;
                float p = spec.progress(float(now - first) / 1e6f, finished);
                s->settle.set(finished ? 0.f : 1 - p);
                if (finished) {
                    finish();
                    co_return;
                }
            }
        });
    }
    void cancel(int index) {
        if (dragging.peek() == index) dragging.set(-1), target.set(-1);
    }
};

inline const KompozitionLocal<std::shared_ptr<ReorderableLinearData>> LocalReorderableLinear{nullptr};

// Marks a ReorderableColumn's item, so that its layout tells the item which child it is.
struct ReorderableLinearItemElement final : ModifierElement {
    std::shared_ptr<ReorderableItemState> item;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const ReorderableLinearItemElement*>(&other);
        return o && o->item == item;
    }
};

} // namespace detail

Modifier detail::reorderableHandle(const Modifier& modifier, bool enabled, std::optional<MutableInteractionSource> interactionSource,
                                   std::function<void(Offset)> onDragStarted, std::function<void()> onDragStopped,
                                   DragGestureDetector detector) {
    std::shared_ptr<ReorderableItemState> item = LocalReorderableItem.current();
    if (!item) outsideItem();
    item->onDragStarted = std::move(onDragStarted);
    item->onDragStopped = std::move(onDragStopped);
    if (!enabled) return modifier;
    auto block = [item, interactionSource, detector](PointerInputScope& scope) -> Task {
        auto started = std::make_shared<std::optional<Interaction>>();
        const void* self = started.get();  // (this handle)
        auto stop = [item, interactionSource, started, self](bool cancelled) {
            if (item->dragHandle != self) return;
            item->dragHandle = nullptr;
            if (item->collection) item->collection->end();
            if (item->linear) item->linear->end();
            if (interactionSource && *started) interactionSource->tryEmit(cancelled ? (*started)->cancel() : (*started)->stop());
            started->reset();
            if (item->onDragStopped) item->onDragStopped();
        };
        DragGestureHandlers h;
        h.onDragStart = [item, interactionSource, started, self](Offset position) {
            if (!item->enabled || item->dragHandle) return;
            item->dragHandle = self;
            if (item->collection) item->collection->begin(item->key);
            if (item->linear) item->linear->begin(item->index);
            if (interactionSource) {
                *started = Interaction::dragStart();
                interactionSource->tryEmit(**started);
            }
            if (item->onDragStarted) item->onDragStarted(position);
        };
        h.onDrag = [item, self](PointerInputChange& change, Offset amount) {
            if (item->dragHandle != self) return;
            change.consume();
            if (item->collection) item->collection->drag(amount);
            if (item->linear) item->linear->drag(item->linear->vertical ? amount.y : amount.x);
        };
        h.onDragEnd = [stop] { stop(false); };
        h.onDragCancel = [stop] { stop(true); };
        if (detector == DragGestureDetector::LongPress) co_await detectDragGesturesAfterLongPress(scope, h);
        else co_await detectDragGestures(scope, h);
    };
    return modifier.pointerInput(std::tuple<const void*, int>(item.get(), int(detector)), block);
}

// ---- ReorderableColumn, ReorderableRow --------------------------------------------------------

void detail::ReorderableLinearItem(const Modifier& modifier, bool enabled, const std::function<void(bool)>& content, CallSite site) {
    KompozableScope scope(site);
    std::shared_ptr<ReorderableLinearData> linear = LocalReorderableLinear.current();
    if (!linear) {
        std::fprintf(stderr, "kompoz: ReorderableItem { … } outside a ReorderableColumn's or ReorderableRow's content\n");
        std::abort();
    }
    auto item = remember([] { return std::make_shared<ReorderableItemState>(); });
    item->linear = linear;
    item->enabled = enabled;
    if (item->index < 0 || linear->dragging.peek() != item->index) item->dragHandle = nullptr;  // (one whose gesture was cancelled)
    DisposableEffect(linear.get(), [linear, item]() -> std::function<void()> {
        return [linear, item] { linear->cancel(item->index); };
    });
    auto marker = std::make_shared<ReorderableLinearItemElement>();
    marker->item = item;
    bool isDragging = item->index >= 0 && linear->dragging.get() == item->index;
    KompozitionLocalProvider({LocalReorderableItem.provides(item)},
                             [&] { Box(Modifier().then(marker).then(modifier), [content, isDragging] { content(isDragging); }); });
}

void detail::reorderableLinear(bool vertical, size_t count, std::function<void(int, int)> onSettle, const Modifier& modifier,
                               std::function<void()> onMove, const Arrangement& arrangement, float crossAxisBias,
                               const std::function<void(size_t, bool)>& content, CallSite site) {
    KompozableScope scope(site);
    auto data = remember([] { return std::make_shared<ReorderableLinearData>(); });
    data->scope = rememberCoroutineScope();
    data->vertical = vertical;
    data->rtl = LocalLayoutDirection.current() == LayoutDirection::Rtl;
    data->onSettle = std::move(onSettle);
    data->onMove = std::move(onMove);
    data->generation.get();  // (composed again once an item has settled)
    if (data->reset) {  // the list is as onSettle left it: shown as it is
        data->reset = false;
        data->settling.set(-1);
        data->target.set(-1);
    }
    int dragging = data->dragging.get();

    struct Glide {
        float shown = 0, from = 0, to = NAN;
        int64_t start = -1;
    };
    auto glides = remember([] { return std::make_shared<std::unordered_map<const LayoutNode*, Glide>>(); });
    MeasurePolicy policy = [data, vertical, arrangement, crossAxisBias, glides](LayoutNode& node, const Constraints& c) {
        size_t n = node.children.size();
        float crossMax = vertical ? c.maxWidth : c.maxHeight;
        std::vector<float> mains(n), crosses(n);
        float cross = 0, sum = 0;
        for (size_t i = 0; i < n; ++i) {
            LayoutNode& child = *node.children[i];
            for (const auto& e : child.modifier.elements())
                if (auto* m = dynamic_cast<const ReorderableLinearItemElement*>(e.get())) m->item->index = int(i);
            Size s = child.measure(vertical ? Constraints{0, crossMax, 0, kInfinity} : Constraints{0, kInfinity, 0, crossMax});
            mains[i] = vertical ? s.height : s.width;
            crosses[i] = vertical ? s.width : s.height;
            cross = std::max(cross, crosses[i]);
            sum += mains[i];
        }
        float content = sum + arrangement.spacing * float(n > 1 ? n - 1 : 0);
        Size own = c.constrain(vertical ? Size{cross, content} : Size{content, cross});
        float mainSize = vertical ? own.height : own.width, crossSize = vertical ? own.width : own.height;
        bool probe = detail::isProbeMeasure();
        if (!probe) {
            data->sizes = mains;
            data->total = mainSize;
            data->arrangement = arrangement;
        }
        int d = data->dragging.get(), s = data->settling.get(), t = data->target.get();
        int shown = d >= 0 ? d : s;
        std::vector<float> pos;
        {
            ReorderableLinearData copy;  // (arranged with this layout's sizes)
            copy.sizes = mains, copy.total = mainSize, copy.arrangement = arrangement;
            pos = copy.positions(shown, shown >= 0 ? t : -1);
        }
        int64_t now = node.host() ? node.host()->frameTime() : 0;
        bool animating = false;
        for (size_t i = 0; i < n; ++i) {
            LayoutNode& child = *node.children[i];
            float main = pos[i], z = 0;
            if (int(i) == d) {
                main = data->start + data->dragged.get();
                z = 1;
            } else if (int(i) == s) {
                main = pos[i] + (data->settleFrom - pos[i]) * data->settle.get();
                z = 1;
            }
            if (!probe) {
                Glide& g = (*glides)[&child];
                if (int(i) == d || int(i) == s || std::isnan(g.to)) {
                    g = {main, main, pos[i], -1};
                } else {
                    if (g.to != pos[i]) g.from = g.shown, g.to = pos[i], g.start = now;
                    if (g.start >= 0) {
                        bool finished = false;
                        float p = settleSpec().progress(float(now - g.start) / 1e6f, finished);
                        main = finished ? pos[i] : g.from + (pos[i] - g.from) * p;
                        if (finished) g.start = -1;
                        else animating = true;
                    }
                    g.shown = main;
                }
            }
            float across = (crossSize - crosses[i]) / 2 * (1 + crossAxisBias);
            if (vertical) child.place(across, main, z);
            else child.place(main, across, z);
        }
        if (!probe) {
            std::erase_if(*glides, [&](const auto& g) {
                return std::none_of(node.children.begin(), node.children.end(), [&](const LayoutNode* c) { return c == g.first; });
            });
            if (animating && node.host()) node.host()->requestLayoutFrame();
        }
        return own;
    };
    KompozitionLocalProvider({LocalReorderableLinear.provides(data)}, [&] {
        detail::Kompozer::require().emit(
            detail::keyOf(CallSite::current()),
            [&](LayoutNode& node) {
                node.modifier = modifier;
                node.measurePolicy = policy;
                node.drawContent = {};
            },
            [&] {
                for (size_t i = 0; i < count; ++i) content(i, int(i) == dragging);
            });
    });
}

} // namespace kompoz
