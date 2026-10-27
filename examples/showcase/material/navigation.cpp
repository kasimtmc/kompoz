// Material screen: tabs, the FAB menu, the short navigation bar, the wide navigation rail, floating toolbars.

#include "showcase.hpp"

void TabsSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto tab = remember([] { return mutableStateOf(0); });
        auto subTab = remember([] { return mutableStateOf(0); });
        auto travel = remember([] { return mutableStateOf(0); });
        auto city = remember([] { return mutableStateOf(0); });
        Section("Sekmeler", [&] {
            PrimaryTabRow(tab.get(), Modifier(), [&] {
                const char* titles[] = {"Gelen", "Yıldızlı", "Ayarlar"};
                const ImageVector* icons[] = {&Icons::Filled::Email, &Icons::Filled::Star, &Icons::Filled::Settings};
                for (int i = 0; i < 3; ++i)
                    Tab(tab.get() == i, [tab, i] { tab.set(i); }, [&, i] { Text(titles[i]); },
                        [&, i] { Icon(*icons[i], titles[i]); });
            });
            SecondaryTabRow(subTab.get(), Modifier(), [&] {
                for (int i = 0; i < 3; ++i)
                    Tab(subTab.get() == i, [subTab, i] { subTab.set(i); }, [i] { Text(i == 0 ? "Tümü" : i == 1 ? "Yeni" : "Arşiv"); });
            });
            // LeadingIconTab: the icon beside the text, a lower row; unselected ones in
            // onSurfaceVariant (by default they take the selected color, as in Compose).
            PrimaryTabRow(travel.get(), Modifier(), [&] {
                const char* titles[] = {"Uçuş", "Otel", "Yer"};
                const ImageVector* icons[] = {&Icons::Filled::Send, &Icons::Filled::Home, &Icons::Filled::Place};
                for (int i = 0; i < 3; ++i)
                    LeadingIconTab(travel.get() == i, [travel, i] { travel.set(i); }, [&, i] { Text(titles[i]); },
                                   [&, i] { Icon(*icons[i], titles[i], Modifier().size(18)); }, Modifier(),
                                   {.unselectedContentColor = cs.onSurfaceVariant});
            });
            AnimatedContent<int>(tab.get(), [&](const int& t) {
                const char* texts[] = {"3 yeni ileti var.", "Yıldızlı ileti yok.", "Bildirimler ve hesap ayarları."};
                Text(texts[t], Modifier().padding(4, 8), {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            });
            static const char* cities[] = {"İstanbul", "Ankara", "İzmir", "Bursa", "Antalya", "Konya", "Adana", "Gaziantep",
                                           "Kayseri", "Trabzon", "Eskişehir", "Samsun"};
            PrimaryScrollableTabRow(city.get(), Modifier(), [&] {
                for (int i = 0; i < 12; ++i) Tab(city.get() == i, [city, i] { city.set(i); }, [i] { Text(cities[i]); });
            }, {.edgePadding = 16});
            Text(std::string("PrimaryScrollableTabRow: ") + cities[city.get()] + " — seçilen sekme ortaya kayar.",
                 Modifier().padding(4, 8), {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
        });
    });
}

void FabMenuSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        // M3 Expressive's FAB menu: its actions open above the button, which turns into a round close button.
        Section("FloatingActionButtonMenu", [&] {
            auto fabOpen = remember([] { return mutableStateOf(false); });
            auto fabPicked = remember([] { return mutableStateOf(std::string("yok")); });
            BackHandler(fabOpen.get(), [fabOpen] { fabOpen.set(false); });
            Box(Modifier().fillMaxWidth().height(280), Alignment::BottomEnd, [&] {
                Text("Seçilen: " + fabPicked.get(), Modifier().align(Alignment::BottomStart),
                     {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                FloatingActionButtonMenu(fabOpen.get(), [&] {
                    ToggleFloatingActionButton(fabOpen.get(), [fabOpen](bool on) { fabOpen.set(on); },
                                               [](const ToggleFloatingActionButtonScope& s) {  // (turned as it is drawn)
                                                   Icon(Icons::Filled::Add, "Menü",
                                                        Modifier().graphicsLayer([s](GraphicsLayerScope& l) { l.rotationZ = 45 * s.checkedProgress(); }));
                                               });
                }, [&](const FloatingActionButtonMenuScope& menu) {
                    struct Action {
                        const char* label;
                        const ImageVector* icon;
                    };
                    for (Action a : {Action{"Not", &Icons::Filled::Edit}, Action{"Konum", &Icons::Filled::Place},
                                     Action{"E-posta", &Icons::Filled::Email}})
                        FloatingActionButtonMenuItem(menu, [fabOpen, fabPicked, a] {
                            fabPicked.set(a.label);
                            fabOpen.set(false);
                        }, [a] { Text(a.label); }, [a] { Icon(*a.icon, ""); });
                });
            });
        });
    });
}

void ShortNavigationBarSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        // M3 Expressive's short navigation bar: items sharing the width (compact) or together in the middle (wider).
        Section("ShortNavigationBar", [&] {
            auto page = remember([] { return mutableStateOf(0); });
            struct Tab {
                const char* label;
                const ImageVector* icon;
            };
            const Tab tabs[] = {{"Ana sayfa", &Icons::Filled::Home}, {"Ara", &Icons::Filled::Search}, {"Profil", &Icons::Filled::Person}};
            for (ShortNavigationBarArrangement arrangement : {ShortNavigationBarArrangement::EqualWeight, ShortNavigationBarArrangement::Centered}) {
                Text(arrangement == ShortNavigationBarArrangement::EqualWeight ? "Eşit paylaşım" : "Ortada",
                     {.font = typography().labelLarge, .color = cs.onSurfaceVariant});
                ShortNavigationBar(Modifier().clip(RoundedCornerShape(16)), [&] {
                    for (int i = 0; i < 3; ++i)
                        ShortNavigationBarItem(page.get() == i, [page, i] { page.set(i); },
                                               [&, i] { Icon(*tabs[i].icon, ""); }, [&, i] { Text(tabs[i].label); });
                }, {.arrangement = arrangement, .windowInsets = WindowInsets{}});
            }
        });
    });
}

void WideNavigationRailSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        // M3 Expressive's wide navigation rail: ☰ widens it, the labels going after the icons.
        Section("WideNavigationRail", [&] {
            auto rail = rememberWideNavigationRailState();
            auto box = remember([] { return mutableStateOf(0); });
            struct Box_ {
                const char* label;
                const ImageVector* icon;
            };
            const Box_ boxes[] = {{"Gelen", &Icons::Filled::Email}, {"Yıldızlı", &Icons::Filled::Star}, {"Ayarlar", &Icons::Filled::Settings}};
            // Modal: it opens over everything from the window's start edge, above a scrim.
            auto modal = remember([] { return mutableStateOf(false); });
            Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                Switch(modal.get(), [modal, rail](bool on) {
                    rail.collapse();
                    modal.set(on);
                });
                Text("  Modal (ModalWideNavigationRail)", {.font = typography().bodyMedium});
            });
            Row(Modifier().fillMaxWidth().height(380).clip(RoundedCornerShape(16)).background(cs.surfaceContainerHigh), [&] {
                auto items = [&] {
                    for (int i = 0; i < 3; ++i)
                        WideNavigationRailItem(box.get() == i, [box, i, rail, modal] {
                            box.set(i);
                            if (modal.peek()) rail.collapse();  // a modal one closes once a destination is picked
                        }, [&, i] { Icon(*boxes[i].icon, ""); }, [&, i] { Text(boxes[i].label); });
                };
                Content header = [rail] {
                    IconButton([rail] { rail.toggle(); },
                               [rail] { Icon(rail.isExpanded() ? Icons::AutoMirrored::Filled::ArrowBack : Icons::Filled::Menu, "Rayı aç/kapat"); });
                };
                if (modal.get())
                    ModalWideNavigationRail(Modifier(), items, {.state = rail, .header = header});
                else
                    WideNavigationRail(Modifier(), items, {.state = rail, .header = header, .windowInsets = WindowInsets{}});
                Box(Modifier().weight(1).fillMaxHeight(), Alignment::Center,
                    [&] { Text(boxes[box.get()].label, {.font = typography().headlineSmall}); });
            });
        });
    });
}

void FloatingToolbarsSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        // M3 Expressive's floating toolbars over a list: they leave as it scrolls on and come back as it scrolls back.
        Section("Kayan araç çubukları", [&] {
            auto below = FloatingToolbarDefaults::exitAlwaysScrollBehavior(FloatingToolbarExitDirection::Bottom);
            auto aside = FloatingToolbarDefaults::exitAlwaysScrollBehavior(FloatingToolbarExitDirection::End);
            auto more = remember([] { return mutableStateOf(false); });
            auto tools = remember([] { return mutableStateOf(false); });
            Box(Modifier()
                    .fillMaxWidth()
                    .height(340)
                    .clip(RoundedCornerShape(16))
                    .background(cs.surfaceContainerHigh)
                    .nestedScroll(below.nestedScrollConnection)
                    .nestedScroll(aside.nestedScrollConnection),
                [&] {
                    LazyColumn(Modifier().fillMaxSize(), {}, [](LazyListScope& list) {
                        list.items(40, [](int i) { Text(std::format("Satır {}", i + 1), Modifier().fillMaxWidth().padding(16, 12)); });
                    });
                    // The middle actions stay; ⋮ shows the ones on the sides.
                    HorizontalFloatingToolbar(more.get(), [&] {
                        IconButton([] {}, [] { Icon(Icons::Filled::Edit, "Düzenle"); });
                        IconButton([] {}, [] { Icon(Icons::Filled::Share, "Paylaş"); });
                        IconButton([more] { more.set(!more.get()); }, [] { Icon(Icons::Filled::MoreVert, "Daha fazla"); });
                    }, Modifier().align(Alignment::BottomCenter).offset(0, -FloatingToolbarDefaults::ScreenOffset),
                        {.scrollBehavior = below,
                         .leadingContent = [] { IconButton([] {}, [] { Icon(Icons::Filled::Favorite, "Beğen"); }); },
                         .trailingContent = [] { IconButton([] {}, [] { Icon(Icons::Filled::Delete, "Sil"); }); }});
                    // A vibrant one with a FAB: collapsed, only the FAB is left.
                    VerticalFloatingToolbar(tools.get(), [&] {
                        FloatingToolbarDefaults::VibrantFloatingActionButton([tools] { tools.set(!tools.get()); },
                                                                            [&] { Icon(tools.get() ? Icons::Filled::Close : Icons::Filled::Add, "Araçlar"); });
                    }, [&] {
                        IconButton([] {}, [] { Icon(Icons::Filled::Place, "Konum"); });
                        IconButton([] {}, [] { Icon(Icons::Filled::Email, "E-posta"); });
                    }, Modifier().align(Alignment::TopEnd).offset(-FloatingToolbarDefaults::ScreenOffset, FloatingToolbarDefaults::ScreenOffset),
                        {.colors = FloatingToolbarDefaults::vibrantFloatingToolbarColors(), .scrollBehavior = aside});
                });
        });
    });
}
