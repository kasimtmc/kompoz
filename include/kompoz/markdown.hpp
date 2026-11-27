#pragma once

// Markdown shown as composables, with the API of multiplatform-markdown-renderer (mikepenz's, Compose
// Multiplatform's most used): Compose has none.
//
//   Markdown(
//       content = """
//           # Notlar
//           Bir **kalın** söz, bir [bağlantı](https://kompoz.dev) ve `kod`.
//           - [x] bitti
//           - [ ] sırada
//       """.trimIndent(),
//       modifier = Modifier.fillMaxWidth().verticalScroll(rememberScrollState()),
//   )
//
// CommonMark and GitHub's Markdown (markdown_parser.hpp): headings, paragraphs, emphasis, links
// (opened with LocalUriHandler), images (AsyncImage: files, assets, http(s)), block quotes, lists
// (nested, ordered from their start, task items with checkboxes), fenced and indented code, thematic
// breaks, tables (their columns aligned) and inline HTML's common tags. Its looks: colors
// (markdownColor), typography (markdownTypography) and padding (markdownPadding) from the theme
// where not given; each kind of block drawn by a component of your own (markdownComponents).

#include "markdown_parser.hpp"
#include "named.hpp"
#include "runtime.hpp"
#include "text.hpp"
#include "widgets.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace kompoz {

// The colors (mikepenz's MarkdownColors; markdownColor(…) fills the ones not given from the theme).
struct MarkdownColors {
    Color text = Color::Unspecified;                  // onSurface
    Color codeText = Color::Unspecified;              // a code block's: onSurfaceVariant
    Color inlineCodeText = Color::Unspecified;        // `code`'s: the text's
    Color linkText = Color::Unspecified;              // primary
    Color codeBackground = Color::Unspecified;        // onSurface at 8%
    Color inlineCodeBackground = Color::Unspecified;  // onSurface at 10%
    Color dividerColor = Color::Unspecified;          // outlineVariant
    Color tableText = Color::Unspecified;             // the text's
    Color tableBackground = Color::Unspecified;       // surfaceContainerLow
};

// The type styles (mikepenz's MarkdownTypography): headings, body text, code, quotes, list items,
// links, tables.
struct MarkdownTypography {
    TextStyle h1, h2, h3, h4, h5, h6;
    TextStyle text, code, inlineCode, quote, paragraph, ordered, bullet, list;
    TextLinkStyles textLink;
    TextStyle table;
};

// The room around blocks (mikepenz's MarkdownPadding), dp.
struct MarkdownPadding {
    float block = 8;            // between blocks
    float list = 0;             // around a list
    float listItemTop = 4, listItemBottom = 0;
    float listIndent = 8;       // a nested list's in from its item's text
    PaddingValues codeBlock{16, 16, 16, 16};
    PaddingValues blockQuote{16, 0, 16, 0};  // (start, top, end, bottom)
    float blockQuoteText = 4;   // a quote's text, below and above its blocks
};

// What a component draws: the block, the source it came from, the typography.
struct MarkdownComponentModel {
    std::string content;                  // the Markdown it was read from
    const markdown::Block* node = nullptr;
    MarkdownTypography typography;
    std::string url, alt;                 // an image's
    std::shared_ptr<const void> context_;  // (what MarkdownBlocks draws with)
};
using MarkdownComponent = std::function<void(const MarkdownComponentModel&)>;

// Your own composables for kinds of blocks (mikepenz's MarkdownComponents); the default for those
// not given. Inside one, MarkdownBlocks(model) draws a block's children as Markdown does.
struct MarkdownComponents {
    MarkdownComponent paragraph, heading1, heading2, heading3, heading4, heading5, heading6, blockQuote, codeFence, codeBlock,
        orderedList, unorderedList, image, horizontalRule, table, checkbox;
};

namespace detail {
MarkdownColors markdownColor(const MarkdownColors& given);
MarkdownTypography markdownTypography(const MarkdownTypography& given, uint32_t givenMask);
void markdown(const std::string& content, const MarkdownColors& colors, const MarkdownTypography& typography, const Modifier& modifier,
              const MarkdownPadding& padding, const MarkdownComponents& components, CallSite site);
void markdownBlocks(const MarkdownComponentModel& model, CallSite site);
AnnotatedString markdownAnnotated(const std::vector<markdown::Inline>& inlines);

// markdownColor(text = …, codeText = …, …): the colors given, the theme's for the others.
struct MarkdownColorImpl {
    using P = Params<names::Text, names::CodeText, names::InlineCodeText, names::LinkText, names::CodeBackground, names::InlineCodeBackground,
                     names::DividerColor, names::TableText, names::TableBackground>;
    template <class... A>
    static MarkdownColors call(CallSite, A&&... args) {
        static_assert(P::template check<A...>());
        MarkdownColors c;
        P::template set<names::Text>(c.text, args...);
        P::template set<names::CodeText>(c.codeText, args...);
        P::template set<names::InlineCodeText>(c.inlineCodeText, args...);
        P::template set<names::LinkText>(c.linkText, args...);
        P::template set<names::CodeBackground>(c.codeBackground, args...);
        P::template set<names::InlineCodeBackground>(c.inlineCodeBackground, args...);
        P::template set<names::DividerColor>(c.dividerColor, args...);
        P::template set<names::TableText>(c.tableText, args...);
        P::template set<names::TableBackground>(c.tableBackground, args...);
        return markdownColor(c);
    }
};
// markdownTypography(h1 = …, text = …, …): the styles given, the theme's for the others.
struct MarkdownTypographyImpl {
    using P = Params<names::H1, names::H2, names::H3, names::H4, names::H5, names::H6, names::Text, names::Code, names::InlineCode, names::Quote,
                     names::Paragraph, names::Ordered, names::Bullet, names::List, names::TextLink, names::Table>;
    template <class... A>
    static MarkdownTypography call(CallSite, A&&... args) {
        static_assert(P::template check<A...>());
        MarkdownTypography t;
        uint32_t mask = 0;
        auto take = [&]<class Tag>(Tag*, auto& field, int bit) {
            if constexpr (P::template given<Tag, A...>()) {
                const auto& v = P::template arg<Tag>(args...);
                // (a theme's type style is a Font here: Compose's TextStyle of it)
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(v)>, Font> && std::is_same_v<std::remove_cvref_t<decltype(field)>, TextStyle>)
                    field = TextStyle{.font = v};
                else
                    field = v;
                mask |= 1u << bit;
            }
        };
        take((names::H1*)nullptr, t.h1, 0), take((names::H2*)nullptr, t.h2, 1), take((names::H3*)nullptr, t.h3, 2);
        take((names::H4*)nullptr, t.h4, 3), take((names::H5*)nullptr, t.h5, 4), take((names::H6*)nullptr, t.h6, 5);
        take((names::Text*)nullptr, t.text, 6), take((names::Code*)nullptr, t.code, 7), take((names::InlineCode*)nullptr, t.inlineCode, 8);
        take((names::Quote*)nullptr, t.quote, 9), take((names::Paragraph*)nullptr, t.paragraph, 10), take((names::Ordered*)nullptr, t.ordered, 11);
        take((names::Bullet*)nullptr, t.bullet, 12), take((names::List*)nullptr, t.list, 13), take((names::TextLink*)nullptr, t.textLink, 14);
        take((names::Table*)nullptr, t.table, 15);
        return markdownTypography(t, mask);
    }
};
// markdownPadding(block = …, list = …, …).
struct MarkdownPaddingImpl {
    using P = Params<names::Block, names::List, names::ListItemTop, names::ListItemBottom, names::ListIndent, names::CodeBlock,
                     names::BlockQuote, names::BlockQuoteText>;
    template <class... A>
    static MarkdownPadding call(CallSite, A&&... args) {
        static_assert(P::template check<A...>());
        MarkdownPadding p;
        P::template set<names::Block>(p.block, args...);
        P::template set<names::List>(p.list, args...);
        P::template set<names::ListItemTop>(p.listItemTop, args...);
        P::template set<names::ListItemBottom>(p.listItemBottom, args...);
        P::template set<names::ListIndent>(p.listIndent, args...);
        P::template set<names::CodeBlock>(p.codeBlock, args...);
        P::template set<names::BlockQuote>(p.blockQuote, args...);
        P::template set<names::BlockQuoteText>(p.blockQuoteText, args...);
        return p;
    }
};
// markdownComponents(codeBlock = { model -> … }, …).
struct MarkdownComponentsImpl {
    using P = Params<names::Paragraph, names::Heading1, names::Heading2, names::Heading3, names::Heading4, names::Heading5, names::Heading6,
                     names::BlockQuote, names::CodeFence, names::CodeBlock, names::OrderedList, names::UnorderedList, names::Image,
                     names::HorizontalRule, names::Table, names::Checkbox>;
    template <class... A>
    static MarkdownComponents call(CallSite, A&&... args) {
        static_assert(P::template check<A...>());
        MarkdownComponents c;
        P::template set<names::Paragraph>(c.paragraph, args...);
        P::template set<names::Heading1>(c.heading1, args...);
        P::template set<names::Heading2>(c.heading2, args...);
        P::template set<names::Heading3>(c.heading3, args...);
        P::template set<names::Heading4>(c.heading4, args...);
        P::template set<names::Heading5>(c.heading5, args...);
        P::template set<names::Heading6>(c.heading6, args...);
        P::template set<names::BlockQuote>(c.blockQuote, args...);
        P::template set<names::CodeFence>(c.codeFence, args...);
        P::template set<names::CodeBlock>(c.codeBlock, args...);
        P::template set<names::OrderedList>(c.orderedList, args...);
        P::template set<names::UnorderedList>(c.unorderedList, args...);
        P::template set<names::Image>(c.image, args...);
        P::template set<names::HorizontalRule>(c.horizontalRule, args...);
        P::template set<names::Table>(c.table, args...);
        P::template set<names::Checkbox>(c.checkbox, args...);
        return c;
    }
};

// Markdown(content, colors, typography, modifier, padding, components).
struct MarkdownImpl {
    using P = Params<names::Content, names::Colors, names::Typography, names::Modifier, names::Padding, names::Components>;
    static constexpr bool takesContentByName = true;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::template check<A...>());
        MarkdownColors colors;
        if constexpr (P::template given<names::Colors, A...>()) colors = P::template arg<names::Colors>(args...);
        else colors = markdownColor({});
        MarkdownTypography typography;
        if constexpr (P::template given<names::Typography, A...>()) typography = P::template arg<names::Typography>(args...);
        else typography = markdownTypography({}, 0);
        markdown(std::string(P::template arg<names::Content>(args...)), colors, typography, P::template get<names::Modifier>(Modifier{}, args...),
                 P::template get<names::Padding>(MarkdownPadding{}, args...), P::template get<names::Components>(MarkdownComponents{}, args...),
                 site);
    }
};
} // namespace detail

inline constexpr Composable<detail::MarkdownColorImpl> markdownColor{};
inline constexpr Composable<detail::MarkdownTypographyImpl> markdownTypography{};
inline constexpr Composable<detail::MarkdownPaddingImpl> markdownPadding{};
inline constexpr Composable<detail::MarkdownComponentsImpl> markdownComponents{};
inline constexpr Composable<detail::MarkdownImpl> Markdown{};

// A block's children drawn as Markdown draws them (a component of your own around them).
inline void MarkdownBlocks(const MarkdownComponentModel& model, CallSite site = CallSite::current()) { detail::markdownBlocks(model, site); }
// Inlines as styled text (bold, italic, code, links, …), for a component's own Text.
inline AnnotatedString buildMarkdownAnnotatedString(const std::vector<markdown::Inline>& inlines) {
    return detail::markdownAnnotated(inlines);
}

} // namespace kompoz
