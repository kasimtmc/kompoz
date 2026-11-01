#pragma once

// Snapshots, as Compose's snapshot system: every state keeps its value in records, one per snapshot
// that wrote it, and code sees the state as of the snapshot it runs in. Outside any, code runs in the
// global snapshot, whose writes are told to whoever listens (the hosts: their readers recompose)
// when it is applied (Snapshot::sendApplyNotifications, at every frame). A composition runs in a
// snapshot of its own, applied when it is over: it sees one consistent state however other threads
// write meanwhile.
//
//   Snapshot::withMutableSnapshot([&] {  // both written at once: no one sees one without the other
//       first.set(a);
//       second.set(b);
//   });
//
// Threads: on the UI thread (the one the hosts run on) states are read and written freely. Any other
// thread may write them (each write applied at once, or with others in withMutableSnapshot) and reads
// them in a snapshot: Snapshot::takeSnapshot().enter([&] { … }) or withMutableSnapshot.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kompoz {

namespace detail {

struct SnapshotData;

inline constexpr int64_t kInvalidSnapshot = 0;      // a record no one reads (dropped)
inline constexpr int64_t kPreexistingSnapshot = 1;  // a state's first record, seen in every snapshot

// One value of a state, written in the snapshot `snapshotId`.
struct StateRecord {
    std::atomic<int64_t> snapshotId{kPreexistingSnapshot};
    std::atomic<StateRecord*> next{nullptr};
    virtual ~StateRecord() = default;
    virtual StateRecord* copy() const = 0;               // a new record with the same value
    virtual void assign(const StateRecord& from) = 0;   // its value made `from`'s
};

// A state whose value is kept per snapshot (Compose's StateObject): its records, newest first.
class StateObject {
public:
    StateObject() = default;
    StateObject(const StateObject&) = delete;
    StateObject& operator=(const StateObject&) = delete;
    virtual ~StateObject();  // its records freed, and it leaves the snapshots that wrote it
    // Whether `applied`, written in a snapshot being applied, may stand where `current` was written
    // meanwhile by another (Compose's mergeRecords: its policy's equivalent); otherwise the apply fails.
    virtual bool mergeable(const StateRecord& current, const StateRecord& applied) const { return false; }

    std::atomic<StateRecord*> first{nullptr};
};

// The record of `object` the snapshot this thread runs in sees (told to its read observer).
const StateRecord& readable(const StateObject& object, bool observe = true);

// Writing `object` in the snapshot this thread runs in: own() is the record to write in place if it
// has one of its own, else add() one; the write is recorded (and told) when it ends. Nothing written
// (cancel()): nothing recorded.
class StateWriter {
public:
    explicit StateWriter(StateObject& object);
    ~StateWriter();
    StateWriter(const StateWriter&) = delete;
    StateWriter& operator=(const StateWriter&) = delete;

    const StateRecord& current() const { return *current_; }  // what the snapshot sees now
    StateRecord* own() const { return own_; }
    void add(StateRecord* fresh);
    void cancel() { written_ = false; }

private:
    std::unique_lock<std::recursive_mutex> lock_;
    StateObject& object_;
    SnapshotData* snapshot_;
    const StateRecord* current_;
    StateRecord* own_ = nullptr;
    bool written_ = true, freshId_ = false;
};

// A plain object that isn't kept per snapshot (an animation's, a lazy list's) changed: its readers
// are told as the snapshot it changed in is applied.
void stateChanged(const void* key);

// The UI thread: the one the hosts run on (the first host's).
void setUiThread();
// Whether the global snapshot has writes not applied yet (a frame is wanted to tell them).
bool globalSnapshotChanged();
// Called (outside the lock, on the writing thread) at the first write to the global snapshot after
// it was applied: the hosts ask for a frame.
void setGlobalWriteObserver(void (*observer)());

} // namespace detail

// What applying a mutable snapshot gave: Failure if a state it wrote was written meanwhile by
// another snapshot to a value its policy doesn't count as the same.
enum class SnapshotApplyResult { Success, Failure };

// withMutableSnapshot's apply failed (see SnapshotApplyResult).
struct SnapshotApplyConflictException : std::runtime_error {
    SnapshotApplyConflictException() : std::runtime_error("a state written in the snapshot was written meanwhile elsewhere") {}
};

using ReadObserver = std::function<void(const void* state)>;
using WriteObserver = std::function<void(const void* state)>;
// Told the states a snapshot changed as it is applied (the global one at sendApplyNotifications),
// on the thread applying it.
using ApplyObserver = std::function<void(const std::vector<const void*>& changed)>;

class MutableSnapshot;

// A view of the states as they were when it was taken (Compose's Snapshot): what runs in it with
// enter() reads them so, whatever is written meanwhile; dispose() it when done.
class Snapshot {
public:
    Snapshot() = default;
    explicit Snapshot(std::shared_ptr<detail::SnapshotData> data) : data_(std::move(data)) {}

    // A read-only snapshot of what this thread sees now (inside a snapshot: of that one).
    static Snapshot takeSnapshot(ReadObserver readObserver = {});
    // A snapshot to write in too; its writes are seen by others once it is applied.
    static MutableSnapshot takeMutableSnapshot(ReadObserver readObserver = {}, WriteObserver writeObserver = {});
    // Runs `block` in a new mutable snapshot and applies it: its writes are seen together, or not
    // at all (SnapshotApplyConflictException).
    template <class F>
    static auto withMutableSnapshot(F&& block) -> std::invoke_result_t<F&>;
    // Applies the global snapshot: what was written outside any snapshot is told to the apply
    // observers. The hosts do it at every frame.
    static void sendApplyNotifications();
    // Told what every applied snapshot changed, until the returned handle's dispose().
    struct ObserverHandle {
        std::shared_ptr<void> registration;
        void dispose() { registration.reset(); }
    };
    static ObserverHandle registerApplyObserver(ApplyObserver observer);

    // Runs `block` in this snapshot (inside it, states read and written as it sees them).
    template <class F>
    auto enter(F&& block) const -> std::invoke_result_t<F&>;
    void dispose() const;
    bool readOnly() const;
    explicit operator bool() const { return data_ != nullptr; }

    // In this snapshot while it lives (what enter() does around its block).
    class Entered {
    public:
        explicit Entered(const Snapshot& snapshot);
        ~Entered();
        Entered(const Entered&) = delete;
        Entered& operator=(const Entered&) = delete;

    private:
        detail::SnapshotData* previous_;
    };

protected:
    std::shared_ptr<detail::SnapshotData> data_;
};

class MutableSnapshot : public Snapshot {
public:
    using Snapshot::Snapshot;
    // Makes its writes seen by others (the global snapshot's, or its parent's if it was taken in
    // another mutable snapshot), unless one conflicts; it can't be applied twice.
    SnapshotApplyResult apply() const;
};

template <class F>
auto Snapshot::enter(F&& block) const -> std::invoke_result_t<F&> {
    Entered entered(*this);
    return block();
}

template <class F>
auto Snapshot::withMutableSnapshot(F&& block) -> std::invoke_result_t<F&> {
    MutableSnapshot snapshot = takeMutableSnapshot();
    struct Disposed {
        const MutableSnapshot& snapshot;
        ~Disposed() { snapshot.dispose(); }
    } disposed{snapshot};
    if constexpr (std::is_void_v<std::invoke_result_t<F&>>) {
        snapshot.enter(block);
        if (snapshot.apply() != SnapshotApplyResult::Success) throw SnapshotApplyConflictException();
    } else {
        auto result = snapshot.enter(block);
        if (snapshot.apply() != SnapshotApplyResult::Success) throw SnapshotApplyConflictException();
        return result;
    }
}

} // namespace kompoz
