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

@Composable
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
| `items(list, key = { it.id }) { … }` | The same, with a key per element. |
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

## Controlling the scroll position

`rememberLazyListState()` gives a `LazyListState` to pass as `state`. Read where the list is with `firstVisibleItemIndex` (observable) and `layoutInfo`; move it with `scrollToItem` or, in a coroutine, `animateScrollToItem`:

```kotlin
@Composable
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
- `rememberLazyGridState()` works like the list's state.

`LazyVerticalStaggeredGrid` and `LazyHorizontalStaggeredGrid` place items of different sizes in columns (or rows) of their own, like a masonry layout: each item goes in the shortest column.

```kotlin body
#! val notes = listOf("A short note", "A longer note that takes more room")
LazyVerticalStaggeredGrid(columns = StaggeredGridCells.Fixed(2), modifier = Modifier.fillMaxSize(), verticalItemSpacing = 8.dp) {
    items(notes) { note ->
        OutlinedCard(modifier = Modifier.padding(4.dp)) { Text(note, modifier = Modifier.padding(12.dp)) }
    }
}
```

## Pagers

`HorizontalPager` and `VerticalPager` show one page at a time and snap to pages as the user swipes, like Compose's pagers:

```kotlin
@Composable
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
@Composable
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

`PullToRefreshBox` adds Material's pull-to-refresh indicator to a scrolling container (`fetchNews` here is a suspending function of yours, written in C++: see [Side effects](side-effects.md)):

```kotlin
#! TaskOf<std::vector<std::string>> fetchNews() { co_return std::vector<std::string>{}; }
@Composable
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

## See also

- [Layouts](layouts.md): `Column`, `Row` and the other non-lazy layouts.
- [State](state.md): `derivedStateOf` for values computed from the scroll position.
- [Gestures](gestures.md): `scrollable`, dragging and flinging of your own.
