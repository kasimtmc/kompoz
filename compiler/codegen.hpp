#pragma once

// kompozc's C++ output: what each @Composable function becomes, in the C++ that the Kompoz
// library is written in (the way Compose's compiler plugin rewrites Kotlin functions).

#include "ast.hpp"

#include <map>
#include <string>

namespace kompozc {

// What kompozc knows of the code it translates: the @Composable functions (calls to them pass
// their arguments by position, named ones put in place and defaults filled in) and the data
// classes (made with their fields in the order they are declared).
struct Module {
    std::map<std::string, const Function*, std::less<>> functions;
    std::map<std::string, const DataClass*, std::less<>> classes;
};

class Output {
public:
    explicit Output(std::string file) : file_(std::move(file)) {}
    // What follows comes from `line` of the original source: a #line directive, or new lines,
    // so that the C++ compiler's messages and remember()'s call sites point there.
    void at(Pos pos);
    void text(std::string_view s);
    void line(int n);  // resume the original's text from line n
    std::string str() && { return std::move(out_); }
    const std::string& file() const { return file_; }

private:
    std::string file_;
    std::string out_;
    int current_ = 0;  // the original line the output's current line stands for (0: none yet)
};

// A data class: a struct with its fields, == and toString().
void generateClass(Output& out, const DataClass& c, const Module& module);
// Declarations: what every @Composable becomes before any is defined, defaults included.
void generatePrototype(Output& out, const Function& f, const Module& module);
// The definition: void name(params, CallSite site) { composable(site, params..., [=] { body }); }
void generateDefinition(Output& out, const Function& f, const Module& module);

}  // namespace kompozc
