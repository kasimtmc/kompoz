# Lifecycle and identity

A composable enters the composition, recomposes zero or more times, and leaves. This page explains that lifecycle, how Kompoz tells one call from another without a compiler plugin, and what makes a composable skip.

## The lifecycle of a composable

When Kompoz first runs your UI, it records every composable that was called in a tree: the *composition*. From then on, each call in that tree has a lifecycle:

1. **It enters the composition** the first time it is called. Its `remember` blocks compute their values, its effects are scheduled.
2. **It recomposes** whenever a state it read changes, or when its parent runs and its arguments have changed. Remembered values are kept.
3. **It leaves the composition** when it is no longer called: an `if` became false, a list item went away, the screen was navigated away from. Its remembered values are forgotten, its `DisposableEffect` clean-ups run, its coroutines are cancelled.

```cpp
void Details(bool show) {
    if (show) {
        // Enters when show becomes true, leaves when it becomes false.
        auto opened = remember([] { return mutableStateOf(0); });
        DisposableEffect(0, [] {
            // here: it entered
            return [] { /* here: it left */ };
        });
        Text("Details");
    }
}
```

A composable that leaves and comes back starts fresh: `remember` runs its lambda again. To keep a value across that, use `rememberSaveable` inside a `SaveableStateHolder` (navigation does this for its screens) or hoist the state above the `if`.

## Identity: where a call comes from

To know which remembered values belong to which call, Kompoz needs to identify each call across recompositions. Jetpack Compose does it with a compiler plugin. Kompoz does it at run time, with `std::source_location`:

- Every composable takes a last parameter `CallSite site = CallSite::current()`. The default argument is evaluated where the composable is *called*, so `site` is the file, line and column of the call.
- Inside a parent, calls from the same site are told apart by their order: the first call from a site, the second call from it, and so on.

You don't pass `site` yourself: the default does it. You only write it when you define a composable that needs its own identity:

```cpp
void Badge(int count, CallSite site = CallSite::current()) {
    composable(site, count, [=] {
        Text(std::to_string(count));
    });
}
```

`remember` takes a `CallSite` the same way, so two `remember` calls on different lines are different values.

### What a plain function is

A plain C++ function that calls composables, without `composable` or `ComposableScope`, has no identity of its own. The calls inside it are identified by their own call sites, inside the composable that called the function. That is fine, and most small helpers are written that way:

```cpp
void Header(const std::string& title) {      // a plain function
    Text(title, {.font = typography().headlineSmall});
    HorizontalDivider();
}
```

What a plain function doesn't get is a *scope*: the states it reads belong to the nearest enclosing scope, which recomposes when they change, and it is never skipped on its own.

### Scopes

`composable(site, args…, body)` makes a *restartable* and *skippable* scope, what an ordinary `@Composable` function is in Kotlin:

- **Restartable**: when a state the body read changes, only this body runs again, not the parent.
- **Skippable**: when the parent runs and `args…` compare equal to last time, the body doesn't run.

Because the body can run on its own later, it must capture by value: `[=]`.

`ComposableScope scope(site);` gives a composable an identity without making it a scope. Components that take a content lambda use it, the way `Column` and `Row` are inline functions in Compose: their content lambdas capture by reference and can't be run again on their own, so the state they read belongs to the caller.

```cpp
void Section(const std::string& title, const Content& content, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    Column(Modifier().padding(16), [&] {
        Text(title, {.font = typography().titleMedium});
        content();
    });
}
```

## Skipping

A scope is skipped when its parent runs, its arguments compare equal to the previous call, and nothing it read has changed since. For that, arguments need `==`:

- Numbers, strings, enums and your own structs with `operator==` (or `= default`) compare by value.
- A `MutableState` compares by identity: the same state is equal even if its value changed. The scope still recomposes when the value changes, because it read it.
- A `Modifier` compares by the values of its elements.
- `std::function` can't be compared. A composable that takes one is restartable but never skipped.

```cpp
struct Todo {
    int id = 0;
    std::string title;
    bool done = false;
    friend bool operator==(const Todo&, const Todo&) = default;
};

void TodoRow(const Todo& todo, MutableState<std::vector<Todo>> todos, CallSite site = CallSite::current()) {
    composable(site, todo, todos, [=] {   // skipped while todo and todos are the same
        Row([&] {
            Checkbox(todo.done, [todos, id = todo.id](bool done) {
                todos.update([&](std::vector<Todo>& list) {
                    for (Todo& t : list)
                        if (t.id == id) t.done = done;
                });
            });
            Text(todo.title);
        });
    });
}
```

When one todo changes, the list recomposes, and every other `TodoRow` is skipped.

`Text` is skippable too. Most components that take content lambdas are not scopes and run with their caller; that is cheap, because what they lay out only changes when their inputs do.

## Calls in loops

Inside a loop, every iteration calls from the same site, so the calls are identified by their order. That works while the list only grows at the end. When items are inserted, removed or reordered, state would stay with the *position*, not the item:

```cpp nocheck
for (const Todo& todo : todos.get()) {
    TodoRow(todo, todos);   // insert at the front, and every row's remembered state shifts by one
}
```

`key` gives each call an identity of your choosing:

```cpp
#! struct Todo { int id = 0; std::string title; bool done = false; friend bool operator==(const Todo&, const Todo&) = default; };
#! void TodoRow(const Todo&, MutableState<std::vector<Todo>>, CallSite site = CallSite::current()) {}
void TodoList(MutableState<std::vector<Todo>> todos) {
    Column([&] {
        for (const Todo& todo : todos.get()) {
            key(todo.id, [&] { TodoRow(todo, todos); });
        }
    });
}
```

Now each row's remembered values, effects and nodes follow its `id`. Lazy lists take keys in their `items` calls for the same reason (see [Lists and grids](lists.md)).

## Moving content

Calling the same composable from a different place in the tree is a different call: its state starts over. `movableContentOf` makes content whose state moves with it, as in Compose:

```cpp
void Player(bool wide) {
    auto video = remember([] { return movableContentOf([] {
        auto position = remember([] { return mutableStateOf(0); });   // kept when it moves
        Text("Playing at " + std::to_string(position.get()));
    }); });

    if (wide) {
        Row([&] { video(); Text("Comments"); });
    } else {
        Column([&] { video(); Text("Comments"); });
    }
}
```

When `wide` changes, the content's remembered values, nodes and running effects move from the column to the row instead of being made again.

## See also

- [State](state.md): `remember`, `rememberSaveable` and state holders.
- [Side effects](side-effects.md): effects that start when a composable enters and stop when it leaves.
- [Phases](phases.md): what happens after composition.
