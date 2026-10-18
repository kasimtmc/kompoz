# Changelog

**English** · [Türkçe](CHANGELOG.tr.md)

## 0.1.0 — 2026-10-16

The first public release. Kompoz brings Jetpack Compose's model and API to C++20 and runs on Linux, Windows, Android
and the web.

- **Runtime**: composable functions with smart recomposition and skipping, `remember`/`rememberSaveable` (restored
  after Android ends the process), effects, `CompositionLocal`, C++20 coroutines, `Flow`/`StateFlow`/`SharedFlow`,
  `ViewModel` with `SavedStateHandle`, lifecycle, navigation with type-safe routes, nested graphs and deep links.
- **Layout**: incremental measuring, `Box`/`Row`/`Column`, `FlowRow`/`FlowColumn`, `FlexBox`, `Grid`, `Layout`,
  `SubcomposeLayout`, `LookaheadScope`, intrinsics, window insets, right-to-left layouts.
- **Lists and scrolling**: lazy lists, grids and staggered grids, pagers, carousels, nested scroll, Android's fling
  curve, overscroll stretch, snapping, pull to refresh, the mouse wheel.
- **Material 3** (Compose Material 3 1.4 with the Expressive components) and `MaterialTheme` with dynamic color from
  the source the app or its user picks: the system's palette (the wallpaper's on Android 12+, the desktop's accent
  color on Linux), the wallpaper itself (Linux desktops, Windows, Android 8.1+), any picture
  (`TonalPalette::fromImage`) or any color (`TonalPalette::fromSeed`).
- **Drawing and text**: `Canvas`, brushes, paths, shapes (Material's shape library and morphing), blurred shadows,
  `graphicsLayer`, vectors, images loaded over HTTP, HarfBuzz shaping, bidirectional text, selection, text fields
  with undo, autofill and hardware keyboard editing.
- **Input and accessibility**: touch and multi-touch gestures, mouse and keyboard, drag and drop, the predictive back
  gesture; a semantics tree for TalkBack.
- **Localization**: the library's own texts (context menus, accessibility descriptions, the pickers) in English or
  Turkish by the system language (`LocalStrings`, `KOMPOZ_LANGUAGE`); an app can provide its own.
- **Tools**: headless UI tests, previews, `scripts/dev.sh` (rebuild and restart on save, keeping the state),
  scripted screenshots, frame profiling, `scripts/package.sh` (packages for every platform).

What was built, step by step, is in [docs/components.md](docs/components.md)'s build log.
