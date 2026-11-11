#pragma once

// kompozc's tokens: the Kotlin that @Kompozable functions are written in, read from a C++ source.

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kompozc {

// A place in the source: 1-based line and column, and the byte offset.
struct Pos {
    int line = 1;
    int column = 1;
    size_t offset = 0;
};

// A mistake in the Kotlin, reported as the compiler would: file:line:column: error: ...
struct CompileError : std::runtime_error {
    Pos pos;
    CompileError(Pos p, const std::string& message) : std::runtime_error(message), pos(p) {}
};

enum class Tok {
    Ident,    // names and keywords (fun, val, if, ...); `quoted` names without the backticks
    Number,   // as written: 16, 1.5f, 0xFF00FF00, 10L, 1_000
    String,   // "..." or """...""": its parts
    Char,     // 'a': the C++ form of the character between the quotes
    Op,       // punctuation and operators: ( ) { } [ ] . , ; : ?. ?: !! -> .. ..< == += ...
    Newline,  // ends a statement unless the expression goes on
    End,
};

// A piece of a string literal: text (in C++ escape form) or a template expression ($name,
// ${...}): its source and where it starts.
struct StringPart {
    bool expr = false;
    std::string text;
    Pos pos;
};

struct Token {
    Tok kind = Tok::End;
    std::string text;
    std::vector<StringPart> parts;  // a String's
    Pos pos;
    Pos end;                        // just past it
    bool spaceBefore = false;       // whitespace (not a newline) right before it
};

// Reads tokens from `source` starting at `start`, on demand: where a @Kompozable function ends
// is known only once it is parsed.
class Lexer {
public:
    Lexer(std::string_view source, Pos start);
    Token next();

private:
    char peek(size_t ahead = 0) const;
    void advance(size_t n = 1);
    void skipSpaceAndComments(bool& sawSpace);
    Token string(Pos start);
    Token number(Pos start);
    std::string escape(Pos at);  // after a backslash: the C++ form of the escape

    std::string_view src_;
    Pos pos_;
};

}  // namespace kompozc
