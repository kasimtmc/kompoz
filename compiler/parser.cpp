#include "parser.hpp"

#include <set>
#include <utility>

namespace kompozc {

Type::Type(const Type& o)
    : name(o.name),
      args(o.args),
      nullable(o.nullable),
      function(o.function),
      composable(o.composable),
      params(o.params),
      result(o.result ? std::make_unique<Type>(*o.result) : nullptr),
      receiver(o.receiver ? std::make_unique<Type>(*o.receiver) : nullptr),
      pos(o.pos) {}

Type& Type::operator=(const Type& o) {
    if (this != &o) *this = Type(o);
    return *this;
}

namespace {

// Words that can't be an infix function's name.
const std::set<std::string, std::less<>> kKeywords = {
    "as", "break", "class", "continue", "do", "else", "false", "for", "fun", "if", "in", "interface", "is", "null", "object",
    "package", "return", "super", "this", "throw", "true", "try", "typealias", "val", "var", "when", "while", "by"};

// While alive: newlines count (true) or not (false), as they do inside braces or parentheses.
struct NewlineMode {
    bool& flag;
    bool saved;
    NewlineMode(bool& f, bool value) : flag(f), saved(f) { flag = value; }
    ~NewlineMode() { flag = saved; }
};

NodeP make(Node::Kind kind, Pos pos) { return std::make_unique<Node>(kind, pos); }

}  // namespace

Parser::Parser(std::string_view source, Pos start) : src_(source), lexer_(source, start) {}

const Token& Parser::peekRaw(size_t ahead) {
    while (tokens_.size() <= index_ + ahead) {
        if (!tokens_.empty() && tokens_.back().kind == Tok::End) return tokens_.back();
        tokens_.push_back(lexer_.next());
    }
    return tokens_[index_ + ahead];
}

const Token& Parser::cur() {
    if (!newlines_)
        while (peekRaw().kind == Tok::Newline) ++index_;
    return peekRaw();
}

Token Parser::take() {
    Token t = cur();
    if (t.kind != Tok::End) ++index_;
    return t;
}

bool Parser::isOp(std::string_view op) {
    const Token& t = cur();
    return t.kind == Tok::Op && t.text == op;
}

bool Parser::isIdent(std::string_view name) {
    const Token& t = cur();
    return t.kind == Tok::Ident && t.text == name;
}

bool Parser::acceptOp(std::string_view op) {
    if (!isOp(op)) return false;
    take();
    return true;
}

void Parser::expectOp(std::string_view op) {
    if (!acceptOp(op)) fail("expected '" + std::string(op) + "'");
}

std::string Parser::expectIdent() {
    if (cur().kind != Tok::Ident) fail("expected a name");
    return take().text;
}

void Parser::skipNewlines() {
    while (peekRaw().kind == Tok::Newline) ++index_;
}

bool Parser::opAfterNewlines(std::string_view op) {
    size_t k = 0;
    while (peekRaw(k).kind == Tok::Newline) ++k;
    const Token& t = peekRaw(k);
    if (t.kind != Tok::Op || t.text != op) return false;
    index_ += k;
    return true;
}

void Parser::fail(const std::string& message) { fail(cur().pos, message); }
void Parser::fail(Pos at, const std::string& message) { throw CompileError(at, message); }

// ---- Declarations -------------------------------------------------------------------------------

void Parser::annotations(std::vector<std::string>* names, std::map<std::string, std::string>* args) {
    while (isOp("@")) {
        take();
        std::string name = expectIdent();
        while (isOp(".") && peekRaw(1).kind == Tok::Ident) {
            take();
            name += "." + take().text;
        }
        if (isOp("(") && !cur().spaceBefore) {  // @Preview(widthDp = 400, showBackground = true): each argument's text
            NewlineMode mode(newlines_, false);
            take();
            while (!isOp(")")) {
                if (cur().kind == Tok::End) fail("unterminated annotation");
                std::string key, value;
                if (cur().kind == Tok::Ident && peekRaw(1).kind == Tok::Op && peekRaw(1).text == "=") {
                    key = take().text;
                    take();
                }
                int depth = 0;
                while (!(depth == 0 && (isOp(",") || isOp(")")))) {
                    if (cur().kind == Tok::End) fail("unterminated annotation");
                    if (isOp("(")) ++depth;
                    if (isOp(")")) --depth;
                    const Token& t = take();
                    value += t.kind == Tok::String ? "\"" + t.text + "\"" : t.text;
                }
                if (args && !key.empty()) (*args)[key] = value;
                acceptOp(",");
            }
            expectOp(")");
        }
        if (names) names->push_back(name);
        skipNewlines();
    }
}

Function Parser::function() {
    Function f;
    f.pos = cur().pos;
    std::vector<std::string> names;
    for (bool more = true; more;) {
        annotations(&names, &f.annotationArgs);
        more = false;
        while (isIdent("private") || isIdent("internal") || isIdent("public") || isIdent("inline") || isIdent("override") ||
               isIdent("protected") || isIdent("open") || isIdent("suspend")) {
            std::string word = take().text;
            if (word == "private") f.isPrivate = true;
            if (word == "override") f.overrides = true;
            if (word == "suspend") f.isSuspend = true;
            more = true;
        }
    }
    f.composable = false;
    for (auto& n : names) {
        if (n == "Kompozable") f.composable = true;
        else if (n == "Composable") fail(f.pos, "Kompoz's composables are @Kompozable (not @Composable)");
        else f.annotations.push_back(n);
    }
    if (!isIdent("fun")) fail("expected 'fun'");
    take();
    // fun name, or fun Receiver.name (an extension)
    Pos at = cur().pos;
    std::vector<std::string> parts{expectIdent()};
    while (isOp(".") && peekRaw(1).kind == Tok::Ident) {
        take();
        parts.push_back(take().text);
    }
    f.name = parts.back();
    parts.pop_back();
    if (!parts.empty()) {
        f.receiver.emplace();
        f.receiver->pos = at;
        for (size_t i = 0; i < parts.size(); ++i) f.receiver->name += (i ? "." : "") + parts[i];
    }
    if (f.composable && f.isSuspend) fail(f.pos, "a @Kompozable function can't be suspend");
    f.params = params();
    if (acceptOp(":")) f.result = type();
    if (abstractMembers_ && !isOp("=") && !opAfterNewlines("{")) {  // an interface's fun without a body
        f.isAbstract = true;
    } else if (isOp("=")) {  // fun x() = expression
        Pos at = take().pos;
        skipNewlines();
        auto ret = make(Node::Return, at);
        ret->kids.push_back(expression());
        f.body = make(Node::Block, at);
        f.body->kids.push_back(std::move(ret));
        f.expressionBody = true;
    } else {
        skipNewlines();
        if (!isOp("{")) fail("expected the function's body");
        f.body = block();
    }
    f.end = tokens_[index_ - 1].end;
    return f;
}

ClassDecl Parser::classDecl() {
    ClassDecl c;
    c.pos = cur().pos;
    while (isIdent("private") || isIdent("internal") || isIdent("public") || isIdent("open") || isIdent("abstract"))
        if (take().text == "private") c.isPrivate = true;
    if (!isIdent("class")) fail("expected 'class'");
    take();
    c.name = expectIdent();
    if (isOp("(")) {  // the primary constructor: (val a: T, b: U = d)
        NewlineMode mode(newlines_, false);
        take();
        while (!isOp(")")) {
            annotations(nullptr);
            bool isPrivate = false, overrides = false;
            while (isIdent("private") || isIdent("public") || isIdent("internal") || isIdent("protected") || isIdent("override")) {
                std::string word = take().text;
                isPrivate = word == "private" || isPrivate;
                overrides = word == "override" || overrides;
            }
            char kind = ' ';
            if (isIdent("val") || isIdent("var")) kind = take().text == "val" ? 'v' : 'm';
            Param p;
            p.pos = cur().pos;
            p.name = expectIdent();
            expectOp(":");
            p.type = type();
            if (acceptOp("=")) p.defaultValue = expression();
            c.ctorParams.push_back(std::move(p));
            if (overrides && kind == ' ') fail(p.pos, "override needs val or var");
            c.ctorKinds.push_back(kind);
            c.ctorPrivate.push_back(isPrivate);
            c.ctorOverride.push_back(overrides);
            if (!acceptOp(",")) break;
        }
        expectOp(")");
    }
    if (acceptOp(":")) {  // : Base(args), A, B: the base class constructed, the interfaces named
        do {
            skipNewlines();
            Type t = type();
            if (!isOp("(")) {
                c.interfaces.push_back(std::move(t));
                continue;
            }
            if (c.base) fail(t.pos, "a class has one base class (" + c.base->name + "); the others are interfaces, without ()");
            c.base = std::move(t);
            NewlineMode mode(newlines_, false);
            take();
            while (!isOp(")")) {
                c.baseArgs.push_back(expression());
                if (!acceptOp(",")) break;
            }
            expectOp(")");
        } while (acceptOp(","));
    }
    if (opAfterNewlines("{")) {
        skipNewlines();
        take();
        members(c.properties, c.methods, c.order, c.initBlocks, "class");
    }
    c.end = tokens_[index_ - 1].end;
    return c;
}

void Parser::members(std::vector<ClassProperty>& properties, std::vector<Function>& methods, std::vector<size_t>& order,
                     std::vector<NodeP>& initBlocks, const char* what) {
    std::string kind = what;
    for (skipNewlines(); !isOp("}"); skipNewlines()) {
        if (acceptOp(";")) continue;  // (members on one line: var x = 0; fun f() { })
        if (isIdent("init")) {
            take();
            skipNewlines();
            initBlocks.push_back(block());
            continue;
        }
        // A property, or a function (its modifiers read by function()).
        size_t save = index_;
        bool isPrivate = false, overrides = false;
        annotations(nullptr);
        while (isIdent("private") || isIdent("internal") || isIdent("public") || isIdent("protected") || isIdent("open") ||
               isIdent("override") || isIdent("const")) {
            std::string word = take().text;
            isPrivate = word == "private" || isPrivate;
            overrides = word == "override" || overrides;
        }
        std::string a = kind == "object" || kind == "interface" ? "an " + kind : "a " + kind;
        if (isIdent("val") || isIdent("var")) {
            ClassProperty p;
            p.pos = cur().pos;
            p.isVar = take().text == "var";
            p.isPrivate = isPrivate;
            p.overrides = overrides;
            p.name = expectIdent();
            if (acceptOp(":")) p.type = type();
            if (isIdent("by")) {  // var x by mutableStateOf(0): read and set through the delegate
                if (kind == "interface") fail(a + "'s property can't be delegated (by …)");
                take();
                skipNewlines();
                p.delegated = true;
                p.init = expression();
            } else if (acceptOp("=")) {
                if (kind == "interface") fail(a + "'s property has no value: a getter (get() = …) or none (abstract)");
                skipNewlines();
                p.init = expression();
            }
            accessors(p);
            if (p.delegated && (p.getter || p.setter)) fail(p.pos, p.name + ": a delegated property has no get() or set() of its own");
            if (p.setter && !p.isVar) fail(p.pos, p.name + ": a val has no set()");
            if (!p.init && !p.getter && !p.type) fail(p.pos, a + "'s property needs a value, a type or a getter");
            if (kind == "object" && !p.init && !p.getter) fail(p.pos, a + "'s property needs a value or a getter");
            if (p.getter && p.init) fail(p.pos, p.name + ": a property with get() has no value of its own here");
            if (p.isVar && p.getter && !p.setter && kind != "interface") fail(p.pos, p.name + ": a var with get() needs set(v) { … } too here");
            order.push_back(properties.size() * 2);
            properties.push_back(std::move(p));
        } else {
            index_ = save;  // (function() reads the modifiers itself)
            bool wasAbstract = std::exchange(abstractMembers_, kind == "interface");
            Function f = function();
            abstractMembers_ = wasAbstract;
            if (f.composable) fail(f.pos, a + "'s function can't be @Kompozable here: write it at the top level");
            order.push_back(methods.size() * 2 + 1);
            methods.push_back(std::move(f));
        }
    }
    expectOp("}");
}

// After a property: get() = … / get() { … }, set(v) { … } / set(v) = …, [private] set, each on a line of its own
// (or on the property's).
void Parser::accessors(ClassProperty& p) {
    for (int i = 0; i < 2; ++i) {
        size_t save = index_;
        skipNewlines();
        bool isPrivate = false;
        while (isIdent("private") || isIdent("internal") || isIdent("public") || isIdent("protected"))
            isPrivate = take().text != "public" || isPrivate;
        if (isIdent("get") && peekRaw(1).kind == Tok::Op && peekRaw(1).text == "(") {
            if (p.getter) fail(p.name + " has get() twice");
            take();
            expectOp("(");
            expectOp(")");
            if (acceptOp(":")) {
                Type t = type();
                if (!p.type) p.type = std::move(t);
            }
            if (isOp("=")) {
                Pos at = take().pos;
                skipNewlines();
                auto ret = make(Node::Return, at);
                ret->kids.push_back(expression());
                p.getter = make(Node::Block, at);
                p.getter->kids.push_back(std::move(ret));
            } else {
                skipNewlines();
                p.getter = block();
            }
            continue;
        }
        if (isIdent("set")) {
            if (p.setter || p.privateSetter) fail(p.name + " has set twice");
            if (!p.isVar) fail(p.name + ": a val has no setter");
            take();
            p.privateSetter = isPrivate;
            if (isOp("(")) {  // set(value) { field = value }
                take();
                p.setterParam = expectIdent();
                if (acceptOp(":")) type();
                expectOp(")");
                if (isOp("=")) {
                    Pos at = take().pos;
                    skipNewlines();
                    auto stmt = make(Node::ExprStmt, at);
                    stmt->kids.push_back(expression());
                    p.setter = make(Node::Block, at);
                    p.setter->kids.push_back(std::move(stmt));
                } else {
                    skipNewlines();
                    p.setter = block();
                }
            } else if (!isPrivate) {
                fail("set needs (value) { … } here, or a visibility (private set)");
            }
            continue;
        }
        index_ = save;
        return;
    }
}

ObjectDecl Parser::objectDecl() {
    ObjectDecl o;
    o.pos = cur().pos;
    annotations(&o.annotations);
    if (isIdent("data")) take();  // data object (its toString its name: as an object's here)
    if (!isIdent("object")) fail("expected 'object' (a sealed class's body holds data classes and objects)");
    take();
    o.name = expectIdent();
    o.supers = supers();
    if (opAfterNewlines("{")) {
        skipNewlines();
        take();
        members(o.properties, o.methods, o.order, o.initBlocks, "object");
    }
    o.end = tokens_[index_ - 1].end;
    return o;
}

DataClass Parser::dataClass() {
    DataClass c;
    c.pos = cur().pos;
    annotations(&c.annotations);
    if (!isIdent("data")) fail("expected 'data class'");
    take();
    if (!isIdent("class")) fail("expected 'class' after 'data'");
    take();
    c.name = expectIdent();
    NewlineMode mode(newlines_, false);
    expectOp("(");
    while (!isOp(")")) {
        annotations(nullptr);
        bool overrides = false;
        while (isIdent("override") || isIdent("public")) overrides = take().text == "override" || overrides;
        bool isVar = isIdent("var");
        if (!isVar && !isIdent("val")) fail("a data class's parameters are val or var");
        take();
        Param p;
        p.pos = cur().pos;
        p.name = expectIdent();
        expectOp(":");
        p.type = type();
        if (acceptOp("=")) p.defaultValue = expression();
        c.fields.push_back(std::move(p));
        c.mutableFields.push_back(isVar);
        c.overridingFields.push_back(overrides);
        if (!acceptOp(",")) break;
    }
    expectOp(")");
    c.supers = supers();
    c.end = tokens_[index_ - 1].end;
    if (c.fields.empty()) fail(c.pos, "a data class needs a parameter");
    return c;
}

// : Sealed(), A, B after a data class or an object: a sealed class it is a case of (constructed, with no
// arguments), interfaces and sealed interfaces (named).
std::vector<Type> Parser::supers() {
    std::vector<Type> out;
    if (!acceptOp(":")) return out;
    do {
        skipNewlines();
        out.push_back(type());
        if (acceptOp("(")) {
            if (!isOp(")")) fail("a sealed class with a constructor isn't supported yet: give its subclasses the fields");
            expectOp(")");
        }
    } while (acceptOp(","));
    return out;
}

SealedDecl Parser::sealedDecl() {
    SealedDecl s;
    s.pos = cur().pos;
    annotations(nullptr);
    while (isIdent("private") || isIdent("internal") || isIdent("public")) take();
    if (!isIdent("sealed")) fail("expected 'sealed'");
    take();
    if (isIdent("interface")) s.isInterface = true;
    else if (!isIdent("class")) fail("expected 'class' or 'interface' after 'sealed'");
    take();
    s.name = expectIdent();
    if (isOp("(")) fail("a sealed class with a constructor isn't supported yet: give its subclasses the fields");
    if (isOp(":")) fail("a sealed class with a base of its own isn't supported");
    if (opAfterNewlines("{")) {
        skipNewlines();
        take();
        for (skipNewlines(); !isOp("}"); skipNewlines()) {
            size_t save = index_;
            annotations(nullptr);
            bool data = isIdent("data") && peekRaw(1).kind == Tok::Ident && peekRaw(1).text == "class";
            index_ = save;
            if (data) {
                DataClass d = dataClass();
                if (!namesSuper(d.supers, s.name)) fail(d.pos, d.name + ": a subclass of " + s.name + " is written `: " + s.name + (s.isInterface ? "`" : "()`"));
                s.order.emplace_back('d', s.dataClasses.size());
                s.dataClasses.push_back(std::move(d));
            } else {
                ObjectDecl o = objectDecl();
                if (!namesSuper(o.supers, s.name)) fail(o.pos, o.name + ": a subclass of " + s.name + " is written `: " + s.name + (s.isInterface ? "`" : "()`"));
                s.order.emplace_back('o', s.objects.size());
                s.objects.push_back(std::move(o));
            }
        }
        expectOp("}");
    }
    s.end = tokens_[index_ - 1].end;
    return s;
}

InterfaceDecl Parser::interfaceDecl() {
    InterfaceDecl i;
    i.pos = cur().pos;
    annotations(nullptr);
    while (isIdent("private") || isIdent("internal") || isIdent("public")) take();
    if (isIdent("fun")) {
        take();
        i.isFun = true;
    }
    if (!isIdent("interface")) fail("expected 'interface'");
    take();
    i.name = expectIdent();
    if (isOp("<")) fail("a generic interface isn't supported yet");
    if (acceptOp(":")) {
        do {
            skipNewlines();
            i.bases.push_back(type());
            if (isOp("(")) fail("an interface extends interfaces only (no constructor call)");
        } while (acceptOp(","));
    }
    if (opAfterNewlines("{")) {
        skipNewlines();
        take();
        std::vector<NodeP> initBlocks;
        members(i.properties, i.methods, i.order, initBlocks, "interface");
        if (!initBlocks.empty()) fail(i.pos, "an interface has no init block");
    }
    i.end = tokens_[index_ - 1].end;
    if (i.isFun) {
        size_t abstract = 0;
        for (const Function& m : i.methods) abstract += m.isAbstract;
        for (const ClassProperty& p : i.properties) abstract += !p.getter;
        if (abstract != 1 || i.methods.empty())
            fail(i.pos, "a fun interface has one abstract function (and nothing else abstract)");
    }
    return i;
}

EnumClass Parser::enumClass() {
    EnumClass e;
    e.pos = cur().pos;
    if (!isIdent("enum")) fail("expected 'enum class'");
    take();
    if (!isIdent("class")) fail("expected 'class' after 'enum'");
    take();
    e.name = expectIdent();
    skipNewlines();
    expectOp("{");
    for (skipNewlines(); !isOp("}"); skipNewlines()) {
        e.entries.push_back(expectIdent());
        skipNewlines();
        if (!acceptOp(",")) break;
    }
    skipNewlines();
    if (acceptOp(";")) skipNewlines();  // (Kotlin allows one after the entries)
    expectOp("}");
    e.end = tokens_[index_ - 1].end;
    if (e.entries.empty()) fail(e.pos, "an enum class needs an entry");
    return e;
}

Property Parser::property() {
    Property p;
    p.pos = cur().pos;
    while (isIdent("private") || isIdent("internal") || isIdent("public") || isIdent("const"))
        if (take().text == "private") p.isPrivate = true;
    if (isIdent("var")) fail("a top-level var isn't supported: keep state in a composable (remember) or a state holder");
    if (!isIdent("val")) fail("expected 'val'");
    p.decl = declaration();
    if (p.decl->delegated) fail(p.decl->pos, "a top-level val by … isn't supported");
    if (!p.decl->names.empty()) fail(p.decl->pos, "a top-level val can't be destructured");
    if (p.decl->kids.empty()) fail(p.decl->pos, "a top-level val needs a value");
    p.name = p.decl->text;
    p.end = tokens_[index_ - 1].end;
    return p;
}

std::vector<Param> Parser::params() {
    NewlineMode mode(newlines_, false);
    expectOp("(");
    std::vector<Param> out;
    while (!isOp(")")) {
        annotations(nullptr);
        Param p;
        p.pos = cur().pos;
        if (isIdent("val") || isIdent("var")) take();
        p.name = expectIdent();
        expectOp(":");
        p.type = type();
        if (acceptOp("=")) p.defaultValue = expression();
        out.push_back(std::move(p));
        if (!acceptOp(",")) break;
    }
    expectOp(")");
    return out;
}

Type Parser::type() {
    Type t;
    t.pos = cur().pos;
    std::vector<std::string> names;
    if (isOp("@")) annotations(&names);
    for (auto& n : names) {
        if (n == "Kompozable") t.composable = true;
        else if (n == "Composable") fail(t.pos, "a composable function type is @Kompozable () -> Unit (not @Composable)");
    }
    if (isOp("(")) {
        take();
        std::vector<Type> inner;
        {
            NewlineMode mode(newlines_, false);
            while (!isOp(")")) {
                if (cur().kind == Tok::Ident && peekRaw(1).kind == Tok::Op && peekRaw(1).text == ":") {  // (name: Type) -> R
                    take();
                    take();
                }
                inner.push_back(type());
                if (!acceptOp(",")) break;
            }
            expectOp(")");
        }
        if (acceptOp("->")) {
            t.function = true;
            t.params = std::move(inner);
            t.result = std::make_unique<Type>(type());
        } else if (inner.size() == 1) {  // (() -> Unit)?: a type in parentheses
            bool composable = t.composable;
            t = std::move(inner[0]);
            t.composable = t.composable || composable;
        } else {
            fail(t.pos, "expected '->' after a function type's parameters");
        }
    } else if (isOp("*")) {
        take();
        t.name = "*";
    } else {
        t.name = expectIdent();
        while (isOp(".") && peekRaw(1).kind == Tok::Ident) {
            take();
            t.name += "." + take().text;
        }
        if (isOp("<")) {
            take();
            NewlineMode mode(newlines_, false);
            while (!isOp(">")) {
                if (isIdent("out") || isIdent("in")) take();
                t.args.push_back(type());
                if (!acceptOp(",")) break;
            }
            expectOp(">");
        }
        if (isOp(".") && peekRaw(1).kind == Tok::Op && peekRaw(1).text == "(") {  // RowScope.() -> Unit
            take();
            Type fn = type();
            if (!fn.function) fail(t.pos, "expected a function type after the receiver");
            fn.receiver = std::make_unique<Type>(std::move(t));
            fn.composable = fn.composable || fn.receiver->composable;
            fn.receiver->composable = false;
            fn.pos = fn.receiver->pos;
            t = std::move(fn);
        }
    }
    if (isOp("?") && !cur().spaceBefore) {
        take();
        t.nullable = true;
    }
    return t;
}

// ---- Statements ---------------------------------------------------------------------------------

NodeP Parser::block() {
    NewlineMode mode(newlines_, true);
    auto b = make(Node::Block, cur().pos);
    expectOp("{");
    statements(*b, true);
    expectOp("}");
    return b;
}

NodeP Parser::body() {
    skipNewlines();
    if (isOp("{")) return block();
    auto b = make(Node::Block, cur().pos);
    b->kids.push_back(statement());
    return b;
}

void Parser::statements(Node& into, bool untilBrace) {
    for (;;) {
        while (peekRaw().kind == Tok::Newline || (peekRaw().kind == Tok::Op && peekRaw().text == ";")) ++index_;
        const Token& t = peekRaw();
        if (t.kind == Tok::End) {
            if (untilBrace) fail(t.pos, "expected '}'");
            return;
        }
        if (untilBrace && t.kind == Tok::Op && t.text == "}") return;
        into.kids.push_back(statement());
        const Token& after = peekRaw();
        bool ends = after.kind == Tok::Newline || after.kind == Tok::End || (after.kind == Tok::Op && (after.text == ";" || after.text == "}"));
        if (!ends) fail(after.pos, "expected a new line or ';' after the statement");
    }
}

NodeP Parser::statement() {
    const Token& t = cur();
    if (t.kind == Tok::Ident) {
        if (t.text == "val" || t.text == "var") return declaration();
        if (t.text == "return") {
            auto r = make(Node::Return, take().pos);
            if (isOp("@") && !cur().spaceBefore && peekRaw(1).kind == Tok::Ident) {  // return@Label: from the lambda (a C++ lambda's return)
                take();
                r->text = take().text;
            }
            const Token& n = peekRaw();
            if (!(n.kind == Tok::Newline || n.kind == Tok::End || (n.kind == Tok::Op && (n.text == ";" || n.text == "}"))))
                r->kids.push_back(expression());
            return r;
        }
        if (t.text == "break") return make(Node::Break, take().pos);
        if (t.text == "continue") return make(Node::Continue, take().pos);
        if (t.text == "for") {
            auto f = make(Node::For, take().pos);
            {
                NewlineMode mode(newlines_, false);
                expectOp("(");
                if (acceptOp("(")) {  // for ((i, x) in ...)
                    do {
                        f->names.push_back(expectIdent());
                        if (acceptOp(":")) type();
                    } while (acceptOp(","));
                    expectOp(")");
                } else {
                    f->names.push_back(expectIdent());
                    if (acceptOp(":")) type();
                }
                if (!isIdent("in")) fail("expected 'in'");
                take();
                f->kids.push_back(expression());
                expectOp(")");
            }
            f->kids.push_back(body());
            return f;
        }
        if (t.text == "while") {
            auto w = make(Node::While, take().pos);
            {
                NewlineMode mode(newlines_, false);
                expectOp("(");
                w->kids.push_back(expression());
                expectOp(")");
            }
            w->kids.push_back(body());
            return w;
        }
        if (t.text == "try") {
            auto y = make(Node::Try, take().pos);
            y->kids.push_back(block());
            for (;;) {  // catch and finally may follow on later lines; nothing else is taken
                size_t before = index_;
                skipNewlines();
                if (isIdent("catch")) {
                    take();
                    {
                        NewlineMode mode(newlines_, false);
                        expectOp("(");
                        if (cur().kind != Tok::Ident) fail(cur().pos, "expected the exception's name");
                        y->names.push_back(take().text);
                        expectOp(":");
                        y->typeArgs.push_back(type());
                        expectOp(")");
                    }
                    y->kids.push_back(block());
                    continue;
                }
                if (isIdent("finally")) {
                    take();
                    y->flag = true;
                    y->kids.push_back(block());
                }
                else index_ = before;
                break;
            }
            return y;
        }
        if (t.text == "do") {
            auto d = make(Node::DoWhile, take().pos);
            d->kids.push_back(body());
            skipNewlines();
            if (!isIdent("while")) fail("expected 'while' after do { }");
            take();
            NewlineMode mode(newlines_, false);
            expectOp("(");
            d->kids.push_back(expression());
            expectOp(")");
            return d;
        }
    }
    NodeP e = expression();
    for (std::string_view op : {"=", "+=", "-=", "*=", "/=", "%="}) {
        if (isOp(op)) {
            auto a = make(Node::Assign, take().pos);
            a->text = std::string(op);
            a->kids.push_back(std::move(e));
            skipNewlines();
            a->kids.push_back(expression());
            return a;
        }
    }
    auto s = make(Node::ExprStmt, e->pos);
    s->kids.push_back(std::move(e));
    return s;
}

NodeP Parser::declaration() {
    auto d = make(Node::Decl, cur().pos);
    d->flag = take().text == "var";
    if (acceptOp("(")) {  // val (a, b) = pair
        NewlineMode mode(newlines_, false);
        do {
            d->names.push_back(expectIdent());
            if (acceptOp(":")) type();
        } while (acceptOp(","));
        expectOp(")");
    } else {
        d->text = expectIdent();
    }
    if (acceptOp(":")) d->type = type();
    if (isIdent("by")) {
        take();
        d->delegated = true;
        skipNewlines();
        d->kids.push_back(expression());
    } else if (acceptOp("=")) {
        skipNewlines();
        d->kids.push_back(expression());
    }
    return d;
}

// ---- Expressions --------------------------------------------------------------------------------

NodeP Parser::expression() { return disjunction(); }

NodeP Parser::disjunction() {
    NodeP l = conjunction();
    while (opAfterNewlines("||")) {
        auto b = make(Node::Binary, take().pos);
        b->text = "||";
        skipNewlines();
        b->kids.push_back(std::move(l));
        b->kids.push_back(conjunction());
        l = std::move(b);
    }
    return l;
}

NodeP Parser::conjunction() {
    NodeP l = equality();
    while (opAfterNewlines("&&")) {
        auto b = make(Node::Binary, take().pos);
        b->text = "&&";
        skipNewlines();
        b->kids.push_back(std::move(l));
        b->kids.push_back(equality());
        l = std::move(b);
    }
    return l;
}

NodeP Parser::equality() {
    NodeP l = comparison();
    while (isOp("==") || isOp("!=") || isOp("===") || isOp("!==")) {
        auto b = make(Node::Binary, cur().pos);
        b->text = take().text;
        skipNewlines();
        b->kids.push_back(std::move(l));
        b->kids.push_back(comparison());
        l = std::move(b);
    }
    return l;
}

NodeP Parser::comparison() {
    NodeP l = namedCheck();
    while (isOp("<") || isOp(">") || isOp("<=") || isOp(">=")) {
        auto b = make(Node::Binary, cur().pos);
        b->text = take().text;
        skipNewlines();
        b->kids.push_back(std::move(l));
        b->kids.push_back(namedCheck());
        l = std::move(b);
    }
    return l;
}

NodeP Parser::namedCheck() {
    NodeP l = elvis();
    for (;;) {
        bool negated = isOp("!") && peekRaw(1).kind == Tok::Ident && (peekRaw(1).text == "in" || peekRaw(1).text == "is");
        if (!negated && !isIdent("in") && !isIdent("is")) return l;
        Pos at = cur().pos;
        if (negated) take();
        std::string word = take().text;
        if (word == "is") {
            auto n = make(Node::Is, at);
            n->flag = negated;
            n->kids.push_back(std::move(l));
            n->type = type();
            l = std::move(n);
        } else {
            auto b = make(Node::Binary, at);
            b->text = negated ? "!in" : "in";
            b->kids.push_back(std::move(l));
            b->kids.push_back(elvis());
            l = std::move(b);
        }
    }
}

NodeP Parser::elvis() {
    NodeP l = infixCall();
    while (opAfterNewlines("?:")) {
        auto b = make(Node::Binary, take().pos);
        b->text = "?:";
        skipNewlines();
        b->kids.push_back(std::move(l));
        b->kids.push_back(infixCall());
        l = std::move(b);
    }
    return l;
}

// a until b, a downTo b, r step 2, a to b: a name between two operands on one line.
NodeP Parser::infixCall() {
    NodeP l = range();
    while (cur().kind == Tok::Ident && !kKeywords.contains(cur().text)) {
        auto b = make(Node::Binary, cur().pos);
        b->text = take().text;
        skipNewlines();
        b->kids.push_back(std::move(l));
        b->kids.push_back(range());
        l = std::move(b);
    }
    return l;
}

NodeP Parser::range() {
    NodeP l = additive();
    while (isOp("..") || isOp("..<")) {
        auto b = make(Node::Binary, cur().pos);
        b->text = take().text;
        skipNewlines();
        b->kids.push_back(std::move(l));
        b->kids.push_back(additive());
        l = std::move(b);
    }
    return l;
}

NodeP Parser::additive() {
    NodeP l = multiplicative();
    while (isOp("+") || isOp("-")) {
        auto b = make(Node::Binary, cur().pos);
        b->text = take().text;
        skipNewlines();
        b->kids.push_back(std::move(l));
        b->kids.push_back(multiplicative());
        l = std::move(b);
    }
    return l;
}

NodeP Parser::multiplicative() {
    NodeP l = asExpression();
    while (isOp("*") || isOp("/") || isOp("%")) {
        auto b = make(Node::Binary, cur().pos);
        b->text = take().text;
        skipNewlines();
        b->kids.push_back(std::move(l));
        b->kids.push_back(asExpression());
        l = std::move(b);
    }
    return l;
}

NodeP Parser::asExpression() {
    NodeP l = prefix();
    while (isIdent("as")) {
        auto n = make(Node::As, take().pos);
        if (isOp("?") && !cur().spaceBefore) {
            take();
            n->flag = true;
        }
        n->kids.push_back(std::move(l));
        n->type = type();
        l = std::move(n);
    }
    return l;
}

NodeP Parser::prefix() {
    for (std::string_view op : {"-", "+", "!", "++", "--"}) {
        if (isOp(op)) {
            auto u = make(Node::Unary, take().pos);
            u->text = std::string(op);
            u->kids.push_back(prefix());
            return u;
        }
    }
    return postfix();
}

bool Parser::typeArguments(std::vector<Type>& out) {
    if (!isOp("<") || cur().spaceBefore) return false;
    size_t saved = index_;
    try {
        NewlineMode mode(newlines_, false);
        take();
        std::vector<Type> args;
        while (!isOp(">")) {
            args.push_back(type());
            if (!acceptOp(",")) break;
        }
        expectOp(">");
        if (isOp("(") || isOp("{") || isOp("::")) {
            out = std::move(args);
            return true;
        }
    } catch (const CompileError&) {
    }
    index_ = saved;
    return false;
}

NodeP Parser::postfix() {
    NodeP e = primary();
    for (;;) {
        std::vector<Type> generic;
        bool generics = e->kind == Node::Name || e->kind == Node::Member ? typeArguments(generic) : false;
        if (isOp("(") && (generics || !cur().spaceBefore || true)) {
            auto call = make(Node::Call, cur().pos);
            call->typeArgs = std::move(generic);
            call->kids.push_back(std::move(e));
            arguments(*call);
            if (isOp("{")) call->lambda = lambda();  // f(x) { ... }: on the same line
            e = std::move(call);
        } else if (isOp("{")) {  // f { ... }
            auto call = make(Node::Call, cur().pos);
            call->typeArgs = std::move(generic);
            call->kids.push_back(std::move(e));
            call->lambda = lambda();
            e = std::move(call);
        } else if (opAfterNewlines(".") || opAfterNewlines("?.")) {
            auto m = make(Node::Member, cur().pos);
            m->flag = take().text == "?.";
            skipNewlines();
            m->text = expectIdent();
            m->kids.push_back(std::move(e));
            e = std::move(m);
        } else if (isOp("[") ) {
            auto ix = make(Node::Index, take().pos);
            NewlineMode mode(newlines_, false);
            ix->kids.push_back(std::move(e));
            do ix->kids.push_back(expression());
            while (acceptOp(","));
            expectOp("]");
            e = std::move(ix);
        } else if (isOp("!!")) {
            auto u = make(Node::Unary, take().pos);
            u->text = "!!";
            u->flag = true;
            u->kids.push_back(std::move(e));
            e = std::move(u);
        } else if ((isOp("++") || isOp("--")) && !cur().spaceBefore) {
            auto u = make(Node::Unary, cur().pos);
            u->text = take().text;
            u->flag = true;
            u->kids.push_back(std::move(e));
            e = std::move(u);
        } else {
            return e;
        }
    }
}

void Parser::arguments(Node& call) {
    NewlineMode mode(newlines_, false);
    expectOp("(");
    while (!isOp(")")) {
        std::string name;
        if (cur().kind == Tok::Ident && peekRaw(1).kind == Tok::Op && peekRaw(1).text == "=") {
            name = take().text;
            take();
        }
        acceptOp("*");  // (a spread: passed as it is)
        call.kids.push_back(expression());
        call.names.push_back(name);
        if (!acceptOp(",")) break;
    }
    expectOp(")");
}

// object : NestedScrollConnection { override fun onPreScroll(available: Offset, source: NestedScrollSource): Offset { … } }
NodeP Parser::objectExpression() {
    auto o = make(Node::Object, take().pos);
    expectOp(":");
    o->type = type();
    if (isOp("(")) {  // a class's constructor: object : Base() { … }
        NewlineMode mode(newlines_, false);
        take();
        expectOp(")");
    }
    skipNewlines();
    expectOp("{");
    for (skipNewlines(); !isOp("}"); skipNewlines()) {
        annotations(nullptr);
        bool overrides = false;
        while (isIdent("override") || isIdent("public") || isIdent("private"))
            overrides = take().text == "override" || overrides;
        if (isIdent("val")) {  // override val name: Type get() = …, or = …: a property (flag), its value the body's
            auto m = make(Node::Method, take().pos);
            m->flag = true;
            m->text = expectIdent();
            if (!overrides) fail("an object's property here overrides one of its type's (override val …)");
            if (acceptOp(":")) m->type = type();
            {
                NewlineMode mode(newlines_, false);
                if (isIdent("get")) {
                    take();
                    expectOp("(");
                    expectOp(")");
                }
            }
            if (isOp("=")) {
                Pos at = take().pos;
                skipNewlines();
                auto ret = make(Node::Return, at);
                ret->kids.push_back(expression());
                auto body = make(Node::Block, at);
                body->kids.push_back(std::move(ret));
                m->kids.push_back(std::move(body));
            } else {
                skipNewlines();
                m->kids.push_back(block());
            }
            o->kids.push_back(std::move(m));
            continue;
        }
        if (!isIdent("fun")) fail("an object here has functions (override fun …) and properties (override val …) only");
        if (!overrides) fail("an object's function here overrides one of its type's (override fun …)");
        auto m = make(Node::Method, take().pos);
        m->text = expectIdent();
        NodeP receiver;  // fun DrawScope.onDraw(): the receiver's type (kids[1], after the body)
        if (acceptOp(".")) {
            receiver = make(Node::Name, m->pos);
            receiver->text = m->text;
            m->text = expectIdent();
        }
        for (Param& p : params()) {
            m->names.push_back(p.name);
            m->typeArgs.push_back(std::move(p.type));
        }
        if (acceptOp(":")) m->type = type();
        if (isOp("=")) {
            Pos at = take().pos;
            skipNewlines();
            auto ret = make(Node::Return, at);
            ret->kids.push_back(expression());
            auto body = make(Node::Block, at);
            body->kids.push_back(std::move(ret));
            m->kids.push_back(std::move(body));
        } else {
            skipNewlines();
            m->kids.push_back(block());
        }
        if (receiver) m->kids.push_back(std::move(receiver));
        o->kids.push_back(std::move(m));
    }
    expectOp("}");
    return o;
}

NodeP Parser::primary() {
    const Token& t = cur();
    switch (t.kind) {
        case Tok::Number: {
            auto n = make(Node::Number, t.pos);
            n->text = take().text;
            return n;
        }
        case Tok::String: return string(take());
        case Tok::Char: {
            auto n = make(Node::Char, t.pos);
            n->text = take().text;
            return n;
        }
        case Tok::Ident: {
            if (t.text == "true" || t.text == "false") {
                auto n = make(Node::Bool, t.pos);
                n->text = take().text;
                return n;
            }
            if (t.text == "null") return make(Node::Null, take().pos);
            if (t.text == "this") {  // this, or this@Label: the receiver of the lambda Label(…) { } around it
                auto n = make(Node::This, take().pos);
                if (isOp("@") && !cur().spaceBefore && peekRaw(1).kind == Tok::Ident) {
                    take();
                    n->text = take().text;
                }
                return n;
            }
            if (t.text == "if") return ifExpression();
            if (t.text == "when") return whenExpression();
            if (t.text == "object" && peekRaw(1).kind == Tok::Op && peekRaw(1).text == ":") return objectExpression();
            if (kKeywords.contains(t.text)) fail("unexpected '" + t.text + "'");
            auto n = make(Node::Name, t.pos);
            n->text = take().text;
            return n;
        }
        case Tok::Op:
            if (t.text == "(") {
                auto p = make(Node::Paren, take().pos);
                NewlineMode mode(newlines_, false);
                p->kids.push_back(expression());
                expectOp(")");
                return p;
            }
            if (t.text == "{") return lambda();
            fail("unexpected '" + t.text + "'");
        case Tok::Newline: fail("unexpected end of line");
        case Tok::End: fail("unexpected end of the function");
    }
    fail("unexpected token");
}

NodeP Parser::lambda() {
    NewlineMode mode(newlines_, true);
    auto l = make(Node::Lambda, cur().pos);
    expectOp("{");
    // { a, b -> ... }: parameters, if an arrow follows them.
    size_t saved = index_;
    {
        NewlineMode inner(newlines_, false);
        std::vector<std::string> names;
        bool ok = true;
        while (ok) {
            if (cur().kind != Tok::Ident || kKeywords.contains(cur().text)) {
                ok = false;
                break;
            }
            names.push_back(take().text);
            if (acceptOp(":")) {
                try {
                    type();
                } catch (const CompileError&) {
                    ok = false;
                    break;
                }
            }
            if (!acceptOp(",")) break;
        }
        if (ok && !names.empty() && acceptOp("->")) {
            l->names = std::move(names);
            l->flag = true;  // parameters written
        } else {
            index_ = saved;
        }
    }
    statements(*l, true);
    expectOp("}");
    return l;
}

NodeP Parser::ifExpression() {
    auto n = make(Node::If, take().pos);
    {
        NewlineMode mode(newlines_, false);
        expectOp("(");
        n->kids.push_back(expression());
        expectOp(")");
    }
    n->kids.push_back(body());
    size_t saved = index_;
    skipNewlines();
    if (isIdent("else")) {
        take();
        n->kids.push_back(body());
    } else {
        index_ = saved;
    }
    return n;
}

NodeP Parser::whenExpression() {
    auto n = make(Node::When, take().pos);
    if (isOp("(")) {
        NewlineMode mode(newlines_, false);
        take();
        if (isIdent("val")) {  // when (val x = f())
            take();
            auto d = make(Node::Decl, cur().pos);
            d->text = expectIdent();
            expectOp("=");
            d->kids.push_back(expression());
            n->kids.push_back(std::move(d));
        } else {
            n->kids.push_back(expression());
        }
        expectOp(")");
    } else {
        n->kids.push_back(nullptr);
    }
    skipNewlines();
    NewlineMode mode(newlines_, true);
    expectOp("{");
    for (;;) {
        while (peekRaw().kind == Tok::Newline || (peekRaw().kind == Tok::Op && peekRaw().text == ";")) ++index_;
        if (isOp("}")) break;
        Node::WhenBranch br;
        br.pos = cur().pos;
        if (isIdent("else")) {
            take();
        } else {
            NewlineMode conds(newlines_, false);
            do {
                bool negated = isOp("!") && peekRaw(1).kind == Tok::Ident && (peekRaw(1).text == "in" || peekRaw(1).text == "is");
                if (negated) take();
                if (isIdent("in")) {
                    take();
                    br.conds.emplace_back(negated ? Node::WhenBranch::NotIn : Node::WhenBranch::In, expression());
                    br.types.emplace_back();
                } else if (isIdent("is")) {
                    take();
                    br.conds.emplace_back(negated ? Node::WhenBranch::NotIs : Node::WhenBranch::Is, nullptr);
                    br.types.push_back(type());
                } else {
                    br.conds.emplace_back(Node::WhenBranch::Expr, expression());
                    br.types.emplace_back();
                }
            } while (acceptOp(",") && !isOp("->"));
        }
        expectOp("->");
        br.body = body();
        n->branches.push_back(std::move(br));
    }
    expectOp("}");
    return n;
}

NodeP Parser::string(const Token& t) {
    auto s = make(Node::String, t.pos);
    s->parts = t.parts;
    for (const StringPart& part : t.parts) {
        if (!part.expr) continue;
        if (part.text.empty() || !(std::isalpha(static_cast<unsigned char>(part.text[0])) || part.text[0] == '_') ||
            part.text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos) {
            Parser inner(src_, part.pos);  // ${expression}
            s->kids.push_back(inner.templateExpression());
        } else {
            auto n = make(Node::Name, part.pos);  // $name
            n->text = part.text;
            s->kids.push_back(std::move(n));
        }
    }
    return s;
}

NodeP Parser::templateExpression() {
    NewlineMode mode(newlines_, false);
    NodeP e = expression();
    if (!isOp("}")) fail("expected '}' after the template's expression");
    return e;
}

}  // namespace kompozc
