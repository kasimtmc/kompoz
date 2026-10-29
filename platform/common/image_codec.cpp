#include "image_codec.hpp"

#include <png.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace kompoz::platform {

namespace {

// What is decoded at most: 32768 pixels each way and 2^26 in all (256 MB), more than is drawn
// (images are scaled down to maxImageDimension) and not more than memory takes.
bool sizeOk(int64_t width, int64_t height) { return width > 0 && height > 0 && width <= 32768 && height <= 32768 && width * height <= (int64_t(1) << 26); }

// A channel from a pixel by its mask, as a byte.
uint8_t channel(uint32_t pixel, uint32_t mask) {
    if (mask == 0) return 0;
    int shift = 0;
    while (!((mask >> shift) & 1)) ++shift;
    uint64_t max = mask >> shift;
    return uint8_t(uint64_t((pixel & mask) >> shift) * 255 / max);
}

} // namespace

RgbaImage decodePng(const uint8_t* data, size_t size) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data, size)) return {};
    if (!sizeOk(image.width, image.height)) {
        png_image_free(&image);
        return {};
    }
    image.format = PNG_FORMAT_RGBA;
    // A 16-bit file that says nothing of its gamma is sRGB, as everyone else reads it (libpng's own
    // default is linear).
    image.flags |= PNG_IMAGE_FLAG_16BIT_sRGB;
    RgbaImage out{int(image.width), int(image.height), std::vector<uint8_t>(PNG_IMAGE_SIZE(image))};
    if (!png_image_finish_read(&image, nullptr, out.rgba.data(), 0, nullptr)) return {};  // (freed either way)
    return out;
}

RgbaImage decodeBmp(const uint8_t* d, size_t n) {
    auto u16 = [d](size_t at) { return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8; };
    auto u32 = [&](size_t at) { return u16(at) | u16(at + 2) << 16; };
    if (n < 26 || d[0] != 'B' || d[1] != 'M') return {};
    uint32_t offset = u32(10), header = u32(14);
    int64_t width = 0, height = 0;
    uint32_t bpp = 0, compression = 0, colors = 0;
    size_t paletteAt = 14 + size_t(header), entry = 4;
    if (header == 12) {  // OS/2's: 16-bit sizes, 3-byte palette entries
        width = u16(18), height = int16_t(u16(20)), bpp = u16(24), entry = 3;
    } else if (header >= 40 && header <= 124 && n >= 54) {
        width = int32_t(u32(18)), height = int32_t(u32(22)), bpp = u16(28), compression = u32(30), colors = u32(46);
    } else {
        return {};
    }
    bool topDown = height < 0;  // (else the bottom row first)
    height = std::abs(height);
    if (!sizeOk(width, height)) return {};
    uint32_t masks[4] = {};  // red, green, blue, alpha
    if (compression == 3 || compression == 6) {  // BI_BITFIELDS, BI_ALPHABITFIELDS: the masks after the header's first 40 bytes
        if (bpp != 16 && bpp != 32) return {};
        size_t count = compression == 6 || header >= 56 ? 4 : 3;
        if (n < 54 + count * 4) return {};
        for (size_t i = 0; i < count; ++i) masks[i] = u32(54 + i * 4);
    } else if (compression != 0) {
        return {};  // run-length encoded, or a JPEG or PNG inside: not read
    } else if (bpp == 16) {
        masks[0] = 0x7C00, masks[1] = 0x03E0, masks[2] = 0x001F;
    } else if (bpp == 32) {  // the fourth byte taken as alpha, as most write it, unless it is 0 everywhere
        masks[0] = 0xFF0000, masks[1] = 0xFF00, masks[2] = 0xFF, masks[3] = 0xFF000000;
    }
    std::vector<uint8_t> palette;  // RGB
    if (bpp == 1 || bpp == 4 || bpp == 8) {
        size_t count = colors ? colors : size_t(1) << bpp;
        if (count > 256 || paletteAt + count * entry > n) return {};
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* e = d + paletteAt + i * entry;
            palette.insert(palette.end(), {e[2], e[1], e[0]});
        }
    } else if (bpp != 16 && bpp != 24 && bpp != 32) {
        return {};
    }
    size_t stride = (size_t(width) * bpp + 31) / 32 * 4;
    if (offset > n || (n - offset) / stride < size_t(height)) return {};
    RgbaImage out{int(width), int(height), std::vector<uint8_t>(size_t(width) * size_t(height) * 4)};
    bool alphaSeen = false;
    for (int64_t y = 0; y < height; ++y) {
        const uint8_t* row = d + offset + stride * size_t(topDown ? y : height - 1 - y);
        uint8_t* o = out.rgba.data() + size_t(y) * size_t(width) * 4;
        for (int64_t x = 0; x < width; ++x, o += 4) {
            if (!palette.empty()) {
                size_t index = bpp == 8 ? row[x] : bpp == 4 ? (row[x / 2] >> (x % 2 ? 0 : 4)) & 15 : (row[x / 8] >> (7 - x % 8)) & 1;
                if (index * 3 < palette.size()) std::memcpy(o, &palette[index * 3], 3);  // (else black)
                o[3] = 255;
            } else if (bpp == 24) {
                o[0] = row[x * 3 + 2], o[1] = row[x * 3 + 1], o[2] = row[x * 3], o[3] = 255;
            } else {
                uint32_t pixel = bpp == 16 ? u16(size_t(row - d) + size_t(x) * 2) : u32(size_t(row - d) + size_t(x) * 4);
                o[0] = channel(pixel, masks[0]), o[1] = channel(pixel, masks[1]), o[2] = channel(pixel, masks[2]);
                o[3] = masks[3] ? channel(pixel, masks[3]) : 255;
                alphaSeen = alphaSeen || (masks[3] && o[3] != 0);
            }
        }
    }
    if (masks[3] && !alphaSeen)  // an alpha of 0 everywhere: not alpha after all
        for (size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;
    return out;
}

RgbaImage decodePngOrBmp(const uint8_t* data, size_t size) {
    if (size >= 8 && std::memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) return decodePng(data, size);
    if (size >= 2 && data[0] == 'B' && data[1] == 'M') return decodeBmp(data, size);
    return {};
}

std::vector<uint8_t> encodePng(int width, int height, const uint8_t* rgba) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = png_uint_32(width), image.height = png_uint_32(height);
    image.format = PNG_FORMAT_RGBA;
    std::vector<uint8_t> out(PNG_IMAGE_PNG_SIZE_MAX(image));  // (as large as it can come out: compressed once)
    png_alloc_size_t size = out.size();
    if (width <= 0 || height <= 0 || !png_image_write_to_memory(&image, out.data(), &size, 0, rgba, 0, nullptr)) return {};
    out.resize(size);
    return out;
}

bool savePng(const std::string& path, int width, int height, const uint8_t* rgba) {
    std::vector<uint8_t> bytes = encodePng(width, height, rgba);
    if (bytes.empty()) return false;
    std::ofstream file(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    return file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())) && file.flush();
}

} // namespace kompoz::platform
