# Kompoz

**English** · [Türkçe](README.tr.md)

A declarative, cross-platform UI library for C++20 in the style of Jetpack Compose: the same mental
model (composable functions, state, recomposition, modifiers), the same names where C++ allows, and
Material 3 components that look and behave like Compose's. It runs on Linux, Windows, Android and the
web from one code base.

Version 0.1.1. The API follows Compose closely, but it may still change.
Licensed under the [Apache License 2.0](LICENSE).

<p>
<img src="docs/screenshots/home.png" width="250" alt="The showcase's home screen, light theme">
<img src="docs/screenshots/material-dark.png" width="250" alt="The Material screen, dark theme">
</p>
<img src="docs/screenshots/components-wide.png" width="760" alt="The components screen in a wide window, with a navigation rail">
<img src="docs/screenshots/dynamic-color-linux/d9.png" width="760" alt="Dynamic Color implementation example 1">
<img src="docs/screenshots/dynamic-color-linux/d4.png" width="760" alt="Dynamic Color implementation example 2">
<img src="docs/screenshots/dynamic-color-linux/d16.png" width="760" alt="Dynamic Color implementation example 3">

```cpp
#include <kompoz/kompoz.hpp>
using namespace kompoz;

void Counter() {
    auto count = remember([] { return mutableStateOf(0); });

    Column(Modifier().padding(16), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
        Text("Count: " + std::to_string(count.get()), {.font = typography().titleLarge});
        Button([count] { count.set(count.get() + 1); }, [] { Text("Increment"); });
    });
}

kompoz::Application kompoz::createApplication(int, char**) {
    return {.title = "Demo", .content = [] { MaterialTheme(lightColorScheme(), Counter); }};
}
```

## What's in it

- **Runtime**: composable functions with smart recomposition (only what read a changed state runs again;
  unchanged children are skipped), `remember`/`rememberSaveable`, effects, `CompositionLocal`, C++20
  coroutines (`LaunchedEffect`, `delay`, `Flow`/`StateFlow`), `ViewModel`, navigation with type-safe routes and
  deep links, lifecycle.
- **Layout**: incremental (a node is measured again only when something in it changed), `Box`/`Row`/`Column`,
  `FlowRow`, `FlexBox`, `Grid`, custom `Layout` and `SubcomposeLayout`, intrinsics, window insets, right to left.
- **Lists and scrolling**: `LazyColumn`/`LazyRow`/lazy grids/staggered grids, pagers, carousels, nested scroll,
  Android's fling curve and overscroll stretch, snapping, the mouse wheel and touchpads.
- **Material 3** (Compose Material 3 1.4, including the Expressive components): Scaffold and app bars,
  navigation bars, rails and drawers, `NavigationSuiteScaffold`, buttons and button groups, cards, chips, lists,
  sliders, switches, tabs, text fields, date and time pickers, dialogs, menus, sheets, snackbars, tooltips,
  search bars, progress and loading indicators, carousels, `MaterialTheme` with dynamic color.
- **Drawing and text**: `Canvas`/`DrawScope`, brushes, paths, any shape as a clip or outline, blurred shadows,
  `graphicsLayer`, vector images, `AsyncImage` (JPEG, PNG, WebP, over HTTP), HarfBuzz text shaping, bidirectional
  text, `AnnotatedString`, selection, emoji and system fallback fonts.
- **Input**: touch, multi-touch gestures, mouse (hover, right click, text selection by mouse), keyboard focus
  and shortcuts, the software keyboard with suggestions, drag and drop, the predictive back gesture.
- **Accessibility**: a semantics tree that TalkBack reads on Android, and screen readers read through AT-SPI on
  Linux (Orca) and UI Automation on Windows (Narrator, NVDA): labels, roles, states, actions, custom actions, live
  regions.
- **Platform integration**: the system's dark mode; dynamic color, from a source the app (or its user) picks: the
  system's palette (the wallpaper's on Android 12+, the desktop's accent color on Linux and Windows) or the wallpaper itself (Linux
  desktops, Windows, Android 8.1+); system font scale; the system language (the library's own texts in English or
  Turkish, `LocalStrings`, and right-to-left layouts); clipboard, haptics, URIs, the IME.
- **Tools**: headless UI tests (`ComposeTestRule`), previews (`KOMPOZ_PREVIEW`), rebuild-and-restart on save
  that keeps the app's state (`scripts/dev.sh`), scripted screenshots, frame profiling, packaging for every
  platform (`scripts/package.sh`).

The full list, with the status of each Compose API, is in [docs/components.md](docs/components.md).

## Compose → Kompoz

| Compose | Kompoz |
|---|---|
| `@Composable` + compiler plugin | Every composable takes a default `CallSite site = CallSite::current()` parameter (`std::source_location`). The call site plus the order within the parent is the identity |
| Restartable / skippable composable | `composable(site, args..., [=]{ ... })`: runs again on its own when a state it read changes; skipped when its arguments are equal |
| Inline composable (`Column`, `Row`) | `ComposableScope scope(site);`: gives an identity without a scope of its own (for composables that take a content lambda) |
| `remember { }`, `remember(key) { }` | `remember([]{...})`, `remember(key, []{...})` |
| `mutableStateOf` | `mutableStateOf(x)` → `MutableState<T>` (a copyable handle; capture it by value in callbacks) |
| `key(id) { }` | `key(id, [&]{...})` |
| `DisposableEffect`, `SideEffect` | The same names |
| `CompositionLocal` | `CompositionLocal<T>`, `CompositionLocalProvider({Local.provides(v)}, ...)` |
| `animate*AsState` | `animateFloatAsState`, `animateColorAsState` (only their own scope runs each frame) |
| `LaunchedEffect`, `rememberCoroutineScope`, `delay` | C++20 coroutines: `LaunchedEffect(key, [=]() -> Task { co_await delay(1s); ... })` |
| `Animatable`, `tween`, `spring`, `infiniteRepeatable` | `co_await anim.animateTo(1.f, tween(300))`, the same spec names |
| `AnimatedVisibility`, `AnimatedContent`, `Crossfade` | The same names; `fadeIn() + expandVertically()`, `scaleIn()`, `togetherWith` |
| `NavHost(nav, "home") { composable("detail/{id}") { e -> } }`, `nav.navigate("detail/3")` | `NavHost(nav, "home", m, [&](NavGraphBuilder& g) { g.composable("detail/{id}", [&](const NavBackStackEntry& e) { e.arguments().getInt("id"); }); })`, `nav.navigate("detail/3", {.popUpTo = "home", .saveState = true, .restoreState = true})` |
| `@Serializable data class Profile(val id: String, val tab: Int? = null)`, `composable<Profile> { e -> e.toRoute<Profile>() }`, `nav.navigate(Profile("42"))` | `struct Profile { std::string id; std::optional<int> tab; KOMPOZ_ROUTE(Profile, id, tab) };` (required fields are the path, `std::optional`s query parameters), `g.composable<Profile>([&](const NavBackStackEntry& e) { e.toRoute<Profile>(); })`, `nav.navigate(Profile{.id = "42"})`, `navigation<Login>(Username{}, …)`, `hasRoute<T>()`, `getBackStackEntry<T>()`, `navDeepLink<T>("kompoz://profile")` |
| `composable("item/{n}", deepLinks = listOf(navDeepLink { uriPattern = "kompoz://item/{n}" })) { … }` | `g.composable("item/{n}", {navDeepLink("kompoz://item/{n}")}, [&](const NavBackStackEntry& e) { … })`; `nav.navigate("kompoz://item/3")`, `nav.handleDeepLink(...)` |
| `navigation(startDestination = "username", route = "login") { composable("username") { … } }` | `g.navigation("username", "login", [&](NavGraphBuilder& login) { login.composable("username", [&](const NavBackStackEntry& e) { auto vm = viewModel<LoginViewModel>(*e.parent()); }); })` |
| `class SearchViewModel(private val state: SavedStateHandle) : ViewModel()`, `state.getStateFlow("q", "")`, `state["q"] = q` | `explicit SearchViewModel(SavedStateHandle s) : state(s), query(s.getStateFlow<std::string>("q", "")) {}`, `state.set("q", q)` (`viewModel<SearchViewModel>()` passes the handle; it survives the process being ended) |
| `rememberSaveable { mutableStateOf(0) }` | `rememberSaveable([] { return mutableStateOf(0); })` (also when Android ends the process and brings it back) |
| `Saver(save = { … }, restore = { … })` | `template <> struct kompoz::Saver<Point> { static SavedValue save(const Point&); static std::optional<Point> restore(const SavedValue&); };` |
| `AsyncImage(model = url, contentDescription = "…", placeholder = …, contentScale = ContentScale.Crop)` (Coil) | `AsyncImage(url, "…", Modifier().size(120), {.placeholder = [] { … }, .error = [] { … }, .contentScale = ContentScale::Crop, .crossfadeMillis = 200});` (an asset path or http(s); JPEG, PNG, WebP; decoded at the box's size; downloads are kept on disk) |
| `val vm: MyViewModel = viewModel()` | `auto vm = viewModel<MyViewModel>();` (`class MyViewModel : public ViewModel`, `viewModelScope()`, `onCleared()`) |
| `SharedTransitionLayout { AnimatedContent(x) { Modifier.sharedElement(rememberSharedContentState(key), this@AnimatedContent) } }` | `SharedTransitionLayout(m, [&](const SharedTransitionScope& s) { AnimatedContent<T>(x, [&](const AnimatedContentScope& scope, const T& v) { Box(Modifier().sharedElement(s.rememberSharedContentState("key"), scope)); }); })` |
| `derivedStateOf`, `mutableStateListOf`, `rememberUpdatedState`, `produceState` | The same names (`state.hpp`, `coroutines.hpp`) |
| `LazyColumn { items(list, key) { ... } }`, `stickyHeader { }` | `LazyColumn(modifier, [&](LazyListScope& list) { list.stickyHeader([] { ... }); list.items(data, key, content); })` |
| `LazyColumn(reverseLayout = true)`, `listState.animateScrollToItem(0)`, `scrollState.animateScrollTo(0)` | `LazyColumn(modifier, {.state = list, .reverseLayout = true}, ...)`, `scope.launch([list]() -> Task { co_await list.animateScrollToItem(0); })`, `co_await scroll.animateScrollTo(0)` |
| `LazyVerticalGrid(GridCells.Adaptive(96))`, `HorizontalPager`/`VerticalPager` | `LazyVerticalGrid(GridCells::Adaptive(96), modifier, {}, [&](LazyGridScope& g) { g.items(...); })`, `HorizontalPager(rememberPagerState(0, count), modifier, [](int page) {...})` |
| `val grid = rememberLazyGridState(); LazyVerticalGrid(GridCells.Fixed(3), state = grid)`, `grid.animateScrollToItem(0)`, `grid.layoutInfo.visibleItemsInfo` | `auto grid = rememberLazyGridState(); LazyVerticalGrid(GridCells::Fixed(3), Modifier(), {.state = grid}, …)`, `co_await grid.animateScrollToItem(0)`, `grid.layoutInfo().visibleItemsInfo` |
| `HorizontalMultiBrowseCarousel(state = rememberCarouselState { n }, preferredItemWidth = 186.dp) { i -> … }` | `HorizontalMultiBrowseCarousel(rememberCarouselState(0, [&] { return n; }), 186, Modifier().height(221), {.itemSpacing = 8}, [&](int i) { … })` |
| `Modifier.nestedScroll(connection)`, `TopAppBarDefaults.enterAlwaysScrollBehavior()` | `Modifier().nestedScroll(rememberNestedScrollConnection({.onPreScroll = [](Offset available, NestedScrollSource) { ... }}))`; `auto s = TopAppBarDefaults::enterAlwaysScrollBehavior(); Scaffold(Modifier().nestedScroll(s.nestedScrollConnection), {.topBar = [&] { TopAppBar({..., .scrollBehavior = s}); }}, ...)`, `LargeTopAppBar` |
| `BoxWithConstraints`, `onSizeChanged`, `animateContentSize`, `updateTransition`, `FlowRow` | The same names |
| `Layout(content) { measurables, constraints -> }`, `Modifier.layout { }` | `Layout(modifier, content, [](const std::vector<Measurable>& m, const Constraints& c) { ... return Size{w, h}; })`, `Modifier().layout(...)`, `height(IntrinsicSize::Min)` |
| `FontFamily.Serif`, `FontFamily(Font(R.font.x))` | `{.fontFamily = FontFamily::Serif}`, `FontFamily::fromFiles({{"fonts/x.ttf"}, {"fonts/x-bold.ttf", FontWeight::Bold}})` |
| `buildAnnotatedString { withStyle(SpanStyle(...)) { append(...) } }` | `buildAnnotatedString([&](AnnotatedString::Builder& b) { b.withStyle({.fontWeight = FontWeight::Bold}, [&] { b.append("bold"); }); })`, `withLink`; `TextOverflow`, `FontStyle::Italic`, `LocalDensity` (the system's font scale) |
| `Canvas`, `drawBehind`, `drawWithContent`, `drawWithCache` | `Canvas(modifier, [](DrawScope& d) { d.drawCircle(color); })` |
| `Brush.verticalGradient(...)`, `Path` | `Brush::verticalGradient({a, b})`, `background(brush, shape)`, `d.drawPath(path, brush, Stroke(3))` |
| `Modifier.graphicsLayer { rotationZ = ... }` | `graphicsLayer({.rotationZ = 45})`, or a lambda read while drawing: `graphicsLayer([=](GraphicsLayerScope& l) { ... })` |
| `TriStateCheckbox(state = ToggleableState.Indeterminate, onClick)`, `Text(…, autoSize = TextAutoSize.StepBased(12.sp, 48.sp))` | `TriStateCheckbox(ToggleableState::Indeterminate, onClick)`, `Text("…", Modifier(), {.maxLines = 1, .autoSize = TextAutoSize::StepBased(12, 48)})` |
| `SubcomposeLayout { constraints -> val m = subcompose("main") { … }.map { it.measure(constraints) }; layout(w, h) { … } }` | `SubcomposeLayout(Modifier(), [](SubcomposeMeasureScope& s, const Constraints& c) { auto m = s.subcompose(std::string("main"), [] { … }); …; return Size{w, h}; })` |
| `Modifier.clickable(interactionSource = s, indication = null) { … }` + `Modifier.indication(s, ripple())` | `Modifier().clickable([] { … }, {.interactionSource = s, .indication = false})` + `Modifier().indication(s, ripple())` |
| `Modifier.clickable(indication = ripple(bounded = false, radius = 24.dp)) { … }`, `CompositionLocalProvider(LocalIndication provides …)` | `Modifier().clickable([] { … }, {.indication = ripple(false, 24)})`, `CompositionLocalProvider({LocalIndication.provides(false)}, …)` (every clickable has Material's ripple by default) |
| `val source = remember { MutableInteractionSource() }`, `val pressed by source.collectIsPressedAsState()`, `Modifier.clickable(source, indication = null) { … }` | `auto source = remember([] { return MutableInteractionSource(); });`, `bool pressed = source.collectIsPressedAsState();`, `Modifier().clickable([] { … }, {.interactionSource = source, .indication = false})` (`Button(…, Modifier(), {.interactionSource = source}, …)`, `Slider(v, set, Modifier(), {.interactionSource = source})`, `source.interactions().collect(…)`, `pagerState.interactionSource().collectIsDraggedAsState()`) |
| `val state = remember { AnchoredDraggableState(DragValue.Start) }`, `state.updateAnchors(DraggableAnchors { Start at 0f; End at w })`, `Modifier.offset { IntOffset(state.requireOffset().roundToInt(), 0) }.anchoredDraggable(state, Orientation.Horizontal)` | `auto state = rememberAnchoredDraggableState(DragValue::Start);`, `state.updateAnchors({{DragValue::Start, 0}, {DragValue::End, w}});`, `Modifier().offset([state] { return Offset{state.requireOffset(), 0}; }).anchoredDraggable(state, Orientation::Horizontal)` (`anchored_draggable.hpp`) |
| `val measurer = rememberTextMeasurer()`, `drawText(measurer.measure("%42", style), topLeft = …)`, `Text(text, onTextLayout = { r -> cut = r.hasVisualOverflow })` | `auto measurer = rememberTextMeasurer();`, `d.drawText(measurer.measure("%42", {.fontSize = 12}), {x, y})`, `Text(text, Modifier(), {.maxLines = 2}, [cut](const TextLayoutResult& r) { cut.set(r.hasVisualOverflow()); })` (`text_measure.hpp`) |
| `val v = velocityTracker.calculateVelocity()`, `offset.animateDecay(v.x, rememberSplineBasedDecay())`, `offset.animateTo(0f, spring(), initialVelocity = v.x)`, `offset.updateBounds(0f, max)` | `Offset v = tracker.calculateVelocity();`, `co_await offset.animateDecay(v.x, splineBasedDecay());`, `co_await offset.animateTo(0.f, spring(), v.x);`, `offset.updateBounds(0.f, max);` |
| `AnimatedContent(count, transitionSpec = { if (targetState > initialState) slideInVertically { it } togetherWith slideOutVertically { -it } else … using SizeTransform(clip = false) })`, `keyframes { durationMillis = 400; 1.3f at 100 }` | `AnimatedContent<int>(count, content, Modifier(), [](const int& from, const int& to) { return togetherWith(slideInVertically(to > from ? 1 : -1), slideOutVertically(to > from ? -1 : 1)).withSizeTransform(SizeTransform{.clip = false}); })`, `keyframes(400, {{100, 1.3f}})` |
| `AnimatedContent(state, contentAlignment = Alignment.Center, contentKey = { it::class }) { … }` | `AnimatedContent<Screen>(state, content, Modifier(), defaultContentTransform(), {.contentAlignment = Alignment::Center, .contentKey = [](const Screen& s) { return uint64_t(s.index()); }})` |
| `AnimatedVisibility(open, enter = EnterTransition.None, exit = ExitTransition.None) { Box(Modifier.animateEnterExit(enter = slideInVertically { it }, exit = slideOutVertically { it })) }` | `AnimatedVisibility(open, Modifier(), EnterTransition::None, ExitTransition::None, [&](const AnimatedVisibilityScope& s) { Box(s.animateEnterExit(slideInVertically(1), slideOutVertically(1))); })` |
| `Modifier.onGloballyPositioned { c -> offset = c.positionInRoot(); visible = c.boundsInRoot().height > 0 }` | `Modifier().onGloballyPositioned([=](const LayoutCoordinates& c) { offset.set(c.positionInRoot()); visible.set(c.boundsInRoot().height > 0); })` |
| `Row(Modifier.selectable(selected, onClick = { … }, role = Role.RadioButton)) { RadioButton(selected, onClick = null) }`, `Modifier.matchParentSize()` | `Row(Modifier().selectable(selected, [=] { … }, SemanticsRole::RadioButton), [&] { RadioButton(selected, {}); })`, `Modifier().matchParentSize()` |
| `scope.launch { if (snackbarHostState.showSnackbar("Deleted", actionLabel = "Undo") == SnackbarResult.ActionPerformed) undo() }` | `scope.launch([s]() -> Task { if (co_await s.showSnackbarAsync("Deleted", "Undo") == SnackbarResult::ActionPerformed) undo(); });` |
| `val sheetState = rememberModalBottomSheetState()`, `ModalBottomSheet(onDismissRequest = { show = false }, sheetState = sheetState) { … }`, `scope.launch { sheetState.hide() }.invokeOnCompletion { show = false }` | `auto sheetState = rememberModalBottomSheetState();`, `ModalBottomSheet([show] { show.set(false); }, sheetState, [&] { … });`, `scope.launch([=]() -> Task { co_await sheetState.hide(); show.set(false); });` |
| `Modifier.pointerHoverIcon(PointerIcon.Hand)` | `Modifier().pointerHoverIcon(PointerIcon::Hand)` (`Cursor::Move`, `ResizeHorizontal`…) |
| `CenterAlignedTopAppBar(title = { … })`, `Card(onClick = { … }) { … }`, `Scaffold(floatingActionButtonPosition = FabPosition.Center)` | `CenterAlignedTopAppBar({.title = [] { … }})`, `Card([] { … }, Modifier(), [] { … })`, `Scaffold(Modifier(), {.floatingActionButton = …, .floatingActionButtonPosition = FabPosition::Center}, …)` |
| `LazyRow(state = s, flingBehavior = rememberSnapFlingBehavior(s)) { … }` | `LazyRow(Modifier(), {.state = s, .flingBehavior = rememberSnapFlingBehavior(s)}, [](LazyListScope& l) { … })` |
| `CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Rtl) { … }`, `Modifier.absoluteOffset(x)` | `CompositionLocalProvider({LocalLayoutDirection.provides(LayoutDirection::Rtl)}, [&] { … })`, `Modifier().absoluteOffset(x, 0)` |
| `IconToggleButton(checked, onCheckedChange = { checked = it }) { Icon(…) }`, `OutlinedIconButton(onClick) { … }` | `IconToggleButton(checked.get(), [checked](bool on) { checked.set(on); }, [] { Icon(…); })` (`FilledIconToggleButton`, `FilledTonalIconToggleButton`, `OutlinedIconToggleButton`), `OutlinedIconButton([] { … }, [] { … })` |
| `OutlinedTextField(value, onValueChange, label = { Text("Amount") }, prefix = { Text("$") }, suffix = { Text("kg") })` | `OutlinedTextField(value, set, Modifier(), {.label = "Amount", .prefix = "$", .suffix = "kg"})` |
| `Modifier.zIndex(1f)`, `placeable.place(x, y, zIndex)` | `Modifier().zIndex(1)`, `p.place(x, y, 1)` |
| `Modifier.dragAndDropSource { DragAndDropTransferData(...) }`, `Modifier.dragAndDropTarget(shouldStartDragAndDrop, target)` | `Modifier().dragAndDropSource([](Offset) { return DragAndDropTransferData{.text = "…", .localState = item}; })`, `dragAndDropTarget([](const DragAndDropEvent& e) { return e.hasMimeType("text/plain"); }, {.onDrop = [](const DragAndDropEvent& e) { …; return true; }})` |
| Material 3 components | `ListItem({.headline = ..., .supporting = ...})`, `FilterChip(selected, onClick, label)`, `Slider(value, onChange)`, `PrimaryTabRow`/`Tab`, `BadgedBox`, `SegmentedButton`, `BottomAppBar` (`material.hpp`) |
| `TextField`/`OutlinedTextField`, `KeyboardOptions`, `FocusRequester` | `OutlinedTextField(value, onChange, modifier, {.singleLine = true, .label = "Email", .keyboardOptions = {.keyboardType = KeyboardType::Email, .imeAction = ImeAction::Next}})`; as in Compose, a field without `singleLine` has several lines (`minLines`/`maxLines`); `OutlinedTextField(state)` with `rememberTextFieldState()` (`state.edit([](TextFieldBuffer& b) { b.selectAll(); })`, `{.inputTransformation = InputTransformation::maxLength(10)}`, `{.outputTransformation = OutputTransformation{[](TextFieldBuffer& b) { b.insert(3, " "); }}}`, `state.undoState().undo()`); `Modifier().focusRequester(r)`, `r.requestFocus()`, `focusManager().clearFocus()`, `Modifier().focusProperties({.canFocus = false, .next = r})` |
| `LocalUriHandler.current.openUri(url)`, `LocalSoftwareKeyboardController.current?.hide()`, `LocalHapticFeedback.current.performHapticFeedback(...)`, `LocalClipboardManager` | `uriHandler().openUri(url)`, `softwareKeyboardController().hide()`, `hapticFeedback().performHapticFeedback(HapticFeedbackType::LongPress)`, `clipboardManager().setText(...)` (taken in composition, used in callbacks) |
| `ModalNavigationDrawer`, `NavigationRail`, the window's size | `ModalNavigationDrawer(drawerContent, rememberDrawerState(), content)`; a rail in wide windows from `LocalWindowSize.current()` |
| `Dialog`, `AlertDialog`, `DropdownMenu`, `ModalBottomSheet`, `BackHandler` | The same names; `if (open) AlertDialog(onDismiss, {.title = ..., .confirmButton = ...})`. Popups are composed where they are called in the tree (their state and composition locals are kept); the host places them on top |
| `BottomSheetScaffold(sheetContent = { ... }, scaffoldState = state, sheetPeekHeight = 128.dp) { padding -> ... }` | `BottomSheetScaffold([&] { ... }, Modifier(), {.scaffoldState = state, .sheetPeekHeight = 128}, [&](PaddingValues padding) { ... })`, `co_await state.bottomSheetState.expand()` |
| `NavigationSuiteScaffold(navigationSuiteItems = { item(selected, onClick, icon, label) }) { content }`, `currentWindowAdaptiveInfo().windowSizeClass` | `NavigationSuiteScaffold([&](NavigationSuiteScope& nav) { nav.item(selected, onClick, Icons::Filled::Home, "Home"); }, Modifier(), {}, content)`, `currentWindowAdaptiveInfo().windowSizeClass.windowWidthSizeClass` |
| `Modifier.imePadding()`, `WindowInsets.statusBars`, `consumeWindowInsets` | `Modifier().padding(padding).consumeWindowInsets(padding).imePadding()`, `WindowInsets::statusBars()`, `windowInsetsPadding(WindowInsets::systemBars())`; `SystemBarsAppearance(!dark)` for the system bars' icons |
| `dynamicLightColorScheme(context)` (Material You) | `MaterialTheme(isDynamicColorAvailable() ? dynamicLightColorScheme() : lightColorScheme(), ...)`: the wallpaper's colors on Android 12+, the desktop's accent color on Linux (XDG portal) and Windows; `TonalPalette::fromSeed(color)` anywhere; `wallpaperLightColorScheme()` from the wallpaper itself (Linux desktops, Windows, Android 8.1+), `TonalPalette::fromImage(bitmap)` from any picture |
| `Modifier.semantics { contentDescription = ...; role = Role.Button }` | `Modifier().semantics({.contentDescription = "...", .role = SemanticsRole::Button})`; TalkBack reads this tree on Android |
| `Modifier.semantics(mergeDescendants = true) {}`, `Modifier.clearAndSetSemantics { contentDescription = "Rating: 4 of 5" }`, `Modifier.semantics { hideFromAccessibility() }` | `Modifier().semantics({.mergeDescendants = true})`, `Modifier().clearAndSetSemantics({.contentDescription = "Rating: 4 of 5"})`, `Modifier().hideFromAccessibility()` |
| `Modifier.animateItem()`, `SwipeToDismissBox`, `Modifier.draggable` | The same names; `SwipeToDismissBox(rememberSwipeToDismissBoxState(confirm), background, content)` |
| `Modifier.focusable()`, `onKeyEvent { }` | `Modifier().onKeyEvent([](const KeyEvent& e) { return e.key == Key::Enter && e.type == KeyEventType::KeyDown; }).focusable()`; focus moves with Tab and the arrow keys, Enter/Space click |
| `SearchBar(inputField = { SearchBarDefaults.InputField(...) }, expanded, onExpandedChange) { }` | `SearchBar([&] { SearchBarDefaults::InputField({.query = q, .onQueryChange = ..., .expanded = open, ...}); }, open, setOpen, modifier, [&] { ...suggestions... })`; `DockedSearchBar`, which opens in place, takes the same arguments; `BasicTextField` |
| `DatePickerDialog(onDismiss, confirmButton = { }) { DatePicker(state) }` | `auto state = rememberDatePickerState(); DatePickerDialog(close, [=] { TextButton([=] { pick(state.selectedDate()); }, ...); }, cancel, [&] { DatePicker(state, Modifier(), {.strings = DatePickerStrings::turkish()}); })`; dates are `std::chrono::year_month_day` |
| `DateRangePicker(state)` | `auto state = rememberDateRangePickerState(); DateRangePicker(state, Modifier(), {.strings = DatePickerStrings::turkish()})`; `state.selectedStartDate()` / `selectedEndDate()`; start and end tabs, each like `DatePicker` |
| `TimePickerDialog(onDismiss, confirmButton = { }) { TimePicker(state) }` | `auto state = rememberTimePickerState(9, 30); TimePickerDialog(close, {.title = ..., .confirmButton = ..., .modeToggleButton = ...}, [&] { TimePicker(state); })`; `TimeInput(state)` to type it |
| `TooltipBox(positionProvider, tooltip = { PlainTooltip { Text(..) } }, state) { IconButton(..) }` | `TooltipBox([] { PlainTooltip([] { Text("Add to favorites"); }); }, rememberTooltipState(), Modifier(), [&] { IconButton(...); })`; `RichTooltip({.title, .text, .action})` |
| `ExposedDropdownMenuBox(expanded, onExpandedChange) { TextField(readOnly = true, modifier = Modifier.menuAnchor()); ExposedDropdownMenu(..) }` | `ExposedDropdownMenuBox(open, setOpen, Modifier(), [&] { TextField(choice, {}, Modifier().fillMaxWidth(), {.readOnly = true}); ExposedDropdownMenu(open, close, [&] { DropdownMenuItem(...); }); })`; the box itself is the anchor |
| `RangeSlider(value = 20f..80f, onValueChange, valueRange = 0f..100f)` | `RangeSlider(std::pair(20.f, 80.f), [=](std::pair<float, float> v) { ... }, Modifier(), {.valueFrom = 0, .valueTo = 100, .steps = 9})` |
| `PrimaryScrollableTabRow(selectedTabIndex) { Tab(...) }`, `SecondaryTabRow` | `PrimaryScrollableTabRow(i, Modifier(), [&] { Tab(...); }, {.edgePadding = 16})`; `SecondaryTabRow(i, Modifier(), [&] { Tab(...); })` |
| `LazyVerticalStaggeredGrid(columns = StaggeredGridCells.Fixed(2)) { items(..) }`, `LazyHorizontalGrid(rows = GridCells.Fixed(3))` | `LazyVerticalStaggeredGrid(StaggeredGridCells::Fixed(2), Modifier().fillMaxSize(), {.verticalItemSpacing = 8}, [&](LazyGridScope& g) { g.items(...); })`; `LazyHorizontalGrid(GridCells::Fixed(3), Modifier().height(180), {}, ...)` |
| `Modifier.scrollable(rememberScrollableState { delta -> ... }, Orientation.Horizontal)` | `auto s = rememberScrollableState([=](float delta) { ...; return delta; }); Modifier().scrollable(s, Orientation::Horizontal)`; `Modifier().scale(2).rotate(45)` |
| `PermanentNavigationDrawer(drawerContent = { PermanentDrawerSheet { } }) { }`, `DismissibleNavigationDrawer` | `PermanentNavigationDrawer([&] { PermanentDrawerSheet(Modifier(), items); }, Modifier(), page)`; `DismissibleNavigationDrawer([&] { DismissibleDrawerSheet(Modifier(), items); }, drawerState, page)` |
| `LaunchedEffect(Unit) { snapshotFlow { listState.firstVisibleItemIndex }.collect { } }` | `LaunchedEffect(0, [=]() -> Task { co_await snapshotFlow([=] { return list.firstVisibleItemIndex(); }).collect([](int i) { ... }); })` |
| `val results = query.debounce(300).flatMapLatest { repo.search(it) }.stateIn(viewModelScope, Eagerly, emptyList())` | `StateFlow<Results> results = query.debounce(300ms).flatMapLatest([](const std::string& q) { return repo.search(q); }).stateIn(viewModelScope(), {});` (`flow<T>([](FlowCollector<T> emit) -> Task { co_await emit(v); })`, `map`, `filter`, `take`, `combine`…) |
| `MutableSharedFlow<Event>()`, `stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), initial)` | `MutableSharedFlow<Event> events;` (`events.tryEmit(e)` from any thread), `flow.stateIn(viewModelScope(), SharingStarted::WhileSubscribed(5000ms), initial)`, `shareIn(scope, SharingStarted::Lazily, 1)` |
| `val nearEnd by remember { derivedStateOf { state.layoutInfo.visibleItemsInfo.lastOrNull()?.index == total - 1 } }`, `items(n, key = …, contentType = …)` | `auto nearEnd = remember([state] { return derivedStateOf([state] { auto v = state.layoutInfo().visibleItemsInfo; return !v.empty() && v.back().index == total - 1; }); });`, `l.items(n, key, [](int) { return size_t(1); }, …)` |
| `val uiState by viewModel.uiState.collectAsState()` | `UiState uiState = viewModel->uiState.collectAsState();` (`MutableStateFlow<UiState>`, `setValue`/`update` from any thread) |
| `val items by vm.items.collectAsStateWithLifecycle()`, `LifecycleResumeEffect(key) { player.play(); onPauseOrDispose { player.pause() } }`, `LifecycleEventEffect(Lifecycle.Event.ON_START) { … }` | `auto items = collectAsStateWithLifecycle(vm->items);`, `LifecycleResumeEffect(key, [=] { player.play(); return [=] { player.pause(); }; });`, `LifecycleEventEffect(LifecycleEvent::OnStart, [&] { … });` (`currentLifecycleOwner()->lifecycle()`: the app's or the navigation entry's) |
| `SelectionContainer { Text(..) }` | `SelectionContainer(Modifier(), [] { Text("..."); })`: one selection across texts, handles, Copy, dragging with the mouse, Ctrl+C |
| `DisableSelection { Text(..) }` | `DisableSelection([] { Text("..."); })` |
| `PullToRefreshBox(isRefreshing, onRefresh) { LazyColumn { } }` | `PullToRefreshBox(refreshing, [=] { reload(); }, Modifier().fillMaxSize(), [&] { LazyColumn(...); })` |
| `Modifier.transformable(state)`, `detectTransformGestures` | `auto t = rememberTransformableState([=](float zoom, Offset pan, float rotation) { ... }); Modifier().transformable(t)`; `PointerEvent::changes` carries every finger |
| `pointerInput(key) { detectTapGestures(...) }` | `pointerInput(key, [=](PointerInputScope& s) -> Task { co_await detectTapGestures(s, {.onTap = ...}); })` |
| `Modifier.padding(..).background(..)` | `Modifier().padding(..).background(..)` (the order matters, as in Compose) |
| Named arguments | Designated initializers: `{.fontSize = 24, .color = ...}` |
| `@Preview` | `KOMPOZ_PREVIEW(Name, .width = 360, .height = 200) { ... }`, shown with `KOMPOZ_PREVIEW=Name` (see [Development](#development)) |
| `createComposeRule()`, `onNodeWithText(..).assertIsDisplayed()` | `kompoz::testing::ComposeTestRule` (UI tests without a window): `rule.onNodeWithText("OK").assertIsDisplayed().performClick()`; a failure throws `testing::AssertionError` |
| `Modifier.recomposeHighlighter()` (snippets) | `Modifier().recomposeHighlighter()`: a border showing how many times the composable that made the node ran |

Which Compose APIs came first was decided by how often the android/snippets repository uses them; see
[docs/components.md](docs/components.md).

## Smart recomposition

It works as in Compose:

- Every function written with `composable(...)` is a **scope**. Each `MutableState` and `CompositionLocal` its
  body reads while it runs is recorded.
- When a state changes, **only the scopes that read it** run again; their parents don't.
- When a parent runs, children whose arguments are **equal** to last time and that read nothing that changed are
  **skipped**: their slots and nodes stay as they are.
- When a `CompositionLocalProvider` starts giving a different value, the scopes below it that read it run again
  (whatever was skipped in between).
- Animations run only their own scope each frame; values read while drawing or laying out (a `graphicsLayer`
  lambda, `Modifier.offset { }`, `InfiniteTransition::animateFloatState`) don't recompose at all.

```cpp
void TodoRow(const Todo& todo, MutableState<std::vector<Todo>> todos, CallSite site = CallSite::current()) {
    composable(site, todo, todos, [=] {   // skipped while the list is redrawn, if todo and todos are the same
        ...
    });
}
```

Rules:

- The body lambda captures with `[=]`: the scope may later run on its own, without its parent.
- To be skipped, the arguments must compare with `==`. A `MutableState` compares by identity, a `Modifier` by its
  elements' values. An argument that can't be compared (`std::function`, ...) makes the composable restartable but
  never skipped.
- `Text` is skippable. Components that take a content lambda (`Column`, `Button`, `Card`...) are like Compose's
  inline functions: state read inside them belongs to the enclosing scope.
- Event callbacks (`onClick`, ...) run after composition, so capture local variables **by value**, not with `[&]`.
  Content lambdas (`Column(..., [&]{...})`) run right away, so `[&]` is safe there.

Layout is incremental as well: a node keeps its size and its children's places when it is measured with the
same constraints and nothing in or below it changed; only what was composed again, or read a state that
changed while it was measured, is measured again.

`KOMPOZ_TRACE=1` logs how many scopes ran and how many were skipped in each recomposition. The showcase's
"Recomposition'ları göster" switch turns on the highlighter from the snippets.

## Architecture

```
 app code (composable functions)
        │
 ┌──────▼─────────────────────────────── kompoz_core (platform independent, no dependencies)
 │ runtime     slot table (a tree of groups), scopes and read tracking, remember, state, effects, CompositionLocal
 │ state       derivedStateOf, mutableStateListOf, rememberUpdatedState
 │ coroutines  Task, Dispatcher (delay/awaitFrame on the frame clock), LaunchedEffect, CoroutineScope
 │ flow        Flow, StateFlow/SharedFlow (written from any thread), collect, collectAsState
 │ lifecycle   Lifecycle/LifecycleOwner (the app's, navigation entries'), lifecycle effects
 │ layout      LayoutNode tree, Constraints, the Modifier chain, incremental measure → place → draw
 │ graphics    Color, HCT and tonal palettes, Rect, ImageVector, ImageBitmap, Brush, Path, Shape, Font/FontFamily
 │ draw        DrawScope, Canvas, drawBehind/drawWithContent/drawWithCache, graphicsLayer
 │ text        AnnotatedString, multi-style line breaking, bidi (right-to-left text)
 │ gestures    pointerInput (a coroutine per node), detectTap/Drag/TransformGestures
 │ host        ComposeHost: composition + layout + popup layer + hit testing, focus, scrolling, flings (dp)
 │ nested_scroll NestedScrollConnection/Dispatcher: scrolls and flings chained to the parents
 │ lazy, pager LazyColumn/LazyRow/grids (only what is in view, plus a margin), pagers, carousels
 │ animation   specs, Animatable, animate*AsState, transitions, animateContentSize;
 │             animated: AnimatedVisibility/AnimatedContent/Crossfade, SharedTransitionLayout
 │ widgets     foundation and basic components, TextField, Popup, BackHandler, previews
 │ material    Scaffold and bars, components (chips, sliders, tabs...), overlays (dialogs, menus,
 │             sheets), navigation (drawers, rails, suites), adaptive layouts
 └──────┬─────────────────────────────── GraphicsContext / TextMeasurer interfaces
 ┌──────▼─────────────────────────────── platform backend
 │ sdl3      window, input, IME/soft keyboard, anti-aliased drawing with SDL_Renderer, HarfBuzz + FreeType
 │           glyph atlas, images, the desktop's accent color (Linux)
 │ android   the Java side: keyboard (InputConnection), accessibility, insets, dynamic color, back gesture
 └────────────────────────────────────── (a Skia, Metal or Vulkan backend would go here)
```

A new platform backend implements `GraphicsContext` and `TextMeasurer` (two functions, plus two optional calls for
gradients and paths), gives input events to `ComposeHost`, and calls `frame()` and `draw()`; redrawing only what
changed through a `DisplayList` is optional. The SDL3 backend is about 3,600 lines (glyph atlas with fallback
fonts and color emoji, vector and path rasterization, gradient meshes, transformed offscreen layers, shadows).

## Building

Requirements: a C++20 compiler (GCC 13+, Clang 17+; tested with GCC 16 and Clang 22; GCC 13 crashes on some coroutine
lambdas, an internal compiler error that GCC 14 doesn't have), CMake 3.24+, SDL3 and
FreeType. Optional: HarfBuzz (text shaping: Arabic joining, Indic scripts, ligatures, emoji sequences; without it
text is drawn character by character), and on Linux libcurl, libwebp and libturbojpeg 3.0+ for `AsyncImage` (HTTP,
WebP, JPEG). On Linux, libdbus is loaded at run time, when present, for the desktop's accent color.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/showcase
./build/kompoz_tests
```

The core and the tests alone (no SDL): `-DKOMPOZ_BUILD_SDL3=OFF`; without the examples: `-DKOMPOZ_BUILD_EXAMPLES=OFF`.
On desktops that have lld (and when no other linker was chosen) the build links with it: about 1 second instead of
GNU ld's 25 for the showcase.

### Using Kompoz in your project

Add it as a subdirectory (a clone or a git submodule) and link the SDL3 backend, which brings the entry point:

```cmake
cmake_minimum_required(VERSION 3.24)
project(myapp LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

set(KOMPOZ_BUILD_EXAMPLES OFF)
set(KOMPOZ_BUILD_TESTS OFF)
add_subdirectory(kompoz)        # a clone or a git submodule

add_executable(myapp main.cpp)  # the example at the top
target_link_libraries(myapp PRIVATE kompoz_sdl3)
kompoz_copy_assets(myapp)       # the fonts and the licenses next to the binary
```

`main.cpp` defines `kompoz::createApplication` (see the example at the top); the backend owns `main`. For Android and
the web, start from the showcase's setup: its `android/` Gradle project and the Emscripten options in
`examples/showcase/CMakeLists.txt`.

Layout of the repository:

| Path | What |
|---|---|
| `include/kompoz/`, `src/` | the platform-independent core (`#include <kompoz/kompoz.hpp>`) |
| `platform/sdl3/` | the SDL3 backend (desktop, Android's native side, the web) |
| `platform/android/java/` | the Android Java side (keyboard, accessibility, insets, intents) |
| `platform/web/` | the web page the app is drawn in |
| `assets/` | what every app ships: the bundled fonts (`fonts/`) and the licenses (`licenses/`) |
| `tests/` | the headless tests |
| `examples/showcase/` | the showcase app: a folder per screen (`home/`, `list/`, `components/`, `material/`, `places/`), each screen's sections in files of their own, `showcase.hpp` for what they share; `android/` is the same app for Android, `assets/` its images |
| `scripts/` | `fetch-deps.sh` (the vendored dependencies), `dev.sh`, `package.sh` |
| `docs/` | [components.md](docs/components.md) (every API and its status, the build log), screenshots |

The showcase's texts are in Turkish, and it gives the library's own texts in Turkish too (`LocalStrings`).

## Development

### Rebuild and restart on save

```bash
scripts/dev.sh
```

When a source file is saved, the showcase is rebuilt and started again; the screen it was on, scroll positions and
`rememberSaveable` values come back (through `KOMPOZ_STATE_FILE`, as Android brings an app back after ending its
process). A build that fails leaves the running app alone. An edit to a showcase file is on the screen in about 7
seconds (Debug with sanitizers, lld). Arguments: the target, the build directory and what the app is started with
(`scripts/dev.sh showcase build kompoz://components`). Closing the window or Ctrl+C ends it.

### Previews

As Compose's `@Preview`: a composable on its own, without the rest of the app, at its size.

```cpp
KOMPOZ_PREVIEW(CounterCardPreview, .width = 400, .height = 220) {
    MaterialTheme(lightColorScheme(), [] { CounterCard(); });
}
```

```bash
KOMPOZ_PREVIEW=CounterCardPreview scripts/dev.sh
```

The window shows only that preview and comes back on every save; `KOMPOZ_PREVIEW='*'` (or a name it doesn't know)
shows them all, one under another, with their names and sizes. `.darkTheme = true` sets `isSystemInDarkTheme()`
inside it, `.background` a background; the text scale is the window's (`KOMPOZ_FONT_SCALE`). As a picture:
`SDL_VIDEODRIVER=offscreen KOMPOZ_PREVIEW=CounterCardPreview KOMPOZ_SCRIPT="wait 10; shot preview.png; quit" ./build/showcase`.

### Tests

`kompoz::testing::ComposeTestRule` composes, lays out and draws without a window, with a virtual clock and a text
measurer of fixed sizes; find nodes by text, tag or semantics, click, type, swipe, and check bounds, semantics and
what was drawn. `./build/kompoz_tests` runs them all (`./build/kompoz_tests lazy` only those whose name contains
"lazy"). Every incremental layout is checked against a full one while the tests run.

## Packaging

```bash
scripts/package.sh
```

Packages the showcase for every platform it builds on, into `dist/`: Linux (`.tar.gz`, Release; it uses the
system's SDL3, FreeType, HarfBuzz...), Windows (`.zip`, built statically with MinGW; Windows 10+), Android (`.apk`,
signed with the debug key, installable as it is) and the web (`.zip`; serve the folder over HTTP:
`python3 -m http.server 8765`). Only some of them: `scripts/package.sh linux web`. A platform whose toolchain is
missing is skipped, with a note.

### Android

Requirements: the Android SDK, NDK 29.0.14206865, SDK CMake 4.1.2 (from Android Studio's SDK Manager or
`sdkmanager "ndk;29.0.14206865" "cmake;4.1.2"`), JDK 17+ (Android Studio's `jbr` will do).

```bash
scripts/fetch-deps.sh
```

```bash
cd examples/showcase/android && ./gradlew installDebug
```

`scripts/fetch-deps.sh` downloads SDL3, FreeType and HarfBuzz into `third_party/` at pinned versions, checking their
sha256, and applies the fixes in `scripts/patches/` (one at the moment: a deadlock on Android when the activity
pauses while SDL creates its renderer, not fixed in SDL 3.4.18 either). Gradle builds the root `CMakeLists.txt` with
the NDK: `libSDL3.so` and the app's `libmain.so` (the showcase's sources). The Java side is SDL's own
`SDLActivity`; the example's `android/` folder only has the manifest, a theme and a short `MainActivity`. The fonts
come into the APK from `assets/`, the example's images from `examples/showcase/assets/`.

Deep links (`navDeepLink`): when the app is opened with a link, the NavHost goes to that screen, with the start
screen and the screens of the graphs in between under it, so back goes up. On the desktop it is the command line
(`./build/showcase kompoz://item/42`), on the web the address's `#` part (`showcase.html#item/42`; the screen shown
is written there too, so reloading opens the same one), on Android a `VIEW` intent: an `intent-filter` for its
scheme in the manifest, and `KompozInput.onNewIntent(intent)` from `MainActivity.onNewIntent` for links that come
while the app is open (as in the example). For `rememberSaveable` values to come back after the process was ended,
`MainActivity` calls `KompozInput`'s methods of the same names from `onCreate` (before super), `onPause` (before
super) and `onSaveInstanceState` (after super); on the desktop `KOMPOZ_STATE_FILE=state.bin` writes the state at
exit and reads it at start (to try the process being ended). To try a deep link:

```bash
adb shell am start -a android.intent.action.VIEW -d "kompoz://item/42"
```

The keyboard goes through an `InputConnection` of our own rather than SDL's
([KompozInput.java](platform/android/java/dev/kompoz/KompozInput.java)). SDL's keeps a separate copy of the text and
sends only differences, so suggestions and autocorrect garbled the text. Ours has a single `Editable`; every change
sends the whole text, the selection and the word being composed to the native side. When the app changes the text
itself (clearing it after "Ekle", say), `restartInput` brings the keyboard back in step. Compose's
`TextInputServiceAndroid` works the same way. `MainActivity` replaces `createSDLSurface` with
`KompozInput.createSurface(context)`, so that SDL's surface doesn't take the keyboard for its own and bring its input
view forward when the window's size changes (when the keyboard opens).

What works on Android: touch, scrolling and flings, the keyboard with suggestions and autocorrect, the keyboard's
type and action key from `KeyboardOptions` (email, number, password; Next/Done/Search...), back (popup → focus →
`BackHandler` → leaving the app) and the predictive back gesture, density (dp) and the system's font size (sp, also
when it changes while running), edge to edge (status bar, navigation bar, cutout, keyboard and gesture edges as
typed `WindowInsets`; the system bars' icons for the light or dark theme), the system's dark mode
(`isSystemInDarkTheme()`), dynamic color on Android 12+ (the wallpaper's palette, refreshed when the app comes to the
front), TalkBack, Serif/Monospace, symbols, CJK and color emoji from the system's fonts (Android's COLRv1 emoji font
is drawn with the platform's text stack), the glyph atlas rebuilt when the GPU context is lost.

### Web (Emscripten)

Requirements: Emscripten (Arch: `pacman -S emscripten`; its commands are under `/usr/lib/emscripten`) and
`scripts/fetch-deps.sh`. SDL3, FreeType and HarfBuzz are built from source; the fonts and images are packed into
`showcase.data`; the page ([platform/web/shell.html](platform/web/shell.html)) is just a canvas and the app fills it.

```bash
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DKOMPOZ_BUILD_TESTS=OFF
```

```bash
cmake --build build-web -j
```

```bash
python3 -m http.server 8765 --directory build-web
```

Then open `http://localhost:8765/showcase.html`. There are no threads on the web (`std::mutex` does nothing; the
example's "download" runs in a coroutine). No font files either: characters the bundled font lacks (emoji, CJK,
Arabic, Indic scripts, symbols) are drawn by the browser with its own fonts on a 2D canvas, runs of them together
(Arabic words come out joined); `FontFamily::Serif`/`Monospace` fall back to the bundled sans-serif on the web.

### Windows (cross-built from Linux with MinGW)

Requirements: MinGW-w64 (Arch: `pacman -S mingw-w64-gcc`) and `scripts/fetch-deps.sh`. The dependencies are built
from source, statically; `showcase.exe` depends only on Windows' own DLLs and goes along with the `fonts/`,
`licenses/` and `images/` folders next to it.

```bash
cmake -S . -B build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build-windows -j
```

The tests also run under Wine: `WINEPATH=/usr/x86_64-w64-mingw32/bin wine build-windows/kompoz_tests.exe`.

### Automation and screenshots

`KOMPOZ_SCRIPT` drives the app without a user (coordinates in dp; while the script runs, time is virtual and
animations are deterministic; when it ends, real time takes over and you can go on using the app from where the
script left it):

```bash
SDL_VIDEODRIVER=offscreen SDL_RENDER_DRIVER=opengl \
KOMPOZ_SCRIPT="click 160 263; wait 30; shot out.png; quit" ./build/showcase
```

Commands: `click x y`, `rclick x y`, `move x y`, `down x y` / `up x y` (holding down), `drag x1 y1 x2 y2`,
`mdrag x1 y1 x2 y2` (with the mouse), `resize w h`, `swipe x1 y1 x2 y2 frames` (a move per frame, so it flings),
`fdown|fmove|fup id x y` (multi-touch), `wheel x y dy`, `zoom x y factor` (Ctrl+wheel / touchpad pinch),
`type text`, `key enter|backspace|escape|tab|shift+tab|space|up|down|left|right`, `wait frames`, `sleep ms` (real
time, for work on other threads), `shot file.png`, `quit`. Environment variables: `KOMPOZ_TRACE=1` logs how many scopes ran or were skipped in each recomposition;
`KOMPOZ_FONT_SCALE=1.3` stands in for the system's font scale on the desktop, `KOMPOZ_SCALE=2.625` for a phone's
density; `KOMPOZ_NO_DESKTOP_ACCENT=1` and `KOMPOZ_NO_WALLPAPER_COLORS=1` leave out the desktop's accent color and the
wallpaper's colors (screenshots the same on every machine); `KOMPOZ_LANGUAGE=tr|en` stands in for the system language of the library's own texts;
`KOMPOZ_STATE_FILE=file` keeps the saved state across runs; `KOMPOZ_PREVIEW=name` shows a preview.

`KOMPOZ_PROFILE=1` logs frame times: for each slow frame the input, compose + layout, drawing and present times, the
offscreen layers and the glyphs rasterized, and a summary every second. Android has no environment variables, so it
looks for a file in the app's own storage (the output is in `adb logcat -s SDL/APP`):

```bash
adb shell run-as dev.kompoz.showcase touch files/kompoz-profile
```

Frames are recorded first (`DisplayList`); compared with the frame before, only what changed is drawn again over
the kept pixels. A frame in which nothing on the screen changes (an indicator spinning out of view, a recomposition
that looks the same) is neither drawn nor shown. `KOMPOZ_DAMAGE_SHOW=1` outlines what was drawn again,
`KOMPOZ_DAMAGE_CHECK=1` also draws each frame from scratch and compares the pixels (logging any difference), and
`KOMPOZ_FULL_REDRAW=1` draws every frame from scratch.

## Platforms

| Platform | Status |
|---|---|
| Linux | Built and tested (GCC 16 with ASan/UBSan, Clang 22). Dark mode and the accent color (Material You) come from the XDG desktop portal as they change: SDL follows the mode, Kompoz the accent color (libdbus, loaded at run time) |
| Android | Built; run on a phone (Xiaomi 24030PN60G, Android 16, arm64) and in the emulator (API 36, x86_64, GLES). The APK has `arm64-v8a` and `x86_64` |
| Windows | Cross-built from Linux with MinGW-w64 (GCC 16); the tests pass under Wine and the showcase draws there |
| Web | Built with Emscripten (WebGL, SDL3's main callbacks) and run in browsers (clicks, typing, scrolling) |
| macOS | The code is portable (SDL3 + FreeType, CMake); not built yet |
| iOS | The vendored dependencies are ready (`KOMPOZ_VENDORED_DEPS`); it needs an Xcode project (on macOS) |

## Known limitations

1. **macOS and iOS** haven't been built (they need a Mac), nor connected to NSAccessibility; TalkBack on Android,
   AT-SPI on Linux and UI Automation on Windows work.
2. Overscroll is Android 12's stretch done as a scale, not a true warp. Content scaled up with `graphicsLayer` is
   scaled from a 1× layer texture (Compose draws it again at the scale), so it softens when zoomed far in. In nested
   scrolling, `onPreFling`/`onPostFling` don't suspend as in Compose (a connection that animates starts its own
   coroutine).
3. Text: right-to-left text is ordered by a simplified Unicode bidi algorithm (isolates and bracket pairs, but no
   explicit embeddings or overrides). Missing glyphs come from the system's fallback fonts.
4. Drawing: gradients are drawn as meshes interpolating between their vertices' colors. SDL's software renderer
   doesn't do premultiplied blending in transformed layers, so their edges come out a little dark there. There is
   no GPU/Skia backend; drawing goes through SDL_Renderer.
5. The recompose highlighter's counts don't reset after 3 seconds as the snippets' do; they add up.
6. Lazy lists don't reuse items' compositions by `contentType` (Compose's SubcomposeLayout pool); measured, it
   wasn't needed yet.
7. Kotlin's `by` delegation has no counterpart: state is read and written with `.get()` and `.set()`.
8. The library's own texts (`LocalStrings`) come in English and Turkish only; an app in another language provides
   its own `Strings`.

## License

Kompoz is licensed under the [Apache License, Version 2.0](LICENSE); see [NOTICE](NOTICE). It follows the behavior
and API of Jetpack Compose and Material Color Utilities (both Apache 2.0) without including their sources.

Apps built with Kompoz ship the bundled Noto Sans fonts (SIL Open Font License 1.1) and, in the vendored builds
(Android, iOS, Windows, the web), SDL (zlib), FreeType (FreeType License) and HarfBuzz (MIT). Their license texts are
in [assets/licenses](assets/licenses), which is copied next to every app with its other assets; keep it when you
ship one.
