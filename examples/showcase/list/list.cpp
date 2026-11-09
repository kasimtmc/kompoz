// The list screen (a lazy list, grid or staggered grid) and the item screen.

#include "showcase.hpp"

// One row of the 10,000-row list. Skipped whenever the list recomposes and `index` is unchanged.
@Composable
fun ListRow(index: Int, nav: NavController) {
    val cs = MaterialTheme.colorScheme
    Row(modifier = traced(Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(cs.surfaceContainer)
                              .clickable { nav.navigate(routes.Item(index)) }.padding(12.dp)),
        horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(modifier = Modifier.size(40.dp).background(hueColor(index), CircleShape), contentAlignment = Alignment.Center) {
            Text("${index % 100}", fontWeight = FontWeight.Bold, color = Color.White)
        }
        Column(modifier = Modifier.weight(1f)) {
            Text("Öğe $index", style = MaterialTheme.typography.titleMedium)
            Text("Sadece ekrandakiler kompoze edilir", color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        }
    }
}

// The window's size class (currentWindowAdaptiveInfo): what NavigationSuiteScaffold goes by.
@Composable
fun WindowClassLine() {
    val c = currentWindowAdaptiveInfo().windowSizeClass
    val window = LocalWindowSize.current
    val widths = listOf("Compact", "Medium", "Expanded")
    Text("Pencere: %s × %s (%.0f × %.0f dp)".format(widths[c.windowWidthSizeClass.ordinal], widths[c.windowHeightSizeClass.ordinal],
                                                    window.width, window.height),
         color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
}

// What the app runs on (mediaQuery): the pointer, the keyboard, a microphone and a camera, how
// far the screen is seen from. Not recomposed as the window is resized.
@Composable
fun MediaLine() {
    val line = mediaQuery {
        val pointers = listOf("yok", "kumanda", "dokunmatik", "fare/kalem")
        val keyboards = listOf("yok", "ekranda", "fiziksel")
        val distances = listOf("yakın", "orta", "uzak")
        "İşaretçi: %s · Klavye: %s · Mikrofon %s · Kamera %s · Mesafe: %s".format(
            pointers[pointerPrecision.ordinal], keyboards[keyboardKind.ordinal], if (hasMicrophone) "var" else "yok",
            if (hasCamera) "var" else "yok", distances[viewingDistance.ordinal])
    }
    Text(line, color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
}

// Reads the list's first visible index, so only this small scope recomposes while scrolling.
@Composable
fun ListStatus(state: LazyListState) {
    val first = state.firstVisibleItemIndex
    val scope = rememberCoroutineScope()
    // snapshotFlow: a coroutine told of each new section the top of the list is in.
    var sections by remember { mutableStateOf(0) }
    LaunchedEffect(state) {
        snapshotFlow { state.firstVisibleItemIndex / 50 }.collect { sections++ }
    }
    Row(modifier = Modifier.fillMaxWidth().padding(16.dp, 4.dp), verticalAlignment = Alignment.CenterVertically) {
        // isScrollInProgress: while dragged, flung or animated.
        Text("İlk görünen öğe: $first · bölüm değişimi: ${sections - 1}${if (state.isScrollInProgress) " · kayıyor" else ""}",
             modifier = traced(Modifier.weight(1f)), color = MaterialTheme.colorScheme.onSurfaceVariant,
             style = MaterialTheme.typography.bodyMedium)
        // Animated: from far down it jumps close first, then glides (animateScrollToItem).
        TextButton(onClick = { scope.launch { state.animateScrollToItem(0) } }, enabled = first > 0) { Text("Başa dön") }
    }
}

// A square grid tile (aspectRatio keeps it square whatever the column width).
@Composable
fun GridTile(index: Int) {
    Box(modifier = Modifier.fillMaxWidth().aspectRatio(1f).background(hueColor(index), RoundedCornerShape(16.dp)),
        contentAlignment = Alignment.Center) {
        Text("$index", fontWeight = FontWeight.Bold, color = Color.White)
    }
}

// A card of its own height for the staggered grid.
@Composable
fun StaggeredTile(index: Int) {
    val height = (72 + (index * 53) % 140).toFloat()
    Box(modifier = Modifier.fillMaxWidth().height(height.dp).background(hueColor(index), RoundedCornerShape(16.dp)).padding(12.dp),
        contentAlignment = Alignment.BottomStart) {
        Text("#%d · %.0f dp".format(index, height), fontWeight = FontWeight.Bold, color = Color.White)
    }
}

@Composable
fun ListScreen(modifier: Modifier, state: LazyListState, nav: NavController, likes: LikesViewModel) {
    var mode by remember { mutableStateOf(0) }  // list, grid, staggered grid
    val gridState = rememberLazyGridState()
    val staggeredScroll = rememberLazyStaggeredGridState()
    // Pull down at the top to refresh (a pretend reload of a second and a half).
    var refreshing by remember { mutableStateOf(false) }
    var refreshes by remember { mutableStateOf(0) }
    val scope = rememberCoroutineScope()
    val refresh = {
        refreshing = true
        scope.launch {
            delay(1500)
            refreshes++
            refreshing = false
        }
    }
    val rows = remember { List(10000) { it } }
    val cs = MaterialTheme.colorScheme
    Column(modifier = modifier) {
        LazyRow(modifier = Modifier.fillMaxWidth(), contentPadding = PaddingValues(16.dp, 4.dp, 16.dp, 4.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            items(40) { i ->
                Box(modifier = Modifier.background(cs.primaryContainer, RoundedCornerShape(16.dp)).padding(14.dp, 8.dp)) {
                    Text("Etiket ${i + 1}", color = cs.onPrimaryContainer, style = MaterialTheme.typography.labelLarge)
                }
            }
        }
        SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth().padding(16.dp, 4.dp)) {
            val labels = listOf("Liste", "Izgara", "Kademeli")
            for (i in 0 until 3) {
                SegmentedButton(selected = mode == i, onClick = { mode = i }, shape = SegmentedButtonDefaults.itemShape(i, 3)) { Text(labels[i]) }
            }
        }
        Text(if (refreshes > 0) "Aşağı çekip yenileyin · $refreshes kez yenilendi" else "Aşağı çekip yenileyin (PullToRefreshBox)",
             modifier = Modifier.padding(16.dp, 0.dp), color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
        val liked by likes.likes.collectAsState()
        val total = liked.values.sum()
        Text(if (liked.isEmpty()) "Bir satıra girip beğenin: beğeniler liste grafiğinin ViewModel'inde"
             else "${liked.size} öğede $total beğeni (liste grafiğinin ViewModel'i)",
             modifier = Modifier.padding(16.dp, 0.dp), color = cs.primary, style = MaterialTheme.typography.bodySmall)
        if (mode == 0) {
            ListStatus(state)
            PullToRefreshBox(isRefreshing = refreshing, onRefresh = refresh, modifier = Modifier.fillMaxWidth().weight(1f)) {
                // Sections of 50 rows under sticky headers (opaque: the rows scroll under them).
                LazyColumn(modifier = Modifier.fillMaxSize(), state = state, contentPadding = PaddingValues(16.dp, 4.dp, 16.dp, 88.dp),
                           verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    for (g in 0 until rows.size step 50) {
                        stickyHeader(key = "section$g") {
                            Box(modifier = Modifier.fillMaxWidth().background(cs.background).padding(4.dp, 10.dp)) {
                                Text("$g – ${g + 49}", color = cs.primary, style = MaterialTheme.typography.labelLarge)
                            }
                        }
                        items(50, key = { g + it }) { i -> ListRow(g + i, nav) }
                    }
                }
            }
        } else if (mode == 1) {
            // LazyGridState: the tiles in view (layoutInfo) and scrolls to an item.
            Row(modifier = Modifier.fillMaxWidth().padding(16.dp, 0.dp), verticalAlignment = Alignment.CenterVertically) {
                val info = gridState.layoutInfo
                // Item 0 is the title row: tile n is item n + 1.
                val seen = if (info.visibleItemsInfo.isEmpty()) "—"
                           else "%d–%d".format(maxOf(0, info.visibleItemsInfo.first().index - 1), maxOf(0, info.visibleItemsInfo.last().index - 1))
                Text("Görünen kareler: $seen · ${info.maxSpan} sütun", modifier = Modifier.weight(1f),
                     color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
                TextButton(onClick = { scope.launch { gridState.animateScrollToItem(0) } }) { Text("Başa") }
                TextButton(onClick = { scope.launch { gridState.scrollToItem(5001) } }) { Text("Kare 5000") }
            }
            // 10,000 tiles, as many columns of at least 88 dp as fit; only rows in view are composed.
            PullToRefreshBox(isRefreshing = refreshing, onRefresh = refresh, modifier = Modifier.fillMaxWidth().weight(1f)) {
                LazyVerticalGrid(columns = GridCells.Adaptive(88.dp), modifier = Modifier.fillMaxSize(), state = gridState,
                                 contentPadding = PaddingValues(16.dp, 8.dp, 16.dp, 88.dp), verticalArrangement = Arrangement.spacedBy(8.dp),
                                 horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    item(span = { GridItemSpan(maxLineSpan) }) {
                        Text("10.000 kare", modifier = Modifier.padding(4.dp, 0.dp, 0.dp, 4.dp), style = MaterialTheme.typography.titleMedium)
                    }
                    items(rows, key = { it }) { GridTile(it) }
                }
            }
        } else {
            // LazyStaggeredGridState: the cards in view and scrolls to one (item 0 is the title).
            Row(modifier = Modifier.fillMaxWidth().padding(16.dp, 0.dp), verticalAlignment = Alignment.CenterVertically) {
                val info = staggeredScroll.layoutInfo
                val seen = if (info.visibleItemsInfo.isEmpty()) "—"
                           else "%d–%d".format(maxOf(0, info.visibleItemsInfo.first().index - 1), maxOf(0, info.visibleItemsInfo.last().index - 1))
                Text("Görünen kartlar: $seen · ${info.lanes} şerit", modifier = Modifier.weight(1f),
                     color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
                TextButton(onClick = { scope.launch { staggeredScroll.animateScrollToItem(0) } }) { Text("Başa") }
                TextButton(onClick = { scope.launch { staggeredScroll.scrollToItem(3001) } }) { Text("Kart 3000") }
            }
            // Cards of their own heights, each under the shorter lane (LazyVerticalStaggeredGrid).
            LazyVerticalStaggeredGrid(columns = StaggeredGridCells.Adaptive(150.dp), modifier = Modifier.fillMaxWidth().weight(1f),
                                      state = staggeredScroll, contentPadding = PaddingValues(16.dp, 8.dp, 16.dp, 88.dp),
                                      verticalItemSpacing = 8.dp, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                item(span = StaggeredGridItemSpan.FullLine) {
                    Text("10.000 kart, kendi yükseklikleriyle", modifier = Modifier.padding(4.dp, 0.dp, 0.dp, 4.dp),
                         style = MaterialTheme.typography.titleMedium)
                }
                items(rows, key = { it }) { StaggeredTile(it) }
            }
        }
    }
}

// A row of the list, opened in the "list" navigation graph ("item/{n}"): its likes are in the
// graph's ViewModel, which the list shows too.
@Composable
fun ItemScreen(index: Int, nav: NavController, vm: LikesViewModel) {
    val cs = MaterialTheme.colorScheme
    val liked by vm.likes.collectAsState()
    val likes = liked[index] ?: 0
    Column(modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
           verticalArrangement = Arrangement.spacedBy(16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Box(modifier = Modifier.size(120.dp).background(hueColor(index), CircleShape), contentAlignment = Alignment.Center) {
            Text("${index % 100}", color = Color.White, style = MaterialTheme.typography.displaySmall)
        }
        Text("Öğe $index", style = MaterialTheme.typography.headlineSmall)
        Text("NavHost'taki \"list\" grafiğinin \"item/{n}\" rotası; n argümanı yoldan okunur. Beğeniler " +
             "grafiğin ViewModel'inde, liste ekranı da gösterir. Başka sekmeye geçip Liste'ye dönünce " +
             "grafik, bu ekran ve beğeniler yerinde durur (saveState / restoreState). kompoz://item/$index " +
             "bağlantısı da bu ekranı açar (geri: liste, sonra ana sayfa).",
             color = cs.onSurfaceVariant, textAlign = TextAlign.Center, style = MaterialTheme.typography.bodyMedium)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            FilledTonalButton(onClick = { vm.like(index) }) { Text("Beğen · $likes") }
            TextButton(onClick = { nav.popBackStack() }) { Text("Listeye dön") }
        }
    }
}
