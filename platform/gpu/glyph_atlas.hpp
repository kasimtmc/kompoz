#pragma once

// The glyph atlas: rasterized glyphs (and ImageVectors) packed into pages of RGBA pixels kept on
// the CPU, which a renderer uploads to its own textures as they change. White with coverage in
// alpha for glyphs that take the text's color, straight-alpha colors for emoji.
//
// A page that fills up leaves room to the next one; pages not drawn from for a while are cleared
// for reuse between frames, never during one (a frame's glyphs stay where they were put until it
// has been drawn).

#include <kompoz/graphics.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace kompoz::gpu {

// Where a glyph is drawn (px, from the frame's origin) and where it is in the atlas.
struct GlyphQuad {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int page = 0;
    int u = 0, v = 0, width = 0, height = 0;  // texels on the page
    bool color = false;                       // drawn as it is (an emoji), not tinted
};

// A rectangle of texels that changed since the renderer last uploaded the page.
struct DirtyRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x1 <= x0 || y1 <= y0; }
    void add(int ax0, int ay0, int ax1, int ay1) {
        if (empty()) {
            *this = {ax0, ay0, ax1, ay1};
        } else {
            x0 = std::min(x0, ax0), y0 = std::min(y0, ay0), x1 = std::max(x1, ax1), y1 = std::max(y1, ay1);
        }
    }
};

class AtlasPage {
public:
    static constexpr int kSize = 1024;
    AtlasPage() : rgba_(size_t(kSize) * kSize * 4, 0) { dirty_.add(0, 0, kSize, kSize); }

    const uint8_t* pixels() const { return rgba_.data(); }
    // What changed since the last call (the whole page at first, and after it was cleared).
    DirtyRect takeDirty() { return std::exchange(dirty_, {}); }
    // All of it is to be uploaded again (a renderer's texture for it was new, or lost).
    void markAllDirty() { dirty_ = {0, 0, kSize, kSize}; }
    // Cleared each time it is reused: what was cached on it is gone.
    uint64_t generation() const { return generation_; }

private:
    friend class GlyphAtlas;
    std::vector<uint8_t> rgba_;
    DirtyRect dirty_;
    uint64_t generation_ = 0;
    int shelfX_ = 0, shelfY_ = 0, shelfHeight_ = 0;
    uint64_t lastUsed_ = 0;  // the frame it was last drawn from
};

class GlyphAtlas {
public:
    // A spot for a w x h glyph (pixels: w * h straight-alpha RGBA), on the current page or a new
    // one; false if it is too big for a page.
    bool add(const uint8_t* pixels, int w, int h, int& page, int& u, int& v) {
        if (w + 1 > AtlasPage::kSize || h + 1 > AtlasPage::kSize) return false;
        if (pages_.empty() || !fits(*pages_[size_t(current_)], w, h)) current_ = freshPage();
        AtlasPage& p = *pages_[size_t(current_)];
        if (p.shelfX_ + w + 1 > AtlasPage::kSize) {  // next shelf
            p.shelfX_ = 0;
            p.shelfY_ += p.shelfHeight_;
            p.shelfHeight_ = 0;
        }
        page = current_;
        u = p.shelfX_;
        v = p.shelfY_;
        p.shelfX_ += w + 1;
        p.shelfHeight_ = std::max(p.shelfHeight_, h + 1);
        for (int row = 0; row < h; ++row)
            std::copy_n(pixels + size_t(row) * size_t(w) * 4, size_t(w) * 4,
                        p.rgba_.data() + (size_t(v + row) * AtlasPage::kSize + size_t(u)) * 4);
        p.dirty_.add(u, v, u + w, v + h);
        p.lastUsed_ = frame_;
        return true;
    }

    // A glyph on `page` is drawn this frame: the page stays until the frame is over.
    void use(int page) { pages_[size_t(page)]->lastUsed_ = frame_; }
    bool valid(int page, uint64_t generation) const {
        return page >= 0 && page < int(pages_.size()) && pages_[size_t(page)]->generation_ == generation;
    }
    uint64_t generation(int page) const { return pages_[size_t(page)]->generation_; }

    int pageCount() const { return int(pages_.size()); }
    AtlasPage& page(int i) { return *pages_[size_t(i)]; }

    // Between frames: with more pages than `kKeep`, the ones unused longest are cleared for
    // reuse (their glyphs are rasterized again when drawn).
    void endFrame() {
        ++frame_;
        if (int(pages_.size()) <= kKeep) return;
        std::vector<int> order;
        for (int i = 0; i < int(pages_.size()); ++i) order.push_back(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return pages_[size_t(a)]->lastUsed_ < pages_[size_t(b)]->lastUsed_; });
        for (size_t k = 0; k + kKeep < order.size(); ++k)
            if (order[k] != current_ && frame_ - pages_[size_t(order[k])]->lastUsed_ > 2) clear(order[k]);
    }

private:
    static constexpr int kKeep = 4;  // pages kept whatever happens (16 MB)

    static bool fits(const AtlasPage& p, int w, int h) {
        int y = p.shelfX_ + w + 1 > AtlasPage::kSize ? p.shelfY_ + p.shelfHeight_ : p.shelfY_;
        return y + h + 1 <= AtlasPage::kSize;
    }
    // A page with room: one cleared earlier (the emptiest), else a new one.
    int freshPage() {
        for (int i = 0; i < int(pages_.size()); ++i)
            if (i != current_ && pages_[size_t(i)]->shelfX_ == 0 && pages_[size_t(i)]->shelfY_ == 0) return i;
        pages_.push_back(std::make_unique<AtlasPage>());
        return int(pages_.size()) - 1;
    }
    void clear(int i) {
        AtlasPage& p = *pages_[size_t(i)];
        std::fill(p.rgba_.begin(), p.rgba_.end(), uint8_t(0));
        p.dirty_ = {};
        p.dirty_.add(0, 0, AtlasPage::kSize, AtlasPage::kSize);
        p.shelfX_ = p.shelfY_ = p.shelfHeight_ = 0;
        ++p.generation_;
    }

    std::vector<std::unique_ptr<AtlasPage>> pages_;
    int current_ = 0;
    uint64_t frame_ = 1;
};

// Where a renderer's text comes from: the platform's font system lays text out as glyphs on the
// atlas, and rasterizes ImageVectors there.
class TextSource {
public:
    virtual ~TextSource() = default;
    // The glyphs of text drawn from the pen at (xPx, baselinePx), appended to `out`. `shadow`:
    // only the glyphs that take a color (no emoji), blurred by `blurPx`.
    virtual void layout(std::string_view text, float xPx, float baselinePx, const Font& font, bool rtl, std::vector<GlyphQuad>& out,
                        bool shadow = false, int blurPx = 0) = 0;
    // `vector` filled into the px rect (x, y, w, h); false if there is nothing to draw.
    virtual bool vectorQuad(const ImageVector& vector, float x, float y, float w, float h, GlyphQuad& out) = 0;
    virtual GlyphAtlas& atlas() = 0;
};

} // namespace kompoz::gpu
