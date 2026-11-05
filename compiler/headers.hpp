#pragma once

// What kompozc reads of C++ headers: the structs made with designated initializers, and their
// fields in the order they are declared. A Kotlin call with named arguments to one of them,
// DialogProperties(dismissOnClickOutside = false), becomes DialogProperties{.dismissOnClickOutside = false},
// its fields in that order (C++'s designated initializers take no other).

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
    // using colors = TextFieldColors;: a struct under another name (TextFieldDefaults.colors(…)).
    std::map<std::string, std::set<std::string>, std::less<>> aliases;

    const std::vector<std::string>* find(std::string_view name) const;
    // A header's structs, and those of the headers it includes with quotes, found next to it.
    void readHeader(const std::filesystem::path& header);
    // The C++ text of a source (not a header of its own): its quoted includes' structs.
    void readIncludes(std::string_view source, const std::filesystem::path& dir);
    // The structs of a C++ text (a source's own, or a header's).
    void readText(std::string_view src);
};

}  // namespace kompozc
