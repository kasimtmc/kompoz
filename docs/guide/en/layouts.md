# Layouts

Kompoz's layouts arrange children in columns, rows, stacks, wrapping lines and grids, and adapt to the window's size. This page covers the standard layouts; [Custom layouts](custom-layouts.md) shows how to write your own.

## How layout works

Layout happens in one pass down and up the tree:

1. A parent gives each child *constraints*: a minimum and maximum width and height.
2. Each child measures itself within them, measuring its own children first, and reports its size.
3. The parent places its children and reports its own size.

Each node is measured once per pass, so layout stays fast however deep the tree is. All sizes are in dp, laid out on whole pixels as Compose's: sizes, padding, spacing and places are rounded to the nearest pixel (a half up) where Compose rounds them, and a text is as big as the whole pixels around it.

## Column, Row and Box

The three basic layouts:

- `Column` places its children one under another.
- `Row` places them side by side, from the start (left, or right in a right-to-left layout).
- `Box` stacks them on top of each other.

```cpp
void ProfileHeader(const std::string& name, const std::string& handle) {
    Row(Modifier().padding(16), {.verticalAlignment = Alignment::CenterVertically}, [&] {
        Box([&] {
            Icon(Icons::Filled::AccountCircle, "", Modifier().size(56));
            Box(Modifier().size(14).align(Alignment::BottomEnd).background(Color::hex(0x2E7D32), CircleShape));
        });
        Spacer(Modifier().width(12));
        Column([&] {
            Text(name, {.font = typography().titleMedium});
            Text(handle, {.font = typography().bodyMedium, .color = colorScheme().onSurfaceVariant});
        });
    });
}
```

### Arrangement and alignment

A `Column`'s style arranges children along its height and aligns them across its width; a `Row`'s does the opposite:

```cpp body
Column(Modifier().fillMaxSize(), {.verticalArrangement = Arrangement::SpaceBetween,
                                  .horizontalAlignment = Alignment::CenterHorizontally}, [&] {
    Text("Top");
    Text("Middle");
    Text("Bottom");
});
```

| Arrangement | Children are |
|---|---|
| `Arrangement::Start`, `Top` | Packed at the start. |
| `Arrangement::End`, `Bottom` | Packed at the end. |
| `Arrangement::Center` | Packed in the middle. |
| `Arrangement::SpaceBetween` | Spread out, with no space at the ends. |
| `Arrangement::SpaceAround` | Spread out, with half the space at the ends. |
| `Arrangement::SpaceEvenly` | Spread out, with equal space everywhere. |
| `Arrangement::spacedBy(8)` | Packed with 8 dp between each; `spacedBy(8, Arrangement::Kind::Center)` packs them in the middle. |

A single child can override its alignment: `Modifier().align(Alignment::End)` in a `Column`, `Modifier().align(Alignment::CenterVertically)` in a `Row`, `Modifier().align(Alignment::BottomEnd)` in a `Box`. A `Box` aligns all its children with its `contentAlignment` argument: `Box(modifier, Alignment::Center, content)`.

### Weight

`weight` shares the space a `Row` or `Column` has left, after the unweighted children are measured, among the weighted ones in proportion:

```cpp body
Row(Modifier().fillMaxWidth().height(48), [&] {
    Box(Modifier().weight(2).fillMaxHeight().background(colorScheme().primary));    // two thirds
    Box(Modifier().weight(1).fillMaxHeight().background(colorScheme().secondary));  // one third
});
```

`weight(w, false)` lets the child be smaller than its share.

### Spacer

`Spacer(Modifier().width(8))` is empty space; `Spacer(Modifier().weight(1))` pushes what comes after it to the end.

## Wrapping lines

`FlowRow` places children in a row and starts a new row when one is full, as chips or tags need; `FlowColumn` does the same in columns.

```cpp body
#! std::vector<std::string> tags{"C++", "UI", "Compose"};
FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8}, [&] {
    for (const std::string& tag : tags) AssistChip([] {}, [&] { Text(tag); });
});
```

`maxItemsInEachLine` limits how many go on one line.

## FlexBox

`FlexBox` is CSS's flexbox, as in Compose 1.12: a superset of `Row`, `Column` and the flow layouts. Children go along the main axis (`direction`), wrap or not (`wrap`), are spread along it (`justifyContent`) and lined up across their line (`alignItems`). Each child takes its share with `Modifier().flex({.grow, .shrink, .basis})`:

```cpp body
FlexBox(Modifier().fillMaxWidth(), {.wrap = FlexWrap::Wrap, .justifyContent = FlexJustifyContent::SpaceBetween,
                                    .alignItems = FlexAlignItems::Center, .rowGap = 8, .columnGap = 8}, [&] {
    OutlinedTextField("", [](std::string) {}, Modifier().flex({.grow = 1}), {.label = "Search"});
    Button([] {}, [] { Text("Go"); });
});
```

## Grid

`Grid` lays children out in rows and columns, as CSS grid and Compose 1.12's `Grid`. You give the column and row tracks; each child goes in the next free cell, or where `Modifier().gridItem(row, column, rowSpan, columnSpan)` puts it:

```cpp body
Grid(Modifier().fillMaxWidth(), {.columns = {GridTrackSize::Fixed(96), GridTrackSize::Flex(1)}, .rowGap = 8, .columnGap = 16}, [&] {
    Text("Name");
    Text("Ada Lovelace");
    Text("Born");
    Text("10 December 1815");
    Text("Known for", Modifier().gridItem(3, 1));
    Text("The first published algorithm", Modifier().gridItem(3, 2));
});
```

Track sizes: `Fixed(dp)`, `Percentage(fraction)`, `Flex(weight)` (a share of the room left), `MinMax(minDp, weight)`, `MinContent()`, `MaxContent()` and `Auto()`. Named areas in the config let children say `Modifier().gridItem("header")`.

## Scrolling

A layout doesn't scroll by itself. `verticalScroll` and `horizontalScroll` make a `Column` or `Row` scroll when its content is bigger than the space it has:

```cpp body
Column(Modifier().fillMaxSize().verticalScroll(rememberScrollState()), [&] {
    for (int i = 0; i < 50; ++i) Text("Line " + std::to_string(i));
});
```

This composes every child. For long or endless content, use a lazy list instead: see [Lists and grids](lists.md).

## Adapting to the window

### Size classes

Lay out by the window's size class rather than by exact sizes. `currentWindowAdaptiveInfo()` gives the window's width and height classes, as Compose's adaptive library does: Compact (narrower than 600 dp), Medium (narrower than 840 dp) or Expanded.

```cpp
#! void ListPane() {}
#! void DetailPane() {}
void MailScreen() {
    WindowAdaptiveInfo info = currentWindowAdaptiveInfo();
    if (info.windowSizeClass.windowWidthSizeClass == WindowWidthSizeClass::Expanded) {
        Row(Modifier().fillMaxSize(), [&] {
            Box(Modifier().weight(1), [] { ListPane(); });
            Box(Modifier().weight(2), [] { DetailPane(); });
        });
    } else {
        ListPane();
    }
}
```

It recomposes when the window changes class. `NavigationSuiteScaffold` uses it to switch between a navigation bar and a rail (see [Material components](material.md)).

### Media queries

`mediaQuery` asks about what the app runs on: the window's size, the most precise pointer, whether there is a keyboard. `derivedMediaQuery` gives a state that changes only when the answer does, which is what you want for the window's size, since that changes at every step of a resize:

```cpp
#! void TwoPanes() {}
#! void OnePane() {}
void Adaptive() {
    auto wide = derivedMediaQuery([](const UiMediaScope& m) { return m.windowWidth() >= 600; });
    if (wide.get()) TwoPanes(); else OnePane();

    if (mediaQuery([](const UiMediaScope& m) { return m.pointerPrecision() == PointerPrecision::Fine; }))
        Text("Hover for details");
}
```

### BoxWithConstraints

`BoxWithConstraints` gives its content the constraints the box is laid out with, so the content can choose a layout by the space it actually has, not by the window's:

```cpp body
BoxWithConstraints(Modifier().fillMaxWidth(), [&](const Constraints& c) {
    if (c.maxWidth > 400) Text("A wide layout");
    else Text("A narrow layout");
});
```

The very first composition sees unbounded constraints; the content recomposes in the same frame once they are known.

## Window insets

On phones, the system bars, the display cutout and the keyboard cover parts of the window. *Insets* tell you how much, so content can stay clear of them:

- `Scaffold` passes its content padding that already keeps clear of the bars and the system insets.
- `Modifier().safeDrawingPadding()`, `systemBarsPadding()`, `statusBarsPadding()`, `navigationBarsPadding()` and `imePadding()` pad by one kind of inset.
- `windowInsetsPadding(WindowInsets::systemBars())` takes any `WindowInsets`.

Insets paddings don't add up: an inner one pads only by what outer ones didn't already take. In a `Scaffold` with a text field, pad the content as Compose does:

```cpp body
Scaffold(Modifier(), {}, [&](PaddingValues padding) {
    Column(Modifier().padding(padding).consumeWindowInsets(padding).imePadding(), [&] {
        OutlinedTextField("", [](std::string) {});
    });
});
```

## Right to left

When the system language is written right to left (Arabic, Hebrew, Persian…), the layout direction is right to left: `Row`s start at the right, `start` and `end` padding swap, and horizontal lists scroll from the right. `CompositionLocalProvider({LocalLayoutDirection.provides(LayoutDirection::Rtl)}, …)` changes it for part of the tree, and `KOMPOZ_LAYOUT_DIRECTION=rtl` for the whole app. Drawing, `absoluteOffset` and `absolutePadding` don't mirror.

## See also

- [Modifiers](modifiers.md): size, padding and the scope modifiers.
- [Custom layouts](custom-layouts.md): `Layout`, intrinsics and `SubcomposeLayout`.
- [Lists and grids](lists.md): lazy layouts for long content.
