#pragma once

// Images for AsyncImage and imageResource: read from the app's assets, a file or an http(s)
// URL, decoded with what each platform has (SDL for PNG/BMP and libjpeg-turbo/libwebp on
// desktop Linux, BitmapFactory on Android, WIC on Windows, the browser on the web).

#include <kompoz/graphics.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace kompoz::sdl {

// An image file's bytes decoded (RGBA, straight alpha), scaled down for `size` as
// imageSampleSize says (as they are decoded where the decoder can); empty if they can't be.
// Not on the web, where the browser decodes asynchronously.
ImageBitmap decodeImage(const std::vector<uint8_t>& bytes, const ImageRequestSize& size = {});
// A file's bytes: from the app's assets (the APK on Android), or next to the binary.
std::vector<uint8_t> readFile(const std::string& path);
// Loads `model` (a path or an http(s) URL) for `size` away from the UI thread and calls
// `done` with it, from any thread; empty if it failed. Downloads are kept in the disk cache.
void loadImageAsync(const std::string& model, const ImageRequestSize& size, std::function<void(ImageBitmap)> done);
// The app's own cache directory, where the system keeps apps' caches (a directory named after the
// app); empty where there is none (the web).
std::filesystem::path appCacheDirectory(const std::string& appName);
// Where downloaded images are kept: the app's cache directory's images (KOMPOZ_IMAGE_CACHE overrides
// it, empty for none), up to maxBytes (0: none). Before the first load.
void startImageDiskCache(const std::string& appName, uint64_t maxBytes);
// At exit: loads waiting are dropped, the running ones stop soon; the worker threads end.
void stopImageLoader();

} // namespace kompoz::sdl
