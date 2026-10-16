#pragma once

// Lifecycles, as Jetpack's: whether what is composed is on the screen and in front. The app's
// follows the window (Android: the activity going to the background and back; desktops: the
// window minimized or not focused); a navigation entry's is the app's at most — resumed while
// it is the screen shown, started while it comes or goes, created while it waits on the back
// stack, destroyed once popped for good. LocalLifecycleOwner gives the nearest one.
//
//   LifecycleEventEffect(LifecycleEvent::OnResume, [&] { analytics.screenShown(); });
//   int unread = collectAsStateWithLifecycle(vm->unread);  // not collected while in the background

#include "coroutines.hpp"
#include "flow.hpp"
#include "runtime.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace kompoz {

enum class LifecycleState { Destroyed, Initialized, Created, Started, Resumed };
enum class LifecycleEvent { OnCreate, OnStart, OnResume, OnPause, OnStop, OnDestroy };

// Moved from state to state by its owner, one step (event) at a time, as LifecycleRegistry.
class Lifecycle {
public:
    explicit Lifecycle(LifecycleState initial = LifecycleState::Initialized);
    LifecycleState currentState() const;         // not observable
    LifecycleState currentStateAsState() const;  // observable: read in a composable, it recomposes with it
    StateFlow<LifecycleState> currentStateFlow() const;
    // An observer gets the events from now on, after the ones that bring it up to the current
    // state (so one added while resumed hears OnCreate, OnStart, OnResume at once).
    using Observer = std::function<void(LifecycleEvent)>;
    std::shared_ptr<void> addObserver(Observer observer);  // removed when the handle goes
    // The owner's: steps from the current state to `state`, telling the observers each event.
    void setCurrentState(LifecycleState state);

    struct Data;
    std::shared_ptr<Data> data_;
};

class LifecycleOwner {
public:
    virtual ~LifecycleOwner() = default;
    virtual Lifecycle& lifecycle() const = 0;
};

// The nearest owner: a navigation entry's inside a NavHost, else the app's (the host's).
extern const CompositionLocal<std::shared_ptr<LifecycleOwner>> LocalLifecycleOwner;
std::shared_ptr<LifecycleOwner> currentLifecycleOwner();  // inside a composition

// Runs `onEvent` when the owner's lifecycle has `event` (also the ones it is already past,
// when first composed: OnResume runs at once on a resumed screen).
void LifecycleEventEffect(LifecycleEvent event, std::function<void()> onEvent, CallSite site = CallSite::current());
// `effect` when it starts (resumes), what it returns when it stops (pauses) or leaves the
// composition; again for each start. Keyed like DisposableEffect.
template <class K>
void LifecycleStartEffect(const K& key, std::function<std::function<void()>()> effect, CallSite site = CallSite::current());
template <class K>
void LifecycleResumeEffect(const K& key, std::function<std::function<void()>()> effect, CallSite site = CallSite::current());

namespace detail {
void lifecycleEffect(size_t key, LifecycleState from, std::function<std::function<void()>()> effect, const CallSite& site);
// While `lifecycle` is at least `min`: `start` each time it gets there, what that returns each
// time it drops below. The returned function stops it (running the last one if still due).
std::function<void()> whileAtLeast(Lifecycle& lifecycle, LifecycleState min, std::function<std::function<void()>()> start);
}
template <class K>
void LifecycleStartEffect(const K& key, std::function<std::function<void()>()> effect, CallSite site) {
    detail::lifecycleEffect(std::hash<K>{}(key), LifecycleState::Started, std::move(effect), site);
}
template <class K>
void LifecycleResumeEffect(const K& key, std::function<std::function<void()>()> effect, CallSite site) {
    detail::lifecycleEffect(std::hash<K>{}(key), LifecycleState::Resumed, std::move(effect), site);
}

// The flow's values as state, collected only while the lifecycle is at least minActiveState
// (Jetpack's collectAsStateWithLifecycle): in the background it stops — and so no longer
// counts as a subscriber (stateIn's WhileSubscribed lets the upstream go); back, it catches up.
template <class T>
T collectAsStateWithLifecycle(const StateFlow<T>& flow, LifecycleState minActiveState = LifecycleState::Started,
                              CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    auto owner = currentLifecycleOwner();
    auto& state = remember([&] { return mutableStateOf(flow.value()); });
    detail::Dispatcher* dispatcher = &detail::currentDispatcher();
    struct Key {
        std::shared_ptr<LifecycleOwner> owner;
        StateFlow<T> flow;
        LifecycleState min;
        bool operator==(const Key&) const = default;
    };
    DisposableEffect(Key{owner, flow, minActiveState}, [owner, flow, state, minActiveState, dispatcher]() -> std::function<void()> {
        return detail::whileAtLeast(owner->lifecycle(), minActiveState, [flow, state, dispatcher]() -> std::function<void()> {
            CoroutineScope collecting(*dispatcher);
            collecting.launch([flow, state]() -> Task { co_await flow.collect([state](const T& value) { state.set(value); }); });
            return [collecting] { collecting.cancel(); };
        });
    });
    return state.get();
}

} // namespace kompoz
