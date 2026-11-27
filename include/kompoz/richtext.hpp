#pragma once

// Rich text: a text field whose text is bold, italic, colored, linked, in headings and lists, with the
// API of Compose Rich Editor (com.mohamedrejeb.richeditor). Compose has none.
//
//   val state = rememberRichTextState()
//   Row {
//       IconButton(onClick = { state.toggleSpanStyle(SpanStyle(fontWeight = FontWeight.Bold)) }) { Icon(Icons.Filled.FormatBold, "Kalın") }
//       IconButton(onClick = { state.toggleUnorderedList() }) { Icon(Icons.Filled.FormatListBulleted, "Liste") }
//   }
//   RichTextEditor(state = state, modifier = Modifier.fillMaxWidth())
//   Button(onClick = { save(state.toHtml()) }) { Text("Kaydet") }
//
// A RichTextState holds the text and its styles: character styles (SpanStyle), links and code spans,
// and each paragraph's style (ParagraphStyle), heading (HeadingStyle) and list (ordered, unordered,
// nested). Its toggle and add functions change the selection's text (with no selection, what is typed
// next); currentSpanStyle and the is… properties tell the selection's, for a toolbar. Typed text takes
// the style before it; Enter continues a list (an empty item leaves it), Backspace at an item's start
// leaves the list; Tab and Shift+Tab nest an item deeper and back. "- ", "* " and "1. " at a
// paragraph's start make lists, and with config's shortcuts "# " headings and **bold**, *italic*,
// `code`, ~~strike~~. Ctrl+B, Ctrl+I and Ctrl+U toggle bold, italic and underline; Ctrl+Z and
// Ctrl+Shift+Z (Ctrl+Y) undo and redo through history. setHtml / toHtml and setMarkdown / toMarkdown
// read and write it. RichText shows it, not editable, its links opened as Text's.

#include "named.hpp"
#include "runtime.hpp"
#include "saved_value.hpp"
#include "text.hpp"
#include "widgets.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kompoz {

// A paragraph's heading level (Normal: a paragraph of body text), in the theme's typography.
enum class HeadingStyle { Normal, H1, H2, H3, H4, H5, H6 };

namespace detail {
struct RichTextData;
}

// How a rich text looks where its HTML and Markdown say nothing (Compose Rich Editor's
// RichTextConfig): links', code spans' and lists' looks, and which typing shortcuts work. Read and
// set through state.config (observable: what shows it changes with it).
class RichTextConfig {
public:
#define KOMPOZ_RICH_CONFIG(Type, name, Setter) \
    Type name() const;                       \
    void Setter(Type value) const;
    KOMPOZ_RICH_CONFIG(Color, linkColor, setLinkColor)                      // unset: the scheme's primary
    KOMPOZ_RICH_CONFIG(TextDecoration, linkTextDecoration, setLinkTextDecoration)  // Underline
    KOMPOZ_RICH_CONFIG(Color, codeSpanColor, setCodeSpanColor)              // unset: the text's
    KOMPOZ_RICH_CONFIG(Color, codeSpanBackgroundColor, setCodeSpanBackgroundColor)  // unset: onSurface at 10%
    KOMPOZ_RICH_CONFIG(int, listIndent, setListIndent)                      // sp each level of a list goes in (38)
    KOMPOZ_RICH_CONFIG(int, orderedListIndent, setOrderedListIndent)
    KOMPOZ_RICH_CONFIG(int, unorderedListIndent, setUnorderedListIndent)
    KOMPOZ_RICH_CONFIG(bool, exitListOnEmptyItem, setExitListOnEmptyItem)   // Enter on an empty item leaves the list (true)
    KOMPOZ_RICH_CONFIG(bool, listTypingShortcutsEnabled, setListTypingShortcutsEnabled)        // "- ", "1. " (true)
    KOMPOZ_RICH_CONFIG(bool, headingTypingShortcutsEnabled, setHeadingTypingShortcutsEnabled)  // "# " (false)
    KOMPOZ_RICH_CONFIG(bool, inlineTypingShortcutsEnabled, setInlineTypingShortcutsEnabled)    // **bold** (false)
    KOMPOZ_RICH_CONFIG(SpanStyle, listMarkerStyle, setListMarkerStyle)      // the bullets' and numbers' own
#undef KOMPOZ_RICH_CONFIG

    std::shared_ptr<detail::RichTextData> data_;
};

// A rich text's undo history (Compose Rich Editor's state.history): what is typed comes back a burst
// at a time (edits within coalesceWindowMs of each other), each style change a step of its own.
class RichTextHistory {
public:
    bool canUndo() const;  // observable
    bool canRedo() const;
    void undo() const;
    void redo() const;
    void clear() const;
    int limit() const;  // the steps kept (100), the oldest dropped
    void setLimit(int limit) const;
    // What `changes` does, one step.
    void group(const std::function<void()>& changes) const;

    std::shared_ptr<detail::RichTextData> data_;
};

class RichTextState {
public:
    explicit RichTextState(int historyLimit = 100, int64_t coalesceWindowMs = 500);

    // ---- What it holds (observable) ----
    // The text as shown: its styles, list markers ("• ", "1. ") and links.
    AnnotatedString annotatedString() const;
    TextRange selection() const;
    void setSelection(TextRange selection) const;
    // The style the selection has all of (with no selection: what is typed next).
    SpanStyle currentSpanStyle() const;
    ParagraphStyle currentParagraphStyle() const;
    HeadingStyle currentHeadingStyle() const;
    bool isLink() const;
    bool isCodeSpan() const;
    bool isOrderedList() const;
    bool isUnorderedList() const;
    bool isList() const { return isOrderedList() || isUnorderedList(); }
    std::optional<std::string> selectedLinkText() const;  // the link at the selection
    std::optional<std::string> selectedLinkUrl() const;
    RichTextConfig config() const { return {data_}; }
    RichTextHistory history() const { return {data_}; }
    // The style all of `range` has.
    SpanStyle getSpanStyle(TextRange range) const;
    ParagraphStyle getParagraphStyle(TextRange range) const;

    // ---- Character styles: the selection's (with none, what is typed next), or a range's ----
    void toggleSpanStyle(const SpanStyle& style) const;
    void addSpanStyle(const SpanStyle& style) const;
    void addSpanStyle(const SpanStyle& style, TextRange range) const;
    void removeSpanStyle(const SpanStyle& style) const;
    void removeSpanStyle(const SpanStyle& style, TextRange range) const;

    // ---- The selection's paragraphs ----
    void toggleParagraphStyle(const ParagraphStyle& style) const;
    void addParagraphStyle(const ParagraphStyle& style) const;
    void removeParagraphStyle(const ParagraphStyle& style) const;
    void setHeadingStyle(HeadingStyle heading) const;

    // ---- Links ----
    void addLink(const std::string& text, const std::string& url) const;  // inserted after the selection
    void addLinkToSelection(const std::string& url) const;
    void addLinkToTextRange(const std::string& url, TextRange range) const;
    void updateLink(const std::string& url) const;  // the selected link's
    void removeLink() const;
    // Compose Rich Editor's named forms: addLink(text = …, url = …), addLinkToSelection(url = …),
    // addLinkToTextRange(url = …, textRange = …), updateLink(url = …).
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    void addLink(A&&... args) const {
        using P = detail::Params<detail::names::Text, detail::names::Url>;
        static_assert(P::template check<A...>());
        addLink(std::string(P::template arg<detail::names::Text>(args...)), std::string(P::template arg<detail::names::Url>(args...)));
    }
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    void addLinkToSelection(A&&... args) const {
        using P = detail::Params<detail::names::Url>;
        static_assert(P::template check<A...>());
        addLinkToSelection(std::string(P::template arg<detail::names::Url>(args...)));
    }
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    void addLinkToTextRange(A&&... args) const {
        using P = detail::Params<detail::names::Url, detail::names::TextRange>;
        static_assert(P::template check<A...>());
        addLinkToTextRange(std::string(P::template arg<detail::names::Url>(args...)), P::template arg<detail::names::TextRange>(args...));
    }
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    void updateLink(A&&... args) const {
        using P = detail::Params<detail::names::Url>;
        static_assert(P::template check<A...>());
        updateLink(std::string(P::template arg<detail::names::Url>(args...)));
    }

    // ---- Code spans ----
    void toggleCodeSpan() const;
    void addCodeSpan() const;
    void removeCodeSpan() const;

    // ---- Lists: the selection's paragraphs ----
    void toggleOrderedList() const;
    void toggleUnorderedList() const;
    void addOrderedList() const;
    void addUnorderedList() const;
    void removeOrderedList() const;
    void removeUnorderedList() const;
    bool canIncreaseListLevel() const;
    bool canDecreaseListLevel() const;
    void increaseListLevel() const;
    void decreaseListLevel() const;

    // ---- Text ----
    void setText(const std::string& text) const;  // no styles
    void addTextAtIndex(int index, const std::string& text) const;
    void addTextAfterSelection(const std::string& text) const;
    void removeTextRange(TextRange range) const;
    void removeSelectedText() const;
    void replaceTextRange(TextRange range, const std::string& text) const;
    void replaceSelectedText(const std::string& text) const;
    void clear() const;
    std::string toText() const;
    std::string toText(TextRange range) const;
    void setAnnotatedString(const AnnotatedString& text) const;  // its span and paragraph styles, links

    // ---- HTML and Markdown ----
    void setHtml(std::string_view html) const;
    std::string toHtml() const;
    std::string toHtml(TextRange range) const;
    void insertHtml(std::string_view html, int position) const;
    template <class H, class... A>
        requires(detail::anyNamedArgument<A...>)
    void insertHtml(const H& html, A&&... args) const {  // insertHtml(html, position = …)
        using P = detail::Params<detail::names::Position>;
        static_assert(P::template check<A...>());
        insertHtml(std::string_view(html), P::template arg<detail::names::Position>(args...));
    }
    void insertHtmlAfterSelection(std::string_view html) const;
    void setMarkdown(std::string_view markdown) const;
    std::string toMarkdown() const;
    std::string toMarkdown(TextRange range) const;
    void insertMarkdown(std::string_view markdown, int position) const;
    template <class M, class... A>
        requires(detail::anyNamedArgument<A...>)
    void insertMarkdown(const M& markdown, A&&... args) const {  // insertMarkdown(markdown, position = …)
        using P = detail::Params<detail::names::Position>;
        static_assert(P::template check<A...>());
        insertMarkdown(std::string_view(markdown), P::template arg<detail::names::Position>(args...));
    }
    void insertMarkdownAfterSelection(std::string_view markdown) const;

    // The editor's own text field state (its plain text and selection).
    const TextFieldState& textFieldState() const;

    friend bool operator==(const RichTextState& a, const RichTextState& b) { return a.data_ == b.data_; }
    std::shared_ptr<detail::RichTextData> data_;
};

// Saved as its HTML and selection.
template <>
struct Saver<RichTextState> {
    static SavedValue save(const RichTextState& s);
    static std::optional<RichTextState> restore(const SavedValue& v);
};

namespace detail {
RichTextState rememberRichTextState(int historyLimit, int64_t coalesceWindowMs, const std::function<void(RichTextState&)>& init,
                                    CallSite site);
// rememberRichTextState(historyLimit = …, coalesceWindowMs = …) { setHtml(…) }: the block (its first
// state) once, as the state is made.
struct RememberRichTextStateImpl {
    using P = Params<names::HistoryLimit, names::CoalesceWindowMs, BlockWith<RichTextState&>>;
    template <class... A>
    static RichTextState call(CallSite site, A&&... args) {
        static_assert(P::template check<A...>());
        std::function<void(RichTextState&)> init;
        if constexpr (P::template blockGiven<A...>()) {
            auto& block = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
            init = [&block](RichTextState& s) { block(s); };
        }
        return rememberRichTextState(P::template get<names::HistoryLimit>(100, args...),
                                     P::template get<names::CoalesceWindowMs>(int64_t(500), args...), init, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::RememberRichTextStateImpl> rememberRichTextState{};

// The editors' colors are a text field's (RichTextEditorDefaults.richTextEditorColors(…)).
struct RichTextEditorDefaults {
    using richTextEditorColors = TextFieldColors;
    using outlinedRichTextEditorColors = TextFieldColors;
};

namespace detail {
enum class RichEditorKind { Filled, Outlined, Basic };
void richTextEditor(RichEditorKind kind, const RichTextState& state, const Modifier& modifier, TextFieldOptions options, CallSite site);

// RichTextEditor(state, modifier, enabled, readOnly, textStyle, label, placeholder, leadingIcon,
// trailingIcon, supportingText, isError, keyboardOptions, keyboardActions, singleLine, maxLines,
// minLines, interactionSource, shape, colors): a TextField's (OutlinedRichTextEditor an
// OutlinedTextField's, BasicRichTextEditor a BasicTextField's) with the state's rich text.
template <RichEditorKind Kind>
struct RichTextEditorImpl {
    using P = Params<names::State, names::Modifier, names::Enabled, names::ReadOnly, names::TextStyle, names::Label, names::Placeholder,
                     names::LeadingIcon, names::TrailingIcon, names::SupportingText, names::IsError, names::KeyboardOptions,
                     names::KeyboardActions, names::SingleLine, names::MaxLines, names::MinLines, names::InteractionSource, names::Shape,
                     names::Colors, names::TextFieldColors, names::Decorator>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::template check<A...>());
        TextFieldOptions options;
        P::template set<names::Enabled>(options.enabled, args...);
        P::template set<names::ReadOnly>(options.readOnly, args...);
        P::template set<names::TextStyle>(options.textStyle, args...);
        textOrContent<names::Label>(options.label, options.labelContent, args...);
        textOrContent<names::Placeholder>(options.placeholder, options.placeholderContent, args...);
        P::template set<names::LeadingIcon>(options.leadingIcon, args...);
        P::template set<names::TrailingIcon>(options.trailingIcon, args...);
        textOrContent<names::SupportingText>(options.supportingText, options.supportingTextContent, args...);
        P::template set<names::IsError>(options.isError, args...);
        P::template set<names::KeyboardOptions>(options.keyboardOptions, args...);
        P::template set<names::KeyboardActions>(options.keyboardActions, args...);
        P::template set<names::SingleLine>(options.singleLine, args...);
        P::template set<names::MaxLines>(options.maxLines, args...);
        P::template set<names::MinLines>(options.minLines, args...);
        P::template set<names::InteractionSource>(options.interactionSource, args...);
        P::template set<names::Shape>(options.shape, args...);
        P::template set<names::Colors>(options.colors, args...);
        P::template set<names::TextFieldColors>(options.colors, args...);
        P::template set<names::Decorator>(options.decorator, args...);
        richTextEditor(Kind, P::template arg<names::State>(args...), P::template get<names::Modifier>(Modifier{}, args...), std::move(options),
                       site);
    }
};
} // namespace detail
inline constexpr Composable<detail::RichTextEditorImpl<detail::RichEditorKind::Filled>> RichTextEditor{};
inline constexpr Composable<detail::RichTextEditorImpl<detail::RichEditorKind::Outlined>> OutlinedRichTextEditor{};
inline constexpr Composable<detail::RichTextEditorImpl<detail::RichEditorKind::Basic>> BasicRichTextEditor{};

// RichText(state, modifier, color, fontSize, …, style): the state's text shown as a Text, its links
// opened with LocalUriHandler. BasicRichText as BasicText.
namespace detail {
struct RichTextImpl {
    template <class... A>
    static void call(CallSite site, const RichTextState& state, A&&... args) {
        TextImpl::call(site, state.annotatedString(), std::forward<A>(args)...);
    }
    template <class S, class... A>
        requires std::is_same_v<TagOf<S>, names::State>
    static void call(CallSite site, S&& state, A&&... args) {
        call(site, state.value, std::forward<A>(args)...);
    }
};
struct BasicRichTextImpl {
    using P = Params<names::State, names::Modifier, names::Style>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::template check<A...>());
        BasicText(P::template arg<names::State>(args...).annotatedString(), P::template get<names::Modifier>(Modifier{}, args...),
                  P::template get<names::Style>(TextStyle{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::RichTextImpl> RichText{};
inline constexpr Composable<detail::BasicRichTextImpl> BasicRichText{};

} // namespace kompoz
