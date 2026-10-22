// Material 3 pull to refresh: Modifier::pullToRefresh on nested scrolling, PullToRefreshBox and
// its indicator.

#include <kompoz/animated.hpp>
#include <kompoz/animation.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/material.hpp>
#include <kompoz/nested_scroll.hpp>
#include <kompoz/state.hpp>

#include <cmath>

namespace kompoz {

namespace {
constexpr float kDragMultiplier = 0.5f;  // the indicator moves half as far as the finger, as in Compose
constexpr float kContainerSize = 40, kSpinnerSize = 16, kStrokeWidth = 2.5f, kArcRadius = 5.5f;
constexpr float kArrowWidth = 10, kArrowHeight = 5;

// What Modifier::pullToRefresh keeps, and what it was last composed with.
struct PullNode {
    float pulled = 0;       // dp the finger has pulled
    bool attached = false;  // composed before: from now on isRefreshing animates the state
    bool isRefreshing = false, enabled = true;
    float threshold = PullToRefreshDefaults::PositionalThreshold;
    std::function<void()> onRefresh;
    std::optional<PullToRefreshState> state;
};

// How far down the indicator is for a pull: half the finger's way up to the threshold, then
// slower and slower, at most twice the threshold (Compose's tension).
float indicatorOffset(float pulled, float threshold) {
    float adjusted = pulled * kDragMultiplier;
    if (adjusted <= threshold) return adjusted;
    float tension = std::clamp(adjusted / threshold - 1, 0.f, 2.f);
    return threshold + threshold * (tension - tension * tension / 4);
}
} // namespace

struct PullToRefreshState::Data {
    Animatable<float> fraction{0.f};
};

PullToRefreshState::PullToRefreshState() : data_(std::make_shared<Data>()) {}

float PullToRefreshState::distanceFraction() const { return data_->fraction.value(); }
bool PullToRefreshState::isAnimating() const { return data_->fraction.isRunning(); }
Task PullToRefreshState::animateToThreshold() const { return data_->fraction.animateTo(1.f, spring()); }
Task PullToRefreshState::animateToHidden() const { return data_->fraction.animateTo(0.f, spring()); }
void PullToRefreshState::snapTo(float fraction) const { data_->fraction.snapTo(std::max(0.f, fraction)); }

PullToRefreshState rememberPullToRefreshState(CallSite site) {
    return remember([] { return PullToRefreshState(); }, site);
}

Modifier Modifier::pullToRefresh(bool isRefreshing, const PullToRefreshState& state, std::function<void()> onRefresh,
                                 bool enabled, float threshold, CallSite site) const {
    ComposableScope scope(site);
    auto node = remember([] { return std::make_shared<PullNode>(); });
    node->isRefreshing = isRefreshing;
    node->enabled = enabled;
    node->threshold = std::max(1.f, threshold);
    node->onRefresh = std::move(onRefresh);
    node->state = state;
    auto coroutines = rememberCoroutineScope();
    // Refreshing (from the pull or set by the app): the state waits at the threshold. Where it
    // starts out, it is put at once.
    LaunchedEffect(isRefreshing, [node, state, isRefreshing]() -> Task {
        node->pulled = 0;
        if (!std::exchange(node->attached, true))
            state.snapTo(isRefreshing ? 1.f : 0.f);
        else if (isRefreshing)
            co_await state.animateToThreshold();
        else
            co_await state.animateToHidden();
    });
    // Takes `available` into the pull (none while refreshing); what it took.
    auto pull = [node](Offset available) -> Offset {
        if (node->isRefreshing) return {};
        float next = std::max(0.f, node->pulled + available.y), took = next - node->pulled;
        node->pulled = next;
        if (took != 0) node->state->snapTo(indicatorOffset(next, node->threshold) / node->threshold);
        return {0, took};
    };
    auto connection = rememberNestedScrollConnection({
        // Pulled down: going back up takes the indicator in before the content scrolls.
        .onPreScroll = [node, pull](Offset available, NestedScrollSource source) -> Offset {
            if (node->state->isAnimating() || !node->enabled || source != NestedScrollSource::UserInput || available.y >= 0)
                return {};
            return pull(available);
        },
        // What the content leaves at its top pulls the indicator down.
        .onPostScroll = [node, pull](Offset, Offset available, NestedScrollSource source) -> Offset {
            if (node->state->isAnimating() || !node->enabled || source != NestedScrollSource::UserInput) return {};
            return pull(available);
        },
        // Let go: refresh past the threshold; the indicator goes back in (or, once isRefreshing
        // comes, to the threshold). A fling down is used up while it was out; one up goes on.
        .onPreFling = [node, coroutines](Velocity available) -> Velocity {
            if (node->isRefreshing) return {};
            if (node->pulled * kDragMultiplier > node->threshold && node->onRefresh) node->onRefresh();
            float used = node->pulled == 0 || available.y < 0 ? 0 : available.y;
            node->pulled = 0;
            coroutines.launch([state = *node->state]() -> Task { co_await state.animateToHidden(); });
            return {0, used};
        },
    });
    return nestedScroll(connection);
}

void PullToRefreshBox(bool isRefreshing, std::function<void()> onRefresh, const Modifier& modifier, const Content& content,
                      PullToRefreshOptions options, CallSite site) {
    ComposableScope scope(site);
    PullToRefreshState state = options.state ? *options.state : rememberPullToRefreshState();
    Box(modifier.pullToRefresh(isRefreshing, state, std::move(onRefresh), options.enabled, options.threshold),
        options.contentAlignment, [&] {
            content();
            if (options.indicator)
                options.indicator(state, isRefreshing);
            else
                PullToRefreshDefaults::Indicator(state, isRefreshing, Modifier().align(Alignment::TopCenter));
        });
}

namespace PullToRefreshDefaults {

Color indicatorContainerColor() { return colorScheme().surfaceContainerHigh; }
Color indicatorColor() { return colorScheme().onSurfaceVariant; }

void IndicatorBox(const PullToRefreshState& state, bool isRefreshing, const Modifier& modifier, const Content& content,
                  float maxDistance, Shape shape, std::optional<Color> containerColor, float elevation, CallSite site) {
    ComposableScope scope(site);
    float fraction = state.distanceFraction();  // recomposes this small box as it moves
    Modifier m = modifier.size(kContainerSize)
                     .drawWithContent([](ContentDrawScope& s) {
                         s.clipRect({-1e6f, 0}, {2e6f, 1e6f}, [&](DrawScope&) { s.drawContent(); });
                     })
                     .graphicsLayer([fraction, maxDistance](GraphicsLayerScope& l) {
                         l.translationY = fraction * maxDistance - kContainerSize;
                     })
                     .shadow(fraction > 0 || isRefreshing ? elevation : 0, shape)
                     .clip(shape);
    if (containerColor) m = m.background(*containerColor, shape);
    Box(m, Alignment::Center, content);
}

namespace {
// The arc that grows with the pull and its arrowhead, faint until the threshold (Compose's
// CircularArrowProgressIndicator, after SwipeRefreshLayout).
void ArrowIndicator(float progress, Color color, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    float alpha = animateFloatAsState(progress >= 1 ? 1.f : 0.3f, tween(150));
    Canvas(Modifier().size(kSpinnerSize), [=](DrawScope& s) {
        float adjusted = std::max(std::min(1.f, progress) - 0.4f, 0.f) * 5 / 3;  // the first 40% shows nothing
        float tension = std::clamp(std::abs(progress) - 1, 0.f, 2.f);
        tension -= tension * tension / 4;
        float turn = (-0.25f + 0.4f * adjusted + tension) * 0.5f;  // of a full turn
        float start = turn * 360, end = (turn + adjusted * 0.8f) * 360, scale = std::min(1.f, adjusted);
        if (scale <= 0) return;
        Color c = color.copy(color.alpha() * alpha);
        float r = kArcRadius + kStrokeWidth / 2;
        Offset o = s.center();
        s.drawArc(c, start, end - start, false, {o.x - r, o.y - r}, Size{2 * r, 2 * r}, 1, Stroke(kStrokeWidth));
        // Across the arc's end, pointing the way it grows: a V turned with the end around the center.
        float a = (end - kStrokeWidth) * 3.14159265f / 180, cosA = std::cos(a), sinA = std::sin(a);
        auto at = [&](float x, float y) { return Offset{o.x + x * cosA - y * sinA, o.y + x * sinA + y * cosA}; };
        float w = kArrowWidth * scale, h = kArrowHeight * scale;
        Offset left = at(r - w / 2, -kStrokeWidth), tip = at(r, h - kStrokeWidth), right = at(r + w / 2, -kStrokeWidth);
        Path arrow;
        arrow.moveTo(left.x, left.y);
        arrow.lineTo(tip.x, tip.y);
        arrow.lineTo(right.x, right.y);
        s.drawPath(arrow, c, 1, Stroke(kStrokeWidth));
    });
}
} // namespace

void Indicator(const PullToRefreshState& state, bool isRefreshing, const Modifier& modifier, std::optional<Color> containerColor,
               std::optional<Color> color, float maxDistance, CallSite site) {
    ComposableScope scope(site);
    Color c = color.value_or(indicatorColor());
    float fraction = state.distanceFraction();
    Modifier m = modifier;
    if (fraction > 0 || isRefreshing)
        m = m.semantics({.contentDescription = isRefreshing ? LocalStrings.current().refreshing : LocalStrings.current().pullToRefresh});
    IndicatorBox(state, isRefreshing, m, [&] {
        Crossfade(isRefreshing, [c, fraction](const bool& refreshing) {
            if (refreshing)
                CircularProgressIndicator(Modifier().size(kSpinnerSize), c, kStrokeWidth);
            else
                ArrowIndicator(fraction, c);
        }, {}, tween(150));
    }, maxDistance, indicatorShape, containerColor.value_or(indicatorContainerColor()));
}

} // namespace PullToRefreshDefaults

} // namespace kompoz
