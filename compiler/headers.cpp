#include "headers.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace kompozc {

namespace {

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// The text with comments blanked and string and character literals emptied (so that their
// braces and semicolons don't count); its length and lines kept.
std::string stripped(std::string_view s) {
    std::string out(s);
    size_t i = 0;
    auto blank = [&](size_t from, size_t to) {
        for (size_t k = from; k < to && k < out.size(); ++k)
            if (out[k] != '\n') out[k] = ' ';
    };
    while (i < s.size()) {
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            size_t e = s.find('\n', i);
            if (e == std::string_view::npos) e = s.size();
            blank(i, e);
            i = e;
        } else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            size_t e = s.find("*/", i + 2);
            e = e == std::string_view::npos ? s.size() : e + 2;
            blank(i, e);
            i = e;
        } else if (s[i] == '"' && i > 0 && s[i - 1] == 'R') {
            size_t open = s.find('(', i);
            if (open == std::string_view::npos) break;
            std::string close = ")" + std::string(s.substr(i + 1, open - i - 1)) + "\"";
            size_t e = s.find(close, open);
            e = e == std::string_view::npos ? s.size() : e + close.size();
            blank(i + 1, e - 1);
            i = e;
        } else if (s[i] == '"' || (s[i] == '\'' && !(i > 0 && std::isxdigit(static_cast<unsigned char>(s[i - 1]))))) {
            char q = s[i];
            size_t k = i + 1;
            while (k < s.size() && s[k] != q && s[k] != '\n') k += s[k] == '\\' ? 2 : 1;
            blank(i + 1, k);
            i = k + 1;
        } else {
            ++i;
        }
    }
    return out;
}

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool startsWithWord(std::string_view s, std::string_view word) {
    return s.substr(0, word.size()) == word && (s.size() == word.size() || !identChar(s[word.size()]));
}

// Whether a statement declares a function: a parenthesis before its first initializer, outside <>.
bool looksLikeFunction(std::string_view st) {
    int angle = 0;
    for (char c : st) {
        if (c == '<') ++angle;
        else if (c == '>' && angle > 0) --angle;
        else if ((c == '=' || c == '{') && angle == 0) return false;
        else if (c == '(' && angle == 0) return true;
    }
    return false;
}

// One statement of a struct's body: its fields' names (none if it isn't a field's declaration);
// `constructor` set if it declares one.
std::vector<std::string> fieldsOf(const std::string& statement, const std::string& structName, bool& constructor) {
    std::string st = trim(statement);
    for (std::string_view word : {"static", "using", "typedef", "friend", "template", "enum", "struct", "class", "union",
                                  "static_assert", "operator", "virtual", "inline", "constexpr", "consteval", "explicit"}) {
        if (startsWithWord(st, word)) {
            if (word == "explicit") constructor = true;
            return {};
        }
    }
    if (st.empty() || st[0] == '~' || st[0] == '#' || st[0] == '[') return {};
    // A function: a parenthesis in what comes before the first initializer, outside <>.
    int angle = 0;
    for (size_t k = 0; k < st.size(); ++k) {
        char c = st[k];
        if (c == '<') ++angle;
        else if (c == '>' && angle > 0) --angle;
        else if ((c == '=' || c == '{') && angle == 0) break;
        else if (c == '(' && angle == 0) {
            std::string before = trim(st.substr(0, k));
            if (before == structName || before.ends_with(" " + structName)) constructor = true;
            return {};
        }
    }
    // Declarators: the type and the first name, then more names after commas; initializers
    // (= …, { … }), array sizes and bit widths dropped.
    std::vector<std::string> names;
    std::string current;
    int depth = 0;
    bool skipping = false;
    angle = 0;
    auto flush = [&] {
        std::string t = trim(current);
        size_t e = t.size();
        while (e > 0 && !identChar(t[e - 1])) --e;
        size_t b = e;
        while (b > 0 && identChar(t[b - 1])) --b;
        if (e > b) names.push_back(t.substr(b, e - b));
        current.clear();
        skipping = false;
    };
    for (size_t k = 0; k < st.size(); ++k) {
        char c = st[k];
        bool scope = c == ':' && ((k + 1 < st.size() && st[k + 1] == ':') || (k > 0 && st[k - 1] == ':'));  // (std::, not a bit width)
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') --depth;
        else if (c == '<') ++angle;
        else if (c == '>' && angle > 0) --angle;
        if (depth == 0 && angle == 0 && c == ',') {
            flush();
            continue;
        }
        if (!skipping && angle == 0 && depth <= 1 && (c == '=' || c == '{' || c == '[' || (c == ':' && !scope && depth == 0))) skipping = true;
        if (!skipping) current += c;
    }
    flush();
    return names;
}

}  // namespace

const std::vector<std::string>* Structs::find(std::string_view name) const {
    if (ambiguous.contains(name)) return nullptr;
    if (auto f = fields.find(name); f != fields.end()) return &f->second;
    // An alias: of one struct, or of several with the same fields.
    auto a = aliases.find(name);
    if (a == aliases.end()) return nullptr;
    const std::vector<std::string>* found = nullptr;
    for (const std::string& target : a->second) {
        if (ambiguous.contains(target)) return nullptr;
        auto f = fields.find(target);
        if (f == fields.end()) continue;
        if (found && *found != f->second) return nullptr;
        found = &f->second;
    }
    return found;
}

void Structs::readIncludes(std::string_view source, const fs::path& dir) {
    size_t i = 0;
    while (i < source.size()) {
        size_t eol = source.find('\n', i);
        size_t end = eol == std::string_view::npos ? source.size() : eol + 1;
        std::string line = trim(source.substr(i, end - i));
        i = end;
        if (line.empty() || line[0] != '#') continue;
        std::string rest = trim(std::string_view(line).substr(1));
        if (!startsWithWord(rest, "include")) continue;
        size_t q = rest.find('"');
        size_t q2 = q == std::string::npos ? q : rest.find('"', q + 1);
        if (q2 == std::string::npos) continue;
        fs::path target = dir / rest.substr(q + 1, q2 - q - 1);
        if (fs::exists(target)) readHeader(target);
    }
}

void Structs::readHeader(const fs::path& header) {
    std::error_code ec;
    fs::path path = fs::weakly_canonical(header, ec);
    if (ec) path = header;
    if (!read.insert(path).second) return;
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string src = buffer.str();
    readIncludes(src, path.parent_path());
    readText(src);
}

void Structs::readText(std::string_view src) {
    std::string text = stripped(src);
    // using Name = Some::Struct;
    for (size_t i = 0; (i = text.find("using", i)) != std::string::npos; i += 5) {
        if ((i > 0 && identChar(text[i - 1])) || (i + 5 < text.size() && identChar(text[i + 5]))) continue;
        size_t semi = text.find(';', i);
        if (semi == std::string::npos) break;
        std::string decl = trim(std::string_view(text).substr(i + 5, semi - i - 5));
        size_t eq = decl.find('=');
        if (eq == std::string::npos || decl.find_first_of("<(") != std::string::npos) continue;
        std::string alias = trim(std::string_view(decl).substr(0, eq)), target = trim(std::string_view(decl).substr(eq + 1));
        if (size_t colon = target.rfind("::"); colon != std::string::npos) target = target.substr(colon + 2);
        bool plain = !alias.empty() && !target.empty();
        for (char c : alias + target) plain = plain && identChar(c);
        if (plain) aliases[alias].insert(target);
    }

    for (size_t i = 0; (i = text.find("struct", i)) != std::string::npos; i += 6) {
        if ((i > 0 && identChar(text[i - 1])) || (i + 6 < text.size() && identChar(text[i + 6]))) continue;
        // Not a template's, nor an enum's (enum struct).
        size_t back = text.find_last_of(";{}", i == 0 ? 0 : i - 1);
        std::string before = trim(std::string_view(text).substr(back == std::string::npos ? 0 : back + 1, i - (back == std::string::npos ? 0 : back + 1)));
        if (before.find("template") != std::string::npos || before.ends_with("enum")) continue;
        size_t k = i + 6;
        while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
        size_t nameStart = k;
        while (k < text.size() && identChar(text[k])) ++k;
        std::string name = text.substr(nameStart, k - nameStart);
        if (name.empty()) continue;
        while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
        if (text.compare(k, 5, "final") == 0) {
            k += 5;
            while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
        }
        if (k >= text.size() || text[k] != '{') continue;  // a declaration, a base list (not an aggregate's to fill in so), ...
        // Its body: statements at its own depth.
        size_t b = k + 1;
        int depth = 1;
        std::string statement;
        std::vector<std::string> names;
        bool constructor = false, hidden = false;
        for (; b < text.size() && depth > 0; ++b) {
            char c = text[b];
            if (c == '{') {
                ++depth;
                statement += c;
                continue;
            }
            if (c == '}') {
                if (--depth == 0) break;
                statement += c;
                // A function's body ends its statement (no semicolon after it).
                if (depth == 1 && looksLikeFunction(statement)) {
                    fieldsOf(statement, name, constructor);
                    statement.clear();
                }
                continue;
            }
            if (depth == 1 && c == ';') {
                for (auto& n : fieldsOf(statement, name, constructor)) names.push_back(n);
                statement.clear();
                continue;
            }
            if (depth == 1 && c == ':' && b + 1 < text.size() && text[b + 1] != ':' && (b == 0 || text[b - 1] != ':')) {
                std::string label = trim(statement);
                if (label == "private" || label == "protected") hidden = true;
                if (label == "private" || label == "protected" || label == "public") {
                    statement.clear();
                    continue;
                }
            }
            statement += c;
        }
        if (constructor || hidden || names.empty()) continue;
        auto [at, added] = fields.emplace(name, names);
        if (!added && at->second != names) ambiguous.insert(name);
    }
}

}  // namespace kompozc
