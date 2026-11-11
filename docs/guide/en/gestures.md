# Gestures and input

Handling taps, clicks, drags, swipes, pinches, the keyboard, focus, hover and drag and drop. Kompoz takes touch, mouse, touchpad and keyboard input on every platform.

## Clicks

`Modifier.clickable(onClick)` is the simplest gesture: a tap or a click, with Material's ripple, keyboard activation (Enter and Space when focused) and an accessibility action.

```kotlin body
Text("Open", modifier = Modifier.clickable { /* … */ }.padding(16.dp))
```

`combinedClickable(onClick, onLongClick)` also takes a long press (and, through its options, a double tap). `ClickableOptions` turns the click off (`enabled`), shares an `interactionSource`, or changes the `indication`: `false` for none, `ripple(false, 24)` for an unbounded ripple.

Components that act on clicks (`Button`, `Card`, `ListItem`) take an `onClick` of their own; you don't add `clickable` to them.

## Scrolling and dragging

For scrolling content, use the scroll modifiers or a lazy list (see [Lists and grids](lists.md)): they handle drags, flings with Android's curve, the overscroll stretch, the mouse wheel and touchpads.

`Modifier.draggable(state, orientation)` reports drags along one axis, as deltas in dp. It doesn't move anything itself: move what you want, typically with an offset read during layout:

```kotlin
@Kompozable
fun Slider1D() {
    var x by remember { mutableStateOf(0f) }
    Box(modifier = Modifier.fillMaxWidth().height(56.dp)) {
        Box(modifier = Modifier
                .offset { IntOffset(x.roundToInt(), 0) }
                .draggable(orientation = Orientation.Horizontal, state = rememberDraggableState { delta -> x = maxOf(0f, x + delta) })
                .size(56.dp)
                .background(MaterialTheme.colorScheme.primary, CircleShape))
    }
}
```

`draggable2D` reports drags in both directions. `scrollable(state, orientation)` is lower level than the scroll modifiers: it reports scroll deltas and flings, and you decide how much of each you consume.

### Snapping between positions

`anchoredDraggable` drags something between named positions and settles at one of them when let go, with a fling's velocity: the building block of bottom sheets, drawers and swipe-to-reveal.

```kotlin
enum class DragValue { Start, End }

@Kompozable
fun Toggle() {
    val state = rememberAnchoredDraggableState(DragValue.Start)
    state.updateAnchors(DraggableAnchors {
        DragValue.Start at 0f
        DragValue.End at 120f
    })
    Box(modifier = Modifier.width(176.dp).height(56.dp).background(MaterialTheme.colorScheme.surfaceContainerHigh, CircleShape)) {
        Box(modifier = Modifier
                .offset { IntOffset(state.requireOffset().roundToInt(), 0) }
                .anchoredDraggable(state, Orientation.Horizontal)
                .size(56.dp)
                .background(MaterialTheme.colorScheme.primary, CircleShape))
    }
}
```

`SwipeToDismissBox` builds on it for list items that are swiped away.

## Pinch, zoom and rotate

`transformable` turns multi-touch gestures into zoom, pan and rotation changes. Ctrl+wheel and touchpad pinches zoom too:

```kotlin
@Kompozable
fun ZoomableImage() {
    var scale by remember { mutableStateOf(1f) }
    val state = rememberTransformableState { zoom, _, _ -> scale = (scale * zoom).coerceIn(1f, 5f) }
    Image(imageResource("images/map.png"), contentDescription = "Map",
          modifier = Modifier.fillMaxSize()
              .graphicsLayer { scaleX = scale; scaleY = scale }
              .transformable(state))
}
```

## pointerInput

For gestures of your own, `Modifier.pointerInput(key) { }` runs a coroutine that receives the node's pointer events. The gesture detectors run inside it:

```kotlin
@Kompozable
fun TapTarget() {
    var last by remember { mutableStateOf("Nothing yet") }
    Box(modifier = Modifier.size(200.dp).background(MaterialTheme.colorScheme.secondaryContainer)
            .pointerInput(Unit) {
                detectTapGestures(onTap = { last = "Tap" }, onDoubleTap = { last = "Double tap" }, onLongPress = { last = "Long press" })
            },
        contentAlignment = Alignment.Center) { Text(last) }
}
```

| Detector | Recognizes |
|---|---|
| `detectTapGestures(s, {.onPress, .onTap, .onDoubleTap, .onLongPress})` | Taps, double taps, long presses. |
| `detectDragGestures(s, {.onDragStart, .onDrag, .onDragEnd})` | Drags in any direction, after the touch slop. |
| `detectHorizontalDragGestures`, `detectVerticalDragGestures` | Drags along one axis. |
| `detectDragGesturesAfterLongPress` | A drag that starts with a long press, to pick something up. |
| `detectTransformGestures(s, onGesture)` | Pinch, pan and rotate with several fingers. |

The block restarts when `key` changes, so give it a key that captures what it depends on. For lower-level work, `awaitPointerEvent()` (in the block: Compose's `awaitPointerEventScope { }` is implicit here) gives each event with every pointer's change; consume a change (`change.consume()`) so that scrolling and clicks around it leave it alone. `awaitEachGesture` and `waitForUpOrCancellation` help structure a gesture, and `VelocityTracker` measures how fast a finger moved when it lifted, for a fling.

```kotlin
@Kompozable
fun Drawing() {
    val points = remember { mutableStateListOf<Offset>() }
    val ink = MaterialTheme.colorScheme.onSurface
    Canvas(modifier = Modifier.fillMaxSize().pointerInput(Unit) {
        detectDragGestures(onDragStart = { at -> points.clear(); points.add(at) }, onDrag = { change, _ -> points.add(change.position) })
    }) {
        drawPoints(points.toList(), PointMode.Polygon, ink, strokeWidth = 3f)
    }
}
```

## Keyboard

`onKeyEvent` handles keys on a focused node; return `true` when you handled one. `onPreviewKeyEvent` sees keys before the focused node does.

```kotlin body
#! fun submit() {}
Box(modifier = Modifier
        .onKeyEvent { e ->
            if (e.type == KeyEventType.KeyDown && e.key == Key.Enter && e.isCtrlPressed) {
                submit()
                true
            } else {
                false
            }
        }
        .focusable())
```

## Focus

Tab and Shift+Tab move keyboard focus between focusable nodes, and the arrow keys move it in a direction. Clickable components and text fields are focusable already; `Modifier.focusable()` makes any node so.

- `FocusRequester` moves focus from code: remember one, attach it with `Modifier.focusRequester(r)`, call `r.requestFocus()`.
- `LocalFocusManager.current.clearFocus()` takes focus away, which also hides the soft keyboard.
- `onFocusChanged([](FocusState s) { … })` says when a node gains or loses focus.
- `focusProperties({.canFocus = false})` takes a node out of the order; its `next` and the other directions change where focus goes.
- `focusGroup()` and `focusRestorer()` keep focus moving sensibly through groups, such as a row of chips.

Keyboard focus is shown, as in Compose Material 3, by the component's focus state layer.

## Hover and the mouse

```kotlin
@Kompozable
fun HoverCard() {
    val source = remember { MutableInteractionSource() }
    val hovered by source.collectIsHoveredAsState()
    Card(modifier = Modifier.hoverable(source).pointerHoverIcon(PointerIcon.Hand)) {
        Text(if (hovered) "Hovering" else "Point at me", modifier = Modifier.padding(24.dp))
    }
}
```

`hoverable(source)` reports the mouse coming over a node and leaving. `pointerHoverIcon(PointerIcon.Hand)` changes the cursor. Text fields and selections show a context menu on a right click.

Compose Multiplatform's desktop APIs are here with their names: `onPointerEvent(type) { event -> }` for the mouse coming in and out, moving, pressing and the wheel (`event.changes.first().consume()` keeps the wheel from scrolling the page), `onClick(matcher = PointerMatcher.mouse(PointerButton.Secondary)) { }` and `onClick(keyboardModifiers = { it.isCtrlPressed }) { }` for clicks by button or with keys held, `ContextMenuArea` for a menu of your own on a right click, and `TooltipArea` for a tooltip that follows the mouse:

```kotlin
@Kompozable
fun DesktopMouse() {
    var last by remember { mutableStateOf("-") }
    Column {
        ContextMenuArea(items = { listOf(ContextMenuItem("Refresh") { last = "Refresh" }, ContextMenuItem("Copy name") { last = "Copy name" }) }) {
            Box(modifier = Modifier.size(200.dp, 80.dp).background(MaterialTheme.colorScheme.secondaryContainer))
        }
        TooltipArea(tooltip = { Text("Shown under the pointer") }, delayMillis = 300) {
            Box(modifier = Modifier.size(200.dp, 40.dp).onClick(matcher = PointerMatcher.mouse(PointerButton.Secondary)) { last = "right click" })
        }
        Text(last)
    }
}
```

A window's menus are Compose Multiplatform's `MenuBar { Menu("…") { … } }`, drawn as a strip at the top of the content: `Item`, `CheckboxItem`, `RadioButtonItem`, `Separator` and submenus, each with a `KeyShortcut` that works while the window has the keys (key names as Compose's: `Key.DirectionLeft`):

```kotlin
@Kompozable
fun Menus() {
    var dark by remember { mutableStateOf(false) }
    var size by remember { mutableStateOf(1) }
    Column {
        MenuBar {
            Menu("File") {
                Item("Open", shortcut = KeyShortcut(Key.O, ctrl = true)) { }
                Menu("Recent") { Item("notes.txt") { } }
            }
            Menu("View") {
                CheckboxItem("Dark theme", checked = dark, shortcut = KeyShortcut(Key.D, ctrl = true), onCheckedChange = { dark = it })
                Separator()
                RadioButtonItem("Small", selected = size == 1, onClick = { size = 1 })
                RadioButtonItem("Large", selected = size == 2, onClick = { size = 2 })
            }
        }
        Text("Size $size")
    }
}
```

## Interactions

A `MutableInteractionSource` is told of the presses, hovers, focus and drags of the components it is given to. Read whether one is going on with `collectIsPressedAsState()`, `collectIsHoveredAsState()`, `collectIsFocusedAsState()` and `collectIsDraggedAsState()` (read with `by`); they recompose when it changes.

```kotlin
@Kompozable
fun PressToRecord() {
    val source = remember { MutableInteractionSource() }
    val pressed by source.collectIsPressedAsState()
    Button(onClick = {}, interactionSource = source) { Text(if (pressed) "Recording…" else "Hold to record") }
}
```

`interactions()` gives the stream of interactions as a flow. `Modifier.indication(source, ripple())` draws an indication for interactions that happen somewhere else.

## Drag and drop

`dragAndDropSource` starts a drag with data; `dragAndDropTarget` accepts drops, from inside the app or, on the desktop, files from outside it:

```kotlin
@Kompozable
fun DropZone() {
    var dropped by remember { mutableStateOf("Drop text here") }
    Column {
        Text("Drag me", modifier = Modifier.padding(16.dp).dragAndDropSource { DragAndDropTransferData(text = "Hello") })
        Box(modifier = Modifier.size(200.dp).border(2.dp, MaterialTheme.colorScheme.outline, RoundedCornerShape(12.dp))
                .dragAndDropTarget(shouldStartDragAndDrop = { it.hasMimeType("text/plain") },
                                   target = DragAndDropTarget(onDrop = { e -> dropped = e.text(); true })),
            contentAlignment = Alignment.Center) { Text(dropped) }
    }
}
```

## See also

- [Lists and grids](lists.md): scrolling, flinging and nested scrolling.
- [Animation](animation.md): `Animatable` for gestures that animate when let go.
- [Accessibility](accessibility.md): making custom gestures reachable without them.
