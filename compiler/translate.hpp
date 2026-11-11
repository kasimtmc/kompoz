#pragma once

// kompozc's translation of a source: where its Kotlin declarations are (scan), and the C++ they
// become with the C++ around them (translate). The command line (main.cpp) and the language
// server (lsp.cpp) both use it.

#include "codegen.hpp"
#include "headers.hpp"
#include "parser.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kompozc {

// A Kotlin declaration in the C++: a @Composable function, a data class, a top-level val, a class, an interface, ...
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

    // One that couldn't be read (scan with errors): where it is, nothing of it.
    bool failed = false;
    std::optional<SealedDecl> sealed = std::nullopt;  // (last: the declarations above are made by place)
    std::optional<InterfaceDecl> interface = std::nullopt;

    const std::string& name() const {
        static const std::string none;
        return failed ? none
               : function ? function->name : dataClass ? dataClass->name : property ? property->name : classDecl ? classDecl->name
               : object   ? object->name   : sealed ? sealed->name : interface ? interface->name : enumClass->name;
    }
};

// Where a statement of the C++ at a namespace's depth may begin (right after a ; or a } there), and
// the namespaces open there: where a declaration can be put before a use.
struct Boundary {
    size_t at;
    std::string qualifier;
};

// Another source of the module (the target): its declarations, for the calls to them here.
struct OtherSource {
    std::string lineFile;
    std::vector<Found> found;
};

// Walks the C++ around the functions (skips comments, strings, characters and raw strings) and
// finds each Kotlin declaration; `boundaries`, given, gets where C++ statements may begin.
// With `errors`, a declaration that can't be read is one of them, not thrown: found failed, to
// the line its block closes on at the left edge, or before the next line that starts there (what
// the language server shows while a declaration is being written).
std::vector<Found> scan(std::string_view src, std::vector<Boundary>* boundaries = nullptr, std::vector<CompileError>* errors = nullptr);
// The source's C++ with its Kotlin translated (#line naming `lineFile`); `others`: the module's
// other sources' declarations. Throws CompileError.
std::string translate(const std::string& src, const std::filesystem::path& source, const std::string& lineFile, const Structs& structs,
                      const std::vector<OtherSource>& others);

// The structs of Kompoz's headers (the .hpp and .h files in `headerDirs`), a quoted include of a
// source looked for in `includeDirs` too; a source's own are added to a copy (readIncludes, readText).
Structs readStructs(const std::vector<std::string>& headerDirs, const std::vector<std::string>& includeDirs);
// The module's other sources' declarations (one that can't be read is left out: its own translation
// says what is wrong); a source's text from `textOf` where it gives one (an editor's unsaved text),
// else from the file.
std::vector<OtherSource> readOthers(const std::vector<std::string>& moduleSources, const std::filesystem::path& input,
                                    const std::function<std::optional<std::string>(const std::filesystem::path&)>& textOf = {});
// The source as C++ tools see it beside its Kotlin (the language server's view for clangd): each
// Kotlin declaration's text blanked, keeping its lines and the columns after it, and on its lines
// what C++ needs to know of it (a @Composable's prototype, a data class's struct, ...), the first
// one's after kotlin.hpp's #include (as the translation has it); the rest of the file as it is.
std::string maskedView(std::string_view src, const std::vector<Found>& found, const Structs& structs);
// A declaration made known to the module (a sealed class: its nested subclasses too, under its name).
void declareIn(Module& module, const Found& f, bool qualified);

}  // namespace kompozc
