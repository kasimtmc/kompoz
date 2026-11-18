#include "clip_formats.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace kompoz::platform {

namespace {
// The names text goes by, best first: UTF-8's (X11's UTF8_STRING before a "text/plain" of no
// told charset), then X11's Latin-1 STRING and TEXT.
constexpr const char* kTextTypes[] = {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "STRING", "TEXT"};

bool has(const std::vector<std::string>& types, std::string_view type) { return std::ranges::find(types, type) != types.end(); }

// The text of `bytes` as UTF-8: UTF-16 with its byte order mark (as some browsers write HTML)
// turned to it, a trailing NUL dropped.
std::string utf8Of(std::string bytes) {
    bool le = bytes.size() >= 2 && uint8_t(bytes[0]) == 0xFF && uint8_t(bytes[1]) == 0xFE;
    bool be = bytes.size() >= 2 && uint8_t(bytes[0]) == 0xFE && uint8_t(bytes[1]) == 0xFF;
    if (le || be) {
        std::string out;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            auto unit = [&](size_t at) {
                return le ? uint32_t(uint8_t(bytes[at])) | uint32_t(uint8_t(bytes[at + 1])) << 8
                          : uint32_t(uint8_t(bytes[at])) << 8 | uint32_t(uint8_t(bytes[at + 1]));
            };
            uint32_t c = unit(i);
            if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes.size()) {  // a surrogate pair
                c = 0x10000 + ((c - 0xD800) << 10) + (unit(i + 2) - 0xDC00);
                i += 2;
            }
            if (c < 0x80) {
                out += char(c);
            } else if (c < 0x800) {
                out += char(0xC0 | c >> 6), out += char(0x80 | (c & 0x3F));
            } else if (c < 0x10000) {
                out += char(0xE0 | c >> 12), out += char(0x80 | (c >> 6 & 0x3F)), out += char(0x80 | (c & 0x3F));
            } else {
                out += char(0xF0 | c >> 18), out += char(0x80 | (c >> 12 & 0x3F)), out += char(0x80 | (c >> 6 & 0x3F)), out += char(0x80 | (c & 0x3F));
            }
        }
        bytes = std::move(out);
    }
    while (!bytes.empty() && bytes.back() == '\0') bytes.pop_back();
    return bytes;
}

// The URIs on a "text/uri-list" (a line each, "#" lines comments), or of a file manager's copied
// files ("copy" or "cut", then a URI a line).
std::vector<std::string> urisIn(const std::string& list) {
    std::vector<std::string> uris;
    for (size_t at = 0; at < list.size();) {
        size_t end = std::min(list.find('\n', at), list.size());
        std::string line = list.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line == "copy" || line == "cut") continue;
        uris.push_back(std::move(line));
    }
    return uris;
}

const char* kUriTypes[] = {"text/uri-list", "x-special/gnome-copied-files"};

// The image type an offer has, PNG first.
std::string imageType(const std::vector<std::string>& types) {
    if (has(types, "image/png")) return "image/png";
    for (const std::string& t : types)
        if (t.starts_with("image/")) return t;
    return {};
}
} // namespace

std::vector<ClipFormat> clipFormats(const ClipEntry& entry) {
    std::vector<ClipFormat> formats;
    if (std::optional<std::string> text = entry.readPlainText())
        for (const char* type : kTextTypes) formats.push_back({type, *text});
    for (const ClipEntry::Item& item : entry.items)
        if (item.htmlText) {
            formats.push_back({"text/html", *item.htmlText});
            break;
        }
    std::string uris;
    for (const ClipEntry::Item& item : entry.items)
        if (item.uri) uris += *item.uri + "\r\n";
    if (!uris.empty()) formats.push_back({"text/uri-list", uris});
    for (const ClipEntry::Item& item : entry.items)
        if (!item.data.empty() && !item.mimeType.empty() &&
            std::ranges::none_of(formats, [&](const ClipFormat& f) { return f.type == item.mimeType; }))
            formats.push_back({item.mimeType, std::string(item.data.begin(), item.data.end())});
    return formats;
}

ClipEntry clipFromFormats(const std::vector<std::string>& types, const std::function<std::string(const std::string& type)>& read) {
    for (const char* type : kUriTypes)
        if (has(types, type))
            if (std::vector<std::string> uris = urisIn(read(type)); !uris.empty()) return clipOfFiles(uris);  // (a file manager's text is the same files)
    ClipEntry entry;
    entry.clipMetadata = clipMetadataOf(types);
    ClipEntry::Item text;
    for (const char* type : kTextTypes)
        if (has(types, type)) {
            text.text = utf8Of(read(type));
            break;
        }
    if (has(types, "text/html")) text.htmlText = utf8Of(read("text/html"));
    if (text.text || text.htmlText) entry.items.push_back(std::move(text));
    if (std::string type = imageType(types); !type.empty()) {
        std::string bytes = read(type);
        if (!bytes.empty()) entry.items.push_back({.mimeType = type, .data = std::vector<uint8_t>(bytes.begin(), bytes.end())});
    }
    return entry;
}

ClipMetadata clipMetadataOf(const std::vector<std::string>& types) {
    ClipMetadata metadata;
    if (std::ranges::any_of(kUriTypes, [&](const char* t) { return has(types, t); })) {
        metadata.mimeTypes.push_back("text/uri-list");
        return metadata;
    }
    if (std::ranges::any_of(kTextTypes, [&](const char* t) { return has(types, t); })) metadata.mimeTypes.push_back("text/plain");
    if (has(types, "text/html")) metadata.mimeTypes.push_back("text/html");
    if (std::string type = imageType(types); !type.empty()) metadata.mimeTypes.push_back(type);
    return metadata;
}

std::string imageTypeOfName(std::string_view name) {
    size_t dot = name.rfind('.');
    if (dot == std::string_view::npos) return {};
    std::string ext(name.substr(dot + 1));
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    static const std::pair<const char*, const char*> kinds[] = {{"png", "image/png"},   {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
                                                                {"gif", "image/gif"},   {"webp", "image/webp"}, {"bmp", "image/bmp"},
                                                                {"heic", "image/heic"}, {"avif", "image/avif"}, {"svg", "image/svg+xml"}};
    for (auto [e, type] : kinds)
        if (ext == e) return type;
    return {};
}

ClipEntry clipOfFiles(const std::vector<std::string>& uris) {
    ClipEntry entry;
    entry.clipMetadata.mimeTypes = {"text/uri-list"};
    for (const std::string& uri : uris) {
        entry.items.push_back({.uri = uri});
        std::string type = imageTypeOfName(uri.substr(0, uri.find_first_of("?#")));
        if (!type.empty() && !has(entry.clipMetadata.mimeTypes, type)) entry.clipMetadata.mimeTypes.push_back(type);
    }
    return entry;
}

std::vector<std::string> clipFiles(const ClipEntry& entry) {
    std::vector<std::string> files;
    for (const ClipEntry::Item& item : entry.items) {
        if (!item.uri || !item.uri->starts_with("file://")) continue;
        std::string line = item.uri->substr(7);
        if (size_t slash = line.find('/'); slash != 0 && slash != std::string::npos) line.erase(0, slash);  // (a host before the path)
        std::string path;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '%' && i + 2 < line.size() && std::isxdigit(uint8_t(line[i + 1])) && std::isxdigit(uint8_t(line[i + 2]))) {
                path += char(std::stoi(line.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else {
                path += line[i];
            }
        }
        if (path.size() > 2 && path[0] == '/' && path[2] == ':') path.erase(0, 1);  // Windows' "/C:/..."
        if (!path.empty()) files.push_back(path);
    }
    return files;
}

// Windows' backslashes and drive letters turned around, anything but letters, digits and
// -._~/: percent-encoded.
std::string fileUri(const std::string& path) {
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    if (p.size() > 1 && p[1] == ':') p.insert(p.begin(), '/');
    std::string out = "file://";
    for (unsigned char c : p) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/' || c == ':') out += char(c);
        else out += std::format("%{:02X}", c);
    }
    return out;
}

} // namespace kompoz::platform
