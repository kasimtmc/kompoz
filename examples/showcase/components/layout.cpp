// Components screen: panes, layout info, measuring text, positions, snapping, pagers, z-index, FlexBox, Grid, layouts, carousels.

#include "showcase.hpp"

// ListDetailPaneScaffold: cities and the one picked — one pane at a time on a phone held
// upright (Back goes back), side by side on an expanded window (a phone on its side, a desktop),
// where the handle between them drags the split (settling on a third, half or two thirds).
@Composable
fun ListDetailSection() {
    val navigator = rememberListDetailPaneScaffoldNavigator<Int>()
    val expansion = rememberPaneExpansionState(listOf(PaneExpansionAnchor.Proportion(1 / 3f), PaneExpansionAnchor.Proportion(0.5f), PaneExpansionAnchor.Proportion(2 / 3f)))
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val cities = listOf("İstanbul", "Ankara", "İzmir", "Bursa", "Antalya", "Trabzon")
    val notes = listOf("İki kıtada bir şehir", "Başkent", "Ege'nin incisi", "Yeşil Bursa", "Turizmin merkezi", "Karadeniz'in kıyısında")
    Section("ListDetailPaneScaffold") {
        Text("Dikey telefonda liste ya da ayrıntı (geri tuşu listeye döner); yatayda ya da geniş pencerede yan yana, aradaki " +
                 "tutamaçla genişlikleri sürüklenir.",
             color = cs.onSurfaceVariant, style = typography.bodySmall)
        Box(modifier = Modifier.fillMaxWidth().height(280.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).clip(RoundedCornerShape(12.dp))) {
            ListDetailPaneScaffold(
                navigator = navigator,
                listPane = {
                    Column(modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
                        for (i in 0 until 6)
                            ListItem(headlineContent = { Text(cities[i]) }, supportingContent = { Text(notes[i]) },
                                     colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                                     modifier = Modifier.clickable { navigator.navigateTo(ListDetailPaneScaffoldRole.Detail, i) })
                    }
                },
                detailPane = { city ->
                    Column(modifier = Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        if (city == null) {
                            Text("Bir şehir seçin", color = cs.onSurfaceVariant)
                        } else {
                            Text(cities[city], style = typography.headlineSmall)
                            Text(notes[city], color = cs.onSurfaceVariant)
                            TextButton(onClick = { navigator.navigateBack() }) { Text("Geri") }
                        }
                    }
                },
                listPaneWidth = 260.dp,
                paneExpansionState = expansion,
                paneExpansionDragHandle = { state ->
                    val source = remember { MutableInteractionSource() }
                    VerticalDragHandle(modifier = Modifier.paneExpansionDraggable(state, source), interactionSource = source)
                })
        }
    }
}

// A main pane with a supporting one: side by side on a wide window, slid in on a narrow one.
@Composable
fun SupportingPaneSection() {
    val navigator = rememberSupportingPaneScaffoldNavigator<Int>()
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    Section("SupportingPaneScaffold") {
        Text("Dikey telefonda ana bölme; \"İlgili\" destek bölmesini kaydırarak getirir (geri tuşu döner). Geniş pencerede destek bölmesi sonda.",
             color = cs.onSurfaceVariant, style = typography.bodySmall)
        Box(modifier = Modifier.fillMaxWidth().height(240.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).clip(RoundedCornerShape(12.dp))) {
            SupportingPaneScaffold(
                navigator = navigator,
                mainPane = {
                    Column(modifier = Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text("Kapadokya", style = typography.headlineSmall)
                        Text("Peri bacaları, balonlar ve yer altı şehirleri.", color = cs.onSurfaceVariant)
                        FilledTonalButton(onClick = { navigator.navigateTo(SupportingPaneScaffoldRole.Supporting, 1) }) { Text("İlgili") }
                    }
                },
                supportingPane = {
                    Column(modifier = Modifier.fillMaxSize().background(cs.surfaceContainerHighest).padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text("İlgili yerler", style = typography.titleMedium)
                        for (place in listOf("Göreme", "Ürgüp", "Derinkuyu")) Text(place)
                        TextButton(onClick = { navigator.navigateBack() }) { Text("Geri") }
                    }
                },
                supportingPaneWidth = 220.dp)
        }
    }
}

// LazyListState.layoutInfo: the items in view. Near the end of the list, 20 more are loaded
// (a pretend 700 ms fetch); derivedStateOf recomposes only when its answer changes, not on
// every scrolled frame.
@Composable
fun LayoutInfoSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val list = rememberLazyListState()
    var count by remember { mutableStateOf(20) }
    var loading by remember { mutableStateOf(false) }
    val nearEnd by remember {
        derivedStateOf {
            val info = list.layoutInfo
            info.visibleItemsInfo.isNotEmpty() && info.visibleItemsInfo.last().index >= info.totalItemsCount - 3
        }
    }
    val shown by remember {
        derivedStateOf {
            val v = list.layoutInfo.visibleItemsInfo
            if (v.isEmpty()) 0 to 0 else v.first().index to v.last().index
        }
    }
    // The fetch runs in the section's scope: the answer flickering as the footer comes in
    // must not cancel it halfway (and leave it loading for good).
    val scope = rememberCoroutineScope()
    LaunchedEffect(nearEnd) {
        if (nearEnd && !loading && count < 200) {
            loading = true
            scope.launch {
                delay(700)
                count += 20
                loading = false
            }
        }
    }
    val (first, last) = shown
    Section("LazyListState.layoutInfo · contentType") {
        Text("Liste görünen öğelerini layoutInfo ile söyler; sona yaklaşınca 20 öğe daha yüklenir. derivedStateOf yalnızca " +
                 "cevabı değişince yeniden kompoze ettirir.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Text("Görünen: ${first + 1}–${last + 1} · toplam $count${if (loading) " · yükleniyor" else ""}", modifier = Modifier.padding(0.dp, 8.dp, 0.dp, 0.dp), style = typography.titleSmall)
        LazyColumn(modifier = Modifier.fillMaxWidth().padding(0.dp, 8.dp, 0.dp, 0.dp).height(260.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)),
                   state = list, contentPadding = PaddingValues(12.dp, 8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            items(count, key = { it }, contentType = { 1 }) { i ->
                Text("Öğe ${i + 1}", modifier = Modifier.fillMaxWidth().height(40.dp).padding(4.dp, 10.dp), style = typography.bodyLarge)
            }
            if (loading)
                item(key = "yükleniyor", contentType = "alt bilgi") {
                    Row(modifier = Modifier.fillMaxWidth().height(48.dp), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
                        CircularProgressIndicator(modifier = Modifier.size(20.dp))
                        Text("  yükleniyor…", style = typography.bodyMedium)
                    }
                }
        }
    }
}

// rememberTextMeasurer + drawText: a bar chart whose labels are measured to sit centered over
// their bars; onTextLayout: a paragraph that offers "devamını oku" only when it was cut off.
@Composable
fun TextMeasureSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val measurer = rememberTextMeasurer()
    var expanded by remember { mutableStateOf(false) }
    var cut by remember { mutableStateOf(false) }
    Section("rememberTextMeasurer · drawText · onTextLayout") {
        Canvas(modifier = Modifier.fillMaxWidth().height(170.dp)) {
            val months = listOf("Oca" to 42, "Şub" to 58, "Mar" to 35, "Nis" to 71, "May" to 64, "Haz" to 88)
            val w = size.width
            val slot = w / 6
            val top = 22f
            val bottom = size.height - 22
            val h = bottom - top
            for (i in 0 until 6) {
                val (name, value) = months[i]
                val x = slot * i + slot * 0.2f
                val bw = slot * 0.6f
                val bh = h * value / 100f
                drawRoundRect(cs.primary, topLeft = Offset(x, bottom - bh), size = Size(bw, bh), cornerRadius = CornerRadius(6f))
                val v = measurer.measure("%$value", style = TextStyle(fontSize = 12.sp, fontWeight = FontWeight.Bold))
                drawText(v, color = cs.onSurface, topLeft = Offset(x + (bw - v.size.width) / 2, bottom - bh - v.size.height - 2))
                val m = measurer.measure(name, style = TextStyle(fontSize = 12.sp))
                drawText(m, color = cs.onSurfaceVariant, topLeft = Offset(x + (bw - m.size.width) / 2, bottom + 4))
            }
        }
        Text("Kompoz, Jetpack Compose'un fikirlerini C++20'ye taşır: durum değişince yalnızca onu okuyan parçalar yeniden " +
                 "kompoze edilir, düzen ölçüp yerleştirir, çizim de yalnızca değişen bölgeyi yeniler. Aynı kod masaüstünde, " +
                 "Android'de, tarayıcıda ve Windows'ta çalışır; bileşenler Material 3'ü izler.",
             modifier = Modifier.padding(0.dp, 12.dp, 0.dp, 0.dp),
             maxLines = if (expanded) Int.MAX_VALUE else 2, overflow = TextOverflow.Ellipsis, style = typography.bodyMedium,
             onTextLayout = { if (!expanded) cut = it.hasVisualOverflow })
        if (cut)
            TextButton(onClick = { expanded = !expanded }) { Text(if (expanded) "Daha az" else "Devamını oku") }
    }
}

// Where the card is, read here and not around the section: as it moves, only these lines are composed again.
@Composable
fun PositionLines(where: State<LayoutCoordinates>, placed: State<Int>, color: Color) {
    val c = where.value
    val shown = if (c.size.height > 0) c.boundsInRoot().height / c.size.height else 0f
    Text("Ekranda: x ${c.positionInRoot().x.roundToInt()}, y ${c.positionInRoot().y.roundToInt()} dp · ${c.size.width.roundToInt()} × ${c.size.height.roundToInt()}",
         color = color, style = MaterialTheme.typography.titleSmall)
    Text("Görünen kısmı: %${(shown * 100).roundToInt()} · onPlaced ${placed.value} kez", color = color, style = MaterialTheme.typography.bodyMedium)
}

// onGloballyPositioned: a card that knows where it is on the screen and how much of it shows,
// as the page scrolls (onPlaced hears only of its size and place in its parent).
@Composable
fun PositionSection() {
    val cs = MaterialTheme.colorScheme
    val where = remember { mutableStateOf(LayoutCoordinates()) }
    val placed = remember { mutableStateOf(0) }
    Section("onGloballyPositioned · onPlaced · LayoutCoordinates") {
        Column(modifier = Modifier.fillMaxWidth().background(cs.tertiaryContainer, RoundedCornerShape(16.dp)).padding(16.dp)
                   .onGloballyPositioned { where.value = it }
                   .onPlaced { placed.value = placed.value + 1 },
               verticalArrangement = Arrangement.spacedBy(4.dp)) {
            PositionLines(where, placed, cs.onTertiaryContainer)
            Text("Sayfayı kaydırın: konum her karede güncellenir; onPlaced yalnızca boyutu ya da ebeveynindeki yeri değişince çağrılır.",
                 color = cs.onTertiaryContainer, style = MaterialTheme.typography.bodySmall)
        }
    }
}

// rememberSnapFlingBehavior: a row of cards that settles with one in the middle, however hard
// it is flung (the one nearest to where the fling would have stopped).
@Composable
fun SnapRowSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val row = rememberLazyListState()
    val tints = listOf(Color(0xFF6750A4), Color(0xFF7D5260), Color(0xFF386A20), Color(0xFF00639B), Color(0xFF8C4A00))
    // onFirstVisible: a card counts as seen once all of it has shown for half a second.
    var seen by remember { mutableStateOf(0) }
    // onLayoutRectChanged: where the first card is, told once the row stops moving.
    var firstCard by remember { mutableStateOf("—") }
    Section("LazyRow · rememberSnapFlingBehavior · onFirstVisible · onLayoutRectChanged") {
        Text("Fırlatın: liste, fırlatmanın varacağı yere en yakın kartı ortaya getirerek durur.", color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Text("Görülen kart (tamamı yarım saniye görünen): $seen / 20", style = typography.labelLarge)
        Text("Kart 1 (kaydırma durunca): $firstCard", color = cs.onSurfaceVariant, style = typography.bodyMedium)
        LazyRow(modifier = Modifier.fillMaxWidth().padding(0.dp, 12.dp, 0.dp, 0.dp).height(120.dp), state = row,
                horizontalArrangement = Arrangement.spacedBy(12.dp), flingBehavior = rememberSnapFlingBehavior(row)) {
            items(20) { i ->
                val where = if (i == 0) Modifier.onLayoutRectChanged { b ->
                    firstCard = "x = ${b.positionInWindow().x.roundToInt()} dp · %${(b.fractionVisibleInWindow() * 100).roundToInt()} görünür"
                } else Modifier
                Box(modifier = Modifier
                        .size(200.dp, 120.dp)
                        .background(tints[i % 5], RoundedCornerShape(20.dp))
                        .onFirstVisible(minDurationMs = 500) { seen++ }
                        .then(where),
                    contentAlignment = Alignment.Center) { Text("Kart ${i + 1}", color = Color.White, style = typography.titleMedium) }
            }
        }
    }
}

// Carousels: a multi-browse one (large, medium and small items that grow and shrink as they
// move, settling on an item) and an uncontained one (one width, free scrolling).
@Composable
fun CarouselSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val photos = listOf("images/mountains.jpg", "images/lake.webp", "images/landscape.png")
    val names = listOf("Dağlar", "Göl", "Manzara", "Gün batımı", "Orman", "Kıyı", "Vadi", "Tepe")
    val browse = rememberCarouselState { 8 }
    val free = rememberCarouselState { 12 }
    val hero = rememberCarouselState { 8 }
    Section("Carousel (HorizontalMultiBrowseCarousel · HorizontalUncontainedCarousel · HorizontalCenteredHeroCarousel)") {
        Text("Kaydırın: öğeler ilerledikçe büyüyüp küçülür, bırakınca bir öğeye oturur · şu an ${browse.currentItem + 1}. öğe",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        // The name fades with the item's size (carouselItemInfo, read while drawing).
        HorizontalMultiBrowseCarousel(state = browse, preferredItemWidth = 186.dp, modifier = Modifier.fillMaxWidth().height(200.dp), itemSpacing = 8.dp) { i ->
            val info = carouselItemInfo
            // maskBorder: a thin light line along the part of the photo the item shows.
            Box(modifier = Modifier.fillMaxSize().background(hueColor(i * 7)).then(maskBorder(BorderStroke(2.dp, Color.White.copy(alpha = 0.7f)), RoundedCornerShape(28.dp))),
                contentAlignment = Alignment.BottomStart) {
                AsyncImage(model = photos[i % 3], contentDescription = names[i], modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
                Text(names[i], modifier = Modifier.graphicsLayer {
                    val span = info.maxSize - info.minSize
                    alpha = if (span > 0) ((info.size - info.minSize) / span).coerceIn(0f, 1f) else 1f
                }.padding(16.dp, 12.dp), color = Color.White, style = typography.titleMedium)
            }
        }
        HorizontalUncontainedCarousel(state = free, itemWidth = 120.dp, modifier = Modifier.fillMaxWidth().height(120.dp), itemSpacing = 8.dp) { i ->
            Box(modifier = Modifier.fillMaxSize().clip(RoundedCornerShape(16.dp)).background(hueColor(i * 5)), contentAlignment = Alignment.Center) {
                Text("${i + 1}", color = Color.White, style = typography.headlineSmall)
            }
        }
        // One large in the middle, a small one either side (the first and the last large at the ends).
        Text("Ortada büyük olan: ${names[hero.currentItem]}", color = cs.onSurfaceVariant, style = typography.bodyMedium)
        HorizontalCenteredHeroCarousel(state = hero, modifier = Modifier.fillMaxWidth().height(180.dp), itemSpacing = 8.dp) { i ->
            Box(modifier = Modifier.fillMaxSize().background(hueColor(i * 9)), contentAlignment = Alignment.BottomStart) {
                AsyncImage(model = photos[(i + 1) % 3], contentDescription = names[i], modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
                Text(names[i], modifier = Modifier.padding(16.dp, 12.dp), color = Color.White, style = typography.titleMedium)
            }
        }
    }
}

// Three rows of tiles that scroll sideways, composed column by column.
@Composable
fun HorizontalGridSection() {
    Section("LazyHorizontalGrid · LazyHorizontalStaggeredGrid") {
        LazyHorizontalGrid(rows = GridCells.Fixed(3), modifier = Modifier.fillMaxWidth().height(180.dp),
                           verticalArrangement = Arrangement.spacedBy(6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            items(300) { i ->
                Box(modifier = Modifier.width(if (i % 7 == 0) 120.dp else 72.dp).fillMaxHeight().background(hueColor(i), RoundedCornerShape(12.dp)),
                    contentAlignment = Alignment.Center) { Text("$i", fontWeight = FontWeight.Bold, color = Color.White) }
            }
        }
        // Items of their own widths, each after the shorter row (LazyHorizontalStaggeredGrid).
        LazyHorizontalStaggeredGrid(rows = StaggeredGridCells.Fixed(2), modifier = Modifier.fillMaxWidth().height(120.dp),
                                    horizontalItemSpacing = 6.dp, verticalArrangement = Arrangement.spacedBy(6.dp)) {
            items(200) { i ->
                Box(modifier = Modifier.width((48 + (i * 37) % 90).dp).fillMaxHeight().background(hueColor(i * 3), RoundedCornerShape(12.dp)),
                    contentAlignment = Alignment.Center) { Text("$i", fontWeight = FontWeight.Bold, color = Color.White) }
            }
        }
    }
}

@Composable
fun PagerSection() {
    val pager = rememberPagerState(pageCount = { 5 })
    val cards = rememberPagerState(pageCount = { 10 })
    val scope = rememberCoroutineScope()
    var expanded by remember { mutableStateOf(false) }
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val current = pager.currentPage
    Section("HorizontalPager · PageSize · animateContentSize · BoxWithConstraints") {
        HorizontalPager(state = pager, modifier = Modifier.fillMaxWidth().height(150.dp), pageSpacing = 12.dp) { page ->
            Box(modifier = Modifier.fillMaxSize().background(Brush.linearGradient(listOf(hueColor(page * 1700 + 300), hueColor(page * 1700 + 900))), RoundedCornerShape(20.dp)),
                contentAlignment = Alignment.Center) {
                Text("Sayfa ${page + 1}", fontSize = 28.sp, fontWeight = FontWeight.Bold, color = Color.White)
            }
        }
        Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = { scope.launch { pager.animateScrollToPage(current - 1) } }) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Önceki") }
            Row(modifier = Modifier.weight(1f), horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally), verticalAlignment = Alignment.CenterVertically) {
                for (i in 0 until 5)
                    Box(modifier = Modifier.size(if (i == current) 10.dp else 8.dp).background(if (i == current) cs.primary else cs.outlineVariant, CircleShape))
            }
            IconButton(onClick = { scope.launch { pager.animateScrollToPage(current + 1) } }) { Icon(Icons.AutoMirrored.Filled.ArrowForward, contentDescription = "Sonraki") }
        }
        // PageSize.Fixed pages in the middle (SnapPosition.Center; the side padding, half the
        // room the page leaves, lets the first and last ones get there too), shrinking and fading
        // as they leave it (getOffsetDistanceInPages, read in graphicsLayer's block while
        // drawing); a fling turns up to three pages.
        Text("PageSize.Fixed · SnapPosition.Center · PagerSnapDistance.atMost(3)", color = cs.onSurfaceVariant, style = typography.labelLarge)
        BoxWithConstraints(modifier = Modifier.fillMaxWidth()) {
            val side = if (constraints.hasBoundedWidth) max(0f, (maxWidth.value - 140) / 2) else 0f  // (unbounded before it is first measured)
            HorizontalPager(state = cards, modifier = Modifier.fillMaxWidth().height(120.dp), contentPadding = PaddingValues(horizontal = side.dp),
                            pageSize = PageSize.Fixed(140.dp), pageSpacing = 12.dp, snapPosition = SnapPosition.Center,
                            flingBehavior = PagerDefaults.flingBehavior(cards, PagerSnapDistance.atMost(3))) { page ->
                Box(modifier = Modifier
                        .fillMaxSize()
                        .graphicsLayer {
                            val away = min(1f, abs(cards.getOffsetDistanceInPages(page)))
                            scaleX = 1 - 0.15f * away
                            scaleY = 1 - 0.15f * away
                            alpha = 1 - 0.5f * away
                        }
                        .background(hueColor(page * 900 + 200), RoundedCornerShape(16.dp)),
                    contentAlignment = Alignment.Center) { Text("${page + 1}", fontSize = 32.sp, fontWeight = FontWeight.Bold, color = Color.White) }
            }
        }
        Text("currentPage ${cards.currentPage + 1} · settledPage ${cards.settledPage + 1} · targetPage ${cards.targetPage + 1}", color = cs.onSurfaceVariant, style = typography.bodyMedium)
        // key: a page put in before the current one leaves it the current page (one further on);
        // reverseLayout: the pages from the end, the finger the other way.
        var pageNumbers by remember { mutableStateOf(listOf(1, 2, 3)) }
        var fromEnd by remember { mutableStateOf(false) }
        val numbers = pageNumbers
        val keyed = rememberPagerState(pageCount = { numbers.size })
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedButton(onClick = { pageNumbers = listOf(pageNumbers.maxOf { it } + 1) + pageNumbers }) { Text("Başa sayfa ekle") }
            Switch(checked = fromEnd, onCheckedChange = { fromEnd = it })
            Text("reverseLayout", style = typography.bodyMedium)
        }
        HorizontalPager(state = keyed, modifier = Modifier.fillMaxWidth().height(72.dp), pageSpacing = 8.dp, reverseLayout = fromEnd, key = { numbers[it] }) { i ->
            Box(modifier = Modifier.fillMaxSize().background(hueColor(numbers[i] * 700 + 100), RoundedCornerShape(12.dp)), contentAlignment = Alignment.Center) {
                Text("Sayfa ${numbers[i]}", color = Color.White, style = typography.titleMedium)
            }
        }
        Text("key ile: currentPage ${keyed.currentPage + 1} · ${numbers.size} sayfa", color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Column(modifier = Modifier.fillMaxWidth().background(cs.secondaryContainer, RoundedCornerShape(16.dp)).clickable { expanded = !expanded }
                   .animateContentSize().padding(16.dp),
               verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text("animateContentSize (dokunun)", color = cs.onSecondaryContainer, style = typography.titleMedium)
            Text(if (expanded) "Boyut değişince kart yumuşakça büyür ve küçülür. Animasyon layout sırasında kare saatine " +
                                   "göre hesaplanır; yeniden kompozisyon gerekmez. İçerik animasyon boyunca kırpılır."
                 else "Kısa metin.",
                 color = cs.onSecondaryContainer, style = typography.bodyMedium)
        }
        BoxWithConstraints(modifier = Modifier.fillMaxWidth()) {
            val wide = maxWidth >= 360.dp
            Column(modifier = Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("BoxWithConstraints: ${if (constraints.hasBoundedWidth) maxWidth.value.roundToInt() else 0} dp, ${if (wide) "yan yana" else "alt alta"}",
                     color = cs.onSurfaceVariant, style = typography.bodyMedium)
                val tile: @Composable (String, Color) -> Unit = { label, color ->
                    Box(modifier = Modifier.fillMaxWidth().height(48.dp).background(color, RoundedCornerShape(12.dp)).padding(12.dp, 0.dp), contentAlignment = Alignment.CenterStart) {
                        Text(label, color = Color.White)
                    }
                }
                if (wide)
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Box(modifier = Modifier.weight(1f)) { tile("Birinci", hueColor(100)) }
                        Box(modifier = Modifier.weight(1f)) { tile("İkinci", hueColor(400)) }
                    }
                else {
                    tile("Birinci", hueColor(100))
                    tile("İkinci", hueColor(400))
                }
            }
        }
    }
}

// Modifier.zIndex: overlapping cards stacked by zIndex; the one tapped goes to the top of the
// stack, the others keep their order (drawn and touched in that order).
@Composable
fun ZIndexSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var stack by remember { mutableStateOf(listOf(0, 1, 2)) }  // bottom to top
    Section("Modifier.zIndex") {
        Text("Üst üste üç kart; her birinin zIndex'i yığındaki yeri. Dokunduğun en üste çıkar, diğerleri sırasını korur; " +
                 "dokunuşu da en üstteki alır.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Text("Alttan üste: ${stack.joinToString(" · ") { (it + 1).toString() }}", color = cs.primary, style = typography.labelLarge)
        Box(modifier = Modifier.fillMaxWidth().height(150.dp)) {
            val fills = listOf(cs.primaryContainer, cs.secondaryContainer, cs.tertiaryContainer)
            val inks = listOf(cs.onPrimaryContainer, cs.onSecondaryContainer, cs.onTertiaryContainer)
            for (i in 0 until 3) {
                val level = stack.indexOf(i)
                val onTop = level == 2
                Box(modifier = Modifier
                        .offset((i * 56).dp, (i * 24).dp)
                        .zIndex(level.toFloat())
                        .size(180.dp, 100.dp)
                        .shadow(if (onTop) 6.dp else 1.dp, RoundedCornerShape(16.dp))
                        .background(fills[i], RoundedCornerShape(16.dp))
                        .clickable { stack = stack.filter { it != i } + i }  // to the top; the rest stay as they were
                        .padding(14.dp)) {
                    Text("Kart ${i + 1}${if (onTop) " · üstte" else ""}", color = inks[i], style = typography.titleMedium)
                }
            }
        }
    }
}

// FlexBox (Compose 1.12): grow and shrink, wrapping, justifyContent and alignment picked here.
@Composable
fun FlexBoxSection() {
    var justify by remember { mutableStateOf(0) }
    var wrapping by remember { mutableStateOf(true) }
    var baseline by remember { mutableStateOf(false) }
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    Section("FlexBox · FlexConfig (grow, shrink, basis, order)") {
        val names = listOf("Start", "Center", "SpaceBetween", "SpaceEvenly")
        val values = listOf(FlexJustifyContent.Start, FlexJustifyContent.Center, FlexJustifyContent.SpaceBetween, FlexJustifyContent.SpaceEvenly)
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for (i in 0 until 4) FilterChip(selected = justify == i, onClick = { justify = i }, label = { Text(names[i]) })
            FilterChip(selected = wrapping, onClick = { wrapping = !wrapping }, label = { Text("Wrap") })
            FilterChip(selected = baseline, onClick = { baseline = !baseline }, label = { Text("Baseline") })
        }
        FlexBox(modifier = Modifier.fillMaxWidth().background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).clip(RoundedCornerShape(12.dp)).padding(8.dp),
                config = {
                    wrap(if (wrapping) FlexWrap.Wrap else FlexWrap.NoWrap)
                    justifyContent(values[justify])
                    alignItems(if (baseline) FlexAlignItems.Baseline else FlexAlignItems.Center)
                    gap(8.dp)
                }) {
            val tag: @Composable (String, TextUnit, Modifier) -> Unit = { text, size, modifier ->
                Box(modifier = modifier.background(cs.secondaryContainer, RoundedCornerShape(8.dp)).padding(10.dp, 6.dp)) { Text(text, fontSize = size, color = cs.onSecondaryContainer) }
            }
            tag("Kotlin", 14.sp, Modifier)
            tag("C++20", 22.sp, Modifier)
            tag("Compose", 14.sp, Modifier.flex { order(-1) })  // first, by its order
            tag("Vulkan", 18.sp, Modifier)
            tag("Büyür (grow 1)", 14.sp, Modifier.flex { grow(1f) })
            tag("HarfBuzz", 12.sp, Modifier)
        }
        Text("Compose ilk sırada (order -1); \"Büyür\" satırında kalan yeri alır. Wrap kapalıyken hepsi tek satırda kalır: en uzun " +
                 "kelimelerine kadar küçülür (\"Büyür\" alt alta kırılır), sığmayan taşar (CSS'teki gibi).",
             color = cs.onSurfaceVariant, style = typography.bodySmall)
    }
}

@Composable
fun GridSection() {
    var byColumn by remember { mutableStateOf(false) }
    var gaps by remember { mutableStateOf(true) }
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    Section("Grid · gridItem (satır, sütun, yayılma, alan)") {
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(selected = byColumn, onClick = { byColumn = !byColumn }, label = { Text("Sütun akışı") })
            FilterChip(selected = gaps, onClick = { gaps = !gaps }, label = { Text("Boşluk 8 dp") })
        }
        val spacing = if (gaps) 8.dp else 0.dp
        Grid(modifier = Modifier.fillMaxWidth().background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).padding(8.dp),
             config = {
                 columns(72.dp, 1.fr, 2.fr)
                 rows(GridTrackSize.Auto, 56.dp, 56.dp)
                 gap(row = spacing, column = spacing)
                 flow = if (byColumn) GridFlow.Column else GridFlow.Row
                 area("başlık", row = 1, column = 1, rowSpan = 1, columnSpan = 3)
                 area("yan", row = 2, column = 1, rowSpan = 2, columnSpan = 1)
             }) {
            val tile: @Composable (String, Color, Color, Modifier) -> Unit = { text, background, color, modifier ->
                Box(modifier = modifier.background(background, RoundedCornerShape(8.dp)).padding(8.dp)) { Text(text, color = color, style = typography.labelLarge) }
            }
            tile("Başlık: alan, 3 sütun", cs.primaryContainer, cs.onPrimaryContainer, Modifier.fillMaxWidth().gridItem("başlık"))
            tile("Yan: 2 satır", cs.tertiaryContainer, cs.onTertiaryContainer, Modifier.fillMaxSize().gridItem("yan"))
            for (i in 1..5) tile("$i", cs.secondaryContainer, cs.onSecondaryContainer, Modifier.fillMaxSize())
            Box(modifier = Modifier.size(16.dp).gridItem(row = -1, column = -1, alignment = Alignment.BottomEnd).background(cs.primary, RoundedCornerShape(8.dp)))
        }
        Text("Sütunlar 72 dp, 1fr, 2fr. 1-5 boş hücrelere akar; 5. yapılandırılmamış bir satıra (sütun akışında sütuna) taşar. " +
                 "Nokta son satır ve sütunda, sağ altta (-1, -1).",
             color = cs.onSurfaceVariant, style = typography.bodySmall)
    }
}

@Composable
fun LayoutSection() {
    var extra by remember { mutableStateOf(0) }  // how much longer the SubcomposeLayout title is
    val selected = remember { mutableStateOf(0) }
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    Section("Layout · IntrinsicSize · FlowRow · updateTransition") {
        // Alignment lines: texts of different sizes and a button's label on one baseline.
        val caption: @Composable (String) -> Unit = { text -> Text(text, color = cs.onSurfaceVariant, style = typography.bodyMedium) }
        caption("alignByBaseline: farklı boyutlarda metinler ve düğme etiketi aynı taban çizgisinde.")
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Fiyat", modifier = Modifier.alignByBaseline(), style = typography.labelLarge)
            Text("₺249", modifier = Modifier.alignByBaseline(), color = cs.primary, style = typography.displaySmall)
            Text("/ay", modifier = Modifier.alignByBaseline(), style = typography.bodySmall)
            Spacer(modifier = Modifier.weight(1f))
            OutlinedButton(onClick = {}, modifier = Modifier.alignByBaseline()) { Text("Seç") }
        }
        var city by remember { mutableStateOf("Ankara") }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Text("Şehir:", modifier = Modifier.alignByBaseline(), style = typography.titleMedium)
            OutlinedTextField(value = city, onValueChange = { city = it }, modifier = Modifier.weight(1f).alignByBaseline(), singleLine = true, label = { Text("Teslimat") })
        }
        caption("paddingFromBaseline(32, 16): metin bir ızgaraya göre (ilk taban çizgisi üstten 32, son taban çizgisi alttan 16).")
        Text("Taban çizgisinden boşluk", modifier = Modifier.fillMaxWidth().background(cs.surfaceContainerHigh, RoundedCornerShape(8.dp)).paddingFromBaseline(32.dp, 16.dp).padding(12.dp, 0.dp))
        // A custom Layout: children down a staircase, spread over the width.
        Layout(modifier = Modifier.fillMaxWidth(), content = {
            for (i in 0 until 5)
                Box(modifier = Modifier.size(44.dp).background(hueColor(i * 3 + 1), RoundedCornerShape(12.dp)), contentAlignment = Alignment.Center) {
                    Text("${i + 1}", fontWeight = FontWeight.Bold, color = Color.White)
                }
        }) { measurables, constraints ->
            val w = if (constraints.hasBoundedWidth) constraints.maxWidth else 300f
            val placeables = measurables.map { it.measure(constraints.copy(minWidth = 0, minHeight = 0)) }
            val n = placeables.size
            var h = 0f
            placeables.forEachIndexed { i, p ->
                val y = i * 10f
                h = max(h, y + p.height)
            }
            layout(w, h) {
                placeables.forEachIndexed { i, p ->
                    val x = if (n > 1) i * (w - p.width) / (n - 1) else 0f
                    p.placeRelative(x, i * 10f)
                }
            }
        }
        // SubcomposeLayout: the title measured first, then a line composed as wide as it is.
        val word = "Kompoz" + "!".repeat(extra * 2)
        TextButton(onClick = { extra = (extra + 1) % 6 }) { Text("Başlığı uzat") }
        SubcomposeLayout(modifier = Modifier.fillMaxWidth()) { constraints ->
            val loose = constraints.copy(minWidth = 0, minHeight = 0)
            val title = subcompose("başlık") { Text(word, style = typography.titleMedium) }.map { it.measure(loose) }
            val w = title.maxOf { it.width }
            val h = title.maxOf { it.height }
            val line = subcompose("çizgi", w) {
                Column {
                    Box(modifier = Modifier.width(w.dp).height(3.dp).background(cs.primary, RoundedCornerShape(2.dp)))
                    Text("SubcomposeLayout: başlığın genişliği ${w.roundToInt()} dp", style = typography.bodySmall)
                }
            }.map { it.measure(loose) }
            layout(constraints.maxWidth, h + 2 + line.sumOf { it.height }) {
                title.forEach { it.placeRelative(0, 0) }
                line.forEach { it.placeRelative(0, h + 2) }
            }
        }
        // IntrinsicSize.Min: the divider is as tall as the taller text.
        Row(modifier = Modifier.fillMaxWidth().height(IntrinsicSize.Min).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).padding(12.dp)) {
            Text("Kısa metin", modifier = Modifier.weight(1f), style = typography.bodyMedium)
            VerticalDivider(modifier = Modifier.padding(8.dp, 0.dp))
            Text("Uzun metin iki üç satıra iner; IntrinsicSize.Min sayesinde bölücü de onunla birlikte uzar.", modifier = Modifier.weight(1f), style = typography.bodyMedium)
        }
        // width(IntrinsicSize.Min / Max): as narrow as the widest word, or as the text on one line.
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            for (size in listOf(IntrinsicSize.Min, IntrinsicSize.Max))
                Column(modifier = Modifier.width(size).background(cs.tertiaryContainer, RoundedCornerShape(12.dp)).padding(12.dp)) {
                    Text(if (size == IntrinsicSize.Min) "width(Min)" else "width(Max)", color = cs.onTertiaryContainer, style = typography.labelMedium)
                    Text("Ortak yağmur bulutları", color = cs.onTertiaryContainer, style = typography.bodyMedium)
                    Box(modifier = Modifier.fillMaxWidth().padding(0.dp, 4.dp).height(2.dp).background(cs.tertiary))  // as wide as the column came out
                }
        }
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            for (tag in listOf("C++20", "Compose", "Vulkan", "FreeType", "coroutine", "slot table", "Android", "Linux"))
                SuggestionChip(onClick = {}, label = { Text(tag) })
        }
        // animateBounds in a LookaheadScope: the box glides to the next corner and grows or
        // shrinks on each tap; shuffled chips glide to their new places.
        var corner by remember { mutableStateOf(0) }
        var order by remember { mutableStateOf(listOf(0, 1, 2, 3, 4, 5, 6)) }
        Text("animateBounds · LookaheadScope", style = typography.titleSmall)
        LookaheadScope(modifier = Modifier.fillMaxWidth().height(132.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).clickable { corner = (corner + 1) % 5 }) {
            val aligns = listOf(Alignment.TopStart, Alignment.TopEnd, Alignment.BottomEnd, Alignment.BottomStart, Alignment.Center)
            Box(modifier = Modifier.fillMaxSize().padding(10.dp), contentAlignment = aligns[corner]) {
                Box(modifier = Modifier.animateBounds().size(if (corner % 2 == 0) 56.dp else 112.dp, 56.dp).background(cs.primary, RoundedCornerShape(16.dp)), contentAlignment = Alignment.Center) {
                    Text("dokun", color = cs.onPrimary, style = typography.labelLarge)
                }
            }
        }
        val words = listOf("bir", "iki", "üç", "dört", "beş", "altı", "yedi")
        val glide = spring<Float>(Spring.DampingRatioLowBouncy, Spring.StiffnessLow)  // slow enough to follow
        LookaheadScope(modifier = Modifier.fillMaxWidth()) {
            FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                for (id in order) key(id) { SuggestionChip(onClick = {}, label = { Text(words[id]) }, modifier = Modifier.animateBounds(glide)) }
            }
        }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilledTonalButton(onClick = { order = order.reversed().let { it.drop(2) + it.take(2) } }, modifier = Modifier.weight(1f)) { Text("Karıştır") }  // the far ones swap rows, the near ones swap places
            OutlinedButton(onClick = { order = listOf(0, 1, 2, 3, 4, 5, 6) }, modifier = Modifier.weight(1f)) { Text("Sırala") }
        }
        // skipToLookaheadSize: the first card's text is laid out at the width the card is
        // going to, so it doesn't reflow while the card grows or shrinks; the second's does.
        var wideCards by remember { mutableStateOf(false) }
        Text("skipToLookaheadSize · kartlara dokunun", style = typography.titleSmall)
        Column(modifier = Modifier.fillMaxWidth().clickable { wideCards = !wideCards }, verticalArrangement = Arrangement.spacedBy(8.dp)) {
            for (skip in listOf(true, false))
                Box(modifier = Modifier.animateBounds(glide).width(if (wideCards) 320.dp else 150.dp).background(cs.secondaryContainer, RoundedCornerShape(12.dp)).clipToBounds().padding(10.dp)) {
                    Text(if (skip) "Atlayan: metin gideceği genişlikte dizilir, kart büyürken kaymaz." else "Atlamayan: metin her karede yeniden kırılır.",
                         modifier = if (skip) Modifier.skipToLookaheadSize() else Modifier, color = cs.onSecondaryContainer, style = typography.bodyMedium)
                }
        }
        // approachLayout + DeferredTargetAnimation: a tag that grows and shrinks to its text
        // (the lookahead) by a spring, laid out along the way.
        var longTag by remember { mutableStateOf(false) }
        val tagSize = remember { DeferredTargetAnimation<Size>(spring(Spring.DampingRatioMediumBouncy, Spring.StiffnessLow)) }
        val tagScope = rememberCoroutineScope()
        Text("approachLayout · etikete dokunun", style = typography.titleSmall)
        Box(modifier = Modifier
                .approachLayout(
                    isMeasurementApproachInProgress = { target ->
                        tagSize.updateTarget(target, tagScope)
                        !tagSize.isIdle()
                    },
                    approachMeasure = { scope, measure, _, _ ->
                        val now = tagSize.updateTarget(scope.lookaheadSize, tagScope)
                        measure(Constraints.fixed(max(0f, now.width), max(0f, now.height)))
                        now
                    })
                .background(cs.tertiaryContainer, RoundedCornerShape(16.dp))
                .clipToBounds()
                .clickable { longTag = !longTag }
                .padding(12.dp, 8.dp)) {
            Text(if (longTag) "Uzun bir etiket: gideceği boyuta yaylanarak gider" else "Kısa", color = cs.onTertiaryContainer)
        }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for (i in 0 until 3) SelectableCard(i, selected)
        }
    }
}
