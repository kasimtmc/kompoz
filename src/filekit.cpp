#include <kompoz/filekit.hpp>
#include <kompoz/host.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace fs = std::filesystem;

namespace kompoz {

// ---- files that aren't paths ------------------------------------------------------------------

namespace {
std::mutex gContentLock;
detail::ContentFiles& contentFiles() {
    static detail::ContentFiles files;
    return files;
}
detail::ContentFiles content() {
    std::lock_guard lock(gContentLock);
    return contentFiles();
}

fs::path pathOf(const std::string& s) { return fs::path(reinterpret_cast<const char8_t*>(s.c_str())); }
std::string pathString(const fs::path& p) {
    std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// file:///home/a%20b.txt is /home/a b.txt (file:///C:/x on Windows: C:/x).
std::string pathOfFileUri(std::string_view uri) {
    std::string_view rest = uri.substr(7);
    if (rest.starts_with("localhost/")) rest.remove_prefix(9);
    std::string out;
    for (size_t i = 0; i < rest.size(); ++i) {
        int hi, lo;
        if (rest[i] == '%' && i + 2 < rest.size() + 0 && (hi = hexValue(rest[i + 1])) >= 0 && (lo = hexValue(rest[i + 2])) >= 0) {
            out += char(hi * 16 + lo);
            i += 2;
        } else {
            out += rest[i];
        }
    }
#ifdef _WIN32
    if (out.size() > 2 && out[0] == '/' && out[2] == ':') out.erase(0, 1);
#endif
    return out;
}

std::optional<detail::ContentFileInfo> query(const std::string& uri) {
    auto files = content();
    return files.query ? files.query(uri) : std::nullopt;
}

#ifdef _WIN32
int sysRead(int fd, void* to, unsigned n) { return _read(fd, to, n); }
int sysWrite(int fd, const void* from, unsigned n) { return _write(fd, from, n); }
void sysClose(int fd) { _close(fd); }
#else
ssize_t sysRead(int fd, void* to, size_t n) { return ::read(fd, to, n); }
ssize_t sysWrite(int fd, const void* from, size_t n) { return ::write(fd, from, n); }
void sysClose(int fd) { ::close(fd); }
#endif

struct Fd {
    int fd = -1;
    ~Fd() {
        if (fd >= 0) sysClose(fd);
    }
};

std::vector<uint8_t> readAll(int fd, const std::string& what) {
    std::vector<uint8_t> out;
    uint8_t buffer[64 * 1024];
    while (true) {
        auto n = sysRead(fd, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) throw std::runtime_error("kompoz: can't read " + what);
        if (n == 0) break;
        out.insert(out.end(), buffer, buffer + n);
    }
    return out;
}
void writeAll(int fd, const std::vector<uint8_t>& bytes, const std::string& what) {
    size_t done = 0;
    while (done < bytes.size()) {
        auto n = sysWrite(fd, bytes.data() + done, unsigned(std::min<size_t>(bytes.size() - done, 1 << 30)));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("kompoz: can't write " + what);
        done += size_t(n);
    }
}

// The common types by extension, where the system doesn't say (a path's).
std::optional<std::string> mimeOfExtension(std::string e) {
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    static const std::pair<const char*, const char*> types[] = {
        {"txt", "text/plain"}, {"md", "text/markdown"}, {"csv", "text/csv"}, {"html", "text/html"}, {"htm", "text/html"},
        {"css", "text/css"}, {"js", "text/javascript"}, {"json", "application/json"}, {"xml", "application/xml"},
        {"pdf", "application/pdf"}, {"zip", "application/zip"}, {"gz", "application/gzip"}, {"png", "image/png"},
        {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"gif", "image/gif"}, {"webp", "image/webp"}, {"bmp", "image/bmp"},
        {"svg", "image/svg+xml"}, {"heic", "image/heic"}, {"avif", "image/avif"}, {"mp4", "video/mp4"},
        {"webm", "video/webm"}, {"mov", "video/quicktime"}, {"mkv", "video/x-matroska"}, {"mp3", "audio/mpeg"},
        {"wav", "audio/wav"}, {"ogg", "audio/ogg"}, {"flac", "audio/flac"}, {"m4a", "audio/mp4"},
        {"ttf", "font/ttf"}, {"otf", "font/otf"}, {"woff2", "font/woff2"},
    };
    for (const auto& [ext, type] : types)
        if (e == ext) return type;
    return std::nullopt;
}
} // namespace

void detail::setContentFiles(ContentFiles files) {
    std::lock_guard lock(gContentLock);
    contentFiles() = std::move(files);
}

bool detail::isContentUri(std::string_view path) { return path.starts_with("content://"); }

// ---- PlatformFile -----------------------------------------------------------------------------

PlatformFile::PlatformFile(std::string path) : path_(std::string_view(path).starts_with("file://") ? pathOfFileUri(path) : std::move(path)) {}

PlatformFile::PlatformFile(const PlatformFile& parent, std::string_view child) : PlatformFile(parent.resolve(child)) {}

std::string PlatformFile::name() const {
    if (!pendingName_.empty()) return pendingName_;
    if (!knownName_.empty()) return knownName_;
    if (detail::isContentUri(path_)) {
        if (auto info = query(path_)) return info->name;
        size_t slash = path_.find_last_of("/%");  // (…/document/primary%3ADownload%2Fa.txt)
        std::string tail = path_.substr(slash == std::string::npos ? 0 : slash + 1);
        if (slash != std::string::npos && path_[slash] == '%' && tail.size() >= 2) tail.erase(0, 2);
        return tail;
    }
    return pathString(pathOf(path_).filename());
}

std::string PlatformFile::extension() const {
    std::string n = name();
    size_t dot = n.rfind('.');
    return dot == std::string::npos ? std::string() : n.substr(dot + 1);
}

std::string PlatformFile::nameWithoutExtension() const {
    std::string n = name();
    size_t dot = n.rfind('.');
    return dot == std::string::npos ? n : n.substr(0, dot);
}

int64_t PlatformFile::size() const {
    if (detail::isContentUri(path_)) {
        auto info = query(path_);
        return info ? info->size : -1;
    }
    std::error_code error;
    auto n = fs::file_size(pathOf(path_), error);
    return error ? -1 : int64_t(n);
}

bool PlatformFile::exists() const {
    if (detail::isContentUri(path_)) return query(path_).has_value();
    std::error_code error;
    return fs::exists(pathOf(path_), error);
}

bool PlatformFile::isDirectory() const {
    if (detail::isContentUri(path_)) {
        auto info = query(path_);
        return info && info->directory;
    }
    std::error_code error;
    return fs::is_directory(pathOf(path_), error);
}

bool PlatformFile::isRegularFile() const {
    if (detail::isContentUri(path_)) {
        auto info = query(path_);
        return info && !info->directory;
    }
    std::error_code error;
    return fs::is_regular_file(pathOf(path_), error);
}

bool PlatformFile::isAbsolute() const { return detail::isContentUri(path_) || pathOf(path_).is_absolute(); }

std::optional<PlatformFile> PlatformFile::parent() const {
    if (detail::isContentUri(path_) || path_.empty()) return std::nullopt;
    fs::path p = pathOf(path_);
    fs::path up = p.has_relative_path() && !p.has_filename() ? p.parent_path().parent_path() : p.parent_path();  // (a/b/: a)
    if (up.empty() || up == p) return std::nullopt;
    return PlatformFile(pathString(up));
}

std::string PlatformFile::absolutePath() const {
    if (detail::isContentUri(path_)) return path_;
    std::error_code error;
    fs::path p = fs::absolute(pathOf(path_), error);
    return error ? path_ : pathString(p);
}

PlatformFile PlatformFile::resolve(std::string_view relative) const {
    if (!detail::isContentUri(path_)) return PlatformFile(pathString(pathOf(path_) / pathOf(std::string(relative))));
    // A picked folder's: each part found among its children's names; one that isn't there yet is
    // made where it is written (or createDirectories).
    PlatformFile at = *this;
    size_t start = 0;
    while (start <= relative.size()) {
        size_t slash = relative.find('/', start);
        std::string_view part = relative.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        start = slash == std::string_view::npos ? relative.size() + 1 : slash + 1;
        if (part.empty() || part == ".") continue;
        std::optional<PlatformFile> found;
        if (!at.pendingName_.empty()) {
            found = PlatformFile(at.path_ + "/" + std::string(part));
            found->pendingParent_ = std::make_shared<PlatformFile>(at);
            found->pendingName_ = part;
        } else {
            for (PlatformFile& child : at.list())
                if (child.name() == part) {
                    found = std::move(child);
                    break;
                }
            if (!found) {
                found = PlatformFile(at.path_ + "/" + std::string(part));
                found->pendingParent_ = std::make_shared<PlatformFile>(at);
                found->pendingName_ = part;
            }
        }
        at = std::move(*found);
    }
    return at;
}

std::vector<PlatformFile> PlatformFile::list() const {
    std::vector<PlatformFile> out;
    if (detail::isContentUri(path_)) {
        if (!pendingName_.empty()) return out;
        auto files = content();
        if (files.children)
            for (auto& [uri, name] : files.children(path_)) {
                out.emplace_back(std::move(uri));
                out.back().knownName_ = std::move(name);
            }
        return out;
    }
    std::error_code error;
    for (fs::directory_iterator it(pathOf(path_), error), end; !error && it != end; it.increment(error)) out.emplace_back(pathString(it->path()));
    std::sort(out.begin(), out.end(), [](const PlatformFile& a, const PlatformFile& b) { return a.path_ < b.path_; });
    return out;
}

std::optional<std::string> PlatformFile::mimeType() const {
    if (detail::isContentUri(path_)) {
        auto info = query(path_);
        if (info && !info->mimeType.empty() && !info->directory) return info->mimeType;
        return std::nullopt;
    }
    if (isDirectory()) return std::nullopt;
    return mimeOfExtension(extension());
}

std::chrono::system_clock::time_point PlatformFile::lastModified() const {
    if (detail::isContentUri(path_)) {
        auto info = query(path_);
        return std::chrono::system_clock::time_point(std::chrono::milliseconds(info ? info->lastModified : 0));
    }
    std::error_code error;
    auto t = fs::last_write_time(pathOf(path_), error);
    if (error) return {};
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
}

std::vector<uint8_t> PlatformFile::bookmarkData() const {
    if (detail::isContentUri(path_)) {
        auto files = content();
        if (files.persist) files.persist(path_);
    }
    return {path_.begin(), path_.end()};
}

PlatformFile PlatformFile::fromBookmarkData(const std::vector<uint8_t>& data) { return PlatformFile(std::string(data.begin(), data.end())); }

namespace {

std::vector<uint8_t> readFile(const PlatformFile& file) {
    if (detail::isContentUri(file.path())) {
        auto files = content();
        Fd fd{files.open ? files.open(file.path(), "r") : -1};
        if (fd.fd < 0) throw std::runtime_error("kompoz: can't open " + file.path());
        return readAll(fd.fd, file.path());
    }
    std::ifstream in(pathOf(file.path()), std::ios::binary);
    if (!in) throw std::runtime_error("kompoz: can't open " + file.path());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

void PlatformFile::writeBytes(const std::vector<uint8_t>& bytes) const {
    if (detail::isContentUri(path_)) {
        auto files = content();
        std::string uri = path_;
        if (!pendingName_.empty()) {  // (a child of a picked folder not made yet)
            if (pendingParent_ && !pendingParent_->pendingName_.empty()) pendingParent_->createDirectories();
            std::string parentUri = pendingParent_ ? pendingParent_->resolvedUri() : std::string();
            auto made = files.create ? files.create(parentUri, pendingName_, false) : std::nullopt;
            if (!made) throw std::runtime_error("kompoz: can't make " + pendingName_);
            uri = *made;
        }
        Fd fd{files.open ? files.open(uri, "wt") : -1};
        if (fd.fd < 0) throw std::runtime_error("kompoz: can't write " + uri);
        writeAll(fd.fd, bytes, uri);
        return;
    }
    fs::path p = pathOf(path_);
    std::error_code error;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), error);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("kompoz: can't write " + path_);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!out) throw std::runtime_error("kompoz: can't write " + path_);
}

std::string PlatformFile::resolvedUri() const {
    if (pendingName_.empty() || !pendingParent_) return path_;
    for (PlatformFile& child : PlatformFile(pendingParent_->resolvedUri()).list())
        if (child.name() == pendingName_) return child.path_;
    return path_;
}

BackgroundAwaiter<std::vector<uint8_t>> PlatformFile::readBytes() const {
    return withBackground([file = *this] { return readFile(file); });
}

BackgroundAwaiter<std::string> PlatformFile::readString() const {
    return withBackground([file = *this] {
        std::vector<uint8_t> bytes = readFile(file);
        return std::string(bytes.begin(), bytes.end());
    });
}

BackgroundAwaiter<void> PlatformFile::write(std::vector<uint8_t> bytes) const {
    return withBackground([file = *this, bytes = std::move(bytes)] { file.writeBytes(bytes); });
}

BackgroundAwaiter<void> PlatformFile::write(const PlatformFile& source) const {
    return withBackground([file = *this, source] { file.writeBytes(readFile(source)); });
}

BackgroundAwaiter<void> PlatformFile::writeString(std::string text) const {
    return withBackground([file = *this, text = std::move(text)] { file.writeBytes(std::vector<uint8_t>(text.begin(), text.end())); });
}

void PlatformFile::copyTo(const PlatformFile& target) const {
    PlatformFile to = target.isDirectory() ? target.resolve(name()) : target;
    to.writeBytes(readFile(*this));
}

void PlatformFile::createDirectories() const {
    if (detail::isContentUri(path_)) {
        if (pendingName_.empty()) return;  // (there already)
        if (pendingParent_) pendingParent_->createDirectories();
        auto files = content();
        if (!files.create || !files.create(pendingParent_ ? pendingParent_->resolvedUri() : std::string(), pendingName_, true))
            throw std::runtime_error("kompoz: can't make " + pendingName_);
        return;
    }
    std::error_code error;
    fs::create_directories(pathOf(path_), error);
    if (error) throw std::runtime_error("kompoz: can't make " + path_ + ": " + error.message());
}

void PlatformFile::delete_(bool mustExist) const {
    if (!exists()) {
        if (mustExist) throw std::runtime_error("kompoz: there is no " + path_);
        return;
    }
    if (detail::isContentUri(path_)) {
        auto files = content();
        if (!files.remove || !files.remove(resolvedUri())) throw std::runtime_error("kompoz: can't delete " + path_);
        return;
    }
    std::error_code error;
    fs::remove_all(pathOf(path_), error);
    if (error) throw std::runtime_error("kompoz: can't delete " + path_ + ": " + error.message());
}

// ---- the dialogs ------------------------------------------------------------------------------

void KompozHost::showFileDialog(detail::FileDialogRequest request, std::function<void(detail::FileDialogResult)> done) {
    if (!callbacks_.fileDialog) {
        runOnUiThread([done = std::move(done)] { done({.error = "kompoz: there are no file dialogs here"}); });
        return;
    }
    auto once = std::make_shared<std::atomic<bool>>(false);
    callbacks_.fileDialog(request, [done = std::move(done), once](detail::FileDialogResult result) mutable {
        if (once->exchange(true)) return;
        runOnUiThread([done, result = std::move(result)]() mutable { done(std::move(result)); });
    });
}

std::shared_ptr<detail::FileLauncherData> detail::rememberFileLauncher(CallSite site) {
    KompozableScope scope(site);
    KompozHost& host = Kompozer::require().host();
    auto data = remember([&host] {
        auto d = std::make_shared<FileLauncherData>();
        d->host = &host;
        return d;
    });
    DisposableEffect(0, [data]() -> std::function<void()> {
        data->active = true;
        return [data] { data->active = false; };
    });
    return data;
}

void detail::launchFileDialog(const std::shared_ptr<FileLauncherData>& data, FileDialogRequest request) {
    if (!data || !data->host) return;
    data->host->showFileDialog(std::move(request), [data](FileDialogResult result) {
        if (data->active && data->deliver) data->deliver(std::move(result));
    });
}

void detail::FileDialogAwaiter::suspend(JobState& job, std::coroutine_handle<> h) {
    std::weak_ptr<JobState> weak = job.weak_from_this();
    Kompozer* kompozer = job.dispatcher ? job.dispatcher->kompozer : nullptr;
    if (!kompozer) {
        *result = {.error = "kompoz: a file dialog needs a coroutine of a composition's"};
        runOnUiThread([weak, h] {
            if (auto j = weak.lock()) j->dispatcher->resumeNow(j, h);
        });
        return;
    }
    kompozer->host().showFileDialog(std::move(request), [out = result, weak, h](FileDialogResult r) {
        *out = std::move(r);
        if (auto j = weak.lock()) j->dispatcher->resumeNow(j, h);  // (not if cancelled meanwhile)
    });
}

#ifdef __EMSCRIPTEN__
// clang-format off
EM_JS(void, kompoz_download, (const uint8_t* bytes, int size, const char* name), {
    const blob = new Blob([HEAPU8.slice(bytes, bytes + size)]);
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = UTF8ToString(name);
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
});
// clang-format on

void FileKit::download(const std::vector<uint8_t>& bytes, const std::string& fileName) {
    kompoz_download(bytes.data(), int(bytes.size()), fileName.c_str());
}
#endif

} // namespace kompoz
