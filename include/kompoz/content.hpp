#pragma once

// Content that goes from one app to another (Compose foundation's content package and ui's
// ClipEntry): what a clipboard holds, a drag carries or a keyboard sends — text, HTML, images,
// files. Modifier::contentReceiver (layout.hpp) takes it where it lands:
//
//   Box(Modifier().contentReceiver([](const TransferableContent& content) -> std::optional<TransferableContent> {
//       if (!content.hasMediaType(MediaType::Image)) return content;  // not ours: on to the field
//       return content.consume([](const ClipEntry::Item& item) { return item.uri && (show(*item.uri), true); });
//   }), [] { TextField(state); });

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kompoz {

// A kind of content (Compose's MediaType): a MIME type, as Android has them; a wildcard one
// ("image/*", "*/*") stands for all its kinds.
struct MediaType {
    std::string representation;

    static const MediaType Text;       // any text: plain, HTML, styled
    static const MediaType PlainText;  // text with nothing in it but its characters
    static const MediaType HtmlText;
    static const MediaType Image;      // any image: PNG, JPEG, GIF...
    static const MediaType All;        // anything
    friend bool operator==(const MediaType&, const MediaType&) = default;
};
inline const MediaType MediaType::Text{"text/*"};
inline const MediaType MediaType::PlainText{"text/plain"};
inline const MediaType MediaType::HtmlText{"text/html"};
inline const MediaType MediaType::Image{"image/*"};
inline const MediaType MediaType::All{"*/*"};

// Whether `type` is one of the kinds `wanted` stands for (Android's ClipDescription.compareMimeTypes):
// "*/*" takes any, "image/*" any image, else the same type (case aside, parameters as ";charset=utf-8" aside).
bool mimeTypeMatches(std::string_view type, std::string_view wanted);

// What a clip holds, told without reading it (Compose's ClipMetadata; Android's ClipDescription):
// its items' MIME types and a label.
struct ClipMetadata {
    std::vector<std::string> mimeTypes;
    std::string label;
    // A password: its source asked for it not to be kept or shown (Android's EXTRA_IS_SENSITIVE; a
    // Linux password manager's x-kde-passwordManagerHint type).
    bool isSensitive = false;
    // Whether one of its types is one `mimeType` (a wildcard one too) stands for.
    bool hasMimeType(std::string_view mimeType) const;
    friend bool operator==(const ClipMetadata&, const ClipMetadata&) = default;
};

// What is on the clipboard, dragged or sent by a keyboard (Compose's ClipEntry; Android's ClipData):
// one item or more, each text, HTML (with its text), a URI (a file's, a content provider's, a web
// address), or content carried as bytes (an image a desktop clipboard holds, PNG).
struct ClipEntry {
    struct Item {
        std::optional<std::string> text;
        std::optional<std::string> htmlText;  // HTML markup (its plain form in `text`)
        std::optional<std::string> uri;
        std::string mimeType;       // of `data`
        std::vector<uint8_t> data;  // the content itself, where the platform gave it and no URI
        friend bool operator==(const Item&, const Item&) = default;
    };
    std::vector<Item> items;
    ClipMetadata clipMetadata;

    static ClipEntry withPlainText(std::string text);
    // HTML, with its plain text for an app that only takes that.
    static ClipEntry withHtmlText(std::string text, std::string html);
    // The items' text, a line between each (empty if none has any).
    std::string getPlainText() const { return readPlainText().value_or(std::string()); }
    // The same, none if no item has text (Compose's ClipEntry.readPlainText).
    std::optional<std::string> readPlainText() const;
    friend bool operator==(const ClipEntry&, const ClipEntry&) = default;
};

// What a platform alone has to say of a TransferableContent (Compose's PlatformTransferableContent):
// on Android, the link an input method sent with its content (InputConnection.commitContent).
struct PlatformTransferableContent {
    std::optional<std::string> linkUri;
    friend bool operator==(const PlatformTransferableContent&, const PlatformTransferableContent&) = default;
};

// Content that arrived (Compose's TransferableContent): the clip, what it holds, and where it
// came from.
struct TransferableContent {
    // Where it came from (Compose's TransferableContent.Source).
    enum class Source {
        Keyboard,     // the soft keyboard (an image or GIF it inserts)
        DragAndDrop,  // dropped
        Clipboard,    // pasted (the menu's Paste, Ctrl+V)
    };
    ClipEntry clipEntry;
    ClipMetadata clipMetadata;
    Source source = Source::Clipboard;
    std::optional<PlatformTransferableContent> platformTransferableContent;
    using Item = ClipEntry::Item;

    // Whether it has an item of `mediaType` (Compose's hasMediaType).
    bool hasMediaType(const MediaType& mediaType) const { return clipMetadata.hasMimeType(mediaType.representation); }
    // What is left once `predicate` took the items it handles (true: handled here): none if it
    // took them all, this one as it is if it took none (Compose's consume, Android's).
    std::optional<TransferableContent> consume(const std::function<bool(const Item&)>& predicate) const;
    friend bool operator==(const TransferableContent&, const TransferableContent&) = default;
};

// What a contentReceiver does (Compose's ReceiveContentListener). onReceive takes content that
// arrived on it or inside it, and gives back what it left (none: all of it taken), which goes on
// to the contentReceiver around it, then to where it was going (a text field inserts its text).
// The drag callbacks: a drag began or ended anywhere (all of them hear it), went over this one
// (one inside it is over it too) or left it.
struct ReceiveContentListener {
    virtual ~ReceiveContentListener() = default;
    virtual void onDragStart() {}
    virtual void onDragEnd() {}
    virtual void onDragEnter() {}
    virtual void onDragExit() {}
    virtual std::optional<TransferableContent> onReceive(TransferableContent transferableContent) = 0;
};

} // namespace kompoz
