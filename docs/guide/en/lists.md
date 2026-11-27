# Lists and grids

Lazy lists and grids compose only the items in view, so they scroll through thousands of items as smoothly as through ten. This page covers them, along with pagers, carousels, scroll modifiers, nested scrolling and pull to refresh.

## Scrolling a column

For a handful of items, a `Column` with `verticalScroll` is enough:

```kotlin body
Column(modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)) {
    for (i in 0 until 20) Text("Paragraph $i")
}
```

It composes and lays out every child, whether it is on the screen or not. For long lists, and lists whose length you don't know, use a lazy list.

## Lazy lists

`LazyColumn` and `LazyRow` compose items as they come into view, plus a small margin, and drop the ones that leave it. While they scroll, the next item past those is composed ahead in the pauses between frames, as Compose prefetches: when what an item of its content type took before fits in the pause. Instead of content, they take a block that *describes* the items, as Compose's `LazyListScope` does:

```kotlin
data class Message(val id: Int, val author: String, val body: String)

@Kompozable
fun Conversation(messages: List<Message>) {
    LazyColumn(modifier = Modifier.fillMaxSize()) {
        item { Text("Today", style = MaterialTheme.typography.labelLarge) }
        items(messages, key = { it.id }) { message ->
            ListItem(headlineContent = { Text(message.author) }, supportingContent = { Text(message.body) })
        }
        item { Text("No more messages") }
    }
}
```

| Call | Adds |
|---|---|
| `item { … }`, `item(key = …) { … }` | One item. |
| `items(count) { index -> … }` | `count` items. |
| `items(list) { item -> … }` | One item per element. |
| `items(list, key = { it.id }) { … }` | The same, with a key per element. A `mutableStateListOf` goes here as it is. |
| `items(list, key = …, contentType = …) { … }` | The same, with a content type (reported in `layoutInfo`; items of a type are timed together for composing ahead). |
| `itemsIndexed(list) { index, item -> … }` | One item per element, with its index. |
| `stickyHeader { … }` | A header that stays at the top while the items after it scroll under it. |

Item content runs later, when the item comes into view, not while the block runs; it captures what it uses by value, as every lambda here does. The items read the list you give them then: one made for the call (`items(listOf("a", "b"))`) is kept by them.

### Keys

Without keys, an item's state is tied to its position. Insert an item at the top, and every remembered value shifts down by one. Give items a stable, unique key, and their state follows them:

```kotlin body
#! data class Message(val id: Int, val author: String, val body: String)
#! val messages = listOf(Message(1, "Ada", "Hello"))
LazyColumn(modifier = Modifier.fillMaxSize()) {
    items(messages, key = { it.id }) { message ->
        var expanded by remember { mutableStateOf(false) }   // stays with the message
        Text(message.body, modifier = Modifier.clickable { expanded = !expanded }, maxLines = if (expanded) Int.MAX_VALUE else 1)
    }
}
```

A key can be anything hashable: an id, a string. Keys must be unique within the list.

### Options

Named arguments set its padding, spacing and direction, as in Compose:

```kotlin body
LazyColumn(
    modifier = Modifier.fillMaxSize(),
    contentPadding = PaddingValues(16.dp),
    verticalArrangement = Arrangement.spacedBy(8.dp),
) {
    items(50) { i -> Text("Item $i") }
}
```

| Option | What it does |
|---|---|
| `state` | A `LazyListState` to read or control the scroll position. |
| `contentPadding` | Padding around the items, inside the scrolling area: items scroll into it. |
| `verticalArrangement` (a `LazyRow`'s `horizontalArrangement`) | Space between items, `Arrangement.spacedBy(8.dp)`, and how they are arranged when they all fit (`Arrangement.Bottom`, `Center`…). |
| `reverseLayout` | Item 0 at the bottom (a `LazyRow`'s end), as in a chat. |
| `flingBehavior` | How a fling ends: `rememberSnapFlingBehavior(state)` snaps to an item. |
| `userScrollEnabled` | `false`: scrolled only from code. |
| `horizontalAlignment` (a `LazyRow`'s `verticalAlignment`) | Where items narrower than the list sit: `Alignment.Start`, `CenterHorizontally` or `End`. |

### Animating items

`Modifier.animateItem()` on an item's root animates it when it appears (fading in), moves to a new position, or goes away (fading out). It needs keys, so that Kompoz can tell which item moved:

```kotlin body
#! val names = listOf("Ada", "Grace", "Linus")
LazyColumn(modifier = Modifier.fillMaxSize()) {
    items(names, key = { it }) { name ->
        Text(name, modifier = Modifier.animateItem().padding(16.dp))
    }
}
```

### Reordering by dragging

Compose itself has no list the user reorders by dragging; Kompoz has the API of the library Compose apps use for it ([Reorderable](https://github.com/Calvin-LL/Reorderable)). `rememberReorderableLazyListState(listState) { from, to -> … }` is told each time the dragged item passes the middle of another one: move yours there. Each item's content goes in a `ReorderableItem(state, key = …)`, with the item's key, and `Modifier.draggableHandle()` in it starts the drag:

```kotlin body
val songs = remember { mutableStateListOf("Intro", "Verse", "Chorus", "Outro") }
val listState = rememberLazyListState()
val reorderState = rememberReorderableLazyListState(listState) { from, to ->
    songs.add(to.index, songs.removeAt(from.index))
}
LazyColumn(state = listState, modifier = Modifier.fillMaxSize()) {
    items(songs, key = { it }) { song ->
        ReorderableItem(reorderState, key = song) { isDragging ->
            val elevation by animateDpAsState(if (isDragging) 8.dp else 0.dp)
            Surface(shadowElevation = elevation) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(song, modifier = Modifier.weight(1f).padding(16.dp))
                    Icon(Icons.Default.DragHandle, contentDescription = "Reorder", modifier = Modifier.draggableHandle())
                }
            }
        }
    }
}
```

The dragged item follows the finger, drawn over the others, which glide out of its way (`Modifier.animateItem()`: give `animateItemModifier = …` for other animations, or `Modifier` for none); let go, it settles into its place. Dragged near an end of the list, within `scrollThreshold` (48 dp) of it, the list scrolls; `scrollThresholdPadding = …` keeps that area off what covers the list's ends (its `contentPadding` under the system bars). `isDragging` is true while the item is dragged, and `reorderState.isAnyItemDragging` while any is.

| Handle | Starts dragging |
|---|---|
| `Modifier.draggableHandle()` | When pressed and moved: a handle icon. |
| `Modifier.longPressDraggableHandle()` | After a long press, so that a finger still scrolls the list: the whole item, as a launcher's icons. |

Both take `enabled`, `interactionSource` (it hears the drag), `onDragStarted = { startedPosition -> … }` and `onDragStopped = { … }`, a place for haptic feedback. `onMove` may be a `suspend` lambda (one that saves the new order): the next move waits for it. `ReorderableItem(…, enabled = false)` is never moved past.

Grids reorder the same way: `rememberReorderableLazyGridState(gridState) { from, to -> … }` for `LazyVerticalGrid` and `LazyHorizontalGrid`, `rememberReorderableLazyStaggeredGridState(staggeredGridState) { from, to -> … }` for the staggered ones (`from` and `to` are their item infos). In a grid the dragged item moves freely and goes to the place of the item under its middle:

```kotlin body
val photos = remember { listOf("a.jpg", "b.jpg", "c.jpg", "d.jpg").toMutableStateList() }
val gridState = rememberLazyGridState()
val reorderState = rememberReorderableLazyGridState(gridState) { from, to ->
    photos.add(to.index, photos.removeAt(from.index))
}
LazyVerticalGrid(columns = GridCells.Adaptive(96.dp), state = gridState) {
    items(photos, key = { it }) { photo ->
        ReorderableItem(reorderState, key = photo) { isDragging ->
            AsyncImage(model = photo, contentDescription = null, contentScale = ContentScale.Crop,
                       modifier = Modifier.aspectRatio(1f).longPressDraggableHandle())
        }
    }
}
```

A short list that isn't lazy goes in a `ReorderableColumn` (or `ReorderableRow`): the order changes on the screen while dragging, `onMove` is called at each step, and `onSettle(fromIndex, toIndex)` gets the move once the item is let go. Each element's `ReorderableItem` goes in a `key` of its own:

```kotlin body
var cities by remember { mutableStateOf(listOf("Ankara", "Bursa", "Çorum")) }
ReorderableColumn(list = cities, onSettle = { from, to -> val moved = cities.toMutableList(); moved.add(to, moved.removeAt(from)); cities = moved },
                  verticalArrangement = Arrangement.spacedBy(8.dp)) { _, city, isDragging ->
    key(city) {
        ReorderableItem {
            Card(modifier = Modifier.fillMaxWidth().draggableHandle()) { Text(city, modifier = Modifier.padding(16.dp)) }
        }
    }
}
```

For accessibility services and the keyboard, give each item actions that move it (see [Accessibility](accessibility.md)):

```kotlin body
#! val songs = remember { mutableStateListOf("Intro", "Verse") }
#! val index = 1
Text(songs[index], modifier = Modifier.semantics {
    customActions = listOf(CustomAccessibilityAction("Move up") { songs.add(index - 1, songs.removeAt(index)); true })
})
```

## Controlling the scroll position

`rememberLazyListState()` gives a `LazyListState` to pass as `state`. Read where the list is with `firstVisibleItemIndex` (observable) and `layoutInfo`; move it with `scrollToItem` or, in a coroutine, `animateScrollToItem`:

```kotlin
@Kompozable
fun Messages(messages: List<String>) {
    val list = rememberLazyListState()
    val scope = rememberCoroutineScope()
    val showTop by remember { derivedStateOf { list.firstVisibleItemIndex > 3 } }

    Box(modifier = Modifier.fillMaxSize()) {
        LazyColumn(modifier = Modifier.fillMaxSize(), state = list) {
            items(messages) { Text(it, modifier = Modifier.padding(16.dp)) }
        }
        if (showTop) {
            SmallFloatingActionButton(
                onClick = { scope.launch { list.animateScrollToItem(0) } },
                modifier = Modifier.align(Alignment.BottomEnd).padding(16.dp),
            ) {
                Icon(Icons.Filled.KeyboardArrowUp, contentDescription = "Back to top")
            }
        }
    }
}
```

The list's state is saveable: kept with `rememberSaveable`, or inside a navigation destination, the position comes back.

To load more as the user nears the end, watch the last visible item:

```kotlin body
#! val total = 100
#! fun loadMore() {}
val list = rememberLazyListState()
val nearEnd by remember {
    derivedStateOf { (list.layoutInfo.visibleItemsInfo.lastOrNull()?.index ?: 0) >= total - 5 }
}
LaunchedEffect(nearEnd) {
    if (nearEnd) loadMore()
}
```

## Lazy grids

`LazyVerticalGrid` and `LazyHorizontalGrid` lay items out in a grid, composing rows (or columns) as they come into view:

```kotlin body
#! data class Photo(val id: Int, val url: String)
#! val photos = listOf(Photo(1, "photo.jpg"))
LazyVerticalGrid(
    columns = GridCells.Adaptive(120.dp),
    modifier = Modifier.fillMaxSize(),
    contentPadding = PaddingValues(8.dp),
    verticalArrangement = Arrangement.spacedBy(8.dp),
    horizontalArrangement = Arrangement.spacedBy(8.dp),
) {
    item(span = { GridItemSpan(maxLineSpan) }) { Text("Photos", style = MaterialTheme.typography.titleLarge) }
    items(photos, key = { it.id }) { photo ->
        AsyncImage(model = photo.url, contentDescription = null, modifier = Modifier.aspectRatio(1f), contentScale = ContentScale.Crop)
    }
}
```

- `GridCells.Fixed(3)` gives three columns; `GridCells.Adaptive(120.dp)` as many columns of at least 120 dp as fit.
- An item can span several cells: `item(span = { GridItemSpan(maxLineSpan) })` for a header across the grid, `items(list, span = { item -> GridItemSpan(2) })` for each of a list.
- `rememberLazyGridState()` works like the list's state; `reverseLayout = true` puts the first line at the bottom.
- `Modifier.animateItem()` on an item's root animates it in a grid as in a list: an item moved to another line or column glides there, a new one fades in, a removed one fades out.

`LazyVerticalStaggeredGrid` and `LazyHorizontalStaggeredGrid` place items of different sizes in columns (or rows) of their own, like a masonry layout: each item goes in the shortest column.

```kotlin body
#! val notes = listOf("A short note", "A longer note that takes more room")
LazyVerticalStaggeredGrid(columns = StaggeredGridCells.Fixed(2), modifier = Modifier.fillMaxSize(), verticalItemSpacing = 8.dp) {
    item(span = StaggeredGridItemSpan.FullLine) { Text("Notes", style = MaterialTheme.typography.titleLarge) }
    items(notes) { note ->
        OutlinedCard(modifier = Modifier.padding(4.dp)) { Text(note, modifier = Modifier.padding(12.dp)) }
    }
}
```

An item with `span = StaggeredGridItemSpan.FullLine` crosses all the lanes, below the longest one, like a header. `Modifier.animateItem()` works here too.

## Trees

A tree the user opens and closes (files and folders, an outline) is a `LazyTree`: Compose has none; Kompoz has the API of JetBrains' Jewel (IntelliJ's Compose UI). `buildTree { }` makes it with `addNode(data) { children }` and `addLeaf(data)`; a node's block runs when it is first opened, so a big tree (a disk) is built as it is looked at:

```kotlin body
val tree = remember {
    buildTree {
        addNode("src") {
            addLeaf("main.cpp")
            addNode("ui") { addLeaf("list.cpp") }
        }
        addLeaf("README.md")
    }
}
val treeState = rememberTreeState()
LazyTree(tree = tree, treeState = treeState, modifier = Modifier.fillMaxSize(),
         onElementDoubleClick = { println("open " + it.data) }) { element ->
    Text(element.data)
}
```

A row is its element's content after an arrow that opens and closes it, indented by its depth (`indentSize`, 20 dp); only the rows in view are composed, and opening a node fades its rows in. A click selects a row; with `selectionMode = SelectionMode.Multiple` (the default) Ctrl adds or takes one away and Shift selects a range; a double click opens or closes a node and calls `onElementDoubleClick`. `onElementClick` and `onSelectionChange` are told too.

The tree takes the keyboard focus as one: Up and Down move the selection (Shift extends it), Right opens a node or goes to its first child, Left closes it or goes to its parent, Home and End go to the ends, Enter is a double click, Ctrl+A selects all. To accessibility services each row says whether it is selected and open, with actions that open and close it.

An element's id is its data unless given (`addNode(data, id = …)`); `treeState.toggleNode(id)`, `openNodes(listOf(…))`, `closeNode(id)`, `selectOnly(id)` and `clearSelection()` change the tree from code, `isNodeOpen(id)` and `isSelected(id)` read it. `rememberTreeState()` is saved with the app's state. An element has `data`, `depth`, `parent`, `isNode()`, `isLeaf()` and `children()`; `buildTree<Item> { }` for data other than text.

## Data tables

A table of rows under a header of columns is a `DataTable`: Compose has none; Kompoz has the API of compose-data-table (Compose Multiplatform's, after Flutter's DataTable), with what big tables need on top. Its block adds rows as a `LazyColumn`'s adds items: `row { }` one, `items(list, key = …) { row { … } }` one for each element, only those in view composed; in a row, `cell { }` a cell for each column, in order:

```kotlin body
#! data class Order(val id: Int, val customer: String, val total: Int)
#! val orders = listOf(Order(1, "Ada", 120))
var sortColumn by remember { mutableStateOf(-1) }
var ascending by remember { mutableStateOf(true) }
val shown = if (sortColumn == 2) (if (ascending) orders.sortedBy { it.total } else orders.sortedByDescending { it.total }) else orders
DataTable(
    columns = listOf(
        DataColumn(width = TableColumnWidth.Fixed(64.dp)) { Text("No") },
        DataColumn { Text("Customer") },
        DataColumn(alignment = Alignment.End, width = TableColumnWidth.Wrap,
                   onSort = { index, up -> sortColumn = index; ascending = up }) { Text("Total") },
    ),
    sortColumnIndex = sortColumn, sortAscending = ascending, frozenColumns = 1,
    modifier = Modifier.fillMaxSize(),
) {
    items(shown, key = { it.id }) { order ->
        row(onClick = { }) {
            cell { Text("${order.id}") }
            cell { Text(order.customer) }
            cell { Text("${order.total}") }
        }
    }
}
```

A `DataColumn` has its header (its block), a `width` (`TableColumnWidth.Fixed(dp)`, `Fraction(f)` of the table, `Wrap`: its widest cell yet, or `Flex(weight)`: a share of what the others leave, no narrower than its widest cell yet, the default), an `alignment` for its cells' content, and `onSort = { index, ascending -> … }`: a click on its header calls it (ascending first, then the other way), and `sortColumnIndex` and `sortAscending` show the arrow. A column's header end is dragged to widen it (`resizable = false` keeps it as it is); the widths given stay in the `DataTableState` (`rememberDataTableState()`, saved, with the rows' and the sideways scroll positions; `resetColumnWidths()`).

The header stays as the rows scroll; columns wider than the table scroll sideways together, `frozenColumns` of them at the start staying in view. `row(selected = …, onClick = …)` shows a selected row and is clicked; `headerHeight`, `rowHeight` (the least: a taller cell makes its row taller), `horizontalPadding`, `backgroundColor` (what frozen columns cover the scrolled ones with; the scheme's surface) and `headerBackgroundColor` change its look. Rows say whether they are selected, sorted headers which way, to accessibility services.

## Pagers

`HorizontalPager` and `VerticalPager` show one page at a time and snap to pages as the user swipes, like Compose's pagers:

```kotlin
@Kompozable
fun Onboarding() {
    val pager = rememberPagerState(pageCount = { 3 })
    val scope = rememberCoroutineScope()
    Column(modifier = Modifier.fillMaxSize()) {
        HorizontalPager(state = pager, modifier = Modifier.weight(1f)) { page ->
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { Text("Page ${page + 1}") }
        }
        Button(onClick = { scope.launch { pager.animateScrollToPage(pager.currentPage + 1) } }) { Text("Next") }
    }
}
```

`currentPage`, `settledPage` and `targetPage` are observable. `getOffsetDistanceInPages(page)`, read while drawing, gives how far a page is from the center, for pages that shrink or fade as they move.

## Carousels

Material 3's carousels show a row of items of changing sizes: `HorizontalMultiBrowseCarousel` (large items with smaller ones beside them), `HorizontalUncontainedCarousel` (items of one size) and `HorizontalCenteredHeroCarousel`:

```kotlin body
#! val images = listOf("one.jpg", "two.jpg", "three.jpg")
HorizontalMultiBrowseCarousel(
    state = rememberCarouselState { images.size },
    preferredItemWidth = 186.dp,
    modifier = Modifier.height(221.dp),
    itemSpacing = 8.dp,
) { i ->
    AsyncImage(model = images[i], contentDescription = null, modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
}
```

## Scrollbars

On the desktop, show a scrollbar next to a scrolling container with `VerticalScrollbar` or `HorizontalScrollbar` and an adapter for its state:

```kotlin body
val scroll = rememberScrollState()
Box(modifier = Modifier.fillMaxSize()) {
    Column(modifier = Modifier.fillMaxSize().verticalScroll(scroll)) {
        for (i in 0 until 100) Text("Line $i")
    }
    VerticalScrollbar(adapter = rememberScrollbarAdapter(scroll), modifier = Modifier.align(Alignment.CenterEnd).fillMaxHeight())
}
```

`rememberScrollbarAdapter` takes a `ScrollState` or a `LazyListState`. The thumb can be dragged, and clicking the track pages there.

## Nested scrolling

A scrollable inside another, such as a list under a collapsing app bar, shares scroll deltas with its parents through *nested scrolling*. Most of the time it just works: a `LargeTopAppBar` with a scroll behavior collapses as the list under it scrolls:

```kotlin body
val behavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
Scaffold(
    modifier = Modifier.nestedScroll(behavior.nestedScrollConnection),
    topBar = { LargeTopAppBar(title = { Text("Inbox") }, scrollBehavior = behavior) },
) { padding ->
    LazyColumn(modifier = Modifier.fillMaxSize(), contentPadding = padding) {
        items(50) { Text("Message $it", modifier = Modifier.padding(16.dp)) }
    }
}
```

To take part yourself, give `Modifier.nestedScroll` a connection, an object of your own as in Compose (kompozc makes `object : NestedScrollConnection { … }` a class of its own, holding the locals its functions use). Its `onPreScroll` sees each delta before the child scrolls and returns what it consumed; `onPostScroll` sees what the child left:

```kotlin
@Kompozable
fun CollapsingHeader() {
    var height by remember { mutableStateOf(160f) }
    val connection = remember {
        object : NestedScrollConnection {
            override fun onPreScroll(available: Offset, source: NestedScrollSource): Offset {
                val next = (height + available.y).coerceIn(56f, 160f)
                val used = next - height
                height = next
                return Offset(0f, used)
            }
        }
    }
    Column(modifier = Modifier.fillMaxSize().nestedScroll(connection)) {
        Box(modifier = Modifier.fillMaxWidth().height(height.dp).background(MaterialTheme.colorScheme.primaryContainer))
        LazyColumn(modifier = Modifier.fillMaxSize()) {
            items(50) { Text("Row $it", modifier = Modifier.padding(16.dp)) }
        }
    }
}
```

> **Note:** unlike Compose's, `onPreFling` and `onPostFling` don't suspend: a connection that animates a fling launches its own coroutine.

## Pull to refresh

`PullToRefreshBox` adds Material's pull-to-refresh indicator to a scrolling container (`fetchNews` here is a `suspend fun` of yours: see [Side effects](side-effects.md)):

```kotlin
#! suspend fun fetchNews(): List<String> = emptyList()
@Kompozable
fun News() {
    var news by remember { mutableStateOf(listOf("First")) }
    var refreshing by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    PullToRefreshBox(
        isRefreshing = refreshing,
        onRefresh = {
            refreshing = true
            scope.launch {
                news = fetchNews()
                refreshing = false
            }
        },
        modifier = Modifier.fillMaxSize(),
    ) {
        LazyColumn(modifier = Modifier.fillMaxSize()) {
            items(news) { Text(it, modifier = Modifier.padding(16.dp)) }
        }
    }
}
```

On a plain container, `Modifier.pullToRefresh(isRefreshing = …, state = rememberPullToRefreshState(), onRefresh = …)` does the same, with `PullToRefreshDefaults.Indicator(state = …, isRefreshing = …)` or an `IndicatorBox(state = …, isRefreshing = …) { … }` of your own over it. A list's or pager's `overscrollEffect` is the stretch by default (`rememberOverscrollEffect()`), `null` for none, or an `OverscrollEffect` of your own.

## See also

- [Layouts](layouts.md): `Column`, `Row` and the other non-lazy layouts.
- [State](state.md): `derivedStateOf` for values computed from the scroll position.
- [Gestures](gestures.md): `scrollable`, dragging and flinging of your own.
