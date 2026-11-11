// Components screen: selectable text, context menus, pointers, indications, the text toolbar, multi-touch, the keyboard, drag and drop.

#include "showcase.hpp"

// SelectionContainer: long press a word, drag the handles from text to text, copy;
// DisableSelection keeps a line out of it.
@Kompozable
fun SelectableTextSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    Section("SelectionContainer · DisableSelection") {
        SelectionContainer(modifier = Modifier.fillMaxWidth()) {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Bu paragraftaki bir kelimeye uzun basın: seçilir, tutamaçları bir metinden ötekine " +
                         "sürükleyip kopyalayabilirsiniz. Fareyle sürükleyerek de seçilir, Ctrl+C kopyalar.",
                     style = typography.bodyLarge)
                Text("Kompoz, Jetpack Compose'un C++20 karşılığıdır: seçim bu metne de uzanır.",
                     color = cs.onSurfaceVariant, style = typography.bodyMedium)
                DisableSelection {
                    Text("DisableSelection içindeki bu satır seçilmez.", color = cs.outline, style = typography.labelLarge)
                }
                Text("Tümünü seç, seçilebilen her metni alır; çift tıklama bir kelimeyi.", style = typography.bodyMedium)
            }
        }
    }
}

// ContextMenuArea: a right click on the card opens its menu there; text fields have their own.
@Kompozable
fun ContextMenuSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var last by remember { mutableStateOf("yok") }
    var note by remember { mutableStateOf("Sağ tıkla: kes, kopyala, yapıştır") }
    Section("ContextMenuArea (sağ tık)") {
        Text("Masaüstünde karta sağ tıklayınca menüsü tıklanan yerde açılır; metin alanlarının kendi menüsü var (Kes, Kopyala, " +
                 "Yapıştır, Tümünü seç).",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        ContextMenuArea(items = {
            listOf(ContextMenuItem("Yenile") { last = "Yenile" },
                   ContextMenuItem("Adını kopyala") { last = "Adını kopyala" },
                   ContextMenuItem("Sil (kapalı)", {}, false))
        }) {
            Box(modifier = Modifier.fillMaxWidth().height(88.dp).background(cs.secondaryContainer, RoundedCornerShape(16.dp)),
                contentAlignment = Alignment.Center) {
                Text("Buraya sağ tıklayın — son seçim: $last", color = cs.onSecondaryContainer)
            }
        }
        OutlinedTextField(value = note, onValueChange = { note = it }, modifier = Modifier.fillMaxWidth(), singleLine = true)
    }
}

// The mouse on the desktop: onPointerEvent (in, out, moving, buttons, the wheel), onClick by
// button and keys held, TooltipArea.
@Kompozable
fun PointerSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var inside by remember { mutableStateOf(false) }
    var at by remember { mutableStateOf(Offset.Zero) }
    var wheel by remember { mutableStateOf(0f) }
    var button by remember { mutableStateOf("-") }
    var clicks by remember { mutableStateOf(List(3) { 0 }) }
    Section("onPointerEvent · onClick · TooltipArea (fare)") {
        Text("Masaüstünde: fareyi kutunun üstünde gezdirin, tekerleği çevirin (sayfa kaymaz, sayaç değişir), tuşlara basın.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        val bump: (Int) -> Unit = { i -> clicks = clicks.mapIndexed { j, c -> if (j == i) c + 1 else c } }
        Box(modifier = Modifier
                .fillMaxWidth()
                .height(96.dp)
                .background(if (inside) cs.tertiaryContainer else cs.surfaceContainerHigh, RoundedCornerShape(16.dp))
                .onPointerEvent(PointerEventType.Enter) { inside = true }
                .onPointerEvent(PointerEventType.Exit) { inside = false }
                .onPointerEvent(PointerEventType.Move) { at = it.changes.first().position }
                .onPointerEvent(PointerEventType.Press) { e ->
                    val mods = (if (e.keyboardModifiers.isCtrlPressed) "Ctrl+" else "") + (if (e.keyboardModifiers.isShiftPressed) "Shift+" else "")
                    val pressed = e.button
                    val name = if (pressed == null) "parmak" else when (pressed) {
                        PointerButton.Primary -> "birincil"
                        PointerButton.Secondary -> "ikincil"
                        PointerButton.Tertiary -> "orta"
                        PointerButton.Back -> "geri"
                        else -> "ileri"
                    }
                    button = mods + name
                }
                .onPointerEvent(PointerEventType.Scroll) { e ->
                    wheel += e.changes.first().scrollDelta.y
                    e.changes.first().consume()  // the page doesn't scroll
                },
            contentAlignment = Alignment.Center) {
            Text("${if (inside) "içeride" else "dışarıda"} · ${at.x.roundToInt()}, ${at.y.roundToInt()} · tekerlek ${"%+.0f".format(wheel)} · son tuş: $button",
                 color = if (inside) cs.onTertiaryContainer else cs.onSurface)
        }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            val chip = Modifier.height(48.dp).background(cs.secondaryContainer, RoundedCornerShape(12.dp))
            Box(modifier = chip.weight(1f).onClick(matcher = PointerMatcher.mouse(PointerButton.Secondary)) { bump(0) },
                contentAlignment = Alignment.Center) { Text("Sağ tık: ${clicks[0]}", style = typography.labelLarge) }
            Box(modifier = chip.weight(1f).onClick(matcher = PointerMatcher.mouse(PointerButton.Tertiary)) { bump(1) },
                contentAlignment = Alignment.Center) { Text("Orta tık: ${clicks[1]}", style = typography.labelLarge) }
            Box(modifier = chip.weight(1f).onClick(keyboardModifiers = { it.isCtrlPressed }) { bump(2) },
                contentAlignment = Alignment.Center) { Text("Ctrl+tık: ${clicks[2]}", style = typography.labelLarge) }
        }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            TooltipArea(tooltip = {
                Text("İmlecin altında çıkan ipucu", modifier = Modifier.shadow(4.dp, RoundedCornerShape(4.dp)).background(cs.inverseSurface, RoundedCornerShape(4.dp)).padding(8.dp, 4.dp),
                     color = cs.inverseOnSurface, style = typography.bodySmall)
            }, modifier = Modifier.weight(1f)) {
                OutlinedButton(onClick = {}, modifier = Modifier.fillMaxWidth()) { Text("Üstünde bekleyin") }
            }
            TooltipArea(tooltip = {
                Text("Düğmenin altında, ortada", modifier = Modifier.shadow(4.dp, RoundedCornerShape(4.dp)).background(cs.inverseSurface, RoundedCornerShape(4.dp)).padding(8.dp, 4.dp),
                     color = cs.inverseOnSurface, style = typography.bodySmall)
            }, modifier = Modifier.weight(1f), delayMillis = 300,
               tooltipPlacement = TooltipPlacement.ComponentRect(Alignment.BottomCenter, Alignment.BottomCenter, DpOffset(0.dp, 4.dp))) {
                OutlinedButton(onClick = {}, modifier = Modifier.fillMaxWidth()) { Text("ComponentRect") }
            }
        }
    }
}

// Indications of one's own (IndicationNodeFactory): one shrinks what is pressed a little (as
// Compose's ScaleIndication sample), the other lights its outline while pressed, hovered or
// focused. Their nodes are C++ (classes with a base and coroutines).
class ShrinkIndicationNode final : public IndicationNode {
public:
    void onInteraction(const Interaction& i) override {
        using T = Interaction::Type;
        if (i.type != T::Press && i.type != T::Release && i.type != T::Cancel) return;
        if (i.type == T::Press) pivot_ = i.pressPosition;
        coroutineScope().launch([s = scale_, to = i.type == T::Press ? 0.9f : 1.f]() -> Task { co_await s.animateTo(to, spring(0.5f, 800)); });
    }
    void draw(ContentDrawScope& d) override {
        float s = scale_.value();
        d.scale(s, s, [&](DrawScope&) { d.drawContent(); }, pivot_);
    }

private:
    Animatable<float> scale_{1.f};
    Offset pivot_;
};

struct ShrinkIndication final : IndicationNodeFactory {
    std::shared_ptr<IndicationNode> create() const override { return std::make_shared<ShrinkIndicationNode>(); }
};

class OutlineIndicationNode final : public IndicationNode {
public:
    explicit OutlineIndicationNode(Color color) : color_(color) {}
    void onInteraction(const Interaction& i) override {
        if (i.isStart()) ++active_;
        else if (i.type != Interaction::Type::DragStop && i.type != Interaction::Type::DragCancel) active_ = std::max(0, active_ - 1);
        coroutineScope().launch([a = alpha_, to = active_ > 0 ? 1.f : 0.f]() -> Task { co_await a.animateTo(to, tween(150)); });
    }
    void draw(ContentDrawScope& d) override {
        d.drawContent();
        if (float a = alpha_.value(); a > 0) d.drawRoundRect(color_.copy(a), {1.5f, 1.5f}, Size{d.size().width - 3, d.size().height - 3}, CornerRadius(12), Stroke(3));
    }

private:
    Color color_;
    Animatable<float> alpha_{0.f};
    int active_ = 0;
};

struct OutlineIndication final : IndicationNodeFactory {
    Color color;
    explicit OutlineIndication(Color c) : color(c) {}
    std::shared_ptr<IndicationNode> create() const override { return std::make_shared<OutlineIndicationNode>(color); }
};

// The indications as values (Compose's ScaleIndication object).
Indication shrinkIndication() { return Indication(std::make_shared<ShrinkIndication>()); }
Indication outlineIndication(Color color) { return Indication(std::make_shared<OutlineIndication>(color)); }

@Kompozable
fun CustomIndicationSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val shrink = remember { shrinkIndication() }
    val outline = remember { outlineIndication(cs.primary) }
    var taps by remember { mutableStateOf(0) }
    Section("IndicationNodeFactory · özel göstergeler") {
        // The indication draws what comes after the clickable: put it before the background
        // to shrink the whole button (a Material button's clickable comes after its background).
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            for ((label, bg, fg) in listOf(Triple("Küçülür", cs.primary, cs.onPrimary), Triple("Bu da", cs.secondaryContainer, cs.onSecondaryContainer)))
                Box(modifier = Modifier.clickable(interactionSource = null, indication = shrink) { taps++ }.background(bg, RoundedCornerShape(20.dp)).padding(20.dp, 10.dp),
                    contentAlignment = Alignment.Center) { Text(label, color = fg, style = typography.labelLarge) }
            Text("$taps dokunuş", style = typography.bodyMedium)
        }
        Box(modifier = Modifier
                .fillMaxWidth()
                .height(64.dp)
                .background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp))
                .clickable(interactionSource = null, indication = outline) { taps++ }
                .padding(16.dp, 0.dp),
            contentAlignment = Alignment.Center) { Text("Basınca (fareyle üzerine gelince, odaklanınca) çerçevesi yanar") }
        Text("clickable'ın indication'ı (ya da LocalIndication): kendi IndicationNode'u etkileşimleri dinler, ondan sonrasını sarıp çizer.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
    }
}

// A text toolbar of the app's own (LocalTextToolbar): Kompoz tells it where the selection is and
// what can be done; the Kotlin below shows its own pill over it (in a popup, as the platform's
// floats in a window of its own: a press on it doesn't end the selection).
struct PillAction {
    std::string label;
    std::function<void()> run;
};
class PillToolbar final : public TextToolbar {
public:
    explicit PillToolbar(MutableState<int> changes) : changes_(changes) {}
    void showMenu(Rect rect, std::function<void()> copy, std::function<void()> paste, std::function<void()> cut,
                  std::function<void()> selectAll) override {
        rect_ = rect;
        actions_.clear();
        for (auto& [label, action] : {std::pair{"✂ Kes", std::move(cut)}, std::pair{"⧉ Kopyala", std::move(copy)},
                                      std::pair{"📋 Yapıştır", std::move(paste)}, std::pair{"▣ Tümü", std::move(selectAll)}})
            if (action) actions_.push_back({label, action});  // only what can be done now
        shown_ = true;
        changes_.set(changes_.peek() + 1);
    }
    void hide() override {
        shown_ = false;
        changes_.set(changes_.peek() + 1);
    }
    TextToolbarStatus status() const override { return shown_ ? TextToolbarStatus::Shown : TextToolbarStatus::Hidden; }
    bool shown() const { return shown_; }
    Rect rect() const { return rect_; }
    const std::vector<PillAction>& actions() const { return actions_; }

private:
    bool shown_ = false;
    Rect rect_;
    std::vector<PillAction> actions_;
    MutableState<int> changes_;
};
std::shared_ptr<PillToolbar> pillToolbar(MutableState<int> changes) { return std::make_shared<PillToolbar>(changes); }

@Kompozable
fun TextToolbarSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val changes = remember { mutableStateOf(0) }
    val pill = remember { pillToolbar(changes) }
    var text by remember { mutableStateOf("Bir kelimeye uzun basın") }
    Section("LocalTextToolbar · TextToolbar") {
        KompozitionLocalProvider(LocalTextToolbar provides pill) {
            OutlinedTextField(value = text, onValueChange = { text = it }, modifier = Modifier.fillMaxWidth(),
                              singleLine = true, label = { Text("Kendi araç çubuğumuz") })
            SelectionContainer { Text("Bu metin de seçilebilir: uzun basın, kopyala ve tümünü seç çıkar.") }
        }
        if (changes.value >= 0 && pill.shown) {  // shown and hidden by the toolbar's calls
            val r = pill.rect
            Popup(popupPositionProvider = object : PopupPositionProvider {
                override fun calculatePosition(anchorBounds: IntRect, windowSize: IntSize, layoutDirection: LayoutDirection, popupContentSize: IntSize): IntOffset {
                    val y = r.y - popupContentSize.height - 12
                    val x = (r.x + r.width / 2 - popupContentSize.width / 2).coerceIn(8f, max(8f, windowSize.width - popupContentSize.width - 8f))
                    return IntOffset(x.roundToInt(), (if (y < 0) r.bottom + 36 else y).roundToInt())
                }
            }, properties = PopupProperties(focusable = false)) {
                Row(modifier = Modifier.shadow(4.dp, RoundedCornerShape(24.dp)).background(cs.tertiaryContainer, RoundedCornerShape(24.dp)).padding(4.dp),
                    horizontalArrangement = Arrangement.spacedBy(2.dp)) {
                    for (action in pill.actions)
                        Box(modifier = Modifier.clickable { action.run() }.padding(12.dp, 8.dp), contentAlignment = Alignment.Center) {
                            Text(action.label, color = cs.onTertiaryContainer, style = typography.labelLarge)
                        }
                }
            }
        }
        Text("Seçim çubuğu Kompoz'unki değil: LocalTextToolbar'a seçimin yeri ve yapılabilecekler bildirilir, o kendisininkini gösterir.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
    }
}

std::string keyName(Key key) {
    if (key >= Key::A && key <= Key::Z) return std::string(1, char('A' + (int(key) - int(Key::A))));
    if (key >= Key::Zero && key <= Key::Nine) return std::string(1, char('0' + (int(key) - int(Key::Zero))));
    if (key >= Key::F1 && key <= Key::F12) return std::format("F{}", int(key) - int(Key::F1) + 1);
    switch (key) {
    case Key::Enter: return "Enter";
    case Key::Escape: return "Escape";
    case Key::Tab: return "Tab";
    case Key::Backspace: return "Backspace";
    case Key::Delete: return "Delete";
    case Key::Space: return "Space";
    case Key::Left: return "←";
    case Key::Right: return "→";
    case Key::Up: return "↑";
    case Key::Down: return "↓";
    case Key::Home: return "Home";
    case Key::End: return "End";
    case Key::PageUp: return "PageUp";
    case Key::PageDown: return "PageDown";
    default: return "?";
    }
}

// Pinch and twist the photo; zoomed in, one finger pans it (otherwise it scrolls the page).
@Kompozable
fun MultiTouchSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var scale by remember { mutableStateOf(1f) }
    var rotation by remember { mutableStateOf(0f) }
    var offset by remember { mutableStateOf(Offset.Zero) }
    val state = rememberTransformableState { zoom, pan, turn ->
        scale = (scale * zoom).coerceIn(1f, 6f)
        rotation += turn
        offset += pan
    }
    val photo = imageResource("images/landscape.png")
    Section("Çoklu dokunma (transformable)") {
        Text("İki parmakla yakınlaştırın ve döndürün; yakınken tek parmakla kaydırın, çift dokunuş sıfırlar.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Box(modifier = Modifier
                .fillMaxWidth()
                .height(220.dp)
                .clip(RoundedCornerShape(16.dp))
                .background(cs.surfaceContainerHighest)
                .transformable(state, canPan = { scale > 1.01f })
                .pointerInput(Unit) {
                    detectTapGestures(onDoubleTap = {
                        scale = 1f
                        rotation = 0f
                        offset = Offset.Zero
                    })
                },
            contentAlignment = Alignment.Center) {
            // Read while drawing: the gesture doesn't recompose the image.
            Image(photo, contentDescription = "Manzara",
                  modifier = Modifier.fillMaxSize().graphicsLayer {
                      scaleX = scale
                      scaleY = scale
                      rotationZ = rotation
                      translationX = offset.x
                      translationY = offset.y
                  },
                  contentScale = ContentScale.Crop)
        }
        Text("Ölçek ×${"%.2f".format(scale)} · açı ${rotation.roundToInt()}°", color = cs.onSurfaceVariant, style = typography.bodyMedium)
    }
}

// Hardware keyboard: Tab / arrows move focus between clickables, Enter / Space click them, and
// a focusable() box reports the keys it gets.
@Kompozable
fun KeyboardSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var clicks by remember { mutableStateOf(List(9) { 0 }) }
    var lastKey by remember { mutableStateOf("yok") }
    var boxFocused by remember { mutableStateOf(false) }
    Section("Klavye ve odak (focusable, onKeyEvent)") {
        DesktopNote("Masaüstü için: telefonda yalnızca bir donanım klavyesi (Bluetooth ya da USB) bağlıyken çalışır.")
        Text("Tab / Shift+Tab ve oklarla gezin, Enter ya da Space ile tıklayın (masaüstü, donanım klavyesi).",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        for (row in 0 until 3)
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for (col in 0 until 3) {
                    val i = row * 3 + col
                    Box(modifier = Modifier.weight(1f).height(48.dp).background(cs.secondaryContainer, RoundedCornerShape(12.dp))
                            .clickable { clicks = clicks.mapIndexed { j, c -> if (j == i) c + 1 else c } },
                        contentAlignment = Alignment.Center) {
                        Text("${i + 1} · ${clicks[i]}", color = cs.onSecondaryContainer)
                    }
                }
            }
        Box(modifier = Modifier
                .fillMaxWidth()
                .height(56.dp)
                .border(2.dp, if (boxFocused) cs.primary else cs.outlineVariant, RoundedCornerShape(12.dp))
                .onFocusChanged { boxFocused = it.isFocused }
                .onKeyEvent { e ->
                    if (e.key == Key.Tab || e.key == Key.Escape) false  // keep moving focus / leaving
                    else {
                        lastKey = (if (e.isCtrlPressed) "Ctrl+" else "") + (if (e.isShiftPressed) "Shift+" else "") + keyName(e.key) +
                            (if (e.type == KeyEventType.KeyDown) " basıldı" else " bırakıldı")
                        true
                    }
                }
                .focusable()
                .padding(16.dp, 0.dp),
            contentAlignment = Alignment.CenterStart) {
            Text(if (boxFocused) "Son tuş: $lastKey" else "Tab ile bu kutuya gelin, sonra bir tuşa basın",
                 color = if (boxFocused) cs.onSurface else cs.onSurfaceVariant)
        }
    }
}

// A card that animates several values together when it gets selected.
@Kompozable
fun SelectableCard(index: Int, selected: MutableState<Int>) {
    val cs = MaterialTheme.colorScheme
    val t = updateTransition(selected.value == index)
    val container by t.animateColor { on -> if (on) cs.primaryContainer else cs.surfaceContainerHigh }
    val content by t.animateColor { on -> if (on) cs.onPrimaryContainer else cs.onSurfaceVariant }
    val lift by t.animateFloat(transitionSpec = { spring(Spring.DampingRatioMediumBouncy) }) { on -> if (on) 1f else 0f }
    Column(modifier = Modifier.weight(1f).height(84.dp).graphicsLayer(scaleX = 1 + 0.05f * lift, scaleY = 1 + 0.05f * lift)
               .background(container, RoundedCornerShape(16.dp)).clickable { selected.value = index }.padding(12.dp),
           verticalArrangement = Arrangement.SpaceBetween) {
        Icon(when (index) {
                 0 -> Icons.Filled.Home
                 1 -> Icons.Filled.Star
                 else -> Icons.Filled.Favorite
             }, contentDescription = "Kart", tint = content)
        Text(if (t.currentState) "Seçili" else if (t.isRunning) "…" else "Kart ${index + 1}",
             color = content, style = MaterialTheme.typography.labelLarge)
    }
}

// A card to drag between the columns.
data class TaskCard(val id: Int, val text: String, val column: Int)  // column 0: to do, 1: done

// Drag and drop: cards moved between two columns. A finger holds a card, then drags it (the
// mouse just drags); the other column lights up as it takes part, more under the pointer.
@Kompozable
fun DragAndDropSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var cards by remember {
        mutableStateOf(listOf(TaskCard(0, "Taslağı yaz", 0), TaskCard(1, "Testleri çalıştır", 0), TaskCard(2, "Ekran görüntüsü", 0),
                              TaskCard(3, "Derle", 1)))
    }
    var taking by remember { mutableStateOf(-1) }  // the column a drag could go to
    var over by remember { mutableStateOf(-1) }    // the column under the pointer
    var dropped by remember { mutableStateOf("") }
    var overDrop by remember { mutableStateOf(false) }
    Section("Sürükle-bırak (dragAndDropSource / dragAndDropTarget)") {
        Text("Bir kartı basılı tutup öbür sütuna sürükleyin (fareyle doğrudan sürükleyin); Escape vazgeçer. Masaüstünde " +
                 "dosya yöneticisinden ya da tarayıcıdan aşağıdaki alana dosya / metin de bırakabilirsiniz.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for (column in 0 until 2) {
                val tint = if (over == column) cs.primaryContainer else if (taking == column) cs.secondaryContainer else cs.surfaceContainerHigh
                Column(modifier = Modifier
                           .weight(1f)
                           .heightIn(180.dp)
                           .background(tint, RoundedCornerShape(16.dp))
                           .dragAndDropTarget(
                               shouldStartDragAndDrop = { e ->
                                   val card = e.localState<TaskCard>()
                                   card != null && card.column != column  // from the other column
                               },
                               target = DragAndDropTarget(
                                   onDrop = { e ->
                                       val moved = e.localState<TaskCard>()
                                       if (moved != null) cards = cards.map { if (it.id == moved.id) it.copy(column = column) else it }
                                       true
                                   },
                                   onStarted = { taking = column },
                                   onEntered = { over = column },
                                   onExited = { over = -1 },
                                   onEnded = {
                                       taking = -1
                                       over = -1
                                   }))
                           .padding(10.dp),
                       verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(if (column == 0) "Yapılacak" else "Bitti", style = typography.titleSmall)
                    for (card in cards) {
                        if (card.column != column) continue
                        key(card.id) {
                            Box(modifier = Modifier
                                    .fillMaxWidth()
                                    .shadow(1.dp, RoundedCornerShape(12.dp))
                                    .background(cs.surface, RoundedCornerShape(12.dp))
                                    .dragAndDropSource { DragAndDropTransferData(text = card.text, localState = card) }
                                    .padding(12.dp, 10.dp)) { Text(card.text, style = typography.bodyMedium) }
                        }
                    }
                }
            }
        }
        // Anything with text: a card, files from a file manager (their paths), text from a browser.
        Box(modifier = Modifier
                .fillMaxWidth()
                .heightIn(64.dp)
                .background(if (overDrop) cs.primaryContainer else cs.surfaceContainerHigh, RoundedCornerShape(16.dp))
                .border(1.dp, cs.outlineVariant, RoundedCornerShape(16.dp))
                .dragAndDropTarget(shouldStartDragAndDrop = { it.hasMimeType("text/uri-list") || it.hasMimeType("text/plain") },
                                   target = DragAndDropTarget(
                                       onDrop = { e ->
                                           val files = e.data.files
                                           dropped = if (files.isNotEmpty()) {
                                               files.take(3).joinToString(", ") { it.substringAfterLast('/').substringAfterLast('\\') } +
                                                   (if (files.size > 3) " (+${files.size - 3})" else "")
                                           } else if (e.text().length > 80) e.text().substring(0, 80) + "…" else e.text()
                                           true
                                       },
                                       onEntered = { overDrop = true },
                                       onExited = { overDrop = false },
                                       onEnded = { overDrop = false }))
                .padding(14.dp, 10.dp),
            contentAlignment = Alignment.CenterStart) {
            Text(if (dropped.isEmpty()) "Buraya bırakın" else "Bırakılan: $dropped", color = cs.onSurfaceVariant, style = typography.bodyMedium)
        }
    }
}
