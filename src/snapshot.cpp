#include <kompoz/snapshot.hpp>

#include <algorithm>
#include <limits>
#include <thread>
#include <unordered_set>

namespace kompoz::detail {

namespace {

// Snapshot ids, as sorted ranges that don't touch: those a snapshot doesn't see.
struct IdSet {
    std::vector<std::pair<int64_t, int64_t>> ranges;  // [first, last]

    bool contains(int64_t id) const {
        auto it = std::upper_bound(ranges.begin(), ranges.end(), id, [](int64_t v, const auto& r) { return v < r.first; });
        return it != ranges.begin() && std::prev(it)->second >= id;
    }
    void add(int64_t first, int64_t last) {
        if (first > last) return;
        std::vector<std::pair<int64_t, int64_t>> out;
        bool placed = false;
        for (const auto& r : ranges) {
            if (r.second + 1 < first) {
                out.push_back(r);
            } else if (last + 1 < r.first) {
                if (!placed) out.emplace_back(first, last), placed = true;
                out.push_back(r);
            } else {
                first = std::min(first, r.first), last = std::max(last, r.second);
            }
        }
        if (!placed) out.emplace_back(first, last);
        ranges = std::move(out);
    }
    void add(int64_t id) { add(id, id); }
};

// The global snapshot as the UI thread reads it without the lock: replaced (never changed) under
// it, the old one freed by the UI thread between frames.
struct GlobalView {
    int64_t id;
    IdSet invalid;
};

} // namespace

struct SnapshotData : std::enable_shared_from_this<SnapshotData> {
    int64_t id = 0;
    IdSet invalid;
    bool readOnly = false, applied = false, disposed = false, open = false;
    std::shared_ptr<SnapshotData> parent;  // a nested mutable snapshot's: what it applies to
    std::vector<StateObject*> modified;
    std::unordered_set<StateObject*> modifiedSet;
    std::vector<const void*> changed;  // plain objects (stateChanged)
    ReadObserver read;
    WriteObserver write;

    void wrote(StateObject* object) {
        if (modifiedSet.insert(object).second) modified.push_back(object);
    }
    ~SnapshotData();  // dropped without dispose(): disposed
};

namespace {

// What is kept for all snapshots, never destroyed: states living in statics of their own may be
// destroyed after it would be.
struct Globals {
    std::recursive_mutex lock;
    std::vector<SnapshotData*> open;  // snapshots with ids of their own, not applied or disposed
    std::vector<const GlobalView*> retiredViews;
    // The global snapshot's writes since it was last applied.
    std::vector<StateObject*> globalModified;
    std::unordered_set<StateObject*> globalModifiedSet;
    std::vector<const void*> globalChanged;
    std::vector<std::weak_ptr<ApplyObserver>> applyObservers;
    // What may hold records no one reads any more, and the records taken out of a list (freed once
    // no snapshot open when they were taken out is left: one may have been going through them).
    std::unordered_set<StateObject*> toPrune;
    std::vector<std::pair<StateRecord*, int64_t>> retired;
};
Globals& globals() {
    static Globals* g = new Globals();
    return *g;
}

constinit int64_t gNextId = kPreexistingSnapshot + 2;
constinit std::atomic<const GlobalView*> gView{nullptr};  // nullptr: firstView()
constinit std::atomic<bool> gGlobalDirty{false};
constinit void (*gGlobalWriteObserver)() = nullptr;
constinit std::atomic<bool> gUiThreadSet{false};
std::thread::id gUiThread;
constinit thread_local SnapshotData* tCurrent = nullptr;  // nullptr: the global snapshot

// The global snapshot's first view, before any was replaced (made when first asked for, so a state
// used while statics are made finds it).
const GlobalView& firstView() {
    static const GlobalView* first = new GlobalView{kPreexistingSnapshot + 1, {}};  // (never destroyed)
    return *first;
}
const GlobalView* loadView(std::memory_order order) {
    const GlobalView* view = gView.load(order);
    return view ? view : &firstView();
}

bool onUiThread() { return !gUiThreadSet.load(std::memory_order_acquire) || std::this_thread::get_id() == gUiThread; }

// The newest record of `object` written at `id` or before and not by a snapshot in `invalid`.
StateRecord* find(const StateObject& object, int64_t id, const IdSet& invalid) {
    StateRecord* best = nullptr;
    int64_t bestId = 0;
    for (StateRecord* r = object.first.load(std::memory_order_acquire); r; r = r->next.load(std::memory_order_acquire)) {
        int64_t rid = r->snapshotId.load(std::memory_order_acquire);
        if (rid != kInvalidSnapshot && rid <= id && rid > bestId && !invalid.contains(rid)) best = r, bestId = rid;
    }
    return best;
}

IdSet openIds() {
    IdSet ids;
    for (const SnapshotData* s : detail::globals().open) ids.add(s->id);
    return ids;
}

// A new global view (under the lock): its invalid set the open snapshots', at `id`.
void replaceView(int64_t id) {
    const GlobalView* old = loadView(std::memory_order_relaxed);
    gView.store(new GlobalView{id, openIds()}, std::memory_order_release);
    detail::globals().retiredViews.push_back(old);
}

void prepend(StateObject& object, StateRecord* record, int64_t id) {
    record->snapshotId.store(id, std::memory_order_relaxed);
    record->next.store(object.first.load(std::memory_order_relaxed), std::memory_order_relaxed);
    object.first.store(record, std::memory_order_release);
}

void close(SnapshotData& s) {
    std::erase(detail::globals().open, &s);
    s.open = false;
}

// Takes out the records of `object` no snapshot reads (under the lock, on the UI thread): what the
// global snapshot reads is what every snapshot taken from now on will, and the open ones (an open
// mutable one's own records among them) keep what they read.
void prune(StateObject& object) {
    const GlobalView* view = loadView(std::memory_order_relaxed);
    std::unordered_set<const StateRecord*> read{find(object, view->id, view->invalid)};
    for (const SnapshotData* s : detail::globals().open) read.insert(find(object, s->id, s->invalid));
    StateRecord* previous = nullptr;
    for (StateRecord* r = object.first.load(std::memory_order_relaxed); r;) {
        StateRecord* next = r->next.load(std::memory_order_relaxed);
        if (read.contains(r)) {
            previous = r;
        } else {
            (previous ? previous->next : object.first).store(next, std::memory_order_release);
            detail::globals().retired.emplace_back(r, gNextId);
        }
        r = next;
    }
}

// Between frames, on the UI thread (under the lock): what no one can be reading any more freed.
void collect() {
    for (StateObject* object : detail::globals().toPrune) prune(*object);
    detail::globals().toPrune.clear();
    int64_t oldestOpen = std::numeric_limits<int64_t>::max();
    for (const SnapshotData* s : detail::globals().open) oldestOpen = std::min(oldestOpen, s->id);
    std::erase_if(detail::globals().retired, [&](const auto& retired) {
        if (retired.second > oldestOpen) return false;
        delete retired.first;
        return true;
    });
    for (const GlobalView* v : detail::globals().retiredViews)
        if (v != &firstView()) delete v;
    detail::globals().retiredViews.clear();
}

void notify(const std::vector<const void*>& changed) {
    if (changed.empty()) return;
    std::vector<std::shared_ptr<ApplyObserver>> observers;
    {
        std::lock_guard lock(detail::globals().lock);
        std::erase_if(detail::globals().applyObservers, [&](const auto& weak) {
            auto o = weak.lock();
            if (!o) return true;
            observers.push_back(std::move(o));
            return false;
        });
    }
    for (const auto& o : observers) (*o)(changed);
}

} // namespace

StateObject::~StateObject() {
    std::lock_guard lock(detail::globals().lock);
    for (SnapshotData* s : detail::globals().open)
        if (s->modifiedSet.erase(this)) std::erase(s->modified, this);
    if (detail::globals().globalModifiedSet.erase(this)) std::erase(detail::globals().globalModified, this);
    detail::globals().toPrune.erase(this);
    for (StateRecord* r = first.load(std::memory_order_relaxed); r;) {
        StateRecord* next = r->next.load(std::memory_order_relaxed);
        delete r;
        r = next;
    }
}

const StateRecord& readable(const StateObject& object, bool observe) {
    if (SnapshotData* s = tCurrent) {
        if (observe && s->read) s->read(&object);
        return *find(object, s->id, s->invalid);
    }
    if (!onUiThread()) {  // (a read meant for a snapshot: at least not torn by a write)
        std::lock_guard lock(detail::globals().lock);
        const GlobalView* view = loadView(std::memory_order_relaxed);
        return *find(object, view->id, view->invalid);
    }
    const GlobalView* view = loadView(std::memory_order_acquire);
    return *find(object, view->id, view->invalid);
}

StateWriter::StateWriter(StateObject& object) : lock_(detail::globals().lock), object_(object), snapshot_(tCurrent) {
    if (snapshot_) {
        if (snapshot_->readOnly) throw std::logic_error("kompoz: a state written in a read-only snapshot");
        current_ = find(object, snapshot_->id, snapshot_->invalid);
        if (current_->snapshotId.load(std::memory_order_relaxed) == snapshot_->id) own_ = const_cast<StateRecord*>(current_);
        return;
    }
    const GlobalView* view = loadView(std::memory_order_relaxed);
    current_ = find(object, view->id, view->invalid);
    if (!onUiThread())
        freshId_ = true;  // never in place: the UI thread may be reading it
    else if (current_->snapshotId.load(std::memory_order_relaxed) == view->id)
        own_ = const_cast<StateRecord*>(current_);
}

void StateWriter::add(StateRecord* fresh) {
    if (snapshot_) {
        prepend(object_, fresh, snapshot_->id);
    } else if (freshId_) {
        // An id of its own below the global snapshot's next one: seen by it (and by what is taken
        // after), not by the snapshots open now.
        prepend(object_, fresh, gNextId++);
        replaceView(gNextId++);
    } else {
        prepend(object_, fresh, loadView(std::memory_order_relaxed)->id);
    }
    own_ = fresh;
}

StateWriter::~StateWriter() {
    if (!written_) return;
    WriteObserver observer;
    bool first = false;
    if (snapshot_) {
        snapshot_->wrote(&object_);
        observer = snapshot_->write;
    } else {
        if (detail::globals().globalModifiedSet.insert(&object_).second) detail::globals().globalModified.push_back(&object_);
        first = !gGlobalDirty.exchange(true);
    }
    lock_.unlock();
    if (observer) observer(&object_);
    if (first && gGlobalWriteObserver) gGlobalWriteObserver();
}

void stateChanged(const void* key) {
    bool first = false;
    {
        std::lock_guard lock(detail::globals().lock);
        if (SnapshotData* s = tCurrent; s && !s->readOnly) {
            s->changed.push_back(key);
        } else {
            detail::globals().globalChanged.push_back(key);
            first = !gGlobalDirty.exchange(true);
        }
    }
    if (first && gGlobalWriteObserver) gGlobalWriteObserver();
}

void setUiThread() {
    if (gUiThreadSet.load(std::memory_order_acquire)) return;
    std::lock_guard lock(detail::globals().lock);
    gUiThread = std::this_thread::get_id();
    gUiThreadSet.store(true, std::memory_order_release);
}

bool globalSnapshotChanged() { return gGlobalDirty.load(std::memory_order_acquire); }

void setGlobalWriteObserver(void (*observer)()) {
    std::lock_guard lock(detail::globals().lock);
    gGlobalWriteObserver = observer;
}

} // namespace kompoz::detail

namespace kompoz {

using detail::SnapshotData;

Snapshot Snapshot::takeSnapshot(ReadObserver readObserver) {
    std::lock_guard lock(detail::globals().lock);
    auto d = std::make_shared<SnapshotData>();
    d->readOnly = true;
    d->read = std::move(readObserver);
    if (SnapshotData* parent = detail::tCurrent) {  // what that one sees
        d->id = parent->id;
        d->invalid = parent->invalid;
        return Snapshot(std::move(d));
    }
    d->id = detail::gNextId++;
    d->invalid = detail::openIds();
    d->open = true;
    detail::globals().open.push_back(d.get());
    detail::replaceView(detail::gNextId++);  // (the global snapshot's writes from now on: newer than it)
    return Snapshot(std::move(d));
}

MutableSnapshot Snapshot::takeMutableSnapshot(ReadObserver readObserver, WriteObserver writeObserver) {
    std::lock_guard lock(detail::globals().lock);
    auto d = std::make_shared<SnapshotData>();
    d->read = std::move(readObserver);
    d->write = std::move(writeObserver);
    SnapshotData* parent = detail::tCurrent;
    if (parent && parent->readOnly) throw std::logic_error("kompoz: a mutable snapshot taken in a read-only one");
    d->id = detail::gNextId++;
    if (parent) {  // sees what its parent sees, not what was written since it was taken
        d->parent = parent->shared_from_this();
        d->invalid = parent->invalid;
        d->invalid.add(parent->id + 1, d->id - 1);
    } else {
        d->invalid = detail::openIds();
    }
    d->open = true;
    detail::globals().open.push_back(d.get());
    if (!parent) detail::replaceView(detail::gNextId++);
    return MutableSnapshot(std::move(d));
}

SnapshotApplyResult MutableSnapshot::apply() const {
    std::vector<const void*> changed;
    {
        std::lock_guard lock(detail::globals().lock);
        SnapshotData& d = *data_;
        if (d.applied || d.disposed) throw std::logic_error("kompoz: a snapshot applied twice, or after it was disposed");
        int64_t intoId;
        detail::IdSet into;
        if (d.parent) {
            intoId = d.parent->id, into = d.parent->invalid;
        } else {
            const detail::GlobalView* view = detail::loadView(std::memory_order_relaxed);
            intoId = view->id, into = view->invalid;
        }
        detail::IdSet before = d.invalid;  // what it saw before it wrote
        before.add(d.id);
        for (detail::StateObject* object : d.modified) {
            const detail::StateRecord* current = detail::find(*object, intoId, into);
            if (current == detail::find(*object, d.id, before)) continue;  // not written meanwhile
            if (!object->mergeable(*current, *detail::find(*object, d.id, d.invalid))) return SnapshotApplyResult::Failure;
        }
        d.applied = true;
        detail::close(d);
        if (d.parent) {  // its writes become its parent's
            SnapshotData& p = *d.parent;
            for (detail::StateObject* object : d.modified) {
                detail::StateRecord* applied = detail::find(*object, d.id, d.invalid);
                detail::StateRecord* own = detail::find(*object, p.id, p.invalid);
                if (own->snapshotId.load(std::memory_order_relaxed) == p.id)
                    own->assign(*applied);
                else
                    detail::prepend(*object, applied->copy(), p.id);
                applied->snapshotId.store(detail::kInvalidSnapshot, std::memory_order_release);
                p.wrote(object);
                detail::globals().toPrune.insert(object);
            }
            p.changed.insert(p.changed.end(), d.changed.begin(), d.changed.end());
            return SnapshotApplyResult::Success;
        }
        detail::replaceView(detail::loadView(std::memory_order_relaxed)->id);  // its writes seen from now on
        for (detail::StateObject* object : d.modified) {
            changed.push_back(object);
            detail::globals().toPrune.insert(object);
        }
        changed.insert(changed.end(), d.changed.begin(), d.changed.end());
    }
    detail::notify(changed);
    return SnapshotApplyResult::Success;
}

namespace detail {
// Under the lock.
void disposeLocked(SnapshotData& d) {
    if (d.disposed) return;
    d.disposed = true;
    if (!d.applied && !d.readOnly)  // its writes dropped
        for (detail::StateObject* object : d.modified) {
            for (auto* r = object->first.load(std::memory_order_relaxed); r; r = r->next.load(std::memory_order_relaxed))
                if (r->snapshotId.load(std::memory_order_relaxed) == d.id) r->snapshotId.store(detail::kInvalidSnapshot, std::memory_order_release);
            detail::globals().toPrune.insert(object);
        }
    if (d.open) {
        detail::close(d);
        if (!d.parent) detail::replaceView(detail::loadView(std::memory_order_relaxed)->id);
    }
}

SnapshotData::~SnapshotData() {
    if (disposed) return;
    std::lock_guard lock(globals().lock);
    disposeLocked(*this);
}
} // namespace detail

void Snapshot::dispose() const {
    std::lock_guard lock(detail::globals().lock);
    detail::disposeLocked(*data_);
}

bool Snapshot::readOnly() const { return data_->readOnly; }

void Snapshot::sendApplyNotifications() {
    std::vector<const void*> changed;
    {
        std::lock_guard lock(detail::globals().lock);
        if (detail::gGlobalDirty.exchange(false)) {
            for (detail::StateObject* object : detail::globals().globalModified) {
                changed.push_back(object);
                detail::globals().toPrune.insert(object);
            }
            changed.insert(changed.end(), detail::globals().globalChanged.begin(), detail::globals().globalChanged.end());
            detail::globals().globalModified.clear();
            detail::globals().globalModifiedSet.clear();
            detail::globals().globalChanged.clear();
            detail::replaceView(detail::gNextId++);  // written from now on: new records
        }
        if (detail::onUiThread() && !detail::tCurrent) detail::collect();
    }
    detail::notify(changed);
}

Snapshot::ObserverHandle Snapshot::registerApplyObserver(ApplyObserver observer) {
    auto shared = std::make_shared<ApplyObserver>(std::move(observer));
    std::lock_guard lock(detail::globals().lock);
    detail::globals().applyObservers.push_back(shared);
    return {shared};
}

Snapshot::Entered::Entered(const Snapshot& snapshot) : previous_(std::exchange(detail::tCurrent, snapshot.data_.get())) {}
Snapshot::Entered::~Entered() { detail::tCurrent = previous_; }

} // namespace kompoz
