#pragma once

// kompozc's C++ output: what each @Composable function becomes, in the C++ that the Kompoz
// library is written in (the way Compose's compiler plugin rewrites Kotlin functions).

#include "ast.hpp"
#include "headers.hpp"

#include <map>
#include <string>

namespace kompozc {

// What kompozc knows of the code it translates: the @Composable functions (calls to them pass
// their arguments by position, named ones put in place and defaults filled in) and the data
// classes (made with their fields in the order they are declared).
struct Module {
    std::map<std::string, const Function*, std::less<>> functions;
    std::map<std::string, const DataClass*, std::less<>> classes;
    std::map<std::string, const Property*, std::less<>> properties;  // top-level vals
    // How a call names each of the file's (or another file's, defined here) functions and vals: its
    // namespace, "::" at the top (a name of all of Kompoz's, label or shape, is not it then).
    std::map<std::string, std::string, std::less<>> qualifiers;
    const Structs* structs = nullptr;  // C++'s, from the headers read
};

class Output {
public:
    explicit Output(std::string file) : file_(std::move(file)) {}
    // What follows comes from `line` of the original source: a #line directive, or new lines,
    // so that the C++ compiler's messages and remember()'s call sites point there.
    void at(Pos pos);
    void text(std::string_view s);
    void line(int n);  // resume the original's text from line n
    // What follows comes from another file (another source's declarations): #line names it.
    void setFile(std::string file) {
        file_ = std::move(file);
        current_ = 0;
    }
    std::string str() && { return std::move(out_); }
    const std::string& file() const { return file_; }

private:
    std::string file_;
    std::string out_;
    int current_ = 0;  // the original line the output's current line stands for (0: none yet)
};

// An enum class: C++'s enum class, its entries in order.
void generateEnum(Output& out, const EnumClass& e);
// A data class: a struct with its fields, == and toString().
void generateClass(Output& out, const DataClass& c, const Module& module);
// A top-level val: a constant of the namespace.
void generateProperty(Output& out, const Property& p, const Module& module);
// Declarations: what every @Composable becomes before any is defined, defaults included.
void generatePrototype(Output& out, const Function& f, const Module& module);
// The definition: void name(params, CallSite site) { composable(site, params..., [=] { body }); }
void generateDefinition(Output& out, const Function& f, const Module& module);

}  // namespace kompozc
