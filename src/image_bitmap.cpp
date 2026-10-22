#include <kompoz/display_list.hpp>
#include <kompoz/runtime.hpp>

#include <atomic>
#include <cmath>
#include <future>
#include <mutex>
#include <thread>

namespace kompoz {

struct ImageBitmap::Data {
    int width = 0, height = 0;
    ImageBitmapConfig config = ImageBitmapConfig::Argb8888;
    bool hasAlpha = true;
    const ColorSpace* colorSpace = &ColorSpaces::Srgb;
    uint64_t id = 0;
    std::mutex mutex;
    uint64_t version = 1;     // what it holds now
    uint64_t cpuVersion = 1;  // what `rgba` (and `floats`) hold
    std::vector<uint8_t> rgba;   // straight RGBA, 8 bits a channel, as its config keeps them (Alpha8: black; Rgb565: 5-6-5 values)
    std::vector<float> floats;   // F16's, straight RGBA, at half floats' precision
    std::vector<std::shared_ptr<const DisplayList>> pending;  // drawn into it with no renderer there
};

namespace {

std::atomic<uint64_t> nextId{1};

std::shared_ptr<ImageBitmap::Data> make(int width, int height, ImageBitmapConfig config, bool hasAlpha) {
    auto d = std::make_shared<ImageBitmap::Data>();
    d->width = width, d->height = height, d->config = config, d->id = nextId++;
    d->hasAlpha = config == ImageBitmapConfig::Alpha8 || (config != ImageBitmapConfig::Rgb565 && hasAlpha);
    return d;
}

// Rounded to what a half float holds (11 significant bits, as far as it reaches).
float half(float v) {
    if (!std::isfinite(v) || v == 0) return v;
    float a = std::min(std::abs(v), 65504.f);
    int e;
    std::frexp(a, &e);
    float step = std::ldexp(1.f, std::max(e, -13) - 11);  // (subnormals below 2^-14: steps of 2^-24)
    return std::copysign(std::round(a / step) * step, v);
}

uint8_t byte(float v) { return uint8_t(std::lround(std::clamp(v, 0.f, 1.f) * 255)); }

struct Renderer {
    std::mutex mutex;
    detail::BitmapRenderer* renderer = nullptr;
    std::thread::id uiThread;
    std::vector<std::weak_ptr<ImageBitmap::Data>> waiting;  // bitmaps with draws pending
};
Renderer& renderer() {
    static Renderer r;
    return r;
}

// `task` on the UI thread: here if this is it, else there, waited for.
void onUiThread(const std::function<void()>& task) {
    Renderer& r = renderer();
    std::thread::id ui;
    {
        std::lock_guard lock(r.mutex);
        ui = r.uiThread;
    }
    if (std::this_thread::get_id() == ui) return task();
    std::promise<void> done;
    runOnUiThread([&] {
        task();
        done.set_value();
    });
    done.get_future().wait();
}

detail::BitmapRenderer* current() {
    std::lock_guard lock(renderer().mutex);
    return renderer().renderer;
}

// A measurer for recordings when there is no renderer: text as wide as half its size a character.
class NoFonts final : public TextMeasurer {
public:
    float measureWidth(std::string_view utf8, const Font& font) override { return float(utf8.size()) * font.size / 2; }
    FontMetrics metrics(const Font& font) override { return {font.size * 0.8f, font.size * 0.2f, font.size}; }
};

} // namespace

ImageBitmap::ImageBitmap(int width, int height, ImageBitmapConfig config, bool hasAlpha, const ColorSpace& colorSpace) {
    if (width <= 0 || height <= 0 || width > maxImageDimension || height > maxImageDimension) return;
    auto d = make(width, height, config, hasAlpha);
    d->colorSpace = &colorSpace;
    d->rgba.assign(size_t(width) * size_t(height) * 4, 0);
    if (!d->hasAlpha)  // black and opaque
        for (size_t i = 3; i < d->rgba.size(); i += 4) d->rgba[i] = 255;
    if (config == ImageBitmapConfig::F16) {
        d->floats.assign(d->rgba.size(), 0.f);
        if (!d->hasAlpha)
            for (size_t i = 3; i < d->floats.size(); i += 4) d->floats[i] = 1;
    }
    data_ = std::move(d);
}

ImageBitmap ImageBitmap::fromPixels(int width, int height, std::vector<uint8_t> rgba) {
    ImageBitmap image;
    if (width <= 0 || height <= 0 || rgba.size() < size_t(width) * size_t(height) * 4) return image;
    auto d = make(width, height, ImageBitmapConfig::Argb8888, true);
    d->rgba = std::move(rgba);
    image.data_ = std::move(d);
    return image;
}

int ImageBitmap::width() const { return data_ ? data_->width : 0; }
int ImageBitmap::height() const { return data_ ? data_->height : 0; }
ImageBitmapConfig ImageBitmap::config() const { return data_ ? data_->config : ImageBitmapConfig::Argb8888; }
bool ImageBitmap::hasAlpha() const { return !data_ || data_->hasAlpha; }
const ColorSpace& ImageBitmap::colorSpace() const { return data_ ? *data_->colorSpace : ColorSpaces::Srgb; }
uint64_t ImageBitmap::id() const { return data_ ? data_->id : 0; }

uint64_t ImageBitmap::version() const {
    if (!data_) return 0;
    std::lock_guard lock(data_->mutex);
    return data_->version;
}

const uint8_t* ImageBitmap::pixels() const {
    if (!data_) return nullptr;
    if (!detail::bitmaps::pixelsCurrent(*this))
        onUiThread([&] {
            if (detail::bitmaps::pixelsCurrent(*this)) return;
            if (detail::BitmapRenderer* r = current()) r->readBack(*this);
        });
    return data_->rgba.data();
}

void ImageBitmap::readPixels(uint32_t* buffer, int startX, int startY, std::optional<int> width, std::optional<int> height, int bufferOffset,
                             std::optional<int> stride) const {
    if (!data_ || !buffer) return;
    const uint8_t* p = pixels();
    int w = width.value_or(this->width()), h = height.value_or(this->height()), s = stride.value_or(w);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sx = startX + x, sy = startY + y;
            if (sx < 0 || sy < 0 || sx >= this->width() || sy >= this->height()) continue;
            const uint8_t* q = p + (size_t(sy) * size_t(this->width()) + size_t(sx)) * 4;
            buffer[size_t(bufferOffset) + size_t(y) * size_t(s) + size_t(x)] =
                uint32_t(q[3]) << 24 | uint32_t(q[0]) << 16 | uint32_t(q[1]) << 8 | uint32_t(q[2]);
        }
}

PixelMap ImageBitmap::toPixelMap(int startX, int startY, std::optional<int> width, std::optional<int> height) const {
    PixelMap map;
    map.width = width.value_or(this->width()), map.height = height.value_or(this->height()), map.stride = map.width;
    map.buffer.assign(size_t(std::max(0, map.width)) * size_t(std::max(0, map.height)), 0);
    readPixels(map.buffer.data(), startX, startY, map.width, map.height);
    return map;
}

Color PixelMap::operator()(int x, int y) const {
    uint32_t c = buffer[size_t(bufferOffset) + size_t(y) * size_t(stride) + size_t(x)];
    return Color::argb(c);
}

namespace detail {

void setBitmapRenderer(BitmapRenderer* r) {
    std::vector<std::shared_ptr<ImageBitmap::Data>> waiting;
    {
        std::lock_guard lock(renderer().mutex);
        renderer().renderer = r;
        renderer().uiThread = std::this_thread::get_id();
        if (r)
            for (auto& w : std::exchange(renderer().waiting, {}))
                if (auto d = w.lock()) waiting.push_back(std::move(d));
    }
    for (auto& d : waiting) {  // what was drawn into them meanwhile, now
        ImageBitmap bitmap;
        bitmap.data_ = d;
        std::vector<std::shared_ptr<const DisplayList>> draws;
        {
            std::lock_guard lock(d->mutex);
            draws = std::exchange(d->pending, {});
        }
        for (const auto& list : draws) r->draw(bitmap, *list);
    }
}

void drawIntoBitmap(const ImageBitmap& bitmap, std::shared_ptr<const DisplayList> draws) {
    if (!bitmap.data_ || !draws) return;
    if (!current()) {  // kept for a renderer to come
        std::lock_guard lock(renderer().mutex);
        {
            std::lock_guard keep(bitmap.data_->mutex);
            bitmap.data_->pending.push_back(std::move(draws));
        }
        renderer().waiting.push_back(bitmap.data_);
        return;
    }
    onUiThread([&] {
        if (BitmapRenderer* r = current()) r->draw(bitmap, *draws);
    });
}

TextMeasurer& bitmapMeasurer() {
    static NoFonts none;
    BitmapRenderer* r = current();
    TextMeasurer* m = r ? r->measurer() : nullptr;
    return m ? *m : none;
}

namespace bitmaps {

bool pixelsCurrent(const ImageBitmap& bitmap) {
    if (!bitmap.data_) return true;
    std::lock_guard lock(bitmap.data_->mutex);
    return bitmap.data_->cpuVersion == bitmap.data_->version;
}

const std::vector<float>& floats(const ImageBitmap& bitmap) {
    static const std::vector<float> none;
    return bitmap.data_ ? bitmap.data_->floats : none;
}

namespace {
// Premultiplied RGBA (`at(i, channel)`, 0..1) kept as `d`'s config keeps it.
template <class At>
void keep(ImageBitmap::Data& d, At at, bool drawn) {
    size_t n = size_t(d.width) * size_t(d.height);
    d.rgba.resize(n * 4);
    bool f16 = d.config == ImageBitmapConfig::F16;
    if (f16) d.floats.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
        float r = at(i, 0), g = at(i, 1), b = at(i, 2), a = at(i, 3);
        uint8_t* q = &d.rgba[i * 4];
        switch (d.config) {
        case ImageBitmapConfig::Alpha8: q[0] = q[1] = q[2] = 0, q[3] = byte(a); break;
        case ImageBitmapConfig::Rgb565: {  // (premultiplied, the alpha left out: over black; rounded to 5, 6 and 5 bits, as Skia stores them)
            int r5 = int(std::lround(std::clamp(r, 0.f, 1.f) * 31)), g6 = int(std::lround(std::clamp(g, 0.f, 1.f) * 63));
            int b5 = int(std::lround(std::clamp(b, 0.f, 1.f) * 31));
            q[0] = uint8_t(std::lround(r5 * 255 / 31.0)), q[1] = uint8_t(std::lround(g6 * 255 / 63.0)), q[2] = uint8_t(std::lround(b5 * 255 / 31.0));
            q[3] = 255;
            break;
        }
        default: {
            if (!d.hasAlpha) a = 1;  // (opaque: what was drawn over black)
            float k = a > 0 ? 1 / a : 0;
            if (f16) {
                float* s = &d.floats[i * 4];
                s[0] = half(r * k), s[1] = half(g * k), s[2] = half(b * k), s[3] = half(a);
                q[0] = byte(s[0]), q[1] = byte(s[1]), q[2] = byte(s[2]), q[3] = byte(s[3]);
            } else {
                uint8_t a8 = byte(a);  // (as the renderer's 8 bits divide back out)
                float k8 = a8 > 0 ? 255.f / a8 : 0;
                q[0] = byte(byte(r) / 255.f * k8), q[1] = byte(byte(g) / 255.f * k8), q[2] = byte(byte(b) / 255.f * k8), q[3] = a8;
            }
        }
        }
    }
    if (drawn) ++d.version;
    d.cpuVersion = d.version;
}
} // namespace

void store(const ImageBitmap& bitmap, const std::vector<uint8_t>& premultiplied, bool drawn) {
    if (!bitmap.data_ || premultiplied.size() < size_t(bitmap.width()) * size_t(bitmap.height()) * 4) return;
    std::lock_guard lock(bitmap.data_->mutex);
    keep(*bitmap.data_, [&](size_t i, int c) { return premultiplied[i * 4 + size_t(c)] / 255.f; }, drawn);
}

void storeFloats(const ImageBitmap& bitmap, const std::vector<float>& premultiplied, bool drawn) {
    if (!bitmap.data_ || premultiplied.size() < size_t(bitmap.width()) * size_t(bitmap.height()) * 4) return;
    std::lock_guard lock(bitmap.data_->mutex);
    keep(*bitmap.data_, [&](size_t i, int c) { return premultiplied[i * 4 + size_t(c)]; }, drawn);
}

void drawnOnRenderer(const ImageBitmap& bitmap) {
    if (!bitmap.data_) return;
    std::lock_guard lock(bitmap.data_->mutex);
    ++bitmap.data_->version;
}

std::vector<std::shared_ptr<const DisplayList>> pending(const ImageBitmap& bitmap) {
    if (!bitmap.data_) return {};
    std::lock_guard lock(bitmap.data_->mutex);
    return bitmap.data_->pending;
}

} // namespace bitmaps

} // namespace detail

} // namespace kompoz
