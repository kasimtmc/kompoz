# Architecture

How to structure an app: unidirectional data flow, state holders, `ViewModel`s, flows, saved state and the lifecycle. The patterns are the ones Android recommends for Compose apps, in C++.

## Unidirectional data flow

State flows down, events flow up:

- The UI shows state it is given.
- When the user does something, the UI reports an *event* (a callback) rather than changing the state itself.
- Whoever owns the state decides how the event changes it, and the new state flows back down.

```kotlin
data class TodoItem(val id: Int, val title: String, val done: Boolean = false)

// Stateless: shows the items, reports what the user did.
@Composable
fun TodoList(todos: List<TodoItem>, onToggle: (Int, Boolean) -> Unit) {
    LazyColumn(modifier = Modifier.fillMaxSize()) {
        items(todos, key = { it.id }) { t ->
            ListItem(
                headlineContent = { Text(t.title) },
                leadingContent = { Checkbox(checked = t.done, onCheckedChange = { onToggle(t.id, it) }) },
                modifier = Modifier.toggleable(value = t.done, onValueChange = { onToggle(t.id, it) }),
            )
        }
    }
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

```kotlin continued
class TodoViewModel : ViewModel() {
    val items = MutableStateFlow(listOf(TodoItem(1, "Write docs"), TodoItem(2, "Ship")))

    fun toggle(id: Int, done: Boolean) {
        items.update { list -> list.map { if (it.id == id) it.copy(done = done) else it } }
    }

    override fun onCleared() { /* cancel subscriptions, close files */ }
}

@Composable
fun TodoScreen() {
    val vm = viewModel<TodoViewModel>()   // the same one while its owner lives
    val items by vm.items.collectAsState()
    TodoList(items) { id, done -> vm.toggle(id, done) }
}
```

- `viewModel<T>()` gives the `ViewModel`, made the first time and the same one afterwards (a shared pointer in C++, reached as the object in Kotlin).
- `viewModelScope` is a `CoroutineScope` cancelled when the `ViewModel` is cleared; launch its work there.
- `onCleared()` runs when its owner goes away.
- Screens of a nested navigation graph can share one through the graph's entry: `viewModel<T>(*entry.parent())` (see [Navigation](navigation.md)).

To give a `ViewModel` dependencies, use the overload that takes a factory: `viewModel<T>(key, factory)`, the factory making it with them (in C++, `std::make_shared<T>(repository)`).

A `ViewModel`'s state can also be its properties `by mutableStateOf(…)`, read in composition as any state is, with a `private set` so that only it changes them. Its dependencies can be interfaces, with a fake one given by default; their `suspend fun`s are awaited in its coroutines:

```kotlin
interface NoteRepository {
    suspend fun notes(): List<String>
}

class FakeNotes : NoteRepository {
    override suspend fun notes() = listOf("Write docs", "Ship")
}

class NotesViewModel(private val repo: NoteRepository = FakeNotes()) : ViewModel() {
    var notes by mutableStateOf(emptyList<String>())
        private set

    init {
        viewModelScope.launch { notes = repo.notes() }
    }
}

@Composable
fun NotesScreen() {
    val vm = viewModel<NotesViewModel>()
    Column {
        for (note in vm.notes) Text(note)
    }
}
```

## Flows

A `ViewModel` exposes its state as a `StateFlow`, and the UI reads it with `collectAsState()`, which recomposes when the value changes.

- `MutableStateFlow<T>` holds a value. `setValue(v)` and `update(f)` can be called from **any thread**, which makes it the bridge between worker threads and the UI.
- `value` reads the current value; `collect(f)` follows it in a coroutine.
- It is conflated, as in Kotlin: setting the value it already has does nothing.

`Flow<T>` is a cold stream, produced by a coroutine for each collector, with Kotlin's operators: `map`, `filter`, `take`, `debounce`, `distinctUntilChanged`, `flatMapLatest`, `combine` and more. `stateIn` turns one into a `StateFlow` kept by a scope:

```kotlin
#! class Repository { fun search(q: String): Flow<List<String>> = flow { emit(listOf(q)) } }
class SearchViewModel(repo: Repository) : ViewModel() {
    val query = MutableStateFlow("")
    val results: StateFlow<List<String>> = query.debounce(300)
        .flatMapLatest { q -> repo.search(q) }
        .stateIn(viewModelScope, SharingStarted.Eagerly, emptyList())
}
```

`MutableSharedFlow<T>` is a hot stream for one-off events (show a message, navigate), and `tryEmit` works from any thread. `shareIn` and `stateIn` take `SharingStarted.Eagerly`, `Lazily` or `WhileSubscribed(5000)`, which collects the upstream only while someone is collecting.

Making a flow by hand:

```kotlin
fun ticks(): Flow<Int> = flow {
    var i = 0
    while (true) {
        emit(i++)
        delay(1000)
    }
}
```

Everything runs on the UI thread, in the collecting coroutine. Cancelling the coroutine stops the producer too.

### Channels

A `Channel` hands values from one coroutine (or a click handler) to another, as Kotlin's: `send` waits for room, `trySend` never does (its result says whether it was taken), `receive` waits for the next value, and `consumeEach` takes them one by one, suspending in between. `scope.produce<Int> { send(i) }` starts a coroutine that fills a channel and closes it when it is done.

```kotlin
@Composable
fun Kitchen() {
    val orders = remember { Channel<Int>(3) }   // at most three waiting
    var cooking by remember { mutableStateOf("idle") }
    var next by remember { mutableStateOf(1) }
    LaunchedEffect(Unit) {
        orders.consumeEach { order ->              // one at a time
            cooking = "order $order"
            delay(1500)
            cooking = "order $order done"
        }
    }
    Button(onClick = { if (orders.trySend(next).isSuccess) next++ }) { Text("Order ($cooking)") }
}
```

## Saved state

Android may end an app's process in the background and bring it back later, expecting it to look as it did. Two tools cover this:

- `rememberSaveable` in composables (see [State](state.md#remembersaveable)).
- `SavedStateHandle` in `ViewModel`s: a `ViewModel` whose constructor takes one gets it, with the values it had, when it is made again.

```kotlin
class SearchScreenViewModel(private val state: SavedStateHandle) : ViewModel() {
    val query: StateFlow<String> = state.getStateFlow("query", "")
    fun setQuery(q: String) { state["query"] = q }
}
```

`viewModel<SearchScreenViewModel>()` passes the handle. Values must be saveable types. On the desktop, `KOMPOZ_STATE_FILE=file` saves and restores across runs, so you can try this without a phone.

## Lifecycle

The app, and each navigation entry, has a `Lifecycle` that moves through `Created`, `Started` and `Resumed` as it comes to the front and back. Effects can follow it, as Jetpack's do:

```kotlin
#! class Player { fun play() {} fun pause() {} }
@Composable
fun Video(player: Player) {
    LifecycleResumeEffect(Unit) {
        player.play()
        onPauseOrDispose { player.pause() }   // when it is no longer resumed
    }
}
```

`LifecycleStartEffect`, `LifecycleEventEffect(LifecycleEvent.OnStart, …)` and `LocalLifecycleOwner.current` exist too; `LocalLifecycleOwner.current.lifecycle.currentStateAsState()` gives the state as Compose names it (`Lifecycle.State.RESUMED`, `STARTED`, …). `flow.collectAsStateWithLifecycle()` collects a flow only while the lifecycle is at least started, so in the background it stops counting as a subscriber.

## See also

- [State](state.md): local state and hoisting.
- [Side effects](side-effects.md): coroutines and effects.
- [Navigation](navigation.md): screens, each with its own `ViewModel`s.
- [Testing](testing.md): testing stateless composables and `ViewModel`s.
