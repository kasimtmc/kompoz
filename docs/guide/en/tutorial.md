# Tutorial

Build a small app step by step: text, layout, state, a scrolling list and Material 3 styling. Each step adds a little to the one before.

This tutorial assumes you have built Kompoz once (see [Setup](setup.md)) and can run your own app with it. Every snippet is complete C++: copy it into your `main.cpp`.

## Step 1: a composable function

A Kompoz app is made of *composable functions*: plain C++ functions that describe part of the UI by calling other composables. `Text` is the simplest one.

```cpp
void Greeting(const std::string& name) {
    Text("Hello, " + name + "!");
}

Application kompoz::createApplication(int, char**) {
    return {.title = "Tutorial", .content = [] { Greeting("Kompoz"); }};
}
```

`createApplication` is the app's entry point: the platform backend owns `main` and calls it once. The `Application` it returns has the window's title, its size on the desktop, and `content`, the composable that fills the window.

There is no XML, no view objects and no `setText`: to change what is on the screen, you call composables with different data, and Kompoz updates the screen to match.

## Step 2: layout

Composables placed one after another are drawn on top of each other unless you say how to arrange them. `Column` puts its children in a column, `Row` in a row, `Box` on top of each other.

```cpp
struct Message {
    std::string author, body;
};

void MessageCard(const Message& msg) {
    Row(Modifier().padding(8), [&] {
        Icon(Icons::Filled::AccountCircle, "Profile picture", Modifier().size(40));
        Spacer(Modifier().width(8));
        Column([&] {
            Text(msg.author, {.font = typography().titleSmall});
            Spacer(Modifier().height(4));
            Text(msg.body, {.font = typography().bodyMedium});
        });
    });
}
```

The children go in a lambda, the last argument, like Compose's trailing lambda. Content lambdas run right away, while the parent is being composed, so capturing with `[&]` is safe there.

`Modifier` decorates a composable: size, padding, background, clicks and much more. Modifiers chain, and their order matters (`padding` then `background` is not the same as `background` then `padding`). See [Modifiers](modifiers.md).

## Step 3: state

A UI that never changes is not much of an app. Kompoz redraws a composable when a *state* it read changes. `mutableStateOf` makes one; `remember` keeps it across recompositions, so it isn't created anew each time the function runs.

```cpp
void Counter() {
    auto count = remember([] { return mutableStateOf(0); });

    Column(Modifier().padding(16), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
        Text("You clicked " + std::to_string(count.get()) + " times");
        Button([count] { count.set(count.get() + 1); }, [] { Text("Click me"); });
    });
}
```

- `count.get()` reads the state. Because `Counter` read it, `Counter` runs again when it changes.
- `count.set(...)` writes it, here from the button's `onClick`.
- `MutableState<T>` is a small handle you can copy. The click callback runs later, after composition, so it captures `count` **by value** (`[count]`), never by reference.

The second argument of `Column` is a style struct with designated initializers: C++'s answer to Compose's named parameters. See [State](state.md) for more.

## Step 4: a scrolling list

A `Column` composes and lays out every child, even those out of view. For long lists use `LazyColumn`: it composes only the items on the screen, plus a small margin, as you scroll.

```cpp
#! struct Message { std::string author, body; };
#! void MessageCard(const Message&) {}
void Conversation(const std::vector<Message>& messages) {
    LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& list) {
        list.items(messages, [](const Message& msg) { MessageCard(msg); });
    });
}
```

The lambda gets a `LazyListScope` that describes the items: `item` adds one, `items` a whole list. Item content is composed later, as each item comes into view, so the item lambda captures nothing by reference that might be gone by then. Here it only uses its argument.

When items can be added, removed or moved, give each a stable key so its state follows it:

```cpp body
#! struct Message { int id; std::string author, body; };
#! void MessageCard(const Message&) {}
#! std::vector<Message> messages;
LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& list) {
    list.items(messages, [](const Message& msg) { return msg.id; }, [](const Message& msg) { MessageCard(msg); });
});
```

See [Lists and grids](lists.md) for grids, pagers, sticky headers and scrolling from code.

## Step 5: Material 3

Kompoz implements Material 3, as Compose Material 3 does. `MaterialTheme` gives everything inside it a color scheme, a typography and shapes; `Scaffold` lays out a screen with its app bar, floating action button and snackbars.

```cpp
#! struct Message { std::string author, body; };
#! void Conversation(const std::vector<Message>&) {}
void ConversationScreen(const std::vector<Message>& messages) {
    Scaffold(Modifier(), {.topBar = [] { TopAppBar({.title = [] { Text("Messages"); }}); }},
             [&](PaddingValues padding) {
                 Box(Modifier().padding(padding), [&] { Conversation(messages); });
             });
}

Application kompoz::createApplication(int, char**) {
    return {.title = "Tutorial", .content = [] {
                static const std::vector<Message> messages = {{"Ada", "Hello!"}, {"Linus", "Hi Ada."}};
                bool dark = isSystemInDarkTheme();
                MaterialTheme(dark ? darkColorScheme() : lightColorScheme(), [] { ConversationScreen(messages); });
            }};
}
```

`Scaffold` passes its content the padding that keeps it clear of the bars and the system's insets (the status bar, a display cutout). `isSystemInDarkTheme()` follows the system's dark mode as it changes; on Android 12+ and on Linux desktops you can use the system's colors as well, with `dynamicLightColorScheme()`. See [Theming](theming.md).

## Step 6: a card that expands

Let's make each message a card that shows its whole text when clicked. The expanded flag is state that belongs to one card, so each card remembers its own.

```cpp
#! struct Message { std::string author, body; };
void MessageCard(const Message& msg) {
    auto expanded = remember([] { return mutableStateOf(false); });

    Card([expanded] { expanded.set(!expanded.get()); }, Modifier().fillMaxWidth().padding(8, 4), [&] {
        Column(Modifier().padding(16).animateContentSize(), [&] {
            Text(msg.author, {.font = typography().titleSmall, .color = colorScheme().primary});
            Spacer(Modifier().height(4));
            Text(msg.body, {.font = typography().bodyMedium, .maxLines = expanded.get() ? 0 : 1,
                            .overflow = TextOverflow::Ellipsis});
        });
    });
}
```

- `Card(onClick, modifier, content)` is a clickable card with Material's ripple.
- `colorScheme()` and `typography()` read the current theme's colors and type styles.
- `maxLines = 0` means no limit; with one line the text ends in an ellipsis.
- `animateContentSize()` animates the column's size when the text grows or shrinks.

Inside a lazy list, `remember` is per item: each card keeps its own `expanded`. With keys, it stays with the message when the list changes.

## Where to go next

- [Thinking in Kompoz](thinking.md): the ideas behind declarative UI and recomposition.
- [Coming from Compose](coming-from-compose.md): how Kotlin's Compose maps to C++.
- [Layouts](layouts.md), [Material components](material.md) and [Animation](animation.md) for building real screens.
- [Tools](tools.md): previews and rebuilding on save while you work.
