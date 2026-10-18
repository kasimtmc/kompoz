# Kompoz documentation

Kompoz is a declarative UI library for C++20 in the style of Jetpack Compose, with Material 3 components, running on Linux, Windows, Android and the web from one codebase.

If you know Compose, you know Kompoz: the same way of thinking (composable functions, state, recomposition, modifiers), the same names where C++ allows, and Material 3 components that look and behave like Compose's. Kompoz 0.1.1 is licensed under the Apache License 2.0.

![The showcase's home screen](../../screenshots/home.png)

```cpp
void Counter() {
    auto count = remember([] { return mutableStateOf(0); });

    Column(Modifier().padding(16), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
        Text("Count: " + std::to_string(count.get()), {.font = typography().titleLarge});
        Button([count] { count.set(count.get() + 1); }, [] { Text("Add"); });
    });
}

Application kompoz::createApplication(int, char**) {
    return {.title = "Demo", .content = [] { MaterialTheme(lightColorScheme(), Counter); }};
}
```

## Get started

- [Setup](setup.md): build Kompoz, add it to your CMake project, and run on each platform.
- [Tutorial](tutorial.md): build a first app step by step, from a line of text to a Material list.
- [Coming from Compose](coming-from-compose.md): how Kotlin's Compose maps to C++, at a glance.

## Foundation

- [Thinking in Kompoz](thinking.md): declarative UI, composable functions and recomposition.
- [State](state.md): observable state, `remember`, `rememberSaveable`, hoisting and state holders.
- [Lifecycle and identity](lifecycle.md): how calls are identified without a compiler plugin, and what makes them skip.
- [Side effects](side-effects.md): coroutines, `LaunchedEffect`, `DisposableEffect` and the other effect APIs.
- [Phases](phases.md): composition, layout and drawing, and how to keep each one cheap.
- [CompositionLocal](composition-local.md): handing values down the tree, and the built-in locals.

## Design

- [Modifiers](modifiers.md): decorating composables, why order matters, and writing your own.
- [Layouts](layouts.md): `Column`, `Row`, `Box`, flow layouts, `FlexBox`, `Grid` and adaptive layouts.
- [Custom layouts](custom-layouts.md): `Layout`, constraints, intrinsics and `SubcomposeLayout`.
- [Lists and grids](lists.md): lazy lists and grids, pagers, carousels and nested scrolling.
- [Material components](material.md): a tour of the Material 3 components.
- [Theming](theming.md): color schemes, dark theme, dynamic color, typography and design systems.
- [Text](text.md): styling text, fonts, selection and text fields.
- [Graphics](graphics.md): `Canvas`, brushes, paths, shapes, shadows, images and vectors.
- [Animation](animation.md): animated values, visibility, content, transitions and shared elements.
- [Gestures and input](gestures.md): clicks, drags, pinches, the keyboard, focus and drag and drop.

## App

- [Architecture](architecture.md): unidirectional data flow, `ViewModel`s, flows and saved state.
- [Navigation](navigation.md): screens, the back stack, typed routes and deep links.
- [Resources](resources.md): assets, the library's texts in the system language, and platform services.
- [Accessibility](accessibility.md): semantics, screen readers, touch targets and the keyboard.
- [Testing](testing.md): headless UI tests with a virtual clock.
- [Tools](tools.md): previews, rebuilding on save, scripted screenshots and profiling.

## Reference

- [API status](api.md): every Compose API and its status in Kompoz, and the build log.

![The Components screen in a wide window, with a navigation rail](../../screenshots/components-wide.png)

![Dynamic 1](../../screenshots/dynamic-color-linux/d9.png)

![Dynamic 1](../../screenshots/dynamic-color-linux/d16_light.png)

To see [more](../../screenshots/dynamic-color-linux/)
