# Listeler ve ızgaralar

Tembel listeler ve ızgaralar yalnızca görünen öğeleri kompoze eder, bu yüzden binlerce öğe arasında da on öğe arasındaki kadar akıcı kayarlar. Bu sayfa onları; pager'ları, carousel'leri, kaydırma modifier'larını, iç içe kaydırmayı ve çekip yenilemeyi anlatıyor.

## Bir sütunu kaydırmak

Bir avuç öğe için `verticalScroll`'lu bir `Column` yeter:

```cpp body
Column(Modifier().fillMaxSize().verticalScroll(rememberScrollState()).padding(16), [&] {
    for (int i = 0; i < 20; ++i) Text("Paragraph " + std::to_string(i));
});
```

Bu, ekranda olsun olmasın her çocuğu kompoze eder ve yerleştirir. Uzun listeler ve uzunluğunu bilmediğiniz listeler için tembel bir liste kullanın.

## Tembel listeler

`LazyColumn` ve `LazyRow` öğeleri görünür alana girdikçe (artı küçük bir pay) kompoze eder, çıkanları da bırakır. İçerik yerine, öğeleri bir `LazyListScope` üzerinden *tarif eden* bir lambda alırlar:

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

| Çağrı | Ekler |
|---|---|
| `item(content)`, `item(key, content)` | Bir öğe. |
| `items(count, content)` | `count` öğe; `content` indeksi alır. |
| `items(list, content)` | Her eleman için bir öğe; `content` elemanı alır. |
| `items(list, key, content)` | Aynısı, her eleman için bir anahtarla. |
| `items(list, key, contentType, content)` | Aynısı, bir içerik türüyle (`layoutInfo()`'da bildirilir). |
| `itemsIndexed(list, content)` | Her eleman için bir öğe; `content` indeksi ve elemanı alır. |
| `stickyHeader(content)` | Arkasından gelen öğeler altından kayarken üstte kalan bir başlık. |

Öğe içeriği scope lambda'sı çalışırken değil, daha sonra, öğe görünür alana girince çalışır. Öğenin ihtiyaç duyduğunu değerle yakalayın ya da ona verilen argümanları kullanın: verdiğiniz `list`'i öğeler okur, bu yüzden onu kullanan kompozisyondan uzun yaşamalıdır; bir durumun değeri ya da bir `ViewModel`'in üyesi gibi.

### Anahtarlar

Anahtar yoksa bir öğenin durumu konumuna bağlıdır. En üste bir öğe ekleyin, hatırlanan her değer bir aşağı kayar. Öğelere kararlı, benzersiz bir anahtar verin, durumları da onları izlesin:

```cpp body
#! struct Message { int id; std::string author, body; };
#! std::vector<Message> messages;
LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& list) {
    list.items(messages, [](const Message& m) { return m.id; }, [](const Message& m) {
        auto expanded = remember([] { return mutableStateOf(false); });   // mesajla birlikte kalır
        Text(m.body, Modifier().clickable([expanded] { expanded.set(!expanded.get()); }),
             {.maxLines = expanded.get() ? 0 : 1});
    });
});
```

Anahtar, hash'lenebilen herhangi bir şey olabilir: bir id, bir string. Anahtarlar liste içinde benzersiz olmalıdır.

### Seçenekler

Seçenekler struct'ı iç boşluğu, aralığı ve yönü ayarlar:

```cpp body
LazyColumn(Modifier().fillMaxSize(),
           {.contentPadding = PaddingValues(16), .spacing = 8},
           [&](LazyListScope& list) { list.items(50, [](int i) { Text("Item " + std::to_string(i)); }); });
```

| Seçenek | Ne yapar |
|---|---|
| `state` | Kaydırma konumunu okumak ya da yönetmek için bir `LazyListState`. |
| `contentPadding` | Öğelerin çevresinde, kaydırma alanının içinde iç boşluk: öğeler onun içine kayar. |
| `spacing` | Öğeler arasındaki boşluk. |
| `arrangement` | `spacing` yerine: hepsi sığdığında öğelerin nasıl dizileceği (`Arrangement::Bottom`, `Center`…). |
| `reverseLayout` | Bir sohbetteki gibi 0. öğe altta (bir `LazyRow`'da sonda). |
| `flingBehavior` | Bir fırlatmanın nasıl biteceği: `rememberSnapFlingBehavior(state)` bir öğeye yakalar. |
| `userScrollEnabled` | `false`: yalnızca koddan kaydırılır. |
| `crossAxisBias` | Listeden dar öğelerin nerede duracağı: başta (-1), ortada (0) ya da sonda (1). |

### Öğeleri canlandırmak

Bir öğenin kökündeki `Modifier().animateItem()`, öğe belirdiğinde (belirerek), yeni bir konuma geçtiğinde ya da kaybolduğunda (solarak) onu canlandırır. Kompoz'un hangi öğenin yer değiştirdiğini anlayabilmesi için anahtar ister:

```cpp body
#! std::vector<std::string> names;
LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& list) {
    list.items(names, [](const std::string& n) { return n; }, [](const std::string& n) {
        Text(n, Modifier().animateItem().padding(16));
    });
});
```

## Kaydırma konumunu yönetmek

`rememberLazyListState()` seçeneklerde vereceğiniz bir `LazyListState` verir. Listenin nerede olduğunu `firstVisibleItemIndex()` (gözlemlenebilir) ve `layoutInfo()` ile okuyun; onu `scrollToItem` ile ya da bir coroutine içinde `animateScrollToItem` ile hareket ettirin:

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

Listenin durumu kaydedilebilir: `rememberSaveable` ile ya da bir gezinme hedefinin içinde tutulursa konum geri gelir.

Kullanıcı sona yaklaştıkça daha fazlasını yüklemek için son görünen öğeyi izleyin:

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

## Tembel ızgaralar

`LazyVerticalGrid` ve `LazyHorizontalGrid` öğeleri bir ızgaraya dizer, satırları (ya da sütunları) görünür alana girdikçe kompoze eder:

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

- `GridCells::Fixed(3)` üç sütun verir; `GridCells::Adaptive(120)` en az 120 dp'lik sütunlardan sığabildiği kadar.
- Bir öğe birkaç hücreye yayılabilir: `item(content, span)`; ızgara boyunca uzanan bir başlık için `LazyGridScope::FullLine`.
- `rememberLazyGridState()` listenin durumu gibi çalışır.

`LazyVerticalStaggeredGrid` ve `LazyHorizontalStaggeredGrid` farklı boyutlardaki öğeleri, bir masonry yerleşimi gibi kendi sütunlarına (ya da satırlarına) yerleştirir: her öğe en kısa sütuna gider.

```cpp body
#! std::vector<std::string> notes;
LazyVerticalStaggeredGrid(StaggeredGridCells::Fixed(2), Modifier().fillMaxSize(), {.verticalItemSpacing = 8},
                          [&](LazyGridScope& grid) {
                              grid.items(notes, [](const std::string& note) {
                                  OutlinedCard(Modifier().padding(4), [&] { Text(note, Modifier().padding(12)); });
                              });
                          });
```

## Pager'lar

`HorizontalPager` ve `VerticalPager`, Compose'un pager'ları gibi bir seferde bir sayfa gösterir ve kullanıcı kaydırdıkça sayfalara yakalanır:

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

`currentPage()`, `settledPage()` ve `targetPage()` gözlemlenebilirdir. Çizim sırasında okunan `getOffsetDistanceInPages(page)`, hareket ettikçe küçülen ya da solan sayfalar için bir sayfanın merkezden ne kadar uzakta olduğunu verir.

## Carousel'ler

Material 3'ün carousel'leri boyutları değişen öğelerden bir sıra gösterir: `HorizontalMultiBrowseCarousel` (yanında küçükleri olan büyük öğeler), `HorizontalUncontainedCarousel` (tek boyutlu öğeler) ve `HorizontalCenteredHeroCarousel`:

```cpp body
#! std::vector<std::string> images;
HorizontalMultiBrowseCarousel(rememberCarouselState(0, [&] { return int(images.size()); }), 186, Modifier().height(221),
                              {.itemSpacing = 8}, [&](int i) {
                                  AsyncImage(images[size_t(i)], "", Modifier().fillMaxSize(), {.contentScale = ContentScale::Crop});
                              });
```

## Kaydırma çubukları

Masaüstünde, kaydırılan bir kabın yanında `VerticalScrollbar` ya da `HorizontalScrollbar` ve onun durumu için bir adaptörle bir kaydırma çubuğu gösterin:

```cpp body
auto scroll = rememberScrollState();
Box(Modifier().fillMaxSize(), [&] {
    Column(Modifier().fillMaxSize().verticalScroll(scroll), [&] {
        for (int i = 0; i < 100; ++i) Text("Line " + std::to_string(i));
    });
    VerticalScrollbar(rememberScrollbarAdapter(scroll), Modifier().align(Alignment::CenterEnd).fillMaxHeight());
});
```

`rememberScrollbarAdapter` bir `ScrollState` ya da bir `LazyListState` alır. Tutamaç sürüklenebilir; iz üzerine tıklamak sayfa sayfa oraya gider.

## İç içe kaydırma

Bir başkasının içindeki kaydırılabilir bir öğe, örneğin daralan bir uygulama çubuğunun altındaki bir liste, kaydırma miktarlarını ebeveynleriyle *iç içe kaydırma* üzerinden paylaşır. Çoğu zaman kendiliğinden çalışır: kaydırma davranışı olan bir `LargeTopAppBar`, altındaki liste kaydıkça daralır:

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

Kendiniz katılmak için `Modifier().nestedScroll`'a bir bağlantı verin. Bunun `onPreScroll`'u her miktarı çocuk kaymadan önce görür ve ne kadarını tükettiğini döndürür; `onPostScroll` çocuğun bıraktığını görür:

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

> **Not:** Compose'dakinin aksine `onPreFling` ve `onPostFling` askıya alınmaz (suspend etmez): bir fırlatmayı canlandıran bağlantı kendi coroutine'ini başlatır.

## Çekip yenileme

`PullToRefreshBox`, kaydırılan bir kaba Material'ın çekip yenileme göstergesini ekler:

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

## Ayrıca bakın

- [Yerleşimler](layouts.md): `Column`, `Row` ve tembel olmayan diğer yerleşimler.
- [Durum](state.md): kaydırma konumundan hesaplanan değerler için `derivedStateOf`.
- [Hareketler](gestures.md): `scrollable`, kendi sürükleme ve fırlatmanız.
