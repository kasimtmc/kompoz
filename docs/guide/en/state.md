# State

State is any value that can change over time and that the UI shows. This page covers how Kompoz observes state, how to keep it across recompositions and restarts, and where it should live.

## Observable state

Kompoz recomposes when *observable* state changes. `mutableStateOf` makes the basic kind, a `MutableState<T>`:

```cpp body
auto name = mutableStateOf(std::string("Ada"));
Text(name.get());         // reading it subscribes the running composable
name.set("Grace");        // writing it recomposes the readers
```

- `get()` reads the value and records that the running composable depends on it.
- `set(v)` stores a new value. If `T` can be compared with `==` and the value is the same, nothing happens; otherwise every composable that read it recomposes.
- `update(f)` changes the value in place, for containers: `todos.update([](auto& v) { v.push_back(item); })`.
- `peek()` reads without subscribing, for code that must not recompose when the value changes.

A plain variable is not observable: changing it doesn't recompose anything, and a local variable is gone the moment the composable returns.

`MutableState<T>` is a handle to a shared value. Copies are cheap and all see the same value, so you pass states around and capture them **by value** in callbacks:

```cpp
void NameField() {
    auto name = remember([] { return mutableStateOf(std::string()); });
    OutlinedTextField(name.get(), [name](std::string v) { name.set(std::move(v)); });
}
```

## remember

A composable's body runs on every recomposition, so a state created in it would start over each time. `remember` keeps a value across recompositions:

```cpp
void Counter() {
    auto count = remember([] { return mutableStateOf(0); });   // made once
    Button([count] { count.set(count.get() + 1); }, [&] { Text(std::to_string(count.get())); });
}
```

The lambda runs the first time the composable is composed; after that, `remember` returns the stored value. The value is forgotten when the composable leaves the composition (an `if` turns false, a list item is removed), and made again if it comes back.

`remember` works for any value, not only states. Use it to keep an expensive object, or one whose identity matters, such as a `MutableInteractionSource` or a `FocusRequester`.

### Remembering with a key

`remember(key, init)` computes the value again when `key` changes:

```cpp
#! struct User { std::string first, last; };
void Greeting(const User& user) {
    auto fullName = remember(user.first + " " + user.last, [&] { return "Dear " + user.first + " " + user.last; });
    Text(fullName);
}
```

The key can be any value that compares with `==`; for several, use a `std::tuple` or a struct.

## rememberSaveable

`remember` keeps a value while the composable stays in the composition. `rememberSaveable` also keeps it:

- when the composable leaves and comes back inside a `SaveableStateHolder`, as the screens of a `NavHost` do when you go back to them;
- when Android ends the app's process in the background and brings it back, and on the desktop across runs with `KOMPOZ_STATE_FILE`.

```cpp body
auto query = rememberSaveable([] { return mutableStateOf(std::string()); });
```

To survive the process ending, the value must be *saveable*. Numbers, `bool`, enums, strings, and `std::vector` and `std::optional` of saveable types are; so are `MutableState` of a saveable value and the library's own state objects (scroll and lazy list states, `TextFieldState`, `NavController`). For your own type, specialize `Saver`:

```cpp
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

void Pin() {
    auto where = rememberSaveable([] { return mutableStateOf(Point{}); });
    Text(std::to_string(where.get().x));
}
```

Or pass a saver made with `listSaver` or `mapSaver`, as Compose's `rememberSaveable(saver = …)`:

```cpp
struct City {
    std::string name, country;
};

const auto citySaver = mapSaver<City>(
    [](const City& c) { return SavedValue::Map{{"name", c.name}, {"country", c.country}}; },
    [](const SavedValue::Map& m) -> std::optional<City> {
        auto name = m.at("name").string(), country = m.at("country").string();
        return name && country ? std::optional(City{*name, *country}) : std::nullopt;
    });

void CityScreen() {
    auto city = rememberSaveable(citySaver, [] { return mutableStateOf(City{"Madrid", "Spain"}); });
    Text(city.get().name);
}
```

A value that isn't saveable is still kept by a `SaveableStateHolder`, but starts over after the process ends.

## Other state holders

### derivedStateOf

When a state is computed from others and changes less often than they do, wrap the computation in `derivedStateOf`. Its readers recompose only when the result changes, not on every change of its inputs:

```cpp
void ScrollToTop() {
    auto list = rememberLazyListState();
    auto showButton = remember([list] { return derivedStateOf([list] { return list.firstVisibleItemIndex() > 0; }); });

    Box(Modifier().fillMaxSize(), [&] {
        LazyColumn(Modifier().fillMaxSize(), {.state = list}, [](LazyListScope& l) {
            l.items(100, [](int i) { Text("Row " + std::to_string(i)); });
        });
        if (showButton.get()) Text("Back to top");
    });
}
```

Without `derivedStateOf`, reading `firstVisibleItemIndex()` directly would recompose on every item scrolled past. Create a derived state inside `remember`, so it is made once and can observe its inputs.

### State lists and maps

`mutableStateListOf` and `mutableStateMapOf` are observable containers, like Compose's `SnapshotStateList` and `SnapshotStateMap`. Reading one subscribes; adding, removing or changing items recomposes the readers:

```cpp
void Tags() {
    auto tags = remember([] { return mutableStateListOf<std::string>({"red", "green"}); });
    Column([&] {
        for (const std::string& tag : tags) Text(tag);
        Button([tags] { tags.add("blue"); }, [] { Text("Add"); });
    });
}
```

A list has `add`, `set`, `removeAt`, `removeIf`, `clear` and `update`; a map has `put`, `remove`, `getOrDefault` and the like. A `MutableState<std::vector<T>>` changed with `update` works just as well.

### rememberUpdatedState

A long-running effect that must not restart, but should use the latest value of a parameter, reads it through `rememberUpdatedState`. See [Side effects](side-effects.md#rememberupdatedstate).

### Flows

`StateFlow` and other flows are observable too: `collectAsState()` turns one into a value that recomposes when it changes. See [Architecture](architecture.md).

## State hoisting

A composable that holds its own state is easy to use but hard to control from outside: its caller can't read the state, set it, or share it with others. *Hoisting* moves the state up to the caller: the composable takes the value and a callback instead.

```cpp
// Stateless: shows `name`, reports edits. Easy to reuse, preview and test.
void NameInput(const std::string& name, std::function<void(std::string)> onNameChange) {
    OutlinedTextField(name, std::move(onNameChange), Modifier(), {.label = "Name"});
}

// Stateful: owns the state and hands it down.
void Profile() {
    auto name = rememberSaveable([] { return mutableStateOf(std::string()); });
    Column([&] {
        NameInput(name.get(), [name](std::string v) { name.set(std::move(v)); });
        Text("Hello, " + name.get());
    });
}
```

The rule of thumb, as in Compose:

- Hoist state to the lowest common ancestor of everything that reads or writes it.
- Pass state down as values, and events up as callbacks (*unidirectional data flow*).
- Offer a stateless version of reusable components; add a stateful wrapper when it is convenient.

### State holders

When a screen's state and logic grow, gather them in a class: a *state holder*. A plain class kept with `remember` suits UI logic (which panel is open, a form's fields); a `ViewModel` suits state that outlives the screen and talks to the rest of the app. Kompoz's own components do this: `LazyListState`, `DrawerState` and `SnackbarHostState` are state holders made by `rememberLazyListState()` and friends.

```cpp
class SearchState {
public:
    MutableState<std::string> query = mutableStateOf(std::string());
    MutableState<bool> expanded = mutableStateOf(false);
    void clear() const {
        query.set("");
        expanded.set(false);
    }
};

void SearchScreen() {
    auto& search = remember([] { return SearchState(); });
    Text(search.query.get());
}
```

See [Architecture](architecture.md) for `ViewModel`s.

## See also

- [Side effects](side-effects.md): work that runs in response to state, and coroutines.
- [Lifecycle and identity](lifecycle.md): when remembered values are kept or forgotten.
- [Architecture](architecture.md): ViewModels and flows.
