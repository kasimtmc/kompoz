# Lifecycle and identity

A composable enters the composition, recomposes zero or more times, and leaves. This page explains that lifecycle, how Kompoz tells one call from another, and what makes a composable skip.

## The lifecycle of a composable

When Kompoz first runs your UI, it records every composable that was called in a tree: the *composition*. From then on, each call in that tree has a lifecycle:

1. **It enters the composition** the first time it is called. Its `remember` blocks compute their values, its effects are scheduled.
2. **It recomposes** whenever a state it read changes, or when its parent runs and its arguments have changed. Remembered values are kept.
3. **It leaves the composition** when it is no longer called: an `if` became false, a list item went away, the screen was navigated away from. Its remembered values are forgotten, its `DisposableEffect` clean-ups run, its coroutines are cancelled.

```kotlin
@Composable
fun Details(show: Boolean) {
    if (show) {
        // Enters when show becomes true, leaves when it becomes false.
        val opened = remember { mutableStateOf(0) }
        DisposableEffect(Unit) {
            // here: it entered
            onDispose { /* here: it left */ }
        }
        Text("Details")
    }
}
```

What enters and leaves is told after the composition, in the order Compose tells it. First, what left is forgotten, last composed first: `DisposableEffect` clean-ups, also those of effects whose key changed. Then what entered is remembered in the order it was composed: `DisposableEffect` and `LaunchedEffect` start. Then the `SideEffect`s run. A remembered value whose type derives from `RememberObserver` (a C++ type, next to your composables) is told too:

```kotlin
void connect();
void disconnect();

struct Connection : RememberObserver {
    void onRemembered() override { connect(); }
    void onForgotten() override { disconnect(); }
};

@Composable
fun Chat() {
    remember { Connection() }  // connected while Chat is in the composition
    Text("Chat")
}
```

A composable that leaves and comes back starts fresh: `remember` runs its lambda again. To keep a value across that, use `rememberSaveable` inside a `SaveableStateHolder` (navigation does this for its screens) or hoist the state above the `if`.

## Identity: where a call comes from

To know which remembered values belong to which call, Kompoz needs to identify each call across recompositions. Compose's compiler plugin gives each call a key from where it is in the source; kompozc does the same through C++'s `std::source_location`:

- Every `@Composable fun` becomes a C++ function with a last parameter `CallSite site = CallSite::current()`. The default is evaluated where the composable is *called*, so `site` is the file, line and column of the call; kompozc's `#line` keeps them your source's.
- Inside a parent, calls from the same site are told apart by their order: the first call from a site, the second call from it, and so on.
- `remember` takes a `CallSite` the same way, so two `remember` calls in different places are different values.

You never pass `site` yourself. Calling a composable from C++, the default does it too.

## Scopes

A Unit `@Composable fun` is a *restartable* and *skippable* scope, as in Compose:

- **Restartable**: when a state the function read changes, only it runs again, not its parent.
- **Skippable**: when its parent runs and its arguments compare equal to last time, it doesn't run.

A composable lambda is a scope of its own too: the content of `Box`, `Column`, `Row`, a `Scaffold`'s slots, your own `content: @Composable () -> Unit`. When only what the lambda read changes, the lambda alone runs again. Because it can run on its own later, it captures what it uses by value, as Kotlin's lambdas do.

```kotlin
@Composable
fun Section(title: String, content: @Composable () -> Unit) {
    Column(modifier = Modifier.padding(16.dp)) {
        Text(title, style = MaterialTheme.typography.titleMedium)
        content()
    }
}
```

A `@Composable fun` that returns a value (`rememberSomething(): T`) is not a scope, as in Compose: what it reads belongs to its caller.

## Skipping

A scope is skipped when its parent runs, its arguments compare equal to the previous call, and nothing it read has changed since. For that, arguments need `==`:

- Numbers, strings, enums and data classes compare by value; so do C++ structs with `operator==` (`= default` will do).
- A state compares by identity: the same state is equal even if its value changed. The scope still recomposes when the value changes, because it read it.
- A `Modifier` compares by the values of its elements.
- A function (a lambda passed as `onClick`, say) can't be compared. A composable that takes one is restartable but never skipped; so is one that takes a list of something without `==`.

```kotlin
data class Todo(val id: Int, val title: String, val done: Boolean = false)

@Composable
fun TodoRow(todo: Todo, todos: MutableState<List<Todo>>) {  // skipped while todo and todos are the same
    Row(verticalAlignment = Alignment.CenterVertically) {
        Checkbox(checked = todo.done, onCheckedChange = { done ->
            todos.value = todos.value.map { if (it.id == todo.id) it.copy(done = done) else it }
        })
        Text(todo.title)
    }
}
```

When one todo changes, the list recomposes, and every other `TodoRow` is skipped.

`Text` is skippable too. The components that lay out content run with their caller when it runs; that is cheap, because what they lay out only changes when their inputs do.

## Calls in loops

Inside a loop, every iteration calls from the same site, so the calls are identified by their order. That works while the list only grows at the end. When items are inserted, removed or reordered, state would stay with the *position*, not the item:

```kotlin nocheck
for (todo in todos.value) {
    TodoRow(todo, todos)   // insert at the front, and every row's remembered state shifts by one
}
```

`key` gives each call an identity of your choosing:

```kotlin
data class Todo(val id: Int, val title: String, val done: Boolean = false)

@Composable
fun TodoRow(todo: Todo, todos: MutableState<List<Todo>>) {
    Text(todo.title)
}

@Composable
fun TodoList(todos: MutableState<List<Todo>>) {
    Column {
        for (todo in todos.value) {
            key(todo.id) { TodoRow(todo, todos) }
        }
    }
}
```

Now each row's remembered values, effects and nodes follow its `id`. Lazy lists take keys in their `items` calls for the same reason (see [Lists and grids](lists.md)).

## Moving content

Calling the same composable from a different place in the tree is a different call: its state starts over. `movableContentOf` makes content whose state moves with it, as in Compose:

```kotlin
@Composable
fun Player(wide: Boolean) {
    val video = remember {
        movableContentOf {
            var position by remember { mutableStateOf(0) }   // kept when it moves
            Text("Playing at $position")
        }
    }

    if (wide) {
        Row { video(); Text("Comments") }
    } else {
        Column { video(); Text("Comments") }
    }
}
```

When `wide` changes, the content's remembered values, nodes and running effects move from the column to the row instead of being made again.

## See also

- [State](state.md): `remember`, `rememberSaveable` and state holders.
- [Side effects](side-effects.md): effects that start when a composable enters and stop when it leaves.
- [Phases](phases.md): what happens after composition.
- [Writing in C++](cpp.md): `composable(site, …)` and `ComposableScope`, the scopes kompozc writes, in C++ itself.
