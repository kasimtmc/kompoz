#pragma once

// What kompozc reads of C++ headers: the structs made with designated initializers, and their
// fields in the order they are declared. A Kotlin call with named arguments to one of them,
// DialogProperties(dismissOnClickOutside = false), becomes DialogProperties{.dismissOnClickOutside = false},
// its fields in that order (C++'s designated initializers take no other). And every class's public
// members: a lambda with one as its receiver (Canvas { drawCircle(…) }) reaches them by their names.

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace kompozc {

struct Structs {
    std::map<std::string, std::vector<std::string>, std::less<>> fields;  // by the struct's name
    std::set<std::string, std::less<>> ambiguous;  // two structs of that name with other fields: neither is used
    std::set<std::filesystem::path> read;          // the headers read (for the build's dependencies)
    std::vector<std::filesystem::path> includeDirs;  // where a quoted include not next to its file is looked for (the target's -I)
    // using colors = TextFieldColors;: a struct under another name (TextFieldDefaults.colors(…)).
    std::map<std::string, std::set<std::string>, std::less<>> aliases;

    // A class's (or struct's) public members, fields and functions, and the classes it derives from.
    struct Members {
        std::set<std::string, std::less<>> names;
        std::vector<std::string> bases;
    };
    std::map<std::string, Members, std::less<>> classes;
    std::set<std::string, std::less<>> ambiguousClasses;  // two classes of that name with other members
    // The namespaces declared: Kotlin's routes.Item(…) is C++'s routes::Item{…} when `routes` is one.
    std::set<std::string, std::less<>> namespaces;

    const std::vector<std::string>* find(std::string_view name) const;
    // Whether `name` is a public member of the class `type` (or of a class it derives from).
    bool member(std::string_view type, std::string_view name) const;
    // Whether the class `type` is `base` or derives from it (a ViewModel: handed out as a shared pointer).
    bool derivesFrom(std::string_view type, std::string_view base) const;
    // A header's structs, and those of the headers it includes with quotes, found next to it.
    void readHeader(const std::filesystem::path& header);
    // The C++ text of a source (not a header of its own): its quoted includes' structs.
    void readIncludes(std::string_view source, const std::filesystem::path& dir);
    // The structs of a C++ text (a source's own, or a header's).
    void readText(std::string_view src);
};

}  // namespace kompozc
