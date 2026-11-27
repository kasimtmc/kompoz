#pragma once

// Markdown read into blocks and inlines: CommonMark's (headings, paragraphs, block quotes, lists,
// fenced and indented code, thematic breaks, emphasis, code spans, links, images, autolinks, hard
// breaks, escapes and entities) and GitHub's (tables, task list items, strikethrough, bare URLs).
// What Markdown (markdown.hpp) shows and a rich text (richtext.hpp) takes in.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kompoz::markdown {

struct Inline {
    enum class Kind { Text, Emphasis, Strong, Strikethrough, Code, Link, Image, HardBreak, SoftBreak, Html };
    Kind kind = Kind::Text;
    std::string text;         // Text's, Code's, Html's (the tag)
    std::string url, title;   // Link's, Image's
    std::vector<Inline> children;  // Emphasis', Strong's, Strikethrough's, Link's; Image's: its alt text
};

enum class TableAlign { None, Left, Center, Right };

struct Block {
    enum class Kind { Paragraph, Heading, BlockQuote, List, ListItem, CodeBlock, ThematicBreak, Table, HtmlBlock };
    Kind kind = Kind::Paragraph;
    int level = 0;            // Heading's 1..6
    bool ordered = false;     // List's
    int start = 1;            // an ordered List's first number
    bool tight = true;        // List's: its items' paragraphs without blank lines between
    char marker = '-';        // List's: '-', '*', '+' or the ordered one's '.' or ')'
    std::optional<bool> checked;  // a task ListItem's
    bool fenced = false;      // CodeBlock's: in ``` or ~~~ (else indented)
    std::string info;         // CodeBlock's (a fence's language)
    std::string literal;      // CodeBlock's text, HtmlBlock's
    std::vector<Inline> inlines;   // Paragraph's, Heading's
    std::vector<Block> children;   // BlockQuote's, List's (its items), ListItem's
    std::vector<TableAlign> aligns;                       // Table's columns
    std::vector<std::vector<std::vector<Inline>>> rows;   // Table's: the header, then the body
};

std::vector<Block> parse(std::string_view markdown);
std::vector<Inline> parseInlines(std::string_view text);
// The text of inlines without their markup (an image's alt text, a heading's anchor).
std::string plainText(const std::vector<Inline>& inlines);

} // namespace kompoz::markdown
