#pragma once

// StateFlow: Compose apps keep screen state in a ViewModel as a StateFlow and read it with
// collectAsState(). Here too: a MutableStateFlow holds a value that any thread may set (a
// worker finishing a download, say); composables read it with collectAsState() and
// coroutines with collect(), both on the UI thread.
//
//   struct CounterViewModel {
//       MutableStateFlow<int> count{0};
//       void increment() { count.update([](int n) { return n + 1; }); }
//   };
//   int count = viewModel.count.collectAsState();  // recomposes when it changes
//
// It is conflated, as in Kotlin: a collector that falls behind sees the latest value, and
// setting the value it already has does nothing.
//
// Flow: a cold stream of values, produced anew for each collector by a coroutine that
// suspends in emit until the collector has taken the value (Kotlin's flow { }). Operators
// make new flows; stateIn turns one into a StateFlow kept by a scope (a ViewModel's):
//
//   Flow<int> ticks = flow<int>([](FlowCollector<int> emit) -> Task {
//       for (int i = 0;; ++i) {
//           co_await emit(i);
//           co_await delay(std::chrono::seconds(1));
//       }
//   });
//   StateFlow<std::string> results = query.debounce(300ms).flatMapLatest([](const std::string& q) {
//       return repository.search(q);  // a Flow<std::string>
//   }).stateIn(viewModelScope(), "");
//
// Everything runs on the UI thread, in the collecting coroutine; cancelling it stops the
// producer too (its frames are destroyed, running their destructors).
//
// SharedFlow: a hot stream every collector gets the values of, from when it starts (and the
// last `replay` ones before): one-off events. MutableSharedFlow::tryEmit from any thread.
// shareIn and stateIn share a cold flow as SharingStarted says: Eagerly, Lazily, or
// WhileSubscribed(5000ms), collecting it only while someone collects what it is shared as
// (and a while after the last one left: a configuration change doesn't restart it).

#include "coroutines.hpp"

#include <deque>
#include <mutex>

namespace kompoz {

template <class T>
class Flow;
template <class T>
class StateFlow;
template <class T>
class MutableStateFlow;
template <class T>
class SharedFlow;
template <class T>
class MutableSharedFlow;

// When shareIn and stateIn collect the flow they share (Kotlin's SharingStarted).
struct SharingStarted {
    enum class Kind { Eagerly, Lazily, WhileSubscribed };
    Kind kind = Kind::Eagerly;
    std::chrono::nanoseconds stopTimeout{0};
    static const SharingStarted Eagerly;  // from now on, while the scope lives
    static const SharingStarted Lazily;   // from the first collector on, while the scope lives
    // While it is collected, and `stopTimeout` after the last collector left (then from the
    // next one on, again).
    static SharingStarted WhileSubscribed(std::chrono::milliseconds stopTimeout = {}) { return {Kind::WhileSubscribed, stopTimeout}; }
};
inline const SharingStarted SharingStarted::Eagerly{SharingStarted::Kind::Eagerly};
inline const SharingStarted SharingStarted::Lazily{SharingStarted::Kind::Lazily};

// What a flow's producer emits to: co_await emit(value) carries on once the collector took it.
template <class T>
using FlowCollector = std::function<Task(T)>;

namespace detail {

// Who collects a hot flow (UI thread): what WhileSubscribed watches.
struct Subscriptions {
    int count = 0;
    int64_t lastLeft = 0;            // when the count last went down to 0 (dispatcher time)
    std::function<void()> changed;  // the sharing coroutine's
    void add() {
        ++count;
        if (changed) changed();
    }
    void remove(int64_t now) {
        if (--count == 0) lastLeft = now;
        if (changed) changed();
    }
};

template <class T>
struct StateFlowCell {
    explicit StateFlowCell(T initial) : value(std::move(initial)) {}
    std::mutex mutex;  // guards value and version
    T value;
    uint64_t version = 0;
    // Collectors waiting for a new value (UI thread only).
    struct Waiter {
        std::coroutine_handle<> handle;
        std::weak_ptr<JobState> job;
    };
    std::vector<Waiter> waiters;
    Subscriptions subscriptions;

    // After a change, on the UI thread: the waiting collectors resume.
    static void notify(const std::shared_ptr<StateFlowCell>& cell) {
        runOnUiThread([cell] {
            auto waiters = std::move(cell->waiters);
            cell->waiters.clear();
            for (Waiter& w : waiters)
                if (auto job = w.job.lock(); job && !job->cancelled && !job->done)
                    job->dispatcher->schedule(job->dispatcher->now(), w.handle, w.job);
        });
    }
};

// co_await: carries on once the value's version is past `seen`.
template <class T>
struct StateFlowChangeAwaiter {
    StateFlowCell<T>* cell;
    uint64_t seen;
    bool await_ready() const {
        std::lock_guard lock(cell->mutex);
        return cell->version != seen;
    }
    template <class P>
    void await_suspend(std::coroutine_handle<P> h) {
        // A change made since await_ready posted its notification to this thread, which runs
        // after this: it finds the waiter.
        std::erase_if(cell->waiters, [](const auto& w) { return w.job.expired(); });
        cell->waiters.push_back({h, h.promise().job->weak_from_this()});
    }
    void await_resume() const noexcept {}
};

// co_await thisCoroutine(): the awaiting frame itself (no suspension).
struct ThisCoroutineAwaiter {
    std::coroutine_handle<> self;
    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        self = h;
        return false;
    }
    std::coroutine_handle<> await_resume() const noexcept { return self; }
};
inline ThisCoroutineAwaiter thisCoroutine() { return {}; }

// co_await FlowAbort{frame}: goes on in `frame` (an outer one, waiting on a Task) after its
// co_await, leaving the frames in between suspended; they are destroyed with that Task. How
// take(n) stops the producer without exceptions.
struct FlowAbort {
    std::coroutine_handle<> target;
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept { return target; }
    void await_resume() const noexcept {}
};

// One coroutine waits for others to raise it (combine, flatMapLatest, debounce).
struct FlowSignal {
    Dispatcher* dispatcher = nullptr;
    bool raised = false;
    std::coroutine_handle<> waiting;
    std::weak_ptr<JobState> job;
    void raise() {
        raised = true;
        if (auto h = std::exchange(waiting, {})) dispatcher->schedule(dispatcher->now(), h, job);
    }
    struct Awaiter {
        FlowSignal* signal;
        bool await_ready() const noexcept { return signal->raised; }
        template <class P>
        void await_suspend(std::coroutine_handle<P> h) {
            signal->waiting = h;
            signal->job = h.promise().job->weak_from_this();
        }
        void await_resume() const noexcept { signal->raised = false; }
    };
    Awaiter wait() { return {this}; }
};

// Coroutines a flow operator collects its sources in, cancelled when it goes.
class FlowChildren {
public:
    explicit FlowChildren(Dispatcher& dispatcher) : scope_(dispatcher) {}
    FlowChildren(const FlowChildren&) = delete;
    FlowChildren& operator=(const FlowChildren&) = delete;
    ~FlowChildren() { scope_.cancel(); }
    template <class F>
    void launch(F&& coroutine) const {
        scope_.launch(std::forward<F>(coroutine));
    }

private:
    CoroutineScope scope_;
};

template <class T, class C>
FlowCollector<T> toCollector(C onValue) {
    if constexpr (std::is_same_v<std::invoke_result_t<C&, const T&>, Task>) {
        return FlowCollector<T>([onValue](T value) mutable -> Task { co_await onValue(value); });
    } else {
        return FlowCollector<T>([onValue](T value) mutable -> Task {
            onValue(value);
            co_return;
        });
    }
}

} // namespace detail

// A cold stream of values (Kotlin's Flow). Copies are the same flow.
template <class T>
class Flow {
public:
    using value_type = T;
    using Producer = std::function<Task(FlowCollector<T>)>;

    explicit Flow(Producer producer) : producer_(std::make_shared<Producer>(std::move(producer))) {}

    // Inside a coroutine: runs the producer, calling `onValue` (returning void, or a Task to
    // suspend in) with each value; done when the producer is.
    template <class C>
    Task collect(C onValue) const {
        return run(producer_, detail::toCollector<T>(std::move(onValue)));
    }

    // Each value transformed.
    template <class F>
    auto map(F transform) const -> Flow<std::decay_t<std::invoke_result_t<F&, const T&>>> {
        using R = std::decay_t<std::invoke_result_t<F&, const T&>>;
        return Flow<R>([up = *this, transform](FlowCollector<R> down) mutable -> Task {
            co_await up.collect([down, &transform](const T& value) -> Task { co_await down(transform(value)); });
        });
    }
    // The values `keep` says yes to.
    template <class P>
    Flow filter(P keep) const {
        return Flow([up = *this, keep](FlowCollector<T> down) mutable -> Task {
            co_await up.collect([down, &keep](const T& value) -> Task {
                if (keep(value)) co_await down(value);
            });
        });
    }
    // `action` with each value on its way (it may return a Task to suspend in).
    template <class F>
    Flow onEach(F action) const {
        return Flow([up = *this, action](FlowCollector<T> down) mutable -> Task {
            co_await up.collect([down, &action](const T& value) -> Task {
                if constexpr (std::is_same_v<std::invoke_result_t<F&, const T&>, Task>) co_await action(value);
                else action(value);
                co_await down(value);
            });
        });
    }
    // Any number of values for each one: transform<R>([](const T& value, FlowCollector<R>& emit) -> Task { ... }).
    template <class R, class F>
    Flow<R> transform(F transform) const {
        return Flow<R>([up = *this, transform](FlowCollector<R> down) mutable -> Task {
            co_await up.collect([&down, &transform](const T& value) -> Task { co_await transform(value, down); });
        });
    }
    // The first `count` values; then the producer is stopped.
    Flow take(size_t count) const {
        return Flow([up = *this, count](FlowCollector<T> down) -> Task {
            if (count == 0) co_return;
            std::coroutine_handle<> self = co_await detail::thisCoroutine();
            size_t taken = 0;
            co_await up.collect([down, count, &taken, self](const T& value) -> Task {
                co_await down(value);
                if (++taken == count) co_await detail::FlowAbort{self};
            });
        });
    }
    // Leaves out a value equal to the one before it.
    Flow distinctUntilChanged() const {
        return Flow([up = *this](FlowCollector<T> down) -> Task {
            std::optional<T> last;
            co_await up.collect([down, &last](const T& value) -> Task {
                if (last && *last == value) co_return;
                last = value;
                co_await down(value);
            });
        });
    }
    // A value once none came after it for `timeout` (and the last one when the flow ends):
    // a search as the user stops typing.
    Flow debounce(std::chrono::nanoseconds timeout) const;
    // For each value, the flow `transform` makes of it, collected until the next value comes
    // (then that one is cancelled, and what it sent but wasn't taken yet dropped): search
    // results for the latest query.
    template <class F>
    auto flatMapLatest(F transform) const -> Flow<typename std::invoke_result_t<F&, const T&>::value_type>;

    // Collects it in `scope` from now on, into a StateFlow starting at `initial` (Kotlin's
    // stateIn(scope, SharingStarted.Eagerly, initial)): a ViewModel's screen state.
    StateFlow<T> stateIn(const CoroutineScope& scope, T initial) const { return stateIn(scope, SharingStarted::Eagerly, std::move(initial)); }
    StateFlow<T> stateIn(const CoroutineScope& scope, SharingStarted started, T initial) const;
    // A SharedFlow of its values, collected as `started` says; new collectors get the last `replay`.
    SharedFlow<T> shareIn(const CoroutineScope& scope, SharingStarted started, size_t replay = 0) const;
    // Collects it in `scope` (with onEach doing the work).
    void launchIn(const CoroutineScope& scope) const {
        scope.launch([up = *this]() -> Task { co_await up.collect([](const T&) {}); });
    }
    // In a composable: the latest value, `initial` until the first, recomposing the caller.
    // Make the flow once (in a ViewModel, or remember it): a new one starts over.
    T collectAsState(T initial, CallSite site = CallSite::current()) const {
        ComposableScope scope(site);
        Flow flow = *this;
        auto state = remember(flow, [&] { return mutableStateOf(std::move(initial)); });
        LaunchedEffect(flow, [flow, state]() -> Task {
            co_await flow.collect([state](const T& value) { state.set(value); });
        });
        return state.get();
    }

    friend bool operator==(const Flow& a, const Flow& b) { return a.producer_ == b.producer_; }

private:
    // By value: the producer and the collector live as long as the coroutine.
    static Task run(std::shared_ptr<Producer> producer, FlowCollector<T> emit) { co_await (*producer)(std::move(emit)); }
    std::shared_ptr<Producer> producer_;
};

// A flow made by a coroutine: flow<int>([](FlowCollector<int> emit) -> Task { co_await emit(1); }).
template <class T, class F>
Flow<T> flow(F producer) {
    return Flow<T>(typename Flow<T>::Producer(std::move(producer)));
}
template <class T>
Flow<T> flowOf(std::vector<T> values) {
    return Flow<T>([values](FlowCollector<T> emit) -> Task {
        for (const T& value : values) co_await emit(value);
    });
}
template <class T, class... R>
Flow<T> flowOf(T first, R... rest) {
    return flowOf(std::vector<T>{std::move(first), T(std::move(rest))...});
}
template <class T>
Flow<T> emptyFlow() {
    return Flow<T>([](FlowCollector<T>) -> Task { co_return; });
}

namespace detail {
// Flow's operators on a hot flow's values (a StateFlow's, a SharedFlow's), through its asFlow().
template <class Self, class T>
class HotFlowOperators {
public:
    template <class F>
    auto map(F transform) const {
        return self().asFlow().map(std::move(transform));
    }
    template <class P>
    Flow<T> filter(P keep) const {
        return self().asFlow().filter(std::move(keep));
    }
    template <class F>
    Flow<T> onEach(F action) const {
        return self().asFlow().onEach(std::move(action));
    }
    Flow<T> take(size_t count) const { return self().asFlow().take(count); }
    Flow<T> debounce(std::chrono::nanoseconds timeout) const { return self().asFlow().debounce(timeout); }
    template <class F>
    auto flatMapLatest(F transform) const {
        return self().asFlow().flatMapLatest(std::move(transform));
    }

private:
    const Self& self() const { return static_cast<const Self&>(*this); }
};
} // namespace detail

// A read-only view of a MutableStateFlow (Kotlin's asStateFlow()). Copies share the value.
template <class T>
class StateFlow : public detail::HotFlowOperators<StateFlow<T>, T> {
public:
    using value_type = T;

    // The current value, from any thread.
    T value() const {
        std::lock_guard lock(cell_->mutex);
        return cell_->value;
    }

    // Inside a coroutine: calls `onValue` with the value now and then with each new one, until
    // the coroutine is cancelled.
    //   LaunchedEffect(0, [flow]() -> Task { co_await flow.collect([](int n) { ... }); });
    template <class C>
    Task collect(C onValue) const {
        return asFlow().collect(std::move(onValue));
    }

    // As a Flow: its value now, then each new one (it never ends).
    Flow<T> asFlow() const {
        return Flow<T>([cell = cell_](FlowCollector<T> emit) -> Task {
            detail::JobState* job = co_await detail::currentJob();
            cell->subscriptions.add();
            struct Leave {  // counted while it collects
                detail::StateFlowCell<T>* cell;
                detail::Dispatcher* dispatcher;
                ~Leave() { cell->subscriptions.remove(dispatcher->now()); }
            } leave{cell.get(), job->dispatcher};
            uint64_t seen = 0;
            auto read = [&] {
                std::lock_guard lock(cell->mutex);
                seen = cell->version;
                return cell->value;
            };
            T last = read();
            co_await emit(last);
            while (true) {
                co_await detail::StateFlowChangeAwaiter<T>{cell.get(), seen};
                T next = read();
                if constexpr (std::equality_comparable<T>)
                    if (next == last) continue;  // changed and changed back
                last = std::move(next);
                co_await emit(last);
            }
        });
    }
    // In a composable: the value, recomposing the caller when it changes (Compose's
    // collectAsState()). Collects while the caller is in the composition.
    T collectAsState(CallSite site = CallSite::current()) const {
        ComposableScope scope(site);
        StateFlow flow = *this;
        auto state = remember(flow, [&] { return mutableStateOf(flow.value()); });
        LaunchedEffect(flow, [flow, state]() -> Task {
            co_await flow.collect([state](const T& value) { state.set(value); });
        });
        return state.get();
    }

    // The same flow, not the same value.
    friend bool operator==(const StateFlow& a, const StateFlow& b) { return a.cell_ == b.cell_; }

    // Collectors right now (UI thread).
    int subscriptionCount() const { return cell_->subscriptions.count; }

protected:
    template <class U>
    friend class Flow;
    explicit StateFlow(std::shared_ptr<detail::StateFlowCell<T>> cell) : cell_(std::move(cell)) {}
    std::shared_ptr<detail::StateFlowCell<T>> cell_;
};

template <class T>
class MutableStateFlow : public StateFlow<T> {
public:
    explicit MutableStateFlow(T initial) : StateFlow<T>(std::make_shared<detail::StateFlowCell<T>>(std::move(initial))) {}

    // From any thread. Setting the value it already has does nothing.
    void setValue(T value) const {
        {
            std::lock_guard lock(cell()->mutex);
            if constexpr (std::equality_comparable<T>)
                if (cell()->value == value) return;
            cell()->value = std::move(value);
            ++cell()->version;
        }
        detail::StateFlowCell<T>::notify(cell());
    }

    // Sets the value to `transform(current)` atomically: updates from several threads don't
    // overwrite each other (if another one got in first, transform runs again on its result).
    //   count.update([](int n) { return n + 1; });
    template <class F>
    void update(F&& transform) const {
        while (true) {
            uint64_t version = 0;
            T current = [&] {
                std::lock_guard lock(cell()->mutex);
                version = cell()->version;
                return cell()->value;
            }();
            T next = transform(std::as_const(current));
            {
                std::lock_guard lock(cell()->mutex);
                if (cell()->version != version) continue;
                if constexpr (std::equality_comparable<T>)
                    if (cell()->value == next) return;
                cell()->value = std::move(next);
                ++cell()->version;
            }
            detail::StateFlowCell<T>::notify(cell());
            return;
        }
    }

    // Sets `update` if the value is still `expect`; returns whether it did.
    bool compareAndSet(const T& expect, T update) const {
        {
            std::lock_guard lock(cell()->mutex);
            if (!(cell()->value == expect)) return false;
            if (cell()->value == update) return true;
            cell()->value = std::move(update);
            ++cell()->version;
        }
        detail::StateFlowCell<T>::notify(cell());
        return true;
    }

    StateFlow<T> asStateFlow() const { return *this; }

private:
    const std::shared_ptr<detail::StateFlowCell<T>>& cell() const { return this->cell_; }
};

namespace detail {

template <class T>
struct SharedFlowCell {
    explicit SharedFlowCell(size_t r) : replay(r) {}
    std::mutex mutex;  // guards the cache and the subscribers' queues
    size_t replay;
    std::deque<T> cache;  // the last `replay` values
    struct Subscriber {
        std::deque<T> pending;
        std::coroutine_handle<> waiting;
        std::weak_ptr<JobState> job;
    };
    std::vector<std::shared_ptr<Subscriber>> subscribers;
    Subscriptions subscriptions;  // UI thread

    // From any thread: into the cache and every collector's queue; they resume on the UI thread.
    static void emit(const std::shared_ptr<SharedFlowCell>& cell, const T& value) {
        {
            std::lock_guard lock(cell->mutex);
            if (cell->replay > 0) {
                cell->cache.push_back(value);
                if (cell->cache.size() > cell->replay) cell->cache.pop_front();
            }
            for (auto& s : cell->subscribers) s->pending.push_back(value);
        }
        runOnUiThread([cell] {
            std::vector<std::pair<std::coroutine_handle<>, std::weak_ptr<JobState>>> resume;
            {
                std::lock_guard lock(cell->mutex);
                for (auto& s : cell->subscribers)
                    if (s->waiting && !s->pending.empty()) resume.push_back({std::exchange(s->waiting, {}), s->job});
            }
            for (auto& [h, weak] : resume)
                if (auto job = weak.lock(); job && !job->cancelled && !job->done) job->dispatcher->schedule(job->dispatcher->now(), h, weak);
        });
    }
};

template <class T>
struct SharedFlowAwaiter {
    SharedFlowCell<T>* cell;
    typename SharedFlowCell<T>::Subscriber* subscriber;
    bool await_ready() const {
        std::lock_guard lock(cell->mutex);
        return !subscriber->pending.empty();
    }
    template <class P>
    void await_suspend(std::coroutine_handle<P> h) {
        std::lock_guard lock(cell->mutex);
        subscriber->waiting = h;
        subscriber->job = h.promise().job->weak_from_this();
    }
    void await_resume() const noexcept {}
};

// Collects `up` into `sink` as `started` says, watching who collects what it is shared as.
template <class T, class Sink>
void share(const CoroutineScope& scope, SharingStarted started, Flow<T> up, Sink sink, std::shared_ptr<void> keep,
           Subscriptions* subscriptions) {
    if (started.kind == SharingStarted::Kind::Eagerly) {
        scope.launch([up, sink]() -> Task { co_await up.collect(sink); });
        return;
    }
    scope.launch([up, sink, started, keep, subscriptions]() -> Task {
        JobState* job = co_await currentJob();
        Dispatcher* dispatcher = job->dispatcher;
        FlowSignal signal;
        signal.dispatcher = dispatcher;
        subscriptions->changed = [&signal] { signal.raise(); };
        struct Unwatch {
            Subscriptions* s;
            ~Unwatch() { s->changed = {}; }
        } unwatch{subscriptions};
        std::optional<FlowChildren> running;  // collecting the upstream flow
        while (true) {
            if (subscriptions->count > 0 && !running) {
                running.emplace(*dispatcher);
                running->launch([up, sink]() -> Task { co_await up.collect(sink); });
            } else if (subscriptions->count == 0 && running && started.kind == SharingStarted::Kind::WhileSubscribed) {
                int64_t wait = subscriptions->lastLeft + started.stopTimeout.count() - dispatcher->now();
                if (wait > 0) {
                    co_await DelayAwaiter{wait};
                    continue;  // someone may have come back meanwhile
                }
                running.reset();  // until someone collects again
            }
            co_await signal.wait();
        }
    });
}

} // namespace detail

template <class T>
StateFlow<T> Flow<T>::stateIn(const CoroutineScope& scope, SharingStarted started, T initial) const {
    MutableStateFlow<T> state(std::move(initial));
    detail::share(scope, started, *this, [state](const T& value) { state.setValue(value); }, state.cell_, &state.cell_->subscriptions);
    return state.asStateFlow();
}

// A read-only view of a MutableSharedFlow. Copies are the same flow.
template <class T>
class SharedFlow : public detail::HotFlowOperators<SharedFlow<T>, T> {
public:
    using value_type = T;

    // Inside a coroutine: calls `onValue` with each value from now on (the replayed ones first),
    // until the coroutine is cancelled.
    template <class C>
    Task collect(C onValue) const {
        return asFlow().collect(std::move(onValue));
    }
    // As a Flow (it never ends).
    Flow<T> asFlow() const {
        return Flow<T>([cell = cell_](FlowCollector<T> emit) -> Task {
            using Cell = detail::SharedFlowCell<T>;
            auto subscriber = std::make_shared<typename Cell::Subscriber>();
            {
                std::lock_guard lock(cell->mutex);
                subscriber->pending.assign(cell->cache.begin(), cell->cache.end());
                cell->subscribers.push_back(subscriber);
            }
            detail::JobState* job = co_await detail::currentJob();
            cell->subscriptions.add();
            struct Leave {
                Cell* cell;
                typename Cell::Subscriber* subscriber;
                detail::Dispatcher* dispatcher;
                ~Leave() {
                    {
                        std::lock_guard lock(cell->mutex);
                        std::erase_if(cell->subscribers, [&](const auto& s) { return s.get() == subscriber; });
                    }
                    cell->subscriptions.remove(dispatcher->now());
                }
            } leave{cell.get(), subscriber.get(), job->dispatcher};
            while (true) {
                std::optional<T> next;
                {
                    std::lock_guard lock(cell->mutex);
                    if (!subscriber->pending.empty()) {
                        next = std::move(subscriber->pending.front());
                        subscriber->pending.pop_front();
                    }
                }
                if (next) co_await emit(std::move(*next));
                else co_await detail::SharedFlowAwaiter<T>{cell.get(), subscriber.get()};
            }
        });
    }
    // Collectors right now (UI thread), and the values a new one gets first.
    int subscriptionCount() const { return cell_->subscriptions.count; }
    std::vector<T> replayCache() const {
        std::lock_guard lock(cell_->mutex);
        return {cell_->cache.begin(), cell_->cache.end()};
    }
    friend bool operator==(const SharedFlow& a, const SharedFlow& b) { return a.cell_ == b.cell_; }

protected:
    template <class U>
    friend class Flow;
    explicit SharedFlow(std::shared_ptr<detail::SharedFlowCell<T>> cell) : cell_(std::move(cell)) {}
    std::shared_ptr<detail::SharedFlowCell<T>> cell_;
};

template <class T>
class MutableSharedFlow : public SharedFlow<T> {
public:
    explicit MutableSharedFlow(size_t replay = 0) : SharedFlow<T>(std::make_shared<detail::SharedFlowCell<T>>(replay)) {}
    // From any thread. It never waits: every collector keeps what it hasn't taken yet.
    bool tryEmit(T value) const {
        detail::SharedFlowCell<T>::emit(this->cell_, value);
        return true;
    }
    // co_await events.emit(value) inside a coroutine.
    Task emit(T value) const {
        tryEmit(std::move(value));
        co_return;
    }
    void resetReplayCache() const {
        std::lock_guard lock(this->cell_->mutex);
        this->cell_->cache.clear();
    }
    SharedFlow<T> asSharedFlow() const { return *this; }
};

template <class T>
SharedFlow<T> Flow<T>::shareIn(const CoroutineScope& scope, SharingStarted started, size_t replay) const {
    MutableSharedFlow<T> shared(replay);
    detail::share(scope, started, *this, [shared](const T& value) { shared.tryEmit(value); }, shared.cell_, &shared.cell_->subscriptions);
    return shared.asSharedFlow();
}

template <class T>
Flow<T> Flow<T>::debounce(std::chrono::nanoseconds timeout) const {
    return Flow([up = *this, timeout](FlowCollector<T> down) -> Task {
        struct Shared {
            std::optional<T> latest;
            uint64_t version = 0;
            int64_t at = 0;  // when the latest came
            bool done = false;
            detail::FlowSignal signal;
        };
        auto shared = std::make_shared<Shared>();
        detail::JobState* job = co_await detail::currentJob();
        detail::Dispatcher* dispatcher = job->dispatcher;
        shared->signal.dispatcher = dispatcher;
        detail::FlowChildren source(*dispatcher);
        source.launch([up, shared, dispatcher]() -> Task {
            co_await up.collect([shared, dispatcher](const T& value) {
                shared->latest = value;
                ++shared->version;
                shared->at = dispatcher->now();
                shared->signal.raise();
            });
            shared->done = true;
            shared->signal.raise();
        });
        uint64_t emitted = 0;
        while (true) {
            co_await shared->signal.wait();
            while (shared->version != emitted && !shared->done) {  // none newer for `timeout`?
                int64_t wait = shared->at + timeout.count() - dispatcher->now();
                if (wait > 0) {
                    co_await DelayAwaiter{wait};
                    continue;
                }
                emitted = shared->version;
                co_await down(*shared->latest);
            }
            if (shared->done) {
                if (shared->version != emitted) co_await down(*shared->latest);
                co_return;
            }
        }
    });
}

template <class T>
template <class F>
auto Flow<T>::flatMapLatest(F transform) const -> Flow<typename std::invoke_result_t<F&, const T&>::value_type> {
    using R = typename std::invoke_result_t<F&, const T&>::value_type;
    return Flow<R>([up = *this, transform](FlowCollector<R> down) -> Task {
        struct Shared {
            std::deque<R> items;
            bool sourceDone = false, innerDone = true;
            detail::FlowSignal signal;
            std::optional<detail::FlowChildren> inner;  // collecting the latest value's flow
        };
        auto shared = std::make_shared<Shared>();
        struct InnerGuard {  // the inner collection holds `shared`: it goes with this frame
            std::shared_ptr<Shared> shared;
            ~InnerGuard() { shared->inner.reset(); }
        } guard{shared};
        detail::JobState* job = co_await detail::currentJob();
        detail::Dispatcher* dispatcher = job->dispatcher;
        shared->signal.dispatcher = dispatcher;
        detail::FlowChildren source(*dispatcher);
        source.launch([up, transform, shared, dispatcher]() mutable -> Task {
            co_await up.collect([&transform, shared, dispatcher](const T& value) {
                shared->inner.reset();  // the previous one, cancelled; what it sent is stale
                shared->items.clear();
                shared->innerDone = false;
                shared->inner.emplace(*dispatcher);
                shared->inner->launch([flow = transform(value), shared]() -> Task {
                    co_await flow.collect([shared](const R& item) {
                        shared->items.push_back(item);
                        shared->signal.raise();
                    });
                    shared->innerDone = true;
                    shared->signal.raise();
                });
            });
            shared->sourceDone = true;
            shared->signal.raise();
        });
        while (true) {
            co_await shared->signal.wait();
            while (!shared->items.empty()) {
                R item = std::move(shared->items.front());
                shared->items.pop_front();
                co_await down(std::move(item));
            }
            if (shared->sourceDone && shared->innerDone) co_return;
        }
    });
}

namespace detail {
template <class T>
Flow<T> toFlow(const Flow<T>& flow) {
    return flow;
}
template <class T>
Flow<T> toFlow(const StateFlow<T>& flow) {
    return flow.asFlow();
}
template <class T>
Flow<T> toFlow(const SharedFlow<T>& flow) {
    return flow.asFlow();
}
} // namespace detail

// The latest values of two flows (or StateFlows), combined, each time either has a new one
// once both have one (Kotlin's combine).
template <class FA, class FB, class F>
auto combine(const FA& first, const FB& second, F transform) {
    auto a = detail::toFlow(first);
    auto b = detail::toFlow(second);
    using A = typename decltype(a)::value_type;
    using B = typename decltype(b)::value_type;
    using R = std::decay_t<std::invoke_result_t<F&, const A&, const B&>>;
    return Flow<R>([a, b, transform](FlowCollector<R> down) mutable -> Task {
        struct Latest {
            std::optional<A> a;
            std::optional<B> b;
            uint64_t version = 0;
            int finished = 0;
            detail::FlowSignal signal;
        };
        auto latest = std::make_shared<Latest>();
        detail::JobState* job = co_await detail::currentJob();
        latest->signal.dispatcher = job->dispatcher;
        detail::FlowChildren sources(*job->dispatcher);
        sources.launch([a, latest]() -> Task {
            co_await a.collect([latest](const A& value) {
                latest->a = value;
                ++latest->version;
                latest->signal.raise();
            });
            ++latest->finished;
            latest->signal.raise();
        });
        sources.launch([b, latest]() -> Task {
            co_await b.collect([latest](const B& value) {
                latest->b = value;
                ++latest->version;
                latest->signal.raise();
            });
            ++latest->finished;
            latest->signal.raise();
        });
        uint64_t emitted = 0;
        while (true) {
            co_await latest->signal.wait();
            if (latest->a && latest->b && latest->version != emitted) {
                emitted = latest->version;
                co_await down(transform(*latest->a, *latest->b));
                continue;  // more may have come meanwhile
            }
            if (latest->finished == 2) co_return;
        }
    });
}

} // namespace kompoz
