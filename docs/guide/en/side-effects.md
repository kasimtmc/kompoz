# Side effects

A side effect is a change to the world outside a composable: starting a timer, registering a listener, loading data. Composables run at times you don't control, so effects go through APIs that run them at defined moments. This page covers those APIs and Kompoz's coroutines.

## Coroutines

A `LaunchedEffect` or `launch` block is a coroutine, as Kotlin's suspend lambda is: kompozc makes it a C++20 coroutine and awaits each call in it that suspends:

```kotlin body
var seconds by remember { mutableStateOf(0) }
LaunchedEffect(Unit) {
    while (true) {
        delay(1000)
        seconds++
    }
}
Text("$seconds s")
```

Coroutines run on the UI thread, driven by the frame clock, so they can read and write state directly. They are *cancelled* when their scope ends: the coroutine is destroyed where it waits, running the destructors of what it holds. That is why the loop above can be endless: it stops when the composable leaves.

Things you can wait for, in a coroutine:

| Call | Like Compose's | Waits |
|---|---|---|
| `delay(300)` | `delay(300)` | For a time in milliseconds, on the frame clock. |
| `awaitFrame()` | `withFrameNanos { }` | Until the next frame; gives its time in nanoseconds. |
| `withBackground { work }` | `withContext(Dispatchers.IO) { }` | While `work` runs on a worker thread; gives what it returned. |
| `anim.animateTo(1f)` | `animateTo(1f)` | Until an animation ends (see [Animation](animation.md)). |
| a function returning `TaskOf<T>` | a `suspend fun` | Until it gives its result. |

A suspending function of your own is written in C++ for now: a coroutine returning `TaskOf<T>` (or `Task`), with `co_await` and `co_return`. A coroutine block calls it as Kotlin calls a `suspend fun`, and kompozc awaits it; it runs as part of the caller and is cancelled with it:

```kotlin
TaskOf<std::string> loadName(int id) {
    co_await delay(std::chrono::milliseconds(300));
    co_return "User " + std::to_string(id);
}

@Composable
fun UserName(id: Int) {
    var name by remember { mutableStateOf("…") }
    LaunchedEffect(id) { name = loadName(id) }
    Text(name)
}
```

A coroutine block captures what it uses by value, as every lambda does here, so it never holds a reference to something gone; what it captured is kept while it runs.

### Jobs, async and structured scopes

`launch` gives back a `Job`: `cancel()` it, ask `isActive()`, `isCompleted()` or `isCancelled()`, or wait in a coroutine with `job.join()` for it to end. `async { … }` starts a coroutine (its block calls a suspending function) and gives back a `Deferred<T>`, whose `await()` gives its value or throws what it threw, so two pieces of work can run at once:

```kotlin
#! struct User { std::string name; }; struct Posts { int count = 0; };
#! TaskOf<User> loadUser(int) { co_return User{"Ada"}; }
#! TaskOf<Posts> loadPosts(int) { co_return Posts{3}; }
@Composable
fun Profile(id: Int) {
    var line by remember { mutableStateOf("…") }
    val scope = rememberCoroutineScope()
    LaunchedEffect(id) {
        val user = scope.async { loadUser(id) }
        val posts = scope.async { loadPosts(id) }  // loading at the same time
        line = "${user.await().name}: ${posts.await().count}"
    }
    Text(line)
}
```

`coroutineScope`, as Kotlin's, runs a block that launches in a scope of its own (the block is written in C++ for now: a coroutine lambda given the `CoroutineScope`, see [Writing in C++](cpp.md)), and goes on only when the block and everything it launched are done, with the block's value if it returns `TaskOf<T>`. A child failing cancels the others and the call throws what it threw; the coroutine awaiting it cancelled, they are all cancelled. `supervisorScope` is the same, but its children fail on their own: a child's failure is reported and cancels nothing.

`withTimeout(limit, block)` is a `coroutineScope` with a time limit: when it runs out, all of it is cancelled and the call throws `TimeoutCancellationException`. `withTimeoutOrNull` gives an empty `std::optional` instead (for a block that gives no value, `false`). A coroutine that ends with a `CancellationException` it didn't catch was cancelled, not failed: it isn't reported.

### Heavy work

Coroutines run on the UI thread: a long computation in one freezes the UI just as it would anywhere else. Put reading files, decoding and heavy sums on a worker thread with `withBackground { … }` (Compose's `withContext(Dispatchers.IO) { … }`), and come back to the UI thread with the result:

```kotlin
#! std::string readFile(const std::string& path) { return path; }
@Composable
fun FileView(path: String) {
    var text by remember { mutableStateOf("") }
    LaunchedEffect(path) {
        text = withBackground { readFile(path) }
    }
    Text(text)
}
```

The work on the worker thread returns its result, and the coroutine, back on the UI thread, sets the state; the work may also write states itself, and reads them only inside a snapshot (see [State](state.md#snapshots-and-threads)). If the coroutine is cancelled meanwhile, the work still runs to its end and its result is dropped. On the web there are no threads, so the work runs right away.

## LaunchedEffect

`LaunchedEffect(key) { … }` runs its block, a coroutine, when the composable enters the composition, and again whenever `key` changes, cancelling the previous run first. It is cancelled when the composable leaves.

```kotlin
@Composable
fun Greeting(user: String, snackbar: SnackbarHostState) {
    LaunchedEffect(user) {
        snackbar.showSnackbar("Welcome back, $user")
    }
}
```

- Use a key that changes exactly when the effect should restart: an id, a URL. For an effect that should run once, use a constant: `LaunchedEffect(Unit)`.
- The key must compare with `==`. `LaunchedEffect`, `DisposableEffect` and `produceState` take two or three keys too, as in Compose (`LaunchedEffect(userId, query) { … }`: restarted when either changes); for more, use a `data class`.

## rememberCoroutineScope

`LaunchedEffect` starts work from composition. To start work from an event, such as a click, use a `CoroutineScope` from `rememberCoroutineScope`. Its coroutines are cancelled when the composable that made it leaves.

```kotlin
@Composable
fun ScrollUpButton(list: LazyListState) {
    val scope = rememberCoroutineScope()
    Button(onClick = { scope.launch { list.animateScrollToItem(0) } }) { Text("Top") }
}
```

## DisposableEffect

For effects that need cleaning up, such as registering and unregistering a listener, `DisposableEffect(key) { … }` runs its block after composition when it enters or when `key` changes. Its `onDispose { … }` runs when it leaves, or before it runs again for a new key:

```kotlin
#! struct Sensor { void subscribe(std::function<void(float)>) {} void unsubscribe() {} };
#! Sensor& sensor();
@Composable
fun Compass() {
    var heading by remember { mutableStateOf(0f) }
    DisposableEffect(Unit) {
        sensor().subscribe { h -> heading = h }
        onDispose { sensor().unsubscribe() }
    }
    Text("${heading.toInt()}°")
}
```

## SideEffect

`SideEffect { … }` runs after every successful composition of the enclosing scope, once what entered it was told so. Use it to hand state to something that isn't managed by Kompoz, such as an analytics library, after the UI it describes was composed:

```kotlin
#! struct Analytics { void setUserType(const std::string&) {} };
#! Analytics& analytics();
@Composable
fun Shop(userType: String) {
    SideEffect { analytics().setUserType(userType) }
    Text("Shop")
}
```

## rememberUpdatedState

A long-running effect keyed to a constant doesn't restart when the composable's parameters change, so it would keep using the values it captured first. `rememberUpdatedState` holds the latest value for it to read:

```kotlin
@Composable
fun SplashScreen(onTimeout: () -> Unit) {
    val currentOnTimeout by rememberUpdatedState(onTimeout)
    LaunchedEffect(Unit) {  // not restarted when onTimeout changes
        delay(3000)
        currentOnTimeout()   // the latest one
    }
    Text("Loading…")
}
```

## produceState

`produceState(initial, key) { … }` turns the result of a coroutine into a value that composition reads. The coroutine sets `value`, and is restarted when `key` changes:

```kotlin
#! TaskOf<int> fetchCount(const std::string&) { co_return 3; }
@Composable
fun Downloads(url: String) {
    val count by produceState(0, url) {
        value = fetchCount(url)
    }
    Text("$count files")
}
```

## snapshotFlow

`snapshotFlow { … }.collect { … }` watches states from inside a coroutine: it calls you with the block's value now, and again each time a state the block read changes it to a different value. It goes on until the coroutine is cancelled.

```kotlin
@Composable
fun ScrollLogger(list: LazyListState) {
    LaunchedEffect(Unit) {
        snapshotFlow { list.firstVisibleItemIndex }.collect { first ->
            println("first visible: $first")
        }
    }
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
