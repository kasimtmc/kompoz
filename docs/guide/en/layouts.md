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

```kotlin
@Composable
fun ProfileHeader(name: String, handle: String) {
    Row(modifier = Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
        Box {
            Icon(Icons.Filled.AccountCircle, contentDescription = null, modifier = Modifier.size(56.dp))
            Box(modifier = Modifier.size(14.dp).align(Alignment.BottomEnd).background(Color(0xFF2E7D32), CircleShape))
        }
        Spacer(Modifier.width(12.dp))
        Column {
            Text(name, style = MaterialTheme.typography.titleMedium)
            Text(handle, color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        }
    }
}
```

### Arrangement and alignment

A `Column`'s style arranges children along its height and aligns them across its width; a `Row`'s does the opposite:

```kotlin body
Column(
    modifier = Modifier.fillMaxSize(),
    verticalArrangement = Arrangement.SpaceBetween,
    horizontalAlignment = Alignment.CenterHorizontally,
) {
    Text("Top")
    Text("Middle")
    Text("Bottom")
}
```

| Arrangement | Children are |
|---|---|
| `Arrangement.Start`, `Top` | Packed at the start. |
| `Arrangement.End`, `Bottom` | Packed at the end. |
| `Arrangement.Center` | Packed in the middle. |
| `Arrangement.SpaceBetween` | Spread out, with no space at the ends. |
| `Arrangement.SpaceAround` | Spread out, with half the space at the ends. |
| `Arrangement.SpaceEvenly` | Spread out, with equal space everywhere. |
| `Arrangement.spacedBy(8.dp)` | Packed with 8 dp between each; `spacedBy(8.dp, Arrangement.Kind.Center)` packs them in the middle. |

A single child can override its alignment: `Modifier.align(Alignment.End)` in a `Column`, `Modifier.align(Alignment.CenterVertically)` in a `Row`, `Modifier.align(Alignment.BottomEnd)` in a `Box`. A `Box` aligns all its children with its `contentAlignment`: `Box(contentAlignment = Alignment.Center) { … }`.

### Weight

`weight` shares the space a `Row` or `Column` has left, after the unweighted children are measured, among the weighted ones in proportion:

```kotlin body
Row(modifier = Modifier.fillMaxWidth().height(48.dp)) {
    Box(modifier = Modifier.weight(2f).fillMaxHeight().background(MaterialTheme.colorScheme.primary))    // two thirds
    Box(modifier = Modifier.weight(1f).fillMaxHeight().background(MaterialTheme.colorScheme.secondary))  // one third
}
```

`weight(1f, false)` (`fill` false) lets the child be smaller than its share.

### Spacer

`Spacer(Modifier.width(8.dp))` is empty space; `Spacer(Modifier.weight(1f))` pushes what comes after it to the end.

## Wrapping lines

`FlowRow` places children in a row and starts a new row when one is full, as chips or tags need; `FlowColumn` does the same in columns. Their arrangements' spacing goes between the items and between the lines.

```kotlin body
#! val tags = listOf("C++", "UI", "Compose")
FlowRow(
    modifier = Modifier.fillMaxWidth(),
    horizontalArrangement = Arrangement.spacedBy(8.dp),
    verticalArrangement = Arrangement.spacedBy(8.dp),
) {
    for (tag in tags) {
        Box(modifier = Modifier.background(MaterialTheme.colorScheme.secondaryContainer, RoundedCornerShape(8.dp)).padding(horizontal = 12.dp, vertical = 6.dp)) {
            Text(tag)
        }
    }
}
```

`maxItemsInEachLine` limits how many go on one line.

## FlexBox

`FlexBox` is CSS's flexbox, as in Compose 1.12: a superset of `Row`, `Column` and the flow layouts. Its `config` says how: children go along the main axis (`direction`), wrap or not (`wrap`), are spread along it (`justifyContent`) and lined up across their line (`alignItems`). Each child takes its share with `Modifier.flex { grow(1f) }` (and `shrink`, `basis`, `alignSelf`, `order`):

```kotlin body
var query by remember { mutableStateOf("") }
FlexBox(
    modifier = Modifier.fillMaxWidth(),
    config = {
        wrap(FlexWrap.Wrap)
        justifyContent(FlexJustifyContent.SpaceBetween)
        alignItems(FlexAlignItems.Center)
        gap(8.dp)
    },
) {
    OutlinedTextField(value = query, onValueChange = { query = it }, modifier = Modifier.flex { grow(1f) })
    Button(onClick = { }) { Text("Go") }
}
```

The config runs while the box is laid out, with the box's `constraints`: `direction(if (constraints.maxWidth < 400.dp.roundToPx()) FlexDirection.Column else FlexDirection.Row)` picks the direction by the room there is, and a state read in it lays the box out again without recomposing. `basis(80.dp)` sets a child's size to start from; a share of the box is `basis(FlexBasis.Percent(0.5f))` (Compose's `basis(0.5f)`: a dp is a float here).

## Grid

`Grid` lays children out in rows and columns, as CSS grid and Compose 1.12's `Grid`. Its `config` gives the column and row tracks; each child goes in the next free cell, or where `Modifier.gridItem(row = …, column = …, rowSpan = …, columnSpan = …)` puts it:

```kotlin body
Grid(
    modifier = Modifier.fillMaxWidth(),
    config = {
        column(96.dp)
        column(1.fr)
        gap(row = 8.dp, column = 16.dp)
    },
) {
    Text("Name")
    Text("Ada Lovelace")
    Text("Born")
    Text("10 December 1815")
    Text("Known for", modifier = Modifier.gridItem(row = 3, column = 1))
    Text("The first published algorithm", modifier = Modifier.gridItem(row = 3, column = 2))
}
```

Tracks: `column(96.dp)` is fixed, `column(1.fr)` a share of the room left, `column(minmax(0.dp, 1.fr))` a share from a minimum; `GridTrackSize.Percentage(0.5f)`, `GridTrackSize.MinContent`, `MaxContent` and `Auto` go in `column(…)` and `row(…)` too. `area("header", row = 1, columnSpan = 2)` names cells that a child takes with `Modifier.gridItem("header")`. As a `FlexBox`'s, the config runs while the grid is laid out and sees its `constraints`.

## Scrolling

A layout doesn't scroll by itself. `verticalScroll` and `horizontalScroll` make a `Column` or `Row` scroll when its content is bigger than the space it has:

```kotlin body
Column(modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
    for (i in 0 until 50) Text("Line $i")
}
```

This composes every child. For long or endless content, use a lazy list instead: see [Lists and grids](lists.md).

## Adapting to the window

### Size classes

Lay out by the window's size class rather than by exact sizes. `currentWindowAdaptiveInfo()` gives the window's width and height classes, as Compose's adaptive library does: Compact (narrower than 600 dp), Medium (narrower than 840 dp) or Expanded.

```kotlin
#! @Composable fun ListPane() {}
#! @Composable fun DetailPane() {}
@Composable
fun MailScreen() {
    val info = currentWindowAdaptiveInfo()
    if (info.windowSizeClass.windowWidthSizeClass == WindowWidthSizeClass.Expanded) {
        Row(modifier = Modifier.fillMaxSize()) {
            Box(modifier = Modifier.weight(1f)) { ListPane() }
            Box(modifier = Modifier.weight(2f)) { DetailPane() }
        }
    } else {
        ListPane()
    }
}
```

It recomposes when the window changes class. `NavigationSuiteScaffold` uses it to switch between a navigation bar and a rail (see [Material components](material.md)).

### Media queries

`mediaQuery { … }` asks about what the app runs on, as Compose's: the window's size, the most precise pointer, whether there is a keyboard. `derivedMediaQuery { … }` gives a state that changes only when the answer does, which is what you want for the window's size, since that changes at every step of a resize:

```kotlin
#! @Composable fun TwoPanes() {}
#! @Composable fun OnePane() {}
@Composable
fun Adaptive() {
    val wide by derivedMediaQuery { windowWidth >= 600.dp }
    if (wide) TwoPanes() else OnePane()

    if (mediaQuery { pointerPrecision == UiMediaScope.PointerPrecision.Fine }) {
        Text("Hover for details")
    }
}
```

### BoxWithConstraints

`BoxWithConstraints` gives its content the constraints the box is laid out with (`maxWidth`, `minHeight` and the rest, and `constraints`), so the content can choose a layout by the space it actually has, not by the window's:

```kotlin body
BoxWithConstraints(modifier = Modifier.fillMaxWidth()) {
    if (maxWidth > 400.dp) Text("A wide layout")
    else Text("A narrow layout")
}
```

The very first composition sees unbounded constraints; the content recomposes in the same frame once they are known.

## Window insets

On phones, the system bars, the display cutout and the keyboard cover parts of the window. *Insets* tell you how much, so content can stay clear of them:

- `Scaffold` passes its content padding that already keeps clear of the bars and the system insets.
- `Modifier.safeDrawingPadding()`, `systemBarsPadding()`, `statusBarsPadding()`, `navigationBarsPadding()` and `imePadding()` pad by one kind of inset.
- `windowInsetsPadding(WindowInsets.systemBars)` takes any `WindowInsets`.

Insets paddings don't add up: an inner one pads only by what outer ones didn't already take. In a `Scaffold` with a text field, pad the content as Compose does:

```kotlin body
var text by remember { mutableStateOf("") }
Scaffold { padding ->
    Column(modifier = Modifier.padding(padding).consumeWindowInsets(padding).imePadding()) {
        OutlinedTextField(value = text, onValueChange = { text = it })
    }
}
```

## Right to left

When the system language is written right to left (Arabic, Hebrew, Persian…), the layout direction is right to left: `Row`s start at the right, `start` and `end` padding swap, and horizontal lists scroll from the right. `CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Rtl) { … }` changes it for part of the tree, and `KOMPOZ_LAYOUT_DIRECTION=rtl` for the whole app. Drawing, `absoluteOffset` and `absolutePadding` don't mirror.

## See also

- [Modifiers](modifiers.md): size, padding and the scope modifiers.
- [Custom layouts](custom-layouts.md): `Layout`, intrinsics and `SubcomposeLayout`.
- [Lists and grids](lists.md): lazy layouts for long content.
