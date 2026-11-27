#include "file_chooser.hpp"

#include "libdbus.hpp"

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cstring>
#include <optional>
#include <thread>
#include <vector>

extern char** environ;

namespace kompoz::platform {

namespace {

using kompoz::detail::FileDialogRequest;
using kompoz::detail::FileDialogResult;
using Kind = FileDialogRequest::Kind;

std::string titleOf(const FileDialogRequest& r, const Strings& strings) {
    if (r.title) return *r.title;
    switch (r.kind) {
    case Kind::OpenFiles: return r.multiple ? strings.openFiles : strings.openFile;
    case Kind::OpenDirectory: return strings.selectFolder;
    case Kind::SaveFile: return strings.saveFile;
    }
    return {};
}

// What the type offers: patterns ("*.pdf", both cases: the dialogs match them as written) or MIME
// types ("image/*").
struct Filter {
    std::string name;
    std::vector<std::string> globs, mimeTypes;
};
std::optional<Filter> filterOf(const FileDialogRequest& r) {
    Filter f;
    auto globs = [&](const std::vector<std::string>& extensions) {
        for (const std::string& e : extensions) {
            std::string lower = e, upper = e;
            for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
            f.globs.push_back("*." + lower);
            if (upper != lower) f.globs.push_back("*." + upper);
            f.name += (f.name.empty() ? "" : ", ") + lower;
        }
    };
    if (r.kind == Kind::SaveFile) {
        if (r.allowedExtensions.empty()) return std::nullopt;
        globs(r.allowedExtensions);
        return f;
    }
    if (r.kind != Kind::OpenFiles) return std::nullopt;
    switch (r.type.kind) {
    case FileKitType::Kind::File:
        if (r.type.extensions.empty()) return std::nullopt;
        globs(r.type.extensions);
        return f;
    case FileKitType::Kind::Image: f.mimeTypes = {"image/*"}, f.name = "image/*"; return f;
    case FileKitType::Kind::Video: f.mimeTypes = {"video/*"}, f.name = "video/*"; return f;
    case FileKitType::Kind::ImageAndVideo: f.mimeTypes = {"image/*", "video/*"}, f.name = "image/*, video/*"; return f;
    }
    return std::nullopt;
}

std::string saveName(const FileDialogRequest& r) {
    std::string name = r.suggestedName;
    if (r.defaultExtension && !r.defaultExtension->empty()) name += "." + *r.defaultExtension;
    return name;
}

// ---- the portal --------------------------------------------------------------------------------

constexpr const char* kPortal = "org.freedesktop.portal.Desktop";
constexpr const char* kPath = "/org/freedesktop/portal/desktop";
constexpr const char* kFileChooser = "org.freedesktop.portal.FileChooser";
constexpr const char* kRequest = "org.freedesktop.portal.Request";

struct Writer {
    dbus::Lib& bus;
    void string(dbus::Iter* it, const std::string& s) {
        const char* c = s.c_str();
        bus.message_iter_append_basic(it, dbus::kTypeString, &c);
    }
    // One a{sv} entry: its key and a variant of `signature` written by `value`.
    template <class F>
    void entry(dbus::Iter* dict, const char* key, const char* signature, F&& value) {
        dbus::Iter e, v;
        bus.message_iter_open_container(dict, dbus::kTypeDictEntry, nullptr, &e);
        string(&e, key);
        bus.message_iter_open_container(&e, dbus::kTypeVariant, signature, &v);
        value(&v);
        bus.message_iter_close_container(&e, &v);
        bus.message_iter_close_container(dict, &e);
    }
    void boolean(dbus::Iter* it, bool b) {
        unsigned value = b;
        bus.message_iter_append_basic(it, dbus::kTypeBoolean, &value);
    }
    void bytes(dbus::Iter* it, const std::string& s) {  // ay, with its terminating NUL (the portal's paths)
        dbus::Iter a;
        bus.message_iter_open_container(it, dbus::kTypeArray, "y", &a);
        for (size_t i = 0; i <= s.size(); ++i) {
            unsigned char c = i < s.size() ? static_cast<unsigned char>(s[i]) : 0;
            bus.message_iter_append_basic(&a, dbus::kTypeByte, &c);
        }
        bus.message_iter_close_container(it, &a);
    }
    void filter(dbus::Iter* it, const Filter& f) {  // (sa(us)): its name, then (0, glob) / (1, MIME type)
        dbus::Iter s, list;
        bus.message_iter_open_container(it, dbus::kTypeStruct, nullptr, &s);
        string(&s, f.name);
        bus.message_iter_open_container(&s, dbus::kTypeArray, "(us)", &list);
        auto add = [&](unsigned kind, const std::string& pattern) {
            dbus::Iter p;
            bus.message_iter_open_container(&list, dbus::kTypeStruct, nullptr, &p);
            bus.message_iter_append_basic(&p, dbus::kTypeUint32, &kind);
            string(&p, pattern);
            bus.message_iter_close_container(&list, &p);
        };
        for (const auto& g : f.globs) add(0, g);
        for (const auto& m : f.mimeTypes) add(1, m);
        bus.message_iter_close_container(&s, &list);
        bus.message_iter_close_container(it, &s);
    }
};

// The URIs in the Response's results (a{sv}: "uris" as).
std::vector<std::string> urisIn(dbus::Lib& bus, dbus::Iter* results) {
    std::vector<std::string> out;
    if (bus.message_iter_get_arg_type(results) != dbus::kTypeArray) return out;
    dbus::Iter dict;
    bus.message_iter_recurse(results, &dict);
    while (bus.message_iter_get_arg_type(&dict) == dbus::kTypeDictEntry) {
        dbus::Iter e, v;
        bus.message_iter_recurse(&dict, &e);
        const char* key = nullptr;
        bus.message_iter_get_basic(&e, &key);
        bus.message_iter_next(&e);
        if (key && std::strcmp(key, "uris") == 0 && bus.message_iter_get_arg_type(&e) == dbus::kTypeVariant) {
            bus.message_iter_recurse(&e, &v);
            if (bus.message_iter_get_arg_type(&v) == dbus::kTypeArray) {
                dbus::Iter a;
                bus.message_iter_recurse(&v, &a);
                while (bus.message_iter_get_arg_type(&a) == dbus::kTypeString) {
                    const char* uri = nullptr;
                    bus.message_iter_get_basic(&a, &uri);
                    if (uri) out.emplace_back(uri);
                    bus.message_iter_next(&a);
                }
            }
        }
        bus.message_iter_next(&dict);
    }
    return out;
}

// The portal's dialog: its answer, or none where there is no portal (to try the others then).
std::optional<FileDialogResult> viaPortal(const std::string& parentWindow, const std::string& title, const FileDialogRequest& r) {
    static dbus::Lib bus;
    static const bool loaded = bus.load();
    if (!loaded) return std::nullopt;
    dbus::Error error;
    bus.error_init(&error);
    dbus::Connection* connection = bus.bus_get_private(dbus::kSessionBus, &error);
    bus.error_free(&error);
    if (!connection) return std::nullopt;
    bus.connection_set_exit_on_disconnect(connection, 0);
    struct Close {
        dbus::Lib& bus;
        dbus::Connection* c;
        ~Close() {
            bus.connection_close(c);
            bus.connection_unref(c);
        }
    } close{bus, connection};

    // The Request object's path is known before the call (its handle_token): listened to first, so
    // that a quick answer isn't missed.
    static std::atomic<int> next{0};
    std::string token = "kompoz" + std::to_string(getpid()) + "_" + std::to_string(++next);
    std::string sender = bus.bus_get_unique_name(connection) ? bus.bus_get_unique_name(connection) + 1 : "";
    for (char& c : sender)
        if (c == '.') c = '_';
    std::string requestPath = std::string(kPath) + "/request/" + sender + "/" + token;
    std::string match = "type='signal',interface='" + std::string(kRequest) + "',member='Response',path='" + requestPath + "'";
    bus.error_init(&error);
    bus.bus_add_match(connection, match.c_str(), &error);
    bus.error_free(&error);

    dbus::Message* call = bus.message_new_method_call(kPortal, kPath, kFileChooser, r.kind == Kind::SaveFile ? "SaveFile" : "OpenFile");
    if (!call) return std::nullopt;
    Writer w{bus};
    dbus::Iter args, options;
    bus.message_iter_init_append(call, &args);
    w.string(&args, parentWindow);
    w.string(&args, title);
    bus.message_iter_open_container(&args, dbus::kTypeArray, "{sv}", &options);
    w.entry(&options, "handle_token", "s", [&](dbus::Iter* v) { w.string(v, token); });
    w.entry(&options, "modal", "b", [&](dbus::Iter* v) { w.boolean(v, true); });
    if (r.kind == Kind::OpenFiles && r.multiple) w.entry(&options, "multiple", "b", [&](dbus::Iter* v) { w.boolean(v, true); });
    if (r.kind == Kind::OpenDirectory) w.entry(&options, "directory", "b", [&](dbus::Iter* v) { w.boolean(v, true); });
    if (auto f = filterOf(r)) {
        w.entry(&options, "filters", "a(sa(us))", [&](dbus::Iter* v) {
            dbus::Iter list;
            bus.message_iter_open_container(v, dbus::kTypeArray, "(sa(us))", &list);
            w.filter(&list, *f);
            bus.message_iter_close_container(v, &list);
        });
        w.entry(&options, "current_filter", "(sa(us))", [&](dbus::Iter* v) { w.filter(v, *f); });
    }
    if (r.kind == Kind::SaveFile) w.entry(&options, "current_name", "s", [&](dbus::Iter* v) { w.string(v, saveName(r)); });
    if (!r.directory.empty()) w.entry(&options, "current_folder", "ay", [&](dbus::Iter* v) { w.bytes(v, r.directory); });
    bus.message_iter_close_container(&args, &options);

    bus.error_init(&error);
    dbus::Message* reply = bus.connection_send_with_reply_and_block(connection, call, 5000, &error);
    bus.message_unref(call);
    bool noPortal = !reply;  // (no portal, or one without a FileChooser)
    bus.error_free(&error);
    if (noPortal) return std::nullopt;
    // The Request's path as the portal gives it (older ones don't take the token).
    std::string handle = requestPath;
    if (dbus::Iter it; bus.message_iter_init(reply, &it) && bus.message_iter_get_arg_type(&it) == dbus::kTypeObjectPath) {
        const char* path = nullptr;
        bus.message_iter_get_basic(&it, &path);
        if (path && handle != path) {
            handle = path;
            std::string other = "type='signal',interface='" + std::string(kRequest) + "',member='Response',path='" + handle + "'";
            bus.error_init(&error);
            bus.bus_add_match(connection, other.c_str(), &error);
            bus.error_free(&error);
        }
    }
    bus.message_unref(reply);

    // The answer: Response(u code, a{sv} results); 0 chosen, 1 cancelled, 2 ended otherwise.
    while (bus.connection_read_write(connection, 500)) {
        while (dbus::Message* m = bus.connection_pop_message(connection)) {
            std::optional<FileDialogResult> result;
            const char* path = bus.message_get_path(m);
            if (bus.message_is_signal(m, kRequest, "Response") && path && handle == path) {
                dbus::Iter it;
                uint32_t code = 2;
                result.emplace();
                if (bus.message_iter_init(m, &it) && bus.message_iter_get_arg_type(&it) == dbus::kTypeUint32) {
                    bus.message_iter_get_basic(&it, &code);
                    if (code == 0 && bus.message_iter_next(&it)) result->files = urisIn(bus, &it);
                }
                if (code == 2) result->error = "kompoz: the desktop's file dialog failed";
            }
            bus.message_unref(m);
            if (result) return result;
        }
    }
    return FileDialogResult{.error = "kompoz: the desktop portal went away"};
}

// ---- zenity, kdialog ---------------------------------------------------------------------------

// Runs `argv`, its output's lines (what was chosen) back; none: it isn't there (it couldn't be run).
std::optional<std::pair<int, std::string>> run(const std::vector<std::string>& argv) {
    int out[2];
    if (pipe2(out, O_CLOEXEC) != 0) return std::nullopt;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    pid_t pid = 0;
    int spawned = posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(out[1]);
    if (spawned != 0) {
        ::close(out[0]);
        return std::nullopt;
    }
    std::string text;
    char buffer[4096];
    for (ssize_t n; (n = ::read(out[0], buffer, sizeof buffer)) != 0;) {
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) break;
        text.append(buffer, size_t(n));
    }
    ::close(out[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (code == 127) return std::nullopt;  // (the shell's "not found", posix_spawnp's child's)
    return std::pair(code, text);
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        if (end > start) out.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

std::optional<FileDialogResult> viaZenity(const std::string& title, const FileDialogRequest& r) {
    std::vector<std::string> argv = {"zenity", "--file-selection", "--title=" + title};
    if (r.kind == Kind::OpenFiles && r.multiple) argv.insert(argv.end(), {"--multiple", "--separator=\n"});
    if (r.kind == Kind::OpenDirectory) argv.push_back("--directory");
    if (r.kind == Kind::SaveFile) {
        argv.insert(argv.end(), {"--save", "--confirm-overwrite"});
        argv.push_back("--filename=" + (r.directory.empty() ? std::string() : r.directory + "/") + saveName(r));
    } else if (!r.directory.empty()) {
        argv.push_back("--filename=" + r.directory + "/");
    }
    if (auto f = filterOf(r)) {
        std::string patterns;
        for (const auto& g : f->globs) patterns += (patterns.empty() ? "" : " ") + g;
        for (const auto& m : f->mimeTypes) {  // (zenity takes patterns only: the common extensions)
            const char* common = m == "image/*" ? "*.png *.jpg *.jpeg *.gif *.webp *.bmp *.svg *.heic *.avif"
                                                : "*.mp4 *.webm *.mov *.mkv *.avi *.m4v";
            patterns += std::string(patterns.empty() ? "" : " ") + common;
        }
        argv.push_back("--file-filter=" + f->name + " | " + patterns);
    }
    auto ran = run(argv);
    if (!ran) return std::nullopt;
    if (ran->first == 1) return FileDialogResult{};
    if (ran->first != 0) return FileDialogResult{.error = "kompoz: zenity's file dialog failed"};
    return FileDialogResult{.files = lines(ran->second)};
}

std::optional<FileDialogResult> viaKdialog(const std::string& title, const FileDialogRequest& r) {
    std::vector<std::string> argv = {"kdialog", "--title", title};
    std::string start = r.directory.empty() ? "." : r.directory;
    std::string filter;
    if (auto f = filterOf(r)) {
        for (const auto& g : f->globs) filter += (filter.empty() ? "" : " ") + g;
        for (const auto& m : f->mimeTypes) filter += (filter.empty() ? "" : " ") + m;
    }
    switch (r.kind) {
    case Kind::OpenFiles:
        argv.insert(argv.end(), {"--getopenfilename", start});
        if (!filter.empty()) argv.push_back(filter);
        if (r.multiple) argv.insert(argv.end(), {"--multiple", "--separate-output"});
        break;
    case Kind::OpenDirectory: argv.insert(argv.end(), {"--getexistingdirectory", start}); break;
    case Kind::SaveFile:
        argv.insert(argv.end(), {"--getsavefilename", start + "/" + saveName(r)});
        if (!filter.empty()) argv.push_back(filter);
        break;
    }
    auto ran = run(argv);
    if (!ran) return std::nullopt;
    if (ran->first == 1) return FileDialogResult{};
    if (ran->first != 0) return FileDialogResult{.error = "kompoz: kdialog's file dialog failed"};
    return FileDialogResult{.files = lines(ran->second)};
}

} // namespace

void showFileChooser(const std::string& parentWindow, const Strings& strings, const FileDialogRequest& request,
                     std::function<void(FileDialogResult)> done) {
    std::thread([parentWindow, title = titleOf(request, strings), request, done = std::move(done)] {
        // (KOMPOZ_FILE_DIALOG=zenity or kdialog: that one, even where there is a portal)
        const char* forced = std::getenv("KOMPOZ_FILE_DIALOG");
        std::string only = forced ? forced : "";
        std::optional<FileDialogResult> result;
        if (only.empty() || only == "portal") result = viaPortal(parentWindow, title, request);
        if (!result && (only.empty() || only == "zenity")) result = viaZenity(title, request);
        if (!result && (only.empty() || only == "kdialog")) result = viaKdialog(title, request);
        done(result ? std::move(*result) : FileDialogResult{.error = "kompoz: no file dialog here (no desktop portal, zenity or kdialog)"});
    }).detach();
}

} // namespace kompoz::platform
