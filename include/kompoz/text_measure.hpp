#pragma once

// Text laid out outside Text (Compose's TextMeasurer and TextLayoutResult): measure a text to
// draw it on a Canvas (chart labels, a custom gauge), or hear how a Text was laid out
// (onTextLayout: how many lines, whether it was cut off, where a character is).
//
//   auto measurer = rememberTextMeasurer();
//   Canvas(Modifier().size(200, 80), [=](DrawScope& d) {
//       TextLayoutResult label = measurer.measure("%42", {.fontSize = 24});
//       d.drawText(label, {d.center().x - label.size().width / 2, d.center().y - label.size().height / 2});
//   });
//
//   Text(article, Modifier(), {.maxLines = 3, .overflow = TextOverflow::Ellipsis},
//        [cut](const TextLayoutResult& r) { cut.set(r.hasVisualOverflow()); });  // "devamını oku"
//
// Offsets are UTF-8 byte offsets into the text, as everywhere in Kompoz.

#include "graphics.hpp"
#include "layout.hpp"
#include "runtime.hpp"
#include "text.hpp"
#include "widgets.hpp"

#include <memory>
#include <string>
#include <vector>

namespace kompoz {

class TextLayoutResult {
public:
    Size size() const;  // the text's extent (lines as wide as the widest)
    int lineCount() const;
    // Cut off: by maxLines, by the height it was given, or too wide for its width.
    bool hasVisualOverflow() const;
    bool didOverflowWidth() const;
    bool didOverflowHeight() const;
    int getLineStart(int line) const;  // the first byte it shows
    int getLineEnd(int line) const;    // past the last
    float getLineTop(int line) const;
    float getLineBottom(int line) const;
    float getLineBaseline(int line) const;
    float getLineLeft(int line) const;
    float getLineRight(int line) const;
    bool isLineEllipsized(int line) const;
    float firstBaseline() const;
    float lastBaseline() const;
    int getLineForOffset(int offset) const;
    int getLineForVerticalPosition(float y) const;
    int getOffsetForPosition(Offset position) const;  // the nearest caret position
    Rect getBoundingBox(int offset) const;            // the character's
    Rect getCursorRect(int offset) const;             // a caret there (no width)
    std::vector<Rect> getSelectionRects(int from, int to) const;  // what selecting [from, to) highlights
    const std::string& text() const;

    struct Data;
    std::shared_ptr<const Data> data_;
};

// Lays text out with the composition's text style and content color as defaults (Compose's
// TextMeasurer); keep it, measure while drawing.
class TextLayoutMeasurer {
public:
    TextLayoutMeasurer(TextMeasurer& measurer, Font font, Color color, Color linkColor, bool rtl = false)
        : measurer_(&measurer), font_(font), color_(color), linkColor_(linkColor), rtl_(rtl) {}
    // maxLines, overflow, softWrap, alignment and direction come from the style (autoSize doesn't).
    TextLayoutResult measure(const std::string& text, const TextStyle& style = {}, float maxWidth = kInfinity,
                             float maxHeight = kInfinity) const;
    TextLayoutResult measure(const AnnotatedString& text, const TextStyle& style = {}, float maxWidth = kInfinity,
                             float maxHeight = kInfinity) const;
    friend bool operator==(const TextLayoutMeasurer&, const TextLayoutMeasurer&) = default;

private:
    TextMeasurer* measurer_;
    Font font_;
    Color color_, linkColor_;
    bool rtl_ = false;  // laid out right to left where it was remembered: text with no direction of its own goes so
};

TextLayoutMeasurer rememberTextMeasurer(CallSite site = CallSite::current());

// Text that tells how it was laid out (each time that changes).
void Text(std::string text, const Modifier& modifier, const TextStyle& style, std::function<void(const TextLayoutResult&)> onTextLayout,
          CallSite site = CallSite::current());
void Text(const AnnotatedString& text, const Modifier& modifier, const TextStyle& style,
          std::function<void(const TextLayoutResult&)> onTextLayout, CallSite site = CallSite::current());

} // namespace kompoz
