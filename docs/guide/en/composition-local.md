# CompositionLocal

A `CompositionLocal` hands a value down the composition tree without passing it through every function in between. The theme's colors, the text style and the layout direction reach every component this way.

## Reading a local

Every local has a value at every point in the tree: what the nearest provider above gives, or else its default. `current()` reads it:

```cpp
void Caption(const std::string& text) {
    Color color = LocalContentColor.current();          // the color text and icons use here
    Text(text, {.font = typography().labelSmall, .color = color});
}
```

Reading a local subscribes the running scope, like reading a state: when a provider above starts giving a different value, the scopes that read it recompose, even if everything between them was skipped.

The theme accessors are locals underneath: `colorScheme()` is `LocalColorScheme.current()`, `typography()` is `LocalTypography.current()`, `isSystemInDarkTheme()` reads `LocalSystemDarkTheme`.

## Providing a value

`CompositionLocalProvider` gives one or more locals a value for everything inside its content:

```cpp
void Warning(const std::string& message) {
    CompositionLocalProvider({LocalContentColor.provides(colorScheme().error)}, [&] {
        Row([&] {
            Icon(Icons::Filled::Info, "");   // tinted with the error color
            Text(message);                        // so is the text
        });
    });
}
```

The value only applies inside the content lambda; outside it, the local has its previous value. Providers nest: an inner one overrides an outer one for its own content.

`providesDefault(value)` gives a value only where no provider above has given one: a library's way of setting a default that an app can still override.

## Defining your own

A local is identified by its address, so define it once, as a global, with its default:

```cpp
struct Spacing {
    float small = 4, medium = 8, large = 16;
    friend bool operator==(const Spacing&, const Spacing&) = default;
};

inline const CompositionLocal<Spacing> LocalSpacing{Spacing{}};

void Card1(const std::string& title) {
    Column(Modifier().padding(LocalSpacing.current().large), [&] { Text(title); });
}

void Compact(const Content& content) {
    CompositionLocalProvider({LocalSpacing.provides(Spacing{2, 4, 8})}, content);
}
```

When there is no sensible default, make the local with `compositionLocalOf`, whose default is made only when it is read without a provider; it can throw to say that the local must be provided:

```cpp
struct User {
    std::string name;
    friend bool operator==(const User&, const User&) = default;
};

inline const auto LocalUser = compositionLocalOf<User>([]() -> User { throw std::logic_error("LocalUser was not provided"); });
```

`staticCompositionLocalOf` exists for code coming from Compose; in Kompoz both kinds recompose only the scopes that read the local.

Give a local's type `operator==`: when a provider gives an equal value, nothing recomposes.

## Built-in locals

| Local | What it holds |
|---|---|
| `LocalColorScheme`, `LocalTypography`, `LocalMotionScheme` | The Material theme (read with `colorScheme()`, `typography()`, `motionScheme()`). |
| `LocalContentColor` | The color for text and icons here; Material containers set it to their content color. |
| `LocalTextStyle` | The font `Text` uses when none is given. |
| `LocalLayoutDirection` | Left to right or right to left; from the system language at the root. |
| `LocalDensity` | The screen's density and the system's font scale. |
| `LocalWindowSize`, `LocalWindowInfo` | The window's size in dp, and whether it has focus. |
| `LocalSystemDarkTheme` | Whether the system is in dark mode (read with `isSystemInDarkTheme()`). |
| `LocalWindowInsets`, `LocalSystemInsets` | The system bars', cutout's and keyboard's insets. |
| `LocalViewConfiguration` | Touch slop, long press timeout and the platform's other gesture thresholds. |
| `LocalIndication` | What clickables draw when pressed: Material's ripple by default. |
| `LocalMinimumInteractiveComponentSize` | The smallest touch target components keep (48 dp). |
| `LocalTextSelectionColors` | The colors of selected text and its handles. |
| `LocalScrollbarStyle` | How scrollbars look. |
| `LocalStrings` | The library's own texts, in English or Turkish (see [Resources](resources.md)). |
| `LocalUiMediaScope` | The window's size, pointer and keyboard, for `mediaQuery`. |

## When to use one

A local is implicit: a function that reads one depends on something its signature doesn't show. Use them sparingly:

- **Good uses** are values that much of the tree needs and that rarely change: a theme, a design system's tokens, a text direction, the current user's settings.
- **Avoid them** for data only a few composables need, and for state that changes often. Pass those as parameters, so the dependency is visible and the composable is easy to preview and test.

A local isn't a replacement for state hoisting: it hands values down, not events up.

## Passing locals to another composition

Content composed elsewhere, such as in a popup, already gets the locals of the place it is called from. If you need to carry them by hand, `currentCompositionLocalContext()` captures the locals at a point and `CompositionLocalProvider` can provide them again.

## See also

- [Theming](theming.md): Material's theme is built on locals.
- [State](state.md): explicit data flow with parameters and hoisting.
