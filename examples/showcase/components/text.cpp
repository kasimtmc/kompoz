// Components screen: text (AnnotatedString, fonts, overflow, fields).

#include "showcase.hpp"

// AnnotatedString (styles, a link), overflow and the system font scale.
void TextSection(CallSite site) {
    composable(site, [=] {
        auto linkClicks = remember([] { return mutableStateOf(0); });
        const ColorScheme& cs = colorScheme();
        Density density = LocalDensity.current();
        auto fitWidth = remember([] { return mutableStateOf(0.6f); });
        Section("Metin: AnnotatedString · FontFamily · TextOverflow", [&] {
            // autoSize: the largest size that fits the box (TextAutoSize.StepBased).
            Box(Modifier().fillMaxWidth(fitWidth.get()).height(56).background(cs.surfaceContainerHigh, 12).padding(8, 4), Alignment::CenterStart,
                [&] { Text("Sığdığı kadar büyük", Modifier().fillMaxWidth(), {.maxLines = 1, .autoSize = TextAutoSize::StepBased(10, 48, 1)}); });
            Slider(fitWidth.get(), [fitWidth](float v) { fitWidth.set(v); }, Modifier().fillMaxWidth(), {.valueFrom = 0.2f, .valueTo = 1});
            Text(buildAnnotatedString([&](AnnotatedString::Builder& b) {
                     b.append("Bir metinde ");
                     b.withStyle({.fontWeight = FontWeight::Bold}, [&] { b.append("kalın"); });
                     b.append(", ");
                     b.withStyle({.fontStyle = FontStyle::Italic}, [&] { b.append("eğik"); });
                     b.append(", ");
                     b.withStyle({.color = cs.primary, .background = cs.primaryContainer}, [&] { b.append(" vurgulu "); });
                     b.append(", ");
                     b.withStyle({.textDecoration = TextDecoration::LineThrough}, [&] { b.append("üstü çizili"); });
                     b.append(" ve ");
                     b.withStyle({.fontSize = 22, .fontWeight = FontWeight::Bold, .color = cs.tertiary}, [&] { b.append("büyük"); });
                     b.append(" parçalar bir arada satır kaydırılır. ");
                     b.withLink([linkClicks] { linkClicks.set(linkClicks.get() + 1); }, [&] { b.append("Bu bir bağlantı"); });
                     b.append(std::format(" ({} kez tıklandı).", linkClicks.get()));
                 }),
                 Modifier(), {.font = typography().bodyLarge});
            Text("FontFamily.Serif: Kalite hızın önündedir.", {.font = typography().bodyLarge, .fontFamily = FontFamily::Serif});
            Text("FontFamily.Monospace: int main() { return 0; }",
                 {.font = typography().bodyMedium, .fontFamily = FontFamily::Monospace});
            Text("Yedek fontlar: → ← ✓ ★ ♫ ∑ √ · 😀 🚀 🎉 ❤️ · 日本語 中文 한국어", {.font = typography().bodyLarge});
            // ZWJ sequences, skin tones, flags, a keycap and a tag sequence: one glyph each.
            Text("Emoji dizileri: \U0001F468\u200D\U0001F469\u200D\U0001F467 \U0001F469\u200D\U0001F4BB \U0001F44D\U0001F3FD "
                 "\U0001F1F9\U0001F1F7 \U0001F1E9\U0001F1EA \U0001F3F3\uFE0F\u200D\U0001F308 1\uFE0F\u20E3 "
                 "\U0001F3F4\U000E0067\U000E0062\U000E0065\U000E006E\U000E0067\U000E007F",
                 {.font = typography().bodyLarge});
            // HarfBuzz shapes all text: Devanagari reorders and joins, Arabic letters join and go
            // right to left; a paragraph's direction comes from its first letter.
            Text("Hintçe: नमस्ते दुनिया · İbranice: שלום עולם", {.font = typography().bodyLarge});
            Text("Türkçe içinde Arapça: \u00AB\u0645\u0631\u062D\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645\u00BB ve 2026.",
                 {.font = typography().bodyLarge});
            Text("\u0645\u0631\u062D\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645! \u0647\u0630\u0627 \u0646\u0635 "
                 "\u0639\u0631\u0628\u064A \u0645\u0639 Kompoz \u0648 123.",
                 Modifier().fillMaxWidth(), {.font = typography().bodyLarge});
            // Bidi isolates: an Arabic name in a Turkish sentence; isolated (RLI ... PDI), the number
            // after it stays in the sentence instead of joining the name.
            Text("Yalıtımsız: Yorum \u0623\u062D\u0645\u062F 3 kez", {.font = typography().bodyMedium});
            Text("RLI…PDI ile: Yorum \u2067\u0623\u062D\u0645\u062F\u2069 3 kez", {.font = typography().bodyMedium});
            // Bracket pairs: an Arabic word in parentheses after Arabic keeps both parentheses
            // (mirrored) around it, not the closing one out by the Turkish.
            Text("Parantez: \u0645\u0631\u062D\u0628\u0627 (\u0639\u0627\u0644\u0645) d\u00FCnya", {.font = typography().bodyMedium});
            // Styled pieces of both directions are ordered with the line: the bold Arabic word
            // comes first (on the right) of the Arabic stretch.
            Text(buildAnnotatedString([&](AnnotatedString::Builder& b) {
                     b.append("Bi\u00E7imli: ");
                     b.withStyle({.fontWeight = FontWeight::Bold, .color = cs.primary}, [&] { b.append("\u0645\u0631\u062D\u0628\u0627"); });
                     b.append(" \u0628\u0627\u0644\u0639\u0627\u0644\u0645 d\u00FCnya");
                 }),
                 Modifier(), {.font = typography().bodyMedium});
            Text("TextOverflow.Ellipsis: çok uzun bir başlık tek satıra sığmayınca sonu üç noktayla biter",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant, .maxLines = 1, .overflow = TextOverflow::Ellipsis});
            // LinkAnnotation: a URL (opened by the platform) and a tagged clickable, restyled while
            // focused (Tab goes from link to link, Enter opens one), the mouse is over them or they
            // are pressed; BaselineShift: super- and subscripts.
            Text(buildAnnotatedString([&](AnnotatedString::Builder& b) {
                     TextLinkStyles styles{.style = SpanStyle{.color = cs.primary},
                                           .focusedStyle = SpanStyle{.color = cs.onTertiaryContainer, .background = cs.tertiaryContainer},
                                           .hoveredStyle = SpanStyle{.background = cs.primaryContainer},
                                           .pressedStyle = SpanStyle{.color = cs.tertiary}};
                     b.append("LinkAnnotation: ");
                     b.withLink(LinkAnnotation::Url("https://developer.android.com/develop/ui/compose/text", styles), [&] { b.append("Compose metin belgesi"); });
                     b.append(" · ");
                     b.withLink(LinkAnnotation::Clickable("sayaç", styles, [linkClicks](const LinkAnnotation&) { linkClicks.set(linkClicks.get() + 1); }),
                                [&] { b.append(std::format("tıkla ({})", linkClicks.get())); });
                     b.append(" · E = mc");
                     b.withStyle({.fontSize = 11, .baselineShift = BaselineShift::Superscript}, [&] { b.append("2"); });
                     b.append(" · H");
                     b.withStyle({.fontSize = 11, .baselineShift = BaselineShift::Subscript}, [&] { b.append("2"); });
                     b.append("O · x");
                     b.withStyle({.fontSize = 11, .baselineShift = BaselineShift::Superscript}, [&] { b.append("n+1"); });
                 }),
                 Modifier(), {.font = typography().bodyLarge});
            // TextOverflow::StartEllipsis / MiddleEllipsis (one line) and minLines.
            Text("StartEllipsis: /home/kullanici/Projeler/kompoz/examples/showcase/main.cpp", Modifier().fillMaxWidth(),
                 {.font = typography().bodyMedium, .maxLines = 1, .overflow = TextOverflow::StartEllipsis});
            Text("MiddleEllipsis: kompoz-showcase-android-arm64-v8a-debug-2026-10-09.apk", Modifier().fillMaxWidth(),
                 {.font = typography().bodyMedium, .maxLines = 1, .overflow = TextOverflow::MiddleEllipsis});
            Text("minLines = 3: tek satırlık metin bile üç satır yer kaplar", Modifier().fillMaxWidth().border(1, cs.outlineVariant, 8).padding(8),
                 {.font = typography().bodyMedium, .minLines = 3});
            // brush: a gradient across the text; shadow: blurred (or sharp) glyphs under it.
            Text("Degrade yazı (TextStyle.brush)", {.font = typography().headlineSmall,
                                                   .brush = Brush::horizontalGradient({cs.primary, cs.tertiary, Color{0.9f, 0.3f, 0.2f, 1}})});
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(24)}, [&] {
                Text("Bulanık gölge", {.font = typography().titleLarge, .shadow = Shadow{cs.primary.copy(0.7f), {2, 3}, 4}});
                Text("Keskin gölge", {.font = typography().titleLarge, .shadow = Shadow{cs.tertiary, {2, 2}, 0}});
            });
            // drawStyle: Stroke outlines the glyphs (a span's too) instead of filling them.
            Text(buildAnnotatedString([&](AnnotatedString::Builder& b) {
                     b.append("Dolu ");
                     b.withStyle({.color = cs.primary, .drawStyle = Stroke(1.5f, StrokeCap::Butt, StrokeJoin::Round)}, [&] { b.append("DIŞ HAT"); });
                     b.append(" yazı");
                 }),
                 Modifier(), {.font = typography().headlineMedium, .fontWeight = FontWeight::Black});
            // FontVariation: one variable font file at three weights (here the system's Cantarell,
            // where there is one).
            static const char* const variable = "/usr/share/fonts/cantarell/Cantarell-VF.otf";
            static const bool hasVariable = [] {
                std::FILE* f = std::fopen(variable, "rb");
                if (f) std::fclose(f);
                return f != nullptr;
            }();
            if (hasVariable) {
                static const FontFamily cantarell = FontFamily::fromFiles(
                    {{variable, FontWeight::Thin, FontStyle::Normal, {FontVariation::weight(100)}},
                     {variable, FontWeight::Normal, FontStyle::Normal, {FontVariation::weight(400)}},
                     {variable, FontWeight::ExtraBold, FontStyle::Normal, {FontVariation::weight(800)}}});
                Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(12)}, [&] {
                    for (FontWeight w : {FontWeight::Thin, FontWeight::Normal, FontWeight::ExtraBold})
                        Text(std::format("wght {}", w.weight), {.font = typography().titleLarge, .fontWeight = w, .fontFamily = cantarell});
                });
            }
            // textGeometricTransform: glyphs stretched across (their advances too) and slanted.
            Text(buildAnnotatedString([&](AnnotatedString::Builder& b) {
                     b.withStyle({.textGeometricTransform = TextGeometricTransform{1.6f, 0}}, [&] { b.append("geniş"); });
                     b.append(" · ");
                     b.withStyle({.textGeometricTransform = TextGeometricTransform{0.6f, 0}}, [&] { b.append("dar"); });
                     b.append(" · ");
                     b.withStyle({.textGeometricTransform = TextGeometricTransform{1, 0.35f}}, [&] { b.append("eğik"); });
                     b.append(" · ");
                     b.withStyle({.textGeometricTransform = TextGeometricTransform{1, -0.35f}}, [&] { b.append("ters eğik"); });
                 }),
                 Modifier(), {.font = typography().titleLarge, .shadow = Shadow{cs.primary.copy(0.6f), {2, 2}, 3}});  // its shadow as they are
            // letterSpacing (sp after each letter, here and in a span) and lineHeight (baseline to
            // baseline; Material's styles have theirs, 20 sp for bodyMedium).
            Text(buildAnnotatedString([&](AnnotatedString::Builder& b) {
                     b.append("letterSpacing: ");
                     b.withStyle({.letterSpacing = 6}, [&] { b.append("GENİŞ"); });
                     b.append(" · sıkı ");
                     b.withStyle({.letterSpacing = -1}, [&] { b.append("yazı"); });
                     b.append(" · bitişik yazılar ayrılmaz: ");
                     b.withStyle({.letterSpacing = 6}, [&] { b.append("\u0645\u0631\u062D\u0628\u0627"); });
                 }),
                 Modifier(), {.font = typography().bodyLarge});
            // em: shares of a font size — a size of the text around it, spacing of its own size.
            Text(buildAnnotatedString([&](AnnotatedString::Builder& b) {
                     b.append("em: ");
                     b.withStyle({.fontSize = 1.5_em}, [&] { b.append("1.5 em"); });
                     b.append(" · ");
                     b.withStyle({.fontSize = 0.75_em}, [&] { b.append("0.75 em"); });
                     b.append(" · ");
                     b.withStyle({.letterSpacing = 0.3_em}, [&] { b.append("aralık 0.3 em"); });
                 }),
                 Modifier(), {.font = typography().bodyLarge});
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(12)}, [&] {
                for (float lh : {0.f, 28.f}) {
                    Text(lh > 0 ? "lineHeight 28 sp: satırlar arası daha açık, okuması daha rahat bir paragraf"
                                : "bodyMedium'un 20 sp'si: Material'in tip ölçeğindeki satır yüksekliği",
                         Modifier().weight(1).background(cs.surfaceContainerHigh, 8).padding(8),
                         {.font = typography().bodyMedium, .lineHeight = lh > 0 ? std::optional<TextUnit>(lh) : std::nullopt});
                }
            });
            // LineHeightStyle: where the glyphs sit in a 40 sp line (Material's styles center them).
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                std::pair<const char*, float> aligns[] = {
                    {"Top", LineHeightStyle::Alignment::Top}, {"Center", LineHeightStyle::Alignment::Center}, {"Bottom", LineHeightStyle::Alignment::Bottom}};
                for (auto [name, alignment] : aligns)
                    Text(name, Modifier().weight(1).background(cs.surfaceContainerHigh, 8),
                         {.font = typography().labelLarge,
                          .textAlign = TextAlign::Center,
                          .lineHeight = 40,
                          .lineHeightStyle = LineHeightStyle{.alignment = alignment, .trim = LineHeightStyle::Trim::None}});
            });
            // TextAlign::Justify and TextIndent: wrapped lines stretch to the width at their spaces
            // (the last one stays at the start); the first line begins further in.
            Text("TextAlign.Justify ve TextIndent: paragrafın ilk satırı 24 sp içeriden başlar, sarılan satırlar boşluklarından "
                 "esneyerek iki kenara yaslanır, son satır ise başta kalır. Sağdan sola paragraflarda girinti sağdadır.",
                 Modifier().fillMaxWidth().background(cs.surfaceContainerHigh, 8).padding(8),
                 {.font = typography().bodyMedium, .textAlign = TextAlign::Justify, .textIndent = {24, 0}});
            // FontWeight 100..900: the bundled font has 400, 500 and 700; the rest come in the nearest
            // (as CSS picks), Black emboldened, as Android does.
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 12, .verticalSpacing = 4}, [&] {
                std::pair<const char*, FontWeight> weights[] = {{"Light", FontWeight::Light},       {"Normal", FontWeight::Normal},
                                                                {"Medium", FontWeight::Medium},     {"SemiBold", FontWeight::SemiBold},
                                                                {"Bold", FontWeight::Bold},         {"Black", FontWeight::Black}};
                for (auto [name, weight] : weights) Text(name, {.font = typography().titleMedium, .fontWeight = weight});
            });
            // ParagraphStyle: parts of one text as paragraphs of their own: a centered title, a
            // justified paragraph with an indent, a signature at the end.
            Text(buildAnnotatedString([&](AnnotatedString::Builder& b) {
                     b.withStyle(ParagraphStyle{.textAlign = TextAlign::Center}, [&] {
                         b.withStyle({.fontWeight = FontWeight::Bold}, [&] { b.append("ParagraphStyle"); });
                     });
                     b.withStyle(ParagraphStyle{.textAlign = TextAlign::Justify, .textIndent = TextIndent{20, 0}}, [&] {
                         b.append("Tek bir metnin parçaları kendi paragrafları olur: bu paragraf iki yana yaslı ve ilk satırı içeriden "
                                  "başlıyor, başlık ortalı, imza sonda.");
                     });
                     b.withStyle(ParagraphStyle{.textAlign = TextAlign::End}, [&] {
                         b.withStyle({.fontStyle = FontStyle::Italic, .color = cs.onSurfaceVariant}, [&] { b.append("— Kompoz"); });
                     });
                 }),
                 Modifier().fillMaxWidth().background(cs.surfaceContainerHigh, 8).padding(8), {.font = typography().bodyMedium});
            // InlineTextContent: composables in a sentence, in the room each placeholder keeps (em: as
            // big as the text), flowing and wrapping with it: in the text's middle, or on its baseline.
            auto inlineRoom = [](float width, PlaceholderVerticalAlign align) { return Placeholder{TextUnit::em(width), TextUnit::em(1.2f), align}; };
            Text(buildAnnotatedString([&](AnnotatedString::Builder& b) {
                     b.append("Metnin içinde bir kalp ");
                     b.appendInlineContent("kalp", "[kalp]");
                     b.append(", bir ");
                     b.appendInlineContent("rozet", "[yeni]");
                     b.append(" rozeti ve tabanında duran bir yıldız ");
                     b.appendInlineContent("yıldız", "[yıldız]");
                     b.append(" var; satırla birlikte akıp kayıyorlar.");
                 }),
                 Modifier().fillMaxWidth(), {.font = typography().bodyLarge},
                 {{"kalp", {inlineRoom(1.2f, PlaceholderVerticalAlign::TextCenter),
                            [&](const std::string& alt) { Icon(Icons::Filled::Favorite, alt, Modifier().fillMaxSize(), cs.error); }}},
                  {"rozet", {inlineRoom(2.8f, PlaceholderVerticalAlign::TextCenter),
                             [&](const std::string&) {
                                 Box(Modifier().fillMaxSize().background(cs.primary, 50), Alignment::Center, [&] {
                                     Text("YENİ", {.font = typography().labelSmall, .color = cs.onPrimary});
                                 });
                             }}},
                  {"yıldız", {inlineRoom(1.2f, PlaceholderVerticalAlign::AboveBaseline),
                              [&](const std::string& alt) { Icon(Icons::Filled::Star, alt, Modifier().fillMaxSize(), cs.tertiary); }}}});
            // AnnotatedString::fromHtml: styled text and links from HTML (Compose's, the tags Android's Html knows).
            Text(AnnotatedString::fromHtml(
                     "<h3>fromHtml</h3><b>Kalın</b>, <i>eğik</i>, <u>altı çizili</u>, <del>silik</del>, H<sub>2</sub>O, "
                     "<font color=\"#E57373\">renkli</font>, <span style=\"background-color:#3949AB\">vurgulu</span>, "
                     "<code>kod()</code> ve <a href=\"https://developer.android.com\">bir bağlantı</a>."
                     "<ul><li>liste öğesi</li><li>&amp; varlıklar: &copy; &#8364;</li></ul>",
                     TextLinkStyles{.style = SpanStyle{.color = cs.primary}}),
                 Modifier().fillMaxWidth(), {.font = typography().bodyMedium});
            // fontFeatureSettings: OpenType features of the font (where it has them: this one's digits
            // are as wide by default, and it has small capitals and fractions).
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(16)}, [&] {
                Column([&] {
                    Text("Düz", {.font = typography().labelSmall, .color = cs.onSurfaceVariant});
                    Text("Kompoz UI 1/2", {.font = typography().titleMedium});
                });
                Column([&] {
                    Text("\"smcp\"", {.font = typography().labelSmall, .color = cs.onSurfaceVariant});
                    Text("Kompoz UI", {.font = typography().titleMedium, .fontFeatureSettings = "smcp"});
                });
                Column([&] {
                    Text("\"frac\"", {.font = typography().labelSmall, .color = cs.onSurfaceVariant});
                    Text("1/2 · 3/4", {.font = typography().titleMedium, .fontFeatureSettings = "frac"});
                });
            });
            // LineBreak: the same heading and paragraph broken greedily (Simple), evenly (Heading)
            // and with the least uneven ends (Paragraph).
            auto lineBreak = remember([] { return mutableStateOf(0); });
            SingleChoiceSegmentedButtonRow(Modifier().fillMaxWidth(), [&] {
                const char* names[] = {"Simple", "Heading", "Paragraph"};
                for (int i = 0; i < 3; ++i) SegmentedButton(lineBreak.get() == i, [lineBreak, i] { lineBreak.set(i); }, [&, i] { Text(names[i]); });
            });
            LineBreak breaks[] = {LineBreak::Simple, LineBreak::Heading, LineBreak::Paragraph};
            // The heading in 85% of its one line width: filled greedily, a word or two is left over.
            const std::string heading = "Kompoz ile çok platformlu arayüzler artık kolay";
            auto headingMeasurer = rememberTextMeasurer();
            float headingWidth = headingMeasurer.measure(heading, {.font = typography().titleMedium}).size().width * 0.85f;
            Column(Modifier().background(cs.surfaceContainerHigh, 8).padding(8), {.verticalArrangement = Arrangement::spacedBy(6)}, [&] {
                Text(heading, Modifier().width(headingWidth), {.font = typography().titleMedium, .lineBreak = breaks[lineBreak.get()]});
                Text("LineBreak satırların nerede kırılacağını seçer: Simple her satırı sırayla doldurur, Heading satırları "
                     "eşitler, Paragraph satır sonlarında kalan boşlukları en az dengesiz bırakır.",
                     Modifier().width(180), {.font = typography().bodySmall, .lineBreak = breaks[lineBreak.get()]});
            });
            // basicMarquee: a line too long for its room scrolls through it, its copy following;
            // the second one only while it has the focus.
            Text("basicMarquee: sığmayan satır kayan yazı gibi akar, ardından kendi kopyası gelir (sonsuza dek)",
                 Modifier().fillMaxWidth().basicMarquee({.iterations = MarqueeDefaults::Forever}).testTag("marquee"),
                 {.font = typography().bodyMedium});
            auto marqueeFocus = remember([] { return FocusRequester(); });
            Text("WhileFocused: dokunun, odaklanınca kayar (60 dp/s); başka bir alana geçince başa döner",
                 Modifier()
                     .fillMaxWidth()
                     .basicMarquee({.animationMode = MarqueeAnimationMode::WhileFocused, .velocity = 60})
                     .focusRequester(marqueeFocus)
                     .focusable()
                     .clickable([marqueeFocus] { marqueeFocus.requestFocus(); }),
                 {.font = typography().bodyMedium, .color = cs.primary});
            Text(std::format("LocalDensity: {:.2f} px/dp, yazı ölçeği {:.2f} (Ayarlar › Ekran › Yazı tipi boyutu)",
                             density.density, density.fontScale),
                 {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
        });
    });
}
