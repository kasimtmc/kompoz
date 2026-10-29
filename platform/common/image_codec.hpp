#pragma once

// PNG and BMP read, PNG written: images decoded where the platform has nothing of its own for them
// (Linux and macOS, the web's imageResource), and screenshots and test pictures written on every
// platform. PNG is libpng's (gamma and color space chunks honored, as Android's and the browsers'
// decoders do); BMP is read here: 1, 4, 8, 16, 24 and 32 bits a pixel, uncompressed or with bit
// fields.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kompoz::platform {

// Pixels as RGBA bytes, not premultiplied, the top row first; empty when nothing was read.
struct RgbaImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    bool empty() const { return rgba.empty(); }
};

RgbaImage decodePng(const uint8_t* data, size_t size);
RgbaImage decodeBmp(const uint8_t* data, size_t size);
// Either, by what the bytes are.
RgbaImage decodePngOrBmp(const uint8_t* data, size_t size);

std::vector<uint8_t> encodePng(int width, int height, const uint8_t* rgba);
// Written to `path` (UTF-8); false if it couldn't be.
bool savePng(const std::string& path, int width, int height, const uint8_t* rgba);

} // namespace kompoz::platform
