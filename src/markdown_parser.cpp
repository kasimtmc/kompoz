#include <kompoz/markdown_parser.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>

namespace kompoz::markdown {

namespace {

// ---- lines ------------------------------------------------------------------------------------

std::vector<std::string> splitLines(std::string_view s) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find('\n', start);
        if (end == std::string_view::npos) end = s.size();
        std::string line(s.substr(start, end - start));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Tabs to the next stop of four (as CommonMark counts columns).
        std::string expanded;
        for (char c : line) {
            if (c == '\t') expanded.append(4 - expanded.size() % 4, ' ');
            else expanded += c;
        }
        lines.push_back(std::move(expanded));
        if (end == s.size()) break;
        start = end + 1;
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

int indentOf(std::string_view line) {
    int n = 0;
    while (n < int(line.size()) && line[size_t(n)] == ' ') ++n;
    return n;
}
bool blank(std::string_view line) { return line.find_first_not_of(' ') == std::string_view::npos; }
std::string_view trimmed(std::string_view s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string_view::npos) return {};
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}
std::string dropIndent(std::string_view line, int n) {
    int have = indentOf(line);
    return std::string(line.substr(size_t(std::min(have, n))));
}

bool thematicBreak(std::string_view line) {
    if (indentOf(line) > 3) return false;
    char c = 0;
    int count = 0;
    for (char x : line) {
        if (x == ' ') continue;
        if (x != '-' && x != '*' && x != '_') return false;
        if (c && x != c) return false;
        c = x, ++count;
    }
    return count >= 3;
}

int atxLevel(std::string_view line) {
    if (indentOf(line) > 3) return 0;
    std::string_view t = line.substr(size_t(indentOf(line)));
    int n = 0;
    while (n < int(t.size()) && t[size_t(n)] == '#') ++n;
    if (n < 1 || n > 6) return 0;
    if (n < int(t.size()) && t[size_t(n)] != ' ') return 0;
    return n;
}

struct Fence {
    char c;
    int length, indent;
    std::string info;
};
std::optional<Fence> fenceOpen(std::string_view line) {
    int indent = indentOf(line);
    if (indent > 3) return std::nullopt;
    std::string_view t = line.substr(size_t(indent));
    if (t.empty() || (t[0] != '`' && t[0] != '~')) return std::nullopt;
    char c = t[0];
    int n = 0;
    while (n < int(t.size()) && t[size_t(n)] == c) ++n;
    if (n < 3) return std::nullopt;
    std::string info(trimmed(t.substr(size_t(n))));
    if (c == '`' && info.find('`') != std::string::npos) return std::nullopt;
    return Fence{c, n, indent, info};
}
bool fenceCloses(std::string_view line, const Fence& f) {
    if (indentOf(line) > 3) return false;
    std::string_view t = trimmed(line);
    if (int(t.size()) < f.length) return false;
    return std::all_of(t.begin(), t.end(), [&](char x) { return x == f.c; });
}

struct ListMarker {
    bool ordered;
    char c;         // '-', '*', '+', or '.' / ')'
    int number;
    int contentIndent;  // the column its content starts at
    bool empty;     // nothing after the marker
};
std::optional<ListMarker> listMarker(std::string_view line) {
    int indent = indentOf(line);
    if (indent > 3) return std::nullopt;
    std::string_view t = line.substr(size_t(indent));
    if (t.empty()) return std::nullopt;
    ListMarker m{};
    int width;
    if (t[0] == '-' || t[0] == '*' || t[0] == '+') {
        m.ordered = false, m.c = t[0], width = 1;
    } else {
        int d = 0;
        while (d < int(t.size()) && d < 10 && std::isdigit(static_cast<unsigned char>(t[size_t(d)]))) ++d;
        if (d == 0 || d > 9 || d >= int(t.size()) || (t[size_t(d)] != '.' && t[size_t(d)] != ')')) return std::nullopt;
        m.ordered = true, m.c = t[size_t(d)], m.number = std::stoi(std::string(t.substr(0, size_t(d)))), width = d + 1;
    }
    std::string_view rest = t.substr(size_t(width));
    if (!rest.empty() && rest[0] != ' ') return std::nullopt;
    m.empty = blank(rest);
    int spaces = indentOf(rest);
    if (m.empty || spaces > 4) spaces = 1;  // (an indented code block after it: one space is the marker's)
    m.contentIndent = indent + width + spaces;
    return m;
}

bool tableDelimiterRow(std::string_view line, std::vector<TableAlign>* aligns) {
    std::string_view t = trimmed(line);
    if (t.empty() || t.find('-') == std::string_view::npos) return false;
    if (t.front() == '|') t.remove_prefix(1);
    if (!t.empty() && t.back() == '|') t.remove_suffix(1);
    std::vector<TableAlign> out;
    size_t start = 0;
    while (start <= t.size()) {
        size_t bar = t.find('|', start);
        std::string_view cell = trimmed(t.substr(start, bar == std::string_view::npos ? std::string_view::npos : bar - start));
        if (cell.empty()) return false;
        bool left = cell.front() == ':', right = cell.back() == ':';
        std::string_view dashes = cell.substr(left ? 1 : 0, cell.size() - (left ? 1 : 0) - (right && cell.size() > 1 ? 1 : 0));
        if (dashes.empty() || !std::all_of(dashes.begin(), dashes.end(), [](char c) { return c == '-'; })) return false;
        out.push_back(left && right ? TableAlign::Center : left ? TableAlign::Left : right ? TableAlign::Right : TableAlign::None);
        if (bar == std::string_view::npos) break;
        start = bar + 1;
    }
    if (aligns) *aligns = std::move(out);
    return true;
}

std::vector<std::string> tableCells(std::string_view line) {
    std::string_view t = trimmed(line);
    if (!t.empty() && t.front() == '|') t.remove_prefix(1);
    if (!t.empty() && t.back() == '|' && (t.size() < 2 || t[t.size() - 2] != '\\')) t.remove_suffix(1);
    std::vector<std::string> cells;
    std::string cell;
    bool code = false;
    for (size_t i = 0; i < t.size(); ++i) {
        char c = t[i];
        if (c == '\\' && i + 1 < t.size() && t[i + 1] == '|') {
            cell += '|';
            ++i;
        } else if (c == '`') {
            code = !code;
            cell += c;
        } else if (c == '|' && !code) {
            cells.emplace_back(trimmed(cell));
            cell.clear();
        } else {
            cell += c;
        }
    }
    cells.emplace_back(trimmed(cell));
    return cells;
}

// Whether a line starts a block that ends a paragraph (a list only with content, an ordered one
// only at 1: CommonMark's rule).
bool interruptsParagraph(std::string_view line) {
    if (blank(line)) return true;
    if (indentOf(line) > 3) return false;
    if (atxLevel(line) || thematicBreak(line) || fenceOpen(line)) return true;
    std::string_view t = line.substr(size_t(indentOf(line)));
    if (!t.empty() && t[0] == '>') return true;
    if (auto m = listMarker(line); m && !m->empty && (!m->ordered || m->number == 1)) return true;
    if (!t.empty() && t[0] == '<' && t.size() > 1 && (std::isalpha(static_cast<unsigned char>(t[1])) || t[1] == '/')) {
        static const char* blocks[] = {"div", "p", "table", "pre", "ul", "ol", "li", "blockquote", "h1", "h2", "h3", "h4", "h5", "h6",
                                       "hr", "section", "details", "summary", "dl", "figure"};
        std::string name;
        for (size_t i = t[1] == '/' ? 2 : 1; i < t.size() && std::isalnum(static_cast<unsigned char>(t[i])); ++i)
            name += char(std::tolower(static_cast<unsigned char>(t[i])));
        for (const char* b : blocks)
            if (name == b) return true;
    }
    return false;
}

std::vector<Block> parseLines(const std::vector<std::string>& lines);

Block paragraph(const std::string& text) {
    Block b;
    b.kind = Block::Kind::Paragraph;
    b.inlines = parseInlines(text);
    return b;
}

std::vector<Block> parseLines(const std::vector<std::string>& lines) {
    std::vector<Block> blocks;
    size_t i = 0;
    while (i < lines.size()) {
        const std::string& line = lines[i];
        if (blank(line)) {
            ++i;
            continue;
        }
        int indent = indentOf(line);
        // Indented code.
        if (indent >= 4) {
            std::vector<std::string> code;
            while (i < lines.size() && (blank(lines[i]) || indentOf(lines[i]) >= 4)) code.push_back(dropIndent(lines[i++], 4));
            while (!code.empty() && blank(code.back())) code.pop_back();
            Block b;
            b.kind = Block::Kind::CodeBlock;
            for (size_t k = 0; k < code.size(); ++k) b.literal += code[k] + (k + 1 < code.size() ? "\n" : "");
            blocks.push_back(std::move(b));
            continue;
        }
        if (auto f = fenceOpen(line)) {
            Block b;
            b.kind = Block::Kind::CodeBlock;
            b.fenced = true;
            b.info = f->info;
            ++i;
            std::vector<std::string> code;
            while (i < lines.size() && !fenceCloses(lines[i], *f)) code.push_back(dropIndent(lines[i++], f->indent));
            if (i < lines.size()) ++i;
            for (size_t k = 0; k < code.size(); ++k) b.literal += code[k] + (k + 1 < code.size() ? "\n" : "");
            blocks.push_back(std::move(b));
            continue;
        }
        if (int level = atxLevel(line)) {
            std::string_view t = trimmed(std::string_view(line).substr(size_t(indent + level)));
            // (a closing run of #s, after a space)
            size_t end = t.size();
            while (end > 0 && t[end - 1] == '#') --end;
            if (end == 0 || t[end - 1] == ' ') t = trimmed(t.substr(0, end));
            Block b;
            b.kind = Block::Kind::Heading;
            b.level = level;
            b.inlines = parseInlines(t);
            blocks.push_back(std::move(b));
            ++i;
            continue;
        }
        if (thematicBreak(line)) {
            blocks.push_back(Block{.kind = Block::Kind::ThematicBreak});
            ++i;
            continue;
        }
        std::string_view t = std::string_view(line).substr(size_t(indent));
        if (t[0] == '>') {
            std::vector<std::string> inner;
            while (i < lines.size()) {
                std::string_view l = lines[i];
                int in = indentOf(l);
                if (in <= 3 && in < int(l.size()) && l[size_t(in)] == '>') {
                    std::string_view rest = l.substr(size_t(in) + 1);
                    if (!rest.empty() && rest[0] == ' ') rest.remove_prefix(1);
                    inner.emplace_back(rest);
                    ++i;
                } else if (!blank(l) && !inner.empty() && !blank(inner.back()) && !interruptsParagraph(l)) {
                    inner.emplace_back(l);  // (a lazy continuation of its paragraph)
                    ++i;
                } else {
                    break;
                }
            }
            Block b;
            b.kind = Block::Kind::BlockQuote;
            b.children = parseLines(inner);
            blocks.push_back(std::move(b));
            continue;
        }
        if (auto m = listMarker(line)) {
            Block list;
            list.kind = Block::Kind::List;
            list.ordered = m->ordered;
            list.start = m->ordered ? m->number : 1;
            list.marker = m->c;
            bool sawBlankBetween = false;
            while (i < lines.size()) {
                auto mm = listMarker(lines[i]);
                if (!mm || mm->ordered != list.ordered || mm->c != list.marker) break;
                // The item: its first line's content, then what is indented to its content (or lazy).
                std::vector<std::string> item;
                std::string first = lines[i].size() > size_t(mm->contentIndent) ? lines[i].substr(size_t(mm->contentIndent)) : std::string();
                item.push_back(first);
                ++i;
                bool blankInside = false;
                while (i < lines.size()) {
                    const std::string& l = lines[i];
                    if (blank(l)) {
                        // (a blank line ends the item if what follows isn't indented to it)
                        size_t k = i;
                        while (k < lines.size() && blank(lines[k])) ++k;
                        if (k < lines.size() && indentOf(lines[k]) >= mm->contentIndent) {
                            for (; i < k; ++i) item.push_back("");
                            blankInside = true;
                            continue;
                        }
                        break;
                    }
                    if (indentOf(l) >= mm->contentIndent) {
                        item.push_back(dropIndent(l, mm->contentIndent));
                        ++i;
                    } else if (!blank(item.back()) && !interruptsParagraph(l) && !listMarker(l)) {
                        item.push_back(l);  // lazy
                        ++i;
                    } else {
                        break;
                    }
                }
                Block it;
                it.kind = Block::Kind::ListItem;
                // A task item: "[ ] " or "[x] " first.
                if (item[0].size() >= 3 && item[0][0] == '[' && item[0][2] == ']' && (item[0].size() == 3 || item[0][3] == ' ') &&
                    (item[0][1] == ' ' || item[0][1] == 'x' || item[0][1] == 'X')) {
                    it.checked = item[0][1] != ' ';
                    item[0] = item[0].size() > 4 ? item[0].substr(4) : std::string();
                }
                it.children = parseLines(item);
                if (blankInside && it.children.size() > 1) list.tight = false;
                list.children.push_back(std::move(it));
                // Blank lines before the next item make the list loose.
                size_t k = i;
                while (k < lines.size() && blank(lines[k])) ++k;
                if (k > i && k < lines.size()) {
                    auto next = listMarker(lines[k]);
                    if (next && next->ordered == list.ordered && next->c == list.marker) {
                        sawBlankBetween = true;
                        i = k;
                    }
                }
            }
            if (sawBlankBetween) list.tight = false;
            blocks.push_back(std::move(list));
            continue;
        }
        // A table: a row of cells, then its delimiter row.
        std::vector<TableAlign> aligns;
        if (line.find('|') != std::string::npos && i + 1 < lines.size() && tableDelimiterRow(lines[i + 1], &aligns) &&
            tableCells(line).size() == aligns.size()) {
            Block b;
            b.kind = Block::Kind::Table;
            b.aligns = aligns;
            auto row = [&](const std::string& l) {
                std::vector<std::vector<Inline>> cells;
                std::vector<std::string> raw = tableCells(l);
                raw.resize(aligns.size());
                for (const std::string& c : raw) cells.push_back(parseInlines(c));
                return cells;
            };
            b.rows.push_back(row(line));
            i += 2;
            while (i < lines.size() && !blank(lines[i]) && !interruptsParagraph(lines[i])) b.rows.push_back(row(lines[i++]));
            blocks.push_back(std::move(b));
            continue;
        }
        // An HTML block: kept as it is, to its blank line.
        if (t[0] == '<' && interruptsParagraph(line)) {
            Block b;
            b.kind = Block::Kind::HtmlBlock;
            while (i < lines.size() && !blank(lines[i])) b.literal += lines[i++] + "\n";
            if (!b.literal.empty()) b.literal.pop_back();
            blocks.push_back(std::move(b));
            continue;
        }
        // A paragraph, to its blank line or a block that interrupts it; a setext heading's underline.
        std::string text(trimmed(line));
        ++i;
        int setext = 0;
        while (i < lines.size()) {
            std::string_view l = lines[i];
            std::string_view tl = trimmed(l);
            if (indentOf(l) <= 3 && !tl.empty() && (std::all_of(tl.begin(), tl.end(), [](char c) { return c == '='; }) ||
                                                     std::all_of(tl.begin(), tl.end(), [](char c) { return c == '-'; }))) {
                setext = tl[0] == '=' ? 1 : 2;
                ++i;
                break;
            }
            if (interruptsParagraph(l)) break;
            text += "\n" + std::string(l.substr(size_t(std::min(indentOf(l), int(l.size())))));
            ++i;
        }
        if (setext) {
            Block b;
            b.kind = Block::Kind::Heading;
            b.level = setext;
            b.inlines = parseInlines(text);
            blocks.push_back(std::move(b));
        } else {
            blocks.push_back(paragraph(text));
        }
    }
    return blocks;
}

// ---- inlines ----------------------------------------------------------------------------------

bool isPunct(char c) { return std::ispunct(static_cast<unsigned char>(c)) != 0; }
bool isSpace(char c) { return c == ' ' || c == '\n' || c == '\t' || c == 0; }

// The entities written often, and numeric ones.
std::optional<std::pair<std::string, size_t>> entityAt(std::string_view s, size_t i) {
    size_t semi = s.find(';', i);
    if (semi == std::string_view::npos || semi - i > 10) return std::nullopt;
    std::string_view name = s.substr(i + 1, semi - i - 1);
    auto utf8 = [](uint32_t cp) {
        std::string out;
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) out += char(0xC0 | (cp >> 6)), out += char(0x80 | (cp & 0x3F));
        else if (cp < 0x10000) out += char(0xE0 | (cp >> 12)), out += char(0x80 | ((cp >> 6) & 0x3F)), out += char(0x80 | (cp & 0x3F));
        else
            out += char(0xF0 | (cp >> 18)), out += char(0x80 | ((cp >> 12) & 0x3F)), out += char(0x80 | ((cp >> 6) & 0x3F)),
                out += char(0x80 | (cp & 0x3F));
        return out;
    };
    if (!name.empty() && name[0] == '#') {
        bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
        std::string digits(name.substr(hex ? 2 : 1));
        if (digits.empty()) return std::nullopt;
        uint32_t cp = 0;
        for (char c : digits) {
            int v = std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : hex && std::isxdigit(static_cast<unsigned char>(c)) ? std::tolower(c) - 'a' + 10 : -1;
            if (v < 0) return std::nullopt;
            cp = cp * (hex ? 16 : 10) + uint32_t(v);
        }
        if (cp == 0 || cp > 0x10FFFF) cp = 0xFFFD;
        return std::pair{utf8(cp), semi + 1};
    }
    static const std::pair<const char*, uint32_t> named[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0}, {"copy", 0xA9}, {"reg", 0xAE},
        {"trade", 0x2122}, {"hellip", 0x2026}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"laquo", 0xAB}, {"raquo", 0xBB},
        {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"middot", 0xB7}, {"times", 0xD7},
        {"euro", 0x20AC}, {"deg", 0xB0}, {"larr", 0x2190}, {"rarr", 0x2192}};
    for (auto [n, cp] : named)
        if (name == n) return std::pair{utf8(cp), semi + 1};
    return std::nullopt;
}

struct Delimiter {
    size_t node;  // the Text node holding the run
    char c;
    int count, original;
    bool canOpen, canClose;
    bool active = true;
};

class InlineParser {
public:
    explicit InlineParser(std::string_view s) : s_(s) {}

    std::vector<Inline> run() {
        size_t i = 0;
        while (i < s_.size()) {
            char c = s_[i];
            if (c == '\\' && i + 1 < s_.size()) {
                if (s_[i + 1] == '\n') {
                    flush();
                    nodes_.push_back({.kind = Inline::Kind::HardBreak});
                    i += 2;
                    continue;
                }
                if (isPunct(s_[i + 1])) {
                    text_ += s_[i + 1];
                    i += 2;
                    continue;
                }
            }
            if (c == '\n') {
                // Two spaces before it: a hard break.
                bool hard = text_.size() >= 2 && text_.ends_with("  ");
                while (!text_.empty() && text_.back() == ' ') text_.pop_back();
                flush();
                nodes_.push_back({.kind = hard ? Inline::Kind::HardBreak : Inline::Kind::SoftBreak});
                ++i;
                while (i < s_.size() && s_[i] == ' ') ++i;
                continue;
            }
            if (c == '&') {
                if (auto e = entityAt(s_, i)) {
                    text_ += e->first;
                    i = e->second;
                    continue;
                }
            }
            if (c == '`') {
                size_t n = 0;
                while (i + n < s_.size() && s_[i + n] == '`') ++n;
                size_t close = i + n;
                bool found = false;
                while ((close = s_.find('`', close)) != std::string_view::npos) {
                    size_t m = 0;
                    while (close + m < s_.size() && s_[close + m] == '`') ++m;
                    if (m == n) {
                        found = true;
                        break;
                    }
                    close += m;
                }
                if (found) {
                    std::string code(s_.substr(i + n, close - i - n));
                    std::replace(code.begin(), code.end(), '\n', ' ');
                    if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ' && code.find_first_not_of(' ') != std::string::npos)
                        code = code.substr(1, code.size() - 2);
                    flush();
                    nodes_.push_back({.kind = Inline::Kind::Code, .text = code});
                    i = close + n;
                } else {
                    text_.append(n, '`');
                    i += n;
                }
                continue;
            }
            if (c == '<') {
                // An autolink <https://…>, <a@b.c>, or a tag kept as HTML.
                size_t close = s_.find('>', i);
                if (close != std::string_view::npos) {
                    std::string_view inner = s_.substr(i + 1, close - i - 1);
                    bool uri = inner.find("://") != std::string_view::npos && inner.find(' ') == std::string_view::npos;
                    bool mail = !uri && inner.find('@') != std::string_view::npos && inner.find(' ') == std::string_view::npos;
                    if (uri || mail) {
                        flush();
                        Inline link{.kind = Inline::Kind::Link, .url = (mail ? "mailto:" : "") + std::string(inner)};
                        link.children.push_back({.text = std::string(inner)});
                        nodes_.push_back(std::move(link));
                        i = close + 1;
                        continue;
                    }
                    if (!inner.empty() && (std::isalpha(static_cast<unsigned char>(inner[0])) || inner[0] == '/' || inner[0] == '!')) {
                        flush();
                        nodes_.push_back({.kind = Inline::Kind::Html, .text = std::string(s_.substr(i, close - i + 1))});
                        i = close + 1;
                        continue;
                    }
                }
            }
            if (c == '!' && i + 1 < s_.size() && s_[i + 1] == '[') {
                flush();
                brackets_.push_back({nodes_.size(), true, delimiters_.size()});
                nodes_.push_back({.text = "!["});
                i += 2;
                continue;
            }
            if (c == '[') {
                flush();
                brackets_.push_back({nodes_.size(), false, delimiters_.size()});
                nodes_.push_back({.text = "["});
                i += 1;
                continue;
            }
            if (c == ']') {
                flush();
                if (closeBracket(i)) continue;
                text_ += ']';
                ++i;
                continue;
            }
            if (c == '*' || c == '_' || c == '~') {
                size_t n = 0;
                while (i + n < s_.size() && s_[i + n] == c) ++n;
                if (c == '~' && n != 2 && n != 1) {
                    text_.append(n, c);
                    i += n;
                    continue;
                }
                char before = i > 0 ? s_[i - 1] : 0, after = i + n < s_.size() ? s_[i + n] : 0;
                bool leftFlanking = !isSpace(after) && (!isPunct(after) || isSpace(before) || isPunct(before));
                bool rightFlanking = !isSpace(before) && (!isPunct(before) || isSpace(after) || isPunct(after));
                bool canOpen = leftFlanking, canClose = rightFlanking;
                if (c == '_') {
                    canOpen = leftFlanking && (!rightFlanking || isPunct(before));
                    canClose = rightFlanking && (!leftFlanking || isPunct(after));
                }
                flush();
                delimiters_.push_back({nodes_.size(), c, int(n), int(n), canOpen, canClose});
                nodes_.push_back({.text = std::string(n, c)});
                i += n;
                continue;
            }
            // GitHub's bare links: http(s)://… and www.…
            if ((c == 'h' || c == 'w') && (i == 0 || isSpace(s_[i - 1]) || s_[i - 1] == '(' || s_[i - 1] == '*' || s_[i - 1] == '_')) {
                std::string_view rest = s_.substr(i);
                if (rest.starts_with("http://") || rest.starts_with("https://") || rest.starts_with("www.")) {
                    size_t end = 0;
                    while (end < rest.size() && !isSpace(rest[end]) && rest[end] != '<') ++end;
                    while (end > 0 && std::strchr("?!.,:*_~'\")", rest[end - 1])) {
                        if (rest[end - 1] == ')' && std::count(rest.begin(), rest.begin() + long(end), '(') >= std::count(rest.begin(), rest.begin() + long(end), ')'))
                            break;
                        --end;
                    }
                    std::string_view url = rest.substr(0, end);
                    if (url.find('.') != std::string_view::npos && end > 4) {
                        flush();
                        Inline link{.kind = Inline::Kind::Link, .url = (url.starts_with("www.") ? "http://" : "") + std::string(url)};
                        link.children.push_back({.text = std::string(url)});
                        nodes_.push_back(std::move(link));
                        i += end;
                        continue;
                    }
                }
            }
            text_ += c;
            ++i;
        }
        flush();
        processEmphasis(0);
        return std::move(nodes_);
    }

private:
    struct Bracket {
        size_t node;
        bool image;
        size_t delimitersBefore;
        bool active = true;
    };

    void flush() {
        if (text_.empty()) return;
        nodes_.push_back({.text = std::move(text_)});
        text_.clear();
    }

    // ](url "title") after an open bracket: a link (or an image) of what is between.
    bool closeBracket(size_t& i) {
        if (brackets_.empty()) return false;
        Bracket open = brackets_.back();
        brackets_.pop_back();
        if (!open.active) return false;
        size_t j = i + 1;
        if (j >= s_.size() || s_[j] != '(') return false;
        ++j;
        while (j < s_.size() && isSpace(s_[j])) ++j;
        std::string url, title;
        if (j < s_.size() && s_[j] == '<') {
            size_t close = s_.find('>', j);
            if (close == std::string_view::npos) return false;
            url = s_.substr(j + 1, close - j - 1);
            j = close + 1;
        } else {
            int depth = 0;
            size_t start = j;
            while (j < s_.size() && !isSpace(s_[j])) {
                if (s_[j] == '\\' && j + 1 < s_.size()) {
                    j += 2;
                    continue;
                }
                if (s_[j] == '(') ++depth;
                if (s_[j] == ')') {
                    if (depth == 0) break;
                    --depth;
                }
                ++j;
            }
            for (size_t k = start; k < j; ++k) {
                if (s_[k] == '\\' && k + 1 < j && isPunct(s_[k + 1])) ++k;
                url += s_[k];
            }
        }
        while (j < s_.size() && isSpace(s_[j])) ++j;
        if (j < s_.size() && (s_[j] == '"' || s_[j] == '\'' || s_[j] == '(')) {
            char close = s_[j] == '(' ? ')' : s_[j];
            size_t end = s_.find(close, j + 1);
            if (end == std::string_view::npos) return false;
            title = s_.substr(j + 1, end - j - 1);
            j = end + 1;
            while (j < s_.size() && isSpace(s_[j])) ++j;
        }
        if (j >= s_.size() || s_[j] != ')') return false;
        processEmphasis(open.delimitersBefore);
        Inline link{.kind = open.image ? Inline::Kind::Image : Inline::Kind::Link, .url = url, .title = title};
        link.children.assign(std::make_move_iterator(nodes_.begin() + long(open.node) + 1), std::make_move_iterator(nodes_.end()));
        nodes_.resize(open.node);
        nodes_.push_back(std::move(link));
        if (!open.image)  // (no links in links)
            for (Bracket& b : brackets_)
                if (!b.image) b.active = false;
        delimiters_.resize(std::min(delimiters_.size(), open.delimitersBefore));
        i = j + 1;
        return true;
    }

    // CommonMark's emphasis: closers matched to the nearest openers before them, strong where both
    // have two, the nodes between them wrapped.
    void processEmphasis(size_t from) {
        size_t closer = from;
        while (closer < delimiters_.size()) {
            Delimiter& c = delimiters_[closer];
            if (!c.canClose || c.count == 0) {
                ++closer;
                continue;
            }
            bool matched = false;
            for (size_t o = closer; o-- > from;) {
                Delimiter& op = delimiters_[o];
                if (op.c != c.c || !op.canOpen || op.count == 0) continue;
                if (c.c == '~' && op.count != c.count) continue;
                if (c.c != '~' && (op.canClose || c.canOpen) && (op.original + c.original) % 3 == 0 &&
                    !(op.original % 3 == 0 && c.original % 3 == 0))
                    continue;
                int use = c.c == '~' ? c.count : (op.count >= 2 && c.count >= 2 ? 2 : 1);
                Inline::Kind kind = c.c == '~' ? Inline::Kind::Strikethrough : use == 2 ? Inline::Kind::Strong : Inline::Kind::Emphasis;
                op.count -= use, c.count -= use;
                nodes_[op.node].text.resize(size_t(op.count));
                nodes_[c.node].text.resize(size_t(c.count));
                // What is between them, wrapped.
                Inline wrap{.kind = kind};
                wrap.children.assign(std::make_move_iterator(nodes_.begin() + long(op.node) + 1),
                                     std::make_move_iterator(nodes_.begin() + long(c.node)));
                size_t removed = c.node - op.node - 1;
                nodes_.erase(nodes_.begin() + long(op.node) + 1, nodes_.begin() + long(c.node));
                nodes_.insert(nodes_.begin() + long(op.node) + 1, std::move(wrap));
                for (Delimiter& d : delimiters_) {
                    if (d.node > op.node && d.node < op.node + 1 + removed) d.count = 0;  // (inside: done)
                    else if (d.node >= op.node + 1 + removed) d.node = d.node - removed + 1;
                }
                for (size_t k = o + 1; k < closer; ++k) delimiters_[k].count = 0;
                matched = true;
                break;
            }
            if (!matched || c.count == 0) ++closer;
        }
        // Delimiters left are text; empty runs go.
        nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [](const Inline& n) { return n.kind == Inline::Kind::Text && n.text.empty(); }),
                     nodes_.end());
        delimiters_.resize(from);
    }

    std::string_view s_;
    std::string text_;
    std::vector<Inline> nodes_;
    std::vector<Delimiter> delimiters_;
    std::vector<Bracket> brackets_;
};

// Adjacent texts made one.
void mergeTexts(std::vector<Inline>& nodes) {
    std::vector<Inline> out;
    for (Inline& n : nodes) {
        mergeTexts(n.children);
        if (n.kind == Inline::Kind::Text && n.text.empty()) continue;
        if (n.kind == Inline::Kind::Text && !out.empty() && out.back().kind == Inline::Kind::Text) out.back().text += n.text;
        else out.push_back(std::move(n));
    }
    nodes = std::move(out);
}

} // namespace

std::vector<Inline> parseInlines(std::string_view text) {
    std::vector<Inline> nodes = InlineParser(trimmed(text)).run();
    mergeTexts(nodes);
    return nodes;
}

std::vector<Block> parse(std::string_view markdown) { return parseLines(splitLines(markdown)); }

std::string plainText(const std::vector<Inline>& inlines) {
    std::string out;
    for (const Inline& n : inlines) {
        switch (n.kind) {
        case Inline::Kind::Text:
        case Inline::Kind::Code: out += n.text; break;
        case Inline::Kind::SoftBreak: out += ' '; break;
        case Inline::Kind::HardBreak: out += '\n'; break;
        case Inline::Kind::Html: break;
        default: out += plainText(n.children); break;
        }
    }
    return out;
}

} // namespace kompoz::markdown
