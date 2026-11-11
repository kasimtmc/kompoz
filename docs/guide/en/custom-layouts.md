# Custom layouts

When `Row`, `Column`, `Box` and the other layouts don't fit, measure and place children yourself with `Layout`. This page also covers the layout modifier, intrinsic sizes, `SubkompozLayout` and animating layout changes.

## Constraints

A parent measures each child with `Constraints`: a minimum and maximum width and height, in dp. A child picks a size within them. Some helpers:

| | |
|---|---|
| `Constraints.fixed(w, h)` | Exactly this size. |
| `constraints.copy(minWidth = 0, minHeight = 0)` | The same maximums, with minimums of zero: the child may be smaller. |
| `constraints.constrainWidth(w)`, `constrainHeight(h)` | A width or height clamped into the constraints. |
| `constraints.hasBoundedWidth`, `hasBoundedHeight` | Whether the maximum is finite: inside a vertical scroll, the height isn't. |
| `constraints.offset(-h, -v)` | The constraints with `h` and `v` dp taken off, for padding. |

They are in dp, and floats: where Compose's are whole pixels (`indent.roundToPx()`), here they are the dp themselves.

## Layout

`Layout(content, modifier) { measurables, constraints -> … }` composes `content` and gives each of its children to the measure policy, its lambda, as a `Measurable`. The policy measures them, and `layout(width, height) { … }` gives the layout's own size and places them:

```kotlin
// Children one under another, each pushed right by `indent` more than the one before.
@Kompozable
fun Staircase(indent: Dp, modifier: Modifier = Modifier, content: @Kompozable () -> Unit) {
    Layout(content = content, modifier = modifier) { measurables, constraints ->
        val placeables = measurables.map { it.measure(constraints.copy(minWidth = 0, minHeight = 0)) }
        var width = 0f
        var height = 0f
        placeables.forEachIndexed { i, placeable ->
            width = maxOf(width, i * indent + placeable.width)
            height += placeable.height
        }
        layout(constraints.constrainWidth(width), constraints.constrainHeight(height)) {
            var y = 0f
            placeables.forEachIndexed { i, placeable ->
                placeable.placeRelative(x = i * indent, y = y)
                y += placeable.height
            }
        }
    }
}

@Kompozable
fun Steps() {
    Staircase(24.dp) {
        Text("Wake up")
        Text("Make coffee")
        Text("Write code")
    }
}
```

- `measure(constraints)` measures a child and returns a `Placeable` with its `width` and `height`. Measure each child once per pass.
- In `layout`'s block, `placeRelative(x, y)` puts a child at that offset from the layout's start and top, mirrored in a right-to-left layout: write the policy as if the layout were left to right. `place(x, y)` places from the left whatever the direction; both take a `zIndex`.
- Give a size within the constraints you were given; `constrainWidth` and `constrainHeight` make sure of it.
- `forEach { }`, `map { }` and Kotlin's other inline functions run their lambda at once, so it changes the `var`s around it (`y` here), as in Kotlin.

The policy runs during layout, not composition. A state it reads lays the layout out again when it changes, without recomposing.

### Telling children apart

A policy sees its children in order. To tell them apart by role, tag them with `Modifier.layoutId("name")` and read `layoutId`:

```kotlin
@Kompozable
fun LabeledValue(label: String, value: String) {
    Layout(
        content = {
            Text(label, modifier = Modifier.layoutId("label"))
            Text(value, modifier = Modifier.layoutId("value"), fontWeight = FontWeight.Bold)
        },
        modifier = Modifier.fillMaxWidth(),
    ) { measurables, constraints ->
        val loose = constraints.copy(minWidth = 0, minHeight = 0)
        val labelPlaceable = measurables.first { it.layoutId == "label" }.measure(loose)
        val valuePlaceable = measurables.first { it.layoutId == "value" }.measure(loose)
        layout(constraints.maxWidth, maxOf(labelPlaceable.height, valuePlaceable.height)) {
            // The label at the start, the value at the end.
            labelPlaceable.placeRelative(0, 0)
            valuePlaceable.placeRelative(constraints.maxWidth - valuePlaceable.width, 0)
        }
    }
}
```

### Alignment lines

A `Placeable` knows where its text baselines are: `placeable[FirstBaseline]` is the first baseline's distance from its top, or NaN if it has none (Compose's `AlignmentLine.Unspecified`). Use it to line up text of different sizes. The `alignByBaseline()` and `paddingFromBaseline(top, bottom)` modifiers cover the common cases without a custom layout.

## The layout modifier

To change how a single composable is measured or placed, use `Modifier.layout { measurable, constraints -> … }` instead of wrapping it in a `Layout`: it measures the content, and `layout(width, height) { … }` gives the node's size and places the content in it:

```kotlin
// At least `min` tall, the content centered vertically in that.
fun Modifier.atLeastHeight(min: Dp) = layout { measurable, constraints ->
    val placeable = measurable.measure(constraints)
    val height = maxOf(placeable.height, minOf(min, constraints.maxHeight))
    layout(placeable.width, height) {
        placeable.placeRelative(0f, (height - placeable.height) / 2)
    }
}
```

## Intrinsic sizes

Layout measures each child once, so a parent can't measure a child twice to find out what size it would like. *Intrinsic* sizes answer that question without measuring for real: how tall would this content be at its natural size?

The typical use is making siblings as tall as the tallest one:

```kotlin body
Row(modifier = Modifier.height(IntrinsicSize.Min)) {
    Text("A short label", modifier = Modifier.weight(1f))
    VerticalDivider(modifier = Modifier.fillMaxHeight())   // as tall as the taller text
    Text("A label that wraps onto more than one line", modifier = Modifier.weight(1f))
}
```

`height(IntrinsicSize.Min)` and `width(IntrinsicSize.Min)` (or `Max`) size the node to its content's intrinsic size, and children that fill it then fill that.

> **Note:** Kompoz's intrinsics are an approximation of Compose's: both `Min` and `Max` measure the content with that axis unbounded, and a `Layout` can't override how its intrinsic sizes are computed. For a min intrinsic width, text breaks at every space, so it comes out as wide as its widest word.

## SubkompozLayout

Sometimes what to compose depends on a measurement: a header sized to match a body, a different layout for narrow constraints, a badge sized by the text it decorates. `SubkompozLayout` composes parts of its content *while* it measures:

```kotlin
// The content, with a line under it exactly as wide as the content turned out to be.
@Kompozable
fun Underlined(content: @Kompozable () -> Unit) {
    SubkompozLayout { constraints ->
        val loose = constraints.copy(minWidth = 0, minHeight = 0)
        val main = subkompoz("main", content).map { it.measure(loose) }
        val width = main.maxOf { it.width }
        val height = main.sumOf { it.height }
        // Composed with the width it has to match; composed again when that changes.
        val line = subkompoz("line", width) {
            Box(modifier = Modifier.size(width, 2.dp).background(MaterialTheme.colorScheme.primary))
        }.map { it.measure(loose) }
        layout(width, height + line.sumOf { it.height }) {
            var y = 0f
            main.forEach { it.placeRelative(0, y); y += it.height }
            line.forEach { it.placeRelative(0, y) }
        }
    }
}
```

- `subkompoz(slotId, content)` composes a slot and returns its measurables: the first time it is asked for, it is composed there and then, as Compose's is.
- `subkompoz(slotId, dependency, content)`, Kompoz's, composes the slot again when `dependency` changes (Compose's composes a slot again whenever it is given new content).
- The content runs later, during composition; it captures what it uses by value, as every lambda here does.

`SubkompozLayout` costs more than `Layout`; use it only when composition really depends on measurement. `BoxWithConstraints` is the common case of it, ready-made.

## Animating layout changes

`Modifier.animateContentSize()` animates a node's size when its content grows or shrinks.

For a node that moves or resizes because the layout around it changed, `Modifier.animateBounds()` makes it glide from where it was drawn to where it is now, as Compose's `animateBounds`. Its position is measured from the nearest `LookaheadScope` (Compose's names it, `animateBounds(this@LookaheadScope)`):

```kotlin
@Kompozable
fun Chips(wide: Boolean) {
    LookaheadScope(modifier = Modifier.fillMaxWidth()) {
        FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            maxItemsInEachLine = if (wide) 4 else 2,
        ) {
            for (name in listOf("One", "Two", "Three", "Four")) {
                Text(name, modifier = Modifier.animateBounds().padding(8.dp))
            }
        }
    }
}
```

Only the drawing moves; taps land where the node is laid out. `skipToLookaheadSize()` lays content out at the size it is going to rather than the sizes along the way, and `approachLayout` lets you write the in-between steps yourself.

## See also

- [Layouts](layouts.md): the built-in layouts.
- [Phases](phases.md): what reading state during layout costs.
- [Modifiers](modifiers.md): the modifier chain that wraps every layout node.
