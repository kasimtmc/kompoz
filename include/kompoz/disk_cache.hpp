#pragma once

// Files kept in a directory by key, the least recently used removed once they add up to more
// than a size: where AsyncImage keeps the images it downloaded (Coil's DiskCache), so they
// show again without the network, after the app is restarted too. Any thread may use it.

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kompoz {

class DiskCache {
public:
    DiskCache(std::filesystem::path directory, uint64_t maxBytes);

    std::optional<std::vector<uint8_t>> get(const std::string& key);  // and it counts as used now
    void put(const std::string& key, const std::vector<uint8_t>& bytes);
    void remove(const std::string& key);
    void clear();
    uint64_t size();  // the bytes kept
    const std::filesystem::path& directory() const { return dir_; }

private:
    struct File {
        uint64_t bytes = 0;
        uint64_t used = 0;
    };
    void open();  // on first use: what is already there, oldest first
    void trim();
    void forget(const std::string& name);

    std::filesystem::path dir_;
    uint64_t max_;
    std::mutex mutex_;
    bool opened_ = false;
    std::unordered_map<std::string, File> files_;  // by file name
    uint64_t total_ = 0, clock_ = 0, temp_ = 0;
};

} // namespace kompoz
