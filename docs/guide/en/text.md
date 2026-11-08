# Text

Showing text, styling it, letting the user select it, and taking text input. Kompoz shapes text with HarfBuzz, lays out right-to-left scripts, and falls back to the system's fonts for characters the bundled font lacks.

## Showing text

`Text` shows a string with the current style (`LocalTextStyle`) in the current content color:

```kotlin body
Text("Hello, world")
Text("A title", style = MaterialTheme.typography.titleLarge)
Text("A note", modifier = Modifier.padding(8.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
```

It takes the text, then Compose's parameters, by place or by name: `modifier`, `color`, `fontSize`, `fontStyle`, `fontWeight`, `fontFamily`, `letterSpacing`, `textDecoration`, `textAlign`, `lineHeight`, `overflow`, `softWrap`, `maxLines`, `minLines`, `onTextLayout`, `style`, `autoSize`. Text sizes are in sp, scaled by the system's font size.

## Styling text

A `TextStyle` holds a text's look: `style = …` gives a whole one (a theme's type style, `typography().titleLarge`, too), and what is given by name goes over it. Its fields, which Text also takes by name unless said otherwise:

| Field | What it does |
|---|---|
| `font` | A whole `Font`: Material's type styles (`typography().bodyLarge`) are fonts (only in a `TextStyle`). |
| `fontSize` | The size: a number of sp, or `1.2.em` relative to the current size. |
| `fontWeight` | `FontWeight.Normal`, `Medium`, `Bold`, or `W100` … `W900`. |
| `color` | The color; unset, `LocalContentColor`. |
| `textAlign` | `TextAlign.Start`, `Center`, `End`, `Justify`. |
| `textDecoration` | `TextDecoration.Underline`, `LineThrough`. |
| `maxLines`, `minLines` | How many lines it may take (0: no limit), and at least. |
| `fontStyle` | `FontStyle.Italic`. |
| `fontFamily` | `FontFamily.Serif`, `Monospace`, or one made from files. |
| `letterSpacing` | Extra space after each character. |
| `fontFeatureSettings` | OpenType features as CSS writes them: `"tnum"`, `"smcp"` (only in a `TextStyle`). |
| `overflow` | What the last line shows when cut off: `TextOverflow.Clip`, `Ellipsis`, `StartEllipsis`, `MiddleEllipsis`. |
| `softWrap` | `false`: one line per paragraph. |
| `autoSize` | The font size that fits the space: `TextAutoSize.StepBased(12.sp, 48.sp)`. |
| `lineHeight` | sp from one baseline to the next. |

Give only the ones you need, in any order:

```kotlin body
#! val article = ""
Text(article, modifier = Modifier.fillMaxWidth(), fontWeight = FontWeight.Medium, textAlign = TextAlign.Justify, maxLines = 3, overflow = TextOverflow.Ellipsis)
```

## Several styles in one text

An `AnnotatedString` carries styles over parts of its text, as Compose's does. Build one with `buildAnnotatedString`:

```kotlin
#! fun openTerms() {}
@Composable
fun Terms() {
    Text(buildAnnotatedString {
        append("By continuing you accept the ")
        withLink(LinkAnnotation.Clickable("terms") { openTerms() }) { append("terms") }
        append(". ")
        withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { append("No refunds.") }
    })
}
```

- `withStyle(SpanStyle, content)` styles what `content` appends; spans nest, inner ones win. `SpanStyle` has `fontSize`, `fontWeight`, `fontStyle`, `fontFamily`, `color`, `textDecoration`, `background`, `letterSpacing`, `brush`…
- `withStyle(ParagraphStyle, content)` makes a paragraph with its own alignment, indent and line height.
- `withLink(LinkAnnotation.Clickable(tag) { … }) { … }` makes what the block appends clickable, underlined in the primary color; `LinkAnnotation.Url(url)` opens a web address. Kompoz's `withLink({ … }) { … }` takes the click alone.
- `pushStyle`/`pop`, `addStyle(style, start, end)` and string annotations work as in Compose.

`AnnotatedString.fromHtml` turns simple HTML (`<b>`, `<i>`, `<u>`, `<a href>`, `<br>`, `<p>`, lists) into an annotated string.

## Fonts

Kompoz bundles Noto Sans in three weights (regular, medium and bold) and uses the system's fonts for everything it lacks: symbols, emoji, CJK, other scripts. `FontFamily.Serif` and `FontFamily.Monospace` use the system's where it has them. `FontFamily.fromFiles` makes a family from font files (Kompoz's; Compose's `FontFamily(Font(…))` names resources).

Your own fonts are asset files:

```kotlin
val Lora = FontFamily.fromFiles(listOf(FontFile("fonts/Lora-Regular.ttf"), FontFile("fonts/Lora-Bold.ttf", FontWeight.Bold)))

@Composable
fun Quote(text: String) {
    Text(text, fontSize = 20.sp, fontStyle = FontStyle.Italic, fontFamily = Lora)
}
```

A weight without a file of its own is made from the nearest one (bold ones emboldened); a missing italic is slanted. A variable font can be listed several times with `FontVariation` settings for each weight. See [Resources](resources.md) for where asset files go.

## Selecting text

Text isn't selectable by default. Inside a `SelectionContainer`, it is: one selection across all its texts, with handles on touch screens, a Copy action, mouse dragging and Ctrl+C:

```kotlin body
SelectionContainer {
    Column {
        Text("Order number")
        Text("KMP-2026-0042", fontWeight = FontWeight.Bold)
        DisableSelection { Text("(not selectable)") }
    }
}
```

## Text fields

`TextField` (filled) and `OutlinedTextField` are Material's text fields. As in Compose, their text is either hoisted, with a value and a callback, or kept in a `TextFieldState`.

### With a value and a callback

```kotlin
@Composable
fun EmailField() {
    var email by rememberSaveable { mutableStateOf("") }
    OutlinedTextField(
        value = email,
        onValueChange = { email = it },
        modifier = Modifier.fillMaxWidth(),
        singleLine = true,
        label = { Text("Email") },
        supportingText = { Text("We never share it") },
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Email, imeAction = ImeAction.Next),
    )
}
```

### With a TextFieldState

A `TextFieldState` keeps the text and its selection, and an undo history:

```kotlin
#! fun send(text: String) {}
@Composable
fun Composer() {
    val message = rememberTextFieldState()
    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        OutlinedTextField(
            state = message,
            modifier = Modifier.weight(1f),
            lineLimits = TextFieldLineLimits.MultiLine(maxHeightInLines = 4),
            placeholder = { Text("Message") },
        )
        IconButton(onClick = { send(message.text.toString()); message.clearText() }) {
            Icon(Icons.AutoMirrored.Filled.Send, contentDescription = "Send")
        }
    }
}
```

`text` and `selection` are observable. `edit { … }` changes both at once (its block on a `TextFieldBuffer`); `undoState.undo()` undoes, and the field also undoes with Ctrl+Z. `InputTransformation.maxLength(10)` limits what can be typed, and an `OutputTransformation` changes how the text is shown without changing it.

### Options

| Option | What it does |
|---|---|
| `singleLine`, `minLines`, `maxLines`; `lineLimits` | One line (Enter runs the keyboard action), or how many lines it shows before scrolling; `TextFieldLineLimits.SingleLine` or `MultiLine(min, max)` with a state. |
| `label`, `placeholder` | A label that floats above the text, and a hint shown while it is empty: composables, `label = { Text("…") }`, as in Compose (a string will do too). |
| `prefix`, `suffix` | Text before and after the input, such as a currency or a unit (strings here, where Compose takes composables). |
| `leadingIcon`, `trailingIcon` | Composables at the ends. |
| `supportingText`, `isError` | A line under the field (a composable, or a string), and the error colors. |
| `keyboardOptions` | The keyboard's type (`Email`, `Number`, `Password`…), its action key (`Next`, `Done`, `Search`…), capitalization and autocorrect. |
| `keyboardActions`, `onSubmit` | What the action key does. |
| `visualTransformation` | `VisualTransformation.Password` shows dots. |
| `enabled`, `readOnly` | Disabled, or focusable and selectable but not editable. |

`BasicTextField` is a field with no decoration, for designs of your own; its `decorator` option draws around the text.

On Android, the soft keyboard shows suggestions and autocorrects, and its type and action key follow `keyboardOptions`. On desktops, fields have the usual keyboard editing: Ctrl+arrows by word, Home and End, Shift to select, double click for a word.

### Focus

A `FocusRequester` moves focus to a field from code, and `focusManager()` clears it:

```kotlin
@Composable
fun Search() {
    val query = rememberTextFieldState()
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) {
        focus.requestFocus()   // focus the field when the screen opens
    }
    OutlinedTextField(state = query, modifier = Modifier.fillMaxWidth().focusRequester(focus), lineLimits = TextFieldLineLimits.SingleLine, label = { Text("Search") })
}
```

## Measuring text

To draw text on a `Canvas`, or to know how a text was laid out, use a text measurer:

```kotlin
@Composable
fun Gauge(percent: Int) {
    val measurer = rememberTextMeasurer()
    val primary = MaterialTheme.colorScheme.primary   // the theme is read in composition, not while drawing
    Canvas(modifier = Modifier.size(120.dp)) {
        drawCircle(primary.copy(alpha = 0.2f))
        val label = measurer.measure("$percent%", style = TextStyle(fontSize = 24.sp))
        drawText(label, topLeft = Offset(center.x - label.size.width / 2, center.y - label.size.height / 2))
    }
}
```

`Text` can also tell you how it was laid out, through an `onTextLayout` callback: how many lines it took, whether it was cut off.

```kotlin
@Composable
fun Excerpt(article: String) {
    var cut by remember { mutableStateOf(false) }
    Column {
        Text(article, maxLines = 3, overflow = TextOverflow.Ellipsis, onTextLayout = { cut = it.hasVisualOverflow })
        if (cut) TextButton(onClick = { }) { Text("Read more") }
    }
}
```

Offsets into text are UTF-8 byte offsets everywhere in Kompoz.

## Right-to-left and mixed text

Text in Arabic, Hebrew or Persian is laid out right to left, and mixed text is ordered by the Unicode bidirectional algorithm. A paragraph's direction comes from its first letter that has one; text without one (digits, punctuation) goes the layout's way.

## See also

- [Resources](resources.md): font files and the library's own texts.
- [Theming](theming.md): the type scale.
- [Accessibility](accessibility.md): how text is read out.
