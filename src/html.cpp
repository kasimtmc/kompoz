// AnnotatedString::fromHtml: the tags Android's Html.fromHtml knows (compact mode), as Compose's
// fromHtml turns them into styles, paragraphs and links.

#include <kompoz/text.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace kompoz {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

// "#rgb", "#rrggbb" or a color's name (the ones Android's Html knows).
std::optional<Color> parseColor(std::string_view value) {
    std::string v = lower(trim(value));
    if (!v.empty() && v[0] == '#') {
        std::string hex = v.substr(1);
        if (hex.size() == 3) hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
        if (hex.size() != 6 || !std::all_of(hex.begin(), hex.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
            return std::nullopt;
        return Color::hex(uint32_t(std::strtoul(hex.c_str(), nullptr, 16)));
    }
    static const std::pair<const char*, uint32_t> named[] = {
        {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000},   {"green", 0x008000}, {"blue", 0x0000FF},  {"yellow", 0xFFFF00},
        {"gray", 0x808080},  {"grey", 0x808080},  {"silver", 0xC0C0C0}, {"maroon", 0x800000}, {"purple", 0x800080}, {"fuchsia", 0xFF00FF},
        {"lime", 0x00FF00},  {"olive", 0x808000}, {"navy", 0x000080},  {"teal", 0x008080},  {"aqua", 0x00FFFF},  {"cyan", 0x00FFFF},
        {"magenta", 0xFF00FF}};
    for (auto [name, rgb] : named)
        if (v == name) return Color::hex(rgb);
    return std::nullopt;
}

// A tag's attribute (name="value", name='value' or name=value).
std::string attribute(std::string_view tag, std::string_view name) {
    std::string low = lower(tag);
    for (size_t at = 0; (at = low.find(name, at)) != std::string::npos; at += name.size()) {
        if (at > 0 && !std::isspace(static_cast<unsigned char>(low[at - 1]))) continue;
        size_t i = at + name.size();
        while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
        if (i >= tag.size() || tag[i] != '=') continue;
        ++i;
        while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
        if (i < tag.size() && (tag[i] == '"' || tag[i] == '\'')) {
            char quote = tag[i++];
            size_t end = tag.find(quote, i);
            return std::string(tag.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i));
        }
        size_t end = i;
        while (end < tag.size() && !std::isspace(static_cast<unsigned char>(tag[end])) && tag[end] != '>' && tag[end] != '/') ++end;
        return std::string(tag.substr(i, end - i));
    }
    return {};
}

// A style="…" attribute's color, background-color, font-weight, font-style and text-decoration.
SpanStyle cssStyle(std::string_view css) {
    SpanStyle style;
    size_t start = 0;
    while (start < css.size()) {
        size_t end = css.find(';', start);
        std::string_view declaration = css.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        start = end == std::string_view::npos ? css.size() : end + 1;
        size_t colon = declaration.find(':');
        if (colon == std::string_view::npos) continue;
        std::string property = lower(trim(declaration.substr(0, colon)));
        std::string value = lower(trim(declaration.substr(colon + 1)));
        if (property == "color") style.color = parseColor(value);
        else if (property == "background-color" || property == "background") style.background = parseColor(value);
        else if (property == "font-weight" && (value == "bold" || value == "bolder" || std::atoi(value.c_str()) >= 600)) style.fontWeight = FontWeight::Bold;
        else if (property == "font-style" && value == "italic") style.fontStyle = FontStyle::Italic;
        else if (property == "text-decoration" && value.find("line-through") != std::string::npos) style.textDecoration = TextDecoration::LineThrough;
        else if (property == "text-decoration" && value.find("underline") != std::string::npos) style.textDecoration = TextDecoration::Underline;
    }
    return style;
}

// "&amp;", "&#252;", "&#xE7;"…: the character, and how much of `s` (from the '&') it took; 0: not an entity.
size_t entity(std::string_view s, std::string& out) {
    size_t semi = s.find(';');
    if (semi == std::string_view::npos || semi > 10) return 0;
    std::string_view name = s.substr(1, semi - 1);
    if (!name.empty() && name[0] == '#') {
        bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
        std::string digits(name.substr(hex ? 2 : 1));
        if (digits.empty()) return 0;
        char* end = nullptr;
        unsigned long c = std::strtoul(digits.c_str(), &end, hex ? 16 : 10);
        if (*end || c == 0 || c > 0x10FFFF) return 0;
        utf8::append(out, char32_t(c));
        return semi + 1;
    }
    static const std::pair<const char*, uint32_t> named[] = {
        {"amp", '&'},      {"lt", '<'},       {"gt", '>'},        {"quot", '"'},     {"apos", '\''},    {"nbsp", 0xA0},
        {"copy", 0xA9},    {"reg", 0xAE},     {"hellip", 0x2026}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"laquo", 0xAB},
        {"raquo", 0xBB},   {"euro", 0x20AC},  {"trade", 0x2122},  {"deg", 0xB0},     {"middot", 0xB7},  {"bull", 0x2022},
        {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C},  {"rdquo", 0x201D}};
    for (auto [n, c] : named)
        if (name == n) {
            utf8::append(out, c);
            return semi + 1;
        }
    return 0;
}

bool isBlock(const std::string& tag) {
    static const char* blocks[] = {"p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "li", "ul", "ol", "blockquote", "pre"};
    return std::any_of(std::begin(blocks), std::end(blocks), [&](const char* b) { return tag == b; });
}

} // namespace

AnnotatedString AnnotatedString::fromHtml(std::string_view html, std::optional<TextLinkStyles> linkStyles,
                                          LinkAnnotation::Listener linkInteractionListener) {
    AnnotatedString out;
    std::string& text = out.text_;
    struct Open {
        std::string tag;
        int start;
        std::optional<SpanStyle> style;
        std::string href;
    };
    std::vector<Open> open;
    int preformatted = 0;  // inside <pre>: its spaces and lines as they are
    // A paragraph's edge: on a line of its own (one line break, as Html's compact mode).
    auto newLine = [&] {
        while (!text.empty() && text.back() == ' ') text.pop_back();
        if (!text.empty() && text.back() != '\n') text += '\n';
    };
    auto close = [&](const Open& o) {
        int end = int(text.size());
        if (o.style && end > o.start) out.spans_.push_back({o.start, end, *o.style});
        if (o.tag == "a" && !o.href.empty() && end > o.start) {
            out.links_.push_back({o.start, end, {}, LinkAnnotation::Url(o.href, linkStyles, linkInteractionListener)});
            out.spans_.push_back({o.start, end, {.textDecoration = TextDecoration::Underline}});
            if (linkStyles && linkStyles->style) out.spans_.push_back({o.start, end, *linkStyles->style});
        }
        if (o.tag == "pre") --preformatted;
        if (isBlock(o.tag)) newLine();
    };
    for (size_t i = 0; i < html.size();) {
        char c = html[i];
        if (c == '<') {
            if (html.substr(i, 4) == "<!--") {  // a comment
                size_t end = html.find("-->", i + 4);
                i = end == std::string_view::npos ? html.size() : end + 3;
                continue;
            }
            size_t end = html.find('>', i);
            if (end == std::string_view::npos) {  // not a tag after all
                text += c;
                ++i;
                continue;
            }
            std::string_view tag = html.substr(i + 1, end - i - 1);
            i = end + 1;
            bool closing = !tag.empty() && tag[0] == '/';
            if (closing) tag.remove_prefix(1);
            size_t nameEnd = 0;
            while (nameEnd < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[nameEnd])))) ++nameEnd;
            std::string name = lower(tag.substr(0, nameEnd));
            if (name.empty()) continue;
            if (closing) {
                // Closes it and whatever was left open inside it (none open: ignored).
                auto match = std::find_if(open.rbegin(), open.rend(), [&](const Open& o) { return o.tag == name; });
                if (match == open.rend()) continue;
                size_t keep = size_t(std::distance(match, open.rend())) - 1;
                while (open.size() > keep) {
                    Open o = std::move(open.back());
                    open.pop_back();
                    close(o);
                }
                continue;
            }
            if (name == "br") {
                text += '\n';
                continue;
            }
            Open o{name, int(text.size()), std::nullopt, {}};
            if (isBlock(name)) {
                newLine();
                o.start = int(text.size());
            }
            static const float kHeadings[] = {1.5f, 1.4f, 1.3f, 1.2f, 1.1f, 1.0f};  // Html's sizes
            if (name == "b" || name == "strong") o.style = SpanStyle{.fontWeight = FontWeight::Bold};
            else if (name == "i" || name == "em" || name == "cite" || name == "dfn" || name == "var") o.style = SpanStyle{.fontStyle = FontStyle::Italic};
            else if (name == "u" || name == "ins") o.style = SpanStyle{.textDecoration = TextDecoration::Underline};
            else if (name == "s" || name == "strike" || name == "del") o.style = SpanStyle{.textDecoration = TextDecoration::LineThrough};
            else if (name == "sup") o.style = SpanStyle{.baselineShift = BaselineShift::Superscript};
            else if (name == "sub") o.style = SpanStyle{.baselineShift = BaselineShift::Subscript};
            else if (name == "big") o.style = SpanStyle{.fontSize = TextUnit::em(1.25f)};
            else if (name == "small") o.style = SpanStyle{.fontSize = TextUnit::em(0.8f)};
            else if (name == "tt" || name == "code" || name == "kbd" || name == "samp" || name == "pre") o.style = SpanStyle{.fontFamily = FontFamily::Monospace};
            else if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6')
                o.style = SpanStyle{.fontSize = TextUnit::em(kHeadings[name[1] - '1']), .fontWeight = FontWeight::Bold};
            else if (name == "font") {
                if (auto color = parseColor(attribute(tag, "color"))) o.style = SpanStyle{.color = color};
            } else if (name == "span") {
                std::string css = attribute(tag, "style");
                if (!css.empty()) o.style = cssStyle(css);
            } else if (name == "a") {
                o.href = attribute(tag, "href");
            }
            if (name == "li") {  // a bullet, as Html's compact mode
                text += "• ";
            }
            if (name == "pre") ++preformatted;
            bool selfClosing = !tag.empty() && tag.back() == '/';
            if (selfClosing) {
                close(o);
                continue;
            }
            open.push_back(std::move(o));
            continue;
        }
        if (c == '&') {
            std::string decoded;
            if (size_t used = entity(html.substr(i), decoded)) {
                text += decoded;
                i += used;
                continue;
            }
        }
        if (std::isspace(static_cast<unsigned char>(c)) && !preformatted) {
            // A run of white space is one space, none at a line's start.
            if (!text.empty() && text.back() != ' ' && text.back() != '\n') text += ' ';
            ++i;
            continue;
        }
        text += c;
        ++i;
    }
    while (!open.empty()) {  // still open at the end: closed there
        Open o = std::move(open.back());
        open.pop_back();
        close(o);
    }
    // No line breaks or spaces left at the end.
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
    int length = int(text.size());
    for (auto& s : out.spans_) s.end = std::min(s.end, length);
    for (auto& l : out.links_) l.end = std::min(l.end, length);
    std::erase_if(out.spans_, [](const Span& s) { return s.start >= s.end; });
    return out;
}

} // namespace kompoz
