# Architecture

How to structure an app: unidirectional data flow, state holders, `ViewModel`s, flows, saved state and the lifecycle. The patterns are the ones Android recommends for Compose apps, in C++.

## Unidirectional data flow

State flows down, events flow up:

- The UI shows state it is given.
- When the user does something, the UI reports an *event* (a callback) rather than changing the state itself.
- Whoever owns the state decides how the event changes it, and the new state flows back down.

```cpp
struct TodoItem {
    int id;
    std::string title;
    bool done;
    friend bool operator==(const TodoItem&, const TodoItem&) = default;
};

// Stateless: shows the items, reports what the user did.
void TodoList(const std::vector<TodoItem>& items, std::function<void(int id, bool done)> onToggle) {
    LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& list) {
        list.items(items, [](const TodoItem& t) { return t.id; }, [onToggle](const TodoItem& t) {
            ListItem(t.done, [onToggle, id = t.id](bool done) { onToggle(id, done); },
                     {.headline = [&] { Text(t.title); }, .leading = [&] { Checkbox(t.done, {}); }});
        });
    });
}
```

This makes composables easy to reuse, preview and test, and leaves one place where each piece of state changes.

## Where state lives

| Kind of state | Where |
|---|---|
| UI element state that nothing else needs (is this menu open?) | `remember` in the composable. |
| State several composables share | Hoisted to their lowest common ancestor (see [State](state.md#state-hoisting)). |
| A screen's UI logic (form fields, what is selected) | A plain state holder class kept with `remember`. |
| Screen state that outlives the UI, and business logic | A `ViewModel`. |
| Data shared across screens (a repository, a cache) | Your own objects, passed to the `ViewModel`s that need them. |

## ViewModel

A `ViewModel` holds a screen's state and does its work, as in Jetpack. It lives in the nearest owner: a navigation entry, cleared when the entry is popped for good, or else the app. So it survives the composables that show it coming and going.

```cpp continued
class TodoViewModel : public ViewModel {
public:
    MutableStateFlow<std::vector<TodoItem>> items{std::vector<TodoItem>{{1, "Write docs", false}, {2, "Ship", false}}};

    void toggle(int id, bool done) {
        items.update([&](const std::vector<TodoItem>& list) {
            std::vector<TodoItem> next = list;
            for (TodoItem& t : next)
                if (t.id == id) t.done = done;
            return next;
        });
    }

protected:
    void onCleared() override { /* cancel subscriptions, close files */ }
};

void TodoScreen() {
    auto vm = viewModel<TodoViewModel>();   // the same one while its owner lives
    std::vector<TodoItem> items = vm->items.collectAsState();
    TodoList(items, [vm](int id, bool done) { vm->toggle(id, done); });
}
```

- `viewModel<T>()` returns a `std::shared_ptr<T>`, made the first time and the same one afterwards. Capture it by value in callbacks.
- `viewModelScope()` is a `CoroutineScope` cancelled when the `ViewModel` is cleared; launch its work there.
- `onCleared()` runs when its owner goes away.
- Screens of a nested navigation graph can share one through the graph's entry: `viewModel<T>(*entry.parent())` (see [Navigation](navigation.md)).

To give a `ViewModel` dependencies, use the overload that takes a factory: `viewModel<T>(key, [&] { return std::make_shared<T>(repository); })`.

## Flows

A `ViewModel` exposes its state as a `StateFlow`, and the UI reads it with `collectAsState()`, which recomposes when the value changes.

- `MutableStateFlow<T>` holds a value. `setValue(v)` and `update(f)` can be called from **any thread**, which makes it the bridge between worker threads and the UI.
- `value()` reads the current value; `collect(f)` follows it in a coroutine.
- It is conflated, as in Kotlin: setting the value it already has does nothing.

`Flow<T>` is a cold stream, produced by a coroutine for each collector, with Kotlin's operators: `map`, `filter`, `take`, `debounce`, `distinctUntilChanged`, `flatMapLatest`, `combine` and more. `stateIn` turns one into a `StateFlow` kept by a scope:

```cpp
#! struct Repository { Flow<std::vector<std::string>> search(const std::string& q) { return flow<std::vector<std::string>>([q](FlowCollector<std::vector<std::string>> emit) -> Task { co_await emit({q}); }); } };
class SearchViewModel : public ViewModel {
public:
    explicit SearchViewModel(Repository& repo)
        : results(query.debounce(std::chrono::milliseconds(300))
                       .flatMapLatest([&repo](const std::string& q) { return repo.search(q); })
                       .stateIn(viewModelScope(), std::vector<std::string>{})) {}

    MutableStateFlow<std::string> query{std::string()};
    StateFlow<std::vector<std::string>> results;
};
```

`MutableSharedFlow<T>` is a hot stream for one-off events (show a message, navigate), and `tryEmit` works from any thread. `shareIn` and `stateIn` take `SharingStarted::Eagerly`, `Lazily` or `WhileSubscribed(5000ms)`, which collects the upstream only while someone is collecting.

Making a flow by hand:

```cpp
Flow<int> ticks() {
    return flow<int>([](FlowCollector<int> emit) -> Task {
        for (int i = 0;; ++i) {
            co_await emit(i);
            co_await delay(std::chrono::seconds(1));
        }
    });
}
```

Everything runs on the UI thread, in the collecting coroutine. Cancelling the coroutine stops the producer too.

## Saved state

Android may end an app's process in the background and bring it back later, expecting it to look as it did. Two tools cover this:

- `rememberSaveable` in composables (see [State](state.md#remembersaveable)).
- `SavedStateHandle` in `ViewModel`s: a `ViewModel` whose constructor takes one gets it, with the values it had, when it is made again.

```cpp
class SearchScreenViewModel : public ViewModel {
public:
    explicit SearchScreenViewModel(SavedStateHandle state) : state_(state), query(state.getStateFlow<std::string>("query", "")) {}
    void setQuery(const std::string& q) { state_.set("query", q); }
    StateFlow<std::string> query;

private:
    SavedStateHandle state_;
};
```

`viewModel<SearchScreenViewModel>()` passes the handle. Values must be saveable types. On the desktop, `KOMPOZ_STATE_FILE=file` saves and restores across runs, so you can try this without a phone.

## Lifecycle

The app, and each navigation entry, has a `Lifecycle` that moves through `Created`, `Started` and `Resumed` as it comes to the front and back. Effects can follow it, as Jetpack's do:

```cpp
#! struct Player { void play() {} void pause() {} };
void Video(std::shared_ptr<Player> player) {
    LifecycleResumeEffect(0, [player] {
        player->play();
        return [player] { player->pause(); };   // when it is no longer resumed
    });
}
```

`LifecycleStartEffect`, `LifecycleEventEffect(LifecycleEvent::OnStart, …)` and `currentLifecycleOwner()` exist too. `collectAsStateWithLifecycle(flow)` collects a flow only while the lifecycle is at least started, so in the background it stops counting as a subscriber.

## See also

- [State](state.md): local state and hoisting.
- [Side effects](side-effects.md): coroutines and effects.
- [Navigation](navigation.md): screens, each with its own `ViewModel`s.
- [Testing](testing.md): testing stateless composables and `ViewModel`s.
