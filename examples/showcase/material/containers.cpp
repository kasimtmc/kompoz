// Material screen: animated list items, ListItem and badges, dialogs, menus and sheets, cards.

#include "showcase.hpp"

@Kompozable
fun AnimateItemSection() {
    val tasks = remember { mutableStateListOf(1, 2, 3, 4, 5) }
    var nextTask by remember { mutableStateOf(6) }
    var shuffles by remember { mutableStateOf(1) }
    Section("animateItem · SwipeToDismissBox") {
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            FilledTonalButton(onClick = { tasks.shuffle(Random(shuffles++)) }) { Text("Karıştır") }
            FilledTonalButton(onClick = { tasks.add(0, nextTask++) }) { Text("Ekle") }
            FilledTonalButton(onClick = { if (tasks.isNotEmpty()) tasks.removeAt(0) }) { Text("İlkini sil") }  // it fades out where it was, the others glide up
            OutlinedButton(onClick = {
                tasks.clear()
                tasks.addAll(listOf(1, 2, 3, 4, 5))
                nextTask = 6
            }) { Text("Sıfırla") }
        }
        LazyColumn(modifier = Modifier.fillMaxWidth().height(330.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            items(tasks, key = { it }) { id -> SwipeableTask(id, tasks) }
        }
    }
}

@Kompozable
fun ListItemSection() {
    val cs = MaterialTheme.colorScheme
    var notifications by remember { mutableStateOf(true) }
    Section("ListItem ve Badge") {
        OutlinedCard(modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp))) {
            ListItem(headlineContent = { Text("Tek satır") },
                     trailingContent = { Switch(checked = notifications, onCheckedChange = { notifications = it }) })
            HorizontalDivider()
            ListItem(headlineContent = { Text("Ayşe Yılmaz") },
                     supportingContent = { Text("Toplantı saat 15:00'te") },
                     leadingContent = {
                         Box(modifier = Modifier.size(40.dp).background(cs.primaryContainer, CircleShape), contentAlignment = Alignment.Center) {
                             Text("AY", color = cs.onPrimaryContainer, style = MaterialTheme.typography.titleMedium)
                         }
                     },
                     trailingContent = { Text("10:42") },
                     modifier = Modifier.clickable { })
            HorizontalDivider()
            ListItem(headlineContent = { Text("Gelen kutusu") },
                     overlineContent = { Text("E-POSTA") },
                     supportingContent = { Text("Okunmamış iletiler ve bildirimler burada görünür.") },
                     leadingContent = {
                         BadgedBox(badge = { Badge { Text("8") } }) { Icon(Icons.Filled.Email, contentDescription = "Posta") }
                     },
                     trailingContent = {
                         BadgedBox(badge = { Badge() }) { Icon(Icons.Filled.Notifications, contentDescription = "Bildirim") }
                     })
        }
        // M3 Expressive: items that make one card, the row taking the tap, rounder when picked.
        Text("SegmentedListItem: tek kart gibi duran, seçilince yuvarlaklaşan satırlar", color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        var plan by remember { mutableStateOf(1) }
        val plans = listOf("Ücretsiz", "Standart", "Aile")
        val details = listOf("Reklamlı, tek cihaz", "Reklamsız, iki cihaz", "Reklamsız, altı kişi")
        Column(modifier = Modifier.fillMaxWidth().background(cs.surfaceContainer, RoundedCornerShape(20.dp)).padding(8.dp),
               verticalArrangement = Arrangement.spacedBy(ListItemDefaults.SegmentedGap)) {
            for (i in 0 until 3) {
                SegmentedListItem(selected = plan == i, onClick = { plan = i }, shapes = ListItemDefaults.segmentedShapes(i, 3),
                                  headlineContent = { Text(plans[i]) }, supportingContent = { Text(details[i]) },
                                  leadingContent = { Icon(Icons.Filled.Star, contentDescription = null) })
            }
        }
    }
}

@Kompozable
fun DialogsSection(nav: NavController) {
    val cs = MaterialTheme.colorScheme
    var dialog by remember { mutableStateOf(false) }
    var basicDialog by remember { mutableStateOf(false) }
    var sheet by remember { mutableStateOf(false) }
    var longSheet by remember { mutableStateOf(false) }
    val sheetState = rememberModalBottomSheetState()  // the sheet slides out from code, then goes
    val scope = rememberCoroutineScope()
    var menu by remember { mutableStateOf(false) }
    var choice by remember { mutableStateOf("yok") }
    var formatMenu by remember { mutableStateOf(false) }
    val format = remember { mutableStateListOf(true, false) }  // bold, italic
    var alignment by remember { mutableStateOf(0) }
    Section("Dialog, menü ve alt sayfa") {
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            FilledTonalButton(onClick = { dialog = true }) { Text("Dialog") }
            FilledTonalButton(onClick = { basicDialog = true }) { Text("BasicAlertDialog") }
            FilledTonalButton(onClick = { sheet = true }) { Text("Alt sayfa") }
            FilledTonalButton(onClick = { longSheet = true }) { Text("Uzun liste") }
            FilledTonalButton(onClick = { nav.navigate(routes.Places()) }) { Text("Harita (BottomSheetScaffold)") }
            Box {
                OutlinedButton(onClick = { menu = true }) {
                    Text("Menü")
                    Icon(Icons.Filled.KeyboardArrowDown, contentDescription = "Aç", modifier = Modifier.size(18.dp))
                }
                DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                    DropdownMenuItem(text = { Text("Düzenle") }, onClick = { choice = "Düzenle"; menu = false },
                                     leadingIcon = { Icon(Icons.Filled.Edit, contentDescription = "Düzenle") })
                    DropdownMenuItem(text = { Text("Paylaş") }, onClick = { choice = "Paylaş"; menu = false },
                                     leadingIcon = { Icon(Icons.Filled.Share, contentDescription = "Paylaş") })
                    HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))
                    DropdownMenuItem(text = { Text("Sil") }, onClick = { choice = "Sil"; menu = false },
                                     leadingIcon = { Icon(Icons.Filled.Delete, contentDescription = "Sil") }, trailingIcon = { Text("Del") })
                    DropdownMenuItem(text = { Text("Kapalı seçenek") }, onClick = { }, enabled = false)
                }
            }
            // An expressive menu: groups as cards, checked and selected items.
            Box {
                OutlinedButton(onClick = { formatMenu = true }) {
                    Text("Biçim")
                    Icon(Icons.Filled.KeyboardArrowDown, contentDescription = "Aç", modifier = Modifier.size(18.dp))
                }
                DropdownMenuPopup(expanded = formatMenu, onDismissRequest = { formatMenu = false }) {
                    DropdownMenuGroup(shape = MenuDefaults.groupShape(0, 3)) {
                        val styles = listOf("Kalın", "İtalik")
                        for (i in 0 until 2) {
                            DropdownMenuItem(checked = format[i], onCheckedChange = { format[i] = it }, text = { Text(styles[i]) },
                                             shape = MenuDefaults.itemShape(i, 2))
                        }
                    }
                    Spacer(modifier = Modifier.height(MenuDefaults.GroupSpacing))
                    DropdownMenuGroup(shape = MenuDefaults.groupShape(1, 3)) {
                        val sides = listOf("Sola hizala", "Ortala", "Sağa hizala")
                        for (i in 0 until 3) {
                            DropdownMenuItem(selected = alignment == i, onClick = { alignment = i }, text = { Text(sides[i]) },
                                             shape = MenuDefaults.itemShape(i, 3))
                        }
                    }
                    Spacer(modifier = Modifier.height(MenuDefaults.GroupSpacing))
                    DropdownMenuGroup(shape = MenuDefaults.groupShape(2, 3)) {
                        DropdownMenuItem(text = { Text("Yazı tipi") }, onClick = { choice = "Yazı tipi"; formatMenu = false },
                                         leadingIcon = { Icon(Icons.Filled.Settings, contentDescription = "Yazı tipi") },
                                         supportingText = { Text("Boyut ve aile") }, shape = MenuDefaults.itemShape(0, 1))
                    }
                }
            }
        }
        Text("Son seçim: $choice", color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        if (dialog) {
            AlertDialog(
                onDismissRequest = { dialog = false },
                confirmButton = { TextButton(onClick = { choice = "Dialog: Sil"; dialog = false }) { Text("Sil") } },
                dismissButton = { TextButton(onClick = { dialog = false }) { Text("Vazgeç") } },
                icon = { Icon(Icons.Filled.Delete, contentDescription = "Sil") },
                title = { Text("Taslak silinsin mi?") },
                text = { Text("Bu işlem geri alınamaz. Scrim'e dokunmak ya da geri tuşu da dialog'u kapatır.") },
            )
        }
        if (basicDialog) {  // content of its own, at least 280 dp wide
            BasicAlertDialog(onDismissRequest = { basicDialog = false }) {
                Column(modifier = Modifier.background(cs.surfaceContainerHigh, RoundedCornerShape(28.dp)).padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                    LoadingIndicator(modifier = Modifier.size(48.dp))
                    Text("Eşitleniyor…", modifier = Modifier.padding(vertical = 16.dp), style = MaterialTheme.typography.titleMedium)
                    Text("BasicAlertDialog: içeriği bizim; en az 280 dp genişlik ona da geçer.", color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
                    TextButton(onClick = { basicDialog = false }, modifier = Modifier.padding(top = 16.dp)) { Text("Kapat") }
                }
            }
        }
        if (sheet) {
            ModalBottomSheet(onDismissRequest = { sheet = false }, sheetState = sheetState) {
                Text("Paylaş", modifier = Modifier.padding(24.dp, 0.dp, 24.dp, 8.dp), style = MaterialTheme.typography.titleMedium)
                val actions = listOf(Icons.Filled.Share to "Bağlantıyı paylaş", Icons.Filled.Email to "E-posta ile gönder", Icons.Filled.Edit to "Düzenle")
                for ((icon, label) in actions) {
                    ListItem(headlineContent = { Text(label) }, leadingContent = { Icon(icon, contentDescription = label) },
                             colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                             modifier = Modifier.clickable {
                                 choice = label
                                 scope.launch {  // as Compose apps do: hide, then remove
                                     sheetState.hide()
                                     sheet = false
                                 }
                             })
                }
                Text("Aşağı sürükleyin, scrim'e dokunun ya da geri tuşuna basın.", modifier = Modifier.padding(24.dp, 8.dp, 24.dp, 16.dp),
                     color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
            }
        }
        // A list in a sheet: it scrolls first; pulled down at its top it moves the sheet.
        if (longSheet) {
            ModalBottomSheet(onDismissRequest = { longSheet = false }) {
                Text("Uzun liste: sayfa yarıya kadar açılır; yukarı itince önce sayfa tam açılır, sonra liste kayar; " +
                         "en üstte aşağı çekince sayfa iner",
                     modifier = Modifier.padding(24.dp, 0.dp, 24.dp, 8.dp), style = MaterialTheme.typography.titleSmall)
                LazyColumn(modifier = Modifier.fillMaxWidth().height(420.dp)) {
                    items(40) { i ->
                        val label = "Seçenek ${i + 1}"
                        ListItem(headlineContent = { Text(label) }, colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                                 modifier = Modifier.clickable { choice = label; longSheet = false })
                    }
                }
            }
        }
    }
}

@Kompozable
fun CardsSection() {
    val cs = MaterialTheme.colorScheme
    var cardTaps by remember { mutableStateOf(0) }
    Section("Kartlar ve BottomAppBar") {
        // Card(onClick): the ripple keeps to the card's corners.
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Card(onClick = { cardTaps++ }, modifier = Modifier.weight(1f).height(72.dp)) { Text("Card · $cardTaps", modifier = Modifier.padding(12.dp)) }
            ElevatedCard(onClick = { cardTaps++ }, modifier = Modifier.weight(1f).height(72.dp)) { Text("Elevated", modifier = Modifier.padding(12.dp)) }
            OutlinedCard(onClick = { cardTaps++ }, modifier = Modifier.weight(1f).height(72.dp)) { Text("Outlined", modifier = Modifier.padding(12.dp)) }
        }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
            SmallFloatingActionButton(onClick = { }) { Icon(Icons.Filled.Edit, contentDescription = "Küçük FAB") }
            FloatingActionButton(onClick = { }) { Icon(Icons.Filled.Edit, contentDescription = "FAB") }
            MediumFloatingActionButton(onClick = { }) { Icon(Icons.Filled.Edit, contentDescription = "Orta FAB", modifier = Modifier.size(28.dp)) }
            LargeFloatingActionButton(onClick = { }) { Icon(Icons.Filled.Edit, contentDescription = "Büyük FAB", modifier = Modifier.size(36.dp)) }
        }
        // M3 Expressive's extended FABs: switched off, they narrow to their icon.
        var fabsOpen by remember { mutableStateOf(true) }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Switch(checked = fabsOpen, onCheckedChange = { fabsOpen = it })
            Text("  Genişletilmiş", style = MaterialTheme.typography.bodyMedium)
        }
        Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
            SmallExtendedFloatingActionButton(onClick = { }, expanded = fabsOpen, icon = { Icon(Icons.Filled.Add, contentDescription = null) }, text = { Text("Yeni") })
            MediumExtendedFloatingActionButton(onClick = { }, expanded = fabsOpen, icon = { Icon(Icons.Filled.Edit, contentDescription = null) }, text = { Text("Yaz") })
            LargeExtendedFloatingActionButton(onClick = { }, expanded = fabsOpen, icon = { Icon(Icons.Filled.Email, contentDescription = null) }, text = { Text("Gönder") })
        }
        BottomAppBar(
            actions = {
                IconButton(onClick = { }) { Icon(Icons.Filled.Check, contentDescription = "Tamam") }
                IconButton(onClick = { }) { Icon(Icons.Filled.Edit, contentDescription = "Düzenle") }
                IconButton(onClick = { }) { Icon(Icons.Filled.Delete, contentDescription = "Sil") }
            },
            floatingActionButton = { FloatingActionButton(onClick = { }) { Icon(Icons.Filled.Add, contentDescription = "Ekle") } },
            containerColor = cs.surfaceContainerHighest,
            windowInsets = WindowInsets(0.dp),
            modifier = Modifier.clip(RoundedCornerShape(12.dp)),
        )
    }
}
