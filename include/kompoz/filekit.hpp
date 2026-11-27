#pragma once

// Files picked, saved and opened through the system's own dialogs, with the API of FileKit (Compose
// Multiplatform's file picker: io.github.vinceglb.filekit). Compose has none.
//
//   var text by remember { mutableStateOf("") }
//   val scope = rememberCoroutineScope()
//   val open = rememberFilePickerLauncher(type = FileKitType.File("txt", "md")) { file ->
//       if (file != null) scope.launch { text = file.readString() }
//   }
//   val save = rememberFileSaverLauncher { file ->
//       if (file != null) scope.launch { file.writeString(text) }
//   }
//   Button(onClick = { open.launch() }) { Text("Aç") }
//   Button(onClick = { save.launch(suggestedName = "notlar", defaultExtension = "txt") }) { Text("Kaydet") }
//
// A launcher shows its dialog over the app's window (a sheet of it on macOS) and calls the block
// back with what was chosen, or null when the dialog was closed without it: one PlatformFile
// (FileKitMode.Single), a list of them (Multiple), or the state as files come (the WithState
// modes). rememberDirectoryPickerLauncher picks a folder. In a coroutine, FileKit.openFilePicker(…),
// openFileSaver(…) and openDirectoryPicker(…) give the same back (and throw a
// FileKitDialogException where the dialog can't be shown).
//
// The dialogs are the platform's: the desktop portal's on Linux (GNOME's, KDE's; zenity or kdialog
// where there is no portal), the common item dialogs on Windows, NSOpenPanel / NSSavePanel on macOS,
// the system's document picker on Android (its photo picker for images and videos) and the
// browser's on the web. On Android what is picked is a content:// URI, read and written as a file
// all the same; the app keeps access to it after a restart once bookmarkData() was taken.

#include "coroutines.hpp"
#include "file_dialog.hpp"
#include "named.hpp"
#include "runtime.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace kompoz {

class KompozHost;

// A file or a folder (FileKit's PlatformFile): a path, or on Android a content:// URI the system's
// picker gave (a file:// one is taken as its path). What tells about it answers at once; reading
// and writing are done away from the UI thread, co_await-ed (Kotlin's suspend functions).
class PlatformFile {
public:
    PlatformFile() = default;
    explicit PlatformFile(std::string path);
    PlatformFile(const PlatformFile& parent, std::string_view child);  // a child of a folder (its name)

    const std::string& path() const { return path_; }
    std::string name() const;                  // "notes.txt" (a content URI's display name)
    std::string extension() const;            // "txt"; empty if it has none
    std::string nameWithoutExtension() const;  // "notes"
    int64_t size() const;                      // bytes; -1 when not known
    bool exists() const;
    bool isDirectory() const;
    bool isRegularFile() const;
    bool isAbsolute() const;
    std::optional<PlatformFile> parent() const;
    std::string absolutePath() const;
    PlatformFile absoluteFile() const { return PlatformFile(absolutePath()); }
    PlatformFile resolve(std::string_view relative) const;  // a path under this folder ("a/b.txt")
    PlatformFile operator/(std::string_view child) const { return resolve(child); }
    // A folder's files and folders (a picked folder's on Android too); empty for a file.
    std::vector<PlatformFile> list() const;
    std::optional<std::string> mimeType() const;  // "text/plain"; none when not known
    std::chrono::system_clock::time_point lastModified() const;  // the epoch when not known
    std::string toString() const { return path_; }

    // Kept to open it again in a later run (FileKit's bookmark): on Android the picked URI's
    // access is kept by the system too; elsewhere its path.
    std::vector<uint8_t> bookmarkData() const;
    static PlatformFile fromBookmarkData(const std::vector<uint8_t>& data);

    // ---- co_await-ed: done on a worker thread (they throw std::runtime_error where they can't) ----
    BackgroundAwaiter<std::vector<uint8_t>> readBytes() const;
    BackgroundAwaiter<std::string> readString() const;  // UTF-8
    BackgroundAwaiter<void> write(std::vector<uint8_t> bytes) const;  // made (with its folder) or replaced
    BackgroundAwaiter<void> write(const PlatformFile& source) const;  // another file's bytes
    BackgroundAwaiter<void> writeString(std::string text) const;

    // ---- done at once, where they're called ----
    void copyTo(const PlatformFile& target) const;  // into a folder (under its name) or as the file
    void createDirectories() const;
    void delete_(bool mustExist = true) const;  // Kotlin's delete(): a file, or a folder with all it holds

    friend bool operator==(const PlatformFile& a, const PlatformFile& b) { return a.path_ == b.path_; }

private:
    void writeBytes(const std::vector<uint8_t>& bytes) const;
    std::string resolvedUri() const;  // a child made since it was resolved: its URI now

    std::string path_;
    std::string knownName_;  // a content URI's name as its folder's list() gave it (none asked again)
    // A picked folder's child that isn't there yet (resolve on Android): made where it is written.
    std::shared_ptr<PlatformFile> pendingParent_;
    std::string pendingName_;
};

// What a dialog that couldn't be shown says (none on this platform, the desktop's portal failing).
struct FileKitDialogException : std::runtime_error {
    using std::runtime_error::runtime_error;
    std::string message() const { return what(); }  // (Kotlin's Throwable.message)
};

// How many files are picked, and how they come: one (Single: a PlatformFile, or null), several
// (Multiple(maxItems): a list, or null; at most maxItems where it is given, 1 to 50), or as they are
// taken (the WithState modes: FileKitPickerState's Started, Progress, then Completed).
struct FileKitMode {
    struct SingleMode {};
    struct SingleWithStateMode {};
    struct Multiple {
        std::optional<int> maxItems;
    };
    struct MultipleWithState {
        std::optional<int> maxItems;
    };
    static constexpr SingleMode Single{};
    static constexpr SingleWithStateMode SingleWithState{};
};

// What a WithState mode's block is given as the picked files are taken: Started (how many), Progress
// (those taken so far), then Completed (the result: the PlatformFile, or the list), or Cancelled
// (closed without a choice), or Failed. `when (state) { is FileKitPickerState.Completed -> state.result }`.
struct FileKitPickerState {
    struct Started {
        int total = 0;
    };
    struct Progress {
        std::vector<PlatformFile> processed;
        int total = 0;
    };
    template <class R>
    struct CompletedOf {
        R result;
    };
    // (the name `is` checks: the Completed of the state it is asked of)
    struct Completed {
        template <class State>
        using kompoz_alternative = CompletedOf<typename State::Result>;
    };
    struct Failed {
        FileKitDialogException cause{"failed"};
    };
    struct Cancelled {};
};
template <class R>
struct FileKitPickerStateOf {
    using Result = R;
    std::variant<FileKitPickerState::Started, FileKitPickerState::Progress, FileKitPickerState::CompletedOf<R>,
                 FileKitPickerState::Failed, FileKitPickerState::Cancelled>
        kompoz_value;
};

// The dialog's title (where the platform shows one: its message on macOS), and macOS's settings.
// The dialog is the app window's own: over it, modal to it (the window the launcher is composed in).
struct FileKitDialogSettings {
    std::optional<std::string> title;
    FileKitMacOSSettings macOS;
    static FileKitDialogSettings createDefault() { return {}; }
};

namespace detail {

// Files that aren't paths (Android's content:// URIs), reached through the platform: set by it.
struct ContentFileInfo {
    std::string name;
    int64_t size = -1;
    std::string mimeType;
    int64_t lastModified = 0;  // ms since the epoch
    bool directory = false;
};
struct ContentFiles {
    // A file descriptor of it opened for `mode` ("r", "wt"), -1 if it can't be.
    std::function<int(const std::string& uri, const char* mode)> open;
    std::function<std::optional<ContentFileInfo>(const std::string& uri)> query;  // none: it isn't there
    std::function<std::vector<std::pair<std::string, std::string>>(const std::string& uri)> children;  // each one's URI and name
    // A new file (or folder) in a folder: its URI, none if it couldn't be made.
    std::function<std::optional<std::string>(const std::string& parent, const std::string& name, bool directory)> create;
    std::function<bool(const std::string& uri)> remove;
    std::function<void(const std::string& uri)> persist;  // its access kept after a restart
};
void setContentFiles(ContentFiles files);
bool isContentUri(std::string_view path);

// The dialog shown on `host`, `done` called on the UI thread with what it gave.
void showFileDialog(KompozHost& host, FileDialogRequest request, std::function<void(FileDialogResult)> done);

// The picked paths as a mode's result: done(result) once, or for WithState its states as they come.
template <class Mode>
struct PickerResultOf;
template <>
struct PickerResultOf<FileKitMode::SingleMode> {
    using Type = std::optional<PlatformFile>;
};
template <>
struct PickerResultOf<FileKitMode::Multiple> {
    using Type = std::optional<std::vector<PlatformFile>>;
};
template <>
struct PickerResultOf<FileKitMode::SingleWithStateMode> {
    using Type = FileKitPickerStateOf<PlatformFile>;
};
template <>
struct PickerResultOf<FileKitMode::MultipleWithState> {
    using Type = FileKitPickerStateOf<std::vector<PlatformFile>>;
};
template <class Mode>
using PickerResult = typename PickerResultOf<std::remove_cvref_t<Mode>>::Type;

template <class Mode>
FileDialogRequest pickerRequest(const FileKitType& type, const Mode& mode, const std::optional<PlatformFile>& directory,
                                const FileKitDialogSettings& settings) {
    FileDialogRequest r{.kind = FileDialogRequest::Kind::OpenFiles, .type = type, .title = settings.title, .macOS = settings.macOS};
    if constexpr (requires(const Mode& m) { m.maxItems; }) {
        r.multiple = true;
        r.maxItems = mode.maxItems;
    }
    if (directory) r.directory = directory->path();
    return r;
}
template <class Mode>
void limitPicked(const Mode& mode, std::vector<std::string>& files) {
    if constexpr (requires { mode.maxItems; }) {
        if (mode.maxItems && *mode.maxItems > 0 && files.size() > size_t(*mode.maxItems)) files.resize(size_t(*mode.maxItems));
    } else if (files.size() > 1) {
        files.resize(1);
    }
}
// What the picker gave, to its block (or as the WithState modes' states), on the UI thread; errors
// to onError (the WithState modes: as Failed).
template <class Mode, class F, class E>
void deliverPicked(const Mode& mode, FileDialogResult result, F& onResult, E& onError) {
    using R = PickerResult<Mode>;
    std::vector<PlatformFile> files;
    if (!result.error) {
        limitPicked(mode, result.files);
        for (auto& f : result.files) files.emplace_back(std::move(f));
    }
    if constexpr (requires { typename R::Result; }) {
        using State = R;
        if (result.error) return onResult(State{FileKitPickerState::Failed{FileKitDialogException(*result.error)}});
        if (files.empty()) return onResult(State{FileKitPickerState::Cancelled{}});
        int total = int(files.size());
        onResult(State{FileKitPickerState::Started{total}});
        std::vector<PlatformFile> processed;
        for (const PlatformFile& f : files) {
            processed.push_back(f);
            onResult(State{FileKitPickerState::Progress{processed, total}});
        }
        if constexpr (std::is_same_v<typename R::Result, PlatformFile>)
            onResult(State{FileKitPickerState::CompletedOf<PlatformFile>{files.front()}});
        else
            onResult(State{FileKitPickerState::CompletedOf<std::vector<PlatformFile>>{std::move(files)}});
    } else {
        if (result.error) {
            onError(FileKitDialogException(*result.error));
            return onResult(R());
        }
        if (files.empty()) return onResult(R());
        if constexpr (std::is_same_v<R, std::optional<PlatformFile>>) onResult(R(files.front()));
        else onResult(R(std::move(files)));
    }
}

// A launcher's own: its host and what it shows; its callbacks the latest composition's.
struct FileLauncherData {
    KompozHost* host = nullptr;
    std::function<void(FileDialogResult)> deliver;  // set at each composition
    bool active = true;                             // false once it left the composition
};
std::shared_ptr<FileLauncherData> rememberFileLauncher(CallSite site);
void launchFileDialog(const std::shared_ptr<FileLauncherData>& data, FileDialogRequest request);

} // namespace detail

// What rememberFilePickerLauncher and rememberDirectoryPickerLauncher give: launch() shows the dialog.
class PickerResultLauncher {
public:
    void launch() const { detail::launchFileDialog(data_, request_); }

    std::shared_ptr<detail::FileLauncherData> data_;
    detail::FileDialogRequest request_;
};

namespace detail {
struct SaverLaunchImpl;
}
// What rememberFileSaverLauncher gives: launch(suggestedName, defaultExtension, allowedExtensions,
// directory) shows the dialog.
class SaverResultLauncher {
public:
    template <class... A>
    void launch(A&&... args) const;

    std::shared_ptr<detail::FileLauncherData> data_;
    FileKitDialogSettings settings_;
};

namespace detail {

// A launcher's block, given last (without a name): not a named argument nor a value one of its
// parameters takes. (Told without calling it: what it is given depends on the mode, and a generic
// lambda's body isn't looked into with another type.)
template <class L>
inline constexpr bool isLauncherValue =
    isNamed<L> || std::is_convertible_v<L, FileKitType> || std::is_convertible_v<L, std::optional<PlatformFile>> ||
    std::is_convertible_v<L, FileKitDialogSettings> || std::is_same_v<L, FileKitMode::SingleMode> || std::is_same_v<L, FileKitMode::Multiple> ||
    std::is_same_v<L, FileKitMode::SingleWithStateMode> || std::is_same_v<L, FileKitMode::MultipleWithState>;
template <class... A>
constexpr bool endsWithResultBlock() {
    if constexpr (sizeof...(A) == 0) return false;
    else return !isLauncherValue<std::remove_cvref_t<std::tuple_element_t<sizeof...(A) - 1, std::tuple<A...>>>>;
}
// make(onResult, the other arguments…): the block taken from the end, or onResult = … .
template <class P, class Make, class... A>
decltype(auto) withResultBlock(Make&& make, A&&... args) {
    if constexpr (endsWithResultBlock<A...>()) {
        auto all = std::forward_as_tuple(std::forward<A>(args)...);
        constexpr size_t n = sizeof...(A) - 1;
        return [&]<size_t... I>(std::index_sequence<I...>) -> decltype(auto) {
            return make(std::get<n>(all), std::get<I>(std::move(all))...);
        }(std::make_index_sequence<n>());
    } else {
        static_assert(P::template given<names::OnResult, A...>(), "kompoz: a launcher takes onResult = { … }, or it last");
        return make(P::template arg<names::OnResult>(args...), std::forward<A>(args)...);
    }
}
template <class P, class... A>
std::function<void(const FileKitDialogException&)> errorHandler(A&&... args) {
    if constexpr (P::template given<names::OnError, A...>()) {
        auto f = P::template arg<names::OnError>(args...);
        return [f](const FileKitDialogException& e) mutable { f(e); };
    } else {
        return [](const FileKitDialogException&) {};
    }
}
inline std::function<void(FileDialogResult)> oneFileDelivery(std::function<void(std::optional<PlatformFile>)> onResult,
                                                              std::function<void(const FileKitDialogException&)> onError) {
    return [onResult = std::move(onResult), onError = std::move(onError)](FileDialogResult result) {
        if (result.error) onError(FileKitDialogException(*result.error));
        onResult(result.error || result.files.empty() ? std::optional<PlatformFile>() : std::optional(PlatformFile(result.files.front())));
    };
}

// rememberFilePickerLauncher(type = …, mode = …, directory = …, dialogSettings = …, onError = …) { result -> … }
struct RememberFilePickerLauncherImpl {
    using P = Params<names::Type, names::Mode, names::Directory, names::DialogSettings, names::OnError, names::OnResult>;
    template <class... A>
    static PickerResultLauncher call(CallSite site, A&&... all) {
        return withResultBlock<P>([&](auto&& onResult, auto&&... args) {
            static_assert(P::template check<decltype(args)...>());
            auto mode = [&] {
                if constexpr (P::template given<names::Mode, decltype(args)...>())
                    return std::remove_cvref_t<decltype(P::template arg<names::Mode>(args...))>(P::template arg<names::Mode>(args...));
                else return FileKitMode::SingleMode{};
            }();
            auto onError = errorHandler<P>(args...);
            FileKitType type = P::template get<names::Type>(FileKitType{}, args...);
            std::optional<PlatformFile> directory = P::template get<names::Directory>(std::optional<PlatformFile>{}, args...);
            FileKitDialogSettings settings = P::template get<names::DialogSettings>(FileKitDialogSettings{}, args...);
            auto data = rememberFileLauncher(site);
            data->deliver = [mode, onResult = std::remove_cvref_t<decltype(onResult)>(onResult), onError](FileDialogResult result) mutable {
                deliverPicked(mode, std::move(result), onResult, onError);
            };
            return PickerResultLauncher{data, pickerRequest(type, mode, directory, settings)};
        }, std::forward<A>(all)...);
    }
};

// rememberFileSaverLauncher(dialogSettings = …, onError = …) { file -> … }
struct RememberFileSaverLauncherImpl {
    using P = Params<names::DialogSettings, names::OnError, names::OnResult>;
    template <class... A>
    static SaverResultLauncher call(CallSite site, A&&... all) {
        return withResultBlock<P>([&](auto&& onResult, auto&&... args) {
            static_assert(P::template check<decltype(args)...>());
            auto data = rememberFileLauncher(site);
            data->deliver = oneFileDelivery(onResult, errorHandler<P>(args...));
            return SaverResultLauncher{data, P::template get<names::DialogSettings>(FileKitDialogSettings{}, args...)};
        }, std::forward<A>(all)...);
    }
};

// rememberDirectoryPickerLauncher(directory = …, dialogSettings = …, onError = …) { directory -> … }
struct RememberDirectoryPickerLauncherImpl {
    using P = Params<names::Directory, names::DialogSettings, names::OnError, names::OnResult>;
    template <class... A>
    static PickerResultLauncher call(CallSite site, A&&... all) {
        return withResultBlock<P>([&](auto&& onResult, auto&&... args) {
            static_assert(P::template check<decltype(args)...>());
            std::optional<PlatformFile> directory = P::template get<names::Directory>(std::optional<PlatformFile>{}, args...);
            FileKitDialogSettings settings = P::template get<names::DialogSettings>(FileKitDialogSettings{}, args...);
            auto data = rememberFileLauncher(site);
            data->deliver = oneFileDelivery(onResult, errorHandler<P>(args...));
            FileDialogRequest request{.kind = FileDialogRequest::Kind::OpenDirectory, .title = settings.title, .macOS = settings.macOS};
            if (directory) request.directory = directory->path();
            return PickerResultLauncher{data, std::move(request)};
        }, std::forward<A>(all)...);
    }
};

// launch(suggestedName, defaultExtension = …, allowedExtensions = …, directory = …), and FileKit.openFileSaver's.
struct SaverLaunchImpl {
    using P = Params<names::SuggestedName, names::DefaultExtension, names::AllowedExtensions, names::Directory>;
    template <class... A>
    static FileDialogRequest request(const FileKitDialogSettings& settings, A&&... args) {
        static_assert(P::template check<A...>());
        FileDialogRequest r{.kind = FileDialogRequest::Kind::SaveFile, .title = settings.title, .macOS = settings.macOS};
        r.suggestedName = P::template get<names::SuggestedName>(std::string(), args...);
        r.defaultExtension = P::template get<names::DefaultExtension>(std::optional<std::string>(), args...);
        if constexpr (P::template given<names::AllowedExtensions, A...>())
            for (const auto& e : P::template arg<names::AllowedExtensions>(args...)) r.allowedExtensions.emplace_back(e);
        if (auto d = P::template get<names::Directory>(std::optional<PlatformFile>(), args...)) r.directory = d->path();
        return r;
    }
};

// co_await-ed: the dialog shown on the coroutine's host, the coroutine resumed with what it gave.
struct FileDialogAwaiter {
    FileDialogRequest request;
    std::shared_ptr<FileDialogResult> result = std::make_shared<FileDialogResult>();
    bool await_ready() const noexcept { return false; }
    template <class P>
    void await_suspend(std::coroutine_handle<P> h) {
        JobState* job = h.promise().job;
        suspend(*job, h);
    }
    FileDialogResult await_resume() { return std::move(*result); }

private:
    void suspend(JobState& job, std::coroutine_handle<> h);
};

template <class Mode>
struct PickerAwaiter {
    FileDialogAwaiter dialog;
    Mode mode;
    bool await_ready() const noexcept { return false; }
    template <class P>
    void await_suspend(std::coroutine_handle<P> h) {
        dialog.await_suspend(h);
    }
    PickerResult<Mode> await_resume() {
        FileDialogResult r = dialog.await_resume();
        if (r.error) throw FileKitDialogException(*r.error);
        limitPicked(mode, r.files);
        if (r.files.empty()) return {};
        if constexpr (std::is_same_v<Mode, FileKitMode::SingleMode>) return PlatformFile(r.files.front());
        else {
            std::vector<PlatformFile> files;
            for (auto& f : r.files) files.emplace_back(std::move(f));
            return files;
        }
    }
};
struct OneFileAwaiter {
    FileDialogAwaiter dialog;
    bool await_ready() const noexcept { return false; }
    template <class P>
    void await_suspend(std::coroutine_handle<P> h) {
        dialog.await_suspend(h);
    }
    std::optional<PlatformFile> await_resume() {
        FileDialogResult r = dialog.await_resume();
        if (r.error) throw FileKitDialogException(*r.error);
        if (r.files.empty()) return std::nullopt;
        return PlatformFile(r.files.front());
    }
};

struct OpenFilePickerImpl {
    using P = Params<names::Type, names::Mode, names::Directory, names::DialogSettings>;
    template <class... A>
    static auto call(CallSite, A&&... args) {
        static_assert(P::template check<A...>());
        auto mode = [&] {
            if constexpr (P::template given<names::Mode, A...>()) return std::remove_cvref_t<decltype(P::template arg<names::Mode>(args...))>(P::template arg<names::Mode>(args...));
            else return FileKitMode::SingleMode{};
        }();
        static_assert(std::is_same_v<decltype(mode), FileKitMode::SingleMode> || std::is_same_v<decltype(mode), FileKitMode::Multiple>,
                      "kompoz: FileKit.openFilePicker takes FileKitMode.Single or Multiple (a launcher takes the WithState modes)");
        FileKitType type = P::template get<names::Type>(FileKitType{}, args...);
        std::optional<PlatformFile> directory = P::template get<names::Directory>(std::optional<PlatformFile>{}, args...);
        FileKitDialogSettings settings = P::template get<names::DialogSettings>(FileKitDialogSettings{}, args...);
        return PickerAwaiter<decltype(mode)>{{pickerRequest(type, mode, directory, settings)}, mode};
    }
};
struct OpenFileSaverImpl {
    using P = Params<names::SuggestedName, names::DefaultExtension, names::AllowedExtensions, names::Directory, names::DialogSettings>;
    template <class... A>
    static OneFileAwaiter call(CallSite, A&&... args) {
        static_assert(P::template check<A...>());
        FileKitDialogSettings settings = P::template get<names::DialogSettings>(FileKitDialogSettings{}, args...);
        FileDialogRequest r{.kind = FileDialogRequest::Kind::SaveFile, .title = settings.title, .macOS = settings.macOS};
        r.suggestedName = P::template get<names::SuggestedName>(std::string(), args...);
        r.defaultExtension = P::template get<names::DefaultExtension>(std::optional<std::string>(), args...);
        if constexpr (P::template given<names::AllowedExtensions, A...>())
            for (const auto& e : P::template arg<names::AllowedExtensions>(args...)) r.allowedExtensions.emplace_back(e);
        if (auto d = P::template get<names::Directory>(std::optional<PlatformFile>(), args...)) r.directory = d->path();
        return {{std::move(r)}};
    }
};
struct OpenDirectoryPickerImpl {
    using P = Params<names::Directory, names::DialogSettings>;
    template <class... A>
    static OneFileAwaiter call(CallSite, A&&... args) {
        static_assert(P::template check<A...>());
        FileKitDialogSettings settings = P::template get<names::DialogSettings>(FileKitDialogSettings{}, args...);
        FileDialogRequest r{.kind = FileDialogRequest::Kind::OpenDirectory, .title = settings.title, .macOS = settings.macOS};
        if (auto d = P::template get<names::Directory>(std::optional<PlatformFile>(), args...)) r.directory = d->path();
        return {{std::move(r)}};
    }
};

} // namespace detail

template <class... A>
void SaverResultLauncher::launch(A&&... args) const {
    detail::launchFileDialog(data_, detail::SaverLaunchImpl::request(settings_, std::forward<A>(args)...));
}

inline constexpr Composable<detail::RememberFilePickerLauncherImpl> rememberFilePickerLauncher{};
inline constexpr Composable<detail::RememberFileSaverLauncherImpl> rememberFileSaverLauncher{};
inline constexpr Composable<detail::RememberDirectoryPickerLauncherImpl> rememberDirectoryPickerLauncher{};

// The dialogs in a coroutine: co_await FileKit::openFilePicker(type = …, mode = …) gives what the
// launcher's block would (null: closed without a choice), and throws a FileKitDialogException where
// the dialog can't be shown.
namespace FileKit {
inline constexpr Composable<detail::OpenFilePickerImpl> openFilePicker{};
inline constexpr Composable<detail::OpenFileSaverImpl> openFileSaver{};
inline constexpr Composable<detail::OpenDirectoryPickerImpl> openDirectoryPicker{};
#ifdef __EMSCRIPTEN__
// The web's way of saving (it has no save dialog): the bytes downloaded under the name, as the
// browser does (its downloads folder, or where it asks).
void download(const std::vector<uint8_t>& bytes, const std::string& fileName);
#endif
} // namespace FileKit

} // namespace kompoz
