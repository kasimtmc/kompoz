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
#include "parser.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;
using namespace kompozc;

namespace {

struct Found {
    Function function;
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
            found.push_back({std::move(f), i, end});
            i = end;
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

std::string translate(const std::string& src, const fs::path& source, const std::string& lineFile) {
    std::vector<Found> found = scan(src);
    Module module;
    for (const Found& f : found) module[f.function.name] = &f.function;
    Output out(lineFile);
    out.line(1);
    fs::path dir = source.parent_path();
    size_t done = 0;
    for (size_t k = 0; k < found.size(); ++k) {
        const Found& f = found[k];
        if (f.begin > done) {
            if (done > 0) out.line(positionOf(src, done).line);
            out.text(passThrough(std::string_view(src).substr(done, f.begin - done), dir));
        }
        if (k == 0) {
            // Before the first: the helpers the translations use, and every function declared, so
            // that they call each other in any order (as Kotlin's do).
            out.text("\n#include <kompoz/kotlin.hpp>\n");
            for (const Found& g : found) generatePrototype(out, g.function, module);
        }
        generateDefinition(out, f.function, module);
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
    std::string input, output;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) output = argv[++i];
        else if (input.empty()) input = a;
        else {
            std::cerr << "kompozc: unexpected argument " << a << "\n";
            return 2;
        }
    }
    if (input.empty() || output.empty()) {
        std::cerr << "usage: kompozc SOURCE -o OUTPUT\n";
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
    std::string result;
    try {
        result = translate(src, fs::path(input), lineFile);
    } catch (const CompileError& e) {
        std::cerr << lineFile << ":" << e.pos.line << ":" << e.pos.column << ": error: " << e.what() << "\n";
        return 1;
    }
    if (fs::path dir = fs::path(output).parent_path(); !dir.empty()) fs::create_directories(dir);
    std::ofstream out(output, std::ios::binary);
    out << result;
    return out ? 0 : 1;
}
