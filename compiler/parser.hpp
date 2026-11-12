#pragma once

// kompozc's parser: a @Kompozable function's Kotlin, from its @ to its closing brace.

#include "ast.hpp"

namespace kompozc {

class Parser {
public:
    Parser(std::string_view source, Pos start);

    // [@Kompozable] [@Other...] [private] fun [Receiver.]name(params)[: Type] { body } (or = expression).
    Function function();
    // [@Serializable] data class Name(val a: T, var b: U = d)
    DataClass dataClass();
    SealedDecl sealedDecl();
    // [@Serializable] object Name
    ObjectDecl objectDecl();
    // A class's or an object's body after its {: properties, functions and init blocks, to the }.
    void members(std::vector<ClassProperty>& properties, std::vector<Function>& methods, std::vector<size_t>& order,
                 std::vector<NodeP>& initBlocks, const char* what, std::shared_ptr<ObjectDecl>* companion = nullptr);
    // [private] val [<T>] Receiver.name: Type get() = … [set(v) { … }]: an extension property, as its getter
    Function extensionProperty();
    // [private] typealias Name<T> = Type
    TypeAlias typeAlias();
    // [fun] interface Name : A, B { val a: T; fun f(): R; fun g() = … }
    InterfaceDecl interfaceDecl();
    // enum class Name { A, B, C }
    EnumClass enumClass();
    // class Name(val a: T, b: U) : Base(b) { val c = …; fun f() { … } }
    ClassDecl classDecl();
    // [private | internal | const] val name[: Type] = value, at the top of a file.
    Property property();
    // One expression and then `}`: a string template's ${...}.
    NodeP templateExpression();

private:
    const Token& peekRaw(size_t ahead = 0);  // newlines included
    const Token& cur();                      // newlines skipped where they don't count
    Token take();
    bool isOp(std::string_view op);
    bool isIdent(std::string_view name);
    bool acceptOp(std::string_view op);
    void expectOp(std::string_view op);
    std::string expectIdent();
    void skipNewlines();
    bool opAfterNewlines(std::string_view op);  // the next significant token, across newlines, is op
    [[noreturn]] void fail(const std::string& message);
    [[noreturn]] void fail(Pos at, const std::string& message);

    Type type();
    std::vector<Param> params();
    std::vector<Type> supers();
    std::vector<std::string> typeParams();  // <T, R : Bound> after a class's name (bounds and variance left to C++)
    void accessors(ClassProperty& p);
    void annotations(std::vector<std::string>* names, std::map<std::string, std::string>* args = nullptr);

    NodeP block();             // { statements }
    NodeP body();              // a block, or one statement as a block
    void statements(Node& into, bool untilBrace);
    NodeP statement();
    NodeP declaration();

    NodeP expression();
    NodeP disjunction();
    NodeP conjunction();
    NodeP equality();
    NodeP comparison();
    NodeP namedCheck();
    NodeP elvis();
    NodeP infixCall();
    NodeP range();
    NodeP additive();
    NodeP multiplicative();
    NodeP asExpression();
    NodeP prefix();
    NodeP postfix();
    NodeP primary();
    NodeP lambda();
    NodeP objectExpression();  // object : Interface { override fun … }
    NodeP ifExpression();
    NodeP whenExpression();
    NodeP string(const Token& t);
    void arguments(Node& call);
    bool typeArguments(std::vector<Type>& out);  // <...> before ( or {: false (nothing taken) if not

    std::string_view src_;
    Lexer lexer_;
    std::vector<Token> tokens_;
    size_t index_ = 0;
    bool newlines_ = true;  // newlines end statements (false inside ( ) and [ ])
    bool abstractMembers_ = false;  // an interface's: a fun may have no body
    NestedDecls* nested_ = nullptr;  // the body being read: where its nested classes go
};

}  // namespace kompozc
