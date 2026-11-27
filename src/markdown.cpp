#include <kompoz/markdown.hpp>

#include <kompoz/scrollbar.hpp>

#include <algorithm>

namespace kompoz::detail {

namespace {

using markdown::Block;
using markdown::Inline;

struct MarkdownContext {
    std::string content;
    MarkdownColors colors;
    MarkdownTypography typography;
    MarkdownPadding padding;
    MarkdownComponents components;
};

bool composing() {
    Kompozer* k = Kompozer::current();
    return k && k->currentScope();
}

TextStyle withColor(TextStyle style, Color color) {
    if (!style.color && color.isSpecified()) style.color = color;
    return style;
}

// Inline HTML's common tags as styles (<b>, <i>, <u>, <s>, <code>, <sup>, <sub>, <mark>), <br> a line.
void htmlInline(AnnotatedString::Builder& b, std::string_view tag, std::vector<std::string>& open, const MarkdownColors& colors) {
    bool closing = tag.size() > 1 && tag[1] == '/';
    std::string name;
    for (size_t i = closing ? 2 : 1; i < tag.size() && std::isalnum(static_cast<unsigned char>(tag[i])); ++i)
        name += char(std::tolower(static_cast<unsigned char>(tag[i])));
    if (name == "br") {
        b.append("\n");
        return;
    }
    std::optional<SpanStyle> style;
    if (name == "b" || name == "strong") style = SpanStyle{.fontWeight = FontWeight::Bold};
    else if (name == "i" || name == "em") style = SpanStyle{.fontStyle = FontStyle::Italic};
    else if (name == "u" || name == "ins") style = SpanStyle{.textDecoration = TextDecoration::Underline};
    else if (name == "s" || name == "del" || name == "strike") style = SpanStyle{.textDecoration = TextDecoration::LineThrough};
    else if (name == "code" || name == "kbd")
        style = SpanStyle{.fontFamily = FontFamily::Monospace,
                          .background = colors.inlineCodeBackground.isSpecified() ? std::optional(colors.inlineCodeBackground) : std::nullopt};
    else if (name == "sup") style = SpanStyle{.fontSize = TextUnit::em(0.75f), .baselineShift = BaselineShift::Superscript};
    else if (name == "sub") style = SpanStyle{.fontSize = TextUnit::em(0.75f), .baselineShift = BaselineShift::Subscript};
    else if (name == "mark") style = SpanStyle{.background = Color::argb(0xFFFFEB3B)};
    if (!style) return;
    if (closing) {
        auto it = std::find(open.rbegin(), open.rend(), name);
        if (it == open.rend()) return;
        // (what was opened after it closes with it)
        size_t depth = size_t(open.rend() - it) - 1;
        while (open.size() > depth) {
            b.pop();
            open.pop_back();
        }
        return;
    }
    b.pushStyle(*style);
    open.push_back(name);
}

void annotate(AnnotatedString::Builder& b, const std::vector<Inline>& nodes, const MarkdownColors& colors, const MarkdownTypography& type,
              std::vector<std::string>& open) {
    for (const Inline& n : nodes) {
        switch (n.kind) {
        case Inline::Kind::Text: b.append(n.text); break;
        case Inline::Kind::SoftBreak: b.append(" "); break;
        case Inline::Kind::HardBreak: b.append("\n"); break;
        case Inline::Kind::Html: htmlInline(b, n.text, open, colors); break;
        case Inline::Kind::Code: {
            SpanStyle s{.fontFamily = FontFamily::Monospace};
            if (type.inlineCode.fontSize) s.fontSize = type.inlineCode.fontSize;
            if (colors.inlineCodeText.isSpecified()) s.color = colors.inlineCodeText;
            if (colors.inlineCodeBackground.isSpecified()) s.background = colors.inlineCodeBackground;
            b.withStyle(s, [&] { b.append(n.text); });
            break;
        }
        case Inline::Kind::Image: b.append(markdown::plainText(n.children)); break;
        case Inline::Kind::Link:
            b.withLink(LinkAnnotation::Url(n.url, type.textLink), [&] { annotate(b, n.children, colors, type, open); });
            break;
        case Inline::Kind::Strong: b.withStyle(SpanStyle{.fontWeight = FontWeight::Bold}, [&] { annotate(b, n.children, colors, type, open); }); break;
        case Inline::Kind::Emphasis:
            b.withStyle(SpanStyle{.fontStyle = FontStyle::Italic}, [&] { annotate(b, n.children, colors, type, open); });
            break;
        case Inline::Kind::Strikethrough:
            b.withStyle(SpanStyle{.textDecoration = TextDecoration::LineThrough}, [&] { annotate(b, n.children, colors, type, open); });
            break;
        }
    }
}

AnnotatedString annotated(const std::vector<Inline>& nodes, const MarkdownColors& colors, const MarkdownTypography& type) {
    AnnotatedString::Builder b;
    std::vector<std::string> open;
    annotate(b, nodes, colors, type, open);
    while (!open.empty()) b.pop(), open.pop_back();
    return b.toAnnotatedString();
}

// A paragraph of images alone (whitespace between them): shown as images, not their alt texts.
bool imagesOnly(const std::vector<Inline>& nodes) {
    bool any = false;
    for (const Inline& n : nodes) {
        if (n.kind == Inline::Kind::Image) any = true;
        else if (n.kind == Inline::Kind::SoftBreak || n.kind == Inline::Kind::HardBreak) continue;
        else if (n.kind == Inline::Kind::Text && n.text.find_first_not_of(' ') == std::string::npos) continue;
        else return false;
    }
    return any;
}

void blocks(const std::vector<Block>& list, const std::shared_ptr<const MarkdownContext>& c, int depth, bool quoted, bool tight);

MarkdownComponentModel modelOf(const Block& b, const std::shared_ptr<const MarkdownContext>& c) {
    return {.content = c->content, .node = &b, .typography = c->typography, .context_ = c};
}

void listView(const Block& list, const std::shared_ptr<const MarkdownContext>& c, int depth, bool quoted) {
    const MarkdownTypography& t = c->typography;
    Column(Modifier().fillMaxWidth().padding(0, c->padding.list, 0, c->padding.list), [=, &list] {
        for (size_t i = 0; i < list.children.size(); ++i) {
            const Block& item = list.children[i];
            Row(Modifier().fillMaxWidth().padding(0, c->padding.listItemTop, 0, c->padding.listItemBottom),
                RowStyle{.horizontalArrangement = Arrangement::spacedBy(6), .verticalAlignment = Alignment::Top}, [=, &item] {
                    if (item.checked) {
                        if (c->components.checkbox) c->components.checkbox(modelOf(item, c));
                        else Checkbox(*item.checked, nullptr, Modifier().size(24, 24), false);
                    } else {
                        static const char* bullets[] = {"•", "◦", "▪"};
                        std::string marker = list.ordered ? std::to_string(list.start + int(i)) + std::string(1, list.marker)
                                                          : std::string(bullets[depth % 3]);
                        TextStyle style = withColor(list.ordered ? t.ordered : t.bullet, c->colors.text);
                        Text(AnnotatedString(marker), Modifier(), style);
                    }
                    Column(Modifier().weight(1), ColumnStyle{.verticalArrangement = Arrangement::spacedBy(list.tight ? 2 : c->padding.block)},
                           [=, &item] { blocks(item.children, c, depth + 1, quoted, list.tight); });
                });
        }
    });
}

void blockView(const Block& b, const std::shared_ptr<const MarkdownContext>& c, int depth, bool quoted) {
    const MarkdownTypography& t = c->typography;
    const MarkdownColors& colors = c->colors;
    const MarkdownComponents& comp = c->components;
    switch (b.kind) {
    case Block::Kind::Paragraph:
        if (comp.paragraph) return comp.paragraph(modelOf(b, c));
        if (imagesOnly(b.inlines)) {
            for (const Inline& n : b.inlines) {
                if (n.kind != Inline::Kind::Image) continue;
                MarkdownComponentModel m = modelOf(b, c);
                m.url = n.url, m.alt = markdown::plainText(n.children);
                if (comp.image) comp.image(m);
                else AsyncImage(n.url, m.alt, Modifier().fillMaxWidth(), AsyncImageOptions{}, CallSite::current());
            }
            return;
        }
        Text(annotated(b.inlines, colors, t), Modifier().fillMaxWidth(), withColor(quoted ? t.quote : t.paragraph, colors.text));
        return;
    case Block::Kind::Heading: {
        const MarkdownComponent* own[] = {&comp.heading1, &comp.heading2, &comp.heading3, &comp.heading4, &comp.heading5, &comp.heading6};
        const TextStyle* styles[] = {&t.h1, &t.h2, &t.h3, &t.h4, &t.h5, &t.h6};
        int level = std::clamp(b.level, 1, 6) - 1;
        if (*own[level]) return (*own[level])(modelOf(b, c));
        Text(annotated(b.inlines, colors, t), Modifier().fillMaxWidth().semantics([](SemanticsPropertyReceiver& s) { s.heading(); }),
             withColor(*styles[level], colors.text));
        return;
    }
    case Block::Kind::BlockQuote:
        if (comp.blockQuote) return comp.blockQuote(modelOf(b, c));
        Row(Modifier().fillMaxWidth().height(IntrinsicSize::Min), [=, &b] {
            Box(Modifier().width(3).fillMaxHeight().background(colors.dividerColor, RoundedCornerShape(2)));
            Column(Modifier().weight(1).padding(c->padding.blockQuote), ColumnStyle{.verticalArrangement = Arrangement::spacedBy(c->padding.blockQuoteText)},
                   [=, &b] { blocks(b.children, c, depth, true, false); });
        });
        return;
    case Block::Kind::List:
        if (b.ordered && comp.orderedList) return comp.orderedList(modelOf(b, c));
        if (!b.ordered && comp.unorderedList) return comp.unorderedList(modelOf(b, c));
        listView(b, c, depth, quoted);
        return;
    case Block::Kind::ListItem: blocks(b.children, c, depth, quoted, true); return;
    case Block::Kind::CodeBlock: {
        const MarkdownComponent& own = b.fenced ? comp.codeFence : comp.codeBlock;
        if (own) return own(modelOf(b, c));
        ScrollState scroll = rememberScrollState();
        Box(Modifier().fillMaxWidth().background(colors.codeBackground, RoundedCornerShape(8)).horizontalScroll(scroll).padding(c->padding.codeBlock),
            [=, &b] {
                TextStyle style = withColor(t.code, colors.codeText);
                style.softWrap = false;
                Text(AnnotatedString(b.literal), Modifier(), style);
            });
        return;
    }
    case Block::Kind::ThematicBreak:
        if (comp.horizontalRule) return comp.horizontalRule(modelOf(b, c));
        HorizontalDivider(Modifier().padding(0, 8), 1, colors.dividerColor, CallSite::current());
        return;
    case Block::Kind::Table: {
        if (comp.table) return comp.table(modelOf(b, c));
        Column(Modifier().fillMaxWidth().clip(RoundedCornerShape(8)).background(colors.tableBackground).border(1, colors.dividerColor, RoundedCornerShape(8)),
               [=, &b] {
                   for (size_t r = 0; r < b.rows.size(); ++r) {
                       if (r > 0) HorizontalDivider(Modifier(), 1, colors.dividerColor, CallSite::current());
                       Row(Modifier().fillMaxWidth(), [=, &b] {
                           for (size_t col = 0; col < b.rows[r].size(); ++col) {
                               TextStyle style = withColor(t.table, colors.tableText);
                               if (r == 0) style.fontWeight = FontWeight::Bold;
                               markdown::TableAlign a = col < b.aligns.size() ? b.aligns[col] : markdown::TableAlign::None;
                               style.textAlign = a == markdown::TableAlign::Center  ? TextAlign::Center
                                                 : a == markdown::TableAlign::Right ? TextAlign::End
                                                                                    : TextAlign::Start;
                               Box(Modifier().weight(1).padding(10, 8),
                                   [=, &b] { Text(annotated(b.rows[r][col], colors, t), Modifier().fillMaxWidth(), style); });
                           }
                       });
                   }
               });
        return;
    }
    case Block::Kind::HtmlBlock:
        Text(AnnotatedString::fromHtml(b.literal, t.textLink), Modifier().fillMaxWidth(), withColor(t.paragraph, colors.text));
        return;
    }
}

void blocks(const std::vector<Block>& list, const std::shared_ptr<const MarkdownContext>& c, int depth, bool quoted, bool tight) {
    (void)tight;
    for (const Block& b : list) blockView(b, c, depth, quoted);
}

} // namespace

MarkdownColors markdownColor(const MarkdownColors& given) {
    MarkdownColors c = given;
    ColorScheme cs = composing() ? colorScheme() : lightColorScheme();
    auto fill = [](Color& field, Color theme) {
        if (field.isUnspecified()) field = theme;
    };
    fill(c.text, cs.onSurface);
    fill(c.codeText, cs.onSurfaceVariant);
    fill(c.linkText, cs.primary);
    // (seen on any surface it is drawn on: the text's color, faint)
    fill(c.codeBackground, cs.onSurface.copy(0.08f));
    fill(c.inlineCodeBackground, cs.onSurface.copy(0.1f));
    fill(c.dividerColor, cs.outlineVariant);
    fill(c.tableBackground, cs.surfaceContainerLow);
    return c;
}

MarkdownTypography markdownTypography(const MarkdownTypography& given, uint32_t mask) {
    Typography ty = composing() ? typography() : Typography{};
    auto of = [](const Font& f, std::optional<FontWeight> weight = std::nullopt) {
        TextStyle s{.font = f};
        if (weight) s.fontWeight = weight;
        return s;
    };
    MarkdownTypography t;
    t.h1 = of(ty.displaySmall), t.h2 = of(ty.headlineLarge), t.h3 = of(ty.headlineMedium), t.h4 = of(ty.headlineSmall);
    t.h5 = of(ty.titleLarge), t.h6 = of(ty.titleMedium);
    t.text = t.paragraph = t.ordered = t.bullet = t.list = of(ty.bodyLarge);
    t.quote = of(ty.bodyLarge);
    t.quote.fontStyle = FontStyle::Italic;
    t.code = of(ty.bodyMedium);
    t.code.fontFamily = FontFamily::Monospace;
    t.inlineCode = of(ty.bodyLarge);
    t.inlineCode.fontFamily = FontFamily::Monospace;
    t.table = of(ty.bodyMedium);
    Color link = composing() ? colorScheme().primary : lightColorScheme().primary;
    t.textLink = TextLinkStyles{.style = SpanStyle{.color = link, .textDecoration = TextDecoration::Underline}};
    TextStyle* fields[] = {&t.h1, &t.h2, &t.h3, &t.h4, &t.h5, &t.h6, &t.text, &t.code, &t.inlineCode, &t.quote, &t.paragraph, &t.ordered,
                           &t.bullet, &t.list};
    const TextStyle* from[] = {&given.h1, &given.h2, &given.h3, &given.h4, &given.h5, &given.h6, &given.text, &given.code, &given.inlineCode,
                               &given.quote, &given.paragraph, &given.ordered, &given.bullet, &given.list};
    for (int i = 0; i < 14; ++i)
        if (mask & (1u << i)) *fields[i] = *from[i];
    if (mask & (1u << 14)) t.textLink = given.textLink;
    if (mask & (1u << 15)) t.table = given.table;
    // (text given and paragraph not: paragraphs are text)
    if ((mask & (1u << 6)) && !(mask & (1u << 10))) t.paragraph = t.text;
    return t;
}

AnnotatedString markdownAnnotated(const std::vector<markdown::Inline>& inlines) {
    return annotated(inlines, markdownColor({}), markdownTypography({}, 0));
}

void markdown(const std::string& content, const MarkdownColors& colors, const MarkdownTypography& typography, const Modifier& modifier,
              const MarkdownPadding& padding, const MarkdownComponents& components, CallSite site) {
    KompozableScope scope(site);
    // Read again only when the text changes.
    auto parsed = remember(content, [&] { return std::make_shared<const std::vector<markdown::Block>>(markdown::parse(content)); });
    MarkdownColors themed = markdownColor(colors);
    MarkdownTypography type = typography;
    if (type.textLink.style && !type.textLink.style->color) type.textLink.style->color = themed.linkText;
    auto c = std::make_shared<MarkdownContext>(MarkdownContext{content, themed, type, padding, components});
    Column(modifier, ColumnStyle{.verticalArrangement = Arrangement::spacedBy(padding.block)}, [c, parsed] { blocks(*parsed, c, 0, false, false); });
}

void markdownBlocks(const MarkdownComponentModel& model, CallSite site) {
    KompozableScope scope(site);
    auto c = std::static_pointer_cast<const MarkdownContext>(model.context_);
    if (!c || !model.node) return;
    blocks(model.node->children, c, 0, model.node->kind == markdown::Block::Kind::BlockQuote, false);
}

} // namespace kompoz::detail
