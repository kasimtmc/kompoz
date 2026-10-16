// Desktop scrollbars over a scroll state or a lazy list.

#include <kompoz/animation.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/scrollbar.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cmath>

namespace kompoz {

namespace {

struct ScrollStateAdapter final : ScrollbarAdapter {
    explicit ScrollStateAdapter(ScrollState s) : state(std::move(s)) {}
    ScrollState state;
    float scrollOffset() const override { return state.value(); }
    float contentSize() const override { return state.maxValue() + state.viewportSize(); }
    float viewportSize() const override { return state.viewportSize(); }
    void scrollTo(float offset) const override { state.scrollTo(offset); }
};

// A lazy list's: every item taken as long as the average of those in view.
struct LazyListAdapter final : ScrollbarAdapter {
    explicit LazyListAdapter(LazyListState s) : state(std::move(s)) {}
    LazyListState state;
    struct Measures {
        float item = 0;  // an item and the spacing after it
        float before = 0, after = 0, viewport = 0, spacing = 0;
        int count = 0;
    };
    Measures measures() const {
        LazyListLayoutInfo info = state.layoutInfo();
        Measures m;
        m.count = info.totalItemsCount;
        m.before = info.beforeContentPadding, m.after = info.afterContentPadding, m.spacing = info.mainAxisItemSpacing;
        m.viewport = info.orientation == Orientation::Vertical ? info.viewportSize.height : info.viewportSize.width;
        float sum = 0;
        for (const LazyListItemInfo& i : info.visibleItemsInfo) sum += i.size;
        if (!info.visibleItemsInfo.empty()) m.item = sum / float(info.visibleItemsInfo.size()) + m.spacing;
        return m;
    }
    float scrollOffset() const override {
        Measures m = measures();
        return float(state.firstVisibleItemIndex()) * m.item + state.firstVisibleItemScrollOffset();
    }
    float contentSize() const override {
        Measures m = measures();
        return m.before + float(m.count) * m.item - (m.count > 0 ? m.spacing : 0) + m.after;
    }
    float viewportSize() const override { return measures().viewport; }
    void scrollTo(float offset) const override {
        Measures m = measures();
        if (m.item <= 0) return;
        float o = std::max(0.f, offset);
        int index = std::clamp(int(o / m.item), 0, std::max(0, m.count - 1));
        state.scrollToItem(index, o - float(index) * m.item);
    }
};

// Where the thumb is along a track of `track` dp: its start and length (0: nothing to scroll).
struct Thumb {
    float start = 0, length = 0;
};
Thumb thumbOf(const ScrollbarAdapter& a, float track, float minimal, bool reverse) {
    float content = a.contentSize(), view = a.viewportSize();
    if (content <= view + 0.5f || track <= 0) return {};
    float length = std::clamp(track * view / content, std::min(minimal, track), track);
    float f = std::clamp(a.scrollOffset() / (content - view), 0.f, 1.f);
    return {(track - length) * (reverse ? 1 - f : f), length};
}

void scrollbar(bool vertical, const std::shared_ptr<ScrollbarAdapter>& adapter, const Modifier& modifier, const ScrollbarOptions& o) {
    ScrollbarStyle style = o.style.value_or(LocalScrollbarStyle.current());
    auto own = remember([] { return MutableInteractionSource(); });
    MutableInteractionSource source = o.interactionSource.value_or(own);
    bool active = source.collectIsHoveredAsState() || source.collectIsDraggedAsState();
    Color color = animateColorAsState(active ? style.hoverColor : style.unhoverColor, tween(float(style.hoverDurationMillis)));
    bool reverse = o.reverseLayout;
    float minimal = style.minimalHeight;
    auto along = [vertical](Size s) { return vertical ? s.height : s.width; };
    auto at = [vertical](Offset p) { return vertical ? p.y : p.x; };
    // A drag of the thumb moves the content as far as the thumb's travel stands for.
    struct Drag {
        float startOffset = 0, moved = 0;
        bool onThumb = false;
        std::optional<Interaction> interaction;
    };
    auto drag = remember([] { return std::make_shared<Drag>(); });
    struct Key {
        std::shared_ptr<ScrollbarAdapter> adapter;
        bool vertical, reverse;
        float minimal;
        MutableInteractionSource source;
        bool operator==(const Key&) const = default;
    };
    Key key{adapter, vertical, reverse, minimal, source};
    Shape shape = style.shape;
    Modifier m = modifier.then(vertical ? Modifier().width(style.thickness) : Modifier().height(style.thickness))
                     .hoverable(source)
                     .drawBehind([adapter, color, shape, minimal, reverse, vertical, along](DrawScope& d) {
                         Thumb t = thumbOf(*adapter, along(d.size()), minimal, reverse);
                         if (t.length <= 0) return;
                         Rect r = vertical ? Rect{0, t.start, d.size().width, t.length} : Rect{t.start, 0, t.length, d.size().height};
                         d.drawRoundRect(color, shape.radiusFor(r), {r.x, r.y}, Size{r.width, r.height});
                     })
                     .pointerInput(key, [adapter, minimal, reverse, along, at](PointerInputScope& scope) -> Task {
                         // A click on the track beside the thumb pages towards it.
                         co_await detectTapGestures(scope, {.onTap = [&scope, adapter, minimal, reverse, along, at](Offset p) {
                                                         Thumb t = thumbOf(*adapter, along(scope.size()), minimal, reverse);
                                                         if (t.length <= 0) return;
                                                         float pos = at(p);
                                                         if (pos >= t.start && pos <= t.start + t.length) return;
                                                         bool towardsEnd = (pos > t.start) != reverse;
                                                         float page = adapter->viewportSize();
                                                         adapter->scrollTo(adapter->scrollOffset() + (towardsEnd ? page : -page));
                                                     }});
                     })
                     .pointerInput(key, [adapter, minimal, reverse, along, at, drag, source, vertical](PointerInputScope& scope) -> Task {
                         AxisDragGestureHandlers h{
                             .onDragStart = [&scope, adapter, minimal, reverse, along, at, drag, source](Offset p) {
                                 Thumb t = thumbOf(*adapter, along(scope.size()), minimal, reverse);
                                 float pos = at(p);
                                 drag->onThumb = t.length > 0 && pos >= t.start - 4 && pos <= t.start + t.length + 4;
                                 drag->startOffset = adapter->scrollOffset();
                                 drag->moved = 0;
                                 if (drag->onThumb) source.tryEmit(*(drag->interaction = Interaction::dragStart()));
                             },
                             .onDrag = [&scope, adapter, minimal, reverse, along, drag](PointerInputChange& change, float delta) {
                                 if (!drag->onThumb) return;
                                 change.consume();
                                 Thumb t = thumbOf(*adapter, along(scope.size()), minimal, reverse);
                                 float travel = along(scope.size()) - t.length;
                                 if (travel <= 0) return;
                                 drag->moved += reverse ? -delta : delta;
                                 float range = adapter->contentSize() - adapter->viewportSize();
                                 adapter->scrollTo(drag->startOffset + drag->moved * range / travel);
                             },
                             .onDragEnd = [drag, source] {
                                 if (drag->interaction) source.tryEmit(drag->interaction->stop());
                                 drag->interaction.reset();
                             },
                             .onDragCancel = [drag, source] {
                                 if (drag->interaction) source.tryEmit(drag->interaction->cancel());
                                 drag->interaction.reset();
                             }};
                         if (vertical)
                             co_await detectVerticalDragGestures(scope, std::move(h));
                         else
                             co_await detectHorizontalDragGestures(scope, std::move(h));
                     });
    Box(m);
}

} // namespace

std::shared_ptr<ScrollbarAdapter> rememberScrollbarAdapter(const ScrollState& state, CallSite site) {
    return remember(state, [state] { return std::shared_ptr<ScrollbarAdapter>(std::make_shared<ScrollStateAdapter>(state)); }, site);
}

std::shared_ptr<ScrollbarAdapter> rememberScrollbarAdapter(const LazyListState& state, CallSite site) {
    return remember(state, [state] { return std::shared_ptr<ScrollbarAdapter>(std::make_shared<LazyListAdapter>(state)); }, site);
}

void VerticalScrollbar(std::shared_ptr<ScrollbarAdapter> adapter, const Modifier& modifier, const ScrollbarOptions& options, CallSite site) {
    ComposableScope scope(site);
    scrollbar(true, adapter, modifier, options);
}

void HorizontalScrollbar(std::shared_ptr<ScrollbarAdapter> adapter, const Modifier& modifier, const ScrollbarOptions& options, CallSite site) {
    ComposableScope scope(site);
    scrollbar(false, adapter, modifier, options);
}

} // namespace kompoz
