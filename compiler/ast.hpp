#pragma once

// kompozc's syntax tree: what the parser reads from a @Kompozable function's Kotlin.

#include "lexer.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kompozc {

// A type as written: Int, String?, List<Item>, (String) -> Unit, @Kompozable () -> Unit,
// RowScope.() -> Unit.
struct Type {
    std::string name;                  // dotted for qualified names; empty for a function type
    std::vector<Type> args;            // generic arguments
    bool nullable = false;
    bool function = false;
    bool composable = false;           // @Kompozable function type
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
    std::vector<size_t> spreadArgs;  // a Call's arguments written *list (their indices in kids)

    Node(Kind k, Pos p) : kind(k), pos(p) {}
};

struct Param {
    std::string name;
    Type type;            // a vararg's: List<T> (what the function sees)
    NodeP defaultValue;
    bool isVararg = false;  // vararg xs: T: a call's arguments from here gathered into a list
    Pos pos;
};

// A function: [@Kompozable] fun [Receiver.]name(params): result { body }.
struct Function {
    std::string name;
    std::vector<std::string> typeParams;  // fun <T> first(items: List<T>): T: a function template
    bool composable = true;
    bool isPrivate = false;
    std::optional<Type> receiver;    // an extension's: fun Modifier.card()
    std::vector<Param> params;
    std::optional<Type> result;      // none: Unit
    NodeP body;                      // a Block; or an expression body (= expr) as a Return in a Block
    bool expressionBody = false;
    bool overrides = false;          // override fun (a class's)
    bool isSuspend = false;          // suspend fun: a coroutine (Task, TaskOf<T>)
    bool isAbstract = false;         // an interface's fun without a body, an abstract class's abstract fun
    bool isOpen = false;             // open fun: a subclass may override it
    bool isOperator = false;         // operator fun plus(…): C++'s operator+ too
    // val String.shout: String get() = …: an extension property's getter (a function of its receiver), a var's
    // setter beside it (setShout(receiver, value)).
    bool isExtensionProperty = false;
    std::shared_ptr<Function> setter;
    std::vector<std::string> annotations;  // besides @Kompozable (Preview, ...)
    std::map<std::string, std::string> annotationArgs;  // @Preview(widthDp = 400): "widthDp" -> "400" (the text of each)
    Pos pos;                         // the @ of @Kompozable (or where it starts)
    Pos end;                         // just past the closing brace
};

// Whether two types are written alike (a name with its outer types or alone: Catalog.Entry is Entry inside Catalog).
inline bool sameType(const Type& a, const Type& b) {
    auto last = [](const std::string& n) { return n.substr(n.rfind('.') + 1); };
    if (last(a.name) != last(b.name) || a.nullable != b.nullable || a.function != b.function || a.args.size() != b.args.size() ||
        a.params.size() != b.params.size() || bool(a.result) != bool(b.result) || bool(a.receiver) != bool(b.receiver))
        return false;
    for (size_t i = 0; i < a.args.size(); ++i)
        if (!sameType(a.args[i], b.args[i])) return false;
    for (size_t i = 0; i < a.params.size(); ++i)
        if (!sameType(a.params[i], b.params[i])) return false;
    return (!a.result || sameType(*a.result, *b.result)) && (!a.receiver || sameType(*a.receiver, *b.receiver));
}
// Whether two functions take the same parameters (and receiver): overloads of a name differ in them.
inline bool sameParams(const Function& a, const Function& b) {
    if (a.params.size() != b.params.size() || bool(a.receiver) != bool(b.receiver) || (a.receiver && !sameType(*a.receiver, *b.receiver)))
        return false;
    for (size_t i = 0; i < a.params.size(); ++i)
        if (!sameType(a.params[i].type, b.params[i].type)) return false;
    return true;
}

// A top-level property: [private] val name[: Type] = value.
struct Property {
    std::string name;
    NodeP decl;  // a Decl
    bool isPrivate = false;
    Pos pos;
    Pos end;
};


// A class: class Name(val a: Int, b: String) : Base(b) { val c = …; fun f() { … }; override fun g() { … }; init { … } }.
struct ObjectDecl;
struct ClassDecl;
struct DataClass;
struct EnumClass;
struct InterfaceDecl;
struct SealedDecl;

// The classes, data classes, enums, objects and interfaces declared in another's body (Kotlin's nested ones: not
// inner, they hold no outer this): C++ types nested in it (Outer.Inner is Outer::Inner).
// The arguments a sealed class's case gives its constructor: data class Detail(val id: Int) : Screen("detail/$id").
struct SuperCall {
    std::string name;  // the sealed class (as written: Screen, Outer.Screen)
    std::vector<NodeP> args;
    std::vector<std::string> names;  // "" by position
};

struct NestedDecls {
    std::vector<std::shared_ptr<ClassDecl>> classes;
    std::vector<std::shared_ptr<DataClass>> dataClasses;
    std::vector<std::shared_ptr<EnumClass>> enums;
    std::vector<std::shared_ptr<ObjectDecl>> objects;
    std::vector<std::shared_ptr<InterfaceDecl>> interfaces;
    // inner class: one holding its outer object (made as outer.Inner(…)); defined in the outer class, not with these
    std::vector<std::shared_ptr<ClassDecl>> innerClasses;
    std::vector<std::shared_ptr<SealedDecl>> sealeds;  // (after the rest: its cases among them are complete before it)
    bool empty() const { return classes.empty() && dataClasses.empty() && enums.empty() && objects.empty() && interfaces.empty() && sealeds.empty(); }
};

struct ClassProperty {
    std::string name;
    std::optional<Type> type;  // none: the value's
    NodeP init;                // the value; by …: the delegate
    bool isVar = false, isPrivate = false;
    bool delegated = false;     // val/var x by delegate
    bool overrides = false;     // override val/var (an interface's)
    bool privateSetter = false; // var x … private set
    bool isAbstract = false;    // abstract val x: T (an abstract class's: its subclasses give it)
    bool isOpen = false;        // open val: a subclass may override it
    bool isLateinit = false;    // lateinit var x: T: set later, read only after (else it throws, as Kotlin's)
    NodeP getter;               // get() = … / get() { … }: a Block (an expression one as its Return)
    NodeP setter;               // set(v) { … }: a Block, `field` the stored value
    std::string setterParam;    // set(v)'s name
    // Whether it is read through functions of its own (x(), setX(v)) rather than a field: a delegate,
    // an accessor, or an interface's property.
    bool accessed() const { return delegated || overrides || getter || setter || privateSetter || isAbstract || isOpen || isLateinit; }
    Pos pos;
};
struct ClassDecl {
    std::string name;
    std::vector<std::string> typeParams;  // class Box<T>: a C++ class template
    bool isPrivate = false;
    bool isAbstract = false;  // abstract class: made only as one of its subclasses
    bool isOpen = false;      // open class: subclasses may derive from it
    std::vector<Param> ctorParams;
    std::vector<char> ctorKinds;      // one per parameter: ' ' plain, 'v' val, 'm' var
    std::vector<bool> ctorPrivate;    // private val x
    std::vector<bool> ctorOverride;   // override val x (an interface's property)
    std::optional<Type> base;
    std::vector<NodeP> baseArgs;      // : Base(args)
    std::vector<std::string> baseArgNames;  // and their names ("" by position: a sealed class's are named too)
    std::vector<Type> interfaces;     // : Base(), A, B (those without a constructor call)
    std::shared_ptr<ObjectDecl> companion;  // companion object { … }: its members the class's own, static
    NestedDecls nested;
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
    std::vector<std::string> typeParams;  // data class Pair<A, B>
    std::vector<Type> supers;  // : Sealed(), A, B: a sealed class or interface it is a case of, interfaces
    SuperCall superCall;       // : Screen("…"): its sealed class's constructor's arguments
    std::vector<Param> fields;
    std::vector<bool> mutableFields;  // var (else val)
    std::vector<bool> overridingFields;  // override val (an interface's property)
    // Its body: { val b = …; fun f() = …; init { … } } (not part of ==, toString or copy's arguments, as Kotlin's)
    std::vector<ClassProperty> properties;
    std::vector<Function> methods;
    std::vector<size_t> order;  // as ClassDecl's
    std::vector<NodeP> initBlocks;
    std::shared_ptr<ObjectDecl> companion;
    NestedDecls nested;
    bool hasBody() const { return !properties.empty() || !methods.empty() || !initBlocks.empty() || companion || !nested.empty(); }
    std::vector<std::string> annotations;  // @Serializable: a route
    Pos pos;
    Pos end;
};

// An object: @Serializable object Home (a route without arguments; the name is its type and its value).
// object Name { val a = …; fun f() { … } }: one value of its own type, its members static (none: a route's
// or a marker's type).
struct ObjectDecl {
    std::string name;
    bool isCompanion = false;  // a class's companion object: its members the class's static ones
    std::vector<Type> supers;  // : Sealed(), A, B (as a data class's)
    SuperCall superCall;       // (as a data class's)
    std::vector<std::string> annotations;
    std::vector<ClassProperty> properties;
    std::vector<Function> methods;
    std::vector<size_t> order;  // members in declaration order, as ClassDecl's
    std::vector<NodeP> initBlocks;
    NestedDecls nested;
    bool hasMembers() const { return !properties.empty() || !methods.empty() || !initBlocks.empty() || !nested.empty(); }
    Pos pos;
    Pos end;
};

// An enum class: enum class Name { A, B, C } (entries only: C++'s enum class), or enum class Name(val a: T) { A(1),
// B(2); val b = …; fun f() = … } (a constructor or a body: a struct of its fields, its entries its constants).
// (C++'s with values, enum class X { A = 1 }, is C++ and stays as it is.)
// An enum entry's body: PLUS { override fun apply(a: Int, b: Int) = a + b }: what it overrides of its class's.
struct EnumEntryBody {
    std::vector<ClassProperty> properties;
    std::vector<Function> methods;
    Pos pos;
};

struct EnumClass {
    std::string name;
    std::vector<std::string> entries;
    std::vector<std::shared_ptr<EnumEntryBody>> entryBodies;  // each entry's (none: it has none)
    std::vector<std::vector<NodeP>> entryArgs;           // each entry's constructor arguments
    std::vector<std::vector<std::string>> entryArgNames; // and their names ("" by position)
    std::vector<Param> ctorParams;  // (val/var or not: each a field)
    std::vector<ClassProperty> properties;
    std::vector<Function> methods;
    std::vector<size_t> order;
    std::vector<NodeP> initBlocks;
    std::shared_ptr<ObjectDecl> companion;
    NestedDecls nested;  // declarations in its body (a struct's nested ones, as a class's)
    bool rich() const {
        return !ctorParams.empty() || !properties.empty() || !methods.empty() || !initBlocks.empty() || companion || !nested.empty() ||
               !nested.innerClasses.empty() ||
               std::any_of(entryBodies.begin(), entryBodies.end(), [](const auto& b) { return b != nullptr; });
    }
    Pos pos;
    Pos end;
};

// sealed class Name / sealed interface Name: a value that is one of its subclasses (a variant of them),
// those in its body (nested: Name.Success) and those of the same file that name it as their base.
struct SealedDecl {
    std::string name;
    bool isInterface = false;
    // sealed class Screen(val route: String) : Named { val title = …; abstract val icon: …; fun f() = … }: what each case
    // has (its constructor's parameters given by the case, the body's members), the interfaces it implements
    std::vector<Param> ctorParams;
    std::vector<char> ctorKinds;  // ' ' plain, 'v' val, 'm' var
    std::vector<Type> supers;
    std::vector<ClassProperty> properties;
    std::vector<Function> methods;
    std::vector<size_t> memberOrder;  // as a class's order
    std::vector<NodeP> initBlocks;
    std::shared_ptr<ObjectDecl> companion;
    bool hasMembers() const { return !ctorParams.empty() || !properties.empty() || !methods.empty() || !initBlocks.empty(); }
    std::vector<DataClass> dataClasses;  // nested
    std::vector<ObjectDecl> objects;     // nested
    std::vector<ClassDecl> classes;      // nested (class Failure(val e: Exception) : Result())
    bool classCases = false;             // whether a case is a class (it is defined after them, as late as can be)
    std::vector<std::pair<char, size_t>> order;  // the nested ones in declaration order: 'd' or 'o', an index
    std::vector<std::string> topLevel;   // the same file's subclasses at the top level, in order (scan finds them)
    Pos pos;
    Pos end;
};

// interface Name : A, B { val a: T; fun f(): R; fun g() = …; val b: T get() = … }: what classes, objects
// and data classes implementing it have, its functions and properties without a body abstract.
// fun interface Name { fun f(…) }: made from a lambda too (Name { … }).
struct InterfaceDecl {
    std::string name;
    std::vector<std::string> typeParams;  // interface Source<T>
    bool isFun = false;
    std::vector<Type> bases;
    std::vector<ClassProperty> properties;
    std::vector<Function> methods;
    std::vector<size_t> order;  // members in declaration order, as ClassDecl's
    std::shared_ptr<ObjectDecl> companion;
    NestedDecls nested;
    Pos pos;
    Pos end;
};

// typealias Name<T> = Type: C++'s using.
struct TypeAlias {
    std::string name;
    std::vector<std::string> typeParams;
    Type type;
    Pos pos;
    Pos end;
};

// Whether one of `supers` is `name` (a sealed class's case names it).
inline bool namesSuper(const std::vector<Type>& supers, const std::string& name) {
    for (const Type& t : supers)
        if (t.name == name) return true;
    return false;
}

}  // namespace kompozc
