// Components screen: selectable text, context menus, pointers, indications, the text toolbar, multi-touch, the keyboard, drag and drop.

#include "showcase.hpp"

// HorizontalPager with page dots, animateContentSize and BoxWithConstraints.
// SelectionContainer: long press a word, drag the handles from text to text, copy;
// DisableSelection keeps a line out of it.
void SelectableTextSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        Section("SelectionContainer · DisableSelection", [&] {
            SelectionContainer(Modifier().fillMaxWidth(), [&] {
                Column(Modifier(), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
                    Text("Bu paragraftaki bir kelimeye uzun basın: seçilir, tutamaçları bir metinden ötekine "
                         "sürükleyip kopyalayabilirsiniz. Fareyle sürükleyerek de seçilir, Ctrl+C kopyalar.",
                         {.font = typography().bodyLarge});
                    Text("Kompoz, Jetpack Compose'un C++20 karşılığıdır: seçim bu metne de uzanır.",
                         {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                    DisableSelection([&] {
                        Text("DisableSelection içindeki bu satır seçilmez.", {.font = typography().labelLarge, .color = cs.outline});
                    });
                    Text("Tümünü seç, seçilebilen her metni alır; çift tıklama bir kelimeyi.",
                         {.font = typography().bodyMedium});
                });
            });
        });
    });
}

// ContextMenuArea: a right click on the card opens its menu there; text fields have their own.
void ContextMenuSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto last = remember([] { return mutableStateOf(std::string("yok")); });
        auto note = remember([] { return mutableStateOf(std::string("Sağ tıkla: kes, kopyala, yapıştır")); });
        Section("ContextMenuArea (sağ tık)", [&] {
            Text("Masaüstünde karta sağ tıklayınca menüsü tıklanan yerde açılır; metin alanlarının kendi menüsü var (Kes, Kopyala, "
                 "Yapıştır, Tümünü seç).",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            ContextMenuArea(
                [last] {
                    return std::vector<ContextMenuItem>{{"Yenile", [last] { last.set("Yenile"); }},
                                                        {"Adını kopyala", [last] { last.set("Adını kopyala"); }},
                                                        {"Sil (kapalı)", [] {}, false}};
                },
                [&] {
                    Box(Modifier().fillMaxWidth().height(88).background(cs.secondaryContainer, 16), Alignment::Center,
                        [&] { Text("Buraya sağ tıklayın — son seçim: " + last.get(), {.color = cs.onSecondaryContainer}); });
                });
            OutlinedTextField(note.get(), [note](std::string v) { note.set(std::move(v)); }, Modifier().fillMaxWidth(), {.singleLine = true});
        });
    });
}

// The mouse on the desktop: onPointerEvent (in, out, moving, buttons, the wheel), onClick by
// button and keys held, TooltipArea.
void PointerSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto inside = remember([] { return mutableStateOf(false); });
        auto at = remember([] { return mutableStateOf(Offset{}); });
        auto wheel = remember([] { return mutableStateOf(0.f); });
        auto button = remember([] { return mutableStateOf(std::string("-")); });
        auto clicks = remember([] { return mutableStateOf(std::array<int, 3>{}); });
        Section("onPointerEvent · onClick · TooltipArea (fare)", [&] {
            Text("Masaüstünde: fareyi kutunun üstünde gezdirin, tekerleği çevirin (sayfa kaymaz, sayaç değişir), tuşlara basın.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            auto bump = [clicks](int i) {
                return [clicks, i] {
                    auto c = clicks.peek();
                    ++c[size_t(i)];
                    clicks.set(c);
                };
            };
            const char* names[] = {"birincil", "ikincil", "orta", "geri", "ileri"};
            Box(Modifier()
                    .fillMaxWidth()
                    .height(96)
                    .background(inside.get() ? cs.tertiaryContainer : cs.surfaceContainerHigh, 16)
                    .onPointerEvent(PointerEventType::Enter, [inside](PointerEvent&) { inside.set(true); })
                    .onPointerEvent(PointerEventType::Exit, [inside](PointerEvent&) { inside.set(false); })
                    .onPointerEvent(PointerEventType::Move, [at](PointerEvent& e) { at.set(e.change.position); })
                    .onPointerEvent(PointerEventType::Press,
                                    [button, names](PointerEvent& e) {
                                        std::string mods = std::string(e.keyboardModifiers.ctrl ? "Ctrl+" : "") + (e.keyboardModifiers.shift ? "Shift+" : "");
                                        button.set(mods + (e.button ? names[int(*e.button)] : "parmak"));
                                    })
                    .onPointerEvent(PointerEventType::Scroll,
                                    [wheel](PointerEvent& e) {
                                        wheel.set(wheel.peek() + e.change.scrollDelta.y);
                                        e.change.consume();  // the page doesn't scroll
                                    }),
                Alignment::Center, [&] {
                    Text(std::format("{} · {:.0f}, {:.0f} · tekerlek {:+.0f} · son tuş: {}", inside.get() ? "içeride" : "dışarıda", at.get().x,
                                     at.get().y, wheel.get(), button.get()),
                         {.color = inside.get() ? cs.onTertiaryContainer : cs.onSurface});
                });
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                auto chip = [&](const std::string& label, int i, OnClickOptions o) {
                    Box(Modifier().weight(1).height(48).background(cs.secondaryContainer, 12).onClick(bump(i), std::move(o)), Alignment::Center,
                        [&] { Text(std::format("{}: {}", label, clicks.get()[size_t(i)]), {.font = typography().labelLarge}); });
                };
                chip("Sağ tık", 0, {.matcher = PointerMatcher::mouse(PointerButton::Secondary)});
                chip("Orta tık", 1, {.matcher = PointerMatcher::mouse(PointerButton::Tertiary)});
                chip("Ctrl+tık", 2, {.keyboardModifiers = [](const KeyModifiers& m) { return m.ctrl; }});
            });
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                TooltipArea(
                    [&] {
                        Text("İmlecin altında çıkan ipucu", Modifier().shadow(4, 4).background(cs.inverseSurface, 4).padding(8, 4),
                             {.font = typography().bodySmall, .color = cs.inverseOnSurface});
                    },
                    Modifier().weight(1),
                    [&] { OutlinedButton([] {}, Modifier().fillMaxWidth(), {}, [] { Text("Üstünde bekleyin"); }); });
                TooltipArea(
                    [&] {
                        Text("Düğmenin altında, ortada", Modifier().shadow(4, 4).background(cs.inverseSurface, 4).padding(8, 4),
                             {.font = typography().bodySmall, .color = cs.inverseOnSurface});
                    },
                    Modifier().weight(1), {.delayMillis = 300, .tooltipPlacement = TooltipPlacement::ComponentRect(Alignment::BottomCenter, Alignment::BottomCenter, {0, 4})},
                    [&] { OutlinedButton([] {}, Modifier().fillMaxWidth(), {}, [] { Text("ComponentRect"); }); });
            });
        });
    });
}

// Indications of one's own (IndicationNodeFactory): one shrinks what is pressed a little (as
// Compose's ScaleIndication sample), the other lights its outline while pressed, hovered or
// focused.
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
        if (float a = alpha_.value(); a > 0) d.drawRoundRect(color_.copy(a), 12, {1.5f, 1.5f}, Size{d.size().width - 3, d.size().height - 3}, Stroke(3));
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

void CustomIndicationSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto& shrink = remember([] { return Indication(std::make_shared<ShrinkIndication>()); });
        auto& outline = remember([primary = cs.primary] { return Indication(std::make_shared<OutlineIndication>(primary)); });
        auto taps = remember([] { return mutableStateOf(0); });
        Section("IndicationNodeFactory · özel göstergeler", [&] {
            // The indication draws what comes after the clickable: put it before the background
            // to shrink the whole button (a Material button's clickable comes after its background).
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically}, [&] {
                for (auto [label, bg, fg] : {std::tuple{"Küçülür", cs.primary, cs.onPrimary}, std::tuple{"Bu da", cs.secondaryContainer, cs.onSecondaryContainer}})
                    Box(Modifier().clickable([taps] { taps.set(taps.peek() + 1); }, {.indication = shrink}).background(bg, 20).padding(20, 10),
                        Alignment::Center, [label, fg] { Text(label, {.font = typography().labelLarge, .color = fg}); });
                Text(std::format("{} dokunuş", taps.get()), {.font = typography().bodyMedium});
            });
            Box(Modifier()
                    .fillMaxWidth()
                    .height(64)
                    .background(cs.surfaceContainerHigh, 12)
                    .clickable([taps] { taps.set(taps.peek() + 1); }, {.indication = outline})
                    .padding(16, 0),
                Alignment::Center, [] { Text("Basınca (fareyle üzerine gelince, odaklanınca) çerçevesi yanar"); });
            Text("clickable'ın indication'ı (ya da LocalIndication): kendi IndicationNode'u etkileşimleri dinler, ondan sonrasını sarıp çizer.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
        });
    });
}

// A text toolbar of the app's own (LocalTextToolbar): Kompoz tells it where the selection is and
// what can be done; it shows its own pill over it (in a popup, as the platform's floats in a
// window of its own: a press on it doesn't end the selection).
class PillToolbar final : public TextToolbar {
public:
    explicit PillToolbar(MutableState<int> changes) : changes_(changes) {}
    void showMenu(Rect rect, std::function<void()> copy, std::function<void()> paste, std::function<void()> cut,
                  std::function<void()> selectAll) override {
        rect_ = rect;
        actions_ = {{"✂ Kes", std::move(cut)}, {"⧉ Kopyala", std::move(copy)}, {"📋 Yapıştır", std::move(paste)}, {"▣ Tümü", std::move(selectAll)}};
        shown_ = true;
        changes_.set(changes_.peek() + 1);
    }
    void hide() override {
        shown_ = false;
        changes_.set(changes_.peek() + 1);
    }
    TextToolbarStatus status() const override { return shown_ ? TextToolbarStatus::Shown : TextToolbarStatus::Hidden; }
    bool shown_ = false;
    Rect rect_;
    std::vector<std::pair<const char*, std::function<void()>>> actions_;

private:
    MutableState<int> changes_;
};

void TextToolbarSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto changes = remember([] { return mutableStateOf(0); });
        auto& pill = remember([changes] { return std::make_shared<PillToolbar>(changes); });
        auto text = remember([] { return mutableStateOf(std::string("Bir kelimeye uzun basın")); });
        Section("LocalTextToolbar · TextToolbar", [&] {
            CompositionLocalProvider({LocalTextToolbar.provides(std::shared_ptr<TextToolbar>(pill))}, [&] {
                OutlinedTextField(text.get(), [text](std::string v) { text.set(std::move(v)); }, Modifier().fillMaxWidth(),
                                  {.singleLine = true, .label = "Kendi araç çubuğumuz"});
                SelectionContainer(Modifier(), [] { Text("Bu metin de seçilebilir: uzun basın, kopyala ve tümünü seç çıkar."); });
            });
            (void)changes.get();  // shown and hidden by the toolbar's calls
            if (pill->shown_) {
                Rect r = pill->rect_;
                Popup({.position = [r](const Rect&, Size size, Size viewport) {
                           float y = r.y - size.height - 12;
                           return Offset{std::clamp(r.x + r.width / 2 - size.width / 2, 8.f, std::max(8.f, viewport.width - size.width - 8)),
                                         y < 0 ? r.bottom() + 36 : y};
                       },
                       .focusable = false,
                       .touchable = true},
                      [&] {
                          Row(Modifier().shadow(4, 24).background(cs.tertiaryContainer, 24).padding(4), {.horizontalArrangement = Arrangement::spacedBy(2)}, [&] {
                              for (const auto& [label, action] : pill->actions_)
                                  if (action)
                                      Box(Modifier().clickable(action).padding(12, 8), Alignment::Center,
                                          [label, c = cs.onTertiaryContainer] { Text(label, {.font = typography().labelLarge, .color = c}); });
                          });
                      });
            }
            Text("Seçim çubuğu Kompoz'unki değil: LocalTextToolbar'a seçimin yeri ve yapılabilecekler bildirilir, o kendisininkini gösterir.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
        });
    });
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
void MultiTouchSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto scale = remember([] { return mutableStateOf(1.f); });
        auto rotation = remember([] { return mutableStateOf(0.f); });
        auto offset = remember([] { return mutableStateOf(Offset{}); });
        auto state = rememberTransformableState([scale, rotation, offset](float zoom, Offset pan, float turn) {
            scale.set(std::clamp(scale.peek() * zoom, 1.f, 6.f));
            rotation.set(rotation.peek() + turn);
            offset.set({offset.peek().x + pan.x, offset.peek().y + pan.y});
        });
        ImageBitmap photo = imageResource("images/landscape.png");
        Section("Çoklu dokunma (transformable)", [&] {
            Text("İki parmakla yakınlaştırın ve döndürün; yakınken tek parmakla kaydırın, çift dokunuş sıfırlar.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Box(Modifier()
                    .fillMaxWidth()
                    .height(220)
                    .clip(RoundedCornerShape(16))
                    .background(cs.surfaceContainerHighest)
                    .transformable(state, [scale](Offset) { return scale.peek() > 1.01f; })
                    .pointerInput(0, [scale, rotation, offset](PointerInputScope& s) -> Task {
                        co_await detectTapGestures(s, {.onDoubleTap = [scale, rotation, offset](Offset) {
                                                           scale.set(1);
                                                           rotation.set(0);
                                                           offset.set({});
                                                       }});
                    }),
                Alignment::Center, [&] {
                    // Read while drawing: the gesture doesn't recompose the image.
                    Image(photo, "Manzara",
                          Modifier().fillMaxSize().graphicsLayer([scale, rotation, offset](GraphicsLayerScope& l) {
                              l.scaleX = l.scaleY = scale.get();
                              l.rotationZ = rotation.get();
                              l.translationX = offset.get().x;
                              l.translationY = offset.get().y;
                          }),
                          ContentScale::Crop);
                });
            Text(std::format("Ölçek ×{:.2f} · açı {:.0f}°", scale.get(), rotation.get()),
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
        });
    });
}

// Hardware keyboard: Tab / arrows move focus between clickables, Enter / Space click them, and
// a focusable() box reports the keys it gets.
void KeyboardSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto clicks = remember([] { return mutableStateOf(std::vector<int>(9, 0)); });
        auto lastKey = remember([] { return mutableStateOf(std::string("yok")); });
        auto boxFocused = remember([] { return mutableStateOf(false); });
        Section("Klavye ve odak (focusable, onKeyEvent)", [&] {
            DesktopNote("Masaüstü için: telefonda yalnızca bir donanım klavyesi (Bluetooth ya da USB) bağlıyken çalışır.");
            Text("Tab / Shift+Tab ve oklarla gezin, Enter ya da Space ile tıklayın (masaüstü, donanım klavyesi).",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            for (int row = 0; row < 3; ++row)
                Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                    for (int col = 0; col < 3; ++col) {
                        int i = row * 3 + col;
                        Box(Modifier().weight(1).height(48).background(cs.secondaryContainer, 12).clickable([clicks, i] {
                                auto v = clicks.get();
                                ++v[size_t(i)];
                                clicks.set(v);
                            }),
                            Alignment::Center, [&] {
                                Text(std::format("{} · {}", i + 1, clicks.get()[size_t(i)]), {.color = cs.onSecondaryContainer});
                            });
                    }
                });
            Box(Modifier()
                    .fillMaxWidth()
                    .height(56)
                    .border(2, boxFocused.get() ? cs.primary : cs.outlineVariant, 12)
                    .onFocusChanged([boxFocused](FocusState s) { boxFocused.set(s.isFocused); })
                    .onKeyEvent([lastKey](const KeyEvent& e) {
                        if (e.key == Key::Tab || e.key == Key::Escape) return false;  // keep moving focus / leaving
                        lastKey.set(std::format("{}{}{} {}", e.isCtrlPressed() ? "Ctrl+" : "", e.isShiftPressed() ? "Shift+" : "",
                                                keyName(e.key), e.type == KeyEventType::KeyDown ? "basıldı" : "bırakıldı"));
                        return true;
                    })
                    .focusable()
                    .padding(16, 0),
                Alignment::CenterStart, [&] {
                    Text(boxFocused.get() ? "Son tuş: " + lastKey.get() : "Tab ile bu kutuya gelin, sonra bir tuşa basın",
                         {.color = boxFocused.get() ? cs.onSurface : cs.onSurfaceVariant});
                });
        });
    });
}

// A card that animates several values together when it gets selected.
void SelectableCard(int index, MutableState<int> selected, CallSite site) {
    composable(site, index, selected, [=] {
        const ColorScheme& cs = colorScheme();
        auto t = updateTransition(selected.get() == index);
        Color container = t.animateColor([&](const bool& on) { return on ? cs.primaryContainer : cs.surfaceContainerHigh; });
        Color content = t.animateColor([&](const bool& on) { return on ? cs.onPrimaryContainer : cs.onSurfaceVariant; });
        float lift = t.animateFloat([](const bool& on) { return on ? 1.f : 0.f; }, spring(Spring::DampingRatioMediumBouncy));
        Column(Modifier().weight(1).height(84).graphicsLayer({.scaleX = 1 + 0.05f * lift, .scaleY = 1 + 0.05f * lift})
                   .background(container, 16).clickable([selected, index] { selected.set(index); }).padding(12),
               {.verticalArrangement = Arrangement::SpaceBetween}, [&] {
                   Icon(index == 0 ? Icons::Filled::Home : index == 1 ? Icons::Filled::Star : Icons::Filled::Favorite, "Kart", {}, content);
                   Text(t.currentState() ? "Seçili" : t.isRunning() ? "…" : std::format("Kart {}", index + 1),
                        {.font = typography().labelLarge, .color = content});
               });
    });
}

// The layout toolkit: a custom Layout, IntrinsicSize, FlowRow and updateTransition.
// Drag and drop: cards moved between two columns. A finger holds a card, then drags it (the
// mouse just drags); the other column lights up as it takes part, more under the pointer.
void DragAndDropSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        struct Card {
            int id;
            std::string text;
            int column;  // 0: to do, 1: done
            bool operator==(const Card&) const = default;
        };
        auto cards = remember([] {
            return mutableStateOf(std::vector<Card>{{0, "Taslağı yaz", 0}, {1, "Testleri çalıştır", 0}, {2, "Ekran görüntüsü", 0},
                                                    {3, "Derle", 1}});
        });
        auto taking = remember([] { return mutableStateOf(-1); });  // the column a drag could go to
        auto over = remember([] { return mutableStateOf(-1); });    // the column under the pointer
        auto dropped = remember([] { return mutableStateOf(std::string()); });
        auto overDrop = remember([] { return mutableStateOf(false); });
        Section("Sürükle-bırak (dragAndDropSource / dragAndDropTarget)", [&] {
            Text("Bir kartı basılı tutup öbür sütuna sürükleyin (fareyle doğrudan sürükleyin); Escape vazgeçer. Masaüstünde "
                 "dosya yöneticisinden ya da tarayıcıdan aşağıdaki alana dosya / metin de bırakabilirsiniz.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            std::vector<Card> all = cards.get();
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                for (int column = 0; column < 2; ++column) {
                    Color tint = over.get() == column ? cs.primaryContainer : taking.get() == column ? cs.secondaryContainer : cs.surfaceContainerHigh;
                    Column(Modifier()
                               .weight(1)
                               .heightIn(180)
                               .background(tint, 16)
                               .dragAndDropTarget(
                                   [column](const DragAndDropEvent& e) {
                                       const Card* card = e.localState<Card>();
                                       return card && card->column != column;  // from the other column
                                   },
                                   {.onDrop =
                                        [cards, column](const DragAndDropEvent& e) {
                                            std::vector<Card> v = cards.peek();
                                            for (Card& c : v)
                                                if (c.id == e.localState<Card>()->id) c.column = column;
                                            cards.set(std::move(v));
                                            return true;
                                        },
                                    .onStarted = [taking, column](const DragAndDropEvent&) { taking.set(column); },
                                    .onEntered = [over, column](const DragAndDropEvent&) { over.set(column); },
                                    .onExited = [over](const DragAndDropEvent&) { over.set(-1); },
                                    .onEnded =
                                        [taking, over](const DragAndDropEvent&) {
                                            taking.set(-1);
                                            over.set(-1);
                                        }})
                               .padding(10),
                           {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
                               Text(column == 0 ? "Yapılacak" : "Bitti", {.font = typography().titleSmall});
                               for (const Card& card : all) {
                                   if (card.column != column) continue;
                                   key(card.id, [&] {
                                       Box(Modifier()
                                               .fillMaxWidth()
                                               .shadow(1, 12)
                                               .background(cs.surface, 12)
                                               .dragAndDropSource([card](Offset) -> std::optional<DragAndDropTransferData> {
                                                   return DragAndDropTransferData{.text = card.text, .localState = card};
                                               })
                                               .padding(12, 10),
                                           [&] { Text(card.text, {.font = typography().bodyMedium}); });
                                   });
                               }
                           });
                }
            });
            // Anything with text: a card, files from a file manager (their paths), text from a browser.
            Box(Modifier()
                    .fillMaxWidth()
                    .heightIn(64)
                    .background(overDrop.get() ? cs.primaryContainer : cs.surfaceContainerHigh, 16)
                    .border(1, cs.outlineVariant, 16)
                    .dragAndDropTarget([](const DragAndDropEvent& e) { return e.hasMimeType("text/uri-list") || e.hasMimeType("text/plain"); },
                                       {.onDrop =
                                            [dropped](const DragAndDropEvent& e) {
                                                std::string what;
                                                if (!e.data->files.empty()) {
                                                    for (size_t i = 0; i < e.data->files.size() && i < 3; ++i) {
                                                        const std::string& path = e.data->files[i];
                                                        what += (i ? ", " : "") + path.substr(path.find_last_of("/\\") + 1);
                                                    }
                                                    if (e.data->files.size() > 3) what += std::format(" (+{})", e.data->files.size() - 3);
                                                } else {
                                                    what = e.text().size() > 80 ? e.text().substr(0, 80) + "…" : e.text();
                                                }
                                                dropped.set(what);
                                                return true;
                                            },
                                        .onEntered = [overDrop](const DragAndDropEvent&) { overDrop.set(true); },
                                        .onExited = [overDrop](const DragAndDropEvent&) { overDrop.set(false); },
                                        .onEnded = [overDrop](const DragAndDropEvent&) { overDrop.set(false); }})
                    .padding(14, 10),
                Alignment::CenterStart, [&] {
                    Text(dropped.get().empty() ? std::string("Buraya bırakın") : "Bırakılan: " + dropped.get(),
                         {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                });
        });
    });
}
