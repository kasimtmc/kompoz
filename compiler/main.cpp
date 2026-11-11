// kompozc: Kompoz's compiler for Kotlin code in C++ sources. Each @Kompozable function in a
// .cpp file is written in Kotlin's syntax; kompozc rewrites it into the C++ the
// Kompoz library is written in, before the C++ compiler sees the file, the way Compose's compiler
// plugin rewrites Kotlin. Everything else in the file is C++ and passes through as it is.
//
//   kompozc SOURCE -o OUTPUT
//
// The output points back at the source with #line directives: the C++ compiler's messages and
// the debugger show the source's lines.
//
//   kompozc --lsp [--clangd PATH] [clangd's arguments...]
//
// A language server for editors, in front of clangd (lsp.cpp).

#include "lsp.hpp"
#include "translate.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <sstream>

namespace fs = std::filesystem;
using namespace kompozc;

int main(int argc, char** argv) {
    std::vector<std::string> all(argv + 1, argv + argc);
    if (auto lsp = std::find(all.begin(), all.end(), "--lsp"); lsp != all.end()) {
        all.erase(lsp);
        return runLanguageServer(all);
    }
    // (an editor's clangd extension, given kompozc for clangd, asks clangd's version: clangd's)
    if (all.size() == 1 && all[0] == "--version") {
        std::cout << "kompozc (Kompoz's compiler and language server), in front of clangd:\n" << std::flush;
        return std::system("clangd --version") == 0 ? 0 : 1;
    }
    std::string input, output, depfile;
    bool masked = false;  // the language server's view of the source for clangd, not its translation
    std::vector<std::string> headerDirs, includeDirs, moduleSources;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) output = argv[++i];
        else if (a == "--headers" && i + 1 < argc) headerDirs.push_back(argv[++i]);
        else if (a == "--include" && i + 1 < argc) includeDirs.push_back(argv[++i]);
        else if (a == "--depfile" && i + 1 < argc) depfile = argv[++i];
        else if (a == "--module" && i + 1 < argc) moduleSources.push_back(argv[++i]);
        else if (a == "--masked") masked = true;
        else if (input.empty()) input = a;
        else {
            std::cerr << "kompozc: unexpected argument " << a << "\n";
            return 2;
        }
    }
    if (input.empty() || output.empty()) {
        std::cerr << "usage: kompozc SOURCE -o OUTPUT [--headers DIR]... [--include DIR]... [--module OTHER_SOURCE]... [--depfile FILE] [--masked]\n";
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
    // and of the source's own quoted includes (next to it, or in the target's include directories).
    Structs structs = readStructs(headerDirs, includeDirs);
    structs.readIncludes(src, fs::path(input).parent_path());
    structs.readText(src);  // (the source's own; its Kotlin has no `struct X {`)
    std::string result;
    try {
        // The module's other sources (the target's): their @Kompozable functions and data classes
        // are called here as Kotlin calls them across files.
        std::vector<OtherSource> others = readOthers(moduleSources, fs::path(input));
        result = masked ? maskedView(src, scan(src), structs) : translate(src, fs::path(input), lineFile, structs, others);
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
