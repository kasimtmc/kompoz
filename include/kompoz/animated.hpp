#pragma once

// AnimatedVisibility, AnimatedContent and Crossfade with fade / expand / shrink / slide
// transitions, as in androidx.compose.animation.
//
//   AnimatedVisibility(expanded.get(), [&] { Text("Detaylar"); });
//   AnimatedContent(count.get(), [](const int& n) { Text(std::to_string(n)); });

#include "animation.hpp"
#include "layout.hpp"
#include "runtime.hpp"
#include "widgets.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace kompoz {

namespace detail {

// What a piece of content looks like when hidden; progress 1 = fully shown.
struct TransitionParts {
    bool fade = false;
    float hiddenAlpha = 0;
    bool expandWidth = false, expandHeight = false;
    float slideX = 0, slideY = 0;  // offset when hidden, as a fraction of the content size
    bool scale = false;
    float hiddenScale = 0;
    Offset transformOrigin{0.5f, 0.5f};
    std::optional<AnimationSpec> spec;

    TransitionParts merged(const TransitionParts& o) const;
    Modifier apply(const Modifier& m, float progress) const;
    // With the progress read where it is used: as the content is laid out and drawn.
    Modifier apply(const Modifier& m, const Live<float>& progress) const;
};

// A transition's progress (0: hidden, 1: shown), from where it was when it last turned, along
// its spec: worked out at a frame by the composition (at), or where it is read (live: as the
// content is laid out and drawn, asking for their next frames until it is done).
struct TransitionProgress {
    float from = 0, to = 0;
    AnimationSpec spec;
    int64_t start = 0, end = 0;
    ComposeHost* host = nullptr;
    float at(int64_t frameTime, bool* done = nullptr) const;
    // On to `target` from where it is at `frameTime`, along `spec`.
    void turn(float target, const AnimationSpec& spec, int64_t frameTime);
};
Live<float> live(const std::shared_ptr<const TransitionProgress>& progress);

// Content on its way out takes no taps or focus (the cards of a list fading out can't be
// picked while the next screen comes in).
Modifier ignoringInput(const Modifier& m);

// Whether a piece of content is shown or on its way out, and if it is still animating.
struct VisibilityState {
    bool visible = true;
    bool running = false;
    int64_t holdUntil = 0;  // leaving content stays until then (frame time): a shared element in it still moves
    // Its children with their own enter and exit (animateEnterExit), each true while on its way
    // out: leaving content stays until none is.
    std::vector<std::weak_ptr<bool>> children;
    bool childrenLeaving();
    bool hasChildren();
    // When the content came (back) in through a transition (frame time; -1: shown from the
    // start): children composed then come in with it.
    int64_t shownAt = -1;
    bool composed = false, started = false;  // AnimatedVisibility: content composed last time / ever composed
};

} // namespace detail


struct EnterTransition {
    detail::TransitionParts parts;
    friend EnterTransition operator+(const EnterTransition& a, const EnterTransition& b) {
        return {a.parts.merged(b.parts)};
    }
    // None at all: shown at once (the content's children may still animateEnterExit).
    static const EnterTransition None;
};
struct ExitTransition {
    detail::TransitionParts parts;
    friend ExitTransition operator+(const ExitTransition& a, const ExitTransition& b) { return {a.parts.merged(b.parts)}; }
    static const ExitTransition None;  // gone at once, once its children's exits are done
};
inline const EnterTransition EnterTransition::None{{.spec = snap()}};
inline const ExitTransition ExitTransition::None{{.spec = snap()}};

inline AnimationSpec defaultVisibilitySpec() { return spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow); }

EnterTransition fadeIn(float initialAlpha = 0, AnimationSpec spec = defaultVisibilitySpec());
EnterTransition expandVertically(AnimationSpec spec = defaultVisibilitySpec());
EnterTransition expandHorizontally(AnimationSpec spec = defaultVisibilitySpec());
EnterTransition expandIn(AnimationSpec spec = defaultVisibilitySpec());
EnterTransition slideInVertically(float initialOffsetFraction = -0.5f, AnimationSpec spec = defaultVisibilitySpec());
EnterTransition slideInHorizontally(float initialOffsetFraction = -0.5f, AnimationSpec spec = defaultVisibilitySpec());
// Grows from `initialScale` around `transformOrigin` (drawn through a graphicsLayer).
EnterTransition scaleIn(float initialScale = 0, Offset transformOrigin = {0.5f, 0.5f},
                        AnimationSpec spec = defaultVisibilitySpec());
ExitTransition fadeOut(float targetAlpha = 0, AnimationSpec spec = defaultVisibilitySpec());
ExitTransition scaleOut(float targetScale = 0, Offset transformOrigin = {0.5f, 0.5f},
                        AnimationSpec spec = defaultVisibilitySpec());
ExitTransition shrinkVertically(AnimationSpec spec = defaultVisibilitySpec());
ExitTransition shrinkHorizontally(AnimationSpec spec = defaultVisibilitySpec());
ExitTransition shrinkOut(AnimationSpec spec = defaultVisibilitySpec());
ExitTransition slideOutVertically(float targetOffsetFraction = -0.5f, AnimationSpec spec = defaultVisibilitySpec());
ExitTransition slideOutHorizontally(float targetOffsetFraction = -0.5f, AnimationSpec spec = defaultVisibilitySpec());

// The content of an AnimatedVisibility or AnimatedContent, for shared elements inside it to
// know whether they enter or leave (Compose's AnimatedVisibilityScope).
class AnimatedVisibilityScope {
public:
    AnimatedVisibilityScope() : state_(std::make_shared<detail::VisibilityState>()) {}
    explicit AnimatedVisibilityScope(std::shared_ptr<detail::VisibilityState> state) : state_(std::move(state)) {}
    bool targetVisible() const { return state_->visible; }        // false while it leaves
    bool isTransitionRunning() const { return state_->running; }  // entering or leaving
    const std::shared_ptr<detail::VisibilityState>& state() const { return state_; }
    // A child's own enter and exit on top of the content's (Compose's Modifier.animateEnterExit):
    // it comes in as the content does and leaves as it leaves, its own way, and the content
    // stays until it is gone. Read while composing the content:
    //   AnimatedVisibility(open, Modifier(), EnterTransition::None, ExitTransition::None, [&](const AnimatedVisibilityScope& s) {
    //       Box(Modifier().fillMaxSize().background(scrim).then(s.animateEnterExit(fadeIn(), fadeOut())));
    //       Box(s.animateEnterExit(slideInVertically(1), slideOutVertically(1)).fillMaxWidth().height(240));
    //   });
    Modifier animateEnterExit(const EnterTransition& enter, const ExitTransition& exit, CallSite site = CallSite::current()) const;
    Modifier animateEnterExit(CallSite site = CallSite::current()) const;  // fadeIn(), fadeOut()
    friend bool operator==(const AnimatedVisibilityScope& a, const AnimatedVisibilityScope& b) { return a.state_ == b.state_; }

private:
    std::shared_ptr<detail::VisibilityState> state_;
};
using AnimatedContentScope = AnimatedVisibilityScope;

// Shows `content` with `enter` when `visible` becomes true and hides it with `exit`; once
// hidden the content leaves the composition (its state is disposed).
void AnimatedVisibility(bool visible, const Content& content, CallSite site = CallSite::current());
void AnimatedVisibility(bool visible, const Modifier& modifier, const EnterTransition& enter,
                        const ExitTransition& exit, const Content& content, CallSite site = CallSite::current());
// ...with its AnimatedVisibilityScope, for shared elements.
void AnimatedVisibility(bool visible, const Modifier& modifier, const EnterTransition& enter, const ExitTransition& exit,
                        const std::function<void(const AnimatedVisibilityScope&)>& content, CallSite site = CallSite::current());
// Shown as visibleState's targetState says, starting out from its currentState (Compose's
// AnimatedVisibility(visibleState)): a state made hidden with a visible target comes in the
// first time; its currentState follows once the content is all in or all gone (then isIdle).
void AnimatedVisibility(const MutableTransitionState<bool>& visibleState, const Modifier& modifier, const EnterTransition& enter,
                        const ExitTransition& exit, const Content& content, CallSite site = CallSite::current());
void AnimatedVisibility(const MutableTransitionState<bool>& visibleState, const Modifier& modifier, const EnterTransition& enter,
                        const ExitTransition& exit, const std::function<void(const AnimatedVisibilityScope&)>& content,
                        CallSite site = CallSite::current());

// AnimatedContent's own size between two contents (Compose's SizeTransform): it animates from
// the old content's size to the new one's, clipping what is drawn to it unless clip is false.
struct SizeTransform {
    bool clip = true;
    AnimationSpec spec = spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow);
    friend bool operator==(const SizeTransform&, const SizeTransform&) = default;
};

struct ContentTransform {
    EnterTransition enter;
    ExitTransition exit;
    std::optional<SizeTransform> sizeTransform = SizeTransform{};  // std::nullopt: as big as the contents shown
    // The entering content's z-index among the contents shown (Compose's targetContentZIndex):
    // the higher on top, newer content on top of equal ones; -1 lets it come in under the one
    // leaving (going back).
    float targetContentZIndex = 0;
    // Compose's `using`: (enter togetherWith exit) using SizeTransform(clip = false).
    ContentTransform withSizeTransform(std::optional<SizeTransform> transform) const {
        ContentTransform t = *this;
        t.sizeTransform = transform;
        return t;
    }
    ContentTransform withTargetContentZIndex(float zIndex) const {
        ContentTransform t = *this;
        t.targetContentZIndex = zIndex;
        return t;
    }
};
inline ContentTransform togetherWith(EnterTransition enter, ExitTransition exit) { return {enter, exit}; }

inline ContentTransform defaultContentTransform() { return togetherWith(fadeIn(0, tween(220, 90)), fadeOut(0, tween(90))); }

// AnimatedContent's transitionSpec: the transform for going from one state to the next, so the
// direction can follow them (a counter's digits slide up when it grows, down when it shrinks).
template <class T>
using ContentTransitionSpec = std::function<ContentTransform(const T& initialState, const T& targetState)>;

namespace detail {
struct AnimatedSize {
    Size from, to;
    int64_t start = -1;  // when it set off from `from` (frame time); -1: at `to`
    bool known = false;
};
// The container: children laid over each other at `alignment`; with a SizeTransform its size
// follows child `target`'s, animating.
void animatedContentContainer(const Modifier& modifier, const std::shared_ptr<AnimatedSize>& size,
                              const std::optional<SizeTransform>& transform, int target, Alignment alignment,
                              const Content& children);
// How long an animation takes, its delay included (a spring: until it settles), in ms.
float totalDurationMillis(const AnimationSpec& spec);
} // namespace detail

// How AnimatedContent places its contents and tells them apart.
template <class T>
struct AnimatedContentOptions {
    // Where the contents sit in it — while its size animates, and when they differ in size
    // (Compose's contentAlignment): Alignment::Center keeps them centered as it grows.
    Alignment contentAlignment = Alignment::TopStart;
    // Which states are the same content (Compose's contentKey): a new state with the key of the
    // one shown takes its place without a transition, keeping its state (Loading → Loaded
    // animates, fresh data in Loaded doesn't). Unset, every different state is new content.
    std::function<uint64_t(const T&)> contentKey;
    // A transition to `seekState` shown part way (Compose's SeekableTransitionState.seekTo, as
    // a back gesture shows the screen below): the transform transitionSpec gives, at
    // `seekFraction` (0..1) of its duration. When targetState becomes it, the transition carries
    // on from there; unset before that, its content goes and the one shown is whole again.
    std::optional<T> seekState;
    float seekFraction = 0;
};

// Animates between the content for the old and the new `targetState` whenever it changes.
// The content gets its AnimatedContentScope too, for shared elements (see shared_transition.hpp).
template <class T>
void AnimatedContent(const T& targetState,
                     const std::function<void(const AnimatedContentScope&, const std::type_identity_t<T>&)>& content,
                     const Modifier& modifier, const ContentTransitionSpec<std::type_identity_t<T>>& transitionSpec,
                     const AnimatedContentOptions<std::type_identity_t<T>>& options = {}, CallSite site = CallSite::current()) {
    struct Item {
        T state;
        uint64_t id;
        int64_t start;
        bool exiting;
        float from, progress;  // exiting items fade from where they were
        detail::TransitionParts enter, exit;  // as the transform it came with / left with said
        float zIndex = 0;                     // the transform it came with gave it
        std::shared_ptr<detail::VisibilityState> visibility = std::make_shared<detail::VisibilityState>();
        bool seeking = false;  // seekState's, coming in part way
        std::shared_ptr<const detail::TransitionProgress> clock;  // on its way: its progress, read as it is laid out and drawn
    };
    struct State {
        std::vector<Item> items;
        T target;
        uint64_t nextId = 1;
        std::optional<SizeTransform> sizeTransform;
        // The seeked transition: how the content shown leaves, its size, how long it takes
        // and how far into it (ms) it was last shown.
        detail::TransitionParts seekExit;
        std::optional<SizeTransform> seekSize;
        float seekDuration = 0, seekTime = 0;
    };
    ComposableScope scope(site);
    auto& c = detail::Composer::require();
    int64_t now = c.frameTimeNanos();
    auto& s = remember([&] {
        return State{{Item{targetState, 0, now, false, 1, 1, {}, {}}}, targetState, 1, transitionSpec(targetState, targetState).sizeTransform};
    });
    if (!(s.target == targetState) && options.contentKey && options.contentKey(s.target) == options.contentKey(targetState)) {
        for (auto& it : s.items)  // the same content: the one shown takes the new state
            if (!it.exiting) it.state = targetState;
        s.target = targetState;
    }
    auto seeked = [&] { return std::find_if(s.items.begin(), s.items.end(), [](const Item& it) { return it.seeking; }); };
    bool seek = options.seekState && !(*options.seekState == s.target);
    if (auto it = seeked(); it != s.items.end() && !(seek && it->state == *options.seekState) && !(it->state == targetState))
        s.items.erase(it);  // called off: the one shown is whole again
    if (seek && seeked() == s.items.end()) {
        ContentTransform t = transitionSpec(s.target, *options.seekState);
        s.items.push_back({*options.seekState, s.nextId++, now, false, 0, 0, t.enter.parts, {}, t.targetContentZIndex});
        s.items.back().seeking = true;
        s.items.back().visibility->shownAt = now;
        s.seekExit = t.exit.parts;
        s.seekSize = t.sizeTransform;
        s.seekDuration = std::max(detail::totalDurationMillis(t.enter.parts.spec.value_or(defaultVisibilitySpec())),
                                  detail::totalDurationMillis(t.exit.parts.spec.value_or(defaultVisibilitySpec())));
    }
    if (!(s.target == targetState)) {
        if (auto in = seeked(); in != s.items.end() && in->state == targetState) {
            // The seeked transition: on from where it was shown, along the same timeline.
            int64_t start = now - int64_t(s.seekTime * 1e6f);
            for (auto& it : s.items)
                if (!it.exiting && !it.seeking) {
                    it.exiting = true;
                    it.from = 1;
                    it.start = start;
                    it.exit = s.seekExit;
                }
            in->seeking = false;
            in->start = start;
            s.sizeTransform = s.seekSize;
        } else {
            std::erase_if(s.items, [](const Item& it) { return it.seeking; });  // seeked elsewhere: gone
            ContentTransform t = transitionSpec(s.target, targetState);
            for (auto& it : s.items)
                if (!it.exiting) {
                    it.exiting = true;
                    it.from = it.progress;
                    it.start = now;
                    it.exit = t.exit.parts;
                }
            s.items.push_back({targetState, s.nextId++, now, false, 0, 0, t.enter.parts, {}, t.targetContentZIndex});
            s.items.back().visibility->shownAt = now;
            s.sizeTransform = t.sizeTransform;
        }
        s.target = targetState;
    }
    bool seeking = seeked() != s.items.end();
    if (seeking) s.seekTime = std::clamp(options.seekFraction, 0.f, 1.f) * s.seekDuration;
    bool animating = false;
    int64_t due = INT64_MAX;  // when the first one on its way gets there
    for (auto& it : s.items) {
        bool done = false;
        it.clock = nullptr;
        if (it.seeking) {  // in as far as the seek says, and the one shown out as far
            it.progress = it.enter.spec.value_or(defaultVisibilitySpec()).progress(s.seekTime, done);
            continue;
        }
        if (seeking && !it.exiting) {
            it.progress = 1 - s.seekExit.spec.value_or(defaultVisibilitySpec()).progress(s.seekTime, done);
            continue;
        }
        if (it.id == 0 && !it.exiting) {  // the initial content is just shown
            it.progress = 1;
            continue;
        }
        const auto& parts = it.exiting ? it.exit : it.enter;
        AnimationSpec spec = parts.spec.value_or(defaultVisibilitySpec());
        bool finished = false;
        float t = spec.progress(float(now - it.start) / 1e6f, finished);
        it.progress = finished ? (it.exiting ? 0.f : 1.f) : it.exiting ? it.from * (1 - t) : t;
        if (!finished) {
            animating = true;
            auto clock = std::make_shared<detail::TransitionProgress>();
            clock->from = it.exiting ? it.from : 0.f, clock->to = it.exiting ? 0.f : 1.f;
            clock->spec = spec, clock->start = it.start, clock->end = detail::animationEnd(spec, it.start), clock->host = &c.host();
            due = std::min(due, clock->end);
            it.clock = std::move(clock);
        }
    }
    bool held = false;  // leaving content whose shared elements still move
    std::erase_if(s.items, [&](const Item& it) {
        if (!it.exiting || it.progress > 0) return false;
        // Its children with exits of their own heard of the leaving (visible false) last time.
        if (now < it.visibility->holdUntil || (it.visibility->visible ? it.visibility->hasChildren() : it.visibility->childrenLeaving()))
            return !(held = true);
        return true;
    });
    // Composed again as each gets there (or each frame while leaving content is held); on the
    // way they are only laid out and drawn again.
    if (held) c.requestFrame();
    else if (animating) c.requestFrameAt(due);
    int target = -1;
    for (size_t i = 0; i < s.items.size(); ++i) {
        auto& it = s.items[i];
        it.visibility->visible = !it.exiting;
        it.visibility->running = it.exiting || it.progress < 1 || it.seeking;
        if (!it.exiting && !it.seeking) target = int(i);
    }
    auto& size = remember([] { return std::make_shared<detail::AnimatedSize>(); });
    detail::animatedContentContainer(modifier, size, s.sizeTransform, target, options.contentAlignment, [&] {
        for (const auto& it : s.items) {
            key(it.id, [&] {
                const detail::TransitionParts& parts = it.exiting ? it.exit : seeking && !it.seeking ? s.seekExit : it.enter;
                Modifier m = it.clock ? parts.apply(Modifier().zIndex(it.zIndex), detail::live(it.clock))
                                      : parts.apply(Modifier().zIndex(it.zIndex), it.progress);
                Box(it.exiting ? detail::ignoringInput(m) : m, [&] { content(AnimatedContentScope(it.visibility), it.state); });
            });
        }
    });
}
template <class T>
void AnimatedContent(const T& targetState,
                     const std::function<void(const AnimatedContentScope&, const std::type_identity_t<T>&)>& content,
                     const Modifier& modifier = {}, const ContentTransform& transform = defaultContentTransform(),
                     const AnimatedContentOptions<std::type_identity_t<T>>& options = {}, CallSite site = CallSite::current()) {
    AnimatedContent<T>(targetState, content, modifier, ContentTransitionSpec<T>([transform](const T&, const T&) { return transform; }),
                       options, site);
}
template <class T>
void AnimatedContent(const T& targetState, const std::function<void(const std::type_identity_t<T>&)>& content,
                     const Modifier& modifier = {}, const ContentTransform& transform = defaultContentTransform(),
                     const AnimatedContentOptions<std::type_identity_t<T>>& options = {}, CallSite site = CallSite::current()) {
    AnimatedContent<T>(targetState, [&](const AnimatedContentScope&, const T& state) { content(state); }, modifier, transform, options, site);
}
template <class T>
void AnimatedContent(const T& targetState, const std::function<void(const std::type_identity_t<T>&)>& content, const Modifier& modifier,
                     const ContentTransitionSpec<std::type_identity_t<T>>& transitionSpec,
                     const AnimatedContentOptions<std::type_identity_t<T>>& options = {}, CallSite site = CallSite::current()) {
    AnimatedContent<T>(targetState, [&](const AnimatedContentScope&, const T& state) { content(state); }, modifier, transitionSpec,
                       options, site);
}

// AnimatedContent with a plain cross-fade (its size isn't animated).
template <class T>
void Crossfade(const T& targetState, const std::function<void(const std::type_identity_t<T>&)>& content, const Modifier& modifier = {},
               AnimationSpec spec = tween(300), CallSite site = CallSite::current()) {
    AnimatedContent<T>(targetState, content, modifier, togetherWith(fadeIn(0, spec), fadeOut(0, spec)).withSizeTransform(std::nullopt),
                       {}, site);
}

} // namespace kompoz
