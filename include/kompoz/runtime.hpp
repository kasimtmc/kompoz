#pragma once

// The composition runtime: slot table, state, remember, effects, composition locals and
// smart recomposition.
//
// Compose relies on a Kotlin compiler plugin to give every @Composable call a stable
// identity. Kompoz gets the same "positional memoization" at runtime: each composable
// takes a defaulted `CallSite site = CallSite::current()` parameter, so the call site
// (file, line, column) plus a per-parent occurrence counter identifies the call.
//
// Smart recomposition works like Compose's: a composable written with composable(...) is a
// *scope*. While its body runs, every state and composition local it reads is recorded.
// When one of them changes, only the scopes that read it run again, and a scope whose
// arguments compare equal to last time is skipped when its parent runs.

#include "graphics.hpp"
#include "saved_value.hpp"

#include <algorithm>
#include <concepts>
#include <optional>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <source_location>
#include <tuple>
#include <type_traits>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kompoz {

using Content = std::function<void()>;
using CallSite = std::source_location;

class LayoutNode;
class ComposeHost;

namespace detail {

uint64_t keyOf(const CallSite& site);
uint64_t mixKey(uint64_t a, uint64_t b);

struct SlotBase {
    virtual ~SlotBase() = default;
};

template <class T>
struct Slot final : SlotBase {
    template <class F>
    explicit Slot(F& init) : value(init()) {}
    T value;
};

// The arguments a scope was last called with, compared to decide whether it can be skipped.
struct ArgsBase {
    virtual ~ArgsBase() = default;
    virtual bool equals(const ArgsBase& other) const = 0;
};

template <class... Ts>
struct Args final : ArgsBase {
    explicit Args(std::tuple<Ts...> v) : values(std::move(v)) {}
    std::tuple<Ts...> values;

    bool equals(const ArgsBase& other) const override {
        if constexpr ((std::equality_comparable<Ts> && ...)) {
            auto* o = dynamic_cast<const Args*>(&other);
            return o && o->values == values;
        } else {
            return false;  // e.g. a std::function argument: no way to tell, so never skip
        }
    }
};

// A value given to a CompositionLocal by CompositionLocalProvider.
struct Provided {
    const void* key;
    std::shared_ptr<const void> value;
    bool (*equals)(const void*, const void*);
    bool ifMissing = false;  // providesDefault: only where nothing above provides one
};

class Composer;
struct Group;

// A restartable composable: its body (with the arguments captured by value), what the
// body read last time, and whether it has to run again. A scope without a group observes
// reads for something else (a derived state): when one of them changes it calls
// `onInvalidated` instead of recomposing.
struct Scope {
    Scope(Composer& c, Group* g) : composer(c), group(g) {}
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    Composer& composer;
    Group* group;
    std::function<void()> onInvalidated;
    std::function<void()> body;
    std::unique_ptr<ArgsBase> args;
    std::vector<const void*> reads;  // states and composition locals
    bool invalid = false;
};

struct Group;
// A movable content's identity (movableContentOf): its latest content and the group it is
// composed in (none while it isn't).
struct MovableData {
    Content content;
    Group* group = nullptr;
};

// One entry of the slot table. Every composable call opens a group, so the group tree
// mirrors the call tree.
struct Group {
    Group() = default;
    Group(const Group&) = delete;
    Group& operator=(const Group&) = delete;
    ~Group();

    Group* parent = nullptr;
    int depth = 0;
    uint64_t key = 0;  // its key under the parent (call site and occurrence)
    std::unordered_map<uint64_t, std::unique_ptr<Group>> children;
    std::vector<Group*> order;                            // children in call order
    std::unordered_map<uint64_t, uint32_t> occurrences;  // call-site counters, reset every run
    std::unique_ptr<SlotBase> slot;                       // value held by remember()
    const std::type_info* slotType = nullptr;
    std::unique_ptr<LayoutNode> node;                     // node emitted by a layout composable
    std::unique_ptr<Scope> scope;                         // set for restartable composables
    std::vector<Provided> provided;                       // set for CompositionLocalProvider
    std::function<void()> onForget;                       // cleanup registered by DisposableEffect
    std::shared_ptr<MovableData> movable;                  // set for a movable content's group
    uint64_t composedIn = 0;                               // the composition pass it was last called in
    bool visited = false;
};

struct RecompositionStats {
    uint64_t recompositions = 0;  // frames that recomposed anything
    uint64_t scopesRun = 0;
    uint64_t scopesSkipped = 0;
};

class Composer {
public:
    explicit Composer(ComposeHost& host) : host_(host) {}

    static Composer* current();
    static Composer& require();

    // `root` must have a node; `content` becomes the root scope's body.
    void setRoot(Group& root, std::function<void()> content);
    // Runs every invalid scope, outermost first. Returns false if there was nothing to do.
    bool recompose(int64_t frameTimeNanos);
    bool hasInvalidScopes() const { return !invalid_.empty(); }
    void invalidateRoot();
    void invalidateReaders(const void* key, const Group* within = nullptr);
    bool recording() const { return currentScope_ != nullptr; }  // reads now are a scope's
    void invalidateScope(Scope& scope);
    void invalidateFrameScopes();  // scopes that asked for the next animation frame
    bool hasFrameScopes() const { return !frameScopes_.empty(); }
    const RecompositionStats& stats() const { return stats_; }

    // ---- Used by composables ----
    void startGroup(uint64_t key);
    void endGroup();
    // The group stays as it was, its slots and nodes, without running anything (a lazy
    // list's item fading out after its data is gone).
    void keepGroup(uint64_t key) { beginGroup(key); }
    Group& group() { return *groups_.back(); }

    // Emits a layout node: `update` configures it, `content` emits its children.
    LayoutNode& emit(uint64_t key, const std::function<void(LayoutNode&)>& update, const Content& content);
    // Composes a movable content here: its group (state, nodes, effects) comes along from where
    // it was composed last, unless that place called it already in this composition.
    void composeMovable(const std::shared_ptr<MovableData>& data);

    template <class... Ts, class Body>
    void restartable(uint64_t key, std::tuple<Ts...> args, Body&& body);

    void provide(uint64_t key, std::vector<Provided> values, const Content& content);
    const void* findLocal(const void* key) const;
    const std::vector<Provided>& locals() const { return locals_; }  // all provided here, outermost first
    void recordRead(const void* key);
    // Runs `fn` with its reads recorded into `observer` (a group-less scope), from inside or
    // outside a composition.
    void observeReads(Scope& observer, const std::function<void()>& fn);

    template <class T, class F>
    T& rememberSlot(F& init) {
        Group& g = group();
        if (!g.slot || *g.slotType != typeid(T)) {
            g.slot = std::make_unique<Slot<T>>(init);
            g.slotType = &typeid(T);
        }
        return static_cast<Slot<T>&>(*g.slot).value;
    }

    void afterComposition(std::function<void()> effect) { effects_.push_back(std::move(effect)); }
    int64_t frameTimeNanos() const { return frameTime_; }
    void requestFrame();
    ComposeHost& host() { return host_; }

    void forget(Scope& scope);  // a scope left the composition

private:
    Group& beginGroup(uint64_t key);
    void enter(Group& g);
    // An unvisited group leaving the composition: movable contents in it wait in parked_ until
    // the composition is over (one may be called again further on).
    void park(std::unique_ptr<Group>& owner);
    std::unique_ptr<Group> takeMovable(MovableData& data);
    void run(Scope& scope);
    void restart(Scope& scope);
    void clearReads(Scope& scope);
    std::vector<Provided> localsAbove(const Group& g) const;

    ComposeHost& host_;
    Group* root_ = nullptr;
    std::vector<Group*> groups_;
    std::vector<Provided> locals_;
    std::vector<std::function<void()>> effects_;
    Scope* currentScope_ = nullptr;
    std::unordered_map<const void*, std::vector<Scope*>> readers_;
    std::vector<Scope*> invalid_;
    std::vector<Scope*> frameScopes_;
    std::vector<std::unique_ptr<Group>> parked_;
    uint64_t pass_ = 0;  // which composition this is
    int64_t frameTime_ = 0;
    RecompositionStats stats_;
};

template <class... Ts, class Body>
void Composer::restartable(uint64_t key, std::tuple<Ts...> args, Body&& body) {
    Group& g = beginGroup(key);
    auto next = std::make_unique<Args<Ts...>>(std::move(args));
    Scope* s = g.scope.get();
    if (s && !s->invalid && s->args && s->args->equals(*next)) {
        ++stats_.scopesSkipped;  // same arguments, nothing it read changed: keep its UI as is
        return;
    }
    if (!s) {
        g.scope = std::make_unique<Scope>(*this, &g);
        s = g.scope.get();
    }
    s->args = std::move(next);
    s->body = std::forward<Body>(body);
    run(*s);
}

// Records that the running scope read `key` (a state or a composition local).
void recordRead(const void* key);
// Queues the readers of `key` for recomposition. Safe from any thread.
void notifyStateChanged(const void* key);
// A scroll position or similar layout-only input changed: lay out again, don't recompose.
// UI thread only.
void requestLayout();
// Something only drawn changed (an overscroll stretch): draw again. UI thread only.
void requestDraw();

} // namespace detail

// A restartable, skippable composable: what a plain @Composable function is in Kotlin.
//
//   void Greeting(std::string name, CallSite site = CallSite::current()) {
//       composable(site, name, [=] {
//           Text("Merhaba " + name);
//       });
//   }
//
// The trailing lambda is the body. Capture by value ([=]) so the body can run again on its
// own when a state it read changes. The arguments before it decide skipping: when they
// compare equal to the previous call and nothing the body read has changed, the body does
// not run and its UI stays as it is. Arguments that can't be compared (std::function, ...)
// make the composable restartable but never skipped.
template <class... T>
void composable(const CallSite& site, T&&... argsAndBody) {
    static_assert(sizeof...(T) >= 1, "composable() needs a body");
    constexpr size_t n = sizeof...(T) - 1;
    auto all = std::forward_as_tuple(std::forward<T>(argsAndBody)...);
    auto args = [&]<size_t... I>(std::index_sequence<I...>) {
        return std::tuple<std::decay_t<std::tuple_element_t<I, decltype(all)>>...>(std::get<I>(all)...);
    }(std::make_index_sequence<n>{});
    detail::Composer::require().restartable(detail::keyOf(site), std::move(args), std::get<n>(all));
}

// Gives a composable its own identity without making it a scope. Use it for composables
// that take content lambdas (which capture by reference and can't be re-run on their own),
// the way Column and Row are inline functions in Compose.
class ComposableScope {
public:
    explicit ComposableScope(const CallSite& site) { detail::Composer::require().startGroup(detail::keyOf(site)); }
    ~ComposableScope() { detail::Composer::require().endGroup(); }
    ComposableScope(const ComposableScope&) = delete;
    ComposableScope& operator=(const ComposableScope&) = delete;
};

// Observable value holder. Copies share the same value, so a state can (and should) be
// captured by value in event callbacks. Reading it inside a composable subscribes that
// composable; setting it recomposes exactly the subscribers.
template <class T>
class MutableState {
public:
    explicit MutableState(T initial) : cell_(std::make_shared<T>(std::move(initial))) {}

    const T& get() const {
        detail::recordRead(cell_.get());
        return *cell_;
    }
    // Reads without subscribing the running composable.
    const T& peek() const { return *cell_; }
    const T& operator*() const { return get(); }
    const T* operator->() const { return &get(); }

    void set(T value) const {
        if constexpr (std::equality_comparable<T>) {
            if (*cell_ == value) return;
        }
        *cell_ = std::move(value);
        detail::notifyStateChanged(cell_.get());
    }

    // Mutates the value in place: todos.update([](auto& list) { list.push_back(...); });
    template <class F>
    void update(F&& mutate) const {
        std::forward<F>(mutate)(*cell_);
        detail::notifyStateChanged(cell_.get());
    }

    // The same state, not the same value: lets composables taking a state be skipped.
    friend bool operator==(const MutableState& a, const MutableState& b) { return a.cell_ == b.cell_; }

private:
    std::shared_ptr<T> cell_;
};

template <class T>
MutableState<T> mutableStateOf(T initial) {
    return MutableState<T>(std::move(initial));
}
inline MutableState<std::string> mutableStateOf(const char* initial) {
    return MutableState<std::string>(initial);
}
// Compose's primitive states; here plain mutableStateOf already stores the value unboxed.
inline MutableState<int> mutableIntStateOf(int initial) { return mutableStateOf(initial); }
inline MutableState<float> mutableFloatStateOf(float initial) { return mutableStateOf(initial); }
inline MutableState<long long> mutableLongStateOf(long long initial) { return mutableStateOf(initial); }
inline MutableState<double> mutableDoubleStateOf(double initial) { return mutableStateOf(initial); }

// remember { ... }: computes the value once and returns the same object on every recomposition.
template <class F>
auto remember(F&& init, CallSite site = CallSite::current()) -> std::decay_t<std::invoke_result_t<F&>>& {
    using T = std::decay_t<std::invoke_result_t<F&>>;
    auto& c = detail::Composer::require();
    c.startGroup(detail::keyOf(site));
    T& value = c.rememberSlot<T>(init);
    c.endGroup();
    return value;
}

// remember(key) { ... }: recomputes the value whenever `key` changes.
template <class K, class F>
auto remember(const K& key, F&& init, CallSite site = CallSite::current())
    -> std::decay_t<std::invoke_result_t<F&>>& {
    using T = std::decay_t<std::invoke_result_t<F&>>;
    struct Keyed {
        K key;
        T value;
    };
    auto& c = detail::Composer::require();
    c.startGroup(detail::keyOf(site));
    auto make = [&] { return Keyed{key, init()}; };
    Keyed* slot = &c.rememberSlot<Keyed>(make);
    if (!(slot->key == key)) {
        c.group().slot.reset();
        slot = &c.rememberSlot<Keyed>(make);
    }
    c.endGroup();
    return slot->value;
}

// Gives `content` an explicit identity, e.g. for list items that can move or be removed.
template <class K>
void key(const K& id, const Content& content, CallSite site = CallSite::current()) {
    auto& c = detail::Composer::require();
    c.startGroup(detail::mixKey(detail::keyOf(site), std::hash<K>{}(id)));
    content();
    c.endGroup();
}

// Content whose state goes with it wherever it is composed next (Compose's movableContentOf):
// called from another place in the same composition (a Row instead of a Column, another pane),
// its remembered values, nodes and running effects move there instead of starting over. Called
// in two places at once, the second is a new instance. Keep it with remember:
//   auto video = remember([] { return movableContentOf([] { VideoPlayer(); }); });
//   if (wide) Row([&] { video(); Details(); }); else Column([&] { video(); Details(); });
class MovableContent {
public:
    explicit MovableContent(Content content) : data_(std::make_shared<detail::MovableData>()) { data_->content = std::move(content); }
    void operator()() const { detail::Composer::require().composeMovable(data_); }
    friend bool operator==(const MovableContent& a, const MovableContent& b) { return a.data_ == b.data_; }

private:
    std::shared_ptr<detail::MovableData> data_;
};
inline MovableContent movableContentOf(Content content) { return MovableContent(std::move(content)); }

// The same taking parameters (Compose's movableContentOf((P1, P2…) -> Unit)): each call composes
// it with those, its state going with it. Give their types:
//   auto card = remember([] { return movableContentOf<std::string, bool>([](const std::string& title, const bool& open) {
//       auto likes = remember([] { return mutableStateOf(0); });  // moves with it
//       ...
//   }); });
//   if (wide) Row([&] { card(title, true); }); else Column([&] { card(title, false); });
template <class... P>
class MovableContentOf {
public:
    explicit MovableContentOf(std::function<void(const P&...)> content)
        : data_(std::make_shared<detail::MovableData>()), args_(std::make_shared<std::optional<std::tuple<P...>>>()) {
        data_->content = [content = std::move(content), args = args_] {
            if (*args) std::apply(content, **args);
        };
    }
    void operator()(P... args) const {
        *args_ = std::tuple<P...>(std::move(args)...);  // the latest ones: what it composes with here
        detail::Composer::require().composeMovable(data_);
    }
    friend bool operator==(const MovableContentOf& a, const MovableContentOf& b) { return a.data_ == b.data_; }

private:
    std::shared_ptr<detail::MovableData> data_;
    std::shared_ptr<std::optional<std::tuple<P...>>> args_;
};
template <class P1, class... P>
MovableContentOf<P1, P...> movableContentOf(std::type_identity_t<std::function<void(const P1&, const P&...)>> content) {
    return MovableContentOf<P1, P...>(std::move(content));
}

// With a receiver first (Compose's movableContentWithReceiverOf: R.(P…) -> Unit): the receiver is
// the one it is called with each time.
template <class R, class... P>
class MovableContentWithReceiverOf {
public:
    explicit MovableContentWithReceiverOf(std::function<void(R&, const P&...)> content)
        : inner_([content = std::move(content)](R* const& receiver, const P&... args) { content(*receiver, args...); }) {}
    void operator()(R& receiver, P... args) const { inner_(&receiver, std::move(args)...); }
    friend bool operator==(const MovableContentWithReceiverOf& a, const MovableContentWithReceiverOf& b) { return a.inner_ == b.inner_; }

private:
    MovableContentOf<R*, P...> inner_;
};
template <class R, class... P>
MovableContentWithReceiverOf<R, P...> movableContentWithReceiverOf(std::type_identity_t<std::function<void(R&, const P&...)>> content) {
    return MovableContentWithReceiverOf<R, P...>(std::move(content));
}

// Runs after every successful composition of the enclosing scope.
inline void SideEffect(std::function<void()> effect) {
    detail::Composer::require().afterComposition(std::move(effect));
}

// Runs `effect` after composition when it enters the composition or when `key` changes.
// The function it returns is called when the effect leaves the composition or re-runs.
template <class K>
void DisposableEffect(const K& key, std::function<std::function<void()>()> effect,
                      CallSite site = CallSite::current()) {
    struct State {
        K key;
        bool pending = true;
    };
    auto& c = detail::Composer::require();
    c.startGroup(detail::keyOf(site));
    detail::Group* g = &c.group();
    auto init = [&] { return State{key}; };
    State& s = c.rememberSlot<State>(init);
    if (!(s.key == key)) {
        s.key = key;
        s.pending = true;
    }
    if (s.pending) {
        s.pending = false;
        c.afterComposition([g, effect = std::move(effect)] {
            if (g->onForget) std::exchange(g->onForget, nullptr)();
            g->onForget = effect();
        });
    }
    c.endGroup();
}

// A value handed down the composition (Compose's CompositionLocal): what the nearest provider
// above gives, else its default. Known by its address: define it once, as a global.
//   inline const CompositionLocal<Color> LocalAccent{colors::Blue};
//   CompositionLocalProvider({LocalAccent.provides(cs.tertiary)}, [&] { Box(Modifier().background(LocalAccent.current())); });
template <class T>
class CompositionLocal {
public:
    explicit CompositionLocal(T defaultValue) : default_(std::move(defaultValue)) {}
    // Its default made when first needed (Compose's compositionLocalOf { … }): it may throw for
    // a local that must be provided.
    template <class F>
        requires std::invocable<F&> && std::convertible_to<std::invoke_result_t<F&>, T>
    explicit CompositionLocal(F defaultFactory) : factory_(std::move(defaultFactory)) {}
    CompositionLocal(const CompositionLocal&) = delete;  // its address is who it is
    CompositionLocal& operator=(const CompositionLocal&) = delete;

    // Reading a local subscribes the running scope: it recomposes when a provider above it
    // starts providing a different value.
    const T& current() const {
        if (auto* c = detail::Composer::current()) {
            c->recordRead(this);
            if (auto* v = c->findLocal(this)) return *static_cast<const T*>(v);
        }
        if (!default_) default_.emplace(factory_());
        return *default_;
    }

    detail::Provided provides(T value) const {
        return {this, std::make_shared<const T>(std::move(value)), [](const void* a, const void* b) {
                    if constexpr (std::equality_comparable<T>)
                        return *static_cast<const T*>(a) == *static_cast<const T*>(b);
                    else
                        return a == b;
                }};
    }
    // Only where no provider above gives one (Compose's providesDefault): a library's own
    // default the app may have set already.
    detail::Provided providesDefault(T value) const {
        detail::Provided p = provides(std::move(value));
        p.ifMissing = true;
        return p;
    }

private:
    mutable std::optional<T> default_;
    std::function<T()> factory_;
};

// Compose's compositionLocalOf { default } and staticCompositionLocalOf { default }: a local
// whose default is made when first read without a provider (here both recompose only the
// scopes that read it when a provider gives another value).
//   inline const auto LocalUser = compositionLocalOf<User>([]() -> User { throw std::logic_error("LocalUser verilmedi"); });
template <class T>
CompositionLocal<T> compositionLocalOf(std::function<T()> defaultFactory) {
    return CompositionLocal<T>(std::move(defaultFactory));
}
template <class F>
    requires std::invocable<F&>
CompositionLocal<std::decay_t<std::invoke_result_t<F&>>> compositionLocalOf(F defaultFactory) {
    return CompositionLocal<std::decay_t<std::invoke_result_t<F&>>>(std::function<std::decay_t<std::invoke_result_t<F&>>()>(std::move(defaultFactory)));
}
template <class T>
CompositionLocal<T> staticCompositionLocalOf(std::function<T()> defaultFactory) {
    return CompositionLocal<T>(std::move(defaultFactory));
}
template <class F>
    requires std::invocable<F&>
CompositionLocal<std::decay_t<std::invoke_result_t<F&>>> staticCompositionLocalOf(F defaultFactory) {
    return compositionLocalOf(std::move(defaultFactory));
}

inline void CompositionLocalProvider(std::initializer_list<detail::Provided> values, const Content& content,
                                     CallSite site = CallSite::current()) {
    detail::Composer::require().provide(detail::keyOf(site), std::vector<detail::Provided>(values), content);
}

// Every local provided where it is read (Compose's CompositionLocalContext), to provide them all
// again somewhere else — content composed elsewhere (another window, a host of its own) that
// should see the same theme and values:
//   CompositionLocalContext here = currentCompositionLocalContext();
//   ... CompositionLocalProvider(here, [&] { Text("Aynı tema"); });
struct CompositionLocalContext {
    std::vector<detail::Provided> values;
};
// Reading it subscribes to them all.
inline CompositionLocalContext currentCompositionLocalContext() {
    auto& c = detail::Composer::require();
    for (const detail::Provided& p : c.locals()) c.recordRead(p.key);
    return {c.locals()};
}
inline void CompositionLocalProvider(const CompositionLocalContext& context, const Content& content, CallSite site = CallSite::current()) {
    detail::Composer::require().provide(detail::keyOf(site), context.values, content);
}

// ---- Saveable state ----------------------------------------------------------------------
//
// rememberSaveable keeps its value while the composable is away from the composition, inside a
// SaveableStateHolder's provider: a navigation entry on the back stack, a tab that isn't shown.
// Values of saveable types (see saved_value.hpp) are also written out with the host's state
// (ComposeHost::saveState), which Android keeps while it ends the app in the background, and
// come back with it. Outside any provider a value goes when its composable leaves.

namespace detail {
struct SaveableRegistry {
    std::unordered_map<uint64_t, std::shared_ptr<void>> values;  // by position under the provider
    std::unordered_map<uint64_t, const std::type_info*> types;
    std::unordered_map<uint64_t, std::function<SavedValue()>> savers;  // of the saveable values
    std::unordered_map<uint64_t, SavedValue> restored;  // from a saved state, for values not made yet
    const Group* boundary = nullptr;  // the provider's group
    bool forgetsLeaving = false;      // the host's: a value goes with its composable
    // A SaveableStateHolder's: the values whose composables left (kept if the provider left
    // with them, as Compose saves a provider's values as it goes; else gone once the
    // composition settles, as Compose's leaving rememberSaveable unregisters), and whether
    // the provider is there.
    std::vector<uint64_t> leaving;
    bool present = true;
    SavedValue save() const;
    void restore(const SavedValue& saved);
    void drop(uint64_t key);
};
// A holder's value left with its composable (see SaveableRegistry::leaving).
void noteLeaving(const std::shared_ptr<SaveableRegistry>& registry, uint64_t key);
// After a composition: the values that left with their composables, not their providers, gone.
void settleSaveables();
extern const CompositionLocal<std::shared_ptr<SaveableRegistry>> LocalSaveableRegistry;
// The current group's position below `boundary`: the keys of the groups in between.
uint64_t keyBelow(const Group* boundary);

// What rememberSaveable keeps in its group: the value, and its entry in its registry, which
// goes with the group: at once from the host's, from a holder's unless its provider goes too.
template <class T>
struct SaveableSlot {
    std::shared_ptr<T> value;
    std::weak_ptr<SaveableRegistry> registry;
    uint64_t key = 0;
    SaveableSlot(std::shared_ptr<T> v, std::weak_ptr<SaveableRegistry> r, uint64_t k) : value(std::move(v)), registry(std::move(r)), key(k) {}
    SaveableSlot(const SaveableSlot&) = delete;
    SaveableSlot& operator=(const SaveableSlot&) = delete;
    ~SaveableSlot() {
        if (auto r = registry.lock()) {
            if (r->forgetsLeaving) r->drop(key);
            else noteLeaving(r, key);
        }
    }
};
} // namespace detail

namespace detail {
// rememberSaveable with how its value is saved: `save` and `restore` (Saver<T>'s, a
// CustomSaver's), or nullptr for a value that isn't written out.
template <class T, class F, class Save, class Restore>
T& rememberSaveableWith(F& init, const Save& save, const Restore& restore, CallSite site) {
    constexpr bool saveable = !std::is_same_v<Save, std::nullptr_t>;
    auto& c = detail::Composer::require();
    std::shared_ptr<detail::SaveableRegistry> registry = detail::LocalSaveableRegistry.current();
    c.startGroup(detail::keyOf(site));
    auto make = [&]() -> detail::SaveableSlot<T> {
        if (!registry) return {std::make_shared<T>(init()), {}, 0};
        uint64_t key = detail::keyBelow(registry->boundary);
        std::weak_ptr<detail::SaveableRegistry> forget = registry;
        if (auto left = std::find(registry->leaving.begin(), registry->leaving.end(), key); left != registry->leaving.end()) {
            registry->leaving.erase(left);  // (it left with its composable, its provider still there: a new one)
            registry->drop(key);
        }
        if (auto it = registry->values.find(key); it != registry->values.end() && registry->types[key] == &typeid(T))
            return {std::static_pointer_cast<T>(it->second), forget, key};  // back again: the value it had
        std::shared_ptr<T> value;
        if constexpr (saveable)
            if (auto it = registry->restored.find(key); it != registry->restored.end()) {  // after the process ended
                if (std::optional<T> v = restore(it->second)) value = std::make_shared<T>(std::move(*v));
                registry->restored.erase(it);
            }
        if (!value) value = std::make_shared<T>(init());
        registry->values[key] = value;
        registry->types[key] = &typeid(T);
        if constexpr (saveable)
            registry->savers[key] = [weak = std::weak_ptr<T>(value), save] {
                auto v = weak.lock();
                return v ? save(*v) : SavedValue();
            };
        return {value, forget, key};
    };
    auto& slot = c.rememberSlot<detail::SaveableSlot<T>>(make);
    c.endGroup();
    return *slot.value;
}
} // namespace detail

// remember { ... } that a SaveableStateHolder keeps while the composable is away, and that
// comes back after the process ended if T is saveable (Saver<T>).
template <class F>
auto rememberSaveable(F&& init, CallSite site = CallSite::current()) -> std::decay_t<std::invoke_result_t<F&>>& {
    using T = std::decay_t<std::invoke_result_t<F&>>;
    if constexpr (HasSaver<T>)
        return detail::rememberSaveableWith<T>(init, &Saver<T>::save, &Saver<T>::restore, site);
    else
        return detail::rememberSaveableWith<T>(init, nullptr, nullptr, site);
}

// The same, saved by `saver` (Compose's rememberSaveable(saver = …)); a saver of T saves a
// MutableState<T> made by init too (Compose's stateSaver).
template <class S, class F>
auto rememberSaveable(const CustomSaver<S>& saver, F&& init, CallSite site = CallSite::current()) -> std::decay_t<std::invoke_result_t<F&>>& {
    using T = std::decay_t<std::invoke_result_t<F&>>;
    if constexpr (std::is_same_v<T, S>) {
        return detail::rememberSaveableWith<T>(init, saver.save, saver.restore, site);
    } else {
        static_assert(std::is_same_v<T, MutableState<S>>, "rememberSaveable(saver, init): init makes the saver's type or a MutableState of it");
        auto save = [s = saver.save](const T& state) { return s(state.peek()); };
        auto restore = [r = saver.restore](const SavedValue& saved) -> std::optional<T> {
            std::optional<S> v = r(saved);
            return v ? std::optional<T>(mutableStateOf(std::move(*v))) : std::nullopt;
        };
        return detail::rememberSaveableWith<T>(init, save, restore, site);
    }
}

// A state of a saveable value is saveable.
template <class T>
    requires HasSaver<T>
struct Saver<MutableState<T>> {
    static SavedValue save(const MutableState<T>& state) { return Saver<T>::save(state.peek()); }
    static std::optional<MutableState<T>> restore(const SavedValue& saved) {
        auto v = Saver<T>::restore(saved);
        return v ? std::optional(mutableStateOf(std::move(*v))) : std::nullopt;
    }
};

// Keeps the rememberSaveable values of content shown under different keys (Compose's
// rememberSaveableStateHolder): SaveableStateProvider(key) { ... } finds them again when that
// key comes back; removeState(key) forgets them.
class SaveableStateHolder {
public:
    SaveableStateHolder() : registries_(std::make_shared<Registries>()) {}
    void SaveableStateProvider(const std::string& key, const Content& content, CallSite site = CallSite::current()) const;
    // Content still shown under a removed key (leaving with an animation) keeps nothing new.
    void removeState(const std::string& key) const {
        registries_->erase(key);
        removed_->push_back(key);
    }
    friend bool operator==(const SaveableStateHolder& a, const SaveableStateHolder& b) { return a.registries_ == b.registries_; }
    // Its keys' values, written out and read back (Saver<SaveableStateHolder>).
    SavedValue save() const;
    static SaveableStateHolder restore(const SavedValue& saved);

private:
    using Registries = std::unordered_map<std::string, std::shared_ptr<detail::SaveableRegistry>>;
    std::shared_ptr<Registries> registries_;
    std::shared_ptr<std::vector<std::string>> removed_ = std::make_shared<std::vector<std::string>>();
};
template <>
struct Saver<SaveableStateHolder> {
    static SavedValue save(const SaveableStateHolder& holder) { return holder.save(); }
    static std::optional<SaveableStateHolder> restore(const SavedValue& saved) { return SaveableStateHolder::restore(saved); }
};
inline SaveableStateHolder rememberSaveableStateHolder(CallSite site = CallSite::current()) {
    return rememberSaveable([] { return SaveableStateHolder(); }, site);
}

// Queues `task` to run on the UI thread before the next frame. Safe to call from any thread;
// use it to hand results of background work to the UI.
void runOnUiThread(std::function<void()> task);

} // namespace kompoz
