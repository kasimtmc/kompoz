# Testing

UI tests that run without a window, as Compose's `createComposeRule()`: `KompozTestRule { … }` composes a composable, find nodes, act on them, and check the result, with a virtual clock that makes animations and coroutines deterministic.

## A first test

`KompozTestRule` composes, lays out and draws its content headlessly, with a text measurer of fixed sizes. Find nodes by text, content description or test tag, act on them, and assert:

```kotlin
@Kompozable
fun Counter() {
    var count by remember { mutableStateOf(0) }
    Column {
        Text("Count: $count")
        Button(onClick = { count++ }) { Text("Add") }
    }
}

@Test
fun counterIncrements() {
    val rule = KompozTestRule { Counter() }
    rule.onNodeWithText("Count: 0").assertIsDisplayed()
    rule.onNodeWithText("Add").performClick()
    rule.onNodeWithText("Count: 1").assertIsDisplayed()
}
```

A failed assertion throws `AssertionError` (`kompoz::testing::AssertionError`), saying what it looked for and what it found. Use any test framework, or none: the rule works in a plain function; `@Test` marks one for yours.

## Setting up a test target

Tests need only the core library, no window system:

```cmake
add_executable(myapp_tests tests/main.cpp src/screens.cpp)
target_link_libraries(myapp_tests PRIVATE kompoz_core)
add_test(NAME myapp_tests COMMAND myapp_tests)
```

Keep your composables in source files the app and the tests share. Build Kompoz with `-DKOMPOZ_BUILD_APP=OFF` on a build server that has no window system's headers.

## Finding nodes

| Finder | Finds |
|---|---|
| `onNodeWithText(text)` | A node whose text is `text`; `onNodeWithText(text, true)` for a substring. |
| `onNodeWithContentDescription(d)` | A node with that content description: icons, images. |
| `onNodeWithTag(tag)` | A node with `Modifier.testTag(tag)`. |
| `onAllNodesWithText(text)`, `onAllNodesWithContentDescription(d)`, `onAllNodesWithTag(tag)` | All of them, as a collection: `assertCountEquals(n)`, `[i]`, `size()`. |

As in Compose, finders use merged semantics: a text inside a clickable answers for that clickable, so `onNodeWithText("Add").performClick()` clicks the button and `assertIsEnabled()` checks it.

A test tag is the most robust way to find something whose text changes or is translated:

```kotlin
@Kompozable
fun Search() {
    val query = rememberTextFieldState()
    OutlinedTextField(state = query, modifier = Modifier.testTag("search"), label = { Text("Search") })
}

@Test
fun typingFillsTheField() {
    val rule = KompozTestRule { Search() }
    rule.onNodeWithTag("search").performTextInput("kompoz")
    rule.onNodeWithText("kompoz").assertExists()
}
```

## Actions

| Action | Does |
|---|---|
| `performClick()` | Taps the node's center. |
| `performTextInput(text)` | Focuses the node and types. |
| `performKey(Key.Enter)` | Presses a key. |
| `performScrollTo()` | Scrolls the scrollables around the node to show it. |
| `rule.swipe(from, to, frames)` | A drag across the window, a frame per step: fast enough to fling. |

Each action waits for the UI to settle afterwards.

## Assertions

`assertExists`, `assertDoesNotExist`, `assertIsDisplayed` (in the window, not clipped away), `assertIsNotDisplayed`, `assertTextEquals`, `assertTextContains`, `assertContentDescriptionEquals`, `assertHasClickAction`, `assertIsEnabled`, `assertIsNotEnabled`, `assertIsSelected` (a selected tab, a ticked checkbox, a switch on), `assertIsOn`, `assertIsOff`. They return the node, so they chain:

```kotlin
#! @Kompozable fun Form() {}
@Test
fun saves() {
    val rule = KompozTestRule { Form() }
    rule.onNodeWithText("Save").assertIsDisplayed().assertIsEnabled().performClick()
}
```

For anything else, a node gives its `bounds()` (in dp, in the window) and `text()`, and `exists()`. `rule.drawnTexts()` lists the texts that were actually drawn, for checking what reached the screen.

## Time

The rule's clock is virtual: time moves only when the test says so, so animations, delays and coroutines give the same result on every run.

- `rule.waitForIdle()` runs frames until nothing is pending: recomposition, layout, animations on their way (read while drawing too), and timers that are due within a couple of seconds.
- `rule.advanceTimeBy(ms)` moves the clock forward, running the frames in between.
- `rule.advanceFrame()` runs one frame; `rule.mainClock.advanceTimeBy(ms)` is Compose's name for `rule.advanceTimeBy(ms)`.

```kotlin
@Kompozable
fun Toast(show: Boolean) {
    var visible by remember { mutableStateOf(false) }
    LaunchedEffect(show) {
        if (show) {
            visible = true
            delay(2000)
            visible = false
        }
    }
    if (visible) Text("Saved")
}

@Test
fun toastGoesAwayAfterTwoSeconds() {
    val rule = KompozTestRule { Toast(true) }
    rule.onNodeWithText("Saved").assertExists()
    rule.mainClock.advanceTimeBy(2100)
    rule.onNodeWithText("Saved").assertDoesNotExist()
}
```

Work given to `withBackground` runs on real threads; `waitForIdle` waits for it to finish.

## Changing what the test shows

The content lambda can read state that the test owns, so the test can change it between steps:

```kotlin
@Test
fun showsTheNewName() {
    val name = mutableStateOf("Ada")
    val rule = KompozTestRule { Text("Hello, ${name.value}") }
    rule.onNodeWithText("Hello, Ada").assertExists()
    name.value = "Grace"
    rule.waitForIdle()
    rule.onNodeWithText("Hello, Grace").assertExists()
}
```

`rule.host()` gives the host, to change what the platform would: `setSystemDarkTheme(true)`, `setLayoutDirection(LayoutDirection.Rtl)`, `setStrings(Strings.turkish())`. The second argument of the rule's constructor is the window's size in dp, `{400, 800}` by default.

## What to test

- **Stateless composables** are the easiest: pass data in, check what is shown, check that events call back. Hoisting state (see [State](state.md#state-hoisting)) makes most of your UI testable this way.
- **ViewModels** and other logic are plain C++ objects: test them without a rule at all.
- **Screens** together with their `ViewModel`s: the rule composes them as the app would.

## Screenshots

For checking what a screen looks like, run the real app headless with a script and compare the pictures it saves; see [Tools](tools.md#scripted-runs-and-screenshots).

## See also

- [Accessibility](accessibility.md): the semantics finders use.
- [Architecture](architecture.md): structuring code so it is easy to test.
- [Tools](tools.md): scripted runs of the real app.
