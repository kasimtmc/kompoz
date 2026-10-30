// Components screen: panes, layout info, measuring text, positions, snapping, pagers, z-index, FlexBox, Grid, layouts, carousels.

#include "showcase.hpp"

// LazyListState.layoutInfo: the items in view. Near the end of the list, 20 more are loaded
// (a pretend 700 ms fetch); derivedStateOf recomposes only when its answer changes, not on
// every scrolled frame.
// ListDetailPaneScaffold: cities and the one picked — one pane at a time on a phone held
// upright (Back goes back), side by side on an expanded window (a phone on its side, a desktop),
// where the handle between them drags the split (settling on a third, half or two thirds).
void ListDetailSection(CallSite site) {
    composable(site, [=] {
        auto navigator = rememberListDetailPaneScaffoldNavigator<int>();
        auto expansion = rememberPaneExpansionState(
            {PaneExpansionAnchor::Proportion(1 / 3.f), PaneExpansionAnchor::Proportion(0.5f), PaneExpansionAnchor::Proportion(2 / 3.f)});
        const ColorScheme& cs = colorScheme();
        static constexpr const char* cities[] = {"İstanbul", "Ankara", "İzmir", "Bursa", "Antalya", "Trabzon"};
        static constexpr const char* notes[] = {"İki kıtada bir şehir", "Başkent", "Ege'nin incisi", "Yeşil Bursa", "Turizmin merkezi",
                                                "Karadeniz'in kıyısında"};
        Section("ListDetailPaneScaffold", [&] {
            Text("Dikey telefonda liste ya da ayrıntı (geri tuşu listeye döner); yatayda ya da geniş pencerede yan yana, aradaki "
                 "tutamaçla genişlikleri sürüklenir.",
                 {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
            Box(Modifier().fillMaxWidth().height(280).background(cs.surfaceContainerHigh, 12).clip(RoundedCornerShape(12)), [&] {
                ListDetailPaneScaffold<int>(
                    navigator,
                    [&] {
                        Column(Modifier().fillMaxSize().verticalScroll(rememberScrollState()), [&] {
                            for (int i = 0; i < 6; ++i)
                                ListItem({.headline = [i] { Text(cities[i]); }, .supporting = [i] { Text(notes[i]); },
                                          .containerColor = colors::Transparent},
                                         Modifier().clickable([navigator, i] { navigator.navigateTo(ListDetailPaneScaffoldRole::Detail, i); }));
                        });
                    },
                    [&](const std::optional<int>& city) {
                        Column(Modifier().fillMaxSize().padding(16), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
                            if (!city) {
                                Text("Bir şehir seçin", {.color = cs.onSurfaceVariant});
                                return;
                            }
                            Text(cities[*city], {.font = typography().headlineSmall});
                            Text(notes[*city], {.color = cs.onSurfaceVariant});
                            TextButton([navigator] { navigator.navigateBack(); }, [] { Text("Geri"); });
                        });
                    },
                    Modifier(),
                    {.listPaneWidth = 260,
                     .paneExpansionState = expansion,
                     .paneExpansionDragHandle = [](PaneExpansionState state) {
                         auto source = remember([] { return MutableInteractionSource(); });
                         VerticalDragHandle(paneExpansionDraggable(state, source), source);
                     }});
            });
        });
    });
}

// A main pane with a supporting one: side by side on a wide window, slid in on a narrow one.
void SupportingPaneSection(CallSite site) {
    composable(site, [=] {
        auto navigator = rememberSupportingPaneScaffoldNavigator<int>();
        const ColorScheme& cs = colorScheme();
        Section("SupportingPaneScaffold", [&] {
            Text("Dikey telefonda ana bölme; \"İlgili\" destek bölmesini kaydırarak getirir (geri tuşu döner). Geniş pencerede destek bölmesi sonda.",
                 {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
            Box(Modifier().fillMaxWidth().height(240).background(cs.surfaceContainerHigh, 12).clip(RoundedCornerShape(12)), [&] {
                SupportingPaneScaffold<int>(
                    navigator,
                    [&] {
                        Column(Modifier().fillMaxSize().padding(16), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
                            Text("Kapadokya", {.font = typography().headlineSmall});
                            Text("Peri bacaları, balonlar ve yer altı şehirleri.", {.color = cs.onSurfaceVariant});
                            FilledTonalButton([navigator] { navigator.navigateTo(SupportingPaneScaffoldRole::Supporting, 1); }, [] { Text("İlgili"); });
                        });
                    },
                    [&](const std::optional<int>&) {
                        Column(Modifier().fillMaxSize().background(cs.surfaceContainerHighest).padding(16), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
                            Text("İlgili yerler", {.font = typography().titleMedium});
                            for (const char* place : {"Göreme", "Ürgüp", "Derinkuyu"}) Text(place);
                            TextButton([navigator] { navigator.navigateBack(); }, [] { Text("Geri"); });
                        });
                    },
                    Modifier(), {.supportingPaneWidth = 220});
            });
        });
    });
}

void LayoutInfoSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto list = rememberLazyListState();
        auto count = remember([] { return mutableStateOf(20); });
        auto loading = remember([] { return mutableStateOf(false); });
        auto nearEnd = remember([list] {
            return derivedStateOf([list] {
                auto info = list.layoutInfo();
                return !info.visibleItemsInfo.empty() && info.visibleItemsInfo.back().index >= info.totalItemsCount - 3;
            });
        });
        auto shown = remember([list] {
            return derivedStateOf([list] {
                auto v = list.layoutInfo().visibleItemsInfo;
                return v.empty() ? std::pair(0, 0) : std::pair(v.front().index, v.back().index);
            });
        });
        // The fetch runs in the section's scope: the answer flickering as the footer comes in
        // must not cancel it halfway (and leave it loading for good).
        auto scope = rememberCoroutineScope();
        bool wantMore = nearEnd.get();
        LaunchedEffect(wantMore, [wantMore, loading, count, scope]() -> Task {
            if (wantMore && !loading.peek() && count.peek() < 200) {
                loading.set(true);
                scope.launch([loading, count]() -> Task {
                    co_await delay(std::chrono::milliseconds(700));
                    count.set(count.peek() + 20);
                    loading.set(false);
                });
            }
            co_return;
        });
        auto [first, last] = shown.get();
        Section("LazyListState.layoutInfo · contentType", [&] {
            Text("Liste görünen öğelerini layoutInfo ile söyler; sona yaklaşınca 20 öğe daha yüklenir. derivedStateOf yalnızca "
                 "cevabı değişince yeniden kompoze ettirir.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Text(std::format("Görünen: {}–{} · toplam {}{}", first + 1, last + 1, count.get(), loading.get() ? " · yükleniyor" : ""),
                 Modifier().padding(0, 8, 0, 0), {.font = typography().titleSmall});
            LazyColumn(Modifier().fillMaxWidth().padding(0, 8, 0, 0).height(260).background(cs.surfaceContainerHigh, RoundedCornerShape(12)),
                       {.state = list, .contentPadding = {12, 8, 12, 8}, .spacing = 4}, [&](LazyListScope& l) {
                           l.items(count.get(), [](int i) { return size_t(i); }, [](int) { return size_t(1); }, [&](int i) {
                               Text(std::format("Öğe {}", i + 1), Modifier().fillMaxWidth().height(40).padding(4, 10), {.font = typography().bodyLarge});
                           });
                           if (loading.get())
                               l.item("yükleniyor", "alt bilgi", [&] {
                                   Row(Modifier().fillMaxWidth().height(48), {.horizontalArrangement = Arrangement::Center, .verticalAlignment = Alignment::CenterVertically},
                                       [&] {
                                           CircularProgressIndicator(Modifier().size(20));
                                           Text("  yükleniyor…", {.font = typography().bodyMedium});
                                       });
                               });
                       });
        });
    });
}

// rememberTextMeasurer + drawText: a bar chart whose labels are measured to sit centered over
// their bars; onTextLayout: a paragraph that offers "devamını oku" only when it was cut off.
void TextMeasureSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto measurer = rememberTextMeasurer();
        auto expanded = remember([] { return mutableStateOf(false); });
        auto cut = remember([] { return mutableStateOf(false); });
        Color bar = cs.primary, label = cs.onSurface, faint = cs.onSurfaceVariant;
        Section("rememberTextMeasurer · drawText · onTextLayout", [&] {
            Canvas(Modifier().fillMaxWidth().height(170), [measurer, bar, label, faint](DrawScope& d) {
                static const std::pair<const char*, int> months[] = {{"Oca", 42}, {"Şub", 58}, {"Mar", 35}, {"Nis", 71}, {"May", 64}, {"Haz", 88}};
                float w = d.size().width, slot = w / 6, top = 22, bottom = d.size().height - 22, h = bottom - top;
                for (int i = 0; i < 6; ++i) {
                    auto [name, value] = months[i];
                    float x = slot * float(i) + slot * 0.2f, bw = slot * 0.6f, bh = h * float(value) / 100.f;
                    d.drawRoundRect(bar, {x, bottom - bh}, Size{bw, bh}, CornerRadius(6));
                    TextLayoutResult v = measurer.measure(std::format("%{}", value), {.fontSize = 12, .fontWeight = FontWeight::Bold});
                    d.drawText(v, label, {x + (bw - v.size().width) / 2, bottom - bh - v.size().height - 2});
                    TextLayoutResult m = measurer.measure(name, {.fontSize = 12});
                    d.drawText(m, faint, {x + (bw - m.size().width) / 2, bottom + 4});
                }
            });
            Text("Kompoz, Jetpack Compose'un fikirlerini C++20'ye taşır: durum değişince yalnızca onu okuyan parçalar yeniden "
                 "kompoze edilir, düzen ölçüp yerleştirir, çizim de yalnızca değişen bölgeyi yeniler. Aynı kod masaüstünde, "
                 "Android'de, tarayıcıda ve Windows'ta çalışır; bileşenler Material 3'ü izler.",
                 Modifier().padding(0, 12, 0, 0),
                 {.font = typography().bodyMedium, .maxLines = expanded.get() ? 0 : 2, .overflow = TextOverflow::Ellipsis},
                 [cut, expanded](const TextLayoutResult& r) {
                     if (!expanded.peek()) cut.set(r.hasVisualOverflow());
                 });
            if (cut.get())
                TextButton([expanded] { expanded.set(!expanded.peek()); }, [&] { Text(expanded.get() ? "Daha az" : "Devamını oku"); });
        });
    });
}

// onGloballyPositioned: a card that knows where it is on the screen and how much of it shows,
// as the page scrolls (onPlaced hears only of its size and place in its parent).
void PositionSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto where = remember([] { return mutableStateOf(LayoutCoordinates()); });
        auto placedTimes = remember([] { return std::make_shared<int>(0); });
        auto placed = remember([] { return mutableStateOf(0); });
        Section("onGloballyPositioned · onPlaced · LayoutCoordinates", [&] {
            Column(Modifier().fillMaxWidth().background(cs.tertiaryContainer, 16).padding(16)
                       .onGloballyPositioned([where](const LayoutCoordinates& now) { where.set(now); })
                       .onPlaced([placedTimes, placed](const LayoutCoordinates&) { placed.set(++*placedTimes); }),
                   {.verticalArrangement = Arrangement::spacedBy(4)}, [&] {
                       // Read here, not around the section: as it moves, only these lines are composed again.
                       composable(CallSite::current(), [where, placed, color = cs.onTertiaryContainer] {
                           LayoutCoordinates c = where.get();
                           float shown = c.size().height > 0 ? c.boundsInRoot().height / c.size().height : 0.f;
                           Text(std::format("Ekranda: x {:.0f}, y {:.0f} dp · {:.0f} × {:.0f}", c.positionInRoot().x, c.positionInRoot().y,
                                            c.size().width, c.size().height),
                                {.font = typography().titleSmall, .color = color});
                           Text(std::format("Görünen kısmı: %{:.0f} · onPlaced {} kez", shown * 100, placed.get()),
                                {.font = typography().bodyMedium, .color = color});
                       });
                       Text("Sayfayı kaydırın: konum her karede güncellenir; onPlaced yalnızca boyutu ya da ebeveynindeki yeri değişince çağrılır.",
                            {.font = typography().bodySmall, .color = cs.onTertiaryContainer});
                   });
        });
    });
}

// rememberSnapFlingBehavior: a row of cards that settles with one in the middle, however hard
// it is flung (the one nearest to where the fling would have stopped).
void SnapRowSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto row = rememberLazyListState();
        static const Color tints[] = {Color::hex(0x6750A4), Color::hex(0x7D5260), Color::hex(0x386A20), Color::hex(0x00639B), Color::hex(0x8C4A00)};
        // onFirstVisible: a card counts as seen once all of it has shown for half a second.
        auto seen = remember([] { return mutableStateOf(0); });
        // onLayoutRectChanged: where the first card is, told once the row stops moving.
        auto firstCard = remember([] { return mutableStateOf(std::string("—")); });
        Section("LazyRow · rememberSnapFlingBehavior · onFirstVisible · onLayoutRectChanged", [&] {
            Text("Fırlatın: liste, fırlatmanın varacağı yere en yakın kartı ortaya getirerek durur.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Text(std::format("Görülen kart (tamamı yarım saniye görünen): {} / 20", seen.get()), {.font = typography().labelLarge});
            Text("Kart 1 (kaydırma durunca): " + firstCard.get(), {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            LazyRow(Modifier().fillMaxWidth().padding(0, 12, 0, 0).height(120), {.state = row, .spacing = 12, .flingBehavior = rememberSnapFlingBehavior(row)},
                    [&](LazyListScope& l) {
                        l.items(20, [seen, firstCard](int i) {
                            Modifier where = i == 0 ? Modifier().onLayoutRectChanged([firstCard](const RelativeLayoutBounds& b) {
                                firstCard.set(std::format("x = {:.0f} dp · %{:.0f} görünür", b.positionInWindow().x, b.fractionVisibleInWindow() * 100));
                            }) : Modifier();
                            Box(Modifier()
                                    .size(200, 120)
                                    .background(tints[i % 5], RoundedCornerShape(20))
                                    .onFirstVisible([seen] { seen.set(seen.peek() + 1); }, {.minDurationMs = 500})
                                    .then(where),
                                Alignment::Center,
                                [i] { Text(std::format("Kart {}", i + 1), {.font = typography().titleMedium, .color = colors::White}); });
                        });
                    });
        });
    });
}

// Carousels: a multi-browse one (large, medium and small items that grow and shrink as they
// move, settling on an item) and an uncontained one (one width, free scrolling).
void CarouselSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        static const char* const photos[] = {"images/mountains.jpg", "images/lake.webp", "images/landscape.png"};
        static const char* const names[] = {"Dağlar", "Göl", "Manzara", "Gün batımı", "Orman", "Kıyı", "Vadi", "Tepe"};
        auto browse = rememberCarouselState(0, [] { return 8; });
        auto free = rememberCarouselState(0, [] { return 12; });
        auto hero = rememberCarouselState(0, [] { return 8; });
        Section("Carousel (HorizontalMultiBrowseCarousel · HorizontalUncontainedCarousel · HorizontalCenteredHeroCarousel)", [&] {
            Text(std::format("Kaydırın: öğeler ilerledikçe büyüyüp küçülür, bırakınca bir öğeye oturur · şu an {}. öğe",
                             browse.currentItem() + 1),
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            // The name fades with the item's size (carouselItemInfo, read while drawing).
            HorizontalMultiBrowseCarousel(browse, 186, Modifier().fillMaxWidth().height(200), {.itemSpacing = 8}, [&](const CarouselItemScope& item) {
                int i = item.index;
                CarouselItemInfo info = item.carouselItemInfo;
                // maskBorder: a thin light line along the part of the photo the item shows.
                Box(Modifier().fillMaxSize().background(hueColor(i * 7)).then(item.maskBorder(BorderStroke(2, colors::White.copy(0.7f)), RoundedCornerShape(28))),
                    Alignment::BottomStart, [&] {
                    AsyncImage(photos[i % 3], names[i], Modifier().fillMaxSize(), {.contentScale = ContentScale::Crop});
                    Text(names[i], Modifier().graphicsLayer([info](GraphicsLayerScope& l) {
                        float span = info.maxSize() - info.minSize();
                        l.alpha = span > 0 ? std::clamp((info.size() - info.minSize()) / span, 0.f, 1.f) : 1.f;
                    }).padding(16, 12), {.font = typography().titleMedium, .color = colors::White});
                });
            });
            HorizontalUncontainedCarousel(free, 120, Modifier().fillMaxWidth().height(120), {.itemSpacing = 8, .itemShape = RoundedCornerShape(16)},
                                          [&](int i) {
                                              Box(Modifier().fillMaxSize().background(hueColor(i * 5)), Alignment::Center,
                                                  [&] { Text(std::to_string(i + 1), {.font = typography().headlineSmall, .color = colors::White}); });
                                          });
            // One large in the middle, a small one either side (the first and the last large at the ends).
            Text(std::format("Ortada büyük olan: {}", names[hero.currentItem()]), {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            HorizontalCenteredHeroCarousel(hero, 0, Modifier().fillMaxWidth().height(180), {.itemSpacing = 8}, [&](int i) {
                Box(Modifier().fillMaxSize().background(hueColor(i * 9)), Alignment::BottomStart, [&] {
                    AsyncImage(photos[(i + 1) % 3], names[i], Modifier().fillMaxSize(), {.contentScale = ContentScale::Crop});
                    Text(names[i], Modifier().padding(16, 12), {.font = typography().titleMedium, .color = colors::White});
                });
            });
        });
    });
}

// Three rows of tiles that scroll sideways, composed column by column.
void HorizontalGridSection(CallSite site) {
    composable(site, [=] {
        Section("LazyHorizontalGrid · LazyHorizontalStaggeredGrid", [&] {
            LazyHorizontalGrid(GridCells::Fixed(3), Modifier().fillMaxWidth().height(180), {.verticalSpacing = 6, .horizontalSpacing = 6},
                               [&](LazyGridScope& cells) {
                                   cells.items(300, [](int i) {
                                       Box(Modifier().width(i % 7 == 0 ? 120 : 72).fillMaxHeight().background(hueColor(i), 12),
                                           Alignment::Center,
                                           [&] { Text(std::to_string(i), {.fontWeight = FontWeight::Bold, .color = colors::White}); });
                                   });
                               });
            // Items of their own widths, each after the shorter row (LazyHorizontalStaggeredGrid).
            LazyHorizontalStaggeredGrid(StaggeredGridCells::Fixed(2), Modifier().fillMaxWidth().height(120),
                                        {.horizontalItemSpacing = 6, .verticalSpacing = 6}, [&](LazyGridScope& cells) {
                                            cells.items(200, [](int i) {
                                                Box(Modifier().width(float(48 + (i * 37) % 90)).fillMaxHeight().background(hueColor(i * 3), 12),
                                                    Alignment::Center,
                                                    [&] { Text(std::to_string(i), {.fontWeight = FontWeight::Bold, .color = colors::White}); });
                                            });
                                        });
        });
    });
}

void PagerSection(CallSite site) {
    composable(site, [=] {
        auto pager = rememberPagerState(0, [] { return 5; });
        auto cards = rememberPagerState(0, [] { return 10; });
        auto scope = rememberCoroutineScope();
        auto expanded = remember([] { return mutableStateOf(false); });
        const ColorScheme& cs = colorScheme();
        int current = pager.currentPage();
        Section("HorizontalPager · PageSize · animateContentSize · BoxWithConstraints", [&] {
            HorizontalPager(pager, Modifier().fillMaxWidth().height(150), [](int page) {
                Box(Modifier().fillMaxSize().background(
                        Brush::linearGradient({hueColor(page * 1700 + 300), hueColor(page * 1700 + 900)}), RoundedCornerShape(20)),
                    Alignment::Center, [&] {
                        Text(std::format("Sayfa {}", page + 1), {.fontSize = 28, .fontWeight = FontWeight::Bold, .color = colors::White});
                    });
            }, {.pageSpacing = 12});
            Row(Modifier().fillMaxWidth(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                IconButton([pager, scope, current] {
                    scope.launch([pager, current]() -> Task { co_await pager.animateScrollToPage(current - 1); });
                }, [] { Icon(Icons::AutoMirrored::Filled::ArrowBack, "Önceki"); });
                Row(Modifier().weight(1), {.horizontalArrangement = Arrangement::spacedBy(8, Arrangement::Kind::Center),
                                           .verticalAlignment = Alignment::CenterVertically}, [&] {
                    for (int i = 0; i < 5; ++i)
                        Box(Modifier().size(i == current ? 10.f : 8.f).background(i == current ? cs.primary : cs.outlineVariant, CircleShape));
                });
                IconButton([pager, scope, current] {
                    scope.launch([pager, current]() -> Task { co_await pager.animateScrollToPage(current + 1); });
                }, [] { Icon(Icons::AutoMirrored::Filled::ArrowForward, "Sonraki"); });
            });
            // PageSize::Fixed pages in the middle (SnapPosition::Center; the side padding, half the
            // room the page leaves, lets the first and last ones get there too), shrinking and fading
            // as they leave it (getOffsetDistanceInPages, read in graphicsLayer's block while
            // drawing); a fling turns up to three pages.
            Text("PageSize.Fixed · SnapPosition.Center · PagerSnapDistance.atMost(3)", {.font = typography().labelLarge, .color = cs.onSurfaceVariant});
            BoxWithConstraints(Modifier().fillMaxWidth(), [&](const Constraints& c) {
                float side = c.hasBoundedWidth() ? std::max(0.f, (c.maxWidth - 140) / 2) : 0.f;  // (unbounded before it is first measured)
                HorizontalPager(cards, Modifier().fillMaxWidth().height(120), [cards](int page) {
                    Box(Modifier()
                            .fillMaxSize()
                            .graphicsLayer([cards, page](GraphicsLayerScope& layer) {
                                float away = std::min(1.f, std::abs(cards.getOffsetDistanceInPages(page)));
                                layer.scaleX = layer.scaleY = 1 - 0.15f * away;
                                layer.alpha = 1 - 0.5f * away;
                            })
                            .background(hueColor(page * 900 + 200), RoundedCornerShape(16)),
                        Alignment::Center, [&] { Text(std::to_string(page + 1), {.fontSize = 32, .fontWeight = FontWeight::Bold, .color = colors::White}); });
                }, {.contentPadding = {.start = side, .end = side}, .pageSize = PageSize::Fixed(140), .pageSpacing = 12,
                    .snapPosition = SnapPosition::Center, .flingBehavior = PagerDefaults::flingBehavior(cards, PagerSnapDistance::atMost(3))});
            });
            Text(std::format("currentPage {} · settledPage {} · targetPage {}", cards.currentPage() + 1, cards.settledPage() + 1, cards.targetPage() + 1),
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            // key: a page put in before the current one leaves it the current page (one further on);
            // reverseLayout: the pages from the end, the finger the other way.
            auto pageNumbers = remember([] { return mutableStateOf(std::vector<int>{1, 2, 3}); });
            auto fromEnd = remember([] { return mutableStateOf(false); });
            std::vector<int> numbers = pageNumbers.get();
            auto keyed = rememberPagerState(0, [n = numbers.size()] { return int(n); });
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically}, [&] {
                OutlinedButton([pageNumbers] {
                    auto list = pageNumbers.get();
                    list.insert(list.begin(), *std::max_element(list.begin(), list.end()) + 1);
                    pageNumbers.set(list);
                }, Modifier(), {}, [] { Text("Başa sayfa ekle"); });
                Switch(fromEnd.get(), [fromEnd](bool on) { fromEnd.set(on); });
                Text("reverseLayout", {.font = typography().bodyMedium});
            });
            HorizontalPager(keyed, Modifier().fillMaxWidth().height(72), [&](int i) {
                Box(Modifier().fillMaxSize().background(hueColor(numbers[size_t(i)] * 700 + 100), RoundedCornerShape(12)), Alignment::Center,
                    [&] { Text(std::format("Sayfa {}", numbers[size_t(i)]), {.font = typography().titleMedium, .color = colors::White}); });
            }, {.pageSpacing = 8, .reverseLayout = fromEnd.get(), .key = [numbers](int i) { return size_t(numbers[size_t(i)]); }});
            Text(std::format("key ile: currentPage {} · {} sayfa", keyed.currentPage() + 1, numbers.size()),
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Column(Modifier().fillMaxWidth().background(cs.secondaryContainer, 16).clickable([expanded] { expanded.set(!expanded.get()); })
                       .animateContentSize().padding(16),
                   {.verticalArrangement = Arrangement::spacedBy(6)}, [&] {
                       Text("animateContentSize (dokunun)", {.font = typography().titleMedium, .color = cs.onSecondaryContainer});
                       Text(expanded.get() ? "Boyut değişince kart yumuşakça büyür ve küçülür. Animasyon layout sırasında kare saatine "
                                             "göre hesaplanır; yeniden kompozisyon gerekmez. İçerik animasyon boyunca kırpılır."
                                           : "Kısa metin.",
                            {.font = typography().bodyMedium, .color = cs.onSecondaryContainer});
                   });
            BoxWithConstraints(Modifier().fillMaxWidth(), [&](const Constraints& c) {
                bool wide = c.maxWidth >= 360;
                Column(Modifier().fillMaxWidth(), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
                    Text(std::format("BoxWithConstraints: {:.0f} dp, {}", c.hasBoundedWidth() ? c.maxWidth : 0.f,
                                     wide ? "yan yana" : "alt alta"),
                         {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                    auto tile = [&](const char* label, Color color) {
                        Box(Modifier().fillMaxWidth().height(48).background(color, 12).padding(12, 0), Alignment::CenterStart,
                            [&] { Text(label, {.color = colors::White}); });
                    };
                    if (wide)
                        Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                            Box(Modifier().weight(1), [&] { tile("Birinci", hueColor(100)); });
                            Box(Modifier().weight(1), [&] { tile("İkinci", hueColor(400)); });
                        });
                    else {
                        tile("Birinci", hueColor(100));
                        tile("İkinci", hueColor(400));
                    }
                });
            });
        });
    });
}

// Modifier.zIndex: overlapping cards stacked by zIndex; the one tapped goes to the top of the
// stack, the others keep their order (drawn and touched in that order).
void ZIndexSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto stack = remember([] { return mutableStateOf(std::vector<int>{0, 1, 2}); });  // bottom to top
        Section("Modifier.zIndex", [&] {
            Text("Üst üste üç kart; her birinin zIndex'i yığındaki yeri. Dokunduğun en üste çıkar, diğerleri sırasını korur; "
                 "dokunuşu da en üstteki alır.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            std::vector<int> order = stack.get();
            std::string bottomToTop;
            for (int i : order) bottomToTop += std::format("{}{}", bottomToTop.empty() ? "" : " · ", i + 1);
            Text("Alttan üste: " + bottomToTop, {.font = typography().labelLarge, .color = cs.primary});
            Box(Modifier().fillMaxWidth().height(150), [&] {
                const Color fills[] = {cs.primaryContainer, cs.secondaryContainer, cs.tertiaryContainer};
                const Color inks[] = {cs.onPrimaryContainer, cs.onSecondaryContainer, cs.onTertiaryContainer};
                for (int i = 0; i < 3; ++i) {
                    int level = int(std::find(order.begin(), order.end(), i) - order.begin());
                    bool onTop = level == 2;
                    Box(Modifier()
                            .offset(float(i) * 56, float(i) * 24)
                            .zIndex(float(level))
                            .size(180, 100)
                            .shadow(onTop ? 6.f : 1.f, 16)
                            .background(fills[i], 16)
                            .clickable([stack, i] {
                                std::vector<int> v = stack.peek();
                                std::erase(v, i);
                                v.push_back(i);  // to the top; the rest stay as they were
                                stack.set(std::move(v));
                            })
                            .padding(14),
                        [&] {
                            Text(std::format("Kart {}{}", i + 1, onTop ? " · üstte" : ""),
                                 {.font = typography().titleMedium, .color = inks[i]});
                        });
                }
            });
        });
    });
}

// FlexBox (Compose 1.12): grow and shrink, wrapping, justifyContent and alignment picked here.
void FlexBoxSection(CallSite site) {
    composable(site, [=] {
        auto justify = remember([] { return mutableStateOf(0); });
        auto wrap = remember([] { return mutableStateOf(true); });
        auto baseline = remember([] { return mutableStateOf(false); });
        const ColorScheme& cs = colorScheme();
        Section("FlexBox · FlexConfig (grow, shrink, basis, order)", [&] {
            const char* names[] = {"Start", "Center", "SpaceBetween", "SpaceEvenly"};
            const FlexJustifyContent values[] = {FlexJustifyContent::Start, FlexJustifyContent::Center, FlexJustifyContent::SpaceBetween,
                                                 FlexJustifyContent::SpaceEvenly};
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8}, [&] {
                for (int i = 0; i < 4; ++i) FilterChip(justify.get() == i, [justify, i] { justify.set(i); }, [&, i] { Text(names[i]); });
                FilterChip(wrap.get(), [wrap] { wrap.set(!wrap.get()); }, [] { Text("Wrap"); });
                FilterChip(baseline.get(), [baseline] { baseline.set(!baseline.get()); }, [] { Text("Baseline"); });
            });
            FlexBoxConfig config{.wrap = wrap.get() ? FlexWrap::Wrap : FlexWrap::NoWrap,
                                 .justifyContent = values[justify.get()],
                                 .alignItems = baseline.get() ? FlexAlignItems::Baseline : FlexAlignItems::Center,
                                 .rowGap = 8,
                                 .columnGap = 8};
            FlexBox(Modifier().fillMaxWidth().background(cs.surfaceContainerHigh, 12).clip(RoundedCornerShape(12)).padding(8), config, [&] {
                auto tag = [&](const char* text, float size, FlexConfig flex = {}) {
                    Box(Modifier().flex(flex).background(cs.secondaryContainer, 8).padding(10, 6),
                        [&] { Text(text, {.fontSize = size, .color = cs.onSecondaryContainer}); });
                };
                tag("Kotlin", 14);
                tag("C++20", 22);
                tag("Compose", 14, {.order = -1});  // first, by its order
                tag("Vulkan", 18);
                tag("Büyür (grow 1)", 14, {.grow = 1});
                tag("HarfBuzz", 12);
            });
            Text("Compose ilk sırada (order -1); \"Büyür\" satırında kalan yeri alır. Wrap kapalıyken hepsi tek satırda kalır: en uzun "
                 "kelimelerine kadar küçülür (\"Büyür\" alt alta kırılır), sığmayan taşar (CSS'teki gibi).",
                 {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
        });
    });
}

void GridSection(CallSite site) {
    composable(site, [=] {
        auto byColumn = remember([] { return mutableStateOf(false); });
        auto gaps = remember([] { return mutableStateOf(true); });
        const ColorScheme& cs = colorScheme();
        Section("Grid · gridItem (satır, sütun, yayılma, alan)", [&] {
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8}, [&] {
                FilterChip(byColumn.get(), [byColumn] { byColumn.set(!byColumn.get()); }, [] { Text("Sütun akışı"); });
                FilterChip(gaps.get(), [gaps] { gaps.set(!gaps.get()); }, [] { Text("Boşluk 8 dp"); });
            });
            using T = GridTrackSize;
            float gap = gaps.get() ? 8 : 0;
            GridConfig config{.columns = {T::Fixed(72), T::Flex(1), T::Flex(2)},
                              .rows = {T::Auto(), T::Fixed(56), T::Fixed(56)},
                              .rowGap = gap,
                              .columnGap = gap,
                              .flow = byColumn.get() ? GridFlow::Column : GridFlow::Row,
                              .areas = {{"başlık", 1, 1, 1, 3}, {"yan", 2, 1, 2, 1}}};
            Grid(Modifier().fillMaxWidth().background(cs.surfaceContainerHigh, 12).padding(8), config, [&] {
                auto tile = [&](const std::string& text, Color background, Color color, const Modifier& modifier) {
                    Box(modifier.background(background, 8).padding(8), [&] { Text(text, {.font = typography().labelLarge, .color = color}); });
                };
                tile("Başlık: alan, 3 sütun", cs.primaryContainer, cs.onPrimaryContainer, Modifier().fillMaxWidth().gridItem("başlık"));
                tile("Yan: 2 satır", cs.tertiaryContainer, cs.onTertiaryContainer, Modifier().fillMaxSize().gridItem("yan"));
                for (int i = 1; i <= 5; ++i) tile(std::to_string(i), cs.secondaryContainer, cs.onSecondaryContainer, Modifier().fillMaxSize());
                Box(Modifier().size(16).gridItem(-1, -1, 1, 1, Alignment::BottomEnd).background(cs.primary, 8));
            });
            Text("Sütunlar 72 dp, 1fr, 2fr. 1-5 boş hücrelere akar; 5. yapılandırılmamış bir satıra (sütun akışında sütuna) taşar. "
                 "Nokta son satır ve sütunda, sağ altta (-1, -1).",
                 {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
        });
    });
}

void LayoutSection(CallSite site) {
    composable(site, [=] {
        auto extra = remember([] { return mutableStateOf(0); });  // how much longer the SubcomposeLayout title is
        auto selected = remember([] { return mutableStateOf(0); });
        const ColorScheme& cs = colorScheme();
        Section("Layout · IntrinsicSize · FlowRow · updateTransition", [&] {
            // Alignment lines: texts of different sizes and a button's label on one baseline.
            auto caption = [](const char* text) { Text(text, {.font = typography().bodyMedium, .color = colorScheme().onSurfaceVariant}); };
            caption("alignByBaseline: farklı boyutlarda metinler ve düğme etiketi aynı taban çizgisinde.");
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                Text("Fiyat", Modifier().alignByBaseline(), {.font = typography().labelLarge});
                Text("₺249", Modifier().alignByBaseline(), {.font = typography().displaySmall, .color = colorScheme().primary});
                Text("/ay", Modifier().alignByBaseline(), {.font = typography().bodySmall});
                Spacer(Modifier().weight(1));
                OutlinedButton([] {}, Modifier().alignByBaseline(), {}, [] { Text("Seç"); });
            });
            auto& city = remember([] { return mutableStateOf(std::string("Ankara")); });
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(12)}, [&] {
                Text("Şehir:", Modifier().alignByBaseline(), {.font = typography().titleMedium});
                OutlinedTextField(city.get(), [&city](std::string v) { city.set(std::move(v)); }, Modifier().weight(1).alignByBaseline(),
                                  {.singleLine = true, .label = "Teslimat"});
            });
            caption("paddingFromBaseline(32, 16): metin bir ızgaraya göre (ilk taban çizgisi üstten 32, son taban çizgisi alttan 16).");
            Text("Taban çizgisinden boşluk", Modifier().fillMaxWidth().background(colorScheme().surfaceContainerHigh, 8).paddingFromBaseline(32, 16).padding(12, 0));
            // A custom Layout: children down a staircase, spread over the width.
            Layout(Modifier().fillMaxWidth(), [&] {
                for (int i = 0; i < 5; ++i)
                    Box(Modifier().size(44).background(hueColor(i * 3 + 1), 12), Alignment::Center,
                        [&] { Text(std::to_string(i + 1), {.fontWeight = FontWeight::Bold, .color = colors::White}); });
            }, [](const std::vector<Measurable>& children, const Constraints& c) {
                float w = c.hasBoundedWidth() ? c.maxWidth : 300, h = 0;
                size_t n = children.size();
                for (size_t i = 0; i < n; ++i) {
                    Placeable p = children[i].measure(c.loosen());
                    float x = n > 1 ? float(i) * (w - p.width) / float(n - 1) : 0, y = float(i) * 10;
                    p.place(x, y);
                    h = std::max(h, y + p.height);
                }
                return Size{w, h};
            });
            // SubcomposeLayout: the title measured first, then a line composed as wide as it is.
            std::string word = std::string("Kompoz") + std::string(size_t(extra.get() * 2), '!');
            TextButton([extra] { extra.set((extra.peek() + 1) % 6); }, [] { Text("Başlığı uzat"); });
            SubcomposeLayout(Modifier().fillMaxWidth(), [word, primary = cs.primary](SubcomposeMeasureScope& s, const Constraints& c) {
                float w = 0, h = 0;
                for (const Measurable& m : s.subcompose(std::string("başlık"), [word] { Text(word, {.font = typography().titleMedium}); })) {
                    Placeable p = m.measure(c.loosen());
                    p.place(0, 0);
                    w = std::max(w, p.width);
                    h = std::max(h, p.height);
                }
                for (const Measurable& m : s.subcompose(std::string("çizgi"), w, [w, primary] {
                         Column([&] {
                             Box(Modifier().width(w).height(3).background(primary, 2));
                             Text(std::format("SubcomposeLayout: başlığın genişliği {:.0f} dp", w), {.font = typography().bodySmall});
                         });
                     })) {
                    Placeable p = m.measure(c.loosen());
                    p.place(0, h + 2);
                    h += p.height + 2;
                }
                return Size{c.maxWidth, h};
            });
            // IntrinsicSize::Min: the divider is as tall as the taller text.
            Row(Modifier().fillMaxWidth().height(IntrinsicSize::Min).background(cs.surfaceContainerHigh, 12).padding(12), [&] {
                Text("Kısa metin", Modifier().weight(1), {.font = typography().bodyMedium});
                VerticalDivider(Modifier().padding(8, 0));
                Text("Uzun metin iki üç satıra iner; IntrinsicSize::Min sayesinde bölücü de onunla birlikte uzar.",
                     Modifier().weight(1), {.font = typography().bodyMedium});
            });
            // width(IntrinsicSize::Min / Max): as narrow as the widest word, or as the text on one line.
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(12)}, [&] {
                for (IntrinsicSize size : {IntrinsicSize::Min, IntrinsicSize::Max})
                    Column(Modifier().width(size).background(cs.tertiaryContainer, 12).padding(12), [&] {
                        Text(size == IntrinsicSize::Min ? "width(Min)" : "width(Max)", {.font = typography().labelMedium, .color = cs.onTertiaryContainer});
                        Text("Ortak yağmur bulutları", {.font = typography().bodyMedium, .color = cs.onTertiaryContainer});
                        Box(Modifier().fillMaxWidth().padding(0, 4).height(2).background(cs.tertiary));  // as wide as the column came out
                    });
            });
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8}, [&] {
                for (const char* tag : {"C++20", "Compose", "Vulkan", "FreeType", "coroutine", "slot table", "Android", "Linux"})
                    SuggestionChip([] {}, [tag] { Text(tag); });
            });
            // animateBounds in a LookaheadScope: the box glides to the next corner and grows or
            // shrinks on each tap; shuffled chips glide to their new places.
            auto corner = remember([] { return mutableStateOf(0); });
            auto order = remember([] { return mutableStateOf(std::vector<int>{0, 1, 2, 3, 4, 5, 6}); });
            Text("animateBounds · LookaheadScope", {.font = typography().titleSmall});
            LookaheadScope(Modifier().fillMaxWidth().height(132).background(cs.surfaceContainerHigh, 12).clickable([corner] { corner.set((corner.get() + 1) % 5); }),
                           [&] {
                               const Alignment aligns[] = {Alignment::TopStart, Alignment::TopEnd, Alignment::BottomEnd, Alignment::BottomStart, Alignment::Center};
                               int c = corner.get();
                               Box(Modifier().fillMaxSize().padding(10), aligns[c], [&] {
                                   Box(Modifier().animateBounds().size(c % 2 == 0 ? 56.f : 112.f, 56).background(cs.primary, 16), Alignment::Center,
                                       [&] { Text("dokun", {.font = typography().labelLarge, .color = cs.onPrimary}); });
                               });
                           });
            const char* words[] = {"bir", "iki", "üç", "dört", "beş", "altı", "yedi"};
            AnimationSpec glide = spring(Spring::DampingRatioLowBouncy, Spring::StiffnessLow);  // slow enough to follow
            LookaheadScope(Modifier().fillMaxWidth(), [&] {
                FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8}, [&] {
                    for (int id : order.get())
                        key(id, [&, id] { SuggestionChip([] {}, [&, id] { Text(words[id]); }, {}, Modifier().animateBounds(glide)); });
                });
            });
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                FilledTonalButton([order] {  // the far ones swap rows, the near ones swap places
                    auto v = order.peek();
                    std::reverse(v.begin(), v.end());
                    std::rotate(v.begin(), v.begin() + 2, v.end());
                    order.set(v);
                }, Modifier().weight(1), {}, [] { Text("Karıştır"); });
                OutlinedButton([order] { order.set({0, 1, 2, 3, 4, 5, 6}); }, Modifier().weight(1), {}, [] { Text("Sırala"); });
            });
            // skipToLookaheadSize: the first card's text is laid out at the width the card is
            // going to, so it doesn't reflow while the card grows or shrinks; the second's does.
            auto wideCards = remember([] { return mutableStateOf(false); });
            Text("skipToLookaheadSize · kartlara dokunun", {.font = typography().titleSmall});
            Column(Modifier().fillMaxWidth().clickable([wideCards] { wideCards.set(!wideCards.peek()); }), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
                for (bool skip : {true, false})
                    Box(Modifier().animateBounds(glide).width(wideCards.get() ? 320.f : 150.f).background(cs.secondaryContainer, 12).clipToBounds().padding(10), [&] {
                        Text(skip ? "Atlayan: metin gideceği genişlikte dizilir, kart büyürken kaymaz." : "Atlamayan: metin her karede yeniden kırılır.",
                             skip ? Modifier().skipToLookaheadSize() : Modifier(), {.font = typography().bodyMedium, .color = cs.onSecondaryContainer});
                    });
            });
            // approachLayout + DeferredTargetAnimation: a tag that grows and shrinks to its text
            // (the lookahead) by a spring, laid out along the way.
            auto longTag = remember([] { return mutableStateOf(false); });
            auto& tagSize = remember([] { return DeferredTargetAnimation<Size>(spring(Spring::DampingRatioMediumBouncy, Spring::StiffnessLow)); });
            CoroutineScope tagScope = rememberCoroutineScope();
            Text("approachLayout · etikete dokunun", {.font = typography().titleSmall});
            Box(Modifier()
                    .approachLayout(
                        [tagSize, tagScope](Size target) mutable {
                            tagSize.updateTarget(target, tagScope);
                            return !tagSize.isIdle();
                        },
                        [tagSize, tagScope](const ApproachMeasureScope& s, const MeasureNext& measure, const Constraints&, Offset&) mutable {
                            Size now = tagSize.updateTarget(s.lookaheadSize, tagScope);
                            measure(Constraints::fixed(std::max(0.f, now.width), std::max(0.f, now.height)));
                            return now;
                        })
                    .background(cs.tertiaryContainer, 16)
                    .clipToBounds()
                    .clickable([longTag] { longTag.set(!longTag.peek()); })
                    .padding(12, 8),
                [&] {
                    Text(longTag.get() ? "Uzun bir etiket: gideceği boyuta yaylanarak gider" : "Kısa", {.color = cs.onTertiaryContainer});
                });
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                for (int i = 0; i < 3; ++i) SelectableCard(i, selected);
            });
        });
    });
}
