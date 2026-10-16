// Components screen: flings, scrollbars, predictive back, pointer passes, scrollables, overscroll, bring-into-view, nested scroll, pull to refresh, drawers.

#include "showcase.hpp"

// Flings: a VelocityTracker measures how fast the finger let go; one puck slides on from that
// speed and slows down (animateDecay, stopping at the track's ends: updateBounds), the other
// springs back to the middle carrying it (animateTo with an initial velocity).
void FlingSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto slide = remember([] { return Animatable<float>(0.f); });
        auto bounce = remember([] { return Animatable<float>(0.f); });
        auto released = remember([] { return mutableStateOf(0.f); });
        auto scope = rememberCoroutineScope();
        auto track = [&](Animatable<float> a, bool decays) {
            Box(Modifier().fillMaxWidth().padding(0, 12, 0, 0).height(56).background(cs.surfaceContainerHigh, 28).onSizeChanged([a, decays](Size s) {
                    if (decays) a.updateBounds(0.f, std::max(0.f, s.width - 56));
                }),
                decays ? Alignment::CenterStart : Alignment::Center, [&] {
                    Box(Modifier().offset([a, decays] { return Offset{a.value() + (decays ? 4.f : 0.f), 0}; }).size(48)
                            .background(decays ? cs.primary : cs.tertiary, 24)
                            .pointerInput(0, [a, decays, scope, released](PointerInputScope& s) -> Task {
                                VelocityTracker tracker;
                                float dragged = 0;  // the finger's way so far (the puck moves under it)
                                co_await detectDragGestures(
                                    s, {.onDragStart = [&, a](Offset) {
                                            a.stop();
                                            tracker.resetTracking();
                                        },
                                        .onDrag = [&, a](PointerInputChange& change, Offset amount) {
                                            change.consume();
                                            dragged += amount.x;
                                            tracker.addPosition(change.uptimeMillis, {dragged, 0});
                                            a.snapTo(a.peek() + amount.x);
                                        },
                                        .onDragEnd = [&, a, decays, scope, released] {
                                            float v = tracker.calculateVelocity().x;
                                            released.set(v);
                                            scope.launch([a, v, decays]() -> Task {
                                                if (decays) co_await a.animateDecay(v, splineBasedDecay());
                                                else co_await a.animateTo(0.f, spring(Spring::DampingRatioMediumBouncy, Spring::StiffnessLow), v);
                                            });
                                        }});
                            }));
                });
        };
        Section("animateDecay · VelocityTracker · spring(initialVelocity)", [&] {
            Text("Mor topu fırlatın: bırakış hızıyla kayar, yavaşlar, kenarda durur. Diğerini çekip bırakın: hızını taşıyarak "
                 "yaylanır ve ortaya döner.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            track(slide, true);
            track(bounce, false);
            Text(std::format("Bırakış hızı: {:.0f} dp/s", released.get()), Modifier().padding(0, 8, 0, 0), {.font = typography().bodyMedium});
        });
    });
}

// The standard drawers in small frames: one always shown, one that pushes the page aside.
void DrawersSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto page = remember([] { return mutableStateOf(0); });
        auto drawer = rememberDrawerState();
        auto coroutines = rememberCoroutineScope();
        const char* pages[] = {"Gelen", "Gönderilen", "Taslaklar"};
        auto items = [&](bool closeAfter) {
            for (int i = 0; i < 3; ++i)
                NavigationDrawerItem([&, i] { Text(pages[i]); }, page.get() == i, [page, i, drawer, coroutines, closeAfter] {
                    page.set(i);
                    if (closeAfter) coroutines.launch([drawer]() -> Task { co_await drawer.close(); });
                });
        };
        auto frame = Modifier().fillMaxWidth().height(200).border(1, cs.outlineVariant, 12).clip(RoundedCornerShape(12));
        Section("PermanentNavigationDrawer · DismissibleNavigationDrawer", [&] {
            PermanentNavigationDrawer([&] { PermanentDrawerSheet(Modifier().width(170), [&] { items(false); }); }, frame, [&] {
                Box(Modifier().fillMaxSize().background(cs.surfaceContainer), Alignment::Center, [&] { Text(pages[page.get()]); });
            });
            Box(frame, [&] {
                DismissibleNavigationDrawer([&] { DismissibleDrawerSheet(Modifier().width(170), [&] { items(true); }); }, drawer, [&] {
                    Column(Modifier().fillMaxSize().background(cs.surfaceContainer), [&] {
                        Row(Modifier().fillMaxWidth(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                            IconButton([drawer, coroutines] {
                                coroutines.launch([drawer]() -> Task { co_await (drawer.isOpen() ? drawer.close() : drawer.open()); });
                            }, [] { Icon(Icons::Filled::Menu, "Menü"); });
                            Text(pages[page.get()], {.font = typography().titleMedium});
                        });
                        Text("Menüye dokunun ya da sağa sürükleyin: çekmece sayfayı iter.", Modifier().padding(16, 8),
                             {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                    });
                });
            });
        });
    });
}

// Desktop scrollbars beside a scrolled column, a long lazy list and a row: drag a thumb, click a
// track to page, hover to darken.
void ScrollbarSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto column = rememberScrollState();
        auto row = rememberScrollState();
        auto list = rememberLazyListState();
        Section("VerticalScrollbar · HorizontalScrollbar", [&] {
            Text("Başparmağı sürükleyin, raya tıklayınca bir sayfa kayar; fare üzerindeyken koyulaşır. Liste 1000 satır: "
                 "boyu görünen satırlardan tahmin edilir.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            // BringIntoViewRequester: a paragraph and a box scrolled into view on the scrollables' spring.
            auto& paragraph = remember([] { return BringIntoViewRequester(); });
            auto& box = remember([] { return BringIntoViewRequester(); });
            CoroutineScope scope = rememberCoroutineScope();
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                OutlinedButton([scope, paragraph] { scope.launch([paragraph]() -> Task { co_await paragraph.bringIntoView(); }); },
                               [] { Text("Paragraf 25'i göster"); });
                OutlinedButton([scope, box] { scope.launch([box]() -> Task { co_await box.bringIntoView(); }); }, [] { Text("18'i göster"); });
            });
            Row(Modifier().fillMaxWidth().height(200), {.horizontalArrangement = Arrangement::spacedBy(12)}, [&] {
                Box(Modifier().weight(1).fillMaxHeight().background(cs.surfaceContainerHigh, 12).clip(RoundedCornerShape(12)), [&] {
                    Column(Modifier().fillMaxSize().verticalScroll(column).padding(12, 8), [&] {
                        for (int i = 1; i <= 30; ++i)
                            Text(std::format("Paragraf {}", i),
                                 (i == 25 ? Modifier().bringIntoViewRequester(paragraph) : Modifier()).padding(0, 4));
                    });
                    VerticalScrollbar(rememberScrollbarAdapter(column), Modifier().align(Alignment::CenterEnd).fillMaxHeight().padding(0, 4, 2, 4));
                });
                Box(Modifier().weight(1).fillMaxHeight().background(cs.surfaceContainerHigh, 12).clip(RoundedCornerShape(12)), [&] {
                    LazyColumn(Modifier().fillMaxSize(), {.state = list}, [](LazyListScope& l) {
                        l.items(1000, [](int i) { Text(std::format("Satır {}", i + 1), Modifier().padding(12, 6)); });
                    });
                    VerticalScrollbar(rememberScrollbarAdapter(list), Modifier().align(Alignment::CenterEnd).fillMaxHeight().padding(0, 4, 2, 4));
                });
            });
            Box(Modifier().fillMaxWidth().height(64).background(cs.surfaceContainerHigh, 12).clip(RoundedCornerShape(12)), [&] {
                Row(Modifier().fillMaxSize().horizontalScroll(row).padding(8, 8, 8, 14), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                    for (int i = 1; i <= 24; ++i)
                        Box((i == 18 ? Modifier().bringIntoViewRequester(box) : Modifier()).size(72, 40).background(hueColor(i * 900), 8),
                            Alignment::Center, [i] { Text(std::to_string(i), {.color = colors::White}); });
                });
                HorizontalScrollbar(rememberScrollbarAdapter(row), Modifier().align(Alignment::BottomCenter).fillMaxWidth().padding(4, 0, 4, 2));
            });
        });
    });
}

// PredictiveBackHandler: while the panel is open, Android's back gesture (14+) shrinks it and
// moves it away from the edge the finger came from; done, it closes; called off, it comes back.
// Escape (the back button before Android 13) closes it at once.
void PredictiveBackSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto open = remember([] { return mutableStateOf(false); });
        auto progress = remember([] { return mutableStateOf(0.f); });
        auto fromRight = remember([] { return mutableStateOf(false); });
        auto last = remember([] { return mutableStateOf(std::string("—")); });
        PredictiveBackHandler(open.get(), [open, progress, fromRight, last](Flow<BackEventCompat> steps) -> Task {
            struct Settle {  // also when the gesture is called off (the coroutine is cancelled)
                MutableState<float> progress;
                ~Settle() { progress.set(0); }
            } settle{progress};
            last.set("hareket sürüyor");
            co_await steps.collect([progress, fromRight](const BackEventCompat& e) {
                progress.set(e.progress);
                fromRight.set(e.swipeEdge == BackEventCompat::SwipeEdge::Right);
            });
            last.set("kapandı");
            open.set(false);
        });
        Section("PredictiveBackHandler", [&] {
            Text("Paneli açın, sonra ekranın kenarından geri hareketini yapın: panel parmağı izler, bırakınca kapanır, "
                 "vazgeçince yerine döner (Android 14+). Panel kapalıyken geri hareketi uygulamadan çıkar.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Button([open, last] { open.set(true), last.set("açık"); }, Modifier(), {.enabled = !open.get()}, [] { Text("Paneli aç"); });
                Text(open.get() && progress.get() > 0 ? std::format("geri: %{:.0f} · {}", progress.get() * 100, fromRight.get() ? "sağdan" : "soldan")
                                                      : last.get(),
                     {.font = typography().bodyMedium});
            });
            AnimatedVisibility(open.get(), Modifier().fillMaxWidth(), fadeIn() + expandVertically(), fadeOut() + shrinkVertically(), [&] {
                Box(Modifier()
                        .fillMaxWidth()
                        .height(140)
                        .graphicsLayer([progress, fromRight](GraphicsLayerScope& layer) {  // read while drawing
                            float p = progress.get();
                            layer.scaleX = layer.scaleY = 1 - 0.1f * p;
                            layer.translationX = (fromRight.get() ? -1.f : 1.f) * 24 * p;
                        })
                        .background(cs.primaryContainer, RoundedCornerShape(24))
                        .padding(20),
                    Alignment::CenterStart, [&] {
                        Column(Modifier(), {.verticalArrangement = Arrangement::spacedBy(6)}, [&] {
                            Icon(Icons::AutoMirrored::Filled::ArrowBack, "", Modifier(), cs.onPrimaryContainer);
                            Text("Geri hareketiyle kapatın", {.font = typography().titleMedium, .color = cs.onPrimaryContainer});
                        });
                    });
            });
        });
    });
}

// PointerEventPass: a parent that sees each press before what is inside it (Initial) and can
// take it, and after (Final) to tell whether something inside took it.
void PointerPassSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto locked = remember([] { return mutableStateOf(false); });
        auto clicks = remember([] { return mutableStateOf(0); });
        auto seen = remember([] { return mutableStateOf(std::string("—")); });
        Section("PointerEventPass (Initial · Main · Final)", [&] {
            Text("Kilitliyken dış kutu basışı Initial geçişinde, içtekilerden önce tüketir: düğme tepki vermez. "
                 "Final geçişinde basışı bir şeyin alıp almadığını görür.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                Switch(locked.get(), [locked](bool v) { locked.set(v); });
                Text("Kilitli", Modifier().padding(12, 0, 0, 0));
            });
            Box(Modifier().fillMaxWidth().height(96).background(cs.surfaceContainerHigh, 12)
                    .pointerInput(0, [locked, seen](PointerInputScope& scope) -> Task {
                        while (true) {
                            PointerEvent e = co_await scope.awaitPointerEvent(PointerEventPass::Initial);
                            if (locked.peek() && e.type == PointerEventType::Press) e.change.consume();
                            e = co_await scope.awaitPointerEvent(PointerEventPass::Final);
                            if (e.type == PointerEventType::Press)
                                seen.set(e.change.isConsumed() ? "basış alındı (düğme ya da kilit)" : "basışı kimse almadı");
                        }
                    }),
                Alignment::Center, [&] {
                    Button([clicks] { clicks.set(clicks.peek() + 1); }, [&] { Text(std::format("Bas ({})", clicks.get())); });
                });
            Text("Final: " + seen.get(), {.font = typography().bodySmall, .color = cs.primary});
        });
    });
}

void ScrollableSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto angle = remember([] { return mutableFloatStateOf(0); });
        auto knob = rememberScrollableState([angle](float delta) {
            angle.set(std::clamp(angle.peek() + delta, 0.f, 270.f));
            return angle.peek() == 0.f || angle.peek() == 270.f ? 0.f : delta;  // at the ends the page takes it
        });
        Section("Modifier.scrollable · rotate · scale", [&] {
            Row(Modifier().fillMaxWidth().height(96).scrollable(knob, Orientation::Horizontal),
                {.horizontalArrangement = Arrangement::spacedBy(24), .verticalAlignment = Alignment::CenterVertically}, [&] {
                    Box(Modifier().size(72).rotate(angle.get() - 135).background(cs.secondaryContainer, CircleShape), Alignment::TopCenter,
                        [&] { Box(Modifier().padding(0, 6, 0, 0).size(6, 18).background(cs.primary, 3)); });
                    Box(Modifier().size(40).scale(0.5f + angle.get() / 270.f).background(cs.tertiary, 8));
                    Text(std::format("{:.0f}°", angle.get()), {.font = typography().titleLarge});
                });
            Text("Satırı yana sürükleyin: düğme döner, kare büyür.", {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            // scrollableArea: a ruler that draws itself from where it is scrolled to (0..100 cm,
            // 4 dp a millimetre), clipped, stretching past its ends.
            auto rulerAt = remember([] { return mutableFloatStateOf(0); });
            auto ruler = rememberScrollableState([rulerAt](float delta) {
                float next = std::clamp(rulerAt.peek() - delta, 0.f, 100 * 40.f - 300), used = rulerAt.peek() - next;
                rulerAt.set(next);
                return used;
            });
            Canvas(Modifier().fillMaxWidth().height(56).background(cs.surfaceContainerHigh, 12).scrollableArea(ruler, Orientation::Horizontal),
                   [at = rulerAt.get(), tick = cs.onSurfaceVariant, label = cs.primary](DrawScope& d) {
                       for (int cm = int(at / 40); cm <= 100 && float(cm) * 40 - at < d.size().width + 40; ++cm) {
                           float x = 16 + float(cm) * 40 - at;
                           d.drawLine(tick, {x, 0}, {x, 22}, 2);
                           for (int mm = 1; mm < 10 && cm < 100; ++mm)
                               d.drawLine(tick, {x + float(mm) * 4, 0}, {x + float(mm) * 4, mm == 5 ? 14.f : 8.f}, 1);
                           d.drawText(std::to_string(cm), {x - 4, 28}, Font{.size = 12}, label);
                       }
                   });
            Text("Modifier.scrollableArea: cetvel kendini kaydırıldığı yerden çizer; kırpılır, uçlarında esner.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
        });
    });
}

// An overscroll effect of one's own (as Compose's OverscrollEffect sample): past an end the
// content follows the finger a tenth of the way, and springs back once let go (sideways).
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

// OverscrollEffect: the stretch (the default), an effect of one's own, none.
void OverscrollSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto& offset = remember([] { return std::shared_ptr<OverscrollEffect>(std::make_shared<OffsetOverscroll>()); });
        Section("OverscrollEffect · rememberOverscrollEffect", [&] {
            auto row = [&](const char* title, std::optional<std::shared_ptr<OverscrollEffect>> effect) {
                Text(title, {.font = typography().labelLarge});
                LazyRow(Modifier().fillMaxWidth().height(48).background(cs.surfaceContainerHigh, 12).clip(RoundedCornerShape(12)),
                        {.contentPadding = {6, 6, 6, 6}, .spacing = 6, .overscrollEffect = effect}, [tile = cs.secondaryContainer](LazyListScope& l) {
                            l.items(12, [tile](int i) {
                                Box(Modifier().size(48, 36).background(tile, 8), Alignment::Center, [i] { Text(std::to_string(i + 1)); });
                            });
                        });
            };
            row("Esneme (varsayılan)", std::nullopt);
            row("Kayma (kendi efektimiz)", offset);
            row("Yok (nullptr)", nullptr);
            Text("Satırları uçlarından öteye çekin: Android 12'nin esnemesi; içeriğin parmağın onda biri kadar kaydığı kendi efektimiz; hiçbiri.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
        });
    });
}

// LocalBringIntoViewSpec: where a row brings its focused card — a third of the way in, as a TV
// row keeps it — or, switched off, the default (just far enough that it shows).
void BringIntoViewSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto pinned = remember([] { return mutableStateOf(true); });
        auto focused = remember([] { return mutableStateOf(-1); });
        auto& requesters = remember([] { return std::vector<FocusRequester>(20); });
        BringIntoViewSpec third{[](float offset, float size, float container) { return offset - (container - size) * 0.3f; }};
        Section("LocalBringIntoViewSpec · BringIntoViewSpec", [&] {
            CompositionLocalProvider({LocalBringIntoViewSpec.provides(pinned.get() ? third : BringIntoViewSpec{})}, [&] {
                LazyRow(Modifier().fillMaxWidth().height(72), {.spacing = 8},
                        [reqs = requesters, focused, on = cs.primary, onText = cs.onPrimary, off = cs.secondaryContainer,
                         offText = cs.onSecondaryContainer](LazyListScope& l) {
                            l.items(20, [=](int i) {
                                bool current = focused.get() == i;
                                Box(Modifier()
                                        .size(96, 64)
                                        .focusRequester(reqs[size_t(i)])
                                        .onFocusChanged([focused, i](FocusState s) {
                                            if (s.isFocused) focused.set(i);
                                        })
                                        .focusable()
                                        .background(current ? on : off, 12),
                                    Alignment::Center, [=] { Text(std::format("Kart {}", i + 1), {.color = current ? onText : offText}); });
                            });
                        });
            });
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically}, [&] {
                OutlinedButton([reqs = requesters, focused] { reqs[size_t(std::max(0, focused.peek() - 1))].requestFocus(); }, [] { Text("‹ Önceki"); });
                Button([reqs = requesters, focused] { reqs[size_t(std::min(19, focused.peek() + 1))].requestFocus(); }, [] { Text("Sonraki ›"); });
                Switch(pinned.get(), [pinned](bool v) { pinned.set(v); });
                Text("Üçte birde", Modifier().weight(1), {.font = typography().bodyMedium});
            });
            Text("Odak (Sonraki/Önceki, Tab ya da ok tuşları) kartı satırın üçte birine getirir; anahtar kapalıyken varsayılan: yalnızca görünecek kadar.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
        });
    });
}

// Nested scrolling: an inner list hands over to the page, a pager takes over from the row in
// its page, a vertical pager, and a large app bar that collapses before its list scrolls.
void NestedScrollSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto inner = rememberScrollState();
        auto pager = rememberPagerState(0, [] { return 3; });
        auto vertical = rememberPagerState(0, [] { return 4; });
        auto appBar = TopAppBarDefaults::exitUntilCollapsedScrollBehavior();
        auto twoRows = TopAppBarDefaults::exitUntilCollapsedScrollBehavior();
        auto bottomBar = BottomAppBarDefaults::exitAlwaysScrollBehavior();
        auto searchScroll = SearchBarDefaults::enterAlwaysSearchBarScrollBehavior();
        auto mailSearch = rememberSearchBarState();
        auto mailQuery = rememberTextFieldState();
        auto caption = [&](const char* text) { Text(text, {.font = typography().bodyMedium, .color = cs.onSurfaceVariant}); };
        Section("İç içe kaydırma (nestedScroll)", [&] {
            caption("İçteki liste sonuna gelince sayfa kaymaya devam eder:");
            Column(Modifier().fillMaxWidth().height(150).background(cs.surfaceContainerHighest, 12).verticalScroll(inner).padding(16, 6),
                   [&] {
                       for (int i = 1; i <= 12; ++i) Text(std::format("İç satır {}", i), Modifier().padding(0, 6));
                   });
            caption("Sayfadaki satır sona gelince pager sayfayı çevirir:");
            HorizontalPager(pager, Modifier().fillMaxWidth().height(72), [](int page) {
                LazyRow(Modifier().fillMaxSize(), {.spacing = 8}, [page](LazyListScope& row) {
                    row.items(8, [page](int i) {
                        Box(Modifier().size(72).background(hueColor(page * 2300 + i * 140), 16), Alignment::Center, [&] {
                            Text(std::format("{}·{}", page + 1, i + 1), {.fontWeight = FontWeight::Bold, .color = colors::White});
                        });
                    });
                });
            }, {.pageSpacing = 16});
            caption("VerticalPager (son sayfadan sonra sayfa kayar):");
            VerticalPager(vertical, Modifier().fillMaxWidth().height(96).clip(RoundedCornerShape(16)), [](int page) {
                Box(Modifier().fillMaxSize().background(hueColor(page * 1500 + 200)), Alignment::Center, [&] {
                    Text(std::format("Dikey sayfa {}", page + 1), {.fontSize = 20, .fontWeight = FontWeight::Bold, .color = colors::White});
                });
            });
            caption("TopAppBar: kendi renkleri, 56 dp yükseklik, windowInsets yok (sayfanın ortasında):");
            TopAppBar({.title = [] { Text("Renkli çubuk"); },
                       .navigationIcon = [] { IconButton([] {}, [] { Icon(Icons::Filled::Menu, "Menü"); }); },
                       .actions = [] { IconButton([] {}, [] { Icon(Icons::Filled::Favorite, "Beğen"); }); },
                       .containerColor = cs.primaryContainer,
                       .navigationIconContentColor = cs.onPrimaryContainer,
                       .titleContentColor = cs.onPrimaryContainer,
                       .actionIconContentColor = cs.primary,
                       .expandedHeight = 56,
                       .windowInsets = WindowInsets{}},
                      Modifier().clip(RoundedCornerShape(16)));
            // AppBarRow: actions that don't fit (here more than three slots) go into the ⋮ menu.
            caption("AppBarRow (maxItemCount = 3): beş eylemden ikisi çubukta, gerisi ⋮ menüsünde.");
            TopAppBar({.title = [] { Text("Belge"); },
                       .actions = [] {
                           AppBarRow([](AppBarScope& s) {
                               s.clickableItem([] {}, [] { Icon(Icons::Filled::Share, "Paylaş"); }, "Paylaş");
                               s.clickableItem([] {}, [] { Icon(Icons::Filled::Edit, "Düzenle"); }, "Düzenle");
                               s.clickableItem([] {}, [] { Icon(Icons::Filled::Delete, "Sil"); }, "Sil");
                               s.clickableItem([] {}, [] { Icon(Icons::Filled::Add, "Kopya oluştur"); }, "Kopya oluştur");
                               s.clickableItem([] {}, [] { Icon(Icons::Filled::Info, "Bilgi"); }, "Bilgi");
                           }, Modifier(), {.maxItemCount = 3});
                       },
                       .containerColor = cs.surfaceContainerHigh,
                       .expandedHeight = 56,
                       .windowInsets = WindowInsets{}},
                      Modifier().clip(RoundedCornerShape(16)));
            caption("AppBarColumn: 150 dp yükseklikte iki eylem ve ⋮ (bir ray ya da dikey araç çubuğu için).");
            Box(Modifier().height(150).background(cs.surfaceContainerHigh, 16), [] {
                AppBarColumn([](AppBarScope& s) {
                    s.clickableItem([] {}, [] { Icon(Icons::Filled::Home, "Ana sayfa"); }, "Ana sayfa");
                    s.clickableItem([] {}, [] { Icon(Icons::Filled::Search, "Ara"); }, "Ara");
                    s.clickableItem([] {}, [] { Icon(Icons::Filled::Settings, "Ayarlar"); }, "Ayarlar");
                    s.clickableItem([] {}, [] { Icon(Icons::Filled::Info, "Hakkında"); }, "Hakkında");
                });
            });
            caption("MediumFlexibleTopAppBar: daha büyük başlık ve alt yazı, ortalanmış (titleHorizontalAlignment).");
            MediumFlexibleTopAppBar({.title = [] { Text("Takvim"); },
                                     .subtitle = [] { Text("Ekim 2026"); },
                                     .navigationIcon = [] { IconButton([] {}, [] { Icon(Icons::AutoMirrored::Filled::ArrowBack, "Geri"); }); },
                                     .actions = [] { IconButton([] {}, [] { Icon(Icons::Filled::MoreVert, "Diğer"); }); },
                                     .containerColor = cs.surfaceContainerLow,
                                     .windowInsets = WindowInsets{},
                                     .titleHorizontalAlignment = Alignment::CenterHorizontally},
                                    Modifier().clip(RoundedCornerShape(16)));
            caption("LargeTopAppBar + exitUntilCollapsed (alt başlıklı): önce başlık küçülür, sonra liste kayar.");
            Box(Modifier().fillMaxWidth().height(380).clip(RoundedCornerShape(16)), [&] {
                CompositionLocalProvider({LocalSystemInsets.provides(SystemInsets{})}, [&] {  // not at the window's edge
                    Scaffold(Modifier().fillMaxSize().nestedScroll(appBar.nestedScrollConnection),
                             {.topBar = [&] {
                                  LargeTopAppBar({.title = [] { Text("Gelen kutusu"); },
                                                  .subtitle = [] { Text("3 okunmamış ileti"); },
                                                  .navigationIcon = [] { IconButton([] {}, [] { Icon(Icons::Filled::Menu, "Menü"); }); },
                                                  .actions = [] { IconButton([] {}, [] { Icon(Icons::Filled::Search, "Ara"); }); },
                                                  .containerColor = cs.surfaceContainerLow,
                                                  .scrollBehavior = appBar});
                              },
                              .containerColor = cs.surfaceContainerLow},
                             [&](PaddingValues padding) {
                                 LazyColumn(Modifier().fillMaxSize(), {.contentPadding = padding}, [](LazyListScope& list) {
                                     list.items(30, [](int i) {
                                         ListItem({.headline = [i] { Text(std::format("İleti {}", i + 1)); },
                                                   .supporting = [] { Text("Kaydırınca başlık küçülür"); },
                                                   .leading = [] { Icon(Icons::Filled::Email, "İleti"); },
                                                   .containerColor = colors::Transparent});
                                     });
                                 });
                             });
                });
            });
            caption("TwoRowsTopAppBar: başlık iki satırda da kendi biçimini seçer — açıkken büyük ve iki satır, kapanınca kısa.");
            Box(Modifier().fillMaxWidth().height(380).clip(RoundedCornerShape(16)), [&] {
                CompositionLocalProvider({LocalSystemInsets.provides(SystemInsets{})}, [&] {  // not at the window's edge
                    Scaffold(Modifier().fillMaxSize().nestedScroll(twoRows.nestedScrollConnection),
                             {.topBar = [&] {
                                  TwoRowsTopAppBar(
                                      [](bool expanded) {
                                          if (expanded) Text("Haftalık\nözet", {.font = typography().headlineMedium, .fontWeight = FontWeight::Bold});
                                          else Text("Özet", {.font = typography().titleLarge});
                                      },
                                      {.navigationIcon = [] { IconButton([] {}, [] { Icon(Icons::AutoMirrored::Filled::ArrowBack, "Geri"); }); },
                                       .actions = [] { IconButton([] {}, [] { Icon(Icons::Filled::DateRange, "Tarih"); }); },
                                       .containerColor = cs.surfaceContainerLow,
                                       .expandedHeight = 168,
                                       .scrollBehavior = twoRows},
                                      Modifier(),
                                      [&](bool expanded) {
                                          Text(expanded ? "6–12 Ekim · 14 etkinlik" : "6–12 Ekim",
                                               {.font = expanded ? typography().titleMedium : typography().labelMedium, .color = cs.onSurfaceVariant});
                                      });
                              },
                              .containerColor = cs.surfaceContainerLow},
                             [&](PaddingValues padding) {
                                 LazyColumn(Modifier().fillMaxSize(), {.contentPadding = padding}, [](LazyListScope& list) {
                                     list.items(30, [](int i) {
                                         ListItem({.headline = [i] { Text(std::format("Etkinlik {}", i + 1)); },
                                                   .supporting = [] { Text("Kaydırınca başlık kısalır"); },
                                                   .leading = [] { Icon(Icons::Filled::Schedule, "Etkinlik"); },
                                                   .containerColor = colors::Transparent});
                                     });
                                 });
                             });
                });
            });
            caption("FlexibleBottomAppBar + exitAlways: liste ilerledikçe alt çubuk aşağı kayıp gider, geri kaydırınca döner.");
            Box(Modifier().fillMaxWidth().height(300).clip(RoundedCornerShape(16)), [&] {
                Scaffold(Modifier().fillMaxSize().nestedScroll(bottomBar.nestedScrollConnection),
                         {.bottomBar = [&] {
                              FlexibleBottomAppBar(Modifier(), [] {
                                  IconButton([] {}, [] { Icon(Icons::AutoMirrored::Filled::ArrowBack, "Geri"); });
                                  IconButton([] {}, [] { Icon(Icons::Filled::Edit, "Düzenle"); });
                                  IconButton([] {}, [] { Icon(Icons::Filled::Share, "Paylaş"); });
                                  IconButton([] {}, [] { Icon(Icons::Filled::Delete, "Sil"); });
                              }, {.scrollBehavior = bottomBar, .windowInsets = false});
                          },
                          .containerColor = cs.surfaceContainerLow},
                         [&](PaddingValues padding) {
                             LazyColumn(Modifier().fillMaxSize(), {.contentPadding = padding}, [](LazyListScope& list) {
                                 list.items(30, [](int i) {
                                     ListItem({.headline = [i] { Text(std::format("Fotoğraf {}", i + 1)); }, .containerColor = colors::Transparent});
                                 });
                             });
                         });
            });
            caption("AppBarWithSearch + enterAlways: liste ilerledikçe arama çubuğu yukarı kayar, geri kaydırınca önce o gelir; dokununca "
                    "tam ekran arama açılır.");
            auto mailField = [&] {
                SearchBarDefaults::InputField(mailSearch, mailQuery,
                                              {.placeholder = "Postalarda ara", .leadingIcon = [] { Icon(Icons::Filled::Search, "Ara"); }});
            };
            Box(Modifier().fillMaxWidth().height(380).clip(RoundedCornerShape(16)), [&] {
                CompositionLocalProvider({LocalSystemInsets.provides(SystemInsets{})}, [&] {  // not at the window's edge
                    Scaffold(Modifier().fillMaxSize().nestedScroll(searchScroll.nestedScrollConnection),
                             {.topBar = [&] {
                                  AppBarWithSearch(mailSearch, mailField, Modifier(),
                                                   {.navigationIcon = [] { IconButton([] {}, [] { Icon(Icons::Filled::Menu, "Menü"); }); },
                                                    .actions = [] { IconButton([] {}, [] { Icon(Icons::Filled::Person, "Hesap"); }); },
                                                    .scrollBehavior = searchScroll});
                              }},
                             [&](PaddingValues padding) {
                                 LazyColumn(Modifier().fillMaxSize(), {.contentPadding = padding}, [](LazyListScope& list) {
                                     list.items(30, [](int i) {
                                         ListItem({.headline = [i] { Text(std::format("Posta {}", i + 1)); },
                                                   .supporting = [] { Text("Kaydırınca arama çubuğu çekilir"); },
                                                   .leading = [] { Icon(Icons::Filled::Email, "Posta"); },
                                                   .containerColor = colors::Transparent});
                                     });
                                 });
                             });
                });
            });
            ExpandedFullScreenSearchBar(mailSearch, mailField, [&] {
                std::string typed = mailQuery.text();
                for (int i = 1; i <= 30; ++i) {
                    std::string mail = std::format("Posta {}", i);
                    if (!typed.empty() && mail.find(typed) == std::string::npos) continue;
                    ListItem({.headline = [mail] { Text(mail); }, .leading = [] { Icon(Icons::Filled::Email, "Posta"); },
                              .containerColor = colors::Transparent});
                }
            });
        });
    });
}

// Modifier.pullToRefresh on a plain Box, with the default indicator or one of our own in an IndicatorBox.
void PullToRefreshSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto refreshing = remember([] { return mutableStateOf(false); });
        auto enabled = remember([] { return mutableStateOf(true); });
        auto custom = remember([] { return mutableStateOf(false); });
        auto refreshes = remember([] { return mutableStateOf(0); });
        auto state = rememberPullToRefreshState();
        auto scope = rememberCoroutineScope();
        auto refresh = [refreshing, refreshes, scope] {
            refreshing.set(true);
            scope.launch([refreshing, refreshes]() -> Task {
                co_await delay(std::chrono::milliseconds(2000));
                refreshes.set(refreshes.peek() + 1);
                refreshing.set(false);
            });
        };
        Section("Modifier.pullToRefresh · IndicatorBox", [&] {
            Text("Listeyi en üstteyken aşağı çekin: eşikten sonra gösterge yavaşlar, eşiği geçip bırakınca yenilenir.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8}, [&] {
                FilterChip(enabled.get(), [enabled] { enabled.set(!enabled.peek()); }, [] { Text("Açık (enabled)"); });
                FilterChip(custom.get(), [custom] { custom.set(!custom.peek()); }, [] { Text("Kendi göstergemiz"); });
            });
            Text(std::format("{} kez yenilendi · distanceFraction {:.2f}", refreshes.get(), state.distanceFraction()),
                 {.font = typography().bodySmall, .color = cs.primary});
            int round = refreshes.get();
            Box(Modifier().fillMaxWidth().height(260).clip(RoundedCornerShape(16)).background(cs.surfaceContainerHighest)
                    .pullToRefresh(refreshing.get(), state, refresh, enabled.get()),
                [&] {
                    LazyColumn(Modifier().fillMaxSize(), {.contentPadding = {16, 8, 16, 8}}, [round](LazyListScope& list) {
                        list.items(20, [round](int i) {
                            Text(std::format("Haber {} (tur {})", i + 1, round + 1), Modifier().padding(0, 10));
                        });
                    });
                    if (custom.get()) {
                        // A Material 3 Expressive loading indicator that fills up with the pull.
                        bool busy = refreshing.get();
                        PullToRefreshDefaults::IndicatorBox(state, busy, Modifier().align(Alignment::TopCenter), [&] {
                            if (busy)
                                LoadingIndicator(Modifier().size(36), cs.onPrimaryContainer);
                            else
                                LoadingIndicator(std::min(1.f, state.distanceFraction()), Modifier().size(36),
                                                 cs.onPrimaryContainer);
                        }, PullToRefreshDefaults::IndicatorMaxDistance, CircleShape, cs.primaryContainer);
                    } else {
                        PullToRefreshDefaults::Indicator(state, refreshing.get(), Modifier().align(Alignment::TopCenter));
                    }
                });
        });
    });
}
