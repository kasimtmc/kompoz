// Material screen: buttons, chips and selection controls.

#include "showcase.hpp"

void ButtonsSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        Section("Butonlar", [&] {
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                Button([] {}, [] { Text("Button"); });
                FilledTonalButton([] {}, [] { Text("Tonal"); });
                ElevatedButton([] {}, [] { Text("Elevated"); });
            });
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                OutlinedButton([] {}, [] { Text("Outlined"); });
                TextButton([] {}, [] { Text("Text"); });
                Button([] {}, Modifier(), {.enabled = false}, [] { Text("Kapalı"); });
            });
            // MotionScheme: the buttons below on Material 3's standard springs or the expressive ones
            // (MaterialExpressiveTheme's), which bounce as the corners morph and the held button grows.
            auto expressiveMotion = remember([] { return mutableStateOf(false); });
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Switch(expressiveMotion.get(), [expressiveMotion](bool on) { expressiveMotion.set(on); });
                Text(expressiveMotion.get() ? "MotionScheme.expressive()" : "MotionScheme.standard()", {.font = typography().bodyMedium});
            });
            CompositionLocalProvider({LocalMotionScheme.provides(expressiveMotion.get() ? MotionScheme::expressive() : MotionScheme::standard())}, [&] {
                // M3 Expressive: the corners morph, squarer while held; toggle buttons turn square when checked.
                Button([] {}, Modifier(), {.shapes = ButtonShapes{}}, [] { Text("Basılı tut: köşeler"); });
                // ButtonGroup: the button held grows and squeezes its neighbours.
                ButtonGroup([&](const ButtonGroupScope& group) {
                    const char* labels[] = {"Önceki", "Oynat", "Sonraki"};
                    for (int i = 0; i < 3; ++i) {
                        auto source = remember([] { return MutableInteractionSource(); });
                        if (i == 1)
                            Button([] {}, group.animateWidth(source), {.interactionSource = source}, [&] { Text(labels[i]); });
                        else
                            FilledTonalButton([] {}, group.animateWidth(source), {.interactionSource = source}, [&] { Text(labels[i]); });
                    }
                });
            });
            auto styles = remember([] { return mutableStateOf(std::array<bool, 4>{true, false, false, false}); });
            auto flip = [styles](size_t i) {
                return [styles, i](bool on) {
                    auto s = styles.peek();
                    s[i] = on;
                    styles.set(s);
                };
            };
            // From items: what doesn't fit goes into the ⋮ menu at the end.
            auto bold = remember([] { return mutableStateOf(false); });
            auto lastAction = remember([] { return mutableStateOf(std::string("yok")); });
            ButtonGroup({{.label = "Kopyala", .onClick = [lastAction] { lastAction.set("Kopyala"); }},
                         {.label = "Yapıştır", .onClick = [lastAction] { lastAction.set("Yapıştır"); }},
                         {.label = "Kalın", .checked = bold.get(), .onCheckedChange = [bold](bool on) { bold.set(on); }},
                         {.label = "Sil", .icon = &Icons::Filled::Delete, .onClick = [lastAction] { lastAction.set("Sil"); }},
                         {.label = "Paylaş", .icon = &Icons::Filled::Share, .onClick = [lastAction] { lastAction.set("Paylaş"); }}},
                        Modifier().fillMaxWidth());
            Text("Son eylem: " + lastAction.get() + (bold.get() ? " · kalın" : ""), {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
            // A split button: the menu button turns round while its menu is open.
            auto saveMenu = remember([] { return mutableStateOf(false); });
            Box([&] {
                SplitButtonLayout([&] { SplitButtonDefaults::LeadingButton([] {}, [] { Text("Kaydet"); }); },
                                  [&] {
                                      SplitButtonDefaults::TrailingButton(saveMenu.get(), [saveMenu](bool on) { saveMenu.set(on); },
                                                                          [&] { SplitButtonDefaults::TrailingButtonIcon(saveMenu.get()); });
                                  });
                DropdownMenu(saveMenu.get(), [saveMenu] { saveMenu.set(false); }, [&] {
                    DropdownMenuItem([] { Text("Farklı kaydet"); }, [saveMenu] { saveMenu.set(false); });
                    DropdownMenuItem([] { Text("Taslak olarak kaydet"); }, [saveMenu] { saveMenu.set(false); });
                });
            });
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8}, [&] {
                const auto& on = styles.get();
                ToggleButton(on[0], flip(0), [] { Text("Kalın"); });
                ElevatedToggleButton(on[1], flip(1), [] { Text("İtalik"); });
                TonalToggleButton(on[2], flip(2), [] { Text("Altı çizili"); });
                OutlinedToggleButton(on[3], flip(3), [] { Text("Üstü çizili"); });
            });
        });
    });
}

void ChipsSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto drinks = remember([] { return mutableStateListOf<std::string>({"Kahve"}); });
        auto people = remember([] { return mutableStateListOf<std::string>({"Ayşe", "Mehmet"}); });
        auto openOnly = remember([] { return mutableStateOf(false); });
        Section("Chip'ler", [&] {
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8}, [&] {
                for (const char* drink : {"Kahve", "Çay", "Su", "Ayran"}) {
                    bool on = std::find(drinks.begin(), drinks.end(), drink) != drinks.end();
                    FilterChip(on,
                               [drinks, drink, on] {
                                   if (on)
                                       drinks.removeIf([&](const std::string& d) { return d == drink; });
                                   else
                                       drinks.add(drink);
                               },
                               [drink] { Text(drink); },
                               {.leadingIcon = on ? Content([] { Icon(Icons::Filled::Check, "Seçili", Modifier().size(18)); })
                                                  : Content()});
                }
            });
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8}, [&] {
                AssistChip([] {}, [] { Text("Yol tarifi"); },
                           {.leadingIcon = [] { Icon(Icons::Filled::Place, "Konum", Modifier().size(18)); }});
                SuggestionChip([] {}, [] { Text("Bugün"); });
                // Elevated: a shadow instead of the border.
                ElevatedAssistChip([] {}, [] { Text("Paylaş"); },
                                   {.leadingIcon = [] { Icon(Icons::Filled::Share, "Paylaş", Modifier().size(18)); }});
                ElevatedFilterChip(openOnly.get(), [openOnly] { openOnly.set(!openOnly.get()); }, [] { Text("Şu an açık"); });
                ElevatedSuggestionChip([] {}, [] { Text("Yarın"); });
                for (const std::string& person : people)
                    InputChip(false, [] {}, [person] { Text(person); },
                              {.trailingIcon = [people, person] {
                                  IconButton([people, person] {
                                      people.removeIf([&](const std::string& p) { return p == person; });
                                  }, Modifier().size(24), {}, [] { Icon(Icons::Filled::Close, "Kaldır", Modifier().size(18)); });
                              }});
            });
            std::string chosen;
            for (const std::string& d : drinks) chosen += (chosen.empty() ? "" : ", ") + d;
            Text("Seçilen içecekler: " + (chosen.empty() ? std::string("yok") : chosen),
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
        });
    });
}

void SelectionControlsSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto size = remember([] { return mutableStateOf(std::string("Orta")); });
        auto volume = remember([] { return mutableStateOf(40.f); });
        auto toppings = remember([] { return mutableStateOf(std::array<bool, 3>{true, false, false}); });
        auto rating = remember([] { return mutableStateOf(3.f); });
        auto price = remember([] { return mutableStateOf(std::pair(200.f, 800.f)); });
        // The sliders' interaction sources: the value being moved stands out while held.
        auto volumeSource = remember([] { return MutableInteractionSource(); });
        auto priceFrom = remember([] { return MutableInteractionSource(); });
        auto priceTo = remember([] { return MutableInteractionSource(); });
        auto period = remember([] { return mutableStateOf(1); });
        auto transport = remember([] { return mutableStateOf(std::array<bool, 3>{true, false, false}); });
                   auto touchRoom = remember([] { return mutableStateOf(true); });
        Section("Seçim kontrolleri", [&] {
            // minimumInteractiveComponentSize: 48 dp of room to touch the controls (0: just their own size).
            Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                Switch(touchRoom.get(), [touchRoom](bool v) { touchRoom.set(v); });
                Text("48 dp dokunma alanı", Modifier().padding(12, 0, 0, 0));
            });
            CompositionLocalProvider({LocalMinimumInteractiveComponentSize.provides(touchRoom.get() ? 48.f : 0.f)}, [&] {
                // TriStateCheckbox: all, none or some of the toppings (Indeterminate: a dash).
                ToggleableState all = toppings.get()[0] && toppings.get()[1] && toppings.get()[2] ? ToggleableState::On
                                      : toppings.get()[0] || toppings.get()[1] || toppings.get()[2] ? ToggleableState::Indeterminate
                                                                                                      : ToggleableState::Off;
                Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    TriStateCheckbox(all, [toppings, all] {
                        bool on = all != ToggleableState::On;
                        toppings.set({on, on, on});
                    });
                    Text("Hepsi (TriStateCheckbox)");
                });
                // Each box with its label, onto the next line together where the row is narrow.
                FlowRow(Modifier().padding(24, 0, 0, 0), {}, [&] {
                    const char* names[] = {"Peynir", "Zeytin", "Mantar"};
                    for (int i = 0; i < 3; ++i)
                        Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                            Checkbox(toppings.get()[size_t(i)], [toppings, i](bool v) {
                                auto t = toppings.peek();
                                t[size_t(i)] = v;
                                toppings.set(t);
                            });
                            Text(names[i], Modifier().padding(0, 0, 8, 0));
                        });
                });
                Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    for (const char* option : {"Küçük", "Orta", "Büyük"}) {
                        RadioButton(size.get() == option, [size, option] { size.set(option); });
                        Text(option, Modifier().padding(0, 0, 12, 0));
                    }
                });
                Row(Modifier().fillMaxWidth(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    Icon(Icons::Filled::Notifications, "Ses");
                    Slider(volume.get(), [volume](float v) { volume.set(v); }, Modifier().weight(1).padding(8, 0),
                           {.valueFrom = 0, .valueTo = 100, .interactionSource = volumeSource});
                    bool held = volumeSource.collectIsPressedAsState() || volumeSource.collectIsDraggedAsState();
                    Text(std::format("{:.0f}", volume.get()), Modifier().width(32),
                         {.fontWeight = held ? std::optional(FontWeight::Bold) : std::nullopt,
                          .color = held ? std::optional(cs.primary) : std::nullopt,
                          .textAlign = TextAlign::End});
                });
                Row(Modifier().fillMaxWidth(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    Icon(Icons::Filled::Star, "Puan");
                    Slider(rating.get(), [rating](float v) { rating.set(v); }, Modifier().weight(1).padding(8, 0),
                           {.valueFrom = 1, .valueTo = 5, .steps = 3});
                    Text(std::format("{:.0f}", rating.get()), Modifier().width(32), {.textAlign = TextAlign::End});
                });
                // systemGestureExclusion: two sliders out to the window's edges; a drag
                // from the edge moves the first, but is the system's back on the second.
                {
                    auto edgeA = remember([] { return mutableStateOf(0.05f); });
                    auto edgeB = remember([] { return mutableStateOf(0.05f); });
                    float window = LocalWindowSize.current().width;
                    Text("Kenardan kenara (üstteki systemGestureExclusion ile korunur):", {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                    Slider(edgeA.get(), [edgeA](float v) { edgeA.set(v); }, Modifier().requiredWidth(window).systemGestureExclusion());
                    Slider(edgeB.get(), [edgeB](float v) { edgeB.set(v); }, Modifier().requiredWidth(window));
                }
                Row([&] {  // the end being dragged is shown bold
                    auto end = [&](float v, const MutableInteractionSource& source) {
                        bool held = source.collectIsPressedAsState() || source.collectIsDraggedAsState();
                        Text(std::format("{:.0f}", v), {.font = typography().bodyMedium,
                                                        .fontWeight = held ? std::optional(FontWeight::Bold) : std::nullopt,
                                                        .color = held ? cs.primary : cs.onSurfaceVariant});
                    };
                    Text("Fiyat aralığı: ", {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                    end(price.get().first, priceFrom);
                    Text(" – ", {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                    end(price.get().second, priceTo);
                    Text(" ₺", {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                });
                RangeSlider(price.get(), [price](std::pair<float, float> v) { price.set(v); }, Modifier().fillMaxWidth(),
                            {.valueFrom = 0, .valueTo = 1000, .steps = 19, .interactionSource = priceFrom,
                             .endInteractionSource = priceTo});
                // M3 Expressive's vertical sliders: up for more (the second one in five steps).
                auto volume = remember([] { return mutableStateOf(0.6f); });
                auto light = remember([] { return mutableStateOf(0.5f); });
                Row(Modifier().height(160), {.horizontalArrangement = Arrangement::spacedBy(24), .verticalAlignment = Alignment::CenterVertically}, [&] {
                    VerticalSlider(volume.get(), [volume](float v) { volume.set(v); });
                    VerticalSlider(light.get(), [light](float v) { light.set(v); }, Modifier(), {.steps = 3});
                    Text(std::format("Ses %{} · Işık %{}", int(std::lround(volume.get() * 100)), int(std::lround(light.get() * 100))),
                         {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                });
                // The thumb and track to pick: the classic circle, Material 3's bar, shapes, an icon
                // or the value; the classic thin track or Material 3's thick one; steps or none.
                auto thumbKind = remember([] { return mutableStateOf(0); });
                auto thickTrack = remember([] { return mutableStateOf(false); });
                auto stepped = remember([] { return mutableStateOf(false); });
                auto bubble = remember([] { return mutableStateOf(true); });
                auto picked = remember([] { return mutableStateOf(40.f); });
                Text("Slider imleci", {.font = typography().titleSmall});
                const char* thumbs[] = {"Daire", "M3 çubuk", "Kare", "Elmas", "Kurabiye", "Güneş", "Simge", "Değer"};
                FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 0}, [&] {
                    for (int i = 0; i < 8; ++i)
                        FilterChip(thumbKind.get() == i, [thumbKind, i] { thumbKind.set(i); }, [&, i] { Text(thumbs[i]); });
                });
                Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                    FilterChip(thickTrack.get(), [thickTrack] { thickTrack.set(!thickTrack.get()); }, [] { Text("M3 iz"); });
                    FilterChip(stepped.get(), [stepped] { stepped.set(!stepped.get()); }, [] { Text("10 adım"); });
                    FilterChip(bubble.get(), [bubble] { bubble.set(!bubble.get()); }, [] { Text("Değer balonu"); });
                });
                auto thumbFor = [](int kind) -> std::function<void(const SliderState&)> {
                    switch (kind) {
                    case 1: return [](const SliderState& s) { SliderDefaults::Thumb(s); };
                    case 2: return [](const SliderState& s) { SliderDefaults::ShapeThumb(s, RoundedCornerShape(6), {22, 22}); };
                    case 3: return [](const SliderState& s) { SliderDefaults::ShapeThumb(s, OutlineShape(MaterialShapes::Diamond()), {26, 26}); };
                    case 4: return [](const SliderState& s) { SliderDefaults::ShapeThumb(s, OutlineShape(MaterialShapes::Cookie9Sided()), {28, 28}); };
                    case 5: return [](const SliderState& s) { SliderDefaults::ShapeThumb(s, OutlineShape(MaterialShapes::Sunny()), {28, 28}); };
                    case 6: return [](const SliderState& s) { SliderDefaults::IconThumb(s, Icons::Filled::Favorite); };
                    case 7: return [](const SliderState& s) { SliderDefaults::ValueThumb(s); };
                    default: return {};  // the classic circle
                    }
                };
                // With the bubble: the thumb picked, in a Label showing the value while it is held.
                auto withBubble = [&](std::function<void(const SliderState&)> thumb) -> std::function<void(const SliderState&)> {
                    if (!bubble.get()) return thumb;
                    return [thumb](const SliderState& s) {
                        SliderDefaults::Label(s, [&] { PlainTooltip([&] { Text(std::format("{:.0f}", s.value())); }); }, [&] {
                            if (thumb) thumb(s);
                            else SliderDefaults::CircleThumb(s);
                        });
                    };
                };
                std::function<void(const SliderState&)> track;
                if (thickTrack.get()) track = [](const SliderState& s) { SliderDefaults::Track(s); };
                Slider(picked.get(), [picked](float v) { picked.set(v); }, Modifier().fillMaxWidth(),
                       {.valueFrom = 0, .valueTo = 100, .steps = stepped.get() ? 9 : 0, .thumb = withBubble(thumbFor(thumbKind.get())), .track = track});
                // The same thumbs on a range (each its own), the track picked alike.
                auto pickedRange = remember([] { return mutableStateOf(std::pair{20.f, 70.f}); });
                std::function<void(const RangeSliderState&)> rangeTrack;
                if (thickTrack.get()) rangeTrack = [](const RangeSliderState& s) { SliderDefaults::Track(s); };
                RangeSlider(pickedRange.get(), [pickedRange](std::pair<float, float> v) { pickedRange.set(v); }, Modifier().fillMaxWidth(),
                            {.valueFrom = 0, .valueTo = 100, .steps = stepped.get() ? 9 : 0, .thumb = withBubble(thumbFor(thumbKind.get())),
                             .rangeTrack = rangeTrack});
                // States of their own (rememberSliderState, rememberRangeSliderState): Material 3's look, values shown.
                auto hours = rememberRangeSliderState(9, 17, 23, 0, 24);
                Row(Modifier().fillMaxWidth(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    Text(std::format("Saat: {:02.0f}–{:02.0f}", hours.activeRangeStart(), hours.activeRangeEnd()), Modifier().width(120),
                         {.font = typography().bodyMedium});
                    RangeSlider(hours, Modifier().weight(1),
                                {.thumb = [](const SliderState& s) { SliderDefaults::ValueThumb(s); },
                                 .rangeTrack = [](const RangeSliderState& s) { SliderDefaults::Track(s); }});
                });
                auto stars = rememberSliderState(3, 3, 1, 5);
                Row(Modifier().fillMaxWidth(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    Text(std::format("SliderState: {:.0f} ★", stars.value()), Modifier().width(120), {.font = typography().bodyMedium});
                    Slider(stars, Modifier().weight(1),
                           {.thumb = [](const SliderState& s) { SliderDefaults::ValueThumb(s); },
                            .track = [](const SliderState& s) { SliderDefaults::Track(s); }});
                });
                SingleChoiceSegmentedButtonRow(Modifier().fillMaxWidth(), [&] {
                    const char* labels[] = {"Gün", "Hafta", "Ay"};
                    for (int i = 0; i < 3; ++i)
                        SegmentedButton(period.get() == i, [period, i] { period.set(i); }, [&, i] { Text(labels[i]); });
                });
                // Several at once (MultiChoiceSegmentedButtonRow): each on or off; one that can't be picked.
                MultiChoiceSegmentedButtonRow(Modifier().fillMaxWidth(), [&] {
                    const char* ways[] = {"Yürüyüş", "Bisiklet", "Araba"};
                    for (int i = 0; i < 3; ++i)
                        SegmentedButton(transport.get()[size_t(i)], [transport, i](bool on) {
                            auto t = transport.peek();
                            t[size_t(i)] = on;
                            transport.set(t);
                        }, [&, i] { Text(ways[i]); }, Modifier(), {.enabled = i != 2});
                });
                // M3 Expressive's connected button group: toggle buttons 2 dp apart, the checked one round.
                auto view = remember([] { return mutableStateOf(0); });
                Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(ButtonGroupDefaults::ConnectedSpaceBetween)}, [&] {
                    const char* views[] = {"Liste", "Izgara", "Harita"};
                    for (int i = 0; i < 3; ++i)
                        TonalToggleButton(view.get() == i, [view, i](bool) { view.set(i); }, Modifier().weight(1),
                                          {.shapes = i == 0   ? ButtonGroupDefaults::connectedLeadingButtonShapes()
                                                     : i == 2 ? ButtonGroupDefaults::connectedTrailingButtonShapes()
                                                              : ButtonGroupDefaults::connectedMiddleButtonShapes()},
                                          [&, i] { Text(views[i]); });
                });
            });
        });
    });
}
