// kompozc's translation of a source (translate.hpp).

#include "translate.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <fstream>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace kompozc {

namespace {
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
    if (k < src.size() && src[k] == '(') return true;  // enum class Level(val n: Int): Kotlin's constructor
    if (k >= src.size() || src[k] != '{') return false;
    // Its entries, to a ; or the }: names, commas and Kotlin's arguments, LOW(1), or bodies (C++'s have values, A = 1).
    int depth = 0;
    for (size_t i = k + 1; i < src.size(); ++i) {
        char c = src[i];
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        else if (depth > 0) continue;
        else if (c == ';' || c == '}') return true;
        else if (c == '{') return true;  // PLUS { override fun … }: an entry's body of its own
        else if (!identChar(c) && !std::isspace(static_cast<unsigned char>(c)) && c != ',') return false;
    }
    return false;
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
    bool generic = k < src.size() && src[k] == '<';
    if (generic) {  // class Box<T>: its type parameters (C++'s class X<int> { is a specialization: its body tells)
        int depth = 0;
        for (; k < src.size() && src[k] != '\n'; ++k)
            if (src[k] == '<') ++depth;
            else if (src[k] == '>' && --depth == 0) {
                ++k;
                break;
            }
        while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
    }
    if (k >= src.size()) return false;
    if (src[k] == '(') return true;
    if (src[k] == ':') {
        // A base: Kotlin's is constructed, Base(…); C++'s has an access (public Base). Interfaces alone
        // (: A, B): a body as Kotlin's below, or none.
        size_t brace = src.find_first_of("{\n", k);
        std::string_view bases = src.substr(k, (brace == std::string_view::npos ? src.size() : brace) - k);
        if (bases.find('(') != std::string_view::npos) return true;
        for (std::string_view access : {"public", "private", "protected", "virtual"}) {
            for (size_t at = bases.find(access); at != std::string_view::npos; at = bases.find(access, at + 1))
                if (!identChar(bases[at - 1]) && (at + access.size() >= bases.size() || !identChar(bases[at + access.size()]))) return false;
        }
        if (brace == std::string_view::npos || src[brace] == '\n') return true;  // (a class without a body: C++'s has one, or ;)
        k = brace;
    }
    if (src[k] != '{') return false;
    ++k;
    while (k < src.size() && std::isspace(static_cast<unsigned char>(src[k]))) ++k;  // (the body may start on the next line)
    std::string_view first = word();
    if (first == "private" || first == "public" || first == "internal" || first == "protected") {
        if (k < src.size() && src[k] == ':') return false;  // C++'s access label
        first = word();
    }
    static const std::set<std::string_view> kotlin = {"val",       "var",    "fun",       "init",   "override", "open",   "abstract", "suspend",
                                                      "lateinit",  "data",   "companion", "object", "interface", "sealed", "inner",    "const",
                                                      "operator", "vararg"};
    if (kotlin.contains(first)) return true;
    size_t firstAt = k - first.size();
    if (first == "class") return startsClass(src, firstAt);  // (a nested class: Kotlin's if it is one)
    if (first == "enum" && startsEnumClass(src, firstAt)) {
        // Kotlin's nested enum (C++'s ends with };): a constructor, or no ; after its body
        size_t open = src.find_first_of("({", firstAt);
        if (open != std::string_view::npos && src[open] == '(') return true;
        int depth = 0;
        for (size_t i = open; i < src.size(); ++i) {
            if (src[i] == '{') ++depth;
            else if (src[i] == '}' && --depth == 0) {
                size_t after = src.find_first_not_of(" \t", i + 1);
                return after == std::string_view::npos || src[after] != ';';
            }
        }
    }
    return false;
}

// Whether Kotlin's sealed class or interface starts at `at` (C++ has no `sealed`).
bool startsSealed(std::string_view src, size_t at) {
    if (src.substr(at, 6) != "sealed") return false;
    size_t k = at + 6;
    if (k >= src.size() || !std::isspace(static_cast<unsigned char>(src[k]))) return false;
    while (k < src.size() && std::isspace(static_cast<unsigned char>(src[k]))) ++k;
    return (src.substr(k, 5) == "class" && k + 5 < src.size() && !identChar(src[k + 5])) ||
           (src.substr(k, 9) == "interface" && k + 9 < src.size() && !identChar(src[k + 9]));
}

// Whether Kotlin's interface starts at `at`: [private | internal | public]... [fun] interface Name (C++ has no `interface`).
bool startsInterface(std::string_view src, size_t at) {
    size_t k = at;
    auto word = [&] {
        while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
        size_t b = k;
        while (k < src.size() && identChar(src[k])) ++k;
        return src.substr(b, k - b);
    };
    std::string_view w = word();
    while (w == "private" || w == "internal" || w == "public") w = word();
    if (w == "fun") w = word();
    return w == "interface" && !word().empty();
}

// Whether Kotlin's extension property starts at `at`: [private]… val|var [<T>] Receiver.name (C++ has no `val`).
bool startsExtensionProperty(std::string_view src, size_t at) {
    size_t k = at;
    auto word = [&] {
        while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
        size_t b = k;
        while (k < src.size() && identChar(src[k])) ++k;
        return src.substr(b, k - b);
    };
    std::string_view w = word();
    while (w == "private" || w == "internal" || w == "public") w = word();
    if ((w != "val" && w != "var") || k >= src.size() || (src[k] != ' ' && src[k] != '\t')) return false;
    while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
    if (k < src.size() && src[k] == '<') return true;  // val <T> List<T>.second
    if (word().empty()) return false;
    return k < src.size() && (src[k] == '.' || src[k] == '<');
}

// Whether Kotlin's typealias starts at `at` (C++ has no `typealias`).
bool startsAlias(std::string_view src, size_t at) {
    size_t k = at;
    auto word = [&] {
        while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
        size_t b = k;
        while (k < src.size() && identChar(src[k])) ++k;
        return src.substr(b, k - b);
    };
    std::string_view w = word();
    while (w == "private" || w == "internal" || w == "public") w = word();
    return w == "typealias" && !word().empty();
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
// 'f' a function (@Preview @Kompozable fun, @Test fun), ' ' none (annotations in C++ don't exist).
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
    if (w == "sealed") return 's';
    if (w == "interface" || (w == "fun" && startsInterface(src, b))) return 'i';
    if (w == "suspend") return 'f';
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

// Whether a plain (not @Kompozable) function starts at `at`: [private | internal | public | inline]...
// fun name or fun Receiver.name (C++ has no `fun`).
bool startsFun(std::string_view src, size_t at) {
    size_t k = at;
    for (;;) {
        while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
        size_t b = k;
        while (k < src.size() && identChar(src[k])) ++k;
        std::string_view w = src.substr(b, k - b);
        if (w == "private" || w == "internal" || w == "public" || w == "inline" || w == "suspend" || w == "operator" || w == "infix") continue;
        if (w != "fun") return false;
        while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
        if (k < src.size() && src[k] == '<') {  // fun <T> name: its type parameters
            int depth = 0;
            for (; k < src.size() && src[k] != '\n'; ++k)
                if (src[k] == '<') ++depth;
                else if (src[k] == '>' && --depth == 0) {
                    ++k;
                    break;
                }
            while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) ++k;
        }
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


// Whether the C++'s first use of `name`, at `use`, is a prototype of the file's own (void Name(…);):
// the statement around it ends in a ; with no block, and the name is called right after its type.
// Such a file has declared the function itself (with its defaults): kompozc leaves it to it.
bool declaredInCpp(std::string_view src, const std::vector<Boundary>& boundaries, size_t use, std::string_view name) {
    size_t begin = 0;
    for (const Boundary& b : boundaries)
        if (b.at <= use && b.at >= begin) begin = b.at;
    size_t end = src.find_first_of(";{", use);
    if (end == std::string_view::npos || src[end] != ';') return false;
    std::string before;  // (the statement's text before the name, its comments left out)
    for (size_t i = begin; i < use;) {
        if (src[i] == '/' && i + 1 < use && src[i + 1] == '/') {
            while (i < use && src[i] != '\n') ++i;
        } else if (src[i] == '/' && i + 1 < use && src[i + 1] == '*') {
            size_t e = src.find("*/", i + 2);
            i = e == std::string_view::npos || e + 2 > use ? use : e + 2;
        } else {
            before += src[i++];
        }
    }
    if (before.find_first_of("({=") != std::string::npos) return false;  // (a call, an initializer: not a declaration)
    size_t after = use + name.size();
    while (after < end && std::isspace(static_cast<unsigned char>(src[after]))) ++after;
    return after < end && src[after] == '(';
}

}  // namespace

namespace {
// Where a declaration that can't be read ends: after the line its block closes on at the left edge
// (`}`), or before the next line that starts there with something else.
size_t guessedEnd(std::string_view src, size_t at) {
    size_t line = src.find('\n', at);
    while (line != std::string_view::npos && line + 1 < src.size()) {
        char first = src[line + 1];
        if (first == '}') {
            size_t eol = src.find('\n', line + 1);
            return eol == std::string_view::npos ? src.size() : eol;
        }
        if (!std::isspace(static_cast<unsigned char>(first)) && first != ')') return line;
        line = src.find('\n', line + 1);
    }
    return src.size();
}
}  // namespace

// Walks the C++ around the functions: skips comments, strings, characters and raw strings, and
// finds each @Kompozable.
std::vector<Found> scan(std::string_view src, std::vector<Boundary>* boundaries, std::vector<CompileError>* errors) {
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
    auto boundary = [&](size_t at) {
        if (boundaries && depth == (namespaces.empty() ? 0 : namespaces.back().second)) boundaries->push_back({at, qualifier()});
    };
    size_t i = 0;
    while (i < src.size()) {
        size_t declarationAt = i;
        try {
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
                else if (kind == 's') f.sealed = parser.sealedDecl();
                else if (kind == 'i') f.interface = parser.interfaceDecl();
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
                f.end = f.dataClass ? f.dataClass->end.offset : f.object ? f.object->end.offset : f.sealed ? f.sealed->end.offset
                        : f.interface ? f.interface->end.offset : f.classDecl ? f.classDecl->end.offset : f.function->end.offset;
                f.qualifier = qualifier();
                found.push_back(std::move(f));
                i = f.end;
            } else if (c == '@' && src.substr(i + 1, 10) == "Kompozable" && (i + 11 >= src.size() || !identChar(src[i + 11]))) {
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
                } else if (src.substr(k, 6) == "object" && (k + 6 >= src.size() || !identChar(src[k + 6]))) {  // data object
                    Parser parser(src, positionOf(src, i));
                    Found f;
                    f.object = parser.objectDecl();
                    f.begin = i, f.end = f.object->end.offset, f.qualifier = qualifier();
                    i = f.end;
                    found.push_back(std::move(f));
                } else {
                    i += 4;
                }
            } else if (c == 's' && (i == 0 || !identChar(src[i - 1])) && startsLine(src, i) && startsSealed(src, i)) {
                Parser parser(src, positionOf(src, i));
                Found f;
                f.sealed = parser.sealedDecl();
                f.begin = i, f.end = f.sealed->end.offset, f.qualifier = qualifier();
                i = f.end;
                found.push_back(std::move(f));
            } else if (identChar(c) && (i == 0 || !identChar(src[i - 1])) && startsLine(src, i) && startsExtensionProperty(src, i)) {
                Parser parser(src, positionOf(src, i));
                Function f = parser.extensionProperty();
                size_t end = f.end.offset;
                found.push_back({std::move(f), std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, i, end, qualifier()});
                i = end;
            } else if (identChar(c) && (i == 0 || !identChar(src[i - 1])) && startsLine(src, i) && startsAlias(src, i)) {
                Parser parser(src.substr(0, valEnd(src, i)), positionOf(src, i));  // (its type ends with its line)
                Found f;
                f.alias = parser.typeAlias();
                f.begin = i, f.end = f.alias->end.offset, f.qualifier = qualifier();
                i = f.end;
                found.push_back(std::move(f));
            } else if (identChar(c) && (i == 0 || !identChar(src[i - 1])) && startsLine(src, i) && startsInterface(src, i)) {
                Parser parser(src, positionOf(src, i));
                Found f;
                f.interface = parser.interfaceDecl();
                f.begin = i, f.end = f.interface->end.offset, f.qualifier = qualifier();
                i = f.end;
                found.push_back(std::move(f));
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
                    boundary(i + 1);
                } else if (c == ';') {
                    boundary(i + 1);
                }
                ++i;
            }
        } catch (const CompileError& e) {
            if (!errors) throw;
            errors->push_back(e);
            Found f;
            f.failed = true;
            f.begin = declarationAt;
            f.end = guessedEnd(src, declarationAt);
            f.qualifier = qualifier();
            i = f.end;
            found.push_back(std::move(f));
        }
    }
    // A sealed class's subclasses at the top level of the file: its variant's alternatives after its nested ones.
    for (Found& s : found)
        if (s.sealed)
            for (const Found& f : found) {
                static const std::vector<Type> none;
                const std::vector<Type>& supers = f.dataClass ? f.dataClass->supers : f.object ? f.object->supers : none;
                if (namesSuper(supers, s.sealed->name)) s.sealed->topLevel.push_back(f.name());
            }
    return found;
}

// A body's nested declarations, by their dotted names (Outer.Inner). (Inside it, by their own: codegen's Module copy.)
void declareNested(Module& module, const std::string& outer, const NestedDecls& n) {
    auto both = [&](auto& map, const std::string& name, auto value) { map[outer + "." + name] = value; };
    for (const auto& c : n.classes) {
        both(module.classDecls, c->name, c.get());
        declareNested(module, outer + "." + c->name, c->nested);
        if (c->companion) module.objectDecls[outer + "." + c->name] = c->companion.get();
    }
    for (const auto& d : n.dataClasses) {
        both(module.classes, d->name, d.get());
        declareNested(module, outer + "." + d->name, d->nested);
    }
    for (const auto& e : n.enums) both(module.enums, e->name, e.get());
    for (const auto& o : n.objects) {
        module.objects.insert(outer + "." + o->name);
        both(module.objectDecls, o->name, o.get());
        declareNested(module, outer + "." + o->name, o->nested);
    }
    for (const auto& i : n.interfaces) {
        both(module.interfaces, i->name, i.get());
        declareNested(module, outer + "." + i->name, i->nested);
    }
    for (const auto& c : n.innerClasses) {  // (made through an outer object: outer.Inner(…), by its name alone)
        auto& outers = module.innerClasses[c->name];
        if (std::none_of(outers.begin(), outers.end(), [&](const auto& o) { return o.second == c.get(); })) outers.emplace_back(outer, c.get());
        declareNested(module, outer + "." + c->name, c->nested);
    }
}

void declareIn(Module& module, const Found& f, bool qualified) {
    if (f.failed) return;
    if (f.classDecl) declareNested(module, f.name(), f.classDecl->nested);
    if (f.dataClass) declareNested(module, f.name(), f.dataClass->nested);
    if (f.object) declareNested(module, f.name(), f.object->nested);
    if (f.interface) declareNested(module, f.name(), f.interface->nested);
    // A companion object: Name.member is the class's own (read through a function where it is one of an object's).
    const ObjectDecl* companion = f.classDecl ? f.classDecl->companion.get() : f.dataClass ? f.dataClass->companion.get()
                                  : f.interface ? f.interface->companion.get() : nullptr;
    if (companion) module.objectDecls[f.name()] = companion;
    if (qualified && f.function && f.function->composable) module.qualifiers[f.function->name] = f.qualifier;
    // (a class template is named with its namespace where it is made: inside itself its name alone is the type it is)
    if (qualified && ((f.dataClass && !f.dataClass->typeParams.empty()) || (f.classDecl && !f.classDecl->typeParams.empty())))
        module.qualifiers[f.name()] = f.qualifier;
    if (f.enumClass) module.enums[f.enumClass->name] = &*f.enumClass;
    if (f.alias) module.aliases[f.alias->name] = &*f.alias;
    if (f.function && f.function->isExtensionProperty) {
        module.extensionProperties[f.function->name] = &*f.function;
        return;
    }
    if (f.function) {  // (overloads: each once)
        auto& overloads = module.functions[f.function->name];
        if (std::find(overloads.begin(), overloads.end(), &*f.function) == overloads.end()) overloads.push_back(&*f.function);
    }
    else if (f.dataClass) module.classes[f.dataClass->name] = &*f.dataClass;
    else if (f.property) module.properties[f.property->name] = &*f.property;
    else if (f.classDecl) module.classDecls[f.classDecl->name] = &*f.classDecl;
    else if (f.object) {
        module.objects.insert(f.object->name);
        module.objectDecls[f.object->name] = &*f.object;
    } else if (f.interface) module.interfaces[f.interface->name] = &*f.interface;
    else if (f.sealed) {  // its nested subclasses, by their full names (two sealed classes may each have a Loading)
        for (const DataClass& d : f.sealed->dataClasses) module.classes[f.sealed->name + "." + d.name] = &d;
        for (const ObjectDecl& o : f.sealed->objects) module.objects.insert(f.sealed->name + "." + o.name);
    }
}

namespace {
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
    if (n->kind == Node::Member) {  // x.shout, x.second(): an extension property's getter, an extension function
        out.insert(n->text);
        out.insert("kompoz_get_" + n->text);
    }
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
void properties(const ClassProperty& p, std::set<std::string, std::less<>>& out) {
    if (p.type) typeNames(*p.type, out);
    names(p.init.get(), out);
    names(p.getter.get(), out);
    names(p.setter.get(), out);
}
using NameSet = std::set<std::string, std::less<>>;
void functionNames(const Function& m, NameSet& out) {
    for (const Param& p : m.params) {
        typeNames(p.type, out);
        names(p.defaultValue.get(), out);
    }
    if (m.result) typeNames(*m.result, out);
    if (m.receiver) typeNames(*m.receiver, out);
    names(m.body.get(), out);
    if (m.setter) functionNames(*m.setter, out);
}
void nestedNames(const NestedDecls& n, NameSet& out);
void objectNames(const ObjectDecl* o, NameSet& out) {
    if (!o) return;
    for (const Type& t : o->supers) typeNames(t, out);
    for (const ClassProperty& p : o->properties) properties(p, out);
    for (const Function& m : o->methods) functionNames(m, out);
    for (const NodeP& b : o->initBlocks) names(b.get(), out);
    nestedNames(o->nested, out);
}
void interfaceNames(const InterfaceDecl& i, NameSet& out) {
    for (const Type& t : i.bases) typeNames(t, out);
    for (const ClassProperty& p : i.properties) properties(p, out);
    for (const Function& m : i.methods) functionNames(m, out);
    objectNames(i.companion.get(), out);
    nestedNames(i.nested, out);
}
void enumNames(const EnumClass& e, NameSet& out) {  // (its entries are its own; a constructor's and a body's names)
    for (const Param& p : e.ctorParams) {
        typeNames(p.type, out);
        names(p.defaultValue.get(), out);
    }
    for (const auto& args : e.entryArgs)
        for (const NodeP& a : args) names(a.get(), out);
    for (const ClassProperty& p : e.properties) properties(p, out);
    for (const Function& m : e.methods) functionNames(m, out);
    for (const NodeP& b : e.initBlocks) names(b.get(), out);
    objectNames(e.companion.get(), out);
    for (const auto& b : e.entryBodies)
        if (b) {
            for (const ClassProperty& p : b->properties) properties(p, out);
            for (const Function& m : b->methods) functionNames(m, out);
        }
}
void classNames(const ClassDecl& c, NameSet& out) {
    for (const Param& p : c.ctorParams) {
        typeNames(p.type, out);
        names(p.defaultValue.get(), out);
    }
    if (c.base) typeNames(*c.base, out);
    for (const Type& t : c.interfaces) typeNames(t, out);
    for (const NodeP& a : c.baseArgs) names(a.get(), out);
    for (const ClassProperty& p : c.properties) properties(p, out);
    for (const Function& m : c.methods) functionNames(m, out);
    for (const NodeP& b : c.initBlocks) names(b.get(), out);
    objectNames(c.companion.get(), out);
    nestedNames(c.nested, out);
}
void dataClassNames(const DataClass& d, NameSet& out) {
    for (const Type& t : d.supers) typeNames(t, out);
    for (const Param& p : d.fields) {
        typeNames(p.type, out);
        names(p.defaultValue.get(), out);
    }
    for (const ClassProperty& p : d.properties) properties(p, out);
    for (const Function& m : d.methods) functionNames(m, out);
    for (const NodeP& b : d.initBlocks) names(b.get(), out);
    objectNames(d.companion.get(), out);
    nestedNames(d.nested, out);
}
void nestedNames(const NestedDecls& n, NameSet& out) {
    for (const auto& c : n.classes) classNames(*c, out);
    for (const auto& d : n.dataClasses) dataClassNames(*d, out);
    for (const auto& e : n.enums) enumNames(*e, out);
    for (const auto& o : n.objects) objectNames(o.get(), out);
    for (const auto& i : n.interfaces) interfaceNames(*i, out);
    for (const auto& c : n.innerClasses) classNames(*c, out);
}
void names(const Found& f, NameSet& out) {
    if (f.function) {
        functionNames(*f.function, out);
    } else if (f.property) {
        if (f.property->decl->type) typeNames(*f.property->decl->type, out);
        names(f.property->decl.get(), out);
    } else if (f.object) {  // (an object without members is its name and its interfaces)
        objectNames(&*f.object, out);
    } else if (f.sealed) {  // its subclasses: those in it, and those of its file (taken with it)
        for (const DataClass& d : f.sealed->dataClasses) dataClassNames(d, out);
        for (const ObjectDecl& o : f.sealed->objects) objectNames(&o, out);
        for (const std::string& t : f.sealed->topLevel) out.insert(t);
    } else if (f.interface) {
        interfaceNames(*f.interface, out);
    } else if (f.enumClass) {
        enumNames(*f.enumClass, out);
    } else if (f.classDecl) {
        classNames(*f.classDecl, out);
    } else if (f.dataClass) {
        dataClassNames(*f.dataClass, out);
    } else if (f.alias) {
        typeNames(f.alias->type, out);
    }
}

// Where a file's leading lines end: its comments, #includes, #pragmas and using-directives, after
// which what it calls can be declared (a C++ file's prototypes would stand there).
size_t includesEnd(std::string_view src) {
    size_t at = 0;
    bool inComment = false;
    while (at < src.size()) {
        size_t eol = src.find('\n', at);
        if (eol == std::string_view::npos) eol = src.size();
        std::string_view line = src.substr(at, eol - at);
        size_t b = line.find_first_not_of(" \t\r");
        std::string_view t = b == std::string_view::npos ? std::string_view() : line.substr(b);
        if (inComment) {
            if (t.find("*/") != std::string_view::npos) inComment = false;
        } else if (t.starts_with("/*")) {
            if (t.find("*/", 2) == std::string_view::npos) inComment = true;
        } else if (!(t.empty() || t.starts_with("//") || t.starts_with("#") || t.starts_with("using "))) {
            return at;
        }
        at = eol + 1;
    }
    return src.size();
}

}  // namespace

std::string translate(const std::string& src, const fs::path& source, const std::string& lineFile, const Structs& structs,
                      const std::vector<OtherSource>& others) {
    std::vector<Boundary> boundaries;
    std::vector<Found> found = scan(src, &boundaries);
    Module module;
    module.structs = &structs;
    for (const Found& f : found) declareIn(module, f, true);
    // The other sources' declarations this one uses, and those they use in turn (a data class in
    // a parameter's type): declared here too, as C++ needs (Kotlin's module needs nothing).
    // The identifiers in this file's C++ (outside its Kotlin, its comments and strings), each where
    // it is first used: the module's functions among them are declared before that (below), so
    // they are imported too.
    std::set<std::string, std::less<>> cppIdentifiers;
    std::map<std::string, size_t, std::less<>> cppFirstUse;
    {
        auto scanText = [&](size_t from, size_t to) {
            for (size_t i = from; i < to;) {
                char c = src[i];
                if (c == '/' && i + 1 < to && src[i + 1] == '/') {
                    while (i < to && src[i] != '\n') ++i;
                } else if (c == '/' && i + 1 < to && src[i + 1] == '*') {
                    size_t e = src.find("*/", i + 2);
                    i = e == std::string::npos || e + 2 > to ? to : e + 2;
                } else if (c == '"' || (c == '\'' && !(i > 0 && std::isxdigit(static_cast<unsigned char>(src[i - 1]))))) {
                    for (++i; i < to && src[i] != c; ++i)
                        if (src[i] == '\\') ++i;
                    ++i;
                } else if (!identChar(c)) {
                    ++i;
                } else {
                    size_t b = i;
                    while (i < to && identChar(src[i])) ++i;
                    if (cppIdentifiers.insert(src.substr(b, i - b)).second) cppFirstUse[src.substr(b, i - b)] = b;
                }
            }
        };
        size_t at = 0;
        for (const Found& f : found) {
            scanText(at, f.begin);
            at = f.end;
        }
        scanText(at, src.size());
    }
    std::vector<std::pair<const OtherSource*, const Found*>> imported;
    {
        std::set<std::string, std::less<>> used;
        for (const Found& f : found) names(f, used);
        for (const OtherSource& o : others)
            for (const Found& f : o.found)
                if (cppIdentifiers.contains(f.name())) used.insert(f.name());
        std::set<const Found*> taken;
        for (bool more = true; more;) {
            more = false;
            for (const OtherSource& o : others)
                for (const Found& f : o.found) {
                    const std::string& name = f.name();
                    // (a function of a name defined here too: an overload of it, unless it takes the same parameters)
                    auto here = module.functions.find(name);
                    bool defined = here != module.functions.end() &&
                                   (!f.function || std::any_of(here->second.begin(), here->second.end(),
                                                               [&](const Function* g) { return sameParams(*g, *f.function); }));
                    if (taken.contains(&f) || !used.contains(name) || defined || module.classes.contains(name) ||
                        module.properties.contains(name) || module.classDecls.contains(name) || module.interfaces.contains(name))
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
        for (auto [o, f] : imported) declareIn(module, *f, false);
    }
    Output out(lineFile);
    out.line(1);
    fs::path dir = source.parent_path();
    size_t done = 0;
    std::set<const Function*> declared;  // the functions declared so far
    // The @Kompozable functions a definition uses, declared before it (their prototypes), of this
    // file or another, as late as can be (a parameter's type may be a struct of the file's declared
    // above the function, not at its top).
    auto declare = [&](const std::set<std::string, std::less<>>& used) {
        for (auto [o, g] : imported)
            if (g->function && g->function->composable && used.contains(g->function->name) && declared.insert(&*g->function).second) {
                out.setFile(o->lineFile);
                generatePrototype(out, *g->function, module);
            }
        out.setFile(lineFile);
        for (const Found& g : found)
            if (g.function && g.function->composable && used.contains(g.function->name) && declared.insert(&*g.function).second)
                generatePrototype(out, *g.function, module);
    };
    // A top-level val or a plain function (whose type may be its body's), defined where it is or
    // before the first definition that uses it (C++ reads in order, Kotlin doesn't), after what it
    // uses; one of another file, from its file.
    std::map<std::string, std::vector<std::pair<std::string, const Found*>>, std::less<>> early;  // (a name's overloads together)
    auto isEarly = [](const Found& g) {
        return g.property || g.classDecl || g.interface || (g.function && !g.function->composable) || (g.object && g.object->hasMembers());
    };
    for (const Found& g : found)
        if (isEarly(g)) early[g.name()].emplace_back(lineFile, &g);
    for (auto [o, g] : imported)
        if (isEarly(*g)) early[g->name()].emplace_back(o->lineFile, g);
    std::set<std::string, std::less<>> defined;
    // (`where`: the qualifier of where it is defined, the namespace its calls name)
    auto define = [&](auto&& self, const std::string& name, const std::string& where) -> void {
        auto v = early.find(name);
        if (v == early.end() || !defined.insert(name).second) return;
        module.qualifiers[name] = where;
        std::set<std::string, std::less<>> used;
        for (auto& [file, g] : v->second) names(*g, used);
        for (const std::string& u : used) self(self, u, where);
        declare(used);
        for (auto& [file, gp] : v->second) {
            const Found& g = *gp;
            out.setFile(file);
            if (g.property) generateProperty(out, *g.property, module);
            else if (g.classDecl) generateClassDecl(out, *g.classDecl, module);
            else if (g.interface) generateInterface(out, *g.interface, module);
            else if (g.object) generateObject(out, *g.object, module);
            else generateDefinition(out, *g.function, module);
        }
        out.setFile(lineFile);
    };
    // Before the first declaration: the helpers the translations use, and the data classes, enums
    // and objects without members, so that every function may use them (as Kotlin's do, in any
    // order). (An object with members is defined as a val is: before what uses it, after what it uses.)
    bool preludeDone = false;
    auto prelude = [&] {
        if (std::exchange(preludeDone, true)) return;
        out.text("\n#include <kompoz/kotlin.hpp>\n");
        // Interfaces declared first: a data class or an object implementing one names it (its
        // object's type is made only where one is converted to it, after the interface's definition).
        for (auto [o, g] : imported)
            if (g->interface) declareInterface(out, *g->interface);
        for (const Found& g : found)
            if (g.interface) declareInterface(out, *g.interface);
        // Type aliases (enums first: an alias may name one), the types they may name declared before them.
        bool anyAlias = std::any_of(found.begin(), found.end(), [](const Found& g) { return g.alias.has_value(); }) ||
                        std::any_of(imported.begin(), imported.end(), [](const auto& i) { return i.second->alias.has_value(); });
        if (anyAlias) {
            auto declareTypes = [&](const Found& g) {
                if (g.enumClass) generateEnum(out, *g.enumClass, module);
                if (g.dataClass) declareType(out, g.dataClass->name, g.dataClass->typeParams, false);
                if (g.classDecl) declareType(out, g.classDecl->name, g.classDecl->typeParams, true);
                if (g.sealed) declareType(out, g.sealed->name, {}, false);
            };
            for (auto [o, g] : imported) {
                out.setFile(o->lineFile);
                declareTypes(*g);
            }
            out.setFile(lineFile);
            for (const Found& g : found) declareTypes(g);
            for (auto [o, g] : imported)
                if (g->alias) {
                    out.setFile(o->lineFile);
                    generateAlias(out, *g->alias, module);
                }
            out.setFile(lineFile);
            for (const Found& g : found)
                if (g.alias) generateAlias(out, *g.alias, module);
        }
        for (auto [o, g] : imported) {  // (from their own files: #line says which)
            out.setFile(o->lineFile);
            if (g->enumClass && !anyAlias) generateEnum(out, *g->enumClass, module);
            if (g->dataClass) generateClass(out, *g->dataClass, module);
            if (g->object && !g->object->hasMembers()) generateObject(out, *g->object, module);
        }
        out.setFile(lineFile);
        for (const Found& g : found) {
            if (g.enumClass && !anyAlias) generateEnum(out, *g.enumClass, module);
            if (g.dataClass) generateClass(out, *g.dataClass, module);
            if (g.object && !g.object->hasMembers()) generateObject(out, *g.object, module);
        }
        // Sealed classes after their subclasses (the variant's alternatives are complete types).
        for (auto [o, g] : imported)
            if (g->sealed) {
                out.setFile(o->lineFile);
                generateSealed(out, *g->sealed, module);
            }
        out.setFile(lineFile);
        for (const Found& g : found)
            if (g.sealed) generateSealed(out, *g.sealed, module);
    };
    // The module's functions, classes and vals that this file's C++ (not its Kotlin) names before
    // they are defined here (or defined in another file): declared where the statement using them
    // first begins, as a prototype of their own would be (after the file's own types, which a
    // parameter may be of): a C++ file of the target calling Section(…), written in Kotlin's
    // syntax in another file.
    std::set<std::string, std::less<>> cppUsed;
    size_t declareAt = std::string::npos;
    std::string declareWhere = "::";
    {
        size_t first = std::string::npos;
        for (const std::string& w : cppIdentifiers) {
            // (a data class or object named there wants the prelude by then; it needs no declaration of its own)
            if (!(module.functions.contains(w) || module.classDecls.contains(w) || module.properties.contains(w) || module.classes.contains(w) ||
                  module.objects.contains(w) || module.interfaces.contains(w)))
                continue;
            size_t use = cppFirstUse.at(w);
            bool definedBefore = false;
            for (const Found& f : found)
                if (f.name() == w && f.begin < use) definedBefore = true;
            if (definedBefore || declaredInCpp(src, boundaries, use, w)) continue;
            cppUsed.insert(w);
            first = std::min(first, use);
        }
        if (!cppUsed.empty()) {
            declareAt = std::min(includesEnd(src), found.empty() ? src.size() : found.front().begin);
            for (const Boundary& b : boundaries) {
                bool inside = false;  // (not within a Kotlin declaration's text)
                for (const Found& f : found) inside = inside || (f.begin < b.at && b.at < f.end);
                if (b.at <= first && b.at >= declareAt && !inside) declareAt = b.at, declareWhere = b.qualifier;
            }
        }
    }
    // The C++ up to `to` passed through, the declarations above where they go.
    auto passUntil = [&](size_t to) {
        if (declareAt != std::string::npos && declareAt >= done && declareAt <= to) {
            if (declareAt > done) {
                if (done > 0) out.line(positionOf(src, done).line);
                out.text(passThrough(std::string_view(src).substr(done, declareAt - done), dir));
            }
            prelude();
            for (const std::string& u : cppUsed) define(define, u, declareWhere);
            declare(cppUsed);
            done = declareAt;
            declareAt = std::string::npos;
        }
        if (to > done) {
            if (done > 0) out.line(positionOf(src, done).line);
            out.text(passThrough(std::string_view(src).substr(done, to - done), dir));
            done = to;
        }
    };
    for (size_t k = 0; k < found.size(); ++k) {
        const Found& f = found[k];
        passUntil(f.begin);
        if (k == 0) prelude();
        if (isEarly(f)) {
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
    passUntil(src.size());
    return std::move(out).str();
}


Structs readStructs(const std::vector<std::string>& headerDirs, const std::vector<std::string>& includeDirs) {
    Structs structs;
    for (const std::string& dir : includeDirs) structs.includeDirs.emplace_back(dir);
    for (const std::string& dir : headerDirs) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec))
            if (entry.path().extension() == ".hpp" || entry.path().extension() == ".h") structs.readHeader(entry.path());
    }
    return structs;
}

std::vector<OtherSource> readOthers(const std::vector<std::string>& moduleSources, const fs::path& input,
                                    const std::function<std::optional<std::string>(const fs::path&)>& textOf) {
    std::vector<OtherSource> others;
    for (const std::string& other : moduleSources) {
        std::error_code ec;
        if (fs::equivalent(other, input, ec)) continue;
        std::optional<std::string> text = textOf ? textOf(fs::path(other)) : std::nullopt;
        if (!text) {
            std::ifstream o(other, std::ios::binary);
            if (!o) continue;
            std::stringstream read;
            read << o.rdbuf();
            text = read.str();
        }
        OtherSource os;
        os.lineFile = fs::absolute(other).lexically_normal().generic_string();
        try {
            os.found = scan(*text);
        } catch (const CompileError&) {
            continue;  // (its own translation says what is wrong in it)
        }
        others.push_back(std::move(os));
    }
    return others;
}

std::string maskedView(std::string_view src, const std::vector<Found>& found, const Structs& structs) {
    Module module;
    module.structs = &structs;
    for (const Found& f : found) declareIn(module, f, true);
    std::string out;
    size_t done = 0;
    bool prelude = false;
    std::string interfaces;  // (declared with the #include: a data class implementing one may come first)
    for (const Found& f : found)
        if (f.interface) {
            Output o("");
            declareInterface(o, *f.interface);
            std::istringstream lines(std::move(o).str());
            for (std::string line; std::getline(lines, line);)
                if (line.find("#line") == std::string::npos) interfaces += ' ' + line;
        }
    // A sealed class with the last of its subclasses that come after it in the file (its variant's are complete there).
    std::map<size_t, size_t> sealedAt;  // a sealed class's index -> where it goes
    for (size_t k = 0; k < found.size(); ++k)
        if (found[k].sealed && !found[k].failed)
            for (size_t j = k + 1; j < found.size(); ++j)
                if ((found[j].dataClass || found[j].object) &&
                    std::find(found[k].sealed->topLevel.begin(), found[k].sealed->topLevel.end(), found[j].name()) != found[k].sealed->topLevel.end())
                    sealedAt[k] = j;
    std::map<size_t, std::string> deferred;  // what goes with each
    std::string pending;  // what single-line declarations need, not placed yet
    for (size_t index = 0; index < found.size(); ++index) {
        const Found& f = found[index];
        out.append(src.substr(done, f.begin - done));
        // What C++ needs of it, on one line: its #line directives left out, its lines joined.
        std::string cpp;
        if (!f.failed) {
            Output o("");
            if (f.function && f.function->composable) generatePrototype(o, *f.function, module);
            else if (f.function) generateDefinition(o, *f.function, module);
            else if (f.dataClass) generateClass(o, *f.dataClass, module);
            else if (f.property) generateProperty(o, *f.property, module);
            else if (f.classDecl) generateClassDecl(o, *f.classDecl, module);
            else if (f.object) generateObject(o, *f.object, module);
            else if (f.sealed) generateSealed(o, *f.sealed, module);
            else if (f.interface) generateInterface(o, *f.interface, module);
            else if (f.alias) generateAlias(o, *f.alias, module);
            else if (f.enumClass) generateEnum(o, *f.enumClass, module);
            std::string text = std::move(o).str();
            std::istringstream lines(text);
            for (std::string line; std::getline(lines, line);) {
                size_t b = line.find_first_not_of(" \t");
                if (b != std::string::npos && line.compare(b, 5, "#line") == 0) continue;
                cpp += line + ' ';
            }
            if (auto later = sealedAt.find(index); later != sealedAt.end()) deferred[later->second] += std::exchange(cpp, std::string());
        }
        if (auto with = deferred.find(index); with != deferred.end()) cpp += with->second;
        // On its lines but the last (what follows it there keeps its columns): kotlin.hpp's #include
        // before the first, as the translation has it, then what C++ needs of it, the rest blank.
        std::string_view text = src.substr(f.begin, f.end - f.begin);
        size_t lineCount = size_t(std::count(text.begin(), text.end(), '\n'));  // (the lines before its last)
        std::vector<std::string> payloads;
        if (lineCount > 0 && !prelude) {
            payloads.push_back("#include <kompoz/kotlin.hpp>");
            if (!interfaces.empty()) payloads.push_back(interfaces);
            prelude = true;
        }
        // (one of a single line has none to spare: it goes with the next that has)
        if (!cpp.empty()) pending += cpp;
        if (lineCount > 0 && !pending.empty()) payloads.push_back(std::exchange(pending, std::string()));
        while (payloads.size() > lineCount) {  // (more than its lines: the declarations on one)
            payloads[payloads.size() - 2] += ' ' + payloads.back();
            payloads.pop_back();
        }
        size_t line = 0;
        for (size_t k = 0; k < text.size(); ++k) {
            unsigned char c = static_cast<unsigned char>(text[k]);
            if (c == '\n') {
                ++line;
                out += char(c);
                continue;
            }
            if (line < payloads.size()) {  // (the line given to it: its text, then on to its end)
                out += payloads[line];
                size_t eol = text.find('\n', k);
                k = (eol == std::string_view::npos ? text.size() : eol) - 1;
                continue;
            }
            // Blanked: each character a space (two for one of UTF-16's surrogate pairs).
            if (c == '\r') out += char(c);
            else if ((c & 0xC0) == 0x80) continue;
            else out += c >= 0xF0 ? "  " : " ";
        }
        done = f.end;
    }
    out.append(src.substr(done));
    return out;
}

}  // namespace kompozc
