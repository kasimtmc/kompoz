#include <kompoz/anchored_draggable.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/host.hpp>

#include <cmath>
#include <numbers>

namespace kompoz {

using std::chrono::nanoseconds;

Size PointerInputScope::size() const { return state_->size; }
bool PointerInputScope::anyPointerDown() const { return state_->anyPressed; }
int64_t PointerInputScope::nowNanos() const { return state_->dispatcher ? state_->dispatcher->now() : 0; }
Size PointerInputScope::extendedTouchPadding() const {
    Size min = viewConfiguration().minimumTouchTargetSize, own = size();
    return {std::max(0.f, (min.width - own.width) / 2), std::max(0.f, (min.height - own.height) / 2)};
}
const ViewConfiguration& PointerInputScope::viewConfiguration() const {
    static const ViewConfiguration defaults;
    return state_->element ? state_->element->viewConfiguration : defaults;
}

void PointerInputScope::EventAwaiter::suspend(std::coroutine_handle<> h, detail::JobState* job) {
    state->waiter = h;
    state->waitingJob = job->weak_from_this();
    state->waitingPass = pass;
    state->event.reset();
    state->timeout.reset();
    if (timeoutNanos < 0) return;
    auto token = std::make_shared<int>(0);
    state->timeout = token;
    detail::Dispatcher* dispatcher = job->dispatcher;
    detail::PointerInputState* s = state;
    dispatcher->schedule(dispatcher->now() + timeoutNanos, job->weak_from_this(),
                         [s, dispatcher, weak = std::weak_ptr<int>(token)] {
                             if (weak.expired() || !s->waiter) return;  // an event came first
                             s->timeout.reset();
                             auto waiter = std::exchange(s->waiter, {});
                             dispatcher->resumeNow(s->waitingJob.lock(), waiter);  // resumes with no event
                         });
}

std::optional<PointerEvent> PointerInputScope::EventAwaiter::await_resume() {
    state->timeout.reset();
    return std::exchange(state->event, std::nullopt);
}

namespace detail {

void PointerInputState::dispatch(Dispatcher& d, const PointerEvent& e, PointerEventPass pass) {
    dispatcher = &d;
    anyPressed = std::any_of(e.changes.begin(), e.changes.end(), [](const PointerInputChange& c) { return c.pressed; });
    if (!coroutines) {
        if (e.type != PointerEventType::Press) return;
        coroutines.emplace(d);
        coroutines->launch([block = element->block, s = &scope]() -> Task { co_await block(*s); });
    }
    if (!waiter || waitingPass != pass) return;  // busy, finished or in another pass: dropped, as in Compose
    event = e;
    timeout.reset();
    auto h = std::exchange(waiter, {});
    d.resumeNow(waitingJob.lock(), h);
}

void syncPointerInputs(LayoutNode& node) {
    auto& states = node.pointerInputs;
    size_t count = 0, same = 0;
    for (const auto& e : node.modifier.elements()) {
        auto* input = dynamic_cast<const PointerInputElement*>(e.get());
        if (!input) continue;
        if (count < states.size() && states[count]->element->sameKeys(*input)) {
            states[count]->element = std::shared_ptr<const PointerInputElement>(e, input);
            ++same;
        }
        ++count;
    }
    if (count == states.size() && same == count) return;  // nothing to start or cancel
    std::vector<std::shared_ptr<PointerInputState>> next;
    size_t i = 0;
    for (const auto& e : node.modifier.elements()) {
        auto* input = dynamic_cast<const PointerInputElement*>(e.get());
        if (!input) continue;
        std::shared_ptr<const PointerInputElement> element(e, input);
        if (i < states.size() && states[i]->element->sameKeys(*input)) {
            states[i]->element = element;  // same keys: the running block stays
            next.push_back(states[i]);
        } else {
            if (i < states.size()) states[i]->cancel();
            auto state = std::make_shared<PointerInputState>();
            state->element = element;
            next.push_back(std::move(state));
        }
        ++i;
    }
    for (; i < states.size(); ++i) states[i]->cancel();
    states = std::move(next);
}

} // namespace detail

namespace {

enum class UpOutcome { Up, Cancelled, TimedOut };
struct UpResult {
    UpOutcome outcome = UpOutcome::Cancelled;
    PointerInputChange change;
};

// Waits for the pointer to go up (Compose: waitForUpOrCancellation): cancelled when another
// handler consumes a change or the pointer leaves the element. `timeout` < 0: none.
Task waitForUp(PointerInputScope& scope, int64_t pointer, nanoseconds timeout, UpResult* out) {
    int64_t deadline = timeout.count() >= 0 ? scope.nowNanos() + timeout.count() : -1;
    while (true) {
        std::optional<PointerEvent> e;
        if (deadline < 0) {
            e = co_await scope.awaitPointerEvent();
        } else {
            int64_t left = deadline - scope.nowNanos();
            if (left > 0) e = co_await scope.awaitPointerEvent(nanoseconds(left));
            if (!e) {
                out->outcome = UpOutcome::TimedOut;
                co_return;
            }
        }
        if (e->change.id != pointer && e->type != PointerEventType::Cancel) continue;  // another finger
        switch (e->type) {
        case PointerEventType::Release:
            out->outcome = e->change.isConsumed() ? UpOutcome::Cancelled : UpOutcome::Up;
            out->change = e->change;
            co_return;
        case PointerEventType::Cancel: out->outcome = UpOutcome::Cancelled; co_return;
        case PointerEventType::Move:
            if (e->change.isConsumed() || e->change.isOutOfBounds(scope.size(), scope.extendedTouchPadding())) {
                out->outcome = UpOutcome::Cancelled;
                co_return;
            }
            break;
        default: break;  // presses of other fingers; Enter, Exit and Scroll don't come here
        }
    }
}

// After a long press: everything until the pointer goes up belongs to it.
Task consumeUntilUp(PointerInputScope& scope, int64_t pointer) {
    while (true) {
        PointerEvent e = co_await scope.awaitPointerEvent();
        e.change.consume();
        if (e.type == PointerEventType::Cancel || (e.type == PointerEventType::Release && e.change.id == pointer)) co_return;
    }
}

nanoseconds longPressTimeoutFor(const PointerInputScope& scope, const TapGestureHandlers& h) {
    return h.onLongPress ? nanoseconds(scope.viewConfiguration().longPressTimeout) : nanoseconds(-1);
}

// Follows `pointer`'s drag, consuming each move it reports, until it lifts (true) or the gesture
// is called off or taken by someone else (false): Compose's drag().
TaskOf<bool> followDrag(PointerInputScope& scope, int64_t pointer, std::function<void(PointerInputChange&, Offset)> onDrag) {
    while (true) {
        PointerEvent e = co_await scope.awaitPointerEvent();
        if (e.change.id != pointer && e.type != PointerEventType::Cancel) continue;
        if (e.type == PointerEventType::Release) co_return true;
        if (e.type == PointerEventType::Cancel || (e.type == PointerEventType::Move && e.change.isConsumed())) co_return false;
        if (e.type == PointerEventType::Move) {
            e.change.consume();
            onDrag(e.change, e.change.positionChange());
        }
    }
}

// Shared by the drag detectors: waits for the pointer to move past the slop (measured by
// `distance`), then reports the drag until the pointer goes up.
template <class Distance, class OnStart, class OnDrag>
Task dragGestures(PointerInputScope& scope, Distance distance, OnStart onStart, OnDrag onDrag,
                  std::function<void()> onEnd, std::function<void()> onCancel) {
    while (true) {
        PointerEvent down = co_await scope.awaitPointerEvent();
        if (down.type != PointerEventType::Press) continue;
        int64_t pointer = down.change.id;  // the drag follows this finger
        Offset total;
        bool started = false;
        while (true) {
            PointerEvent e = co_await scope.awaitPointerEvent();
            if (e.change.id != pointer && e.type != PointerEventType::Cancel) continue;
            if (e.type == PointerEventType::Release || e.type == PointerEventType::Cancel) break;
            if (e.type != PointerEventType::Move) continue;
            if (e.change.isConsumed()) break;  // someone else took the gesture
            Offset d = e.change.positionChange();
            total = {total.x + d.x, total.y + d.y};
            float dist = distance(total);
            if (std::abs(dist) > scope.viewConfiguration().touchSlop) {
                started = true;
                float keep = (std::abs(dist) - scope.viewConfiguration().touchSlop) / std::abs(dist);
                e.change.consume();
                onStart(down.change.position);
                onDrag(e.change, Offset{total.x * keep, total.y * keep});
                break;
            }
        }
        if (!started) continue;
        if (co_await followDrag(scope, pointer, onDrag)) {
            if (onEnd) onEnd();
        } else if (onCancel) {
            onCancel();
        }
    }
}

} // namespace

DraggableState rememberDraggableState(std::function<void(float)> onDelta, std::function<void()> onDragStopped,
                                      CallSite site) {
    auto& state = remember([] { return DraggableState({}); }, site);
    state.update({std::move(onDelta), {}, std::move(onDragStopped)});  // the latest callbacks, same state
    return state;
}
DraggableState rememberDraggableState(std::function<void(float)> onDelta, std::function<void(float)> onDragStopped, CallSite site) {
    auto& state = remember([] { return DraggableState({}); }, site);
    state.update({std::move(onDelta), {}, {}, std::move(onDragStopped)});
    return state;
}

void VelocityTracker::addPosition(int64_t timeMillis, Offset position) {
    if (!samples_.empty() && timeMillis < samples_.back().first) samples_.clear();  // time went back: a new gesture
    samples_.emplace_back(timeMillis, position);
    std::erase_if(samples_, [&](const auto& s) { return timeMillis - s.first > 100; });
}

Offset VelocityTracker::calculateVelocity() const {
    // The samples going back from the last one, stopping at a pause (40 ms without a move: it
    // stood still there); a least-squares line through each coordinate over time.
    size_t from = samples_.size();
    while (from > 0 && (from == samples_.size() || samples_[from].first - samples_[from - 1].first <= 40)) --from;
    if (samples_.size() - from < 2) return {};
    int64_t t0 = samples_.back().first;
    double n = 0, st = 0, stt = 0, sx = 0, sy = 0, stx = 0, sty = 0;
    for (size_t i = from; i < samples_.size(); ++i) {
        double t = double(samples_[i].first - t0) / 1000.0;
        n += 1, st += t, stt += t * t;
        sx += samples_[i].second.x, sy += samples_[i].second.y;
        stx += t * samples_[i].second.x, sty += t * samples_[i].second.y;
    }
    double d = n * stt - st * st;
    if (std::abs(d) < 1e-12) return {};
    return {float((n * stx - st * sx) / d), float((n * sty - st * sy) / d)};
}

Modifier Modifier::onPointerEvent(PointerEventType eventType, std::function<void(PointerEvent&)> onEvent) const {
    auto e = std::make_shared<PointerEventElement>();
    e->type = eventType;
    e->onEvent = std::move(onEvent);
    return then(std::move(e));
}

Modifier Modifier::onClick(std::function<void()> onClick, OnClickOptions options, CallSite site) const {
    struct Latest {  // what the running handlers call
        std::function<void()> onClick;
        OnClickOptions options;
        ComposeHost* host = nullptr;
        bool pressed = false;  // another button went down over it...
        int press = 0;         // ...as the host's press of such buttons numbered so far (none since)
        bool keysHeld(const KeyModifiers& m) const { return !options.keyboardModifiers || options.keyboardModifiers(m); }
    };
    auto latest = remember([] { return std::make_shared<Latest>(); }, site);
    latest->onClick = std::move(onClick);
    latest->options = std::move(options);
    latest->host = &detail::Composer::require().host();
    const OnClickOptions& o = latest->options;
    if (!o.enabled) return *this;
    if (o.matcher.button == PointerButton::Primary) {  // a tap of a finger or the primary button
        std::pair<bool, bool> key{bool(o.onDoubleClick), bool(o.onLongClick)};
        return pointerInput(key, [latest, key](PointerInputScope& scope) -> Task {
            auto allowed = [latest] {
                bool mouse = latest->host->pointerType() == ComposeHost::PointerType::Mouse;
                const PointerMatcher& m = latest->options.matcher;
                return (mouse ? m.acceptsMouse : m.acceptsTouch) && latest->keysHeld(latest->host->keyboardModifiers());
            };
            TapGestureHandlers h;
            h.onTap = [latest, allowed](Offset) {
                if (allowed() && latest->onClick) latest->onClick();
            };
            if (key.first)
                h.onDoubleTap = [latest, allowed](Offset) {
                    if (allowed() && latest->options.onDoubleClick) latest->options.onDoubleClick();
                };
            if (key.second)
                h.onLongPress = [latest, allowed](Offset) {
                    if (allowed() && latest->options.onLongClick) latest->options.onLongClick();
                };
            co_await detectTapGestures(scope, std::move(h));
        });
    }
    // Another button: pressed and let go over the node.
    return onPointerEvent(PointerEventType::Press,
                          [latest](PointerEvent& e) {
                              const PointerMatcher& m = latest->options.matcher;
                              latest->pressed = e.mouse && m.acceptsMouse && e.button == m.button && latest->keysHeld(e.keyboardModifiers);
                              latest->press = latest->host->mouseButtonPresses();
                          })
        .onPointerEvent(PointerEventType::Release, [latest](PointerEvent& e) {
            bool same = latest->press == latest->host->mouseButtonPresses();  // let go of the press made over it
            if (e.button == latest->options.matcher.button && std::exchange(latest->pressed, false) && same && latest->onClick)
                latest->onClick();
        });
}

Modifier Modifier::draggable(const DraggableState& state, Orientation orientation, bool enabled,
                             std::optional<MutableInteractionSource> interactionSource) const {
    if (!enabled) return *this;
    struct Key {
        DraggableState state;
        std::optional<MutableInteractionSource> source;
        bool operator==(const Key&) const = default;
    };
    return pointerInput(Key{state, interactionSource}, [state, orientation, interactionSource](PointerInputScope& scope) -> Task {
        // The drag going on, told to the interaction source (cancelled if the handler goes meanwhile).
        auto drag = std::make_shared<std::optional<Interaction>>();
        struct End {
            std::shared_ptr<std::optional<Interaction>> drag;
            std::optional<MutableInteractionSource> source;
            ~End() {
                if (source && *drag) source->tryEmit((*drag)->cancel());
            }
        } end{drag, interactionSource};
        auto finish = [drag, interactionSource](bool stopped) {
            if (interactionSource && *drag) interactionSource->tryEmit(stopped ? (*drag)->stop() : (*drag)->cancel());
            drag->reset();
        };
        // The velocity is that of the drag's own travel, not of the pointer over the node: a node
        // that moves with the finger (a handle) still flings.
        auto tracker = std::make_shared<VelocityTracker>();
        auto travelled = std::make_shared<float>(0.f);
        bool horizontal = orientation == Orientation::Horizontal;
        auto stopped = [state](float velocity) {
            if (state.callbacks().onDragStopped) state.callbacks().onDragStopped();
            if (state.callbacks().onDragStoppedWithVelocity) state.callbacks().onDragStoppedWithVelocity(velocity);
        };
        AxisDragGestureHandlers h{
            .onDragStart = [state, drag, interactionSource, tracker, travelled](Offset) {
                tracker->resetTracking();
                *travelled = 0;
                if (interactionSource) interactionSource->tryEmit(drag->emplace(Interaction::dragStart()));
                if (state.callbacks().onDragStarted) state.callbacks().onDragStarted();
            },
            .onDrag = [state, tracker, travelled, horizontal](PointerInputChange& change, float delta) {
                change.consume();
                *travelled += delta;
                tracker->addPosition(change.uptimeMillis, horizontal ? Offset{*travelled, 0} : Offset{0, *travelled});
                if (state.callbacks().onDelta) state.callbacks().onDelta(delta);
            },
            .onDragEnd = [finish, stopped, tracker, horizontal] {
                finish(true);
                Offset v = tracker->calculateVelocity();
                stopped(horizontal ? v.x : v.y);
            },
            .onDragCancel = [finish, stopped] {
                finish(false);
                stopped(0);
            }};
        if (orientation == Orientation::Horizontal)
            co_await detectHorizontalDragGestures(scope, std::move(h));
        else
            co_await detectVerticalDragGestures(scope, std::move(h));
    });
}

Draggable2DState rememberDraggable2DState(std::function<void(Offset)> onDelta, CallSite site) {
    auto& state = remember([] { return Draggable2DState({}); }, site);
    state.update(std::move(onDelta));  // the latest, same state
    return state;
}

Modifier Modifier::draggable2D(const Draggable2DState& state) const { return draggable2D(state, Draggable2DOptions{}); }

Modifier Modifier::draggable2D(const Draggable2DState& state, const Draggable2DOptions& options) const {
    if (!options.enabled) return *this;
    // The latest start and stop callbacks go to the state, where the running gesture reads them:
    // a new lambda (each recomposition) doesn't restart it.
    state.callbacks().onDragStarted = options.onDragStarted;
    state.callbacks().onDragStopped = options.onDragStopped;
    struct Key {
        Draggable2DState state;
        std::optional<MutableInteractionSource> source;
        bool immediately, reverse;
        bool operator==(const Key&) const = default;
    };
    Key key{state, options.interactionSource, options.startDragImmediately, options.reverseDirection};
    return pointerInput(key, [key](PointerInputScope& scope) -> Task {
        Key k = key;  // in the coroutine's frame
        auto drag = std::make_shared<std::optional<Interaction>>();  // the drag going on, for the interaction source
        struct End {
            std::shared_ptr<std::optional<Interaction>> drag;
            std::optional<MutableInteractionSource> source;
            ~End() {
                if (source && *drag) source->tryEmit((*drag)->cancel());
            }
        } end{drag, k.source};
        float sign = k.reverse ? -1.f : 1.f;
        VelocityTracker tracker;  // over the drag's own travel
        Offset travelled;
        auto start = [&](Offset at) {
            tracker.resetTracking();
            travelled = {};
            if (k.source) k.source->tryEmit(drag->emplace(Interaction::dragStart()));
            if (k.state.callbacks().onDragStarted) k.state.callbacks().onDragStarted(at);
        };
        auto move = [&](PointerInputChange& change, Offset amount) {
            change.consume();
            travelled = {travelled.x + amount.x, travelled.y + amount.y};
            tracker.addPosition(change.uptimeMillis, travelled);
            k.state.dispatchRawDelta({amount.x * sign, amount.y * sign});
        };
        auto stop = [&](bool released) {
            if (k.source && *drag) k.source->tryEmit(released ? (*drag)->stop() : (*drag)->cancel());
            drag->reset();
            Offset v = released ? tracker.calculateVelocity() : Offset{};
            if (k.state.callbacks().onDragStopped) k.state.callbacks().onDragStopped({v.x * sign, v.y * sign});
        };
        if (!k.immediately) {
            co_await detectDragGestures(scope, {.onDragStart = start,
                                                .onDrag = move,
                                                .onDragEnd = [&] { stop(true); },
                                                .onDragCancel = [&] { stop(false); }});
            co_return;
        }
        while (true) {  // the press starts it
            PointerEvent down = co_await scope.awaitPointerEvent();
            if (down.type != PointerEventType::Press || down.change.isConsumed()) continue;
            int64_t pointer = down.change.id;
            down.change.consume();
            start(down.change.position);
            tracker.addPosition(down.change.uptimeMillis, travelled);
            while (true) {
                PointerEvent e = co_await scope.awaitPointerEvent();
                if (e.change.id != pointer && e.type != PointerEventType::Cancel) continue;
                if (e.type == PointerEventType::Release) {
                    stop(true);
                    break;
                }
                if (e.type == PointerEventType::Cancel) {
                    stop(false);
                    break;
                }
                if (e.type == PointerEventType::Move) move(e.change, e.change.positionChange());
            }
        }
    });
}

struct Scrollable2DState::Data {
    std::function<Offset(Offset)> consume;
    MutableState<bool> inProgress{false};
    bool stopFling = false;
};

Scrollable2DState::Scrollable2DState(std::function<Offset(Offset)> consumeScrollDelta) : data_(std::make_shared<Data>()) {
    data_->consume = std::move(consumeScrollDelta);
}
Offset Scrollable2DState::dispatchRawDelta(Offset delta) const { return data_->consume ? data_->consume(delta) : Offset{}; }
bool Scrollable2DState::isScrollInProgress() const { return data_->inProgress.get(); }
void Scrollable2DState::stopScroll() const { data_->stopFling = true; }
void Scrollable2DState::update(std::function<Offset(Offset)> consumeScrollDelta) const { data_->consume = std::move(consumeScrollDelta); }

Scrollable2DState rememberScrollable2DState(std::function<Offset(Offset)> consumeScrollDelta, CallSite site) {
    auto& state = remember([] { return Scrollable2DState({}); }, site);
    state.update(std::move(consumeScrollDelta));  // the latest, same state
    return state;
}

Modifier Modifier::scrollable2D(const Scrollable2DState& state, bool enabled, std::optional<MutableInteractionSource> interactionSource) const {
    if (!enabled) return *this;
    struct Key {
        Scrollable2DState state;
        std::optional<MutableInteractionSource> source;
        bool operator==(const Key&) const = default;
    };
    Key key{state, interactionSource};
    constexpr float kWheelStep = 48;  // dp a notch, as the scrollables' wheel
    // The wheel (and its sideways one): taken unless nothing moved (at an end the page around may
    // scroll); it ends a fling.
    auto wheel = [state](PointerEvent& e) {
        state.stopScroll();
        Offset used = state.dispatchRawDelta({-e.change.scrollDelta.x * kWheelStep, -e.change.scrollDelta.y * kWheelStep});
        if (used.x != 0 || used.y != 0) e.change.consume();
    };
    return pointerInput(key, [key](PointerInputScope& scope) -> Task {
        Key k = key;  // in the coroutine's frame
        auto drag = std::make_shared<std::optional<Interaction>>();  // the drag going on, for the interaction source
        struct End {
            std::shared_ptr<std::optional<Interaction>> drag;
            std::optional<MutableInteractionSource> source;
            std::shared_ptr<Scrollable2DState::Data> data;
            ~End() {
                if (source && *drag) source->tryEmit((*drag)->cancel());
                if (data->inProgress.peek()) data->inProgress.set(false);
            }
        } end{drag, k.source, k.state.data_};
        auto& data = *k.state.data_;
        auto setInProgress = [&](bool on) {
            if (data.inProgress.peek() != on) data.inProgress.set(on);
        };
        DecayAnimationSpec decay = splineBasedDecay();
        std::optional<PointerEvent> next;  // the press that interrupted a fling, to go on with
        while (true) {
            PointerEvent down = next ? *std::exchange(next, std::nullopt) : co_await scope.awaitPointerEvent();
            if (down.type != PointerEventType::Press || down.change.isConsumed()) continue;
            int64_t pointer = down.change.id;
            VelocityTracker tracker;  // over what was scrolled by the finger
            Offset total, travelled;
            bool dragging = false;
            while (!dragging) {  // past the touch slop
                PointerEvent e = co_await scope.awaitPointerEvent();
                if (e.change.id != pointer && e.type != PointerEventType::Cancel) continue;
                if (e.type == PointerEventType::Release || e.type == PointerEventType::Cancel) break;
                if (e.type != PointerEventType::Move) continue;
                if (e.change.isConsumed()) break;  // someone else took the gesture
                Offset d = e.change.positionChange();
                total = {total.x + d.x, total.y + d.y};
                float distance = std::hypot(total.x, total.y);
                if (distance <= scope.viewConfiguration().touchSlop) continue;
                dragging = true;
                e.change.consume();
                float keep = (distance - scope.viewConfiguration().touchSlop) / distance;
                travelled = {total.x * keep, total.y * keep};
                if (k.source) k.source->tryEmit(drag->emplace(Interaction::dragStart()));
                setInProgress(true);
                k.state.dispatchRawDelta(travelled);
                tracker.addPosition(e.change.uptimeMillis, travelled);
            }
            if (!dragging) continue;
            Offset velocity;
            while (true) {
                PointerEvent e = co_await scope.awaitPointerEvent();
                if (e.change.id != pointer && e.type != PointerEventType::Cancel) continue;
                if (e.type == PointerEventType::Release) {
                    velocity = tracker.calculateVelocity();
                    break;
                }
                if (e.type == PointerEventType::Cancel || (e.type == PointerEventType::Move && e.change.isConsumed())) break;
                if (e.type != PointerEventType::Move) continue;
                e.change.consume();
                Offset d = e.change.positionChange();
                travelled = {travelled.x + d.x, travelled.y + d.y};
                tracker.addPosition(e.change.uptimeMillis, travelled);
                k.state.dispatchRawDelta(d);
            }
            if (k.source && *drag) k.source->tryEmit((*drag)->stop());
            drag->reset();
            // The fling: along the finger's way as the scrollables' flings go (Android's spline is
            // not linear in speed: each axis on its own would bend it), each axis until it reaches
            // an end; a press (handled next) or the wheel stops it.
            data.stopFling = false;
            int64_t start = scope.nowNanos();
            Offset moved;
            bool axes[2] = {std::abs(velocity.x) > 1, std::abs(velocity.y) > 1};
            float speed = std::hypot(velocity.x, velocity.y);
            Offset way = speed > 0 ? Offset{velocity.x / speed, velocity.y / speed} : Offset{};
            while ((axes[0] || axes[1]) && !data.stopFling) {
                std::optional<PointerEvent> e = co_await scope.awaitPointerEvent(std::chrono::milliseconds(16));
                if (e) {
                    if (e->type == PointerEventType::Press) {
                        next = std::move(*e);
                        break;
                    }
                    continue;
                }
                float elapsed = float(scope.nowNanos() - start) / 1e6f, along = decay.valueAt(0, speed, elapsed);
                Offset at{axes[0] ? way.x * along : moved.x, axes[1] ? way.y * along : moved.y};
                Offset step{at.x - moved.x, at.y - moved.y};
                Offset used = k.state.dispatchRawDelta(step);
                moved = at;
                for (int axis = 0; axis < 2; ++axis) {
                    float asked = axis ? step.y : step.x, got = axis ? used.y : used.x;
                    if (std::abs(got) + 0.01f < std::abs(asked) || elapsed >= decay.durationMillis(speed)) axes[axis] = false;
                }
            }
            setInProgress(false);
        }
    }).onPointerEvent(PointerEventType::Scroll, std::move(wheel));
}

namespace {
// anchoredDraggable(): the host's drags (past the touch slop, the innermost first, with their
// velocity) move the state; a release settles it.
struct AnchoredDraggableElement final : ScrollableElement {
    std::shared_ptr<detail::AnchoredDragTarget> target;
    Orientation axis = Orientation::Horizontal;
    bool reverse = false;
    std::optional<MutableInteractionSource> source;
    Orientation orientation() const override { return axis; }
    bool overscrolls() const override { return false; }
    bool clipsContent() const override { return false; }
    bool scrollsWithWheel() const override { return false; }
    bool followsLayoutDirection() const override { return false; }
    std::optional<MutableInteractionSource> interactionSource() const override { return source; }
    float scrollBy(float delta) const override {  // towards the end: the finger the other way
        target->drag(reverse ? delta : -delta);
        return delta;
    }
    bool settle(float velocity) const override {
        if (target->coroutines)
            target->coroutines->launch([t = target, finger = reverse ? velocity : -velocity]() -> Task { co_await t->settle(finger); });
        return true;
    }
};
} // namespace

Modifier Modifier::anchoredDraggable(std::shared_ptr<detail::AnchoredDragTarget> target, Orientation orientation, bool enabled,
                                     bool reverseDirection, std::optional<MutableInteractionSource> interactionSource) const {
    if (!enabled) return *this;
    if (!target->coroutines) target->coroutines.emplace(detail::currentDispatcher());
    auto e = std::make_shared<AnchoredDraggableElement>();
    e->target = std::move(target);
    e->axis = orientation;
    e->reverse = reverseDirection;
    e->source = std::move(interactionSource);
    return then(std::move(e));
}

Task detectTapGestures(PointerInputScope& scope, TapGestureHandlers h) {
    while (true) {
        PointerEvent down = co_await scope.awaitPointerEvent();
        if (down.type != PointerEventType::Press || down.change.isConsumed()) continue;
        down.change.consume();
        if (h.onPress) h.onPress(down.change.position);

        int64_t pointer = down.change.id;
        UpResult first;
        co_await waitForUp(scope, pointer, longPressTimeoutFor(scope, h), &first);
        if (first.outcome == UpOutcome::TimedOut) {
            h.onLongPress(down.change.position);
            co_await consumeUntilUp(scope, pointer);
            continue;
        }
        if (first.outcome == UpOutcome::Cancelled) {
            if (h.onCancel) h.onCancel();
            continue;
        }
        first.change.consume();
        if (!h.onDoubleTap) {
            if (h.onTap) h.onTap(first.change.position);
            continue;
        }

        // A second press within the double-tap timeout makes it a double tap; one sooner than
        // its minimum time after the first is no second tap.
        const ViewConfiguration& config = scope.viewConfiguration();
        int64_t up = scope.nowNanos(), deadline = up + nanoseconds(config.doubleTapTimeout).count();
        std::optional<PointerEvent> second;
        while (!second) {
            int64_t left = deadline - scope.nowNanos();
            if (left <= 0) break;
            std::optional<PointerEvent> e = co_await scope.awaitPointerEvent(nanoseconds(left));
            if (!e) break;
            if (e->type == PointerEventType::Press && scope.nowNanos() - up >= nanoseconds(config.doubleTapMinTime).count()) second = e;
        }
        if (!second) {
            if (h.onTap) h.onTap(first.change.position);
            continue;
        }
        second->change.consume();
        if (h.onPress) h.onPress(second->change.position);
        UpResult secondUp;
        co_await waitForUp(scope, second->change.id, longPressTimeoutFor(scope, h), &secondUp);
        if (secondUp.outcome == UpOutcome::Up) {
            secondUp.change.consume();
            h.onDoubleTap(secondUp.change.position);
            continue;
        }
        if (h.onTap) h.onTap(first.change.position);
        if (secondUp.outcome == UpOutcome::TimedOut) {
            h.onLongPress(second->change.position);
            co_await consumeUntilUp(scope, second->change.id);
        } else if (h.onCancel) {
            h.onCancel();
        }
    }
}

Task detectDragGestures(PointerInputScope& scope, DragGestureHandlers h) {
    co_await dragGestures(
        scope, [](Offset t) { return std::hypot(t.x, t.y); },
        [&](Offset p) {
            if (h.onDragStart) h.onDragStart(p);
        },
        [&](PointerInputChange& change, Offset amount) {
            if (h.onDrag) h.onDrag(change, amount);
        },
        h.onDragEnd, h.onDragCancel);
}

TaskOf<PointerInputChange> awaitFirstDown(PointerInputScope& scope, bool requireUnconsumed) {
    while (true) {
        PointerEvent e = co_await scope.awaitPointerEvent();
        if (e.type == PointerEventType::Press && (!requireUnconsumed || !e.change.isConsumed())) co_return e.change;
    }
}

TaskOf<std::optional<PointerInputChange>> waitForUpOrCancellation(PointerInputScope& scope) {
    while (true) {
        PointerEvent e = co_await scope.awaitPointerEvent();
        switch (e.type) {
        case PointerEventType::Release:
            if (std::none_of(e.changes.begin(), e.changes.end(), [](const PointerInputChange& c) { return c.pressed; }))
                co_return e.change.isConsumed() ? std::nullopt : std::optional(e.change);
            break;  // another finger still down
        case PointerEventType::Cancel: co_return std::nullopt;
        case PointerEventType::Move:
            if (std::any_of(e.changes.begin(), e.changes.end(),
                            [&](const PointerInputChange& c) { return c.isConsumed() || c.isOutOfBounds(scope.size(), scope.extendedTouchPadding()); }))
                co_return std::nullopt;
            break;
        default: break;
        }
    }
}

Task awaitEachGesture(PointerInputScope& scope, std::function<Task(PointerInputScope&)> gesture) {
    while (true) {
        co_await gesture(scope);
        while (scope.anyPointerDown()) co_await scope.awaitPointerEvent();  // the gesture's pointers let go first
    }
}

Task detectDragGesturesAfterLongPress(PointerInputScope& scope, DragGestureHandlers h) {
    while (true) {
        PointerEvent down = co_await scope.awaitPointerEvent();
        if (down.type != PointerEventType::Press || down.change.isConsumed()) continue;
        int64_t pointer = down.change.id;
        // Held still (within the slop) until the long press: not let go, moved away or taken.
        int64_t deadline = scope.nowNanos() + nanoseconds(scope.viewConfiguration().longPressTimeout).count();
        Offset total;
        bool held = false;
        while (true) {
            int64_t left = deadline - scope.nowNanos();
            std::optional<PointerEvent> e;
            if (left > 0) e = co_await scope.awaitPointerEvent(nanoseconds(left));
            if (!e) {
                held = true;
                break;
            }
            if (e->change.id != pointer && e->type != PointerEventType::Cancel) continue;
            if (e->type == PointerEventType::Release || e->type == PointerEventType::Cancel) break;
            if (e->type != PointerEventType::Move) continue;
            if (e->change.isConsumed()) break;
            Offset d = e->change.positionChange();
            total = {total.x + d.x, total.y + d.y};
            if (std::hypot(total.x, total.y) > scope.viewConfiguration().touchSlop) break;
        }
        if (!held) continue;
        if (h.onDragStart) h.onDragStart({down.change.position.x + total.x, down.change.position.y + total.y});
        bool ended = co_await followDrag(scope, pointer, [&](PointerInputChange& change, Offset amount) {
            if (h.onDrag) h.onDrag(change, amount);
        });
        if (ended && h.onDragEnd) h.onDragEnd();
        if (!ended && h.onDragCancel) h.onDragCancel();
    }
}

Task detectHorizontalDragGestures(PointerInputScope& scope, AxisDragGestureHandlers h) {
    co_await dragGestures(
        scope, [](Offset t) { return t.x; },
        [&](Offset p) {
            if (h.onDragStart) h.onDragStart(p);
        },
        [&](PointerInputChange& change, Offset amount) {
            if (h.onDrag) h.onDrag(change, amount.x);
        },
        h.onDragEnd, h.onDragCancel);
}

Task detectVerticalDragGestures(PointerInputScope& scope, AxisDragGestureHandlers h) {
    co_await dragGestures(
        scope, [](Offset t) { return t.y; },
        [&](Offset p) {
            if (h.onDragStart) h.onDragStart(p);
        },
        [&](PointerInputChange& change, Offset amount) {
            if (h.onDrag) h.onDrag(change, amount.y);
        },
        h.onDragEnd, h.onDragCancel);
}

} // namespace kompoz

namespace kompoz {

// ---- Multi-touch -----------------------------------------------------------------------------

namespace {
bool bothPressed(const PointerInputChange& c) { return c.pressed && c.previousPressed; }
float angleOf(Offset o) { return std::atan2(o.y, o.x) * 180.f / std::numbers::pi_v<float>; }
} // namespace

Offset calculateCentroid(const PointerEvent& event, bool useCurrent) {
    Offset total;
    int count = 0;
    for (const auto& c : event.changes)
        if (bothPressed(c)) {
            Offset p = useCurrent ? c.position : c.previousPosition;
            total = {total.x + p.x, total.y + p.y};
            ++count;
        }
    return count ? Offset{total.x / float(count), total.y / float(count)} : Offset{};
}

float calculateCentroidSize(const PointerEvent& event, bool useCurrent) {
    Offset centroid = calculateCentroid(event, useCurrent);
    float distance = 0;
    int count = 0;
    for (const auto& c : event.changes)
        if (bothPressed(c)) {
            Offset p = useCurrent ? c.position : c.previousPosition;
            distance += std::hypot(p.x - centroid.x, p.y - centroid.y);
            ++count;
        }
    return count ? distance / float(count) : 0.f;
}

Offset calculatePan(const PointerEvent& event) {
    Offset now = calculateCentroid(event, true), before = calculateCentroid(event, false);
    return {now.x - before.x, now.y - before.y};
}

float calculateZoom(const PointerEvent& event) {
    if (std::count_if(event.changes.begin(), event.changes.end(), bothPressed) < 2) return 1;
    float before = calculateCentroidSize(event, false);
    return before > 0 ? calculateCentroidSize(event, true) / before : 1.f;
}

// Each pointer's turn around the centroid, weighed by its distance from it.
float calculateRotation(const PointerEvent& event) {
    if (std::count_if(event.changes.begin(), event.changes.end(), bothPressed) < 2) return 0;
    Offset now = calculateCentroid(event, true), before = calculateCentroid(event, false);
    float rotation = 0, weight = 0;
    for (const auto& c : event.changes) {
        if (!bothPressed(c)) continue;
        Offset a{c.previousPosition.x - before.x, c.previousPosition.y - before.y};
        Offset b{c.position.x - now.x, c.position.y - now.y};
        float diff = angleOf(b) - angleOf(a);
        if (diff > 180) diff -= 360;
        if (diff < -180) diff += 360;
        float w = std::hypot(a.x + b.x, a.y + b.y) / 2;
        rotation += diff * w;
        weight += w;
    }
    return weight > 0 ? rotation / weight : 0.f;
}

namespace {

// detectTransformGestures, and transformable() with its single-finger pan rule and progress.
Task transformGestures(PointerInputScope& scope, std::function<void(Offset, Offset, float, float)> onGesture,
                       bool panZoomLock, std::function<bool(Offset)> canPan, std::function<void(bool)> onActive) {
    constexpr float pi = std::numbers::pi_v<float>;
    while (true) {
        PointerEvent down = co_await scope.awaitPointerEvent();
        if (down.type != PointerEventType::Press) continue;
        float rotation = 0, zoom = 1;
        Offset pan;
        bool pastSlop = false, lockedToPanZoom = false;
        while (true) {
            PointerEvent e = co_await scope.awaitPointerEvent();
            if (e.type == PointerEventType::Cancel || e.change.isConsumed()) break;  // someone else took it
            float zoomChange = calculateZoom(e), rotationChange = calculateRotation(e);
            Offset panChange = calculatePan(e);
            bool oneFinger = std::count_if(e.changes.begin(), e.changes.end(), bothPressed) < 2;
            if (oneFinger && canPan && !canPan(panChange)) panChange = {};  // left to scrolling
            if (!pastSlop) {
                zoom *= zoomChange;
                rotation += rotationChange;
                pan = {pan.x + panChange.x, pan.y + panChange.y};
                float size = calculateCentroidSize(e, false);
                float zoomMotion = std::abs(1 - zoom) * size, rotationMotion = std::abs(rotation * pi * size / 180);
                float panMotion = std::hypot(pan.x, pan.y), slop = scope.viewConfiguration().touchSlop;
                if (zoomMotion > slop || rotationMotion > slop || panMotion > slop) {
                    pastSlop = true;
                    lockedToPanZoom = panZoomLock && rotationMotion < slop;
                    if (onActive) onActive(true);
                }
            }
            if (pastSlop) {
                float effectiveRotation = lockedToPanZoom ? 0.f : rotationChange;
                bool changed = effectiveRotation != 0 || zoomChange != 1 || panChange.x != 0 || panChange.y != 0;
                if (changed) {
                    onGesture(calculateCentroid(e, false), panChange, zoomChange, effectiveRotation);
                    e.change.consume();
                }
            }
            if (std::none_of(e.changes.begin(), e.changes.end(), [](const PointerInputChange& c) { return c.pressed; })) break;
        }
        if (pastSlop && onActive) onActive(false);
    }
}

} // namespace

Task detectTransformGestures(PointerInputScope& scope, std::function<void(Offset, Offset, float, float)> onGesture,
                             bool panZoomLock) {
    co_await transformGestures(scope, std::move(onGesture), panZoomLock, {}, {});
}

struct TransformableState::Data {
    OnTransformation onTransformation;
    MutableState<bool> inProgress{false};
    std::function<bool(Offset)> canPan;  // from the latest transformable() call
};

TransformableState::TransformableState(OnTransformation onTransformation) : data_(std::make_shared<Data>()) {
    data_->onTransformation = std::move(onTransformation);
}

void TransformableState::transformBy(float zoomChange, Offset panChange, float rotationChange) const {
    if (data_->onTransformation) {
        auto f = data_->onTransformation;  // the callback may replace it
        f(zoomChange, panChange, rotationChange);
    }
}

bool TransformableState::isTransformInProgress() const { return data_->inProgress.get(); }

TransformableState rememberTransformableState(TransformableState::OnTransformation onTransformation, CallSite site) {
    auto& state = remember([] { return TransformableState({}); }, site);
    state.data_->onTransformation = std::move(onTransformation);
    return state;
}

Modifier Modifier::transformable(const TransformableState& state, std::function<bool(Offset)> canPan,
                                 bool lockRotationOnZoomPan, bool enabled) const {
    if (!enabled) return *this;
    state.data_->canPan = std::move(canPan);  // read at each step: the latest one applies
    auto zoom = std::make_shared<ZoomElement>();
    zoom->onZoom = [state](float factor, float rotation, Offset) { state.transformBy(factor, {}, rotation); };
    return pointerInput(state, [state, lockRotationOnZoomPan](PointerInputScope& scope) -> Task {
        auto d = state.data_;
        co_await transformGestures(
            scope, [state](Offset, Offset pan, float zoom, float rotation) { state.transformBy(zoom, pan, rotation); },
            lockRotationOnZoomPan, [d](Offset pan) { return !d->canPan || d->canPan(pan); },
            [d](bool active) { d->inProgress.set(active); });
    }).then(std::move(zoom));
}

} // namespace kompoz
