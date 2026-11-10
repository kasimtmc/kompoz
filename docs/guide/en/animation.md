# Animation

Kompoz has Compose's animation APIs: values that animate to a target, content that animates in and out, transitions of several values together, and low-level `Animatable` for gestures and coroutines.

## Choosing an API

| You want to | Use |
|---|---|
| Animate one value when its target changes | `animateFloatAsState`, `animateColorAsState` |
| Show or hide content with an animation | `AnimatedVisibility` |
| Switch between pieces of content | `AnimatedContent`, `Crossfade` |
| Animate a size change | `Modifier.animateContentSize()` |
| Animate several values from one state | `updateTransition` |
| Animate forever (pulses, spinners) | `rememberInfiniteTransition()` |
| Drive an animation from a coroutine or a gesture | `Animatable` |
| Move an element from one screen to another | `SharedTransitionLayout` and `sharedElement` |

## Animating a value

`animate*AsState` returns a state (`AnimatedState<T>`, Compose's `State<T>`) whose value animates to its target whenever the target changes:

```kotlin
@Composable
fun LikeButton() {
    var liked by remember { mutableStateOf(false) }
    val scale by animateFloatAsState(if (liked) 1.2f else 1f, spring(Spring.DampingRatioMediumBouncy), label = "scale")
    val tint by animateColorAsState(if (liked) Color(0xFFE53935) else MaterialTheme.colorScheme.onSurfaceVariant, tween(300))

    IconButton(onClick = { liked = !liked }) {
        Icon(if (liked) Icons.Filled.Favorite else Icons.Filled.FavoriteBorder, contentDescription = "Like",
             modifier = Modifier.graphicsLayer { scaleX = scale; scaleY = scale }, tint = tint)
    }
}
```

Where the value is read decides what follows it, as in Compose. Used as a value in a composable (`val tint by …`, or `.value` there), that scope recomposes on each frame of the animation, not its parents. Read with `.value` while drawing (`drawBehind`, `graphicsLayer { }`, a `Canvas`), only that node is drawn again (and while nothing it draws shows, scrolled out of view, it asks for no frames: it goes on, where its time has taken it, when it comes back); while laying out (`offset { }`, a `layout`), only it is laid out again (see [Phases](phases.md#deferring-state-reads)). An animation read nowhere asks for no frames. `targetValue` and `isRunning` say where it is going and whether it is on its way. A composition local can be given one too, `LocalContentColor provides color`: then only the composables that read the local follow it, not the one providing it.

The second argument is an animation spec (`animationSpec`), or, Kompoz's own, a duration in milliseconds for a tween; `label` is Compose's name for its tools.

A color animates as Compose's does (`Color.VectorConverter`): in Oklab, its lightness and hue along, so black to white is a mid gray to the eye halfway (`0x636363`), and a spring that overshoots goes past its target there.

## Animation specs

| Spec | Behavior |
|---|---|
| `spring(dampingRatio, stiffness)` | Physics-based. The default; it keeps its velocity when the target changes mid-way. `Spring.DampingRatioMediumBouncy`, `Spring.StiffnessLow` and the other constants are Compose's. |
| `tween(durationMillis, delayMillis, easing)` | A fixed duration along an easing curve: `FastOutSlowInEasing` (the default), `LinearOutSlowInEasing`, `FastOutLinearInEasing`, `LinearEasing`. |
| `keyframes { durationMillis = 400; 1.3f at 100 using LinearEasing; 0.9f at 250 }` | Values at given times, each with the easing of the stretch that starts there (`using`), as Compose's builder. |
| `repeatable(iterations, spec, mode)`, `infiniteRepeatable(spec, mode)` | A spec repeated, restarting or reversing (`RepeatMode.Reverse`). |
| `snap(delayMillis)` | Jumps to the target. |

Material components use `MaterialTheme.motionScheme`'s springs, so they move the way Material 3 (and M3 Expressive with `MaterialExpressiveTheme`) says.

## AnimatedVisibility

`AnimatedVisibility` animates its content in when `visible` becomes true and out when it becomes false. Once hidden, the content leaves the composition.

```kotlin
@Composable
fun Details() {
    var open by remember { mutableStateOf(false) }
    Column {
        TextButton(onClick = { open = !open }) { Text(if (open) "Less" else "More") }
        AnimatedVisibility(visible = open, enter = fadeIn() + expandVertically(), exit = fadeOut() + shrinkVertically()) {
            Text("Here are the details.", modifier = Modifier.padding(16.dp))
        }
    }
}
```

Enter transitions are `fadeIn`, `slideInHorizontally`, `slideInVertically`, `scaleIn`, `expandIn`, `expandHorizontally` and `expandVertically`; exit transitions are their `fadeOut`, `slideOut…`, `scaleOut` and `shrink…` counterparts. Combine them with `+`; the slides take Compose's lambda of the full size (`slideInVertically { it / 2 }`). `EnterTransition.None` and `ExitTransition.None` animate nothing, for when the children animate themselves with `animateEnterExit` from the content's `AnimatedVisibilityScope`.

Each transition takes its spec by name too (`fadeIn(animationSpec = tween(300))`, `slideInVertically(animationSpec = tween(450)) { it }`, `scaleOut(targetScale = 0.5f)`). A row that should come in the first time it shows is given a `MutableTransitionState` made hidden with a visible target, as Compose's `visibleState`; its `currentState` and `isIdle` say where it is:

```kotlin
@Composable
fun Arriving() {
    val shown = remember { MutableTransitionState(false).apply { targetState = true } }
    AnimatedVisibility(visibleState = shown, enter = fadeIn(animationSpec = tween(500)) + expandVertically()) {
        Box(modifier = Modifier.fillMaxWidth().height(48.dp)) {
            Box(modifier = Modifier.animateEnterExit(enter = slideInVertically { it }, exit = fadeOut()).matchParentSize())
            IconButton(onClick = { shown.targetState = false }) { Icon(Icons.Filled.Close, contentDescription = "Remove") }
        }
    }
    if (shown.isIdle && !shown.currentState) Text("Removed")
}
```

As in Compose, a transition is read as its content is laid out and drawn: the composable around it is composed again when it starts and when it is done, not on the frames in between.

## AnimatedContent and Crossfade

`AnimatedContent` animates between contents when its target state changes: the old content leaves while the new one enters, and the size animates between them.

```kotlin
@Composable
fun Counter() {
    var count by remember { mutableStateOf(0) }
    Row(verticalAlignment = Alignment.CenterVertically) {
        IconButton(onClick = { count-- }) { Icon(Icons.Filled.Remove, contentDescription = "Less") }
        AnimatedContent(
            targetState = count,
            transitionSpec = {
                // up when counting up, down when counting down
                if (targetState > initialState) slideInVertically { it } togetherWith slideOutVertically { -it }
                else slideInVertically { -it } togetherWith slideOutVertically { it }
            },
            label = "count",
        ) { n ->
            Text("$n", style = MaterialTheme.typography.headlineMedium)
        }
        IconButton(onClick = { count++ }) { Icon(Icons.Filled.Add, contentDescription = "More") }
    }
}
```

The content lambda gets the state it shows, which for leaving content is the old one: draw from that argument, not from the state variable. `enter togetherWith exit` pairs the two transitions; `using SizeTransform(clip = false)` after it changes how the size animates. The `transitionSpec` lambda reads `initialState` and `targetState`, as Compose's.

`Crossfade` is the simple case: it fades between contents.

```kotlin
enum class Page { Home, Settings }

@Composable
fun PageContent(page: Page) {
    Crossfade(targetState = page, label = "page") { p ->
        when (p) {
            Page.Home -> Text("Home")
            Page.Settings -> Text("Settings")
        }
    }
}
```

## animateContentSize

`Modifier.animateContentSize()` animates a node's size whenever its content makes it grow or shrink:

```kotlin
@Composable
fun Expandable(text: String) {
    var expanded by remember { mutableStateOf(false) }
    Text(text, modifier = Modifier.clickable { expanded = !expanded }.animateContentSize(),
         maxLines = if (expanded) Int.MAX_VALUE else 2, overflow = TextOverflow.Ellipsis)
}
```

Put it before size modifiers in the chain, so it sees the size change.

## updateTransition

When several values animate together towards one state, a transition keeps them in step and tells you when they have all arrived:

```kotlin
enum class BoxState { Collapsed, Expanded }

@Composable
fun TransitionBox() {
    var state by remember { mutableStateOf(BoxState.Collapsed) }
    val transition = updateTransition(targetState = state, label = "box")
    val size by transition.animateDp(label = "size") { if (it == BoxState.Expanded) 200.dp else 80.dp }
    val color by transition.animateColor(label = "color") {
        if (it == BoxState.Expanded) Color(0xFF6750A4) else Color(0xFFB58392)
    }
    Box(modifier = Modifier.size(size).background(color, RoundedCornerShape(16.dp)).clickable {
        state = if (state == BoxState.Expanded) BoxState.Collapsed else BoxState.Expanded
    })
}
```

`transition.currentState` catches up with `transition.targetState` once every value has arrived, and `transition.isRunning` says whether any is still moving. `animateDp` is `animateFloat` here: a dp is a float.

## Infinite animations

`rememberInfiniteTransition()` animates values for as long as they are in the composition:

```kotlin
@Composable
fun Pulse() {
    val pulse by rememberInfiniteTransition(label = "pulse").animateFloat(
        initialValue = 0.85f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(800), RepeatMode.Reverse), label = "scale")
    Box(modifier = Modifier.size(64.dp)
            .graphicsLayer { scaleX = pulse; scaleY = pulse }
            .background(MaterialTheme.colorScheme.primary, CircleShape))
}
```

`animateFloat` and `animateColor` return a state, as Compose's do, to read where it is needed: read while drawing, as here, it is only drawn again each frame; read in a composition (or taken as a `float`), the reader recomposes each frame.

## Animatable

`Animatable` is a value you animate from coroutines: `animateTo` suspends until the animation ends, `snapTo` jumps, `stop` stops it. A new `animateTo` takes over from a running one, keeping its velocity, which makes it the tool for animations driven by gestures:

```kotlin
@Composable
fun Shake(error: Boolean) {
    val offset = remember { Animatable(0f) }
    LaunchedEffect(error) {
        if (error) {
            for (x in listOf(-12f, 12f, -8f, 8f, 0f)) offset.animateTo(x, tween(50))
        }
    }
    Text("Wrong password", modifier = Modifier.offset { IntOffset(offset.value.roundToInt(), 0) },
         color = MaterialTheme.colorScheme.error)
}
```

- `animateTo(target, spec, initialVelocity)` starts from a fling's velocity, so a released drag springs on naturally.
- `animateDecay(velocity, splineBasedDecay())` slows down from a velocity until it stops, like a fling.
- `updateBounds(lower, upper)` keeps it within limits.
- `value` is observable; read it during layout or drawing for smooth motion.

See [Gestures](gestures.md) for combining it with dragging.

## Shared elements

A shared element moves and resizes from its place in one piece of content to its place in another, as in Compose:

```kotlin
@Composable
fun Gallery() {
    var details by remember { mutableStateOf(false) }
    SharedTransitionLayout(modifier = Modifier.fillMaxSize()) {
        AnimatedContent(targetState = details, label = "photo") { open ->
            Box(modifier = Modifier
                    .size(if (open) 280.dp else 72.dp)
                    .sharedElement(rememberSharedContentState(key = "photo"), animatedVisibilityScope = this@AnimatedContent)
                    .background(MaterialTheme.colorScheme.tertiary, RoundedCornerShape(16.dp))
                    .clickable { details = !details })
        }
    }
}
```

Elements with the same key in the leaving and the entering content become one element that animates between them, drawn above everything else. `sharedBounds` is for elements whose contents differ: both are shown, cross-fading, in the moving bounds. With navigation, shared elements move between screens.

## See also

- [Phases](phases.md): animating without recomposing.
- [Gestures](gestures.md): animations that follow the finger.
- [Graphics](graphics.md): `graphicsLayer` for animated transforms.
