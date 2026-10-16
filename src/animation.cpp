#include <kompoz/animation.hpp>
#include <kompoz/host.hpp>

#include <cmath>

namespace kompoz {

namespace {
// Between the frames around time t: `first` at 0 and `last` at the end unless frames say otherwise.
float keyframeAt(const std::vector<Keyframe>& frames, float t, float duration, float first, float last) {
    Keyframe before{0, first}, after{duration, last};
    for (const Keyframe& frame : frames) {
        if (frame.atMillis <= t && frame.atMillis >= before.atMillis) before = frame;
        if (frame.atMillis >= t && frame.atMillis <= after.atMillis) after = frame;
    }
    if (after.atMillis <= before.atMillis) return before.value;
    return lerp(before.value, after.value, before.easing((t - before.atMillis) / (after.atMillis - before.atMillis)));
}
} // namespace

namespace detail {

float cubicBezier(float x1, float y1, float x2, float y2, float x) {
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    auto bezier = [](float p1, float p2, float t) {
        float u = 1 - t;
        return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
    };
    float lo = 0, hi = 1, t = x;  // solve x(t) = x by bisection
    for (int i = 0; i < 28; ++i) {
        float v = bezier(x1, x2, t);
        if (std::abs(v - x) < 1e-6f) break;
        (v < x ? lo : hi) = t;
        t = (lo + hi) / 2;
    }
    return bezier(y1, y2, t);
}

} // namespace detail

float AnimationSpec::progress(float ms, bool& finished) const {
    finished = false;
    switch (kind) {
    case Kind::Snap:
        finished = ms >= delayMillis;
        return finished ? 1.f : 0.f;

    case Kind::Keyframes:
    case Kind::Tween: {
        float period = delayMillis + durationMillis;
        if (period <= 0) {
            finished = true;
            return 1;
        }
        int k = int(std::floor(std::max(0.f, ms) / period));
        if (iterations != 0 && k >= iterations) {
            finished = true;
            return repeatMode == RepeatMode::Reverse && iterations % 2 == 0 ? 0.f : 1.f;
        }
        float local = std::max(0.f, ms) - float(k) * period;
        float f = durationMillis <= 0 ? 1.f : std::clamp((local - delayMillis) / durationMillis, 0.f, 1.f);
        bool backwards = repeatMode == RepeatMode::Reverse && k % 2 == 1;
        if (backwards) f = 1 - f;
        if (kind == Kind::Tween) return easing(f);
        return keyframeAt(keyframes, f * durationMillis, durationMillis, 0, 1);  // the values as fractions of the way
    }

    case Kind::Spring: {
        float x = 0, v = 0;
        if (springMotion(std::max(0.f, ms - delayMillis), -1, 0, x, v, 1e-3f)) {
            finished = true;
            return 1;
        }
        return 1 + x;
    }
    }
    return 1;
}

float AnimationSpec::valueAt(float ms, float start, float target, bool& finished) const {
    float p = progress(ms, finished);
    if (kind != Kind::Keyframes) return finished && p == 1.f ? target : lerp(start, target, p);
    if (finished) return iterations > 0 && repeatMode == RepeatMode::Reverse && iterations % 2 == 0 ? start : target;
    // The time within this round (going backwards on the reversed ones), then the frames as values.
    float period = delayMillis + durationMillis, local = std::max(0.f, ms);
    int k = period > 0 ? int(std::floor(local / period)) : 0;
    local -= float(k) * period;
    float t = std::clamp(local - delayMillis, 0.f, durationMillis);
    if (repeatMode == RepeatMode::Reverse && k % 2 == 1) t = durationMillis - t;
    return keyframeAt(keyframes, t, durationMillis, start, target);
}

bool AnimationSpec::springMotion(float elapsedMillis, float x0, float v0, float& x, float& v, float settled) const {
    // Closed-form damped spring with unit mass.
    float t = std::max(0.f, elapsedMillis) / 1000.f;
    float w0 = std::sqrt(std::max(stiffness, 1e-3f)), z = std::max(dampingRatio, 0.f);
    if (z < 1) {
        float wd = w0 * std::sqrt(1 - z * z), e = std::exp(-z * w0 * t);
        float a = x0, b = (v0 + z * w0 * x0) / wd;
        float cs = std::cos(wd * t), sn = std::sin(wd * t);
        x = e * (a * cs + b * sn);
        v = e * (-z * w0 * (a * cs + b * sn) + (-a * wd * sn + b * wd * cs));
    } else if (z == 1) {
        float e = std::exp(-w0 * t), b = v0 + w0 * x0;
        x = e * (x0 + b * t);
        v = e * (b - w0 * (x0 + b * t));
    } else {
        float s = std::sqrt(z * z - 1), r1 = -w0 * (z - s), r2 = -w0 * (z + s);
        float c2 = (r1 * x0 - v0) / (r1 - r2), c1 = x0 - c2;
        x = c1 * std::exp(r1 * t) + c2 * std::exp(r2 * t);
        v = c1 * r1 * std::exp(r1 * t) + c2 * r2 * std::exp(r2 * t);
    }
    return std::abs(x) < settled && std::abs(v) < settled * 10;
}

namespace {
// OverScroller's spline: the share of a fling's distance gone at each hundredth of its time.
struct FlingSpline {
    static constexpr int kSamples = 100;
    float positions[kSamples + 1];
    FlingSpline() {
        constexpr float kInflexion = 0.35f, kStartTension = 0.5f, kEndTension = 1.0f;
        constexpr float kP1 = kStartTension * kInflexion, kP2 = 1 - kEndTension * (1 - kInflexion);
        float xMin = 0;
        for (int i = 0; i < kSamples; ++i) {
            float alpha = float(i) / kSamples, xMax = 1, x, coef;
            while (true) {
                x = xMin + (xMax - xMin) / 2;
                coef = 3 * x * (1 - x);
                float tx = coef * ((1 - x) * kP1 + x * kP2) + x * x * x;
                if (std::abs(tx - alpha) < 1e-5f) break;
                (tx > alpha ? xMax : xMin) = x;
            }
            positions[i] = coef * ((1 - x) * kStartTension + x) + x * x * x;
        }
        positions[kSamples] = 1;
    }
};
const FlingSpline& flingSpline() {
    static const FlingSpline spline;
    return spline;
}
constexpr double kDecelerationRate = 2.3582018;  // ln(0.78) / ln(0.9)
// ViewConfiguration's scroll friction (0.015) times the physical deceleration (g in inches,
// 160 dp an inch, 0.84), per dp: in dp the screen's density drops out.
constexpr double kFlingDeceleration = 0.015 * 9.80665 * 39.37 * 160 * 0.84;
double splineDeceleration(float velocity) { return std::log(0.35 * std::abs(velocity) / kFlingDeceleration); }
} // namespace

float detail::splineFlingDistance(float velocity) {
    if (std::abs(velocity) < 1e-3f) return 0;
    double distance = kFlingDeceleration * std::exp(kDecelerationRate / (kDecelerationRate - 1) * splineDeceleration(velocity));
    return float(std::copysign(distance, double(velocity)));
}
float detail::splineFlingDurationMillis(float velocity) {
    if (std::abs(velocity) < 1e-3f) return 0;
    return float(1000 * std::exp(splineDeceleration(velocity) / (kDecelerationRate - 1)));
}
std::pair<float, float> detail::splineFlingPosition(float t) {
    constexpr int n = FlingSpline::kSamples;
    int i = int(float(n) * std::clamp(t, 0.f, 1.f));
    if (i >= n) return {1.f, 0.f};
    const float* p = flingSpline().positions;
    float rate = (p[i + 1] - p[i]) * float(n), start = float(i) / float(n);
    return {p[i] + (t - start) * rate, rate};
}

float DecayAnimationSpec::valueAt(float initialValue, float initialVelocity, float ms) const {
    if (spline) {
        float duration = detail::splineFlingDurationMillis(initialVelocity);
        if (duration <= 0) return initialValue;
        return initialValue + detail::splineFlingDistance(initialVelocity) * detail::splineFlingPosition(std::max(0.f, ms) / duration).first;
    }
    float k = std::max(friction, 1e-3f), t = std::max(0.f, ms) / 1000.f;
    return initialValue + initialVelocity / k * (1 - std::exp(-k * t));
}
float DecayAnimationSpec::velocityAt(float initialVelocity, float ms) const {
    if (spline) {
        float duration = detail::splineFlingDurationMillis(initialVelocity);
        if (duration <= 0) return 0;
        return detail::splineFlingPosition(std::max(0.f, ms) / duration).second * detail::splineFlingDistance(initialVelocity) / duration * 1000;
    }
    return initialVelocity * std::exp(-std::max(friction, 1e-3f) * std::max(0.f, ms) / 1000.f);
}
float DecayAnimationSpec::durationMillis(float initialVelocity) const {
    if (spline) return detail::splineFlingDurationMillis(initialVelocity);
    float v = std::abs(initialVelocity), threshold = std::max(absVelocityThreshold, 1e-4f);
    return v <= threshold ? 0.f : 1000.f * std::log(v / threshold) / std::max(friction, 1e-3f);
}
float DecayAnimationSpec::calculateTargetValue(float initialValue, float initialVelocity) const {
    if (spline) return initialValue + detail::splineFlingDistance(initialVelocity);
    return valueAt(initialValue, initialVelocity, durationMillis(initialVelocity));
}

DecayAnimationSpec exponentialDecay(float frictionMultiplier, float absVelocityThreshold) {
    return {4.2f * std::max(frictionMultiplier, 1e-3f), std::max(absVelocityThreshold, 1e-4f)};
}
DecayAnimationSpec splineBasedDecay() { return {.spline = true}; }  // the host's fling

AnimationSpec tween(float durationMillis, float delayMillis, Easing easing) {
    AnimationSpec s;
    s.kind = AnimationSpec::Kind::Tween;
    s.durationMillis = durationMillis;
    s.delayMillis = delayMillis;
    s.easing = easing;
    return s;
}

AnimationSpec spring(float dampingRatio, float stiffness) {
    AnimationSpec s;
    s.kind = AnimationSpec::Kind::Spring;
    s.dampingRatio = dampingRatio;
    s.stiffness = stiffness;
    return s;
}

AnimationSpec snap(float delayMillis) {
    AnimationSpec s;
    s.kind = AnimationSpec::Kind::Snap;
    s.delayMillis = delayMillis;
    return s;
}

AnimationSpec keyframes(float durationMillis, std::vector<Keyframe> frames, float delayMillis) {
    AnimationSpec s;
    s.kind = AnimationSpec::Kind::Keyframes;
    s.durationMillis = durationMillis;
    s.delayMillis = delayMillis;
    std::sort(frames.begin(), frames.end(), [](const Keyframe& a, const Keyframe& b) { return a.atMillis < b.atMillis; });
    s.keyframes = std::move(frames);
    return s;
}

AnimationSpec repeatable(int iterations, AnimationSpec spec, RepeatMode mode) {
    spec.iterations = std::max(1, iterations);
    spec.repeatMode = mode;
    return spec;
}

AnimationSpec infiniteRepeatable(AnimationSpec spec, RepeatMode mode) {
    spec.iterations = 0;
    spec.repeatMode = mode;
    return spec;
}

float InfiniteTransition::progress(const AnimationSpec& spec) const {
    auto& c = detail::Composer::require();
    int64_t now = c.frameTimeNanos();
    if (*start_ < 0) *start_ = now;
    bool finished = false;
    float p = spec.progress(float(now - *start_) / 1e6f, finished);
    if (!finished) c.requestFrame();
    return p;
}

float detail::DrawClock::millis(const LayoutNode& node) const {
    ComposeHost* host = node.host();
    if (!host) return 0;
    host->requestRedraw();
    return float(std::max<int64_t>(0, host->frameTime() - start_)) / 1e6f;
}

float detail::DrawClock::progress(const LayoutNode& node, const AnimationSpec& spec) const {
    bool finished = false;
    return spec.progress(millis(node), finished);
}

float detail::infiniteProgress(const std::shared_ptr<int64_t>& start, const AnimationSpec& spec, ComposeHost* host) {
    Composer* composer = Composer::current();
    bool composing = composer && composer->recording();
    if (!composing && !host) return 0;
    int64_t now = composing ? composer->frameTimeNanos() : host->frameTime();
    if (*start < 0) *start = now;
    bool finished = false;
    float p = spec.progress(float(now - *start) / 1e6f, finished);
    if (!finished) {
        if (composing) composer->requestFrame();
        else if (measuringNode()) host->requestLayoutFrame();
        else host->requestRedraw();
    }
    return p;
}

InfiniteState<float> InfiniteTransition::animateFloatState(float from, float to, const AnimationSpec& spec) const {
    InfiniteState<float> s;
    s.start_ = start_, s.from_ = from, s.to_ = to, s.spec_ = spec, s.host_ = &detail::Composer::require().host();
    return s;
}

InfiniteState<Color> InfiniteTransition::animateColorState(Color from, Color to, const AnimationSpec& spec) const {
    InfiniteState<Color> s;
    s.start_ = start_, s.from_ = from, s.to_ = to, s.spec_ = spec, s.host_ = &detail::Composer::require().host();
    return s;
}

float InfiniteTransition::animateFloat(float from, float to, const AnimationSpec& spec) const {
    return lerp(from, to, progress(spec));
}

Color InfiniteTransition::animateColor(Color from, Color to, const AnimationSpec& spec) const {
    return lerp(from, to, progress(spec));
}

} // namespace kompoz

namespace kompoz {

namespace {

// The size shown while animating towards the content's size; kept in the composition so it
// survives recompositions (a new modifier element is built every time).
struct SizeAnimation {
    AnimationSpec spec;
    Size current{-1, -1}, from, target;
    int64_t start = 0;
    bool running = false;

    // The size to show now, heading for `to`: the first one just appears; a new one is gone to
    // from wherever it is shown, even mid-animation. Asks for the next frame's layout while moving.
    Size step(Size to, ComposeHost& host) {
        int64_t now = host.frameTime();
        if (current.width < 0) {
            current = target = to;
        } else if (!(to == target)) {
            from = current;
            target = to;
            start = now;
            running = true;
        }
        if (running) {
            bool finished = false;
            float p = spec.progress(float(now - start) / 1e6f, finished);
            current = {lerp(from.width, target.width, p), lerp(from.height, target.height, p)};
            if (finished)
                running = false, current = target;
            else
                host.requestLayoutFrame();
        }
        return current;
    }
};

struct AnimateContentSizeElement final : ModifierElement {
    std::shared_ptr<SizeAnimation> anim;
    ComposeHost* host = nullptr;

    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        Size target = next(c);
        if (detail::isProbeMeasure()) return c.constrain(target);  // a size probe: the content's own size, no animation
        return c.constrain(anim->step(target, *host));
    }
    void draw(GraphicsContext& g, const Rect& bounds, const LayoutNode&, const std::function<void()>& drawContent) const override {
        g.pushClip(bounds);
        drawContent();
        g.popClip();
    }
    bool clipsContent() const override { return true; }
};

} // namespace

namespace {

// animateBounds: the size animates as animateContentSize's (the content laid out at it), the
// place as the drawing moved from where it was to where the layout put it, both from where they
// were shown when a change came.
struct BoundsAnimation {
    AnimationSpec spec;
    SizeAnimation size;
    Offset shown{NAN, NAN}, from, target;  // the place, from the scope
    int64_t start = 0;
    bool moving = false;
};

struct AnimateBoundsElement final : ModifierElement {
    std::shared_ptr<BoundsAnimation> anim;
    ComposeHost* host = nullptr;

    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        Size target;
        {
            detail::ProbeMeasure probe;  // the size it is going to
            detail::InLayoutPass lookahead(detail::LayoutPass::Lookahead);
            target = c.constrain(next(c));
        }
        if (detail::isProbeMeasure()) return target;
        anim->size.spec = anim->spec;
        Size shown = anim->size.step(target, *host);
        detail::InLayoutPass approach(detail::LayoutPass::Approach);
        next(Constraints::fixed(shown.width, shown.height));  // the content at the size along the way
        return shown;
    }

    void draw(GraphicsContext& g, const Rect& bounds, const LayoutNode& node, const std::function<void()>& drawContent) const override {
        const LayoutNode* scope = node.parent;
        while (scope && !scope->modifier.find<LookaheadScopeElement>()) scope = scope->parent;
        Offset origin = scope ? Offset{scope->bounds().x, scope->bounds().y} : Offset{};
        Offset at{bounds.x - origin.x, bounds.y - origin.y};
        BoundsAnimation& a = *anim;
        int64_t now = host->frameTime();
        if (std::isnan(a.shown.x)) {
            a.shown = a.target = at;
        } else if (at.x != a.target.x || at.y != a.target.y) {
            // A new place: from where it is shown. One that keeps moving while it goes there (its
            // size animating in an aligned parent) is followed without starting over.
            if (!a.moving) {
                a.from = a.shown;
                a.start = now;
                a.moving = true;
            }
            a.target = at;
        }
        if (a.moving) {
            bool finished = false;
            float p = a.spec.progress(float(now - a.start) / 1e6f, finished);
            a.shown = {lerp(a.from.x, a.target.x, p), lerp(a.from.y, a.target.y, p)};
            if (finished) a.moving = false, a.shown = a.target;
            else host->requestRedraw();  // drawn moved along: nothing to lay out
        }
        Offset by{a.shown.x - at.x, a.shown.y - at.y};
        if (by.x == 0 && by.y == 0) return drawContent();
        g.pushTranslation(by);
        drawContent();
        g.popTranslation();
    }
};

} // namespace

Modifier Modifier::animateBounds(CallSite site) const {
    return animateBounds(spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow), site);
}

Modifier Modifier::animateBounds(const AnimationSpec& spec, CallSite site) const {
    auto e = std::make_shared<AnimateBoundsElement>();
    e->anim = remember([] { return std::make_shared<BoundsAnimation>(); }, site);
    e->anim->spec = spec;
    e->host = &detail::Composer::require().host();
    return then(std::move(e));
}

Modifier Modifier::animateContentSize(CallSite site) const {
    return animateContentSize(spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow), site);
}

Modifier Modifier::animateContentSize(const AnimationSpec& spec, CallSite site) const {
    auto e = std::make_shared<AnimateContentSizeElement>();
    e->anim = remember([] { return std::make_shared<SizeAnimation>(); }, site);
    e->anim->spec = spec;
    e->host = &detail::Composer::require().host();
    return then(std::move(e));
}

} // namespace kompoz

namespace kompoz {

Task detail::animateScroll(std::function<float(float)> scrollBy, std::function<bool()> interrupted, float delta, AnimationSpec spec,
                           std::function<float()> remaining, bool* stoppedShort) {
    if (stoppedShort) *stoppedShort = false;
    if (delta == 0) co_return;
    int64_t start = co_await awaitFrame();
    float done = 0;
    while (true) {
        int64_t now = co_await awaitFrame();
        if (interrupted()) {
            if (stoppedShort) *stoppedShort = true;
            co_return;
        }
        if (remaining)
            if (float left = remaining(); !std::isnan(left)) delta = done + left;  // laid out since: exactly there
        bool finished = false;
        float p = spec.progress(float(now - start) / 1e6f, finished);
        float want = (finished ? delta : delta * p) - done;
        float used = want != 0 ? scrollBy(want) : 0;
        done += used;
        if (std::abs(used) + 0.01f < std::abs(want)) {  // an end
            if (stoppedShort) *stoppedShort = true;
            co_return;
        }
        if (finished) co_return;
    }
}

// One animated scroll, counted while it runs (isScrollInProgress), also when it is stopped.
Task ScrollState::animate(float delta, AnimationSpec spec) const {
    auto d = data_;
    struct Running {
        std::shared_ptr<Data> d;
        explicit Running(std::shared_ptr<Data> data) : d(std::move(data)) { d->animations.set(d->animations.peek() + 1); }
        ~Running() { d->animations.set(d->animations.peek() - 1); }
    } running{d};
    ScrollState state = *this;
    co_await detail::animateScroll([state](float x) { return state.scrollBy(x); },
                                   [d, generation = d->generation] { return d->generation != generation; }, delta, spec);
}

Task ScrollState::animateScrollTo(float value) const { return animateScrollTo(value, spring()); }
Task ScrollState::animateScrollTo(float value, const AnimationSpec& spec) const {
    return animateScrollBy(std::clamp(value, 0.f, data_->maxValue.peek()) - data_->value.peek(), spec);
}
Task ScrollState::animateScrollBy(float delta) const { return animateScrollBy(delta, spring()); }
Task ScrollState::animateScrollBy(float delta, const AnimationSpec& spec) const {
    stopScroll();  // one running at a time: the newer one
    return animate(delta, spec);
}

} // namespace kompoz
