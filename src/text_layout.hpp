#pragma once

// Internal: line breaking and drawing for text made of styled runs (Text, AnnotatedString).

#include <kompoz/text.hpp>
#include <kompoz/text_measure.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kompoz::detail {

// What a double click selects: letters, digits, '_' and anything beyond ASCII (a byte of it).
inline bool isWordByte(char c) {
    auto u = static_cast<unsigned char>(c);
    return u >= 0x80 || std::isalnum(u) || u == '_';
}
// The word around `offset` (between two words: the gap), and the paragraph (between new lines).
inline std::pair<int, int> wordAround(std::string_view text, int offset) {
    size_t from = size_t(std::clamp(offset, 0, int(text.size()))), to = from;
    while (from > 0 && isWordByte(text[from - 1])) --from;
    while (to < text.size() && isWordByte(text[to])) ++to;
    return {int(from), int(to)};
}
inline std::pair<int, int> paragraphAround(std::string_view text, int offset) {
    size_t from = size_t(std::clamp(offset, 0, int(text.size()))), to = from;
    while (from > 0 && text[from - 1] != '\n') --from;
    while (to < text.size() && text[to] != '\n') ++to;
    return {int(from), int(to)};
}

// A stretch of text with one style. Runs cover the text back to back.
struct TextRun {
    int start = 0, end = 0;
    Font font;
    Color color;
    TextDecoration decoration = TextDecoration::None;
    std::optional<Color> background;
    int link = -1;  // index into the AnnotatedString's links
    std::optional<Brush> brush;  // instead of the color
    std::optional<Shadow> shadow;
    float baselineShift = 0;  // up by this share of its ascent (down if negative)
    // Inline content: room of this size (sp) instead of its text, the nth placeholder of the text.
    std::optional<Placeholder> placeholder;
    int inlineIndex = -1;
    // What a span (or a link) gave it rather than the text's style: drawText's color (or brush),
    // shadow, decoration and draw style go only where these aren't.
    enum : uint8_t { OwnPaint = 1, OwnShadow = 2, OwnDecoration = 4, OwnDrawStyle = 8 };
    uint8_t own = 0;
};

struct TextPiece {
    std::string text;  // may end with an ellipsis that isn't in the source
    int run = 0;
    float x = 0, width = 0;
    int start = 0;   // where it starts in the source text
    float rise = 0;  // dp above the line's baseline (a baseline shift)
    bool rtl = false;  // drawn right to left: its paragraph's way, or its stretch's in a visual line
};

struct TextLine {
    std::vector<TextPiece> pieces;
    float width = 0, top = 0, ascent = 0, descent = 0, height = 0;
    int start = 0, end = 0;  // the part of the source text it shows
    bool rtl = false;        // its paragraph goes right to left (pieces are placed from the right)
    std::optional<TextAlign> align;  // its paragraph's own (a ParagraphStyle's), instead of the text's
    // Its pieces are in screen order, from the left (a line of both directions, cut where its
    // stretches of one direction meet), each drawn its stretch's way.
    bool visual = false;
    bool ellipsized = false; // cut off with an ellipsis
};

struct StyledTextLayout {
    std::vector<TextRun> runs;
    std::vector<TextLine> lines;
    float width = 0, height = 0;
    bool clipped = false;    // something didn't fit (TextOverflow::Clip draws inside the bounds)
    bool truncated = false;  // more lines than maxLines
    bool tooWide = false;    // a line wider than the width (no soft wrap, or a word too long to split)
    bool wrapped = false;    // a paragraph went over more than one line
};

} // namespace kompoz::detail

namespace kompoz {
struct TextLayoutResult::Data {
    detail::StyledTextLayout layout;
    std::string text;
    TextMeasurer* measurer = nullptr;
    TextAlign align = TextAlign::Start;
    TextOverflow overflow = TextOverflow::Clip;
    float width = 0;  // what the lines are aligned in
    float maxWidth = kInfinity, maxHeight = kInfinity;
};
} // namespace kompoz

namespace kompoz::detail {

// `base` with what `style` sets (its font, then size, weight, style, family, spacing, line height).
Font styledFont(const TextStyle& style, Font base);
// `font`'s glyphs filled or outlined as `style` says.
void styleGlyphs(Font& font, const DrawStyle& style);

// Runs for `text` with the base style and the spans (and links) applied on top.
// Inline contents' ranges become placeholder runs (in order: their inline indices).
struct PlaceholderRange {
    int start = 0, end = 0;
    Placeholder placeholder;
};
std::vector<TextRun> resolveRuns(const AnnotatedString& text, const Font& base, Color color, TextDecoration decoration,
                                 Color linkColor, const std::optional<Brush>& brush = {}, const std::optional<Shadow>& shadow = {},
                                 const std::vector<PlaceholderRange>& placeholders = {});
// Where each placeholder of a layout is, from its top left, its lines aligned in `width` (by
// inline index; empty where none was laid out, past maxLines).
std::vector<Rect> placeholderRects(const StyledTextLayout& layout, TextMeasurer& measurer, float width, TextAlign align);

// `indent` (sp) narrows lines from the start; `justify` stretches wrapped lines to maxWidth;
// `strategy` picks where wrapped lines break.
// `paragraphs` (an AnnotatedString's ParagraphStyles) change those for their ranges, each a
// paragraph of its own.
StyledTextLayout layoutStyledText(TextMeasurer& measurer, const std::string& text, std::vector<TextRun> runs,
                                  float maxWidth, int maxLines, bool softWrap, TextOverflow overflow,
                                  TextDirection direction = TextDirection::Content, TextIndent indent = {}, bool justify = false,
                                  LineBreak::Strategy strategy = LineBreak::Strategy::Simple,
                                  const std::vector<AnnotatedString::ParagraphSpan>* paragraphs = nullptr);

// Whether a paragraph goes right to left.
bool isRtl(TextDirection direction, std::string_view paragraph);
// Where a line `lineWidth` wide starts in `width`, for its alignment and direction.
float alignedX(TextAlign align, bool rtl, float width, float lineWidth);
// The caret's x at `offset` in text drawn in one font from x = 0 in a paragraph of the given
// direction: right-to-left stretches put it from their right edge. At the boundary of two
// stretches it follows the character before it.
float caretX(TextMeasurer& measurer, std::string_view text, const Font& font, int offset, bool rtl);
// The range [from, to) of that text as spans of x, one per stretch it touches.
std::vector<std::pair<float, float>> rangeXs(TextMeasurer& measurer, std::string_view text, const Font& font, int from,
                                             int to, bool rtl);

void drawStyledText(GraphicsContext& canvas, const StyledTextLayout& layout, const Rect& bounds, TextAlign align,
                    TextOverflow overflow);

// A public TextLayoutResult for a layout: its lines aligned in `width`, laid out for at most
// maxWidth x maxHeight.
TextLayoutResult makeTextLayoutResult(StyledTextLayout layout, std::string text, TextMeasurer& measurer, TextAlign align,
                                      TextOverflow overflow, float width, float maxWidth, float maxHeight);

// The run under a point (local to the text), or -1.
int runAt(const StyledTextLayout& layout, Offset p, float boundsWidth, TextAlign align);

// Selection in laid out text (offsets are bytes of the source text, at code point boundaries):
// the offset nearest a point, the rectangles a range covers, and where the caret at an offset
// stands (its x, and the bottom of its line).
int offsetAt(const StyledTextLayout& layout, TextMeasurer& measurer, const std::string& text, Offset p, float boundsWidth,
             TextAlign align);
std::vector<Rect> selectionRects(const StyledTextLayout& layout, TextMeasurer& measurer, const std::string& text, int from, int to,
                                 float boundsWidth, TextAlign align);
Offset caretPoint(const StyledTextLayout& layout, TextMeasurer& measurer, const std::string& text, int offset, float boundsWidth,
                  TextAlign align);

} // namespace kompoz::detail
