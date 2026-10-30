// What every platform's modules need of the system, without a platform library: the log, the
// app's files, the program's directory, the cache directory. A platform sets what is its own
// (its log, Android's APK assets and cache directory).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace kompoz::platform {

// A line into the platform's log (stderr until it says where: setLogSink). Any thread.
void log(const std::string& message);
void setLogSink(void (*sink)(const char* message));

// The directory the program's binary is in, with a separator at its end (a macOS app bundle's
// Resources); empty where there is none (Android, the web).
const std::string& programDirectory();

// A file's bytes: the path as it is, or the platform's assets for it (setAssetReader: Android's
// APK), or (a relative one) next to the program; empty if none of them has it.
std::vector<uint8_t> readFile(const std::string& path);
void setAssetReader(std::function<std::vector<uint8_t>(const std::string& path)> reader);

// The app's own cache directory, where the system keeps apps' caches (a directory named after the
// app; setCacheDirectory: the platform's own, Android's); empty where there is none (the web).
std::filesystem::path appCacheDirectory(const std::string& appName);
void setCacheDirectory(std::filesystem::path directory);

// What a module working on a thread of its own (a screen reader's bridge, the accent color's
// listener) tells the platform with: `notify` (from that thread) that it has something for the
// main thread, which then asks it; `threadDone` as its thread ends.
struct Notifier {
    std::function<void()> notify;
    std::function<void()> threadDone;
};

// The language the environment asks for (LANGUAGE, LC_ALL, LC_MESSAGES, LANG: "tr_TR.UTF-8" is
// "tr"); on Windows, where those are seldom set, the user's locale. Empty: it doesn't say.
std::string preferredLanguage();

} // namespace kompoz::platform
