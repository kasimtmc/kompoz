# Coming from Compose

If you know Jetpack Compose, you already know Kompoz: the same model, mostly the same names. This page shows how Kotlin's constructs map to C++.

Kompoz follows Compose's behavior closely, down to Material 3's measurements and Android's fling curve. What changes is the language: C++ has no compiler plugin, no named arguments and no trailing lambdas outside the parentheses, so each of those has a C++ form. Once you know the handful of patterns below, Compose's documentation applies almost word for word.

## Composable functions

In Kotlin, `@Composable` and the Compose compiler plugin give every call its identity. In C++ a composable is a plain function. Calling it from composition is enough:

```kotlin
@Composable
fun Greeting(name: String) {
    Text("Hello, $name!")
}
```

```cpp
void Greeting(const std::string& name) {
    Text("Hello, " + name + "!");
}
```

A plain function like this has no scope of its own: the state it reads belongs to whichever composable called it, and that composable runs again when the state changes. That is enough for most small helpers.

For a component that should recompose on its own and be skipped when its arguments haven't changed, as a Kotlin composable is, write its body with `composable` and take a `CallSite`:

```cpp
void Greeting(const std::string& name, CallSite site = CallSite::current()) {
    composable(site, name, [=] {
        Text("Hello, " + name + "!");
    });
}
```

- `CallSite` is `std::source_location`. Its default argument records where `Greeting` was called from; together with the call's position inside its parent, that is the call's identity, which is what the compiler plugin works out in Kotlin.
- The arguments before the lambda decide skipping: when they compare equal (`==`) to last time and nothing the body read has changed, the body doesn't run.
- The body captures by value (`[=]`), because it can run again later on its own, without its caller.

Composables that take a content lambda, like `Column` and `Card`, are Compose's *inline* composables: they use `ComposableScope scope(site);` for an identity without a scope, and the state read in their content belongs to the caller. See [Lifecycle and identity](lifecycle.md).

## Parameters

Kotlin's named and default parameters become C++ *options structs* with designated initializers:

```kotlin
Text("Title", fontSize = 24.sp, maxLines = 1)
OutlinedTextField(value, onValueChange, label = { Text("Email") }, singleLine = true)
```

```cpp body
#! std::string value;
#! std::function<void(std::string)> onValueChange;
Text("Title", Modifier(), {.fontSize = 24, .maxLines = 1});
OutlinedTextField(value, onValueChange, Modifier(), {.singleLine = true, .label = "Email"});
```

Designated initializers must follow the order the fields are declared in the struct. The headers list them in a sensible order, and your compiler tells you when you mix them up.

Most components take, in this order: the required arguments, the `Modifier`, the options struct, then the content lambda. Overloads leave out what you don't need: `Button(onClick, content)` and `Button(onClick, modifier, style, content)` both exist.

## Trailing lambdas

Kotlin's trailing lambda goes inside the parentheses as the last argument:

```kotlin
Column(Modifier.padding(16.dp)) {
    Text("One")
    Text("Two")
}
```

```cpp body
Column(Modifier().padding(16), [&] {
    Text("One");
    Text("Two");
});
```

Content lambdas run right away, during the call, so `[&]` is safe for them. Callbacks such as `onClick` run later, after composition, when the function that created them has returned: capture what they use **by value**.

```cpp
void LikeButton() {
    auto likes = remember([] { return mutableStateOf(0); });
    Button([likes] { likes.set(likes.get() + 1); }, [&] {   // the callback: by value
        Text(std::to_string(likes.get()) + " likes");        // the content: [&] is fine
    });
}
```

Lambdas that receive a scope, such as `LazyColumn`'s, take it as a parameter instead of as a receiver: `LazyColumn(modifier, [&](LazyListScope& list) { list.items(...); })`.

## State

```kotlin
var count by remember { mutableStateOf(0) }
count++
```

```cpp body
auto count = remember([] { return mutableStateOf(0); });
count.set(count.get() + 1);
```

C++ has no property delegates, so a state is read with `get()` and written with `set()`. `MutableState<T>` is a handle: copies share the same value, which is why callbacks can capture it by value. `update` changes the value in place:

```cpp body
auto todos = remember([] { return mutableStateOf(std::vector<std::string>{}); });
todos.update([](std::vector<std::string>& list) { list.push_back("Buy milk"); });
```

`remember(key, init)`, `rememberSaveable`, `derivedStateOf`, `mutableStateListOf`, `rememberUpdatedState` and `produceState` have their Compose names. See [State](state.md).

## Modifiers

```kotlin
Modifier.fillMaxWidth().padding(16.dp).background(Color.Red, RoundedCornerShape(8.dp))
```

```cpp body
Box(Modifier().fillMaxWidth().padding(16).background(Color::hex(0xE53935), RoundedCornerShape(8)));
```

A chain starts from `Modifier()`. Order matters as in Compose. See [Modifiers](modifiers.md).

## Units

Kotlin's `16.dp` is just `16`: every length is a `float` in dp. Font sizes are `TextUnit`, which converts from a number of sp (`.fontSize = 24`) or is written as `TextUnit::sp(24)` and `TextUnit::em(1.5f)`. Durations use `std::chrono`: `delay(300ms)`, `debounce(300ms)`. A `Color` is Compose's 64 bits: made from components from 0 to 1 in sRGB (`Color{1, 0, 0, 1}`), a hex value (`Color::hex(0xE53935)`, `Color::hex(0x000000, 0.5f)`), an ARGB one (`Color::argb(0xFFE53935)`, Compose's `Color(0xFFE53935)`) or in another color space (`Color(0.2f, 0.6f, 0.9f, 1, ColorSpaces::DisplayP3)`), read with `red()`, `green()`, `blue()` and `alpha()`; `colors::Black`, `White` and `Transparent` are named, and `color.copy(0.38f)` changes the alpha like Compose's `copy(alpha = …)`.

## Coroutines

Kotlin's coroutines become C++20 coroutines that return `Task`. They run on the UI thread, on the frame clock, as Compose's do.

```kotlin
LaunchedEffect(userId) {
    delay(1000)
    user = repository.load(userId)
}
val scope = rememberCoroutineScope()
Button(onClick = { scope.launch { listState.animateScrollToItem(0) } }) { Text("Top") }
```

```cpp body
#! int userId = 0;
using namespace std::chrono_literals;
auto user = remember([] { return mutableStateOf(std::string()); });
LaunchedEffect(userId, [user, userId]() -> Task {
    co_await delay(1000ms);
    user.set("user " + std::to_string(userId));
});
auto list = rememberLazyListState();
auto scope = rememberCoroutineScope();
Button([scope, list] { scope.launch([list]() -> Task { co_await list.animateScrollToItem(0); }); },
       [] { Text("Top"); });
```

`suspend` functions are coroutines you `co_await`. `Flow`, `StateFlow` and `SharedFlow` exist with their operators (`map`, `filter`, `debounce`, `flatMapLatest`, `stateIn`…), and `collectAsState()` reads one in composition. See [Side effects](side-effects.md) and [Architecture](architecture.md).

## Composition locals

```kotlin
val color = LocalContentColor.current
CompositionLocalProvider(LocalContentColor provides Color.Red) { … }
```

```cpp body
Color color = LocalContentColor.current();
CompositionLocalProvider({LocalContentColor.provides(Color::hex(0xE53935))}, [&] { Text("Red"); });
```

`MaterialTheme.colorScheme`, `MaterialTheme.typography` and `MaterialTheme.motionScheme` are `colorScheme()`, `typography()` and `motionScheme()`. There is no theme-wide `Shapes`: components have Material's shapes built in, and you pass your own where a component takes one (`RoundedCornerShape(12)`, `CircleShape`). See [CompositionLocal](composition-local.md).

## A quick reference

| Compose | Kompoz |
|---|---|
| `@Composable fun F()` | `void F()`, or `void F(CallSite site = CallSite::current())` with `composable(site, args…, [=] { … })` |
| `remember { x }`, `remember(key) { x }` | `remember([] { return x; })`, `remember(key, [] { return x; })` |
| `var v by mutableStateOf(0)` | `auto v = mutableStateOf(0);` `v.get()`, `v.set(1)` |
| `key(id) { … }` | `key(id, [&] { … })` |
| `Modifier.padding(8.dp)` | `Modifier().padding(8)` |
| Named arguments | Designated initializers: `{.maxLines = 1}` |
| `LaunchedEffect(k) { … }` | `LaunchedEffect(k, [=]() -> Task { … })` |
| `scope.launch { … }` | `scope.launch([=]() -> Task { … })` |
| `DisposableEffect(k) { onDispose { … } }` | `DisposableEffect(k, [=] { return [=] { … }; })` |
| `LocalX.current` | `LocalX.current()` |
| `MaterialTheme.colorScheme.primary` | `colorScheme().primary` |
| `LazyColumn { items(list) { … } }` | `LazyColumn(Modifier(), [&](LazyListScope& l) { l.items(list, [](const T& x) { … }); })` |
| `val vm: MyViewModel = viewModel()` | `auto vm = viewModel<MyViewModel>();` |
| `@Preview` | `KOMPOZ_PREVIEW(Name) { … }` |
| `createComposeRule()` | `kompoz::testing::ComposeTestRule` |

The README's longer table covers navigation, animation, gestures, text fields and more, and [the API page](api.md) lists every Compose API with its status in Kompoz.

## What is different

- **No compiler plugin.** Identity comes from call sites, so two calls on the same line are told apart by their order. In loops whose items move, use `key` (see [Lifecycle](lifecycle.md)).
- **No `by` delegation.** States are read with `get()` and written with `set()`.
- **Skipping needs `==`.** A composable is skipped only if its arguments compare equal. `std::function` can't be compared, so a composable taking one is restartable but never skipped.
- **Platforms.** Kompoz draws everything itself: on Android in a view of its own activity, in windows of its own on Linux, Windows and macOS, on a canvas on the web; there are no Android Views to interoperate with.
