# Modifiers

A `Modifier` decorates or augments a composable: its size and padding, its background, what happens when it is clicked, what it tells accessibility services. Modifiers chain, and the order of the chain matters.

## Using modifiers

Most composables take a `Modifier` as a parameter. A chain starts from `Modifier()` and each call adds an element:

```cpp
void ArtistCard(const std::string& name) {
    Row(Modifier()
            .fillMaxWidth()
            .clickable([] { /* open the artist */ })
            .padding(16),
        [&] {
            Icon(Icons::Filled::Person, "", Modifier().size(40));
            Spacer(Modifier().width(12));
            Text(name, {.font = typography().titleMedium});
        });
}
```

Modifiers are plain values: you can store them in variables, pass them around and build them conditionally.

```cpp body
#! bool selected = false;
Modifier base = Modifier().fillMaxWidth().padding(8);
Box(selected ? base.background(colorScheme().secondaryContainer, 12) : base);
```

## Order matters

Each element wraps everything after it in the chain. The first element is the outermost: it is measured first and draws around the rest. So the same elements in a different order give a different result:

```cpp body
// The whole 72 dp area is clickable, and the padding is inside the click area.
Box(Modifier().clickable([] {}).padding(16).size(40));

// Only the 40 dp box is clickable; the padding is outside it.
Box(Modifier().padding(16).clickable([] {}).size(40));
```

The same goes for backgrounds:

```cpp body
Box(Modifier().background(colorScheme().primaryContainer).padding(16).size(40));   // padding is colored
Box(Modifier().padding(16).background(colorScheme().primaryContainer).size(40));   // padding is not
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
| `width(IntrinsicSize::Min)`, `height(IntrinsicSize::Max)` | The content's intrinsic size (see [Custom layouts](custom-layouts.md)). |

All lengths are in dp.

### Padding and position

| Modifier | What it does |
|---|---|
| `padding(all)`, `padding(h, v)`, `padding(start, top, end, bottom)`, `padding(PaddingValues)` | Space around the content. `start` and `end` swap in right-to-left layouts. |
| `absolutePadding(left, top, right, bottom)` | Padding that doesn't mirror. |
| `offset(x, y)` | Moves the content without changing its layout size. |
| `offset([] { return Offset{…}; })` | The same, read during layout: for offsets that change often (see [Phases](phases.md)). |
| `windowInsetsPadding(insets)`, `safeDrawingPadding()`, `systemBarsPadding()`, `imePadding()`… | Keeps content clear of the system bars, the display cutout or the keyboard. |

### Drawing

| Modifier | What it does |
|---|---|
| `background(color, shape)`, `background(brush, shape)` | Fills behind the content. |
| `border(width, color, shape)` | Draws an outline. |
| `clip(shape)` | Clips the content to a shape. |
| `shadow(elevation, shape)`, `dropShadow`, `innerShadow` | Shadows. |
| `alpha(a)`, `scale(s)`, `rotate(degrees)`, `graphicsLayer(…)`, `blur(r)` | Changes how the content is drawn, not its layout. |
| `drawBehind(…)`, `drawWithContent(…)`, `drawWithCache(…)` | Your own drawing (see [Graphics](graphics.md)). |

### Interaction

| Modifier | What it does |
|---|---|
| `clickable(onClick)`, `combinedClickable(onClick, onLongClick)` | Clicks and taps, with a ripple, keyboard activation and accessibility. |
| `selectable(selected, onClick, role)`, `toggleable(value, onChange, role)` | Selection and toggling, for rows that act as a radio button or a checkbox. |
| `draggable`, `anchoredDraggable`, `transformable`, `pointerInput` | Gestures (see [Gestures](gestures.md)). |
| `verticalScroll(state)`, `horizontalScroll(state)`, `scrollable` | Scrolling (see [Lists and grids](lists.md)). |
| `focusable()`, `focusRequester(r)`, `onKeyEvent(…)` | Keyboard focus and keys. |
| `pointerHoverIcon(PointerIcon::Hand)` | The mouse cursor over it. |

### Accessibility and tests

| Modifier | What it does |
|---|---|
| `semantics({…})`, `clearAndSetSemantics({…})` | What accessibility services are told (see [Accessibility](accessibility.md)). |
| `testTag("name")` | A name tests can find the node by (see [Testing](testing.md)). |

### Layout callbacks

| Modifier | What it does |
|---|---|
| `onSizeChanged([](Size s) { … })` | Called when the node's size changes. |
| `onGloballyPositioned([](const LayoutCoordinates& c) { … })` | Called when its position in the window changes. |
| `onVisibilityChanged([](bool visible) { … })` | Called when it becomes visible or stops being. |
| `zIndex(z)` | Draws it above or below its siblings. |

## Scope modifiers

Some modifiers only make sense inside a particular parent, like Compose's scoped modifiers (`RowScope.weight`):

| Modifier | Inside | What it does |
|---|---|---|
| `weight(w)` | `Row`, `Column` | Shares the space left over among weighted children. |
| `align(Alignment::…)` | `Box` | Aligns this child in the box. |
| `align(Alignment::CenterHorizontally)` | `Column` | Aligns this child across the column. |
| `align(Alignment::CenterVertically)`, `alignByBaseline()` | `Row` | Aligns this child across the row. |
| `matchParentSize()` | `Box` | As big as the box, without making it bigger. |
| `fillParentMaxWidth()`, `fillParentMaxHeight()` | Lazy list items | A share of the list's viewport. |
| `animateItem()` | Lazy list items | Animates the item when it appears, moves or goes away. |
| `flex(config)` | `FlexBox` | How the child grows and shrinks. |
| `gridItem(row, column, …)` | `Grid` | Where the child goes in the grid. |

In C++ these are ordinary `Modifier` methods: the compiler won't stop you from using `weight` outside a `Row` or a `Column`, it just has no effect there.

```cpp body
Row(Modifier().fillMaxWidth(), [&] {
    Text("Left", Modifier().weight(1));
    Text("Right");
});
```

## Writing your own

### Combining modifiers

C++ has no extension functions, so a modifier of your own is a function that returns a chain. Join it to another chain with `then`:

```cpp
Modifier cardSurface() {
    return Modifier().shadow(2, RoundedCornerShape(16)).background(colorScheme().surfaceContainerLow, RoundedCornerShape(16)).padding(16);
}

void Note(const std::string& text) {
    Box(Modifier().fillMaxWidth().then(cardSurface()), [&] { Text(text); });
}
```

A function that takes the chain to extend reads naturally, too: `Modifier card(const Modifier& m = {}) { return m.padding(…); }`.

### Changing measurement

`layout` measures the content through `measure` with the constraints you choose and returns this node's size; `placeAt` says where the content goes inside it:

```cpp
// The content centered in a square as big as its longer side.
Modifier squareAround() {
    return Modifier().layout([](const MeasureNext& measure, const Constraints& c, Offset& placeAt) {
        Size content = measure(c.loosen());
        float side = std::max(content.width, content.height);
        placeAt = {(side - content.width) / 2, (side - content.height) / 2};
        return c.constrain(Size{side, side});
    });
}
```

### Custom drawing

`drawBehind` draws behind the content, `drawWithContent` around it. See [Graphics](graphics.md).

```cpp
Modifier underline(Color color) {
    return Modifier().drawBehind([color](DrawScope& d) {
        d.drawLine(color, {0, d.size().height}, {d.size().width, d.size().height}, 2);
    });
}
```

### Modifier elements

Underneath, a modifier is a list of `ModifierElement`s. An element can take part in measuring, drawing and input; `then(std::make_shared<MyElement>(…))` adds one. This is a lower-level API, used by the library's own modifiers; the functions above cover most needs. If you write an element, implement `equals` so that a composable called with an equal modifier can still be skipped.

## See also

- [Layouts](layouts.md): the parents scope modifiers work in.
- [Phases](phases.md): modifiers that read state while laying out or drawing.
- [Gestures](gestures.md) and [Graphics](graphics.md) for the interaction and drawing modifiers.
