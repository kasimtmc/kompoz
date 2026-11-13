#include "parser.hpp"

#include <cctype>
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
    "package", "return", "super", "this", "throw", "true", "try", "typealias", "val", "var", "when", "while"};

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
               isIdent("protected") || isIdent("open") || isIdent("suspend") || isIdent("abstract") || isIdent("operator") || isIdent("infix")) {
            std::string word = take().text;
            if (word == "operator") f.isOperator = true;
            if (word == "private") f.isPrivate = true;
            if (word == "override") f.overrides = true;
            if (word == "suspend") f.isSuspend = true;
            if (word == "open") f.isOpen = true;
            if (word == "abstract") f.isAbstract = true;
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
    f.typeParams = typeParams();  // fun <T> name
    // fun name, or fun Receiver.name (an extension; a generic receiver's type read whole: fun <T> List<T>.second())
    Pos at = cur().pos;
    if (cur().kind == Tok::Ident && peekRaw(1).kind == Tok::Op && peekRaw(1).text == "<" && !peekRaw(1).spaceBefore) {
        f.receiver = type();
        expectOp(".");
        f.name = expectIdent();
    } else {
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
    }
    if (f.composable && f.isSuspend) fail(f.pos, "a @Kompozable function can't be suspend");
    f.params = params();
    if (acceptOp(":")) f.result = type();
    if (f.isAbstract || (abstractMembers_ && !isOp("=") && !opAfterNewlines("{"))) {  // an interface's fun without a body, an abstract one
        if (isOp("=") || (f.isAbstract && opAfterNewlines("{") && !abstractMembers_)) fail(f.pos, "an abstract fun has no body");
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
    while (isIdent("private") || isIdent("internal") || isIdent("public") || isIdent("open") || isIdent("abstract") || isIdent("inner")) {
        std::string word = take().text;
        if (word == "private") c.isPrivate = true;
        if (word == "abstract") c.isAbstract = true;
        if (word == "open") c.isOpen = true;
    }
    if (!isIdent("class")) fail("expected 'class'");
    take();
    c.name = expectIdent();
    c.typeParams = typeParams();
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
                std::string name;
                if (cur().kind == Tok::Ident && peekRaw(1).kind == Tok::Op && peekRaw(1).text == "=") {
                    name = take().text;
                    take();
                }
                c.baseArgNames.push_back(name);
                c.baseArgs.push_back(expression());
                if (!acceptOp(",")) break;
            }
            expectOp(")");
        } while (acceptOp(","));
    }
    if (opAfterNewlines("{")) {
        skipNewlines();
        take();
        NestedDecls* was = std::exchange(nested_, &c.nested);
        members(c.properties, c.methods, c.order, c.initBlocks, "class", &c.companion);
        nested_ = was;
    }
    c.end = tokens_[index_ - 1].end;
    return c;
}

void Parser::members(std::vector<ClassProperty>& properties, std::vector<Function>& methods, std::vector<size_t>& order,
                     std::vector<NodeP>& initBlocks, const char* what, std::shared_ptr<ObjectDecl>* companion) {
    std::string kind = what;
    for (skipNewlines(); !isOp("}"); skipNewlines()) {
        if (acceptOp(";")) continue;  // (members on one line: var x = 0; fun f() { })
        if (isIdent("companion")) {  // companion object [Name] { … }: the class's own static members
            Pos at = take().pos;
            if (!companion) fail(at, "an " + kind + " has no companion object");
            if (*companion) fail(at, "a class has one companion object");
            if (!isIdent("object")) fail("expected 'object' after 'companion'");
            take();
            auto o = std::make_shared<ObjectDecl>();
            o->isCompanion = true;
            o->pos = at;
            o->name = cur().kind == Tok::Ident ? take().text : "Companion";
            o->supers = supers();  // (its interfaces: companion object : Factory<Foo>)
            if (opAfterNewlines("{")) {
                skipNewlines();
                take();
                members(o->properties, o->methods, o->order, o->initBlocks, "object");
            }
            o->end = tokens_[index_ - 1].end;
            *companion = std::move(o);
            continue;
        }
        if (isIdent("init")) {
            take();
            skipNewlines();
            initBlocks.push_back(block());
            continue;
        }
        {  // A nested class, data class, enum, object or interface.
            size_t save = index_;
            annotations(nullptr);
            std::set<std::string> words;
            while (isIdent("private") || isIdent("internal") || isIdent("public") || isIdent("protected") || isIdent("open") ||
                   isIdent("abstract") || isIdent("data") || isIdent("enum") || isIdent("sealed") || isIdent("inner") || isIdent("fun"))
                words.insert(take().text);
            std::string keyword = cur().kind == Tok::Ident ? cur().text : std::string();
            bool isNested = keyword == "class" || keyword == "interface" || (keyword == "object" && !words.contains("fun"));
            Pos at = cur().pos;
            index_ = save;
            if (isNested) {
                if (!nested_) fail(at, "a nested class isn't supported in " + std::string(kind == "object" || kind == "interface" || kind == "enum class" ? "an " : "a ") + kind);
                if (words.contains("inner") && (keyword != "class" || words.contains("data") || words.contains("enum")))
                    fail(at, "inner is a class's (inner class Name)");
                if (words.contains("inner") && kind != "class" && kind != "data class" && kind != "enum class")
                    fail(at, "an " + kind + " has no inner class, as in Kotlin: a nested one (without inner)");
                NestedDecls* into = nested_;
                if (words.contains("sealed")) into->sealeds.push_back(std::make_shared<SealedDecl>(sealedDecl()));
                else if (words.contains("data") && keyword == "class") into->dataClasses.push_back(std::make_shared<DataClass>(dataClass()));
                else if (words.contains("enum")) into->enums.push_back(std::make_shared<EnumClass>(enumClass()));
                else if (keyword == "object") into->objects.push_back(std::make_shared<ObjectDecl>(objectDecl()));
                else if (keyword == "interface") into->interfaces.push_back(std::make_shared<InterfaceDecl>(interfaceDecl()));
                else if (words.contains("inner")) into->innerClasses.push_back(std::make_shared<ClassDecl>(classDecl()));
                else into->classes.push_back(std::make_shared<ClassDecl>(classDecl()));
                continue;
            }
        }
        // A property, or a function (its modifiers read by function()).
        size_t save = index_;
        bool isPrivate = false, overrides = false, isOpen = false, isAbstract = false, isLateinit = false;
        annotations(nullptr);
        while (isIdent("private") || isIdent("internal") || isIdent("public") || isIdent("protected") || isIdent("open") ||
               isIdent("override") || isIdent("const") || isIdent("abstract") || isIdent("lateinit")) {
            std::string word = take().text;
            isLateinit = word == "lateinit" || isLateinit;
            isPrivate = word == "private" || isPrivate;
            overrides = word == "override" || overrides;
            isOpen = word == "open" || isOpen;
            isAbstract = word == "abstract" || isAbstract;
        }
        std::string a = kind == "object" || kind == "interface" ? "an " + kind : "a " + kind;
        if (isIdent("val") || isIdent("var")) {
            ClassProperty p;
            p.pos = cur().pos;
            p.isVar = take().text == "var";
            p.isPrivate = isPrivate;
            p.overrides = overrides;
            p.isOpen = isOpen;
            p.isAbstract = isAbstract;
            p.isLateinit = isLateinit;
            if (isLateinit && (!p.isVar || kind != "class"))
                fail(p.pos, "lateinit is a class's var's (lateinit var name: Type)");
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
            if (p.isAbstract && (p.init || p.getter || p.setter)) fail(p.pos, p.name + ": an abstract property has no value or accessors");
            if (p.isLateinit && (p.init || p.getter || p.setter || p.delegated || !p.type)) fail(p.pos, p.name + ": a lateinit var has a type and nothing else");
            if (p.isAbstract && !p.type) fail(p.pos, p.name + ": an abstract property needs a type");
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
    // A nested sealed class's cases beside it in the body (data class Ready(…) : State()): its variant's too.
    if (nested_)
        for (auto& sealed : nested_->sealeds) {
            for (auto& d : nested_->dataClasses)
                if (namesSuper(d->supers, sealed->name)) sealed->topLevel.push_back(d->name);
            for (auto& o : nested_->objects)
                if (namesSuper(o->supers, sealed->name)) sealed->topLevel.push_back(o->name);
            for (auto& c : nested_->classes)
                if ((c->base && c->base->name == sealed->name) || namesSuper(c->interfaces, sealed->name)) {
                    sealed->topLevel.push_back(c->name);
                    sealed->classCases = true;
                }
        }
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
    while (isIdent("private") || isIdent("internal") || isIdent("public") || isIdent("protected")) take();
    if (isIdent("data")) take();  // data object (its toString its name: as an object's here)
    if (!isIdent("object")) fail("expected 'object' (a sealed class's body holds data classes and objects)");
    take();
    o.name = expectIdent();
    o.supers = supers(&o.superCall);
    if (opAfterNewlines("{")) {
        skipNewlines();
        take();
        NestedDecls* was = std::exchange(nested_, &o.nested);
        members(o.properties, o.methods, o.order, o.initBlocks, "object");
        nested_ = was;
    }
    o.end = tokens_[index_ - 1].end;
    return o;
}

DataClass Parser::dataClass() {
    DataClass c;
    c.pos = cur().pos;
    annotations(&c.annotations);
    while (isIdent("private") || isIdent("internal") || isIdent("public") || isIdent("protected")) take();
    if (!isIdent("data")) fail("expected 'data class'");
    take();
    if (!isIdent("class")) fail("expected 'class' after 'data'");
    take();
    c.name = expectIdent();
    c.typeParams = typeParams();
    {
    NewlineMode mode(newlines_, false);  // (the parameters', to the `:` of its supertypes)
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
    c.supers = supers(&c.superCall);
    }
    if (opAfterNewlines("{")) {  // its body: functions, properties, init blocks
        skipNewlines();
        take();
        NestedDecls* was = std::exchange(nested_, &c.nested);
        members(c.properties, c.methods, c.order, c.initBlocks, "data class", &c.companion);
        nested_ = was;
    }
    c.end = tokens_[index_ - 1].end;
    if (c.fields.empty()) fail(c.pos, "a data class needs a parameter");
    return c;
}

std::vector<std::string> Parser::typeParams() {
    std::vector<std::string> out;
    if (!isOp("<")) return out;
    NewlineMode mode(newlines_, false);
    take();
    while (!isOp(">")) {
        while (isIdent("out") || isIdent("in") || isIdent("reified")) take();
        out.push_back(expectIdent());
        if (acceptOp(":")) type();  // (a bound: C++ checks what is done with it)
        if (!acceptOp(",")) break;
    }
    expectOp(">");
    return out;
}

// : Sealed(), A, B after a data class or an object: a sealed class it is a case of (constructed, with no
// arguments), interfaces and sealed interfaces (named).
std::vector<Type> Parser::supers(SuperCall* call) {
    std::vector<Type> out;
    if (!acceptOp(":")) return out;
    do {
        skipNewlines();
        out.push_back(type());
        if (acceptOp("(")) {  // Screen("home"): the sealed class's constructor's arguments
            NewlineMode mode(newlines_, false);
            if (!isOp(")") && !call) fail("this takes no constructor arguments here");
            if (call) call->name = out.back().name;
            while (!isOp(")")) {
                std::string name;
                if (cur().kind == Tok::Ident && peekRaw(1).kind == Tok::Op && peekRaw(1).text == "=") {
                    name = take().text;
                    take();
                }
                call->names.push_back(name);
                call->args.push_back(expression());
                if (!acceptOp(",")) break;
            }
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
    if (isOp("(")) {  // sealed class Screen(val route: String): what each case gives (Screen("home"))
        if (s.isInterface) fail("an interface has no constructor");
        NewlineMode mode(newlines_, false);
        take();
        while (!isOp(")")) {
            annotations(nullptr);
            while (isIdent("private") || isIdent("public") || isIdent("internal") || isIdent("protected")) take();
            char kind = ' ';
            if (isIdent("val") || isIdent("var")) kind = take().text == "val" ? 'v' : 'm';
            Param p;
            p.pos = cur().pos;
            p.name = expectIdent();
            expectOp(":");
            p.type = type();
            if (acceptOp("=")) p.defaultValue = expression();
            s.ctorParams.push_back(std::move(p));
            s.ctorKinds.push_back(kind);
            if (!acceptOp(",")) break;
        }
        expectOp(")");
    }
    if (acceptOp(":")) {  // the interfaces it implements (each case does)
        do {
            skipNewlines();
            s.supers.push_back(type());
            if (isOp("(")) fail(s.supers.back().pos, "a sealed class's base is an interface here (without a constructor call)");
        } while (acceptOp(","));
    }
    if (opAfterNewlines("{")) {  // its cases (data classes, objects) and its members
        skipNewlines();
        take();
        NestedDecls cases;
        NestedDecls* was = std::exchange(nested_, &cases);
        members(s.properties, s.methods, s.memberOrder, s.initBlocks, s.isInterface ? "interface" : "class", &s.companion);
        nested_ = was;
        if (!cases.enums.empty() || !cases.interfaces.empty() || !cases.innerClasses.empty())
            fail(s.pos, "a sealed class's body holds its cases here: data classes, classes and objects (and its members)");
        for (auto& c : cases.classes) {
            if (!(c->base && c->base->name == s.name) && !namesSuper(c->interfaces, s.name))
                fail(c->pos, c->name + ": a subclass of " + s.name + " is written `: " + s.name + (s.isInterface ? "`" : "()`"));
            s.classCases = true;
        }
        for (auto& d : cases.dataClasses)
            if (!namesSuper(d->supers, s.name)) fail(d->pos, d->name + ": a subclass of " + s.name + " is written `: " + s.name + (s.isInterface ? "`" : "()`"));
        for (auto& o : cases.objects)
            if (!namesSuper(o->supers, s.name)) fail(o->pos, o->name + ": a subclass of " + s.name + " is written `: " + s.name + (s.isInterface ? "`" : "()`"));
        // (in the order they are written: the variant's alternatives')
        std::vector<std::pair<size_t, char>> at;  // (each case's place: data classes, objects, classes)
        for (size_t i = 0; i < cases.dataClasses.size(); ++i) at.emplace_back(cases.dataClasses[i]->pos.offset, 'd');
        for (size_t i = 0; i < cases.objects.size(); ++i) at.emplace_back(cases.objects[i]->pos.offset, 'o');
        for (size_t i = 0; i < cases.classes.size(); ++i) at.emplace_back(cases.classes[i]->pos.offset, 'c');
        std::sort(at.begin(), at.end());
        size_t d = 0, o = 0, c = 0;
        for (auto [offset, kind] : at) {
            if (kind == 'd') {
                s.order.emplace_back('d', s.dataClasses.size());
                s.dataClasses.push_back(std::move(*cases.dataClasses[d++]));
            } else if (kind == 'o') {
                s.order.emplace_back('o', s.objects.size());
                s.objects.push_back(std::move(*cases.objects[o++]));
            } else {
                s.order.emplace_back('c', s.classes.size());
                s.classes.push_back(std::move(*cases.classes[c++]));
            }
        }
    }
    s.end = tokens_[index_ - 1].end;
    return s;
}

Function Parser::extensionProperty() {
    Function f;
    f.composable = false;
    f.isExtensionProperty = true;
    f.pos = cur().pos;
    while (isIdent("private") || isIdent("internal") || isIdent("public"))
        if (take().text == "private") f.isPrivate = true;
    bool isVar = isIdent("var");
    if (!isVar && !isIdent("val")) fail("expected 'val' or 'var'");
    take();
    f.typeParams = typeParams();
    Pos at = cur().pos;
    if (cur().kind == Tok::Ident && peekRaw(1).kind == Tok::Op && peekRaw(1).text == "<" && !peekRaw(1).spaceBefore) {
        f.receiver = type();  // (a generic receiver: List<T>.second)
        expectOp(".");
        f.name = expectIdent();
    } else {
        std::vector<std::string> parts{expectIdent()};
        while (isOp(".") && peekRaw(1).kind == Tok::Ident) {
            take();
            parts.push_back(take().text);
        }
        if (parts.size() < 2) fail(at, "an extension property is written Receiver.name");
        f.name = parts.back();
        parts.pop_back();
        f.receiver.emplace();
        f.receiver->pos = at;
        for (size_t i = 0; i < parts.size(); ++i) f.receiver->name += (i ? "." : "") + parts[i];
    }
    if (acceptOp(":")) f.result = type();
    ClassProperty p;
    p.name = f.name;
    p.isVar = isVar;
    p.pos = f.pos;
    if (f.result) p.type = *f.result;
    accessors(p);
    if (!p.getter) fail(f.pos, f.name + ": an extension property needs a getter (get() = …): it keeps no value");
    if (isVar && !p.setter) fail(f.pos, f.name + ": an extension var needs set(value) { … }");
    f.body = std::move(p.getter);
    f.expressionBody = f.body->kids.size() == 1 && f.body->kids[0]->kind == Node::Return && !f.result;
    if (p.setter) {
        auto set = std::make_shared<Function>();
        set->composable = false;
        set->isPrivate = f.isPrivate;
        set->typeParams = f.typeParams;
        set->name = "kompoz_set_" + f.name;  // (a name of its own: Kotlin may also have a fun setShout)
        set->receiver = f.receiver;
        set->pos = f.pos;
        Param v;
        v.name = p.setterParam;
        v.pos = f.pos;
        if (!f.result) fail(f.pos, f.name + ": an extension var needs its type");
        v.type = *f.result;
        set->params.push_back(std::move(v));
        set->body = std::move(p.setter);
        f.setter = std::move(set);
    }
    f.end = tokens_[index_ - 1].end;
    return f;
}

TypeAlias Parser::typeAlias() {
    TypeAlias a;
    a.pos = cur().pos;
    while (isIdent("private") || isIdent("internal") || isIdent("public")) take();
    if (!isIdent("typealias")) fail("expected 'typealias'");
    take();
    a.name = expectIdent();
    a.typeParams = typeParams();
    expectOp("=");
    a.type = type();
    a.end = tokens_[index_ - 1].end;
    return a;
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
    i.typeParams = typeParams();
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
        NestedDecls* was = std::exchange(nested_, &i.nested);
        members(i.properties, i.methods, i.order, initBlocks, "interface", &i.companion);
        nested_ = was;
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
    annotations(nullptr);
    while (isIdent("private") || isIdent("internal") || isIdent("public") || isIdent("protected")) take();
    if (!isIdent("enum")) fail("expected 'enum class'");
    take();
    if (!isIdent("class")) fail("expected 'class' after 'enum'");
    take();
    e.name = expectIdent();
    if (isOp("(")) {  // enum class Level(val n: Int)
        NewlineMode mode(newlines_, false);
        take();
        while (!isOp(")")) {
            annotations(nullptr);
            while (isIdent("private") || isIdent("public") || isIdent("internal")) take();
            if (isIdent("val") || isIdent("var")) take();
            Param p;
            p.pos = cur().pos;
            p.name = expectIdent();
            expectOp(":");
            p.type = type();
            if (acceptOp("=")) p.defaultValue = expression();
            e.ctorParams.push_back(std::move(p));
            if (!acceptOp(",")) break;
        }
        expectOp(")");
    }
    skipNewlines();
    expectOp("{");
    for (skipNewlines(); !isOp("}") && !isOp(";"); skipNewlines()) {
        e.entries.push_back(expectIdent());
        e.entryArgs.emplace_back();
        e.entryArgNames.emplace_back();
        if (isOp("(")) {  // LOW(1): its constructor's arguments
            NewlineMode mode(newlines_, false);
            take();
            while (!isOp(")")) {
                std::string name;
                if (cur().kind == Tok::Ident && peekRaw(1).kind == Tok::Op && peekRaw(1).text == "=") {
                    name = take().text;
                    take();
                }
                e.entryArgNames.back().push_back(name);
                e.entryArgs.back().push_back(expression());
                if (!acceptOp(",")) break;
            }
            expectOp(")");
        }
        e.entryBodies.emplace_back();
        if (opAfterNewlines("{")) {  // PLUS { override fun apply(…) = … }: what it overrides of the class's
            skipNewlines();
            auto body = std::make_shared<EnumEntryBody>();
            body->pos = take().pos;
            std::vector<size_t> order;
            std::vector<NodeP> initBlocks;
            NestedDecls* was = std::exchange(nested_, nullptr);
            members(body->properties, body->methods, order, initBlocks, "enum entry", nullptr);
            nested_ = was;
            if (!initBlocks.empty()) fail(body->pos, "an enum entry's body has no init block here: its class's constructor runs for it");
            for (const ClassProperty& p : body->properties)
                if (!p.overrides) fail(p.pos, "an enum entry's body overrides its class's members: override val " + p.name);
            for (const Function& m : body->methods)
                if (!m.overrides) fail(m.pos, "an enum entry's body overrides its class's members: override fun " + m.name);
            e.entryBodies.back() = std::move(body);
        }
        skipNewlines();
        if (!acceptOp(",")) break;
    }
    skipNewlines();
    if (acceptOp(";")) {  // (after the entries: Kotlin's members, if any)
        skipNewlines();
        if (!isOp("}")) {
            NestedDecls* was = std::exchange(nested_, &e.nested);
            members(e.properties, e.methods, e.order, e.initBlocks, "enum class", &e.companion);
            nested_ = was;
            e.end = tokens_[index_ - 1].end;
            if (e.entries.empty()) fail(e.pos, "an enum class needs an entry");
            for (const Function& m : e.methods)  // (an abstract one: each entry's body has it)
                for (size_t i = 0; m.isAbstract && i < e.entries.size(); ++i)
                    if (!e.entryBodies[i] || std::none_of(e.entryBodies[i]->methods.begin(), e.entryBodies[i]->methods.end(),
                                                          [&](const Function& f) { return f.name == m.name && f.params.size() == m.params.size(); }))
                        fail(m.pos, e.entries[i] + " doesn't implement the abstract fun " + m.name);
            for (const ClassProperty& p : e.properties)
                for (size_t i = 0; p.isAbstract && i < e.entries.size(); ++i)
                    if (!e.entryBodies[i] || std::none_of(e.entryBodies[i]->properties.begin(), e.entryBodies[i]->properties.end(),
                                                          [&](const ClassProperty& q) { return q.name == p.name; }))
                        fail(p.pos, e.entries[i] + " doesn't implement the abstract val " + p.name);
            return e;
        }
    }
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
    if (!isIdent("val") && !isIdent("var")) fail("expected 'val' or 'var'");
    p.decl = declaration();  // (flag: var; delegated: by …)
    if (!p.decl->names.empty()) fail(p.decl->pos, "a top-level val can't be destructured");
    if (p.decl->kids.empty()) fail(p.decl->pos, "a top-level " + std::string(p.decl->flag ? "var" : "val") + " needs a value");
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
        if (isIdent("vararg")) {
            take();
            p.isVararg = true;
        }
        p.name = expectIdent();
        expectOp(":");
        p.type = type();
        if (p.isVararg) {  // (the function sees a List<T>)
            Type list;
            list.name = "List";
            list.pos = p.type.pos;
            list.args.push_back(std::move(p.type));
            p.type = std::move(list);
        }
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
        if (t.text == "try") return tryExpression();
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
    // (`by` is a name too, as in Kotlin: a soft keyword, delegation's only after a property's name)
    while (cur().kind == Tok::Ident && !kKeywords.contains(cur().text) && cur().text != "by") {
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
        if (acceptOp("*")) call.spreadArgs.push_back(call.kids.size());  // (*list: a vararg's elements, or passed as it is)
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
            if (t.text == "super") {  // super.f(): the base class's (codegen: super is a name there)
                auto n = make(Node::Name, take().pos);
                n->text = "super";
                return n;
            }
            if (t.text == "if") return ifExpression();
            if (t.text == "when") return whenExpression();
            if (t.text == "try") return tryExpression();
            if (t.text == "throw") {  // throw e: an expression (Kotlin's Nothing), x ?: throw … too
                auto n = make(Node::Unary, take().pos);
                n->text = "throw";
                n->kids.push_back(expression());
                return n;
            }
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

// try { } catch (e: T) { } finally { }: a statement, or a value (the body's or the catching block's last expression).
NodeP Parser::tryExpression() {
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
            auto n = make(part.text == "this" ? Node::This : Node::Name, part.pos);  // $name ($this: the receiver)
            if (n->kind == Node::Name) n->text = part.text;
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
