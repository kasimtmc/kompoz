#pragma once

// Text with several styles (Compose's AnnotatedString and SpanStyle):
//
//   Text(buildAnnotatedString([](AnnotatedString::Builder& b) {
//       b.append("Merhaba ");
//       b.withStyle({.fontWeight = FontWeight::Bold, .color = colors::Red}, [&] { b.append("dünya"); });
//       b.append(", ayrıntılar ");
//       b.withLink([] { openDetails(); }, [&] { b.append("burada"); });
//   }));

#include "graphics.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kompoz {

enum class TextDecoration { None, LineThrough, Underline };

// A length in text (Compose's TextUnit): sp, or em — a share of a font size. A font size in em is
// that share of the size it would have otherwise (the style around it: the enclosing span, the
// Text's style, LocalTextStyle); letter spacing and line height in em are shares of their own
// font size. Plain numbers are sp: {.fontSize = 20, .letterSpacing = 0.1_em}.
struct TextUnit {
    enum class Type { Sp, Em };
    float value = 0;
    Type type = Type::Sp;
    constexpr TextUnit() = default;
    constexpr TextUnit(float sp) : value(sp) {}
    static constexpr TextUnit sp(float v) { return {v, Type::Sp}; }
    static constexpr TextUnit em(float v) { return {v, Type::Em}; }
    bool isEm() const { return type == Type::Em; }
    // In sp, given the font size an em is a share of.
    constexpr float resolve(float fontSize) const { return type == Type::Em ? value * fontSize : value; }
    friend constexpr bool operator==(const TextUnit&, const TextUnit&) = default;

private:
    constexpr TextUnit(float v, Type t) : value(v), type(t) {}
};
constexpr TextUnit operator""_sp(long double v) { return TextUnit::sp(float(v)); }
constexpr TextUnit operator""_sp(unsigned long long v) { return TextUnit::sp(float(v)); }
constexpr TextUnit operator""_em(long double v) { return TextUnit::em(float(v)); }
constexpr TextUnit operator""_em(unsigned long long v) { return TextUnit::em(float(v)); }

// Unset fields keep the style around them (the Text's style, an enclosing span).
// Glyphs stretched across (scaleX, their advances too) and slanted (skewX: a point y above the
// baseline moves y * skewX to the right; -0.25 leans as italics do the other way), as Compose's
// TextGeometricTransform.
struct TextGeometricTransform {
    float scaleX = 1, skewX = 0;
    friend constexpr bool operator==(const TextGeometricTransform&, const TextGeometricTransform&) = default;
};

struct SpanStyle {
    std::optional<TextUnit> fontSize;
    std::optional<FontWeight> fontWeight;
    std::optional<FontStyle> fontStyle;
    std::optional<FontFamily> fontFamily;
    std::optional<Color> color;
    std::optional<TextDecoration> textDecoration;
    std::optional<Color> background;
    std::optional<TextUnit> letterSpacing;  // after each character
    std::optional<std::string> fontFeatureSettings;  // OpenType features, as CSS writes them ("tnum, smcp")
    std::optional<Brush> brush;          // paints the text instead of its color (spread over the whole text)
    std::optional<Shadow> shadow;
    std::optional<float> baselineShift;  // up (down if negative) by this share of its ascent (BaselineShift)
    std::optional<DrawStyle> drawStyle;  // Stroke: its glyphs outlined (see TextStyle::drawStyle)
    std::optional<TextGeometricTransform> textGeometricTransform;
    friend bool operator==(const SpanStyle&, const SpanStyle&) = default;
};

// SpanStyle::baselineShift's usual values (Compose's BaselineShift).
namespace BaselineShift {
inline constexpr float Superscript = 0.5f, Subscript = -0.5f, None = 0;
}

// How a link looks (Compose's TextLinkStyles): always, and while it is focused, the mouse is
// over it or it is pressed (each merged over the ones before).
struct TextLinkStyles {
    std::optional<SpanStyle> style, focusedStyle, hoveredStyle, pressedStyle;
    friend bool operator==(const TextLinkStyles&, const TextLinkStyles&) = default;
};

// A link in an AnnotatedString (Compose's LinkAnnotation): a URL, opened by the platform
// (uriHandler) unless a listener takes the click, or a tagged clickable stretch.
struct LinkAnnotation {
    enum class Kind { Url, Clickable };
    using Listener = std::function<void(const LinkAnnotation&)>;
    static LinkAnnotation Url(std::string url, std::optional<TextLinkStyles> styles = {}, Listener listener = {}) {
        return {Kind::Url, std::move(url), {}, std::move(styles), std::move(listener)};
    }
    static LinkAnnotation Clickable(std::string tag, std::optional<TextLinkStyles> styles, Listener listener) {
        return {Kind::Clickable, {}, std::move(tag), std::move(styles), std::move(listener)};
    }
    Kind kind = Kind::Url;
    std::string url, tag;
    std::optional<TextLinkStyles> styles;
    Listener linkInteractionListener;
};

// Start and End follow the paragraph's direction (Start is the right edge of Arabic text);
// Left and Right don't. Justify stretches each wrapped line to the width at its spaces; a
// paragraph's last line, and a line mixing directions, start at the start.
enum class TextAlign { Start, Center, End, Left, Right, Justify };

// How far in (sp, from the start) a paragraph's first line and its other lines begin
// (Compose's TextIndent).
struct TextIndent {
    float firstLine = 0, restLine = 0;
    friend bool operator==(const TextIndent&, const TextIndent&) = default;
};
// How a paragraph's lines are broken (Compose's LineBreak). Strategy: Simple fills each line in
// turn; Balanced makes the lines as even as it can without adding one (headings); HighQuality
// leaves the least uneven room at the lines' ends, the last line aside (paragraphs). Strictness
// and word break are rules for Chinese, Japanese and Korean text: kept, but they change nothing
// yet. Unspecified is Simple.
struct LineBreak {
    enum class Strategy { Unspecified, Simple, HighQuality, Balanced };
    enum class Strictness { Unspecified, Default, Loose, Normal, Strict };
    enum class WordBreak { Unspecified, Default, Phrase };
    Strategy strategy = Strategy::Unspecified;
    Strictness strictness = Strictness::Unspecified;
    WordBreak wordBreak = WordBreak::Unspecified;
    static const LineBreak Simple, Heading, Paragraph, Unspecified;
    friend bool operator==(const LineBreak&, const LineBreak&) = default;
};
inline const LineBreak LineBreak::Simple{Strategy::Simple, Strictness::Normal, WordBreak::Default};
inline const LineBreak LineBreak::Heading{Strategy::Balanced, Strictness::Loose, WordBreak::Phrase};
inline const LineBreak LineBreak::Paragraph{Strategy::HighQuality, Strictness::Strict, WordBreak::Default};
inline const LineBreak LineBreak::Unspecified{};
// A paragraph's direction: Content (the default) takes it from its first strong character
// (left to right if none), like Compose's ContentOrLtr.
enum class TextDirection { Content, ContentOrLtr, ContentOrRtl, Ltr, Rtl };
// A paragraph's own settings in an AnnotatedString (Compose's ParagraphStyle): unset ones are
// the text's. A paragraph style's range is a paragraph of its own (a line break before and
// after it, as in Compose); nested ones merge, the inner winning.
struct ParagraphStyle {
    std::optional<TextAlign> textAlign;
    std::optional<TextDirection> textDirection;
    std::optional<TextUnit> lineHeight;  // em: of the paragraph's font size
    std::optional<LineHeightStyle> lineHeightStyle;
    std::optional<TextIndent> textIndent;
    std::optional<LineBreak> lineBreak;
    friend bool operator==(const ParagraphStyle&, const ParagraphStyle&) = default;
};

class AnnotatedStringBuilder;

// Where an inline content's room sits on its line (Compose's PlaceholderVerticalAlign): its
// bottom on the baseline, or lined up with the text's top, bottom or middle (Top, Bottom and
// Center the same here).
enum class PlaceholderVerticalAlign { AboveBaseline, Top, Bottom, Center, TextTop, TextBottom, TextCenter };
// The room an inline content takes in a text (Compose's Placeholder): sp, or em of the text's size.
struct Placeholder {
    TextUnit width, height;
    PlaceholderVerticalAlign verticalAlign = PlaceholderVerticalAlign::AboveBaseline;
    friend constexpr bool operator==(const Placeholder&, const Placeholder&) = default;
};
// The annotation appendInlineContent puts on its alternate text (Compose's).
inline constexpr const char* kInlineContentTag = "androidx.compose.foundation.text.inlineContent";

class AnnotatedString {
public:
    using Builder = AnnotatedStringBuilder;
    struct Span {
        int start, end;  // UTF-8 byte offsets
        SpanStyle style;
        friend bool operator==(const Span&, const Span&) = default;
    };
    struct ParagraphSpan {
        int start, end;
        ParagraphStyle style;
        friend bool operator==(const ParagraphSpan&, const ParagraphSpan&) = default;
    };
    struct Link {
        int start, end;
        std::function<void()> onClick;
        std::optional<LinkAnnotation> annotation;  // withLink(LinkAnnotation, ...)
    };
    // Metadata on a range (Compose's string annotations, AnnotatedString.Range<String>): a tag
    // and its item — a URL, an id — to look up where the text was tapped.
    struct Annotation {
        std::string tag, item;
        int start, end;
        friend bool operator==(const Annotation&, const Annotation&) = default;
    };

    AnnotatedString() = default;
    explicit AnnotatedString(std::string text) : text_(std::move(text)) {}
    // Styled text from HTML (Compose's AnnotatedString.fromHtml, the tags Android's Html knows):
    // <b>/<strong>, <i>/<em>, <u>, <s>/<del>, <sup>/<sub>, <big>/<small>, <tt>/<code>, <h1>–<h6>,
    // <font color>, <span style="color: …; background-color: …; font-weight: bold; …">,
    // <a href> (a LinkAnnotation::Url with `linkStyles`), <br>, and <p>, <div>, <ul>/<li> (•),
    // <blockquote>, <pre> each on lines of their own; entities (&amp;, &#252;…); white space
    // collapsed as in HTML. Other tags are left out, their text kept.
    static AnnotatedString fromHtml(std::string_view html, std::optional<TextLinkStyles> linkStyles = {},
                                    LinkAnnotation::Listener linkInteractionListener = {});

    const std::string& text() const { return text_; }
    const std::vector<Span>& spans() const { return spans_; }
    const std::vector<Link>& links() const { return links_; }
    const std::vector<Annotation>& annotations() const { return annotations_; }
    const std::vector<ParagraphSpan>& paragraphStyles() const { return paragraphStyles_; }
    void addStyle(const SpanStyle& style, int start, int end) { spans_.push_back({start, end, style}); }
    void addStyle(const ParagraphStyle& style, int start, int end) { paragraphStyles_.push_back({start, end, style}); }
    // The annotations (with `tag`) on [start, end) — a range collapsed to an offset finds the
    // ones around it, as Compose's getStringAnnotations does (ClickableText's way).
    std::vector<Annotation> getStringAnnotations(const std::string& tag, int start, int end) const;
    std::vector<Annotation> getStringAnnotations(int start, int end) const;
    bool hasStringAnnotations(const std::string& tag, int start, int end) const { return !getStringAnnotations(tag, start, end).empty(); }

    // Equal text, spans and annotations (links hold callbacks and never compare equal).
    friend bool operator==(const AnnotatedString& a, const AnnotatedString& b) {
        return a.links_.empty() && b.links_.empty() && a.text_ == b.text_ && a.spans_ == b.spans_ && a.annotations_ == b.annotations_ &&
               a.paragraphStyles_ == b.paragraphStyles_;
    }

private:
    friend class AnnotatedStringBuilder;
    std::string text_;
    std::vector<Span> spans_;
    std::vector<Link> links_;
    std::vector<Annotation> annotations_;
    std::vector<ParagraphSpan> paragraphStyles_;
};

class AnnotatedStringBuilder {
public:
    void append(std::string_view text) { s_.text_ += text; }
    // Styles what `content` appends (spans nest: inner fields win).
    void withStyle(const SpanStyle& style, const std::function<void()>& content);
    void addStyle(const SpanStyle& style, int start, int end) { s_.spans_.push_back({start, end, style}); }
    // What `content` appends is a paragraph of its own with these settings (Compose's
    // withStyle(ParagraphStyle)).
    void withStyle(const ParagraphStyle& style, const std::function<void()>& content);
    void addStyle(const ParagraphStyle& style, int start, int end) { s_.paragraphStyles_.push_back({start, end, style}); }
    int pushStyle(const ParagraphStyle& style);
    // Compose's pushStyle / pop: what is appended from here on is styled until pop() (the last
    // pushed first) or pop(index) (it and all pushed after it); returns its index.
    int pushStyle(const SpanStyle& style);
    void pop();
    void pop(int index);
    // String annotations: on a range, or on what is appended until pop(), or on what `content` appends.
    void addStringAnnotation(std::string tag, std::string annotation, int start, int end) {
        s_.annotations_.push_back({std::move(tag), std::move(annotation), start, end});
    }
    int pushStringAnnotation(std::string tag, std::string annotation);
    void withAnnotation(std::string tag, std::string annotation, const std::function<void()>& content);
    // Room for inline content (Compose's appendInlineContent): Text's inlineContent[id] goes there,
    // in place of `alternateText` (what copying the text or accessibility gets).
    void appendInlineContent(const std::string& id, std::string_view alternateText = "\uFFFD") {
        int start = length();
        append(alternateText);
        addStringAnnotation(kInlineContentTag, id, start, length());
    }
    // What `content` appends is clickable (underlined in the primary color unless styled).
    void withLink(std::function<void()> onClick, const std::function<void()>& content,
                  SpanStyle style = {.textDecoration = TextDecoration::Underline});
    // The same with Compose's LinkAnnotation: underlined in the primary color, then its styles.
    void withLink(const LinkAnnotation& link, const std::function<void()>& content);
    int length() const { return int(s_.text_.size()); }
    // (What is still pushed ends with the text.)
    AnnotatedString toAnnotatedString() {
        pop(0);
        return std::move(s_);
    }

private:
    AnnotatedString s_;
    struct Pushed {  // what pop() ends
        int start;
        std::optional<SpanStyle> style;
        std::optional<std::pair<std::string, std::string>> annotation;
        std::optional<ParagraphStyle> paragraph;
    };
    std::vector<Pushed> pushed_;
};

AnnotatedString buildAnnotatedString(const std::function<void(AnnotatedString::Builder&)>& build);

} // namespace kompoz
