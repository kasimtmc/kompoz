// Modifier::basicMarquee: content too wide for its room scrolling through it, as on a ticker.

#include <kompoz/animation.hpp>
#include <kompoz/coroutines.hpp>
#include <kompoz/host.hpp>
#include <kompoz/layout.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace kompoz {

namespace {

// What a layout found: the content's width (unlimited) and the room it got.
struct MarqueeWidths {
    float content = 0, container = 0;
    friend bool operator==(const MarqueeWidths&, const MarqueeWidths&) = default;
};

// Kept in the composition: the widths (a change restarts the scrolling), the focus, and when the
// pass under way began, which only the drawing reads: it scrolls as it is drawn, by the frame's
// time (a frame of scrolling composes and lays out nothing, and one out of view asks for none).
struct MarqueeData {
    MutableState<MarqueeWidths> widths = mutableStateOf(MarqueeWidths{});
    MutableState<bool> focused = mutableStateOf(false);
    MutableState<int64_t> passStart = mutableStateOf(int64_t(-1));  // a frame's time (ns); none: at the start
    float distance = 0;  // the content and the gap after it: where its copy follows
    float speed = 0;     // dp a second
};

// A pass's length: from the start to where the copy was, at `speed` dp a second.
int64_t passNanos(float distance, float speed) { return int64_t(std::ceil(double(distance) / speed * 1e9)); }

struct MarqueeElement final : ModifierElement {
    std::shared_ptr<MarqueeData> data;
    bool towardsStart = true;  // a positive velocity: the content moves the way its text reads

    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};  // at the start (the right, right to left)
        Size content = next({c.minWidth, kInfinity, c.minHeight, c.maxHeight});
        float container = std::clamp(content.width, c.minWidth, c.maxWidth);
        if (!detail::isProbeMeasure()) data->widths.set({content.width, container});
        return {container, std::clamp(content.height, c.minHeight, c.maxHeight)};
    }

    void draw(GraphicsContext& g, const Rect& bounds, const LayoutNode& node, const std::function<void()>& drawContent) const override {
        const MarqueeData& d = *data;
        // How far the pass has scrolled (dp, from 0 to `distance`); drawn on until it ends.
        float offset = 0;
        if (int64_t start = d.passStart.get(); start >= 0 && node.host()) {
            int64_t now = node.host()->frameTime();
            offset = std::min(d.distance, float(now - start) / 1e9f * d.speed);
            if (int64_t end = start + passNanos(d.distance, d.speed); now < end) detail::animationNextFrame(node.host(), end);
        }
        g.pushClip(bounds);
        if (offset <= 0) {
            drawContent();
        } else {
            MarqueeWidths w = d.widths.peek();
            float way = towardsStart != node.rtl ? -1.f : 1.f;  // the drawing moves left (-1) or right
            // Moving towards its start, the content leaves first and the copy comes in from
            // the end; the other way round, the content stays until it has crossed the room.
            bool first = offset < (towardsStart ? w.content : w.container);
            bool second = offset > d.distance - (towardsStart ? w.container : w.content);
            if (first) {
                g.pushTranslation({way * offset, 0});
                drawContent();
                g.popTranslation();
            }
            if (second) {
                g.pushTranslation({way * (offset - d.distance), 0});
                drawContent();
                g.popTranslation();
            }
        }
        g.popClip();
    }
    bool clipsContent() const override { return true; }
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const MarqueeElement*>(&other);
        return o && o->data == data && o->towardsStart == towardsStart;
    }
};

struct MarqueeRun {
    bool running;
    float distance, speed;
    int iterations, repeatDelay, initialDelay;
    bool operator==(const MarqueeRun&) const = default;
};

// The passes: each one from the start to where the copy was (drawn as it goes), then back to the
// start (where the copy is now, so nothing jumps).
Task scroll(std::shared_ptr<MarqueeData> data, MarqueeRun run) {
    data->distance = run.distance;
    data->speed = run.speed;
    data->passStart.set(-1);
    if (!run.running) co_return;
    if (run.initialDelay > 0) co_await delay(std::chrono::milliseconds(run.initialDelay));
    for (int i = 0; run.iterations == MarqueeDefaults::Forever || i < run.iterations; ++i) {
        if (i > 0 && run.repeatDelay > 0) co_await delay(std::chrono::milliseconds(run.repeatDelay));
        data->passStart.set(co_await awaitFrame());
        co_await delay(std::chrono::nanoseconds(passNanos(run.distance, run.speed)));
        data->passStart.set(-1);
    }
}

} // namespace

Modifier Modifier::basicMarquee(const MarqueeOptions& o, CallSite site) const {
    auto data = remember([] { return std::make_shared<MarqueeData>(); }, site);
    MarqueeWidths w = data->widths.get();  // read while composing: new widths start it again
    bool whileFocused = o.animationMode == MarqueeAnimationMode::WhileFocused;
    bool active = !whileFocused || data->focused.get();
    float spacing = o.spacing.calculate ? std::max(0.f, o.spacing.calculate(w.content, w.container)) : 0;
    MarqueeRun run{w.content > w.container && o.velocity != 0 && active && o.iterations > 0,
                   w.content + spacing,
                   std::abs(o.velocity),
                   o.iterations,
                   o.repeatDelayMillis,
                   o.initialDelayMillis.value_or(whileFocused ? 0 : o.repeatDelayMillis)};
    LaunchedEffect(run, [data, run]() -> Task { return scroll(data, run); }, site);

    auto e = std::make_shared<MarqueeElement>();
    e->data = data;
    e->towardsStart = o.velocity >= 0;
    Modifier m = *this;
    if (whileFocused) m = m.onFocusChanged([data](FocusState s) { data->focused.set(s.hasFocus); });
    return m.then(std::move(e));
}

} // namespace kompoz
