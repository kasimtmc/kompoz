#include <kompoz/gestures.hpp>
#include <kompoz/media_query.hpp>
#include <kompoz/splitpane.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace kompoz {

struct SplitPaneState::Data {
    MutableState<float> percentage;
    MutableState<bool> moveEnabled;
    float minPosition = 0, maxPosition = 0;  // where the splitter can be, from the last layout (dp)
    Data(float at, bool enabled) : percentage(std::clamp(at, 0.f, 1.f)), moveEnabled(enabled) {}
};

SplitPaneState::SplitPaneState(float initialPositionPercentage, bool moveEnabled)
    : data_(std::make_shared<Data>(initialPositionPercentage, moveEnabled)) {}
float SplitPaneState::positionPercentage() const { return data_->percentage.get(); }
void SplitPaneState::setPositionPercentage(float positionPercentage) const {
    float at = std::clamp(positionPercentage, 0.f, 1.f);
    if (data_->percentage.peek() != at) data_->percentage.set(at);
}
bool SplitPaneState::moveEnabled() const { return data_->moveEnabled.get(); }
void SplitPaneState::setMoveEnabled(bool moveEnabled) const {
    if (data_->moveEnabled.peek() != moveEnabled) data_->moveEnabled.set(moveEnabled);
}
void SplitPaneState::dispatchRawMovement(float delta) const {
    float room = data_->maxPosition - data_->minPosition;
    if (room <= 0) return;
    setPositionPercentage(std::clamp(room * data_->percentage.peek() + delta, 0.f, room) / room);
}

SplitPaneState detail::rememberSplitPaneState(float initialPositionPercentage, bool moveEnabled, CallSite site) {
    return rememberSaveable([&] { return SplitPaneState(initialPositionPercentage, moveEnabled); }, site);
}

detail::SplitPaneReceiver*& detail::splitPaneReceiver() {
    thread_local SplitPaneReceiver* current = nullptr;
    return current;
}

void detail::outsideSplitPane(const char* function) {
    std::fprintf(stderr, "kompoz: %s() outside a split pane's block (HorizontalSplitPane { %s(…) { … } })\n", function, function);
    std::abort();
}

namespace {

// What a handle's Modifier.markAsHandle() moves.
struct SplitPaneHandle {
    SplitPaneState state;
    bool vertical = false, rtl = false;
};
const KompozitionLocal<std::shared_ptr<SplitPaneHandle>> LocalSplitPaneHandle{nullptr};

// How far an arrow key moves the splitter (dp); with Shift, four times as far.
constexpr float KeyStep = 16;

} // namespace

Modifier Modifier::markAsHandle() const {
    std::shared_ptr<SplitPaneHandle> handle = LocalSplitPaneHandle.current();
    if (!handle) {
        std::fprintf(stderr, "kompoz: markAsHandle() outside a split pane's splitter { handle { … } }\n");
        std::abort();
    }
    SplitPaneState state = handle->state;
    bool vertical = handle->vertical, rtl = handle->rtl;
    bool enabled = state.moveEnabled();
    SemanticsProperties slider{.role = SemanticsRole::Slider,
                               .progressBarRangeInfo = ProgressBarRangeInfo{state.positionPercentage(), 0, 1, 0}};
    if (enabled) slider.setProgress = [state](float at) {
        state.setPositionPercentage(at);
        return true;
    };
    Modifier m = Modifier().semantics(slider);
    if (!enabled) return then(m);
    // Dragged along the split (right to left, a horizontal one's first pane is at the right: the
    // other way); and with the keys when focused.
    return then(m)
        .pointerInput(std::tuple<const void*, bool, bool>(state.data_.get(), vertical, rtl),
                      [state, vertical, rtl](PointerInputScope& scope) -> Task {
                          DragGestureHandlers h;
                          h.onDrag = [state, vertical, rtl](PointerInputChange& change, Offset amount) {
                              change.consume();
                              state.dispatchRawMovement(vertical ? amount.y : rtl ? -amount.x : amount.x);
                          };
                          co_await detectDragGestures(scope, h);
                      })
        .onKeyEvent([state, vertical, rtl](const KeyEvent& e) {
            if (e.type != KeyEventType::KeyDown) return false;
            float step = e.isShiftPressed() ? 4 * KeyStep : KeyStep;
            Key back = vertical ? Key::Up : rtl ? Key::Right : Key::Left, on = vertical ? Key::Down : rtl ? Key::Left : Key::Right;
            if (e.key == back) state.dispatchRawMovement(-step);
            else if (e.key == on) state.dispatchRawMovement(step);
            else if (e.key == Key::Home) state.setPositionPercentage(0);
            else if (e.key == Key::End) state.setPositionPercentage(1);
            else return false;
            return true;
        })
        .focusable();
}

void detail::splitPane(bool vertical, const Modifier& modifier, const std::optional<SplitPaneState>& given, const SplitPaneReceiver& panes,
                       CallSite site) {
    KompozableScope scope(site);
    SplitPaneState own = rememberSaveable([] { return SplitPaneState(); });
    SplitPaneState state = given ? *given : own;
    bool rtl = LocalLayoutDirection.current() == LayoutDirection::Rtl;
    auto handleOf = remember([] { return std::make_shared<SplitPaneHandle>(); });
    *handleOf = {state, vertical, rtl};
    // The default splitter: a line, and a handle over it as wide as the pointer needs.
    bool fine = mediaQuery([](const UiMediaScope& m) { return m.pointerPrecision() == PointerPrecision::Fine; });
    Color line = colorScheme().outlineVariant;
    Content visiblePart = panes.visiblePart ? panes.visiblePart : Content([vertical, line] {
        detail::Box(vertical ? Modifier().height(1).fillMaxWidth().background(line) : Modifier().width(1).fillMaxHeight().background(line), Content{});
    });
    float reach = fine ? 8 : 24;
    Content handle = panes.handle ? panes.handle : Content([vertical, reach] {
        detail::Box(vertical ? Modifier().markAsHandle().cursorForVerticalResize().height(reach).fillMaxWidth()
                             : Modifier().markAsHandle().cursorForHorizontalResize().width(reach).fillMaxHeight(),
                    Content{});
    });
    float firstMin = panes.first ? panes.first->minSize : 0, secondMin = panes.second ? panes.second->minSize : 0;
    Content first = panes.first ? panes.first->content : Content{}, second = panes.second ? panes.second->content : Content{};
    SplitterHandleAlignment alignment = panes.handleAlignment;
    std::shared_ptr<SplitPaneState::Data> d = state.data_;
    detail::Layout(
        modifier,
        [&] {
            detail::Box(Modifier().clipToBounds(), first);
            detail::Box(Modifier(), visiblePart);
            detail::Box(Modifier().clipToBounds(), second);
            KompozitionLocalProvider({LocalSplitPaneHandle.provides(handleOf)}, [&] { detail::Box(Modifier(), handle); });
        },
        [d, vertical, firstMin, secondMin, alignment](const std::vector<Measurable>& children, const Constraints& c) {
            float width = c.hasBoundedWidth() ? c.maxWidth : c.minWidth, height = c.hasBoundedHeight() ? c.maxHeight : c.minHeight;
            float total = vertical ? height : width, across = vertical ? width : height;
            auto fixed = [&](float along) { return vertical ? Constraints::fixed(across, along) : Constraints::fixed(along, across); };
            auto loose = [&] {
                return vertical ? Constraints{across, across, 0, total} : Constraints{0, total, across, across};
            };
            auto alongOf = [&](Size s) { return vertical ? s.height : s.width; };
            auto at = [&](float along) { return vertical ? Offset{0, along} : Offset{along, 0}; };
            Placeable splitter = children[1].measure(loose());
            float splitterSize = std::min(alongOf(Size{splitter.width, splitter.height}), total);
            // The splitter's room: from the first pane's least to the second pane's least.
            float minPosition = std::min(firstMin, std::max(0.f, total - splitterSize));
            float maxPosition = std::max(minPosition, total - secondMin - splitterSize);
            if (!detail::isProbeMeasure()) d->minPosition = minPosition, d->maxPosition = maxPosition;
            float position = minPosition + (maxPosition - minPosition) * d->percentage.get();
            Offset p = at(0);
            children[0].measure(fixed(position)).place(p.x, p.y);
            p = at(position);
            splitter.place(p.x, p.y);
            float after = position + splitterSize;
            p = at(after);
            children[2].measure(fixed(std::max(0.f, total - after))).place(p.x, p.y);
            Placeable handle = children[3].measure(loose());
            float handleSize = alongOf(Size{handle.width, handle.height});
            float handleAt = alignment == SplitterHandleAlignment::BEFORE ? position - handleSize
                             : alignment == SplitterHandleAlignment::AFTER ? after
                                                                           : position + splitterSize / 2 - handleSize / 2;
            p = at(handleAt);
            handle.place(p.x, p.y);
            return Size{width, height};
        },
        CallSite::current());
}

} // namespace kompoz
