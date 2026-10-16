// Material screen: text fields and focus.

#include "showcase.hpp"

void TextFieldsSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto email = remember([] { return mutableStateOf(std::string()); });
        auto password = rememberTextFieldState();
        auto showPassword = remember([] { return mutableStateOf(false); });
        auto query = remember([] { return mutableStateOf(std::string()); });
        auto amount = remember([] { return mutableStateOf(std::string("250")); });
        auto kilos = remember([] { return mutableStateOf(std::string()); });
        auto searched = remember([] { return mutableStateOf(std::string()); });
        auto motto = remember([] { return mutableStateOf(std::string("Az olsun, öz olsun")); });
        // Two paragraphs: Turkish from the left, Arabic from the right (each its own way).
        auto notes = rememberTextFieldState("Merhaba dünya!\n\u0645\u0631\u062D\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645!");
        auto plate = rememberTextFieldState("34 ABC 12");
        auto phoneNumber = rememberTextFieldState("5551234567");
        auto tagQuery = rememberTextFieldState();
        auto& emailFocus = remember([] { return FocusRequester(); });
        FocusManager focus = focusManager();
        Section("Metin alanları ve odak", [&] {
            bool badEmail = !email.get().empty() && email.get().find('@') == std::string::npos;
            OutlinedTextField(email.get(), [email](std::string v) { email.set(std::move(v)); },
                              Modifier().fillMaxWidth().focusRequester(emailFocus),
                              {.singleLine = true, .placeholder = "ad@ornek.com", .label = "E-posta",
                               .leadingIcon = [] { Icon(Icons::Filled::Email, "E-posta"); },
                               .supportingText = badEmail ? "Geçerli bir e-posta adresi girin"
                                                          : "Next: şifreye geçer · koddan odaklanınca klavye açılmaz",
                               .isError = badEmail,
                               // Focused from code ("E-postaya odaklan" below): no keyboard until a tap.
                               .keyboardOptions = {.autoCorrect = false, .keyboardType = KeyboardType::Email,
                                                   .imeAction = ImeAction::Next, .showKeyboardOnFocus = false}});
            // A secure field: the character just typed shows for a moment; the eye shows it all.
            OutlinedSecureTextField(password, Modifier().fillMaxWidth(),
                                    {.label = "Şifre",
                                     .leadingIcon = [] { Icon(Icons::Filled::Lock, "Şifre"); },
                                     .trailingIcon = [showPassword] {
                                         IconButton([showPassword] { showPassword.set(!showPassword.get()); }, [&] {
                                             Icon(Icons::Filled::Visibility, showPassword.get() ? "Gizle" : "Göster", {},
                                                  showPassword.get() ? std::optional(colorScheme().primary) : std::nullopt);
                                         });
                                     },
                                     .supportingText = "Yazdığınız son karakter bir an görünür; kopyalanamaz",
                                     .textObfuscationMode = showPassword.get() ? TextObfuscationMode::Visible
                                                                               : TextObfuscationMode::RevealLastTyped,
                                     .keyboardOptions = {.autoCorrect = false, .keyboardType = KeyboardType::Password,
                                                         .imeAction = ImeAction::Done}});
            TextField(query.get(), [query](std::string v) { query.set(std::move(v)); }, Modifier().fillMaxWidth(),
                      {.singleLine = true, .placeholder = "örn. kahve", .label = "Ara (filled TextField)",
                       .leadingIcon = [] { Icon(Icons::Filled::Search, "Ara"); },
                       .keyboardOptions = {.imeAction = ImeAction::Search},
                       .keyboardActions = {.onSearch = [query, searched, focus] {
                           searched.set(query.get());
                           focus.clearFocus();
                       }}});
            if (!searched.get().empty())
                Text("Arandı: " + searched.get(), {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            // Its own colors (TextFieldColors) and shape: tertiary, the selection too, round
            // without the indicator line (a common Compose look).
            TextField(motto.get(), [motto](std::string v) { motto.set(std::move(v)); }, Modifier().fillMaxWidth(),
                      {.singleLine = true, .label = "Slogan (kendi renkleri)",
                       .leadingIcon = [] { Icon(Icons::Filled::Favorite, "Slogan"); },
                       .supportingText = "TextFieldColors + shape: üçüncül renkler, yuvarlak köşeler",
                       .colors = {.focusedTextColor = cs.onTertiaryContainer,
                                  .unfocusedTextColor = cs.onTertiaryContainer,
                                  .focusedContainerColor = cs.tertiaryContainer,
                                  .unfocusedContainerColor = cs.tertiaryContainer.copy(0.5f),
                                  .cursorColor = cs.tertiary,
                                  .textSelectionColors = TextSelectionColors{cs.tertiary, cs.tertiary.copy(0.4f)},
                                  .focusedIndicatorColor = colors::Transparent,
                                  .unfocusedIndicatorColor = colors::Transparent,
                                  .focusedLeadingIconColor = cs.tertiary,
                                  .unfocusedLeadingIconColor = cs.tertiary,
                                  .focusedLabelColor = cs.tertiary,
                                  .unfocusedLabelColor = cs.onTertiaryContainer},
                       .shape = RoundedCornerShape(16)});
            // enabled = false: no focus, the disabled colors.
            OutlinedTextField("TR-0042", {}, Modifier().fillMaxWidth(),
                              {.singleLine = true, .label = "Müşteri no (enabled = false)",
                               .leadingIcon = [] { Icon(Icons::Filled::Lock, "Kilitli"); },
                               .supportingText = "Odaklanmaz, düzenlenmez · etiket üstte (Above)",
                               .enabled = false,
                               .labelPosition = TextFieldLabelPosition::Above()});
            // Prefix and suffix: shown once the label floats; digits only.
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                auto digits = [](MutableState<std::string> to) {
                    return [to](std::string v) {
                        std::erase_if(v, [](char c) { return c < '0' || c > '9'; });
                        to.set(std::move(v));
                    };
                };
                OutlinedTextField(amount.get(), digits(amount), Modifier().weight(1),
                                  {.singleLine = true, .label = "Tutar", .prefix = "₺", .suffix = ",00",
                                   .keyboardOptions = {.keyboardType = KeyboardType::Number}});
                // Its label always up: the placeholder and "kg" show while it is empty.
                OutlinedTextField(kilos.get(), digits(kilos), Modifier().weight(1),
                                  {.singleLine = true, .placeholder = "0", .label = "Ağırlık", .suffix = "kg",
                                   .keyboardOptions = {.keyboardType = KeyboardType::Number},
                                   .labelPosition = TextFieldLabelPosition::Attached(true)});
            });
            // A TextFieldState with an InputTransformation: what is typed comes in as
            // capitals and stops at 10 characters; the state keeps the selection too.
            std::string plateText = plate.text();
            size_t plateChars = 0;
            for (size_t i = 0; i < plateText.size(); ++plateChars) utf8::next(plateText, i);
            TextRange plateSelection = plate.selection();
            OutlinedTextField(plate, Modifier().fillMaxWidth(),
                              {.singleLine = true, .label = "Plaka (büyük harf, en çok 10)",
                               .supportingText = std::format("{} / 10 · seçim {}–{}", plateChars, plateSelection.start, plateSelection.end),
                               .inputTransformation = InputTransformation::allCaps("tr").then(InputTransformation::maxLength(10))});
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                TextButton([plate] { plate.edit([](TextFieldBuffer& b) { b.selectAll(); }); }, [] { Text("Tümünü seç"); });
                TextButton([plate] { plate.edit([](TextFieldBuffer& b) { b.placeCursorBeforeCharAt(0); }); },
                           [] { Text("İmleci başa al"); });
                TextButton([plate] { plate.clearText(); }, [] { Text("Temizle"); });
            });
            // OutputTransformation: the state keeps the digits, the field shows them in
            // groups (the caret and taps step over the spaces); digits only, at most 10.
            OutlinedTextField(phoneNumber, Modifier().fillMaxWidth(),
                              {.singleLine = true, .label = "Telefon", .prefix = "+90 ",
                               .supportingText = "Değer: " + phoneNumber.text() + " · gösterim: 555 123 45 67",
                               .keyboardOptions = {.keyboardType = KeyboardType::Phone},
                               .inputTransformation = InputTransformation::byValue([](const std::string& current, const std::string& proposed) {
                                                          return std::ranges::all_of(proposed, [](char c) { return c >= '0' && c <= '9'; }) ? proposed : current;
                                                      }).then(InputTransformation::maxLength(10)),
                               .outputTransformation = OutputTransformation{[](TextFieldBuffer& b) {
                                   for (int at : {8, 6, 3})
                                       if (b.length() > at) b.insert(at, " ");
                               }}});
            // Multi-line (Compose's default; TextFieldLineLimits::MultiLine(2, 5)): wraps, Enter starts a
            // new line, up to 5 lines then scrolls.
            std::string note = notes.text();
            size_t noteChars = 0;
            for (size_t i = 0; i < note.size(); ++noteChars) utf8::next(note, i);
            OutlinedTextField(notes, Modifier().fillMaxWidth(),
                              {.lineLimits = TextFieldLineLimits::MultiLine(2, 5), .placeholder = "Birkaç satır yazın", .label = "Not (çok satırlı)",
                               .supportingText = std::format("{} karakter · rememberTextFieldState", noteChars),
                               .keyboardOptions = {.capitalization = KeyboardCapitalization::Sentences}});
            // Its undo history: a word at a time (Ctrl+Z / Ctrl+Shift+Z on a keyboard too).
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                UndoState undo = notes.undoState();
                TextButton([undo] { undo.undo(); }, Modifier(), {.enabled = undo.canUndo()}, [] { Text("Geri al"); });
                TextButton([undo] { undo.redo(); }, Modifier(), {.enabled = undo.canRedo()}, [] { Text("Yinele"); });
            });
            // BasicTextField's decorator (Compose's decorationBox): its own look around the text — a
            // rounded box with an icon and a clear button; a tap anywhere on it types into the field.
            BasicTextField(tagQuery, Modifier().fillMaxWidth(),
                           {.lineLimits = TextFieldLineLimits::SingleLine, .placeholder = "Etiket ara (decorator)",
                            .decorator = [&](const Content& innerTextField) {
                                Row(Modifier().fillMaxWidth().heightIn(48).background(cs.surfaceContainerHigh, CircleShape).padding(16, 4, 4, 4),
                                    {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically}, [&] {
                                        Icon(Icons::Filled::Search, "", {}, cs.onSurfaceVariant);
                                        Box(Modifier().weight(1), innerTextField);
                                        if (!tagQuery.text().empty())
                                            IconButton([tagQuery] { tagQuery.clearText(); }, [] { Icon(Icons::Filled::Close, "Temizle"); });
                                    });
                            }});
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                FilledTonalButton([emailFocus] { emailFocus.requestFocus(); }, [] { Text("E-postaya odaklan"); });
                OutlinedButton([focus] { focus.clearFocus(); }, [] { Text("Odağı temizle"); });
            });
            FocusPropertiesDemo();
        });
    });
}
