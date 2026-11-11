// Components screen: flings, scrollbars, predictive back, pointer passes, scrollables, overscroll, bring-into-view, nested scroll, pull to refresh, drawers.

#include "showcase.hpp"

// A puck on a track: thrown, it slides on from the finger's speed and slows down (animateDecay,
// stopping at the track's ends: updateBounds); or it springs back to the middle carrying that
// speed (animateTo with an initial velocity). A VelocityTracker measures how fast the finger let go.
@Kompozable
fun FlingTrack(a: Animatable<Float, AnimationVector1D>, decays: Boolean, onReleased: (Float) -> Unit) {
    val cs = MaterialTheme.colorScheme
    val scope = rememberCoroutineScope()
    Box(modifier = Modifier.fillMaxWidth().padding(0.dp, 12.dp, 0.dp, 0.dp).height(56.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(28.dp))
            .onSizeChanged { if (decays) a.updateBounds(0f, max(0f, it.width - 56f)) },
        contentAlignment = if (decays) Alignment.CenterStart else Alignment.Center) {
        Box(modifier = Modifier.offset { IntOffset((a.value + (if (decays) 4f else 0f)).roundToInt(), 0) }.size(48.dp)
                .background(if (decays) cs.primary else cs.tertiary, CircleShape)
                .pointerInput(Unit) {
                    val tracker = VelocityTracker()
                    var dragged by mutableStateOf(0f)  // the finger's way so far (the puck moves under it)
                    detectDragGestures(
                        onDragStart = {
                            a.stop()
                            tracker.resetTracking()
                        },
                        onDrag = { change, amount ->
                            change.consume()
                            dragged += amount.x
                            tracker.addPosition(change.uptimeMillis, Offset(dragged, 0f))
                            a.snapTo(a.value + amount.x)
                        },
                        onDragEnd = {
                            val v = tracker.calculateVelocity().x
                            onReleased(v)
                            scope.launch {
                                if (decays) a.animateDecay(v, splineBasedDecay())
                                else a.animateTo(0f, spring(Spring.DampingRatioMediumBouncy, Spring.StiffnessLow), v)
                            }
                        })
                })
    }
}

@Kompozable
fun FlingSection() {
    val cs = MaterialTheme.colorScheme
    val slide = remember { Animatable(0f) }
    val bounce = remember { Animatable(0f) }
    var released by remember { mutableStateOf(0f) }
    Section("animateDecay · VelocityTracker · spring(initialVelocity)") {
        Text("Mor topu fırlatın: bırakış hızıyla kayar, yavaşlar, kenarda durur. Diğerini çekip bırakın: hızını taşıyarak " +
                 "yaylanır ve ortaya döner.",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        FlingTrack(slide, true) { released = it }
        FlingTrack(bounce, false) { released = it }
        Text("Bırakış hızı: ${released.roundToInt()} dp/s", modifier = Modifier.padding(0.dp, 8.dp, 0.dp, 0.dp), style = MaterialTheme.typography.bodyMedium)
    }
}

// The standard drawers in small frames: one always shown, one that pushes the page aside.
@Kompozable
fun DrawersSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var page by remember { mutableStateOf(0) }
    val drawer = rememberDrawerState()
    val coroutines = rememberCoroutineScope()
    val pages = listOf("Gelen", "Gönderilen", "Taslaklar")
    val items: @Kompozable (Boolean) -> Unit = { closeAfter ->
        for (i in 0 until 3)
            NavigationDrawerItem(label = { Text(pages[i]) }, selected = page == i, onClick = {
                page = i
                if (closeAfter) coroutines.launch { drawer.close() }
            })
    }
    val frame = Modifier.fillMaxWidth().height(200.dp).border(1.dp, cs.outlineVariant, RoundedCornerShape(12.dp)).clip(RoundedCornerShape(12.dp))
    Section("PermanentNavigationDrawer · DismissibleNavigationDrawer") {
        PermanentNavigationDrawer(drawerContent = { PermanentDrawerSheet(modifier = Modifier.width(170.dp)) { items(false) } }, modifier = frame) {
            Box(modifier = Modifier.fillMaxSize().background(cs.surfaceContainer), contentAlignment = Alignment.Center) { Text(pages[page]) }
        }
        Box(modifier = frame) {
            DismissibleNavigationDrawer(drawerContent = { DismissibleDrawerSheet(modifier = Modifier.width(170.dp)) { items(true) } }, drawerState = drawer) {
                Column(modifier = Modifier.fillMaxSize().background(cs.surfaceContainer)) {
                    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        IconButton(onClick = {
                            coroutines.launch { if (drawer.isOpen) drawer.close() else drawer.open() }
                        }) { Icon(Icons.Filled.Menu, contentDescription = "Menü") }
                        Text(pages[page], style = typography.titleMedium)
                    }
                    Text("Menüye dokunun ya da sağa sürükleyin: çekmece sayfayı iter.", modifier = Modifier.padding(16.dp, 8.dp),
                         color = cs.onSurfaceVariant, style = typography.bodyMedium)
                }
            }
        }
    }
}

// Desktop scrollbars beside a scrolled column, a long lazy list and a row: drag a thumb, click a
// track to page, hover to darken.
@Kompozable
fun ScrollbarSection() {
    val cs = MaterialTheme.colorScheme
    val column = rememberScrollState()
    val row = rememberScrollState()
    val list = rememberLazyListState()
    Section("VerticalScrollbar · HorizontalScrollbar") {
        Text("Başparmağı sürükleyin, raya tıklayınca bir sayfa kayar; fare üzerindeyken koyulaşır. Liste 1000 satır: " +
                 "boyu görünen satırlardan tahmin edilir.",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        // BringIntoViewRequester: a paragraph and a box scrolled into view on the scrollables' spring.
        val paragraph = remember { BringIntoViewRequester() }
        val box = remember { BringIntoViewRequester() }
        val scope = rememberCoroutineScope()
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { scope.launch { paragraph.bringIntoView() } }) { Text("Paragraf 25'i göster") }
            OutlinedButton(onClick = { scope.launch { box.bringIntoView() } }) { Text("18'i göster") }
        }
        Row(modifier = Modifier.fillMaxWidth().height(200.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Box(modifier = Modifier.weight(1f).fillMaxHeight().background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).clip(RoundedCornerShape(12.dp))) {
                Column(modifier = Modifier.fillMaxSize().verticalScroll(column).padding(12.dp, 8.dp)) {
                    for (i in 1..30)
                        Text("Paragraf $i", modifier = (if (i == 25) Modifier.bringIntoViewRequester(paragraph) else Modifier).padding(0.dp, 4.dp))
                }
                VerticalScrollbar(adapter = rememberScrollbarAdapter(column), modifier = Modifier.align(Alignment.CenterEnd).fillMaxHeight().padding(0.dp, 4.dp, 2.dp, 4.dp))
            }
            Box(modifier = Modifier.weight(1f).fillMaxHeight().background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).clip(RoundedCornerShape(12.dp))) {
                LazyColumn(modifier = Modifier.fillMaxSize(), state = list) {
                    items(1000) { i -> Text("Satır ${i + 1}", modifier = Modifier.padding(12.dp, 6.dp)) }
                }
                VerticalScrollbar(adapter = rememberScrollbarAdapter(list), modifier = Modifier.align(Alignment.CenterEnd).fillMaxHeight().padding(0.dp, 4.dp, 2.dp, 4.dp))
            }
        }
        Box(modifier = Modifier.fillMaxWidth().height(64.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).clip(RoundedCornerShape(12.dp))) {
            Row(modifier = Modifier.fillMaxSize().horizontalScroll(row).padding(8.dp, 8.dp, 8.dp, 14.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for (i in 1..24)
                    Box(modifier = (if (i == 18) Modifier.bringIntoViewRequester(box) else Modifier).size(72.dp, 40.dp).background(hueColor(i * 900), RoundedCornerShape(8.dp)),
                        contentAlignment = Alignment.Center) { Text(i.toString(), color = Color.White) }
            }
            HorizontalScrollbar(adapter = rememberScrollbarAdapter(row), modifier = Modifier.align(Alignment.BottomCenter).fillMaxWidth().padding(4.dp, 0.dp, 4.dp, 2.dp))
        }
    }
}

// PredictiveBackHandler: while the panel is open, Android's back gesture (14+) shrinks it and
// moves it away from the edge the finger came from; done, it closes; called off, it comes back.
// Escape (the back button before Android 13) closes it at once.
@Kompozable
fun PredictiveBackSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var open by remember { mutableStateOf(false) }
    var progress by remember { mutableStateOf(0f) }
    var fromRight by remember { mutableStateOf(false) }
    var last by remember { mutableStateOf("—") }
    PredictiveBackHandler(open) { steps ->
        try {
            last = "hareket sürüyor"
            steps.collect { e ->
                progress = e.progress
                fromRight = e.swipeEdge == BackEventCompat.SwipeEdge.Right
            }
            last = "kapandı"
            open = false
        } finally {
            progress = 0f  // also when the gesture is called off (the coroutine is cancelled)
        }
    }
    Section("PredictiveBackHandler") {
        Text("Paneli açın, sonra ekranın kenarından geri hareketini yapın: panel parmağı izler, bırakınca kapanır, " +
                 "vazgeçince yerine döner (Android 14+). Panel kapalıyken geri hareketi uygulamadan çıkar.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Button(onClick = {
                open = true
                last = "açık"
            }, enabled = !open) { Text("Paneli aç") }
            Text(if (open && progress > 0) "geri: %${(progress * 100).roundToInt()} · ${if (fromRight) "sağdan" else "soldan"}" else last,
                 style = typography.bodyMedium)
        }
        AnimatedVisibility(visible = open, modifier = Modifier.fillMaxWidth(), enter = fadeIn() + expandVertically(), exit = fadeOut() + shrinkVertically()) {
            Box(modifier = Modifier
                    .fillMaxWidth()
                    .height(140.dp)
                    .graphicsLayer {  // read while drawing
                        val p = progress
                        scaleX = 1 - 0.1f * p
                        scaleY = 1 - 0.1f * p
                        translationX = (if (fromRight) -1f else 1f) * 24 * p
                    }
                    .background(cs.primaryContainer, RoundedCornerShape(24.dp))
                    .padding(20.dp),
                contentAlignment = Alignment.CenterStart) {
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null, tint = cs.onPrimaryContainer)
                    Text("Geri hareketiyle kapatın", color = cs.onPrimaryContainer, style = typography.titleMedium)
                }
            }
        }
    }
}

// PointerEventPass: a parent that sees each press before what is inside it (Initial) and can
// take it, and after (Final) to tell whether something inside took it.
@Kompozable
fun PointerPassSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var locked by remember { mutableStateOf(false) }
    var clicks by remember { mutableStateOf(0) }
    var seen by remember { mutableStateOf("—") }
    Section("PointerEventPass (Initial · Main · Final)") {
        Text("Kilitliyken dış kutu basışı Initial geçişinde, içtekilerden önce tüketir: düğme tepki vermez. " +
                 "Final geçişinde basışı bir şeyin alıp almadığını görür.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Switch(checked = locked, onCheckedChange = { locked = it })
            Text("Kilitli", modifier = Modifier.padding(12.dp, 0.dp, 0.dp, 0.dp))
        }
        Box(modifier = Modifier.fillMaxWidth().height(96.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp))
                .pointerInput(Unit) {
                    while (true) {
                        val first = awaitPointerEvent(PointerEventPass.Initial)
                        if (locked && first.type == PointerEventType.Press) first.changes.first().consume()
                        val last = awaitPointerEvent(PointerEventPass.Final)
                        if (last.type == PointerEventType.Press)
                            seen = if (last.changes.first().isConsumed) "basış alındı (düğme ya da kilit)" else "basışı kimse almadı"
                    }
                },
            contentAlignment = Alignment.Center) {
            Button(onClick = { clicks++ }) { Text("Bas ($clicks)") }
        }
        Text("Final: $seen", color = cs.primary, style = typography.bodySmall)
    }
}

@Kompozable
fun ScrollableSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val measurer = rememberTextMeasurer()
    var angle by remember { mutableFloatStateOf(0f) }
    val knob = rememberScrollableState { delta ->
        angle = (angle + delta).coerceIn(0f, 270f)
        if (angle == 0f || angle == 270f) 0f else delta  // at the ends the page takes it
    }
    Section("Modifier.scrollable · rotate · scale") {
        Row(modifier = Modifier.fillMaxWidth().height(96.dp).scrollable(knob, Orientation.Horizontal),
            horizontalArrangement = Arrangement.spacedBy(24.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(modifier = Modifier.size(72.dp).rotate(angle - 135).background(cs.secondaryContainer, CircleShape), contentAlignment = Alignment.TopCenter) {
                Box(modifier = Modifier.padding(0.dp, 6.dp, 0.dp, 0.dp).size(6.dp, 18.dp).background(cs.primary, RoundedCornerShape(3.dp)))
            }
            Box(modifier = Modifier.size(40.dp).scale(0.5f + angle / 270f).background(cs.tertiary, RoundedCornerShape(8.dp)))
            Text("${angle.roundToInt()}°", style = typography.titleLarge)
        }
        Text("Satırı yana sürükleyin: düğme döner, kare büyür.", color = cs.onSurfaceVariant, style = typography.bodyMedium)
        // scrollableArea: a ruler that draws itself from where it is scrolled to (0..100 cm,
        // 4 dp a millimetre), clipped, stretching past its ends.
        var rulerAt by remember { mutableFloatStateOf(0f) }
        val ruler = rememberScrollableState { delta ->
            val next = (rulerAt - delta).coerceIn(0f, 100 * 40f - 300)
            val used = rulerAt - next
            rulerAt = next
            used
        }
        Canvas(modifier = Modifier.fillMaxWidth().height(56.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).scrollableArea(ruler, Orientation.Horizontal)) {
            val at = rulerAt
            var cm = (at / 40).toInt()
            while (cm <= 100 && cm * 40 - at < size.width + 40) {
                val x = 16 + cm * 40 - at
                drawLine(cs.onSurfaceVariant, Offset(x, 0f), Offset(x, 22f), strokeWidth = 2f)
                if (cm < 100)
                    for (mm in 1 until 10)
                        drawLine(cs.onSurfaceVariant, Offset(x + mm * 4, 0f), Offset(x + mm * 4, if (mm == 5) 14f else 8f), strokeWidth = 1f)
                drawText(measurer, cm.toString(), topLeft = Offset(x - 4, 28f), style = TextStyle(fontSize = 12.sp, color = cs.primary))
                cm++
            }
        }
        Text("Modifier.scrollableArea: cetvel kendini kaydırıldığı yerden çizer; kırpılır, uçlarında esner.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
    }
}

// An overscroll effect of one's own (as Compose's OverscrollEffect sample): past an end the
// content follows the finger a tenth of the way, and springs back once let go (sideways). Its
// fling is a coroutine: this class stays C++.
class OffsetOverscroll final : public OverscrollEffect {
public:
    Offset applyToScroll(Offset delta, NestedScrollSource source, const std::function<Offset(Offset)>& performScroll) override {
        // The finger takes the offset back first.
        float offset = offset_.value(), pre = 0;
        if (std::abs(offset) > 0.5f && (delta.x > 0) != (offset > 0)) {
            float next = offset + delta.x;
            pre = (next > 0) != (offset > 0) ? -offset : delta.x;
            offset_.snapTo((next > 0) != (offset > 0) ? 0.f : next);
        }
        Offset used = performScroll({delta.x - pre, delta.y});
        float left = delta.x - pre - used.x;
        if (std::abs(left) > 0.5f && source == NestedScrollSource::UserInput) offset_.snapTo(offset_.value() + left * 0.1f);
        return {pre + used.x, used.y};
    }
    Task applyToFling(Velocity velocity, std::function<TaskOf<Velocity>(Velocity)> performFling) override {
        Velocity used = co_await performFling(velocity);
        co_await offset_.animateTo(0, spring(0.5f, 400), (velocity.x - used.x) * 0.1f);
    }
    bool isInProgress() const override { return offset_.value() != 0; }
    void draw(ContentDrawScope& scope) override {
        scope.translate(offset_.value(), 0, [&](DrawScope&) { scope.drawContent(); });
    }

private:
    Animatable<float> offset_{0.f};
};
std::shared_ptr<OverscrollEffect> offsetOverscroll() { return std::make_shared<OffsetOverscroll>(); }

// A row of tiles with the overscroll effect given: the stretch, one of our own, or none (null).
@Kompozable
fun OverscrollRow(title: String, effect: OverscrollEffect?) {
    val cs = MaterialTheme.colorScheme
    Text(title, style = MaterialTheme.typography.labelLarge)
    LazyRow(modifier = Modifier.fillMaxWidth().height(48.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).clip(RoundedCornerShape(12.dp)),
            contentPadding = PaddingValues(6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp), overscrollEffect = effect) {
        items(12) { i ->
            Box(modifier = Modifier.size(48.dp, 36.dp).background(cs.secondaryContainer, RoundedCornerShape(8.dp)), contentAlignment = Alignment.Center) { Text("${i + 1}") }
        }
    }
}

// OverscrollEffect: the stretch (the default), an effect of one's own, none.
@Kompozable
fun OverscrollSection() {
    val cs = MaterialTheme.colorScheme
    val offset = remember { offsetOverscroll() }
    Section("OverscrollEffect · rememberOverscrollEffect") {
        OverscrollRow("Esneme (varsayılan)", rememberOverscrollEffect())
        OverscrollRow("Kayma (kendi efektimiz)", offset)
        OverscrollRow("Yok (null)", null)
        Text("Satırları uçlarından öteye çekin: Android 12'nin esnemesi; içeriğin parmağın onda biri kadar kaydığı kendi efektimiz; hiçbiri.",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
    }
}

// LocalBringIntoViewSpec: where a row brings its focused card — a third of the way in, as a TV
// row keeps it — or, switched off, the default (just far enough that it shows).
@Kompozable
fun BringIntoViewSection() {
    val cs = MaterialTheme.colorScheme
    var pinned by remember { mutableStateOf(true) }
    var focused by remember { mutableStateOf(-1) }
    val requesters = remember { List(20) { FocusRequester() } }
    val third = BringIntoViewSpec(calculateScrollDistance = { offset, size, container -> offset - (container - size) * 0.3f })
    Section("LocalBringIntoViewSpec · BringIntoViewSpec") {
        KompozitionLocalProvider(LocalBringIntoViewSpec provides (if (pinned) third else BringIntoViewSpec())) {
            LazyRow(modifier = Modifier.fillMaxWidth().height(72.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                items(20) { i ->
                    val current = focused == i
                    Box(modifier = Modifier
                            .size(96.dp, 64.dp)
                            .focusRequester(requesters[i])
                            .onFocusChanged { if (it.isFocused) focused = i }
                            .focusable()
                            .background(if (current) cs.primary else cs.secondaryContainer, RoundedCornerShape(12.dp)),
                        contentAlignment = Alignment.Center) { Text("Kart ${i + 1}", color = if (current) cs.onPrimary else cs.onSecondaryContainer) }
                }
            }
        }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedButton(onClick = { requesters[max(0, focused - 1)].requestFocus() }) { Text("‹ Önceki") }
            Button(onClick = { requesters[min(19, focused + 1)].requestFocus() }) { Text("Sonraki ›") }
            Switch(checked = pinned, onCheckedChange = { pinned = it })
            Text("Üçte birde", modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        }
        Text("Odak (Sonraki/Önceki, Tab ya da ok tuşları) kartı satırın üçte birine getirir; anahtar kapalıyken varsayılan: yalnızca görünecek kadar.",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
    }
}

// Nested scrolling: an inner list hands over to the page, a pager takes over from the row in
// its page, a vertical pager, and a large app bar that collapses before its list scrolls.
@Kompozable
fun NestedScrollSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val inner = rememberScrollState()
    val pager = rememberPagerState(pageCount = { 3 })
    val vertical = rememberPagerState(pageCount = { 4 })
    val appBar = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
    val twoRows = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
    val bottomBar = BottomAppBarDefaults.exitAlwaysScrollBehavior()
    val searchScroll = SearchBarDefaults.enterAlwaysSearchBarScrollBehavior()
    val mailSearch = rememberSearchBarState()
    val mailQuery = rememberTextFieldState()
    val caption: @Kompozable (String) -> Unit = { text -> Text(text, color = cs.onSurfaceVariant, style = typography.bodyMedium) }
    Section("İç içe kaydırma (nestedScroll)") {
        caption("İçteki liste sonuna gelince sayfa kaymaya devam eder:")
        Column(modifier = Modifier.fillMaxWidth().height(150.dp).background(cs.surfaceContainerHighest, RoundedCornerShape(12.dp)).verticalScroll(inner).padding(16.dp, 6.dp)) {
            for (i in 1..12) Text("İç satır $i", modifier = Modifier.padding(0.dp, 6.dp))
        }
        caption("Sayfadaki satır sona gelince pager sayfayı çevirir:")
        HorizontalPager(state = pager, modifier = Modifier.fillMaxWidth().height(72.dp), pageSpacing = 16.dp) { page ->
            LazyRow(modifier = Modifier.fillMaxSize(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                items(8) { i ->
                    Box(modifier = Modifier.size(72.dp).background(hueColor(page * 2300 + i * 140), RoundedCornerShape(16.dp)), contentAlignment = Alignment.Center) {
                        Text("${page + 1}·${i + 1}", fontWeight = FontWeight.Bold, color = Color.White)
                    }
                }
            }
        }
        caption("VerticalPager (son sayfadan sonra sayfa kayar):")
        VerticalPager(state = vertical, modifier = Modifier.fillMaxWidth().height(96.dp).clip(RoundedCornerShape(16.dp))) { page ->
            Box(modifier = Modifier.fillMaxSize().background(hueColor(page * 1500 + 200)), contentAlignment = Alignment.Center) {
                Text("Dikey sayfa ${page + 1}", fontSize = 20.sp, fontWeight = FontWeight.Bold, color = Color.White)
            }
        }
        caption("TopAppBar: kendi renkleri, 56 dp yükseklik, windowInsets yok (sayfanın ortasında):")
        TopAppBar(title = { Text("Renkli çubuk") },
                  navigationIcon = { IconButton(onClick = {}) { Icon(Icons.Filled.Menu, contentDescription = "Menü") } },
                  actions = { IconButton(onClick = {}) { Icon(Icons.Filled.Favorite, contentDescription = "Beğen") } },
                  colors = TopAppBarDefaults.topAppBarColors(containerColor = cs.primaryContainer, navigationIconContentColor = cs.onPrimaryContainer,
                                                             titleContentColor = cs.onPrimaryContainer, actionIconContentColor = cs.primary),
                  expandedHeight = 56.dp,
                  windowInsets = WindowInsets(0.dp),
                  modifier = Modifier.clip(RoundedCornerShape(16.dp)))
        // AppBarRow: actions that don't fit (here more than three slots) go into the ⋮ menu.
        caption("AppBarRow (maxItemCount = 3): beş eylemden ikisi çubukta, gerisi ⋮ menüsünde.")
        TopAppBar(title = { Text("Belge") },
                  actions = {
                      AppBarRow(maxItemCount = 3) {
                          clickableItem(onClick = {}, icon = { Icon(Icons.Filled.Share, contentDescription = "Paylaş") }, label = "Paylaş")
                          clickableItem(onClick = {}, icon = { Icon(Icons.Filled.Edit, contentDescription = "Düzenle") }, label = "Düzenle")
                          clickableItem(onClick = {}, icon = { Icon(Icons.Filled.Delete, contentDescription = "Sil") }, label = "Sil")
                          clickableItem(onClick = {}, icon = { Icon(Icons.Filled.Add, contentDescription = "Kopya oluştur") }, label = "Kopya oluştur")
                          clickableItem(onClick = {}, icon = { Icon(Icons.Filled.Info, contentDescription = "Bilgi") }, label = "Bilgi")
                      }
                  },
                  colors = TopAppBarDefaults.topAppBarColors(containerColor = cs.surfaceContainerHigh),
                  expandedHeight = 56.dp,
                  windowInsets = WindowInsets(0.dp),
                  modifier = Modifier.clip(RoundedCornerShape(16.dp)))
        caption("AppBarColumn: 150 dp yükseklikte iki eylem ve ⋮ (bir ray ya da dikey araç çubuğu için).")
        Box(modifier = Modifier.height(150.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(16.dp))) {
            AppBarColumn {
                clickableItem(onClick = {}, icon = { Icon(Icons.Filled.Home, contentDescription = "Ana sayfa") }, label = "Ana sayfa")
                clickableItem(onClick = {}, icon = { Icon(Icons.Filled.Search, contentDescription = "Ara") }, label = "Ara")
                clickableItem(onClick = {}, icon = { Icon(Icons.Filled.Settings, contentDescription = "Ayarlar") }, label = "Ayarlar")
                clickableItem(onClick = {}, icon = { Icon(Icons.Filled.Info, contentDescription = "Hakkında") }, label = "Hakkında")
            }
        }
        caption("MediumFlexibleTopAppBar: daha büyük başlık ve alt yazı, ortalanmış (titleHorizontalAlignment).")
        MediumFlexibleTopAppBar(title = { Text("Takvim") },
                                subtitle = { Text("Ekim 2026") },
                                navigationIcon = { IconButton(onClick = {}) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Geri") } },
                                actions = { IconButton(onClick = {}) { Icon(Icons.Filled.MoreVert, contentDescription = "Diğer") } },
                                colors = TopAppBarDefaults.topAppBarColors(containerColor = cs.surfaceContainerLow),
                                windowInsets = WindowInsets(0.dp),
                                titleHorizontalAlignment = Alignment.CenterHorizontally,
                                modifier = Modifier.clip(RoundedCornerShape(16.dp)))
        caption("LargeTopAppBar + exitUntilCollapsed (alt başlıklı): önce başlık küçülür, sonra liste kayar.")
        Box(modifier = Modifier.fillMaxWidth().height(380.dp).clip(RoundedCornerShape(16.dp))) {
            KompozitionLocalProvider(LocalSystemInsets provides SystemInsets()) {  // not at the window's edge
                Scaffold(modifier = Modifier.fillMaxSize().nestedScroll(appBar.nestedScrollConnection),
                         topBar = {
                             LargeTopAppBar(title = { Text("Gelen kutusu") },
                                            subtitle = { Text("3 okunmamış ileti") },
                                            navigationIcon = { IconButton(onClick = {}) { Icon(Icons.Filled.Menu, contentDescription = "Menü") } },
                                            actions = { IconButton(onClick = {}) { Icon(Icons.Filled.Search, contentDescription = "Ara") } },
                                            colors = TopAppBarDefaults.topAppBarColors(containerColor = cs.surfaceContainerLow),
                                            scrollBehavior = appBar)
                         },
                         containerColor = cs.surfaceContainerLow) { padding ->
                    LazyColumn(modifier = Modifier.fillMaxSize(), contentPadding = padding) {
                        items(30) { i ->
                            ListItem(headlineContent = { Text("İleti ${i + 1}") },
                                     supportingContent = { Text("Kaydırınca başlık küçülür") },
                                     leadingContent = { Icon(Icons.Filled.Email, contentDescription = "İleti") },
                                     colors = ListItemDefaults.colors(containerColor = Color.Transparent))
                        }
                    }
                }
            }
        }
        caption("TwoRowsTopAppBar: başlık iki satırda da kendi biçimini seçer — açıkken büyük ve iki satır, kapanınca kısa.")
        Box(modifier = Modifier.fillMaxWidth().height(380.dp).clip(RoundedCornerShape(16.dp))) {
            KompozitionLocalProvider(LocalSystemInsets provides SystemInsets()) {  // not at the window's edge
                Scaffold(modifier = Modifier.fillMaxSize().nestedScroll(twoRows.nestedScrollConnection),
                         topBar = {
                             TwoRowsTopAppBar(
                                 title = { expanded ->
                                     if (expanded) Text("Haftalık\nözet", fontWeight = FontWeight.Bold, style = typography.headlineMedium)
                                     else Text("Özet", style = typography.titleLarge)
                                 },
                                 navigationIcon = { IconButton(onClick = {}) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Geri") } },
                                 actions = { IconButton(onClick = {}) { Icon(Icons.Filled.DateRange, contentDescription = "Tarih") } },
                                 colors = TopAppBarDefaults.topAppBarColors(containerColor = cs.surfaceContainerLow),
                                 expandedHeight = 168.dp,
                                 scrollBehavior = twoRows,
                                 subtitle = { expanded ->
                                     Text(if (expanded) "6–12 Ekim · 14 etkinlik" else "6–12 Ekim",
                                          color = cs.onSurfaceVariant, style = if (expanded) typography.titleMedium else typography.labelMedium)
                                 })
                         },
                         containerColor = cs.surfaceContainerLow) { padding ->
                    LazyColumn(modifier = Modifier.fillMaxSize(), contentPadding = padding) {
                        items(30) { i ->
                            ListItem(headlineContent = { Text("Etkinlik ${i + 1}") },
                                     supportingContent = { Text("Kaydırınca başlık kısalır") },
                                     leadingContent = { Icon(Icons.Filled.Schedule, contentDescription = "Etkinlik") },
                                     colors = ListItemDefaults.colors(containerColor = Color.Transparent))
                        }
                    }
                }
            }
        }
        caption("FlexibleBottomAppBar + exitAlways: liste ilerledikçe alt çubuk aşağı kayıp gider, geri kaydırınca döner.")
        Box(modifier = Modifier.fillMaxWidth().height(300.dp).clip(RoundedCornerShape(16.dp))) {
            Scaffold(modifier = Modifier.fillMaxSize().nestedScroll(bottomBar.nestedScrollConnection),
                     bottomBar = {
                         FlexibleBottomAppBar(scrollBehavior = bottomBar, windowInsets = WindowInsets(0.dp)) {
                             IconButton(onClick = {}) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Geri") }
                             IconButton(onClick = {}) { Icon(Icons.Filled.Edit, contentDescription = "Düzenle") }
                             IconButton(onClick = {}) { Icon(Icons.Filled.Share, contentDescription = "Paylaş") }
                             IconButton(onClick = {}) { Icon(Icons.Filled.Delete, contentDescription = "Sil") }
                         }
                     },
                     containerColor = cs.surfaceContainerLow) { padding ->
                LazyColumn(modifier = Modifier.fillMaxSize(), contentPadding = padding) {
                    items(30) { i -> ListItem(headlineContent = { Text("Fotoğraf ${i + 1}") }, colors = ListItemDefaults.colors(containerColor = Color.Transparent)) }
                }
            }
        }
        caption("AppBarWithSearch + enterAlways: liste ilerledikçe arama çubuğu yukarı kayar, geri kaydırınca önce o gelir; dokununca " +
                "tam ekran arama açılır.")
        val mailField: @Kompozable () -> Unit = {
            SearchBarDefaults.InputField(searchBarState = mailSearch, textFieldState = mailQuery, onSearch = {},
                                         placeholder = { Text("Postalarda ara") }, leadingIcon = { Icon(Icons.Filled.Search, contentDescription = "Ara") })
        }
        Box(modifier = Modifier.fillMaxWidth().height(380.dp).clip(RoundedCornerShape(16.dp))) {
            KompozitionLocalProvider(LocalSystemInsets provides SystemInsets()) {  // not at the window's edge
                Scaffold(modifier = Modifier.fillMaxSize().nestedScroll(searchScroll.nestedScrollConnection),
                         topBar = {
                             AppBarWithSearch(state = mailSearch, inputField = mailField,
                                              navigationIcon = { IconButton(onClick = {}) { Icon(Icons.Filled.Menu, contentDescription = "Menü") } },
                                              actions = { IconButton(onClick = {}) { Icon(Icons.Filled.Person, contentDescription = "Hesap") } },
                                              scrollBehavior = searchScroll)
                         }) { padding ->
                    LazyColumn(modifier = Modifier.fillMaxSize(), contentPadding = padding) {
                        items(30) { i ->
                            ListItem(headlineContent = { Text("Posta ${i + 1}") },
                                     supportingContent = { Text("Kaydırınca arama çubuğu çekilir") },
                                     leadingContent = { Icon(Icons.Filled.Email, contentDescription = "Posta") },
                                     colors = ListItemDefaults.colors(containerColor = Color.Transparent))
                        }
                    }
                }
            }
        }
        ExpandedFullScreenSearchBar(state = mailSearch, inputField = mailField) {
            val typed = mailQuery.text.toString()
            for (i in 1..30) {
                val mail = "Posta $i"
                if (typed.isNotEmpty() && !mail.contains(typed)) continue
                ListItem(headlineContent = { Text(mail) }, leadingContent = { Icon(Icons.Filled.Email, contentDescription = "Posta") },
                         colors = ListItemDefaults.colors(containerColor = Color.Transparent))
            }
        }
    }
}

// Modifier.pullToRefresh on a plain Box, with the default indicator or one of our own in an IndicatorBox.
@Kompozable
fun PullToRefreshSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var refreshing by remember { mutableStateOf(false) }
    var enabled by remember { mutableStateOf(true) }
    var custom by remember { mutableStateOf(false) }
    var refreshes by remember { mutableStateOf(0) }
    val state = rememberPullToRefreshState()
    val scope = rememberCoroutineScope()
    val refresh: () -> Unit = {
        refreshing = true
        scope.launch {
            delay(2000)
            refreshes++
            refreshing = false
        }
    }
    Section("Modifier.pullToRefresh · IndicatorBox") {
        Text("Listeyi en üstteyken aşağı çekin: eşikten sonra gösterge yavaşlar, eşiği geçip bırakınca yenilenir.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(selected = enabled, onClick = { enabled = !enabled }, label = { Text("Açık (enabled)") })
            FilterChip(selected = custom, onClick = { custom = !custom }, label = { Text("Kendi göstergemiz") })
        }
        Text("$refreshes kez yenilendi · distanceFraction ${"%.2f".format(state.distanceFraction)}", color = cs.primary, style = typography.bodySmall)
        val round = refreshes
        Box(modifier = Modifier.fillMaxWidth().height(260.dp).clip(RoundedCornerShape(16.dp)).background(cs.surfaceContainerHighest)
                .pullToRefresh(isRefreshing = refreshing, state = state, onRefresh = refresh, enabled = enabled)) {
            LazyColumn(modifier = Modifier.fillMaxSize(), contentPadding = PaddingValues(16.dp, 8.dp)) {
                items(20) { i -> Text("Haber ${i + 1} (tur ${round + 1})", modifier = Modifier.padding(0.dp, 10.dp)) }
            }
            if (custom) {
                // A Material 3 Expressive loading indicator that fills up with the pull.
                val busy = refreshing
                PullToRefreshDefaults.IndicatorBox(state = state, isRefreshing = busy, modifier = Modifier.align(Alignment.TopCenter),
                                                   shape = CircleShape, containerColor = cs.primaryContainer) {
                    if (busy) LoadingIndicator(modifier = Modifier.size(36.dp), color = cs.onPrimaryContainer)
                    else LoadingIndicator(progress = { min(1f, state.distanceFraction) }, modifier = Modifier.size(36.dp), color = cs.onPrimaryContainer)
                }
            } else {
                PullToRefreshDefaults.Indicator(state = state, isRefreshing = refreshing, modifier = Modifier.align(Alignment.TopCenter))
            }
        }
    }
}
