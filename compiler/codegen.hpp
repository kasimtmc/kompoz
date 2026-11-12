#pragma once

// kompozc's C++ output: what each @Kompozable function becomes, in the C++ that the Kompoz
// library is written in (the way Compose's compiler plugin rewrites Kotlin functions).

#include "ast.hpp"
#include "headers.hpp"

#include <map>
#include <set>
#include <string>

namespace kompozc {

// What kompozc knows of the code it translates: the @Kompozable functions (calls to them pass
// their arguments by position, named ones put in place and defaults filled in) and the data
// classes (made with their fields in the order they are declared).
struct Module {
    std::map<std::string, const Function*, std::less<>> functions;
    std::map<std::string, const DataClass*, std::less<>> classes;
    std::map<std::string, const Property*, std::less<>> properties;  // top-level vals
    std::map<std::string, const ClassDecl*, std::less<>> classDecls;  // classes (constructed with their parameters in order)
    std::set<std::string, std::less<>> objects;  // objects: a name that is a type and its one value
    std::map<std::string, const ObjectDecl*, std::less<>> objectDecls;  // the objects declared so (not a sealed class's nested ones)
    std::map<std::string, const InterfaceDecl*, std::less<>> interfaces;
    std::map<std::string, const EnumClass*, std::less<>> enums;
    std::map<std::string, const TypeAlias*, std::less<>> aliases;
    std::map<std::string, const Function*, std::less<>> extensionProperties;  // val String.shout: by its name
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
    // Where the output is, to go back to it (what was written since, undone).
    struct Mark {
        size_t size;
        int current;
    };
    Mark mark() const { return {out_.size(), current_}; }
    void rewind(const Mark& m) {
        out_.resize(m.size);
        current_ = m.current;
    }
    const std::string& file() const { return file_; }

private:
    std::string file_;
    std::string out_;
    int current_ = 0;  // the original line the output's current line stands for (0: none yet)
};

// An enum class: C++'s enum class, its entries in order.
void generateEnum(Output& out, const EnumClass& e, const Module& module);
// An object: an empty struct, a route when @Serializable; one with members, a struct of static members.
void generateObject(Output& out, const ObjectDecl& o, const Module& module);
// A sealed class or interface: a struct holding one of its subclasses (a variant), its nested ones inside it.
void generateSealed(Output& out, const SealedDecl& s, const Module& module);
// A data class: a struct with its fields, == and toString().
void generateClass(Output& out, const DataClass& c, const Module& module);
// A top-level val: a constant of the namespace.
void generateProperty(Output& out, const Property& p, const Module& module);
// A typealias: C++'s using (a template's for a generic one).
void generateAlias(Output& out, const TypeAlias& a, const Module& module);
// Declarations of the types an alias may name before they are defined (data classes, classes, sealed ones).
void declareType(Output& out, const std::string& name, const std::vector<std::string>& typeParams, bool isClass);
// An interface: a handle to an object implementing it (its abstract C++ class, Name::Object).
void generateInterface(Output& out, const InterfaceDecl& i, const Module& module);
// Its name declared (data classes and objects implementing it name it before it is defined).
void declareInterface(Output& out, const InterfaceDecl& i);
// A class: a C++ class with its constructor, members and functions.
void generateClassDecl(Output& out, const ClassDecl& c, const Module& module);
// Declarations: what every @Kompozable becomes before any is defined, defaults included.
void generatePrototype(Output& out, const Function& f, const Module& module);
// The definition: void name(params, CallSite site) { kompozable(site, params..., [=] { body }); }
void generateDefinition(Output& out, const Function& f, const Module& module);

}  // namespace kompozc
