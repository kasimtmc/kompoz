# CompositionLocal

A `CompositionLocal` hands a value down the composition tree without passing it through every function in between. The theme's colors, the text style and the layout direction reach every component this way.

## Reading a local

Every local has a value at every point in the tree: what the nearest provider above gives, or else its default. `current` reads it:

```kotlin
@Composable
fun Caption(text: String) {
    val tint = LocalContentColor.current   // the color text and icons use here
    Text(text, color = tint, style = MaterialTheme.typography.labelSmall)
}
```

Reading a local subscribes the running scope, like reading a state: when a provider above starts giving a different value, the scopes that read it recompose, even if everything between them was skipped.

The theme is read through locals too, as in Compose: `MaterialTheme.colorScheme` is `LocalColorScheme.current`, `MaterialTheme.typography` is `LocalTypography.current`, and `isSystemInDarkTheme()` reads `LocalSystemDarkTheme`.

## Providing a value

`CompositionLocalProvider` gives one or more locals a value for everything inside its content:

```kotlin
@Composable
fun Warning(message: String) {
    CompositionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.error) {
        Row {
            Icon(Icons.Filled.Info, contentDescription = null)   // tinted with the error color
            Text(message)                                        // so is the text
        }
    }
}
```

The value only applies inside the content lambda; outside it, the local has its previous value. Up to three locals can be given at once, `CompositionLocalProvider(LocalA provides a, LocalB provides b) { … }`. Providers nest: an inner one overrides an outer one for its own content.

`LocalX providesDefault value` gives a value only where no provider above has given one: a library's way of setting a default that an app can still override.

## Defining your own

A local is known by which one it is, so define it once, as a top-level `val`, with its default (kompozc makes such a `val` a constant of the namespace, defined before what uses it):

```kotlin
data class Spacing(val small: Dp = 4.dp, val medium: Dp = 8.dp, val large: Dp = 16.dp)

val LocalSpacing = staticCompositionLocalOf { Spacing() }

@Composable
fun Titled(title: String) {
    Column(modifier = Modifier.padding(LocalSpacing.current.large)) { Text(title) }
}

@Composable
fun Compact(content: @Composable () -> Unit) {
    CompositionLocalProvider(LocalSpacing provides Spacing(2.dp, 4.dp, 8.dp), content = content)
}
```

The default is made only when the local is read without a provider. When there is no sensible one, it can say so, as in Compose:

```kotlin
data class User(val name: String)

val LocalUser = compositionLocalOf<User> { error("LocalUser was not provided") }
```

`compositionLocalOf` and `staticCompositionLocalOf` are both there, as in Compose; in Kompoz both kinds recompose only the scopes that read the local.

A data class (or a C++ type with `operator==`) compares by value: when a provider gives an equal value, nothing recomposes.

## Built-in locals

| Local | What it holds |
|---|---|
| `LocalColorScheme`, `LocalTypography`, `LocalMotionScheme` | The Material theme (read with `MaterialTheme.colorScheme`, `.typography`, `.motionScheme`). |
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

Content composed elsewhere, such as in a popup, already gets the locals of the place it is called from. If you need to carry them by hand, `currentCompositionLocalContext` captures the locals at a point, and `CompositionLocalProvider(context) { … }` provides them again.

## See also

- [Theming](theming.md): Material's theme is built on locals.
- [State](state.md): explicit data flow with parameters and hoisting.
