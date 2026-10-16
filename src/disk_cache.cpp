#include <kompoz/disk_cache.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace kompoz {

namespace fs = std::filesystem;

namespace {

// Each file: a mark, the key (two keys with the same file name aren't mixed up), the bytes.
constexpr char kMark[4] = {'K', 'Z', 'D', '1'};

std::string fileName(const std::string& key) {
    uint64_t h = 14695981039346656037ull;  // FNV-1a
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char out[17];
    std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
    return out;
}

bool isHex(const std::string& s, size_t from, size_t count) {
    if (s.size() < from + count) return false;
    for (size_t i = from; i < from + count; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}
bool isEntry(const std::string& name) { return name.size() == 16 && isHex(name, 0, 16); }
// What a write leaves until it is renamed into place ("<entry>.<n>.tmp").
bool isTemp(const std::string& name) { return name.size() > 21 && isHex(name, 0, 16) && name[16] == '.' && name.ends_with(".tmp"); }

std::vector<uint8_t> header(const std::string& key) {
    std::vector<uint8_t> out(kMark, kMark + 4);
    for (uint64_t n = key.size(); ; n >>= 7) {
        out.push_back(uint8_t(n & 0x7F) | (n >= 0x80 ? 0x80 : 0));
        if (n < 0x80) break;
    }
    out.insert(out.end(), key.begin(), key.end());
    return out;
}

} // namespace

DiskCache::DiskCache(fs::path directory, uint64_t maxBytes) : dir_(std::move(directory)), max_(maxBytes) {}

void DiskCache::open() {
    if (opened_) return;
    opened_ = true;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    struct Found {
        std::string name;
        uint64_t bytes;
        fs::file_time_type time;
    };
    std::vector<Found> found;
    std::vector<fs::path> unfinished;
    for (fs::directory_iterator it(dir_, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e;
        std::string name = it->path().filename().string();
        if (!it->is_regular_file(e)) continue;
        if (isTemp(name)) unfinished.push_back(it->path());
        else if (isEntry(name)) found.push_back({name, uint64_t(it->file_size(e)), it->last_write_time(e)});
    }
    for (const fs::path& p : unfinished) fs::remove(p, ec);
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.time < b.time; });
    for (const Found& f : found) {
        files_[f.name] = {f.bytes, ++clock_};
        total_ += f.bytes;
    }
    trim();
}

std::optional<std::vector<uint8_t>> DiskCache::get(const std::string& key) {
    std::lock_guard lock(mutex_);
    open();
    std::string name = fileName(key);
    auto it = files_.find(name);
    if (it == files_.end()) return std::nullopt;
    fs::path path = dir_ / name;
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> expected = header(key);
    std::vector<uint8_t> head(expected.size());
    if (!in.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size()))) {
        forget(name);  // gone, or cut short
        return std::nullopt;
    }
    if (head != expected) {
        if (!std::equal(kMark, kMark + 4, head.begin())) forget(name);
        return std::nullopt;  // another key's (or not ours)
    }
    in.seekg(0, std::ios::end);
    auto end = in.tellg();
    if (end < std::streamoff(expected.size())) return std::nullopt;
    std::vector<uint8_t> bytes(size_t(end) - expected.size());
    in.seekg(std::streamoff(expected.size()));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()))) return std::nullopt;
    it->second.used = ++clock_;
    std::error_code ec;
    fs::last_write_time(path, fs::file_time_type::clock::now(), ec);  // the order, for the next run
    return bytes;
}

void DiskCache::put(const std::string& key, const std::vector<uint8_t>& bytes) {
    std::lock_guard lock(mutex_);
    std::vector<uint8_t> head = header(key);
    uint64_t size = head.size() + bytes.size();
    if (size > max_) return;  // it would never fit
    open();
    std::string name = fileName(key);
    // Written beside it and renamed into place: a reader (or the next run, after a crash)
    // never sees half of it.
    auto stamp = uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::path temp = dir_ / (name + "." + std::to_string(stamp % 1000000007 + ++temp_) + ".tmp");
    std::error_code ec;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(head.data()), std::streamsize(head.size()));
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!out.flush()) {
            out.close();
            fs::remove(temp, ec);
            return;
        }
    }
    fs::path path = dir_ / name;
    fs::rename(temp, path, ec);
    if (ec) {  // where renaming doesn't replace
        std::error_code e;
        fs::remove(path, e);
        fs::rename(temp, path, ec);
    }
    if (ec) {
        fs::remove(temp, ec);
        return;
    }
    if (auto it = files_.find(name); it != files_.end()) total_ -= it->second.bytes;
    files_[name] = {size, ++clock_};
    total_ += size;
    trim();
}

void DiskCache::remove(const std::string& key) {
    std::lock_guard lock(mutex_);
    open();
    std::string name = fileName(key);
    if (!files_.contains(name)) return;
    std::error_code ec;
    fs::remove(dir_ / name, ec);
    forget(name);
}

void DiskCache::clear() {
    std::lock_guard lock(mutex_);
    open();
    std::error_code ec;
    for (const auto& [name, file] : files_) fs::remove(dir_ / name, ec);
    files_.clear();
    total_ = 0;
}

uint64_t DiskCache::size() {
    std::lock_guard lock(mutex_);
    open();
    return total_;
}

void DiskCache::forget(const std::string& name) {
    auto it = files_.find(name);
    if (it == files_.end()) return;
    total_ -= it->second.bytes;
    files_.erase(it);
}

void DiskCache::trim() {
    if (total_ <= max_) return;
    std::vector<std::pair<uint64_t, std::string>> byUse;
    for (const auto& [name, file] : files_) byUse.push_back({file.used, name});
    std::sort(byUse.begin(), byUse.end());
    std::error_code ec;
    for (const auto& [used, name] : byUse) {
        if (total_ <= max_) break;
        fs::remove(dir_ / name, ec);
        forget(name);
    }
}

} // namespace kompoz
