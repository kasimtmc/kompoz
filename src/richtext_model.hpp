#pragma once

// Internal: a rich text's model (richtext.hpp's RichTextState) — its text, the runs of character
// styles over it and its paragraphs — how it is shown, and HTML and Markdown in and out.

#include <kompoz/richtext.hpp>

#include "text_layout.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kompoz::detail {

// A stretch of the text with one style: its characters' SpanStyle, a link's URL, a code span.
struct RichRun {
    int start = 0, end = 0;
    SpanStyle style;
    std::string link;
    bool code = false;
    bool sameStyle(const RichRun& o) const { return style == o.style && link == o.link && code == o.code; }
};

enum class ListKind { None, Ordered, Unordered };

// A paragraph (a line of the text): its style, heading, and the list it is an item of (level 1 the
// outermost).
struct RichParagraph {
    ParagraphStyle style;
    ListKind list = ListKind::None;
    int level = 1;
    HeadingStyle heading = HeadingStyle::Normal;
    friend bool operator==(const RichParagraph&, const RichParagraph&) = default;
};

// The runs cover the text end to end (merged where their styles are the same); a paragraph for each
// line.
struct RichModel {
    std::string text;
    std::vector<RichRun> runs;
    std::vector<RichParagraph> paragraphs{RichParagraph{}};

    std::vector<int> paragraphStarts() const;
    int paragraphAt(int offset) const;
    std::pair<int, int> paragraphRange(int index) const;  // without its line break
    const RichRun* runAt(int offset) const;
    RichRun insertionStyle(int at) const;

    void splitAt(int at);
    void normalize();
    void insert(int at, std::string_view s, const RichRun& style);
    void erase(int from, int to);
    void replace(int from, int to, std::string_view s, const RichRun& style);
    void styleRange(int from, int to, const std::function<void(RichRun&)>& change);
    RichModel slice(int from, int to) const;
    void insertModel(int at, const RichModel& other);
    // Text appended with a style (building one from HTML or Markdown).
    void append(std::string_view s, const RichRun& style) { insert(int(text.size()), s, style); }
};

// SpanStyle and ParagraphStyle fields set in `style`: all of them in `on`; set; taken out; those
// two have in common.
bool spanHas(const SpanStyle& on, const SpanStyle& style);
void spanAdd(SpanStyle& on, const SpanStyle& style);
void spanRemove(SpanStyle& on, const SpanStyle& style);
SpanStyle spanCommon(const SpanStyle& a, const SpanStyle& b);
bool paragraphHas(const ParagraphStyle& on, const ParagraphStyle& style);
void paragraphAdd(ParagraphStyle& on, const ParagraphStyle& style);
void paragraphRemove(ParagraphStyle& on, const ParagraphStyle& style);
ParagraphStyle paragraphCommon(const ParagraphStyle& a, const ParagraphStyle& b);

struct RichConfigValues {
    Color linkColor = Color::Unspecified;
    TextDecoration linkTextDecoration = TextDecoration::Underline;
    Color codeSpanColor = Color::Unspecified, codeSpanBackgroundColor = Color::Unspecified;
    int orderedListIndent = 38, unorderedListIndent = 38;
    bool exitListOnEmptyItem = true;
    bool listTypingShortcutsEnabled = true, headingTypingShortcutsEnabled = false, inlineTypingShortcutsEnabled = false;
    SpanStyle listMarkerStyle;
};

// What showing it needs from around: the theme, a list marker's width (sp), Text's links or a field's.
struct RichShowEnv {
    Typography typography;
    Color primary = Color::Unspecified, codeBackground = Color::Unspecified;
    std::function<float(const std::string&)> measure;
    float fontSize = 16;
    bool links = false;  // LinkAnnotations (a Text's, opened); else the links' look alone (a field's)
};
struct ShownRichText {
    AnnotatedString text;
    std::vector<int> paragraphStarts, markerLengths;  // each paragraph's start in the model, its marker's bytes
    int toShown(int offset) const;
    int toValue(int shown) const;
};
std::vector<std::string> listMarkers(const std::vector<RichParagraph>& paragraphs);
Font headingFont(HeadingStyle heading, const Typography& typography);
ShownRichText showRichText(const RichModel& model, const RichConfigValues& config, const RichShowEnv& env);

// HTML and Markdown in and out (richtext_html.cpp, richtext_markdown.cpp); an AnnotatedString in.
RichModel modelOfHtml(std::string_view html);
std::string htmlOfModel(const RichModel& model);
RichModel modelOfMarkdown(std::string_view markdown);
std::string markdownOfModel(const RichModel& model);
RichModel modelOfAnnotatedString(const AnnotatedString& text);

struct RichTextData {
    RichModel model;
    TextFieldState field;
    MutableState<int> version = mutableStateOf(0);         // what shows it is shown again
    MutableState<int> historyVersion = mutableStateOf(0);  // canUndo, canRedo
    RichConfigValues config;
    // What is typed next at `typingAt` (a style toggled with no selection).
    std::optional<RichRun> typing;
    int typingAt = -1;
    // History: the model and selection before each step.
    struct Step {
        RichModel model;
        TextRange selection;
    };
    std::vector<Step> undoStack, redoStack;
    int historyLimit = 100;
    int64_t coalesceWindowMs = 500;
    int64_t lastEditAt = 0;
    bool lastWasTyping = false, restoring = false;
    int grouping = 0;
    bool groupRecorded = false;

    void changed();
    void record(bool typing);
    void syncModel();
    void pushToField(TextRange selection);
    void userEdit(TextFieldBuffer& buffer);
    TextRange selection() const;
};

} // namespace kompoz::detail
