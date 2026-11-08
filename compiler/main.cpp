// kompozc: Kompoz's compiler for Compose code in C++ sources. Each @Composable function in a
// .cpp file is written in Compose's own syntax (Kotlin's); kompozc rewrites it into the C++ the
// Kompoz library is written in, before the C++ compiler sees the file, the way Compose's compiler
// plugin rewrites Kotlin. Everything else in the file is C++ and passes through as it is.
//
//   kompozc SOURCE -o OUTPUT
//
// The output points back at the source with #line directives: the C++ compiler's messages and
// the debugger show the source's lines.

#include "codegen.hpp"
#include "headers.hpp"
#include "parser.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using namespace kompozc;

namespace {

// A Kotlin declaration in the C++: a @Composable function, a data class or a top-level val.
struct Found {
    std::optional<Function> function;
    std::optional<DataClass> dataClass;
    std::optional<Property> property;
    std::optional<EnumClass> enumClass;
    std::optional<ClassDecl> classDecl;
    std::optional<ObjectDecl> object;
    size_t begin, end;  // offsets in the source
    // The C++ namespace it is in, as a call names it: "::" at the top, "::app::" in namespace app;
    // empty in an unnamed one (a call there can't name it so).
    std::string qualifier;

    const std::string& name() const {
        return function ? function->name : dataClass ? dataClass->name : property ? property->name : classDecl ? classDecl->name : object ? object->name : enumClass->name;
    }
};

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// The position (line, column) of an offset.
Pos positionOf(std::string_view src, size_t offset) {
    Pos p;
    for (size_t i = 0; i < offset; ++i) {
        if (src[i] == '\n') {
            ++p.line;
            p.column = 1;
        } else if ((static_cast<unsigned char>(src[i]) & 0xC0) != 0x80) {
            ++p.column;
        }
    }
    p.offset = offset;
    return p;
}

// Whether only blanks come before `at` on its line.
bool startsLine(std::string_view src, size_t at) {
    while (at > 0 && (src[at - 1] == ' ' || src[at - 1] == '\t')) --at;
    return at == 0 || src[at - 1] == '\n';
}

// Whether Kotlin's enum class starts at `at`: enum class Name { A, B } with entries only (C++'s with
// a base, enum class X : int, or values, A = 1, is C++ and stays as it is; one with entries only is
// the same in both, so it may be written either way).
bool startsEnumClass(std::string_view src, size_t at) {
    size_t k = at;
    auto word = [&] {
        while (k < src.size() && std::isspace(static_cast<unsigned char>(src[k]))) ++k;
        size_t b = k;
        while (k < src.size() && identChar(src[k])) ++k;
        return src.substr(b, k - b);
    };
    if (word() != "enum" || word() != "class" || word().empty()) return false;
    while (k < src.size() && std::isspace(static_cast<unsigned char>(src[k]))) ++k;
    if (k >= src.size() || src[k] != '{') return false;
    size_t close = src.find('}', k);
    if (close == std::string_view::npos) return false;
    for (size_t i = k + 1; i < close; ++i)
        if (!identChar(src[i]) && !std::isspace(static_cast<unsigned char>(src[i])) && src[i] != ',' && src[i] != ';') return false;
    return true;
}

// Whether Kotlin's class starts at `at`: [private | internal | public | open | abstract]... class Name, then a
// primary constructor `(`, a base with its arguments `: Base(…)`, or a body that begins as Kotlin's do
// (val, var, fun, init, override, …; C++'s begin with public: or a type).
bool startsClass(std::string_view src, size_t at) {
    size_t k = at;
    auto word = [&] {
        while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
        size_t b = k;
        while (k < src.size() && identChar(src[k])) ++k;
        return src.substr(b, k - b);
    };
    std::string_view w = word();
    while (w == "private" || w == "internal" || w == "public" || w == "open" || w == "abstract") w = word();
    if (w != "class" || word().empty()) return false;
    while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
    if (k >= src.size()) return false;
    if (src[k] == '(') return true;
    if (src[k] == ':') {  // a base: Kotlin's is constructed, Base(…)
        size_t brace = src.find_first_of("{\n", k);
        return src.substr(k, (brace == std::string_view::npos ? src.size() : brace) - k).find('(') != std::string_view::npos;
    }
    if (src[k] != '{') return false;
    ++k;
    while (k < src.size() && std::isspace(static_cast<unsigned char>(src[k]))) ++k;  // (the body may start on the next line)
    std::string_view first = word();
    if (first == "private" || first == "public" || first == "internal" || first == "protected") {
        if (k < src.size() && src[k] == ':') return false;  // C++'s access label
        first = word();
    }
    static const std::set<std::string_view> kotlin = {"val", "var", "fun", "init", "override", "open", "abstract", "suspend", "lateinit"};
    return kotlin.contains(first);
}

// Whether Kotlin's object starts at `at`: object Name, with no body or an empty one (C++ has no `object`).
bool startsObject(std::string_view src, size_t at) {
    size_t k = at;
    auto word = [&] {
        while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
        size_t b = k;
        while (k < src.size() && identChar(src[k])) ++k;
        return src.substr(b, k - b);
    };
    std::string_view w = word();
    while (w == "private" || w == "internal" || w == "public" || w == "data") w = word();
    if (w != "object") return false;
    return !word().empty();
}

// What annotated Kotlin declaration starts at `at` (an '@'): 'd' a data class, 'o' an object, 'c' a class,
// 'f' a function (@Preview @Composable fun, @Test fun), ' ' none (annotations in C++ don't exist).
char annotatedDeclaration(std::string_view src, size_t at) {
    size_t k = at;
    for (;;) {
        if (k >= src.size() || src[k] != '@') break;
        ++k;
        size_t b = k;
        while (k < src.size() && (identChar(src[k]) || src[k] == '.')) ++k;
        if (k == b) return ' ';
        if (k < src.size() && src[k] == '(') {  // its arguments
            int depth = 0;
            for (; k < src.size(); ++k) {
                if (src[k] == '(') ++depth;
                else if (src[k] == ')' && --depth == 0) {
                    ++k;
                    break;
                }
            }
        }
        while (k < src.size() && std::isspace(static_cast<unsigned char>(src[k]))) ++k;
    }
    size_t b = k;
    while (k < src.size() && identChar(src[k])) ++k;
    std::string_view w = src.substr(b, k - b);
    while (w == "private" || w == "internal" || w == "public" || w == "open" || w == "abstract") {
        while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
        b = k;
        while (k < src.size() && identChar(src[k])) ++k;
        w = src.substr(b, k - b);
    }
    if (w == "data") {
        size_t j = k;
        while (j < src.size() && std::isspace(static_cast<unsigned char>(src[j]))) ++j;
        if (src.substr(j, 5) == "class") return 'd';
        if (src.substr(j, 6) == "object") return 'o';
        return ' ';
    }
    if (w == "object") return 'o';
    if (w == "class" && startsClass(src, b)) return 'c';
    if (w == "fun") return 'f';
    return ' ';
}

// Whether a top-level val starts at `at`: [private | internal | public | const]... val name, then
// `:`, `=` or `by` (C++ has no `val`; a variable named so is followed by none of those).
bool startsVal(std::string_view src, size_t at) {
    auto word = [&](size_t& k) {
        while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
        size_t b = k;
        while (k < src.size() && identChar(src[k])) ++k;
        return src.substr(b, k - b);
    };
    size_t k = at;
    std::string_view w = word(k);
    while (w == "private" || w == "internal" || w == "public" || w == "const") w = word(k);
    if (w != "val" && w != "var") return false;
    if (word(k).empty()) return false;
    while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
    if (k < src.size() && (src[k] == ':' || (src[k] == '=' && (k + 1 >= src.size() || src[k + 1] != '=')))) return true;
    return src.substr(k, 2) == "by" && (k + 2 >= src.size() || !identChar(src[k + 2]));
}

// Whether a plain (not @Composable) function starts at `at`: [private | internal | public | inline]...
// fun name or fun Receiver.name (C++ has no `fun`).
bool startsFun(std::string_view src, size_t at) {
    size_t k = at;
    for (;;) {
        while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
        size_t b = k;
        while (k < src.size() && identChar(src[k])) ++k;
        std::string_view w = src.substr(b, k - b);
        if (w == "private" || w == "internal" || w == "public" || w == "inline") continue;
        if (w != "fun") return false;
        while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
        return k < src.size() && identChar(src[k]);
    }
}

// Where a top-level val's Kotlin (or an expression body's) ends: the end of the line its brackets
// close on, unless the line ends wanting more (=, an operator, a comma) or the next line goes on
// with it (.member, ?.member, ?: x, && x, || x), as Kotlin reads it. (The C++ after it is never
// read: it may hold what Kotlin's lexer takes for an error, a #.)
size_t valEnd(std::string_view src, size_t at) {
    int depth = 0;
    size_t i = at;
    char last = 0;  // the line's last character outside comments
    while (i < src.size()) {
        char c = src[i];
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            size_t e = src.find("*/", i + 2);
            i = e == std::string_view::npos ? src.size() : e + 2;
            continue;
        }
        if (src.substr(i, 3) == "\"\"\"") {
            size_t e = src.find("\"\"\"", i + 3);
            i = e == std::string_view::npos ? src.size() : e + 3;
            continue;
        }
        if (c == '"' || c == '\'') {
            ++i;
            while (i < src.size() && src[i] != c && src[i] != '\n') i += src[i] == '\\' ? 2 : 1;
            ++i;
            last = c;
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(c))) last = c;
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') --depth;
        else if (c == '\n' && depth <= 0) {
            size_t k = i;
            while (k < src.size() && std::isspace(static_cast<unsigned char>(src[k]))) ++k;
            std::string_view next = src.substr(k, 2);
            bool wantsMore = std::string_view("=+-*/%,.&|:>!").find(last) != std::string_view::npos;
            bool elseNext = src.substr(k, 4) == "else" && (k + 4 >= src.size() || !identChar(src[k + 4]));  // if … \n else …
            if (!wantsMore && !elseNext && !(next.starts_with('.') || next == "?." || next == "?:" || next == "&&" || next == "||")) return i;
        }
        ++i;
    }
    return src.size();
}

// Walks the C++ around the functions: skips comments, strings, characters and raw strings, and
// finds each @Composable.
std::vector<Found> scan(std::string_view src) {
    std::vector<Found> found;
    // The C++ namespaces around: each one's name ("" unnamed) and the brace depth inside it.
    std::vector<std::pair<std::string, int>> namespaces;
    int depth = 0;
    auto qualifier = [&] {
        std::string q = "::";
        for (auto& [name, d] : namespaces) {
            if (name.empty()) return std::string();
            q += name + "::";
        }
        return q;
    };
    size_t i = 0;
    while (i < src.size()) {
        char c = src[i];
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') ++i;
        } else if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            size_t e = src.find("*/", i + 2);
            i = e == std::string_view::npos ? src.size() : e + 2;
        } else if (c == '"' && i > 0 && src[i - 1] == 'R' && (i < 2 || !identChar(src[i - 2]) || src[i - 2] == '8' || src[i - 2] == 'u' || src[i - 2] == 'U' || src[i - 2] == 'L')) {
            size_t open = src.find('(', i);
            if (open == std::string_view::npos) break;
            std::string close = ")" + std::string(src.substr(i + 1, open - i - 1)) + "\"";
            size_t e = src.find(close, open);
            i = e == std::string_view::npos ? src.size() : e + close.size();
        } else if (c == '"' || c == '\'') {
            // A ' after a digit inside a number is a separator (1'000), not a character.
            if (c == '\'' && i > 0 && std::isxdigit(static_cast<unsigned char>(src[i - 1]))) {
                size_t w = i;
                while (w > 0 && (identChar(src[w - 1]) || src[w - 1] == '\'' || src[w - 1] == '.')) --w;
                if (std::isdigit(static_cast<unsigned char>(src[w]))) {
                    ++i;
                    continue;
                }
            }
            ++i;
            while (i < src.size() && src[i] != c && src[i] != '\n') {
                if (src[i] == '\\') ++i;
                ++i;
            }
            ++i;
        } else if (c == '@' && annotatedDeclaration(src, i) != ' ') {
            // @Serializable data class / object / class: read from the annotation
            char kind = annotatedDeclaration(src, i);
            Parser parser(src, positionOf(src, i));
            Found f;
            if (kind == 'd') f.dataClass = parser.dataClass();
            else if (kind == 'o') f.object = parser.objectDecl();
            else if (kind == 'c') f.classDecl = parser.classDecl();
            else {
                f.function = parser.function();
                if (f.function->expressionBody) {  // (its expression ends with its line, as a val's: measured from `fun`)
                    size_t funAt = i;
                    while ((funAt = src.find("fun", funAt + 1)) != std::string_view::npos &&
                           (identChar(src[funAt - 1]) || (funAt + 3 < src.size() && identChar(src[funAt + 3]))))
                        ;
                    Parser again(src.substr(0, valEnd(src, funAt)), positionOf(src, i));
                    f.function = again.function();
                }
            }
            f.begin = i;
            f.end = f.dataClass ? f.dataClass->end.offset : f.object ? f.object->end.offset : f.classDecl ? f.classDecl->end.offset : f.function->end.offset;
            f.qualifier = qualifier();
            found.push_back(std::move(f));
            i = f.end;
        } else if (c == '@' && src.substr(i + 1, 10) == "Composable" && (i + 11 >= src.size() || !identChar(src[i + 11]))) {
            Parser parser(src, positionOf(src, i));
            Function f = parser.function();
            size_t end = f.end.offset;
            found.push_back({std::move(f), std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, i, end, qualifier()});
            i = end;
        } else if (c == 'd' && (i == 0 || !identChar(src[i - 1])) && src.substr(i, 4) == "data" && i + 4 < src.size() &&
                   std::isspace(static_cast<unsigned char>(src[i + 4]))) {
            // data class: never C++ (a name, then the class keyword)
            size_t k = i + 4;
            while (k < src.size() && std::isspace(static_cast<unsigned char>(src[k]))) ++k;
            if (src.substr(k, 5) == "class" && (k + 5 >= src.size() || !identChar(src[k + 5]))) {
                Parser parser(src, positionOf(src, i));
                DataClass d = parser.dataClass();
                size_t end = d.end.offset;
                found.push_back({std::nullopt, std::move(d), std::nullopt, std::nullopt, std::nullopt, std::nullopt, i, end, qualifier()});
                i = end;
            } else {
                i += 4;
            }
        } else if (identChar(c) && (i == 0 || !identChar(src[i - 1])) && startsLine(src, i) && startsClass(src, i)) {
            Parser parser(src, positionOf(src, i));
            ClassDecl k = parser.classDecl();
            size_t end = k.end.offset;
            Found f;
            f.classDecl = std::move(k);
            f.begin = i, f.end = end, f.qualifier = qualifier();
            found.push_back(std::move(f));
            i = end;
        } else if (c == 'o' && (i == 0 || !identChar(src[i - 1])) && startsLine(src, i) && startsObject(src, i)) {
            Parser parser(src, positionOf(src, i));
            Found f;
            f.object = parser.objectDecl();
            f.begin = i, f.end = f.object->end.offset, f.qualifier = qualifier();
            found.push_back(std::move(f));
            i = f.end;
        } else if (c == 'e' && (i == 0 || !identChar(src[i - 1])) && startsEnumClass(src, i)) {
            Parser parser(src, positionOf(src, i));
            EnumClass e = parser.enumClass();
            size_t end = e.end.offset;
            found.push_back({std::nullopt, std::nullopt, std::nullopt, std::move(e), std::nullopt, std::nullopt, i, end, qualifier()});
            i = end;
        } else if (identChar(c) && (i == 0 || !identChar(src[i - 1])) && startsLine(src, i) && startsFun(src, i)) {
            Parser parser(src, positionOf(src, i));
            Function f = parser.function();
            size_t end = f.end.offset;
            if (f.expressionBody) {  // (its expression ends with its line, as a val's: what follows is C++)
                Parser again(src.substr(0, valEnd(src, i)), positionOf(src, i));
                f = again.function();
                end = f.end.offset;
            }
            found.push_back({std::move(f), std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, i, end, qualifier()});
            i = end;
        } else if (identChar(c) && (i == 0 || !identChar(src[i - 1])) && startsLine(src, i) && startsVal(src, i)) {
            Parser parser(src.substr(0, valEnd(src, i)), positionOf(src, i));
            Property p = parser.property();
            size_t end = p.end.offset;
            found.push_back({std::nullopt, std::nullopt, std::move(p), std::nullopt, std::nullopt, std::nullopt, i, end, qualifier()});
            i = end;
        } else if (identChar(c)) {
            size_t b = i;
            while (i < src.size() && identChar(src[i])) ++i;  // (a name: "data" inside one isn't a declaration)
            if (src.substr(b, i - b) == "namespace" && (b == 0 || !identChar(src[b - 1]))) {
                // namespace a::b { (not using namespace x; nor an alias, namespace x = y;)
                size_t k = i;
                std::string name;
                while (k < src.size() && (std::isspace(static_cast<unsigned char>(src[k])) || identChar(src[k]) || src[k] == ':'))
                    if (!std::isspace(static_cast<unsigned char>(src[k]))) name += src[k++];
                    else ++k;
                if (k < src.size() && src[k] == '{') {
                    namespaces.emplace_back(name, depth + 1);
                    i = k;
                }
            }
        } else {
            if (c == '{') {
                ++depth;
            } else if (c == '}') {
                while (!namespaces.empty() && namespaces.back().second == depth) namespaces.pop_back();
                --depth;
            }
            ++i;
        }
    }
    return found;
}

// The C++ between the functions, its quoted #includes made absolute: the output is elsewhere,
// and "x.hpp" is looked for next to the file that includes it first.
std::string passThrough(std::string_view text, const fs::path& dir) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        size_t eol = text.find('\n', i);
        size_t end = eol == std::string_view::npos ? text.size() : eol + 1;
        std::string_view line = text.substr(i, end - i);
        size_t h = line.find_first_not_of(" \t");
        if (h != std::string_view::npos && line[h] == '#') {
            size_t d = line.find_first_not_of(" \t", h + 1);
            if (d != std::string_view::npos && line.substr(d, 7) == "include") {
                size_t q = line.find('"', d + 7);
                size_t q2 = q == std::string_view::npos ? q : line.find('"', q + 1);
                if (q2 != std::string_view::npos) {
                    fs::path target = dir / std::string(line.substr(q + 1, q2 - q - 1));
                    if (fs::exists(target)) {
                        out += std::string(line.substr(0, q + 1)) + fs::absolute(target).lexically_normal().generic_string() +
                               std::string(line.substr(q2));
                        i = end;
                        continue;
                    }
                }
            }
        }
        out += line;
        i = end;
    }
    return out;
}

// The names a declaration uses (calls, types): what of the module's other sources it needs.
void typeNames(const Type& t, std::set<std::string, std::less<>>& out) {
    if (!t.name.empty()) out.insert(t.name);
    for (const Type& a : t.args) typeNames(a, out);
    for (const Type& p : t.params) typeNames(p, out);
    if (t.result) typeNames(*t.result, out);
    if (t.receiver) typeNames(*t.receiver, out);
}
void names(const Node* n, std::set<std::string, std::less<>>& out) {
    if (!n) return;
    if (n->kind == Node::Name) out.insert(n->text);
    if (n->type) typeNames(*n->type, out);
    for (const Type& t : n->typeArgs) typeNames(t, out);
    for (const NodeP& k : n->kids) names(k.get(), out);
    names(n->lambda.get(), out);
    for (const auto& b : n->branches) {
        for (const auto& c : b.conds) names(c.second.get(), out);
        for (const Type& t : b.types) typeNames(t, out);
        names(b.body.get(), out);
    }
}
void names(const Found& f, std::set<std::string, std::less<>>& out) {
    if (f.function) {
        for (const Param& p : f.function->params) {
            typeNames(p.type, out);
            names(p.defaultValue.get(), out);
        }
        if (f.function->result) typeNames(*f.function->result, out);
        if (f.function->receiver) typeNames(*f.function->receiver, out);
        names(f.function->body.get(), out);
    } else if (f.property) {
        if (f.property->decl->type) typeNames(*f.property->decl->type, out);
        names(f.property->decl.get(), out);
    } else if (f.enumClass || f.object) {
        // (nothing: its entries are its own; an object is its name)
    } else if (f.classDecl) {
        const ClassDecl& c = *f.classDecl;
        for (const Param& p : c.ctorParams) {
            typeNames(p.type, out);
            names(p.defaultValue.get(), out);
        }
        if (c.base) typeNames(*c.base, out);
        for (const NodeP& a : c.baseArgs) names(a.get(), out);
        for (const ClassProperty& p : c.properties) {
            if (p.type) typeNames(*p.type, out);
            names(p.init.get(), out);
        }
        for (const Function& m : c.methods) {
            for (const Param& p : m.params) {
                typeNames(p.type, out);
                names(p.defaultValue.get(), out);
            }
            if (m.result) typeNames(*m.result, out);
            names(m.body.get(), out);
        }
        for (const NodeP& b : c.initBlocks) names(b.get(), out);
    } else {
        for (const Param& p : f.dataClass->fields) {
            typeNames(p.type, out);
            names(p.defaultValue.get(), out);
        }
    }
}

// Another source of the module (the target): its declarations, for the calls to them here.
struct OtherSource {
    std::string lineFile;
    std::vector<Found> found;
};

std::string translate(const std::string& src, const fs::path& source, const std::string& lineFile, const Structs& structs,
                      const std::vector<OtherSource>& others) {
    std::vector<Found> found = scan(src);
    Module module;
    module.structs = &structs;
    for (const Found& f : found) {
        if (f.function && f.function->composable) module.qualifiers[f.function->name] = f.qualifier;
        if (f.function) module.functions[f.function->name] = &*f.function;
        else if (f.dataClass) module.classes[f.dataClass->name] = &*f.dataClass;
        else if (f.property) module.properties[f.property->name] = &*f.property;
        else if (f.classDecl) module.classDecls[f.classDecl->name] = &*f.classDecl;
        else if (f.object) module.objects.insert(f.object->name);
    }
    // The other sources' declarations this one uses, and those they use in turn (a data class in
    // a parameter's type): declared here too, as C++ needs (Kotlin's module needs nothing).
    std::vector<std::pair<const OtherSource*, const Found*>> imported;
    {
        std::set<std::string, std::less<>> used;
        for (const Found& f : found) names(f, used);
        std::set<const Found*> taken;
        for (bool more = true; more;) {
            more = false;
            for (const OtherSource& o : others)
                for (const Found& f : o.found) {
                    const std::string& name = f.name();
                    if (taken.contains(&f) || !used.contains(name) || module.functions.contains(name) || module.classes.contains(name) ||
                        module.properties.contains(name) || module.classDecls.contains(name))
                        continue;
                    // A private val or function only for what is taken from its own file (uses it there).
                    bool isPrivate = (f.property && f.property->isPrivate) || (f.function && f.function->isPrivate);
                    if (isPrivate && std::none_of(imported.begin(), imported.end(), [&](const auto& i) {
                            if (i.first != &o) return false;
                            std::set<std::string, std::less<>> its;
                            names(*i.second, its);
                            return its.contains(name);
                        }))
                        continue;
                    taken.insert(&f);
                    imported.emplace_back(&o, &f);
                    names(f, used);
                    more = true;
                }
        }
        for (auto [o, f] : imported) {
            if (f->function) module.functions[f->function->name] = &*f->function;
            else if (f->dataClass) module.classes[f->dataClass->name] = &*f->dataClass;
            else if (f->property) module.properties[f->property->name] = &*f->property;
            else if (f->classDecl) module.classDecls[f->classDecl->name] = &*f->classDecl;
            else if (f->object) module.objects.insert(f->object->name);
        }
    }
    Output out(lineFile);
    out.line(1);
    fs::path dir = source.parent_path();
    size_t done = 0;
    std::set<std::string, std::less<>> declared;  // the functions declared so far
    // The @Composable functions a definition uses, declared before it (their prototypes), of this
    // file or another, as late as can be (a parameter's type may be a struct of the file's declared
    // above the function, not at its top).
    auto declare = [&](const std::set<std::string, std::less<>>& used) {
        for (auto [o, g] : imported)
            if (g->function && g->function->composable && used.contains(g->function->name) && declared.insert(g->function->name).second) {
                out.setFile(o->lineFile);
                generatePrototype(out, *g->function, module);
            }
        out.setFile(lineFile);
        for (const Found& g : found)
            if (g.function && g.function->composable && used.contains(g.function->name) && declared.insert(g.function->name).second)
                generatePrototype(out, *g.function, module);
    };
    // A top-level val or a plain function (whose type may be its body's), defined where it is or
    // before the first definition that uses it (C++ reads in order, Kotlin doesn't), after what it
    // uses; one of another file, from its file.
    std::map<std::string, std::pair<std::string, const Found*>, std::less<>> early;
    for (const Found& g : found)
        if (g.property || g.classDecl || (g.function && !g.function->composable)) early[g.name()] = {lineFile, &g};
    for (auto [o, g] : imported)
        if (g->property || g->classDecl || (g->function && !g->function->composable)) early[g->name()] = {o->lineFile, g};
    std::set<std::string, std::less<>> defined;
    // (`where`: the qualifier of where it is defined, the namespace its calls name)
    auto define = [&](auto&& self, const std::string& name, const std::string& where) -> void {
        auto v = early.find(name);
        if (v == early.end() || !defined.insert(name).second) return;
        const Found& g = *v->second.second;
        module.qualifiers[name] = where;
        std::set<std::string, std::less<>> used;
        names(g, used);
        for (const std::string& u : used) self(self, u, where);
        declare(used);
        out.setFile(v->second.first);
        if (g.property) generateProperty(out, *g.property, module);
        else if (g.classDecl) generateClassDecl(out, *g.classDecl, module);
        else generateDefinition(out, *g.function, module);
        out.setFile(lineFile);
    };
    for (size_t k = 0; k < found.size(); ++k) {
        const Found& f = found[k];
        if (f.begin > done) {
            if (done > 0) out.line(positionOf(src, done).line);
            out.text(passThrough(std::string_view(src).substr(done, f.begin - done), dir));
        }
        if (k == 0) {
            // Before the first: the helpers the translations use, and the data classes, so that
            // every function may use them (as Kotlin's do, in any order).
            out.text("\n#include <kompoz/kotlin.hpp>\n");
            for (auto [o, g] : imported) {  // (from their own files: #line says which)
                out.setFile(o->lineFile);
                if (g->enumClass) generateEnum(out, *g->enumClass);
                if (g->dataClass) generateClass(out, *g->dataClass, module);
                if (g->object) generateObject(out, *g->object);
            }
            out.setFile(lineFile);
            for (const Found& g : found) {
                if (g.enumClass) generateEnum(out, *g.enumClass);
                if (g.dataClass) generateClass(out, *g.dataClass, module);
                if (g.object) generateObject(out, *g.object);
            }
        }
        if (f.property || f.classDecl || (f.function && !f.function->composable)) {
            define(define, f.name(), f.qualifier);
        } else if (f.function) {
            // The function itself declared, and what it uses declared or defined, before it.
            std::set<std::string, std::less<>> used;
            names(f, used);
            for (const std::string& u : used) define(define, u, f.qualifier);
            used.insert(f.function->name);
            declare(used);
            generateDefinition(out, *f.function, module);
        }
        done = f.end;
    }
    if (done < src.size()) {
        if (done > 0) out.line(positionOf(src, done).line);
        out.text(passThrough(std::string_view(src).substr(done), dir));
    }
    return std::move(out).str();
}

}  // namespace

int main(int argc, char** argv) {
    std::string input, output, depfile;
    std::vector<std::string> headerDirs, moduleSources;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) output = argv[++i];
        else if (a == "--headers" && i + 1 < argc) headerDirs.push_back(argv[++i]);
        else if (a == "--depfile" && i + 1 < argc) depfile = argv[++i];
        else if (a == "--module" && i + 1 < argc) moduleSources.push_back(argv[++i]);
        else if (input.empty()) input = a;
        else {
            std::cerr << "kompozc: unexpected argument " << a << "\n";
            return 2;
        }
    }
    if (input.empty() || output.empty()) {
        std::cerr << "usage: kompozc SOURCE -o OUTPUT [--headers DIR]... [--module OTHER_SOURCE]... [--depfile FILE]\n";
        return 2;
    }
    std::ifstream in(input, std::ios::binary);
    if (!in) {
        std::cerr << "kompozc: can't read " << input << "\n";
        return 1;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string src = buffer.str();
    std::string lineFile = fs::absolute(input).lexically_normal().generic_string();
    // The structs a call with named arguments may make: those of the headers in DIRs (Kompoz's),
    // and of the source's own quoted includes.
    Structs structs;
    for (const std::string& dir : headerDirs) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec))
            if (entry.path().extension() == ".hpp" || entry.path().extension() == ".h") structs.readHeader(entry.path());
    }
    structs.readIncludes(src, fs::path(input).parent_path());
    structs.readText(src);  // (the source's own; its Kotlin has no `struct X {`)
    std::string result;
    std::vector<OtherSource> others;
    try {
        // The module's other sources (the target's): their @Composable functions and data classes
        // are called here as Kotlin calls them across files.
        for (const std::string& other : moduleSources) {
            if (fs::equivalent(other, input)) continue;
            std::ifstream o(other, std::ios::binary);
            if (!o) continue;
            std::stringstream text;
            text << o.rdbuf();
            OtherSource os;
            os.lineFile = fs::absolute(other).lexically_normal().generic_string();
            std::string otherSrc = text.str();
            try {
                os.found = scan(otherSrc);
            } catch (const CompileError&) {
                continue;  // (its own translation says what is wrong in it)
            }
            others.push_back(std::move(os));
        }
        result = translate(src, fs::path(input), lineFile, structs, others);
    } catch (const CompileError& e) {
        std::cerr << lineFile << ":" << e.pos.line << ":" << e.pos.column << ": error: " << e.what() << "\n";
        return 1;
    }
    if (fs::path dir = fs::path(output).parent_path(); !dir.empty()) fs::create_directories(dir);
    // Written only when it changed: a change in another source of the module translates this one
    // again, and what it gives most often is the same (the C++ compiler then has nothing to do).
    bool same = false;
    if (std::ifstream old(output, std::ios::binary); old) {
        std::stringstream was;
        was << old.rdbuf();
        same = was.str() == result;
    }
    std::ofstream out;
    if (!same) {
        out.open(output, std::ios::binary);
        out << result;
    }
    if (!depfile.empty()) {  // the headers read: a change in one translates the source again
        std::ofstream dep(depfile, std::ios::binary);
        auto escaped = [](std::string p) {
            std::string e;
            for (char c : p) e += c == ' ' ? std::string("\\ ") : std::string(1, c);
            return e;
        };
        dep << escaped(output) << ":";
        for (const fs::path& h : structs.read) dep << " \\\n  " << escaped(h.generic_string());
        dep << "\n";
    }
    return same || out ? 0 : 1;
}
