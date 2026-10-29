#include "image_loader.hpp"

#include "system.hpp"

#include <kompoz/disk_cache.hpp>

#include <SDL3/SDL.h>

#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#ifdef SDL_PLATFORM_ANDROID
#include "../android/android_ime.hpp"
#endif
#ifdef SDL_PLATFORM_EMSCRIPTEN
#include <emscripten.h>
#endif
#ifdef SDL_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <wininet.h>
#endif
#ifdef KOMPOZ_TURBOJPEG
#include <turbojpeg.h>
#endif
#ifdef KOMPOZ_WEBP
#include <webp/decode.h>
#endif
#ifdef KOMPOZ_CURL
#include <curl/curl.h>
#endif

namespace kompoz::platform {

namespace {

std::atomic<bool> gStopping{false};

bool isUrl(const std::string& model) { return model.starts_with("http://") || model.starts_with("https://"); }

ImageBitmap fromRgba(int width, int height, const uint8_t* rgba, size_t pitch) {
    if (width <= 0 || height <= 0) return {};
    std::vector<uint8_t> pixels(size_t(width) * size_t(height) * 4);
    for (int y = 0; y < height; ++y) std::memcpy(pixels.data() + size_t(y) * size_t(width) * 4, rgba + size_t(y) * pitch, size_t(width) * 4);
    return ImageBitmap::fromPixels(width, height, std::move(pixels));
}

// PNG and BMP, by SDL (also what the other decoders don't take); scaled down afterwards.
ImageBitmap decodeWithSdl(const std::vector<uint8_t>& bytes, const ImageRequestSize& size) {
    SDL_Surface* surface = SDL_LoadSurface_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true);
    if (!surface) return {};
    SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (!rgba) return {};
    ImageBitmap image = fromRgba(rgba->w, rgba->h, static_cast<const uint8_t*>(rgba->pixels), size_t(rgba->pitch));
    SDL_DestroySurface(rgba);
    return scaleDown(image, imageSampleSize(image.width(), image.height(), size));
}

#ifdef SDL_PLATFORM_WINDOWS
// Windows Imaging Component: JPEG, PNG, GIF, BMP, TIFF, and WebP/HEIF where their codecs are.
ImageBitmap decodeWithWic(const std::vector<uint8_t>& bytes, const ImageRequestSize& size) {
    thread_local bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    (void)com;
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return {};
    ImageBitmap image;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICBitmapSource* rgba = nullptr;
    // Scaled down for the size by WIC's scaler (JPEG decoders scale as they decode).
    auto scaled = [&]() -> IWICBitmapSource* {
        UINT w = 0, h = 0;
        if (FAILED(frame->GetSize(&w, &h))) return frame;
        int sample = imageSampleSize(int(w), int(h), size);
        if (sample <= 1 || FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(frame, (w + sample - 1) / sample, (h + sample - 1) / sample, WICBitmapInterpolationModeFant)))
            return frame;
        return scaler;
    };
    if (SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), DWORD(bytes.size()))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, scaled(), &rgba))) {
        UINT w = 0, h = 0;
        rgba->GetSize(&w, &h);
        std::vector<uint8_t> pixels(size_t(w) * size_t(h) * 4);
        if (w > 0 && h > 0 && SUCCEEDED(rgba->CopyPixels(nullptr, w * 4, UINT(pixels.size()), pixels.data())))
            image = ImageBitmap::fromPixels(int(w), int(h), std::move(pixels));
    }
    for (IUnknown* p : {static_cast<IUnknown*>(rgba), static_cast<IUnknown*>(scaler), static_cast<IUnknown*>(frame),
                        static_cast<IUnknown*>(decoder), static_cast<IUnknown*>(stream), static_cast<IUnknown*>(factory)})
        if (p) p->Release();
    return image;
}
#endif

#ifdef KOMPOZ_TURBOJPEG
// libjpeg-turbo scales by 1/2, 1/4 or 1/8 as it decodes (and much faster for it). Its TurboJPEG 3
// API where it has it (3.0 and later), else the one before.
#ifdef KOMPOZ_TURBOJPEG3
ImageBitmap decodeJpeg(const std::vector<uint8_t>& bytes, const ImageRequestSize& size) {
    tjhandle tj = tj3Init(TJINIT_DECOMPRESS);
    if (!tj) return {};
    ImageBitmap image;
    int rest = 1;
    if (tj3DecompressHeader(tj, bytes.data(), bytes.size()) == 0) {
        int w = tj3Get(tj, TJPARAM_JPEGWIDTH), h = tj3Get(tj, TJPARAM_JPEGHEIGHT);
        int sample = w > 0 && h > 0 ? imageSampleSize(w, h, size) : 1;
        tjscalingfactor factor{1, std::min(sample, 8)};
        rest = sample / factor.denom;
        if (factor.denom > 1 && tj3SetScalingFactor(tj, factor) != 0) {
            factor = TJUNSCALED;
            rest = sample;
        }
        int sw = TJSCALED(w, factor), sh = TJSCALED(h, factor);
        std::vector<uint8_t> pixels(size_t(std::max(sw, 0)) * size_t(std::max(sh, 0)) * 4);
        if (sw > 0 && sh > 0 && tj3Decompress8(tj, bytes.data(), bytes.size(), pixels.data(), sw * 4, TJPF_RGBA) == 0)
            image = ImageBitmap::fromPixels(sw, sh, std::move(pixels));
    }
    tj3Destroy(tj);
    return scaleDown(image, rest);
}
#else
ImageBitmap decodeJpeg(const std::vector<uint8_t>& bytes, const ImageRequestSize& size) {
    tjhandle tj = tjInitDecompress();
    if (!tj) return {};
    ImageBitmap image;
    int rest = 1, w = 0, h = 0, subsampling = 0, colorspace = 0;
    auto* data = const_cast<unsigned char*>(bytes.data());  // (2.x takes them as not const)
    auto length = static_cast<unsigned long>(bytes.size());
    if (tjDecompressHeader3(tj, data, length, &w, &h, &subsampling, &colorspace) == 0) {
        int sample = w > 0 && h > 0 ? imageSampleSize(w, h, size) : 1;
        // The smallest of its 1/n factors that the sample allows (it has 1/2, 1/4 and 1/8).
        tjscalingfactor factor{1, 1};
        int count = 0;
        if (tjscalingfactor* factors = tjGetScalingFactors(&count))
            for (int i = 0; i < count; ++i)
                if (factors[i].num == 1 && factors[i].denom <= std::min(sample, 8) && factors[i].denom > factor.denom)
                    factor = factors[i];
        rest = sample / factor.denom;
        int sw = TJSCALED(w, factor), sh = TJSCALED(h, factor);
        std::vector<uint8_t> pixels(size_t(std::max(sw, 0)) * size_t(std::max(sh, 0)) * 4);
        if (sw > 0 && sh > 0 && tjDecompress2(tj, data, length, pixels.data(), sw, sw * 4, sh, TJPF_RGBA, 0) == 0)
            image = ImageBitmap::fromPixels(sw, sh, std::move(pixels));
    }
    tjDestroy(tj);
    return scaleDown(image, rest);
}
#endif
#endif

#ifdef KOMPOZ_WEBP
ImageBitmap decodeWebp(const std::vector<uint8_t>& bytes, const ImageRequestSize& size) {
    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config) || WebPGetFeatures(bytes.data(), bytes.size(), &config.input) != VP8_STATUS_OK) return {};
    int w = config.input.width, h = config.input.height;
    if (int sample = imageSampleSize(w, h, size); sample > 1) {  // libwebp scales as it decodes
        config.options.use_scaling = 1;
        config.options.scaled_width = (w + sample - 1) / sample;
        config.options.scaled_height = (h + sample - 1) / sample;
    }
    config.output.colorspace = MODE_RGBA;
    if (WebPDecode(bytes.data(), bytes.size(), &config) != VP8_STATUS_OK) return {};
    const WebPRGBABuffer& out = config.output.u.RGBA;
    ImageBitmap image = fromRgba(config.output.width, config.output.height, out.rgba, size_t(out.stride));
    WebPFreeDecBuffer(&config.output);
    return image;
}
#endif

#ifdef KOMPOZ_CURL
size_t curlWrite(char* data, size_t size, size_t count, void* out) {
    auto* body = static_cast<std::vector<uint8_t>*>(out);
    body->insert(body->end(), data, data + size * count);
    return size * count;
}
int curlProgress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return gStopping ? 1 : 0; }  // the app is quitting
#endif

#ifdef SDL_PLATFORM_WINDOWS
std::wstring wide(const std::string& s) {
    std::wstring out(size_t(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), out.data(), int(out.size()));
    return out;
}
#endif

// The body of an http(s) GET; empty if it failed.
std::vector<uint8_t> httpGet(const std::string& url) {
    std::vector<uint8_t> body;
#if defined(SDL_PLATFORM_ANDROID)
    body = android_ime::httpGet(url);
#elif defined(SDL_PLATFORM_WINDOWS)
    HINTERNET net = InternetOpenW(L"Kompoz", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!net) return {};
    DWORD timeout = 20000;
    InternetSetOptionW(net, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof timeout);
    InternetSetOptionW(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof timeout);
    if (HINTERNET request = InternetOpenUrlW(net, wide(url).c_str(), nullptr, 0, INTERNET_FLAG_NO_UI, 0)) {
        DWORD status = 0, size = sizeof status;
        HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, nullptr);
        if (status / 100 == 2) {
            char buffer[16384];
            DWORD read = 0;
            while (!gStopping && InternetReadFile(request, buffer, sizeof buffer, &read) && read > 0) body.insert(body.end(), buffer, buffer + read);
        }
        InternetCloseHandle(request);
    }
    InternetCloseHandle(net);
#elif defined(KOMPOZ_CURL)
    static std::once_flag init;
    std::call_once(init, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL* curl = curl_easy_init();
    if (!curl) return {};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgress);
    long status = 0;
    if (curl_easy_perform(curl) != CURLE_OK || (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status), status / 100 != 2)) body.clear();
    curl_easy_cleanup(curl);
#else
    (void)url;
    platform::log("kompoz: no HTTP client in this build (libcurl was not found): " + url);
#endif
    return body;
}

// Downloaded images, kept (as they came) for the next time; null if there is no disk cache.
std::mutex gDiskMutex;
std::shared_ptr<DiskCache> gDisk;

std::shared_ptr<DiskCache> diskCache() {
    std::lock_guard lock(gDiskMutex);
    return gDisk;
}

bool logging() {
    static const bool on = std::getenv("KOMPOZ_IMAGE_LOG") != nullptr;  // where each one came from, how big
    return on;
}

ImageBitmap loadNow(const std::string& model, const ImageRequestSize& size) {
    std::vector<uint8_t> bytes;
    const char* from = "file";
    std::shared_ptr<DiskCache> disk = isUrl(model) ? diskCache() : nullptr;
    if (disk)
        if (auto kept = disk->get(model)) {
            bytes = std::move(*kept);
            from = "disk";
        }
    if (bytes.empty() && isUrl(model)) {
        bytes = httpGet(model);
        from = "network";
    } else if (!isUrl(model)) {
        bytes = platform::readFile(model);
    }
    if (bytes.empty()) {
        platform::log("kompoz: can't load " + model);
        return {};
    }
    ImageBitmap image = decodeImage(bytes, size);
    if (image.empty()) {
        platform::log("kompoz: can't decode " + model);
        if (disk && from[0] == 'd') disk->remove(model);
    } else if (disk && from[0] == 'n') {
        disk->put(model, bytes);  // only what could be decoded
    }
    if (logging())
        platform::log(std::format("kompoz: image {} from {}, {} bytes, decoded {}x{} for {}x{}{}", model, from, bytes.size(), image.width(),
                                  image.height(), size.width, size.height, size.fill ? " (fill)" : ""));
    return image;
}

#ifdef SDL_PLATFORM_EMSCRIPTEN
// The browser fetches and decodes (and caches what it fetched); the pixels come back through
// kompoz_web_image_loaded, drawn smaller as imageSampleSize says.
EM_JS(void, kompoz_web_load_image, (int id, const char* url, const uint8_t* bytes, int size, int wantW, int wantH, int fill), {
    const done = (w, h, data) => {
        let ptr = 0;
        if (data) {
            ptr = _kompoz_web_alloc(data.length);
            HEAPU8.set(data, ptr);
        }
        _kompoz_web_image_loaded(id, ptr, w, h);
    };
    const decode = (blob) => createImageBitmap(blob).then((bitmap) => {
        const sample = _kompoz_web_sample_size(bitmap.width, bitmap.height, wantW, wantH, fill);
        const w = Math.ceil(bitmap.width / sample), h = Math.ceil(bitmap.height / sample);
        const canvas = new OffscreenCanvas(w, h);
        const ctx = canvas.getContext('2d');
        ctx.imageSmoothingQuality = 'high';
        ctx.drawImage(bitmap, 0, 0, w, h);
        bitmap.close();
        done(w, h, ctx.getImageData(0, 0, w, h).data);
    });
    const loading = bytes ? decode(new Blob([HEAPU8.slice(bytes, bytes + size)]))
                          : fetch(UTF8ToString(url)).then((r) => { if (!r.ok) throw new Error(r.status); return r.blob(); }).then(decode);
    loading.catch(() => done(0, 0, null));
});
EM_JS_DEPS(kompoz_web_image, "$UTF8ToString");

std::unordered_map<int, std::function<void(ImageBitmap)>>& webLoads() {
    static std::unordered_map<int, std::function<void(ImageBitmap)>> loads;
    return loads;
}
#else
// A few worker threads take the loads in turn.
class Loader {
public:
    void add(std::string model, ImageRequestSize size, std::function<void(ImageBitmap)> done) {
        std::lock_guard lock(mutex_);
        if (gStopping) return;
        if (threads_.empty())
            for (int i = 0; i < 3; ++i)
                threads_.emplace_back([this] {
                    work();
                    SDL_CleanupTLS();  // what SDL keeps for a thread it didn't start
                });
        queue_.push_back({std::move(model), size, std::move(done)});
        wake_.notify_one();
    }
    void stop() {
        {
            std::lock_guard lock(mutex_);
            gStopping = true;
            queue_.clear();
        }
        wake_.notify_all();
        for (auto& t : threads_) t.join();
        threads_.clear();
    }

private:
    struct Job {
        std::string model;
        ImageRequestSize size;
        std::function<void(ImageBitmap)> done;
    };
    void work() {
        while (true) {
            Job job;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return gStopping || !queue_.empty(); });
                if (gStopping) return;
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            ImageBitmap image = loadNow(job.model, job.size);
            if (!gStopping) job.done(std::move(image));
        }
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> queue_;
    std::vector<std::thread> threads_;
};

Loader& loader() {
    static Loader l;
    return l;
}
#endif

} // namespace


ImageBitmap decodeImage(const std::vector<uint8_t>& bytes, const ImageRequestSize& size) {
    if (bytes.size() < 12) return {};
#if defined(SDL_PLATFORM_ANDROID)
    return android_ime::decodeImage(bytes, size);
#elif defined(SDL_PLATFORM_WINDOWS)
    if (ImageBitmap image = decodeWithWic(bytes, size); !image.empty()) return image;
    return decodeWithSdl(bytes, size);
#else
    const uint8_t* b = bytes.data();
#ifdef KOMPOZ_TURBOJPEG
    if (b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return decodeJpeg(bytes, size);
#endif
#ifdef KOMPOZ_WEBP
    if (std::memcmp(b, "RIFF", 4) == 0 && std::memcmp(b + 8, "WEBP", 4) == 0) return decodeWebp(bytes, size);
#endif
    (void)b;
    return decodeWithSdl(bytes, size);
#endif
}

void loadImageAsync(const std::string& model, const ImageRequestSize& size, std::function<void(ImageBitmap)> done) {
#ifdef SDL_PLATFORM_EMSCRIPTEN
    static int next = 0;
    int id = ++next;
    webLoads()[id] = std::move(done);
    if (isUrl(model)) {
        kompoz_web_load_image(id, model.c_str(), nullptr, 0, size.width, size.height, size.fill);
    } else {
        std::vector<uint8_t> bytes = platform::readFile(model);  // from the preloaded files
        if (bytes.empty()) {
            auto it = webLoads().find(id);
            auto finish = std::move(it->second);
            webLoads().erase(it);
            return finish({});
        }
        kompoz_web_load_image(id, nullptr, bytes.data(), int(bytes.size()), size.width, size.height, size.fill);
    }
#else
    loader().add(model, size, std::move(done));
#endif
}


void startImageDiskCache(const std::string& appName, uint64_t maxBytes) {
#ifdef SDL_PLATFORM_EMSCRIPTEN
    (void)appName;
    (void)maxBytes;
#else
    std::filesystem::path dir;
    if (const char* own = std::getenv("KOMPOZ_IMAGE_CACHE"))
        dir = std::filesystem::path(std::u8string(own, own + std::strlen(own)));
    else if (std::filesystem::path base = platform::appCacheDirectory(appName); !base.empty())
        dir = base / "images";
    std::lock_guard lock(gDiskMutex);
    gDisk = dir.empty() || maxBytes == 0 ? nullptr : std::make_shared<DiskCache>(dir, maxBytes);
#endif
}

void stopImageLoader() {
#ifndef SDL_PLATFORM_EMSCRIPTEN
    loader().stop();
#endif
}

// An image asset (APK on Android) or a file next to the binary: JPEG, PNG, WebP, BMP (on the
// web, where the browser decodes asynchronously, PNG and BMP; AsyncImage takes the others).
ImageBitmap loadImageFile(std::string_view path) {
    std::string p(path);
#ifndef SDL_PLATFORM_EMSCRIPTEN
    if (std::vector<uint8_t> bytes = platform::readFile(p); !bytes.empty())
        if (ImageBitmap image = decodeImage(bytes); !image.empty()) return image;
#endif
    SDL_Surface* surface = SDL_LoadSurface(p.c_str());
    if (!surface && SDL_GetBasePath()) surface = SDL_LoadSurface((SDL_GetBasePath() + p).c_str());
    if (!surface) {
        SDL_Log("kompoz: can't load image %s: %s", p.c_str(), SDL_GetError());
        return {};
    }
    SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (!rgba) return {};
    std::vector<uint8_t> pixels(size_t(rgba->w) * size_t(rgba->h) * 4);
    for (int y = 0; y < rgba->h; ++y)
        std::memcpy(pixels.data() + size_t(y) * size_t(rgba->w) * 4,
                    static_cast<const uint8_t*>(rgba->pixels) + size_t(y) * size_t(rgba->pitch), size_t(rgba->w) * 4);
    ImageBitmap image = ImageBitmap::fromPixels(rgba->w, rgba->h, std::move(pixels));
    SDL_DestroySurface(rgba);
    return image;
}

// A screenshot from pixels read back (RGBA, top row first).
void savePixels(const std::string& path, int w, int h, std::vector<uint8_t>& pixels) {
    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels.data(), w * 4);
    if (!surface) return;
    bool ok;
#if SDL_VERSION_ATLEAST(3, 4, 0)
    ok = path.ends_with(".png") ? SDL_SavePNG(surface, path.c_str()) : SDL_SaveBMP(surface, path.c_str());
#else
    ok = SDL_SaveBMP(surface, path.c_str());
#endif
    SDL_Log("kompoz: screenshot %s %s", path.c_str(), ok ? "saved" : SDL_GetError());
    SDL_DestroySurface(surface);
}

} // namespace kompoz::platform

#ifdef SDL_PLATFORM_EMSCRIPTEN
extern "C" {
EMSCRIPTEN_KEEPALIVE void* kompoz_web_alloc(int size) { return std::malloc(size_t(size)); }
EMSCRIPTEN_KEEPALIVE int kompoz_web_sample_size(int width, int height, int wantWidth, int wantHeight, int fill) {
    return kompoz::imageSampleSize(width, height, {wantWidth, wantHeight, fill != 0});
}
EMSCRIPTEN_KEEPALIVE void kompoz_web_image_loaded(int id, uint8_t* pixels, int width, int height) {
    auto& loads = kompoz::platform::webLoads();
    auto it = loads.find(id);
    kompoz::ImageBitmap image;
    if (pixels && width > 0 && height > 0) image = kompoz::platform::fromRgba(width, height, pixels, size_t(width) * 4);
    std::free(pixels);
    if (it == loads.end()) return;
    auto done = std::move(it->second);
    loads.erase(it);
    done(std::move(image));
}
}
#endif
