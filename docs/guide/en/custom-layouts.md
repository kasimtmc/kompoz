# Custom layouts

When `Row`, `Column`, `Box` and the other layouts don't fit, measure and place children yourself with `Layout`. This page also covers the layout modifier, intrinsic sizes, `SubcomposeLayout` and animating layout changes.

## Constraints

A parent measures each child with `Constraints`: a minimum and maximum width and height, in dp. A child picks a size within them. Some helpers:

| | |
|---|---|
| `Constraints::fixed(w, h)` | Exactly this size. |
| `c.loosen()` | The same maximums, with minimums of zero: the child may be smaller. |
| `c.constrain(size)` | `size` clamped into the constraints. |
| `c.hasBoundedWidth()`, `c.hasBoundedHeight()` | Whether the maximum is finite: inside a vertical scroll, the height isn't. |
| `c.deflate(h, v)` | The constraints with `h` and `v` dp taken off, for padding. |

## Layout

`Layout(modifier, content, measurePolicy)` composes `content` and gives each of its children to `measurePolicy` as a `Measurable`. The policy measures them, places them and returns the layout's own size:

```cpp
// Children one under another, each pushed right by `indent` dp more than the one before.
void Staircase(float indent, const Content& content) {
    Layout(Modifier(), content, [indent](const std::vector<Measurable>& children, const Constraints& c) {
        float x = 0, y = 0, width = 0;
        for (const Measurable& child : children) {
            Placeable p = child.measure(c.loosen());
            p.place(x, y);
            width = std::max(width, x + p.width);
            x += indent;
            y += p.height;
        }
        return c.constrain(Size{width, y});
    });
}

void Steps() {
    Staircase(24, [] {
        Text("Wake up");
        Text("Make coffee");
        Text("Write code");
    });
}
```

- `measure(constraints)` measures a child and returns a `Placeable` with its `width` and `height`. Measure each child once per pass.
- `place(x, y)` puts it at that offset from the layout's start and top; an optional third argument is its z-index. In a right-to-left layout Kompoz mirrors what you placed, as Compose's `placeRelative` does: write the policy as if the layout were left to right.
- Return a size within the constraints you were given; `c.constrain` makes sure of it.

The policy runs during layout, not composition. A state it reads lays the layout out again when it changes, without recomposing.

### Telling children apart

A policy sees its children in order. To tell them apart by role, tag them with `Modifier().layoutId("name")` and read `child.layoutId()`:

```cpp
void LabeledValue(const std::string& label, const std::string& value) {
    Layout(Modifier().fillMaxWidth(), [&] {
        Text(label, Modifier().layoutId("label"));
        Text(value, Modifier().layoutId("value"), {.fontWeight = FontWeight::Bold});
    }, [](const std::vector<Measurable>& children, const Constraints& c) {
        float height = 0;
        for (const Measurable& child : children) {
            Placeable p = child.measure(c.loosen());
            // The label at the start, the value at the end.
            p.place(child.layoutId() == "value" ? c.maxWidth - p.width : 0, 0);
            height = std::max(height, p.height);
        }
        return c.constrain(Size{c.maxWidth, height});
    });
}
```

### Alignment lines

A `Placeable` knows where its text baselines are: `p[AlignmentLine::FirstBaseline]` is the first baseline's distance from its top, or NaN if it has none. Use it to line up text of different sizes. The `alignByBaseline()` and `paddingFromBaseline(top, bottom)` modifiers cover the common cases without a custom layout.

## The layout modifier

To change how a single composable is measured or placed, use `Modifier().layout` instead of wrapping it in a `Layout`. It measures the content through `measure`, returns the node's size, and sets `placeAt` to where the content goes inside it:

```cpp
// At least `min` dp tall, the content centered vertically in that.
Modifier atLeastHeight(float min) {
    return Modifier().layout([min](const MeasureNext& measure, const Constraints& c, Offset& placeAt) {
        Size content = measure(c);
        float height = std::max(content.height, std::min(min, c.maxHeight));
        placeAt = {0, (height - content.height) / 2};
        return Size{content.width, height};
    });
}
```

## Intrinsic sizes

Layout measures each child once, so a parent can't measure a child twice to find out what size it would like. *Intrinsic* sizes answer that question without measuring for real: how tall would this content be at its natural size?

The typical use is making siblings as tall as the tallest one:

```cpp body
Row(Modifier().height(IntrinsicSize::Min), [&] {
    Text("A short label", Modifier().weight(1));
    VerticalDivider(Modifier().fillMaxHeight());   // as tall as the taller text
    Text("A label that wraps onto more than one line", Modifier().weight(1));
});
```

`height(IntrinsicSize::Min)` and `width(IntrinsicSize::Min)` (or `Max`) size the node to its content's intrinsic size, and children that fill it then fill that.

> **Note:** Kompoz's intrinsics are an approximation of Compose's: both `Min` and `Max` measure the content with that axis unbounded, and a `Layout` can't override how its intrinsic sizes are computed. For a min intrinsic width, text breaks at every space, so it comes out as wide as its widest word.

## SubcomposeLayout

Sometimes what to compose depends on a measurement: a header sized to match a body, a different layout for narrow constraints, a badge sized by the text it decorates. `SubcomposeLayout` composes parts of its content *while* it measures:

```cpp
// The content, with a line under it exactly as wide as the content turned out to be.
void Underlined(const Content& content) {
    SubcomposeLayout(Modifier(), [content](SubcomposeMeasureScope& s, const Constraints& c) {
        float width = 0, height = 0;
        for (const Measurable& m : s.subcompose(std::string("main"), content)) {
            Placeable p = m.measure(c.loosen());
            p.place(0, height);
            width = std::max(width, p.width);
            height += p.height;
        }
        // Composed with the width it has to match; composed again when that changes.
        for (const Measurable& m : s.subcompose(std::string("line"), width, [width] {
                 Box(Modifier().size(width, 2).background(colorScheme().primary));
             })) {
            Placeable line = m.measure(c.loosen());
            line.place(0, height);
            height += line.height;
        }
        return c.constrain(Size{width, height});
    });
}
```

- `subcompose(slotId, content)` composes a slot and returns its measurables. A slot is composed the first time it is asked for, and the layout runs again in the same frame.
- `subcompose(slotId, dependency, content)` composes the slot again when `dependency` changes.
- The content runs later, during composition, so capture by value.

`SubcomposeLayout` costs more than `Layout`; use it only when composition really depends on measurement. `BoxWithConstraints` is the common case of it, ready-made.

## Animating layout changes

`Modifier().animateContentSize()` animates a node's size when its content grows or shrinks.

For a node that moves or resizes because the layout around it changed, `Modifier().animateBounds()` makes it glide from where it was drawn to where it is now, as Compose's `animateBounds`. Its position is measured from the nearest `LookaheadScope`:

```cpp
void Chips(bool wide) {
    LookaheadScope(Modifier().fillMaxWidth(), [&] {
        FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .maxItemsInEachLine = wide ? 4 : 2}, [&] {
            for (const char* name : {"One", "Two", "Three", "Four"})
                AssistChip([] {}, [&] { Text(name); }, {}, Modifier().animateBounds());
        });
    });
}
```

Only the drawing moves; taps land where the node is laid out. `skipToLookaheadSize()` lays content out at the size it is going to rather than the sizes along the way, and `approachLayout` lets you write the in-between steps yourself.

## See also

- [Layouts](layouts.md): the built-in layouts.
- [Phases](phases.md): what reading state during layout costs.
- [Modifiers](modifiers.md): the modifier chain that wraps every layout node.
