#include <kompoz/animation.hpp>
#include <kompoz/coroutines.hpp>
#include <kompoz/host.hpp>
#include <kompoz/material.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cmath>
#include <deque>

namespace kompoz {

namespace {
constexpr float kCorner = 28;            // the sheet's top corners
constexpr float kFlingVelocity = 125;    // dp/s: a release faster than this goes on to the next anchor
}

struct SheetState::Data {
    Data(SheetValue initial, bool skip) : current(mutableStateOf(initial)), target(mutableStateOf(initial)), skipHidden(skip) {}
    MutableState<SheetValue> current, target;
    bool skipHidden;
    Animatable<float> offset{0.f};  // the sheet's top, dp from the scaffold's top
    bool placed = false;            // laid out at least once: the anchors are known
    float anchors[3] = {NAN, NAN, NAN};  // by SheetValue (Hidden, Expanded, PartiallyExpanded); NaN: not one

    float anchor(SheetValue v) const { return anchors[int(v)]; }
    bool has(SheetValue v) const { return !std::isnan(anchor(v)); }
    // The other value to rest at where the asked one isn't there (no room above the peek).
    SheetValue available(SheetValue v) const {
        if (has(v)) return v;
        if (v != SheetValue::Hidden && has(SheetValue::PartiallyExpanded)) return SheetValue::PartiallyExpanded;
        return has(SheetValue::Expanded) ? SheetValue::Expanded : SheetValue::PartiallyExpanded;
    }
    float top() const {  // the highest it goes
        float m = kInfinity;
        for (float a : anchors)
            if (!std::isnan(a)) m = std::min(m, a);
        return m;
    }
    float bottom() const {  // the lowest
        float m = -kInfinity;
        for (float a : anchors)
            if (!std::isnan(a)) m = std::max(m, a);
        return m;
    }
    // Where a released sheet goes: flung, on to the next anchor that way; else the nearest one.
    SheetValue settleTarget(float velocity) const {
        float at = offset.peek();
        SheetValue best = current.peek();
        float bestDistance = kInfinity;
        for (SheetValue v : {SheetValue::Expanded, SheetValue::PartiallyExpanded, SheetValue::Hidden}) {
            if (!has(v)) continue;
            float a = anchor(v), distance = std::abs(a - at);
            if (velocity >= kFlingVelocity && a <= at + 0.5f) continue;  // down: only those below
            if (velocity <= -kFlingVelocity && a >= at - 0.5f) continue;  // up: only those above
            if (distance < bestDistance) bestDistance = distance, best = v;
        }
        if (bestDistance == kInfinity) {  // flung past the last one that way: the nearest after all
            for (SheetValue v : {SheetValue::Expanded, SheetValue::PartiallyExpanded, SheetValue::Hidden})
                if (has(v) && std::abs(anchor(v) - at) < bestDistance) bestDistance = std::abs(anchor(v) - at), best = v;
        }
        return best;
    }
};

namespace {

// On a spring from `velocity` (dp/s downwards: a fling's), not past the top or bottom anchor.
Task animateSheetTo(std::shared_ptr<SheetState::Data> d, SheetValue value, float velocity = 0) {
    if (!d->placed) {  // before the first layout: it starts there
        d->current.set(value);
        d->target.set(value);
        co_return;
    }
    value = d->available(value);
    if (value == SheetValue::Hidden && !d->has(value)) co_return;
    d->target.set(value);
    float to = d->anchor(value);
    struct Bounded {  // while it animates (new anchors may move it after)
        Animatable<float> offset;
        ~Bounded() { offset.updateBounds(std::nullopt, std::nullopt); }
    } bounded{d->offset};
    d->offset.updateBounds(d->top(), d->bottom());
    co_await d->offset.animateTo(to, spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow), velocity);
    if (d->offset.peek() == to) {  // got there (not taken over by a drag or another one)
        d->current.set(value);
        d->target.set(value);
    }
}

} // namespace

SheetState::SheetState(SheetValue initial, bool skipHiddenState) : data_(std::make_shared<Data>(initial, skipHiddenState)) {
    if (skipHiddenState && initial == SheetValue::Hidden) data_->current.set(SheetValue::PartiallyExpanded), data_->target.set(SheetValue::PartiallyExpanded);
}
SheetValue SheetState::currentValue() const { return data_->current.get(); }
SheetValue SheetState::targetValue() const { return data_->target.get(); }
bool SheetState::hasExpandedState() const { return data_->has(SheetValue::Expanded); }
Task SheetState::expand() const { return animateSheetTo(data_, SheetValue::Expanded); }
Task SheetState::partialExpand() const { return animateSheetTo(data_, SheetValue::PartiallyExpanded); }
Task SheetState::hide() const {
    if (data_->skipHidden) return animateSheetTo(data_, data_->current.peek());  // stays where it is
    return animateSheetTo(data_, SheetValue::Hidden);
}
float SheetState::requireOffset() const { return data_->offset.peek(); }

SavedValue Saver<SheetState>::save(const SheetState& s) {
    return SavedValue::List{int64_t(s.data_->current.peek()), s.data_->skipHidden};
}
std::optional<SheetState> Saver<SheetState>::restore(const SavedValue& v) {
    if (!v.list() || v.list()->size() != 2) return std::nullopt;
    auto value = (*v.list())[0].integer();
    auto skip = (*v.list())[1].boolean();
    if (!value || !skip || *value < 0 || *value > 2) return std::nullopt;
    return SheetState(SheetValue(*value), *skip);
}

SheetState rememberStandardBottomSheetState(SheetValue initialValue, bool skipHiddenState, CallSite site) {
    return rememberSaveable([&] { return SheetState(initialValue, skipHiddenState); }, site);
}

BottomSheetScaffoldState rememberBottomSheetScaffoldState(CallSite site) {
    ComposableScope scope(site);
    return {rememberStandardBottomSheetState()};
}
BottomSheetScaffoldState rememberBottomSheetScaffoldState(SheetState bottomSheetState, CallSite site) {
    ComposableScope scope(site);
    (void)scope;
    return {std::move(bottomSheetState)};
}

void BottomSheetDefaults::DragHandle(CallSite site) {
    ComposableScope scope(site);
    // Compose 1.4's: onSurfaceVariant (no longer faded), 32 × 4 with 22 dp above and below.
    Box(Modifier().padding(0, 22).semantics(LocalStrings.current().dragHandle).size(32, 4).background(colorScheme().onSurfaceVariant, 2));
}

void BottomSheetScaffold(const Content& sheetContent, const Modifier& modifier, const BottomSheetScaffoldOptions& options,
                         const std::function<void(PaddingValues)>& content, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    BottomSheetScaffoldState state = options.scaffoldState ? *options.scaffoldState : rememberBottomSheetScaffoldState();
    std::shared_ptr<SheetState::Data> d = state.bottomSheetState.data_;
    // Kept clear of the system bars the layouts around it haven't kept clear of yet (inside an
    // app's Scaffold, none): found as it is laid out, so the sheet's bottom padding follows.
    WindowInsets bars = detail::systemBarsForVisualComponents();
    auto padding = remember([] { return mutableStateOf(PaddingValues{}); });
    auto sheetBottom = remember([] { return mutableStateOf(0.f); });
    auto coroutines = rememberCoroutineScope();
    bool swipe = options.sheetSwipeEnabled;
    auto settle = [d, coroutines](float velocity) {
        coroutines.launch([d, velocity]() -> Task { co_await animateSheetTo(d, d->settleTarget(velocity), velocity); });
    };
    auto moveBy = [d](float dy) {  // returns what it moved
        float now = d->offset.peek(), next = std::clamp(now + dy, d->top(), d->bottom());
        if (next == now) return 0.f;
        d->offset.snapTo(next);
        for (SheetValue v : {SheetValue::Expanded, SheetValue::PartiallyExpanded, SheetValue::Hidden})
            if (d->has(v) && std::abs(d->anchor(v) - next) < 0.01f) d->current.set(v), d->target.set(v);  // pushed all the way there
        return next - now;
    };
    // A list in the sheet: dragged up, the sheet goes up first, all the way; dragged down at the
    // list's top, the sheet comes down (Compose's ConsumeSwipeWithinBottomSheetBoundsNestedScrollConnection).
    auto connection = rememberNestedScrollConnection({
        .onPreScroll = [moveBy, swipe](Offset available, NestedScrollSource source) -> Offset {
            if (!swipe || source != NestedScrollSource::UserInput || available.y >= 0) return {};
            return {0, moveBy(available.y)};
        },
        .onPostScroll = [moveBy, swipe](Offset, Offset available, NestedScrollSource source) -> Offset {
            if (!swipe || source != NestedScrollSource::UserInput || available.y <= 0) return {};
            return {0, moveBy(available.y)};
        },
        .onPreFling = [d, settle, swipe](Velocity available) -> Velocity {
            if (!swipe || available.y >= 0 || d->offset.peek() <= d->top() + 0.5f) return {};
            settle(available.y);
            return available;
        },
        .onPostFling = [d, settle, swipe](Velocity, Velocity available) -> Velocity {
            if (!swipe) return {};
            bool resting = false;
            for (float a : d->anchors) resting = resting || (!std::isnan(a) && std::abs(a - d->offset.peek()) < 0.5f);
            if (resting && std::abs(available.y) < kFlingVelocity) return {};
            settle(available.y);
            return available;
        },
    });

    float peek = std::max(0.f, options.sheetPeekHeight);
    auto policy = [d, padding, sheetBottom, bars, peek](LayoutNode& node, const Constraints& c) {
        WindowInsets insets = detail::unconsumedInsets(node, bars);
        float w = c.hasBoundedWidth() ? c.maxWidth : c.minWidth, h = c.hasBoundedHeight() ? c.maxHeight : c.minHeight;
        auto& ch = node.children;  // content, top bar, sheet, snackbar host
        Constraints loose{0, w, 0, h};
        Size top = ch[1]->measure(loose);
        ch[1]->place(0, 0);
        // The sheet reaches kCorner below its height so that only its top corners show round.
        float maxSheet = std::max(0.f, h - insets.top);
        Size sheet = ch[2]->measure({0, w, 0, maxSheet + kCorner});
        float sheetHeight = std::max(0.f, sheet.height - kCorner);
        float peeking = std::min(peek + insets.bottom, sheetHeight);
        float anchors[3] = {NAN, NAN, NAN};
        if (!d->skipHidden) anchors[int(SheetValue::Hidden)] = h;
        if (peeking > 0) anchors[int(SheetValue::PartiallyExpanded)] = h - peeking;
        if (sheetHeight > peeking + 0.5f || peeking <= 0) anchors[int(SheetValue::Expanded)] = h - sheetHeight;
        if (!detail::isProbeMeasure()) {
            bool moved = false;
            for (int i = 0; i < 3; ++i)
                moved = moved || (std::isnan(anchors[i]) != std::isnan(d->anchors[i])) ||
                        (!std::isnan(anchors[i]) && std::abs(anchors[i] - d->anchors[i]) > 0.01f);
            std::copy(anchors, anchors + 3, d->anchors);
            // At rest (or not yet placed): on the anchor of where it rests, wherever that went.
            if (!d->offset.isRunning() && (!d->placed || moved)) {
                SheetValue v = d->available(d->current.peek());
                if (d->has(v)) d->offset.snapTo(d->anchor(v));
                if (v != d->current.peek()) d->current.set(v), d->target.set(v);
            }
            d->placed = true;
        }
        ch[2]->place((w - sheet.width) / 2, d->offset.peek());
        ch[0]->measure(loose);  // (as Compose's: content that doesn't fill keeps its size)
        ch[0]->place(0, 0);
        Size snack = ch[3]->measure({0, std::min(w, 640.f), 0, h});
        ch[3]->place((w - snack.width) / 2, std::min(d->offset.peek(), h - insets.bottom) - snack.height - 8);
        PaddingValues inner{insets.left, top.height > 0 ? top.height : insets.top, insets.right, peeking};
        if (!detail::isProbeMeasure() && !(inner == padding.peek())) padding.set(inner);
        if (!detail::isProbeMeasure() && insets.bottom != sheetBottom.peek()) sheetBottom.set(insets.bottom);
        return Size{w, h};
    };

    Color container = options.containerColor.value_or(cs.surface);
    Color sheetColor = options.sheetContainerColor.value_or(cs.surfaceContainerLow);
    d->offset.value();  // laid out again as it moves
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurface)}, [&] {
        detail::Composer::require().emit(
            detail::keyOf(CallSite::current()),
            [&](LayoutNode& node) {
                node.modifier = modifier.background(container).clip(RectangleShape);
                node.measurePolicy = policy;
                node.drawContent = {};
            },
            [&] {
                detail::stackSlot([&] { content(padding.get()); });
                detail::stackSlot(options.topBar);
                Modifier sheet = Modifier().widthIn(0, 640).fillMaxWidth().shadow(options.sheetShadowElevation, kCorner).background(sheetColor, kCorner);
                if (swipe)
                    sheet = sheet.pointerInput(0, [d, moveBy, settle](PointerInputScope& s) -> Task {
                        // Released: how fast it went over the last 100 ms.
                        auto samples = std::make_shared<std::deque<std::pair<int64_t, float>>>();
                        auto total = std::make_shared<float>(0.f);
                        co_await detectVerticalDragGestures(
                            s, {.onDragStart = [samples, total](Offset) { samples->clear(), *total = 0; },
                                .onDrag = [moveBy, samples, total](PointerInputChange& change, float dy) {
                                    change.consume();
                                    moveBy(dy);
                                    *total += dy;
                                    samples->push_back({change.uptimeMillis, *total});
                                    while (samples->size() > 2 && change.uptimeMillis - samples->front().first > 100) samples->pop_front();
                                },
                                .onDragEnd = [settle, samples] {
                                    float v = 0;
                                    if (samples->size() >= 2 && samples->back().first > samples->front().first)
                                        v = (samples->back().second - samples->front().second) * 1000.f /
                                            float(samples->back().first - samples->front().first);
                                    settle(v);
                                },
                                .onDragCancel = [settle] { settle(0); }});
                    });
                sheet = sheet.nestedScroll(connection);
                CompositionLocalProvider({LocalContentColor.provides(cs.onSurface)}, [&] {
                    Column(sheet.padding(0, 0, 0, sheetBottom.get() + kCorner), {.horizontalAlignment = Alignment::CenterHorizontally}, [&] {
                        if (!options.sheetDragHandle) BottomSheetDefaults::DragHandle();
                        else if (*options.sheetDragHandle) (*options.sheetDragHandle)();
                        Column(Modifier().fillMaxWidth(), sheetContent);
                    });
                });
                detail::stackSlot(options.snackbarHost);
            });
    });
}

} // namespace kompoz
