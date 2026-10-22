#pragma once

// Text for the SDL3 backend: fonts loaded with FreeType (the bundled ones, the generic families'
// system fonts, fallbacks for what they lack), text shaped by HarfBuzz into glyphs, and glyphs
// rasterized into the glyph atlas (platform/gpu/glyph_atlas.hpp), which every renderer draws from.

#include <kompoz/graphics.hpp>

#include "../gpu/glyph_atlas.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kompoz::sdl {

class FontSystem final : public TextMeasurer, public gpu::TextSource {
public:
    FontSystem();
    ~FontSystem() override;

    // The bundled sans-serif in one weight (Normal, Medium or Bold).
    bool load(FontWeight weight, const std::string& preferredPath);

    void setScale(float scale);  // px per dp
    void setFontScale(float fontScale);
    FT_Library library() const;
    int rasterized() const;  // glyphs rasterized so far (KOMPOZ_PROFILE)

    float measureWidth(std::string_view utf8, const Font& font) override;
    FontMetrics metrics(const Font& font) override;
    float fontScale() const override;

    // The glyphs of text drawn from the pen at (xPx, baselinePx), appended to `out`. `shadow`:
    // only the glyphs that take a color (no emoji), blurred by `blurPx`. `scale`: its px per dp,
    // if not setScale's.
    void layout(std::string_view text, float xPx, float baselinePx, const Font& font, bool rtl, std::vector<gpu::GlyphQuad>& out,
                bool shadow = false, int blurPx = 0, float scale = 0) override;

    gpu::GlyphAtlas& atlas() override;
    // After each frame (the atlas reuses pages not drawn from for a while).
    void endFrame();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kompoz::sdl
