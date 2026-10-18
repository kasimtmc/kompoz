// Material screen: search, tooltips, menus, interactions, swipes, ripples, semantics, focus, pickers, dynamic color, insets.

#include "showcase.hpp"

// One task row that can be swiped away (left: delete, right: done).
void SwipeableTask(int id, SnapshotStateList<int> tasks, CallSite site) {
    composable(site, id, tasks, [=] {
        const ColorScheme& cs = colorScheme();
        auto state = rememberSwipeToDismissBoxState([tasks, id](SwipeToDismissBoxValue) {
            tasks.removeIf([id](int t) { return t == id; });  // the others glide into place (animateItem)
            return true;
        });
        SwipeToDismissBox(
            state,
            [&] {
                bool del = state.dismissDirection() == SwipeToDismissBoxValue::EndToStart;
                Color color = state.dismissDirection() == SwipeToDismissBoxValue::Settled ? cs.surfaceContainerHigh
                              : del                                                       ? cs.error
                                                                                          : Color::hex(0x2E7D32);
                Box(Modifier().fillMaxSize().background(color, 12).padding(20, 0),
                    del ? Alignment::CenterEnd : Alignment::CenterStart, [&] {
                        Icon(del ? Icons::Filled::Delete : Icons::Filled::Check, del ? "Sil" : "Tamamla", {}, colors::White);
                    });
            },
            [&] {
                ListItem({.headline = [id] { Text(std::format("Görev {}", id)); },
                          .supporting = [] { Text("Sola kaydır: sil · sağa: tamamla"); },
                          .leading = [id] {
                              Box(Modifier().size(36).background(hueColor(id * 5), CircleShape), Alignment::Center,
                                  [id] { Text(std::to_string(id), {.fontWeight = FontWeight::Bold, .color = colors::White}); });
                          },
                          .containerColor = cs.surface},
                         Modifier().clip(RoundedCornerShape(12)));
            },
            {}, Modifier().animateItem());
    });
}

// Material 3 components: buttons, chips, selection controls, tabs, list items, badges, cards
// and the bottom app bar.
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

// A search bar over a list of cities: the suggestions narrow as you type (in either case).
void SearchSection(CallSite site) {
    composable(site, [=] {
        static const std::vector<std::string> cities = {
            "Adana", "Ankara", "Antalya", "Aydın", "Balıkesir", "Bursa", "Denizli", "Diyarbakır", "Edirne", "Erzurum",
            "Eskişehir", "Gaziantep", "Hatay", "İstanbul", "İzmir", "Kayseri", "Kocaeli", "Konya", "Malatya", "Manisa",
            "Mardin", "Mersin", "Muğla", "Ordu", "Rize", "Sakarya", "Samsun", "Trabzon", "Van", "Zonguldak"};
        const ColorScheme& cs = colorScheme();
        auto chosen = remember([] { return mutableStateOf(std::string()); });
        // The same search, full screen (ExpandedFullScreenSearchBar) or opening in place
        // (ExpandedDockedSearchBar), each a SearchBarState with its query in a TextFieldState.
        auto bar = [&](bool docked, SearchBarState state, TextFieldState query) {
            auto scope = rememberCoroutineScope();
            auto close = [state, scope] { scope.launch([state]() -> Task { co_await state.animateToCollapsed(); }); };
            bool expanded = state.targetValue() == SearchBarValue::Expanded;
            auto field = [&] {
                SearchBarDefaults::InputField(state, query,
                                              {.onSearch = [chosen, close](std::string q) { chosen.set(q); close(); },
                                               .placeholder = docked ? "Şehir ara (yerinde)" : "Şehir ara",
                                               .leadingIcon = [&] {
                                                   if (expanded && !docked)
                                                       IconButton(close, [] { Icon(Icons::AutoMirrored::Filled::ArrowBack, "Kapat"); });
                                                   else
                                                       Icon(Icons::Filled::Search, "Ara");
                                               },
                                               .trailingIcon = [&] {
                                                   if (!query.text().empty())
                                                       IconButton([query] { query.clearText(); }, [] { Icon(Icons::Filled::Close, "Temizle"); });
                                               }});
            };
            auto suggestions = [&] {
                auto scroll = rememberScrollState();
                Column(Modifier().fillMaxWidth().verticalScroll(scroll), [&] {
                    std::string typed = query.text();
                    for (const std::string& city : cities) {
                        if (!startsWithIgnoringCase(city, typed)) continue;
                        ListItem({.headline = [city] { Text(city); }, .leading = [] { Icon(Icons::Filled::Place, "Şehir"); },
                                  .containerColor = colors::Transparent},
                                 Modifier().clickable([query, chosen, close, city] {
                                     query.setTextAndPlaceCursorAtEnd(city);
                                     chosen.set(city);
                                     close();
                                 }));
                    }
                });
            };
            SearchBar(state, field, Modifier().fillMaxWidth());
            if (docked)
                ExpandedDockedSearchBar(state, field, suggestions);
            else
                ExpandedFullScreenSearchBar(state, field, suggestions);
        };
        auto state = rememberSearchBarState();
        auto query = rememberTextFieldState();
        auto dockedState = rememberSearchBarState();
        auto dockedQuery = rememberTextFieldState();
        Section("SearchBar · SearchBarState", [&] {
            key(0, [&] { bar(false, state, query); });  // the same composables twice: keyed apart
            key(1, [&] { bar(true, dockedState, dockedQuery); });
            Text(chosen.get().empty() ? "Üstteki tam ekran açılır, alttaki yerinde; yazdıkça öneriler daralır."
                                      : "Seçilen: " + chosen.get(),
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
        });
    });
}

// TooltipBox: a long press (or a resting mouse) shows the tooltip; the rich one stays. The share
// and info tooltips point at their buttons with a caret.
void TooltipSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto liked = remember([] { return mutableStateOf(false); });
        auto side = remember([] { return mutableStateOf(0); });  // where the share tooltip goes
        static const std::pair<TooltipAnchorPosition, const char*> sides[] = {
            {TooltipAnchorPosition::Above, "Üst"}, {TooltipAnchorPosition::Below, "Alt"}, {TooltipAnchorPosition::Start, "Baş"},
            {TooltipAnchorPosition::End, "Son"}};
        Section("TooltipBox · PlainTooltip · RichTooltip · TooltipAnchorPosition", [&] {
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically}, [&] {
                TooltipBox([] { PlainTooltip([] { Text("Favorilere ekle"); }); }, rememberTooltipState(), Modifier(), [&] {
                    IconButton([liked] { liked.set(!liked.get()); }, [liked] {
                        Icon(liked.get() ? Icons::Filled::Favorite : Icons::Filled::FavoriteBorder, "Favori");
                    });
                });
                TooltipBox(TooltipDefaults::rememberTooltipPositionProvider(sides[side.get()].first),
                           [] { PlainTooltip([] { Text("Paylaş"); }, Modifier(), TooltipDefaults::caretSize); }, rememberTooltipState(), Modifier(),
                           [] { IconButton([] {}, [] { Icon(Icons::Filled::Share, "Paylaş"); }); });
                auto rich = rememberTooltipState(false, true);
                TooltipBox(
                    [rich] {
                        RichTooltip({.title = [] { Text("Yedekleme"); },
                                     .text = [] { Text("Fotoğraflarınız her gece Wi-Fi'ye bağlıyken yedeklenir."); },
                                     .action = [rich] { TextButton([rich] { rich.dismiss(); }, [] { Text("Anladım"); }); }},
                                    Modifier(), TooltipDefaults::caretSize);
                    },
                    rich, Modifier(), [rich] { IconButton([rich] { rich.show(); }, [] { Icon(Icons::Filled::Info, "Bilgi"); }); });
            });
            // TooltipAnchorPosition: the share tooltip's side.
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8}, [&] {
                for (int i = 0; i < 4; ++i)
                    FilterChip(side.get() == i, [side, i] { side.set(i); }, [i] { Text(sides[i].second); });
            });
            Text("Telefonda uzun basın, masaüstünde fareyle üzerinde bekleyin; bilgi düğmesi kalıcı ipucunu açar. Paylaş ve bilgi "
                 "ipuçlarının oku düğmeyi gösterir; paylaş ipucu yukarıda seçilen yanda açılır (yer yoksa karşı yanda).",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
        });
    });
}

// ExposedDropdownMenuBox: a read-only field that picks from a menu, and one that filters as you type.
void DropdownSection(CallSite site) {
    composable(site, [=] {
        static const std::vector<std::string> fruits = {"Elma", "Armut", "Kiraz", "Muz", "Çilek", "Karpuz", "Kayısı", "Şeftali",
                                                        "Üzüm", "İncir", "Nar", "Ayva"};
        auto open = remember([] { return mutableStateOf(false); });
        auto fruit = remember([] { return mutableStateOf(std::string("Elma")); });
        auto searching = remember([] { return mutableStateOf(false); });
        auto query = remember([] { return mutableStateOf(std::string()); });
        Section("ExposedDropdownMenuBox", [&] {
            ExposedDropdownMenuBox(open.get(), [open](bool v) { open.set(v); }, Modifier().fillMaxWidth(), [&] {
                TextField(fruit.get(), {}, Modifier().fillMaxWidth(),
                          {.label = "Meyve", .trailingIcon = [&] { ExposedDropdownMenuDefaults::TrailingIcon(open.get()); },
                           .readOnly = true});
                ExposedDropdownMenu(open.get(), [open] { open.set(false); }, [&] {
                    for (const std::string& f : fruits)
                        DropdownMenuItem([f] { Text(f); }, [fruit, open, f] {
                            fruit.set(f);
                            open.set(false);
                        });
                });
            });
            ExposedDropdownMenuBox(searching.get(), [searching](bool v) { searching.set(v); }, Modifier().fillMaxWidth(), [&] {
                OutlinedTextField(query.get(), [query, searching](std::string v) {
                    query.set(std::move(v));
                    searching.set(true);
                }, Modifier().fillMaxWidth(), {.singleLine = true, .label = "Meyve ara (yazdıkça süzülür)",
                                              .trailingIcon = [&] { ExposedDropdownMenuDefaults::TrailingIcon(searching.get()); }});
                ExposedDropdownMenu(searching.get(), [searching] { searching.set(false); }, [&] {
                    for (const std::string& f : fruits)
                        if (startsWithIgnoringCase(f, query.get()))
                            DropdownMenuItem([f] { Text(f); }, [query, searching, f] {
                                query.set(f);
                                searching.set(false);
                            });
                });
            });
        });
    });
}

// InteractionSource: what the user is doing to a component, read as state (a button that
// shrinks while held, a card with its own pressed and hover looks instead of the overlay) or
// heard one by one (the button's log; a read-only field that opens a date picker when tapped).
// A pager that moves on by itself waits while the user holds it (collectIsDraggedAsState).
void InteractionSection(CallSite site) {
    composable(site, [=] {
        using namespace std::chrono;
        const ColorScheme& cs = colorScheme();
        static const DatePickerStrings strings = DatePickerStrings::turkish();
        auto button = remember([] { return MutableInteractionSource(); });
        auto card = remember([] { return MutableInteractionSource(); });
        auto field = remember([] { return MutableInteractionSource(); });
        auto log = remember([] { return mutableStateOf(std::vector<std::string>()); });
        auto pickerOpen = remember([] { return mutableStateOf(false); });
        auto date = remember([] { return mutableStateOf(std::optional<year_month_day>()); });
        LaunchedEffect(button, [button, log]() -> Task {
            co_await button.interactions().collect([log](const Interaction& i) {
                static const char* const names[] = {"Press", "Release", "Cancel", "HoverEnter", "HoverExit",
                                                    "Focus", "Unfocus", "DragStart", "DragStop", "DragCancel"};
                auto list = log.peek();
                list.push_back(names[int(i.type)]);
                if (list.size() > 4) list.erase(list.begin());
                log.set(std::move(list));
            });
        });
        LaunchedEffect(field, [field, pickerOpen]() -> Task {
            co_await field.interactions().collect([pickerOpen](const Interaction& i) {
                if (i.type == Interaction::Type::Release) pickerOpen.set(true);
            });
        });
        auto pager = rememberPagerState(0, [] { return 4; });
        bool held = pager.interactionSource().collectIsDraggedAsState();
        LaunchedEffect(held, [pager, held]() -> Task {
            if (held) co_return;  // waits while held; let go, it starts counting again
            while (true) {
                co_await delay(milliseconds(2500));
                co_await pager.animateScrollToPage((pager.currentPage() + 1) % pager.pageCount());
            }
        });
        bool pressed = button.collectIsPressedAsState();
        float scale = animateFloatAsState(pressed ? 0.92f : 1.f, 120);
        bool cardHovered = card.collectIsHoveredAsState(), cardPressed = card.collectIsPressedAsState();
        Section("InteractionSource · collectIsPressedAsState · collectIsDraggedAsState", [&] {
            Text("Bileşenler basmayı, fareyle üzerine gelmeyi, odağı ve sürüklemeyi bir MutableInteractionSource'a söyler; "
                 "ekran bunları durum olarak okur ya da tek tek dinler.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Row(Modifier().fillMaxWidth().padding(0, 12, 0, 0), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    Button([] {}, Modifier().scale(scale), {.interactionSource = button}, [&] { Text(pressed ? "Basılı tutuluyor" : "Basılı tut"); });
                    std::string events;
                    for (const std::string& e : log.get()) events += (events.empty() ? "" : " · ") + e;
                    Text(events.empty() ? "olay yok" : events, Modifier().weight(1), {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
                });
            // Its own looks instead of the overlay (indication = false).
            Box(Modifier().fillMaxWidth().padding(0, 12, 0, 0).height(64)
                    .background(cardPressed ? cs.primaryContainer : cs.surfaceContainerHigh, RoundedCornerShape(16))
                    .border(cardHovered ? 2.f : 1.f, cardHovered ? cs.primary : cs.outlineVariant, RoundedCornerShape(16))
                    .clickable([] {}, {.interactionSource = card, .indication = false}),
                Alignment::Center, [&] {
                    Text(cardPressed ? "Basılı: kendi görünümü, kaplama yok" : cardHovered ? "Fare üzerinde" : "Üzerine gelin ya da basın",
                         {.font = typography().bodyMedium, .color = cardPressed ? cs.onPrimaryContainer : cs.onSurface});
                });
            auto format = [&](year_month_day d) {
                return std::format("{} {} {}", unsigned(d.day()), strings.months[unsigned(d.month()) - 1], int(d.year()));
            };
            OutlinedTextField(date.get() ? format(*date.get()) : "", [](std::string) {}, Modifier().fillMaxWidth().padding(0, 12, 0, 0),
                              {.singleLine = true, .placeholder = "Seçmek için dokunun", .label = "Doğum tarihi",
                               .trailingIcon = [] { Icon(Icons::Filled::DateRange, ""); }, .readOnly = true, .interactionSource = field});
            if (pickerOpen.get()) {
                auto state = rememberDatePickerState(date.get());
                auto close = [pickerOpen] { pickerOpen.set(false); };
                DatePickerDialog(
                    close,
                    [state, date, close] {
                        TextButton([state, date, close] {
                            date.set(state.selectedDate());
                            close();
                        }, [] { Text("Tamam"); });
                    },
                    [close] { TextButton(close, [] { Text("İptal"); }); },
                    [&] { DatePicker(state); });
            }
            static const Color pages[] = {Color::hex(0x6750A4), Color::hex(0x7D5260), Color::hex(0x386A20), Color::hex(0x00639B)};
            HorizontalPager(pager, Modifier().fillMaxWidth().padding(0, 12, 0, 0).height(96), [&](int page) {
                Box(Modifier().fillMaxSize().padding(4, 0).background(pages[page], RoundedCornerShape(16)), Alignment::Center, [&] {
                    Text(held ? "tutuluyor: bekliyor" : std::format("Sayfa {} · kendiliğinden ilerler", page + 1),
                         {.font = typography().titleSmall, .color = colors::White});
                });
            });
        });
    });
}

// anchoredDraggable: a handle that rests at three places, and rows that swipe open to their
// actions (a row's anchors are known from the start; the handle's come from its track's size).
enum class Spot { Start, Middle, End };

enum class RowReveal { Closed, Open };

void RevealRow(std::string title, std::function<void(std::string)> onAction, CallSite site = CallSite::current()) {
    composable(site, title, [=] {
        const ColorScheme& cs = colorScheme();
        auto state = remember([] {
            return AnchoredDraggableState<RowReveal>(RowReveal::Closed, DraggableAnchors<RowReveal>{{RowReveal::Closed, 0}, {RowReveal::Open, -144}});
        });
        auto scope = rememberCoroutineScope();
        auto act = [=](std::string what) {
            onAction(what + ": " + title);
            scope.launch([state]() -> Task { co_await state.animateTo(RowReveal::Closed); });
        };
        Box(Modifier().fillMaxWidth().height(64).clip(RoundedCornerShape(12)), [&] {
            Row(Modifier().align(Alignment::CenterEnd).fillMaxHeight(), {}, [&] {
                Box(Modifier().width(72).fillMaxHeight().background(cs.tertiaryContainer).clickable([act] { act("yıldızlandı"); }), Alignment::Center,
                    [&] { Icon(Icons::Filled::Star, "Yıldızla", Modifier(), cs.onTertiaryContainer); });
                Box(Modifier().width(72).fillMaxHeight().background(cs.error).clickable([act] { act("silindi"); }), Alignment::Center,
                    [&] { Icon(Icons::Filled::Delete, "Sil", Modifier(), cs.surface); });
            });
            Box(Modifier().fillMaxSize().offset([state] { return Offset{state.requireOffset(), 0}; })
                    .anchoredDraggable(state, Orientation::Horizontal).background(cs.surfaceContainerHighest).padding(16, 0),
                Alignment::CenterStart, [&] {
                    Text(title + (state.targetValue() == RowReveal::Open ? "  ← açık" : "  ← sola kaydırın"), {.font = typography().bodyLarge});
                });
        });
    });
}

void AnchoredDraggableSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        static const char* const names[] = {"Start", "Middle", "End"};
        auto spot = rememberAnchoredDraggableState(Spot::Start);
        auto scope = rememberCoroutineScope();
        auto lastAction = remember([] { return mutableStateOf(std::string()); });
        Section("anchoredDraggable · AnchoredDraggableState", [&] {
            Text("Tutamak üç yerden birinde durur: yarı yoldan fazla sürüklenir ya da fırlatılırsa sonrakine, yoksa geri gider. "
                 "Satırlar sola kaydırılınca eylemleri açılır.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Box(Modifier().fillMaxWidth().padding(0, 12, 0, 0).height(56).background(cs.surfaceContainerHighest, 28).onSizeChanged([spot](Size s) {
                    spot.updateAnchors({{Spot::Start, 4}, {Spot::Middle, (s.width - 48) / 2}, {Spot::End, s.width - 52}});
                }),
                [&] {
                    Box(Modifier().offset([spot] { return Offset{spot.requireOffset(), 4}; }).anchoredDraggable(spot, Orientation::Horizontal)
                            .size(48).background(cs.primary, 24),
                        Alignment::Center, [&] { Icon(Icons::Filled::KeyboardArrowRight, "Tutamak", Modifier(), cs.onPrimary); });
                });
            Text(std::format("currentValue: {} · targetValue: {}", names[int(spot.currentValue())], names[int(spot.targetValue())]),
                 Modifier().padding(0, 8, 0, 0), {.font = typography().bodyMedium});
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                for (Spot target : {Spot::Start, Spot::Middle, Spot::End})
                    TextButton([scope, spot, target] { scope.launch([spot, target]() -> Task { co_await spot.animateTo(target); }); },
                               [&] { Text(names[int(target)]); });
            });
            Column(Modifier().fillMaxWidth().padding(0, 8, 0, 0), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
                for (const char* title : {"Fatura hatırlatması", "Toplantı notları"})
                    key(title, [&] { RevealRow(title, [lastAction](std::string what) { lastAction.set(what); }); });
            });
            if (!lastAction.get().empty()) Text(lastAction.get(), {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
        });
    });
}

// Ripples (Compose's indication): every clickable has Material's; ripple() makes it unbounded,
// sized or colored, a flat state layer or none instead; LocalIndication sets it for a subtree.
void RippleSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto labelled = [&](const char* label, const Content& content) {
            Column(Modifier().width(76), {.horizontalAlignment = Alignment::CenterHorizontally}, [&] {
                content();
                Text(label, Modifier().padding(0, 6, 0, 0), {.font = typography().bodySmall, .color = cs.onSurfaceVariant, .textAlign = TextAlign::Center});
            });
        };
        Section("Ripple · indication · LocalIndication", [&] {
            Text("Her tıklanabilirde Material dalgası var: basılan yerden köşelere yayılır, bırakınca söner. ripple() onu sınırsız, "
                 "yarıçaplı ya da renkli yapar; LocalIndication bir alt ağaç için değiştirir.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Box(Modifier().fillMaxWidth().padding(0, 12, 0, 0).height(96).background(cs.surfaceContainerHigh, RoundedCornerShape(16)).clickable([] {}),
                Alignment::Center, [&] { Text("Bir köşesine basın", {.font = typography().bodyLarge}); });
            // indication(source, ...): the row takes the press without a ripple of its own; the icon shows it.
            auto rowSource = remember([] { return MutableInteractionSource(); });
            Row(Modifier().fillMaxWidth().padding(0, 12, 0, 0).height(56).background(cs.surfaceContainerHigh, RoundedCornerShape(16))
                    .clickable([] {}, {.interactionSource = rowSource, .indication = false}).padding(12, 0),
                {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                    Box(Modifier().size(40).clip(CircleShape).indication(rowSource, ripple()), Alignment::Center,
                        [] { Icon(Icons::Filled::Notifications, "Bildirim"); });
                    Text("Satıra basın: dalga yalnızca simgede (indication)", {.font = typography().bodyMedium});
                });
            Row(Modifier().fillMaxWidth().padding(0, 12, 0, 0), {.horizontalArrangement = Arrangement::SpaceBetween}, [&] {
                labelled("sınırsız", [&] {
                    Box(Modifier().size(64).clickable([] {}, {.indication = ripple(false, 28)}), Alignment::Center,
                        [] { Icon(Icons::Filled::Favorite, "Sınırsız"); });
                });
                labelled("renkli", [&] {
                    Box(Modifier().size(64).background(cs.secondaryContainer, 16).clickable([] {}, {.indication = ripple(true, 0, cs.primary)}),
                        Alignment::Center, [] { Icon(Icons::Filled::Star, "Renkli"); });
                });
                labelled("düz katman", [&] {
                    Box(Modifier().size(64).background(cs.secondaryContainer, 16).clickable([] {}, {.indication = Indication::Kind::StateLayer}),
                        Alignment::Center, [] { Icon(Icons::Filled::Check, "Düz"); });
                });
                labelled("yok (Local)", [&] {
                    CompositionLocalProvider({LocalIndication.provides(false)}, [&] {
                        Box(Modifier().size(64).background(cs.secondaryContainer, 16).clickable([] {}), Alignment::Center,
                            [] { Icon(Icons::Filled::Close, "Yok"); });
                    });
                });
            });
        });
    });
}

// selectable / toggleable rows: the whole row takes the tap (and reads as one radio button or
// switch to accessibility services), the controls in it have no callback of their own.
// matchParentSize: a gradient as big as the text in front of it made the box.
// Semantics: what accessibility services (TalkBack) read — a row as one, five star icons as
// one sentence, a decoration not at all.
void SemanticsSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto unread = remember([] { return mutableStateOf(3); });
        auto lastAction = remember([] { return mutableStateOf(std::string("—")); });
        Section("Erişilebilirlik: merge · clearAndSet · hide · customActions · liveRegion", [&] {
            Row(Modifier().fillMaxWidth().semantics({.mergeDescendants = true}),
                {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                    Icon(Icons::Filled::Place, "Konum", {}, cs.primary);
                    Column(Modifier().weight(1), [&] {
                        Text("İstanbul", {.font = typography().titleSmall});
                        Text("Parçalı bulutlu, 18°", {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
                    });
                    IconButton([] {}, [] { Icon(Icons::Filled::Refresh, "Yenile"); });  // still on its own
                });
            Row(Modifier().clearAndSetSemantics({.contentDescription = "Puan: 4 / 5"}), [&] {
                for (int i = 0; i < 5; ++i) Icon(Icons::Filled::Star, "yıldız", {}, i < 4 ? cs.primary : cs.outlineVariant);
            });
            Text("• • •", Modifier().hideFromAccessibility(), {.color = cs.outlineVariant});
            // customActions: the row's own actions, in TalkBack's actions menu (no buttons for them).
            ListItem({.headline = [] { Text("Ayşe: Toplantı yarın 10'da"); },
                      .supporting = [&] { Text("Son eylem: " + lastAction.get()); },
                      .leading = [] { Icon(Icons::Filled::Email, ""); }},
                     Modifier().semantics({.mergeDescendants = true,
                                           .customActions = {{"Arşivle", [lastAction] { lastAction.set("arşivlendi"); return true; }},
                                                             {"Okundu say", [lastAction, unread] {
                                                                  lastAction.set("okundu");
                                                                  unread.set(std::max(0, unread.get() - 1));
                                                                  return true;
                                                              }}}}));
            // liveRegion: a change to the count is read out without moving the focus there.
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Text(std::format("{} okunmamış ileti", unread.get()), Modifier().weight(1).semantics({.liveRegion = LiveRegionMode::Polite}),
                     {.font = typography().bodyLarge});
                OutlinedButton([unread] { unread.set(unread.get() + 1); }, [] { Text("Yeni ileti"); });
            });
            Text("TalkBack açıkken: ilk satır tek parça okunur (Yenile ayrı), yıldızlar \"Puan: 4 / 5\" diye, noktalar hiç okunmaz; "
                 "ileti satırının eylemler menüsünde \"Arşivle\" ve \"Okundu say\" var; \"Yeni ileti\"ye basınca sayı odak değişmeden okunur. "
                 "Kaydırıcılar ve ilerleme çubukları değerlerini yüzde olarak söyler, kaydırıcının değeri ses tuşlarıyla değişir.",
                 {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
        });
    });
}

// focusProperties: "Bir" sends Tab straight to "Dört", "İki" never takes the focus. A keyboard's
// Tab moves it; without one, the buttons below do (a tap ends the keyboard focus, so they first
// give it back to the button that had it, then move it a frame later).
void FocusPropertiesDemo(CallSite site) {
    composable(site, [=] {
        auto requesters = remember([] { return std::array<FocusRequester, 4>{}; });
        auto focused = remember([] { return mutableStateOf(-1); });
        auto last = remember([] { return std::make_shared<int>(0); });  // the button that had the focus last
        auto scope = rememberCoroutineScope();
        FocusManager focus = focusManager();
        const char* names[] = {"Bir", "İki", "Üç", "Dört"};
        Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
            for (int i = 0; i < 4; ++i) {
                FocusProperties properties;
                if (i == 0) properties.next = requesters[3];
                if (i == 1) properties.canFocus = false;
                OutlinedButton([] {}, Modifier()
                                          .focusRequester(requesters[size_t(i)])
                                          .focusProperties(properties)
                                          .onFocusChanged([focused, last, i](FocusState s) {
                                              if (s.isFocused) focused.set(i), *last = i;
                                              else if (focused.peek() == i) focused.set(-1);
                                          }),
                               {}, [&, i] { Text(names[i]); });
            }
        });
        Text(std::string("Odakta: ") + (focused.get() >= 0 ? names[focused.get()] : "—") + " · İki odak almaz, Bir'den Tab Dört'e gider",
             {.font = typography().bodySmall, .color = colorScheme().onSurfaceVariant});
        auto move = [requesters, last, scope, focus](FocusDirection direction) {
            return [=] {
                requesters[size_t(*last)].requestFocus();
                scope.launch([focus, direction]() -> Task {
                    co_await delay(std::chrono::milliseconds(50));
                    focus.moveFocus(direction);
                });
            };
        };
        Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
            TextButton([requesters] { requesters[0].requestFocus(); }, [] { Text("Bir'e odaklan"); });
            TextButton(move(FocusDirection::Previous), [] { Text("Önceki"); });
            TextButton(move(FocusDirection::Next), [] { Text("Sonraki (Tab)"); });
        });
    });
}

void SelectableRowsSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto choice = remember([] { return mutableStateOf(1); });
        auto notify = remember([] { return mutableStateOf(true); });
        static const char* const options[] = {"Sabah", "Öğle", "Akşam"};
        Section("selectable · toggleable · selectableGroup · matchParentSize", [&] {
            Column(Modifier().fillMaxWidth().selectableGroup(), [&] {
                for (int i = 0; i < 3; ++i)
                    Row(Modifier().fillMaxWidth().height(52).selectable(choice.get() == i, [choice, i] { choice.set(i); }, SemanticsRole::RadioButton)
                            .padding(8, 0),
                        {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically}, [&] {
                            RadioButton(choice.get() == i, {});
                            Text(options[i], {.font = typography().bodyLarge});
                        });
            });
            Row(Modifier().fillMaxWidth().height(56).toggleable(notify.get(), [notify](bool v) { notify.set(v); }, SemanticsRole::Switch).padding(8, 0),
                {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    Text("Hatırlatmalar", Modifier().weight(1), {.font = typography().bodyLarge});
                    // An icon in the thumb (a tick on, a cross off); the thumb grows while held.
                    bool n = notify.get();
                    Switch(n, {}, Modifier(), {.thumbContent = [n] { Icon(n ? Icons::Filled::Check : Icons::Filled::Close, "", Modifier().size(16)); }});
                });
            Box(Modifier().padding(0, 8, 0, 0), [&] {
                Box(Modifier().matchParentSize().background(Brush::horizontalGradient({cs.primaryContainer, cs.tertiaryContainer}), RoundedCornerShape(12)));
                Text(std::format("{} · hatırlatma {}", options[choice.get()], notify.get() ? "açık" : "kapalı"), Modifier().padding(16, 10),
                     {.font = typography().titleSmall, .color = cs.onPrimaryContainer});
            });
        });
    });
}

// DatePickerDialog: the picked date shows under the button; the dialog opens on it.
void DatePickerSection(CallSite site) {
    composable(site, [=] {
        using namespace std::chrono;
        const ColorScheme& cs = colorScheme();
        static const DatePickerStrings strings = DatePickerStrings::turkish();
        auto open = remember([] { return mutableStateOf(false); });
        auto picked = remember([] { return mutableStateOf(std::optional<year_month_day>()); });
        auto rangeOpen = remember([] { return mutableStateOf(false); });
        auto range = remember([] { return mutableStateOf(std::optional<std::pair<year_month_day, year_month_day>>()); });
        auto format = [&](year_month_day d) {
            return std::format("{} {} {}", unsigned(d.day()), strings.months[unsigned(d.month()) - 1], int(d.year()));
        };
        Section("DatePicker · DateRangePicker", [&] {
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(12)}, [&] {
                Button([open] { open.set(true); }, [] {
                    Icon(Icons::Filled::DateRange, "", Modifier().size(18));
                    Text("Tarih seç");
                });
                OutlinedButton([rangeOpen] { rangeOpen.set(true); }, [] { Text("Aralık seç"); });
            });
            std::string text = "Henüz tarih seçilmedi.";
            if (auto d = picked.get()) text = "Seçilen: " + format(*d);
            if (auto r = range.get()) text += "\nAralık: " + format(r->first) + " – " + format(r->second);
            Text(text, {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            if (rangeOpen.get()) {
                auto state = rememberDateRangePickerState(range.get() ? std::optional(range.get()->first) : std::nullopt,
                                                          range.get() ? std::optional(range.get()->second) : std::nullopt);
                auto close = [rangeOpen] { rangeOpen.set(false); };
                DatePickerDialog(
                    close,
                    [state, range, close] {
                        auto start = state.selectedStartDate(), end = state.selectedEndDate();
                        TextButton([state, range, close] {
                            range.set(std::pair(*state.selectedStartDate(), *state.selectedEndDate()));
                            close();
                        }, Modifier(), {.enabled = start && end}, [] { Text("Tamam"); });
                    },
                    [close] { TextButton(close, [] { Text("İptal"); }); },
                    [&] { DateRangePicker(state); });
            }
            if (open.get()) {
                auto state = rememberDatePickerState(picked.get());
                auto close = [open] { open.set(false); };
                DatePickerDialog(
                    close,
                    [state, picked, close] {
                        TextButton([state, picked, close] {
                            picked.set(state.selectedDate());
                            close();
                        }, [] { Text("Tamam"); });
                    },
                    [close] { TextButton(close, [] { Text("İptal"); }); },
                    [&] { DatePicker(state); });
            }
        });
    });
}

// TimePickerDialog: the dial or, with the keyboard button, two number fields.
void TimePickerSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto open = remember([] { return mutableStateOf(false); });
        auto picked = remember([] { return mutableStateOf(std::optional<std::pair<int, int>>()); });
        auto twelveHour = remember([] { return mutableStateOf(false); });
        Section("TimePicker", [&] {
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Button([open] { open.set(true); }, [] {
                    Icon(Icons::Filled::Schedule, "", Modifier().size(18));
                    Text("Saat seç");
                });
                FilterChip(twelveHour.get(), [twelveHour] { twelveHour.set(!twelveHour.get()); }, [] { Text("12 saat"); });
            });
            std::string text = "Henüz saat seçilmedi.";
            if (auto t = picked.get()) text = std::format("Seçilen: {:02}:{:02}", t->first, t->second);
            Text(text, {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            if (open.get()) {
                auto start = picked.get().value_or(std::pair(12, 0));
                auto state = rememberTimePickerState(start.first, start.second, !twelveHour.get());
                auto typing = remember([] { return mutableStateOf(false); });
                auto close = [open] { open.set(false); };
                TimePickerOptions options;  // the labels: LocalStrings's, Turkish here
                TimePickerDialog(
                    close,
                    {.title = [typing] { Text(typing.get() ? "Saati girin" : "Saat seçin"); },
                     .confirmButton = [state, picked, close] {
                         TextButton([state, picked, close] {
                             picked.set(std::pair(state.hour(), state.minute()));
                             close();
                         }, [] { Text("Tamam"); });
                     },
                     .dismissButton = [close] { TextButton(close, [] { Text("İptal"); }); },
                     .modeToggleButton = [typing] {
                         IconButton([typing] { typing.set(!typing.get()); }, [typing] {
                             Icon(typing.get() ? Icons::Filled::Schedule : Icons::Filled::Keyboard,
                                  typing.get() ? "Saat kadranı" : "Klavyeyle gir");
                         });
                     }},
                    [&] {
                        if (typing.get())
                            TimeInput(state, Modifier(), options);
                        else
                            TimePicker(state, Modifier(), options);
                    });
            }
        });
    });
}

// Material You: the switch and the system's palettes (Android 12+).
void DynamicColorSection(MutableState<ColorSource> source, CallSite site) {
    composable(site, source, [=] {
        const ColorScheme& cs = colorScheme();
        const auto& system = LocalDynamicTonalPalette.current();
        const auto& wallpaper = LocalWallpaperTonalPalette.current();
        Section("Dinamik renk (Material You)", [&] {
            // Three sources, each its own API: which one is the app's (or its user's) choice.
            SingleChoiceSegmentedButtonRow(Modifier().fillMaxWidth(), [&] {
                const std::pair<ColorSource, const char*> choices[] = {
                    {ColorSource::App, "Uygulama"}, {ColorSource::System, "Sistem"}, {ColorSource::Wallpaper, "Duvar kağıdı"}};
                for (const auto& [value, label] : choices)
                    SegmentedButton(source.get() == value, [source, value] { source.set(value); }, [label] { Text(label); },
                                    Modifier().weight(1));
            });
            const std::optional<TonalPalette>* shown = source.get() == ColorSource::System      ? &system
                                                       : source.get() == ColorSource::Wallpaper ? &wallpaper
                                                                                                : nullptr;
            Text(source.get() == ColorSource::App         ? "lightColorScheme / darkColorScheme: uygulamanın kendi renkleri"
                 : source.get() == ColorSource::System    ? (system ? "dynamicLightColorScheme: sistemin vurgu rengi (Android 12+, Linux, Windows)"
                                                                    : "Bu sistem vurgu rengi vermiyor; uygulamanın renkleri kullanılıyor")
                 : wallpaper ? "wallpaperLightColorScheme: duvar kağıdından seçilen renkler"
                             : "Duvar kağıdı okunamadı; uygulamanın renkleri kullanılıyor",
                 {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
            if (!shown || !*shown) return;
            const TonalPalette* palette = &**shown;
            // The five key colors in their 13 system tones (white .. black).
            const std::pair<const char*, const std::array<Color, 13>*> rows[] = {
                {"accent1", &palette->accent1}, {"accent2", &palette->accent2}, {"accent3", &palette->accent3},
                {"neutral1", &palette->neutral1}, {"neutral2", &palette->neutral2}};
            for (const auto& [name, tones] : rows)
                Row(Modifier().fillMaxWidth(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    Text(name, Modifier().width(72), {.font = typography().labelMedium, .color = cs.onSurfaceVariant});
                    for (const Color& c : *tones) Box(Modifier().weight(1).height(20).background(c));
                });
        });
    });
}

// The section on its own, in the colors of the source picked in it (KOMPOZ_PREVIEW=DynamicColorPreview).
KOMPOZ_PREVIEW(DynamicColorPreview, .width = 420, .height = 330) {
    auto source = remember([] { return mutableStateOf(ColorSource::Wallpaper); });
    bool dark = isSystemInDarkTheme();
    ColorScheme scheme = source.get() == ColorSource::System && isDynamicColorAvailable() ? (dark ? dynamicDarkColorScheme() : dynamicLightColorScheme())
                         : source.get() == ColorSource::Wallpaper && isWallpaperColorAvailable() ? (dark ? wallpaperDarkColorScheme() : wallpaperLightColorScheme())
                                                                                                 : (dark ? darkColorScheme() : lightColorScheme());
    MaterialTheme(scheme, [&] {
        Box(Modifier().fillMaxSize().background(colorScheme().surface).padding(16), [&] { DynamicColorSection(source); });
    });
}

// imeNestedScroll: a chat whose keyboard comes up as the messages are pulled on past their end
// (and goes as they are pushed down), with the field focused.
void ImeNestedScrollSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto draft = remember([] { return mutableStateOf(std::string()); });
        auto& scroll = remember([] { return ScrollState(); });
        Section("Modifier.imeNestedScroll", [&] {
            Column(Modifier().fillMaxWidth().height(220).background(cs.surfaceContainerHigh, 12).clip(RoundedCornerShape(12)), [&] {
                Column(Modifier().weight(1).fillMaxWidth().imeNestedScroll().verticalScroll(scroll, true, true).padding(12),
                       {.verticalArrangement = Arrangement::spacedBy(6)}, [&] {
                           for (int i = 1; i <= 12; ++i)
                               Box(Modifier().fillMaxWidth(), i % 2 ? Alignment::CenterStart : Alignment::CenterEnd, [&] {
                                   Text(std::format("Mesaj {}", i), Modifier()
                                                                         .background(i % 2 ? cs.secondaryContainer : cs.primaryContainer, 12)
                                                                         .padding(10, 6));
                               });
                       });
                TextField(draft.get(), [draft](std::string s) { draft.set(std::move(s)); }, Modifier().fillMaxWidth(),
                          {.singleLine = true, .placeholder = "Yazın…"});
            });
            Text("Alana dokunup klavyeyi kapatın; mesajları sonlarından öteye yukarı çekin: klavye parmakla gelir, aşağı itince gider (Android 11+).",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
        });
    });
}

// preferredFrameRate: the refresh rate asked for while this is laid out (a dot moving across to
// see it by).
void FrameRateSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto choice = remember([] { return mutableStateOf(0); });
        float x = rememberInfiniteTransition().animateFloat(0, 1, infiniteRepeatable(tween(1600, 0, LinearEasing), RepeatMode::Reverse));
        static const char* const labels[] = {"Yok", "30 Hz", "Normal", "En yüksek"};
        Modifier wish = choice.get() == 1   ? Modifier().preferredFrameRate(30)
                        : choice.get() == 2 ? Modifier().preferredFrameRate(FrameRateCategory::Normal)
                        : choice.get() == 3 ? Modifier().preferredFrameRate(FrameRateCategory::High)
                                            : Modifier();
        Section("Modifier.preferredFrameRate", [&] {
            SingleChoiceSegmentedButtonRow(Modifier().fillMaxWidth(), [&] {
                for (int i = 0; i < 4; ++i) SegmentedButton(choice.get() == i, [choice, i] { choice.set(i); }, [&, i] { Text(labels[i]); });
            });
            BoxWithConstraints(Modifier().fillMaxWidth().height(32).then(wish), [&](const Constraints& box) {
                Box(Modifier().offset(x * (box.maxWidth - 32), 0).size(32).background(cs.primary, CircleShape));
            });
            Text("Seçim, bu bölüm ekrandayken ekranın tazeleme hızına istek olarak gider (Android 11+; telefon ayarı ve pil durumu son sözü söyler).",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
        });
    });
}

// What each kind of system UI covers right now (the ime row follows the keyboard).
void InsetsSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        Section("WindowInsets (dp: sol, üst, sağ, alt)", [&] {
            const std::pair<const char*, WindowInsets> kinds[] = {
                {"statusBars", WindowInsets::statusBars()},   {"navigationBars", WindowInsets::navigationBars()},
                {"ime", WindowInsets::ime()},                 {"displayCutout", WindowInsets::displayCutout()},
                {"systemGestures", WindowInsets::systemGestures()}, {"captionBar", WindowInsets::captionBar()},
                {"mandatorySystemGestures", WindowInsets::mandatorySystemGestures()}, {"tappableElement", WindowInsets::tappableElement()},
                {"waterfall", WindowInsets::waterfall()},     {"safeGestures", WindowInsets::safeGestures()},
                {"safeDrawing", WindowInsets::safeDrawing()}, {"safeContent", WindowInsets::safeContent()},
                {"imeAnimationSource", WindowInsets::imeAnimationSource()}, {"imeAnimationTarget", WindowInsets::imeAnimationTarget()}};
            for (const auto& [name, i] : kinds)
                Row(Modifier().fillMaxWidth(), [&] {
                    Text(name, Modifier().weight(1), {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                    Text(std::format("{:.0f}, {:.0f}, {:.0f}, {:.0f}", i.left, i.top, i.right, i.bottom),
                         {.fontSize = 14, .fontFamily = FontFamily::Monospace});
                });
            // The keyboard: up (or on its way up)? Android 11+ moves ime() with it as it slides.
            Text(std::format("isImeVisible: {} (klavye kayarken ime() onu kare kare izler)", WindowInsets::isImeVisible() ? "evet" : "hayır"),
                 {.font = typography().bodySmall});
            // As tall as the status bar (taken by the screen's padding? then nothing: 0 dp here).
            Text("windowInsetsTopHeight(statusBars): bu sayfanın içinde zaten alınmış, yüksekliği 0", {.font = typography().bodySmall});
            Spacer(Modifier().fillMaxWidth().windowInsetsTopHeight(WindowInsets::statusBars()).background(cs.tertiary));
        });
    });
}
