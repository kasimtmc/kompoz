#include "lexer.hpp"

#include <cctype>

namespace kompozc {

namespace {

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80; }
bool identPart(char c) { return identStart(c) || std::isdigit(static_cast<unsigned char>(c)); }

// Longest first: the lexer takes the first that matches.
constexpr std::string_view kOps[] = {"===", "!==", "..<", "?.", "?:", "!!", "->", "..", "::", "==", "!=", "<=", ">=",
                                     "&&",  "||",  "+=",  "-=", "*=", "/=", "%=", "++", "--", "(",  ")",  "{",  "}",
                                     "[",   "]",   ".",   ",",  ";",  ":",  "?",  "!",  "=",  "<",  ">",  "+",  "-",
                                     "*",   "/",   "%",   "@",  "&",  "|"};

}  // namespace

Lexer::Lexer(std::string_view source, Pos start) : src_(source), pos_(start) {}

char Lexer::peek(size_t ahead) const {
    size_t i = pos_.offset + ahead;
    return i < src_.size() ? src_[i] : '\0';
}

void Lexer::advance(size_t n) {
    for (size_t i = 0; i < n && pos_.offset < src_.size(); ++i) {
        if (src_[pos_.offset] == '\n') {
            ++pos_.line;
            pos_.column = 1;
        } else if ((static_cast<unsigned char>(src_[pos_.offset]) & 0xC0) != 0x80) {
            ++pos_.column;  // (a column per character, not per UTF-8 byte)
        }
        ++pos_.offset;
    }
}

// Spaces, tabs and comments; a newline is a token of its own.
void Lexer::skipSpaceAndComments(bool& sawSpace) {
    for (;;) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f') {
            advance();
            sawSpace = true;
        } else if (c == '/' && peek(1) == '/') {
            while (peek() != '\n' && peek() != '\0') advance();
        } else if (c == '/' && peek(1) == '*') {
            Pos start = pos_;
            advance(2);
            int depth = 1;  // Kotlin's block comments nest
            while (depth > 0) {
                if (peek() == '\0') throw CompileError(start, "unterminated comment");
                if (peek() == '/' && peek(1) == '*') {
                    advance(2);
                    ++depth;
                } else if (peek() == '*' && peek(1) == '/') {
                    advance(2);
                    --depth;
                } else {
                    advance();
                }
            }
            sawSpace = true;
        } else {
            return;
        }
    }
}

Token Lexer::next() {
    bool sawSpace = false;
    skipSpaceAndComments(sawSpace);
    Token t;
    t.pos = pos_;
    t.spaceBefore = sawSpace;
    char c = peek();
    if (c == '\0') {
        t.kind = Tok::End;
    } else if (c == '\n') {
        advance();
        t.kind = Tok::Newline;
    } else if (identStart(c)) {
        size_t start = pos_.offset;
        while (identPart(peek())) advance();
        t.kind = Tok::Ident;
        t.text = std::string(src_.substr(start, pos_.offset - start));
    } else if (c == '`') {
        advance();
        size_t start = pos_.offset;
        while (peek() != '`') {
            if (peek() == '\0' || peek() == '\n') throw CompileError(t.pos, "unterminated `name`");
            advance();
        }
        t.kind = Tok::Ident;
        t.text = std::string(src_.substr(start, pos_.offset - start));
        advance();
    } else if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
        t = number(t.pos);
        t.spaceBefore = sawSpace;
    } else if (c == '"') {
        t = string(t.pos);
        t.spaceBefore = sawSpace;
    } else if (c == '\'') {
        advance();
        t.kind = Tok::Char;
        if (peek() == '\\') {
            advance();
            t.text = escape(t.pos);
        } else {
            size_t start = pos_.offset;
            advance();
            while ((static_cast<unsigned char>(peek()) & 0xC0) == 0x80) advance();  // the rest of a UTF-8 character
            t.text = std::string(src_.substr(start, pos_.offset - start));
        }
        if (peek() != '\'') throw CompileError(t.pos, "a character literal holds one character");
        advance();
    } else {
        for (std::string_view op : kOps) {
            if (src_.substr(pos_.offset, op.size()) == op) {
                t.kind = Tok::Op;
                t.text = std::string(op);
                advance(op.size());
                break;
            }
        }
        if (t.kind != Tok::Op) throw CompileError(t.pos, std::string("unexpected character '") + c + "'");
    }
    t.end = pos_;
    return t;
}

Token Lexer::number(Pos start) {
    Token t;
    t.kind = Tok::Number;
    t.pos = start;
    auto digits = [&](auto isDigit) {
        while (isDigit(peek()) || (peek() == '_' && isDigit(peek(1)))) {
            if (peek() != '_') t.text += peek();  // (1_000: the separators dropped)
            advance();
        }
    };
    auto dec = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        t.text = "0x";
        advance(2);
        digits([](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
    } else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
        t.text = "0b";
        advance(2);
        digits([](char c) { return c == '0' || c == '1'; });
    } else {
        digits(dec);
        // 1.5 is a number; 16.dp is 16, then a member.
        if (peek() == '.' && dec(peek(1))) {
            t.text += '.';
            advance();
            digits(dec);
        }
        if ((peek() == 'e' || peek() == 'E') && (dec(peek(1)) || ((peek(1) == '+' || peek(1) == '-') && dec(peek(2))))) {
            t.text += peek();
            advance();
            if (peek() == '+' || peek() == '-') {
                t.text += peek();
                advance();
            }
            digits(dec);
        }
    }
    // Suffixes: f F (Float), L (Long), u U (unsigned), uL UL.
    while (peek() == 'f' || peek() == 'F' || peek() == 'L' || peek() == 'u' || peek() == 'U') {
        t.text += peek();
        advance();
    }
    if (identPart(peek())) throw CompileError(start, "unexpected character after a number");
    return t;
}

std::string Lexer::escape(Pos at) {
    char c = peek();
    advance();
    switch (c) {
        case 't': return "\\t";
        case 'b': return "\\b";
        case 'n': return "\\n";
        case 'r': return "\\r";
        case '\'': return "\\'";
        case '"': return "\\\"";
        case '\\': return "\\\\";
        case '$': return "$";
        case 'u': {
            std::string hex;
            for (int i = 0; i < 4; ++i) {
                if (!std::isxdigit(static_cast<unsigned char>(peek()))) throw CompileError(at, "\\u needs four hex digits");
                hex += peek();
                advance();
            }
            return "\\u" + hex;
        }
        default: throw CompileError(at, std::string("unknown escape \\") + c);
    }
}

// "..." with escapes, or """...""" raw; both with $name and ${expression} templates.
Token Lexer::string(Pos start) {
    Token t;
    t.kind = Tok::String;
    t.pos = start;
    bool raw = peek(1) == '"' && peek(2) == '"';
    advance(raw ? 3 : 1);
    std::string text;
    auto flush = [&] {
        if (!text.empty()) t.parts.push_back({false, std::move(text), {}});
        text.clear();
    };
    for (;;) {
        char c = peek();
        if (c == '\0' || (!raw && c == '\n')) throw CompileError(start, "unterminated string");
        if (raw && c == '"' && peek(1) == '"' && peek(2) == '"' && peek(3) != '"') {
            advance(3);
            break;
        }
        if (!raw && c == '"') {
            advance();
            break;
        }
        if (!raw && c == '\\') {
            Pos at = pos_;
            advance();
            text += escape(at);
        } else if (c == '$' && identStart(peek(1))) {
            flush();
            advance();
            Pos at = pos_;
            size_t from = pos_.offset;
            while (identPart(peek())) advance();
            t.parts.push_back({true, std::string(src_.substr(from, pos_.offset - from)), at});
        } else if (c == '$' && peek(1) == '{') {
            flush();
            advance(2);
            Pos at = pos_;
            // The expression ends at the brace that closes it: read as tokens, so a brace in a
            // string inside it doesn't count.
            Lexer inner(src_, pos_);
            int depth = 0;
            for (;;) {
                Token tok = inner.next();
                if (tok.kind == Tok::End) throw CompileError(at, "unterminated ${...}");
                if (tok.kind == Tok::Op && tok.text == "{") ++depth;
                if (tok.kind == Tok::Op && tok.text == "}") {
                    if (depth-- == 0) {
                        t.parts.push_back({true, std::string(src_.substr(at.offset, tok.pos.offset - at.offset)), at});
                        pos_ = tok.end;
                        break;
                    }
                }
            }
        } else if (raw && (c == '"' || c == '\\')) {
            text += '\\';
            text += c;
            advance();
        } else if (raw && c == '\n') {
            text += "\\n";
            advance();
        } else if (c == '?' && peek(1) == '?') {
            text += "?\\?";  // (no trigraph, whatever the C++ compiler's mode)
            advance(2);
        } else {
            text += c;
            advance();
        }
    }
    flush();
    return t;
}

}  // namespace kompozc
