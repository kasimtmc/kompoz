#pragma once

// kompozc's syntax tree: what the parser reads from a @Composable function's Kotlin.

#include "lexer.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kompozc {

// A type as written: Int, String?, List<Item>, (String) -> Unit, @Composable () -> Unit,
// RowScope.() -> Unit.
struct Type {
    std::string name;                  // dotted for qualified names; empty for a function type
    std::vector<Type> args;            // generic arguments
    bool nullable = false;
    bool function = false;
    bool composable = false;           // @Composable function type
    std::vector<Type> params;          // a function type's
    std::unique_ptr<Type> result;      // a function type's
    std::unique_ptr<Type> receiver;    // RowScope.() -> Unit
    Pos pos;

    Type() = default;
    Type(const Type& o);
    Type& operator=(const Type& o);
    Type(Type&&) = default;
    Type& operator=(Type&&) = default;
};

struct Node;
using NodeP = std::unique_ptr<Node>;

struct Node {
    enum Kind {
        // Expressions
        Name,      // text
        Number,    // text as written
        String,    // parts; expression parts parsed into kids (in order)
        Char,      // text in C++ escape form
        Bool,      // text "true" / "false"
        Null,
        This,
        Call,      // kids[0] callee, then the arguments (names: "" or the argument's name), lambda: trailing
        Member,    // kids[0] . text   (flag: ?.)
        Index,     // kids[0] [ kids[1..] ]
        Lambda,    // names: parameters (empty: none written); kids: the body's statements
        Unary,     // text op, kids[0]; flag: postfix (x++)
        Binary,    // text op, kids[0], kids[1]; infix functions (until, downTo, step, to) too
        Is,        // kids[0] is type (flag: !is)
        As,        // kids[0] as type (flag: as?)
        If,        // kids: condition, then (a Block), else (a Block or none)
        When,      // kids[0]: subject or none; branches
        Paren,     // ( kids[0] )
        Object,    // object : type { kids: Methods }
        Method,    // override fun text(names: typeArgs): type { kids[0] }
        // Statements
        Block,     // kids: statements
        ExprStmt,  // kids[0]
        Decl,      // val/var text = kids[0] (none without one); flag: var; delegated: by; names: destructured
        Assign,    // kids[0] text(= += ...) kids[1]
        Return,    // kids[0] or none
        Break,
        Continue,
        For,       // names: the loop variables; kids[0] iterable, kids[1] body (a Block)
        While,     // kids[0] condition, kids[1] body
        DoWhile,   // kids[0] body, kids[1] condition
        Try,       // kids[0] body; names / typeArgs: the catches' variables and types, their bodies next; flag: a finally body last
    };

    // A branch of `when`: conditions (empty: else), each with its kind, and the body.
    struct WhenBranch {
        enum CondKind { Expr, In, NotIn, Is, NotIs };
        std::vector<std::pair<CondKind, NodeP>> conds;
        std::vector<Type> types;  // for Is / NotIs, by position in conds
        NodeP body;               // a Block
        Pos pos;
    };

    Kind kind;
    Pos pos;
    std::string text;
    std::vector<NodeP> kids;
    std::vector<std::string> names;
    std::vector<StringPart> parts;
    NodeP lambda;                    // a Call's trailing lambda
    std::optional<Type> type;        // Decl's declared type; Is / As target; Call's generic arguments (first)
    std::vector<Type> typeArgs;      // a Call's <...>
    std::vector<WhenBranch> branches;
    bool flag = false;
    bool delegated = false;

    Node(Kind k, Pos p) : kind(k), pos(p) {}
};

struct Param {
    std::string name;
    Type type;
    NodeP defaultValue;
    Pos pos;
};

// A function: [@Composable] fun [Receiver.]name(params): result { body }.
struct Function {
    std::string name;
    bool composable = true;
    bool isPrivate = false;
    std::optional<Type> receiver;    // an extension's: fun Modifier.card()
    std::vector<Param> params;
    std::optional<Type> result;      // none: Unit
    NodeP body;                      // a Block; or an expression body (= expr) as a Return in a Block
    bool expressionBody = false;
    bool overrides = false;          // override fun (a class's)
    std::vector<std::string> annotations;  // besides @Composable (Preview, ...)
    std::map<std::string, std::string> annotationArgs;  // @Preview(widthDp = 400): "widthDp" -> "400" (the text of each)
    Pos pos;                         // the @ of @Composable (or where it starts)
    Pos end;                         // just past the closing brace
};

// A top-level property: [private] val name[: Type] = value.
struct Property {
    std::string name;
    NodeP decl;  // a Decl
    bool isPrivate = false;
    Pos pos;
    Pos end;
};

// An enum class: enum class Name { A, B, C } (entries only; one with values is written in C++).
struct EnumClass {
    std::string name;
    std::vector<std::string> entries;
    Pos pos;
    Pos end;
};

// A class: class Name(val a: Int, b: String) : Base(b) { val c = …; fun f() { … }; override fun g() { … }; init { … } }.
struct ClassProperty {
    std::string name;
    std::optional<Type> type;  // none: the value's
    NodeP init;
    bool isVar = false, isPrivate = false;
    Pos pos;
};
struct ClassDecl {
    std::string name;
    bool isPrivate = false;
    std::vector<Param> ctorParams;
    std::vector<char> ctorKinds;      // one per parameter: ' ' plain, 'v' val, 'm' var
    std::vector<bool> ctorPrivate;    // private val x
    std::optional<Type> base;
    std::vector<NodeP> baseArgs;      // : Base(args)
    std::vector<ClassProperty> properties;
    std::vector<Function> methods;
    std::vector<size_t> order;        // members in declaration order: an index into properties (even) or methods (odd): i * 2 + kind
    std::vector<NodeP> initBlocks;
    Pos pos;
    Pos end;
};

// A data class: data class Name(val a: Int, var b: String = "").
struct DataClass {
    std::string name;
    std::vector<Param> fields;
    std::vector<bool> mutableFields;  // var (else val)
    std::vector<std::string> annotations;  // @Serializable: a route
    Pos pos;
    Pos end;
};

// An object: @Serializable object Home (a route without arguments; the name is its type and its value).
struct ObjectDecl {
    std::string name;
    std::vector<std::string> annotations;
    Pos pos;
    Pos end;
};

}  // namespace kompozc
