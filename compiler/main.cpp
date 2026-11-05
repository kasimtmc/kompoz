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

// A Kotlin declaration in the C++: a @Composable function or a data class.
struct Found {
    std::optional<Function> function;
    std::optional<DataClass> dataClass;
    size_t begin, end;  // offsets in the source
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

// Walks the C++ around the functions: skips comments, strings, characters and raw strings, and
// finds each @Composable.
std::vector<Found> scan(std::string_view src) {
    std::vector<Found> found;
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
        } else if (c == '@' && src.substr(i + 1, 10) == "Composable" && (i + 11 >= src.size() || !identChar(src[i + 11]))) {
            Parser parser(src, positionOf(src, i));
            Function f = parser.function();
            size_t end = f.end.offset;
            found.push_back({std::move(f), std::nullopt, i, end});
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
                found.push_back({std::nullopt, std::move(d), i, end});
                i = end;
            } else {
                i += 4;
            }
        } else if (identChar(c)) {
            while (i < src.size() && identChar(src[i])) ++i;  // (a name: "data" inside one isn't a declaration)
        } else {
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
        names(f.function->body.get(), out);
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
        if (f.function) module.functions[f.function->name] = &*f.function;
        else module.classes[f.dataClass->name] = &*f.dataClass;
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
                    const std::string& name = f.function ? f.function->name : f.dataClass->name;
                    if (taken.contains(&f) || !used.contains(name) || module.functions.contains(name) || module.classes.contains(name)) continue;
                    taken.insert(&f);
                    imported.emplace_back(&o, &f);
                    names(f, used);
                    more = true;
                }
        }
        for (auto [o, f] : imported) {
            if (f->function) module.functions[f->function->name] = &*f->function;
            else module.classes[f->dataClass->name] = &*f->dataClass;
        }
    }
    Output out(lineFile);
    out.line(1);
    fs::path dir = source.parent_path();
    size_t done = 0;
    std::set<std::string, std::less<>> declared;  // the functions declared so far
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
            for (auto [o, g] : imported)  // (from their own files: #line says which)
                if (g->dataClass) {
                    out.setFile(o->lineFile);
                    generateClass(out, *g->dataClass, module);
                }
            out.setFile(lineFile);
            for (const Found& g : found)
                if (g.dataClass) generateClass(out, *g.dataClass, module);
        }
        if (f.function) {
            // Declared where needed, as late as can be (a parameter's type may be a struct of the
            // file's declared above the function, not at its top): the function itself, and those it
            // calls, of this file or another, not declared yet.
            std::set<std::string, std::less<>> used;
            names(f, used);
            used.insert(f.function->name);
            for (auto [o, g] : imported)
                if (g->function && used.contains(g->function->name) && declared.insert(g->function->name).second) {
                    out.setFile(o->lineFile);
                    generatePrototype(out, *g->function, module);
                }
            out.setFile(lineFile);
            for (const Found& g : found)
                if (g.function && used.contains(g.function->name) && declared.insert(g.function->name).second)
                    generatePrototype(out, *g.function, module);
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
