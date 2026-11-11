# Tutorial

Build a small app step by step: text, layout, state, a scrolling list and Material 3 styling. Each step adds a little to the one before.

This tutorial assumes you have built Kompoz once and added it to your CMake project with `kompozable` (see [Setup](setup.md)). Every snippet is complete: copy it into your `main.cpp`, after `#include <kompoz/kompoz.hpp>` and `using namespace kompoz;`.

## Step 1: a composable function

A Kompoz app is made of *composable functions*: functions marked `@Kompozable` that describe part of the UI by calling other composables. They are written as in Kotlin, in your `.cpp` file. `Text` is the simplest one.

```kotlin
@Kompozable
fun Greeting(name: String) {
    Text("Hello, $name!")
}

kompoz::Application kompoz::createApplication(int, char**) {
    return {.title = "Tutorial", .content = [] { Greeting("Kompoz"); }};
}
```

`createApplication` is the app's entry point, in C++: the platform backend owns `main` and calls it once. The `Application` it returns has the window's title, its size on the desktop, and `content`, the composable that fills the window.

There is no XML, no view objects and no `setText`: to change what is on the screen, you call composables with different data, and Kompoz updates the screen to match.

## Step 2: layout

Composables placed one after another are drawn on top of each other unless you say how to arrange them. `Column` puts its children in a column, `Row` in a row, `Box` on top of each other.

```kotlin
data class Message(val author: String, val body: String)

@Kompozable
fun MessageCard(msg: Message) {
    Row(modifier = Modifier.padding(8.dp)) {
        Icon(Icons.Filled.AccountCircle, contentDescription = "Profile picture", modifier = Modifier.size(40.dp))
        Spacer(Modifier.width(8.dp))
        Column {
            Text(msg.author, style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(4.dp))
            Text(msg.body, style = MaterialTheme.typography.bodyMedium)
        }
    }
}
```

The children go in the trailing lambda, as in Compose. A lambda can run again later on its own, so it captures what it uses by value, as Kotlin's do.

`Modifier` decorates a composable: size, padding, background, clicks and much more. Modifiers chain, and their order matters (`padding` then `background` is not the same as `background` then `padding`). See [Modifiers](modifiers.md).

## Step 3: state

A UI that never changes is not much of an app. Kompoz redraws a composable when a *state* it read changes. `mutableStateOf` makes one; `remember` keeps it across recompositions, so it isn't created anew each time the function runs.

```kotlin
@Kompozable
fun Counter() {
    var count by remember { mutableStateOf(0) }

    Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("You clicked $count times")
        Button(onClick = { count++ }) { Text("Click me") }
    }
}
```

- Reading `count` reads the state. Because `Counter` read it, `Counter` runs again when it changes.
- `count++` writes it, here from the button's `onClick`.
- `by` delegates `count` to the state, as in Kotlin: it reads and writes the state's value.

See [State](state.md) for more.

## Step 4: a scrolling list

A `Column` composes and lays out every child, even those out of view. For long lists use `LazyColumn`: it composes only the items on the screen, plus a small margin, as you scroll.

```kotlin
data class Message(val author: String, val body: String)

@Kompozable
fun MessageCard(msg: Message) {
    Text("${msg.author}: ${msg.body}")
}

@Kompozable
fun Conversation(messages: List<Message>) {
    LazyColumn(modifier = Modifier.fillMaxSize()) {
        items(messages) { message ->
            MessageCard(message)
        }
    }
}
```

The block describes the items, as `LazyColumn`'s receiver does in Compose: `item` adds one, `items` a whole list. Item content is composed later, as each item comes into view.

When items can be added, removed or moved, give each a stable key so its state follows it:

```kotlin
data class Message(val id: Int, val author: String, val body: String)

@Kompozable
fun Conversation(messages: List<Message>) {
    LazyColumn(modifier = Modifier.fillMaxSize()) {
        items(messages, key = { it.id }) { message ->
            Text("${message.author}: ${message.body}")
        }
    }
}
```

See [Lists and grids](lists.md) for grids, pagers, sticky headers and scrolling from code.

## Step 5: Material 3

Kompoz implements Material 3, as Compose Material 3 does. `MaterialTheme` gives everything inside it a color scheme, a typography and shapes; `Scaffold` lays out a screen with its app bar, floating action button and snackbars.

```kotlin
data class Message(val author: String, val body: String)

@Kompozable
fun Conversation(messages: List<Message>) {
    LazyColumn { items(messages) { Text("${it.author}: ${it.body}") } }
}

@Kompozable
fun ConversationScreen(messages: List<Message>) {
    Scaffold(topBar = { TopAppBar(title = { Text("Messages") }) }) { padding ->
        Box(modifier = Modifier.padding(padding)) { Conversation(messages) }
    }
}

@Kompozable
fun TutorialApp() {
    val messages = remember { listOf(Message("Ada", "Hello!"), Message("Linus", "Hi Ada.")) }
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) darkColorScheme() else lightColorScheme()) {
        ConversationScreen(messages)
    }
}

kompoz::Application kompoz::createApplication(int, char**) {
    return {.title = "Tutorial", .content = [] { TutorialApp(); }};
}
```

`Scaffold` passes its content the padding that keeps it clear of the bars and the system's insets (the status bar, a display cutout). `isSystemInDarkTheme()` follows the system's dark mode as it changes; on Android 12+ and on Linux desktops you can use the system's colors as well, with `dynamicLightColorScheme()`. See [Theming](theming.md).

## Step 6: a card that expands

Let's make each message a card that shows its whole text when clicked. The expanded flag is state that belongs to one card, so each card remembers its own.

```kotlin
data class Message(val author: String, val body: String)

@Kompozable
fun MessageCard(msg: Message) {
    var expanded by remember { mutableStateOf(false) }

    Card(onClick = { expanded = !expanded }, modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp)) {
        Column(modifier = Modifier.padding(16.dp).animateContentSize()) {
            Text(msg.author, color = MaterialTheme.colorScheme.primary, style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(4.dp))
            Text(
                msg.body,
                maxLines = if (expanded) Int.MAX_VALUE else 1,
                overflow = TextOverflow.Ellipsis,
                style = MaterialTheme.typography.bodyMedium,
            )
        }
    }
}
```

- `Card(onClick = …)` is a clickable card with Material's ripple.
- `MaterialTheme.colorScheme` and `MaterialTheme.typography` read the current theme's colors and type styles.
- With one line, the text ends in an ellipsis; `Int.MAX_VALUE` lifts the limit.
- `animateContentSize()` animates the column's size when the text grows or shrinks.

Inside a lazy list, `remember` is per item: each card keeps its own `expanded`. With keys, it stays with the message when the list changes.

## Where to go next

- [Thinking in Kompoz](thinking.md): the ideas behind declarative UI and recomposition.
- [Coming from Compose](coming-from-compose.md): what kompozc reads, and the C++ around your composables.
- [Layouts](layouts.md), [Material components](material.md) and [Animation](animation.md) for building real screens.
- [Tools](tools.md): previews and rebuilding on save while you work.
