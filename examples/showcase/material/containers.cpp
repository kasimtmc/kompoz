// Material screen: animated list items, ListItem and badges, dialogs, menus and sheets, cards.

#include "showcase.hpp"

void AnimateItemSection(CallSite site) {
    composable(site, [=] {
        auto tasks = remember([] { return mutableStateListOf<int>({1, 2, 3, 4, 5}); });
        auto nextTask = remember([] { return std::make_shared<int>(6); });
        auto shuffles = remember([] { return std::make_shared<unsigned>(1); });
        Section("animateItem · SwipeToDismissBox", [&] {
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8}, [&] {
                FilledTonalButton([tasks, shuffles] {
                    tasks.update([&](std::vector<int>& v) {
                        std::mt19937 rng((*shuffles)++);
                        std::shuffle(v.begin(), v.end(), rng);
                    });
                }, [] { Text("Karıştır"); });
                FilledTonalButton([tasks, nextTask] { tasks.add(0, (*nextTask)++); }, [] { Text("Ekle"); });
                FilledTonalButton([tasks] {  // it fades out where it was, the others glide up
                    if (!tasks.items().empty()) tasks.removeAt(0);
                }, [] { Text("İlkini sil"); });
                OutlinedButton([tasks, nextTask] {
                    tasks.update([](std::vector<int>& v) { v = {1, 2, 3, 4, 5}; });
                    *nextTask = 6;
                }, [] { Text("Sıfırla"); });
            });
            LazyColumn(Modifier().fillMaxWidth().height(330), {.spacing = 6}, [&](LazyListScope& list) {
                list.items(tasks.items(), [](int id) { return id; }, [&](int id) { SwipeableTask(id, tasks); });
            });
        });
    });
}

void ListItemSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto notifications = remember([] { return mutableStateOf(true); });
        Section("ListItem ve Badge", [&] {
            OutlinedCard(Modifier().fillMaxWidth().clip(RoundedCornerShape(12)), [&] {
                ListItem({.headline = [] { Text("Tek satır"); },
                          .trailing = [&] {
                              Switch(notifications.get(), [notifications](bool on) { notifications.set(on); });
                          }});
                HorizontalDivider();
                ListItem({.headline = [] { Text("Ayşe Yılmaz"); },
                          .supporting = [] { Text("Toplantı saat 15:00'te"); },
                          .leading = [&] {
                              Box(Modifier().size(40).background(cs.primaryContainer, CircleShape), Alignment::Center,
                                  [&] { Text("AY", {.font = typography().titleMedium, .color = cs.onPrimaryContainer}); });
                          },
                          .trailing = [] { Text("10:42"); }},
                         Modifier().clickable([] {}));
                HorizontalDivider();
                ListItem({.headline = [] { Text("Gelen kutusu"); },
                          .overline = [] { Text("E-POSTA"); },
                          .supporting = [] { Text("Okunmamış iletiler ve bildirimler burada görünür."); },
                          .leading = [] {
                              BadgedBox([] { Badge(Modifier(), [] { Text("8"); }); }, Modifier(),
                                        [] { Icon(Icons::Filled::Email, "Posta"); });
                          },
                          .trailing = [] {
                              BadgedBox([] { Badge(); }, Modifier(), [] { Icon(Icons::Filled::Notifications, "Bildirim"); });
                          }});
            });
            // M3 Expressive: items that make one card, the row taking the tap, rounder when picked.
            Text("SegmentedListItem: tek kart gibi duran, seçilince yuvarlaklaşan satırlar",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            auto& plan = remember([] { return mutableStateOf(1); });
            const char* plans[] = {"Ücretsiz", "Standart", "Aile"};
            const char* details[] = {"Reklamlı, tek cihaz", "Reklamsız, iki cihaz", "Reklamsız, altı kişi"};
            Column(Modifier().fillMaxWidth().background(cs.surfaceContainer, 20).padding(8),
                   {.verticalArrangement = Arrangement::spacedBy(ListItemDefaults::SegmentedGap)}, [&] {
                       for (int i = 0; i < 3; ++i)
                           SegmentedListItem(plan.get() == i, [&plan, i] { plan.set(i); }, ListItemDefaults::segmentedShapes(i, 3),
                                             {.headline = [&plans, i] { Text(plans[i]); },
                                              .supporting = [&details, i] { Text(details[i]); },
                                              .leading = [] { Icon(Icons::Filled::Star, ""); }});
                   });
        });
    });
}

void DialogsSection(NavController nav, CallSite site) {
    composable(site, nav, [=] {
        const ColorScheme& cs = colorScheme();
        auto dialog = remember([] { return mutableStateOf(false); });
        auto basicDialog = remember([] { return mutableStateOf(false); });
        auto sheet = remember([] { return mutableStateOf(false); });
        auto longSheet = remember([] { return mutableStateOf(false); });
        auto sheetState = rememberModalBottomSheetState();  // the sheet slides out from code, then goes
        auto sheetScope = rememberCoroutineScope();
        auto menu = remember([] { return mutableStateOf(false); });
        auto choice = remember([] { return mutableStateOf(std::string("yok")); });
        auto formatMenu = remember([] { return mutableStateOf(false); });
        auto format = remember([] { return mutableStateOf(std::array<bool, 2>{true, false}); });  // bold, italic
        auto alignment = remember([] { return mutableStateOf(0); });
        Section("Dialog, menü ve alt sayfa", [&] {
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8}, [&] {
                FilledTonalButton([dialog] { dialog.set(true); }, [] { Text("Dialog"); });
                FilledTonalButton([basicDialog] { basicDialog.set(true); }, [] { Text("BasicAlertDialog"); });
                FilledTonalButton([sheet] { sheet.set(true); }, [] { Text("Alt sayfa"); });
                FilledTonalButton([longSheet] { longSheet.set(true); }, [] { Text("Uzun liste"); });
                FilledTonalButton([nav] { nav.navigate(routes::Places{}); }, [] { Text("Harita (BottomSheetScaffold)"); });
                Box([&] {
                    OutlinedButton([menu] { menu.set(true); }, [] {
                        Text("Menü");
                        Icon(Icons::Filled::KeyboardArrowDown, "Aç", Modifier().size(18));
                    });
                    DropdownMenu(menu.get(), [menu] { menu.set(false); }, [&] {
                        DropdownMenuItem([] { Text("Düzenle"); }, [menu, choice] { choice.set("Düzenle"); menu.set(false); },
                                         {.leadingIcon = [] { Icon(Icons::Filled::Edit, "Düzenle"); }});
                        DropdownMenuItem([] { Text("Paylaş"); }, [menu, choice] { choice.set("Paylaş"); menu.set(false); },
                                         {.leadingIcon = [] { Icon(Icons::Filled::Share, "Paylaş"); }});
                        HorizontalDivider(Modifier().padding(0, 8));
                        DropdownMenuItem([] { Text("Sil"); }, [menu, choice] { choice.set("Sil"); menu.set(false); },
                                         {.leadingIcon = [] { Icon(Icons::Filled::Delete, "Sil"); },
                                          .trailingIcon = [] { Text("Del"); }});
                        DropdownMenuItem([] { Text("Kapalı seçenek"); }, [] {}, {.enabled = false});
                    });
                });
                // An expressive menu: groups as cards, checked and selected items.
                Box([&] {
                    OutlinedButton([formatMenu] { formatMenu.set(true); }, [] {
                        Text("Biçim");
                        Icon(Icons::Filled::KeyboardArrowDown, "Aç", Modifier().size(18));
                    });
                    DropdownMenuPopup(formatMenu.get(), [formatMenu] { formatMenu.set(false); }, [&] {
                        DropdownMenuGroup(MenuDefaults::groupShape(0, 3), [&] {
                            const char* styles[] = {"Kalın", "İtalik"};
                            for (int i = 0; i < 2; ++i)
                                DropdownMenuItem(format.get()[size_t(i)],
                                                 [format, i](bool on) {
                                                     auto f = format.get();
                                                     f[size_t(i)] = on;
                                                     format.set(f);
                                                 },
                                                 [&, i] { Text(styles[i]); }, {.shape = MenuDefaults::itemShape(i, 2)});
                        });
                        Spacer(Modifier().height(MenuDefaults::GroupSpacing));
                        DropdownMenuGroup(MenuDefaults::groupShape(1, 3), [&] {
                            const char* sides[] = {"Sola hizala", "Ortala", "Sağa hizala"};
                            for (int i = 0; i < 3; ++i)
                                DropdownMenuItem(alignment.get() == i, [alignment, i] { alignment.set(i); }, [&, i] { Text(sides[i]); },
                                                 {.shape = MenuDefaults::itemShape(i, 3)});
                        });
                        Spacer(Modifier().height(MenuDefaults::GroupSpacing));
                        DropdownMenuGroup(MenuDefaults::groupShape(2, 3), [&] {
                            DropdownMenuItem([] { Text("Yazı tipi"); }, [formatMenu, choice] { choice.set("Yazı tipi"); formatMenu.set(false); },
                                             {.leadingIcon = [] { Icon(Icons::Filled::Settings, "Yazı tipi"); },
                                              .supportingText = [] { Text("Boyut ve aile"); },
                                              .shape = MenuDefaults::itemShape(0, 1)});
                        });
                    });
                });
            });
            Text("Son seçim: " + choice.get(), {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            if (dialog.get())
                AlertDialog([dialog] { dialog.set(false); },
                            {.confirmButton = [dialog, choice] {
                                 TextButton([dialog, choice] { choice.set("Dialog: Sil"); dialog.set(false); }, [] { Text("Sil"); });
                             },
                             .dismissButton = [dialog] { TextButton([dialog] { dialog.set(false); }, [] { Text("Vazgeç"); }); },
                             .icon = [] { Icon(Icons::Filled::Delete, "Sil"); },
                             .title = [] { Text("Taslak silinsin mi?"); },
                             .text = [] { Text("Bu işlem geri alınamaz. Scrim'e dokunmak ya da geri tuşu da dialog'u kapatır."); }});
            if (basicDialog.get())  // content of its own, at least 280 dp wide
                BasicAlertDialog([basicDialog] { basicDialog.set(false); }, Modifier(), [&] {
                    Column(Modifier().background(cs.surfaceContainerHigh, 28).padding(24), {.horizontalAlignment = Alignment::CenterHorizontally}, [&] {
                        LoadingIndicator(Modifier().size(48));
                        Text("Eşitleniyor…", Modifier().padding(0, 16), {.font = typography().titleMedium});
                        Text("BasicAlertDialog: içeriği bizim; en az 280 dp genişlik ona da geçer.",
                             {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                        TextButton([basicDialog] { basicDialog.set(false); }, Modifier().padding(0, 16, 0, 0), {}, [] { Text("Kapat"); });
                    });
                });
            if (sheet.get())
                ModalBottomSheet([sheet] { sheet.set(false); }, sheetState, [&] {
                    Text("Paylaş", Modifier().padding(24, 0, 24, 8), {.font = typography().titleMedium});
                    const std::pair<const ImageVector*, const char*> actions[] = {
                        {&Icons::Filled::Share, "Bağlantıyı paylaş"}, {&Icons::Filled::Email, "E-posta ile gönder"},
                        {&Icons::Filled::Edit, "Düzenle"}};
                    for (const auto& [icon, label] : actions)
                        ListItem({.headline = [label] { Text(label); }, .leading = [icon, label] { Icon(*icon, label); },
                                  .containerColor = colors::Transparent},
                                 Modifier().clickable([sheet, choice, label, sheetState, sheetScope] {
                                     choice.set(label);
                                     sheetScope.launch([sheetState, sheet]() -> Task {  // as Compose apps do: hide, then remove
                                         co_await sheetState.hide();
                                         sheet.set(false);
                                     });
                                 }));
                    Text("Aşağı sürükleyin, scrim'e dokunun ya da geri tuşuna basın.", Modifier().padding(24, 8, 24, 16),
                         {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
                });
            // A list in a sheet: it scrolls first; pulled down at its top it moves the sheet.
            if (longSheet.get())
                ModalBottomSheet([longSheet] { longSheet.set(false); }, [&] {
                    Text("Uzun liste: sayfa yarıya kadar açılır; yukarı itince önce sayfa tam açılır, sonra liste kayar; "
                         "en üstte aşağı çekince sayfa iner",
                         Modifier().padding(24, 0, 24, 8), {.font = typography().titleSmall});
                    LazyColumn(Modifier().fillMaxWidth().height(420), [&](LazyListScope& list) {
                        list.items(40, [longSheet, choice](int i) {
                            std::string label = std::format("Seçenek {}", i + 1);
                            ListItem({.headline = [label] { Text(label); }, .containerColor = colors::Transparent},
                                     Modifier().clickable([longSheet, choice, label] { choice.set(label); longSheet.set(false); }));
                        });
                    });
                });
        });
    });
}

void CardsSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto cardTaps = remember([] { return mutableStateOf(0); });
        Section("Kartlar ve BottomAppBar", [&] {
            // Card(onClick): the ripple keeps to the card's corners.
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                Card([cardTaps] { cardTaps.set(cardTaps.peek() + 1); }, Modifier().weight(1).height(72),
                     [&] { Text(std::format("Card · {}", cardTaps.get()), Modifier().padding(12)); });
                ElevatedCard([cardTaps] { cardTaps.set(cardTaps.peek() + 1); }, Modifier().weight(1).height(72),
                             [] { Text("Elevated", Modifier().padding(12)); });
                OutlinedCard([cardTaps] { cardTaps.set(cardTaps.peek() + 1); }, Modifier().weight(1).height(72),
                             [] { Text("Outlined", Modifier().padding(12)); });
            });
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(16), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    SmallFloatingActionButton([] {}, [] { Icon(Icons::Filled::Edit, "Küçük FAB"); });
                    FloatingActionButton([] {}, [] { Icon(Icons::Filled::Edit, "FAB"); });
                    MediumFloatingActionButton([] {}, [] { Icon(Icons::Filled::Edit, "Orta FAB", Modifier().size(28)); });
                    LargeFloatingActionButton([] {}, [] { Icon(Icons::Filled::Edit, "Büyük FAB", Modifier().size(36)); });
                });
            // M3 Expressive's extended FABs: switched off, they narrow to their icon.
            auto fabsOpen = remember([] { return mutableStateOf(true); });
            Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                Switch(fabsOpen.get(), [fabsOpen](bool on) { fabsOpen.set(on); });
                Text("  Genişletilmiş", {.font = typography().bodyMedium});
            });
            Column(Modifier(), {.verticalArrangement = Arrangement::spacedBy(12)}, [&] {
                SmallExtendedFloatingActionButton([] {}, Icons::Filled::Add, "Yeni", fabsOpen.get());
                MediumExtendedFloatingActionButton([] {}, Icons::Filled::Edit, "Yaz", fabsOpen.get());
                LargeExtendedFloatingActionButton([] {}, Icons::Filled::Email, "Gönder", fabsOpen.get());
            });
            BottomAppBar({.actions = [] {
                              IconButton([] {}, [] { Icon(Icons::Filled::Check, "Tamam"); });
                              IconButton([] {}, [] { Icon(Icons::Filled::Edit, "Düzenle"); });
                              IconButton([] {}, [] { Icon(Icons::Filled::Delete, "Sil"); });
                          },
                          .floatingActionButton = [] {
                              FloatingActionButton([] {}, [] { Icon(Icons::Filled::Add, "Ekle"); });
                          },
                          .containerColor = cs.surfaceContainerHighest,
                          .windowInsets = false},
                         Modifier().clip(RoundedCornerShape(12)));
        });
    });
}
