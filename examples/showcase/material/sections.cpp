// Material screen: search, tooltips, menus, interactions, swipes, ripples, semantics, focus, pickers, dynamic color, insets.

#include "showcase.hpp"

// One task row that can be swiped away (left: delete, right: done).
@Composable
fun SwipeableTask(id: Int, tasks: SnapshotStateList<Int>) {
    val cs = MaterialTheme.colorScheme
    val state = rememberSwipeToDismissBoxState(confirmValueChange = {
        tasks.remove(id)  // the others glide into place (animateItem)
        true
    })
    SwipeToDismissBox(
        state = state,
        modifier = Modifier.animateItem(),
        backgroundContent = {
            val del = state.dismissDirection == SwipeToDismissBoxValue.EndToStart
            val color = if (state.dismissDirection == SwipeToDismissBoxValue.Settled) cs.surfaceContainerHigh else if (del) cs.error else Color(0xFF2E7D32)
            Box(modifier = Modifier.fillMaxSize().background(color, RoundedCornerShape(12.dp)).padding(20.dp, 0.dp),
                contentAlignment = if (del) Alignment.CenterEnd else Alignment.CenterStart) {
                Icon(if (del) Icons.Filled.Delete else Icons.Filled.Check, contentDescription = if (del) "Sil" else "Tamamla", tint = Color.White)
            }
        },
    ) {
        ListItem(headlineContent = { Text("Görev $id") }, supportingContent = { Text("Sola kaydır: sil · sağa: tamamla") },
                 leadingContent = {
                     Box(modifier = Modifier.size(36.dp).background(hueColor(id * 5), CircleShape), contentAlignment = Alignment.Center) {
                         Text("$id", fontWeight = FontWeight.Bold, color = Color.White)
                     }
                 },
                 colors = ListItemDefaults.colors(containerColor = cs.surface), modifier = Modifier.clip(RoundedCornerShape(12.dp)))
    }
}

// Lower case for matching, the Turkish way (İ -> i, I -> ı).
char32_t lowerTr(char32_t c) {
    if (c == U'I') return U'ı';
    if (c == U'İ') return U'i';
    if (c >= U'A' && c <= U'Z') return c + (U'a' - U'A');
    switch (c) {
    case U'Ç': return U'ç';
    case U'Ğ': return U'ğ';
    case U'Ö': return U'ö';
    case U'Ş': return U'ş';
    case U'Ü': return U'ü';
    default: return c;
    }
}

bool startsWithIgnoringCase(std::string_view text, std::string_view prefix) {
    size_t i = 0, j = 0;
    while (j < prefix.size()) {
        if (i >= text.size() || lowerTr(utf8::next(text, i)) != lowerTr(utf8::next(prefix, j))) return false;
    }
    return true;
}

// A date and a time as the pickers' sections show them (the month names LocalStrings' Turkish ones).
std::string formatDate(std::chrono::year_month_day d) {
    static const DatePickerStrings strings = DatePickerStrings::turkish();
    return std::format("{} {} {}", unsigned(d.day()), strings.months[unsigned(d.month()) - 1], int(d.year()));
}

std::string formatTime(int hour, int minute, bool twelveHour) {
    const Strings& strings = LocalStrings.current();  // (ÖÖ / ÖS)
    return twelveHour ? std::format("{:02}:{:02} {}", hour % 12 == 0 ? 12 : hour % 12, minute, hour < 12 ? strings.am : strings.pm)
                      : std::format("{:02}:{:02}", hour, minute);
}

private val cities = listOf("Adana", "Ankara", "Antalya", "Aydın", "Balıkesir", "Bursa", "Denizli", "Diyarbakır", "Edirne", "Erzurum",
                            "Eskişehir", "Gaziantep", "Hatay", "İstanbul", "İzmir", "Kayseri", "Kocaeli", "Konya", "Malatya", "Manisa",
                            "Mardin", "Mersin", "Muğla", "Ordu", "Rize", "Sakarya", "Samsun", "Trabzon", "Van", "Zonguldak")

// The same search, full screen (ExpandedFullScreenSearchBar) or opening in place
// (ExpandedDockedSearchBar), each a SearchBarState with its query in a TextFieldState.
@Composable
private fun CitySearchBar(docked: Boolean, state: SearchBarState, query: TextFieldState, chosen: MutableState<String>) {
    val scope = rememberCoroutineScope()
    val close: () -> Unit = { scope.launch { state.animateToCollapsed() } }  // (typed: GCC 16 crashes on a lambda returning the launch's job)
    val expanded = state.targetValue == SearchBarValue.Expanded
    val field: @Composable () -> Unit = {
        SearchBarDefaults.InputField(
            searchBarState = state, textFieldState = query,
            onSearch = { chosen.value = it; close() },
            placeholder = { Text(if (docked) "Şehir ara (yerinde)" else "Şehir ara") },
            leadingIcon = {
                if (expanded && !docked) {
                    IconButton(onClick = { close() }) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Kapat") }
                } else {
                    Icon(Icons.Filled.Search, contentDescription = "Ara")
                }
            },
            trailingIcon = {
                if (query.text.isNotEmpty()) {
                    IconButton(onClick = { query.clearText() }) { Icon(Icons.Filled.Close, contentDescription = "Temizle") }
                }
            },
        )
    }
    val suggestions: @Composable () -> Unit = {
        Column(modifier = Modifier.fillMaxWidth().verticalScroll(rememberScrollState())) {
            val typed = query.text
            for (city in cities) {
                if (!startsWithIgnoringCase(city, typed)) continue
                ListItem(headlineContent = { Text(city) }, leadingContent = { Icon(Icons.Filled.Place, contentDescription = "Şehir") },
                         colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                         modifier = Modifier.clickable {
                             query.setTextAndPlaceCursorAtEnd(city)
                             chosen.value = city
                             close()
                         })
            }
        }
    }
    SearchBar(state = state, inputField = field, modifier = Modifier.fillMaxWidth())
    if (docked) ExpandedDockedSearchBar(state = state, inputField = field) { suggestions() }
    else ExpandedFullScreenSearchBar(state = state, inputField = field) { suggestions() }
}

// A search bar over a list of cities: the suggestions narrow as you type (in either case).
@Composable
fun SearchSection() {
    val cs = MaterialTheme.colorScheme
    val chosen = remember { mutableStateOf("") }
    val state = rememberSearchBarState()
    val query = rememberTextFieldState()
    val dockedState = rememberSearchBarState()
    val dockedQuery = rememberTextFieldState()
    Section("SearchBar · SearchBarState") {
        key(0) { CitySearchBar(false, state, query, chosen) }  // the same composables twice: keyed apart
        key(1) { CitySearchBar(true, dockedState, dockedQuery, chosen) }
        Text(if (chosen.value.isEmpty()) "Üstteki tam ekran açılır, alttaki yerinde; yazdıkça öneriler daralır." else "Seçilen: ${chosen.value}",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
    }
}

// TooltipBox: a long press (or a resting mouse) shows the tooltip; the rich one stays. The share
// and info tooltips point at their buttons with a caret.
@Composable
fun TooltipSection() {
    val cs = MaterialTheme.colorScheme
    var liked by remember { mutableStateOf(false) }
    var side by remember { mutableStateOf(0) }  // where the share tooltip goes
    val sides = listOf(TooltipAnchorPosition.Above to "Üst", TooltipAnchorPosition.Below to "Alt", TooltipAnchorPosition.Start to "Baş", TooltipAnchorPosition.End to "Son")
    val scope = rememberCoroutineScope()
    Section("TooltipBox · PlainTooltip · RichTooltip · TooltipAnchorPosition") {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            TooltipBox(positionProvider = TooltipDefaults.rememberTooltipPositionProvider(), tooltip = { PlainTooltip { Text("Favorilere ekle") } },
                       state = rememberTooltipState()) {
                IconButton(onClick = { liked = !liked }) {
                    Icon(if (liked) Icons.Filled.Favorite else Icons.Filled.FavoriteBorder, contentDescription = "Favori")
                }
            }
            TooltipBox(positionProvider = TooltipDefaults.rememberTooltipPositionProvider(sides[side].first),
                       tooltip = { PlainTooltip(caretSize = TooltipDefaults.caretSize) { Text("Paylaş") } }, state = rememberTooltipState()) {
                IconButton(onClick = { }) { Icon(Icons.Filled.Share, contentDescription = "Paylaş") }
            }
            val rich = rememberTooltipState(false, true)
            TooltipBox(
                positionProvider = TooltipDefaults.rememberTooltipPositionProvider(),
                tooltip = {
                    RichTooltip(title = { Text("Yedekleme") }, action = { TextButton(onClick = { scope.launch { rich.dismiss() } }) { Text("Anladım") } },
                                caretSize = TooltipDefaults.caretSize) {
                        Text("Fotoğraflarınız her gece Wi-Fi'ye bağlıyken yedeklenir.")
                    }
                },
                state = rich,
            ) {
                IconButton(onClick = { scope.launch { rich.show() } }) { Icon(Icons.Filled.Info, contentDescription = "Bilgi") }
            }
        }
        // TooltipAnchorPosition: the share tooltip's side.
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for (i in 0 until 4) FilterChip(selected = side == i, onClick = { side = i }, label = { Text(sides[i].second) })
        }
        Text("Telefonda uzun basın, masaüstünde fareyle üzerinde bekleyin; bilgi düğmesi kalıcı ipucunu açar. Paylaş ve bilgi " +
                 "ipuçlarının oku düğmeyi gösterir; paylaş ipucu yukarıda seçilen yanda açılır (yer yoksa karşı yanda).",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
    }
}

private val fruits = listOf("Elma", "Armut", "Kiraz", "Muz", "Çilek", "Karpuz", "Kayısı", "Şeftali", "Üzüm", "İncir", "Nar", "Ayva")

// ExposedDropdownMenuBox: a read-only field that picks from a menu, and one that filters as you type.
@Composable
fun DropdownSection() {
    var open by remember { mutableStateOf(false) }
    var fruit by remember { mutableStateOf("Elma") }
    var searching by remember { mutableStateOf(false) }
    var query by remember { mutableStateOf("") }
    Section("ExposedDropdownMenuBox") {
        ExposedDropdownMenuBox(expanded = open, onExpandedChange = { open = it }, modifier = Modifier.fillMaxWidth()) {
            TextField(value = fruit, onValueChange = { }, modifier = Modifier.fillMaxWidth(), label = "Meyve",
                      trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(open) }, readOnly = true)
            ExposedDropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                for (f in fruits) DropdownMenuItem(text = { Text(f) }, onClick = { fruit = f; open = false })
            }
        }
        ExposedDropdownMenuBox(expanded = searching, onExpandedChange = { searching = it }, modifier = Modifier.fillMaxWidth()) {
            OutlinedTextField(value = query, onValueChange = { query = it; searching = true }, modifier = Modifier.fillMaxWidth(),
                              singleLine = true, label = "Meyve ara (yazdıkça süzülür)",
                              trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(searching) })
            ExposedDropdownMenu(expanded = searching, onDismissRequest = { searching = false }) {
                for (f in fruits) {
                    if (startsWithIgnoringCase(f, query)) DropdownMenuItem(text = { Text(f) }, onClick = { query = f; searching = false })
                }
            }
        }
    }
}

// InteractionSource: what the user is doing to a component, read as state (a button that
// shrinks while held, a card with its own pressed and hover looks instead of the overlay) or
// heard one by one (the button's log; a read-only field that opens a date picker when tapped).
// A pager that moves on by itself waits while the user holds it (collectIsDraggedAsState).
@Composable
fun InteractionSection() {
    val cs = MaterialTheme.colorScheme
    val button = remember { MutableInteractionSource() }
    val card = remember { MutableInteractionSource() }
    val field = remember { MutableInteractionSource() }
    val log = remember { mutableStateListOf<String>() }
    var pickerOpen by remember { mutableStateOf(false) }
    var date by remember { mutableStateOf<LocalDate?>(null) }
    LaunchedEffect(button) {
        val names = listOf("Press", "Release", "Cancel", "HoverEnter", "HoverExit", "Focus", "Unfocus", "DragStart", "DragStop", "DragCancel")
        button.interactions.collect { i ->
            log.add(names[i.type.ordinal])
            if (log.size > 4) log.removeAt(0)
        }
    }
    LaunchedEffect(field) {
        field.interactions.collect { i -> if (i.type == Interaction.Type.Release) pickerOpen = true }
    }
    val pager = rememberPagerState(pageCount = { 4 })
    val held by pager.interactionSource.collectIsDraggedAsState()
    LaunchedEffect(held) {
        if (!held) {  // waits while held; let go, it starts counting again
            while (true) {
                delay(2500)
                pager.animateScrollToPage((pager.currentPage + 1) % pager.pageCount)
            }
        }
    }
    val pressed by button.collectIsPressedAsState()
    val scale by animateFloatAsState(if (pressed) 0.92f else 1f, animationSpec = tween(120))
    val cardHovered by card.collectIsHoveredAsState()
    val cardPressed by card.collectIsPressedAsState()
    Section("InteractionSource · collectIsPressedAsState · collectIsDraggedAsState") {
        Text("Bileşenler basmayı, fareyle üzerine gelmeyi, odağı ve sürüklemeyi bir MutableInteractionSource'a söyler; " +
                 "ekran bunları durum olarak okur ya da tek tek dinler.",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        Row(modifier = Modifier.fillMaxWidth().padding(top = 12.dp), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Button(onClick = { }, modifier = Modifier.scale(scale), interactionSource = button) { Text(if (pressed) "Basılı tutuluyor" else "Basılı tut") }
            Text(log.joinToString(" · ").ifEmpty { "olay yok" }, modifier = Modifier.weight(1f), color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
        }
        // Its own looks instead of the overlay (indication = null).
        Box(modifier = Modifier.fillMaxWidth().padding(top = 12.dp).height(64.dp)
                .background(if (cardPressed) cs.primaryContainer else cs.surfaceContainerHigh, RoundedCornerShape(16.dp))
                .border(if (cardHovered) 2.dp else 1.dp, if (cardHovered) cs.primary else cs.outlineVariant, RoundedCornerShape(16.dp))
                .clickable(interactionSource = card, indication = null) { },
            contentAlignment = Alignment.Center) {
            Text(if (cardPressed) "Basılı: kendi görünümü, kaplama yok" else if (cardHovered) "Fare üzerinde" else "Üzerine gelin ya da basın",
                 color = if (cardPressed) cs.onPrimaryContainer else cs.onSurface, style = MaterialTheme.typography.bodyMedium)
        }
        OutlinedTextField(value = if (date != null) formatDate(date!!) else "", onValueChange = { }, modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
                          singleLine = true, placeholder = "Seçmek için dokunun", label = "Doğum tarihi",
                          trailingIcon = { Icon(Icons.Filled.DateRange, contentDescription = null) }, readOnly = true, interactionSource = field)
        if (pickerOpen) {
            val state = rememberDatePickerState(date)
            val close = { pickerOpen = false }
            DatePickerDialog(
                onDismissRequest = close,
                confirmButton = { TextButton(onClick = { date = state.selectedDate; close() }) { Text("Tamam") } },
                dismissButton = { TextButton(onClick = close) { Text("İptal") } },
            ) { DatePicker(state = state) }
        }
        val pages = listOf(Color(0xFF6750A4), Color(0xFF7D5260), Color(0xFF386A20), Color(0xFF00639B))
        HorizontalPager(state = pager, modifier = Modifier.fillMaxWidth().padding(top = 12.dp).height(96.dp)) { page ->
            Box(modifier = Modifier.fillMaxSize().padding(4.dp, 0.dp).background(pages[page], RoundedCornerShape(16.dp)), contentAlignment = Alignment.Center) {
                Text(if (held) "tutuluyor: bekliyor" else "Sayfa ${page + 1} · kendiliğinden ilerler", color = Color.White, style = MaterialTheme.typography.titleSmall)
            }
        }
    }
}

// anchoredDraggable: a handle that rests at three places, and rows that swipe open to their
// actions (a row's anchors are known from the start; the handle's come from its track's size).
enum class Spot { Start, Middle, End }

enum class RowReveal { Closed, Open }

@Composable
fun RevealRow(title: String, onAction: (String) -> Unit) {
    val cs = MaterialTheme.colorScheme
    val state = remember {
        AnchoredDraggableState<RowReveal>(RowReveal.Closed, DraggableAnchors {
            RowReveal.Closed at 0f
            RowReveal.Open at -144f
        })
    }
    val scope = rememberCoroutineScope()
    val act: (String) -> Unit = { what ->
        onAction("$what: $title")
        scope.launch { state.animateTo(RowReveal.Closed) }
    }
    Box(modifier = Modifier.fillMaxWidth().height(64.dp).clip(RoundedCornerShape(12.dp))) {
        Row(modifier = Modifier.align(Alignment.CenterEnd).fillMaxHeight()) {
            Box(modifier = Modifier.width(72.dp).fillMaxHeight().background(cs.tertiaryContainer).clickable { act("yıldızlandı") }, contentAlignment = Alignment.Center) {
                Icon(Icons.Filled.Star, contentDescription = "Yıldızla", tint = cs.onTertiaryContainer)
            }
            Box(modifier = Modifier.width(72.dp).fillMaxHeight().background(cs.error).clickable { act("silindi") }, contentAlignment = Alignment.Center) {
                Icon(Icons.Filled.Delete, contentDescription = "Sil", tint = cs.surface)
            }
        }
        Box(modifier = Modifier.fillMaxSize().offset { IntOffset(state.requireOffset().roundToInt(), 0) }
                .anchoredDraggable(state, Orientation.Horizontal).background(cs.surfaceContainerHighest).padding(16.dp, 0.dp),
            contentAlignment = Alignment.CenterStart) {
            Text(title + (if (state.targetValue == RowReveal.Open) "  ← açık" else "  ← sola kaydırın"), style = MaterialTheme.typography.bodyLarge)
        }
    }
}

@Composable
fun AnchoredDraggableSection() {
    val cs = MaterialTheme.colorScheme
    val names = listOf("Start", "Middle", "End")
    val spot = rememberAnchoredDraggableState(Spot.Start)
    val scope = rememberCoroutineScope()
    var lastAction by remember { mutableStateOf("") }
    Section("anchoredDraggable · AnchoredDraggableState") {
        Text("Tutamak üç yerden birinde durur: yarı yoldan fazla sürüklenir ya da fırlatılırsa sonrakine, yoksa geri gider. " +
                 "Satırlar sola kaydırılınca eylemleri açılır.",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        Box(modifier = Modifier.fillMaxWidth().padding(top = 12.dp).height(56.dp).background(cs.surfaceContainerHighest, RoundedCornerShape(28.dp)).onSizeChanged { s ->
            spot.updateAnchors(DraggableAnchors {
                Spot.Start at 4f
                Spot.Middle at (s.width - 48) / 2f
                Spot.End at s.width - 52f
            })
        }) {
            Box(modifier = Modifier.offset { IntOffset(spot.requireOffset().roundToInt(), 4) }.anchoredDraggable(spot, Orientation.Horizontal)
                    .size(48.dp).background(cs.primary, CircleShape),
                contentAlignment = Alignment.Center) {
                Icon(Icons.Filled.KeyboardArrowRight, contentDescription = "Tutamak", tint = cs.onPrimary)
            }
        }
        Text("currentValue: ${names[spot.currentValue.ordinal]} · targetValue: ${names[spot.targetValue.ordinal]}",
             modifier = Modifier.padding(top = 8.dp), style = MaterialTheme.typography.bodyMedium)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for (target in listOf(Spot.Start, Spot.Middle, Spot.End)) {
                TextButton(onClick = { scope.launch { spot.animateTo(target) } }) { Text(names[target.ordinal]) }
            }
        }
        Column(modifier = Modifier.fillMaxWidth().padding(top = 8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            for (title in listOf("Fatura hatırlatması", "Toplantı notları")) {
                key(title) { RevealRow(title) { what -> lastAction = what } }
            }
        }
        if (lastAction.isNotEmpty()) Text(lastAction, color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
    }
}

// A control and its label under it, 76 dp wide (the ripple samples).
@Composable
private fun Labelled(label: String, content: @Composable () -> Unit) {
    Column(modifier = Modifier.width(76.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        content()
        Text(label, modifier = Modifier.padding(top = 6.dp), color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center,
             style = MaterialTheme.typography.bodySmall)
    }
}

// Ripples (Compose's indication): every clickable has Material's; ripple() makes it unbounded,
// sized or colored, a flat state layer or none instead; LocalIndication sets it for a subtree.
@Composable
fun RippleSection() {
    val cs = MaterialTheme.colorScheme
    Section("Ripple · indication · LocalIndication") {
        Text("Her tıklanabilirde Material dalgası var: basılan yerden köşelere yayılır, bırakınca söner. ripple() onu sınırsız, " +
                 "yarıçaplı ya da renkli yapar; LocalIndication bir alt ağaç için değiştirir.",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        Box(modifier = Modifier.fillMaxWidth().padding(top = 12.dp).height(96.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(16.dp)).clickable { },
            contentAlignment = Alignment.Center) { Text("Bir köşesine basın", style = MaterialTheme.typography.bodyLarge) }
        // indication(source, ...): the row takes the press without a ripple of its own; the icon shows it.
        val rowSource = remember { MutableInteractionSource() }
        Row(modifier = Modifier.fillMaxWidth().padding(top = 12.dp).height(56.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(16.dp))
                .clickable(interactionSource = rowSource, indication = null) { }.padding(12.dp, 0.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(modifier = Modifier.size(40.dp).clip(CircleShape).indication(rowSource, ripple()), contentAlignment = Alignment.Center) {
                Icon(Icons.Filled.Notifications, contentDescription = "Bildirim")
            }
            Text("Satıra basın: dalga yalnızca simgede (indication)", style = MaterialTheme.typography.bodyMedium)
        }
        Row(modifier = Modifier.fillMaxWidth().padding(top = 12.dp), horizontalArrangement = Arrangement.SpaceBetween) {
            Labelled("sınırsız") {
                Box(modifier = Modifier.size(64.dp).clickable(indication = ripple(false, 28.dp)) { }, contentAlignment = Alignment.Center) {
                    Icon(Icons.Filled.Favorite, contentDescription = "Sınırsız")
                }
            }
            Labelled("renkli") {
                Box(modifier = Modifier.size(64.dp).background(cs.secondaryContainer, RoundedCornerShape(16.dp)).clickable(indication = ripple(true, 0.dp, cs.primary)) { },
                    contentAlignment = Alignment.Center) { Icon(Icons.Filled.Star, contentDescription = "Renkli") }
            }
            Labelled("düz katman") {
                Box(modifier = Modifier.size(64.dp).background(cs.secondaryContainer, RoundedCornerShape(16.dp)).clickable(indication = Indication.Kind.StateLayer) { },
                    contentAlignment = Alignment.Center) { Icon(Icons.Filled.Check, contentDescription = "Düz") }
            }
            Labelled("yok (Local)") {
                CompositionLocalProvider(LocalIndication provides false) {
                    Box(modifier = Modifier.size(64.dp).background(cs.secondaryContainer, RoundedCornerShape(16.dp)).clickable { }, contentAlignment = Alignment.Center) {
                        Icon(Icons.Filled.Close, contentDescription = "Yok")
                    }
                }
            }
        }
    }
}

// Semantics: what accessibility services (TalkBack) read — a row as one, five star icons as
// one sentence, a decoration not at all.
@Composable
fun SemanticsSection() {
    val cs = MaterialTheme.colorScheme
    var unread by remember { mutableStateOf(3) }
    var lastAction by remember { mutableStateOf("—") }
    Section("Erişilebilirlik: merge · clearAndSet · hide · customActions · liveRegion") {
        Row(modifier = Modifier.fillMaxWidth().semantics(mergeDescendants = true) { },
            horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Filled.Place, contentDescription = "Konum", tint = cs.primary)
            Column(modifier = Modifier.weight(1f)) {
                Text("İstanbul", style = MaterialTheme.typography.titleSmall)
                Text("Parçalı bulutlu, 18°", color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
            }
            IconButton(onClick = { }) { Icon(Icons.Filled.Refresh, contentDescription = "Yenile") }  // still on its own
        }
        Row(modifier = Modifier.clearAndSetSemantics { contentDescription = "Puan: 4 / 5" }) {
            for (i in 0 until 5) Icon(Icons.Filled.Star, contentDescription = "yıldız", tint = if (i < 4) cs.primary else cs.outlineVariant)
        }
        Text("• • •", modifier = Modifier.hideFromAccessibility(), color = cs.outlineVariant)
        // customActions: the row's own actions, in TalkBack's actions menu (no buttons for them).
        ListItem(headlineContent = { Text("Ayşe: Toplantı yarın 10'da") }, supportingContent = { Text("Son eylem: $lastAction") },
                 leadingContent = { Icon(Icons.Filled.Email, contentDescription = null) },
                 modifier = Modifier.semantics(mergeDescendants = true) {
                     customActions = listOf(
                         CustomAccessibilityAction("Arşivle") { lastAction = "arşivlendi"; true },
                         CustomAccessibilityAction("Okundu say") {
                             lastAction = "okundu"
                             unread = maxOf(0, unread - 1)
                             true
                         },
                     )
                 })
        // liveRegion: a change to the count is read out without moving the focus there.
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("$unread okunmamış ileti", modifier = Modifier.weight(1f).semantics { liveRegion = LiveRegionMode.Polite }, style = MaterialTheme.typography.bodyLarge)
            OutlinedButton(onClick = { unread++ }) { Text("Yeni ileti") }
        }
        Text("TalkBack açıkken: ilk satır tek parça okunur (Yenile ayrı), yıldızlar \"Puan: 4 / 5\" diye, noktalar hiç okunmaz; " +
                 "ileti satırının eylemler menüsünde \"Arşivle\" ve \"Okundu say\" var; \"Yeni ileti\"ye basınca sayı odak değişmeden okunur. " +
                 "Kaydırıcılar ve ilerleme çubukları değerlerini yüzde olarak söyler, kaydırıcının değeri ses tuşlarıyla değişir.",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
    }
}

// focusProperties: "Bir" sends Tab straight to "Dört", "İki" never takes the focus. A keyboard's
// Tab moves it; without one, the buttons below do (a tap ends the keyboard focus, so they first
// give it back to the button that had it, then move it a frame later).
@Composable
fun FocusPropertiesDemo() {
    val requesters = remember { List(4) { FocusRequester() } }
    var focused by remember { mutableStateOf(-1) }
    var last by remember { mutableStateOf(0) }  // the button that had the focus last
    val scope = rememberCoroutineScope()
    val focus = LocalFocusManager.current
    val names = listOf("Bir", "İki", "Üç", "Dört")
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        for (i in 0 until 4) {
            OutlinedButton(onClick = { }, modifier = Modifier
                               .focusRequester(requesters[i])
                               .focusProperties {
                                   if (i == 0) next = requesters[3]
                                   if (i == 1) canFocus = false
                               }
                               .onFocusChanged { s ->
                                   if (s.isFocused) { focused = i; last = i }
                                   else if (focused == i) focused = -1
                               }) { Text(names[i]) }
        }
    }
    Text("Odakta: ${if (focused >= 0) names[focused] else "—"} · İki odak almaz, Bir'den Tab Dört'e gider",
         color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        TextButton(onClick = { requesters[0].requestFocus() }) { Text("Bir'e odaklan") }
        TextButton(onClick = {
            requesters[last].requestFocus()
            scope.launch { delay(50); focus.moveFocus(FocusDirection.Previous) }
        }) { Text("Önceki") }
        TextButton(onClick = {
            requesters[last].requestFocus()
            scope.launch { delay(50); focus.moveFocus(FocusDirection.Next) }
        }) { Text("Sonraki (Tab)") }
    }
}

// selectable / toggleable rows: the whole row takes the tap (and reads as one radio button or
// switch to accessibility services), the controls in it have no callback of their own.
// matchParentSize: a gradient as big as the text in front of it made the box.
@Composable
fun SelectableRowsSection() {
    val cs = MaterialTheme.colorScheme
    var choice by remember { mutableStateOf(1) }
    var notify by remember { mutableStateOf(true) }
    val options = listOf("Sabah", "Öğle", "Akşam")
    Section("selectable · toggleable · selectableGroup · matchParentSize") {
        Column(modifier = Modifier.fillMaxWidth().selectableGroup()) {
            for (i in 0 until 3) {
                Row(modifier = Modifier.fillMaxWidth().height(52.dp).selectable(selected = choice == i, onClick = { choice = i }, role = Role.RadioButton).padding(8.dp, 0.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                    RadioButton(selected = choice == i, onClick = null)
                    Text(options[i], style = MaterialTheme.typography.bodyLarge)
                }
            }
        }
        Row(modifier = Modifier.fillMaxWidth().height(56.dp).toggleable(value = notify, onValueChange = { notify = it }, role = Role.Switch).padding(8.dp, 0.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text("Hatırlatmalar", modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge)
            // An icon in the thumb (a tick on, a cross off); the thumb grows while held.
            Switch(checked = notify, onCheckedChange = null, thumbContent = { Icon(if (notify) Icons.Filled.Check else Icons.Filled.Close, contentDescription = null, modifier = Modifier.size(16.dp)) })
        }
        Box(modifier = Modifier.padding(top = 8.dp)) {
            Box(modifier = Modifier.matchParentSize().background(Brush.horizontalGradient(listOf(cs.primaryContainer, cs.tertiaryContainer)), RoundedCornerShape(12.dp)))
            Text("${options[choice]} · hatırlatma ${if (notify) "açık" else "kapalı"}", modifier = Modifier.padding(16.dp, 10.dp),
                 color = cs.onPrimaryContainer, style = MaterialTheme.typography.titleSmall)
        }
    }
}

// DatePickerDialog: the picked date shows under the button; the dialog opens on it.
@Composable
fun DatePickerSection() {
    val cs = MaterialTheme.colorScheme
    var open by remember { mutableStateOf(false) }
    var picked by remember { mutableStateOf<LocalDate?>(null) }
    var rangeOpen by remember { mutableStateOf(false) }
    var range by remember { mutableStateOf<Pair<LocalDate, LocalDate>?>(null) }
    Section("DatePicker · DateRangePicker") {
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Button(onClick = { open = true }) {
                Icon(Icons.Filled.DateRange, contentDescription = null, modifier = Modifier.size(18.dp))
                Text("Tarih seç")
            }
            OutlinedButton(onClick = { rangeOpen = true }) { Text("Aralık seç") }
        }
        var text = "Henüz tarih seçilmedi."
        if (picked != null) text = "Seçilen: ${formatDate(picked!!)}"
        if (range != null) text += "\nAralık: ${formatDate(range!!.first)} – ${formatDate(range!!.second)}"
        Text(text, color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        if (rangeOpen) {
            val state = rememberDateRangePickerState(range?.first, range?.second)
            val close = { rangeOpen = false }
            DatePickerDialog(
                onDismissRequest = close,
                confirmButton = {
                    val start = state.selectedStartDate
                    val end = state.selectedEndDate
                    TextButton(onClick = { range = Pair(state.selectedStartDate!!, state.selectedEndDate!!); close() }, enabled = start != null && end != null) { Text("Tamam") }
                },
                dismissButton = { TextButton(onClick = close) { Text("İptal") } },
            ) { DateRangePicker(state = state) }
        }
        if (open) {
            val state = rememberDatePickerState(picked)
            val close = { open = false }
            DatePickerDialog(
                onDismissRequest = close,
                confirmButton = { TextButton(onClick = { picked = state.selectedDate; close() }) { Text("Tamam") } },
                dismissButton = { TextButton(onClick = close) { Text("İptal") } },
            ) { DatePicker(state = state) }
        }
    }
}

// TimePickerDialog: the dial or, with the keyboard button, two number fields.
@Composable
fun TimePickerSection() {
    val cs = MaterialTheme.colorScheme
    var open by remember { mutableStateOf(false) }
    var picked by remember { mutableStateOf<Pair<Int, Int>?>(null) }
    var twelveHour by remember { mutableStateOf(false) }
    Section("TimePicker") {
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Button(onClick = { open = true }) {
                Icon(Icons.Filled.Schedule, contentDescription = null, modifier = Modifier.size(18.dp))
                Text("Saat seç")
            }
            FilterChip(selected = twelveHour, onClick = { twelveHour = !twelveHour }, label = { Text("12 saat") })
        }
        Text(if (picked != null) "Seçilen: ${formatTime(picked!!.first, picked!!.second, twelveHour)}" else "Henüz saat seçilmedi.",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        if (open) {
            val start = picked ?: Pair(12, 0)
            val state = rememberTimePickerState(start.first, start.second, !twelveHour)
            var typing by remember { mutableStateOf(false) }
            val close = { open = false }
            TimePickerDialog(
                onDismissRequest = close,
                title = { Text(if (typing) "Saati girin" else "Saat seçin") },
                confirmButton = { TextButton(onClick = { picked = Pair(state.hour, state.minute); close() }) { Text("Tamam") } },
                dismissButton = { TextButton(onClick = close) { Text("İptal") } },
                modeToggleButton = {
                    IconButton(onClick = { typing = !typing }) {
                        Icon(if (typing) Icons.Filled.Schedule else Icons.Filled.Keyboard, contentDescription = if (typing) "Saat kadranı" else "Klavyeyle gir")
                    }
                },
            ) {
                if (typing) TimeInput(state = state) else TimePicker(state = state)
            }
        }
    }
}

// Material You: the switch and the system's palettes (Android 12+).
@Composable
fun DynamicColorSection(source: MutableState<ColorSource>) {
    val cs = MaterialTheme.colorScheme
    val system = LocalDynamicTonalPalette.current
    val wallpaper = LocalWallpaperTonalPalette.current
    Section("Dinamik renk (Material You)") {
        // Three sources, each its own API: which one is the app's (or its user's) choice.
        SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
            val choices = listOf(ColorSource.App to "Uygulama", ColorSource.System to "Sistem", ColorSource.Wallpaper to "Duvar kağıdı")
            for (i in 0 until 3) {
                SegmentedButton(selected = source.value == choices[i].first, onClick = { source.value = choices[i].first },
                                shape = SegmentedButtonDefaults.itemShape(i, 3), modifier = Modifier.weight(1f)) { Text(choices[i].second) }
            }
        }
        val palette: TonalPalette? = if (source.value == ColorSource.System) system else if (source.value == ColorSource.Wallpaper) wallpaper else null
        Text(if (source.value == ColorSource.App) "lightColorScheme / darkColorScheme: uygulamanın kendi renkleri"
             else if (source.value == ColorSource.System) (if (system != null) "dynamicLightColorScheme: sistemin vurgu rengi (Android 12+, Linux, Windows)" else "Bu sistem vurgu rengi vermiyor; uygulamanın renkleri kullanılıyor")
             else if (wallpaper != null) "wallpaperLightColorScheme: duvar kağıdından seçilen renkler"
             else "Duvar kağıdı okunamadı; uygulamanın renkleri kullanılıyor",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
        if (palette == null) return
        // The five key colors in their 13 system tones (white .. black).
        val rows = listOf("accent1" to palette.accent1, "accent2" to palette.accent2, "accent3" to palette.accent3, "neutral1" to palette.neutral1, "neutral2" to palette.neutral2)
        for ((name, tones) in rows) {
            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(name, modifier = Modifier.width(72.dp), color = cs.onSurfaceVariant, style = MaterialTheme.typography.labelMedium)
                for (c in tones) Box(modifier = Modifier.weight(1f).height(20.dp).background(c))
            }
        }
    }
}

// The section on its own, in the colors of the source picked in it (KOMPOZ_PREVIEW=DynamicColorPreview).
@Preview(widthDp = 420, heightDp = 330)
@Composable
fun DynamicColorPreview() {
    val source = remember { mutableStateOf(ColorSource.Wallpaper) }
    val dark = isSystemInDarkTheme()
    val scheme = if (source.value == ColorSource.System && isDynamicColorAvailable()) (if (dark) dynamicDarkColorScheme() else dynamicLightColorScheme())
                 else if (source.value == ColorSource.Wallpaper && isWallpaperColorAvailable()) (if (dark) wallpaperDarkColorScheme() else wallpaperLightColorScheme())
                 else (if (dark) darkColorScheme() else lightColorScheme())
    MaterialTheme(colorScheme = scheme) {
        Box(modifier = Modifier.fillMaxSize().background(MaterialTheme.colorScheme.surface).padding(16.dp)) { DynamicColorSection(source) }
    }
}

// imeNestedScroll: a chat whose keyboard comes up as the messages are pulled on past their end
// (and goes as they are pushed down), with the field focused.
@Composable
fun ImeNestedScrollSection() {
    val cs = MaterialTheme.colorScheme
    var draft by remember { mutableStateOf("") }
    val scroll = rememberScrollState()
    Section("Modifier.imeNestedScroll") {
        Column(modifier = Modifier.fillMaxWidth().height(220.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).clip(RoundedCornerShape(12.dp))) {
            Column(modifier = Modifier.weight(1f).fillMaxWidth().imeNestedScroll().verticalScroll(scroll, reverseScrolling = true).padding(12.dp),
                   verticalArrangement = Arrangement.spacedBy(6.dp)) {
                for (i in 1..12) {
                    Box(modifier = Modifier.fillMaxWidth(), contentAlignment = if (i % 2 == 1) Alignment.CenterStart else Alignment.CenterEnd) {
                        Text("Mesaj $i", modifier = Modifier.background(if (i % 2 == 1) cs.secondaryContainer else cs.primaryContainer, RoundedCornerShape(12.dp)).padding(10.dp, 6.dp))
                    }
                }
            }
            TextField(value = draft, onValueChange = { draft = it }, modifier = Modifier.fillMaxWidth(), singleLine = true, placeholder = "Yazın…")
        }
        Text("Alana dokunup klavyeyi kapatın; mesajları sonlarından öteye yukarı çekin: klavye parmakla gelir, aşağı itince gider (Android 11+).",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
    }
}

// preferredFrameRate: the refresh rate asked for while this is laid out (a dot moving across to
// see it by).
@Composable
fun FrameRateSection() {
    val cs = MaterialTheme.colorScheme
    var choice by remember { mutableStateOf(0) }
    // (Read while laying out: the dot is placed again, nothing composed again.)
    val x = rememberInfiniteTransition().animateFloat(initialValue = 0f, targetValue = 1f,
                                                      animationSpec = infiniteRepeatable(tween(1600, easing = LinearEasing), RepeatMode.Reverse))
    val labels = listOf("Yok", "30 Hz", "Normal", "En yüksek")
    val wish = when (choice) {
        1 -> Modifier.preferredFrameRate(30f)
        2 -> Modifier.preferredFrameRate(FrameRateCategory.Normal)
        3 -> Modifier.preferredFrameRate(FrameRateCategory.High)
        else -> Modifier
    }
    Section("Modifier.preferredFrameRate") {
        SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
            for (i in 0 until 4) SegmentedButton(selected = choice == i, onClick = { choice = i }, shape = SegmentedButtonDefaults.itemShape(i, 4)) { Text(labels[i]) }
        }
        BoxWithConstraints(modifier = Modifier.fillMaxWidth().height(32.dp).then(wish)) {
            val room = maxWidth - 32
            Box(modifier = Modifier.offset { IntOffset((x.value * room).roundToInt(), 0) }.size(32.dp).background(cs.primary, CircleShape))
        }
        Text("Seçim, bu bölüm ekrandayken ekranın tazeleme hızına istek olarak gider (Android 11+; telefon ayarı ve pil durumu son sözü söyler).",
             color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
    }
}

// What each kind of system UI covers right now (the ime row follows the keyboard).
@Composable
fun InsetsSection() {
    val cs = MaterialTheme.colorScheme
    Section("WindowInsets (dp: sol, üst, sağ, alt)") {
        val kinds = listOf(
            "statusBars" to WindowInsets.statusBars, "navigationBars" to WindowInsets.navigationBars,
            "ime" to WindowInsets.ime, "displayCutout" to WindowInsets.displayCutout,
            "systemGestures" to WindowInsets.systemGestures, "captionBar" to WindowInsets.captionBar,
            "mandatorySystemGestures" to WindowInsets.mandatorySystemGestures, "tappableElement" to WindowInsets.tappableElement,
            "waterfall" to WindowInsets.waterfall, "safeGestures" to WindowInsets.safeGestures,
            "safeDrawing" to WindowInsets.safeDrawing, "safeContent" to WindowInsets.safeContent,
            "imeAnimationSource" to WindowInsets.imeAnimationSource, "imeAnimationTarget" to WindowInsets.imeAnimationTarget,
        )
        for ((name, i) in kinds) {
            Row(modifier = Modifier.fillMaxWidth()) {
                Text(name, modifier = Modifier.weight(1f), color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
                Text("%.0f, %.0f, %.0f, %.0f".format(i.left, i.top, i.right, i.bottom), fontSize = 14.sp, fontFamily = FontFamily.Monospace)
            }
        }
        // The keyboard: up (or on its way up)? Android 11+ moves ime() with it as it slides.
        Text("isImeVisible: ${if (WindowInsets.isImeVisible) "evet" else "hayır"} (klavye kayarken ime() onu kare kare izler)", style = MaterialTheme.typography.bodySmall)
        // As tall as the status bar (taken by the screen's padding? then nothing: 0 dp here).
        Text("windowInsetsTopHeight(statusBars): bu sayfanın içinde zaten alınmış, yüksekliği 0", style = MaterialTheme.typography.bodySmall)
        Spacer(modifier = Modifier.fillMaxWidth().windowInsetsTopHeight(WindowInsets.statusBars).background(cs.tertiary))
    }
}
