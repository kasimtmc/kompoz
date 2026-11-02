# Text

Showing text, styling it, letting the user select it, and taking text input. Kompoz shapes text with HarfBuzz, lays out right-to-left scripts, and falls back to the system's fonts for characters the bundled font lacks.

## Showing text

`Text` shows a string with the current style (`LocalTextStyle`) in the current content color:

```cpp body
Text("Hello, world");
Text("A title", style = typography().titleLarge);
Text("A note", Modifier().padding(8), color = colorScheme().onSurfaceVariant);
```

It takes the text, then Compose's parameters, by place or by name: `modifier`, `color`, `fontSize`, `fontStyle`, `fontWeight`, `fontFamily`, `letterSpacing`, `textDecoration`, `textAlign`, `lineHeight`, `overflow`, `softWrap`, `maxLines`, `minLines`, `onTextLayout`, `style`, `autoSize`. Text sizes are in sp, scaled by the system's font size.

## Styling text

A `TextStyle` holds a text's look: `style = …` gives a whole one (a theme's type style, `typography().titleLarge`, too), and what is given by name goes over it. Its fields, which Text also takes by name unless said otherwise:

| Field | What it does |
|---|---|
| `font` | A whole `Font`: Material's type styles (`typography().bodyLarge`) are fonts (only in a `TextStyle`). |
| `fontSize` | The size: a number of sp, or `TextUnit::em(1.2f)` relative to the current size. |
| `fontWeight` | `FontWeight::Normal`, `Medium`, `Bold`, or `W100` … `W900`. |
| `color` | The color; unset, `LocalContentColor`. |
| `textAlign` | `TextAlign::Start`, `Center`, `End`, `Justify`. |
| `textDecoration` | `TextDecoration::Underline`, `LineThrough`. |
| `maxLines`, `minLines` | How many lines it may take (0: no limit), and at least. |
| `fontStyle` | `FontStyle::Italic`. |
| `fontFamily` | `FontFamily::Serif`, `Monospace`, or one made from files. |
| `letterSpacing` | Extra space after each character. |
| `fontFeatureSettings` | OpenType features as CSS writes them: `"tnum"`, `"smcp"` (only in a `TextStyle`). |
| `overflow` | What the last line shows when cut off: `TextOverflow::Clip`, `Ellipsis`, `StartEllipsis`, `MiddleEllipsis`. |
| `softWrap` | `false`: one line per paragraph. |
| `autoSize` | The font size that fits the space: `TextAutoSize::StepBased(12, 48)`. |
| `lineHeight` | sp from one baseline to the next. |

Give only the ones you need, in any order:

```cpp body
#! std::string article;
Text(article, Modifier().fillMaxWidth(), fontWeight = FontWeight::Medium, textAlign = TextAlign::Justify, maxLines = 3, overflow = TextOverflow::Ellipsis);
```

## Several styles in one text

An `AnnotatedString` carries styles over parts of its text, as Compose's does. Build one with `buildAnnotatedString`:

```cpp
#! void openTerms() {}
void Terms() {
    Text(buildAnnotatedString([](AnnotatedString::Builder& b) {
        b.append("By continuing you accept the ");
        b.withLink([] { openTerms(); }, [&] { b.append("terms"); });
        b.append(". ");
        b.withStyle({.fontWeight = FontWeight::Bold}, [&] { b.append("No refunds."); });
    }));
}
```

- `withStyle(SpanStyle, content)` styles what `content` appends; spans nest, inner ones win. `SpanStyle` has `fontSize`, `fontWeight`, `fontStyle`, `fontFamily`, `color`, `textDecoration`, `background`, `letterSpacing`, `brush`…
- `withStyle(ParagraphStyle, content)` makes a paragraph with its own alignment, indent and line height.
- `withLink(onClick, content)` makes what `content` appends clickable, underlined in the primary color; `LinkAnnotation::Url(url)` opens a web address.
- `pushStyle`/`pop`, `addStyle(style, start, end)` and string annotations work as in Compose.

`AnnotatedString::fromHtml` turns simple HTML (`<b>`, `<i>`, `<u>`, `<a href>`, `<br>`, `<p>`, lists) into an annotated string.

## Fonts

Kompoz bundles Noto Sans in three weights (regular, medium and bold) and uses the system's fonts for everything it lacks: symbols, emoji, CJK, other scripts. `FontFamily::Serif` and `FontFamily::Monospace` use the system's where it has them.

Your own fonts are asset files:

```cpp
void Quote(const std::string& text) {
    static const FontFamily lora = FontFamily::fromFiles({{"fonts/Lora-Regular.ttf"},
                                                          {"fonts/Lora-Bold.ttf", FontWeight::Bold}});
    Text(text, fontSize = 20, fontStyle = FontStyle::Italic, fontFamily = lora);
}
```

A weight without a file of its own is made from the nearest one (bold ones emboldened); a missing italic is slanted. A variable font can be listed several times with `FontVariation` settings for each weight. See [Resources](resources.md) for where asset files go.

## Selecting text

Text isn't selectable by default. Inside a `SelectionContainer`, it is: one selection across all its texts, with handles on touch screens, a Copy action, mouse dragging and Ctrl+C:

```cpp body
SelectionContainer(Modifier(), [&] {
    Column() {
        Text("Order number");
        Text("KMP-2026-0042", fontWeight = FontWeight::Bold);
        DisableSelection([] { Text("(not selectable)"); });
    };
});
```

## Text fields

`TextField` (filled) and `OutlinedTextField` are Material's text fields. As in Compose, their text is either hoisted, with a value and a callback, or kept in a `TextFieldState`.

### With a value and a callback

```cpp
void EmailField() {
    auto email = rememberSaveable([] { return mutableStateOf(std::string()); });
    OutlinedTextField(email.get(), [email](std::string v) { email.set(std::move(v)); }, Modifier().fillMaxWidth(),
                      singleLine = true,
                      label = "Email",
                      supportingText = "We never share it",
                      keyboardOptions = KeyboardOptions{.keyboardType = KeyboardType::Email, .imeAction = ImeAction::Next});
}
```

### With a TextFieldState

A `TextFieldState` keeps the text and its selection, and an undo history:

```cpp
#! void send(const std::string&) {}
void Composer() {
    auto message = rememberTextFieldState();
    Row(Modifier().fillMaxWidth(), verticalAlignment = Alignment::CenterVertically) {
        OutlinedTextField(message, Modifier().weight(1), maxLines = 4, placeholder = "Message");
        IconButton([message] { send(message.text()); message.clearText(); }) { Icon(Icons::AutoMirrored::Filled::Send, "Send"); };
    };
}
```

`text()` and `selection()` are observable. `edit([](TextFieldBuffer& b) { … })` changes both at once; `undoState().undo()` undoes, and the field also undoes with Ctrl+Z. `InputTransformation::maxLength(10)` limits what can be typed, and an `OutputTransformation` changes how the text is shown without changing it.

### Options

| Option | What it does |
|---|---|
| `singleLine`, `minLines`, `maxLines` | One line (Enter runs the keyboard action), or how many lines it shows before scrolling. |
| `label`, `placeholder` | A label that floats above the text, and a hint shown while it is empty. |
| `prefix`, `suffix` | Text before and after the input, such as a currency or a unit. |
| `leadingIcon`, `trailingIcon` | Composables at the ends. |
| `supportingText`, `isError` | A line under the field, and the error colors. |
| `keyboardOptions` | The keyboard's type (`Email`, `Number`, `Password`…), its action key (`Next`, `Done`, `Search`…), capitalization and autocorrect. |
| `keyboardActions`, `onSubmit` | What the action key does. |
| `visualTransformation` | `VisualTransformation::Password` shows dots. |
| `enabled`, `readOnly` | Disabled, or focusable and selectable but not editable. |

`BasicTextField` is a field with no decoration, for designs of your own; its `decorator` option draws around the text.

On Android, the soft keyboard shows suggestions and autocorrects, and its type and action key follow `keyboardOptions`. On desktops, fields have the usual keyboard editing: Ctrl+arrows by word, Home and End, Shift to select, double click for a word.

### Focus

A `FocusRequester` moves focus to a field from code, and `focusManager()` clears it:

```cpp
void Search() {
    auto query = rememberTextFieldState();
    auto focus = remember([] { return FocusRequester(); });
    LaunchedEffect(0, [focus]() -> Task {
        focus.requestFocus();   // focus the field when the screen opens
        co_return;
    });
    OutlinedTextField(query, Modifier().fillMaxWidth().focusRequester(focus), singleLine = true, label = "Search");
}
```

## Measuring text

To draw text on a `Canvas`, or to know how a text was laid out, use a text measurer:

```cpp
void Gauge(int percent) {
    auto measurer = rememberTextMeasurer();
    Canvas(Modifier().size(120), [=](DrawScope& d) {
        d.drawCircle(colorScheme().primary.copy(0.2f));
        TextLayoutResult label = measurer.measure(std::to_string(percent) + "%", {.fontSize = 24});
        d.drawText(label, std::nullopt, {d.center().x - label.size().width / 2, d.center().y - label.size().height / 2});
    });
}
```

`Text` can also tell you how it was laid out, through an `onTextLayout` callback: how many lines it took, whether it was cut off.

```cpp
void Excerpt(const std::string& article) {
    auto cut = remember([] { return mutableStateOf(false); });
    Column() {
        Text(article, Modifier(), maxLines = 3, overflow = TextOverflow::Ellipsis, onTextLayout = [cut](const TextLayoutResult& r) { cut.set(r.hasVisualOverflow()); });
        if (cut.get()) TextButton([] {}) { Text("Read more"); };
    };
}
```

Offsets into text are UTF-8 byte offsets everywhere in Kompoz.

## Right-to-left and mixed text

Text in Arabic, Hebrew or Persian is laid out right to left, and mixed text is ordered by the Unicode bidirectional algorithm. A paragraph's direction comes from its first letter that has one; text without one (digits, punctuation) goes the layout's way.

## See also

- [Resources](resources.md): font files and the library's own texts.
- [Theming](theming.md): the type scale.
- [Accessibility](accessibility.md): how text is read out.
