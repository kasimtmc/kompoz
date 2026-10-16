#pragma once

// anchoredDraggable, as Compose's: something the user drags between a few positions (anchors),
// each standing for a value; let go, it settles on one — the next one if it was dragged far
// enough past half the way (or flung), else back. Swipe-to-reveal rows, custom sheets, a
// three-way switch. The state holds the offset; place the content with it (read it in an
// offset { } lambda, so dragging only lays out again).
//
//   enum class DragValue { Start, Center, End };
//   auto state = rememberAnchoredDraggableState(DragValue::Start);
//   Box(Modifier().fillMaxWidth().height(56).onSizeChanged([state](Size s) {
//       state.updateAnchors({{DragValue::Start, 0}, {DragValue::Center, (s.width - 56) / 2}, {DragValue::End, s.width - 56}});
//   }), [&] {
//       Box(Modifier().offset([state] { return Offset{state.requireOffset(), 0}; })
//               .anchoredDraggable(state, Orientation::Horizontal).size(56).background(colorScheme().primary));
//   });
//   scope.launch([state]() -> Task { co_await state.animateTo(DragValue::End); });

#include "animation.hpp"
#include "coroutines.hpp"
#include "layout.hpp"
#include "runtime.hpp"
#include "saved_value.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace kompoz {

// The positions (dp along the drag) of the values it can rest at.
template <class T>
class DraggableAnchors {
public:
    DraggableAnchors() = default;
    DraggableAnchors(std::initializer_list<std::pair<T, float>> anchors) {
        for (const auto& [value, position] : anchors) at(value, position);
    }
    // Compose's `value at position` (a later one for the same value replaces it).
    DraggableAnchors& at(const T& value, float position) {
        for (auto& e : entries_)
            if (e.first == value) {
                e.second = position;
                return *this;
            }
        entries_.emplace_back(value, position);
        return *this;
    }
    std::optional<float> positionOf(const T& value) const {
        for (const auto& [v, p] : entries_)
            if (v == value) return p;
        return std::nullopt;
    }
    bool hasPositionFor(const T& value) const { return positionOf(value).has_value(); }
    // The anchor nearest `position`; or the nearest at or above it (searchUpwards) / at or below.
    std::optional<T> closestAnchor(float position) const {
        const std::pair<T, float>* best = nullptr;
        for (const auto& e : entries_)
            if (!best || std::abs(e.second - position) < std::abs(best->second - position)) best = &e;
        return best ? std::optional(best->first) : std::nullopt;
    }
    std::optional<T> closestAnchor(float position, bool searchUpwards) const {
        const std::pair<T, float>* best = nullptr;
        for (const auto& e : entries_) {
            float d = searchUpwards ? e.second - position : position - e.second;
            if (d < 0) continue;
            if (!best || d < (searchUpwards ? best->second - position : position - best->second)) best = &e;
        }
        return best ? std::optional(best->first) : std::nullopt;
    }
    float minPosition() const {
        float m = std::numeric_limits<float>::infinity();
        for (const auto& e : entries_) m = std::min(m, e.second);
        return m;
    }
    float maxPosition() const {
        float m = -std::numeric_limits<float>::infinity();
        for (const auto& e : entries_) m = std::max(m, e.second);
        return m;
    }
    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    const std::vector<std::pair<T, float>>& entries() const { return entries_; }
    friend bool operator==(const DraggableAnchors& a, const DraggableAnchors& b) { return a.entries_ == b.entries_; }

private:
    std::vector<std::pair<T, float>> entries_;
};

template <class T>
struct AnchoredDraggableOptions {
    // How far (dp) a release must be past the anchor it came from to go on to the next, given
    // the distance between the two (unset: half of it).
    std::function<float(float distance)> positionalThreshold;
    float velocityThreshold = 125;  // dp/s: a fling at least this fast goes on the way it was flung
    AnimationSpec snapAnimationSpec = spring();
    // Whether a release may settle on a value (false: it goes back where it was).
    std::function<bool(const T&)> confirmValueChange;
};

namespace detail {
// What anchoredDraggable() drives, whatever the value type.
struct AnchoredDragTarget {
    virtual ~AnchoredDragTarget() = default;
    virtual void drag(float delta) = 0;        // the finger's direction (positive: right or down), dp
    virtual Task settle(float velocity) = 0;   // dp/s, the finger's direction
    std::optional<CoroutineScope> coroutines;  // where releases settle (kept with the state)
};
} // namespace detail

// Copies share it.
template <class T>
class AnchoredDraggableState {
public:
    explicit AnchoredDraggableState(T initialValue, AnchoredDraggableOptions<T> options = {})
        : data_(std::make_shared<Data>(std::move(initialValue), std::move(options))) {}
    AnchoredDraggableState(T initialValue, DraggableAnchors<T> anchors, AnchoredDraggableOptions<T> options = {})
        : AnchoredDraggableState(std::move(initialValue), std::move(options)) {
        updateAnchors(std::move(anchors));
    }

    // Observable. Where it rests (or rested last, while dragged); a drag that ends on an anchor
    // and a settle that gets there change it.
    T currentValue() const { return data_->current.get(); }
    // Observable: where it is heading — the anchor an animation goes to, while dragged the next
    // anchor in the drag's direction (the current one at rest).
    T targetValue() const { return data_->target.get(); }
    // Observable: the value it last came to rest at (not while it moves).
    T settledValue() const { return data_->settled.get(); }
    // Observable (read it while laying out or drawing): dp along the drag; NaN until the anchors are set.
    float offset() const { return data_->offset.get(); }
    float requireOffset() const {  // 0 until the anchors are set
        float o = offset();
        return std::isnan(o) ? 0.f : o;
    }
    bool isAnimationRunning() const { return data_->animating.get(); }
    float lastVelocity() const { return data_->lastVelocity; }  // of the last release, dp/s
    const DraggableAnchors<T>& anchors() const { return data_->anchors; }
    // Observable: how far it is from `from` to `to` (0..1).
    float progress(const T& from, const T& to) const {
        auto a = data_->anchors.positionOf(from), b = data_->anchors.positionOf(to);
        float o = offset();
        if (!a || !b || std::isnan(o)) return 1;
        float lo = std::min(*a, *b), hi = std::max(*a, *b);
        float f = (std::clamp(o, lo, hi) - *a) / (*b - *a);
        return std::isnan(f) ? 1.f : std::abs(f);
    }

    // New anchors (as the layout knows the sizes: call it from onSizeChanged or a layout). At
    // rest it moves to `newTarget` (unset: the anchor nearest where it is, or its value before
    // it had anchors); an animation under way heads for its target's new position.
    void updateAnchors(DraggableAnchors<T> anchors, std::optional<T> newTarget = std::nullopt) const {
        Data& d = *data_;
        if (anchors == d.anchors) return;
        d.anchors = std::move(anchors);
        float o = d.offset.peek();
        if (d.animating.peek() || (d.dragging && !std::isnan(o))) {  // goes on, within the new bounds
            if (!std::isnan(o)) d.setOffset(std::clamp(o, d.anchors.minPosition(), d.anchors.maxPosition()));
            return;
        }
        // At rest it stays at its value (a window resized keeps a row open), where that is now.
        T to = newTarget ? *newTarget : d.target.peek();
        if (!d.anchors.hasPositionFor(to) && !std::isnan(o)) to = d.anchors.closestAnchor(o).value_or(to);
        if (auto p = d.anchors.positionOf(to)) {
            d.setOffset(*p);
            d.rest(to);
        } else if (!std::isnan(o)) {
            d.setOffset(std::clamp(o, d.anchors.minPosition(), d.anchors.maxPosition()));
        }
    }
    void snapTo(const T& value) const {
        Data& d = *data_;
        ++d.generation;
        d.animating.set(false);
        if (auto p = d.anchors.positionOf(value)) d.setOffset(*p);
        d.rest(value);
    }
    // In a coroutine: animates there (snapAnimationSpec, or `spec`); a drag or another one stops it.
    Task animateTo(const T& value) const { return Data::animate(data_, value); }
    Task animateTo(const T& value, const AnimationSpec& spec) const { return Data::animate(data_, value, 0, spec); }
    // In a coroutine: the anchor a release here at `velocity` (dp/s along the drag) goes to.
    Task settle(float velocity) const { return data_->settle(velocity); }
    // Moves it by `delta` dp (kept between the first and last anchor); returns how much it moved.
    float dispatchRawDelta(float delta) const { return data_->move(delta); }

    friend bool operator==(const AnchoredDraggableState& a, const AnchoredDraggableState& b) { return a.data_ == b.data_; }

    struct Data final : detail::AnchoredDragTarget, std::enable_shared_from_this<Data> {
        Data(T initial, AnchoredDraggableOptions<T> o)
            : options(std::move(o)), current(initial), target(initial), settled(initial), offset(std::numeric_limits<float>::quiet_NaN()) {}
        AnchoredDraggableOptions<T> options;
        DraggableAnchors<T> anchors;
        MutableState<T> current, target, settled;
        MutableState<float> offset;
        MutableState<bool> animating{false};
        std::optional<T> animationTarget;
        bool dragging = false;  // the user's drag under way (until its release settles)
        float lastVelocity = 0;
        uint64_t generation = 0;  // moved on by whatever stops an animation

        void setOffset(float o) {
            if (!(o == offset.peek())) offset.set(o);
            updateTarget();
        }
        void updateTarget() {
            float o = offset.peek();
            T cur = current.peek();
            if (animationTarget) return setTarget(*animationTarget);
            auto at = anchors.positionOf(cur);
            if (std::isnan(o) || !at || *at == o) return setTarget(cur);
            setTarget(anchors.closestAnchor(o, o > *at).value_or(cur));
        }
        void setTarget(const T& t) {
            if (!(target.peek() == t)) target.set(t);
        }
        void rest(const T& value) {
            if (!(current.peek() == value)) current.set(value);
            if (!(settled.peek() == value)) settled.set(value);
            updateTarget();
        }
        // An offset within half a dp of an anchor makes that anchor the current value.
        void reached() {
            float o = offset.peek();
            auto closest = anchors.closestAnchor(o);
            if (!closest || std::abs(o - *anchors.positionOf(*closest)) > 0.5f) return;
            if (!(current.peek() == *closest) && options.confirmValueChange && !options.confirmValueChange(*closest)) return;
            if (!(current.peek() == *closest)) current.set(*closest);
            updateTarget();
        }
        float move(float delta) {
            if (anchors.empty() || std::isnan(offset.peek())) return 0;
            float before = offset.peek();
            setOffset(std::clamp(before + delta, anchors.minPosition(), anchors.maxPosition()));
            reached();
            return offset.peek() - before;
        }
        void drag(float delta) override {
            dragging = true;
            ++generation;  // the user takes over
            if (animating.peek()) animating.set(false);
            animationTarget.reset();
            move(delta);
        }
        // Compose's computeTarget: past the threshold (or flung) the next anchor, else back.
        T targetFor(float o, float velocity) const {
            T cur = current.peek();
            auto at = anchors.positionOf(cur);
            if (!at || *at == o) return cur;
            if (std::abs(velocity) >= std::abs(options.velocityThreshold)) return anchors.closestAnchor(o, velocity > 0).value_or(cur);
            T next = anchors.closestAnchor(o, o > *at).value_or(cur);
            float distance = std::abs(*at - *anchors.positionOf(next));
            float threshold = options.positionalThreshold ? std::abs(options.positionalThreshold(distance)) : distance / 2;
            return std::abs(*at - o) <= threshold ? cur : next;
        }
        Task settle(float velocity) override {
            dragging = false;
            lastVelocity = velocity;
            float o = offset.peek();
            if (anchors.empty() || std::isnan(o)) co_return;
            T to = targetFor(o, velocity);
            if (!(to == current.peek()) && options.confirmValueChange && !options.confirmValueChange(to)) to = current.peek();
            co_await animate(this->shared_from_this(), to, velocity);
        }

        // A spring carries `velocity` (dp/s along the drag: a fling's) on into the settle.
        static Task animate(std::shared_ptr<Data> d, T to, float velocity = 0, std::optional<AnimationSpec> with = std::nullopt) {
            uint64_t generation = ++d->generation;
            d->animationTarget = to;
            d->animating.set(true);
            d->updateTarget();
            float start = d->offset.peek();
            int64_t t0 = -1;
            while (true) {
                auto p = d->anchors.positionOf(to);  // read again each frame: new anchors move it
                if (!p) break;
                if (std::isnan(start)) start = *p;
                int64_t now = co_await awaitFrame();
                if (d->generation != generation) co_return;
                p = d->anchors.positionOf(to);
                if (!p) break;
                if (t0 < 0) t0 = now;
                bool finished = false;
                float ms = float(now - t0) / 1e6f;
                const AnimationSpec& spec = with ? *with : d->options.snapAnimationSpec;
                if (spec.kind == AnimationSpec::Kind::Spring && spec.delayMillis <= 0) {
                    float x = 0, v = 0;
                    finished = spec.springMotion(ms, start - *p, velocity, x, v, 0.05f);
                    d->setOffset(finished ? *p : std::clamp(*p + x, d->anchors.minPosition(), d->anchors.maxPosition()));  // not past the ends
                } else {
                    float f = spec.progress(ms, finished);
                    d->setOffset(finished && f == 1.f ? *p : lerp(start, *p, f));
                }
                if (finished) break;
            }
            d->animationTarget.reset();
            d->animating.set(false);
            if (d->anchors.positionOf(to)) d->rest(to);
            else d->updateTarget();
        }
    };
    std::shared_ptr<Data> data_;
};

template <class T>
    requires HasSaver<T>
struct Saver<AnchoredDraggableState<T>> {
    static SavedValue save(const AnchoredDraggableState<T>& s) { return Saver<T>::save(s.settledValue()); }
    static std::optional<AnchoredDraggableState<T>> restore(const SavedValue& v) {
        auto value = Saver<T>::restore(v);
        return value ? std::optional(AnchoredDraggableState<T>(*value)) : std::nullopt;
    }
};

// Remembered (saveable when T is); the latest options are used.
template <class T>
AnchoredDraggableState<T> rememberAnchoredDraggableState(T initialValue, AnchoredDraggableOptions<T> options = {},
                                                         CallSite site = CallSite::current()) {
    AnchoredDraggableState<T> state = [&] {
        if constexpr (HasSaver<T>) return rememberSaveable([&] { return AnchoredDraggableState<T>(initialValue); }, site);
        else return remember([&] { return AnchoredDraggableState<T>(initialValue); }, site);
    }();
    state.data_->options = std::move(options);
    return state;
}

template <class T>
Modifier Modifier::anchoredDraggable(const AnchoredDraggableState<T>& state, Orientation orientation, bool enabled, bool reverseDirection,
                                     std::optional<MutableInteractionSource> interactionSource) const {
    return anchoredDraggable(std::static_pointer_cast<detail::AnchoredDragTarget>(state.data_), orientation, enabled, reverseDirection,
                             std::move(interactionSource));
}

} // namespace kompoz
