#pragma once

// Channel: a queue between coroutines (Kotlin's kotlinx.coroutines.channels.Channel). One side
// sends, the other receives, each element once: one-off UI events from a ViewModel, work handed
// to a consumer, a producer and its consumer kept in step.
//
//   Channel<std::string> events(Channel<std::string>::Buffered);
//   void saved() { events.trySend("Kaydedildi"); }      // from anywhere, any thread
//   LaunchedEffect(0, [&]() -> Task {
//       co_await events.consumeEach([&](const std::string& e) { snackbar.showSnackbar(e); });
//   });
//
// `co_await send(value)` waits while the buffer is full (a rendezvous channel, the default, until
// a receiver takes the value); `co_await receive()` until there is an element. Coroutines resume
// on the UI thread. Unlike Kotlin nothing throws: send says whether it went through, receive
// gives nullopt once the channel is closed and empty (Kotlin's receiveCatching).
//
// A coroutine cancelled while it waits leaves the channel as it was; one cancelled after an
// element was handed to it but before it resumed loses that element (as in Kotlin, without
// onUndeliveredElement).

#include "flow.hpp"

#include <climits>
#include <deque>
#include <mutex>
#include <optional>

namespace kompoz {

// What a full channel does with one more element (Kotlin's BufferOverflow).
enum class BufferOverflow {
    Suspend,     // send waits for room, trySend fails
    DropOldest,  // the oldest buffered element makes room
    DropLatest   // the new element is dropped (send and trySend still succeed)
};

namespace detail {

template <class T>
struct ChannelCell {
    ChannelCell(int c, BufferOverflow o)
        : capacity(c < 0 ? 1 : c == 0 && o != BufferOverflow::Suspend ? 1 : c),  // conflated: one, dropping the oldest
          overflow(c < 0 ? BufferOverflow::DropOldest : o) {}
    std::mutex mutex;  // guards everything below
    int capacity;
    BufferOverflow overflow;
    std::deque<T> buffer;
    bool closed = false;
    struct Receiver {
        std::coroutine_handle<> handle;
        std::weak_ptr<JobState> job;
        std::optional<T> value;  // handed to it (none: the channel closed)
    };
    struct Sender {
        std::coroutine_handle<> handle;
        std::weak_ptr<JobState> job;
        T value;
        bool delivered = false;  // taken (or buffered); false: the channel was cancelled
    };
    std::deque<std::shared_ptr<Receiver>> receivers;  // waiting (the buffer is empty then)
    std::deque<std::shared_ptr<Sender>> senders;      // waiting (the buffer is full then)

    using Wakeups = std::vector<std::pair<std::coroutine_handle<>, std::weak_ptr<JobState>>>;
    static bool alive(const std::weak_ptr<JobState>& weak) {
        auto job = weak.lock();
        return job && !job->cancelled && !job->done;
    }
    // Outside the lock: the coroutines whose wait is over resume on the UI thread.
    static void wake(Wakeups wakeups) {
        if (wakeups.empty()) return;
        runOnUiThread([wakeups = std::move(wakeups)] {
            for (const auto& [h, weak] : wakeups)
                if (auto job = weak.lock(); job && !job->cancelled && !job->done) job->dispatcher->schedule(job->dispatcher->now(), h, weak);
        });
    }

    enum class Offer { Delivered, Buffered, Full, Closed };
    // Under the lock: `value` to a waiting receiver, or into the buffer (as the overflow says).
    Offer offer(T& value, Wakeups& wakeups) {
        if (closed) return Offer::Closed;
        while (!receivers.empty()) {
            auto r = receivers.front();
            receivers.pop_front();
            if (!alive(r->job)) continue;
            r->value = std::move(value);
            wakeups.push_back({r->handle, r->job});
            return Offer::Delivered;
        }
        if (capacity == INT_MAX || int(buffer.size()) < capacity) {
            buffer.push_back(std::move(value));
            return Offer::Buffered;
        }
        switch (overflow) {
        case BufferOverflow::DropOldest:
            if (!buffer.empty()) buffer.pop_front();
            buffer.push_back(std::move(value));
            return Offer::Buffered;
        case BufferOverflow::DropLatest: return Offer::Buffered;
        case BufferOverflow::Suspend: break;
        }
        return Offer::Full;
    }
    // Under the lock: the next element, from the buffer (a waiting sender's moving up behind
    // it) or straight from a waiting sender.
    std::optional<T> take(Wakeups& wakeups) {
        auto nextSender = [&]() -> std::shared_ptr<Sender> {
            while (!senders.empty()) {
                auto s = senders.front();
                senders.pop_front();
                if (alive(s->job)) return s;
            }
            return nullptr;
        };
        if (!buffer.empty()) {
            std::optional<T> value(std::move(buffer.front()));
            buffer.pop_front();
            if (auto s = nextSender()) {
                buffer.push_back(std::move(s->value));
                s->delivered = true;
                wakeups.push_back({s->handle, s->job});
            }
            return value;
        }
        if (auto s = nextSender()) {
            s->delivered = true;
            wakeups.push_back({s->handle, s->job});
            return std::move(s->value);
        }
        return std::nullopt;
    }
};

template <class T>
struct ChannelSendAwaiter {
    std::shared_ptr<ChannelCell<T>> cell;
    T value;
    bool result = false;
    std::shared_ptr<typename ChannelCell<T>::Sender> waiting;
    bool await_ready() const noexcept { return false; }
    template <class P>
    bool await_suspend(std::coroutine_handle<P> h) {
        using Cell = ChannelCell<T>;
        typename Cell::Wakeups wakeups;
        bool wait;
        {
            std::lock_guard lock(cell->mutex);
            typename Cell::Offer offer = cell->offer(value, wakeups);
            wait = offer == Cell::Offer::Full;
            if (wait) {
                waiting = std::make_shared<typename Cell::Sender>(
                    typename Cell::Sender{h, h.promise().job->weak_from_this(), std::move(value)});
                cell->senders.push_back(waiting);
            }
            result = offer != Cell::Offer::Closed;
        }
        Cell::wake(std::move(wakeups));
        return wait;
    }
    bool await_resume() {
        if (!waiting) return result;
        std::lock_guard lock(cell->mutex);
        return waiting->delivered;
    }
    ~ChannelSendAwaiter() {  // destroyed while waiting (its coroutine cancelled): out of the queue
        if (!waiting) return;
        std::lock_guard lock(cell->mutex);
        std::erase(cell->senders, waiting);
    }
};

template <class T>
struct ChannelReceiveAwaiter {
    std::shared_ptr<ChannelCell<T>> cell;
    std::optional<T> result;
    std::shared_ptr<typename ChannelCell<T>::Receiver> waiting;
    bool await_ready() const noexcept { return false; }
    template <class P>
    bool await_suspend(std::coroutine_handle<P> h) {
        using Cell = ChannelCell<T>;
        typename Cell::Wakeups wakeups;
        bool wait = false;
        {
            std::lock_guard lock(cell->mutex);
            result = cell->take(wakeups);
            if (!result && !cell->closed) {
                waiting = std::make_shared<typename Cell::Receiver>(typename Cell::Receiver{h, h.promise().job->weak_from_this(), {}});
                cell->receivers.push_back(waiting);
                wait = true;
            }
        }
        Cell::wake(std::move(wakeups));
        return wait;
    }
    std::optional<T> await_resume() {
        if (!waiting) return std::move(result);
        std::lock_guard lock(cell->mutex);
        return std::move(waiting->value);
    }
    ~ChannelReceiveAwaiter() {
        if (!waiting) return;
        std::lock_guard lock(cell->mutex);
        std::erase(cell->receivers, waiting);
    }
};

} // namespace detail

// Copies are the same channel; any thread may use it (coroutines resume on the UI thread).
template <class T>
class Channel {
public:
    using value_type = T;
    // Capacities (Kotlin's Channel.RENDEZVOUS, BUFFERED, CONFLATED, UNLIMITED).
    static constexpr int Rendezvous = 0;  // no buffer: a send waits for its receiver
    static constexpr int Buffered = 64;
    static constexpr int Conflated = -1;  // only the latest element is kept
    static constexpr int Unlimited = INT_MAX;

    explicit Channel(int capacity = Rendezvous, BufferOverflow onBufferOverflow = BufferOverflow::Suspend)
        : cell_(std::make_shared<detail::ChannelCell<T>>(capacity, onBufferOverflow)) {}

    // co_await channel.send(value) inside a coroutine: true once it is received or buffered
    // (waiting while full), false if the channel is closed (or cancelled while it waited).
    detail::ChannelSendAwaiter<T> send(T value) const { return {cell_, std::move(value)}; }
    // Without waiting, from any thread: false if it is full (with BufferOverflow::Suspend) or closed.
    bool trySend(T value) const {
        typename detail::ChannelCell<T>::Wakeups wakeups;
        typename detail::ChannelCell<T>::Offer offer;
        {
            std::lock_guard lock(cell_->mutex);
            offer = cell_->offer(value, wakeups);
        }
        detail::ChannelCell<T>::wake(std::move(wakeups));
        using Offer = typename detail::ChannelCell<T>::Offer;
        return offer == Offer::Delivered || offer == Offer::Buffered;
    }
    // co_await channel.receive() inside a coroutine: the next element (waiting for one), or
    // nullopt once the channel is closed and empty.
    detail::ChannelReceiveAwaiter<T> receive() const { return {cell_}; }
    std::optional<T> tryReceive() const {
        typename detail::ChannelCell<T>::Wakeups wakeups;
        std::optional<T> value;
        {
            std::lock_guard lock(cell_->mutex);
            value = cell_->take(wakeups);
        }
        detail::ChannelCell<T>::wake(std::move(wakeups));
        return value;
    }

    // No more sends; what was sent can still be received, then receivers get nullopt. False if
    // it was closed already.
    bool close() const {
        typename detail::ChannelCell<T>::Wakeups wakeups;
        {
            std::lock_guard lock(cell_->mutex);
            if (cell_->closed) return false;
            cell_->closed = true;
            for (auto& r : cell_->receivers) wakeups.push_back({r->handle, r->job});  // (waiting: nothing is left)
            cell_->receivers.clear();
        }
        detail::ChannelCell<T>::wake(std::move(wakeups));
        return true;
    }
    // Closed, and what was buffered (or waited to be sent) dropped: waiting senders get false.
    void cancel() const {
        typename detail::ChannelCell<T>::Wakeups wakeups;
        {
            std::lock_guard lock(cell_->mutex);
            cell_->closed = true;
            cell_->buffer.clear();
            for (auto& s : cell_->senders) wakeups.push_back({s->handle, s->job});
            for (auto& r : cell_->receivers) wakeups.push_back({r->handle, r->job});
            cell_->senders.clear();
            cell_->receivers.clear();
        }
        detail::ChannelCell<T>::wake(std::move(wakeups));
    }
    bool isClosedForSend() const {
        std::lock_guard lock(cell_->mutex);
        return cell_->closed;
    }
    // Closed, and nothing left to receive.
    bool isClosedForReceive() const {
        std::lock_guard lock(cell_->mutex);
        return cell_->closed && cell_->buffer.empty() && cell_->senders.empty();
    }
    bool isEmpty() const {
        std::lock_guard lock(cell_->mutex);
        return cell_->buffer.empty() && cell_->senders.empty();
    }

    // Inside a coroutine: `onElement` (returning void, or a Task to suspend in) with each element
    // until the channel is closed and empty; the channel is cancelled when it stops (done or
    // cancelled), as Kotlin's consumeEach.
    template <class C>
    Task consumeEach(C onElement) const {
        return consumeAsFlow().collect(std::move(onElement));
    }
    // A flow of its elements, each one to one of its collectors; it ends when the channel is
    // closed and empty.
    Flow<T> receiveAsFlow() const {
        return Flow<T>([cell = cell_](FlowCollector<T> emit) -> Task {
            while (true) {
                std::optional<T> value = co_await detail::ChannelReceiveAwaiter<T>{cell};
                if (!value) co_return;
                co_await emit(std::move(*value));
            }
        });
    }
    // receiveAsFlow for one collector: the channel is cancelled when the collection stops.
    Flow<T> consumeAsFlow() const {
        return Flow<T>([channel = *this](FlowCollector<T> emit) -> Task {
            struct Consume {
                Channel channel;
                ~Consume() { channel.cancel(); }
            } consume{channel};
            co_await channel.receiveAsFlow().collect(emit);
        });
    }

    friend bool operator==(const Channel& a, const Channel& b) { return a.cell_ == b.cell_; }

private:
    std::shared_ptr<detail::ChannelCell<T>> cell_;
};

// A channel that `block` (a coroutine taking the channel) sends to, launched in `scope`
// (Kotlin's produce): closed when the block is done, cancelled if it is cancelled first.
//   Channel<int> numbers = produce<int>(scope, [](Channel<int> out) -> Task {
//       for (int i = 1; i <= 3; ++i) co_await out.send(i);
//   });
template <class T, class F>
Channel<T> produce(const CoroutineScope& scope, F block, int capacity = Channel<T>::Rendezvous) {
    Channel<T> channel(capacity);
    scope.launch([channel, block]() mutable -> Task {
        struct Finish {
            Channel<T> channel;
            bool done = false;
            ~Finish() {
                if (done) channel.close();
                else channel.cancel();
            }
        } finish{channel};
        co_await block(channel);
        finish.done = true;
    });
    return channel;
}

} // namespace kompoz
