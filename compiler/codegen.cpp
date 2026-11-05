#include "codegen.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>
#include <vector>

namespace kompozc {

// ---- Output -------------------------------------------------------------------------------------

void Output::at(Pos pos) {
    if (pos.line == current_) return;
    if (current_ != 0 && pos.line > current_ && pos.line - current_ <= 4) {
        out_.append(size_t(pos.line - current_), '\n');
    } else {
        if (!out_.empty() && out_.back() != '\n') out_ += '\n';
        out_ += "#line " + std::to_string(pos.line) + " \"" + file_ + "\"\n";
    }
    current_ = pos.line;
}

void Output::text(std::string_view s) {
    for (char c : s)
        if (c == '\n' && current_ != 0) ++current_;
    out_ += s;
}

void Output::line(int n) {
    if (!out_.empty() && out_.back() != '\n') out_ += '\n';
    out_ += "#line " + std::to_string(n) + " \"" + file_ + "\"\n";
    current_ = n;
}

namespace {

// Kotlin functions whose lambda gives a value (its last expression); others' lambdas give none.
const std::set<std::string, std::less<>> kValueLambdas = {
    "remember", "rememberSaveable", "derivedStateOf", "map", "mapNotNull", "mapIndexed", "filter", "filterNot",
    "filterIndexed", "sortedBy", "sortedByDescending", "any", "all", "none", "count", "first", "firstOrNull",
    "last", "lastOrNull", "find", "groupBy", "associateBy", "associateWith", "flatMap", "sumOf", "maxOf", "minOf",
    "maxByOrNull", "minByOrNull", "let", "run", "with", "takeIf", "takeUnless", "getOrElse", "lazy", "snapshotFlow",
    "List", "buildString", "buildList", "indexOfFirst", "indexOfLast", "partition", "distinctBy", "chunked"};

// Kotlin types the C++ way.
const std::map<std::string, std::string, std::less<>> kTypes = {
    {"String", "std::string"},   {"Int", "int"},           {"Long", "long long"},     {"Short", "short"},
    {"Byte", "signed char"},     {"Float", "float"},       {"Double", "double"},      {"Boolean", "bool"},
    {"Char", "char32_t"},        {"Unit", "void"},         {"UInt", "unsigned"},      {"ULong", "unsigned long long"},
    {"Dp", "float"},             {"List", "std::vector"},  {"MutableList", "std::vector"}, {"ArrayList", "std::vector"},
    {"Set", "std::set"},         {"MutableSet", "std::set"}, {"Map", "std::map"},    {"MutableMap", "std::map"},
    {"Pair", "std::pair"},       {"Any", "std::any"}};

// Objects whose Kotlin properties are calls here: MaterialTheme.colorScheme -> colorScheme().
const std::map<std::string, std::string, std::less<>> kObjectProperties = {
    {"MaterialTheme.colorScheme", "::kompoz::colorScheme()"},
    {"MaterialTheme.typography", "::kompoz::typography()"},
    {"MaterialTheme.shapes", "::kompoz::shapes()"},
};

// Kotlin's standard functions, from kotlin.hpp: listOf(1, 2) -> kt::listOf(1, 2).
const std::set<std::string, std::less<>> kKtFunctions = {
    "listOf", "mutableListOf", "emptyList", "arrayListOf", "setOf", "mutableSetOf", "emptySet", "mapOf", "mutableMapOf",
    "emptyMap", "repeat", "println", "maxOf", "minOf", "require", "check", "error", "TODO"};

// Members of Kotlin's standard types, from kotlin.hpp: text.trim() -> kt::trim(text). Each uses the
// value's own member of that name where it has one.
const std::set<std::string, std::less<>> kKtMembers = {
    "forEach", "forEachIndexed", "map", "mapIndexed", "filter", "filterNot", "isEmpty", "isNotEmpty", "isBlank",
    "isNotBlank", "trim", "trimIndent", "uppercase", "lowercase", "toInt", "toFloat", "toDouble", "toLong", "toString",
    "first", "last", "firstOrNull", "lastOrNull", "joinToString", "startsWith", "endsWith", "contains", "substring",
    "take", "drop", "reversed", "sorted", "sortedBy", "any", "all", "none", "count", "sum", "sumOf", "indexOf",
    "getOrNull", "let", "also", "takeIf", "withIndex", "toList", "toMutableList", "distinct", "split", "replace",
    "coerceIn", "coerceAtLeast", "coerceAtMost", "roundToInt", "find"};

// Calls whose lambda is a coroutine (Kotlin's suspend lambdas): [=]() -> Task { ...; co_return; }.
const std::set<std::string, std::less<>> kSuspendLambdas = {"LaunchedEffect", "launch", "async", "produceState"};

// Suspending functions: in a coroutine, awaited (co_await).
const std::set<std::string, std::less<>> kSuspendCalls = {
    "delay", "withFrameNanos", "withFrameMillis", "awaitFrame", "animateTo", "snapTo", "animateScrollToItem",
    "scrollToItem", "animateScrollToPage", "scrollToPage", "animateScrollBy", "scrollBy", "show", "hide", "open", "close",
    "expand", "partialExpand", "showSnackbar", "collect", "collectLatest", "emit", "join", "await", "withBackground"};

const std::set<std::string, std::less<>> kCppKeywords = {
    "alignas", "alignof", "and", "asm", "auto", "bitand", "bitor", "bool", "case", "catch", "char", "compl", "concept", "const",
    "consteval", "constexpr", "constinit", "const_cast", "co_await", "co_return", "co_yield", "decltype", "default", "delete",
    "double", "dynamic_cast", "enum", "explicit", "export", "extern", "float", "friend", "goto", "inline", "int", "long",
    "mutable", "namespace", "new", "noexcept", "not", "operator", "or", "private", "protected", "public", "register",
    "reinterpret_cast", "requires", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
    "switch", "template", "thread_local", "throw", "typedef", "typeid", "typename", "union", "unsigned", "using",
    "virtual", "void", "volatile", "wchar_t", "xor", "site"};

bool capitalized(std::string_view s) { return !s.empty() && std::isupper(static_cast<unsigned char>(s[0])); }

std::string cppName(const std::string& name) { return kCppKeywords.contains(name) ? name + "_" : name; }

std::string qualified(std::string dotted) {
    std::string out;
    for (char c : dotted) {
        if (c == '.') out += "::";
        else out += c;
    }
    return out;
}

// Whether a lambda's body says `it` for itself (not inside a lambda of its own that has `it`).
bool usesIt(const Node* n) {
    if (!n) return false;
    if (n->kind == Node::Name && n->text == "it") return true;
    if (n->kind == Node::Lambda && !n->flag) return false;  // its own `it`
    for (auto& k : n->kids)
        if (usesIt(k.get())) return true;
    if (n->lambda && usesIt(n->lambda.get())) return true;
    for (auto& b : n->branches) {
        for (auto& c : b.conds)
            if (usesIt(c.second.get())) return true;
        if (usesIt(b.body.get())) return true;
    }
    return false;
}

class Generator {
public:
    Generator(Output& out, const Module& module) : out_(out), module_(module) {}

    std::string type(const Type& t) {
        std::string s;
        if (t.function) {
            // RowScope.() -> Unit: what the receiver gives (weight, items, ...) Kompoz has on its own.
            if (t.composable && t.params.empty() && t.result && t.result->name == "Unit") return "::kompoz::Content";
            s = "std::function<" + (t.result ? type(*t.result) : std::string("void")) + "(";
            for (size_t i = 0; i < t.params.size(); ++i) s += (i ? ", " : "") + type(t.params[i]);
            return s + ")>";  // (a null one is an empty std::function: no optional around it)
        }
        auto known = kTypes.find(t.name);
        s = known != kTypes.end() ? known->second : qualified(t.name);
        if (!t.args.empty()) {
            s += "<";
            for (size_t i = 0; i < t.args.size(); ++i) s += (i ? ", " : "") + type(t.args[i]);
            s += ">";
        }
        if (t.nullable) s = "std::optional<" + s + ">";
        return s;
    }

    // How a parameter is passed: small values by value, the rest by const reference.
    std::string paramType(const Type& t) {
        std::string s = type(t);
        static const std::set<std::string, std::less<>> byValue = {"int", "long long", "short", "signed char", "float",
                                                                     "double", "bool", "char32_t", "unsigned", "unsigned long long"};
        return byValue.contains(s) ? s : "const " + s + "&";
    }

    void signature(const Function& f, bool defaults) {
        bool unit = !f.result || f.result->name == "Unit";
        out_.at(f.pos);
        out_.text((unit ? std::string("void") : type(*f.result)) + " " + f.name + "(");
        // Defaults: those at the end (C++ takes no others), where kompozc's calls leave them out.
        size_t firstDefault = f.params.size();
        while (firstDefault > 0 && f.params[firstDefault - 1].defaultValue) --firstDefault;
        for (size_t i = 0; i < f.params.size(); ++i) {
            const Param& p = f.params[i];
            out_.text((i ? ", " : "") + paramType(p.type) + " " + cppName(p.name));
            if (defaults && i >= firstDefault) {
                out_.text(" = ");
                scopes_.emplace_back();
                expr(*p.defaultValue);
                scopes_.pop_back();
            }
        }
        if (unit) out_.text(std::string(f.params.empty() ? "" : ", ") + "::kompoz::CallSite site" + (defaults ? " = ::kompoz::CallSite::current()" : ""));
        out_.text(")");
    }

    void definition(const Function& f) {
        bool unit = !f.result || f.result->name == "Unit";
        signature(f, false);
        out_.text(" {");
        scopes_.emplace_back();
        for (const Param& p : f.params) declare(p.name, Local);
        if (unit) {
            // Restartable and skippable, as Compose's compiler makes a Unit @Composable function.
            out_.text(" ::kompoz::composable(site, ");
            for (const Param& p : f.params) out_.text(cppName(p.name) + ", ");
            out_.text("[=] {");
            statements(*f.body, false);
            out_.text(" });");
        } else {
            statements(*f.body, true);
        }
        scopes_.pop_back();
        out_.text(" }\n");
    }

private:
    enum VarKind { Local, Delegated };

    void declare(const std::string& name, VarKind kind) { scopes_.back()[name] = kind; }
    const VarKind* lookup(const std::string& name) const {
        for (auto s = scopes_.rbegin(); s != scopes_.rend(); ++s)
            if (auto f = s->find(name); f != s->end()) return &f->second;
        return nullptr;
    }

    // ---- Statements ----

    // The statements of a block; with `tail`, the last one gives the block's value (return).
    void statements(const Node& block, bool tail) {
        scopes_.emplace_back();
        for (size_t i = 0; i < block.kids.size(); ++i) statement(*block.kids[i], tail && i + 1 == block.kids.size());
        scopes_.pop_back();
    }

    void braced(const Node& block, bool tail) {
        out_.text(" {");
        statements(block, tail);
        out_.text(" }");
    }

    void statement(const Node& s, bool tail) {
        out_.at(s.pos);
        out_.text(" ");
        switch (s.kind) {
            case Node::ExprStmt: {
                const Node& e = *s.kids[0];
                if (e.kind == Node::If) return ifStatement(e, tail);
                if (e.kind == Node::When) return whenStatement(e, tail);
                if (e.kind == Node::Unary && (e.text == "++" || e.text == "--")) {
                    if (const Node* target = e.kids[0].get(); delegatedName(*target)) {
                        out_.text(cppName(target->text) + ".set(::kompoz::kt::value(" + cppName(target->text) + ") " + e.text[0] + " 1);");
                        return;
                    }
                }
                if (tail) out_.text("return ");
                expr(e);
                out_.text(";");
                return;
            }
            case Node::Decl: return declaration(s);
            case Node::Assign: return assignment(s);
            case Node::Return:
                out_.text(suspend_ ? "co_return" : "return");
                if (!s.kids.empty()) {
                    out_.text(" ");
                    expr(*s.kids[0]);
                }
                out_.text(";");
                return;
            case Node::Break: out_.text("break;"); return;
            case Node::Continue: out_.text("continue;"); return;
            case Node::For: return forStatement(s);
            case Node::While:
                out_.text("while (");
                expr(*s.kids[0]);
                out_.text(")");
                braced(*s.kids[1], false);
                return;
            case Node::DoWhile:
                out_.text("do");
                braced(*s.kids[0], false);
                out_.text(" while (");
                expr(*s.kids[1]);
                out_.text(");");
                return;
            default: throw CompileError(s.pos, "unexpected statement");
        }
    }

    bool delegatedName(const Node& n) const {
        if (n.kind != Node::Name) return false;
        const VarKind* k = lookup(n.text);
        return k && *k == Delegated;
    }

    void declaration(const Node& d) {
        if (!d.names.empty()) {  // val (a, b) = pair
            out_.text("auto [");
            for (size_t i = 0; i < d.names.size(); ++i) out_.text((i ? ", " : "") + cppName(d.names[i]));
            out_.text("] = ");
            expr(*d.kids[0]);
            out_.text(";");
            for (auto& n : d.names) declare(n, Local);
            return;
        }
        std::string name = cppName(d.text);
        if (d.delegated) {
            // var x by remember { mutableStateOf(0) }: x holds the state; reading x reads its value.
            out_.text("auto " + name + " = ");
            expr(*d.kids[0]);
            out_.text(";");
            declare(d.text, Delegated);
            return;
        }
        out_.text((d.type ? type(*d.type) : std::string("auto")) + " " + name);
        if (!d.kids.empty()) {
            out_.text(" = ");
            expr(*d.kids[0], d.type ? &*d.type : nullptr);
        } else if (d.type) {
            out_.text("{}");
        } else {
            throw CompileError(d.pos, "a variable without a value needs a type");
        }
        out_.text(";");
        declare(d.text, Local);
    }

    void assignment(const Node& a) {
        const Node& target = *a.kids[0];
        std::string op = a.text;
        auto rhs = [&] {
            if (op == "=") {
                expr(*a.kids[1]);
            } else {  // x += v: x's value (a state's read through) and v
                expr(target);
                out_.text(" " + op.substr(0, 1) + " (");
                expr(*a.kids[1]);
                out_.text(")");
            }
        };
        if (delegatedName(target)) {  // x = v on a delegated x: the state set
            out_.text(cppName(target.text) + ".set(");
            rhs();
            out_.text(");");
            return;
        }
        if (target.kind == Node::Member && target.text == "value") {  // state.value = v
            expr(*target.kids[0]);
            out_.text(".set(");
            rhs();
            out_.text(");");
            return;
        }
        expr(target);
        out_.text(" " + op + " ");
        expr(*a.kids[1]);
        out_.text(";");
    }

    void ifStatement(const Node& n, bool tail) {
        out_.text("if (");
        expr(*n.kids[0]);
        out_.text(")");
        braced(*n.kids[1], tail);
        if (n.kids.size() > 2) {
            out_.text(" else");
            braced(*n.kids[2], tail);
        }
    }

    void whenCondition(const Node::WhenBranch& b, const std::string& subject) {
        for (size_t i = 0; i < b.conds.size(); ++i) {
            if (i) out_.text(" || ");
            auto& [kind, e] = b.conds[i];
            out_.text("(");
            switch (kind) {
                case Node::WhenBranch::Expr:
                    if (subject.empty()) {
                        expr(*e);
                    } else {
                        out_.text(subject + " == ");
                        expr(*e);
                    }
                    break;
                case Node::WhenBranch::In:
                case Node::WhenBranch::NotIn:
                    out_.text(std::string(kind == Node::WhenBranch::NotIn ? "!" : "") + "::kompoz::kt::contains(");
                    expr(*e);
                    out_.text(", " + subject + ")");
                    break;
                default: throw CompileError(b.pos, "`is` in when isn't supported yet");
            }
            out_.text(")");
        }
    }

    void whenStatement(const Node& n, bool tail) {
        std::string subject;
        out_.text("{");
        if (const Node* s = n.kids[0].get()) {
            subject = "kompoz_when_" + std::to_string(n.pos.line) + "_" + std::to_string(n.pos.column);
            out_.text(" const auto& " + subject + " = ");
            expr(s->kind == Node::Decl ? *s->kids[0] : *s);
            out_.text(";");
            if (s->kind == Node::Decl) out_.text(" const auto& " + cppName(s->text) + " = " + subject + ";");
            scopes_.emplace_back();
            if (s->kind == Node::Decl) declare(s->text, Local);
        } else {
            scopes_.emplace_back();
        }
        bool first = true;
        for (const auto& b : n.branches) {
            out_.at(b.pos);
            if (b.conds.empty()) {
                out_.text(first ? " {" : " else {");
            } else {
                out_.text(first ? " if (" : " else if (");
                whenCondition(b, subject);
                out_.text(") {");
            }
            statements(*b.body, tail);
            out_.text(" }");
            first = false;
        }
        scopes_.pop_back();
        out_.text(" }");
    }

    void forStatement(const Node& f) {
        const Node& it = *f.kids[0];
        scopes_.emplace_back();
        for (auto& n : f.names) declare(n, Local);
        if (f.names.size() == 1) {
            out_.text("for (auto&& " + cppName(f.names[0]) + " : ");
        } else {
            out_.text("for (auto&& [");
            for (size_t i = 0; i < f.names.size(); ++i) out_.text((i ? ", " : "") + cppName(f.names[i]));
            out_.text("] : ");
        }
        expr(it);
        out_.text(")");
        braced(*f.kids[1], false);
        scopes_.pop_back();
    }

    // ---- Expressions ----

    // `expected`: the type the value goes to, where it is known (a declaration's).
    void expr(const Node& e, const Type* expected = nullptr) {
        switch (e.kind) {
            case Node::Name: return name(e);
            case Node::Number: out_.text(number(e.text)); return;
            case Node::String: return string(e);
            case Node::Char: out_.text("U'" + e.text + "'"); return;
            case Node::Bool: out_.text(e.text); return;
            case Node::Null: out_.text("::kompoz::kt::null"); return;
            case Node::This: out_.text("(*this)"); return;
            case Node::Paren:
                out_.text("(");
                expr(*e.kids[0]);
                out_.text(")");
                return;
            case Node::Call: return call(e);
            case Node::Member: return member(e);
            case Node::Index:
                expr(*e.kids[0]);
                out_.text("[");
                for (size_t i = 1; i < e.kids.size(); ++i) {
                    if (i > 1) out_.text(", ");
                    expr(*e.kids[i]);
                }
                out_.text("]");
                return;
            case Node::Lambda: return lambda(e, expected && expected->function && expected->result && expected->result->name != "Unit");
            case Node::Unary: return unary(e);
            case Node::Binary: return binary(e);
            case Node::As:
                out_.text("static_cast<" + type(*e.type) + ">(");
                expr(*e.kids[0]);
                out_.text(")");
                return;
            case Node::Is: throw CompileError(e.pos, "`is` isn't supported yet");
            case Node::If: return ifExpression(e);
            case Node::When:
                // A when as a value: run where it stands, each branch giving its value.
                out_.text("[&]() {");
                whenStatement(e, true);
                out_.text(" }()");
                return;
            default: throw CompileError(e.pos, "unexpected expression");
        }
    }

    void name(const Node& e) {
        if (const VarKind* k = lookup(e.text)) {
            if (*k == Delegated) out_.text("::kompoz::kt::value(" + cppName(e.text) + ")");
            else out_.text(cppName(e.text));
            return;
        }
        if (e.text == "Modifier") {
            out_.text("::kompoz::Modifier()");
            return;
        }
        if (e.text == "Unit") {
            out_.text("::kompoz::kt::Unit");
            return;
        }
        out_.text(cppName(e.text));
    }

    static std::string number(std::string n) {
        bool hex = n.size() > 1 && (n[1] == 'x' || n[1] == 'X' || n[1] == 'b' || n[1] == 'B');
        std::string suffix;
        while (!n.empty() && (n.back() == 'L' || n.back() == 'u' || n.back() == 'U' || (!hex && (n.back() == 'f' || n.back() == 'F')))) {
            suffix.insert(suffix.begin(), n.back());
            n.pop_back();
        }
        bool fl = suffix.find_first_of("fF") != std::string::npos;
        if (fl && n.find_first_of(".eE") == std::string::npos) n += ".0";  // 1f -> 1.0f
        std::string out = n;
        for (char c : suffix) {
            if (c == 'L') out += "LL";
            else if (c == 'f' || c == 'F') out += "f";
            else out += "u";
        }
        return out;
    }

    void string(const Node& e) {
        if (e.kids.empty()) {
            std::string text;
            for (auto& p : e.parts) text += p.text;
            out_.text("std::string(\"" + text + "\")");
            return;
        }
        out_.text("::kompoz::kt::str(");
        size_t k = 0;
        for (size_t i = 0; i < e.parts.size(); ++i) {
            if (i) out_.text(", ");
            if (e.parts[i].expr) expr(*e.kids[k++]);
            else out_.text("\"" + e.parts[i].text + "\"");
        }
        out_.text(")");
    }

    // A name that stands for a type or a namespace here (Arrangement, Icons.Filled): what comes
    // after it is reached with ::, not with a dot.
    bool qualifier(const Node& e) const {
        if (e.kind == Node::Name) return capitalized(e.text) && !lookup(e.text) && e.text != "Modifier";
        if (e.kind == Node::Member) return !e.flag && capitalized(e.text) && qualifier(*e.kids[0]);
        return false;
    }

    std::string dotted(const Node& e) const {
        if (e.kind == Node::Name) return e.text;
        return dotted(*e.kids[0]) + "." + e.text;
    }

    void member(const Node& e) {
        if (e.text == "Companion") return expr(companionless(e));
        const Node& obj = companionless(*e.kids[0]);
        if (e.flag) {  // a?.b
            out_.text("::kompoz::kt::safe(");
            expr(obj);
            out_.text(e.text == "size" || e.text == "length" ? ", [&](auto&& kompoz_safe) { return ::kompoz::kt::size(kompoz_safe); })"
                                                               : ", [&](auto&& kompoz_safe) { return kompoz_safe." + cppName(e.text) + "; })");
            return;
        }
        if (obj.kind == Node::Member || obj.kind == Node::Name) {
            if (qualifier(obj)) {
                auto special = kObjectProperties.find(dotted(obj) + "." + e.text);
                if (special != kObjectProperties.end()) {
                    out_.text(special->second);
                    return;
                }
            }
        }
        if (e.text == "dp") {
            out_.text("float(");
            expr(obj);
            out_.text(")");
            return;
        }
        if (e.text == "sp" || e.text == "em") {
            out_.text("::kompoz::TextUnit::" + e.text + "(float(");
            expr(obj);
            out_.text("))");
            return;
        }
        if (obj.kind == Node::Name && e.text == "current" && obj.text.starts_with("Local") && !lookup(obj.text)) {
            out_.text(obj.text + ".current()");  // LocalDensity.current
            return;
        }
        if (e.text == "size" || e.text == "length") {  // list.size, text.length: Kotlin's properties
            out_.text("::kompoz::kt::size(");
            expr(obj);
            out_.text(")");
            return;
        }
        if (e.text == "value") {  // state.value
            out_.text("::kompoz::kt::value(");
            expr(obj);
            out_.text(")");
            return;
        }
        if (qualifier(obj)) {
            out_.text(qualified(dotted(obj)) + "::" + cppName(e.text));
            return;
        }
        expr(obj);
        out_.text("." + cppName(e.text));
    }

    // X.Companion is X.
    static const Node& companionless(const Node& n) {
        return n.kind == Node::Member && n.text == "Companion" ? companionless(*n.kids[0]) : n;
    }

    void call(const Node& c) {
        const Node& callee = companionless(*c.kids[0]);
        std::string calleeName = callee.kind == Node::Name ? callee.text : callee.kind == Node::Member ? callee.text : "";
        if (suspend_ && kSuspendCalls.contains(calleeName)) out_.text("co_await ");
        // A @Composable kompozc translates: arguments by position, defaults filled in.
        if (callee.kind == Node::Name && !lookup(callee.text)) {
            if (auto f = module_.find(callee.text); f != module_.end()) return moduleCall(c, *f->second);
            if (kKtFunctions.contains(callee.text) || (suspend_ && callee.text == "delay")) {
                out_.text("::kompoz::kt::" + callee.text);
                return arguments(c, calleeName, false);
            }
        }
        if (callee.kind == Node::Member) {
            const Node& obj = companionless(*callee.kids[0]);
            if (callee.flag) {  // a?.f(x): f called only when a isn't null
                out_.text("::kompoz::kt::safe(");
                expr(obj);
                out_.text(", [&](auto&& kompoz_safe) { return ");
                if (kKtMembers.contains(callee.text)) {
                    out_.text("::kompoz::kt::" + callee.text + "(kompoz_safe");
                    arguments(c, calleeName, true, true);
                } else {
                    out_.text("kompoz_safe." + cppName(callee.text));
                    arguments(c, calleeName, false);
                }
                out_.text("; })");
                return;
            }
            if (obj.kind == Node::Name && obj.text == "Modifier" && !lookup("Modifier")) {
                out_.text("::kompoz::Modifier()." + cppName(callee.text));
                return arguments(c, calleeName, false);
            }
            if (kKtMembers.contains(callee.text) && !qualifier(obj)) {
                out_.text("::kompoz::kt::" + callee.text + "(");
                expr(obj);
                return arguments(c, calleeName, true, true);
            }
        }
        expr(callee);
        if (!c.typeArgs.empty()) {
            out_.text("<");
            for (size_t i = 0; i < c.typeArgs.size(); ++i) out_.text((i ? ", " : "") + type(c.typeArgs[i]));
            out_.text(">");
        }
        arguments(c, calleeName, false);
    }

    // (args): as written, named ones as kompoz's named arguments, the trailing lambda last.
    // `positional`: names dropped (kotlin.hpp's functions take their arguments by position);
    // `after`: the parenthesis is open and the first argument written.
    void arguments(const Node& c, const std::string& calleeName, bool positional, bool after = false) {
        if (!after) out_.text("(");
        bool any = after;
        for (size_t i = 0; i < c.kids.size() - 1; ++i) {
            if (any) out_.text(", ");
            any = true;
            const std::string& n = c.names[i];
            if (!n.empty() && !positional) out_.text("::kompoz::" + n + " = ");
            expr(*c.kids[i + 1]);
        }
        if (c.lambda) {
            if (any) out_.text(", ");
            lambda(*c.lambda, kValueLambdas.contains(calleeName), kSuspendLambdas.contains(calleeName));
        }
        out_.text(")");
    }

    void moduleCall(const Node& c, const Function& f) {
        std::vector<const Node*> given(f.params.size(), nullptr);
        size_t next = 0;
        for (size_t i = 0; i + 1 < c.kids.size(); ++i) {
            const std::string& n = c.names[i];
            size_t at = next;
            if (!n.empty()) {
                at = f.params.size();
                for (size_t p = 0; p < f.params.size(); ++p)
                    if (f.params[p].name == n) at = p;
                if (at == f.params.size()) throw CompileError(c.kids[i + 1]->pos, f.name + " has no parameter named " + n);
            } else {
                next = at + 1;
            }
            if (at >= f.params.size()) throw CompileError(c.kids[i + 1]->pos, "too many arguments for " + f.name);
            if (given[at]) throw CompileError(c.kids[i + 1]->pos, n.empty() ? "too many arguments for " + f.name : n + " is given twice");
            given[at] = c.kids[i + 1].get();
        }
        if (c.lambda) {
            if (f.params.empty() || given.back()) throw CompileError(c.lambda->pos, f.name + " takes no trailing lambda here");
            given.back() = c.lambda.get();
        }
        size_t last = given.size();  // the defaults left at the end: C++'s
        while (last > 0 && !given[last - 1] && f.params[last - 1].defaultValue) --last;
        out_.text(f.name + "(");
        for (size_t i = 0; i < last; ++i) {
            if (i) out_.text(", ");
            const Param& p = f.params[i];
            const Node* arg = given[i] ? given[i] : p.defaultValue.get();
            if (!arg) throw CompileError(c.pos, f.name + " needs " + p.name);
            if (arg->kind == Node::Lambda) lambda(*arg, p.type.function && p.type.result && p.type.result->name != "Unit");
            else expr(*arg, &p.type);
        }
        out_.text(")");
    }

    void lambda(const Node& l, bool value, bool suspend = false) {
        bool wasSuspend = std::exchange(suspend_, suspend);
        out_.text("[=](");
        scopes_.emplace_back();
        if (suspend) {
            // A coroutine (Kotlin's suspend lambda): its CoroutineScope receiver has no name here.
        } else if (l.flag) {
            for (size_t i = 0; i < l.names.size(); ++i) {
                out_.text((i ? ", " : "") + std::string("auto ") + cppName(l.names[i]));
                declare(l.names[i], Local);
            }
        } else if (std::any_of(l.kids.begin(), l.kids.end(), [](const NodeP& k) { return usesIt(k.get()); })) {
            out_.text("auto it");
            declare("it", Local);
        } else {
            out_.text("auto&&...");  // { ... } where the type has parameters it doesn't use
        }
        out_.text(suspend ? ") -> ::kompoz::Task {" : ") {");
        for (size_t i = 0; i < l.kids.size(); ++i) statement(*l.kids[i], value && i + 1 == l.kids.size());
        if (suspend) out_.text(" co_return;");
        scopes_.pop_back();
        out_.text(" }");
        suspend_ = wasSuspend;
    }

    void unary(const Node& u) {
        const Node& x = *u.kids[0];
        if (u.text == "!!") {
            out_.text("::kompoz::kt::notNull(");
            expr(x);
            out_.text(")");
        } else if ((u.text == "++" || u.text == "--") && delegatedName(x)) {
            throw CompileError(u.pos, u.text + " on a state only as a statement");
        } else if (u.flag) {
            expr(x);
            out_.text(u.text);
        } else {
            out_.text(u.text);
            expr(x);
        }
    }

    void binary(const Node& b) {
        const std::string& op = b.text;
        const Node& l = *b.kids[0];
        const Node& r = *b.kids[1];
        auto helper = [&](const char* fn, const Node& first, const Node& second) {
            out_.text(std::string("::kompoz::kt::") + fn + "(");
            expr(first);
            out_.text(", ");
            expr(second);
            out_.text(")");
        };
        if (op == "..") return helper("rangeTo", l, r);
        if (op == "..<" || op == "until") return helper("until", l, r);
        if (op == "downTo") return helper("downTo", l, r);
        if (op == "step") return helper("step", l, r);
        if (op == "in") return helper("contains", r, l);
        if (op == "!in") {
            out_.text("!");
            return helper("contains", r, l);
        }
        if (op == "to") return helper("to", l, r);
        if (op == "?:") {
            out_.text("::kompoz::kt::elvis(");
            expr(l);
            out_.text(", [&] { return ");
            expr(r);
            out_.text("; })");
            return;
        }
        if ((op == "==" || op == "!=" || op == "===" || op == "!==") && (l.kind == Node::Null || r.kind == Node::Null)) {
            out_.text(op[0] == '!' ? "!::kompoz::kt::isNull(" : "::kompoz::kt::isNull(");
            expr(l.kind == Node::Null ? r : l);
            out_.text(")");
            return;
        }
        static const std::set<std::string, std::less<>> plain = {"+", "-", "*", "/", "%", "<", ">", "<=", ">=", "==", "!=", "&&", "||"};
        if (plain.contains(op) || op == "===" || op == "!==") {
            expr(l);
            out_.text(" " + (op == "===" ? std::string("==") : op == "!==" ? std::string("!=") : op) + " ");
            expr(r);
            return;
        }
        // An infix function: a.name(b).
        expr(l);
        out_.text("." + cppName(op) + "(");
        expr(r);
        out_.text(")");
    }

    // if as a value: a ? b : c where both branches are one expression, else run in place.
    void ifExpression(const Node& n) {
        auto single = [](const Node* b) { return b && b->kids.size() == 1 && b->kids[0]->kind == Node::ExprStmt && b->kids[0]->kids[0]->kind != Node::If && b->kids[0]->kids[0]->kind != Node::When; };
        if (n.kids.size() > 2 && single(n.kids[1].get()) && single(n.kids[2].get())) {
            out_.text("(");
            expr(*n.kids[0]);
            out_.text(" ? ");
            expr(*n.kids[1]->kids[0]->kids[0]);
            out_.text(" : ");
            expr(*n.kids[2]->kids[0]->kids[0]);
            out_.text(")");
            return;
        }
        if (n.kids.size() < 3) throw CompileError(n.pos, "an if used as a value needs an else");
        out_.text("[&]() {");
        ifStatement(n, true);
        out_.text(" }()");
    }

    Output& out_;
    const Module& module_;
    bool suspend_ = false;  // in a coroutine
    std::vector<std::map<std::string, VarKind, std::less<>>> scopes_;
};

}  // namespace

void generatePrototype(Output& out, const Function& f, const Module& module) {
    Generator g(out, module);
    g.signature(f, true);
    out.text(";\n");
}

void generateDefinition(Output& out, const Function& f, const Module& module) {
    Generator g(out, module);
    g.definition(f);
}

}  // namespace kompozc
