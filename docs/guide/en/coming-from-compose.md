# Coming from Compose

If you know Jetpack Compose, you already know Kompoz: the same model, the same names, and the same way of writing it. A composable is a `@Composable fun` in Compose's own syntax, written in a `.cpp` file:

```kotlin
@Composable
fun Greeting(name: String, modifier: Modifier = Modifier) {
    Text("Hello, $name!", modifier = modifier.padding(16.dp), style = MaterialTheme.typography.titleLarge)
}
```

Kompoz follows Compose's behavior closely, down to Material 3's measurements and Android's fling curve. What is new is only where the code lives: in C++ sources, next to C++.

## How it works

Compose's compiler plugin rewrites Kotlin before the Kotlin compiler sees it; Kompoz's compiler, kompozc, does the same for C++. A target given to `kompoz_compose` has its sources rewritten before the C++ compiler sees them:

```cmake
add_executable(app main.cpp screens.cpp)
target_link_libraries(app PRIVATE kompoz::app)
kompoz_compose(app)
```

In each source, a `@Composable fun` (and a `data class`) is Kotlin; everything else in the file is C++ and passes through as it is. The C++ compiler's messages point at your own lines. The sources of a target are one module, as Kotlin's: a composable of one file is called from another with named arguments, no header needed.

The app's entry point is C++, and so is anything else you write around your composables:

```kotlin continued
@Composable
fun App() {
    MaterialTheme(colorScheme = lightColorScheme()) {
        Greeting("Kompoz")
    }
}

kompoz::Application kompoz::createApplication(int, char**) {
    return {.title = "Hello", .content = [] { App(); }};
}
```

## What reads as in Kotlin

Arguments by name, in any order, defaults left out, a trailing lambda for the last one; for Kompoz's composables and for your own:

```kotlin
@Composable
fun Labeled(label: String, value: Int = 0, onClick: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text("$label: $value", modifier = Modifier.weight(1f))
        TextButton(onClick = onClick) { Text("More") }
    }
}

@Composable
fun Screen() {
    Column(modifier = Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Labeled(label = "Messages", value = 3) { println("more") }
        Labeled("Drafts", onClick = {})
    }
}
```

State is delegated as in Kotlin; a lambda's parameter is `it` or named; `if` and `when` give values:

```kotlin
@Composable
fun Counter() {
    var count by remember { mutableStateOf(0) }
    var name by remember { mutableStateOf("") }
    Column {
        Text(if (count == 0) "Not yet" else "Clicked $count times")
        Button(onClick = { count++ }) { Text("Click") }
        TextField(value = name, onValueChange = { name = it }, singleLine = true)
        Text(when {
            name.isBlank() -> "Who are you?"
            name.length < 3 -> "Hi!"
            else -> "Hello, ${name.trim()}"
        })
    }
}
```

Lists and lazy layouts, with Kotlin's standard functions (`listOf`, `filter`, `map`, `joinToString`, `forEach`…):

```kotlin
data class Mail(val from: String, val subject: String, val unread: Boolean = false)

@Composable
fun Inbox(mails: List<Mail>) {
    val unread = mails.filter { it.unread }
    LazyColumn(contentPadding = PaddingValues(16.dp)) {
        item { Text("${unread.size} unread", style = MaterialTheme.typography.titleMedium) }
        items(mails) { mail ->
            ListItem(
                headlineContent = { Text(mail.subject) },
                supportingContent = { Text(mail.from) },
            )
        }
    }
}
```

A `data class` has its `==`, its `toString()` and `copy(…)`; it is made with named arguments in any order (`Mail(subject = "Hi", from = "Ayşe")`).

Coroutines: a `LaunchedEffect` or a `launch` block is a coroutine, and `delay`, `animateScrollToItem` and the other suspending calls in it are awaited:

```kotlin
@Composable
fun Clock() {
    var seconds by remember { mutableStateOf(0) }
    LaunchedEffect(Unit) {
        while (true) {
            delay(1000)
            seconds++
        }
    }
    val list = rememberLazyListState()
    val scope = rememberCoroutineScope()
    Column {
        Text("$seconds s")
        Button(onClick = { scope.launch { list.animateScrollToItem(0) } }) { Text("Top") }
    }
}
```

The rest as you know it: `16.dp`, `14.sp`, modifier chains from `Modifier`, `Color.Red`, `MaterialTheme.colorScheme.primary`, `LocalContentColor.current`, `CompositionLocalProvider(LocalContentColor provides Color.Red) { … }`, `DisposableEffect(key) { …; onDispose { … } }`, `null`, `?.`, `?:` and `!!`, `for` over ranges (`0 until n`, `1..10`, `10 downTo 0 step 2`) and lists, string templates (numbers written as Kotlin writes them), `CardDefaults.cardColors(containerColor = …)`, `TextFieldDefaults.colors(…)` and the other colors objects.

## C++ around your composables

Kotlin's types are C++ ones in the translated code: `String` is `std::string`, `Int` `int`, `Float` `float`, `Boolean` `bool`, `List<T>` `std::vector<T>`, `T?` `std::optional<T>`, a function type `std::function` (`@Composable () -> Unit`: `Content`). Your own C++ types are used by their names, and a struct of yours is made with named arguments in any order, its fields set in the order they are declared:

```kotlin
struct Point {
    float x = 0;
    float y = 0;
};

float distance(Point a, Point b);

@Composable
fun Path(points: List<Point>) {
    val start = Point(y = 4f, x = 2f)
    Text("${points.size} points, the first ${distance(start, points.first())} away")
}
```

Your C++ functions and their members are called as they are, by position. C++ calls a composable as the function kompozc made of it: `Greeting` above is `void Greeting(const std::string& name, const Modifier& modifier = Modifier(), CallSite site = CallSite::current())`.

A Unit `@Composable fun` is restartable and skippable, as the compiler plugin makes it: it is skipped when its arguments compare equal (`==`) to last time. A `data class` has `==`; a C++ struct needs one (`bool operator==(const Point&) const = default;`), or its composable always runs again with its caller.

Lambdas capture by value, as Kotlin's capture: a block can run again later on its own, so it never holds a reference to something gone. A `var` changed inside a lambda changes the lambda's copy, not the variable: keep what changes in a state (`mutableStateOf`), as Compose wants it anyway.

## Not yet

kompozc reads the Kotlin a composable needs: composables, plain and extension functions, top-level `val`s, data classes, enum classes, classes (a ViewModel, a repository), `@Serializable` routes and objects. What it doesn't read yet is written in C++ next to it: `sealed` classes and `is`, interfaces and `object`s with members, `suspend fun`, a class's `by` properties, labels (`return@forEach`). Your editor's C++ tooling (clangd) doesn't read the Kotlin in a `.cpp` file either; the build's messages do point at its lines.

Without kompozc, Kompoz's composables are written in C++: [Writing in C++](cpp.md) shows how each of Kotlin's constructs reads there.

## What is different

- **Platforms.** Kompoz draws everything itself: on Android in a view of its own activity, in windows of its own on Linux, Windows and macOS, on a canvas on the web; there are no Android Views to interoperate with.
- **Identity.** A call's identity is where it is in the source and its place among its parent's calls, as the compiler plugin works it out. In loops whose items move, use `key` (see [Lifecycle](lifecycle.md)).
- **The API.** Most of Compose and Material 3 is there under the same names; [the API page](api.md) lists every Compose API with its status in Kompoz.
