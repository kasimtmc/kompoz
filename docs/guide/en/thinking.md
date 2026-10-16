# Thinking in Kompoz

Kompoz is declarative: you describe what the UI should look like for the current data, and Kompoz updates the screen when the data changes. This page explains that model and the rules that come with it.

## The declarative idea

In an imperative toolkit you build a tree of widgets once and then change it as things happen: find the label, call `setText`, hide the button, add a row. Every change is code you write, and the more states a screen has, the easier it is to forget one transition and show something inconsistent.

In Kompoz you write functions that turn data into UI. When the data changes, the functions run again with the new data, and Kompoz works out what changed on the screen:

```cpp
void Inbox(int unread) {
    if (unread == 0) {
        Text("No new messages");
    } else {
        Text(std::to_string(unread) + " new messages", {.fontWeight = FontWeight::Bold});
    }
}
```

There is no step that "updates" the label: the function always describes the whole result, and the framework keeps the screen in step with it. This is the same model as Jetpack Compose, React or SwiftUI.

## Composable functions

A composable function is a C++ function that emits UI by calling other composables:

- It takes the data it shows as parameters.
- It returns nothing: it *emits* UI by calling `Text`, `Row`, `Button` and so on.
- It can be called from other composables, in loops and conditions, like any function.

```cpp
struct Contact {
    std::string name, phone;
};

void ContactList(const std::vector<Contact>& contacts) {
    Column([&] {
        for (const Contact& c : contacts) {
            Text(c.name, {.font = typography().bodyLarge});
            Text(c.phone, {.color = colorScheme().onSurfaceVariant});
        }
    });
}
```

Composables run while Kompoz *composes*: when the app starts, and again when something they read changes. Calling one outside composition (from a click handler, say) does nothing useful; change state instead and let composition call it.

## Recomposition

When data changes, Kompoz runs the composables that depend on it again: this is *recomposition*. It doesn't rebuild everything:

- A state remembers who read it. When it changes, only the composables that read it run again, not their parents.
- When a parent does run, a child whose arguments are equal to last time, and that read nothing that changed, is *skipped*: it doesn't run, and its part of the screen stays as it is.

```cpp
void NameTag(const std::string& name, CallSite site = CallSite::current()) {
    composable(site, name, [=] {   // skipped while `name` stays the same
        Text("Hello, " + name);
    });
}

void Screen() {
    auto clicks = remember([] { return mutableStateOf(0); });
    Column([&] {
        NameTag("Ada");                                   // not run again when clicks change
        Text("Clicks: " + std::to_string(clicks.get()));  // Screen read clicks
        Button([clicks] { clicks.set(clicks.get() + 1); }, [] { Text("Click"); });
    });
}
```

`Screen` read `clicks`, so it runs again on every click. `NameTag` is a scope of its own whose argument didn't change, so it is skipped. See [Lifecycle and identity](lifecycle.md) for how scopes and skipping work in C++.

## Rules that follow

Because Kompoz decides when and how often your composables run, a few rules keep the results right.

### Composables can run often

A composable may run on every frame of an animation. Keep its body fast: no file reading, no network calls, no heavy computation. Do expensive work once with `remember`, or outside composition in a coroutine (see [Side effects](side-effects.md)), and keep the result in state.

### Composables should be free of side effects

Running a composable should only describe UI. Writing a file, sending a request or changing a variable from inside its body happens again every time it recomposes, and in an order you don't control.

```cpp nocheck
void Bad(int& counter) {
    counter++;              // runs on every recomposition: don't
    Text(std::to_string(counter));
}
```

Effects belong in event handlers (`onClick` runs once per click) or in effect APIs such as `LaunchedEffect` and `DisposableEffect`, which run at defined times.

### Composables can be skipped

Don't rely on a composable running because its parent did. If it read no state that changed and its arguments are equal, it won't. Anything that must happen belongs in state, events or effects.

### Composables don't run in a fixed order

Write each composable so that it doesn't depend on another having run first. Sharing data through global variables written during composition breaks as soon as one of them is skipped.

## State drives the UI

Since the UI is a function of data, changing the UI means changing data. Kompoz watches *state*, values made with `mutableStateOf` and the other state holders, and recomposes what read it:

```cpp
void Toggle() {
    auto on = remember([] { return mutableStateOf(false); });
    Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
        Switch(on.get(), [on](bool v) { on.set(v); });
        Text(on.get() ? "On" : "Off");
    });
}
```

The data flows down as arguments, and events flow up as callbacks: `Switch` doesn't change anything itself, it tells its caller what the user did, and the caller decides what the state becomes. This *unidirectional data flow* is the basis of how Kompoz apps are structured. See [State](state.md) and [Architecture](architecture.md).

## See also

- [State](state.md): holding, remembering and hoisting state.
- [Lifecycle and identity](lifecycle.md): how composables enter and leave the composition, and what makes them skip.
- [Phases](phases.md): composition, layout and drawing.
