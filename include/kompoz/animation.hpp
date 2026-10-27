#pragma once

// Animation specs (tween, spring, repeatable), animate*AsState, Animatable and infinite
// transitions, as in androidx.compose.animation.core.

#include "coroutines.hpp"
#include "runtime.hpp"

#include <algorithm>
#include <memory>

#include <vector>

namespace kompoz {

namespace detail {
float cubicBezier(float x1, float y1, float x2, float y2, float t);
}

// cubic-bezier(x1, y1, x2, y2), like Compose's CubicBezierEasing.
struct Easing {
    float x1, y1, x2, y2;
    float operator()(float t) const { return detail::cubicBezier(x1, y1, x2, y2, t); }
    friend constexpr bool operator==(const Easing&, const Easing&) = default;
};
inline constexpr Easing FastOutSlowInEasing{0.4f, 0.f, 0.2f, 1.f};
inline constexpr Easing LinearOutSlowInEasing{0.f, 0.f, 0.2f, 1.f};
inline constexpr Easing FastOutLinearInEasing{0.4f, 0.f, 1.f, 1.f};
inline constexpr Easing LinearEasing{0.f, 0.f, 1.f, 1.f};
inline constexpr Easing EaseInOut{0.42f, 0.f, 0.58f, 1.f};

namespace Spring {
inline constexpr float DampingRatioNoBouncy = 1.f, DampingRatioLowBouncy = 0.75f, DampingRatioMediumBouncy = 0.5f,
                       DampingRatioHighBouncy = 0.2f;
inline constexpr float StiffnessHigh = 10000.f, StiffnessMedium = 1500.f, StiffnessMediumLow = 400.f,
                       StiffnessLow = 200.f, StiffnessVeryLow = 50.f;
} // namespace Spring

enum class RepeatMode { Restart, Reverse };

// keyframes(): the value at a time and the easing of the stretch that starts there. For float
// animations (Animatable<float>, animateFloatAsState) the value is the value itself, as in
// Compose (a pulse: 1 to 1.3 to 1); for others (colors) it is the fraction of the way from the
// start (0) to the target (1).
struct Keyframe {
    float atMillis = 0;
    float value = 0;
    Easing easing = LinearEasing;
    friend bool operator==(const Keyframe&, const Keyframe&) = default;
};

struct AnimationSpec {
    enum class Kind { Tween, Spring, Snap, Keyframes };
    Kind kind = Kind::Spring;
    float durationMillis = 300, delayMillis = 0;
    Easing easing = FastOutSlowInEasing;
    float dampingRatio = Spring::DampingRatioNoBouncy, stiffness = Spring::StiffnessMedium;
    int iterations = 1;  // 0: forever
    RepeatMode repeatMode = RepeatMode::Restart;
    std::vector<Keyframe> keyframes;  // Kind::Keyframes, by time

    // Progress from the start (0) to the target (1) after `elapsedMillis`; bouncy springs
    // overshoot. Sets `finished` once the animation has settled.
    float progress(float elapsedMillis, bool& finished) const;
    // A float's value after `elapsedMillis` going from `start` to `target`: keyframes' values as
    // they are (starting from `start`, ending at `target` unless frames say otherwise there),
    // the others along the progress.
    float valueAt(float elapsedMillis, float start, float target, bool& finished) const;
    // A spring's motion from any start: the displacement from the target and the velocity (per
    // second) after `elapsedMillis`, from displacement x0 moving at v0. Returns whether it has
    // settled (within `settled` of the target, nearly still).
    bool springMotion(float elapsedMillis, float x0, float v0, float& x, float& v, float settled = 0.01f) const;

    friend bool operator==(const AnimationSpec&, const AnimationSpec&) = default;
};

AnimationSpec tween(float durationMillis = 300, float delayMillis = 0, Easing easing = FastOutSlowInEasing);
AnimationSpec spring(float dampingRatio = Spring::DampingRatioNoBouncy, float stiffness = Spring::StiffnessMedium);
AnimationSpec snap(float delayMillis = 0);
// Compose's keyframes { durationMillis = 375; 0.4f at 75 using FastOutSlowInEasing; 1.1f at 225 }:
// keyframes(375, {{75, 0.4f, FastOutSlowInEasing}, {225, 1.1f}}).
AnimationSpec keyframes(float durationMillis, std::vector<Keyframe> frames, float delayMillis = 0);
AnimationSpec repeatable(int iterations, AnimationSpec spec, RepeatMode mode = RepeatMode::Restart);
AnimationSpec infiniteRepeatable(AnimationSpec spec, RepeatMode mode = RepeatMode::Restart);

// How something flung slows down (Compose's DecayAnimationSpec): velocity v0 falls off as
// exp(-friction * t) until it is slower than absVelocityThreshold — or, `spline`, along
// Android's fling curve (splineBasedDecay).
struct DecayAnimationSpec {
    float friction = 4.2f;              // per second
    float absVelocityThreshold = 0.1f;  // units per second
    bool spline = false;
    float valueAt(float initialValue, float initialVelocity, float elapsedMillis) const;
    float velocityAt(float initialVelocity, float elapsedMillis) const;
    float durationMillis(float initialVelocity) const;
    // Where a fling from there at that velocity comes to rest (snap a pager to the page it is
    // heading for).
    float calculateTargetValue(float initialValue, float initialVelocity) const;
    friend bool operator==(const DecayAnimationSpec&, const DecayAnimationSpec&) = default;
};
// Compose's exponentialDecay(frictionMultiplier, absVelocityThreshold).
DecayAnimationSpec exponentialDecay(float frictionMultiplier = 1, float absVelocityThreshold = 0.1f);
// A fling as the scrollables here fling (Compose's rememberSplineBasedDecay): Android's — the
// OverScroller spline, so a fast fling goes much further than a slow one, in dp units.
DecayAnimationSpec splineBasedDecay();
namespace detail {
// Android's fling (OverScroller's spline, Compose's AndroidFlingSpline and FlingCalculator), in
// dp: how far and how long a fling at `velocity` dp/s goes, and at `t` (0..1) of its time the
// share of the distance it has gone and how fast that share grows (per its whole time).
float splineFlingDistance(float velocity);
float splineFlingDurationMillis(float velocity);
std::pair<float, float> splineFlingPosition(float t);
}  // namespace detail
inline DecayAnimationSpec rememberSplineBasedDecay() { return splineBasedDecay(); }

namespace detail {
// Scrolls `delta` dp over the frames as `spec` goes, by `scrollBy` (it returns what it used).
// `remaining`, if given, says the distance left as it is known now (an item a lazy list laid
// out since, sizes measured since; NaN: not known): the animation then heads there. It ends early
// at an end (less used than asked: *stoppedShort) or once `interrupted` says something else
// scrolls.
Task animateScroll(std::function<float(float)> scrollBy, std::function<bool()> interrupted, float delta, AnimationSpec spec,
                   std::function<float()> remaining = {}, bool* stoppedShort = nullptr);
} // namespace detail

namespace detail {
// `fraction` of the way between two values as an animation goes: a color as Compose animates one
// (detail::animatedColor), anything else by its lerp.
template <class T>
T animationLerp(const T& from, const T& to, float fraction) {
    if constexpr (std::is_same_v<T, Color>) return animatedColor(from, to, fraction);
    else return lerp(from, to, fraction);
}

// animateValueAsState starting out from `initial` (at once on its way to `target` if they differ).
template <class T>
T animateValueFrom(const T& initial, const T& target, const AnimationSpec& spec, CallSite site) {
    struct Anim {
        T from, to, value;
        int64_t start = 0;
        bool running = false;
    };
    auto& c = detail::Composer::require();
    auto& a = remember([&] { return Anim{initial, initial, initial}; }, site);
    int64_t now = c.frameTimeNanos();
    if (!(a.to == target)) {
        a.from = a.value;
        a.to = target;
        a.start = now;
        a.running = true;
    }
    if (!a.running) return a.value;  // at rest: no frames requested
    bool finished = false;
    if constexpr (std::is_same_v<T, float>) {
        a.value = spec.valueAt(float(now - a.start) / 1e6f, a.from, a.to, finished);
    } else {
        float p = spec.progress(float(now - a.start) / 1e6f, finished);
        a.value = finished && p == 1.f ? a.to : animationLerp(a.from, a.to, p);
    }
    a.running = !finished;
    if (a.running) c.requestFrame();
    return a.value;
}
} // namespace detail

namespace detail {
// The time an animation read now goes by: the frame being composed, laid out or drawn (the last
// one between frames).
int64_t animationReadTime(ComposeHost* host);
// Asks for the next frame of what is reading an animation that still moves (until `end`, frame
// time): a composition is composed again, a layout laid out again, a drawing drawn again.
void animationNextFrame(ComposeHost* host, int64_t end);
// When an animation along `spec` from `start` (frame time) comes to rest; INT64_MAX: never.
int64_t animationEnd(const AnimationSpec& spec, int64_t start);
} // namespace detail

template <class T>
class AnimatedState;
template <class T>
AnimatedState<T> animateValueAsState(const T& target, const AnimationSpec& spec, CallSite site);

// What animate*AsState gives (Compose's State<T>): the value where it is now, read where it is
// needed. Read in a composition (or used there as a T), the reader is composed again on every
// frame it moves; read while drawing (drawBehind, graphicsLayer { }, a Canvas) it is only drawn
// again, and while laying out (offset { }, a layout) only laid out again, as in Compose:
//   auto alpha = animateFloatAsState(shown ? 1.f : 0.f);
//   Box(Modifier().graphicsLayer([alpha](GraphicsLayerScope& l) { l.alpha = alpha.value(); }));
template <class T>
class AnimatedState {
public:
    T value() const {
        Data& d = *d_;
        detail::recordRead(d_.get());
        return d.at(detail::animationReadTime(d.host), true);
    }
    T get() const { return value(); }
    operator T() const { return value(); }
    // Where it is going, and whether it is still on its way (neither subscribes).
    T targetValue() const { return d_->to; }
    bool isRunning() const { return d_->running; }
    // The same state, not the same value: lets what holds one be skipped.
    friend bool operator==(const AnimatedState& a, const AnimatedState& b) { return a.d_ == b.d_; }

private:
    friend AnimatedState animateValueAsState<T>(const T& target, const AnimationSpec& spec, CallSite site);
    struct Data {
        T from, to, value;
        AnimationSpec spec;
        int64_t start = 0, end = 0;
        bool running = false;
        ComposeHost* host = nullptr;
        // Where it is at `now`; still moving and `ask`: the reader's next frame is asked for.
        const T& at(int64_t now, bool ask) {
            if (!running) return value;
            bool finished = false;
            float elapsed = float(std::max<int64_t>(0, now - start)) / 1e6f;
            if constexpr (std::is_same_v<T, float>) {
                value = spec.valueAt(elapsed, from, to, finished);
            } else {
                float p = spec.progress(elapsed, finished);
                value = finished && p == 1.f ? to : detail::animationLerp(from, to, p);
            }
            running = !finished;
            if (running && ask) detail::animationNextFrame(host, end);
            return value;
        }
    };
    std::shared_ptr<Data> d_;
};

// Animates towards `target` whenever it changes (from where it is then, along `spec`).
template <class T>
AnimatedState<T> animateValueAsState(const T& target, const AnimationSpec& spec, CallSite site) {
    using Data = typename AnimatedState<T>::Data;
    auto& c = detail::Composer::require();
    auto& d = remember([&] { return std::make_shared<Data>(Data{.from = target, .to = target, .value = target, .spec = spec, .host = &c.host()}); }, site);
    if (!(d->to == target)) {
        int64_t now = c.frameTimeNanos();
        d->from = d->at(now, false);
        d->to = target;
        d->spec = spec;
        d->start = now;
        d->end = detail::animationEnd(spec, now);
        d->running = true;
        detail::notifyStateChanged(d.get());  // (read while laying out or drawing: that again)
    }
    AnimatedState<T> state;
    state.d_ = d;
    return state;
}

inline AnimatedState<float> animateFloatAsState(float target, const AnimationSpec& spec = spring(),
                                                CallSite site = CallSite::current()) {
    return animateValueAsState(target, spec, site);
}
inline AnimatedState<float> animateFloatAsState(float target, float durationMillis, CallSite site = CallSite::current()) {
    return animateValueAsState(target, tween(durationMillis), site);
}
inline AnimatedState<Color> animateColorAsState(Color target, const AnimationSpec& spec = spring(),
                                                CallSite site = CallSite::current()) {
    return animateValueAsState(target, spec, site);
}
inline AnimatedState<Color> animateColorAsState(Color target, float durationMillis, CallSite site = CallSite::current()) {
    return animateValueAsState(target, tween(durationMillis), site);
}

// A value animated by coroutines: co_await anim.animateTo(1.f, tween(500));
// Reading value() in a composable recomposes it on every animation frame.
template <class T>
class Animatable {
public:
    explicit Animatable(T initial) : s_(std::make_shared<State>(initial)) {}

    T value() const { return s_->value.get(); }
    T peek() const { return s_->value.peek(); }  // without subscribing
    T targetValue() const { return s_->target; }
    bool isRunning() const { return s_->running; }
    // Per second, as it moves now (0 at rest; float animatables).
    T velocity() const { return s_->velocity; }

    // Completes when the animation ends or another animateTo/snapTo/stop takes over.
    Task animateTo(T target, AnimationSpec spec = spring()) const { return run(s_, target, spec, T{}); }
    // Starting at `initialVelocity` (per second; a fling's), which a spring carries on: a
    // thrown thing overshoots, a released drag springs back from its speed (float only).
    Task animateTo(T target, AnimationSpec spec, T initialVelocity) const
        requires std::is_same_v<T, float>
    {
        return run(s_, target, spec, initialVelocity);
    }
    // Moves on from `initialVelocity`, slowing down as `spec` says, until it stops (or meets a
    // bound: updateBounds). Compose's animateDecay (float only).
    Task animateDecay(T initialVelocity, DecayAnimationSpec spec) const
        requires std::is_same_v<T, float>
    {
        return decay(s_, initialVelocity, spec);
    }
    // Keeps it within [lower, upper]: snapTo and animations stop at a bound (unset: none).
    void updateBounds(std::optional<T> lower, std::optional<T> upper) const
        requires std::is_same_v<T, float>
    {
        s_->lower = lower;
        s_->upper = upper;
        T v = s_->value.peek(), c = clampToBounds(*s_, v);
        if (c != v) snapTo(c);
    }
    std::optional<T> lowerBound() const { return s_->lower; }
    std::optional<T> upperBound() const { return s_->upper; }
    void snapTo(T v) const {
        ++s_->generation;
        s_->running = false;
        s_->velocity = T{};
        v = clampToBounds(*s_, v);
        s_->target = v;
        s_->value.set(v);
    }
    void stop() const {
        ++s_->generation;
        s_->running = false;
        s_->velocity = T{};
    }

    friend bool operator==(const Animatable& a, const Animatable& b) { return a.s_ == b.s_; }

private:
    struct State {
        explicit State(T v) : value(v), target(v) {}
        MutableState<T> value;
        T target;
        T velocity{};
        std::optional<T> lower, upper;  // float only
        uint64_t generation = 0;
        bool running = false;
    };

    static T clampToBounds(const State& s, T v) {
        if constexpr (std::is_same_v<T, float>) {
            if (s.lower && v < *s.lower) v = *s.lower;
            if (s.upper && v > *s.upper) v = *s.upper;
        }
        return v;
    }
    // Sets the value; false once it met a bound (where it stops).
    static bool step(State& s, T v, T velocity) {
        T c = clampToBounds(s, v);
        s.value.set(c);
        s.velocity = velocity;
        if constexpr (std::is_same_v<T, float>) {
            if (c != v) {
                s.velocity = 0;
                return false;
            }
        }
        return true;
    }

    static Task run(std::shared_ptr<State> s, T target, AnimationSpec spec, T initialVelocity) {
        uint64_t generation = ++s->generation;
        s->target = target;
        s->running = true;
        T start = s->value.peek();
        int64_t t0 = -1, last = -1;
        T previous = start;
        while (true) {
            int64_t now = co_await awaitFrame();
            if (s->generation != generation) co_return;
            if (t0 < 0) t0 = now;
            float ms = float(now - t0) / 1e6f;
            bool finished = false;
            T value{}, velocity{};
            if constexpr (std::is_same_v<T, float>) {
                if (spec.kind == AnimationSpec::Kind::Spring && spec.delayMillis <= 0) {  // physics: carries the start velocity
                    float x = 0, v = 0;
                    float scale = std::max(1.f, std::abs(start - target));
                    finished = spec.springMotion(ms, start - target, initialVelocity, x, v, 0.001f * scale);
                    value = finished ? target : target + x;
                    velocity = finished ? 0.f : v;
                } else {
                    value = spec.valueAt(ms, start, target, finished);
                    velocity = last >= 0 && now > last ? (value - previous) / (float(now - last) / 1e9f) : 0.f;
                    if (finished) velocity = 0;
                }
            } else {
                float p = spec.progress(ms, finished);
                value = finished && p == 1.f ? target : lerp(start, target, p);
            }
            previous = value;
            last = now;
            if (!step(*s, value, velocity)) break;  // met a bound
            if (finished) break;
        }
        s->running = false;
        s->velocity = T{};
    }

    static Task decay(std::shared_ptr<State> s, T initialVelocity, DecayAnimationSpec spec) {
        uint64_t generation = ++s->generation;
        s->running = true;
        T start = s->value.peek();
        s->target = clampToBounds(*s, spec.calculateTargetValue(start, initialVelocity));
        float duration = spec.durationMillis(initialVelocity);
        int64_t t0 = -1;
        while (true) {
            int64_t now = co_await awaitFrame();
            if (s->generation != generation) co_return;
            if (t0 < 0) t0 = now;
            float ms = float(now - t0) / 1e6f;
            bool done = ms >= duration;
            if (!step(*s, spec.valueAt(start, initialVelocity, std::min(ms, duration)), done ? 0.f : spec.velocityAt(initialVelocity, ms)) || done)
                break;
        }
        s->running = false;
        s->velocity = T{};
    }

    std::shared_ptr<State> s_;
};

// An animation whose target is only known as it goes (Compose's DeferredTargetAnimation, for
// approachLayout): the first target is taken as it is; a new one is animated to in `scope`.
// Copies share it: remember one.
//   auto& size = remember([] { return DeferredTargetAnimation<Size>(); });
//   CoroutineScope scope = rememberCoroutineScope();
//   Modifier().approachLayout([=](Size target) { size.updateTarget(target, scope); return !size.isIdle(); },
//                             [=](const ApproachMeasureScope& s, const MeasureNext& measure, const Constraints&, Offset&) {
//                                 Size now = size.updateTarget(s.lookaheadSize, scope);
//                                 return measure(Constraints::fixed(now.width, now.height));
//                             });
template <class T>
class DeferredTargetAnimation {
public:
    explicit DeferredTargetAnimation(AnimationSpec spec = spring()) : s_(std::make_shared<State>()) { s_->spec = spec; }
    // Its value now (read where it is, it is read again each frame it moves), animating towards
    // `target` once that changes.
    T updateTarget(T target, CoroutineScope& scope) const {
        if (!s_->animation) {
            s_->animation.emplace(target);
            s_->target = target;
            return target;
        }
        if (!(target == *s_->target)) {
            s_->target = target;
            scope.launch([a = *s_->animation, target, spec = s_->spec]() -> Task { co_await a.animateTo(target, spec); });
        }
        return s_->animation->value();
    }
    std::optional<T> pendingTarget() const { return isIdle() ? std::nullopt : s_->target; }
    // At its target, not moving (or never given one).
    bool isIdle() const { return !s_->animation || s_->animation->peek() == *s_->target; }

private:
    struct State {
        std::optional<Animatable<T>> animation;
        std::optional<T> target;
        AnimationSpec spec;
    };
    std::shared_ptr<State> s_;
};

namespace detail {
// An infinite transition's progress now along `spec`, from its start (set by the first read),
// asking for the next frame of what reads it: a composition (composed again), a layout (laid out
// again) or drawing (drawn again).
float infiniteProgress(const std::shared_ptr<int64_t>& start, const AnimationSpec& spec, ComposeHost* host);
} // namespace detail

// An InfiniteTransition's value as a state (Compose's animateFloat returns a State): read in a
// composition, the reader composes again every frame; read while drawing (graphicsLayer { },
// drawBehind, a Canvas) it is only drawn again, and while laying out (offset { }) laid out again.
//   auto pulse = rememberInfiniteTransition().animateFloatState(0.8f, 1.f, infiniteRepeatable(tween(600), RepeatMode::Reverse));
//   Box(Modifier().graphicsLayer([pulse](GraphicsLayerScope& l) { l.scaleX = l.scaleY = pulse.get(); }));
template <class T>
class InfiniteState {
public:
    T get() const { return detail::animationLerp(from_, to_, detail::infiniteProgress(start_, spec_, host_)); }
    T value() const { return get(); }

private:
    friend class InfiniteTransition;
    std::shared_ptr<int64_t> start_;
    T from_, to_;
    AnimationSpec spec_;
    ComposeHost* host_ = nullptr;
};

// Values that animate for as long as they are in the composition (loading spinners, pulses).
class InfiniteTransition {
public:
    float animateFloat(float from, float to, const AnimationSpec& spec) const;
    Color animateColor(Color from, Color to, const AnimationSpec& spec) const;
    // The same as states, to read where they are needed (in a composition only to create them).
    InfiniteState<float> animateFloatState(float from, float to, const AnimationSpec& spec) const;
    InfiniteState<Color> animateColorState(Color from, Color to, const AnimationSpec& spec) const;
    friend bool operator==(const InfiniteTransition& a, const InfiniteTransition& b) { return a.start_ == b.start_; }

private:
    float progress(const AnimationSpec& spec) const;
    std::shared_ptr<int64_t> start_ = std::make_shared<int64_t>(-1);
};

inline InfiniteTransition rememberInfiniteTransition(CallSite site = CallSite::current()) {
    return remember([] { return InfiniteTransition(); }, site);
}

namespace detail {
// An endless animation's clock read while drawing, as Compose's progress indicators read theirs
// in the draw phase: ms since it was composed. Reading it asks for the next frame to be drawn
// again, not composed or laid out, and nothing is asked for while the node isn't drawn.
class DrawClock {
public:
    explicit DrawClock(int64_t startNanos) : start_(startNanos) {}
    float millis(const LayoutNode& node) const;
    float progress(const LayoutNode& node, const AnimationSpec& spec) const;  // of an infiniteRepeatable
private:
    int64_t start_;
};
inline DrawClock rememberDrawClock(CallSite site = CallSite::current()) {
    return remember([] { return DrawClock(Composer::require().frameTimeNanos()); }, site);
}
} // namespace detail

// updateTransition: several values animating together towards what `targetState` asks for.
//   auto t = updateTransition(selected.get());
//   Color bg = t.animateColor([&](bool on) { return on ? cs.primary : cs.surface; });
//   float scale = t.animateFloat([](bool on) { return on ? 1.1f : 1.f; });
// currentState() catches up with targetState() once every value has arrived.
template <class S>
class Transition {
public:
    const S& targetState() const { return target_; }
    const S& currentState() const { return data_->current.get(); }
    bool isRunning() const { return data_->running; }

    float animateFloat(const std::function<float(const S&)>& valueFor, AnimationSpec spec = spring(),
                       CallSite site = CallSite::current()) const {
        return track(detail::animateValueFrom(valueFor(data_->initial), valueFor(target_), spec, site), valueFor(target_));
    }
    Color animateColor(const std::function<Color(const S&)>& valueFor, AnimationSpec spec = spring(),
                       CallSite site = CallSite::current()) const {
        Color target = valueFor(target_);
        Color v = detail::animateValueFrom(valueFor(data_->initial), target, spec, site);
        if (!(v == target)) data_->runningNow = true;
        return v;
    }

    struct Data {
        explicit Data(const S& s) : current(s), initial(s) {}
        MutableState<S> current;
        S initial;  // where the values start out from
        bool running = false, runningNow = false;
    };
    S target_;
    std::shared_ptr<Data> data_;

private:
    float track(float value, float target) const {
        if (value != target) data_->runningNow = true;
        return value;
    }
};

template <class S>
Transition<S> updateTransition(const S& targetState, CallSite site = CallSite::current()) {
    auto& data = remember([&] { return std::make_shared<typename Transition<S>::Data>(targetState); }, site);
    // Last composition's answer; the values report again in this one.
    data->running = data->runningNow || !(data->current.peek() == targetState);
    data->runningNow = false;
    SideEffect([data, targetState] {  // after the values have been read this time
        if (!data->runningNow && !(data->current.peek() == targetState)) data->current.set(targetState);
    });
    return Transition<S>{targetState, data};
}

// A transition's state kept outside it (Compose's MutableTransitionState): set targetState to
// start it — a target other than the initial state at once animates the first time it shows
// (an AnimatedVisibility that comes in as the screen opens); currentState catches up once the
// values have arrived, and isIdle says it rests there (an item faded out: remove it now).
//   auto shown = remember([] { MutableTransitionState<bool> s(false); s.setTargetState(true); return s; });
//   AnimatedVisibility(shown, Modifier(), fadeIn(), fadeOut(), [] { Text("Merhaba"); });
template <class S>
class MutableTransitionState {
public:
    explicit MutableTransitionState(S initialState) : data_(std::make_shared<Data>(std::move(initialState))) {}
    const S& currentState() const { return data_->current.get(); }  // observable
    const S& targetState() const { return data_->target.get(); }    // observable
    void setTargetState(S state) const { data_->target.set(std::move(state)); }
    bool isIdle() const { return currentState() == targetState() && !data_->running.get(); }  // observable
    friend bool operator==(const MutableTransitionState& a, const MutableTransitionState& b) { return a.data_ == b.data_; }

    struct Data {
        explicit Data(S s) : current(s), target(std::move(s)) {}
        MutableState<S> current, target;
        MutableState<bool> running{false};
    };
    std::shared_ptr<Data> data_;
};

// A transition that follows a MutableTransitionState (Compose's rememberTransition): from its
// currentState towards its targetState, telling it where the values have got to.
template <class S>
Transition<S> rememberTransition(const MutableTransitionState<S>& transitionState, CallSite site = CallSite::current()) {
    auto state = transitionState.data_;
    auto& data = remember([&] { return std::make_shared<typename Transition<S>::Data>(state->current.peek()); }, site);
    S target = state->target.get();
    data->running = data->runningNow || !(data->current.peek() == target);
    data->runningNow = false;
    SideEffect([data, state, target] {
        if (!data->runningNow && !(data->current.peek() == target)) data->current.set(target);
        bool running = data->runningNow || !(data->current.peek() == target);
        if (!(state->current.peek() == data->current.peek())) state->current.set(data->current.peek());
        if (state->running.peek() != running) state->running.set(running);
    });
    return Transition<S>{target, data};
}
template <class S>
Transition<S> updateTransition(const MutableTransitionState<S>& transitionState, CallSite site = CallSite::current()) {
    return rememberTransition(transitionState, site);
}

} // namespace kompoz
