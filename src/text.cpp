#include "text_layout.hpp"

#include <kompoz/bidi.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace kompoz {

int AnnotatedStringBuilder::pushStyle(const SpanStyle& style) {
    pushed_.push_back({length(), style, std::nullopt});
    return int(pushed_.size()) - 1;
}
int AnnotatedStringBuilder::pushStringAnnotation(std::string tag, std::string annotation) {
    pushed_.push_back({length(), std::nullopt, std::pair{std::move(tag), std::move(annotation)}});
    return int(pushed_.size()) - 1;
}
int AnnotatedStringBuilder::pushStyle(const ParagraphStyle& style) {
    pushed_.push_back({length(), std::nullopt, std::nullopt, style});
    return int(pushed_.size()) - 1;
}
void AnnotatedStringBuilder::withStyle(const ParagraphStyle& style, const std::function<void()>& content) {
    int start = length();
    content();
    s_.paragraphStyles_.push_back({start, length(), style});
}
void AnnotatedStringBuilder::pop() {
    if (pushed_.empty()) return;
    Pushed p = std::move(pushed_.back());
    pushed_.pop_back();
    if (p.style) s_.spans_.push_back({p.start, length(), *p.style});
    if (p.annotation) s_.annotations_.push_back({std::move(p.annotation->first), std::move(p.annotation->second), p.start, length()});
    if (p.paragraph) s_.paragraphStyles_.push_back({p.start, length(), *p.paragraph});
}
void AnnotatedStringBuilder::pop(int index) {
    while (int(pushed_.size()) > std::max(0, index)) pop();
}
void AnnotatedStringBuilder::withAnnotation(std::string tag, std::string annotation, const std::function<void()>& content) {
    int start = length();
    content();
    addStringAnnotation(std::move(tag), std::move(annotation), start, length());
}

namespace {
// Compose's range intersection for annotation queries: overlapping, or one inside the other (a
// collapsed range — an offset — inside a range counts, except at its end).
bool contains(int baseStart, int baseEnd, int targetStart, int targetEnd) {
    return baseStart <= targetStart && targetEnd <= baseEnd && (baseEnd != targetEnd || (targetStart == targetEnd) == (baseStart == baseEnd));
}
bool intersects(int lStart, int lEnd, int rStart, int rEnd) {
    return std::max(lStart, rStart) < std::min(lEnd, rEnd) || contains(lStart, lEnd, rStart, rEnd) || contains(rStart, rEnd, lStart, lEnd);
}
} // namespace

std::vector<AnnotatedString::Annotation> AnnotatedString::getStringAnnotations(const std::string& tag, int start, int end) const {
    std::vector<Annotation> out;
    for (const Annotation& a : annotations_)
        if (a.tag == tag && intersects(start, end, a.start, a.end)) out.push_back(a);
    return out;
}
std::vector<AnnotatedString::Annotation> AnnotatedString::getStringAnnotations(int start, int end) const {
    std::vector<Annotation> out;
    for (const Annotation& a : annotations_)
        if (intersects(start, end, a.start, a.end)) out.push_back(a);
    return out;
}

void AnnotatedStringBuilder::withStyle(const SpanStyle& style, const std::function<void()>& content) {
    int start = length();
    content();
    s_.spans_.push_back({start, length(), style});
}

void AnnotatedStringBuilder::withLink(std::function<void()> onClick, const std::function<void()>& content,
                                        SpanStyle style) {
    int start = length();
    content();
    s_.links_.push_back({start, length(), std::move(onClick)});
    s_.spans_.push_back({start, length(), style});
}

void AnnotatedStringBuilder::withLink(const LinkAnnotation& link, const std::function<void()>& content) {
    int start = length();
    content();
    s_.links_.push_back({start, length(), {}, link});
    s_.spans_.push_back({start, length(), {.textDecoration = TextDecoration::Underline}});
    if (link.styles && link.styles->style) s_.spans_.push_back({start, length(), *link.styles->style});
}

AnnotatedString buildAnnotatedString(const std::function<void(AnnotatedString::Builder&)>& build) {
    AnnotatedString::Builder builder;
    build(builder);
    return builder.toAnnotatedString();
}

namespace detail {

void styleGlyphs(Font& font, const DrawStyle& style) {
    font.stroked = style.isStroke();
    font.strokeWidth = style.width, font.strokeJoin = style.join, font.strokeMiter = style.miter;
}

Font styledFont(const TextStyle& style, Font base) {
    Font font = style.font.value_or(base);
    if (style.fontSize) font.size = style.fontSize->resolve(font.size);  // em: of the size it had
    if (style.fontWeight) font.weight = *style.fontWeight;
    if (style.fontStyle) font.style = *style.fontStyle;
    if (style.fontFamily) font.family = style.fontFamily->id;
    if (style.letterSpacing) font.letterSpacing = style.letterSpacing->resolve(font.size);  // em: of its size
    if (style.lineHeight) font.lineHeight = style.lineHeight->resolve(font.size);
    if (style.lineHeightStyle) font.lineHeightStyle = *style.lineHeightStyle;
    if (style.fontFeatureSettings) font.features = FontFeatures::id(*style.fontFeatureSettings);
    if (style.drawStyle) styleGlyphs(font, *style.drawStyle);
    if (style.textGeometricTransform) font.scaleX = style.textGeometricTransform->scaleX, font.skewX = style.textGeometricTransform->skewX;
    return font;
}

std::vector<TextRun> resolveRuns(const AnnotatedString& text, const Font& base, Color color, TextDecoration decoration,
                                 Color linkColor, const std::optional<Brush>& brush, const std::optional<Shadow>& shadow,
                                 const std::vector<PlaceholderRange>& placeholders) {
    int length = int(text.text().size());
    std::vector<int> cuts{0, length};
    for (const auto& s : text.spans()) cuts.insert(cuts.end(), {std::clamp(s.start, 0, length), std::clamp(s.end, 0, length)});
    for (const auto& p : placeholders) cuts.insert(cuts.end(), {std::clamp(p.start, 0, length), std::clamp(p.end, 0, length)});
    for (const auto& l : text.links()) cuts.insert(cuts.end(), {std::clamp(l.start, 0, length), std::clamp(l.end, 0, length)});
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    std::vector<TextRun> runs;
    for (size_t i = 0; i + 1 < cuts.size(); ++i) {
        TextRun run{cuts[i], cuts[i + 1], base, color, decoration, std::nullopt, -1, brush, shadow};
        for (size_t k = 0; k < text.links().size(); ++k)
            if (text.links()[k].start <= run.start && run.end <= text.links()[k].end) {
                run.link = int(k);
                run.color = linkColor;
                run.own |= TextRun::OwnPaint;
            }
        // An inner withStyle ends (and is added) before the outer one: apply the widest spans
        // first so nested styles override the ones around them.
        std::vector<const AnnotatedString::Span*> covering;
        for (const auto& s : text.spans())
            if (s.start <= run.start && run.end <= s.end) covering.push_back(&s);
        std::stable_sort(covering.begin(), covering.end(), [](auto* a, auto* b) { return a->end - a->start > b->end - b->start; });
        for (const auto* s : covering) {
            if (s->style.fontSize) run.font.size = s->style.fontSize->resolve(run.font.size);
            if (s->style.fontWeight) run.font.weight = *s->style.fontWeight;
            if (s->style.fontStyle) run.font.style = *s->style.fontStyle;
            if (s->style.fontFamily) run.font.family = s->style.fontFamily->id;
            if (s->style.color) run.color = *s->style.color, run.own |= TextRun::OwnPaint;
            if (s->style.textDecoration) run.decoration = *s->style.textDecoration, run.own |= TextRun::OwnDecoration;
            if (s->style.background) run.background = s->style.background;
            if (s->style.letterSpacing) run.font.letterSpacing = s->style.letterSpacing->resolve(run.font.size);
            if (s->style.fontFeatureSettings) run.font.features = FontFeatures::id(*s->style.fontFeatureSettings);
            if (s->style.color) run.brush.reset();  // a span's color goes over the text's brush
            if (s->style.brush) run.brush = s->style.brush, run.own |= TextRun::OwnPaint;
            if (s->style.shadow) run.shadow = s->style.shadow, run.own |= TextRun::OwnShadow;
            if (s->style.baselineShift) run.baselineShift = *s->style.baselineShift;
            if (s->style.drawStyle) styleGlyphs(run.font, *s->style.drawStyle), run.own |= TextRun::OwnDrawStyle;
            if (const auto& t = s->style.textGeometricTransform) run.font.scaleX = t->scaleX, run.font.skewX = t->skewX;
        }
        for (size_t k = 0; k < placeholders.size(); ++k)  // inline content: its room, em of the size there
            if (placeholders[k].start <= run.start && run.end <= placeholders[k].end) {
                Placeholder p = placeholders[k].placeholder;
                p.width = TextUnit::sp(p.width.resolve(run.font.size));
                p.height = TextUnit::sp(p.height.resolve(run.font.size));
                run.placeholder = p;
                run.inlineIndex = int(k);
            }
        runs.push_back(run);
    }
    if (runs.empty()) runs.push_back({0, 0, base, color, decoration, std::nullopt, -1, brush, shadow});
    return runs;
}

namespace {

constexpr std::string_view kEllipsis = "…";

// An inline content's room: its width, and how far it reaches above and below the baseline (dp).
float placeholderWidth(const TextRun& run, TextMeasurer& measurer) { return run.placeholder->width.value * measurer.fontScale(); }
std::pair<float, float> placeholderExtent(const TextRun& run, TextMeasurer& measurer) {
    float h = run.placeholder->height.value * measurer.fontScale();
    FontMetrics fm = measurer.metrics(run.font);
    switch (run.placeholder->verticalAlign) {
    case PlaceholderVerticalAlign::AboveBaseline: return {h, 0.f};
    case PlaceholderVerticalAlign::Top:
    case PlaceholderVerticalAlign::TextTop: return {fm.ascent, h - fm.ascent};
    case PlaceholderVerticalAlign::Bottom:
    case PlaceholderVerticalAlign::TextBottom: return {h - fm.descent, fm.descent};
    default: {  // Center, TextCenter: on the middle of the text's ascent and descent
        float middle = (fm.ascent - fm.descent) / 2;
        return {middle + h / 2, h / 2 - middle};
    }
    }
}

struct Measure {
    TextMeasurer& measurer;
    const std::string& text;
    const std::vector<TextRun>& runs;

    int runOf(int offset) const {
        for (size_t i = 0; i < runs.size(); ++i)
            if (offset < runs[i].end) return int(i);
        return int(runs.size()) - 1;
    }
    // Pieces for text[from, to), split where the run changes (and after each space, `words`:
    // pieces a justified line spreads apart).
    void pieces(std::vector<TextPiece>& out, int from, int to, bool words = false) const {
        if (words) {
            for (int start = from; start < to;) {
                int space = nextSpace(start);
                int end = space < 0 || space >= to ? to : space + 1;
                std::vector<TextPiece> word;
                pieces(word, start, end);
                out.insert(out.end(), word.begin(), word.end());
                start = end;
            }
            return;
        }
        while (from < to) {
            int run = runOf(from);
            int end = std::max(from + 1, std::min(to, runs[size_t(run)].end));
            std::string_view s = std::string_view(text).substr(size_t(from), size_t(end - from));
            const TextRun& r = runs[size_t(run)];
            // Inline content (never broken): its room, not its alternate text.
            float w = r.placeholder ? placeholderWidth(r, measurer) : measurer.measureWidth(s, r.font);
            if (!out.empty() && out.back().run == run) {
                out.back().text += s;
                out.back().width += w;
            } else {
                out.push_back({std::string(s), run, 0, w, from});
            }
            from = end;
        }
    }
    float width(int from, int to) const {
        std::vector<TextPiece> p;
        pieces(p, from, to);
        float w = 0;
        for (const auto& piece : p) w += piece.width;
        return w;
    }
    // Whole clusters, and inline content whole (its alternate text isn't broken).
    int nextCodePoint(int i) const {
        int next = int(utf8::nextCluster(text, size_t(i)));
        const TextRun& r = runs[size_t(runOf(next))];
        return r.placeholder && next > r.start && next < r.end ? r.end : next;
    }
    // The next space from `i` (-1: none), past those in inline content's alternate text.
    int nextSpace(int i) const {
        int space = int(text.find(' ', size_t(i)));
        while (space >= 0 && runs[size_t(runOf(space))].placeholder) space = int(text.find(' ', size_t(runs[size_t(runOf(space))].end)));
        return space;
    }
};

// A line of both directions, ordered as one: its pieces (of one style each) cut where the line's
// stretches of one direction (bidi::visualRuns over the whole line, the neighbours of other
// styles counting) begin and end, put in screen order, each drawn its stretch's way. A line of
// one direction, or of one piece (the backend orders that itself), stays as it is.
void reorderPieces(TextLine& line, const Measure& m) {
    if (line.pieces.size() < 2) return;
    std::string shown;    // what the line shows (an ellipsis too)
    std::vector<int> at;  // where each piece starts in it
    for (const TextPiece& p : line.pieces) {
        at.push_back(int(shown.size()));
        shown += p.text;
    }
    auto runs = bidi::visualRuns(shown, line.rtl);
    if (runs.size() < 2) return;
    std::vector<TextPiece> ordered;
    for (const bidi::Run& r : runs) {
        std::vector<TextPiece> parts;  // the stretch's pieces, in source order
        for (size_t k = 0; k < line.pieces.size(); ++k) {
            const TextPiece& p = line.pieces[k];
            int a = std::max(r.start, at[k]), b = std::min(r.end, at[k] + int(p.text.size()));
            if (m.runs[size_t(p.run)].placeholder) {  // inline content whole, in the stretch it begins in
                if (at[k] < r.start || at[k] >= r.end) continue;
                a = at[k], b = at[k] + int(p.text.size());
            }
            if (a >= b) continue;
            TextPiece part = p;
            part.text = p.text.substr(size_t(a - at[k]), size_t(b - a));
            part.start = p.start + (a - at[k]);
            part.width = a == at[k] && b == at[k] + int(p.text.size()) ? p.width : m.measurer.measureWidth(part.text, m.runs[size_t(p.run)].font);
            part.rtl = r.rtl;
            parts.push_back(std::move(part));
        }
        if (r.rtl) std::reverse(parts.begin(), parts.end());
        ordered.insert(ordered.end(), parts.begin(), parts.end());
    }
    line.pieces = std::move(ordered);
    line.visual = true;
}


// LineBreak::Strategy::HighQuality: the words of [start, end] on the lines that leave the least
// uneven room at their ends (the sum of each line's leftover squared, the last line's free) of
// every way of breaking at the spaces. None if a word is wider than its line (the greedy way
// splits those).
std::optional<std::vector<std::pair<int, int>>> evenLines(Measure& m, int start, int end, float maxWidth,
                                                          float firstIndent, float restIndent) {
    std::vector<std::pair<int, int>> words;  // each one's bytes, the spaces between left out
    for (int i = start; i <= end;) {
        int space = m.nextSpace(i);
        int wordEnd = space < 0 || space > end ? end : space;
        words.push_back({i, wordEnd});
        i = wordEnd + 1;
    }
    size_t n = words.size();
    constexpr double kNone = std::numeric_limits<double>::infinity();
    std::vector<double> best(n + 1, kNone);  // the least cost of laying out the first i words
    std::vector<size_t> from(n + 1, 0);      // where the last line of that starts
    best[0] = 0;
    for (size_t j = 0; j < n; ++j) {  // a line from word j...
        if (best[j] == kNone) continue;
        float room = maxWidth - (j == 0 ? firstIndent : restIndent);
        for (size_t i = j + 1; i <= n; ++i) {  // ...to word i - 1
            float width = m.width(words[j].first, words[i - 1].second);
            if (width > room) {
                if (i == j + 1) return std::nullopt;
                break;
            }
            double slack = room - width, cost = best[j] + (i == n ? 0 : slack * slack);
            if (cost < best[i]) best[i] = cost, from[i] = j;
        }
    }
    if (best[n] == kNone) return std::nullopt;
    std::vector<std::pair<int, int>> lines;
    for (size_t i = n; i > 0; i = from[i]) lines.push_back({words[from[i]].first, words[i - 1].second});
    std::ranges::reverse(lines);
    return lines;
}

} // namespace

bool isRtl(TextDirection direction, std::string_view paragraph) {
    switch (direction) {
    case TextDirection::Ltr: return false;
    case TextDirection::Rtl: return true;
    case TextDirection::ContentOrRtl: return bidi::firstStrongIsRtl(paragraph, true);
    default: return bidi::firstStrongIsRtl(paragraph, false);
    }
}

float alignedX(TextAlign align, bool rtl, float width, float lineWidth) {
    float free = width - lineWidth;
    switch (align) {
    case TextAlign::Center: return free / 2;
    case TextAlign::Left: return 0;
    case TextAlign::Right: return free;
    case TextAlign::End: return rtl ? 0 : free;
    default: return rtl ? free : 0;  // Start
    }
}

float caretX(TextMeasurer& measurer, std::string_view text, const Font& font, int offset, bool rtl) {
    offset = std::clamp(offset, 0, int(text.size()));
    auto runs = bidi::visualRuns(text, rtl);
    if (runs.size() == 1 && !runs[0].rtl) return measurer.measureWidth(text.substr(0, size_t(offset)), font);
    float x = 0;
    for (const bidi::Run& r : runs) {
        float w = measurer.measureWidth(text.substr(size_t(r.start), size_t(r.end - r.start)), font);
        if (offset == 0 ? r.start == 0 : r.start < offset && offset <= r.end) {
            float in = measurer.measureWidth(text.substr(size_t(r.start), size_t(offset - r.start)), font);
            return r.rtl ? x + w - in : x + in;
        }
        x += w;
    }
    return x;
}

std::vector<std::pair<float, float>> rangeXs(TextMeasurer& measurer, std::string_view text, const Font& font, int from,
                                             int to, bool rtl) {
    std::vector<std::pair<float, float>> spans;
    float x = 0;
    for (const bidi::Run& r : bidi::visualRuns(text, rtl)) {
        auto width = [&](int end) { return measurer.measureWidth(text.substr(size_t(r.start), size_t(end - r.start)), font); };
        float w = width(r.end);
        int a = std::max(from, r.start), b = std::min(to, r.end);
        if (a < b) {
            float xa = width(a), xb = width(b);
            spans.push_back(r.rtl ? std::pair{x + w - xb, x + w - xa} : std::pair{x + xa, x + xb});
        }
        x += w;
    }
    return spans;
}

StyledTextLayout layoutStyledText(TextMeasurer& measurer, const std::string& text, std::vector<TextRun> runs,
                                  float maxWidth, int maxLines, bool softWrap, TextOverflow overflow,
                                  TextDirection direction, TextIndent indent, bool justify, LineBreak::Strategy strategy,
                                  const std::vector<AnnotatedString::ParagraphSpan>* paragraphs) {
    StyledTextLayout layout;
    layout.runs = std::move(runs);
    Measure m{measurer, text, layout.runs};
    int length = int(text.size());
    // A cut at the start or in the middle is for one line: its paragraph unwrapped. More lines clip.
    bool cutInside = overflow == TextOverflow::StartEllipsis || overflow == TextOverflow::MiddleEllipsis;
    if (cutInside && (maxLines == 1 || !softWrap)) softWrap = false;
    else if (cutInside) overflow = TextOverflow::Clip;
    // A paragraph's settings: the text's, as the ParagraphStyles over it change them (the
    // widest first, the inner winning).
    struct Paragraph {
        TextDirection direction;
        float firstIndent, restIndent;  // dp
        LineBreak::Strategy strategy;
        bool justify;
        std::optional<TextAlign> align;
        std::optional<float> lineHeight;  // sp
        std::optional<LineHeightStyle> lineStyle;
    };
    auto settingsOf = [&](int start, int end) {
        Paragraph p{direction, std::max(0.f, indent.firstLine * measurer.fontScale()), std::max(0.f, indent.restLine * measurer.fontScale()),
                    strategy, justify, std::nullopt, std::nullopt, std::nullopt};
        if (!paragraphs) return p;
        std::vector<const AnnotatedString::ParagraphSpan*> over;
        for (const auto& span : *paragraphs)
            if (span.start < span.end && span.start <= start && end <= span.end) over.push_back(&span);
        std::stable_sort(over.begin(), over.end(), [](auto* a, auto* b) { return a->end - a->start > b->end - b->start; });
        for (const auto* span : over) {
            const ParagraphStyle& s = span->style;
            if (s.textDirection) p.direction = *s.textDirection;
            if (s.textIndent) {
                p.firstIndent = std::max(0.f, s.textIndent->firstLine * measurer.fontScale());
                p.restIndent = std::max(0.f, s.textIndent->restLine * measurer.fontScale());
            }
            if (s.lineBreak) p.strategy = s.lineBreak->strategy == LineBreak::Strategy::Unspecified ? LineBreak::Strategy::Simple : s.lineBreak->strategy;
            if (s.textAlign) p.align = *s.textAlign, p.justify = *s.textAlign == TextAlign::Justify;
            if (s.lineHeight && !layout.runs.empty()) p.lineHeight = s.lineHeight->resolve(layout.runs[size_t(m.runOf(start))].font.size);
            if (s.lineHeightStyle) p.lineStyle = *s.lineHeightStyle;
        }
        return p;
    };
    std::vector<int> cuts;  // where ParagraphStyles begin and end: paragraphs of their own
    if (paragraphs)
        for (const auto& span : *paragraphs) cuts.insert(cuts.end(), {std::clamp(span.start, 0, length), std::clamp(span.end, 0, length)});
    std::sort(cuts.begin(), cuts.end());

    // Line breaking over byte ranges: paragraphs at '\n' (and ParagraphStyles' ends), greedy word
    // wrap, overlong words split. Each paragraph has its own direction and settings; its first
    // line has its own indent.
    std::vector<std::pair<int, int>> ranges;
    std::vector<bool> rtl, first, last;  // each line's paragraph direction, and whether it starts or ends it
    std::vector<Paragraph> settings;     // each line's paragraph's
    std::vector<bool> blockFirst, blockLast;  // a line starting or ending a ParagraphStyle's block (or the text)
    for (int start = 0; start <= length;) {
        int newline = int(text.find('\n', size_t(start)));
        int end = newline < 0 ? length : newline;
        bool styleCut = false;  // it ends where a ParagraphStyle begins or ends, not at a '\n'
        for (int cut : cuts)
            if (cut > start && cut < end) {
                end = cut;
                styleCut = true;
                break;
            }
        Paragraph para = settingsOf(start, end);
        float firstIndent = para.firstIndent, restIndent = para.restIndent;
        LineBreak::Strategy strategy = para.strategy;
        bool startsBlock = start == 0 || std::binary_search(cuts.begin(), cuts.end(), start);
        bool paragraphRtl = isRtl(para.direction, std::string_view(text).substr(size_t(start), size_t(end - start)));
        size_t lines = ranges.size();
        if (!softWrap) {
            ranges.push_back({start, end});
        } else {
            // Greedy: each line as full as `width` lets it be.
            bool keepWords = isMinIntrinsicWidth();  // a min intrinsic width: a word a line, none split
            auto wrap = [&](float width) {
                std::vector<std::pair<int, int>> out;
                int lineStart = start, lineEnd = start;
                auto room = [&] { return width - (lineStart == start ? firstIndent : restIndent); };
                for (int i = start; i <= end;) {
                    int space = m.nextSpace(i);
                    int wordEnd = space < 0 || space > end ? end : space;
                    if (lineEnd == lineStart || m.width(lineStart, wordEnd) <= room()) {
                        lineEnd = wordEnd;
                    } else {
                        out.push_back({lineStart, lineEnd});
                        lineStart = i;  // the space between stays behind
                        lineEnd = wordEnd;
                    }
                    while (!keepWords && m.width(lineStart, lineEnd) > room()) {  // one word wider than the line
                        int cut = m.nextCodePoint(lineStart);
                        while (cut < lineEnd && m.width(lineStart, m.nextCodePoint(cut)) <= room()) cut = m.nextCodePoint(cut);
                        if (cut >= lineEnd) break;
                        out.push_back({lineStart, cut});
                        lineStart = cut;
                    }
                    i = wordEnd + 1;
                }
                out.push_back({lineStart, lineEnd});
                return out;
            };
            std::vector<std::pair<int, int>> lines = wrap(maxWidth);
            if (lines.size() > 1 && maxWidth < kInfinity) {
                if (strategy == LineBreak::Strategy::Balanced) {
                    // As narrow as it gets with no more lines: they come out about as long.
                    float lo = 0, hi = maxWidth;
                    while (hi - lo > 0.5f) {
                        float mid = (lo + hi) / 2;
                        (wrap(mid).size() <= lines.size() ? hi : lo) = mid;
                    }
                    lines = wrap(hi);
                } else if (strategy == LineBreak::Strategy::HighQuality) {
                    lines = evenLines(m, start, end, maxWidth, firstIndent, restIndent).value_or(lines);
                }
            }
            if (lines.size() > 1) layout.wrapped = true;
            ranges.insert(ranges.end(), lines.begin(), lines.end());
        }
        rtl.resize(ranges.size(), paragraphRtl);
        first.resize(ranges.size(), false);
        last.resize(ranges.size(), false);
        settings.resize(ranges.size(), para);
        blockFirst.resize(ranges.size(), false);
        blockLast.resize(ranges.size(), false);
        first[lines] = true;
        last.back() = true;
        if (startsBlock) blockFirst[lines] = true;
        if (styleCut || end == length || std::binary_search(cuts.begin(), cuts.end(), end)) blockLast.back() = true;
        if (styleCut) {
            start = end;
            continue;
        }
        if (newline < 0) break;
        start = newline + 1;
    }

    bool truncated = maxLines > 0 && int(ranges.size()) > maxLines;
    if (truncated) ranges.resize(size_t(maxLines));
    layout.truncated = truncated;
    float textLineHeight = layout.runs.empty() ? 0 : layout.runs.front().font.lineHeight;  // sp: a paragraph's
    LineHeightStyle textLineStyle = layout.runs.empty() ? LineHeightStyle{} : layout.runs.front().font.lineHeightStyle;
    using Trim = LineHeightStyle::Trim;
    for (size_t li = 0; li < ranges.size(); ++li) {
        auto [from, to] = ranges[li];
        const Paragraph& para = settings[li];
        float lineHeight = para.lineHeight.value_or(textLineHeight) * measurer.fontScale();
        LineHeightStyle lineStyle = para.lineStyle.value_or(textLineStyle);
        // The first line of the text (or of a ParagraphStyle's block) keeps its top, the last its bottom.
        bool trimTop = (li == 0 || blockFirst[li]) && (lineStyle.trim == Trim::Both || lineStyle.trim == Trim::FirstLineTop);
        bool trimBottom = (li + 1 == ranges.size() || blockLast[li]) && (lineStyle.trim == Trim::Both || lineStyle.trim == Trim::LastLineBottom);
        TextLine line;
        line.align = para.align;
        float lead = first[li] ? para.firstIndent : para.restIndent, room = maxWidth - lead;
        // Justified: a wrapped line (not a paragraph's last) of one direction, its words apart.
        bool spread = para.justify && softWrap && maxWidth < kInfinity && !last[li] &&
                      bidi::visualRuns(std::string_view(text).substr(size_t(from), size_t(to - from)), rtl[li]).size() == 1;
        m.pieces(line.pieces, from, to, spread);
        if (line.pieces.empty()) line.pieces.push_back({"", m.runOf(from), 0, 0, from});  // keeps its style's height
        auto total = [&] {
            float w = 0;
            for (const auto& p : line.pieces) w += p.width;
            return w;
        };
        bool tooWide = total() > room;
        layout.tooWide = layout.tooWide || tooWide;
        if (cutInside && tooWide) {  // "…the end", or "the st…end"
            line.ellipsized = true;
            spread = false;
            int at = overflow == TextOverflow::StartEllipsis ? from : (from + to) / 2;
            float ellipsis = measurer.measureWidth(kEllipsis, layout.runs[size_t(m.runOf(at))].font);
            std::vector<int> cuts{from};  // the cluster boundaries
            for (int i = from; i < to;) cuts.push_back(i = m.nextCodePoint(i));
            int n = int(cuts.size()) - 1, head = 0, tail = 0;
            for (int keep = n; keep >= 0; --keep) {  // as many clusters as fit, the start getting the odd one
                head = overflow == TextOverflow::StartEllipsis ? 0 : (keep + 1) / 2;
                tail = keep - head;
                if (m.width(from, cuts[size_t(head)]) + ellipsis + m.width(cuts[size_t(n - tail)], to) <= room) break;
            }
            int a = cuts[size_t(head)], b = cuts[size_t(n - tail)];
            line.pieces.clear();
            m.pieces(line.pieces, from, a);
            line.pieces.push_back({std::string(kEllipsis), m.runOf(std::min(a, std::max(from, to - 1))), 0, ellipsis, a});
            std::vector<TextPiece> end;  // its own pieces: the ellipsis stands for what it hides
            m.pieces(end, b, to);
            line.pieces.insert(line.pieces.end(), end.begin(), end.end());
        } else if (overflow == TextOverflow::Ellipsis && ((truncated && li + 1 == ranges.size()) || tooWide)) {
            line.ellipsized = true;
            spread = false;
            const Font& font = layout.runs[size_t(line.pieces.back().run)].font;
            float ellipsis = measurer.measureWidth(kEllipsis, font);
            while (to > from && m.width(from, to) + ellipsis > room) to = int(utf8::previousCluster(text, size_t(to)));
            line.pieces.clear();
            m.pieces(line.pieces, from, to);
            if (line.pieces.empty()) line.pieces.push_back({"", m.runOf(from), 0, 0, from});
            line.pieces.back().text += kEllipsis;
            line.pieces.back().width += ellipsis;
        } else if (tooWide || truncated) {
            layout.clipped = true;
        }
        // What a justified line lacks of its room goes into the gaps after its words, evenly
        // (pieces ending in a space; others are where the style changes).
        auto gapAfter = [&](size_t k) { return k + 1 < line.pieces.size() && !line.pieces[k].text.empty() && line.pieces[k].text.back() == ' '; };
        int gaps = 0;
        for (size_t k = 0; spread && k < line.pieces.size(); ++k) gaps += gapAfter(k);
        float extra = gaps > 0 ? std::max(0.f, room - total()) / float(gaps) : 0;
        line.rtl = rtl[li];
        for (TextPiece& p : line.pieces) p.rtl = line.rtl;
        reorderPieces(line, m);
        float x = line.visual && line.rtl ? 0 : lead;  // from the left: a right-to-left indent comes last
        for (size_t k = 0; k < line.pieces.size(); ++k) {
            TextPiece& p = line.pieces[k];
            p.x = x;
            x += p.width;
            if (gaps > 0 && gapAfter(k)) x += extra;
            const TextRun& run = layout.runs[size_t(p.run)];
            FontMetrics fm = measurer.metrics(run.font);
            float shift = p.rise = run.baselineShift * fm.ascent;  // a superscript raises the line's top, a subscript its bottom
            line.ascent = std::max(line.ascent, fm.ascent + shift);
            line.descent = std::max(line.descent, fm.descent);
            if (run.placeholder) {  // inline content (Compose's PlaceholderSpan): as tall as its room needs, at least its text's
                auto [above, below] = placeholderExtent(run, measurer);
                line.ascent = std::max(line.ascent, above);
                line.descent = std::max(line.descent, below);
            }
            line.height = std::max({line.height, fm.lineHeight + std::abs(shift), line.ascent + line.descent});
        }
        if (lineHeight > 0 && line.height > 0) {
            // Compose's LineHeightStyleSpan: what the line gains or loses is shared between above
            // and below it as the alignment says (Proportional: as its ascent and descent stand;
            // the font's leading counts as descent); a trimmed first top or last bottom stays.
            float a = line.ascent, d = line.height - line.ascent;
            float height = lineStyle.mode == LineHeightStyle::Mode::Minimum ? std::max(lineHeight, line.height) : lineHeight;
            float diff = height - line.height, ratio = lineStyle.alignment < 0 ? a / line.height : lineStyle.alignment;
            float descent = d + diff * (diff <= 0 ? ratio : 1 - ratio);
            line.ascent = trimTop ? a : height - descent;
            line.height = line.ascent + (trimBottom ? d : descent);
        }
        if (line.visual && line.rtl) x += lead;
        line.width = x;
        if (line.rtl && !line.visual)  // from the right: the first piece is the rightmost
            for (auto& p : line.pieces) p.x = x - p.x - p.width;
        line.start = from;
        line.end = to;
        line.top = layout.height;
        layout.height += line.height;
        layout.width = std::max(layout.width, x);
        layout.lines.push_back(std::move(line));
    }
    return layout;
}

namespace {
float lineX(const TextLine& line, float width, TextAlign align) { return alignedX(line.align.value_or(align), line.rtl, width, line.width); }
} // namespace

std::vector<Rect> placeholderRects(const StyledTextLayout& layout, TextMeasurer& measurer, float width, TextAlign align) {
    std::vector<Rect> rects;
    for (const TextLine& line : layout.lines)
        for (const TextPiece& p : line.pieces) {
            const TextRun& run = layout.runs[size_t(p.run)];
            if (!run.placeholder) continue;
            auto [above, below] = placeholderExtent(run, measurer);
            if (int(rects.size()) <= run.inlineIndex) rects.resize(size_t(run.inlineIndex) + 1);
            rects[size_t(run.inlineIndex)] = {lineX(line, width, align) + p.x, line.top + line.ascent - above, p.width, above + below};
        }
    return rects;
}

void drawStyledText(GraphicsContext& canvas, const StyledTextLayout& layout, const Rect& b, TextAlign align,
                    TextOverflow overflow) {
    bool clip = overflow == TextOverflow::Clip && layout.clipped;
    if (clip) canvas.pushClip(b);
    for (const TextLine& line : layout.lines) {
        float x0 = b.x + lineX(line, b.width, align);
        float lineBaseline = b.y + line.top + line.ascent;  // pieces of different sizes share the baseline
        for (const TextPiece& p : line.pieces) {
            const TextRun& run = layout.runs[size_t(p.run)];
            if (run.placeholder) continue;  // inline content draws itself there
            float x = x0 + p.x;
            float baseline = lineBaseline - p.rise;
            if (run.background) canvas.fillRoundRect({x, b.y + line.top, p.width, line.height}, 0, *run.background);
            if (run.shadow && run.shadow->color.alpha() > 0)
                canvas.drawTextShadow(p.text, {x + run.shadow->offset.x, baseline + run.shadow->offset.y}, run.font, run.shadow->color,
                                      run.shadow->blurRadius, p.rtl);
            if (run.brush) canvas.drawTextBrush(p.text, {x, baseline}, run.font, *run.brush, b, p.rtl);  // spread over the text
            else canvas.drawText(p.text, {x, baseline}, run.font, run.color, p.rtl);
            Color ink = run.brush ? run.brush->colorAt({x + p.width / 2, baseline}, b) : run.color;
            float thickness = std::max(1.f, run.font.size / 14);
            if (run.decoration == TextDecoration::LineThrough)
                canvas.drawLine({x, baseline - line.ascent * 0.3f}, {x + p.width, baseline - line.ascent * 0.3f}, thickness, ink);
            else if (run.decoration == TextDecoration::Underline)
                canvas.drawLine({x, baseline + 2}, {x + p.width, baseline + 2}, thickness, ink);
        }
    }
    if (clip) canvas.popClip();
}

int runAt(const StyledTextLayout& layout, Offset p, float width, TextAlign align) {
    for (const TextLine& line : layout.lines) {
        if (p.y < line.top || p.y >= line.top + line.height) continue;
        float x0 = lineX(line, width, align);
        for (const TextPiece& piece : line.pieces)
            if (p.x >= x0 + piece.x && p.x < x0 + piece.x + piece.width) return piece.run;
    }
    return -1;
}

namespace {

// A line's pieces in source order (a visual line has them in screen order), each with where it
// ends in the source.
std::vector<std::pair<const TextPiece*, int>> sourceOrder(const TextLine& line) {
    std::vector<const TextPiece*> pieces;
    for (const TextPiece& p : line.pieces) pieces.push_back(&p);
    if (line.visual) std::stable_sort(pieces.begin(), pieces.end(), [](const TextPiece* a, const TextPiece* b) { return a->start < b->start; });
    std::vector<std::pair<const TextPiece*, int>> out;
    for (size_t i = 0; i < pieces.size(); ++i) out.push_back({pieces[i], i + 1 < pieces.size() ? pieces[i + 1]->start : line.end});
    return out;
}

// x of `offset` within `line` (from the line's start), measuring the source text piece by piece.
float xIn(const TextLine& line, const StyledTextLayout& layout, TextMeasurer& measurer, const std::string& text, int offset) {
    offset = std::clamp(offset, line.start, line.end);
    auto pieces = sourceOrder(line);
    for (size_t i = 0; i < pieces.size(); ++i) {
        auto [p, pieceEnd] = pieces[i];
        // After a word's space the caret goes with the next word (a justified line has a gap there).
        bool space = !p->text.empty() && p->text.back() == ' ';
        if ((offset > pieceEnd || (offset == pieceEnd && space)) && i + 1 < pieces.size()) continue;
        int to = std::clamp(offset, p->start, pieceEnd);
        (void)text;  // the piece's own text: an ellipsis it ends with moves right-to-left text over
        return p->x + caretX(measurer, p->text, layout.runs[size_t(p->run)].font, to - p->start, p->rtl);
    }
    return 0;
}

size_t lineFor(const StyledTextLayout& layout, int offset) {
    for (size_t i = 0; i < layout.lines.size(); ++i)
        if (offset <= layout.lines[i].end) return i;
    return layout.lines.empty() ? 0 : layout.lines.size() - 1;
}

} // namespace

int offsetAt(const StyledTextLayout& layout, TextMeasurer& measurer, const std::string& text, Offset p, float width,
             TextAlign align) {
    if (layout.lines.empty()) return 0;
    size_t li = 0;
    while (li + 1 < layout.lines.size() && p.y >= layout.lines[li].top + layout.lines[li].height) ++li;
    const TextLine& line = layout.lines[li];
    float x = p.x - lineX(line, width, align);
    // The nearest code point boundary.
    int best = line.start;
    float bestDistance = std::abs(xIn(line, layout, measurer, text, line.start) - x);
    for (size_t k = size_t(line.start); k < size_t(line.end);) {
        k = std::min(utf8::nextCluster(text, k), size_t(line.end));
        float d = std::abs(xIn(line, layout, measurer, text, int(k)) - x);
        if (d < bestDistance) {
            bestDistance = d;
            best = int(k);
        }
    }
    return best;
}

std::vector<Rect> selectionRects(const StyledTextLayout& layout, TextMeasurer& measurer, const std::string& text, int from, int to,
                                 float width, TextAlign align) {
    std::vector<Rect> rects;
    if (from >= to) return rects;
    (void)text;
    for (const TextLine& line : layout.lines) {
        if (to <= line.start || from > line.end) continue;
        float x0 = lineX(line, width, align);
        // Piece by piece, stretch by stretch: right-to-left text in a line makes a range several spans.
        for (auto [p, pieceEnd] : sourceOrder(line)) {
            int a = std::max(from, p->start), b = std::min(to, pieceEnd);
            if (a >= b) continue;
            for (auto [xa, xb] : rangeXs(measurer, p->text, layout.runs[size_t(p->run)].font, a - p->start, b - p->start, p->rtl))
                rects.push_back({x0 + p->x + xa, line.top, xb - xa, line.height});
        }
        if (to > line.end && &line != &layout.lines.back())  // the line break is selected too, at the line's end
            rects.push_back({line.rtl ? x0 - 4 : x0 + line.width, line.top, 4, line.height});
    }
    return rects;
}

Offset caretPoint(const StyledTextLayout& layout, TextMeasurer& measurer, const std::string& text, int offset, float width,
                  TextAlign align) {
    if (layout.lines.empty()) return {};
    const TextLine& line = layout.lines[lineFor(layout, offset)];
    return {lineX(line, width, align) + xIn(line, layout, measurer, text, offset), line.top + line.height};
}

} // namespace detail
} // namespace kompoz
