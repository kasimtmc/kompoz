# State

State is any value that can change over time and that the UI shows. This page covers how Kompoz observes state, how to keep it across recompositions and restarts, and where it should live.

## Observable state

Kompoz recomposes when *observable* state changes. `mutableStateOf` makes the basic kind, a `MutableState<T>`:

```kotlin body
val name = remember { mutableStateOf("Ada") }
Text(name.value)                                                // reading it subscribes the running composable
Button(onClick = { name.value = "Grace" }) { Text("Rename") }   // writing it recomposes the readers
```

- Reading `value` records that the running composable depends on the state.
- Writing it stores a new value. If `T` can be compared with `==` and the value is the same, nothing happens; otherwise every composable that read it recomposes. A policy, Compose's `SnapshotMutationPolicy`, can say otherwise: `mutableStateOf(v, neverEqualPolicy())` recomposes the readers on every write, the same value too; `structuralEqualityPolicy()` is the default, `==`; one of your own is a C++ `SnapshotMutationPolicy<T>{[](const T& a, const T& b) { … }}` (a function without captures). `derivedStateOf(policy, calc)` takes one as well.
- `update { }` changes the value in place, for containers: `todos.update { it.add(item) }`.
- `peek()` reads without subscribing, for code that must not recompose when the value changes.

With `var x by mutableStateOf(…)` (or `by remember { mutableStateOf(…) }`), `x` reads and writes the state's value, as in Kotlin. A plain variable is not observable: changing it doesn't recompose anything, and a local variable is gone the moment the composable returns.

A state is shared: copies of it see the same value, so a lambda that captures it (by value, as every lambda does here) reads and writes the same state:

```kotlin
@Composable
fun NameField() {
    var name by remember { mutableStateOf("") }
    OutlinedTextField(value = name, onValueChange = { name = it })
}
```

A composable that only reads a state can take a `State<T>`, Compose's read-only `State`: a `MutableState<T>` or a derived state (`derivedStateOf`) converts to it. It has `value` and `peek()`, but can't be written, and compares by which state it is, so the composable is skipped when it is given the same one again:

```kotlin
@Composable
fun Progress(fraction: State<Float>) {
    LinearProgressIndicator(fraction.value)
}
```

## remember

A composable's body runs on every recomposition, so a state created in it would start over each time. `remember` keeps a value across recompositions:

```kotlin
@Composable
fun Counter() {
    var count by remember { mutableStateOf(0) }   // made once
    Button(onClick = { count++ }) { Text("$count") }
}
```

The lambda runs the first time the composable is composed; after that, `remember` returns the stored value. The value is forgotten when the composable leaves the composition (an `if` turns false, a list item is removed), and made again if it comes back.

`remember` works for any value, not only states. Use it to keep an expensive object, or one whose identity matters, such as a `MutableInteractionSource` or a `FocusRequester`.

### Remembering with a key

`remember(key) { … }` computes the value again when `key` changes:

```kotlin
data class User(val first: String, val last: String)

@Composable
fun Greeting(user: User) {
    val fullName = remember(user.first, user.last) { "Dear ${user.first} ${user.last}" }
    Text(fullName)
}
```

The key can be any value that compares with `==`. Two or three keys can be given, as here, and the value is computed again when any of them changes; for more, use a `data class` or a `std::tuple`.

## rememberSaveable

`remember` keeps a value while the composable stays in the composition. `rememberSaveable` also keeps it:

- when the composable leaves and comes back inside a `SaveableStateHolder`, as the screens of a `NavHost` do when you go back to them;
- when Android ends the app's process in the background and brings it back, and on the desktop across runs with `KOMPOZ_STATE_FILE`.

```kotlin body
var query by rememberSaveable { mutableStateOf("") }
Text(query)
```

Given inputs first, as Compose's `rememberSaveable(inputs)`, it makes the value again when they change, dropping the one it had and its saved one: `rememberSaveable(userId) { mutableStateOf("") }`.

To survive the process ending, the value must be *saveable*. Numbers, `Boolean`, enums, strings, and lists and nullables of saveable types are; so are states of a saveable value and the library's own state objects (scroll and lazy list states, `TextFieldState`, `NavController`). For your own type, specialize `Saver`, next to your composables in C++:

```kotlin
struct Point {
    float x = 0, y = 0;
};

template <>
struct kompoz::Saver<Point> {
    static SavedValue save(const Point& p) { return SavedValue::List{p.x, p.y}; }
    static std::optional<Point> restore(const SavedValue& v) {
        auto* list = v.list();
        if (!list || list->size() != 2) return std::nullopt;
        auto x = (*list)[0].number(), y = (*list)[1].number();
        if (!x || !y) return std::nullopt;
        return Point{float(*x), float(*y)};
    }
};

@Composable
fun Pin() {
    val where by rememberSaveable { mutableStateOf(Point()) }
    Text("${where.x}")
}
```

Or pass a saver made with `listSaver` or `mapSaver`, as Compose's `rememberSaveable(saver = …)`:

```kotlin
struct City {
    std::string name, country;
};

const auto citySaver = mapSaver<City>(
    [](const City& c) { return SavedValue::Map{{"name", c.name}, {"country", c.country}}; },
    [](const SavedValue::Map& m) -> std::optional<City> {
        auto name = m.at("name").string(), country = m.at("country").string();
        return name && country ? std::optional(City{*name, *country}) : std::nullopt;
    });

@Composable
fun CityScreen() {
    val city by rememberSaveable(saver = citySaver) { mutableStateOf(City("Madrid", "Spain")) }
    Text(city.name)
}
```

A value that isn't saveable is still kept by a `SaveableStateHolder`, but starts over after the process ends.

## Other state holders

### derivedStateOf

When a state is computed from others and changes less often than they do, wrap the computation in `derivedStateOf`. Its readers recompose only when the result changes, not on every change of its inputs:

```kotlin
@Composable
fun ScrollToTop() {
    val list = rememberLazyListState()
    val showButton by remember { derivedStateOf { list.firstVisibleItemIndex > 0 } }

    Box(modifier = Modifier.fillMaxSize()) {
        LazyColumn(modifier = Modifier.fillMaxSize(), state = list) {
            items(100) { Text("Row $it") }
        }
        if (showButton) Text("Back to top")
    }
}
```

Without `derivedStateOf`, reading `firstVisibleItemIndex` directly would recompose on every item scrolled past. Create a derived state inside `remember`, so it is made once and can observe its inputs.

### Snapshots and threads

States are kept per *snapshot*, as in Compose: code sees each state as of the snapshot it runs in. Outside any, it runs in the global snapshot, whose writes are told to their readers at the next frame. A composition runs in a snapshot of its own, so it sees one consistent state whatever other threads write meanwhile, and what it writes is seen once it is done.

`Snapshot.withMutableSnapshot` writes several states at once: nothing sees one change without the others. If one of them was written meanwhile elsewhere to a value its policy doesn't count as the same, it throws `SnapshotApplyConflictException` and none of its writes is kept:

```kotlin body
var from by remember { mutableStateOf(100) }
var to by remember { mutableStateOf(0) }
Button(onClick = {
    Snapshot.withMutableSnapshot {  // a transfer: never seen half done
        from -= 10
        to += 10
    }
}) { Text("Move 10") }
```

On the UI thread, states are read and written freely. Another thread may write them, and reads them inside a snapshot: `Snapshot.takeSnapshot()` gives a view of them as they are, read in its `enter { }`, and `dispose()` lets it go. `takeMutableSnapshot()` gives one to write in too, applied with `apply()`; taken inside another, it is applied to that one. Snapshots take read and write observers, and `Snapshot.registerApplyObserver` hears what every applied snapshot changed.

### State lists and maps

`mutableStateListOf` and `mutableStateMapOf` are observable containers, like Compose's `SnapshotStateList` and `SnapshotStateMap`. Reading one subscribes; adding, removing or changing items recomposes the readers:

```kotlin
@Composable
fun Tags() {
    val tags = remember { mutableStateListOf("red", "green") }
    Column {
        for (tag in tags) Text(tag)
        Button(onClick = { tags.add("blue") }) { Text("Add") }
    }
}
```

A list has `add`, `set`, `removeAt`, `removeIf`, `clear` and `update`; a map has `put`, `remove`, `getOrDefault` and the like. A state of a list changed with `update` works just as well.

### rememberUpdatedState

A long-running effect that must not restart, but should use the latest value of a parameter, reads it through `rememberUpdatedState`. See [Side effects](side-effects.md#rememberupdatedstate).

### Flows

`StateFlow` and other flows are observable too: `collectAsState()` turns one into a value that recomposes when it changes. See [Architecture](architecture.md).

## State hoisting

A composable that holds its own state is easy to use but hard to control from outside: its caller can't read the state, set it, or share it with others. *Hoisting* moves the state up to the caller: the composable takes the value and a callback instead.

```kotlin
// Stateless: shows `name`, reports edits. Easy to reuse, preview and test.
@Composable
fun NameInput(name: String, onNameChange: (String) -> Unit) {
    OutlinedTextField(value = name, onValueChange = onNameChange, singleLine = true)
}

// Stateful: owns the state and hands it down.
@Composable
fun Profile() {
    var name by rememberSaveable { mutableStateOf("") }
    Column {
        NameInput(name = name, onNameChange = { name = it })
        Text("Hello, $name")
    }
}
```

The rule of thumb, as in Compose:

- Hoist state to the lowest common ancestor of everything that reads or writes it.
- Pass state down as values, and events up as callbacks (*unidirectional data flow*).
- Offer a stateless version of reusable components; add a stateful wrapper when it is convenient.

### State holders

When a screen's state and logic grow, gather them in a class: a *state holder*. A plain class kept with `remember` suits UI logic (which panel is open, a form's fields); a `ViewModel` suits state that outlives the screen and talks to the rest of the app. Kompoz's own components do this: `LazyListState`, `DrawerState` and `SnackbarHostState` are state holders made by `rememberLazyListState()` and friends. Such a class is written next to your composables, as in Kotlin: its properties `by mutableStateOf(…)` are states, read and set by their names, and a composable reading one recomposes when it changes:

```kotlin
class SearchState {
    var query by mutableStateOf("")
    var expanded by mutableStateOf(false)

    fun clear() {
        query = ""
        expanded = false
    }
}

@Composable
fun SearchScreen() {
    val search = remember { SearchState() }
    Text(search.query)
}
```

See [Architecture](architecture.md) for `ViewModel`s.

## See also

- [Side effects](side-effects.md): work that runs in response to state, and coroutines.
- [Lifecycle and identity](lifecycle.md): when remembered values are kept or forgotten.
- [Architecture](architecture.md): ViewModels and flows.
