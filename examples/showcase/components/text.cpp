// Components screen: text (AnnotatedString, fonts, overflow, fields).

#include "showcase.hpp"

// FontVariation: one variable font file at three weights (here the system's Cantarell, where
// there is one). Font files are the platform's: this part stays C++.
void VariableFontSample(CallSite site = CallSite::current()) {
    composable(site, [=] {
        static const char* const variable = "/usr/share/fonts/cantarell/Cantarell-VF.otf";
        static const bool hasVariable = [] {
            std::FILE* f = std::fopen(variable, "rb");
            if (f) std::fclose(f);
            return f != nullptr;
        }();
        if (!hasVariable) return;
        static const FontFamily cantarell = FontFamily::fromFiles(
            {{variable, FontWeight::Thin, FontStyle::Normal, {FontVariation::weight(100)}},
             {variable, FontWeight::Normal, FontStyle::Normal, {FontVariation::weight(400)}},
             {variable, FontWeight::ExtraBold, FontStyle::Normal, {FontVariation::weight(800)}}});
        Row(Modifier().fillMaxWidth(), horizontalArrangement = Arrangement::spacedBy(12), [&] {
            for (FontWeight w : {FontWeight::Thin, FontWeight::Normal, FontWeight::ExtraBold})
                Text(std::format("wght {}", w.weight), fontWeight = w, fontFamily = cantarell, style = typography().titleLarge);
        });
    });
}

// AnnotatedString (styles, a link), overflow and the system font scale.
@Composable
fun TextSection() {
    var linkClicks by remember { mutableStateOf(0) }
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val density = LocalDensity.current
    var fitWidth by remember { mutableStateOf(0.6f) }
    Section("Metin: AnnotatedString · FontFamily · TextOverflow") {
        // autoSize: the largest size that fits the box (TextAutoSize.StepBased).
        Box(modifier = Modifier.fillMaxWidth(fitWidth).height(56.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).padding(8.dp, 4.dp),
            contentAlignment = Alignment.CenterStart) {
            Text("Sığdığı kadar büyük", modifier = Modifier.fillMaxWidth(), maxLines = 1, autoSize = TextAutoSize.StepBased(10.sp, 48.sp, 1.sp))
        }
        Slider(value = fitWidth, onValueChange = { fitWidth = it }, modifier = Modifier.fillMaxWidth(), valueRange = 0.2f..1f)
        Text(buildAnnotatedString {
            append("Bir metinde ")
            withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { append("kalın") }
            append(", ")
            withStyle(SpanStyle(fontStyle = FontStyle.Italic)) { append("eğik") }
            append(", ")
            withStyle(SpanStyle(color = cs.primary, background = cs.primaryContainer)) { append(" vurgulu ") }
            append(", ")
            withStyle(SpanStyle(textDecoration = TextDecoration.LineThrough)) { append("üstü çizili") }
            append(" ve ")
            withStyle(SpanStyle(fontSize = 22.sp, fontWeight = FontWeight.Bold, color = cs.tertiary)) { append("büyük") }
            append(" parçalar bir arada satır kaydırılır. ")
            withLink(LinkAnnotation.Clickable("bağlantı") { linkClicks++ }) { append("Bu bir bağlantı") }
            append(" ($linkClicks kez tıklandı).")
        }, style = typography.bodyLarge)
        Text("FontFamily.Serif: Kalite hızın önündedir.", fontFamily = FontFamily.Serif, style = typography.bodyLarge)
        Text("FontFamily.Monospace: int main() { return 0; }", fontFamily = FontFamily.Monospace, style = typography.bodyMedium)
        Text("Yedek fontlar: → ← ✓ ★ ♫ ∑ √ · 😀 🚀 🎉 ❤️ · 日本語 中文 한국어", style = typography.bodyLarge)
        // ZWJ sequences, skin tones, flags, a keycap and a tag sequence: one glyph each.
        Text("Emoji dizileri: 👨‍👩‍👧 👩‍💻 👍🏽 " +
                 "🇹🇷 🇩🇪 🏳️‍🌈 1️⃣ " +
                 "🏴󠁧󠁢󠁥󠁮󠁧󠁿",
             style = typography.bodyLarge)
        // HarfBuzz shapes all text: Devanagari reorders and joins, Arabic letters join and go
        // right to left; a paragraph's direction comes from its first letter.
        Text("Hintçe: नमस्ते दुनिया · İbranice: שלום עולם", style = typography.bodyLarge)
        Text("Türkçe içinde Arapça: «مرحبا بالعالم» ve 2026.", style = typography.bodyLarge)
        Text("مرحبا بالعالم! هذا نص عربي مع Kompoz و 123.",
             modifier = Modifier.fillMaxWidth(), style = typography.bodyLarge)
        // Bidi isolates: an Arabic name in a Turkish sentence; isolated (RLI ... PDI), the number
        // after it stays in the sentence instead of joining the name.
        Text("Yalıtımsız: Yorum أحمد 3 kez", style = typography.bodyMedium)
        Text("RLI…PDI ile: Yorum ⁧أحمد⁩ 3 kez", style = typography.bodyMedium)
        // Bracket pairs: an Arabic word in parentheses after Arabic keeps both parentheses
        // (mirrored) around it, not the closing one out by the Turkish.
        Text("Parantez: مرحبا (عالم) dünya", style = typography.bodyMedium)
        // Styled pieces of both directions are ordered with the line: the bold Arabic word
        // comes first (on the right) of the Arabic stretch.
        Text(buildAnnotatedString {
            append("Biçimli: ")
            withStyle(SpanStyle(fontWeight = FontWeight.Bold, color = cs.primary)) { append("مرحبا") }
            append(" بالعالم dünya")
        }, style = typography.bodyMedium)
        Text("TextOverflow.Ellipsis: çok uzun bir başlık tek satıra sığmayınca sonu üç noktayla biter",
             color = cs.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis, style = typography.bodyMedium)
        // LinkAnnotation: a URL (opened by the platform) and a tagged clickable, restyled while
        // focused (Tab goes from link to link, Enter opens one), the mouse is over them or they
        // are pressed; BaselineShift: super- and subscripts.
        val linkStyles = TextLinkStyles(style = SpanStyle(color = cs.primary),
                                        focusedStyle = SpanStyle(color = cs.onTertiaryContainer, background = cs.tertiaryContainer),
                                        hoveredStyle = SpanStyle(background = cs.primaryContainer),
                                        pressedStyle = SpanStyle(color = cs.tertiary))
        Text(buildAnnotatedString {
            append("LinkAnnotation: ")
            withLink(LinkAnnotation.Url("https://developer.android.com/develop/ui/compose/text", linkStyles)) { append("Compose metin belgesi") }
            append(" · ")
            withLink(LinkAnnotation.Clickable("sayaç", linkStyles) { linkClicks++ }) { append("tıkla ($linkClicks)") }
            append(" · E = mc")
            withStyle(SpanStyle(fontSize = 11.sp, baselineShift = BaselineShift.Superscript)) { append("2") }
            append(" · H")
            withStyle(SpanStyle(fontSize = 11.sp, baselineShift = BaselineShift.Subscript)) { append("2") }
            append("O · x")
            withStyle(SpanStyle(fontSize = 11.sp, baselineShift = BaselineShift.Superscript)) { append("n+1") }
        }, style = typography.bodyLarge)
        // TextOverflow.StartEllipsis / MiddleEllipsis (one line) and minLines.
        Text("StartEllipsis: /home/kullanici/Projeler/kompoz/examples/showcase/main.cpp", modifier = Modifier.fillMaxWidth(),
             maxLines = 1, overflow = TextOverflow.StartEllipsis, style = typography.bodyMedium)
        Text("MiddleEllipsis: kompoz-showcase-android-arm64-v8a-debug-2026-10-09.apk", modifier = Modifier.fillMaxWidth(),
             maxLines = 1, overflow = TextOverflow.MiddleEllipsis, style = typography.bodyMedium)
        Text("minLines = 3: tek satırlık metin bile üç satır yer kaplar", modifier = Modifier.fillMaxWidth().border(1.dp, cs.outlineVariant, RoundedCornerShape(8.dp)).padding(8.dp),
             minLines = 3, style = typography.bodyMedium)
        // brush: a gradient across the text; shadow: blurred (or sharp) glyphs under it.
        Text("Degrade yazı (TextStyle.brush)", style = typography.headlineSmall.copy(brush = Brush.horizontalGradient(listOf(cs.primary, cs.tertiary, Color(0.9f, 0.3f, 0.2f, 1f)))))
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(24.dp)) {
            Text("Bulanık gölge", style = typography.titleLarge.copy(shadow = Shadow(cs.primary.copy(alpha = 0.7f), Offset(2f, 3f), 4f)))
            Text("Keskin gölge", style = typography.titleLarge.copy(shadow = Shadow(cs.tertiary, Offset(2f, 2f), 0f)))
        }
        // drawStyle: Stroke outlines the glyphs (a span's too) instead of filling them.
        Text(buildAnnotatedString {
            append("Dolu ")
            withStyle(SpanStyle(color = cs.primary, drawStyle = Stroke(width = 1.5f, miter = 4f, cap = StrokeCap.Butt, join = StrokeJoin.Round))) { append("DIŞ HAT") }
            append(" yazı")
        }, fontWeight = FontWeight.Black, style = typography.headlineMedium)
        VariableFontSample()
        // textGeometricTransform: glyphs stretched across (their advances too) and slanted.
        Text(buildAnnotatedString {
            withStyle(SpanStyle(textGeometricTransform = TextGeometricTransform(1.6f, 0f))) { append("geniş") }
            append(" · ")
            withStyle(SpanStyle(textGeometricTransform = TextGeometricTransform(0.6f, 0f))) { append("dar") }
            append(" · ")
            withStyle(SpanStyle(textGeometricTransform = TextGeometricTransform(1f, 0.35f))) { append("eğik") }
            append(" · ")
            withStyle(SpanStyle(textGeometricTransform = TextGeometricTransform(1f, -0.35f))) { append("ters eğik") }
        }, style = typography.titleLarge.copy(shadow = Shadow(cs.primary.copy(alpha = 0.6f), Offset(2f, 2f), 3f)))  // its shadow as they are
        // letterSpacing (sp after each letter, here and in a span) and lineHeight (baseline to
        // baseline; Material's styles have theirs, 20 sp for bodyMedium).
        Text(buildAnnotatedString {
            append("letterSpacing: ")
            withStyle(SpanStyle(letterSpacing = 6.sp)) { append("GENİŞ") }
            append(" · sıkı ")
            withStyle(SpanStyle(letterSpacing = (-1).sp)) { append("yazı") }
            append(" · bitişik yazılar ayrılmaz: ")
            withStyle(SpanStyle(letterSpacing = 6.sp)) { append("مرحبا") }
        }, style = typography.bodyLarge)
        // em: shares of a font size — a size of the text around it, spacing of its own size.
        Text(buildAnnotatedString {
            append("em: ")
            withStyle(SpanStyle(fontSize = 1.5.em)) { append("1.5 em") }
            append(" · ")
            withStyle(SpanStyle(fontSize = 0.75.em)) { append("0.75 em") }
            append(" · ")
            withStyle(SpanStyle(letterSpacing = 0.3.em)) { append("aralık 0.3 em") }
        }, style = typography.bodyLarge)
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Text("bodyMedium'un 20 sp'si: Material'in tip ölçeğindeki satır yüksekliği",
                 modifier = Modifier.weight(1f).background(cs.surfaceContainerHigh, RoundedCornerShape(8.dp)).padding(8.dp), style = typography.bodyMedium)
            Text("lineHeight 28 sp: satırlar arası daha açık, okuması daha rahat bir paragraf",
                 modifier = Modifier.weight(1f).background(cs.surfaceContainerHigh, RoundedCornerShape(8.dp)).padding(8.dp), lineHeight = 28.sp, style = typography.bodyMedium)
        }
        // LineHeightStyle: where the glyphs sit in a 40 sp line (Material's styles center them).
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            val aligns = listOf("Top" to LineHeightStyle.Alignment.Top, "Center" to LineHeightStyle.Alignment.Center, "Bottom" to LineHeightStyle.Alignment.Bottom)
            for ((name, alignment) in aligns) {
                Text(name, modifier = Modifier.weight(1f).background(cs.surfaceContainerHigh, RoundedCornerShape(8.dp)), textAlign = TextAlign.Center, lineHeight = 40.sp,
                     style = typography.labelLarge.copy(lineHeightStyle = LineHeightStyle(alignment = alignment, trim = LineHeightStyle.Trim.None)))
            }
        }
        // TextAlign.Justify and TextIndent: wrapped lines stretch to the width at their spaces
        // (the last one stays at the start); the first line begins further in.
        Text("TextAlign.Justify ve TextIndent: paragrafın ilk satırı 24 sp içeriden başlar, sarılan satırlar boşluklarından " +
                 "esneyerek iki kenara yaslanır, son satır ise başta kalır. Sağdan sola paragraflarda girinti sağdadır.",
             modifier = Modifier.fillMaxWidth().background(cs.surfaceContainerHigh, RoundedCornerShape(8.dp)).padding(8.dp),
             textAlign = TextAlign.Justify, style = typography.bodyMedium.copy(textIndent = TextIndent(24.sp)))
        // FontWeight 100..900: the bundled font has 400, 500 and 700; the rest come in the nearest
        // (as CSS picks), Black emboldened, as Android does.
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            val weights = listOf("Light" to FontWeight.Light, "Normal" to FontWeight.Normal, "Medium" to FontWeight.Medium,
                                 "SemiBold" to FontWeight.SemiBold, "Bold" to FontWeight.Bold, "Black" to FontWeight.Black)
            for ((name, weight) in weights) Text(name, fontWeight = weight, style = typography.titleMedium)
        }
        // ParagraphStyle: parts of one text as paragraphs of their own: a centered title, a
        // justified paragraph with an indent, a signature at the end.
        Text(buildAnnotatedString {
            withStyle(ParagraphStyle(textAlign = TextAlign.Center)) {
                withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { append("ParagraphStyle") }
            }
            withStyle(ParagraphStyle(textAlign = TextAlign.Justify, textIndent = TextIndent(20.sp))) {
                append("Tek bir metnin parçaları kendi paragrafları olur: bu paragraf iki yana yaslı ve ilk satırı içeriden " +
                           "başlıyor, başlık ortalı, imza sonda.")
            }
            withStyle(ParagraphStyle(textAlign = TextAlign.End)) {
                withStyle(SpanStyle(fontStyle = FontStyle.Italic, color = cs.onSurfaceVariant)) { append("— Kompoz") }
            }
        }, modifier = Modifier.fillMaxWidth().background(cs.surfaceContainerHigh, RoundedCornerShape(8.dp)).padding(8.dp), style = typography.bodyMedium)
        // InlineTextContent: composables in a sentence, in the room each placeholder keeps (em: as
        // big as the text), flowing and wrapping with it: in the text's middle, or on its baseline.
        Text(buildAnnotatedString {
            append("Metnin içinde bir kalp ")
            appendInlineContent("kalp", "[kalp]")
            append(", bir ")
            appendInlineContent("rozet", "[yeni]")
            append(" rozeti ve tabanında duran bir yıldız ")
            appendInlineContent("yıldız", "[yıldız]")
            append(" var; satırla birlikte akıp kayıyorlar.")
        }, modifier = Modifier.fillMaxWidth(), style = typography.bodyLarge, inlineContent = mapOf(
            "kalp" to InlineTextContent(Placeholder(1.2.em, 1.2.em, PlaceholderVerticalAlign.TextCenter)) { alt ->
                Icon(Icons.Filled.Favorite, contentDescription = alt, modifier = Modifier.fillMaxSize(), tint = cs.error)
            },
            "rozet" to InlineTextContent(Placeholder(2.8.em, 1.2.em, PlaceholderVerticalAlign.TextCenter)) {
                Box(modifier = Modifier.fillMaxSize().background(cs.primary, RoundedCornerShape(50)), contentAlignment = Alignment.Center) {
                    Text("YENİ", color = cs.onPrimary, style = typography.labelSmall)
                }
            },
            "yıldız" to InlineTextContent(Placeholder(1.2.em, 1.2.em, PlaceholderVerticalAlign.AboveBaseline)) { alt ->
                Icon(Icons.Filled.Star, contentDescription = alt, modifier = Modifier.fillMaxSize(), tint = cs.tertiary)
            },
        ))
        // AnnotatedString.fromHtml: styled text and links from HTML (Compose's, the tags Android's Html knows).
        Text(AnnotatedString.fromHtml(
                 "<h3>fromHtml</h3><b>Kalın</b>, <i>eğik</i>, <u>altı çizili</u>, <del>silik</del>, H<sub>2</sub>O, " +
                     "<font color=\"#E57373\">renkli</font>, <span style=\"background-color:#3949AB\">vurgulu</span>, " +
                     "<code>kod()</code> ve <a href=\"https://developer.android.com\">bir bağlantı</a>." +
                     "<ul><li>liste öğesi</li><li>&amp; varlıklar: &copy; &#8364;</li></ul>",
                 TextLinkStyles(style = SpanStyle(color = cs.primary))),
             modifier = Modifier.fillMaxWidth(), style = typography.bodyMedium)
        // fontFeatureSettings: OpenType features of the font (where it has them: this one's digits
        // are as wide by default, and it has small capitals and fractions).
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            Column {
                Text("Düz", color = cs.onSurfaceVariant, style = typography.labelSmall)
                Text("Kompoz UI 1/2", style = typography.titleMedium)
            }
            Column {
                Text("\"smcp\"", color = cs.onSurfaceVariant, style = typography.labelSmall)
                Text("Kompoz UI", style = typography.titleMedium.copy(fontFeatureSettings = "smcp"))
            }
            Column {
                Text("\"frac\"", color = cs.onSurfaceVariant, style = typography.labelSmall)
                Text("1/2 · 3/4", style = typography.titleMedium.copy(fontFeatureSettings = "frac"))
            }
        }
        // LineBreak: the same heading and paragraph broken greedily (Simple), evenly (Heading)
        // and with the least uneven ends (Paragraph).
        var lineBreak by remember { mutableStateOf(0) }
        SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
            val names = listOf("Simple", "Heading", "Paragraph")
            for (i in 0 until 3) SegmentedButton(selected = lineBreak == i, onClick = { lineBreak = i }, shape = SegmentedButtonDefaults.itemShape(i, 3)) { Text(names[i]) }
        }
        val breaks = listOf(LineBreak.Simple, LineBreak.Heading, LineBreak.Paragraph)
        // The heading in 85% of its one line width: filled greedily, a word or two is left over.
        val heading = "Kompoz ile çok platformlu arayüzler artık kolay"
        val headingMeasurer = rememberTextMeasurer()
        val headingWidth = headingMeasurer.measure(heading, style = typography.titleMedium).size.width * 0.85f
        Column(modifier = Modifier.background(cs.surfaceContainerHigh, RoundedCornerShape(8.dp)).padding(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(heading, modifier = Modifier.width(headingWidth.dp), style = typography.titleMedium.copy(lineBreak = breaks[lineBreak]))
            Text("LineBreak satırların nerede kırılacağını seçer: Simple her satırı sırayla doldurur, Heading satırları " +
                     "eşitler, Paragraph satır sonlarında kalan boşlukları en az dengesiz bırakır.",
                 modifier = Modifier.width(180.dp), style = typography.bodySmall.copy(lineBreak = breaks[lineBreak]))
        }
        // basicMarquee: a line too long for its room scrolls through it, its copy following;
        // the second one only while it has the focus.
        Text("basicMarquee: sığmayan satır kayan yazı gibi akar, ardından kendi kopyası gelir (sonsuza dek)",
             modifier = Modifier.fillMaxWidth().basicMarquee(iterations = Int.MAX_VALUE).testTag("marquee"), style = typography.bodyMedium)
        val marqueeFocus = remember { FocusRequester() }
        Text("WhileFocused: dokunun, odaklanınca kayar (60 dp/s); başka bir alana geçince başa döner",
             modifier = Modifier.fillMaxWidth().basicMarquee(animationMode = MarqueeAnimationMode.WhileFocused, velocity = 60.dp)
                 .focusRequester(marqueeFocus).focusable().clickable { marqueeFocus.requestFocus() },
             color = cs.primary, style = typography.bodyMedium)
        Text("LocalDensity: %.2f px/dp, yazı ölçeği %.2f (Ayarlar › Ekran › Yazı tipi boyutu)".format(density.density, density.fontScale),
             color = cs.onSurfaceVariant, style = typography.bodySmall)
    }
}
