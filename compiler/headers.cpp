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
    // (static data members are the type's values: Color::White; its static functions have parentheses;
    // constexpr / inline / explicit say nothing of what follows: a constexpr constructor is one)
    for (bool more = true; more;) {
        more = false;
        for (std::string_view word : {"static", "constexpr", "consteval", "inline", "explicit"}) {
            if (startsWithWord(st, word) && !startsWithWord(st, "static_assert")) {
                if (word == "explicit") constructor = true;
                st = trim(std::string_view(st).substr(word.size()));
                more = true;
            }
        }
    }
    for (std::string_view word : {"using", "typedef", "friend", "template", "enum", "struct", "class", "union", "static_assert", "operator", "virtual"}) {
        if (startsWithWord(st, word)) return {};
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

// A function's declaration (or definition): its name, the first name called outside brackets and
// angles that isn't a keyword (template <class A> requires(…) Modifier padding(A&&...) const is
// padding). Empty for an operator.
std::string functionName(std::string_view st) {
    int depth = 0, angle = 0;
    for (size_t k = 0; k < st.size(); ++k) {
        char c = st[k];
        if (c == '(' || c == '[') {
            if (c == '(' && depth == 0 && angle == 0) {
                size_t e = k;
                while (e > 0 && std::isspace(static_cast<unsigned char>(st[e - 1]))) --e;
                size_t b = e;
                while (b > 0 && identChar(st[b - 1])) --b;
                std::string word(st.substr(b, e - b));
                if (word == "operator") return {};
                if (!word.empty() && word != "requires" && word != "decltype" && word != "noexcept" && word != "alignas" &&
                    word != "sizeof" && word != "explicit")
                    return word;
            }
            ++depth;
        } else if (c == ')' || c == ']') {
            --depth;
        } else if (c == '<' && depth == 0) {
            ++angle;
        } else if (c == '>' && depth == 0 && angle > 0) {
            --angle;
        }
    }
    return {};
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

bool Structs::member(std::string_view type, std::string_view name) const {
    for (int depth = 0; depth < 16; ++depth) {  // (bases: one at a time, as deep as they go)
        if (ambiguousClasses.contains(type)) return false;
        auto c = classes.find(type);
        if (c == classes.end()) return false;
        if (c->second.names.contains(name)) return true;
        if (c->second.bases.size() != 1) {
            for (const std::string& base : c->second.bases)
                if (member(base, name)) return true;
            return false;
        }
        type = c->second.bases[0];
    }
    return false;
}

bool Structs::staticFunction(std::string_view type, std::string_view name) const {
    auto c = classes.find(type);
    return c != classes.end() && c->second.staticFunctions.contains(name);
}

bool Structs::derivesFrom(std::string_view type, std::string_view base) const {
    if (type == base) return true;
    if (ambiguousClasses.contains(type)) return false;
    auto c = classes.find(type);
    if (c == classes.end()) return false;
    for (const std::string& b : c->second.bases)
        if (b != type && derivesFrom(b, base)) return true;
    return false;
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
        std::string name = rest.substr(q + 1, q2 - q - 1);
        fs::path target = dir / name;
        for (size_t k = 0; !fs::exists(target) && k < includeDirs.size(); ++k) target = includeDirs[k] / name;  // (as the compiler's -I)
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

// The namespaces open at `at` (below kompoz's own): "ListItemDefaults" for an alias declared in it.
std::string namespacesAt(std::string_view text, size_t at) {
    std::vector<std::pair<std::string, int>> open;  // name, the depth it opened at
    int depth = 0;
    for (size_t i = 0; i < at && i < text.size(); ++i) {
        char c = text[i];
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            while (!open.empty() && open.back().second == depth) open.pop_back();
            --depth;
        } else if (c == 'n' && text.compare(i, 9, "namespace") == 0 && (i == 0 || !identChar(text[i - 1])) && i + 9 < text.size() &&
                   !identChar(text[i + 9])) {
            size_t k = i + 9;
            while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
            size_t b = k;
            while (k < text.size() && (identChar(text[k]) || text[k] == ':')) ++k;
            std::string name(text.substr(b, k - b));
            while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
            if (k < text.size() && text[k] == '{' && !name.empty()) {
                open.emplace_back(name, depth + 1);
                ++depth;
                i = k;
            }
        }
    }
    std::string path;
    for (const auto& [name, d] : open)
        if (name != "kompoz") path += (path.empty() ? "" : "::") + name;
    return path;
}

void Structs::readText(std::string_view src) {
    std::string text = stripped(src);
    // using Name = Some::Struct; (recorded under its name and, in a namespace, its qualified name:
    // TextFieldDefaults::colors and ListItemDefaults::colors are different structs)
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
        if (plain) {
            aliases[alias].insert(target);
            if (std::string ns = namespacesAt(text, i); !ns.empty()) aliases[ns + "::" + alias].insert(target);
        }
    }

    // namespace routes { … }: a name Kotlin writes before a dot (routes.Item(…)).
    for (size_t i = 0; (i = text.find("namespace", i)) != std::string::npos; i += 9) {
        if ((i > 0 && identChar(text[i - 1])) || (i + 9 < text.size() && identChar(text[i + 9]))) continue;
        size_t k = i + 9;
        while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
        size_t b = k;
        while (k < text.size() && identChar(text[k])) ++k;
        std::string name = text.substr(b, k - b);
        while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
        if (!name.empty() && k < text.size() && text[k] == '{' && name != "kompoz" && name != "detail" && name != "std") namespaces.insert(name);
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

    // Every class's public members (its fields and functions) and bases.
    for (std::string_view keyword : {"struct", "class"}) {
        for (size_t i = 0; (i = text.find(keyword, i)) != std::string::npos; i += keyword.size()) {
            if ((i > 0 && identChar(text[i - 1])) || (i + keyword.size() < text.size() && identChar(text[i + keyword.size()]))) continue;
            size_t back = text.find_last_of(";{}", i == 0 ? 0 : i - 1);
            std::string before = trim(std::string_view(text).substr(back == std::string::npos ? 0 : back + 1, i - (back == std::string::npos ? 0 : back + 1)));
            if (before.ends_with("enum")) continue;
            size_t k = i + keyword.size();
            while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
            size_t nameStart = k;
            while (k < text.size() && identChar(text[k])) ++k;
            std::string name = text.substr(nameStart, k - nameStart);
            while (text.compare(k, 2, "::") == 0) {  // class Outer::Inner { … }: defined outside, Inner's
                k += 2;
                nameStart = k;
                while (k < text.size() && identChar(text[k])) ++k;
                name = text.substr(nameStart, k - nameStart);
            }
            if (name.empty()) continue;
            size_t open = text.find_first_of("{;()<>=", k);
            if (open == std::string::npos || text[open] != '{') continue;  // a declaration, a template's parameter, ...
            // Bases: what comes after `:`, each one's last name (public Base, virtual detail::Other<T>).
            std::string head = trim(std::string_view(text).substr(k, open - k));
            if (head.starts_with("final")) head = trim(std::string_view(head).substr(5));
            Members members;
            if (!head.empty()) {
                if (head[0] != ':') continue;
                int angle = 0;
                std::string current;
                auto flush = [&] {
                    std::string t = trim(current);
                    if (size_t lt = t.find('<'); lt != std::string::npos) t = trim(std::string_view(t).substr(0, lt));
                    size_t e = t.size(), b = e;
                    while (b > 0 && identChar(t[b - 1])) --b;
                    if (e > b) members.bases.push_back(t.substr(b, e - b));
                    current.clear();
                };
                for (char c : head.substr(1)) {
                    if (c == '<') ++angle;
                    else if (c == '>' && angle > 0) --angle;
                    if (c == ',' && angle == 0) flush();
                    else current += c;
                }
                flush();
            }
            bool visible = keyword == "struct", constructor = false;
            int depth = 1, parens = 0;  // (braces in parentheses, a default argument's = {}, are no block)
            std::string statement;
            auto take = [&] {
                std::string st = trim(statement);
                statement.clear();
                if (!visible || st.empty()) return;
                for (std::string_view word : {"using", "typedef", "friend", "enum", "struct", "class", "union", "static_assert"})
                    if (startsWithWord(st, word)) return;
                if (looksLikeFunction(st) || startsWithWord(st, "template")) {
                    std::string f = functionName(st);
                    if (!f.empty() && f != name) {
                        members.names.insert(f);
                        if (startsWithWord(st, "static")) members.staticFunctions.insert(f);
                    }
                } else {
                    for (auto& n : fieldsOf(st, name, constructor)) members.names.insert(n);
                }
            };
            for (size_t b = open + 1; b < text.size() && depth > 0; ++b) {
                char c = text[b];
                if (depth == 1 && c == '(') ++parens;
                if (depth == 1 && c == ')' && parens > 0) --parens;
                if (c == '{' && !(depth == 1 && parens > 0)) {
                    ++depth;
                    statement += c;
                    continue;
                }
                if (c == '}' && !(depth == 1 && parens > 0)) {
                    if (--depth == 0) break;
                    statement += c;
                    if (depth == 1 && looksLikeFunction(statement)) take();  // a function's body ends it
                    continue;
                }
                if (depth == 1 && c == ';') {
                    take();
                    continue;
                }
                if (depth == 1 && c == ':' && b + 1 < text.size() && text[b + 1] != ':' && text[b - 1] != ':') {
                    std::string label = trim(statement);
                    if (label == "private" || label == "protected" || label == "public") {
                        visible = label == "public";
                        statement.clear();
                        continue;
                    }
                }
                statement += c;
            }
            // (An empty one, a named argument's tag, names::Modifier, gives way to one with members.)
            bool empty = members.names.empty() && members.bases.empty();
            auto [at, added] = classes.emplace(name, members);
            if (!added && !empty) {
                if (at->second.names.empty() && at->second.bases.empty()) at->second = std::move(members);
                else if (at->second.names != members.names || at->second.bases != members.bases) ambiguousClasses.insert(name);
            }
        }
    }
}

}  // namespace kompozc
