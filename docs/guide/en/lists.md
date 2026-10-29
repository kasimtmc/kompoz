# Lists and grids

Lazy lists and grids compose only the items in view, so they scroll through thousands of items as smoothly as through ten. This page covers them, along with pagers, carousels, scroll modifiers, nested scrolling and pull to refresh.

## Scrolling a column

For a handful of items, a `Column` with `verticalScroll` is enough:

```cpp body
Column(Modifier().fillMaxSize().verticalScroll(rememberScrollState()).padding(16), [&] {
    for (int i = 0; i < 20; ++i) Text("Paragraph " + std::to_string(i));
});
```

It composes and lays out every child, whether it is on the screen or not. For long lists, and lists whose length you don't know, use a lazy list.

## Lazy lists

`LazyColumn` and `LazyRow` compose items as they come into view, plus a small margin, and drop the ones that leave it. While they scroll, the next item past those is composed ahead in the pauses between frames, as Compose prefetches: when what an item of its content type took before fits in the pause. Instead of content, they take a lambda that *describes* the items through a `LazyListScope`:

```cpp
struct Message {
    int id;
    std::string author, body;
};

void Conversation(const std::vector<Message>& messages) {
    LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& list) {
        list.item([] { Text("Today", {.font = typography().labelLarge}); });
        list.items(messages, [](const Message& m) { return m.id; }, [](const Message& m) {
            ListItem({.headline = [&] { Text(m.author); }, .supporting = [&] { Text(m.body); }});
        });
        list.item([] { Text("No more messages"); });
    });
}
```

| Call | Adds |
|---|---|
| `item(content)`, `item(key, content)` | One item. |
| `items(count, content)` | `count` items; `content` gets the index. |
| `items(list, content)` | One item per element; `content` gets the element. |
| `items(list, key, content)` | The same, with a key per element. |
| `items(list, key, contentType, content)` | The same, with a content type (reported in `layoutInfo()`; items of a type are timed together for composing ahead). |
| `itemsIndexed(list, content)` | One item per element; `content` gets the index and the element. |
| `stickyHeader(content)` | A header that stays at the top while the items after it scroll under it. |

Item content runs later, when the item comes into view, not while the scope lambda runs. Capture what the item needs by value, or use the arguments it is given: the `list` you pass is read by the items, so it must outlive the composition that uses it, as a state's value or a member of a `ViewModel` does.

### Keys

Without keys, an item's state is tied to its position. Insert an item at the top, and every remembered value shifts down by one. Give items a stable, unique key, and their state follows them:

```cpp body
#! struct Message { int id; std::string author, body; };
#! std::vector<Message> messages;
LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& list) {
    list.items(messages, [](const Message& m) { return m.id; }, [](const Message& m) {
        auto expanded = remember([] { return mutableStateOf(false); });   // stays with the message
        Text(m.body, Modifier().clickable([expanded] { expanded.set(!expanded.get()); }),
             {.maxLines = expanded.get() ? 0 : 1});
    });
});
```

A key can be anything hashable: an id, a string. Keys must be unique within the list.

### Options

The options struct sets padding, spacing and direction:

```cpp body
LazyColumn(Modifier().fillMaxSize(),
           {.contentPadding = PaddingValues(16), .spacing = 8},
           [&](LazyListScope& list) { list.items(50, [](int i) { Text("Item " + std::to_string(i)); }); });
```

| Option | What it does |
|---|---|
| `state` | A `LazyListState` to read or control the scroll position. |
| `contentPadding` | Padding around the items, inside the scrolling area: items scroll into it. |
| `spacing` | Space between items. |
| `arrangement` | Instead of `spacing`: how items are arranged when they all fit (`Arrangement::Bottom`, `Center`…). |
| `reverseLayout` | Item 0 at the bottom (a `LazyRow`'s end), as in a chat. |
| `flingBehavior` | How a fling ends: `rememberSnapFlingBehavior(state)` snaps to an item. |
| `userScrollEnabled` | `false`: scrolled only from code. |
| `crossAxisBias` | Where items narrower than the list sit: start (-1), center (0) or end (1). |

### Animating items

`Modifier().animateItem()` on an item's root animates it when it appears (fading in), moves to a new position, or goes away (fading out). It needs keys, so that Kompoz can tell which item moved:

```cpp body
#! std::vector<std::string> names;
LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& list) {
    list.items(names, [](const std::string& n) { return n; }, [](const std::string& n) {
        Text(n, Modifier().animateItem().padding(16));
    });
});
```

## Controlling the scroll position

`rememberLazyListState()` gives a `LazyListState` to pass in the options. Read where the list is with `firstVisibleItemIndex()` (observable) and `layoutInfo()`; move it with `scrollToItem` or, in a coroutine, `animateScrollToItem`:

```cpp
void Messages(const std::vector<std::string>& messages) {
    auto list = rememberLazyListState();
    auto scope = rememberCoroutineScope();
    auto showTop = remember([list] { return derivedStateOf([list] { return list.firstVisibleItemIndex() > 3; }); });

    Box(Modifier().fillMaxSize(), [&] {
        LazyColumn(Modifier().fillMaxSize(), {.state = list}, [&](LazyListScope& l) {
            l.items(messages, [](const std::string& m) { Text(m, Modifier().padding(16)); });
        });
        if (showTop.get()) {
            SmallFloatingActionButton([scope, list] { scope.launch([list]() -> Task { co_await list.animateScrollToItem(0); }); },
                                      [] { Icon(Icons::Filled::KeyboardArrowUp, "Back to top"); },
                                      Modifier().align(Alignment::BottomEnd).padding(16));
        }
    });
}
```

The list's state is saveable: kept with `rememberSaveable`, or inside a navigation destination, the position comes back.

To load more as the user nears the end, watch the last visible item:

```cpp body
#! int total = 100;
#! std::function<void()> loadMore;
auto list = rememberLazyListState();
auto nearEnd = remember([list, total] { return derivedStateOf([list, total] {
    auto visible = list.layoutInfo().visibleItemsInfo;
    return !visible.empty() && visible.back().index >= total - 5;
}); });
LaunchedEffect(nearEnd.get(), [nearEnd, loadMore]() -> Task {
    if (nearEnd.get()) loadMore();
    co_return;
});
```

## Lazy grids

`LazyVerticalGrid` and `LazyHorizontalGrid` lay items out in a grid, composing rows (or columns) as they come into view:

```cpp body
#! struct Photo { int id; std::string url; };
#! std::vector<Photo> photos;
LazyVerticalGrid(GridCells::Adaptive(120), Modifier().fillMaxSize(),
                 {.contentPadding = PaddingValues(8), .verticalSpacing = 8, .horizontalSpacing = 8},
                 [&](LazyGridScope& grid) {
                     grid.item([] { Text("Photos", {.font = typography().titleLarge}); }, LazyGridScope::FullLine);
                     grid.items(photos, [](const Photo& p) { return p.id; }, [](const Photo& p) {
                         AsyncImage(p.url, "", Modifier().aspectRatio(1), {.contentScale = ContentScale::Crop});
                     });
                 });
```

- `GridCells::Fixed(3)` gives three columns; `GridCells::Adaptive(120)` as many columns of at least 120 dp as fit.
- An item can span several cells: `item(content, span)`, with `LazyGridScope::FullLine` for a header across the grid.
- `rememberLazyGridState()` works like the list's state.

`LazyVerticalStaggeredGrid` and `LazyHorizontalStaggeredGrid` place items of different sizes in columns (or rows) of their own, like a masonry layout: each item goes in the shortest column.

```cpp body
#! std::vector<std::string> notes;
LazyVerticalStaggeredGrid(StaggeredGridCells::Fixed(2), Modifier().fillMaxSize(), {.verticalItemSpacing = 8},
                          [&](LazyGridScope& grid) {
                              grid.items(notes, [](const std::string& note) {
                                  OutlinedCard(Modifier().padding(4), [&] { Text(note, Modifier().padding(12)); });
                              });
                          });
```

## Pagers

`HorizontalPager` and `VerticalPager` show one page at a time and snap to pages as the user swipes, like Compose's pagers:

```cpp
void Onboarding() {
    auto pager = rememberPagerState(0, [] { return 3; });
    auto scope = rememberCoroutineScope();
    Column(Modifier().fillMaxSize(), [&] {
        HorizontalPager(pager, Modifier().weight(1), [](int page) {
            Box(Modifier().fillMaxSize(), Alignment::Center, [&] { Text("Page " + std::to_string(page + 1)); });
        });
        Button([scope, pager] { scope.launch([pager]() -> Task { co_await pager.animateScrollToPage(pager.currentPage() + 1); }); },
               [] { Text("Next"); });
    });
}
```

`currentPage()`, `settledPage()` and `targetPage()` are observable. `getOffsetDistanceInPages(page)`, read while drawing, gives how far a page is from the center, for pages that shrink or fade as they move.

## Carousels

Material 3's carousels show a row of items of changing sizes: `HorizontalMultiBrowseCarousel` (large items with smaller ones beside them), `HorizontalUncontainedCarousel` (items of one size) and `HorizontalCenteredHeroCarousel`:

```cpp body
#! std::vector<std::string> images;
HorizontalMultiBrowseCarousel(rememberCarouselState(0, [&] { return int(images.size()); }), 186, Modifier().height(221),
                              {.itemSpacing = 8}, [&](int i) {
                                  AsyncImage(images[size_t(i)], "", Modifier().fillMaxSize(), {.contentScale = ContentScale::Crop});
                              });
```

## Scrollbars

On the desktop, show a scrollbar next to a scrolling container with `VerticalScrollbar` or `HorizontalScrollbar` and an adapter for its state:

```cpp body
auto scroll = rememberScrollState();
Box(Modifier().fillMaxSize(), [&] {
    Column(Modifier().fillMaxSize().verticalScroll(scroll), [&] {
        for (int i = 0; i < 100; ++i) Text("Line " + std::to_string(i));
    });
    VerticalScrollbar(rememberScrollbarAdapter(scroll), Modifier().align(Alignment::CenterEnd).fillMaxHeight());
});
```

`rememberScrollbarAdapter` takes a `ScrollState` or a `LazyListState`. The thumb can be dragged, and clicking the track pages there.

## Nested scrolling

A scrollable inside another, such as a list under a collapsing app bar, shares scroll deltas with its parents through *nested scrolling*. Most of the time it just works: a `LargeTopAppBar` with a scroll behavior collapses as the list under it scrolls:

```cpp body
auto behavior = TopAppBarDefaults::exitUntilCollapsedScrollBehavior();
Scaffold(Modifier().nestedScroll(behavior.nestedScrollConnection),
         {.topBar = [&] { LargeTopAppBar({.title = [] { Text("Inbox"); }, .scrollBehavior = behavior}); }},
         [&](PaddingValues padding) {
             LazyColumn(Modifier().fillMaxSize(), {.contentPadding = padding}, [](LazyListScope& l) {
                 l.items(50, [](int i) { Text("Message " + std::to_string(i), Modifier().padding(16)); });
             });
         });
```

To take part yourself, give `Modifier().nestedScroll` a connection. Its `onPreScroll` sees each delta before the child scrolls and returns what it consumed; `onPostScroll` sees what the child left:

```cpp
void CollapsingHeader() {
    auto height = remember([] { return mutableStateOf(160.f); });
    auto connection = rememberNestedScrollConnection({.onPreScroll = [height](Offset available, NestedScrollSource) {
        float next = std::clamp(height.get() + available.y, 56.f, 160.f);
        float used = next - height.get();
        height.set(next);
        return Offset{0, used};
    }});
    Column(Modifier().fillMaxSize().nestedScroll(connection), [&] {
        Box(Modifier().fillMaxWidth().height(height.get()).background(colorScheme().primaryContainer));
        LazyColumn(Modifier().fillMaxSize(), [](LazyListScope& l) {
            l.items(50, [](int i) { Text("Row " + std::to_string(i), Modifier().padding(16)); });
        });
    });
}
```

> **Note:** unlike Compose's, `onPreFling` and `onPostFling` don't suspend: a connection that animates a fling launches its own coroutine.

## Pull to refresh

`PullToRefreshBox` adds Material's pull-to-refresh indicator to a scrolling container:

```cpp
#! TaskOf<std::vector<std::string>> fetchNews() { co_return std::vector<std::string>{}; }
void News() {
    auto items = remember([] { return mutableStateOf(std::vector<std::string>{"First"}); });
    auto refreshing = remember([] { return mutableStateOf(false); });
    auto scope = rememberCoroutineScope();
    PullToRefreshBox(refreshing.get(), [=] {
        refreshing.set(true);
        scope.launch([items, refreshing]() -> Task {
            items.set(co_await fetchNews());
            refreshing.set(false);
        });
    }, Modifier().fillMaxSize(), [&] {
        LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& l) {
            l.items(items.get(), [](const std::string& s) { Text(s, Modifier().padding(16)); });
        });
    });
}
```

## See also

- [Layouts](layouts.md): `Column`, `Row` and the other non-lazy layouts.
- [State](state.md): `derivedStateOf` for values computed from the scroll position.
- [Gestures](gestures.md): `scrollable`, dragging and flinging of your own.
