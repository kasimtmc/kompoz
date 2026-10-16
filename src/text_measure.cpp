#include "text_layout.hpp"

#include <kompoz/host.hpp>
#include <kompoz/material.hpp>
#include <kompoz/text_measure.hpp>

#include <algorithm>
#include <cmath>

namespace kompoz {

namespace {

const detail::TextLine* lineAt(const TextLayoutResult::Data& d, int line) {
    return line >= 0 && size_t(line) < d.layout.lines.size() ? &d.layout.lines[size_t(line)] : nullptr;
}

} // namespace

Size TextLayoutResult::size() const { return {std::ceil(data_->width), std::ceil(data_->layout.height)}; }
int TextLayoutResult::lineCount() const { return int(data_->layout.lines.size()); }
bool TextLayoutResult::didOverflowWidth() const { return data_->layout.tooWide; }
bool TextLayoutResult::didOverflowHeight() const { return data_->layout.truncated || data_->layout.height > data_->maxHeight + 0.5f; }
bool TextLayoutResult::hasVisualOverflow() const { return didOverflowWidth() || didOverflowHeight(); }

int TextLayoutResult::getLineStart(int line) const {
    auto* l = lineAt(*data_, line);
    return l ? l->start : 0;
}
int TextLayoutResult::getLineEnd(int line) const {
    auto* l = lineAt(*data_, line);
    return l ? l->end : 0;
}
float TextLayoutResult::getLineTop(int line) const {
    auto* l = lineAt(*data_, line);
    return l ? l->top : 0;
}
float TextLayoutResult::getLineBottom(int line) const {
    auto* l = lineAt(*data_, line);
    return l ? l->top + l->height : 0;
}
float TextLayoutResult::getLineBaseline(int line) const {
    auto* l = lineAt(*data_, line);
    return l ? l->top + l->ascent : 0;
}
float TextLayoutResult::getLineLeft(int line) const {
    auto* l = lineAt(*data_, line);
    return l ? detail::alignedX(data_->align, l->rtl, data_->width, l->width) : 0;
}
float TextLayoutResult::getLineRight(int line) const {
    auto* l = lineAt(*data_, line);
    return l ? getLineLeft(line) + l->width : 0;
}
bool TextLayoutResult::isLineEllipsized(int line) const {
    auto* l = lineAt(*data_, line);
    return l && l->ellipsized;
}
float TextLayoutResult::firstBaseline() const { return getLineBaseline(0); }
float TextLayoutResult::lastBaseline() const { return getLineBaseline(lineCount() - 1); }

int TextLayoutResult::getLineForOffset(int offset) const {
    const auto& lines = data_->layout.lines;
    for (size_t i = 0; i < lines.size(); ++i)
        if (offset < lines[i].end || (offset == lines[i].end && i + 1 == lines.size())) return int(i);
    return lines.empty() ? 0 : int(lines.size()) - 1;
}
int TextLayoutResult::getLineForVerticalPosition(float y) const {
    const auto& lines = data_->layout.lines;
    for (size_t i = 0; i < lines.size(); ++i)
        if (y < lines[i].top + lines[i].height) return int(i);
    return lines.empty() ? 0 : int(lines.size()) - 1;
}
int TextLayoutResult::getOffsetForPosition(Offset position) const {
    const Data& d = *data_;
    return d.measurer ? detail::offsetAt(d.layout, *d.measurer, d.text, position, d.width, d.align) : 0;
}
std::vector<Rect> TextLayoutResult::getSelectionRects(int from, int to) const {
    const Data& d = *data_;
    return d.measurer && from < to ? detail::selectionRects(d.layout, *d.measurer, d.text, from, to, d.width, d.align) : std::vector<Rect>{};
}
Rect TextLayoutResult::getBoundingBox(int offset) const {
    const Data& d = *data_;
    if (offset < 0 || size_t(offset) >= d.text.size()) return {};
    size_t next = size_t(offset);
    utf8::next(d.text, next);
    auto rects = getSelectionRects(offset, int(next));
    return rects.empty() ? Rect{} : rects.front();
}
Rect TextLayoutResult::getCursorRect(int offset) const {
    const Data& d = *data_;
    if (!d.measurer || d.layout.lines.empty()) return {};
    Offset bottom = detail::caretPoint(d.layout, *d.measurer, d.text, offset, d.width, d.align);
    int line = getLineForOffset(offset);
    return {bottom.x, getLineTop(line), 0, bottom.y - getLineTop(line)};
}
const std::string& TextLayoutResult::text() const { return data_->text; }

namespace detail {

TextLayoutResult makeTextLayoutResult(StyledTextLayout layout, std::string text, TextMeasurer& measurer, TextAlign align,
                                      TextOverflow overflow, float width, float maxWidth, float maxHeight) {
    auto d = std::make_shared<TextLayoutResult::Data>();
    d->layout = std::move(layout);
    d->text = std::move(text);
    d->measurer = &measurer;
    d->align = align;
    d->overflow = overflow;
    d->width = width;
    d->maxWidth = maxWidth;
    d->maxHeight = maxHeight;
    TextLayoutResult r;
    r.data_ = std::move(d);
    return r;
}

} // namespace detail

TextLayoutResult TextLayoutMeasurer::measure(const std::string& text, const TextStyle& style, float maxWidth, float maxHeight) const {
    return measure(AnnotatedString(text), style, maxWidth, maxHeight);
}

TextLayoutResult TextLayoutMeasurer::measure(const AnnotatedString& text, const TextStyle& style, float maxWidth, float maxHeight) const {
    Font font = detail::styledFont(style, font_);
    auto runs = detail::resolveRuns(text, font, style.color.value_or(color_), style.textDecoration, linkColor_, style.brush, style.shadow);
    TextDirection direction = style.textDirection == TextDirection::Content
                                  ? (rtl_ ? TextDirection::ContentOrRtl : TextDirection::ContentOrLtr)
                                  : style.textDirection;
    auto layout = detail::layoutStyledText(*measurer_, text.text(), runs, maxWidth, style.maxLines, style.softWrap, style.overflow,
                                           direction, style.textIndent, style.textAlign == TextAlign::Justify, style.lineBreak.strategy,
                                           &text.paragraphStyles());
    // As wide as its widest line would be unwrapped, at most the width (Compose's paragraph width):
    // wrapped text aligns in the whole width.
    float width = layout.width;
    if (maxWidth < kInfinity && style.softWrap) {
        auto natural = detail::layoutStyledText(*measurer_, text.text(), std::move(runs), kInfinity, style.maxLines, true, style.overflow,
                                                direction, style.textIndent, false, LineBreak::Strategy::Simple, &text.paragraphStyles());
        width = std::max(width, std::min(natural.width, maxWidth));
    }
    return detail::makeTextLayoutResult(std::move(layout), text.text(), *measurer_, style.textAlign, style.overflow, width, maxWidth, maxHeight);
}

TextLayoutMeasurer rememberTextMeasurer(CallSite site) {
    ComposableScope scope(site);
    return TextLayoutMeasurer(detail::Composer::require().host().textMeasurer(), LocalTextStyle.current(), LocalContentColor.current(),
                              colorScheme().primary, LocalLayoutDirection.current() == LayoutDirection::Rtl);
}

} // namespace kompoz
