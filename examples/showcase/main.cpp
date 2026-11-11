// Kompoz showcase: state, remember, keys, effects, theming, animation, text input and smart
// recomposition. Turn on "Recomposition'ları göster" to see which parts actually run again.

#include "showcase.hpp"

@Kompozable
fun ShowcaseApp() {
    val system = isSystemInDarkTheme()
    var dark by remember { mutableStateOf(system) }
    // The system's, as it changes, until the switch is used: then the user's.
    var darkChosen by remember { mutableStateOf(false) }
    LaunchedEffect(system) {
        if (!darkChosen) dark = system
    }
    val colorSource = rememberSaveable { mutableStateOf(ColorSource.System) }  // Material You where the system has it
    var showRekompozitions by remember { mutableStateOf(false) }
    // The screens are routes of a NavHost; the tabs keep their own back stacks (and scroll
    // positions), saved while another tab is shown, as Compose's bottom navigation does.
    val nav = rememberNavController()
    val entry by nav.currentBackStackEntryAsState()
    val destination = entry?.destination
    val route = destination?.route ?: "home"
    val tab = when {
        destination?.hierarchy?.any { it.hasRoute<routes.ListGraph>() } == true -> 1
        destination?.hierarchy?.any { it.hasRoute<routes.Components>() } == true -> 2
        destination?.hierarchy?.any { it.hasRoute<routes.Material>() } == true -> 3
        else -> 0
    }
    val tabs = navOptions {
        popUpTo<routes.Home> { saveState = true }
        launchSingleTop = true
        restoreState = true
    }
    val goTab: (Int) -> Unit = { i ->
        when (i) {
            0 -> nav.navigate(routes.Home(), tabs)
            1 -> nav.navigate(routes.ListGraph(), tabs)
            2 -> nav.navigate(routes.Components(), tabs)
            else -> nav.navigate(routes.Material(), tabs)
        }
    }
    val listState = rememberLazyListState()
    val snackbar = remember { SnackbarHostState() }
    var overflow by remember { mutableStateOf(false) }
    // The whole app laid out right to left: the system language's direction at first, then the menu's.
    val systemDirection = LocalLayoutDirection.current
    var rightToLeft by remember { mutableStateOf(systemDirection == LayoutDirection.Rtl) }
    var about by remember { mutableStateOf(false) }
    val drawer = rememberDrawerState(DrawerValue.Closed)
    val scope = rememberCoroutineScope()
    // The app bar scrolls away with the content and comes back when it scrolls the other way.
    val barScroll = TopAppBarDefaults.enterAlwaysScrollBehavior()
    // A new screen starts with the bar shown; the screen it was on keeps it as it was (after
    // Android ended the app too: the bar's state and the screen seen last are saved).
    var seen by rememberSaveable { mutableStateOf(route) }
    LaunchedEffect(route) {
        if (seen != route) {
            barScroll.state.heightOffset = 0f
            barScroll.state.contentOffset = 0f
            seen = route
        }
    }
    val destinations = listOf("Ana sayfa" to Icons.Filled.Home, "Liste" to Icons.Filled.List,
                              "Bileşenler" to Icons.Filled.Star, "Material" to Icons.Filled.CheckCircle)
    val closeDrawer: () -> Unit = { scope.launch { drawer.close() } }
    var notified by remember { mutableStateOf(0) }

    val drawerContent: @Kompozable () -> Unit = {
        ModalDrawerSheet {
            Text("Kompoz", modifier = Modifier.padding(16.dp, 8.dp, 16.dp, 16.dp), style = MaterialTheme.typography.titleLarge)
            for (i in 0 until 4) {
                val (label, icon) = destinations[i]
                NavigationDrawerItem(
                    label = { Text(label) },
                    selected = tab == i,
                    onClick = {
                        goTab(i)
                        closeDrawer()
                    },
                    icon = { Icon(icon, contentDescription = label) },
                    badge = if (i == 3) { { Badge { Text("Yeni") } } } else null)
            }
            HorizontalDivider(modifier = Modifier.padding(16.dp, 8.dp))
            NavigationDrawerItem(
                label = { Text("Hakkında") },
                selected = false,
                onClick = {
                    about = true
                    closeDrawer()
                },
                icon = { Icon(Icons.Filled.Info, contentDescription = "Hakkında") })
        }
    }

    val topBar: @Kompozable () -> Unit = {
        TopAppBar(
            title = { Text("Kompoz") },
            navigationIcon = {
                IconButton(onClick = { scope.launch { drawer.open() } }) { Icon(Icons.Filled.Menu, contentDescription = "Menü") }
            },
            actions = {
                IconButton(onClick = {
                    dark = !dark
                    darkChosen = true
                }) { Icon(Icons.Filled.DarkMode, contentDescription = "Koyu tema") }
                IconButton(onClick = {
                    showRekompozitions = !showRekompozitions
                    snackbar.showSnackbar(if (showRekompozitions) "Recomposition vurgusu açık" else "Recomposition vurgusu kapalı")
                }) {
                    Icon(Icons.Filled.Visibility, contentDescription = "Recomposition'ları göster",
                         tint = if (showRekompozitions) MaterialTheme.colorScheme.primary else LocalContentColor.current)
                }
                Box {  // the menu opens below its anchor, this Box
                    IconButton(onClick = { overflow = true }) { Icon(Icons.Filled.MoreVert, contentDescription = "Diğer seçenekler") }
                    DropdownMenu(expanded = overflow, onDismissRequest = { overflow = false }) {
                        DropdownMenuItem(
                            text = { Text("Hakkında") },
                            onClick = {
                                overflow = false
                                about = true
                            },
                            leadingIcon = { Icon(Icons.Filled.Info, contentDescription = "Hakkında") })
                        DropdownMenuItem(
                            text = { Text("Material bileşenleri") },
                            onClick = {
                                overflow = false
                                goTab(3)
                            },
                            leadingIcon = { Icon(Icons.Filled.CheckCircle, contentDescription = "Material") })
                        // LocalLayoutDirection: rows from the right, Start is the right, horizontal lists start there.
                        DropdownMenuItem(
                            text = { Text(if (rightToLeft) "Soldan sağa (LTR)" else "Sağdan sola (RTL)") },
                            onClick = {
                                overflow = false
                                rightToLeft = !rightToLeft
                            },
                            leadingIcon = { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Yön") })
                    }
                }
            },
            scrollBehavior = barScroll)
    }

    val fab: @Kompozable () -> Unit = {
        if (destination?.hasRoute<routes.Home>() == true) {
            ExtendedFloatingActionButton(
                onClick = {
                    // As Compose apps do: wait for how it went (with an action it stays until answered or closed).
                    // Every other one has a long action, which goes on a line of its own (see snackbarHost).
                    val longAction = notified++ % 2 == 1
                    scope.launch {
                        val result = snackbar.showSnackbar(
                            if (longAction) "Bildirimler kapalı; önemli haberleri kaçırabilirsiniz." else "Merhaba! Bu bir Snackbar.",
                            actionLabel = if (longAction) "Bildirimleri aç" else "Geri al",
                            withDismissAction = true)
                        if (result == SnackbarResult.ActionPerformed) snackbar.showSnackbar(if (longAction) "Bildirimler açıldı" else "Geri alındı")
                    }
                },
                icon = { Icon(Icons.Filled.Notifications, contentDescription = null) },
                text = { Text("Bildir") })
        } else if (destination?.hasRoute<routes.ListAll>() == true) {
            ScrollToTopButton(listState)
        }
    }

    val screen: @Kompozable (PaddingValues) -> Unit = { padding ->
        // Clear of the bars; the keyboard only takes what the bottom bar doesn't already cover.
        NavHost(
            navController = nav,
            startDestination = routes.Home(),
            modifier = Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding).imePadding(),
            enterTransition = { fadeIn(animationSpec = tween(220, delayMillis = 60)) },
            exitTransition = { fadeOut(animationSpec = tween(120)) },
        ) {
            composable<routes.Home> { HomeScreen(Modifier.fillMaxSize()) }
            // The list and its items: a nested graph, with a ViewModel of its own.
            navigation<routes.ListGraph>(startDestination = routes.ListAll()) {
                composable<routes.ListAll> { e ->
                    ListScreen(Modifier.fillMaxSize(), listState, nav, viewModel<LikesViewModel>(e.parent!!))
                }
                // Also opened by a link: `showcase kompoz://item/42`, an Android intent, or #item/42 on the web.
                composable<routes.Item>(deepLinks = listOf(navDeepLink<routes.Item>("kompoz://item"))) { e ->
                    ItemScreen(e.toRoute<routes.Item>().n, nav, viewModel<LikesViewModel>(e.parent!!))
                }
            }
            composable<routes.Components>(deepLinks = listOf(navDeepLink { uriPattern = "kompoz://components" })) {
                ComponentsScreen(Modifier.fillMaxSize())
            }
            composable<routes.Material>(deepLinks = listOf(navDeepLink { uriPattern = "kompoz://material" })) {
                MaterialScreen(Modifier.fillMaxSize(), colorSource, nav)
            }
            composable<routes.Places>(deepLinks = listOf(navDeepLink { uriPattern = "kompoz://places" })) {
                PlacesScreen(Modifier.fillMaxSize())
            }
        }
        if (about) {
            AlertDialog(
                onDismissRequest = { about = false },
                confirmButton = { TextButton(onClick = { about = false }) { Text("Tamam") } },
                icon = { Icon(Icons.Filled.Info, contentDescription = "Bilgi") },
                title = { Text("Kompoz") },
                text = {
                    Text("C++20 ile Jetpack Compose tarzı, çok platformlu bir arayüz kütüphanesi: Linux, Windows, Android " +
                             "ve web. Apache 2.0 lisanslı.\n\n" +
                             "İçindekiler: Noto Sans fontları (SIL OFL 1.1), FreeType (portions of this software are " +
                             "copyright © 2026 The FreeType Project, www.freetype.org), HarfBuzz (MIT), libpng ve zlib. " +
                             "Lisans metinleri uygulamanın licenses klasöründe.")
                })
        }
    }

    // The colors from where the user chose, the app's own where that has none.
    val scheme = when {
        colorSource.value == ColorSource.System && isDynamicColorAvailable() -> if (dark) dynamicDarkColorScheme() else dynamicLightColorScheme()
        colorSource.value == ColorSource.Wallpaper && isWallpaperColorAvailable() -> if (dark) wallpaperDarkColorScheme() else wallpaperLightColorScheme()
        else -> if (dark) darkColorScheme() else lightColorScheme()
    }
    SystemBarsAppearance(!dark)  // dark status bar icons over the light theme
    MaterialTheme(colorScheme = animateColorScheme(scheme)) {
        // The showcase speaks Turkish, and so do the library's own texts in it (LocalStrings).
        KompozitionLocalProvider(
            LocalStrings provides Strings.turkish(),
            LocalShowRekompozitions provides showRekompozitions,
            LocalLayoutDirection provides (if (rightToLeft) LayoutDirection.Rtl else LayoutDirection.Ltr),
        ) {
            val app: @Kompozable () -> Unit = {
                ModalNavigationDrawer(drawerContent = drawerContent, drawerState = drawer) {
                    // The tabs where they suit the window (currentWindowAdaptiveInfo), as M3
                    // Expressive's: a short bar on a phone (its labels beside the icons when it lies
                    // on its side), a wide rail on a wider window.
                    NavigationSuiteScaffold(
                        navigationItems = {
                            for (i in 0 until 4) {
                                val (label, icon) = destinations[i]
                                NavigationSuiteItem(selected = tab == i, onClick = { goTab(i) }, icon = { Icon(icon, contentDescription = null) },
                                                    label = { Text(label) })
                            }
                        },
                        modifier = Modifier.fillMaxSize(),
                    ) {
                        Scaffold(
                            modifier = Modifier.fillMaxSize().nestedScroll(barScroll.nestedScrollConnection),
                            topBar = topBar,
                            floatingActionButton = fab,
                            // A snackbar of the app's own (here the standard one): a long action under the text.
                            snackbarHost = {
                                SnackbarHost(snackbar) { data ->
                                    Snackbar(data, actionOnNewLine = (data.visuals.actionLabel?.length ?: 0) > 12)
                                }
                            },
                        ) { padding -> screen(padding) }
                    }
                }
            }
            if (kOnAndroid) {
                app()
            } else {
                // On a desktop (and the web), the window's menus and their shortcuts above the app.
                Column(modifier = Modifier.fillMaxSize()) {
                    MenuBar {
                        Menu("Dosya") {
                            Item("Hakkında", shortcut = KeyShortcut(Key.F1), onClick = { about = true })
                        }
                        Menu("Görünüm") {
                            CheckboxItem("Koyu tema", checked = dark, shortcut = KeyShortcut(Key.D, ctrl = true), onCheckedChange = {
                                dark = it
                                darkChosen = true
                            })
                            Separator()
                            RadioButtonItem("Uygulamanın renkleri", selected = colorSource.value == ColorSource.App,
                                            onClick = { colorSource.value = ColorSource.App })
                            RadioButtonItem("Sistemin renkleri", selected = colorSource.value == ColorSource.System,
                                            onClick = { colorSource.value = ColorSource.System })
                            RadioButtonItem("Duvar kağıdının renkleri", selected = colorSource.value == ColorSource.Wallpaper,
                                            onClick = { colorSource.value = ColorSource.Wallpaper })
                            Separator()
                            CheckboxItem("Sağdan sola", checked = rightToLeft, shortcut = KeyShortcut(Key.R, ctrl = true, shift = true),
                                         onCheckedChange = { rightToLeft = it })
                            Separator()
                            CheckboxItem("Yeniden çizimleri göster", checked = showRekompozitions, onCheckedChange = { showRekompozitions = it })
                        }
                        Menu("Git") {
                            val keys = listOf(Key.One, Key.Two, Key.Three, Key.Four)
                            for (i in 0 until 4)
                                RadioButtonItem(destinations[i].first, selected = tab == i, shortcut = KeyShortcut(keys[i], ctrl = true),
                                                onClick = { goTab(i) })
                            Separator()
                            Item("Geri", enabled = nav.previousBackStackEntry != null, shortcut = KeyShortcut(Key.DirectionLeft, alt = true),
                                 onClick = { nav.popBackStack() })
                        }
                    }
                    Box(modifier = Modifier.fillMaxWidth().weight(1f)) { app() }
                }
            }
        }
    }
}

kompoz::Application kompoz::createApplication(int, char**) {
    return {.title = "Kompoz Showcase", .width = 440, .height = 900, .content = [] { ShowcaseApp(); }};
}
