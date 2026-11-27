#include "richtext_model.hpp"

#include <kompoz/host.hpp>
#include <kompoz/state.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace kompoz {

namespace detail {

// ---- styles' fields, one by one ----------------------------------------------------------------

namespace {

// Each optional field of a SpanStyle but its decoration (combined lines: handled on its own).
template <class F>
void eachSpanField(F&& f) {
    f(&SpanStyle::fontSize);
    f(&SpanStyle::fontWeight);
    f(&SpanStyle::fontStyle);
    f(&SpanStyle::fontFamily);
    f(&SpanStyle::color);
    f(&SpanStyle::background);
    f(&SpanStyle::letterSpacing);
    f(&SpanStyle::fontFeatureSettings);
    f(&SpanStyle::brush);
    f(&SpanStyle::shadow);
    f(&SpanStyle::baselineShift);
    f(&SpanStyle::drawStyle);
    f(&SpanStyle::textGeometricTransform);
}
template <class F>
void eachParagraphField(F&& f) {
    f(&ParagraphStyle::textAlign);
    f(&ParagraphStyle::textDirection);
    f(&ParagraphStyle::lineHeight);
    f(&ParagraphStyle::lineHeightStyle);
    f(&ParagraphStyle::textIndent);
    f(&ParagraphStyle::lineBreak);
}

} // namespace

bool spanHas(const SpanStyle& on, const SpanStyle& style) {
    bool all = true;
    eachSpanField([&](auto field) {
        if ((style.*field) && on.*field != style.*field) all = false;
    });
    if (style.textDecoration && *style.textDecoration != TextDecoration::None &&
        !contains(on.textDecoration.value_or(TextDecoration::None), *style.textDecoration))
        all = false;
    return all;
}

void spanAdd(SpanStyle& on, const SpanStyle& style) {
    eachSpanField([&](auto field) {
        if (style.*field) on.*field = style.*field;
    });
    if (style.textDecoration) {
        TextDecoration d = on.textDecoration.value_or(TextDecoration::None) | *style.textDecoration;
        on.textDecoration = d == TextDecoration::None ? std::nullopt : std::optional(d);
    }
}

void spanRemove(SpanStyle& on, const SpanStyle& style) {
    eachSpanField([&](auto field) {
        if ((style.*field) && on.*field == style.*field) (on.*field).reset();
    });
    if (style.textDecoration && on.textDecoration) {
        TextDecoration d = minus(*on.textDecoration, *style.textDecoration);
        on.textDecoration = d == TextDecoration::None ? std::nullopt : std::optional(d);
    }
}

SpanStyle spanCommon(const SpanStyle& a, const SpanStyle& b) {
    SpanStyle out;
    eachSpanField([&](auto field) {
        if (a.*field == b.*field) out.*field = a.*field;
    });
    TextDecoration d = TextDecoration(int(a.textDecoration.value_or(TextDecoration::None)) & int(b.textDecoration.value_or(TextDecoration::None)));
    if (d != TextDecoration::None) out.textDecoration = d;
    return out;
}

bool paragraphHas(const ParagraphStyle& on, const ParagraphStyle& style) {
    bool all = true;
    eachParagraphField([&](auto field) {
        if ((style.*field) && on.*field != style.*field) all = false;
    });
    return all;
}
void paragraphAdd(ParagraphStyle& on, const ParagraphStyle& style) {
    eachParagraphField([&](auto field) {
        if (style.*field) on.*field = style.*field;
    });
}
void paragraphRemove(ParagraphStyle& on, const ParagraphStyle& style) {
    eachParagraphField([&](auto field) {
        if ((style.*field) && on.*field == style.*field) (on.*field).reset();
    });
}
ParagraphStyle paragraphCommon(const ParagraphStyle& a, const ParagraphStyle& b) {
    ParagraphStyle out;
    eachParagraphField([&](auto field) {
        if (a.*field == b.*field) out.*field = a.*field;
    });
    return out;
}

// ---- the model: text, runs over it, its paragraphs ----------------------------------------------

std::vector<int> RichModel::paragraphStarts() const {
    std::vector<int> starts{0};
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n') starts.push_back(int(i) + 1);
    return starts;
}

int RichModel::paragraphAt(int offset) const {
    int index = 0;
    for (int i = 0; i < std::min(offset, int(text.size())); ++i)
        if (text[size_t(i)] == '\n') ++index;
    return index;
}

std::pair<int, int> RichModel::paragraphRange(int index) const {
    std::vector<int> starts = paragraphStarts();
    int start = starts[size_t(std::clamp(index, 0, int(starts.size()) - 1))];
    size_t end = text.find('\n', size_t(start));
    return {start, end == std::string::npos ? int(text.size()) : int(end)};
}

const RichRun* RichModel::runAt(int offset) const {
    for (const RichRun& r : runs)
        if (offset >= r.start && offset < r.end) return &r;
    return nullptr;
}

void RichModel::splitAt(int at) {
    for (size_t i = 0; i < runs.size(); ++i) {
        RichRun& r = runs[i];
        if (at > r.start && at < r.end) {
            RichRun tail = r;
            tail.start = at;
            r.end = at;
            runs.insert(runs.begin() + long(i) + 1, tail);
            return;
        }
    }
}

void RichModel::normalize() {
    std::vector<RichRun> out;
    for (RichRun& r : runs) {
        if (r.end <= r.start) continue;
        if (!out.empty() && out.back().end == r.start && out.back().sameStyle(r)) out.back().end = r.end;
        else out.push_back(std::move(r));
    }
    runs = std::move(out);
    // (the paragraphs: one per line of the text)
    size_t count = size_t(std::count(text.begin(), text.end(), '\n')) + 1;
    if (paragraphs.size() < count) paragraphs.resize(count);
    if (paragraphs.size() > count) paragraphs.resize(count);
}

// What text typed at `at` takes: the style of the character before it in its paragraph (or after
// it, at a paragraph's start); a link or code span only inside one, not at its ends.
RichRun RichModel::insertionStyle(int at) const {
    auto lineChar = [&](int o) { return o >= 0 && o < int(text.size()) && text[size_t(o)] != '\n'; };
    const RichRun* before = lineChar(at - 1) ? runAt(at - 1) : nullptr;
    const RichRun* after = lineChar(at) ? runAt(at) : nullptr;
    const RichRun* base = before ? before : after ? after : runAt(at - 1);
    RichRun style = base ? *base : RichRun{};
    bool inside = before && after;
    if (!inside || before->link != after->link) style.link.clear();
    if (!inside || !before->code || !after->code) style.code = false;
    return style;
}

void RichModel::insert(int at, std::string_view s, const RichRun& style) {
    at = std::clamp(at, 0, int(text.size()));
    if (s.empty()) return;
    // The paragraphs: each new line splits the one it is typed in; the new ones go on as it was (a
    // list's item), but after a heading's end, a paragraph of body text.
    int index = paragraphAt(at);
    auto [ps, pe] = paragraphRange(index);
    int lines = int(std::count(s.begin(), s.end(), '\n'));
    if (lines > 0) {
        RichParagraph next = paragraphs[size_t(index)];
        if (at == pe && next.heading != HeadingStyle::Normal) next.heading = HeadingStyle::Normal;
        paragraphs.insert(paragraphs.begin() + index + 1, size_t(lines), next);
        if (at == ps && pe > ps) {  // (a new line before a paragraph's text: the empty one before it is plain)
            paragraphs[size_t(index)] = RichParagraph{};
            paragraphs[size_t(index)].style = paragraphs[size_t(index) + 1].style;
        }
    }
    text.insert(size_t(at), s);
    int n = int(s.size());
    splitAt(at);
    for (RichRun& r : runs) {
        if (r.start >= at) r.start += n, r.end += n;
    }
    RichRun added = style;
    added.start = at, added.end = at + n;
    auto where = std::find_if(runs.begin(), runs.end(), [&](const RichRun& r) { return r.start >= at + n; });
    runs.insert(where, added);
    normalize();
}

void RichModel::erase(int from, int to) {
    from = std::clamp(from, 0, int(text.size()));
    to = std::clamp(to, from, int(text.size()));
    if (from == to) return;
    int first = paragraphAt(from);
    int lines = int(std::count(text.begin() + from, text.begin() + to, '\n'));
    // (the paragraphs it joins: the first keeps its own, unless it lost all its text and the next
    // one goes on)
    if (lines > 0) {
        auto [ps, pe] = paragraphRange(first);
        bool emptied = from == ps;
        RichParagraph kept = emptied ? paragraphs[size_t(first + lines)] : paragraphs[size_t(first)];
        paragraphs.erase(paragraphs.begin() + first + 1, paragraphs.begin() + first + 1 + lines);
        paragraphs[size_t(first)] = kept;
    }
    text.erase(size_t(from), size_t(to - from));
    int n = to - from;
    std::vector<RichRun> out;
    for (RichRun r : runs) {
        if (r.end <= from) {
        } else if (r.start >= to) {
            r.start -= n, r.end -= n;
        } else {
            int s = std::min(r.start, from), e = r.end >= to ? r.end - n : from;
            if (r.start >= from) s = from;
            r.start = s, r.end = std::max(s, e);
        }
        out.push_back(r);
    }
    runs = std::move(out);
    normalize();
}

void RichModel::replace(int from, int to, std::string_view s, const RichRun& style) {
    erase(from, to);
    insert(from, s, style);
}

void RichModel::styleRange(int from, int to, const std::function<void(RichRun&)>& change) {
    if (from >= to) return;
    splitAt(from);
    splitAt(to);
    for (RichRun& r : runs)
        if (r.start >= from && r.end <= to) change(r);
    normalize();
}

RichModel RichModel::slice(int from, int to) const {
    RichModel out;
    from = std::clamp(from, 0, int(text.size()));
    to = std::clamp(to, from, int(text.size()));
    out.text = text.substr(size_t(from), size_t(to - from));
    for (const RichRun& r : runs) {
        int s = std::max(r.start, from), e = std::min(r.end, to);
        if (s >= e) continue;
        RichRun c = r;
        c.start = s - from, c.end = e - from;
        out.runs.push_back(c);
    }
    int a = paragraphAt(from), b = paragraphAt(to);
    out.paragraphs.assign(paragraphs.begin() + a, paragraphs.begin() + b + 1);
    out.normalize();
    return out;
}

void RichModel::insertModel(int at, const RichModel& other) {
    at = std::clamp(at, 0, int(text.size()));
    if (other.text.empty()) return;
    int index = paragraphAt(at);
    RichModel copy = *this;
    insert(at, other.text, RichRun{});
    // Its runs in place of the plain ones; its paragraphs over those it made (the first joins the
    // one it went into: its list or heading only if that one had none).
    styleRange(at, at + int(other.text.size()), [](RichRun& r) { r = RichRun{r.start, r.end}; });
    for (const RichRun& r : other.runs) {
        RichRun c = r;
        c.start += at, c.end += at;
        styleRange(c.start, c.end, [&](RichRun& x) {
            int s = x.start, e = x.end;
            x = c;
            x.start = s, x.end = e;
        });
    }
    for (size_t i = 0; i < other.paragraphs.size(); ++i) {
        RichParagraph& p = paragraphs[size_t(index) + i];
        if (i == 0 && (copy.paragraphRange(index).second > copy.paragraphRange(index).first) && p.list != ListKind::None) continue;
        p = other.paragraphs[i];
    }
    normalize();
}

// ---- how it is shown -------------------------------------------------------------------------

namespace {

std::string toLetters(int n) {  // 1 a, 26 z, 27 aa
    std::string s;
    while (n > 0) {
        --n;
        s.insert(s.begin(), char('a' + n % 26));
        n /= 26;
    }
    return s;
}
std::string toRoman(int n) {
    static const std::pair<int, const char*> numerals[] = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"},
                                                           {50, "l"},   {40, "xl"},  {10, "x"},  {9, "ix"},   {5, "v"},   {4, "iv"}, {1, "i"}};
    std::string s;
    for (auto [v, r] : numerals)
        while (n >= v) s += r, n -= v;
    return s;
}

} // namespace

std::vector<std::string> listMarkers(const std::vector<RichParagraph>& paragraphs) {
    std::vector<std::string> out(paragraphs.size());
    std::vector<int> counters(8, 0);
    for (size_t i = 0; i < paragraphs.size(); ++i) {
        const RichParagraph& p = paragraphs[i];
        if (p.list == ListKind::None) {
            std::fill(counters.begin(), counters.end(), 0);
            continue;
        }
        int level = std::clamp(p.level, 1, 7);
        for (size_t d = size_t(level) + 1; d < counters.size(); ++d) counters[d] = 0;
        if (p.list == ListKind::Unordered) {
            static const char* bullets[] = {"•", "◦", "▪"};
            counters[size_t(level)] = 0;
            out[i] = std::string(bullets[(level - 1) % 3]) + " ";
        } else {
            int n = ++counters[size_t(level)];
            std::string number = (level - 1) % 3 == 0 ? std::to_string(n) : (level - 1) % 3 == 1 ? toLetters(n) : toRoman(n);
            out[i] = number + ". ";
        }
    }
    return out;
}

Font headingFont(HeadingStyle heading, const Typography& t) {
    Font f;
    switch (heading) {
    case HeadingStyle::H1: f = t.headlineLarge; break;
    case HeadingStyle::H2: f = t.headlineMedium; break;
    case HeadingStyle::H3: f = t.headlineSmall; break;
    case HeadingStyle::H4: f = t.titleLarge; break;
    case HeadingStyle::H5: f = t.titleMedium; break;
    case HeadingStyle::H6: f = t.titleSmall; break;
    case HeadingStyle::Normal: break;
    }
    f.weight = FontWeight::Bold;
    return f;
}

ShownRichText showRichText(const RichModel& m, const RichConfigValues& config, const RichShowEnv& env) {
    ShownRichText out;
    std::vector<std::string> markers = listMarkers(m.paragraphs);
    std::vector<int> starts = m.paragraphStarts();
    AnnotatedString::Builder b;
    Color linkColor = config.linkColor.isSpecified() ? config.linkColor : env.primary;
    auto spanOf = [&](const RichRun& r) {
        SpanStyle s = r.style;
        if (r.code) {
            SpanStyle code{.fontFamily = FontFamily::Monospace,
                           .background = config.codeSpanBackgroundColor.isSpecified() ? config.codeSpanBackgroundColor : env.codeBackground};
            if (config.codeSpanColor.isSpecified()) code.color = config.codeSpanColor;
            SpanStyle merged = code;
            spanAdd(merged, s);
            s = merged;
        }
        if (!r.link.empty() && !env.links) {  // (Text's links get their style from the link itself)
            s.color = s.color.value_or(linkColor);
            s.textDecoration = s.textDecoration.value_or(TextDecoration::None) | config.linkTextDecoration;
        }
        return s;
    };
    for (size_t i = 0; i < m.paragraphs.size() && i < starts.size(); ++i) {
        const RichParagraph& p = m.paragraphs[i];
        int ps = starts[i];
        int pe = i + 1 < starts.size() ? starts[i + 1] - 1 : int(m.text.size());
        out.paragraphStarts.push_back(ps);
        out.markerLengths.push_back(int(markers[i].size()));
        ParagraphStyle ps_ = p.style;
        Font heading = headingFont(p.heading, env.typography);
        if (p.heading != HeadingStyle::Normal && !ps_.lineHeight && heading.lineHeight > 0) ps_.lineHeight = TextUnit::sp(heading.lineHeight);
        if (p.list != ListKind::None) {
            int indent = p.list == ListKind::Ordered ? config.orderedListIndent : config.unorderedListIndent;
            float start = float(indent) * float(std::max(0, p.level - 1));
            float marker = env.measure ? env.measure(markers[i]) : float(markers[i].size()) * env.fontSize * 0.5f;
            if (!ps_.textIndent) ps_.textIndent = TextIndent(TextUnit::sp(start), TextUnit::sp(start + marker));
        }
        bool last = i + 1 == m.paragraphs.size() || i + 1 >= starts.size();
        b.withStyle(ps_, [&] {
            if (p.heading != HeadingStyle::Normal)
                b.pushStyle(SpanStyle{.fontSize = TextUnit::sp(heading.size), .fontWeight = heading.weight});
            if (!markers[i].empty()) {
                SpanStyle ms = config.listMarkerStyle;
                b.withStyle(ms, [&] { b.append(markers[i]); });
            }
            for (const RichRun& r : m.runs) {
                int s = std::max(r.start, ps), e = std::min(r.end, pe);
                if (s >= e) continue;
                std::string piece = m.text.substr(size_t(s), size_t(e - s));
                SpanStyle style = spanOf(r);
                if (!r.link.empty() && env.links) {
                    b.withLink(LinkAnnotation::Url(r.link, TextLinkStyles{.style = SpanStyle{.color = linkColor,
                                                                                                .textDecoration = config.linkTextDecoration}}),
                               [&] { b.withStyle(style, [&] { b.append(piece); }); });
                } else {
                    b.withStyle(style, [&] { b.append(piece); });
                }
            }
            if (p.heading != HeadingStyle::Normal) b.pop();
            if (!last) b.append("\n");
        });
    }
    out.text = b.toAnnotatedString();
    return out;
}

int ShownRichText::toShown(int offset) const {
    int extra = 0;
    for (size_t i = 0; i < paragraphStarts.size(); ++i) {
        if (paragraphStarts[i] > offset) break;
        extra += markerLengths[i];
    }
    return offset + extra;
}

int ShownRichText::toValue(int shown) const {
    int extra = 0;
    for (size_t i = 0; i < paragraphStarts.size(); ++i) {
        int start = paragraphStarts[i] + extra;  // where its marker begins in what is shown
        int next = i + 1 < paragraphStarts.size() ? paragraphStarts[i + 1] + extra + markerLengths[i] : std::numeric_limits<int>::max();
        if (shown < next || i + 1 == paragraphStarts.size()) {
            if (shown < start + markerLengths[i]) return paragraphStarts[i];  // on the marker: the paragraph's start
            return shown - extra - markerLengths[i];
        }
        extra += markerLengths[i];
    }
    return shown;
}

} // namespace detail

// ---- the state ---------------------------------------------------------------------------------

using detail::ListKind;
using detail::RichModel;
using detail::RichRun;
using detail::RichTextData;

namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The theme's where there is a composition, else Material's defaults.
detail::RichShowEnv showEnv(bool links, const Font& font) {
    detail::RichShowEnv env;
    env.links = links;
    env.fontSize = font.size;
    detail::Kompozer* k = detail::Kompozer::current();
    bool composing = k && k->currentScope();
    if (composing) {
        env.typography = typography();
        const ColorScheme& cs = colorScheme();
        env.primary = cs.primary;
        env.codeBackground = cs.onSurface.copy(0.1f);  // (seen on any surface: the text's color, faint)
        TextMeasurer* tm = &k->host().textMeasurer();
        float scale = std::max(0.01f, tm->fontScale());
        env.measure = [tm, font, scale](const std::string& s) { return tm->measureWidth(s, font) / scale; };
    } else {
        ColorScheme cs = lightColorScheme();
        env.primary = cs.primary;
        env.codeBackground = cs.onSurface.copy(0.1f);
    }
    return env;
}

} // namespace

namespace detail {

void RichTextData::changed() { version.set(version.peek() + 1); }

void RichTextData::record(bool typing) {
    if (restoring) return;
    int64_t now = nowMs();
    if (grouping > 0) {
        if (groupRecorded) return;
        groupRecorded = true;
    } else if (typing && lastWasTyping && now - lastEditAt <= coalesceWindowMs && !undoStack.empty()) {
        lastEditAt = now;
        return;
    }
    undoStack.push_back({model, field.selection()});
    if (int(undoStack.size()) > historyLimit) undoStack.erase(undoStack.begin());
    redoStack.clear();
    lastWasTyping = typing;
    lastEditAt = now;
    historyVersion.set(historyVersion.peek() + 1);
}

void RichTextData::syncModel() {
    std::string text = field.text();
    if (model.text == text) return;
    model = RichModel{};
    model.text = text;
    if (!text.empty()) model.runs.push_back({0, int(text.size())});
    model.normalize();
}

// The field's text and selection set to the model's (an edit in code: not through its input
// transformation).
void RichTextData::pushToField(TextRange selection) {
    std::string text = model.text;
    int n = int(text.size());
    selection = {std::clamp(selection.start, 0, n), std::clamp(selection.end, 0, n)};
    field.edit([&](TextFieldBuffer& b) {
        if (b.text() != text) b.replace(0, b.length(), text);
        b.setSelection(selection);
    });
    typing.reset();
    changed();
}

// What the user typed, pasted or deleted (the field's input transformation): the runs and paragraphs
// follow, with what Enter and Backspace do in lists and the typing shortcuts.
void RichTextData::userEdit(TextFieldBuffer& buffer) {
    const std::string& before = buffer.originalText();
    const std::string& after = buffer.text();
    if (model.text != before) {  // (changed elsewhere: plain from here)
        model = RichModel{};
        model.text = before;
        if (!before.empty()) model.runs.push_back({0, int(before.size())});
        model.normalize();
    }
    if (before == after) return;
    // The edit: what was taken out and put in between the common start and end (whole characters).
    size_t prefix = 0, max = std::min(before.size(), after.size());
    while (prefix < max && before[prefix] == after[prefix]) ++prefix;
    while (prefix > 0 && (static_cast<unsigned char>(before[prefix]) & 0xC0) == 0x80) --prefix;
    size_t suffix = 0;
    while (suffix < max - prefix && before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix]) ++suffix;
    while (suffix > 0 && (static_cast<unsigned char>(before[before.size() - suffix]) & 0xC0) == 0x80) --suffix;
    int from = int(prefix), removedEnd = int(before.size() - suffix);
    std::string inserted = after.substr(prefix, after.size() - suffix - prefix);
    TextRange old = buffer.originalSelection();

    // Enter on an empty list item: out of the list (a level back first).
    if (inserted == "\n" && removedEnd == from && config.exitListOnEmptyItem) {
        int index = model.paragraphAt(from);
        auto [ps, pe] = model.paragraphRange(index);
        RichParagraph& p = model.paragraphs[size_t(index)];
        if (p.list != ListKind::None && ps == pe) {
            record(false);
            if (p.level > 1) --p.level;
            else p.list = ListKind::None, p.level = 1;
            buffer.revertAllChanges();
            changed();
            return;
        }
    }
    // Backspace at a list item's start: out of the list.
    if (inserted.empty() && removedEnd == from + 1 && before[size_t(from)] == '\n' && old.collapsed() && old.start == from + 1) {
        int index = model.paragraphAt(from + 1);
        RichParagraph& p = model.paragraphs[size_t(index)];
        if (p.list != ListKind::None) {
            record(false);
            p.list = ListKind::None, p.level = 1;
            buffer.revertAllChanges();
            changed();
            return;
        }
    }
    record(true);
    RichRun style = typing && typingAt == from && removedEnd == from ? *typing : model.insertionStyle(from);
    if (removedEnd > from) {
        RichRun at = model.insertionStyle(from + 1 <= removedEnd ? from + 1 : from);
        if (const RichRun* r = model.runAt(from)) at = *r;  // (what replaces a selection takes its first character's style)
        at.link.clear();
        if (!typing || typingAt != from) style = at;
        if (const RichRun* r = model.runAt(from); r && !r->link.empty() && model.runAt(removedEnd - 1) &&
                                                  model.runAt(removedEnd - 1)->link == r->link && removedEnd - from < r->end - r->start)
            style.link = r->link;  // (a link's text changed inside it: still the link)
    }
    model.replace(from, removedEnd, inserted, style);
    if (!inserted.empty()) typing.reset();
    int caret = from + int(inserted.size());
    bool rewritten = false;
    // Typing shortcuts at a paragraph's start: "- ", "* ", "1. " lists, "# " headings.
    if (inserted == " ") {
        int index = model.paragraphAt(from);
        auto [ps, pe] = model.paragraphRange(index);
        std::string_view head = std::string_view(model.text).substr(size_t(ps), size_t(caret - ps));
        RichParagraph& p = model.paragraphs[size_t(index)];
        std::optional<std::pair<ListKind, HeadingStyle>> made;
        if (config.listTypingShortcutsEnabled && p.list == ListKind::None) {
            if (head == "- " || head == "* " || head == "+ ") made = std::pair{ListKind::Unordered, HeadingStyle::Normal};
            else if (head.size() >= 3 && head.ends_with(". ") &&
                     std::all_of(head.begin(), head.end() - 2, [](char c) { return c >= '0' && c <= '9'; }))
                made = std::pair{ListKind::Ordered, HeadingStyle::Normal};
        }
        if (config.headingTypingShortcutsEnabled && !made && head.size() >= 2 && head.size() <= 7 && head.back() == ' ' &&
            std::all_of(head.begin(), head.end() - 1, [](char c) { return c == '#'; }))
            made = std::pair{ListKind::None, HeadingStyle(int(head.size()) - 1)};
        if (made) {
            model.erase(ps, caret);
            if (made->first != ListKind::None) p = model.paragraphs[size_t(index)], model.paragraphs[size_t(index)].list = made->first,
                                               model.paragraphs[size_t(index)].level = 1;
            else model.paragraphs[size_t(index)].heading = made->second;
            caret = ps;
            rewritten = true;
        }
    }
    // Inline shortcuts: the closing **, *, _, `, ~~ makes what it closes bold, italic, code, struck.
    if (config.inlineTypingShortcutsEnabled && inserted.size() == 1 && std::string_view("*_`~").find(inserted[0]) != std::string_view::npos) {
        int index = model.paragraphAt(from);
        auto [ps, pe] = model.paragraphRange(index);
        std::string_view line = std::string_view(model.text).substr(size_t(ps), size_t(caret - ps));
        struct Mark {
            std::string_view delimiter;
            std::function<void(RichRun&)> apply;
        };
        const Mark marks[] = {
            {"**", [](RichRun& r) { r.style.fontWeight = FontWeight::Bold; }},
            {"__", [](RichRun& r) { r.style.fontWeight = FontWeight::Bold; }},
            {"~~", [](RichRun& r) { r.style.textDecoration = r.style.textDecoration.value_or(TextDecoration::None) | TextDecoration::LineThrough; }},
            {"*", [](RichRun& r) { r.style.fontStyle = FontStyle::Italic; }},
            {"_", [](RichRun& r) { r.style.fontStyle = FontStyle::Italic; }},
            {"`", [](RichRun& r) { r.code = true; }},
        };
        for (const Mark& mark : marks) {
            size_t d = mark.delimiter.size();
            if (line.size() < 2 * d + 1 || !line.ends_with(mark.delimiter)) continue;
            size_t open = line.rfind(mark.delimiter, line.size() - d - 1);
            if (open == std::string_view::npos || open + d >= line.size() - d) continue;
            if (d == 1 && open > 0 && line[open - 1] == mark.delimiter[0]) continue;  // (one of a ** pair)
            if (line[open + d] == ' ') continue;
            int a = ps + int(open), e = ps + int(line.size());
            model.erase(e - int(d), e);
            model.erase(a, a + int(d));
            model.styleRange(a, e - 2 * int(d), mark.apply);
            caret = e - 2 * int(d);
            typing = model.insertionStyle(caret);
            if (mark.delimiter == "`") typing->code = false;
            typingAt = caret;
            rewritten = true;
            break;
        }
    }
    if (rewritten) {
        buffer.replace(0, buffer.length(), model.text);
        buffer.setSelection({caret, caret});
        if (typing) typingAt = caret;
    }
    changed();
}

TextRange RichTextData::selection() const { return field.selection(); }

} // namespace detail

namespace {

using Data = detail::RichTextData;

// The selection's runs' range, and its paragraphs.
std::pair<int, int> ordered(TextRange r) { return {std::min(r.start, r.end), std::max(r.start, r.end)}; }

// A change to styles (not text): one history step, shown again.
void styleChange(Data& d, const std::function<void()>& change) {
    d.syncModel();
    d.record(false);
    change();
    d.changed();
}

void eachParagraph(Data& d, const std::function<void(detail::RichParagraph&, int index)>& f) {
    auto [a, e] = ordered(d.selection());
    int first = d.model.paragraphAt(a), last = d.model.paragraphAt(e);
    for (int i = first; i <= last && i < int(d.model.paragraphs.size()); ++i) f(d.model.paragraphs[size_t(i)], i);
}

// The runs a range covers (with none: the style typing there gets).
template <class F>
void eachRunIn(const Data& d, TextRange range, F&& f) {
    auto [a, e] = ordered(range);
    if (a == e) {
        f(d.typing && d.typingAt == a ? *d.typing : d.model.insertionStyle(a));
        return;
    }
    for (const RichRun& r : d.model.runs)
        if (r.end > a && r.start < e) f(r);
}

std::optional<std::pair<int, int>> linkAround(const Data& d, TextRange selection) {
    auto [a, e] = ordered(selection);
    const RichRun* r = d.model.runAt(a);
    if ((!r || r->link.empty()) && a > 0) r = d.model.runAt(a - 1);
    if (!r || r->link.empty()) return std::nullopt;
    // The whole link: the runs around with its URL.
    int s = r->start, t = r->end;
    for (bool grew = true; grew;) {
        grew = false;
        for (const RichRun& x : d.model.runs) {
            if (x.link != r->link) continue;
            if (x.end == s) s = x.start, grew = true;
            if (x.start == t) t = x.end, grew = true;
        }
    }
    if (e > t) return std::nullopt;
    return std::pair{s, t};
}

} // namespace

// ---- RichTextConfig ----

#define KOMPOZ_RICH_CONFIG_IMPL(Type, name, Setter)                       \
    Type RichTextConfig::name() const {                                   \
        (void)data_->version.get();                                       \
        return data_->config.name;                                        \
    }                                                                     \
    void RichTextConfig::Setter(Type value) const {                       \
        data_->config.name = value;                                       \
        data_->changed();                                                 \
    }
KOMPOZ_RICH_CONFIG_IMPL(Color, linkColor, setLinkColor)
KOMPOZ_RICH_CONFIG_IMPL(TextDecoration, linkTextDecoration, setLinkTextDecoration)
KOMPOZ_RICH_CONFIG_IMPL(Color, codeSpanColor, setCodeSpanColor)
KOMPOZ_RICH_CONFIG_IMPL(Color, codeSpanBackgroundColor, setCodeSpanBackgroundColor)
KOMPOZ_RICH_CONFIG_IMPL(int, orderedListIndent, setOrderedListIndent)
KOMPOZ_RICH_CONFIG_IMPL(int, unorderedListIndent, setUnorderedListIndent)
KOMPOZ_RICH_CONFIG_IMPL(bool, exitListOnEmptyItem, setExitListOnEmptyItem)
KOMPOZ_RICH_CONFIG_IMPL(bool, listTypingShortcutsEnabled, setListTypingShortcutsEnabled)
KOMPOZ_RICH_CONFIG_IMPL(bool, headingTypingShortcutsEnabled, setHeadingTypingShortcutsEnabled)
KOMPOZ_RICH_CONFIG_IMPL(bool, inlineTypingShortcutsEnabled, setInlineTypingShortcutsEnabled)
KOMPOZ_RICH_CONFIG_IMPL(SpanStyle, listMarkerStyle, setListMarkerStyle)
#undef KOMPOZ_RICH_CONFIG_IMPL
int RichTextConfig::listIndent() const {
    (void)data_->version.get();
    return data_->config.orderedListIndent;
}
void RichTextConfig::setListIndent(int value) const {  // both lists'
    data_->config.orderedListIndent = data_->config.unorderedListIndent = value;
    data_->changed();
}

// ---- RichTextHistory ----

bool RichTextHistory::canUndo() const {
    (void)data_->historyVersion.get();
    return !data_->undoStack.empty();
}
bool RichTextHistory::canRedo() const {
    (void)data_->historyVersion.get();
    return !data_->redoStack.empty();
}
void RichTextHistory::undo() const {
    Data& d = *data_;
    if (d.undoStack.empty()) return;
    d.syncModel();
    d.redoStack.push_back({d.model, d.field.selection()});
    auto step = std::move(d.undoStack.back());
    d.undoStack.pop_back();
    d.restoring = true;
    d.model = step.model;
    d.pushToField(step.selection);
    d.restoring = false;
    d.lastWasTyping = false;
    d.historyVersion.set(d.historyVersion.peek() + 1);
}
void RichTextHistory::redo() const {
    Data& d = *data_;
    if (d.redoStack.empty()) return;
    d.syncModel();
    d.undoStack.push_back({d.model, d.field.selection()});
    auto step = std::move(d.redoStack.back());
    d.redoStack.pop_back();
    d.restoring = true;
    d.model = step.model;
    d.pushToField(step.selection);
    d.restoring = false;
    d.lastWasTyping = false;
    d.historyVersion.set(d.historyVersion.peek() + 1);
}
void RichTextHistory::clear() const {
    data_->undoStack.clear();
    data_->redoStack.clear();
    data_->historyVersion.set(data_->historyVersion.peek() + 1);
}
int RichTextHistory::limit() const { return data_->historyLimit; }
void RichTextHistory::setLimit(int limit) const {
    data_->historyLimit = std::max(1, limit);
    while (int(data_->undoStack.size()) > data_->historyLimit) data_->undoStack.erase(data_->undoStack.begin());
}
void RichTextHistory::group(const std::function<void()>& changes) const {
    Data& d = *data_;
    if (d.grouping++ == 0) d.groupRecorded = false;
    changes();
    if (--d.grouping == 0) d.lastWasTyping = false;
}

// ---- RichTextState ----

RichTextState::RichTextState(int historyLimit, int64_t coalesceWindowMs) : data_(std::make_shared<Data>()) {
    data_->historyLimit = std::max(1, historyLimit);
    data_->coalesceWindowMs = coalesceWindowMs;
}

const TextFieldState& RichTextState::textFieldState() const { return data_->field; }

AnnotatedString RichTextState::annotatedString() const {
    Data& d = *data_;
    (void)d.version.get();
    d.syncModel();
    Font font;
    if (detail::Kompozer* k = detail::Kompozer::current(); k && k->currentScope()) font = typography().bodyLarge;
    return detail::showRichText(d.model, d.config, showEnv(true, font)).text;
}

TextRange RichTextState::selection() const { return data_->field.selection(); }
void RichTextState::setSelection(TextRange selection) const {
    data_->syncModel();
    data_->field.edit([&](TextFieldBuffer& b) { b.setSelection(selection); });
}

SpanStyle RichTextState::getSpanStyle(TextRange range) const {
    Data& d = *data_;
    (void)d.version.get();
    d.syncModel();
    std::optional<SpanStyle> common;
    eachRunIn(d, range, [&](const RichRun& r) { common = common ? detail::spanCommon(*common, r.style) : r.style; });
    return common.value_or(SpanStyle{});
}
SpanStyle RichTextState::currentSpanStyle() const { return getSpanStyle(selection()); }

ParagraphStyle RichTextState::getParagraphStyle(TextRange range) const {
    Data& d = *data_;
    (void)d.version.get();
    d.syncModel();
    auto [a, e] = ordered(range);
    std::optional<ParagraphStyle> common;
    for (int i = d.model.paragraphAt(a); i <= d.model.paragraphAt(e) && i < int(d.model.paragraphs.size()); ++i) {
        const ParagraphStyle& s = d.model.paragraphs[size_t(i)].style;
        common = common ? detail::paragraphCommon(*common, s) : s;
    }
    return common.value_or(ParagraphStyle{});
}
ParagraphStyle RichTextState::currentParagraphStyle() const { return getParagraphStyle(selection()); }

HeadingStyle RichTextState::currentHeadingStyle() const {
    Data& d = *data_;
    (void)d.version.get();
    d.syncModel();
    auto [a, e] = ordered(d.selection());
    int i = d.model.paragraphAt(a);
    return i < int(d.model.paragraphs.size()) ? d.model.paragraphs[size_t(i)].heading : HeadingStyle::Normal;
}

bool RichTextState::isLink() const {
    (void)data_->version.get();
    data_->syncModel();
    return linkAround(*data_, selection()).has_value();
}
std::optional<std::string> RichTextState::selectedLinkText() const {
    (void)data_->version.get();
    data_->syncModel();
    auto around = linkAround(*data_, selection());
    if (!around) return std::nullopt;
    return data_->model.text.substr(size_t(around->first), size_t(around->second - around->first));
}
std::optional<std::string> RichTextState::selectedLinkUrl() const {
    (void)data_->version.get();
    data_->syncModel();
    auto around = linkAround(*data_, selection());
    if (!around) return std::nullopt;
    return data_->model.runAt(around->first)->link;
}

bool RichTextState::isCodeSpan() const {
    Data& d = *data_;
    (void)d.version.get();
    d.syncModel();
    bool all = true, any = false;
    eachRunIn(d, selection(), [&](const RichRun& r) { any = true, all = all && r.code; });
    return any && all;
}

namespace {
bool allParagraphs(const RichTextState& s, ListKind kind) {
    Data& d = *s.data_;
    (void)d.version.get();
    d.syncModel();
    bool all = true;
    eachParagraph(d, [&](detail::RichParagraph& p, int) { all = all && p.list == kind; });
    return all;
}
} // namespace
bool RichTextState::isOrderedList() const { return allParagraphs(*this, ListKind::Ordered); }
bool RichTextState::isUnorderedList() const { return allParagraphs(*this, ListKind::Unordered); }

void RichTextState::addSpanStyle(const SpanStyle& style, TextRange range) const {
    Data& d = *data_;
    auto [a, e] = ordered(range);
    if (a == e) return;
    styleChange(d, [&] { d.model.styleRange(a, e, [&](RichRun& r) { detail::spanAdd(r.style, style); }); });
}
void RichTextState::removeSpanStyle(const SpanStyle& style, TextRange range) const {
    Data& d = *data_;
    auto [a, e] = ordered(range);
    if (a == e) return;
    styleChange(d, [&] { d.model.styleRange(a, e, [&](RichRun& r) { detail::spanRemove(r.style, style); }); });
}

namespace {
// With no selection: what is typed next, changed by `change`.
void changeTyping(Data& d, const std::function<void(RichRun&)>& change) {
    d.syncModel();
    int caret = d.selection().start;
    RichRun t = d.typing && d.typingAt == caret ? *d.typing : d.model.insertionStyle(caret);
    change(t);
    d.typing = t;
    d.typingAt = caret;
    d.changed();
}
} // namespace

void RichTextState::addSpanStyle(const SpanStyle& style) const {
    if (selection().collapsed()) return changeTyping(*data_, [&](RichRun& r) { detail::spanAdd(r.style, style); });
    addSpanStyle(style, selection());
}
void RichTextState::removeSpanStyle(const SpanStyle& style) const {
    if (selection().collapsed()) return changeTyping(*data_, [&](RichRun& r) { detail::spanRemove(r.style, style); });
    removeSpanStyle(style, selection());
}
void RichTextState::toggleSpanStyle(const SpanStyle& style) const {
    if (detail::spanHas(currentSpanStyle(), style)) removeSpanStyle(style);
    else addSpanStyle(style);
}

void RichTextState::addParagraphStyle(const ParagraphStyle& style) const {
    Data& d = *data_;
    styleChange(d, [&] { eachParagraph(d, [&](detail::RichParagraph& p, int) { detail::paragraphAdd(p.style, style); }); });
}
void RichTextState::removeParagraphStyle(const ParagraphStyle& style) const {
    Data& d = *data_;
    styleChange(d, [&] { eachParagraph(d, [&](detail::RichParagraph& p, int) { detail::paragraphRemove(p.style, style); }); });
}
void RichTextState::toggleParagraphStyle(const ParagraphStyle& style) const {
    if (detail::paragraphHas(currentParagraphStyle(), style)) removeParagraphStyle(style);
    else addParagraphStyle(style);
}
void RichTextState::setHeadingStyle(HeadingStyle heading) const {
    Data& d = *data_;
    styleChange(d, [&] { eachParagraph(d, [&](detail::RichParagraph& p, int) { p.heading = heading; }); });
}

// ---- links ----

void RichTextState::addLink(const std::string& text, const std::string& url) const {
    Data& d = *data_;
    if (text.empty()) return;
    d.syncModel();
    d.record(false);
    auto [a, e] = ordered(d.selection());
    RichRun style = d.model.insertionStyle(e);
    style.link = url;
    style.code = false;
    d.model.insert(e, text, style);
    d.pushToField({e + int(text.size()), e + int(text.size())});
}
void RichTextState::addLinkToTextRange(const std::string& url, TextRange range) const {
    Data& d = *data_;
    auto [a, e] = ordered(range);
    if (a == e) return;
    styleChange(d, [&] { d.model.styleRange(a, e, [&](RichRun& r) { r.link = url; }); });
}
void RichTextState::addLinkToSelection(const std::string& url) const { addLinkToTextRange(url, selection()); }
void RichTextState::updateLink(const std::string& url) const {
    Data& d = *data_;
    d.syncModel();
    auto around = linkAround(d, d.selection());
    if (!around) return;
    styleChange(d, [&] { d.model.styleRange(around->first, around->second, [&](RichRun& r) { r.link = url; }); });
}
void RichTextState::removeLink() const {
    Data& d = *data_;
    d.syncModel();
    auto around = linkAround(d, d.selection());
    auto [a, e] = ordered(d.selection());
    int s = around ? std::min(around->first, a) : a, t = around ? std::max(around->second, e) : e;
    if (s == t) return;
    styleChange(d, [&] { d.model.styleRange(s, t, [&](RichRun& r) { r.link.clear(); }); });
}

// ---- code spans ----

void RichTextState::addCodeSpan() const {
    Data& d = *data_;
    if (selection().collapsed()) return changeTyping(d, [](RichRun& r) { r.code = true; });
    auto [a, e] = ordered(selection());
    styleChange(d, [&] { d.model.styleRange(a, e, [](RichRun& r) { r.code = true; }); });
}
void RichTextState::removeCodeSpan() const {
    Data& d = *data_;
    if (selection().collapsed()) return changeTyping(d, [](RichRun& r) { r.code = false; });
    auto [a, e] = ordered(selection());
    styleChange(d, [&] { d.model.styleRange(a, e, [](RichRun& r) { r.code = false; }); });
}
void RichTextState::toggleCodeSpan() const {
    if (isCodeSpan()) removeCodeSpan();
    else addCodeSpan();
}

// ---- lists ----

namespace {
void setList(const RichTextState& s, ListKind kind) {
    Data& d = *s.data_;
    styleChange(d, [&] {
        eachParagraph(d, [&](detail::RichParagraph& p, int) {
            if (p.list == ListKind::None) p.level = 1;
            p.list = kind;
        });
    });
}
void unsetList(const RichTextState& s, ListKind kind) {
    Data& d = *s.data_;
    styleChange(d, [&] {
        eachParagraph(d, [&](detail::RichParagraph& p, int) {
            if (p.list == kind) p.list = ListKind::None, p.level = 1;
        });
    });
}
} // namespace
void RichTextState::addOrderedList() const { setList(*this, ListKind::Ordered); }
void RichTextState::addUnorderedList() const { setList(*this, ListKind::Unordered); }
void RichTextState::removeOrderedList() const { unsetList(*this, ListKind::Ordered); }
void RichTextState::removeUnorderedList() const { unsetList(*this, ListKind::Unordered); }
void RichTextState::toggleOrderedList() const {
    if (isOrderedList()) removeOrderedList();
    else addOrderedList();
}
void RichTextState::toggleUnorderedList() const {
    if (isUnorderedList()) removeUnorderedList();
    else addUnorderedList();
}

// An item goes a level deeper under an item of a list before it at least as deep.
bool RichTextState::canIncreaseListLevel() const {
    Data& d = *data_;
    (void)d.version.get();
    d.syncModel();
    auto [a, e] = ordered(d.selection());
    int first = d.model.paragraphAt(a);
    if (first == 0) return false;
    const auto& ps = d.model.paragraphs;
    if (ps[size_t(first)].list == ListKind::None || ps[size_t(first) - 1].list == ListKind::None) return false;
    return ps[size_t(first) - 1].level >= ps[size_t(first)].level && ps[size_t(first)].level < 7;
}
bool RichTextState::canDecreaseListLevel() const {
    Data& d = *data_;
    (void)d.version.get();
    d.syncModel();
    bool any = false;
    eachParagraph(d, [&](detail::RichParagraph& p, int) { any = any || (p.list != ListKind::None && p.level > 1); });
    return any;
}
void RichTextState::increaseListLevel() const {
    if (!canIncreaseListLevel()) return;
    Data& d = *data_;
    styleChange(d, [&] {
        eachParagraph(d, [&](detail::RichParagraph& p, int) {
            if (p.list != ListKind::None && p.level < 7) ++p.level;
        });
    });
}
void RichTextState::decreaseListLevel() const {
    Data& d = *data_;
    styleChange(d, [&] {
        eachParagraph(d, [&](detail::RichParagraph& p, int) {
            if (p.list != ListKind::None && p.level > 1) --p.level;
        });
    });
}

// ---- text ----

void RichTextState::setText(const std::string& text) const {
    Data& d = *data_;
    d.syncModel();
    d.record(false);
    d.model = RichModel{};
    d.model.text = text;
    if (!text.empty()) d.model.runs.push_back({0, int(text.size())});
    d.model.normalize();
    d.pushToField({int(text.size()), int(text.size())});
}
void RichTextState::replaceTextRange(TextRange range, const std::string& text) const {
    Data& d = *data_;
    d.syncModel();
    d.record(false);
    auto [a, e] = ordered(range);
    a = std::clamp(a, 0, int(d.model.text.size()));
    e = std::clamp(e, a, int(d.model.text.size()));
    RichRun style = d.model.runAt(a) && e > a ? *d.model.runAt(a) : d.model.insertionStyle(a);
    d.model.replace(a, e, text, style);
    d.pushToField({a + int(text.size()), a + int(text.size())});
}
void RichTextState::addTextAtIndex(int index, const std::string& text) const { replaceTextRange({index, index}, text); }
void RichTextState::addTextAfterSelection(const std::string& text) const {
    int e = std::max(selection().start, selection().end);
    replaceTextRange({e, e}, text);
}
void RichTextState::removeTextRange(TextRange range) const { replaceTextRange(range, {}); }
void RichTextState::removeSelectedText() const { replaceTextRange(selection(), {}); }
void RichTextState::replaceSelectedText(const std::string& text) const { replaceTextRange(selection(), text); }
void RichTextState::clear() const { setText({}); }

std::string RichTextState::toText() const {
    (void)data_->version.get();
    data_->syncModel();
    return data_->model.text;
}
std::string RichTextState::toText(TextRange range) const {
    auto [a, e] = ordered(range);
    std::string t = toText();
    a = std::clamp(a, 0, int(t.size()));
    e = std::clamp(e, a, int(t.size()));
    return t.substr(size_t(a), size_t(e - a));
}

void RichTextState::setAnnotatedString(const AnnotatedString& text) const {
    Data& d = *data_;
    d.syncModel();
    d.record(false);
    d.model = detail::modelOfAnnotatedString(text);
    d.pushToField({int(d.model.text.size()), int(d.model.text.size())});
}

void RichTextState::setHtml(std::string_view html) const {
    Data& d = *data_;
    d.syncModel();
    d.record(false);
    d.model = detail::modelOfHtml(html);
    d.pushToField({int(d.model.text.size()), int(d.model.text.size())});
}
std::string RichTextState::toHtml() const {
    (void)data_->version.get();
    data_->syncModel();
    return detail::htmlOfModel(data_->model);
}
std::string RichTextState::toHtml(TextRange range) const {
    (void)data_->version.get();
    data_->syncModel();
    auto [a, e] = ordered(range);
    return detail::htmlOfModel(data_->model.slice(a, e));
}
void RichTextState::insertHtml(std::string_view html, int position) const {
    Data& d = *data_;
    d.syncModel();
    d.record(false);
    RichModel inserted = detail::modelOfHtml(html);
    position = std::clamp(position, 0, int(d.model.text.size()));
    d.model.insertModel(position, inserted);
    d.pushToField({position + int(inserted.text.size()), position + int(inserted.text.size())});
}
void RichTextState::insertHtmlAfterSelection(std::string_view html) const { insertHtml(html, std::max(selection().start, selection().end)); }

void RichTextState::setMarkdown(std::string_view markdown) const {
    Data& d = *data_;
    d.syncModel();
    d.record(false);
    d.model = detail::modelOfMarkdown(markdown);
    d.pushToField({int(d.model.text.size()), int(d.model.text.size())});
}
std::string RichTextState::toMarkdown() const {
    (void)data_->version.get();
    data_->syncModel();
    return detail::markdownOfModel(data_->model);
}
std::string RichTextState::toMarkdown(TextRange range) const {
    (void)data_->version.get();
    data_->syncModel();
    auto [a, e] = ordered(range);
    return detail::markdownOfModel(data_->model.slice(a, e));
}
void RichTextState::insertMarkdown(std::string_view markdown, int position) const {
    Data& d = *data_;
    d.syncModel();
    d.record(false);
    RichModel inserted = detail::modelOfMarkdown(markdown);
    position = std::clamp(position, 0, int(d.model.text.size()));
    d.model.insertModel(position, inserted);
    d.pushToField({position + int(inserted.text.size()), position + int(inserted.text.size())});
}
void RichTextState::insertMarkdownAfterSelection(std::string_view markdown) const {
    insertMarkdown(markdown, std::max(selection().start, selection().end));
}

SavedValue Saver<RichTextState>::save(const RichTextState& s) {
    TextRange sel = s.selection();
    return std::vector<SavedValue>{SavedValue(s.toHtml()), SavedValue(int64_t(sel.start)), SavedValue(int64_t(sel.end))};
}
std::optional<RichTextState> Saver<RichTextState>::restore(const SavedValue& v) {
    const auto* items = v.list();
    if (!items || items->size() != 3 || !(*items)[0].string()) return std::nullopt;
    RichTextState s;
    s.setHtml(*(*items)[0].string());
    s.history().clear();
    int a = int((*items)[1].integer().value_or(0)), e = int((*items)[2].integer().value_or(0));
    s.setSelection({a, e});
    return s;
}

RichTextState detail::rememberRichTextState(int historyLimit, int64_t coalesceWindowMs, const std::function<void(RichTextState&)>& init,
                                            CallSite site) {
    return rememberSaveable(
        [&] {
            RichTextState s(historyLimit, coalesceWindowMs);
            if (init) {
                init(s);
                s.history().clear();  // (its first content: no step to undo)
            }
            return s;
        },
        site);
}

// ---- the editors ----

void detail::richTextEditor(RichEditorKind kind, const RichTextState& state, const Modifier& modifier, TextFieldOptions options,
                            CallSite site) {
    KompozableScope scope(site);
    auto data = state.data_;
    (void)data->version.get();  // (shown again as its styles change)
    data->syncModel();
    Font font = styledFont(options.textStyle, typography().bodyLarge);
    RichShowEnv env = showEnv(false, font);
    // The model as the field shows it: markers before list items, styles and paragraph styles; the
    // offsets mapped around the markers.
    options.visualTransformation = VisualTransformation([data, env](const AnnotatedString& text) {
        if (text.text() != data->model.text) return TransformedText{text, OffsetMapping::Identity};
        auto shown = std::make_shared<ShownRichText>(showRichText(data->model, data->config, env));
        AnnotatedString styled = shown->text;
        return TransformedText{std::move(styled), OffsetMapping{[shown](int o) { return shown->toShown(o); },
                                                                 [shown](int o) { return shown->toValue(o); }}};
    });
    options.inputTransformation = InputTransformation{[data](TextFieldBuffer& buffer) { data->userEdit(buffer); }};
    // Its keys: history, styles, list levels.
    Modifier keyed = modifier.onPreviewKeyEvent([state](const KeyEvent& e) {
        if (e.type != KeyEventType::KeyDown) return false;
        bool ctrl = e.modifiers.ctrl;  // (Command on macOS)
        if (ctrl && e.key == Key::Z) {
            if (e.modifiers.shift) state.history().redo();
            else state.history().undo();
            return true;
        }
        if (ctrl && e.key == Key::Y) {
            state.history().redo();
            return true;
        }
        if (ctrl && e.key == Key::B) {
            state.toggleSpanStyle({.fontWeight = FontWeight::Bold});
            return true;
        }
        if (ctrl && e.key == Key::I) {
            state.toggleSpanStyle({.fontStyle = FontStyle::Italic});
            return true;
        }
        if (ctrl && e.key == Key::U) {
            state.toggleSpanStyle({.textDecoration = TextDecoration::Underline});
            return true;
        }
        if (e.key == Key::Tab && !ctrl && state.isList()) {
            if (e.modifiers.shift) {
                if (!state.canDecreaseListLevel()) return false;
                state.decreaseListLevel();
            } else {
                if (!state.canIncreaseListLevel()) return false;
                state.increaseListLevel();
            }
            return true;
        }
        return false;
    });
    switch (kind) {
    case RichEditorKind::Filled: TextField(data->field, keyed, std::move(options)); break;
    case RichEditorKind::Outlined: OutlinedTextField(data->field, keyed, std::move(options)); break;
    case RichEditorKind::Basic: BasicTextField(data->field, keyed, std::move(options)); break;
    }
}

} // namespace kompoz
