#pragma once

// kompozc's parser: a @Composable function's Kotlin, from its @ to its closing brace.

#include "ast.hpp"

namespace kompozc {

class Parser {
public:
    Parser(std::string_view source, Pos start);

    // [@Composable] [@Other...] [private] fun [Receiver.]name(params)[: Type] { body } (or = expression).
    Function function();
    // data class Name(val a: T, var b: U = d)
    DataClass dataClass();
    // enum class Name { A, B, C }
    EnumClass enumClass();
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
    void annotations(std::vector<std::string>* names);

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
};

}  // namespace kompozc
