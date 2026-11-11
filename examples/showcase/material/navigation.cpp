// Material screen: tabs, the FAB menu, the short navigation bar, the wide navigation rail, floating toolbars.

#include "showcase.hpp"

// A place to go: its label and icon (the navigation bar's and rail's items, the FAB menu's actions).
data class Destination(val label: String, val icon: ImageVector)

@Kompozable
fun TabsSection() {
    val cs = MaterialTheme.colorScheme
    var tab by remember { mutableStateOf(0) }
    var subTab by remember { mutableStateOf(0) }
    var travel by remember { mutableStateOf(0) }
    var city by remember { mutableStateOf(0) }
    Section("Sekmeler") {
        val titles = listOf("Gelen", "Yıldızlı", "Ayarlar")
        val icons = listOf(Icons.Filled.Email, Icons.Filled.Star, Icons.Filled.Settings)
        PrimaryTabRow(selectedTabIndex = tab) {
            for (i in 0 until 3) {
                Tab(selected = tab == i, onClick = { tab = i }, text = { Text(titles[i]) }, icon = { Icon(icons[i], contentDescription = titles[i]) })
            }
        }
        SecondaryTabRow(selectedTabIndex = subTab) {
            for (i in 0 until 3) {
                Tab(selected = subTab == i, onClick = { subTab = i }, text = { Text(if (i == 0) "Tümü" else if (i == 1) "Yeni" else "Arşiv") })
            }
        }
        // LeadingIconTab: the icon beside the text, a lower row; unselected ones in
        // onSurfaceVariant (by default they take the selected color, as in Compose).
        val trips = listOf("Uçuş", "Otel", "Yer")
        val tripIcons = listOf(Icons.Filled.Send, Icons.Filled.Home, Icons.Filled.Place)
        PrimaryTabRow(selectedTabIndex = travel) {
            for (i in 0 until 3) {
                LeadingIconTab(selected = travel == i, onClick = { travel = i }, text = { Text(trips[i]) },
                               icon = { Icon(tripIcons[i], contentDescription = trips[i], modifier = Modifier.size(18.dp)) },
                               unselectedContentColor = cs.onSurfaceVariant)
            }
        }
        AnimatedContent(targetState = tab) { t ->
            val texts = listOf("3 yeni ileti var.", "Yıldızlı ileti yok.", "Bildirimler ve hesap ayarları.")
            Text(texts[t], modifier = Modifier.padding(4.dp, 8.dp), color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        }
        val cities = listOf("İstanbul", "Ankara", "İzmir", "Bursa", "Antalya", "Konya", "Adana", "Gaziantep", "Kayseri", "Trabzon", "Eskişehir", "Samsun")
        PrimaryScrollableTabRow(selectedTabIndex = city, edgePadding = 16.dp) {
            for (i in 0 until 12) Tab(selected = city == i, onClick = { city = i }, text = { Text(cities[i]) })
        }
        Text("PrimaryScrollableTabRow: ${cities[city]} — seçilen sekme ortaya kayar.", modifier = Modifier.padding(4.dp, 8.dp),
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
    }
}

@Kompozable
fun FabMenuSection() {
    val cs = MaterialTheme.colorScheme
    // M3 Expressive's FAB menu: its actions open above the button, which turns into a round close button.
    Section("FloatingActionButtonMenu") {
        var fabOpen by remember { mutableStateOf(false) }
        var fabPicked by remember { mutableStateOf("yok") }
        BackHandler(enabled = fabOpen) { fabOpen = false }
        Box(modifier = Modifier.fillMaxWidth().height(280.dp), contentAlignment = Alignment.BottomEnd) {
            Text("Seçilen: $fabPicked", modifier = Modifier.align(Alignment.BottomStart), color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
            FloatingActionButtonMenu(
                expanded = fabOpen,
                button = {
                    ToggleFloatingActionButton(checked = fabOpen, onCheckedChange = { fabOpen = it }) {
                        // The plus turns into a cross, as it is drawn.
                        Icon(Icons.Filled.Add, contentDescription = "Menü", modifier = Modifier.graphicsLayer { rotationZ = 45 * checkedProgress })
                    }
                },
            ) {
                val actions = listOf(Destination("Not", Icons.Filled.Edit), Destination("Konum", Icons.Filled.Place), Destination("E-posta", Icons.Filled.Email))
                for (a in actions) {
                    FloatingActionButtonMenuItem(onClick = { fabPicked = a.label; fabOpen = false }, text = { Text(a.label) },
                                                 icon = { Icon(a.icon, contentDescription = null) })
                }
            }
        }
    }
}

@Kompozable
fun ShortNavigationBarSection() {
    val cs = MaterialTheme.colorScheme
    // M3 Expressive's short navigation bar: items sharing the width (compact) or together in the middle (wider).
    Section("ShortNavigationBar") {
        var page by remember { mutableStateOf(0) }
        val tabs = listOf(Destination("Ana sayfa", Icons.Filled.Home), Destination("Ara", Icons.Filled.Search), Destination("Profil", Icons.Filled.Person))
        for (arrangement in listOf(ShortNavigationBarArrangement.EqualWeight, ShortNavigationBarArrangement.Centered)) {
            Text(if (arrangement == ShortNavigationBarArrangement.EqualWeight) "Eşit paylaşım" else "Ortada", color = cs.onSurfaceVariant, style = MaterialTheme.typography.labelLarge)
            ShortNavigationBar(modifier = Modifier.clip(RoundedCornerShape(16.dp)), arrangement = arrangement, windowInsets = WindowInsets(0.dp)) {
                for (i in 0 until 3) {
                    ShortNavigationBarItem(selected = page == i, onClick = { page = i }, icon = { Icon(tabs[i].icon, contentDescription = null) }, label = { Text(tabs[i].label) })
                }
            }
        }
    }
}

@Kompozable
fun WideNavigationRailSection() {
    val cs = MaterialTheme.colorScheme
    // M3 Expressive's wide navigation rail: ☰ widens it, the labels going after the icons.
    Section("WideNavigationRail") {
        val rail = rememberWideNavigationRailState()
        val scope = rememberCoroutineScope()
        var box by remember { mutableStateOf(0) }
        val boxes = listOf(Destination("Gelen", Icons.Filled.Email), Destination("Yıldızlı", Icons.Filled.Star), Destination("Ayarlar", Icons.Filled.Settings))
        // Modal: it opens over everything from the window's start edge, above a scrim.
        var modal by remember { mutableStateOf(false) }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Switch(checked = modal, onCheckedChange = {
                scope.launch { rail.collapse() }
                modal = it
            })
            Text("  Modal (ModalWideNavigationRail)", style = MaterialTheme.typography.bodyMedium)
        }
        Row(modifier = Modifier.fillMaxWidth().height(380.dp).clip(RoundedCornerShape(16.dp)).background(cs.surfaceContainerHigh)) {
            val items: @Kompozable () -> Unit = {
                for (i in 0 until 3) {
                    WideNavigationRailItem(selected = box == i, onClick = {
                                               box = i
                                               if (modal) scope.launch { rail.collapse() }  // a modal one closes once a destination is picked
                                           },
                                           icon = { Icon(boxes[i].icon, contentDescription = null) }, label = { Text(boxes[i].label) })
                }
            }
            val header: @Kompozable () -> Unit = {
                IconButton(onClick = { scope.launch { rail.toggle() } }) {
                    Icon(if (rail.isExpanded) Icons.AutoMirrored.Filled.ArrowBack else Icons.Filled.Menu, contentDescription = "Rayı aç/kapat")
                }
            }
            if (modal) {
                ModalWideNavigationRail(state = rail, header = header) { items() }
            } else {
                WideNavigationRail(state = rail, header = header, windowInsets = WindowInsets(0.dp)) { items() }
            }
            Box(modifier = Modifier.weight(1f).fillMaxHeight(), contentAlignment = Alignment.Center) { Text(boxes[box].label, style = MaterialTheme.typography.headlineSmall) }
        }
    }
}

@Kompozable
fun FloatingToolbarsSection() {
    val cs = MaterialTheme.colorScheme
    // M3 Expressive's floating toolbars over a list: they leave as it scrolls on and come back as it scrolls back.
    Section("Kayan araç çubukları") {
        val below = FloatingToolbarDefaults.exitAlwaysScrollBehavior(FloatingToolbarExitDirection.Bottom)
        val aside = FloatingToolbarDefaults.exitAlwaysScrollBehavior(FloatingToolbarExitDirection.End)
        var more by remember { mutableStateOf(false) }
        var tools by remember { mutableStateOf(false) }
        Box(modifier = Modifier.fillMaxWidth().height(340.dp).clip(RoundedCornerShape(16.dp)).background(cs.surfaceContainerHigh)
                .nestedScroll(below.nestedScrollConnection).nestedScroll(aside.nestedScrollConnection)) {
            LazyColumn(modifier = Modifier.fillMaxSize()) {
                items(40) { i -> Text("Satır ${i + 1}", modifier = Modifier.fillMaxWidth().padding(16.dp, 12.dp)) }
            }
            // The middle actions stay; ⋮ shows the ones on the sides.
            HorizontalFloatingToolbar(
                expanded = more,
                modifier = Modifier.align(Alignment.BottomCenter).offset(y = -FloatingToolbarDefaults.ScreenOffset),
                scrollBehavior = below,
                leadingContent = { IconButton(onClick = { }) { Icon(Icons.Filled.Favorite, contentDescription = "Beğen") } },
                trailingContent = { IconButton(onClick = { }) { Icon(Icons.Filled.Delete, contentDescription = "Sil") } },
            ) {
                IconButton(onClick = { }) { Icon(Icons.Filled.Edit, contentDescription = "Düzenle") }
                IconButton(onClick = { }) { Icon(Icons.Filled.Share, contentDescription = "Paylaş") }
                IconButton(onClick = { more = !more }) { Icon(Icons.Filled.MoreVert, contentDescription = "Daha fazla") }
            }
            // A vibrant one with a FAB: collapsed, only the FAB is left.
            VerticalFloatingToolbar(
                expanded = tools,
                floatingActionButton = {
                    FloatingToolbarDefaults.VibrantFloatingActionButton(onClick = { tools = !tools }) {
                        Icon(if (tools) Icons.Filled.Close else Icons.Filled.Add, contentDescription = "Araçlar")
                    }
                },
                modifier = Modifier.align(Alignment.TopEnd).offset(x = -FloatingToolbarDefaults.ScreenOffset, y = FloatingToolbarDefaults.ScreenOffset),
                colors = FloatingToolbarDefaults.vibrantFloatingToolbarColors(),
                scrollBehavior = aside,
            ) {
                IconButton(onClick = { }) { Icon(Icons.Filled.Place, contentDescription = "Konum") }
                IconButton(onClick = { }) { Icon(Icons.Filled.Email, contentDescription = "E-posta") }
            }
        }
    }
}
