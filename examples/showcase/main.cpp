// Kompoz showcase: state, remember, keys, effects, theming, animation, text input and smart
// recomposition. Turn on "Recomposition'ları göster" to see which parts actually run again.

#include "showcase.hpp"

void ShowcaseApp() {
    auto dark = remember([] { return mutableStateOf(isSystemInDarkTheme()); });
    // The system's, as it changes, until the switch is used: then the user's.
    auto darkChosen = remember([] { return mutableStateOf(false); });
    LaunchedEffect(isSystemInDarkTheme(), [dark, darkChosen, system = isSystemInDarkTheme()]() -> Task {
        if (!darkChosen.peek()) dark.set(system);
        co_return;
    });
    auto dynamicColor = remember([] { return mutableStateOf(true); });  // Material You where the system has it
    auto showRecompositions = remember([] { return mutableStateOf(false); });
    // The screens are routes of a NavHost; the tabs keep their own back stacks (and scroll
    // positions), saved while another tab is shown, as Compose's bottom navigation does.
    auto nav = rememberNavController();
    auto entry = nav.currentBackStackEntryAsState();
    std::string route = entry ? entry->route() : "home";
    auto in = [&](std::string_view graph) {  // the screen is in this graph (or is this destination)
        auto routes = entry ? entry->hierarchy() : std::vector<std::string>{};
        return std::find(routes.begin(), routes.end(), graph) != routes.end();
    };
    int tab = in(routePattern<routes::ListGraph>())    ? 1
              : in(routePattern<routes::Components>()) ? 2
              : in(routePattern<routes::Material>())   ? 3
                                                       : 0;
    auto goTab = [nav](int i) {
        NavOptions tabs{.popUpTo = routePattern<routes::Home>(), .saveState = true, .launchSingleTop = true, .restoreState = true};
        if (i == 0) nav.navigate(routes::Home{}, tabs);
        else if (i == 1) nav.navigate(routes::ListGraph{}, tabs);
        else if (i == 2) nav.navigate(routes::Components{}, tabs);
        else nav.navigate(routes::Material{}, tabs);
    };
    auto listState = rememberLazyListState();
    auto snackbar = rememberSnackbarHostState();
    auto overflow = remember([] { return mutableStateOf(false); });
    // The whole app laid out right to left: the system language's direction at first, then the menu's.
    auto rightToLeft = remember([] { return mutableStateOf(LocalLayoutDirection.current() == LayoutDirection::Rtl); });
    auto about = remember([] { return mutableStateOf(false); });
    auto drawer = rememberDrawerState();
    auto scope = rememberCoroutineScope();
    // The app bar scrolls away with the content and comes back when it scrolls the other way.
    auto barScroll = TopAppBarDefaults::enterAlwaysScrollBehavior();
    // A new screen starts with the bar shown; the screen it was on keeps it as it was (after
    // Android ended the app too: the bar's state and the screen seen last are saved).
    auto& seen = rememberSaveable([&] { return mutableStateOf(route); });
    LaunchedEffect(route, [barScroll, seen, route]() -> Task {
        if (seen.peek() != route) {
            barScroll.state.setHeightOffset(0);
            barScroll.state.setContentOffset(0);
            seen.set(route);
        }
        co_return;
    });
    struct Destination {
        const char* label;
        const ImageVector* icon;
    };
    const Destination destinations[] = {{"Ana sayfa", &Icons::Filled::Home},
                                        {"Liste", &Icons::Filled::List},
                                        {"Bileşenler", &Icons::Filled::Star},
                                        {"Material", &Icons::Filled::CheckCircle}};
    auto closeDrawer = [drawer, scope] { scope.launch([drawer]() -> Task { co_await drawer.close(); }); };

    // The content lambdas below run while composing, so they capture by reference;
    // event handlers capture by value.
    auto drawerContent = [&] {
        ModalDrawerSheet(Modifier(), [&] {
            Text("Kompoz", Modifier().padding(16, 8, 16, 16), {.font = typography().titleLarge});
            for (int i = 0; i < 4; ++i) {
                const Destination& dest = destinations[i];
                NavigationDrawerItem([&] { Text(dest.label); }, tab == i,
                                     [goTab, i, closeDrawer] {
                                         goTab(i);
                                         closeDrawer();
                                     },
                                     [&] { Icon(*dest.icon, dest.label); },
                                     i == 3 ? Content([] { Badge(Modifier(), [] { Text("Yeni"); }); }) : Content());
            }
            HorizontalDivider(Modifier().padding(16, 8));
            NavigationDrawerItem([] { Text("Hakkında"); }, false,
                                 [about, closeDrawer] {
                                     about.set(true);
                                     closeDrawer();
                                 },
                                 [] { Icon(Icons::Filled::Info, "Hakkında"); });
        });
    };

    auto topBar = [&] {
        TopAppBar(
            {.title = [] { Text("Kompoz"); },
             .navigationIcon =
                 [drawer, scope] {
                     IconButton([drawer, scope] { scope.launch([drawer]() -> Task { co_await drawer.open(); }); },
                                [] { Icon(Icons::Filled::Menu, "Menü"); });
                 },
             .actions =
                 [&] {
                     IconButton([dark, darkChosen] { dark.set(!dark.get()), darkChosen.set(true); }, [] { Icon(Icons::Filled::DarkMode, "Koyu tema"); });
                     IconButton(
                         [showRecompositions, snackbar] {
                             bool on = !showRecompositions.get();
                             showRecompositions.set(on);
                             snackbar.showSnackbar(on ? "Recomposition vurgusu açık" : "Recomposition vurgusu kapalı");
                         },
                         [&] {
                             Icon(Icons::Filled::Visibility, "Recomposition'ları göster", {},
                                  showRecompositions.get() ? std::optional(colorScheme().primary) : std::nullopt);
                         });
                     Box([&] {  // the menu opens below its anchor, this Box
                         IconButton([overflow] { overflow.set(true); },
                                    [] { Icon(Icons::Filled::MoreVert, "Diğer seçenekler"); });
                         DropdownMenu(overflow.get(), [overflow] { overflow.set(false); }, [&] {
                             DropdownMenuItem([] { Text("Hakkında"); },
                                              [overflow, about] {
                                                  overflow.set(false);
                                                  about.set(true);
                                              },
                                              {.leadingIcon = [] { Icon(Icons::Filled::Info, "Hakkında"); }});
                             DropdownMenuItem([] { Text("Material bileşenleri"); },
                                              [overflow, goTab] {
                                                  overflow.set(false);
                                                  goTab(3);
                                              },
                                              {.leadingIcon = [] { Icon(Icons::Filled::CheckCircle, "Material"); }});
                             // LocalLayoutDirection: rows from the right, Start is the right, horizontal lists start there.
                             DropdownMenuItem([rightToLeft] { Text(rightToLeft.get() ? "Soldan sağa (LTR)" : "Sağdan sola (RTL)"); },
                                              [overflow, rightToLeft] {
                                                  overflow.set(false);
                                                  rightToLeft.set(!rightToLeft.get());
                                              },
                                              {.leadingIcon = [] { Icon(Icons::AutoMirrored::Filled::ArrowBack, "Yön"); }});
                         });
                     });
                 },
             .scrollBehavior = barScroll});
    };

    auto fab = [&] {
        if (entry && entry->hasRoute<routes::Home>())
            ExtendedFloatingActionButton(
                [snackbar, scope, notified = remember([] { return std::make_shared<int>(0); })] {
                    // As Compose apps do: wait for how it went (with an action it stays until answered or closed).
                    // Every other one has a long action, which goes on a line of its own (see snackbarHost).
                    bool longAction = (*notified)++ % 2 == 1;
                    scope.launch([snackbar, longAction]() -> Task {
                        SnackbarResult r = co_await snackbar.showSnackbarAsync(
                            longAction ? "Bildirimler kapalı; önemli haberleri kaçırabilirsiniz." : "Merhaba! Bu bir Snackbar.",
                            longAction ? "Bildirimleri aç" : "Geri al", std::nullopt, true);
                        if (r == SnackbarResult::ActionPerformed) snackbar.showSnackbar(longAction ? "Bildirimler açıldı" : "Geri alındı");
                    });
                },
                Icons::Filled::Notifications, "Bildir");
        else if (entry && entry->hasRoute<routes::ListAll>())
            ScrollToTopButton(listState);
    };

    auto screen = [&](PaddingValues padding) {
        // Clear of the bars; the keyboard only takes what the bottom bar doesn't already cover.
        Modifier m = Modifier().fillMaxSize().padding(padding).consumeWindowInsets(padding).imePadding();
        ContentTransform fade = togetherWith(fadeIn(0, tween(220, 60)), fadeOut(0, tween(120)));
        NavHost(nav, routes::Home{}, m, [&](NavGraphBuilder& graph) {
            graph.composable<routes::Home>([&](const NavBackStackEntry&) { HomeScreen(Modifier().fillMaxSize()); });
            // The list and its items: a nested graph, with a ViewModel of its own.
            graph.navigation<routes::ListGraph>(routes::ListAll{}, [&](NavGraphBuilder& list) {
                list.composable<routes::ListAll>([&](const NavBackStackEntry& e) {
                    ListScreen(Modifier().fillMaxSize(), listState, nav, viewModel<LikesViewModel>(*e.parent()));
                });
                // Also opened by a link: `showcase kompoz://item/42`, an Android intent, or #item/42 on the web.
                list.composable<routes::Item>({navDeepLink<routes::Item>("kompoz://item")}, [&](const NavBackStackEntry& e) {
                    ItemScreen(e.toRoute<routes::Item>().n, nav, viewModel<LikesViewModel>(*e.parent()));
                });
            });
            graph.composable<routes::Components>({navDeepLink("kompoz://components")},
                                                 [&](const NavBackStackEntry&) { ComponentsScreen(Modifier().fillMaxSize()); });
            graph.composable<routes::Material>({navDeepLink("kompoz://material")},
                                               [&](const NavBackStackEntry&) { MaterialScreen(Modifier().fillMaxSize(), dynamicColor, nav); });
            graph.composable<routes::Places>({navDeepLink("kompoz://places")},
                                             [&](const NavBackStackEntry&) { PlacesScreen(Modifier().fillMaxSize()); });
        }, {.transition = fade, .popTransition = fade});
        if (about.get())
            AlertDialog([about] { about.set(false); },
                        {.confirmButton = [about] { TextButton([about] { about.set(false); }, [] { Text("Tamam"); }); },
                         .icon = [] { Icon(Icons::Filled::Info, "Bilgi"); },
                         .title = [] { Text("Kompoz"); },
                         .text = [] {
                             Text("C++20 ile Jetpack Compose tarzı, çok platformlu bir arayüz kütüphanesi: Linux, Windows, Android "
                                  "ve web. Apache 2.0 lisanslı.\n\n"
                                  "İçindekiler: Noto Sans fontları (SIL OFL 1.1), SDL (zlib), FreeType (portions of this software are "
                                  "copyright © 2026 The FreeType Project, www.freetype.org) ve HarfBuzz (MIT). Lisans metinleri "
                                  "uygulamanın licenses klasöründe.");
                         }});
    };

    ColorScheme scheme = dynamicColor.get() && isDynamicColorAvailable()
                             ? (dark.get() ? dynamicDarkColorScheme() : dynamicLightColorScheme())
                             : (dark.get() ? darkColorScheme() : lightColorScheme());
    SystemBarsAppearance(!dark.get());  // dark status bar icons over the light theme
    MaterialTheme(animateColorScheme(scheme), [&] {
        // The showcase speaks Turkish, and so do the library's own texts in it (LocalStrings).
        CompositionLocalProvider({LocalStrings.provides(Strings::turkish()),
                                  LocalShowRecompositions.provides(showRecompositions.get()),
                                  LocalLayoutDirection.provides(rightToLeft.get() ? LayoutDirection::Rtl : LayoutDirection::Ltr)}, [&] {
            auto app = [&] {
                ModalNavigationDrawer(drawerContent, drawer, [&] {
                    // The tabs where they suit the window (currentWindowAdaptiveInfo), as M3
                    // Expressive's: a short bar on a phone (its labels beside the icons when it lies
                    // on its side), a wide rail on a wider window.
                    NavigationSuiteScaffold(
                        [&] {
                            for (int i = 0; i < 4; ++i) {
                                const ImageVector& icon = *destinations[i].icon;
                                std::string label = destinations[i].label;
                                NavigationSuiteItem(tab == i, [goTab, i] { goTab(i); }, [&icon] { Icon(icon, ""); }, [label] { Text(label); });
                            }
                        },
                        Modifier().fillMaxSize(), {}, [&] {
                            Scaffold(Modifier().fillMaxSize().nestedScroll(barScroll.nestedScrollConnection),
                                     {.topBar = topBar,
                                      .floatingActionButton = fab,
                                      // A snackbar of the app's own (here the standard one): a long action under the text.
                                      .snackbarHost = [&] {
                                          SnackbarHost(snackbar, Modifier(), [](const SnackbarData& data) {
                                              Snackbar(data, Modifier(), {.actionOnNewLine = data.actionLabel.size() > 12});
                                          });
                                      }},
                                     screen);
                        });
                });
            };
#ifdef __ANDROID__
            app();
#else
            // On a desktop (and the web), the window's menus and their shortcuts above the app.
            Column(Modifier().fillMaxSize(), [&] {
                std::vector<MenuBarItem> go;
                for (int i = 0; i < 4; ++i)
                    go.push_back({.text = destinations[i].label, .onClick = [goTab, i] { goTab(i); },
                                  .shortcut = KeyShortcut{Key(int(Key::One) + i), true}, .checked = tab == i});
                go.push_back(MenuBarItem::Separator());
                go.push_back({.text = "Geri", .onClick = [nav] { nav.popBackStack(); }, .shortcut = KeyShortcut{Key::Left, false, false, true},
                              .enabled = nav.previousBackStackEntry() != nullptr});
                MenuBar({{"Dosya", {{.text = "Hakkında", .onClick = [about] { about.set(true); }, .shortcut = KeyShortcut{Key::F1}}}},
                         {"Görünüm",
                          {{.text = "Koyu tema", .onClick = [dark, darkChosen] { dark.set(!dark.get()), darkChosen.set(true); }, .shortcut = KeyShortcut{Key::D, true},
                            .checked = dark.get()},
                           {.text = "Dinamik renk", .onClick = [dynamicColor] { dynamicColor.set(!dynamicColor.get()); }, .checked = dynamicColor.get()},
                           {.text = "Sağdan sola", .onClick = [rightToLeft] { rightToLeft.set(!rightToLeft.get()); },
                            .shortcut = KeyShortcut{Key::R, true, true}, .checked = rightToLeft.get()},
                           MenuBarItem::Separator(),
                           {.text = "Yeniden çizimleri göster", .onClick = [showRecompositions] { showRecompositions.set(!showRecompositions.get()); },
                            .checked = showRecompositions.get()}}},
                         {"Git", std::move(go)}});
                Box(Modifier().fillMaxWidth().weight(1), [&] { app(); });
            });
#endif
        });
    });
}

kompoz::Application kompoz::createApplication(int, char**) {
    return {.title = "Kompoz Showcase", .width = 440, .height = 900, .content = ShowcaseApp};
}
