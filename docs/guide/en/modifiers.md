# Modifiers

A `Modifier` decorates or augments a composable: its size and padding, its background, what happens when it is clicked, what it tells accessibility services. Modifiers chain, and the order of the chain matters.

## Using modifiers

Most composables take a `modifier` parameter. A chain starts from `Modifier` and each call adds an element:

```kotlin
@Kompozable
fun ArtistCard(name: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { /* open the artist */ }
            .padding(16.dp)
    ) {
        Icon(Icons.Filled.Person, contentDescription = null, modifier = Modifier.size(40.dp))
        Spacer(Modifier.width(12.dp))
        Text(name, style = MaterialTheme.typography.titleMedium)
    }
}
```

Modifiers are plain values: you can store them in variables, pass them around and build them conditionally.

```kotlin body
#! val picked = false
val base = Modifier.fillMaxWidth().padding(8.dp)
Box(modifier = if (picked) base.background(MaterialTheme.colorScheme.secondaryContainer, RoundedCornerShape(12.dp)) else base)
```

## Order matters

Each element wraps everything after it in the chain. The first element is the outermost: it is measured first and draws around the rest. So the same elements in a different order give a different result:

```kotlin body
// The whole 72 dp area is clickable, and the padding is inside the click area.
Box(modifier = Modifier.clickable { }.padding(16.dp).size(40.dp))

// Only the 40 dp box is clickable; the padding is outside it.
Box(modifier = Modifier.padding(16.dp).clickable { }.size(40.dp))
```

The same goes for backgrounds:

```kotlin body
Box(modifier = Modifier.background(MaterialTheme.colorScheme.primaryContainer).padding(16.dp).size(40.dp))   // padding is colored
Box(modifier = Modifier.padding(16.dp).background(MaterialTheme.colorScheme.primaryContainer).size(40.dp))   // padding is not
```

Read a chain from left to right as "from the outside in".

## Common modifiers

### Size

| Modifier | What it does |
|---|---|
| `size(s)`, `size(w, h)`, `width(w)`, `height(h)` | A preferred size, within the parent's constraints. |
| `requiredSize(…)`, `requiredWidth(w)`, `requiredHeight(h)` | The size even if the parent's constraints say otherwise. |
| `widthIn(min, max)`, `heightIn(min, max)`, `sizeIn(…)` | Bounds on the size. |
| `defaultMinSize(w, h)` | A minimum the content may override. |
| `fillMaxWidth(f)`, `fillMaxHeight(f)`, `fillMaxSize(f)` | All of the space the parent offers (or a fraction of it). |
| `wrapContentSize(align)`, `wrapContentWidth`, `wrapContentHeight` | The content's own size, aligned in what the parent forces. |
| `aspectRatio(r)` | A width-to-height ratio within the constraints. |
| `width(IntrinsicSize.Min)`, `height(IntrinsicSize.Max)` | The content's intrinsic size (see [Custom layouts](custom-layouts.md)). |

All lengths are in dp (`16.dp`).

### Padding and position

| Modifier | What it does |
|---|---|
| `padding(all)`, `padding(h, v)`, `padding(start, top, end, bottom)`, `padding(PaddingValues)` | Space around the content. `start` and `end` swap in right-to-left layouts. |
| `absolutePadding(left, top, right, bottom)` | Padding that doesn't mirror. |
| `offset(x, y)` | Moves the content without changing its layout size. |
| `offset { IntOffset(…) }` | The same, read during layout: for offsets that change often (see [Phases](phases.md)). |
| `windowInsetsPadding(insets)`, `safeDrawingPadding()`, `systemBarsPadding()`, `imePadding()`… | Keeps content clear of the system bars, the display cutout or the keyboard. |

### Drawing

| Modifier | What it does |
|---|---|
| `background(color, shape)`, `background(brush, shape)` | Fills behind the content. |
| `border(width, color, shape)` | Draws an outline. |
| `clip(shape)` | Clips the content to a shape. |
| `shadow(elevation, shape)`, `dropShadow`, `innerShadow` | Shadows. |
| `alpha(a)`, `scale(s)`, `rotate(degrees)`, `graphicsLayer(…)`, `graphicsLayer { … }`, `blur(r)` | Changes how the content is drawn, not its layout. |
| `drawBehind { … }`, `drawWithContent { … }`, `drawWithCache { … }` | Your own drawing (see [Graphics](graphics.md)). |

### Interaction

| Modifier | What it does |
|---|---|
| `clickable { … }`, `combinedClickable(onClick, onLongClick)` | Clicks and taps, with a ripple, keyboard activation and accessibility. |
| `selectable(selected, onClick, role)`, `toggleable(value, onChange, role)` | Selection and toggling, for rows that act as a radio button or a checkbox. |
| `draggable`, `anchoredDraggable`, `transformable`, `pointerInput` | Gestures (see [Gestures](gestures.md)). |
| `verticalScroll(state)`, `horizontalScroll(state)`, `scrollable` | Scrolling (see [Lists and grids](lists.md)). |
| `focusable()`, `focusRequester(r)`, `onKeyEvent { … }` | Keyboard focus and keys. |
| `pointerHoverIcon(PointerIcon.Hand)` | The mouse cursor over it. |

### Accessibility and tests

| Modifier | What it does |
|---|---|
| `semantics { … }`, `clearAndSetSemantics { … }` | What accessibility services are told (see [Accessibility](accessibility.md)). |
| `testTag("name")` | A name tests can find the node by (see [Testing](testing.md)). |

### Layout callbacks

| Modifier | What it does |
|---|---|
| `onSizeChanged { size -> … }` | Called when the node's size changes. |
| `onGloballyPositioned { coordinates -> … }` | Called when its position in the window changes. |
| `onVisibilityChanged { visible -> … }` | Called when it becomes visible or stops being. |
| `zIndex(z)` | Draws it above or below its siblings. |

## Scope modifiers

Some modifiers only make sense inside a particular parent, like Compose's scoped modifiers (`RowScope.weight`):

| Modifier | Inside | What it does |
|---|---|---|
| `weight(w)` | `Row`, `Column` | Shares the space left over among weighted children. |
| `align(Alignment.…)` | `Box` | Aligns this child in the box. |
| `align(Alignment.CenterHorizontally)` | `Column` | Aligns this child across the column. |
| `align(Alignment.CenterVertically)`, `alignByBaseline()` | `Row` | Aligns this child across the row. |
| `matchParentSize()` | `Box` | As big as the box, without making it bigger. |
| `fillParentMaxWidth()`, `fillParentMaxHeight()` | Lazy list items | A share of the list's viewport. |
| `animateItem()` | Lazy list items | Animates the item when it appears, moves or goes away. |
| `flex(config)` | `FlexBox` | How the child grows and shrinks. |
| `gridItem(row, column, …)` | `Grid` | Where the child goes in the grid. |

Here they are ordinary members of `Modifier`, not of a scope: nothing stops you from using `weight` outside a `Row` or a `Column`, as Compose's scopes would; it just has no effect there.

```kotlin body
Row(modifier = Modifier.fillMaxWidth()) {
    Text("Left", modifier = Modifier.weight(1f))
    Text("Right")
}
```

## Writing your own

### Combining modifiers

A modifier of your own is an extension function of `Modifier` that adds to the chain it is called on, as in Compose:

```kotlin
fun Modifier.cardSurface(color: Color) =
    shadow(2.dp, RoundedCornerShape(16.dp))
        .background(color, RoundedCornerShape(16.dp))
        .padding(16.dp)

@Kompozable
fun Note(text: String) {
    Box(modifier = Modifier.fillMaxWidth().cardSurface(MaterialTheme.colorScheme.surfaceContainerLow)) { Text(text) }
}
```

kompozc makes it a function that takes the chain first: `Modifier.fillMaxWidth().cardSurface(c)` is `cardSurface(Modifier.fillMaxWidth(), c)`. In its body, `shadow` and the rest are the chain's own, as `this` is. `then` joins two chains: `Modifier.fillMaxWidth().then(other)`.

### Changing measurement

`layout { measurable, constraints -> }` measures the content with the constraints you choose; `layout(width, height) { }` gives this node's size and places the content in it, as in Compose:

```kotlin
// The content centered in a square as big as its longer side.
fun Modifier.squareAround() = layout { measurable, constraints ->
    val placeable = measurable.measure(constraints)
    val side = maxOf(placeable.width, placeable.height)
    layout(side, side) {
        placeable.placeRelative((side - placeable.width) / 2, (side - placeable.height) / 2)
    }
}
```

`placeRelative` places from the start, so the content mirrors right to left; `place` places from the left whatever the direction.

### Custom drawing

`drawBehind` draws behind the content, `drawWithContent` around it. See [Graphics](graphics.md).

```kotlin
fun Modifier.underline(color: Color) = drawBehind {
    drawLine(color, Offset(0f, size.height), Offset(size.width, size.height), 2f)
}
```

### Modifier elements

Underneath, a modifier is a list of `ModifierElement`s. An element can take part in measuring, drawing and input; `then(std::make_shared<MyElement>(…))` adds one. This is a lower-level API, in C++ (see [Writing in C++](cpp.md)), used by the library's own modifiers; the functions above cover most needs. If you write an element, implement `equals` so that a composable called with an equal modifier can still be skipped.

## See also

- [Layouts](layouts.md): the parents scope modifiers work in.
- [Phases](phases.md): modifiers that read state while laying out or drawing.
- [Gestures](gestures.md) and [Graphics](graphics.md) for the interaction and drawing modifiers.
