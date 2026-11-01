#pragma once

// Coroutines on the UI thread, built on C++20 coroutines: the counterpart of Compose's
// LaunchedEffect, rememberCoroutineScope, delay and withFrameNanos.
//
//   LaunchedEffect(0, [seconds]() -> Task {
//       while (true) {
//           co_await delay(std::chrono::seconds(1));
//           seconds.set(seconds.get() + 1);
//       }
//   });
//
// A coroutine is a lambda returning Task. Its captures are kept alive by the scope for as
// long as it runs (capture by value). Leaving the composition, or a key change, cancels it:
// the coroutine is destroyed at its current co_await, running its destructors.

#include "runtime.hpp"

#include <chrono>
#include <coroutine>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kompoz {

// What a cancelled coroutineScope throws to the coroutine awaiting it, and what a withTimeout that
// ran out throws (TimeoutCancellationException). A coroutine ending with one was cancelled, not
// failed: it isn't reported, and a coroutineScope isn't failed by a child ending with one.
struct CancellationException : std::runtime_error {
    CancellationException() : std::runtime_error("cancelled") {}
    using std::runtime_error::runtime_error;
};
struct TimeoutCancellationException : CancellationException {
    TimeoutCancellationException() : CancellationException("timed out") {}
};

namespace detail {
struct JobState;
class Dispatcher;
struct ScopeAccess;
} // namespace detail

namespace detail {
// What every coroutine frame here keeps (Task's and TaskOf's): the launched coroutine it belongs
// to, who awaits it, and what it threw.
struct PromiseBase {
    JobState* job = nullptr;  // the launched coroutine this frame belongs to
    std::coroutine_handle<> continuation;
    std::exception_ptr exception;

    std::suspend_always initial_suspend() noexcept { return {}; }
    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }
        template <class P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) noexcept {
            if (auto c = h.promise().continuation) return c;  // back to whoever co_awaited us
            return std::noop_coroutine();
        }
        void await_resume() noexcept {}
    };
    FinalAwaiter final_suspend() noexcept { return {}; }
    void unhandled_exception() { exception = std::current_exception(); }
};
} // namespace detail

class Task {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    struct promise_type : detail::PromiseBase {
        Task get_return_object() { return Task(Handle::from_promise(*this)); }
        void return_void() {}
    };

    explicit Task(Handle h) : handle_(h) {}
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    ~Task() {
        if (handle_) handle_.destroy();
    }

    // `co_await task` runs it as part of the awaiting coroutine.
    bool await_ready() const noexcept { return !handle_ || handle_.done(); }
    template <class P>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> parent) noexcept {
        handle_.promise().job = parent.promise().job;
        handle_.promise().continuation = parent;
        return handle_;
    }
    void await_resume() {
        if (handle_ && handle_.promise().exception) std::rethrow_exception(handle_.promise().exception);
    }

    Handle release() { return std::exchange(handle_, {}); }

private:
    Handle handle_;
};

// A coroutine that gives back a value (Kotlin's suspend fun returning one): co_return it, and
// co_await the call from another coroutine (it runs as part of it, cancelled with it).
//   TaskOf<std::string> loadName(int id) {
//       co_await delay(std::chrono::milliseconds(300));
//       co_return names[id];
//   }
//   LaunchedEffect(id, [id, name]() -> Task { name.set(co_await loadName(id)); });
template <class T>
class TaskOf {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    using value_type = T;
    struct promise_type : detail::PromiseBase {
        std::optional<T> value;
        TaskOf get_return_object() { return TaskOf(Handle::from_promise(*this)); }
        template <class U>
        void return_value(U&& v) {
            value.emplace(std::forward<U>(v));
        }
    };

    explicit TaskOf(Handle h) : handle_(h) {}
    TaskOf(TaskOf&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    TaskOf& operator=(TaskOf&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    ~TaskOf() {
        if (handle_) handle_.destroy();
    }

    bool await_ready() const noexcept { return !handle_ || handle_.done(); }
    template <class P>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> parent) noexcept {
        handle_.promise().job = parent.promise().job;
        handle_.promise().continuation = parent;
        return handle_;
    }
    T await_resume() {
        if (handle_.promise().exception) std::rethrow_exception(handle_.promise().exception);
        return std::move(*handle_.promise().value);
    }

private:
    Handle handle_;
};

namespace detail {

struct ScopeState;

struct JobState : std::enable_shared_from_this<JobState> {
    Dispatcher* dispatcher = nullptr;
    Task::Handle handle;
    std::shared_ptr<void> callable;  // the lambda whose captures the coroutine refers to
    std::weak_ptr<ScopeState> scope;
    bool running = false, cancelled = false, done = false;
    bool deferred = false;  // an async's: what it throws goes to who awaits it
    bool body = false;      // a coroutineScope's own block: what it throws fails the scope, a supervisor's too
    std::exception_ptr failure;  // what it threw
    std::vector<std::pair<std::weak_ptr<JobState>, std::coroutine_handle<>>> joiners;  // waiting for it to end

    ~JobState() {
        if (handle) handle.destroy();
    }
    void cancel();
    void finish();  // after a resume: clean up if it completed or was cancelled meanwhile

private:
    void complete();  // ended: its frame gone, who waits for it resumed, out of its scope
};

struct ScopeState {
    Dispatcher* dispatcher = nullptr;
    std::vector<std::shared_ptr<JobState>> jobs;
    bool cancelled = false;
    // A coroutineScope's: a child failing fails it, cancelling the others (a supervisor's: only its
    // block does); whenEmpty resumes who awaits it once its last child is done.
    bool structured = false, supervisor = false;
    std::exception_ptr failure;
    std::function<void()> whenEmpty;

    void cancel(const JobState* except = nullptr);
    void childFailed(JobState& child, std::exception_ptr error);
    void left();  // a child is done

private:
    bool cancelling_ = false;
};

// Starts `coroutine` in `scope` (nothing if it's cancelled): it runs until its first co_await.
template <class F>
std::shared_ptr<JobState> launchIn(const std::shared_ptr<ScopeState>& scope, F&& coroutine, bool deferred = false, bool body = false);

// Resumes coroutines on the UI thread: timers and frame waiters, driven by ComposeHost::frame.
class Dispatcher {
public:
    Composer* composer = nullptr;  // the host's, for coroutines that observe state (snapshotFlow)
    int64_t now() const { return now_; }
    // Input events can be newer than the last frame (after idling): timeouts and delays
    // started by them must count from the event, not from that frame.
    void advanceTo(int64_t time) {
        if (time > now_) now_ = time;
    }
    void schedule(int64_t when, std::coroutine_handle<> h, std::weak_ptr<JobState> job) {
        timers_.push_back({when, seq_++, h, std::move(job), {}});
    }
    // Runs `fire` at `when` unless the job is gone by then (fire decides whether to resume).
    void schedule(int64_t when, std::weak_ptr<JobState> job, std::function<void()> fire) {
        timers_.push_back({when, seq_++, {}, std::move(job), std::move(fire)});
    }
    void atNextFrame(std::coroutine_handle<> h, std::weak_ptr<JobState> job, int64_t* time) {
        frames_.push_back({h, std::move(job), time});
    }
    void start(const std::shared_ptr<JobState>& job) { resume(job, job->handle); }
    // Resumes `h` (suspended inside `job`) right away, e.g. with a pointer event.
    void resumeNow(const std::shared_ptr<JobState>& job, std::coroutine_handle<> h) { resume(job, h); }
    // Resumes due timers, then everything waiting for this frame.
    void run(int64_t now);
    bool hasFrameWaiters() const { return !frames_.empty(); }
    bool hasTimers() const;
    int64_t nextDeadline() const;  // -1: no timers

private:
    void resume(const std::shared_ptr<JobState>& job, std::coroutine_handle<> h);

    struct Timer {
        int64_t when;
        uint64_t seq;
        std::coroutine_handle<> handle;
        std::weak_ptr<JobState> job;
        std::function<void()> fire;
    };
    struct FrameWaiter {
        std::coroutine_handle<> handle;
        std::weak_ptr<JobState> job;
        int64_t* time;
    };
    std::vector<Timer> timers_;
    std::vector<FrameWaiter> frames_;
    int64_t now_ = 0;
    uint64_t seq_ = 0;
};

Dispatcher& currentDispatcher();  // the composition's host dispatcher (composition only)

} // namespace detail

// co_await delay(std::chrono::milliseconds(500));
struct DelayAwaiter {
    int64_t nanos;
    bool await_ready() const noexcept { return nanos <= 0; }
    template <class P>
    void await_suspend(std::coroutine_handle<P> h) const {
        detail::JobState* job = h.promise().job;
        job->dispatcher->schedule(job->dispatcher->now() + nanos, h, job->weak_from_this());
    }
    void await_resume() const noexcept {}
};
inline DelayAwaiter delay(std::chrono::nanoseconds duration) { return {duration.count()}; }

// co_await awaitFrame() suspends until the next frame and returns its time in nanoseconds
// (Compose: withFrameNanos).
struct FrameAwaiter {
    int64_t time = 0;
    bool await_ready() const noexcept { return false; }
    template <class P>
    void await_suspend(std::coroutine_handle<P> h) {
        detail::JobState* job = h.promise().job;
        job->dispatcher->atNextFrame(h, job->weak_from_this(), &time);
    }
    int64_t await_resume() const noexcept { return time; }
};
inline FrameAwaiter awaitFrame() { return {}; }

namespace detail {
// Runs `work` on one of a few worker threads (at once, here, where there are none: the web).
void runInBackground(std::function<void()> work);
int backgroundWorkInFlight();  // started and not done yet (the test rule waits for them)
} // namespace detail

// co_await withBackground([] { return load(); }): `work` runs on a worker thread (Compose's
// withContext(Dispatchers.IO) / Default: files, decoding, heavy sums), the coroutine carrying on
// on the UI thread with what it returned (or what it threw). Cancelled meanwhile, the work still
// runs to its end; what it gives is dropped. It mustn't touch composition state but through
// what is safe from any thread (MutableStateFlow, Channel).
//   LaunchedEffect(path, [path, text]() -> Task { text.set(co_await withBackground([path] { return readFile(path); })); });
template <class T>
struct BackgroundAwaiter {
    struct Result {
        std::optional<T> value;
        std::exception_ptr error;
    };
    std::function<T()> work;
    std::shared_ptr<Result> result = std::make_shared<Result>();
    bool await_ready() const noexcept { return false; }
    template <class P>
    void await_suspend(std::coroutine_handle<P> h) {
        std::weak_ptr<detail::JobState> job = h.promise().job->weak_from_this();
        detail::runInBackground([work = std::move(work), result = result, job, h]() mutable {
            try {
                result->value.emplace(work());
            } catch (...) {
                result->error = std::current_exception();
            }
            runOnUiThread([job, h] {
                if (auto j = job.lock()) j->dispatcher->resumeNow(j, h);  // (not if cancelled meanwhile)
            });
        });
    }
    T await_resume() {
        if (result->error) std::rethrow_exception(result->error);
        return std::move(*result->value);
    }
};
template <>
struct BackgroundAwaiter<void> {
    std::function<void()> work;
    std::shared_ptr<std::exception_ptr> error = std::make_shared<std::exception_ptr>();
    bool await_ready() const noexcept { return false; }
    template <class P>
    void await_suspend(std::coroutine_handle<P> h) {
        std::weak_ptr<detail::JobState> job = h.promise().job->weak_from_this();
        detail::runInBackground([work = std::move(work), error = error, job, h]() mutable {
            try {
                work();
            } catch (...) {
                *error = std::current_exception();
            }
            runOnUiThread([job, h] {
                if (auto j = job.lock()) j->dispatcher->resumeNow(j, h);  // (not if cancelled meanwhile)
            });
        });
    }
    void await_resume() const {
        if (*error) std::rethrow_exception(*error);
    }
};
template <class F>
BackgroundAwaiter<std::invoke_result_t<F&>> withBackground(F work) {
    return {std::function<std::invoke_result_t<F&>()>(std::move(work))};
}

// A launched coroutine (Kotlin's Job): cancel it, ask how it is, or co_await job.join() for it to
// end (completed or cancelled).
class Job {
public:
    Job() = default;  // one that never ran: completed
    explicit Job(std::shared_ptr<detail::JobState> state) : state_(std::move(state)) {}

    void cancel() const {
        if (state_) state_->cancel();
    }
    bool isActive() const { return state_ && !state_->done && !state_->cancelled; }
    bool isCompleted() const { return !state_ || state_->done; }
    bool isCancelled() const { return state_ && state_->cancelled; }

    struct JoinAwaiter {
        std::shared_ptr<detail::JobState> job;
        bool await_ready() const noexcept { return !job || job->done; }
        template <class P>
        void await_suspend(std::coroutine_handle<P> h) const {
            job->joiners.emplace_back(h.promise().job->weak_from_this(), h);
        }
        void await_resume() const noexcept {}
    };
    JoinAwaiter join() const { return {state_}; }

    friend bool operator==(const Job& a, const Job& b) { return a.state_ == b.state_; }

protected:
    std::shared_ptr<detail::JobState> state_;
};

// An async's coroutine and the value it will give (Kotlin's Deferred): co_await deferred.await()
// gives it, or throws what the coroutine threw (CancellationException if it was cancelled).
template <class T>
class Deferred : public Job {
public:
    Deferred(std::shared_ptr<detail::JobState> state, std::shared_ptr<std::optional<T>> value)
        : Job(std::move(state)), value_(std::move(value)) {}

    struct Awaiter : JoinAwaiter {
        std::shared_ptr<std::optional<T>> value;
        T await_resume() const {
            if (this->job && this->job->failure) std::rethrow_exception(this->job->failure);
            if (!value->has_value()) throw CancellationException();
            return **value;
        }
    };
    Awaiter await() const { return {{state_}, value_}; }

private:
    std::shared_ptr<std::optional<T>> value_;
};

class CoroutineScope {
public:
    explicit CoroutineScope(detail::Dispatcher& dispatcher) : state_(std::make_shared<detail::ScopeState>()) {
        state_->dispatcher = &dispatcher;
    }

    // Starts `coroutine` (a callable returning Task) right away; it runs until its first co_await.
    template <class F>
    Job launch(F&& coroutine) const {
        return Job(detail::launchIn(state_, std::forward<F>(coroutine)));
    }
    // Starts `coroutine` (a callable returning TaskOf<T>) the same way, for its value:
    //   auto user = scope.async([id] { return loadUser(id); });
    //   auto posts = scope.async([id] { return loadPosts(id); });
    //   show(co_await user.await(), co_await posts.await());  // the two loading at once
    template <class F>
    auto async(F&& coroutine) const -> Deferred<typename std::invoke_result_t<std::decay_t<F>&>::value_type> {
        using T = typename std::invoke_result_t<std::decay_t<F>&>::value_type;
        auto value = std::make_shared<std::optional<T>>();
        auto job = detail::launchIn(
            state_, [coroutine = std::forward<F>(coroutine), value]() mutable -> Task { value->emplace(co_await coroutine()); }, true);
        return Deferred<T>(std::move(job), std::move(value));
    }

    void cancel() const { state_->cancel(); }
    bool isActive() const { return !state_->cancelled; }
    size_t activeJobs() const { return state_->jobs.size(); }

    friend bool operator==(const CoroutineScope& a, const CoroutineScope& b) { return a.state_ == b.state_; }

private:
    friend struct detail::ScopeAccess;
    explicit CoroutineScope(std::shared_ptr<detail::ScopeState> state) : state_(std::move(state)) {}
    std::shared_ptr<detail::ScopeState> state_;
};

namespace detail {
template <class F>
std::shared_ptr<JobState> launchIn(const std::shared_ptr<ScopeState>& scope, F&& coroutine, bool deferred, bool body) {
    if (scope->cancelled) return nullptr;
    auto job = std::make_shared<JobState>();
    job->dispatcher = scope->dispatcher;
    job->deferred = deferred;
    job->body = body;
    auto callable = std::make_shared<std::decay_t<F>>(std::forward<F>(coroutine));
    job->callable = callable;
    job->handle = (*callable)().release();
    job->handle.promise().job = job.get();
    job->scope = scope;
    scope->jobs.push_back(job);
    scope->dispatcher->start(job);
    return job;
}
} // namespace detail

namespace detail {
struct ScopeAccess {
    static const std::shared_ptr<ScopeState>& state(const CoroutineScope& scope) { return scope.state_; }
    static CoroutineScope make(std::shared_ptr<ScopeState> state) { return CoroutineScope(std::move(state)); }
};
} // namespace detail

// co_await coroutineScope(...), supervisorScope(...), withTimeout(...): `block` runs as a child of a
// scope of its own, with what it launches there; the awaiting coroutine resumes once all of them
// are done, with the block's value or what failed it. Cancelled itself (its frame destroyed), the
// awaiter cancels them all. OrNull: a time running out gives nothing instead of throwing.
template <class T, bool OrNull = false>
struct ScopeAwaiter {
    using Value = std::conditional_t<std::is_void_v<T>, bool, T>;
    using Result = std::conditional_t<OrNull, std::conditional_t<std::is_void_v<T>, bool, std::optional<T>>, T>;
    using Start = std::function<void(const std::shared_ptr<detail::ScopeState>&, const std::shared_ptr<std::optional<Value>>&)>;

    ScopeAwaiter(Start start, int64_t timeout) : start_(std::move(start)), timeout_(timeout) {}
    ScopeAwaiter(const ScopeAwaiter&) = delete;
    ScopeAwaiter& operator=(const ScopeAwaiter&) = delete;
    ~ScopeAwaiter() {
        state_->whenEmpty = nullptr;
        state_->cancel();
    }

    bool await_ready() const noexcept { return false; }
    template <class P>
    bool await_suspend(std::coroutine_handle<P> h) {
        detail::JobState* parent = h.promise().job;
        state_->dispatcher = parent->dispatcher;
        state_->structured = true;
        start_(state_, value_);
        if (state_->jobs.empty()) return false;  // all done already: on at once
        if (timeout_ >= 0) {
            timer_ = std::make_shared<detail::JobState>();  // (the timer goes with the awaiter: done early, nothing left waiting)
            timer_->dispatcher = parent->dispatcher;
            parent->dispatcher->schedule(parent->dispatcher->now() + timeout_, timer_,
                                         [state = std::weak_ptr(state_), timedOut = timedOut_] {
                                             if (auto s = state.lock(); s && !s->jobs.empty()) *timedOut = true, s->cancel();
                                         });
        }
        state_->whenEmpty = [job = parent->weak_from_this(), h] {
            if (auto j = job.lock()) j->dispatcher->resumeNow(j, h);
        };
        return true;
    }
    // The block's value; what failed the scope thrown; TimeoutCancellationException if the time
    // ran out (OrNull: nothing), CancellationException if it was cancelled from inside.
    Result await_resume() {
        if (state_->failure) std::rethrow_exception(state_->failure);
        if (*timedOut_) {
            if constexpr (OrNull) return Result{};
            else throw TimeoutCancellationException();
        }
        if (!value_->has_value()) throw CancellationException();
        if constexpr (OrNull && std::is_void_v<T>) return true;
        else if constexpr (!std::is_void_v<T>) return std::move(**value_);
    }

private:
    Start start_;
    int64_t timeout_;  // ns; -1: none
    std::shared_ptr<detail::ScopeState> state_ = std::make_shared<detail::ScopeState>();
    std::shared_ptr<std::optional<Value>> value_ = std::make_shared<std::optional<Value>>();
    std::shared_ptr<bool> timedOut_ = std::make_shared<bool>(false);
    std::shared_ptr<detail::JobState> timer_;  // what the timeout's timer lives by
};

namespace detail {
template <class R>
struct TaskValue {
    using type = typename R::value_type;
};
template <>
struct TaskValue<Task> {
    using type = void;
};
template <class F>
using ScopeResult = typename TaskValue<std::invoke_result_t<F&, CoroutineScope>>::type;
template <bool OrNull, class F>
ScopeAwaiter<ScopeResult<F>, OrNull> scopeAwaiter(F block, bool supervisor, int64_t timeout) {
    using T = ScopeResult<F>;
    using Value = typename ScopeAwaiter<T, OrNull>::Value;
    return {[block = std::move(block), supervisor](const std::shared_ptr<ScopeState>& state, const std::shared_ptr<std::optional<Value>>& value) {
                state->supervisor = supervisor;
                launchIn(
                    state,
                    [block, scope = ScopeAccess::make(state), value]() mutable -> Task {
                        if constexpr (std::is_void_v<T>) {
                            co_await block(scope);
                            value->emplace(true);
                        } else {
                            value->emplace(co_await block(scope));
                        }
                    },
                    false, true);
            },
            timeout};
}
} // namespace detail

// co_await coroutineScope([](CoroutineScope scope) -> Task { scope.launch(...); ... }) (Kotlin's
// coroutineScope): resumes once the block and everything it launched in `scope` are done, with the
// block's value (if it returns TaskOf<T>). A child failing cancels the others, and the await
// throws what it threw.
template <class F>
auto coroutineScope(F block) {
    return detail::scopeAwaiter<false>(std::move(block), false, -1);
}
// The same, its children failing on their own (Kotlin's supervisorScope): a child's failure is
// reported and cancels nothing; only the block's own fails the scope.
template <class F>
auto supervisorScope(F block) {
    return detail::scopeAwaiter<false>(std::move(block), true, -1);
}
// coroutineScope with a time limit (Kotlin's withTimeout): when it runs out, all of it is
// cancelled and the await throws TimeoutCancellationException.
template <class F>
auto withTimeout(std::chrono::nanoseconds limit, F block) {
    return detail::scopeAwaiter<false>(std::move(block), false, limit.count());
}
// The same, giving nothing when the time runs out (Kotlin's withTimeoutOrNull): an optional of
// the block's value, or for a block giving none whether it completed.
template <class F>
auto withTimeoutOrNull(std::chrono::nanoseconds limit, F block) {
    return detail::scopeAwaiter<true>(std::move(block), false, limit.count());
}

// A scope bound to the composition: its coroutines are cancelled when the caller leaves it.
// Use it to launch from event handlers: scope.launch([=]() -> Task { ... });
inline CoroutineScope rememberCoroutineScope(CallSite site = CallSite::current()) {
    detail::Dispatcher* dispatcher = &detail::currentDispatcher();
    CoroutineScope scope = remember([dispatcher] { return CoroutineScope(*dispatcher); }, site);
    DisposableEffect(0, [scope]() -> std::function<void()> { return [scope] { scope.cancel(); }; }, site);
    return scope;
}

// Runs `coroutine` when entering the composition and again whenever `key` changes,
// cancelling the previous run.
template <class K, class F>
void LaunchedEffect(const K& key, F coroutine, CallSite site = CallSite::current()) {
    detail::Dispatcher* dispatcher = &detail::currentDispatcher();
    DisposableEffect(
        key,
        [dispatcher, coroutine]() -> std::function<void()> {
            CoroutineScope scope(*dispatcher);
            scope.launch(coroutine);
            return [scope] { scope.cancel(); };
        },
        site);
}
// With two or three keys: run again when any of them changes.
template <class K1, class K2, class F>
void LaunchedEffect(const K1& key1, const K2& key2, F coroutine, CallSite site = CallSite::current()) {
    LaunchedEffect(std::tuple<K1, K2>(key1, key2), std::move(coroutine), site);
}
template <class K1, class K2, class K3, class F>
void LaunchedEffect(const K1& key1, const K2& key2, const K3& key3, F coroutine, CallSite site = CallSite::current()) {
    LaunchedEffect(std::tuple<K1, K2, K3>(key1, key2, key3), std::move(coroutine), site);
}

namespace detail {

// co_await currentJob(): the launched coroutine the awaiting frame belongs to (no suspension).
struct CurrentJobAwaiter {
    JobState* job = nullptr;
    bool await_ready() const noexcept { return false; }
    template <class P>
    bool await_suspend(std::coroutine_handle<P> h) noexcept {
        job = h.promise().job;
        return false;  // carry on at once
    }
    JobState* await_resume() const noexcept { return job; }
};
inline CurrentJobAwaiter currentJob() { return {}; }

// Watches the states a block read; a change wakes the coroutine waiting on it.
struct SnapshotObserver {
    std::unique_ptr<Scope> scope;
    Dispatcher* dispatcher = nullptr;
    std::coroutine_handle<> waiting;
    std::weak_ptr<JobState> job;
    bool changed = false;
};
struct SnapshotChangeAwaiter {
    std::shared_ptr<SnapshotObserver> observer;
    bool await_ready() const noexcept { return observer->changed; }
    template <class P>
    void await_suspend(std::coroutine_handle<P> h) {
        observer->waiting = h;
        observer->job = h.promise().job->weak_from_this();
    }
    void await_resume() const noexcept { observer->changed = false; }
};

} // namespace detail

// Compose's snapshotFlow { ... }.collect { ... }: inside a coroutine (a LaunchedEffect, say),
// calls `onValue` with the block's value now and whenever a state it read changes it to a
// different one; it goes on until the coroutine is cancelled.
//   LaunchedEffect(0, [list]() -> Task {
//       co_await snapshotFlow([list] { return list.firstVisibleItemIndex(); }).collect([](int first) { ... });
//   });
template <class F>
class SnapshotFlow {
public:
    explicit SnapshotFlow(F block) : block_(std::move(block)) {}

    template <class C>
    Task collect(C onValue) const {
        using T = std::decay_t<std::invoke_result_t<const F&>>;
        F block = block_;  // the coroutine outlives this object
        detail::JobState* job = co_await detail::currentJob();
        detail::Composer* composer = job->dispatcher->composer;
        auto observer = std::make_shared<detail::SnapshotObserver>();
        observer->dispatcher = job->dispatcher;
        if (composer) {
            observer->scope = std::make_unique<detail::Scope>(*composer, nullptr);
            std::weak_ptr<detail::SnapshotObserver> weak = observer;
            observer->scope->onInvalidated = [weak] {
                auto o = weak.lock();
                if (!o || o->changed) return;
                o->changed = true;
                if (auto h = std::exchange(o->waiting, {})) o->dispatcher->schedule(o->dispatcher->now(), h, o->job);
            };
        }
        auto read = [&] {
            std::optional<T> value;
            if (composer)
                composer->observeReads(*observer->scope, [&] { value.emplace(block()); });
            else
                value.emplace(block());
            return std::move(*value);
        };
        T last = read();
        onValue(last);
        if (!composer) co_return;  // nothing to observe with
        while (true) {
            co_await detail::SnapshotChangeAwaiter{observer};
            T next = read();
            if constexpr (std::equality_comparable<T>)
                if (next == last) continue;
            last = next;
            onValue(last);
        }
    }

private:
    F block_;
};
template <class F>
SnapshotFlow<std::decay_t<F>> snapshotFlow(F&& block) {
    return SnapshotFlow<std::decay_t<F>>(std::forward<F>(block));
}

// A state produced by a coroutine (restarted when `key` changes):
//   int progress = produceState(0, url, [url](MutableState<int> state) -> Task { ... state.set(n); });
template <class T, class K, class F>
T produceState(T initial, const K& key, F producer, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    auto state = remember([&] { return mutableStateOf(std::move(initial)); });
    LaunchedEffect(key, [state, producer]() -> Task { co_await producer(state); });
    return state.get();
}
// With two or three keys: restarted when any of them changes.
template <class T, class K1, class K2, class F>
T produceState(T initial, const K1& key1, const K2& key2, F producer, CallSite site = CallSite::current()) {
    return produceState(std::move(initial), std::tuple<K1, K2>(key1, key2), std::move(producer), site);
}
template <class T, class K1, class K2, class K3, class F>
T produceState(T initial, const K1& key1, const K2& key2, const K3& key3, F producer, CallSite site = CallSite::current()) {
    return produceState(std::move(initial), std::tuple<K1, K2, K3>(key1, key2, key3), std::move(producer), site);
}

} // namespace kompoz
