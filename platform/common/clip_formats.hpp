// A ClipEntry as the clipboards and drags that go by MIME types hold it (Wayland's, X11's, the
// web's; Windows and macOS name their formats otherwise and map them to these): written as
// each type it can be had in, read back from those an offer has.
#pragma once

#include <kompoz/content.hpp>

#include <functional>
#include <string>
#include <vector>

namespace kompoz::platform {

struct ClipFormat {
    std::string type;   // a MIME type (and X11's names for text: UTF8_STRING, STRING, TEXT)
    std::string bytes;  // what it is as that type
};

// What `entry` is offered as, best first: its text (UTF-8, under each name text goes by), its
// HTML, its URIs (a "text/uri-list"), and the content it carries as bytes (an image's PNG).
std::vector<ClipFormat> clipFormats(const ClipEntry& entry);

// A clip read from an offer that has `types`: a "text/uri-list" (or a file manager's copied
// files) an item for each URI; else its text and HTML an item, an image's bytes another. `read`
// gives what the offer holds as a type, asked for only those it takes.
ClipEntry clipFromFormats(const std::vector<std::string>& types, const std::function<std::string(const std::string& type)>& read);

// What such an offer holds, told by its types without reading it (the types clipFromFormats'
// clip would have).
ClipMetadata clipMetadataOf(const std::vector<std::string>& types);

// The image type a file's name says (its extension, as a file manager's drag doesn't tell it);
// empty for other files.
std::string imageTypeOfName(std::string_view name);
// Files as clip items (file:// URIs), the clip telling "text/uri-list" and the images' types among them.
ClipEntry clipOfFiles(const std::vector<std::string>& uris);

// The paths of a clip's file:// URIs (unescaped).
std::vector<std::string> clipFiles(const ClipEntry& entry);

// A path as a file:// URI (escaped).
std::string fileUri(const std::string& path);

} // namespace kompoz::platform
