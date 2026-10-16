# Side effects

A side effect is a change to the world outside a composable: starting a timer, registering a listener, loading data. Composables run at times you don't control, so effects go through APIs that run them at defined moments. This page covers those APIs and Kompoz's coroutines.

## Coroutines

Kompoz's asynchronous code uses C++20 coroutines. A coroutine here is a lambda that returns `Task` and uses `co_await` where it waits:

```cpp body
using namespace std::chrono_literals;
auto seconds = remember([] { return mutableStateOf(0); });
LaunchedEffect(0, [seconds]() -> Task {
    while (true) {
        co_await delay(1s);
        seconds.set(seconds.get() + 1);
    }
});
Text(std::to_string(seconds.get()) + " s");
```

Coroutines run on the UI thread, driven by the frame clock, so they can read and write state directly. They are *cancelled* when their scope ends: the coroutine is destroyed at the `co_await` where it waits, running the destructors of what it holds. That is why the loop above can be endless: it stops when the composable leaves.

Things you can wait for:

| Await | Like Compose's | Waits |
|---|---|---|
| `co_await delay(300ms)` | `delay(300)` | For a time, on the frame clock. |
| `co_await awaitFrame()` | `withFrameNanos { }` | Until the next frame; returns its time in nanoseconds. |
| `co_await withBackground(work)` | `withContext(Dispatchers.IO) { }` | While `work` runs on a worker thread; returns what it returned. |
| `co_await anim.animateTo(1.f)` | `animateTo(1f)` | Until an animation ends (see [Animation](animation.md)). |
| `co_await f()`, where `f` returns `TaskOf<T>` | a `suspend fun` | Until another coroutine gives its result. |

A coroutine that gives back a value returns `TaskOf<T>` and uses `co_return`; another coroutine awaits it, and it runs as part of the caller and is cancelled with it:

```cpp
TaskOf<std::string> loadName(int id) {
    co_await delay(std::chrono::milliseconds(300));
    co_return "User " + std::to_string(id);
}

void UserName(int id) {
    auto name = remember([] { return mutableStateOf(std::string("…")); });
    LaunchedEffect(id, [id, name]() -> Task { name.set(co_await loadName(id)); });
    Text(name.get());
}
```

A coroutine's captures are kept alive while it runs, so capture **by value**. A lambda that captures by reference outlives the composable that made it, and its references dangle.

### Heavy work

Coroutines run on the UI thread: a long computation in one freezes the UI just as it would anywhere else. Put reading files, decoding and heavy sums on a worker thread with `withBackground`, and come back to the UI thread with the result:

```cpp
#! std::string readFile(const std::string& path) { return path; }
void FileView(const std::string& path) {
    auto text = remember([] { return mutableStateOf(std::string()); });
    LaunchedEffect(path, [path, text]() -> Task {
        text.set(co_await withBackground([path] { return readFile(path); }));
    });
    Text(text.get());
}
```

The work on the worker thread mustn't touch composition state; it returns its result, and the coroutine, back on the UI thread, sets the state. If the coroutine is cancelled meanwhile, the work still runs to its end and its result is dropped. On the web there are no threads, so the work runs right away.

## LaunchedEffect

`LaunchedEffect(key, coroutine)` runs a coroutine when the composable enters the composition, and again whenever `key` changes, cancelling the previous run first. It is cancelled when the composable leaves.

```cpp
void Greeting(const std::string& user, SnackbarHostState snackbar) {
    LaunchedEffect(user, [user, snackbar]() -> Task {
        co_await snackbar.showSnackbarAsync("Welcome back, " + user);
    });
}
```

- Use a key that changes exactly when the effect should restart: an id, a URL. For an effect that should run once, use a constant (`0`), like Compose's `LaunchedEffect(Unit)`.
- The key must compare with `==`. For several, use a `std::tuple`.

## rememberCoroutineScope

`LaunchedEffect` starts work from composition. To start work from an event, such as a click, use a `CoroutineScope` from `rememberCoroutineScope`. Its coroutines are cancelled when the composable that made it leaves.

```cpp
void ScrollUpButton(LazyListState list) {
    auto scope = rememberCoroutineScope();
    Button([scope, list] {
        scope.launch([list]() -> Task { co_await list.animateScrollToItem(0); });
    }, [] { Text("Top"); });
}
```

## DisposableEffect

For effects that need cleaning up, such as registering and unregistering a listener, `DisposableEffect(key, effect)` runs `effect` after composition when it enters or when `key` changes. The function it returns runs when it leaves, or before it runs again for a new key:

```cpp
#! struct Sensor { void subscribe(std::function<void(float)>) {} void unsubscribe() {} };
#! Sensor& sensor();
void Compass() {
    auto heading = remember([] { return mutableStateOf(0.f); });
    DisposableEffect(0, [heading] {
        sensor().subscribe([heading](float h) { heading.set(h); });
        return [] { sensor().unsubscribe(); };   // like Compose's onDispose { }
    });
    Text(std::to_string(int(heading.get())) + "°");
}
```

## SideEffect

`SideEffect(effect)` runs after every successful composition of the enclosing scope. Use it to hand state to something that isn't managed by Kompoz, such as an analytics library, after the UI it describes was composed:

```cpp
#! struct Analytics { void setUserType(const std::string&) {} };
#! Analytics& analytics();
void Shop(const std::string& userType) {
    SideEffect([userType] { analytics().setUserType(userType); });
    Text("Shop");
}
```

## rememberUpdatedState

A long-running effect keyed to a constant doesn't restart when the composable's parameters change, so it would keep using the values it captured first. `rememberUpdatedState` holds the latest value for it to read:

```cpp
void SplashScreen(std::function<void()> onTimeout) {
    auto currentOnTimeout = rememberUpdatedState(onTimeout);
    LaunchedEffect(0, [currentOnTimeout]() -> Task {   // not restarted when onTimeout changes
        co_await delay(std::chrono::seconds(3));
        currentOnTimeout.get()();                       // the latest one
    });
    Text("Loading…");
}
```

## produceState

`produceState(initial, key, producer)` turns the result of a coroutine into a value that composition reads. The coroutine gets a `MutableState` to set, and is restarted when `key` changes:

```cpp
#! TaskOf<int> fetchCount(const std::string&) { co_return 3; }
void Downloads(const std::string& url) {
    int count = produceState(0, url, [url](MutableState<int> state) -> Task {
        state.set(co_await fetchCount(url));
    });
    Text(std::to_string(count) + " files");
}
```

## snapshotFlow

`snapshotFlow(block)` watches states from inside a coroutine: it calls you with the block's value now, and again each time a state the block read changes it to a different value. It goes on until the coroutine is cancelled.

```cpp
void ScrollLogger(LazyListState list) {
    LaunchedEffect(0, [list]() -> Task {
        co_await snapshotFlow([list] { return list.firstVisibleItemIndex(); }).collect([](int first) {
            std::printf("first visible: %d\n", first);
        });
    });
}
```

Use it to react to state changes with effects (logging, saving, analytics) without recomposing.

## Choosing an API

| You want to | Use |
|---|---|
| Start async work when something appears or a value changes | `LaunchedEffect` |
| Start async work from a click or another event | `rememberCoroutineScope` |
| Register something and unregister it later | `DisposableEffect` |
| Push state to code outside Kompoz after each composition | `SideEffect` |
| Use the latest callback inside a long-running effect | `rememberUpdatedState` |
| Turn a coroutine's result into a value | `produceState` |
| React to state changes inside a coroutine | `snapshotFlow` |

## See also

- [State](state.md): what effects usually change.
- [Architecture](architecture.md): flows, `ViewModel`s and their `viewModelScope()`.
- [Lifecycle and identity](lifecycle.md): when effects start and stop.
