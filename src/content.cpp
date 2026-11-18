#include <kompoz/content.hpp>

#include <algorithm>
#include <cctype>

namespace kompoz {

namespace {
// The type without its parameters ("text/plain;charset=utf-8" -> "text/plain"), lower case.
std::string bareType(std::string_view type) {
    std::string bare(type.substr(0, type.find(';')));
    while (!bare.empty() && bare.back() == ' ') bare.pop_back();
    std::ranges::transform(bare, bare.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return bare;
}
} // namespace

bool mimeTypeMatches(std::string_view type, std::string_view wanted) {
    std::string have = bareType(type), want = bareType(wanted);
    if (want == "*/*") return true;
    if (want.size() > 2 && want.ends_with("/*")) return have.starts_with(want.substr(0, want.size() - 1));
    return have == want;
}

bool ClipMetadata::hasMimeType(std::string_view mimeType) const {
    return std::ranges::any_of(mimeTypes, [&](const std::string& type) { return mimeTypeMatches(type, mimeType); });
}

ClipEntry ClipEntry::withPlainText(std::string text) {
    ClipEntry entry;
    entry.items.push_back({.text = std::move(text)});
    entry.clipMetadata.mimeTypes = {"text/plain"};
    return entry;
}

ClipEntry ClipEntry::withHtmlText(std::string text, std::string html) {
    ClipEntry entry;
    entry.items.push_back({.text = std::move(text), .htmlText = std::move(html)});
    entry.clipMetadata.mimeTypes = {"text/html"};
    return entry;
}

std::optional<std::string> ClipEntry::readPlainText() const {
    std::optional<std::string> text;
    for (const Item& item : items) {
        if (!item.text) continue;
        if (!text) {
            text = *item.text;
        } else {
            *text += '\n';
            *text += *item.text;
        }
    }
    return text;
}

std::optional<TransferableContent> TransferableContent::consume(const std::function<bool(const Item&)>& predicate) const {
    std::vector<Item> left;
    for (const Item& item : clipEntry.items)
        if (!predicate(item)) left.push_back(item);
    if (left.empty()) return std::nullopt;
    if (left.size() == clipEntry.items.size()) return *this;
    // (What it holds is told as it was, as Android copies the clip's description.)
    TransferableContent rest = *this;
    rest.clipEntry.items = std::move(left);
    return rest;
}

} // namespace kompoz
