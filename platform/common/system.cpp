#include "system.hpp"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace kompoz::platform {

namespace {
std::atomic<void (*)(const char*)> gSink{nullptr};
std::mutex gMutex;  // the asset reader and the cache directory
std::function<std::vector<uint8_t>(const std::string&)> gAssets;
std::filesystem::path gCache;
bool gCacheSet = false;

std::filesystem::path utf8Path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

bool readPlain(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    std::streamoff size = in.tellg();
    if (size <= 0) return false;
    out.resize(size_t(size));
    in.seekg(0);
    return bool(in.read(reinterpret_cast<char*>(out.data()), size));
}

std::string findProgramDirectory() {
#if defined(__ANDROID__)
    return {};  // (the app's files are the APK's assets)
#elif defined(__EMSCRIPTEN__)
    return "/";  // (the files packaged with the page)
#elif defined(_WIN32)
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    std::u8string dir = std::filesystem::path(path).parent_path().u8string();
    return std::string(dir.begin(), dir.end()) + "\\";
#else
    std::filesystem::path exe;
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    std::error_code ec;
    exe = std::filesystem::canonical(buffer.c_str(), ec);
    if (ec) return {};
#else
    std::error_code ec;
    exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
#endif
    std::string dir = exe.parent_path().string();
#if defined(__APPLE__)
    // In an app bundle, what it brings is in its Resources.
    if (dir.ends_with(".app/Contents/MacOS")) dir.replace(dir.size() - 5, 5, "Resources");
#endif
    return dir + "/";
#endif
}
} // namespace

void log(const std::string& message) {
    if (auto sink = gSink.load()) sink(message.c_str());
    else std::fprintf(stderr, "%s\n", message.c_str());
}

void setLogSink(void (*sink)(const char* message)) { gSink = sink; }

const std::string& programDirectory() {
    static const std::string dir = findProgramDirectory();
    return dir;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    if (path.empty()) return bytes;
    if (readPlain(utf8Path(path), bytes)) return bytes;
    std::function<std::vector<uint8_t>(const std::string&)> assets;
    {
        std::lock_guard lock(gMutex);
        assets = gAssets;
    }
    if (assets)
        if (bytes = assets(path); !bytes.empty()) return bytes;
    bool absolute = path.front() == '/' || path.front() == '\\' || (path.size() > 1 && path[1] == ':');
    if (!absolute && !programDirectory().empty() && readPlain(utf8Path(programDirectory() + path), bytes)) return bytes;
    return {};
}

void setAssetReader(std::function<std::vector<uint8_t>(const std::string& path)> reader) {
    std::lock_guard lock(gMutex);
    gAssets = std::move(reader);
}

std::filesystem::path appCacheDirectory(const std::string& appName) {
    {
        std::lock_guard lock(gMutex);
        if (gCacheSet) return gCache;
    }
#ifdef __EMSCRIPTEN__
    (void)appName;  // (the browser caches)
    return {};
#else
    // A name that makes a directory.
    std::string name;
    for (char c : appName) name += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || uint8_t(c) >= 0x80) ? c : '_';
    if (name.empty() || name[0] == '.') name = "app" + name;
#if defined(_WIN32)
    if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA")) return std::filesystem::path(local) / "Kompoz" / utf8Path(name);  // (wide: a user's name in any letters)
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / "Library" / "Caches" / utf8Path(name);
#elif !defined(__ANDROID__)
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) return std::filesystem::path(xdg) / "kompoz" / name;
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / ".cache" / "kompoz" / name;
#endif
    return {};
#endif
}

void setCacheDirectory(std::filesystem::path directory) {
    std::lock_guard lock(gMutex);
    gCache = std::move(directory);
    gCacheSet = true;
}

} // namespace kompoz::platform
