#include "headless.hpp"

#include "../app/app_core.hpp"
#include "../common/system.hpp"
#include "headless_gpu.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace kompoz::platform {

namespace {

class HeadlessPlatform final : public Platform {
public:
    std::optional<std::string> option(std::string_view name) const override {
        if (const char* v = std::getenv(std::string(name).c_str())) return std::string(v);
        return std::nullopt;
    }
    void log(const std::string& message) const override { platform::log(message); }
    int64_t now() const override {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::vector<gpu::Backend> backends() const override {
#ifdef __APPLE__
        return {gpu::Backend::Metal};
#else
        return {gpu::Backend::Vulkan, gpu::Backend::GLES};
#endif
    }

    // The window is its size; what the device needs, HeadlessGpu's. It is shown, focused and sized
    // at once, as a window on a screen would be.
    bool openWindow(const Application& app, gpu::Backend backend, gpu::Surface& surface, std::string* error) override {
        if (!gpu_.open(backend, option("KOMPOZ_GPU_VALIDATION") == "1", surface, error)) return false;
        width_ = app.width, height_ = app.height;
        surface.drawableSize = [this](int* w, int* h) { *w = width_, *h = height_; };
        for (EventType type : {EventType::FocusGained, EventType::Shown, EventType::Resized}) post({.type = type});
        return true;
    }
    void closeWindow() override {
        gpu_.close();
        std::lock_guard lock(mutex_);
        events_.clear();
    }
    void showError(const std::string& message) override { platform::log(message); }

    // Its pixels are its points; KOMPOZ_SCALE is a phone's density (2.625, say) and
    // KOMPOZ_FONT_SCALE the system's text size.
    Metrics metrics() const override {
        Metrics m;
        m.pixelWidth = m.pointWidth = width_;
        m.pixelHeight = m.pointHeight = height_;
        if (auto scale = option("KOMPOZ_SCALE"); scale && std::atof(scale->c_str()) > 0) m.scale = float(std::atof(scale->c_str()));
        if (auto fontScale = option("KOMPOZ_FONT_SCALE")) m.fontScale = std::max(0.5f, float(std::atof(fontScale->c_str())));
        m.safeArea = Rect{0, 0, float(width_), float(height_)};
        m.language = preferredLanguage();
        return m;
    }
    void pixelSize(int* width, int* height) const override { *width = width_, *height = height_; }
    void setWindowSize(int width, int height) override {
        width_ = width, height_ = height;
        post({.type = EventType::Resized});
    }
    MediaInfo media(bool) override { return {}; }  // (no pointer, keyboard, microphone or camera)

    void attach(ComposeHost&, ComposeHost::Callbacks& callbacks) override {
        callbacks.wake = [this] { post({.type = EventType::Wake}); };
        callbacks.getClipboard = [this] { return clipboard_; };  // (the app's own)
        callbacks.setClipboard = [this](const std::string& text) { clipboard_ = text; };
    }
    void composed(ComposeHost&, bool, int64_t) override {}

    // From any thread: to the core before its next frame, waking the loop.
    void post(const Event& event) {
        {
            std::lock_guard lock(mutex_);
            events_.push_back(event);
        }
        posted_.notify_one();
    }
    std::deque<Event> take() {
        std::lock_guard lock(mutex_);
        return std::exchange(events_, {});
    }
    // Until something is posted, or until `wakeAt` (the platform's clock; none: < 0).
    void wait(int64_t wakeAt) {
        std::unique_lock lock(mutex_);
        auto ready = [this] { return !events_.empty(); };
        if (wakeAt < 0) posted_.wait(lock, ready);
        else posted_.wait_for(lock, std::chrono::nanoseconds(std::max<int64_t>(wakeAt - now(), 0)), ready);
    }

private:
    HeadlessGpu gpu_;
    int width_ = 1, height_ = 1;
    std::string clipboard_;
    std::mutex mutex_;
    std::condition_variable posted_;
    std::deque<Event> events_;
};

} // namespace

bool headlessRequested() {
    const char* v = std::getenv("KOMPOZ_HEADLESS");
    return v && std::string_view(v) == "1";
}

int runHeadless(int argc, char* argv[]) {
    HeadlessPlatform platform;
    AppCore core{platform};
    if (!core.start(argc, argv)) return 1;
    for (;;) {
        for (const Event& e : platform.take())
            if (e.type != EventType::Wake) core.handle(e);  // (a wake only ends the wait)
        // The pause until the frame is due is the host's for work between frames (a lazy list's next item).
        if (int64_t due = core.frameDue(), at = platform.now(); due >= 0 && at < due) {
            core.idle(due - at);
            if (at = platform.now(); at < due) std::this_thread::sleep_for(std::chrono::nanoseconds(due - at));
        }
        Next next = core.frame();
        if (next.quit) break;
        if (!next.busy) platform.wait(next.wakeAt);  // until an event, a wake, or the next coroutine delay
    }
    core.quit();
    return 0;
}

} // namespace kompoz::platform
