# Gestures and input

Handling taps, clicks, drags, swipes, pinches, the keyboard, focus, hover and drag and drop. Kompoz takes touch, mouse, touchpad and keyboard input on every platform.

## Clicks

`Modifier().clickable(onClick)` is the simplest gesture: a tap or a click, with Material's ripple, keyboard activation (Enter and Space when focused) and an accessibility action.

```cpp body
Text("Open", Modifier().clickable([] { /* … */ }).padding(16));
```

`combinedClickable(onClick, onLongClick)` also takes a long press (and, through its options, a double tap). `ClickableOptions` turns the click off (`enabled`), shares an `interactionSource`, or changes the `indication`: `false` for none, `ripple(false, 24)` for an unbounded ripple.

Components that act on clicks (`Button`, `Card`, `ListItem`) take an `onClick` of their own; you don't add `clickable` to them.

## Scrolling and dragging

For scrolling content, use the scroll modifiers or a lazy list (see [Lists and grids](lists.md)): they handle drags, flings with Android's curve, the overscroll stretch, the mouse wheel and touchpads.

`Modifier().draggable(state, orientation)` reports drags along one axis, as deltas in dp. It doesn't move anything itself: move what you want, typically with an offset read during layout:

```cpp
void Slider1D() {
    auto x = remember([] { return mutableStateOf(0.f); });
    Box(Modifier().fillMaxWidth().height(56), [&] {
        Box(Modifier()
                .offset([x] { return Offset{x.get(), 0}; })
                .draggable(rememberDraggableState([x](float delta) { x.set(std::max(0.f, x.get() + delta)); }),
                           Orientation::Horizontal)
                .size(56)
                .background(colorScheme().primary, CircleShape));
    });
}
```

`draggable2D` reports drags in both directions. `scrollable(state, orientation)` is lower level than the scroll modifiers: it reports scroll deltas and flings, and you decide how much of each you consume.

### Snapping between positions

`anchoredDraggable` drags something between named positions and settles at one of them when let go, with a fling's velocity: the building block of bottom sheets, drawers and swipe-to-reveal.

```cpp
enum class DragValue { Start, End };

void Toggle() {
    auto state = rememberAnchoredDraggableState(DragValue::Start);
    state.updateAnchors({{DragValue::Start, 0}, {DragValue::End, 120}});
    Box(Modifier().width(176).height(56).background(colorScheme().surfaceContainerHigh, CircleShape), [&] {
        Box(Modifier()
                .offset([state] { return Offset{state.requireOffset(), 0}; })
                .anchoredDraggable(state, Orientation::Horizontal)
                .size(56)
                .background(colorScheme().primary, CircleShape));
    });
}
```

`SwipeToDismissBox` builds on it for list items that are swiped away.

## Pinch, zoom and rotate

`transformable` turns multi-touch gestures into zoom, pan and rotation changes. Ctrl+wheel and touchpad pinches zoom too:

```cpp
void ZoomableImage() {
    auto scale = remember([] { return mutableStateOf(1.f); });
    auto state = rememberTransformableState([scale](float zoom, Offset, float) {
        scale.set(std::clamp(scale.get() * zoom, 1.f, 5.f));
    });
    Image(imageResource("images/map.png"), "Map",
          Modifier().fillMaxSize()
              .graphicsLayer([scale](GraphicsLayerScope& l) { l.scaleX = l.scaleY = scale.get(); })
              .transformable(state));
}
```

## pointerInput

For gestures of your own, `Modifier().pointerInput(key, block)` runs a coroutine that receives the node's pointer events. The gesture detectors run inside it:

```cpp
void TapTarget() {
    auto last = remember([] { return mutableStateOf(std::string("Nothing yet")); });
    Box(Modifier().size(200).background(colorScheme().secondaryContainer)
            .pointerInput(0, [last](PointerInputScope& s) -> Task {
                co_await detectTapGestures(s, {.onTap = [last](Offset) { last.set("Tap"); },
                                               .onDoubleTap = [last](Offset) { last.set("Double tap"); },
                                               .onLongPress = [last](Offset) { last.set("Long press"); }});
            }),
        Alignment::Center, [&] { Text(last.get()); });
}
```

| Detector | Recognizes |
|---|---|
| `detectTapGestures(s, {.onPress, .onTap, .onDoubleTap, .onLongPress})` | Taps, double taps, long presses. |
| `detectDragGestures(s, {.onDragStart, .onDrag, .onDragEnd})` | Drags in any direction, after the touch slop. |
| `detectHorizontalDragGestures`, `detectVerticalDragGestures` | Drags along one axis. |
| `detectDragGesturesAfterLongPress` | A drag that starts with a long press, to pick something up. |
| `detectTransformGestures(s, onGesture)` | Pinch, pan and rotate with several fingers. |

The block restarts when `key` changes, so give it a key that captures what it depends on. For lower-level work, `co_await s.awaitPointerEvent()` gives each event with every pointer's change; consume a change (`change.consume()`) so that scrolling and clicks around it leave it alone. `awaitEachGesture` and `waitForUpOrCancellation` help structure a gesture, and `VelocityTracker` measures how fast a finger moved when it lifted, for a fling.

```cpp
void Drawing() {
    auto points = remember([] { return mutableStateOf(std::vector<Offset>{}); });
    Color ink = colorScheme().onSurface;
    Canvas(Modifier().fillMaxSize().pointerInput(0, [points](PointerInputScope& s) -> Task {
        co_await detectDragGestures(s, {.onDragStart = [points](Offset at) { points.set({at}); },
                                        .onDrag = [points](PointerInputChange& change, Offset) {
                                            points.update([&](std::vector<Offset>& v) { v.push_back(change.position); });
                                        }});
    }), [=](DrawScope& d) {
        d.drawPoints(points.get(), PointMode::Polygon, ink, 3);
    });
}
```

## Keyboard

`onKeyEvent` handles keys on a focused node; return `true` when you handled one. `onPreviewKeyEvent` sees keys before the focused node does.

```cpp body
#! std::function<void()> submit;
Box(Modifier()
        .onKeyEvent([submit](const KeyEvent& e) {
            if (e.type == KeyEventType::KeyDown && e.key == Key::Enter && e.isCtrlPressed()) {
                submit();
                return true;
            }
            return false;
        })
        .focusable());
```

## Focus

Tab and Shift+Tab move keyboard focus between focusable nodes, and the arrow keys move it in a direction. Clickable components and text fields are focusable already; `Modifier().focusable()` makes any node so.

- `FocusRequester` moves focus from code: remember one, attach it with `Modifier().focusRequester(r)`, call `r.requestFocus()`.
- `focusManager().clearFocus()` takes focus away, which also hides the soft keyboard.
- `onFocusChanged([](FocusState s) { … })` says when a node gains or loses focus.
- `focusProperties({.canFocus = false})` takes a node out of the order; its `next` and the other directions change where focus goes.
- `focusGroup()` and `focusRestorer()` keep focus moving sensibly through groups, such as a row of chips.

Keyboard focus is shown, as in Compose Material 3, by the component's focus state layer.

## Hover and the mouse

```cpp
void HoverCard() {
    auto source = remember([] { return MutableInteractionSource(); });
    bool hovered = source.collectIsHoveredAsState();
    Card(Modifier().hoverable(source).pointerHoverIcon(PointerIcon::Hand), [&] {
        Text(hovered ? "Hovering" : "Point at me", Modifier().padding(24));
    });
}
```

`hoverable(source)` reports the mouse coming over a node and leaving. `pointerHoverIcon(PointerIcon::Hand)` changes the cursor. Text fields and selections show a context menu on a right click.

## Interactions

A `MutableInteractionSource` is told of the presses, hovers, focus and drags of the components it is given to. Read whether one is going on with `collectIsPressedAsState()`, `collectIsHoveredAsState()`, `collectIsFocusedAsState()` and `collectIsDraggedAsState()`; they recompose when it changes.

```cpp
void PressToRecord() {
    auto source = remember([] { return MutableInteractionSource(); });
    bool pressed = source.collectIsPressedAsState();
    Button([] {}, Modifier(), {.interactionSource = source}, [&] { Text(pressed ? "Recording…" : "Hold to record"); });
}
```

`interactions()` gives the stream of interactions as a flow. `Modifier().indication(source, ripple())` draws an indication for interactions that happen somewhere else.

## Drag and drop

`dragAndDropSource` starts a drag with data; `dragAndDropTarget` accepts drops, from inside the app or, on the desktop, files from outside it:

```cpp
void DropZone() {
    auto dropped = remember([] { return mutableStateOf(std::string("Drop text here")); });
    Column([&] {
        Text("Drag me", Modifier().padding(16).dragAndDropSource([](Offset) {
            return std::optional(DragAndDropTransferData{.text = "Hello"});
        }));
        Box(Modifier().size(200).border(2, colorScheme().outline, 12)
                .dragAndDropTarget([](const DragAndDropEvent& e) { return e.hasMimeType("text/plain"); },
                                   {.onDrop = [dropped](const DragAndDropEvent& e) {
                                       dropped.set(e.text());
                                       return true;
                                   }}),
            Alignment::Center, [&] { Text(dropped.get()); });
    });
}
```

## See also

- [Lists and grids](lists.md): scrolling, flinging and nested scrolling.
- [Animation](animation.md): `Animatable` for gestures that animate when let go.
- [Accessibility](accessibility.md): making custom gestures reachable without them.
