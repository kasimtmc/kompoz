# Listeler ve ızgaralar

Tembel listeler ve ızgaralar yalnızca görünen öğeleri kompoze eder, bu yüzden binlerce öğe arasında da on öğe arasındaki kadar akıcı kayarlar. Bu sayfa onları; pager'ları, carousel'leri, kaydırma modifier'larını, iç içe kaydırmayı ve çekip yenilemeyi anlatıyor.

## Bir sütunu kaydırmak

Bir avuç öğe için `verticalScroll`'lu bir `Column` yeter:

```kotlin body
Column(modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)) {
    for (i in 0 until 20) Text("Paragraph $i")
}
```

Bu, ekranda olsun olmasın her çocuğu kompoze eder ve yerleştirir. Uzun listeler ve uzunluğunu bilmediğiniz listeler için tembel bir liste kullanın.

## Tembel listeler

`LazyColumn` ve `LazyRow` öğeleri görünür alana girdikçe (artı küçük bir pay) kompoze eder, çıkanları da bırakır. Kaydırılırken bunların ötesindeki sıradaki öğe, Compose'un önceden getirmesi (prefetch) gibi, kareler arasındaki duraklamalarda önceden kompoze edilir: aynı içerik türünden bir öğenin daha önce aldığı süre duraklamaya sığdığında. İçerik yerine, Compose'un `LazyListScope`'u gibi öğeleri *tarif eden* bir blok alırlar:

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

| Çağrı | Ekler |
|---|---|
| `item { … }`, `item(key = …) { … }` | Bir öğe. |
| `items(count) { index -> … }` | `count` öğe. |
| `items(list) { item -> … }` | Her eleman için bir öğe. |
| `items(list, key = { it.id }) { … }` | Aynısı, her eleman için bir anahtarla. Bir `mutableStateListOf` buraya olduğu gibi girer. |
| `items(list, key = …, contentType = …) { … }` | Aynısı, bir içerik türüyle (`layoutInfo`'da bildirilir; bir türün öğeleri önceden kompoze etmek için birlikte zamanlanır). |
| `itemsIndexed(list) { index, item -> … }` | Her eleman için bir öğe, sırasıyla. |
| `stickyHeader { … }` | Arkasından gelen öğeler altından kayarken üstte kalan bir başlık. |

Öğe içeriği blok çalışırken değil, daha sonra, öğe görünür alana girince çalışır; buradaki her lambda gibi kullandığını değerle yakalar. Öğeler verdiğiniz listeyi o zaman okur: çağrı için yapılan bir listeyi (`items(listOf("a", "b"))`) kendileri tutar.

### Anahtarlar

Anahtar yoksa bir öğenin durumu konumuna bağlıdır. En üste bir öğe ekleyin, hatırlanan her değer bir aşağı kayar. Öğelere kararlı, benzersiz bir anahtar verin, durumları da onları izlesin:

```kotlin body
#! data class Message(val id: Int, val author: String, val body: String)
#! val messages = listOf(Message(1, "Ada", "Hello"))
LazyColumn(modifier = Modifier.fillMaxSize()) {
    items(messages, key = { it.id }) { message ->
        var expanded by remember { mutableStateOf(false) }   // mesajla birlikte kalır
        Text(message.body, modifier = Modifier.clickable { expanded = !expanded }, maxLines = if (expanded) Int.MAX_VALUE else 1)
    }
}
```

Anahtar, hash'lenebilen herhangi bir şey olabilir: bir id, bir string. Anahtarlar liste içinde benzersiz olmalıdır.

### Seçenekler

Adlı argümanlar, Compose'daki gibi, iç boşluğu, aralığı ve yönü ayarlar:

```kotlin body
LazyColumn(
    modifier = Modifier.fillMaxSize(),
    contentPadding = PaddingValues(16.dp),
    verticalArrangement = Arrangement.spacedBy(8.dp),
) {
    items(50) { i -> Text("Item $i") }
}
```

| Seçenek | Ne yapar |
|---|---|
| `state` | Kaydırma konumunu okumak ya da yönetmek için bir `LazyListState`. |
| `contentPadding` | Öğelerin çevresinde, kaydırma alanının içinde iç boşluk: öğeler onun içine kayar. |
| `verticalArrangement` (bir `LazyRow`'da `horizontalArrangement`) | Öğeler arasındaki boşluk, `Arrangement.spacedBy(8.dp)`, ve hepsi sığdığında nasıl dizilecekleri (`Arrangement.Bottom`, `Center`…). |
| `reverseLayout` | Bir sohbetteki gibi 0. öğe altta (bir `LazyRow`'da sonda). |
| `flingBehavior` | Bir fırlatmanın nasıl biteceği: `rememberSnapFlingBehavior(state)` bir öğeye yakalar. |
| `userScrollEnabled` | `false`: yalnızca koddan kaydırılır. |
| `horizontalAlignment` (bir `LazyRow`'da `verticalAlignment`) | Listeden dar öğelerin nerede duracağı: `Alignment.Start`, `CenterHorizontally` ya da `End`. |

### Öğeleri canlandırmak

Bir öğenin kökündeki `Modifier.animateItem()`, öğe belirdiğinde (belirerek), yeni bir konuma geçtiğinde ya da kaybolduğunda (solarak) onu canlandırır. Kompoz'un hangi öğenin yer değiştirdiğini anlayabilmesi için anahtar ister:

```kotlin body
#! val names = listOf("Ada", "Grace", "Linus")
LazyColumn(modifier = Modifier.fillMaxSize()) {
    items(names, key = { it }) { name ->
        Text(name, modifier = Modifier.animateItem().padding(16.dp))
    }
}
```

## Kaydırma konumunu yönetmek

`rememberLazyListState()`, `state` olarak vereceğiniz bir `LazyListState` verir. Listenin nerede olduğunu `firstVisibleItemIndex` (gözlemlenebilir) ve `layoutInfo` ile okuyun; onu `scrollToItem` ile ya da bir coroutine içinde `animateScrollToItem` ile hareket ettirin:

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

Listenin durumu kaydedilebilir: `rememberSaveable` ile ya da bir gezinme hedefinin içinde tutulursa konum geri gelir.

Kullanıcı sona yaklaştıkça daha fazlasını yüklemek için son görünen öğeyi izleyin:

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

## Tembel ızgaralar

`LazyVerticalGrid` ve `LazyHorizontalGrid` öğeleri bir ızgaraya dizer, satırları (ya da sütunları) görünür alana girdikçe kompoze eder:

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

- `GridCells.Fixed(3)` üç sütun verir; `GridCells.Adaptive(120.dp)` en az 120 dp'lik sütunlardan sığabildiği kadar.
- Bir öğe birkaç hücreye yayılabilir: ızgara boyunca uzanan bir başlık için `item(span = { GridItemSpan(maxLineSpan) })`, bir listenin her biri için `items(list, span = { item -> GridItemSpan(2) })`.
- `rememberLazyGridState()` listenin durumu gibi çalışır.

`LazyVerticalStaggeredGrid` ve `LazyHorizontalStaggeredGrid` farklı boyutlardaki öğeleri, bir masonry yerleşimi gibi kendi sütunlarına (ya da satırlarına) yerleştirir: her öğe en kısa sütuna gider.

```kotlin body
#! val notes = listOf("A short note", "A longer note that takes more room")
LazyVerticalStaggeredGrid(columns = StaggeredGridCells.Fixed(2), modifier = Modifier.fillMaxSize(), verticalItemSpacing = 8.dp) {
    item(span = StaggeredGridItemSpan.FullLine) { Text("Notlar", style = MaterialTheme.typography.titleLarge) }
    items(notes) { note ->
        OutlinedCard(modifier = Modifier.padding(4.dp)) { Text(note, modifier = Modifier.padding(12.dp)) }
    }
}
```

`span = StaggeredGridItemSpan.FullLine` verilen bir öğe, en uzun şeridin altında, tüm şeritleri bir başlık gibi kaplar.

## Pager'lar

`HorizontalPager` ve `VerticalPager`, Compose'un pager'ları gibi bir seferde bir sayfa gösterir ve kullanıcı kaydırdıkça sayfalara yakalanır:

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

`currentPage`, `settledPage` ve `targetPage` gözlemlenebilirdir. Çizim sırasında okunan `getOffsetDistanceInPages(page)`, hareket ettikçe küçülen ya da solan sayfalar için bir sayfanın merkezden ne kadar uzakta olduğunu verir.

## Carousel'ler

Material 3'ün carousel'leri boyutları değişen öğelerden bir sıra gösterir: `HorizontalMultiBrowseCarousel` (yanında küçükleri olan büyük öğeler), `HorizontalUncontainedCarousel` (tek boyutlu öğeler) ve `HorizontalCenteredHeroCarousel`:

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

## Kaydırma çubukları

Masaüstünde, kaydırılan bir kabın yanında `VerticalScrollbar` ya da `HorizontalScrollbar` ve onun durumu için bir adaptörle bir kaydırma çubuğu gösterin:

```kotlin body
val scroll = rememberScrollState()
Box(modifier = Modifier.fillMaxSize()) {
    Column(modifier = Modifier.fillMaxSize().verticalScroll(scroll)) {
        for (i in 0 until 100) Text("Line $i")
    }
    VerticalScrollbar(adapter = rememberScrollbarAdapter(scroll), modifier = Modifier.align(Alignment.CenterEnd).fillMaxHeight())
}
```

`rememberScrollbarAdapter` bir `ScrollState` ya da bir `LazyListState` alır. Tutamaç sürüklenebilir; iz üzerine tıklamak sayfa sayfa oraya gider.

## İç içe kaydırma

Bir başkasının içindeki kaydırılabilir bir öğe, örneğin daralan bir uygulama çubuğunun altındaki bir liste, kaydırma miktarlarını ebeveynleriyle *iç içe kaydırma* üzerinden paylaşır. Çoğu zaman kendiliğinden çalışır: kaydırma davranışı olan bir `LargeTopAppBar`, altındaki liste kaydıkça daralır:

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

Kendiniz katılmak için `Modifier.nestedScroll`'a, Compose'daki gibi kendi nesnenizi, bir bağlantı verin (kompozc `object : NestedScrollConnection { … }`'i, fonksiyonlarının kullandığı yerelleri tutan kendine ait bir sınıf yapar). Bunun `onPreScroll`'u her miktarı çocuk kaymadan önce görür ve ne kadarını tükettiğini döndürür; `onPostScroll` çocuğun bıraktığını görür:

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

> **Not:** Compose'dakinin aksine `onPreFling` ve `onPostFling` askıya alınmaz (suspend etmez): bir fırlatmayı canlandıran bağlantı kendi coroutine'ini başlatır.

## Çekip yenileme

`PullToRefreshBox`, kaydırılan bir kaba Material'ın çekip yenileme göstergesini ekler (buradaki `fetchNews` sizin bir `suspend fun`'ınızdır: bkz. [Yan etkiler](side-effects.md)):

```kotlin
#! suspend fun fetchNews(): List<String> = emptyList()
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

Düz bir kapta aynısını `Modifier.pullToRefresh(isRefreshing = …, state = rememberPullToRefreshState(), onRefresh = …)` yapar; üstüne `PullToRefreshDefaults.Indicator(state = …, isRefreshing = …)` ya da kendi `IndicatorBox(state = …, isRefreshing = …) { … }`'unuz konur. Bir listenin ya da pager'ın `overscrollEffect`'i varsayılan olarak esnemedir (`rememberOverscrollEffect()`), `null` hiçbiri, ya da kendi `OverscrollEffect`'iniz.

## Ayrıca bakın

- [Yerleşimler](layouts.md): `Column`, `Row` ve tembel olmayan diğer yerleşimler.
- [Durum](state.md): kaydırma konumundan hesaplanan değerler için `derivedStateOf`.
- [Hareketler](gestures.md): `scrollable`, kendi sürükleme ve fırlatmanız.
