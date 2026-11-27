#pragma once

// What a file dialog is asked to show and what it gives back: FileKit's types (filekit.hpp) the
// platform reads (KompozHost::Callbacks::fileDialog).

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace kompoz {

// What files the picker offers: any file or those of some extensions (File), or images, videos or
// both (from the photo picker on Android; by their extensions elsewhere).
struct FileKitType {
    enum class Kind { File, Image, Video, ImageAndVideo };
    Kind kind = Kind::File;
    std::vector<std::string> extensions;  // File's, without the dot; none: any file

    static FileKitType File() { return {}; }
    template <class... S>
        requires(sizeof...(S) > 0 && (std::is_convertible_v<const S&, std::string_view> && ...))
    static FileKitType File(const S&... extensions) {
        return {Kind::File, {std::string(std::string_view(extensions))...}};
    }
    template <class R>
        requires requires(const R& r) { std::string(*std::begin(r)); }
    static FileKitType File(const R& extensions) {
        FileKitType t;
        for (const auto& e : extensions) t.extensions.emplace_back(e);
        return t;
    }
    static const FileKitType Image, Video, ImageAndVideo;
    friend bool operator==(const FileKitType&, const FileKitType&) = default;
};
inline const FileKitType FileKitType::Image{FileKitType::Kind::Image, {}};
inline const FileKitType FileKitType::Video{FileKitType::Kind::Video, {}};
inline const FileKitType FileKitType::ImageAndVideo{FileKitType::Kind::ImageAndVideo, {}};

// Where macOS's panels may make folders, and whether they open what an alias points to.
struct FileKitMacOSSettings {
    bool canCreateDirectories = true;
    std::optional<bool> resolvesAliases;
};
namespace detail {

// What the platform is asked to show (KompozHost::Callbacks::fileDialog).
struct FileDialogRequest {
    enum class Kind { OpenFiles, OpenDirectory, SaveFile };
    Kind kind = Kind::OpenFiles;
    FileKitType type;              // OpenFiles'
    bool multiple = false;         // OpenFiles'
    std::optional<int> maxItems;   // OpenFiles' with multiple
    std::optional<std::string> title;
    std::string directory;         // where it opens (a path); empty: where the platform likes
    std::string suggestedName;     // SaveFile's: the name without its extension
    std::optional<std::string> defaultExtension;    // SaveFile's
    std::vector<std::string> allowedExtensions;      // SaveFile's (a hint the dialog's filter shows)
    FileKitMacOSSettings macOS;
};
// What it gave back: the files' paths (or URIs), none if it was closed without a choice; or why it
// couldn't be shown.
struct FileDialogResult {
    std::vector<std::string> files;
    std::optional<std::string> error;
};

} // namespace detail

} // namespace kompoz
