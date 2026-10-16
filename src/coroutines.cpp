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

void JobState::cancel() {
    if (done || cancelled) return;
    cancelled = true;
    if (running) return;  // cancelled from inside: finish() cleans up once it suspends
    done = true;
    if (handle) std::exchange(handle, {}).destroy();
    callable.reset();
}

void JobState::finish() {
    if (!cancelled && handle && !handle.done()) return;  // suspended again: still running
    if (handle && handle.done() && handle.promise().exception) {
        try {
            std::rethrow_exception(handle.promise().exception);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "kompoz: coroutine failed: %s\n", e.what());
        } catch (...) {
            std::fprintf(stderr, "kompoz: coroutine failed\n");
        }
    }
    done = true;
    if (handle) std::exchange(handle, {}).destroy();
    callable.reset();
    if (auto s = scope.lock()) std::erase(s->jobs, shared_from_this());
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
