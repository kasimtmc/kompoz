#pragma once

// Low-level pointer input, the counterpart of Compose's Modifier.pointerInput: a coroutine
// per node that awaits pointer events, plus the usual gesture detectors built on it.
//
//   auto offset = remember([] { return mutableStateOf(Offset{}); });
//   Box(Modifier()
//           .offset([offset] { return offset.get(); })
//           .pointerInput(0, [offset](PointerInputScope& scope) -> Task {
//               co_await detectDragGestures(scope, {.onDrag = [offset](PointerInputChange& change, Offset drag) {
//                   change.consume();
//                   offset.update([&](Offset& o) { o.x += drag.x, o.y += drag.y; });
//               }});
//           })
//           .size(64).background(color, CircleShape));
//
// The coroutine starts with the node's first pointer event and keeps running while the key
// stays the same (like Compose, a new key restarts it; use rememberUpdatedState for
// callbacks that change). Positions are local to the modified element.

#include "coroutines.hpp"
#include "layout.hpp"

#include <chrono>
#include <optional>

namespace kompoz {

enum class PointerEventType { Press, Move, Release, Cancel, Enter, Exit, Scroll };

struct PointerInputChange {
    int64_t id = 0;  // which pointer (finger) it is
    Offset position, previousPosition;  // local to the element receiving the event
    bool pressed = false, previousPressed = false;
    int64_t uptimeMillis = 0;
    Offset scrollDelta;  // a Scroll event's: the wheel's notches, down and right positive
    bool touch = false;  // from a finger (not a mouse)

    // Consuming tells other handlers (and scrolling, clicks) that this change is taken care of.
    void consume() { *consumed_ = true; }
    bool isConsumed() const { return *consumed_; }
    Offset positionChange() const { return {position.x - previousPosition.x, position.y - previousPosition.y}; }
    bool isOutOfBounds(Size size) const {
        return position.x < 0 || position.y < 0 || position.x > size.width || position.y > size.height;
    }
    // ...or, for a finger, out of its minimum touch target too (PointerInputScope::extendedTouchPadding).
    bool isOutOfBounds(Size size, Size extendedTouchPadding) const {
        if (!touch) return isOutOfBounds(size);
        return position.x < -extendedTouchPadding.width || position.y < -extendedTouchPadding.height ||
               position.x > size.width + extendedTouchPadding.width || position.y > size.height + extendedTouchPadding.height;
    }

    std::shared_ptr<bool> consumed_ = std::make_shared<bool>(false);  // shared by every receiver
};

// One pointer pressed, moved or lifted. `change` is that pointer; `changes` has every pointer of
// the gesture (the others unchanged), as in Compose. Single-pointer detectors follow `change` of
// the pointer that started them; detectTransformGestures uses `changes`.
struct PointerEvent {
    PointerEventType type = PointerEventType::Move;
    PointerInputChange change;
    std::vector<PointerInputChange> changes;
    KeyModifiers keyboardModifiers;      // the keys held down
    std::optional<PointerButton> button;  // the mouse button pressed or let go (Press, Release)
    bool mouse = false;                   // from a mouse (not a finger)
};

// Compose's multi-touch helpers, over the pointers pressed before and after the event:
// the centroid, its movement, how far the pointers spread (zoom, 1: none) and turn (degrees).
Offset calculateCentroid(const PointerEvent& event, bool useCurrent = true);
float calculateCentroidSize(const PointerEvent& event, bool useCurrent = true);
Offset calculatePan(const PointerEvent& event);
float calculateZoom(const PointerEvent& event);
float calculateRotation(const PointerEvent& event);

namespace detail {
struct PointerInputState;
}

// The order an event goes round the pointerInput handlers under the pointer (Compose's
// PointerEventPass): Initial from the outermost one in (a parent sees, or consumes, it before
// what is inside), Main from the innermost one out (the usual way), Final from the outermost in
// again (after all of them: to see what the others consumed).
enum class PointerEventPass { Initial, Main, Final };

class PointerInputScope {
public:
    explicit PointerInputScope(detail::PointerInputState& state) : state_(&state) {}

    Size size() const;  // of the element, in dp
    int64_t nowNanos() const;  // the frame clock
    // LocalViewConfiguration where the pointerInput was composed (Compose's viewConfiguration):
    // a copy, good across a suspension (the element it comes from is replaced as its composable
    // runs again while the block goes on).
    ViewConfiguration viewConfiguration() const;
    // How far a finger's touch reaches past each side of an element smaller than the minimum
    // touch target (Compose's extendedTouchPadding): half of what it lacks.
    Size extendedTouchPadding() const;

    struct EventAwaiter {
        detail::PointerInputState* state;
        int64_t timeoutNanos;  // < 0: none
        PointerEventPass pass = PointerEventPass::Main;
        bool await_ready() const noexcept { return false; }
        template <class P>
        void await_suspend(std::coroutine_handle<P> h) {
            suspend(h, h.promise().job);
        }
        void suspend(std::coroutine_handle<> h, detail::JobState* job);
        std::optional<PointerEvent> await_resume();
    };
    struct Awaiter : EventAwaiter {
        PointerEvent await_resume() { return *EventAwaiter::await_resume(); }
    };
    // co_await scope.awaitPointerEvent() suspends until the next event for this element (in that
    // pass: one event comes to each pass in turn, so a handler may see it more than once).
    Awaiter awaitPointerEvent(PointerEventPass pass = PointerEventPass::Main) { return {{state_, -1, pass}}; }
    // ...or gives up after `timeout` and returns nullopt (Compose: withTimeoutOrNull).
    EventAwaiter awaitPointerEvent(std::chrono::nanoseconds timeout, PointerEventPass pass = PointerEventPass::Main) {
        return {state_, timeout.count(), pass};
    }
    // Whether a pointer was down after the last event that came here.
    bool anyPointerDown() const;

private:
    detail::PointerInputState* state_;
};

struct PointerInputElement final : ModifierElement {
    std::shared_ptr<const detail::ArgsBase> keys;
    std::function<Task(PointerInputScope&)> block;
    ViewConfiguration viewConfiguration;
    bool sameKeys(const PointerInputElement& other) const { return keys->equals(*other.keys); }
};

// transformable()'s part for the mouse and touchpads: Ctrl+wheel and touchpad pinches zoom it,
// Ctrl+Shift+wheel turns it (ComposeHost::zoom). It also marks a two-finger target: a second
// finger landing just after the first one started scrolling the page hands the gesture over.
struct ZoomElement final : ModifierElement {
    std::function<void(float zoom, float rotation, Offset centroid)> onZoom;  // centroid local to the element
};

template <class K, class F>
Modifier Modifier::pointerInput(const K& key, F block) const {
    auto e = std::make_shared<PointerInputElement>();
    e->keys = std::make_shared<detail::Args<K>>(std::tuple<K>(key));
    e->block = std::move(block);
    e->viewConfiguration = LocalViewConfiguration.current();
    return then(std::move(e));
}

namespace detail {

// One running pointerInput block of a node.
struct PointerInputState {
    std::shared_ptr<const PointerInputElement> element;  // the current element of the node's modifier
    PointerInputScope scope{*this};
    std::optional<CoroutineScope> coroutines;
    Size size;
    Dispatcher* dispatcher = nullptr;
    std::coroutine_handle<> waiter;
    std::weak_ptr<JobState> waitingJob;
    PointerEventPass waitingPass = PointerEventPass::Main;
    std::optional<PointerEvent> event;  // being delivered
    std::shared_ptr<int> timeout;       // alive while a timed wait is pending
    bool anyPressed = false;            // a pointer down after the last event (awaitEachGesture)

    ~PointerInputState() { cancel(); }
    void cancel() {
        waiter = {};
        timeout.reset();
        if (coroutines) coroutines->cancel();
    }
    // Starts the block if needed and hands it `e` if it is waiting for one in this pass.
    void dispatch(Dispatcher& dispatcher, const PointerEvent& e, PointerEventPass pass);
};

// Matches the node's pointer input states with its current pointerInput elements: same keys
// keep their coroutine, others are cancelled or (lazily) started.
void syncPointerInputs(LayoutNode& node);

} // namespace detail

// Modifier::draggable's state: holds the latest onDelta (and drag start/stop callbacks).
// How fast a pointer moves (Compose's VelocityTracker): feed it positions as they come, ask
// for the velocity when the finger lifts — the slope over its last 100 ms, nothing if it
// stood still at the end.
//   tracker.addPosition(change.uptimeMillis, change.position); ... tracker.calculateVelocity()
class VelocityTracker {
public:
    void addPosition(int64_t timeMillis, Offset position);
    Offset calculateVelocity() const;  // dp/s
    void resetTracking() { samples_.clear(); }

private:
    std::vector<std::pair<int64_t, Offset>> samples_;
};

class DraggableState {
public:
    struct Callbacks {
        std::function<void(float)> onDelta;
        std::function<void()> onDragStarted, onDragStopped;
        std::function<void(float velocity)> onDragStoppedWithVelocity;  // dp/s along the drag
    };
    explicit DraggableState(Callbacks callbacks) : callbacks_(std::make_shared<Callbacks>(std::move(callbacks))) {}
    const Callbacks& callbacks() const { return *callbacks_; }
    void update(Callbacks callbacks) const { *callbacks_ = std::move(callbacks); }
    friend bool operator==(const DraggableState& a, const DraggableState& b) { return a.callbacks_ == b.callbacks_; }

private:
    std::shared_ptr<Callbacks> callbacks_;
};
DraggableState rememberDraggableState(std::function<void(float)> onDelta, std::function<void()> onDragStopped = {},
                                      CallSite site = CallSite::current());
// Told the velocity the drag ended with (fling what was dragged: Animatable::animateDecay).
DraggableState rememberDraggableState(std::function<void(float)> onDelta, std::function<void(float velocity)> onDragStopped,
                                      CallSite site = CallSite::current());

// Modifier::draggable2D's state (Compose's Draggable2DState): where the drags in any direction
// go, the latest onDelta kept (and the modifier's latest start and stop callbacks).
// dispatchRawDelta moves it as a drag would (from code).
class Draggable2DState {
public:
    struct Callbacks {
        std::function<void(Offset)> onDelta;
        std::function<void(Offset)> onDragStarted;
        std::function<void(Velocity)> onDragStopped;
    };
    explicit Draggable2DState(std::function<void(Offset)> onDelta) : callbacks_(std::make_shared<Callbacks>()) {
        callbacks_->onDelta = std::move(onDelta);
    }
    void dispatchRawDelta(Offset delta) const {
        if (callbacks_->onDelta) callbacks_->onDelta(delta);
    }
    void update(std::function<void(Offset)> onDelta) const { callbacks_->onDelta = std::move(onDelta); }
    Callbacks& callbacks() const { return *callbacks_; }
    friend bool operator==(const Draggable2DState& a, const Draggable2DState& b) { return a.callbacks_ == b.callbacks_; }

private:
    std::shared_ptr<Callbacks> callbacks_;
};
Draggable2DState rememberDraggable2DState(std::function<void(Offset)> onDelta, CallSite site = CallSite::current());

struct Draggable2DOptions {
    bool enabled = true;
    std::optional<MutableInteractionSource> interactionSource;  // told when a drag starts and stops
    // From the press, without waiting for the touch slop: the press is the drag's (clicks
    // and scrolling around it don't get it).
    bool startDragImmediately = false;
    std::function<void(Offset startedPosition)> onDragStarted;  // where the finger went down
    std::function<void(Velocity velocity)> onDragStopped;       // dp/s; zero when cancelled
    bool reverseDirection = false;  // deltas and the velocity the other way
};

// Modifier::scrollable2D's state (Compose's Scrollable2DState): consumeScrollDelta gets each
// step of a drag, fling or wheel in any direction (the finger's way: right and down positive)
// and returns what it used; less on an axis is an end there (a fling stops on that axis).
//   auto pan = remember([] { return mutableStateOf(Offset{}); });
//   auto state = rememberScrollable2DState([pan](Offset d) { ... clamp, pan.set(...); return used; });
//   Box(Modifier().size(300).clipToBounds().scrollable2D(state), ...);
class Scrollable2DState {
public:
    explicit Scrollable2DState(std::function<Offset(Offset)> consumeScrollDelta);
    Offset dispatchRawDelta(Offset delta) const;  // what it used
    bool isScrollInProgress() const;              // dragged or flinging (observable)
    void stopScroll() const;                      // a fling going on ends
    void update(std::function<Offset(Offset)> consumeScrollDelta) const;
    friend bool operator==(const Scrollable2DState& a, const Scrollable2DState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};
Scrollable2DState rememberScrollable2DState(std::function<Offset(Offset)> consumeScrollDelta, CallSite site = CallSite::current());

struct TapGestureHandlers {
    std::function<void(Offset)> onPress;  // every press, before the gesture is known
    std::function<void(Offset)> onTap;
    std::function<void(Offset)> onDoubleTap;  // when set, onTap waits for the double-tap timeout
    std::function<void(Offset)> onLongPress;
    std::function<void()> onCancel;  // the press ended without a gesture (moved away, scrolled)
};
Task detectTapGestures(PointerInputScope& scope, TapGestureHandlers handlers);

// Compose's gesture building blocks, inside a pointerInput block:
//   co_await awaitEachGesture(scope, [](PointerInputScope& s) -> Task {
//       PointerInputChange down = co_await awaitFirstDown(s);
//       if (auto up = co_await waitForUpOrCancellation(s)) { /* a tap at up->position */ }
//   });
// The next press (unconsumed, unless told otherwise): its change.
TaskOf<PointerInputChange> awaitFirstDown(PointerInputScope& scope, bool requireUnconsumed = true);
// The change of the last pointer let go, or nothing if the gesture was taken first (a move
// consumed or out of the element, or cancelled).
TaskOf<std::optional<PointerInputChange>> waitForUpOrCancellation(PointerInputScope& scope);
// `gesture` again and again, each one once every pointer is up again (whatever it left).
Task awaitEachGesture(PointerInputScope& scope, std::function<Task(PointerInputScope&)> gesture);

struct DragGestureHandlers {
    std::function<void(Offset)> onDragStart;
    std::function<void(PointerInputChange&, Offset dragAmount)> onDrag;
    std::function<void()> onDragEnd, onDragCancel;
};
// Drags in any direction once the pointer moved past the touch slop. Consumes the changes, so
// enclosing scrollables and clicks leave the gesture alone.
Task detectDragGestures(PointerInputScope& scope, DragGestureHandlers handlers);
// Drags that start with a long press (Compose's detectDragGesturesAfterLongPress: an item picked up
// to reorder it): held still for the long press timeout, the drag starts where the finger is
// (onDragStart), then follows it, the whole way; moved past the touch slop before that, the
// gesture is given up (a scroll around it takes it).
Task detectDragGesturesAfterLongPress(PointerInputScope& scope, DragGestureHandlers handlers);

struct AxisDragGestureHandlers {
    std::function<void(Offset)> onDragStart;
    std::function<void(PointerInputChange&, float dragAmount)> onDrag;
    std::function<void()> onDragEnd, onDragCancel;
};
Task detectHorizontalDragGestures(PointerInputScope& scope, AxisDragGestureHandlers handlers);
Task detectVerticalDragGestures(PointerInputScope& scope, AxisDragGestureHandlers handlers);

// Pinch to zoom, twist to rotate and drag to pan, with one or more fingers, once the motion
// passes the touch slop: onGesture(centroid, pan, zoom, rotation) gets each step's change.
// panZoomLock: a gesture that started as a pan or zoom doesn't rotate.
Task detectTransformGestures(PointerInputScope& scope,
                             std::function<void(Offset centroid, Offset pan, float zoom, float rotation)> onGesture,
                             bool panZoomLock = false);

// Modifier.transformable(state): the gesture's changes go to the state's callback.
//   auto scale = remember([] { return mutableStateOf(1.f); });
//   auto state = rememberTransformableState([scale](float zoom, Offset, float) { scale.set(scale.get() * zoom); });
//   Image(..., Modifier().graphicsLayer([scale](GraphicsLayerScope& l) { l.scaleX = l.scaleY = scale.get(); })
//                        .transformable(state));
class TransformableState {
public:
    using OnTransformation = std::function<void(float zoomChange, Offset panChange, float rotationChange)>;
    explicit TransformableState(OnTransformation onTransformation);
    void transformBy(float zoomChange = 1, Offset panChange = {}, float rotationChange = 0) const;
    bool isTransformInProgress() const;  // observable
    friend bool operator==(const TransformableState& a, const TransformableState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};
// The same state on every recomposition, calling the latest callback.
TransformableState rememberTransformableState(TransformableState::OnTransformation onTransformation,
                                              CallSite site = CallSite::current());

} // namespace kompoz
