// The list screen (a lazy list, grid or staggered grid) and the item screen.

#include "showcase.hpp"

// One row of the 10,000-row list. Skipped whenever the list recomposes and `index` is unchanged.
void ListRow(int index, NavController nav, CallSite site = CallSite::current()) {
    composable(site, index, nav, [=] {
        const ColorScheme& cs = colorScheme();
        Row(traced(Modifier()
                       .fillMaxWidth()
                       .clip(RoundedCornerShape(12))
                       .background(cs.surfaceContainer)
                       .clickable([nav, index] { nav.navigate(routes::Item{index}); })
                       .padding(12)),
            {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Box(Modifier().size(40).background(hueColor(index), 20), Alignment::Center, [&] {
                    Text(std::to_string(index % 100), {.fontWeight = FontWeight::Bold, .color = colors::White});
                });
                Column(Modifier().weight(1), [&] {
                    Text(std::format("Öğe {}", index), {.font = typography().titleMedium});
                    Text("Sadece ekrandakiler kompoze edilir",
                         {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                });
            });
    });
}

// Reads the list's first visible index, so only this small scope recomposes while scrolling.
// The window's size class (currentWindowAdaptiveInfo): what NavigationSuiteScaffold goes by.
void WindowClassLine(CallSite site) {
    composable(site, [=] {
        WindowSizeClass c = currentWindowAdaptiveInfo().windowSizeClass;
        Size window = LocalWindowSize.current();
        const char* widths[] = {"Compact", "Medium", "Expanded"};
        Text(std::format("Pencere: {} × {} ({:.0f} × {:.0f} dp)", widths[int(c.windowWidthSizeClass)], widths[int(c.windowHeightSizeClass)],
                         window.width, window.height),
             {.font = typography().bodySmall, .color = colorScheme().onSurfaceVariant});
    });
}

// What the app runs on (mediaQuery): the pointer, the keyboard, a microphone and a camera, how
// far the screen is seen from. Not recomposed as the window is resized.
void MediaLine(CallSite site) {
    composable(site, [=] {
        std::string line = mediaQuery([](const UiMediaScope& m) {
            const char* pointers[] = {"yok", "kumanda", "dokunmatik", "fare/kalem"};
            const char* keyboards[] = {"yok", "ekranda", "fiziksel"};
            const char* distances[] = {"yakın", "orta", "uzak"};
            return std::format("İşaretçi: {} · Klavye: {} · Mikrofon {} · Kamera {} · Mesafe: {}", pointers[int(m.pointerPrecision())],
                               keyboards[int(m.keyboardKind())], m.hasMicrophone() ? "var" : "yok", m.hasCamera() ? "var" : "yok",
                               distances[int(m.viewingDistance())]);
        });
        Text(line, {.font = typography().bodySmall, .color = colorScheme().onSurfaceVariant});
    });
}

void ListStatus(LazyListState state, CallSite site = CallSite::current()) {
    composable(site, state, [=] {
        int first = state.firstVisibleItemIndex();
        auto scope = rememberCoroutineScope();
        // snapshotFlow: a coroutine told of each new section the top of the list is in.
        auto sections = remember([] { return mutableStateOf(0); });
        LaunchedEffect(state, [state, sections]() -> Task {
            co_await snapshotFlow([state] { return state.firstVisibleItemIndex() / 50; }).collect([sections](int) {
                sections.set(sections.peek() + 1);
            });
        });
        Row(Modifier().fillMaxWidth().padding(16, 4), {.verticalAlignment = Alignment::CenterVertically}, [&] {
            // isScrollInProgress: while dragged, flung or animated.
            Text(std::format("İlk görünen öğe: {} · bölüm değişimi: {}{}", first, sections.get() - 1, state.isScrollInProgress() ? " · kayıyor" : ""),
                 traced(Modifier().weight(1)), {.font = typography().bodyMedium, .color = colorScheme().onSurfaceVariant});
            // Animated: from far down it jumps close first, then glides (animateScrollToItem).
            TextButton([state, scope] { scope.launch([state]() -> Task { co_await state.animateScrollToItem(0); }); }, Modifier(),
                       {.enabled = first > 0}, [] { Text("Başa dön"); });
        });
    });
}

// A square grid tile (aspectRatio keeps it square whatever the column width).
void GridTile(int index, CallSite site = CallSite::current()) {
    composable(site, index, [=] {
        Box(Modifier().fillMaxWidth().aspectRatio(1).background(hueColor(index), 16), Alignment::Center,
            [&] { Text(std::to_string(index), {.fontWeight = FontWeight::Bold, .color = colors::White}); });
    });
}

// A card of its own height for the staggered grid.
void StaggeredTile(int index, CallSite site = CallSite::current()) {
    composable(site, index, [=] {
        float height = float(72 + (index * 53) % 140);
        Box(Modifier().fillMaxWidth().height(height).background(hueColor(index), 16).padding(12), Alignment::BottomStart, [&] {
            Text(std::format("#{} · {:.0f} dp", index, height), {.fontWeight = FontWeight::Bold, .color = colors::White});
        });
    });
}

void ListScreen(Modifier modifier, LazyListState state, NavController nav, std::shared_ptr<LikesViewModel> likes,
                CallSite site) {
    composable(site, modifier, state, nav, likes, [=] {
        auto mode = remember([] { return mutableStateOf(0); });  // list, grid, staggered grid
        auto gridState = rememberLazyGridState();
        auto staggeredScroll = rememberLazyStaggeredGridState();
        // Pull down at the top to refresh (a pretend reload of a second and a half).
        auto refreshing = remember([] { return mutableStateOf(false); });
        auto refreshes = remember([] { return mutableStateOf(0); });
        auto scope = rememberCoroutineScope();
        auto refresh = [refreshing, refreshes, scope] {
            refreshing.set(true);
            scope.launch([refreshing, refreshes]() -> Task {
                co_await delay(std::chrono::milliseconds(1500));
                refreshes.set(refreshes.peek() + 1);
                refreshing.set(false);
            });
        };
        auto rows = remember([] {
            std::vector<int> v(10'000);
            for (int i = 0; i < int(v.size()); ++i) v[size_t(i)] = i;
            return v;
        });
        const ColorScheme& cs = colorScheme();
        Column(modifier, [&] {
            LazyRow(Modifier().fillMaxWidth(), {.contentPadding = {16, 4, 16, 4}, .spacing = 8},
                    [&](LazyListScope& row) {
                        row.items(40, [&](int i) {
                            Box(Modifier().background(cs.primaryContainer, 16).padding(14, 8), [&] {
                                Text(std::format("Etiket {}", i + 1),
                                     {.font = typography().labelLarge, .color = cs.onPrimaryContainer});
                            });
                        });
                    });
            SingleChoiceSegmentedButtonRow(Modifier().fillMaxWidth().padding(16, 4), [&] {
                const char* labels[] = {"Liste", "Izgara", "Kademeli"};
                for (int i = 0; i < 3; ++i) SegmentedButton(mode.get() == i, [mode, i] { mode.set(i); }, [&, i] { Text(labels[i]); });
            });
            Text(refreshes.get() ? std::format("Aşağı çekip yenileyin · {} kez yenilendi", refreshes.get())
                                 : std::string("Aşağı çekip yenileyin (PullToRefreshBox)"),
                 Modifier().padding(16, 0), {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
            std::map<int, int> liked = likes->likes.collectAsState();
            int total = 0;
            for (const auto& [item, n] : liked) total += n;
            Text(liked.empty() ? std::string("Bir satıra girip beğenin: beğeniler liste grafiğinin ViewModel'inde")
                               : std::format("{} öğede {} beğeni (liste grafiğinin ViewModel'i)", liked.size(), total),
                 Modifier().padding(16, 0), {.font = typography().bodySmall, .color = cs.primary});
            if (mode.get() == 0) {
                ListStatus(state);
                PullToRefreshBox(refreshing.get(), refresh, Modifier().fillMaxWidth().weight(1), [&] {
                    // Sections of 50 rows under sticky headers (opaque: the rows scroll under them).
                    Color headerColor = cs.background, headerText = cs.primary;
                    LazyColumn(Modifier().fillMaxSize(), {.state = state, .contentPadding = {16, 4, 16, 88}, .spacing = 6},
                               [&](LazyListScope& list) {
                                   for (int g = 0; g < int(rows.size()); g += 50) {
                                       list.stickyHeader(std::format("section{}", g), [g, headerColor, headerText] {
                                           Box(Modifier().fillMaxWidth().background(headerColor).padding(4, 10), [&] {
                                               Text(std::format("{} – {}", g, g + 49),
                                                    {.font = typography().labelLarge, .color = headerText});
                                           });
                                       });
                                       list.items(50, [g](int i) { return size_t(g + i); }, [g, nav](int i) { ListRow(g + i, nav); });
                                   }
                               });
                });
            } else if (mode.get() == 1) {
                // LazyGridState: the tiles in view (layoutInfo) and scrolls to an item.
                Row(Modifier().fillMaxWidth().padding(16, 0), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    LazyGridLayoutInfo info = gridState.layoutInfo();
                    // Item 0 is the title row: tile n is item n + 1.
                    std::string seen = info.visibleItemsInfo.empty()
                                           ? "—"
                                           : std::format("{}–{}", std::max(0, info.visibleItemsInfo.front().index - 1),
                                                         std::max(0, info.visibleItemsInfo.back().index - 1));
                    Text(std::format("Görünen kareler: {} · {} sütun", seen, info.maxSpan), Modifier().weight(1),
                         {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
                    TextButton([gridState, scope] { scope.launch([gridState]() -> Task { co_await gridState.animateScrollToItem(0); }); },
                               [] { Text("Başa"); });
                    TextButton([gridState] { gridState.scrollToItem(5001); }, [] { Text("Kare 5000"); });
                });
                // 10,000 tiles, as many columns of at least 88 dp as fit; only rows in view are composed.
                PullToRefreshBox(refreshing.get(), refresh, Modifier().fillMaxWidth().weight(1), [&] {
                    LazyVerticalGrid(GridCells::Adaptive(88), Modifier().fillMaxSize(),
                                     {.state = gridState, .contentPadding = {16, 8, 16, 88}, .verticalSpacing = 8,
                                      .horizontalSpacing = 8},
                                     [&](LazyGridScope& cells) {
                                         cells.item([&] { Text("10.000 kare", Modifier().padding(4, 0, 0, 4),
                                                               {.font = typography().titleMedium}); },
                                                    LazyGridScope::FullLine);
                                         cells.items(rows, [](int v) { return v; }, [](int v) { GridTile(v); });
                                     });
                });
            } else {
                // LazyStaggeredGridState: the cards in view and scrolls to one (item 0 is the title).
                Row(Modifier().fillMaxWidth().padding(16, 0), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    LazyStaggeredGridLayoutInfo info = staggeredScroll.layoutInfo();
                    std::string seen = info.visibleItemsInfo.empty()
                                           ? "—"
                                           : std::format("{}–{}", std::max(0, info.visibleItemsInfo.front().index - 1),
                                                         std::max(0, info.visibleItemsInfo.back().index - 1));
                    Text(std::format("Görünen kartlar: {} · {} şerit", seen, info.lanes), Modifier().weight(1),
                         {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
                    TextButton([staggeredScroll, scope] {
                        scope.launch([staggeredScroll]() -> Task { co_await staggeredScroll.animateScrollToItem(0); });
                    }, [] { Text("Başa"); });
                    TextButton([staggeredScroll] { staggeredScroll.scrollToItem(3001); }, [] { Text("Kart 3000"); });
                });
                // Cards of their own heights, each under the shorter lane (LazyVerticalStaggeredGrid).
                LazyVerticalStaggeredGrid(StaggeredGridCells::Adaptive(150), Modifier().fillMaxWidth().weight(1),
                                          {.state = staggeredScroll, .contentPadding = {16, 8, 16, 88}, .verticalItemSpacing = 8,
                                           .horizontalSpacing = 8},
                                          [&](LazyGridScope& cells) {
                                              cells.item([&] { Text("10.000 kart, kendi yükseklikleriyle", Modifier().padding(4, 0, 0, 4),
                                                                    {.font = typography().titleMedium}); },
                                                         LazyGridScope::FullLine);
                                              cells.items(rows, [](int v) { return v; }, [](int v) { StaggeredTile(v); });
                                          });
            }
        });
    });
}

// A row of the list, opened in the "list" navigation graph ("item/{n}"): its likes are in the
// graph's ViewModel, which the list shows too.
void ItemScreen(int index, NavController nav, std::shared_ptr<LikesViewModel> vm, CallSite site) {
    composable(site, index, nav, vm, [=] {
        const ColorScheme& cs = colorScheme();
        std::map<int, int> liked = vm->likes.collectAsState();
        int likes = liked.contains(index) ? liked.at(index) : 0;
        Column(Modifier().fillMaxSize().verticalScroll(rememberScrollState()).padding(20),
               {.verticalArrangement = Arrangement::spacedBy(16), .horizontalAlignment = Alignment::CenterHorizontally}, [&] {
                   Box(Modifier().size(120).background(hueColor(index), 60), Alignment::Center, [&] {
                       Text(std::to_string(index % 100), {.font = typography().displaySmall, .color = colors::White});
                   });
                   Text(std::format("Öğe {}", index), {.font = typography().headlineSmall});
                   Text("NavHost'taki \"list\" grafiğinin \"item/{n}\" rotası; n argümanı yoldan okunur. Beğeniler "
                        "grafiğin ViewModel'inde, liste ekranı da gösterir. Başka sekmeye geçip Liste'ye dönünce "
                        "grafik, bu ekran ve beğeniler yerinde durur (saveState / restoreState). kompoz://item/" +
                            std::to_string(index) + " bağlantısı da bu ekranı açar (geri: liste, sonra ana sayfa).",
                        {.font = typography().bodyMedium, .color = cs.onSurfaceVariant, .textAlign = TextAlign::Center});
                   Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically}, [&] {
                       FilledTonalButton([vm, index] { vm->like(index); }, Modifier(), {}, [&] {
                           Text(std::format("Beğen · {}", likes));
                       });
                       TextButton([nav] { nav.popBackStack(); }, [] { Text("Listeye dön"); });
                   });
               });
    });
}
