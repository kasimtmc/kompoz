// Material screen: buttons, chips and selection controls.

#include "showcase.hpp"

@Kompozable
fun ButtonsSection() {
    val cs = MaterialTheme.colorScheme
    Section("Butonlar") {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = { }) { Text("Button") }
            FilledTonalButton(onClick = { }) { Text("Tonal") }
            ElevatedButton(onClick = { }) { Text("Elevated") }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { }) { Text("Outlined") }
            TextButton(onClick = { }) { Text("Text") }
            Button(onClick = { }, enabled = false) { Text("Kapalı") }
        }
        // MotionScheme: the buttons below on Material 3's standard springs or the expressive ones
        // (MaterialExpressiveTheme's), which bounce as the corners morph and the held button grows.
        var expressiveMotion by remember { mutableStateOf(false) }
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Switch(checked = expressiveMotion, onCheckedChange = { expressiveMotion = it })
            Text(if (expressiveMotion) "MotionScheme.expressive()" else "MotionScheme.standard()", style = MaterialTheme.typography.bodyMedium)
        }
        KompozitionLocalProvider(LocalMotionScheme provides if (expressiveMotion) MotionScheme.expressive() else MotionScheme.standard()) {
            // M3 Expressive: the corners morph, squarer while held; toggle buttons turn square when checked.
            Button(onClick = { }, shapes = ButtonDefaults.shapes()) { Text("Basılı tut: köşeler") }
            // ButtonGroup: the button held grows and squeezes its neighbours.
            ButtonGroup {
                val labels = listOf("Önceki", "Oynat", "Sonraki")
                for (i in 0 until 3) {
                    val source = remember { MutableInteractionSource() }
                    if (i == 1) {
                        Button(onClick = { }, modifier = Modifier.animateWidth(source), interactionSource = source) { Text(labels[i]) }
                    } else {
                        FilledTonalButton(onClick = { }, modifier = Modifier.animateWidth(source), interactionSource = source) { Text(labels[i]) }
                    }
                }
            }
        }
        // From items: what doesn't fit goes into the ⋮ menu at the end.
        var bold by remember { mutableStateOf(false) }
        var lastAction by remember { mutableStateOf("yok") }
        ButtonGroup(
            overflowIndicator = { menuState ->
                FilledIconButton(onClick = { if (menuState.isExpanded) menuState.dismiss() else menuState.show() }) {
                    Icon(Icons.Filled.MoreVert, contentDescription = "Daha fazla")
                }
            },
            modifier = Modifier.fillMaxWidth(),
        ) {
            clickableItem(onClick = { lastAction = "Kopyala" }, label = "Kopyala")
            clickableItem(onClick = { lastAction = "Yapıştır" }, label = "Yapıştır")
            toggleableItem(checked = bold, onCheckedChange = { bold = it }, label = "Kalın")
            clickableItem(onClick = { lastAction = "Sil" }, label = "Sil", icon = { Icon(Icons.Filled.Delete, contentDescription = null) })
            clickableItem(onClick = { lastAction = "Paylaş" }, label = "Paylaş", icon = { Icon(Icons.Filled.Share, contentDescription = null) })
        }
        Text("Son eylem: $lastAction${if (bold) " · kalın" else ""}", color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
        // A split button: the menu button turns round while its menu is open.
        var saveMenu by remember { mutableStateOf(false) }
        Box {
            SplitButtonLayout(
                leadingButton = { SplitButtonDefaults.LeadingButton(onClick = { }) { Text("Kaydet") } },
                trailingButton = {
                    SplitButtonDefaults.TrailingButton(checked = saveMenu, onCheckedChange = { saveMenu = it }) {
                        SplitButtonDefaults.TrailingButtonIcon(saveMenu)
                    }
                },
            )
            DropdownMenu(expanded = saveMenu, onDismissRequest = { saveMenu = false }) {
                DropdownMenuItem(text = { Text("Farklı kaydet") }, onClick = { saveMenu = false })
                DropdownMenuItem(text = { Text("Taslak olarak kaydet") }, onClick = { saveMenu = false })
            }
        }
        val styles = remember { mutableStateListOf(true, false, false, false) }
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            ToggleButton(checked = styles[0], onCheckedChange = { styles[0] = it }) { Text("Kalın") }
            ElevatedToggleButton(checked = styles[1], onCheckedChange = { styles[1] = it }) { Text("İtalik") }
            TonalToggleButton(checked = styles[2], onCheckedChange = { styles[2] = it }) { Text("Altı çizili") }
            OutlinedToggleButton(checked = styles[3], onCheckedChange = { styles[3] = it }) { Text("Üstü çizili") }
        }
    }
}

@Kompozable
fun ChipsSection() {
    val cs = MaterialTheme.colorScheme
    val drinks = remember { mutableStateListOf("Kahve") }
    val people = remember { mutableStateListOf("Ayşe", "Mehmet") }
    var openOnly by remember { mutableStateOf(false) }
    Section("Chip'ler") {
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            for (drink in listOf("Kahve", "Çay", "Su", "Ayran")) {
                val on = drink in drinks
                FilterChip(selected = on, onClick = { if (on) drinks.remove(drink) else drinks.add(drink) }, label = { Text(drink) },
                           leadingIcon = if (on) { { Icon(Icons.Filled.Check, contentDescription = "Seçili", modifier = Modifier.size(18.dp)) } } else null)
            }
        }
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            AssistChip(onClick = { }, label = { Text("Yol tarifi") },
                       leadingIcon = { Icon(Icons.Filled.Place, contentDescription = "Konum", modifier = Modifier.size(18.dp)) })
            SuggestionChip(onClick = { }, label = { Text("Bugün") })
            // Elevated: a shadow instead of the border.
            ElevatedAssistChip(onClick = { }, label = { Text("Paylaş") },
                               leadingIcon = { Icon(Icons.Filled.Share, contentDescription = "Paylaş", modifier = Modifier.size(18.dp)) })
            ElevatedFilterChip(selected = openOnly, onClick = { openOnly = !openOnly }, label = { Text("Şu an açık") })
            ElevatedSuggestionChip(onClick = { }, label = { Text("Yarın") })
            for (person in people) {
                InputChip(selected = false, onClick = { }, label = { Text(person) },
                          trailingIcon = {
                              IconButton(onClick = { people.remove(person) }, modifier = Modifier.size(24.dp)) {
                                  Icon(Icons.Filled.Close, contentDescription = "Kaldır", modifier = Modifier.size(18.dp))
                              }
                          })
            }
        }
        Text("Seçilen içecekler: ${drinks.joinToString(", ").ifEmpty { "yok" }}", color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
    }
}

// One end of the price range: bold while that thumb is held.
@Kompozable
private fun RangeEnd(value: Float, source: MutableInteractionSource) {
    val pressed by source.collectIsPressedAsState()
    val dragged by source.collectIsDraggedAsState()
    val held = pressed || dragged
    Text("%.0f".format(value), fontWeight = if (held) FontWeight.Bold else null,
         color = if (held) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
}

@Kompozable
fun SelectionControlsSection() {
    val cs = MaterialTheme.colorScheme
    var size by remember { mutableStateOf("Orta") }
    var volume by remember { mutableStateOf(40f) }
    val toppings = remember { mutableStateListOf(true, false, false) }
    var rating by remember { mutableStateOf(3f) }
    var price by remember { mutableStateOf(200f..800f) }
    // The sliders' interaction sources: the value being moved stands out while held.
    val volumeSource = remember { MutableInteractionSource() }
    val priceFrom = remember { MutableInteractionSource() }
    val priceTo = remember { MutableInteractionSource() }
    var period by remember { mutableStateOf(1) }
    val transport = remember { mutableStateListOf(true, false, false) }
    var touchRoom by remember { mutableStateOf(true) }
    Section("Seçim kontrolleri") {
        // minimumInteractiveComponentSize: 48 dp of room to touch the controls (0: just their own size).
        Row(verticalAlignment = Alignment.CenterVertically) {
            Switch(checked = touchRoom, onCheckedChange = { touchRoom = it })
            Text("48 dp dokunma alanı", modifier = Modifier.padding(start = 12.dp))
        }
        KompozitionLocalProvider(LocalMinimumInteractiveComponentSize provides if (touchRoom) 48.dp else 0.dp) {
            // TriStateCheckbox: all, none or some of the toppings (Indeterminate: a dash).
            val all = if (toppings.all { it }) ToggleableState.On else if (toppings.any { it }) ToggleableState.Indeterminate else ToggleableState.Off
            Row(verticalAlignment = Alignment.CenterVertically) {
                TriStateCheckbox(state = all, onClick = {
                    val on = all != ToggleableState.On
                    for (i in 0 until 3) toppings[i] = on
                })
                Text("Hepsi (TriStateCheckbox)")
            }
            // Each box with its label, onto the next line together where the row is narrow.
            FlowRow(modifier = Modifier.padding(start = 24.dp)) {
                val names = listOf("Peynir", "Zeytin", "Mantar")
                for (i in 0 until 3) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(checked = toppings[i], onCheckedChange = { toppings[i] = it })
                        Text(names[i], modifier = Modifier.padding(end = 8.dp))
                    }
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                for (option in listOf("Küçük", "Orta", "Büyük")) {
                    RadioButton(selected = size == option, onClick = { size = option })
                    Text(option, modifier = Modifier.padding(end = 12.dp))
                }
            }
            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Filled.Notifications, contentDescription = "Ses")
                Slider(value = volume, onValueChange = { volume = it }, modifier = Modifier.weight(1f).padding(8.dp, 0.dp),
                       valueRange = 0f..100f, interactionSource = volumeSource)
                val pressed by volumeSource.collectIsPressedAsState()
                val dragged by volumeSource.collectIsDraggedAsState()
                val held = pressed || dragged
                Text("%.0f".format(volume), modifier = Modifier.width(32.dp), fontWeight = if (held) FontWeight.Bold else null,
                     color = if (held) cs.primary else LocalContentColor.current, textAlign = TextAlign.End)
            }
            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Filled.Star, contentDescription = "Puan")
                Slider(value = rating, onValueChange = { rating = it }, modifier = Modifier.weight(1f).padding(8.dp, 0.dp), valueRange = 1f..5f, steps = 3)
                Text("%.0f".format(rating), modifier = Modifier.width(32.dp), textAlign = TextAlign.End)
            }
            // systemGestureExclusion: two sliders out to the window's edges; a drag
            // from the edge moves the first, but is the system's back on the second.
            var edgeA by remember { mutableStateOf(0.05f) }
            var edgeB by remember { mutableStateOf(0.05f) }
            val window = LocalWindowSize.current.width
            Text("Kenardan kenara (üstteki systemGestureExclusion ile korunur):", color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
            Slider(value = edgeA, onValueChange = { edgeA = it }, modifier = Modifier.requiredWidth(window.dp).systemGestureExclusion())
            Slider(value = edgeB, onValueChange = { edgeB = it }, modifier = Modifier.requiredWidth(window.dp))
            Row {  // the end being dragged is shown bold
                Text("Fiyat aralığı: ", color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
                RangeEnd(price.start, priceFrom)
                Text(" – ", color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
                RangeEnd(price.endInclusive, priceTo)
                Text(" ₺", color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
            }
            RangeSlider(value = price, onValueChange = { price = it }, modifier = Modifier.fillMaxWidth(), valueRange = 0f..1000f, steps = 19,
                        interactionSource = priceFrom, endInteractionSource = priceTo)
            // M3 Expressive's vertical sliders: up for more (the second one in five steps).
            var loudness by remember { mutableStateOf(0.6f) }
            var light by remember { mutableStateOf(0.5f) }
            Row(modifier = Modifier.height(160.dp), horizontalArrangement = Arrangement.spacedBy(24.dp), verticalAlignment = Alignment.CenterVertically) {
                VerticalSlider(value = loudness, onValueChange = { loudness = it })
                VerticalSlider(value = light, onValueChange = { light = it }, steps = 3)
                Text("Ses %${(loudness * 100).roundToInt()} · Işık %${(light * 100).roundToInt()}", color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
            }
            // The thumb and track to pick: the classic circle, Material 3's bar, shapes, an icon
            // or the value; the classic thin track or Material 3's thick one; steps or none.
            var thumbKind by remember { mutableStateOf(0) }
            var thickTrack by remember { mutableStateOf(false) }
            var stepped by remember { mutableStateOf(false) }
            var bubble by remember { mutableStateOf(true) }
            var picked by remember { mutableStateOf(40f) }
            val pickedSource = remember { MutableInteractionSource() }
            Text("Slider imleci", style = MaterialTheme.typography.titleSmall)
            val thumbs = listOf("Daire", "M3 çubuk", "Kare", "Elmas", "Kurabiye", "Güneş", "Simge", "Değer")
            FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for (i in 0 until 8) FilterChip(selected = thumbKind == i, onClick = { thumbKind = i }, label = { Text(thumbs[i]) })
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(selected = thickTrack, onClick = { thickTrack = !thickTrack }, label = { Text("M3 iz") })
                FilterChip(selected = stepped, onClick = { stepped = !stepped }, label = { Text("10 adım") })
                FilterChip(selected = bubble, onClick = { bubble = !bubble }, label = { Text("Değer balonu") })
            }
            val thumb: (SliderState) -> Unit = { s ->
                when (thumbKind) {
                    1 -> SliderDefaults.Thumb(s)
                    2 -> SliderDefaults.ShapeThumb(s, RoundedCornerShape(6.dp), DpSize(22.dp, 22.dp))
                    3 -> SliderDefaults.ShapeThumb(s, MaterialShapes.Diamond.toShape(), DpSize(26.dp, 26.dp))
                    4 -> SliderDefaults.ShapeThumb(s, MaterialShapes.Cookie9Sided.toShape(), DpSize(28.dp, 28.dp))
                    5 -> SliderDefaults.ShapeThumb(s, MaterialShapes.Sunny.toShape(), DpSize(28.dp, 28.dp))
                    6 -> SliderDefaults.IconThumb(s, Icons.Filled.Favorite)
                    7 -> SliderDefaults.ValueThumb(s)
                    else -> SliderDefaults.CircleThumb(s)  // the classic circle
                }
            }
            // With the bubble: the thumb picked, in a Label showing the value while it is held.
            val withBubble: (SliderState) -> Unit = { s ->
                if (bubble) {
                    Label(label = { PlainTooltip { Text("%.0f".format(s.value)) } }, interactionSource = pickedSource) { thumb(s) }
                } else {
                    thumb(s)
                }
            }
            Slider(value = picked, onValueChange = { picked = it }, modifier = Modifier.fillMaxWidth(), valueRange = 0f..100f,
                   steps = if (stepped) 9 else 0, interactionSource = pickedSource, thumb = withBubble,
                   track = if (thickTrack) { { s -> SliderDefaults.Track(s) } } else null)
            // The same thumbs on a range (each its own), the track picked alike.
            var pickedRange by remember { mutableStateOf(20f..70f) }
            RangeSlider(value = pickedRange, onValueChange = { pickedRange = it }, modifier = Modifier.fillMaxWidth(), valueRange = 0f..100f,
                        steps = if (stepped) 9 else 0, startThumb = thumb, endThumb = thumb,
                        track = if (thickTrack) { { s -> SliderDefaults.Track(s) } } else null)
            // States of their own (rememberSliderState, rememberRangeSliderState): Material 3's look, values shown.
            val hours = rememberRangeSliderState(activeRangeStart = 9f, activeRangeEnd = 17f, steps = 23, valueRange = 0f..24f)
            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("Saat: %02.0f–%02.0f".format(hours.activeRangeStart, hours.activeRangeEnd), modifier = Modifier.width(120.dp), style = MaterialTheme.typography.bodyMedium)
                RangeSlider(state = hours, modifier = Modifier.weight(1f), startThumb = { SliderDefaults.ValueThumb(it) },
                            endThumb = { SliderDefaults.ValueThumb(it) }, track = { SliderDefaults.Track(it) })
            }
            val stars = rememberSliderState(value = 3f, steps = 3, valueRange = 1f..5f)
            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("SliderState: %.0f ★".format(stars.value), modifier = Modifier.width(120.dp), style = MaterialTheme.typography.bodyMedium)
                Slider(state = stars, modifier = Modifier.weight(1f), thumb = { SliderDefaults.ValueThumb(it) }, track = { SliderDefaults.Track(it) })
            }
            SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                val labels = listOf("Gün", "Hafta", "Ay")
                for (i in 0 until 3) {
                    SegmentedButton(selected = period == i, onClick = { period = i }, shape = SegmentedButtonDefaults.itemShape(i, 3)) { Text(labels[i]) }
                }
            }
            // Several at once (MultiChoiceSegmentedButtonRow): each on or off; one that can't be picked.
            MultiChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                val ways = listOf("Yürüyüş", "Bisiklet", "Araba")
                for (i in 0 until 3) {
                    SegmentedButton(checked = transport[i], onCheckedChange = { transport[i] = it }, shape = SegmentedButtonDefaults.itemShape(i, 3),
                                    enabled = i != 2) { Text(ways[i]) }
                }
            }
            // M3 Expressive's connected button group: toggle buttons 2 dp apart, the checked one round.
            var view by remember { mutableStateOf(0) }
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(ButtonGroupDefaults.ConnectedSpaceBetween)) {
                val views = listOf("Liste", "Izgara", "Harita")
                for (i in 0 until 3) {
                    TonalToggleButton(checked = view == i, onCheckedChange = { view = i }, modifier = Modifier.weight(1f),
                                      shapes = when (i) {
                                          0 -> ButtonGroupDefaults.connectedLeadingButtonShapes()
                                          2 -> ButtonGroupDefaults.connectedTrailingButtonShapes()
                                          else -> ButtonGroupDefaults.connectedMiddleButtonShapes()
                                      }) { Text(views[i]) }
                }
            }
        }
    }
}
