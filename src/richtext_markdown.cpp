// A rich text's Markdown, in (through the Markdown parser) and out.

#include "richtext_model.hpp"

#include <kompoz/markdown_parser.hpp>

#include <algorithm>

namespace kompoz::detail {

namespace {

using markdown::Block;
using markdown::Inline;

// Paragraphs appended one by one, each with its own attributes.
struct ModelBuilder {
    RichModel m;
    bool started = false;
    void paragraph(const RichParagraph& p) {
        if (started) m.append("\n", RichRun{});
        started = true;
        m.paragraphs.back() = p;
    }
    void text(std::string_view s, const RichRun& style) {
        if (!started) paragraph({});
        m.append(s, style);
    }
};

// Inline HTML Markdown carries (<u>, <b>, <br>…): what it opens and closes.
void htmlTag(std::string_view tag, std::vector<RichRun>& stack, ModelBuilder& b) {
    bool closing = tag.size() > 1 && tag[1] == '/';
    std::string name;
    for (size_t i = closing ? 2 : 1; i < tag.size() && std::isalnum(static_cast<unsigned char>(tag[i])); ++i)
        name += char(std::tolower(static_cast<unsigned char>(tag[i])));
    if (name == "br") {
        b.text("\n", stack.back());
        return;
    }
    static const char* known[] = {"u", "ins", "b", "strong", "i", "em", "s", "del", "strike", "code", "mark", "sup", "sub"};
    if (std::find_if(std::begin(known), std::end(known), [&](const char* k) { return name == k; }) == std::end(known)) return;
    if (closing) {
        if (stack.size() > 1) stack.pop_back();
        return;
    }
    RichRun r = stack.back();
    if (name == "u" || name == "ins") spanAdd(r.style, {.textDecoration = TextDecoration::Underline});
    else if (name == "b" || name == "strong") r.style.fontWeight = FontWeight::Bold;
    else if (name == "i" || name == "em") r.style.fontStyle = FontStyle::Italic;
    else if (name == "s" || name == "del" || name == "strike") spanAdd(r.style, {.textDecoration = TextDecoration::LineThrough});
    else if (name == "code") r.code = true;
    else if (name == "mark") r.style.background = Color::argb(0xFFFFEB3B);
    else if (name == "sup") r.style.baselineShift = BaselineShift::Superscript;
    else if (name == "sub") r.style.baselineShift = BaselineShift::Subscript;
    stack.push_back(r);
}

void inlines(const std::vector<Inline>& nodes, std::vector<RichRun>& stack, ModelBuilder& b) {
    for (const Inline& n : nodes) {
        RichRun r = stack.back();
        switch (n.kind) {
        case Inline::Kind::Text: b.text(n.text, r); break;
        case Inline::Kind::SoftBreak: b.text(" ", r); break;
        case Inline::Kind::HardBreak: b.text("\n", r); break;
        case Inline::Kind::Code:
            r.code = true;
            b.text(n.text, r);
            break;
        case Inline::Kind::Html: htmlTag(n.text, stack, b); break;
        case Inline::Kind::Image:  // (its alt text, as a link to it)
            r.link = n.url;
            b.text(markdown::plainText(n.children).empty() ? n.url : markdown::plainText(n.children), r);
            break;
        default: {
            if (n.kind == Inline::Kind::Emphasis) r.style.fontStyle = FontStyle::Italic;
            if (n.kind == Inline::Kind::Strong) r.style.fontWeight = FontWeight::Bold;
            if (n.kind == Inline::Kind::Strikethrough) spanAdd(r.style, {.textDecoration = TextDecoration::LineThrough});
            if (n.kind == Inline::Kind::Link) r.link = n.url;
            stack.push_back(r);
            size_t depth = stack.size();
            inlines(n.children, stack, b);
            stack.resize(depth - 1);
            break;
        }
        }
    }
}

void blocks(const std::vector<Block>& list, ModelBuilder& b, int listLevel) {
    for (const Block& block : list) {
        std::vector<RichRun> stack{RichRun{}};
        switch (block.kind) {
        case Block::Kind::Paragraph:
            b.paragraph({});
            inlines(block.inlines, stack, b);
            break;
        case Block::Kind::Heading:
            b.paragraph({.heading = HeadingStyle(std::clamp(block.level, 1, 6))});
            inlines(block.inlines, stack, b);
            break;
        case Block::Kind::List:
            for (const Block& item : block.children) {
                bool first = true;
                for (const Block& child : item.children) {
                    if (child.kind == Block::Kind::List) {
                        blocks({child}, b, listLevel + 1);
                        continue;
                    }
                    if (first && (child.kind == Block::Kind::Paragraph || child.kind == Block::Kind::Heading)) {
                        b.paragraph({.list = block.ordered ? ListKind::Ordered : ListKind::Unordered, .level = listLevel + 1});
                        if (item.checked) b.text(*item.checked ? "[x] " : "[ ] ", RichRun{});
                        inlines(child.inlines, stack, b);
                    } else {
                        blocks({child}, b, listLevel);
                    }
                    first = false;
                }
                if (first) b.paragraph({.list = block.ordered ? ListKind::Ordered : ListKind::Unordered, .level = listLevel + 1});
            }
            break;
        case Block::Kind::BlockQuote: blocks(block.children, b, listLevel); break;
        case Block::Kind::CodeBlock: {
            size_t start = 0;
            do {
                size_t end = block.literal.find('\n', start);
                std::string_view line = std::string_view(block.literal).substr(start, end == std::string::npos ? std::string::npos : end - start);
                b.paragraph({});
                if (!line.empty()) b.text(line, RichRun{.code = true});
                if (end == std::string::npos) break;
                start = end + 1;
            } while (true);
            break;
        }
        case Block::Kind::ThematicBreak:
            b.paragraph({});
            b.text("---", RichRun{});
            break;
        case Block::Kind::Table:
            for (const auto& row : block.rows) {
                b.paragraph({});
                for (size_t c = 0; c < row.size(); ++c) {
                    if (c) b.text(" | ", RichRun{});
                    inlines(row[c], stack, b);
                }
            }
            break;
        case Block::Kind::HtmlBlock: {
            RichModel html = modelOfHtml(block.literal);
            if (html.text.empty() && html.paragraphs.size() <= 1) break;
            b.paragraph({});
            b.m.insertModel(int(b.m.text.size()), html);
            break;
        }
        case Block::Kind::ListItem: blocks(block.children, b, listLevel); break;
        }
    }
}

// What Markdown can say of a run: links, bold, italic, struck, underlined (as HTML), code.
struct MdStyle {
    std::string link;
    bool bold = false, italic = false, strike = false, underline = false, code = false;
    friend bool operator==(const MdStyle&, const MdStyle&) = default;
};
MdStyle mdStyleOf(const RichRun& r) {
    TextDecoration d = r.style.textDecoration.value_or(TextDecoration::None);
    return {r.link, r.style.fontWeight && r.style.fontWeight->weight >= 600, r.style.fontStyle == FontStyle::Italic,
            contains(d, TextDecoration::LineThrough), contains(d, TextDecoration::Underline), r.code};
}

std::string escaped(std::string_view s, bool lineStart) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (std::string_view("\\`*_[]<>~").find(c) != std::string_view::npos) out += '\\';
        else if (lineStart && i == 0 && (c == '#' || c == '-' || c == '+' || c == '>')) out += '\\';
        out += c;
    }
    // (a paragraph starting "1. " would be a list)
    if (lineStart) {
        size_t d = 0;
        while (d < out.size() && std::isdigit(static_cast<unsigned char>(out[d]))) ++d;
        if (d > 0 && d < out.size() && (out[d] == '.' || out[d] == ')')) out.insert(d, "\\");
    }
    return out;
}

std::string codeSpan(std::string_view s) {
    size_t longest = 0, run = 0;
    for (char c : s) {
        run = c == '`' ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    std::string fence(longest + 1, '`');
    bool pad = !s.empty() && (s.front() == '`' || s.back() == '`' || s.front() == ' ');
    return fence + (pad ? " " : "") + std::string(s) + (pad ? " " : "") + fence;
}

std::string paragraphMarkdown(const RichModel& m, int start, int end) {
    // Runs as Markdown sees them, adjacent equal ones made one.
    std::vector<std::pair<MdStyle, std::string>> pieces;
    for (const RichRun& r : m.runs) {
        int s = std::max(r.start, start), e = std::min(r.end, end);
        if (s >= e) continue;
        MdStyle st = mdStyleOf(r);
        std::string text = m.text.substr(size_t(s), size_t(e - s));
        if (!pieces.empty() && pieces.back().first == st) pieces.back().second += text;
        else pieces.push_back({st, text});
    }
    std::string out;
    auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || (static_cast<unsigned char>(c) & 0x80); };
    auto punct = [](char c) { return std::ispunct(static_cast<unsigned char>(c)) != 0; };
    for (size_t k = 0; k < pieces.size(); ++k) {
        const auto& [st, raw] = pieces[k];
        // Spaces stay outside the markers (** a ** isn't bold).
        size_t a = raw.find_first_not_of(' ');
        if (a == std::string::npos) {
            out += raw;
            continue;
        }
        size_t z = raw.find_last_not_of(' ');
        std::string lead = raw.substr(0, a), trail = raw.substr(z + 1);
        std::string_view core = std::string_view(raw).substr(a, z - a + 1);
        std::string body = st.code ? codeSpan(core) : escaped(core, out.empty() && lead.empty());
        // Inside a word, next to punctuation, ** and * don't open or close (CommonMark's flanking rule):
        // the HTML tags instead, which Markdown takes in.
        char before = lead.empty() ? (out.empty() ? ' ' : out.back()) : ' ';
        char after = !trail.empty() ? ' ' : k + 1 < pieces.size() && !pieces[k + 1].second.empty() ? pieces[k + 1].second.front() : ' ';
        bool html = (word(before) && punct(core.front())) || (word(after) && punct(core.back()));
        if (st.strike) body = html ? "<s>" + body + "</s>" : "~~" + body + "~~";
        if (st.italic) body = html ? "<em>" + body + "</em>" : "*" + body + "*";
        if (st.bold) body = html ? "<strong>" + body + "</strong>" : "**" + body + "**";
        if (st.underline) body = "<u>" + body + "</u>";
        if (!st.link.empty()) body = "[" + body + "](" + st.link + ")";
        out += lead + body + trail;
    }
    return out;
}

} // namespace

RichModel modelOfMarkdown(std::string_view markdown) {
    ModelBuilder b;
    blocks(markdown::parse(markdown), b, 0);
    b.m.normalize();
    return b.m;
}

std::string markdownOfModel(const RichModel& m) {
    std::string out;
    std::vector<int> starts = m.paragraphStarts();
    std::vector<int> counters(9, 0);
    bool previousList = false;
    ListKind topKind = ListKind::None;  // the outermost list the items are in
    for (size_t i = 0; i < m.paragraphs.size() && i < starts.size(); ++i) {
        const RichParagraph& p = m.paragraphs[i];
        int start = starts[i];
        int end = i + 1 < starts.size() ? starts[i + 1] - 1 : int(m.text.size());
        bool list = p.list != ListKind::None;
        // (items of one list a line apart; a list after another kind, or after text, a blank line apart)
        if (i > 0) out += list && previousList && (p.level > 1 || topKind == p.list) ? "\n" : "\n\n";
        if (list && p.level <= 1) topKind = p.list;
        std::string body = paragraphMarkdown(m, start, end);
        if (list) {
            int level = std::clamp(p.level, 1, 8);
            for (size_t d = size_t(level) + 1; d < counters.size(); ++d) counters[d] = 0;
            out += std::string(size_t(4 * (level - 1)), ' ');
            if (p.list == ListKind::Ordered) out += std::to_string(++counters[size_t(level)]) + ". ";
            else counters[size_t(level)] = 0, out += "- ";
            out += body;
        } else {
            std::fill(counters.begin(), counters.end(), 0);
            if (p.heading != HeadingStyle::Normal) out += std::string(size_t(int(p.heading)), '#') + " " + body;
            else out += body.empty() ? "<br>" : body;
        }
        previousList = list;
    }
    return out;
}

RichModel modelOfAnnotatedString(const AnnotatedString& text) {
    RichModel m;
    m.text = text.text();
    if (!m.text.empty()) m.runs.push_back({0, int(m.text.size())});
    m.normalize();
    for (const auto& span : text.spans())
        m.styleRange(span.start, span.end, [&](RichRun& r) { spanAdd(r.style, span.style); });
    for (const auto& link : text.links())
        if (link.annotation && !link.annotation->url.empty())
            m.styleRange(link.start, link.end, [&](RichRun& r) { r.link = link.annotation->url; });
    for (const auto& ps : text.paragraphStyles())
        for (int i = m.paragraphAt(ps.start); i <= m.paragraphAt(std::max(ps.start, ps.end - 1)) && i < int(m.paragraphs.size()); ++i)
            paragraphAdd(m.paragraphs[size_t(i)].style, ps.style);
    return m;
}

} // namespace kompoz::detail
