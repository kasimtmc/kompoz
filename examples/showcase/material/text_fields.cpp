// Material screen: text fields and focus.

#include "showcase.hpp"

@Composable
fun TextFieldsSection() {
    val cs = MaterialTheme.colorScheme
    var email by remember { mutableStateOf("") }
    val password = rememberTextFieldState()
    var showPassword by remember { mutableStateOf(false) }
    var query by remember { mutableStateOf("") }
    var amount by remember { mutableStateOf("250") }
    var kilos by remember { mutableStateOf("") }
    var searched by remember { mutableStateOf("") }
    var motto by remember { mutableStateOf("Az olsun, öz olsun") }
    // Two paragraphs: Turkish from the left, Arabic from the right (each its own way).
    val notes = rememberTextFieldState("Merhaba dünya!\nمرحبا بالعالم!")
    val plate = rememberTextFieldState("34 ABC 12")
    val phoneNumber = rememberTextFieldState("5551234567")
    val tagQuery = rememberTextFieldState()
    val emailFocus = remember { FocusRequester() }
    val focus = LocalFocusManager.current
    Section("Metin alanları ve odak") {
        val badEmail = email.isNotEmpty() && !email.contains('@')
        OutlinedTextField(
            value = email, onValueChange = { email = it },
            modifier = Modifier.fillMaxWidth().focusRequester(emailFocus),
            singleLine = true, placeholder = "ad@ornek.com", label = "E-posta",
            leadingIcon = { Icon(Icons.Filled.Email, contentDescription = "E-posta") },
            supportingText = if (badEmail) "Geçerli bir e-posta adresi girin" else "Next: şifreye geçer · koddan odaklanınca klavye açılmaz",
            isError = badEmail,
            // Focused from code ("E-postaya odaklan" below): no keyboard until a tap.
            keyboardOptions = KeyboardOptions(autoCorrect = false, keyboardType = KeyboardType.Email, imeAction = ImeAction.Next, showKeyboardOnFocus = false),
        )
        // A secure field: the character just typed shows for a moment; the eye shows it all.
        OutlinedSecureTextField(
            state = password, modifier = Modifier.fillMaxWidth(),
            label = "Şifre",
            leadingIcon = { Icon(Icons.Filled.Lock, contentDescription = "Şifre") },
            trailingIcon = {
                IconButton(onClick = { showPassword = !showPassword }) {
                    Icon(Icons.Filled.Visibility, contentDescription = if (showPassword) "Gizle" else "Göster",
                         tint = if (showPassword) MaterialTheme.colorScheme.primary else LocalContentColor.current)
                }
            },
            supportingText = "Yazdığınız son karakter bir an görünür; kopyalanamaz",
            textObfuscationMode = if (showPassword) TextObfuscationMode.Visible else TextObfuscationMode.RevealLastTyped,
            keyboardOptions = KeyboardOptions(autoCorrect = false, keyboardType = KeyboardType.Password, imeAction = ImeAction.Done),
        )
        TextField(
            value = query, onValueChange = { query = it }, modifier = Modifier.fillMaxWidth(),
            singleLine = true, placeholder = "örn. kahve", label = "Ara (filled TextField)",
            leadingIcon = { Icon(Icons.Filled.Search, contentDescription = "Ara") },
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
            keyboardActions = KeyboardActions(onSearch = {
                searched = query
                focus.clearFocus()
            }),
        )
        if (searched.isNotEmpty()) Text("Arandı: $searched", color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        // Its own colors (TextFieldColors) and shape: tertiary, the selection too, round
        // without the indicator line (a common Compose look).
        TextField(
            value = motto, onValueChange = { motto = it }, modifier = Modifier.fillMaxWidth(),
            singleLine = true, label = "Slogan (kendi renkleri)",
            leadingIcon = { Icon(Icons.Filled.Favorite, contentDescription = "Slogan") },
            supportingText = "TextFieldColors + shape: üçüncül renkler, yuvarlak köşeler",
            colors = TextFieldDefaults.colors(
                focusedTextColor = cs.onTertiaryContainer, unfocusedTextColor = cs.onTertiaryContainer,
                focusedContainerColor = cs.tertiaryContainer, unfocusedContainerColor = cs.tertiaryContainer.copy(alpha = 0.5f),
                cursorColor = cs.tertiary, textSelectionColors = TextSelectionColors(cs.tertiary, cs.tertiary.copy(alpha = 0.4f)),
                focusedIndicatorColor = Color.Transparent, unfocusedIndicatorColor = Color.Transparent,
                focusedLeadingIconColor = cs.tertiary, unfocusedLeadingIconColor = cs.tertiary,
                focusedLabelColor = cs.tertiary, unfocusedLabelColor = cs.onTertiaryContainer,
            ),
            shape = RoundedCornerShape(16.dp),
        )
        // enabled = false: no focus, the disabled colors.
        OutlinedTextField(
            value = "TR-0042", onValueChange = { }, modifier = Modifier.fillMaxWidth(),
            singleLine = true, label = "Müşteri no (enabled = false)",
            leadingIcon = { Icon(Icons.Filled.Lock, contentDescription = "Kilitli") },
            supportingText = "Odaklanmaz, düzenlenmez · etiket üstte (Above)",
            enabled = false,
            labelPosition = TextFieldLabelPosition.Above(),
        )
        // Prefix and suffix: shown once the label floats; digits only.
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(
                value = amount, onValueChange = { amount = it.filter { c -> c.isDigit() } }, modifier = Modifier.weight(1f),
                singleLine = true, label = "Tutar", prefix = "₺", suffix = ",00",
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            )
            // Its label always up: the placeholder and "kg" show while it is empty.
            OutlinedTextField(
                value = kilos, onValueChange = { kilos = it.filter { c -> c.isDigit() } }, modifier = Modifier.weight(1f),
                singleLine = true, placeholder = "0", label = "Ağırlık", suffix = "kg",
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                labelPosition = TextFieldLabelPosition.Attached(true),
            )
        }
        // A TextFieldState with an InputTransformation: what is typed comes in as
        // capitals and stops at 10 characters; the state keeps the selection too.
        OutlinedTextField(
            state = plate, modifier = Modifier.fillMaxWidth(),
            singleLine = true, label = "Plaka (büyük harf, en çok 10)",
            supportingText = "${plate.text.length} / 10 · seçim ${plate.selection.start}–${plate.selection.end}",
            inputTransformation = InputTransformation.allCaps("tr").then(InputTransformation.maxLength(10)),
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            TextButton(onClick = { plate.edit { selectAll() } }) { Text("Tümünü seç") }
            TextButton(onClick = { plate.edit { placeCursorBeforeCharAt(0) } }) { Text("İmleci başa al") }
            TextButton(onClick = { plate.clearText() }) { Text("Temizle") }
        }
        // OutputTransformation: the state keeps the digits, the field shows them in
        // groups (the caret and taps step over the spaces); digits only, at most 10.
        OutlinedTextField(
            state = phoneNumber, modifier = Modifier.fillMaxWidth(),
            singleLine = true, label = "Telefon", prefix = "+90 ",
            supportingText = "Değer: ${phoneNumber.text} · gösterim: 555 123 45 67",
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Phone),
            inputTransformation = InputTransformation.byValue { current, proposed ->
                if (proposed.all { it.isDigit() }) proposed else current
            }.then(InputTransformation.maxLength(10)),
            outputTransformation = OutputTransformation {
                for (at in listOf(8, 6, 3)) if (length > at) insert(at, " ")
            },
        )
        // Multi-line (Compose's default; TextFieldLineLimits.MultiLine(2, 5)): wraps, Enter starts a
        // new line, up to 5 lines then scrolls.
        OutlinedTextField(
            state = notes, modifier = Modifier.fillMaxWidth(),
            lineLimits = TextFieldLineLimits.MultiLine(2, 5), placeholder = "Birkaç satır yazın", label = "Not (çok satırlı)",
            supportingText = "${notes.text.length} karakter · rememberTextFieldState",
            keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences),
        )
        // Its undo history: a word at a time (Ctrl+Z / Ctrl+Shift+Z on a keyboard too).
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            val undo = notes.undoState
            TextButton(onClick = { undo.undo() }, enabled = undo.canUndo) { Text("Geri al") }
            TextButton(onClick = { undo.redo() }, enabled = undo.canRedo) { Text("Yinele") }
        }
        // BasicTextField's decorator (Compose's decorationBox): its own look around the text — a
        // rounded box with an icon and a clear button; a tap anywhere on it types into the field.
        BasicTextField(
            state = tagQuery, modifier = Modifier.fillMaxWidth(),
            lineLimits = TextFieldLineLimits.SingleLine, placeholder = "Etiket ara (decorator)",
            decorator = { innerTextField ->
                Row(modifier = Modifier.fillMaxWidth().heightIn(48.dp).background(cs.surfaceContainerHigh, CircleShape).padding(16.dp, 4.dp, 4.dp, 4.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Filled.Search, contentDescription = null, tint = cs.onSurfaceVariant)
                    Box(modifier = Modifier.weight(1f)) { innerTextField() }
                    if (tagQuery.text.isNotEmpty()) {
                        IconButton(onClick = { tagQuery.clearText() }) { Icon(Icons.Filled.Close, contentDescription = "Temizle") }
                    }
                }
            },
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilledTonalButton(onClick = { emailFocus.requestFocus() }) { Text("E-postaya odaklan") }
            OutlinedButton(onClick = { focus.clearFocus() }) { Text("Odağı temizle") }
        }
        FocusPropertiesDemo()
    }
}
