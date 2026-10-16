#include <kompoz/animated.hpp>
#include <kompoz/animation.hpp>
#include <kompoz/coroutines.hpp>
#include <kompoz/material.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cmath>

namespace kompoz {

struct FloatingToolbarState::Data {
    Animatable<float> offset{0.f};
    float limit = 0;
};

FloatingToolbarState::FloatingToolbarState() : data_(std::make_shared<Data>()) {}

float FloatingToolbarState::offset() const { return data_->offset.value(); }
float FloatingToolbarState::offsetLimit() const { return data_->limit; }

void FloatingToolbarState::setOffset(float offset) const {
    float v = std::clamp(offset, 0.f, std::max(0.f, data_->limit));
    if (v != data_->offset.peek() || data_->offset.isRunning()) data_->offset.snapTo(v);
}

void FloatingToolbarState::setOffsetLimit(float limit) const {
    if (limit == data_->limit) return;
    data_->limit = limit;
    setOffset(data_->offset.peek());
}

SavedValue Saver<FloatingToolbarState>::save(const FloatingToolbarState& s) {
    return SavedValue::List{s.data_->offset.peek(), s.data_->limit};
}
std::optional<FloatingToolbarState> Saver<FloatingToolbarState>::restore(const SavedValue& v) {
    const SavedValue::List* l = v.list();
    if (!l || l->size() != 2) return std::nullopt;
    auto offset = (*l)[0].number(), limit = (*l)[1].number();
    if (!offset || !limit) return std::nullopt;
    FloatingToolbarState s;
    s.setOffsetLimit(float(*limit));
    s.setOffset(float(*offset));
    return s;
}

FloatingToolbarState rememberFloatingToolbarState(CallSite site) {
    return rememberSaveable([] { return FloatingToolbarState(); }, site);
}

namespace {

// Moves away as much as the content scrolls forwards and back as it scrolls back; the toolbar
// floats over the content, so it takes none of the scroll.
struct ExitAlwaysConnection final : NestedScrollConnection {
    ExitAlwaysConnection(FloatingToolbarState s, CoroutineScope c) : state(std::move(s)), scope(std::move(c)) {}
    FloatingToolbarState state;
    CoroutineScope scope;
    Offset onPostScroll(Offset consumed, Offset, NestedScrollSource) override {
        state.setOffset(state.offset() - consumed.y);
        return {};
    }
    // Half way: on the way it was flung, or to the nearer end.
    Velocity onPostFling(Velocity consumed, Velocity available) override {
        float limit = state.offsetLimit(), offset = state.offset();
        if (offset <= 0 || offset >= limit) return {};
        float velocity = consumed.y + available.y;
        float target = std::abs(velocity) > 300 ? (velocity < 0 ? limit : 0.f) : (offset < limit / 2 ? 0.f : limit);
        auto d = state.data_;
        scope.launch([d, target]() -> Task { co_await d->offset.animateTo(target, spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow)); });
        return {};
    }
};

FloatingToolbarColors colorsOf(const FloatingToolbarOptions& options) {
    return options.colors.value_or(FloatingToolbarDefaults::standardFloatingToolbarColors());
}

// The scroll behavior's offset, read during layout (scrolling doesn't recompose), and its
// limit from the toolbar's size.
Modifier scrolled(const Modifier& modifier, const std::optional<FloatingToolbarScrollBehavior>& behavior) {
    if (!behavior) return modifier;
    FloatingToolbarState state = behavior->state;
    FloatingToolbarExitDirection way = behavior->exitDirection;
    bool across = way == FloatingToolbarExitDirection::Start || way == FloatingToolbarExitDirection::End;
    return modifier
        .onSizeChanged([state, across](Size size) { state.setOffsetLimit((across ? size.width : size.height) + FloatingToolbarDefaults::ScreenOffset); })
        .offset([state, way]() -> Offset {
            float o = state.offset();
            switch (way) {
            case FloatingToolbarExitDirection::Bottom: return {0, o};
            case FloatingToolbarExitDirection::Top: return {0, -o};
            case FloatingToolbarExitDirection::End: return {o, 0};
            case FloatingToolbarExitDirection::Start: return {-o, 0};
            }
            return {};
        });
}

// The pill: content between the leading and trailing actions, which come and go with `expanded`.
void toolbar(bool vertical, bool expanded, const Content& content, const Modifier& modifier, const FloatingToolbarOptions& options) {
    FloatingToolbarColors colors = colorsOf(options);
    Modifier m = modifier.shadow(options.shadowElevation, options.shape).background(colors.toolbarContainerColor, options.shape);
    m = (vertical ? m.widthIn(FloatingToolbarDefaults::ContainerSize) : m.heightIn(FloatingToolbarDefaults::ContainerSize))
            .padding(FloatingToolbarDefaults::ContentPadding);
    auto side = [&](const Content& part) {
        if (!part) return;
        if (vertical)
            AnimatedVisibility(expanded, Modifier(), fadeIn() + expandVertically(), fadeOut() + shrinkVertically(), [&] {
                Column(Modifier(), {.horizontalAlignment = Alignment::CenterHorizontally}, part);
            });
        else
            AnimatedVisibility(expanded, Modifier(), fadeIn() + expandHorizontally(), fadeOut() + shrinkHorizontally(), [&] {
                Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, part);
            });
    };
    CompositionLocalProvider({LocalContentColor.provides(colors.toolbarContentColor)}, [&] {
        if (vertical)
            Column(m, {.horizontalAlignment = Alignment::CenterHorizontally}, [&] {
                side(options.leadingContent);
                content();
                side(options.trailingContent);
            });
        else
            Row(m, {.verticalAlignment = Alignment::CenterVertically}, [&] {
                side(options.leadingContent);
                content();
                side(options.trailingContent);
            });
    });
}

// The toolbar and a FAB after it; collapsed, the toolbar goes and the FAB stays.
void toolbarWithFab(bool vertical, bool expanded, const Content& fab, const Content& content, const Modifier& modifier,
                    const FloatingToolbarOptions& options) {
    float gap = FloatingToolbarDefaults::FabSpacing;
    auto bar = [&] {
        if (vertical)
            AnimatedVisibility(expanded, Modifier(), fadeIn() + expandVertically(), fadeOut() + shrinkVertically(),
                               [&] { toolbar(true, true, content, Modifier().padding(0, 0, 0, gap), options); });
        else
            AnimatedVisibility(expanded, Modifier(), fadeIn() + expandHorizontally(), fadeOut() + shrinkHorizontally(),
                               [&] { toolbar(false, true, content, Modifier().padding(0, 0, gap, 0), options); });
    };
    if (vertical)
        Column(scrolled(modifier, options.scrollBehavior), {.horizontalAlignment = Alignment::CenterHorizontally}, [&] {
            bar();
            fab();
        });
    else
        Row(scrolled(modifier, options.scrollBehavior), {.verticalAlignment = Alignment::CenterVertically}, [&] {
            bar();
            fab();
        });
}

void toolbarFab(Color container, Color content, std::function<void()> onClick, const Content& icon, const Modifier& modifier) {
    CompositionLocalProvider({LocalContentColor.provides(content)}, [&] {
        Box(modifier.size(56).shadow(6, 16).background(container, 16).clickable(std::move(onClick)), Alignment::Center, icon);
    });
}

} // namespace

namespace FloatingToolbarDefaults {

FloatingToolbarColors standardFloatingToolbarColors() {
    const ColorScheme& cs = colorScheme();
    return {cs.surfaceContainer, cs.onSurface, cs.secondaryContainer, cs.onSecondaryContainer};
}

FloatingToolbarColors vibrantFloatingToolbarColors() {
    const ColorScheme& cs = colorScheme();
    return {cs.primaryContainer, cs.onPrimaryContainer, cs.tertiaryContainer, cs.onTertiaryContainer};
}

FloatingToolbarScrollBehavior exitAlwaysScrollBehavior(FloatingToolbarExitDirection exitDirection, CallSite site) {
    ComposableScope group(site);
    FloatingToolbarState state = rememberFloatingToolbarState();
    auto scope = rememberCoroutineScope();
    auto& connection = remember([&] { return std::make_shared<ExitAlwaysConnection>(state, scope); });
    return {state, exitDirection, connection};
}

void StandardFloatingActionButton(std::function<void()> onClick, const Content& content, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    FloatingToolbarColors c = standardFloatingToolbarColors();
    toolbarFab(c.fabContainerColor, c.fabContentColor, std::move(onClick), content, modifier);
}

void VibrantFloatingActionButton(std::function<void()> onClick, const Content& content, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    FloatingToolbarColors c = vibrantFloatingToolbarColors();
    toolbarFab(c.fabContainerColor, c.fabContentColor, std::move(onClick), content, modifier);
}

} // namespace FloatingToolbarDefaults

namespace BottomAppBarDefaults {

BottomAppBarScrollBehavior exitAlwaysScrollBehavior(CallSite site) {
    ComposableScope group(site);
    return exitAlwaysScrollBehavior(rememberFloatingToolbarState());
}

BottomAppBarScrollBehavior exitAlwaysScrollBehavior(BottomAppBarState state, CallSite site) {
    ComposableScope group(site);
    auto scope = rememberCoroutineScope();
    auto& connection = remember([&] { return std::make_shared<ExitAlwaysConnection>(state, scope); });
    return {state, connection};
}

} // namespace BottomAppBarDefaults

void HorizontalFloatingToolbar(bool expanded, const Content& content, const Modifier& modifier, const FloatingToolbarOptions& options,
                               CallSite site) {
    ComposableScope scope(site);
    toolbar(false, expanded, content, scrolled(modifier, options.scrollBehavior), options);
}

void VerticalFloatingToolbar(bool expanded, const Content& content, const Modifier& modifier, const FloatingToolbarOptions& options,
                             CallSite site) {
    ComposableScope scope(site);
    toolbar(true, expanded, content, scrolled(modifier, options.scrollBehavior), options);
}

void HorizontalFloatingToolbar(bool expanded, const Content& floatingActionButton, const Content& content, const Modifier& modifier,
                               const FloatingToolbarOptions& options, CallSite site) {
    ComposableScope scope(site);
    toolbarWithFab(false, expanded, floatingActionButton, content, modifier, options);
}

void VerticalFloatingToolbar(bool expanded, const Content& floatingActionButton, const Content& content, const Modifier& modifier,
                             const FloatingToolbarOptions& options, CallSite site) {
    ComposableScope scope(site);
    toolbarWithFab(true, expanded, floatingActionButton, content, modifier, options);
}

} // namespace kompoz
