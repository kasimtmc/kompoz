# Animation

Kompoz has Compose's animation APIs: values that animate to a target, content that animates in and out, transitions of several values together, and low-level `Animatable` for gestures and coroutines.

## Choosing an API

| You want to | Use |
|---|---|
| Animate one value when its target changes | `animateFloatAsState`, `animateColorAsState` |
| Show or hide content with an animation | `AnimatedVisibility` |
| Switch between pieces of content | `AnimatedContent`, `Crossfade` |
| Animate a size change | `Modifier().animateContentSize()` |
| Animate several values from one state | `updateTransition` |
| Animate forever (pulses, spinners) | `rememberInfiniteTransition()` |
| Drive an animation from a coroutine or a gesture | `Animatable` |
| Move an element from one screen to another | `SharedTransitionLayout` and `sharedElement` |

## Animating a value

`animate*AsState` returns a state (`AnimatedState<T>`, Compose's `State<T>`) whose value animates to its target whenever the target changes:

```cpp
void LikeButton() {
    auto liked = remember([] { return mutableStateOf(false); });
    auto scale = animateFloatAsState(liked.get() ? 1.2f : 1.f, spring(Spring::DampingRatioMediumBouncy));
    Color tint = animateColorAsState(liked.get() ? Color::hex(0xE53935) : colorScheme().onSurfaceVariant, 300);

    IconButton([liked] { liked.set(!liked.get()); }, [&] {
        Icon(liked.get() ? Icons::Filled::Favorite : Icons::Filled::FavoriteBorder, "Like",
             Modifier().graphicsLayer([scale](GraphicsLayerScope& l) { l.scaleX = l.scaleY = scale.value(); }), tint);
    });
}
```

Where the value is read decides what follows it, as in Compose. Used as a value in a composable (`Color tint = ...`, or `.value()` there), that scope recomposes on each frame of the animation, not its parents. Read with `.value()` while drawing (`drawBehind`, `graphicsLayer { }`, a `Canvas`), only that node is drawn again; while laying out (`offset { }`, a `layout`), only it is laid out again (see [Phases](phases.md#deferring-state-reads)). An animation read nowhere asks for no frames. `targetValue()` and `isRunning()` say where it is going and whether it is on its way. A composition local can be given one too, `LocalContentColor.provides(color)`: then only the composables that read the local follow it, not the one providing it.

The second argument is an animation spec, or a duration in milliseconds for a tween.

A color animates as Compose's does (`Color.VectorConverter`): in Oklab, its lightness and hue along, so black to white is a mid gray to the eye halfway (`0x636363`), and a spring that overshoots goes past its target there.

## Animation specs

| Spec | Behavior |
|---|---|
| `spring(dampingRatio, stiffness)` | Physics-based. The default; it keeps its velocity when the target changes mid-way. `Spring::DampingRatioMediumBouncy`, `Spring::StiffnessLow` and the other constants are Compose's. |
| `tween(durationMillis, delayMillis, easing)` | A fixed duration along an easing curve: `FastOutSlowInEasing` (the default), `LinearOutSlowInEasing`, `FastOutLinearInEasing`, `LinearEasing`. |
| `keyframes(durationMillis, {{atMillis, value}, …})` | Values at given times. |
| `repeatable(iterations, spec, mode)`, `infiniteRepeatable(spec, mode)` | A spec repeated, restarting or reversing (`RepeatMode::Reverse`). |
| `snap(delayMillis)` | Jumps to the target. |

Material components use `motionScheme()`'s springs, so they move the way Material 3 (and M3 Expressive with `MaterialExpressiveTheme`) says.

## AnimatedVisibility

`AnimatedVisibility` animates its content in when `visible` becomes true and out when it becomes false. Once hidden, the content leaves the composition.

```cpp
void Details() {
    auto open = remember([] { return mutableStateOf(false); });
    Column([&] {
        TextButton([open] { open.set(!open.get()); }, [&] { Text(open.get() ? "Less" : "More"); });
        AnimatedVisibility(open.get(), Modifier(), fadeIn() + expandVertically(), fadeOut() + shrinkVertically(), [] {
            Text("Here are the details.", Modifier().padding(16));
        });
    });
}
```

Enter transitions are `fadeIn`, `slideInHorizontally`, `slideInVertically`, `scaleIn`, `expandIn`, `expandHorizontally` and `expandVertically`; exit transitions are their `fadeOut`, `slideOut…`, `scaleOut` and `shrink…` counterparts. Combine them with `+`. `EnterTransition::None` and `ExitTransition::None` animate nothing, for when the children animate themselves with `animateEnterExit` from the content's `AnimatedVisibilityScope`.

As in Compose, a transition is read as its content is laid out and drawn: the composable around it is composed again when it starts and when it is done, not on the frames in between.

## AnimatedContent and Crossfade

`AnimatedContent` animates between contents when its target state changes: the old content leaves while the new one enters, and the size animates between them.

```cpp
void Counter() {
    auto count = remember([] { return mutableStateOf(0); });
    Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
        IconButton([count] { count.set(count.get() - 1); }, [] { Icon(Icons::Filled::Remove, "Less"); });
        AnimatedContent<int>(count.get(), [](const int& n) {
            Text(std::to_string(n), {.font = typography().headlineMedium});
        }, Modifier(), [](const int& from, const int& to) {
            // up when counting up, down when counting down
            return togetherWith(slideInVertically(to > from ? 1 : -1), slideOutVertically(to > from ? -1 : 1));
        });
        IconButton([count] { count.set(count.get() + 1); }, [] { Icon(Icons::Filled::Add, "More"); });
    });
}
```

The content lambda gets the state it shows, which for leaving content is the old one: draw from that argument, not from the state variable. `togetherWith(enter, exit)` pairs the two transitions; `.withSizeTransform(SizeTransform{.clip = false})` changes how the size animates.

`Crossfade` is the simple case: it fades between contents.

```cpp
enum class Page { Home, Settings };

void PageContent(Page page) {
    Crossfade<Page>(page, [](const Page& p) {
        if (p == Page::Home) Text("Home"); else Text("Settings");
    });
}
```

## animateContentSize

`Modifier().animateContentSize()` animates a node's size whenever its content makes it grow or shrink:

```cpp
void Expandable(const std::string& text) {
    auto expanded = remember([] { return mutableStateOf(false); });
    Text(text, Modifier().clickable([expanded] { expanded.set(!expanded.get()); }).animateContentSize(),
         {.maxLines = expanded.get() ? 0 : 2, .overflow = TextOverflow::Ellipsis});
}
```

Put it before size modifiers in the chain, so it sees the size change.

## updateTransition

When several values animate together towards one state, a transition keeps them in step and tells you when they have all arrived:

```cpp
enum class BoxState { Collapsed, Expanded };

void TransitionBox() {
    auto state = remember([] { return mutableStateOf(BoxState::Collapsed); });
    auto t = updateTransition(state.get());
    float size = t.animateFloat([](BoxState s) { return s == BoxState::Expanded ? 200.f : 80.f; });
    Color color = t.animateColor([](BoxState s) {
        return s == BoxState::Expanded ? Color::hex(0x6750A4) : Color::hex(0xB58392);
    });
    Box(Modifier().size(size).background(color, 16).clickable([state] {
        state.set(state.get() == BoxState::Expanded ? BoxState::Collapsed : BoxState::Expanded);
    }));
}
```

`t.currentState()` catches up with `t.targetState()` once every value has arrived, and `t.isRunning()` says whether any is still moving.

## Infinite animations

`rememberInfiniteTransition()` animates values for as long as they are in the composition:

```cpp
void Pulse() {
    auto pulse = rememberInfiniteTransition().animateFloatState(0.85f, 1.f, infiniteRepeatable(tween(800), RepeatMode::Reverse));
    Box(Modifier().size(64)
            .graphicsLayer([pulse](GraphicsLayerScope& l) { l.scaleX = l.scaleY = pulse.get(); })
            .background(colorScheme().primary, CircleShape));
}
```

`animateFloatState` and `animateColorState` return a state to read where it is needed: read while drawing, as here, it is only drawn again each frame. `animateFloat` and `animateColor` return the value directly, recomposing the reader each frame.

## Animatable

`Animatable<T>` is a value you animate from coroutines: `animateTo` suspends until the animation ends, `snapTo` jumps, `stop` stops it. A new `animateTo` takes over from a running one, keeping its velocity, which makes it the tool for animations driven by gestures:

```cpp
void Shake(bool error) {
    auto offset = remember([] { return Animatable<float>(0.f); });
    LaunchedEffect(error, [offset, error]() -> Task {
        if (!error) co_return;
        for (float x : {-12.f, 12.f, -8.f, 8.f, 0.f}) co_await offset.animateTo(x, tween(50));
    });
    Text("Wrong password", Modifier().offset([offset] { return Offset{offset.value(), 0}; }),
         {.color = colorScheme().error});
}
```

- `animateTo(target, spec, initialVelocity)` starts from a fling's velocity, so a released drag springs on naturally.
- `animateDecay(velocity, splineBasedDecay())` slows down from a velocity until it stops, like a fling.
- `updateBounds(lower, upper)` keeps it within limits.
- `value()` is observable; read it during layout or drawing for smooth motion.

See [Gestures](gestures.md) for combining it with dragging.

## Shared elements

A shared element moves and resizes from its place in one piece of content to its place in another, as in Compose:

```cpp
void Gallery() {
    auto details = remember([] { return mutableStateOf(false); });
    SharedTransitionLayout(Modifier().fillMaxSize(), [&](const SharedTransitionScope& shared) {
        AnimatedContent<bool>(details.get(), [&, details](const AnimatedContentScope& scope, const bool& open) {
            Box(Modifier()
                    .size(open ? 280 : 72)
                    .sharedElement(shared.rememberSharedContentState("photo"), scope)
                    .background(colorScheme().tertiary, 16)
                    .clickable([details] { details.set(!details.get()); }));
        });
    });
}
```

Elements with the same key in the leaving and the entering content become one element that animates between them, drawn above everything else. `sharedBounds` is for elements whose contents differ: both are shown, cross-fading, in the moving bounds. With navigation, shared elements move between screens.

## See also

- [Phases](phases.md): animating without recomposing.
- [Gestures](gestures.md): animations that follow the finger.
- [Graphics](graphics.md): `graphicsLayer` for animated transforms.
