// A rich text's HTML, in and out: the tags and inline styles rich text editors write.

#include "richtext_model.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace kompoz::detail {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
std::string_view trim(std::string_view s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::string utf8(uint32_t cp) {
    std::string out;
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) out += char(0xC0 | (cp >> 6)), out += char(0x80 | (cp & 0x3F));
    else if (cp < 0x10000) out += char(0xE0 | (cp >> 12)), out += char(0x80 | ((cp >> 6) & 0x3F)), out += char(0x80 | (cp & 0x3F));
    else
        out += char(0xF0 | (cp >> 18)), out += char(0x80 | ((cp >> 12) & 0x3F)), out += char(0x80 | ((cp >> 6) & 0x3F)),
            out += char(0x80 | (cp & 0x3F));
    return out;
}

// &amp;, &#252;, &#x1F600; and the named ones written often.
std::string decodeEntities(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        size_t semi = s.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10) {
            out += '&';
            continue;
        }
        std::string_view name = s.substr(i + 1, semi - i - 1);
        std::optional<uint32_t> cp;
        if (!name.empty() && name[0] == '#') {
            bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
            try {
                cp = uint32_t(std::stoul(std::string(name.substr(hex ? 2 : 1)), nullptr, hex ? 16 : 10));
            } catch (...) {
            }
        } else {
            static const std::pair<const char*, uint32_t> named[] = {
                {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0}, {"copy", 0xA9}, {"reg", 0xAE},
                {"hellip", 0x2026}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"euro", 0x20AC},
                {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bull", 0x2022}, {"middot", 0xB7}};
            for (auto [n, c] : named)
                if (name == n) cp = c;
        }
        if (!cp) {
            out += '&';
            continue;
        }
        out += utf8(*cp == 0xA0 ? ' ' : *cp);
        i = semi;
    }
    return out;
}

std::string escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

// CSS colors: #rgb, #rrggbb, #rrggbbaa, rgb(…), rgba(…) and the named ones written often.
std::optional<Color> cssColor(std::string_view v) {
    std::string s = lower(trim(v));
    if (s.starts_with('#')) {
        std::string h = s.substr(1);
        if (h.size() == 3) h = {h[0], h[0], h[1], h[1], h[2], h[2]};
        if (h.size() != 6 && h.size() != 8) return std::nullopt;
        try {
            uint32_t x = uint32_t(std::stoul(h, nullptr, 16));
            return h.size() == 6 ? Color::argb(0xFF000000 | x) : Color::argb((x >> 8) | (x << 24));
        } catch (...) {
            return std::nullopt;
        }
    }
    if (s.starts_with("rgb")) {
        size_t open = s.find('('), close = s.find(')');
        if (open == std::string::npos || close == std::string::npos) return std::nullopt;
        float v4[4] = {0, 0, 0, 1};
        int n = 0;
        std::string inner = s.substr(open + 1, close - open - 1);
        for (char& c : inner)
            if (c == ',' || c == '/') c = ' ';
        const char* p = inner.c_str();
        char* end = nullptr;
        while (n < 4 && *p) {
            float f = std::strtof(p, &end);
            if (end == p) break;
            v4[n++] = f;
            p = end;
            while (*p == ' ' || *p == '%') ++p;
        }
        if (n < 3) return std::nullopt;
        return Color(v4[0] / 255.f, v4[1] / 255.f, v4[2] / 255.f, v4[3] > 1 ? v4[3] / 100.f : v4[3]);
    }
    static const std::pair<const char*, uint32_t> named[] = {
        {"black", 0xFF000000}, {"white", 0xFFFFFFFF}, {"red", 0xFFFF0000}, {"green", 0xFF008000}, {"blue", 0xFF0000FF},
        {"yellow", 0xFFFFFF00}, {"orange", 0xFFFFA500}, {"purple", 0xFF800080}, {"gray", 0xFF808080}, {"grey", 0xFF808080},
        {"pink", 0xFFFFC0CB}, {"brown", 0xFFA52A2A}, {"cyan", 0xFF00FFFF}, {"magenta", 0xFFFF00FF}, {"navy", 0xFF000080},
        {"teal", 0xFF008080}, {"lime", 0xFF00FF00}, {"maroon", 0xFF800000}, {"olive", 0xFF808000}, {"silver", 0xFFC0C0C0}};
    for (auto [n, c] : named)
        if (s == n) return Color::argb(c);
    return std::nullopt;
}

// A CSS length in sp: px and pt (as dp), em (of 16).
std::optional<float> cssLength(std::string_view v) {
    std::string s = lower(trim(v));
    char* end = nullptr;
    float f = std::strtof(s.c_str(), &end);
    if (end == s.c_str()) return std::nullopt;
    std::string unit = trim(std::string_view(end)).data();
    if (unit == "pt") return f * 4 / 3;
    if (unit == "em" || unit == "rem") return f * 16;
    return f;
}

std::string hexOf(Color c) {
    char buf[16];
    auto byte = [](float v) { return int(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
    if (c.alpha() < 1) std::snprintf(buf, sizeof buf, "#%02x%02x%02x%02x", byte(c.red()), byte(c.green()), byte(c.blue()), byte(c.alpha()));
    else std::snprintf(buf, sizeof buf, "#%02x%02x%02x", byte(c.red()), byte(c.green()), byte(c.blue()));
    return buf;
}

std::string number(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

struct Tag {
    std::string name;
    bool closing = false, selfClosing = false;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string attribute(std::string_view key) const {
        for (const auto& [k, v] : attributes)
            if (k == key) return v;
        return {};
    }
};

Tag parseTag(std::string_view s) {  // s: between < and >
    Tag t;
    size_t i = 0;
    if (i < s.size() && s[i] == '/') t.closing = true, ++i;
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '-')) t.name += char(std::tolower(static_cast<unsigned char>(s[i++])));
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i < s.size() && s[i] == '/') {
            t.selfClosing = true;
            ++i;
            continue;
        }
        std::string key;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '=' && s[i] != '/') key += char(std::tolower(static_cast<unsigned char>(s[i++])));
        if (key.empty()) {
            ++i;
            continue;
        }
        std::string value;
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i < s.size() && s[i] == '=') {
            ++i;
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
                char q = s[i++];
                size_t end = s.find(q, i);
                if (end == std::string_view::npos) end = s.size();
                value = s.substr(i, end - i);
                i = end + 1;
            } else {
                while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) value += s[i++];
            }
        }
        t.attributes.push_back({key, decodeEntities(value)});
    }
    return t;
}

// What a style attribute says of a run, and of a paragraph.
void applyStyle(std::string_view css, RichRun* run, ParagraphStyle* paragraph) {
    size_t start = 0;
    while (start < css.size()) {
        size_t end = css.find(';', start);
        if (end == std::string_view::npos) end = css.size();
        std::string_view decl = css.substr(start, end - start);
        start = end + 1;
        size_t colon = decl.find(':');
        if (colon == std::string_view::npos) continue;
        std::string key = lower(trim(decl.substr(0, colon)));
        std::string value = lower(trim(decl.substr(colon + 1)));
        if (run) {
            SpanStyle& s = run->style;
            if (key == "color") s.color = cssColor(value);
            else if (key == "background-color" || key == "background") s.background = cssColor(value);
            else if (key == "font-weight") {
                if (value == "bold" || value == "bolder") s.fontWeight = FontWeight::Bold;
                else if (value == "normal") s.fontWeight = FontWeight::Normal;
                else if (std::isdigit(static_cast<unsigned char>(value[0]))) s.fontWeight = FontWeight{std::atoi(value.c_str())};
            } else if (key == "font-style") s.fontStyle = value == "italic" || value == "oblique" ? FontStyle::Italic : FontStyle::Normal;
            else if (key == "text-decoration" || key == "text-decoration-line") {
                TextDecoration d = TextDecoration::None;
                if (value.find("underline") != std::string::npos) d = d | TextDecoration::Underline;
                if (value.find("line-through") != std::string::npos) d = d | TextDecoration::LineThrough;
                if (d != TextDecoration::None) spanAdd(s, {.textDecoration = d});
            } else if (key == "font-size") {
                if (auto v = cssLength(value)) s.fontSize = TextUnit::sp(*v);
            } else if (key == "letter-spacing") {
                if (auto v = cssLength(value)) s.letterSpacing = TextUnit::sp(*v);
            } else if (key == "font-family" && value.find("mono") != std::string::npos) s.fontFamily = FontFamily::Monospace;
        }
        if (paragraph && key == "text-align") {
            if (value == "center") paragraph->textAlign = TextAlign::Center;
            else if (value == "right") paragraph->textAlign = TextAlign::Right;
            else if (value == "left") paragraph->textAlign = TextAlign::Left;
            else if (value == "justify") paragraph->textAlign = TextAlign::Justify;
            else if (value == "end") paragraph->textAlign = TextAlign::End;
            else if (value == "start") paragraph->textAlign = TextAlign::Start;
        }
    }
}

bool isBlock(std::string_view name) {
    static const char* blocks[] = {"p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "li", "blockquote", "pre", "tr", "section", "article",
                                   "header", "footer", "dt", "dd", "figcaption"};
    return std::find(std::begin(blocks), std::end(blocks), name) != std::end(blocks);
}

class HtmlReader {
public:
    RichModel read(std::string_view html) {
        styles_.push_back(RichRun{});
        size_t i = 0;
        while (i < html.size()) {
            if (html[i] == '<') {
                if (html.substr(i, 4) == "<!--") {
                    size_t end = html.find("-->", i + 4);
                    i = end == std::string_view::npos ? html.size() : end + 3;
                    continue;
                }
                size_t end = html.find('>', i);
                if (end == std::string_view::npos) break;
                std::string_view inside = html.substr(i + 1, end - i - 1);
                i = end + 1;
                if (inside.empty() || inside[0] == '!' || inside[0] == '?') continue;
                Tag t = parseTag(inside);
                if (t.name == "style" || t.name == "script" || t.name == "head" || t.name == "title") {
                    if (!t.closing && !t.selfClosing) {  // (their contents aren't text)
                        size_t close = lower(html.substr(i)).find("</" + t.name);
                        i = close == std::string::npos ? html.size() : i + close;
                    }
                    continue;
                }
                tag(t);
                continue;
            }
            size_t next = html.find('<', i);
            if (next == std::string_view::npos) next = html.size();
            text(decodeEntities(html.substr(i, next - i)));
            i = next;
        }
        for (int k = 1; k < breaks_; ++k) openParagraph(context());
        m_.normalize();
        return m_;
    }

private:
    void openParagraph(const RichParagraph& p) {
        if (!used_) {  // (the model's first, empty paragraph)
            used_ = true;
            m_.paragraphs.back() = p;
        } else {
            m_.append("\n", RichRun{});
            m_.paragraphs.back() = p;
        }
        empty_ = true;
        openedByBlock_ = false;
        pendingBreak_ = false;
        space_ = false;
    }

    RichParagraph context() const {
        RichParagraph p;
        if (!lists_.empty() && inItem_) p.list = lists_.back(), p.level = int(lists_.size());
        p.style = alignStack_.empty() ? ParagraphStyle{} : alignStack_.back();
        return p;
    }

    void text(const std::string& raw) {
        std::string s;
        if (pre_ > 0) {
            s = raw;
        } else {  // white space collapsed, as HTML shows it
            for (char c : raw) {
                bool ws = c == ' ' || c == '\n' || c == '\t' || c == '\r';
                if (ws) {
                    if (!space_ && !(empty_ && !pendingBreak_ && !s.size()) ) s += ' ';
                    space_ = true;
                } else {
                    s += c, space_ = false;
                }
            }
            if (empty_ && !s.empty() && s[0] == ' ') s.erase(0, 1);
        }
        if (s.empty()) return;
        if (!used_ || pendingBreak_) openParagraph(context());
        for (; breaks_ > 0; --breaks_) openParagraph(context());
        if (pre_ > 0) {
            // A <pre>'s lines: paragraphs of their own, as code.
            size_t start = 0;
            while (true) {
                size_t nl = s.find('\n', start);
                std::string_view line = std::string_view(s).substr(start, nl == std::string::npos ? std::string::npos : nl - start);
                if (!line.empty()) m_.append(line, styles_.back());
                if (nl == std::string::npos) break;
                openParagraph(context());
                start = nl + 1;
            }
        } else {
            m_.append(s, styles_.back());
        }
        empty_ = false;
    }

    void tag(const Tag& t) {
        const std::string& n = t.name;
        if (n == "br") {
            // (the one in an empty block, <p><br></p>, holds its line open: no line more)
            if (used_ && empty_ && openedByBlock_ && !pendingBreak_) {
                openedByBlock_ = false;
                return;
            }
            if (!used_) openParagraph(context());
            ++breaks_;  // (made as text follows: one at a block's end shows no line)
            return;
        }
        if (n == "hr" || n == "ul" || n == "ol" || isBlock(n)) {  // (the line breaks before a block: but the last)
            for (int k = 1; k < breaks_; ++k) openParagraph(context());
            breaks_ = 0;
        }
        if (n == "hr") {
            openParagraph({});
            m_.append("---", RichRun{});
            pendingBreak_ = true;
            return;
        }
        if (n == "ul" || n == "ol") {
            if (t.closing) {
                if (!lists_.empty()) lists_.pop_back();
                pendingBreak_ = true;
            } else {
                lists_.push_back(n == "ol" ? ListKind::Ordered : ListKind::Unordered);
                pendingBreak_ = true;
            }
            return;
        }
        if (isBlock(n)) {
            if (t.closing) {
                if (n == "pre") --pre_;
                if (n == "li") inItem_ = std::max(0, inItem_ - 1);
                if (!alignStack_.empty() && n != "li") alignStack_.pop_back();
                if (n == "h1" || n == "h2" || n == "h3" || n == "h4" || n == "h5" || n == "h6") heading_ = HeadingStyle::Normal;
                pendingBreak_ = true;
                if (n == "td" || n == "th") return;
                return;
            }
            ParagraphStyle style = alignStack_.empty() ? ParagraphStyle{} : alignStack_.back();
            applyStyle(t.attribute("style"), nullptr, &style);
            if (t.attribute("align") == "center") style.textAlign = TextAlign::Center;
            if (n != "li") alignStack_.push_back(style);
            if (n == "li") ++inItem_;
            if (n == "pre") ++pre_;
            RichParagraph p = context();
            p.style = style;
            if (n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6') heading_ = HeadingStyle(n[1] - '0');
            p.heading = heading_;
            openParagraph(p);
            openedByBlock_ = true;
            return;
        }
        if (n == "td" || n == "th") {
            if (!t.closing && !empty_) m_.append(" | ", RichRun{});
            return;
        }
        // Inline: a style of what is inside.
        static const char* inlineTags[] = {"b", "strong", "i", "em", "cite", "dfn", "u", "ins", "s", "strike", "del", "code", "kbd", "samp",
                                           "tt", "a", "span", "font", "mark", "sup", "sub", "small", "big"};
        if (std::find(std::begin(inlineTags), std::end(inlineTags), n) == std::end(inlineTags)) return;
        if (t.closing) {
            if (styles_.size() > 1) styles_.pop_back();
            return;
        }
        if (t.selfClosing) return;
        RichRun r = styles_.back();
        if (n == "b" || n == "strong") r.style.fontWeight = FontWeight::Bold;
        else if (n == "i" || n == "em" || n == "cite" || n == "dfn") r.style.fontStyle = FontStyle::Italic;
        else if (n == "u" || n == "ins") spanAdd(r.style, {.textDecoration = TextDecoration::Underline});
        else if (n == "s" || n == "strike" || n == "del") spanAdd(r.style, {.textDecoration = TextDecoration::LineThrough});
        else if (n == "code" || n == "kbd" || n == "samp" || n == "tt") r.code = true;
        else if (n == "a") r.link = t.attribute("href");
        else if (n == "mark") r.style.background = Color::argb(0xFFFFEB3B);
        else if (n == "sup") r.style.baselineShift = BaselineShift::Superscript;
        else if (n == "sub") r.style.baselineShift = BaselineShift::Subscript;
        else if (n == "small") r.style.fontSize = TextUnit::em(0.8f);
        else if (n == "big") r.style.fontSize = TextUnit::em(1.25f);
        else if (n == "font") {
            if (auto c = cssColor(t.attribute("color"))) r.style.color = c;
        }
        applyStyle(t.attribute("style"), &r, nullptr);
        styles_.push_back(r);
    }

    RichModel m_;
    std::vector<RichRun> styles_;
    std::vector<ListKind> lists_;
    std::vector<ParagraphStyle> alignStack_;
    HeadingStyle heading_ = HeadingStyle::Normal;
    int inItem_ = 0, pre_ = 0, breaks_ = 0;
    bool used_ = false, empty_ = true, pendingBreak_ = false, space_ = false, openedByBlock_ = false;
};

// A run's text with its tags around it.
std::string runHtml(const RichRun& r, std::string_view text) {
    std::string body = escape(text);
    const SpanStyle& s = r.style;
    std::string css;
    if (s.color) css += "color: " + hexOf(*s.color) + "; ";
    if (s.background) css += "background-color: " + hexOf(*s.background) + "; ";
    if (s.fontSize && s.fontSize->type == TextUnit::Type::Sp) css += "font-size: " + number(s.fontSize->value) + "px; ";
    if (s.fontSize && s.fontSize->type == TextUnit::Type::Em) css += "font-size: " + number(s.fontSize->value) + "em; ";
    if (s.letterSpacing) css += "letter-spacing: " + number(s.letterSpacing->value) + "px; ";
    if (s.fontWeight && s.fontWeight->weight != 400 && s.fontWeight->weight != 700) css += "font-weight: " + std::to_string(s.fontWeight->weight) + "; ";
    if (s.fontFamily == FontFamily::Monospace) css += "font-family: monospace; ";
    if (!css.empty()) {
        std::string_view decls = trim(css);
        if (decls.ends_with(';')) decls.remove_suffix(1);
        body = "<span style=\"" + std::string(decls) + "\">" + body + "</span>";
    }
    TextDecoration d = s.textDecoration.value_or(TextDecoration::None);
    if (s.baselineShift == BaselineShift::Superscript) body = "<sup>" + body + "</sup>";
    if (s.baselineShift == BaselineShift::Subscript) body = "<sub>" + body + "</sub>";
    if (r.code) body = "<code>" + body + "</code>";
    if (contains(d, TextDecoration::LineThrough)) body = "<s>" + body + "</s>";
    if (contains(d, TextDecoration::Underline)) body = "<u>" + body + "</u>";
    if (s.fontStyle == FontStyle::Italic) body = "<i>" + body + "</i>";
    if (s.fontWeight && s.fontWeight->weight == 700) body = "<b>" + body + "</b>";
    if (!r.link.empty()) body = "<a href=\"" + escape(r.link) + "\">" + body + "</a>";
    return body;
}

std::string paragraphStyleCss(const ParagraphStyle& p) {
    if (!p.textAlign) return {};
    switch (*p.textAlign) {
    case TextAlign::Center: return " style=\"text-align: center\"";
    case TextAlign::Right: return " style=\"text-align: right\"";
    case TextAlign::Left: return " style=\"text-align: left\"";
    case TextAlign::Justify: return " style=\"text-align: justify\"";
    case TextAlign::End: return " style=\"text-align: end\"";
    default: return {};
    }
}

} // namespace

RichModel modelOfHtml(std::string_view html) { return HtmlReader().read(html); }

std::string htmlOfModel(const RichModel& m) {
    std::string out;
    std::vector<int> starts = m.paragraphStarts();
    std::vector<ListKind> open;  // the lists open, outermost first
    auto closeTo = [&](size_t depth) {
        while (open.size() > depth) {
            out += open.back() == ListKind::Ordered ? "</ol>" : "</ul>";
            open.pop_back();
        }
    };
    for (size_t i = 0; i < m.paragraphs.size() && i < starts.size(); ++i) {
        const RichParagraph& p = m.paragraphs[i];
        int start = starts[i];
        int end = i + 1 < starts.size() ? starts[i + 1] - 1 : int(m.text.size());
        std::string body;
        for (const RichRun& r : m.runs) {
            int s = std::max(r.start, start), e = std::min(r.end, end);
            if (s < e) body += runHtml(r, std::string_view(m.text).substr(size_t(s), size_t(e - s)));
        }
        std::string css = paragraphStyleCss(p.style);
        if (p.list != ListKind::None) {
            size_t level = size_t(std::clamp(p.level, 1, 8));
            if (open.size() >= level && open[level - 1] != p.list) closeTo(level - 1);
            closeTo(level);
            while (open.size() < level) {
                out += p.list == ListKind::Ordered ? "<ol>" : "<ul>";
                open.push_back(p.list);
            }
            out += "<li" + css + ">" + body + "</li>";
            continue;
        }
        closeTo(0);
        if (p.heading != HeadingStyle::Normal) {
            std::string h = "h" + std::to_string(int(p.heading));
            out += "<" + h + css + ">" + body + "</" + h + ">";
        } else {
            out += "<p" + css + ">" + (body.empty() ? "<br>" : body) + "</p>";
        }
    }
    closeTo(0);
    return out;
}

} // namespace kompoz::detail
