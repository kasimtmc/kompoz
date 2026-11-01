#include <kompoz/coroutines.hpp>
#include <kompoz/host.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace kompoz::detail {

Dispatcher& currentDispatcher() { return Composer::require().host().dispatcher(); }

namespace {
// What a coroutine threw that nobody takes (a cancellation isn't a failure).
void report(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch (const CancellationException&) {
    } catch (const std::exception& e) {
        std::fprintf(stderr, "kompoz: coroutine failed: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "kompoz: coroutine failed\n");
    }
}
} // namespace

void JobState::cancel() {
    if (done || cancelled) return;
    cancelled = true;
    if (running) return;  // cancelled from inside: finish() cleans up once it suspends
    complete();
}

void JobState::finish() {
    if (!cancelled && handle && !handle.done()) return;  // suspended again: still running
    if (handle && handle.done() && handle.promise().exception) {
        failure = handle.promise().exception;
        auto s = scope.lock();
        if (s && s->structured)
            s->childFailed(*this, failure);
        else if (!deferred)  // (an async's goes to who awaits it)
            report(failure);
    }
    complete();
}

void JobState::complete() {
    if (done) return;
    done = true;
    auto self = shared_from_this();  // (kept while what it ends lets go of it)
    if (handle) std::exchange(handle, {}).destroy();  // (awaiters in it go: a coroutineScope's children are cancelled)
    callable.reset();
    for (auto& [job, h] : std::exchange(joiners, {}))
        if (auto j = job.lock()) dispatcher->resumeNow(j, h);
    if (auto s = scope.lock()) {
        std::erase(s->jobs, self);
        s->left();
    }
}

void ScopeState::cancel(const JobState* except) {
    cancelled = true;
    cancelling_ = true;  // (who awaits it resumes once all of them are gone, not after the first)
    auto all = jobs;
    for (auto& job : all)
        if (job.get() != except) job->cancel();
    cancelling_ = false;
    left();
}

void ScopeState::childFailed(JobState& child, std::exception_ptr error) {
    bool cancellation = false;
    try {
        std::rethrow_exception(error);
    } catch (const CancellationException&) {
        cancellation = true;
    } catch (...) {
    }
    if (cancellation) return;
    if (supervisor && !child.body) {  // a supervisor's children fail on their own
        report(error);
        return;
    }
    if (!failure) failure = error;
    cancel(&child);
}

void ScopeState::left() {
    if (!cancelling_ && jobs.empty() && whenEmpty) std::exchange(whenEmpty, nullptr)();
}

void Dispatcher::resume(const std::shared_ptr<JobState>& job, std::coroutine_handle<> h) {
    if (!job || job->cancelled || job->done) return;
    job->running = true;
    h.resume();
    job->running = false;
    job->finish();
}

void Dispatcher::run(int64_t now) {
    // Never back: what was scheduled at now() (resumed from another thread, at an input event
    // stamped after this frame's time) is due.
    now_ = std::max(now_, now);
    std::vector<Timer> due;
    std::erase_if(timers_, [&](Timer& t) {
        if (t.job.expired()) return true;
        if (t.when > now_) return false;
        due.push_back(std::move(t));
        return true;
    });
    std::sort(due.begin(), due.end(),
              [](const Timer& a, const Timer& b) { return a.when != b.when ? a.when < b.when : a.seq < b.seq; });
    for (Timer& t : due) {
        auto job = t.job.lock();
        if (!job || job->cancelled || job->done) continue;
        if (t.fire)
            t.fire();
        else
            resume(job, t.handle);
    }

    auto waiters = std::move(frames_);
    frames_.clear();
    for (FrameWaiter& w : waiters) {
        auto job = w.job.lock();
        if (!job || job->cancelled || job->done) continue;
        *w.time = now;
        resume(job, w.handle);
    }
}

bool Dispatcher::hasTimers() const {
    return std::any_of(timers_.begin(), timers_.end(), [](const Timer& t) {
        auto job = t.job.lock();
        return job && !job->cancelled && !job->done;
    });
}

int64_t Dispatcher::nextDeadline() const {
    int64_t next = -1;
    for (const Timer& t : timers_) {
        auto job = t.job.lock();
        if (!job || job->cancelled || job->done) continue;
        if (next < 0 || t.when < next) next = t.when;
    }
    return next;
}

namespace {
std::atomic<int> gInFlight{0};

// A few workers taking work in turn; never torn down (they may still be busy as the app ends).
struct WorkerPool {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::function<void()>> queue;
    WorkerPool() {
        unsigned n = std::clamp(std::thread::hardware_concurrency() / 2, 2u, 4u);
        for (unsigned i = 0; i < n; ++i)
            std::thread([this] {
                while (true) {
                    std::function<void()> work;
                    {
                        std::unique_lock lock(mutex);
                        ready.wait(lock, [this] { return !queue.empty(); });
                        work = std::move(queue.front());
                        queue.pop_front();
                    }
                    work();
                    --gInFlight;  // after it posted its result (the UI thread's tasks take over)
                }
            }).detach();
    }
};
} // namespace

void runInBackground(std::function<void()> work) {
    ++gInFlight;
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    work();  // no threads: here and now
    --gInFlight;
#else
    static WorkerPool* pool = new WorkerPool();
    {
        std::lock_guard lock(pool->mutex);
        pool->queue.push_back(std::move(work));
    }
    pool->ready.notify_one();
#endif
}

int backgroundWorkInFlight() { return gInFlight.load(); }

} // namespace kompoz::detail
