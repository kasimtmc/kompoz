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

namespace kompoz::platform {

// An image file's bytes decoded (RGBA, straight alpha), scaled down for `size` as
// imageSampleSize says (as they are decoded where the decoder can); empty if they can't be.
// Not on the web, where the browser decodes asynchronously.
ImageBitmap decodeImage(const std::vector<uint8_t>& bytes, const ImageRequestSize& size = {});
// Loads `model` (a path or an http(s) URL) for `size` away from the UI thread and calls
// `done` with it, from any thread; empty if it failed. Downloads are kept in the disk cache.
void loadImageAsync(const std::string& model, const ImageRequestSize& size, std::function<void(ImageBitmap)> done);
// Where downloaded images are kept: the app's cache directory's images (KOMPOZ_IMAGE_CACHE overrides
// it, empty for none), up to maxBytes (0: none). Before the first load.
void startImageDiskCache(const std::string& appName, uint64_t maxBytes);
// At exit: loads waiting are dropped, the running ones stop soon; the worker threads end.
void stopImageLoader();
// An image asset (the APK's on Android) or a file next to the binary, decoded; empty if it can't be.
ImageBitmap loadImageFile(std::string_view path);
// A screenshot from pixels read back (RGBA, top row first): PNG where the path says so, else BMP.
void savePixels(const std::string& path, int w, int h, std::vector<uint8_t>& pixels);

} // namespace kompoz::platform
