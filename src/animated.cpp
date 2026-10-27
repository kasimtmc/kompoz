#include <kompoz/animated.hpp>
#include <kompoz/host.hpp>

#include <cmath>

namespace kompoz {

namespace {

// Expand/shrink and slide: shows `widthFraction` x `heightFraction` of the content, revealed
// from its far edge (like Compose's expandFrom = Bottom / End), offset by a fraction of its size.
struct RevealElement final : ModifierElement {
    float widthFraction = 1, heightFraction = 1, slideX = 0, slideY = 0;
    // Read as it is laid out instead: the fractions and the slide at that progress.
    std::optional<detail::Live<float>> progress;
    bool expandWidth = false, expandHeight = false;
    float hiddenX = 0, hiddenY = 0;

    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        float widthFraction = this->widthFraction, heightFraction = this->heightFraction, slideX = this->slideX, slideY = this->slideY;
        if (progress) {
            float p = std::max(0.f, (*progress)());
            widthFraction = expandWidth ? p : 1, heightFraction = expandHeight ? p : 1;
            slideX = (1 - p) * hiddenX, slideY = (1 - p) * hiddenY;
        }
        Constraints inner = c;
        if (widthFraction < 1) inner.minWidth = 0;
        if (heightFraction < 1) inner.minHeight = 0;
        Size s = next(inner);
        contentOffset = {slideX * s.width - (1 - widthFraction) * s.width,
                         slideY * s.height - (1 - heightFraction) * s.height};
        return c.constrain({s.width * widthFraction, s.height * heightFraction});
    }
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode&, const std::function<void()>& drawContent) const override {
        canvas.pushClip(bounds);
        drawContent();
        canvas.popClip();
    }
    bool clipsContent() const override { return true; }
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const RevealElement*>(&other);
        return o && !progress && !o->progress && o->widthFraction == widthFraction && o->heightFraction == heightFraction &&
               o->slideX == slideX && o->slideY == slideY;
    }
};

struct IgnoreInputElement final : ModifierElement {
    bool ignoresInput() const override { return true; }
    bool equals(const ModifierElement& other) const override { return dynamic_cast<const IgnoreInputElement*>(&other) != nullptr; }
};

EnterTransition enter(detail::TransitionParts parts) { return {parts}; }
ExitTransition exit(detail::TransitionParts parts) { return {parts}; }

} // namespace

namespace detail {

TransitionParts TransitionParts::merged(const TransitionParts& o) const {
    TransitionParts m = *this;
    if (o.fade) {
        m.fade = true;
        m.hiddenAlpha = o.hiddenAlpha;
    }
    m.expandWidth = expandWidth || o.expandWidth;
    m.expandHeight = expandHeight || o.expandHeight;
    if (o.slideX != 0) m.slideX = o.slideX;
    if (o.slideY != 0) m.slideY = o.slideY;
    if (o.scale) {
        m.scale = true;
        m.hiddenScale = o.hiddenScale;
        m.transformOrigin = o.transformOrigin;
    }
    if (!m.spec) m.spec = o.spec;
    return m;
}

Modifier ignoringInput(const Modifier& m) { return m.then(std::make_shared<IgnoreInputElement>()); }

Modifier TransitionParts::apply(const Modifier& m, float progress) const {
    Modifier out = m;
    if (fade && progress < 1) out = out.alpha(lerp(hiddenAlpha, 1, std::clamp(progress, 0.f, 1.f)));
    if (scale && progress < 1) {
        float s = lerp(hiddenScale, 1, std::max(0.f, progress));
        out = out.graphicsLayer({.scaleX = s, .scaleY = s, .transformOrigin = transformOrigin});
    }
    bool reveal = (expandWidth || expandHeight || slideX != 0 || slideY != 0) && progress < 1;
    if (reveal) {
        auto e = std::make_shared<RevealElement>();
        float p = std::max(0.f, progress);
        e->widthFraction = expandWidth ? p : 1;
        e->heightFraction = expandHeight ? p : 1;
        e->slideX = (1 - p) * slideX;
        e->slideY = (1 - p) * slideY;
        out = out.then(std::move(e));
    }
    return out;
}

Modifier TransitionParts::apply(const Modifier& m, const Live<float>& progress) const {
    Modifier out = m;
    if (fade) out = animatedAlpha(out, Live<float>{[progress, hidden = hiddenAlpha] { return lerp(hidden, 1, std::clamp(progress(), 0.f, 1.f)); }});
    if (scale)
        out = out.graphicsLayer([progress, hidden = hiddenScale, origin = transformOrigin](GraphicsLayerScope& l) {
            l.scaleX = l.scaleY = lerp(hidden, 1, std::max(0.f, progress()));
            l.transformOrigin = origin;
        });
    if (expandWidth || expandHeight || slideX != 0 || slideY != 0) {
        auto e = std::make_shared<RevealElement>();
        e->progress = progress;
        e->expandWidth = expandWidth, e->expandHeight = expandHeight;
        e->hiddenX = slideX, e->hiddenY = slideY;
        out = out.then(std::move(e));
    }
    return out;
}

float TransitionProgress::at(int64_t frameTime, bool* done) const {
    bool finished = from == to;
    float t = finished ? 1.f : spec.progress(float(frameTime - start) / 1e6f, finished);
    if (done) *done = finished;
    return finished ? to : lerp(from, to, t);
}

void TransitionProgress::turn(float target, const AnimationSpec& with, int64_t frameTime) {
    from = at(frameTime);
    to = target;
    spec = with;
    start = frameTime;
    end = animationEnd(spec, frameTime);
}

Live<float> live(const std::shared_ptr<const TransitionProgress>& progress) {
    return {[progress] {
        bool done = false;
        float p = progress->at(animationReadTime(progress->host), &done);
        if (!done) animationNextFrame(progress->host, progress->end);
        return p;
    }};
}

float totalDurationMillis(const AnimationSpec& spec) {
    switch (spec.kind) {
    case AnimationSpec::Kind::Snap: return spec.delayMillis;
    case AnimationSpec::Kind::Tween:
    case AnimationSpec::Kind::Keyframes: return spec.delayMillis + spec.durationMillis;
    case AnimationSpec::Kind::Spring: break;
    }
    bool settled = false;
    float t = 0;
    for (; t < 10000 && !settled; t += 4) spec.progress(t, settled);
    return t;
}

void animatedContentContainer(const Modifier& modifier, const std::shared_ptr<AnimatedSize>& size, const std::optional<SizeTransform>& transform,
                              int target, Alignment alignment, const Content& children) {
    Modifier m = transform && transform->clip ? modifier.clipToBounds() : modifier;
    Composer::require().emit(
        keyOf(CallSite::current()),
        [&](LayoutNode& node) {
            node.modifier = m;
            node.measurePolicy = [size, transform, target, alignment](LayoutNode& n, const Constraints& c) {
                Size biggest{c.minWidth, c.minHeight}, wanted{};
                std::vector<Size> sizes;
                for (size_t i = 0; i < n.children.size(); ++i) {
                    Size s = n.children[i]->measure(c.loosen());
                    sizes.push_back(s);
                    biggest = {std::max(biggest.width, s.width), std::max(biggest.height, s.height)};
                    if (int(i) == target) wanted = s;
                }
                Size shown = biggest;
                if (transform && target >= 0) {
                    AnimatedSize& a = *size;
                    int64_t now = n.host()->frameTime();
                    auto at = [&](bool keep) {  // where the size is now
                        if (a.start < 0) return a.to;
                        bool finished = false;
                        float p = transform->spec.progress(float(now - a.start) / 1e6f, finished);
                        if (finished && keep) a.start = -1;
                        return finished ? a.to : Size{lerp(a.from.width, a.to.width, p), lerp(a.from.height, a.to.height, p)};
                    };
                    if (detail::isProbeMeasure()) {
                        shown = a.known ? at(false) : wanted;
                    } else {
                        if (!a.known) {
                            a = {wanted, wanted, -1, true};
                        } else if (!(wanted == a.to)) {
                            a.from = at(false);
                            a.to = wanted;
                            a.start = now;
                        }
                        shown = at(true);
                        if (a.start >= 0) n.host()->requestLayoutFrame();
                    }
                }
                Size own = c.constrain(detail::toWholePx(shown));  // (in whole px: Compose animates an IntSize)
                for (size_t i = 0; i < n.children.size(); ++i) {
                    Offset o = alignment.align(sizes[i], own);
                    n.children[i]->place(o.x, o.y);
                }
                return own;
            };
            node.drawContent = {};
        },
        children);
}

bool VisibilityState::childrenLeaving() {
    bool leaving = false;
    std::erase_if(children, [&](const std::weak_ptr<bool>& c) {
        auto child = c.lock();
        if (child && *child) leaving = true;
        return !child;
    });
    return leaving;
}

bool VisibilityState::hasChildren() {
    std::erase_if(children, [](const std::weak_ptr<bool>& c) { return c.expired(); });
    return !children.empty();
}

} // namespace detail

Modifier AnimatedVisibilityScope::animateEnterExit(CallSite site) const { return animateEnterExit(fadeIn(), fadeOut(), site); }

Modifier AnimatedVisibilityScope::animateEnterExit(const EnterTransition& enter, const ExitTransition& exit, CallSite site) const {
    struct Child {
        std::shared_ptr<detail::TransitionProgress> clock = std::make_shared<detail::TransitionProgress>();
        std::shared_ptr<bool> leaving = std::make_shared<bool>(false);
    };
    ComposableScope scope(site);
    auto& c = detail::Composer::require();
    int64_t now = c.frameTimeNanos();
    detail::VisibilityState& parent = *state_;
    const auto& parts = parent.visible ? enter.parts : exit.parts;
    auto& child = remember([&] {
        // Composed as the content comes in: it comes in too (from hidden); otherwise shown.
        bool entering = parent.visible && parent.shownAt == now;
        Child made;
        made.clock->from = made.clock->to = entering ? 0.f : 1.f;
        made.clock->host = &c.host();
        if (entering) made.clock->turn(1, parts.spec.value_or(defaultVisibilitySpec()), now);
        parent.children.push_back(made.leaving);
        return made;
    });
    float to = parent.visible ? 1.f : 0.f;
    if (to != child.clock->to) child.clock->turn(to, parts.spec.value_or(defaultVisibilitySpec()), now);  // turned: from where it is
    bool done = false;
    float progress = child.clock->at(now, &done);
    *child.leaving = !parent.visible && !done;
    // On the way it is laid out and drawn (leaving, its parent looks again every frame until it is out).
    return done ? parts.apply(Modifier(), progress) : parts.apply(Modifier(), detail::live(child.clock));
}

EnterTransition fadeIn(float initialAlpha, AnimationSpec spec) {
    return enter({.fade = true, .hiddenAlpha = initialAlpha, .spec = spec});
}
EnterTransition expandVertically(AnimationSpec spec) { return enter({.expandHeight = true, .spec = spec}); }
EnterTransition expandHorizontally(AnimationSpec spec) { return enter({.expandWidth = true, .spec = spec}); }
EnterTransition expandIn(AnimationSpec spec) { return enter({.expandWidth = true, .expandHeight = true, .spec = spec}); }
EnterTransition slideInVertically(float fraction, AnimationSpec spec) { return enter({.slideY = fraction, .spec = spec}); }
EnterTransition slideInHorizontally(float fraction, AnimationSpec spec) {
    return enter({.slideX = fraction, .spec = spec});
}
EnterTransition scaleIn(float initialScale, Offset transformOrigin, AnimationSpec spec) {
    return enter({.scale = true, .hiddenScale = initialScale, .transformOrigin = transformOrigin, .spec = spec});
}
ExitTransition scaleOut(float targetScale, Offset transformOrigin, AnimationSpec spec) {
    return exit({.scale = true, .hiddenScale = targetScale, .transformOrigin = transformOrigin, .spec = spec});
}
ExitTransition fadeOut(float targetAlpha, AnimationSpec spec) {
    return exit({.fade = true, .hiddenAlpha = targetAlpha, .spec = spec});
}
ExitTransition shrinkVertically(AnimationSpec spec) { return exit({.expandHeight = true, .spec = spec}); }
ExitTransition shrinkHorizontally(AnimationSpec spec) { return exit({.expandWidth = true, .spec = spec}); }
ExitTransition shrinkOut(AnimationSpec spec) { return exit({.expandWidth = true, .expandHeight = true, .spec = spec}); }
ExitTransition slideOutVertically(float fraction, AnimationSpec spec) { return exit({.slideY = fraction, .spec = spec}); }
ExitTransition slideOutHorizontally(float fraction, AnimationSpec spec) {
    return exit({.slideX = fraction, .spec = spec});
}

void AnimatedVisibility(bool visible, const Content& content, CallSite site) {
    AnimatedVisibility(visible, Modifier(), fadeIn() + expandVertically(), fadeOut() + shrinkVertically(), content, site);
}

void AnimatedVisibility(bool visible, const Modifier& modifier, const EnterTransition& enterTransition,
                        const ExitTransition& exitTransition, const Content& content, CallSite site) {
    AnimatedVisibility(visible, modifier, enterTransition, exitTransition, [&](const AnimatedVisibilityScope&) { content(); }, site);
}

namespace {

// AnimatedVisibility, starting out shown or not as `initiallyVisible` says; `told` (if any)
// hears when it is all in or all gone.
void animatedVisibility(bool visible, bool initiallyVisible, const std::shared_ptr<MutableTransitionState<bool>::Data>& told,
                        const Modifier& modifier, const EnterTransition& enterTransition, const ExitTransition& exitTransition,
                        const std::function<void(const AnimatedVisibilityScope&)>& content) {
    const auto& parts = visible ? enterTransition.parts : exitTransition.parts;
    auto& composer = detail::Composer::require();
    int64_t now = composer.frameTimeNanos();
    // Its progress, worked out here when it turns and gets there, read on the way where it is
    // used (as the content is laid out and drawn).
    auto clock = remember([&] {
        auto made = std::make_shared<detail::TransitionProgress>();
        made->from = made->to = initiallyVisible ? 1.f : 0.f;
        made->host = &composer.host();
        return made;
    });
    if (clock->to != (visible ? 1.f : 0.f)) clock->turn(visible ? 1.f : 0.f, parts.spec.value_or(defaultVisibilitySpec()), now);
    bool done = false;
    float progress = clock->at(now, &done);
    auto state = remember([] { return std::make_shared<detail::VisibilityState>(); });
    auto tell = [&](bool arrived) {
        if (!told) return;
        SideEffect([told, visible, arrived] {
            if (arrived && told->current.peek() != visible) told->current.set(visible);
            if (told->running.peek() == arrived) told->running.set(!arrived);
        });
    };
    // Fully hidden: the content leaves the composition, once its shared elements are done
    // moving and its children with their own exits are gone (they hear of the hiding first).
    if (!visible && done) {
        bool waiting = now < state->holdUntil || (state->visible ? state->hasChildren() : state->childrenLeaving());
        if (!waiting) {
            state->composed = false, state->started = true;
            tell(true);
            return;
        }
        composer.requestFrame();
    }
    tell(visible && done);
    if (visible && !state->composed && state->started) state->shownAt = now;  // coming (back) in
    state->composed = state->started = true;
    state->visible = visible;
    state->running = visible ? !done : true;
    if (!done) composer.requestFrameAt(clock->end);  // composed again when it gets there
    Modifier m = done ? parts.apply(modifier, progress) : parts.apply(modifier, detail::live(clock));
    Box(visible ? m : detail::ignoringInput(m), [&] { content(AnimatedVisibilityScope(state)); });
}

} // namespace

void AnimatedVisibility(bool visible, const Modifier& modifier, const EnterTransition& enterTransition,
                        const ExitTransition& exitTransition, const std::function<void(const AnimatedVisibilityScope&)>& content,
                        CallSite site) {
    ComposableScope scope(site);
    animatedVisibility(visible, visible, nullptr, modifier, enterTransition, exitTransition, content);
}

void AnimatedVisibility(const MutableTransitionState<bool>& visibleState, const Modifier& modifier, const EnterTransition& enter,
                        const ExitTransition& exit, const Content& content, CallSite site) {
    AnimatedVisibility(visibleState, modifier, enter, exit, [&](const AnimatedVisibilityScope&) { content(); }, site);
}

void AnimatedVisibility(const MutableTransitionState<bool>& visibleState, const Modifier& modifier, const EnterTransition& enter,
                        const ExitTransition& exit, const std::function<void(const AnimatedVisibilityScope&)>& content, CallSite site) {
    ComposableScope scope(site);
    const auto& d = visibleState.data_;
    animatedVisibility(d->target.get(), d->current.peek(), d, modifier, enter, exit, content);
}

} // namespace kompoz
