# Coming from Compose

If you know Jetpack Compose, you already know Kompoz: the same model, the same names, and the same way of writing it. A composable is a `@Kompozable fun` in Kotlin's syntax, written in a `.cpp` file:

```kotlin
@Kompozable
fun Greeting(name: String, modifier: Modifier = Modifier) {
    Text("Hello, $name!", modifier = modifier.padding(16.dp), style = MaterialTheme.typography.titleLarge)
}
```

Where Compose's names carry its own name, Kompoz's carry Kompoz's: `@Kompozable` is Compose's `@Composable`; `KompozitionLocal`, `kompozitionLocalOf`, `staticKompozitionLocalOf` and `KompozitionLocalProvider` are its `CompositionLocal` family; `SubkompozLayout` (with `subkompoz(…)`) is its `SubcomposeLayout`; `KompozTestRule` is its test rule; in a `NavHost`, `kompozable(route) { }` and `kompozable<Route> { }` are navigation's `composable`. Every other name is Compose's.

Kompoz follows Compose's behavior closely, down to Material 3's measurements and Android's fling curve. What is new is only where the code lives: in C++ sources, next to C++.

## How it works

Compose's compiler plugin rewrites Kotlin before the Kotlin compiler sees it; Kompoz's compiler, kompozc, does the same for C++. A target given to `kompozable` has its sources rewritten before the C++ compiler sees them:

```cmake
add_executable(app main.cpp screens.cpp)
target_link_libraries(app PRIVATE kompoz::app)
kompozable(app)
```

In each source, a `@Kompozable fun` (and a `data class`) is Kotlin; everything else in the file is C++ and passes through as it is. The C++ compiler's messages point at your own lines. The sources of a target are one module, as Kotlin's: a composable of one file is called from another with named arguments, no header needed.

The app's entry point is C++, and so is anything else you write around your composables:

```kotlin continued
@Kompozable
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
@Kompozable
fun Labeled(label: String, value: Int = 0, onClick: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text("$label: $value", modifier = Modifier.weight(1f))
        TextButton(onClick = onClick) { Text("More") }
    }
}

@Kompozable
fun Screen() {
    Column(modifier = Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Labeled(label = "Messages", value = 3) { println("more") }
        Labeled("Drafts", onClick = {})
    }
}
```

State is delegated as in Kotlin; a lambda's parameter is `it` or named; `if` and `when` give values:

```kotlin
@Kompozable
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

@Kompozable
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

A `sealed` class or interface is a closed set of cases, a screen's state for one; `is` tells which it holds, and inside a `when` branch or past an `is` in an `if` the value is that case (a smart cast). A `when` over all the cases needs no `else`; `as` and `as?` cast as Kotlin's do:

```kotlin
sealed interface UiState {
    data object Loading : UiState
    data class Ready(val items: List<String>) : UiState
    data class Failed(val message: String) : UiState
}

@Kompozable
fun Screen(state: UiState) {
    when (state) {
        is UiState.Loading -> CircularProgressIndicator()
        is UiState.Ready -> LazyColumn { items(state.items) { Text(it) } }
        is UiState.Failed -> Text("Error: ${state.message}")
    }
}
```

An interface is a type that others implement: a class, a `ViewModel`, a data class, an object, an `object : Name { … }` expression. Its functions and properties without a body are abstract; a value of it is the object it was made from (a copy is the same one), and `is` and `as` tell which. A `fun interface` is made from a lambda too (`Check { it.isNotEmpty() }`):

```kotlin
interface Repository {
    val name: String
    fun items(): List<String>
    fun describe() = "$name: ${items().size}"
}

class FakeRepository(override val name: String = "fake") : Repository {
    override fun items() = listOf("a", "b")
}

@Kompozable
fun Items(repo: Repository) {
    Text(repo.describe())
}
```

A class's property can be delegated, as a composable's local is, and have its own `get()` and `set(value)`. `var count by mutableStateOf(0)` with a `private set` is a state the class changes and everyone reads; `by derivedStateOf { … }` and `by lazy { … }` work as in Kotlin:

```kotlin
class CounterViewModel : ViewModel() {
    var count by mutableStateOf(0)
        private set
    val isEven get() = count % 2 == 0

    fun increment() { count++ }
}
```

Classes can be open or abstract and derive from one another (`override`, `super.describe()`); a data class can have a body (functions, properties computed from its fields, `init`), its `==`, `toString()` and `copy` its fields' as in Kotlin; and classes, data classes, interfaces and functions can be generic, their type arguments written or taken from the values:

```kotlin
abstract class Tile(val name: String) {
    abstract fun area(): Float
    open fun describe() = "$name: ${area()}"
}

class SquareTile(val side: Float) : Tile("square") {
    override fun area() = side * side
}

data class Tagged<T>(val tag: String, val value: T) {
    fun show() = "$tag=$value"
}

fun <T, R> mapAll(items: List<T>, transform: (T) -> R): List<R> = items.map { transform(it) }

@Kompozable
fun Tiles() {
    Text(SquareTile(2f).describe() + " " + Tagged("n", 3).show() + " " + mapAll(listOf(1, 2)) { it * 10 })
}
```

Enum classes with a constructor and a body, `companion object`s, `operator fun`s, extension properties, `typealias`, `vararg` and `lateinit` read as in Kotlin, and so do classes nested in classes:

```kotlin
enum class Tier(val rank: Int) {
    LOW(1), HIGH(9);
    fun next() = if (this == LOW) HIGH else LOW
}

data class Coins(val cents: Int) {
    operator fun plus(other: Coins) = Coins(cents + other.cents)
    companion object {
        val Zero = Coins(0)
    }
}

val Coins.lira get() = cents / 100

@Kompozable
fun Wallet() {
    val total = Coins(150) + Coins(250)
    Text("${Tier.LOW.next()} ${total.lira} ${Coins.Zero} ${Tier.entries.size}")
}
```

`try` is an expression, `throw` too, with Kotlin's exceptions and your own classes deriving from them; functions may share a name (overloads); an `inner class` holds its outer object; an enum entry may have a body of its own:

```kotlin
class Refused(val code: Int) : IllegalStateException("refused $code")

fun parsed(text: String): Int = try { text.toInt() } catch (e: NumberFormatException) { -1 }
fun parsed(text: String, fallback: Int): Int = if (parsed(text) < 0) fallback else parsed(text)
fun checked(n: Int): Int = if (n >= 0) n else throw Refused(n)

enum class Op {
    PLUS { override fun apply(a: Int, b: Int) = a + b },
    TIMES { override fun apply(a: Int, b: Int) = a * b };
    abstract fun apply(a: Int, b: Int): Int
}

class Basket {
    var count by mutableStateOf(0)
    inner class Adder(val step: Int) {
        fun add() { count += step }
    }
}

@Kompozable
fun Checkout() {
    val basket = remember { Basket() }
    val code = try { checked(-3) } catch (e: Refused) { e.code }
    Text("${parsed("12")} ${parsed("x", 0)} ${Op.TIMES.apply(3, 4)} $code ${basket.count}")
    Button(onClick = { basket.Adder(2).add() }) { Text("Add") }
}
```

A sealed class may have a constructor and members, which its cases give values to and have; a top-level `var`, delegated too, reads as in Kotlin:

```kotlin
sealed class Screen(val route: String) {
    object Home : Screen("home")
    data class Detail(val id: Int) : Screen("detail/$id")
    fun label() = route.uppercase()
}

var opened by mutableStateOf(0)

@Kompozable
fun Router(screen: Screen) {
    Text("${screen.label()} $opened")
    Button(onClick = { opened++ }) { Text("Open") }
}
```

Coroutines: a `LaunchedEffect` or a `launch` block is a coroutine, and `delay`, `animateScrollToItem`, your `suspend fun`s and the other suspending calls in it are awaited:

```kotlin
@Kompozable
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

The rest as you know it: `16.dp`, `14.sp`, modifier chains from `Modifier`, `Color.Red`, `MaterialTheme.colorScheme.primary`, `LocalContentColor.current`, `KompozitionLocalProvider(LocalContentColor provides Color.Red) { … }`, `DisposableEffect(key) { …; onDispose { … } }`, `null`, `?.`, `?:` and `!!`, `for` over ranges (`0 until n`, `1..10`, `10 downTo 0 step 2`) and lists, string templates (numbers written as Kotlin writes them), `CardDefaults.cardColors(containerColor = …)`, `TextFieldDefaults.colors(…)` and the other colors objects.

## C++ around your composables

Kotlin's types are C++ ones in the translated code: `String` is `std::string`, `Int` `int`, `Float` `float`, `Boolean` `bool`, `List<T>` `std::vector<T>`, `T?` `std::optional<T>`, a function type `std::function` (`@Kompozable () -> Unit`: `Content`). Your own C++ types are used by their names, and a struct of yours is made with named arguments in any order, its fields set in the order they are declared:

```kotlin
struct Point {
    float x = 0;
    float y = 0;
};

float distance(Point a, Point b);

@Kompozable
fun Path(points: List<Point>) {
    val start = Point(y = 4f, x = 2f)
    Text("${points.size} points, the first ${distance(start, points.first())} away")
}
```

Your C++ functions and their members are called as they are, by position. C++ calls a composable as the function kompozc made of it: `Greeting` above is `void Greeting(const std::string& name, const Modifier& modifier = Modifier(), CallSite site = CallSite::current())`.

A Unit `@Kompozable fun` is restartable and skippable, as the compiler plugin makes it: it is skipped when its arguments compare equal (`==`) to last time. A `data class` has `==`; a C++ struct needs one (`bool operator==(const Point&) const = default;`), or its composable always runs again with its caller.

Lambdas capture by value, as Kotlin's capture: a block can run again later on its own, so it never holds a reference to something gone. A `var` changed inside a lambda changes the lambda's copy, not the variable: keep what changes in a state (`mutableStateOf`), as Compose wants it anyway.

Expressions are evaluated left to right, as Kotlin evaluates them: a call's arguments in the order they are written (named ones too), a string template's parts, an operator's operands. C++ leaves those unordered; kompozc orders them where it can matter, `listOf(next(), next())` giving the first `next()` first.

## Not yet

kompozc reads the Kotlin a composable needs: composables, plain, extension, generic, `operator` and `suspend` functions with `vararg`s (overloads too), top-level `val`s and `var`s (delegated ones too: `by lazy`, `by mutableStateOf`) and extension properties, `typealias`, data classes (with a body too), enum classes (with a constructor and a body too, their entries' own too), classes (open, abstract, generic; a ViewModel, a repository) with delegated, `lateinit` and accessor properties and a `companion object`, interfaces (generic too, `fun interface`s with a `suspend fun` too), `sealed` classes and interfaces (with a constructor and members, their cases data classes, objects or classes), `@Serializable` routes, objects, with members too (`object AppIcons { val Add = … }`: an app's singletons), those declarations nested in classes and inner classes, `try` and `throw` as expressions with Kotlin's exceptions. What it doesn't read yet is written in C++ next to it: an inner class of a generic data class, an inner class deriving from a C++ class. Editors read such files through kompozc's language server (see [Tools](tools.md#editors)); the build's messages point at the Kotlin's lines too.

Without kompozc, Kompoz's composables are written in C++: [Writing in C++](cpp.md) shows how each of Kotlin's constructs reads there.

## What is different

- **Platforms.** Kompoz draws everything itself: on Android in a view of its own activity, in windows of its own on Linux, Windows and macOS, on a canvas on the web; there are no Android Views to interoperate with.
- **Identity.** A call's identity is where it is in the source and its place among its parent's calls, as the compiler plugin works it out. In loops whose items move, use `key` (see [Lifecycle](lifecycle.md)).
- **The API.** Most of Compose and Material 3 is there under the same names; [the API page](api.md) lists every Compose API with its status in Kompoz.
