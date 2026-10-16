#include <kompoz/host.hpp>
#include <kompoz/lifecycle.hpp>
#include <kompoz/state.hpp>

#include <algorithm>

namespace kompoz {

struct Lifecycle::Data {
    explicit Data(LifecycleState initial) : state(initial), observable(mutableStateOf(initial)), flow(initial) {}
    LifecycleState state;
    MutableState<LifecycleState> observable;
    MutableStateFlow<LifecycleState> flow;
    struct Entry {
        Observer observer;
        LifecycleState seen = LifecycleState::Initialized;
    };
    std::vector<std::shared_ptr<Entry>> entries;
};

namespace {

// One step from `from` towards `to`: the state it gets to and the event on the way.
std::pair<LifecycleState, LifecycleEvent> step(LifecycleState from, LifecycleState to) {
    using S = LifecycleState;
    using E = LifecycleEvent;
    if (from < to) {
        switch (from) {
        case S::Destroyed:
        case S::Initialized: return {S::Created, E::OnCreate};
        case S::Created: return {S::Started, E::OnStart};
        default: return {S::Resumed, E::OnResume};
        }
    }
    switch (from) {
    case S::Resumed: return {S::Started, E::OnPause};
    case S::Started: return {S::Created, E::OnStop};
    default: return {S::Destroyed, E::OnDestroy};
    }
}

// Tells the observer the events that bring it to `state`.
void bring(Lifecycle::Data::Entry& entry, LifecycleState state) {
    while (entry.seen != state) {
        if (entry.seen == LifecycleState::Initialized && state == LifecycleState::Destroyed) {
            entry.seen = state;  // never created: nothing to tell
            break;
        }
        auto [next, event] = step(entry.seen, state);
        entry.seen = next;
        auto observer = entry.observer;  // it may remove itself
        if (observer) observer(event);
    }
}

} // namespace

Lifecycle::Lifecycle(LifecycleState initial) : data_(std::make_shared<Data>(initial)) {}
LifecycleState Lifecycle::currentState() const { return data_->state; }
LifecycleState Lifecycle::currentStateAsState() const { return data_->observable.get(); }
StateFlow<LifecycleState> Lifecycle::currentStateFlow() const { return data_->flow; }

std::shared_ptr<void> Lifecycle::addObserver(Observer observer) {
    auto entry = std::make_shared<Data::Entry>();
    entry->observer = std::move(observer);
    data_->entries.push_back(entry);
    bring(*entry, data_->state);
    struct Registration {  // a handle that is there (not null), and removes the observer when it goes
        std::weak_ptr<Data> data;
        std::shared_ptr<Data::Entry> entry;
        ~Registration() {
            entry->observer = nullptr;
            if (auto d = data.lock()) std::erase(d->entries, entry);
        }
    };
    return std::shared_ptr<Registration>(new Registration{data_, entry});  // made in place: a temporary would remove it
}

void Lifecycle::setCurrentState(LifecycleState state) {
    Data& d = *data_;
    if (state == LifecycleState::Initialized && d.state > LifecycleState::Initialized) state = LifecycleState::Created;  // no way back
    while (d.state != state) {
        if (d.state == LifecycleState::Destroyed) break;  // destroyed is for good
        if (d.state == LifecycleState::Initialized && state == LifecycleState::Destroyed) {
            d.state = state;
            break;
        }
        d.state = step(d.state, state).first;
        d.observable.set(d.state);
        d.flow.setValue(d.state);
        for (const auto& entry : std::vector<std::shared_ptr<Data::Entry>>(d.entries))  // observers may come and go
            if (entry->observer) bring(*entry, d.state);
    }
    d.observable.set(d.state);
    d.flow.setValue(d.state);
}

const CompositionLocal<std::shared_ptr<LifecycleOwner>> LocalLifecycleOwner{nullptr};

std::shared_ptr<LifecycleOwner> currentLifecycleOwner() {
    if (auto owner = LocalLifecycleOwner.current()) return owner;
    return detail::Composer::require().host().lifecycleOwner();
}

void LifecycleEventEffect(LifecycleEvent event, std::function<void()> onEvent, CallSite site) {
    ComposableScope scope(site);
    auto owner = currentLifecycleOwner();
    auto latest = rememberUpdatedState(std::move(onEvent));
    DisposableEffect(owner, [owner, latest, event]() -> std::function<void()> {
        auto token = owner->lifecycle().addObserver([latest, event](LifecycleEvent e) {
            if (e == event)
                if (auto f = latest.get()) f();
        });
        return [token] {};  // the observer goes with the token
    });
}

std::function<void()> detail::whileAtLeast(Lifecycle& lifecycle, LifecycleState min, std::function<std::function<void()>()> start) {
    using E = LifecycleEvent;
    E on = min == LifecycleState::Resumed ? E::OnResume : min == LifecycleState::Started ? E::OnStart : E::OnCreate;
    E off = min == LifecycleState::Resumed ? E::OnPause : min == LifecycleState::Started ? E::OnStop : E::OnDestroy;
    auto stop = std::make_shared<std::function<void()>>();
    auto token = lifecycle.addObserver([start = std::move(start), stop, on, off](LifecycleEvent e) {
        if (e == on) {
            if (auto f = std::exchange(*stop, nullptr)) f();
            *stop = start();
        } else if (e == off) {
            if (auto f = std::exchange(*stop, nullptr)) f();
        }
    });
    return [token, stop]() mutable {
        token.reset();  // no more events
        if (auto f = std::exchange(*stop, nullptr)) f();  // still started
    };
}

void detail::lifecycleEffect(size_t key, LifecycleState from, std::function<std::function<void()>()> effect, const CallSite& site) {
    ComposableScope scope(site);
    auto owner = currentLifecycleOwner();
    auto latest = rememberUpdatedState(std::move(effect));
    struct Key {
        size_t key;
        std::shared_ptr<LifecycleOwner> owner;
        bool operator==(const Key&) const = default;
    };
    DisposableEffect(Key{key, owner}, [owner, latest, from]() -> std::function<void()> {
        return whileAtLeast(owner->lifecycle(), from, [latest]() -> std::function<void()> {
            auto effect = latest.get();
            return effect ? effect() : nullptr;
        });
    });
}

} // namespace kompoz
