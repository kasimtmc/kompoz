#pragma once

// kompozc's C++ output: what each @Composable function becomes, in the C++ that the Kompoz
// library is written in (the way Compose's compiler plugin rewrites Kotlin functions).

#include "ast.hpp"

#include <map>
#include <string>

namespace kompozc {

// The @Composable functions kompozc knows (by name): calls to them pass their arguments by
// position, named ones put in place and defaults filled in.
using Module = std::map<std::string, const Function*, std::less<>>;

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

// Declarations: what every @Composable becomes before any is defined, defaults included.
void generatePrototype(Output& out, const Function& f, const Module& module);
// The definition: void name(params, CallSite site) { composable(site, params..., [=] { body }); }
void generateDefinition(Output& out, const Function& f, const Module& module);

}  // namespace kompozc
