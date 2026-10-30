#include "web_platform.hpp"

#include "../app/app_core.hpp"
#include "../common/system.hpp"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace kompoz::platform {
namespace {

// The page's side: a hidden textarea that takes typing (an input method's and a phone keyboard's
// too) and the clipboard's copy and paste events, drops on the canvas (files written into the
// virtual file system), and what the page tells as it changes (the color scheme, the pixel ratio,
// the address's #fragment). What it learns it hands to the functions below.
EM_JS(void, web_install, (), {
    const canvas = document.getElementById('canvas');
    const area = document.createElement('textarea');
    area.setAttribute('autocomplete', 'off');
    area.setAttribute('autocapitalize', 'off');
    area.setAttribute('spellcheck', 'false');
    area.setAttribute('inputmode', 'none');
    area.setAttribute('aria-hidden', 'true');
    // Over the field it types into (so that an input method's window is beside it), unseen; 16px,
    // so that iOS doesn't zoom in on it.
    area.style.cssText = 'position:fixed;left:0;top:0;width:1px;height:1px;opacity:0;border:0;padding:0;margin:0;resize:none;'
                       + 'font-size:16px;outline:none;caret-color:transparent;background:transparent;color:transparent;z-index:-1';
    document.body.appendChild(area);
    Module.kompozArea = area;
    Module.kompozComposing = false;
    const send = (fn, text) => { const p = stringToNewUTF8(text); fn(p); _free(p); };
    area.addEventListener('compositionstart', () => { Module.kompozComposing = true; });
    area.addEventListener('compositionend', (e) => {
        Module.kompozComposing = false;
        if (e.data) send(_kompoz_web_text, e.data);
        area.value = '';
    });
    area.addEventListener('input', (e) => {
        if (Module.kompozComposing || e.isComposing) return;
        if (e.inputType === 'deleteContentBackward') _kompoz_web_backspace();
        else if (area.value) send(_kompoz_web_text, area.value);
        area.value = '';
    });
    area.addEventListener('paste', (e) => {
        e.preventDefault();
        send(_kompoz_web_paste, e.clipboardData ? e.clipboardData.getData('text/plain') : '');
    });
    const copy = (e) => {
        e.preventDefault();
        if (e.clipboardData) e.clipboardData.setData('text/plain', UTF8ToString(_kompoz_web_clipboard()));
    };
    area.addEventListener('copy', copy);
    area.addEventListener('cut', copy);
    // Drops: files first (each read into /tmp/drop/<n>/<name>), else text.
    let drops = 0;
    const where = (e) => { const r = canvas.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
    const drop = (kind, e, text) => { const [x, y] = where(e); const p = stringToNewUTF8(text || ''); _kompoz_web_drop(kind, x, y, p); _free(p); };
    canvas.addEventListener('dragenter', (e) => { e.preventDefault(); drop(0, e); });
    canvas.addEventListener('dragover', (e) => { e.preventDefault(); e.dataTransfer.dropEffect = 'copy'; drop(1, e); });
    canvas.addEventListener('dragleave', (e) => { drop(4, e); });
    canvas.addEventListener('drop', (e) => {
        e.preventDefault();
        const t = e.dataTransfer;
        if (t.files && t.files.length) {
            const dir = '/tmp/drop/' + (drops++);
            FS.mkdirTree(dir);
            let left = t.files.length;
            for (const file of t.files) {
                const reader = new FileReader();
                reader.onload = () => {
                    const path = dir + '/' + file.name;
                    FS.writeFile(path, new Uint8Array(reader.result));
                    drop(2, e, path);
                    if (--left === 0) drop(4, e);
                };
                reader.onerror = () => { if (--left === 0) drop(4, e); };
                reader.readAsArrayBuffer(file);
            }
        } else {
            drop(3, e, t.getData('text/plain'));
            drop(4, e);
        }
    });
    // The color scheme, the pixel ratio (another screen, the page zoomed), the #fragment.
    matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => _kompoz_web_changed(0));
    const ratio = () => matchMedia('(resolution: ' + devicePixelRatio + 'dppx)').addEventListener('change', () => { _kompoz_web_changed(1); ratio(); }, {once: true});
    ratio();
    window.addEventListener('hashchange', () => _kompoz_web_changed(2));
    // A wake lock is let go as the page is hidden: asked for again as it shows.
    document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'visible' && Module.kompozKeepOn && navigator.wakeLock)
            navigator.wakeLock.request('screen').then((l) => { Module.kompozWakeLock = l; }).catch(() => {});
    });
    area.focus({preventScroll: true});
});
EM_JS_DEPS(web_install, "$stringToNewUTF8,$UTF8ToString,$FS");

// The textarea focused (so that keys, typing and the clipboard's events go to it), placed at the
// field and set for its keyboard (`mode`: inputmode; "none" while no field takes typing).
EM_JS(void, web_text_input, (int active, float x, float y, float w, float h, const char* mode, const char* enter), {
    const area = Module.kompozArea;
    if (!area) return;
    area.setAttribute('inputmode', active ? UTF8ToString(mode) : 'none');
    area.setAttribute('enterkeyhint', UTF8ToString(enter));
    area.style.left = x + 'px', area.style.top = y + 'px', area.style.width = Math.max(1, w) + 'px', area.style.height = Math.max(1, h) + 'px';
    if (document.activeElement !== area) area.focus({preventScroll: true});
});
EM_JS(void, web_focus_text, (), {
    if (Module.kompozArea && document.activeElement !== Module.kompozArea) Module.kompozArea.focus({preventScroll: true});
});
EM_JS(void, web_canvas_offset, (float* x, float* y), {
    const r = document.getElementById('canvas').getBoundingClientRect();
    HEAPF32[x >> 2] = r.left, HEAPF32[y >> 2] = r.top;
});
EM_JS(void, web_set_cursor, (const char* name), { document.getElementById('canvas').style.cursor = UTF8ToString(name); });
EM_JS(void, web_write_clipboard, (const char* text), {
    if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(UTF8ToString(text)).catch(() => {});
});
EM_JS(void, web_open, (const char* url), { window.open(UTF8ToString(url), '_blank', 'noopener'); });
EM_JS(void, web_vibrate, (int millis), {
    if (navigator.vibrate) navigator.vibrate(millis);
});
EM_JS(void, web_keep_on, (int on), {
    Module.kompozKeepOn = !!on;
    if (on && navigator.wakeLock) navigator.wakeLock.request('screen').then((l) => { Module.kompozWakeLock = l; }).catch(() => {});
    else if (!on && Module.kompozWakeLock) { Module.kompozWakeLock.release(); Module.kompozWakeLock = null; }
});
EM_JS(int, web_dark, (), { return matchMedia('(prefers-color-scheme: dark)').matches ? 1 : 0; });
EM_JS(int, web_matches, (const char* query), { return matchMedia(UTF8ToString(query)).matches ? 1 : 0; });
// The address's #fragment (a route's path), once each time it changes; none: not changed.
EM_JS(char*, web_take_hash, (), {
    const h = location.hash;
    if (h === Module.kompozHash) return 0;
    Module.kompozHash = h;
    return h.length > 1 ? stringToNewUTF8(h.substring(1)) : 0;
});
EM_JS(void, web_set_hash, (const char* path), {
    const h = '#' + UTF8ToString(path);
    Module.kompozHash = h;
    if (location.hash !== h) history.replaceState(null, '', h);
});
EM_JS_DEPS(web_hash, "$stringToNewUTF8,$UTF8ToString");

// A key's name (KeyboardEvent.key: the layout's letter, else its name) as Kompoz's key.
Key keyOf(std::string_view key) {
    if (key.size() == 1) {
        char c = key[0];
        if (c >= 'a' && c <= 'z') return Key(int(Key::A) + (c - 'a'));
        if (c >= 'A' && c <= 'Z') return Key(int(Key::A) + (c - 'A'));
        if (c >= '0' && c <= '9') return Key(int(Key::Zero) + (c - '0'));
        if (c == ' ') return Key::Space;
    }
    if (key.size() >= 2 && key.size() <= 3 && key[0] == 'F') {
        int n = std::atoi(std::string(key.substr(1)).c_str());
        if (n >= 1 && n <= 12) return Key(int(Key::F1) + n - 1);
    }
    static constexpr std::pair<std::string_view, Key> named[] = {
        {"Enter", Key::Enter},     {"Escape", Key::Escape}, {"Tab", Key::Tab},        {"Backspace", Key::Backspace}, {"Delete", Key::Delete},
        {"ArrowLeft", Key::Left},  {"ArrowRight", Key::Right}, {"ArrowUp", Key::Up}, {"ArrowDown", Key::Down},      {"Home", Key::Home},
        {"End", Key::End},         {"PageUp", Key::PageUp}, {"PageDown", Key::PageDown}};
    for (auto [name, k] : named)
        if (key == name) return k;
    return Key::Unknown;
}

// One character, as KeyboardEvent.key has it for a key that types one.
bool isCharacter(std::string_view key) {
    if (key.empty()) return false;
    auto c = uint8_t(key[0]);
    size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    return key.size() == length;
}

class WebPlatform;
WebPlatform* gPlatform = nullptr;

class WebPlatform final : public Platform {
public:
    bool start(int argc, char* argv[]) {
        gPlatform = this;
        core_ = std::make_unique<AppCore>(*this);
        if (!core_->start(argc, argv)) return false;
        web_install();
        listen();
        emscripten_set_main_loop_arg([](void* p) { static_cast<WebPlatform*>(p)->iterate(); }, this, 0, false);
        return true;
    }

    // ---- Platform ------------------------------------------------------------------------

    std::optional<std::string> option(std::string_view name) const override {
        if (const char* v = std::getenv(std::string(name).c_str())) return std::string(v);
        return std::nullopt;
    }
    void log(const std::string& message) const override { platform::log(message); }
    int64_t now() const override { return int64_t(emscripten_get_now() * 1e6); }
    std::vector<gpu::Backend> backends() const override { return {gpu::Backend::WebGPU, gpu::Backend::GLES}; }

    bool openWindow(const Application& app, gpu::Backend backend, gpu::Surface& surface, std::string* error) override {
        title_ = app.title;
        emscripten_set_window_title(title_.c_str());
        measure();
        surface.drawableSize = [this](int* w, int* h) { *w = pixelWidth_, *h = pixelHeight_; };
        if (backend == gpu::Backend::WebGPU) {
            surface.webgpuCanvas = "#canvas";
            return true;
        }
        if (backend != gpu::Backend::GLES) {
            *error = std::string("no ") + gpu::backendName(backend) + " here";
            return false;
        }
        // WebGL 2 on the page's canvas, current on this thread (the browser shows each frame).
        EmscriptenWebGLContextAttributes attributes;
        emscripten_webgl_init_context_attributes(&attributes);
        attributes.majorVersion = 2;
        attributes.depth = attributes.stencil = attributes.antialias = false;
        context_ = emscripten_webgl_create_context("#canvas", &attributes);
        if (context_ <= 0 || emscripten_webgl_make_context_current(context_) != EMSCRIPTEN_RESULT_SUCCESS) {
            context_ = 0;
            *error = "no WebGL 2 context";
            return false;
        }
        surface.glGetProcAddress = [](const char* name) { return emscripten_webgl_get_proc_address(name); };
        surface.glSwap = [] {};
        return true;
    }

    void closeWindow() override {
        if (context_ > 0) emscripten_webgl_destroy_context(std::exchange(context_, 0));
    }

    void showError(const std::string& message) override {
        platform::log(message);
        EM_ASM({ alert(UTF8ToString($0)); }, message.c_str());
    }

    // Points are the page's CSS pixels (a dp each); the scale is the device pixel ratio;
    // KOMPOZ_SCALE and KOMPOZ_FONT_SCALE stand in for them.
    Metrics metrics() const override {
        Metrics m;
        m.pixelWidth = pixelWidth_, m.pixelHeight = pixelHeight_;
        m.pointWidth = int(std::lround(cssWidth_)), m.pointHeight = int(std::lround(cssHeight_));
        m.scale = float(scale());
        m.pointToDp = 1;
        if (auto v = option("KOMPOZ_FONT_SCALE")) m.fontScale = std::max(0.5f, float(std::atof(v->c_str())));
        m.refreshRate = 60;  // (the browser's frames come on the display's beat, whatever it is)
        m.safeArea = Rect{0, 0, float(cssWidth_), float(cssHeight_)};
        m.darkTheme = web_dark() != 0;
        m.language = preferredLanguage();
        return m;
    }
    void pixelSize(int* width, int* height) const override { *width = pixelWidth_, *height = pixelHeight_; }
    void setWindowSize(int, int) override {}  // (the page decides the canvas's size)

    // A fine pointer where the page has one (and with it a keyboard), else a coarse one; the
    // microphone and the camera not looked for (the browser would ask).
    MediaInfo media(bool) override {
        MediaInfo info;
        bool fine = web_matches("(any-pointer: fine)"), coarse = web_matches("(any-pointer: coarse)");
        info.pointerPrecision = fine ? PointerPrecision::Fine : coarse ? PointerPrecision::Coarse : PointerPrecision::None;
        info.physicalKeyboard = fine;
        return info;
    }

    void attach(ComposeHost&, ComposeHost::Callbacks& callbacks) override {
        WebPlatform* self = this;
        callbacks.wake = [self] { self->wake(); };
        callbacks.textInput = [self](bool active, Rect area, const TextEditState&, const ImeOptions& options) {
            self->textActive_ = active;
            const char* mode = "text";
            switch (options.keyboardType) {
            case KeyboardType::Number:
            case KeyboardType::NumberPassword: mode = "numeric"; break;
            case KeyboardType::Decimal: mode = "decimal"; break;
            case KeyboardType::Phone: mode = "tel"; break;
            case KeyboardType::Uri: mode = "url"; break;
            case KeyboardType::Email: mode = "email"; break;
            default: break;
            }
            web_text_input(active, area.x, area.y, area.width, area.height, mode, options.singleLine ? "done" : "enter");
        };
        callbacks.cursor = [](Cursor cursor) {
            static constexpr const char* names[] = {"default", "pointer", "text", "crosshair", "move", "ew-resize", "ns-resize", "not-allowed", "wait"};
            web_set_cursor(names[std::min(size_t(cursor), std::size(names) - 1)]);
        };
        callbacks.getClipboard = [self] { return self->clipboard_; };
        callbacks.setClipboard = [self](const std::string& text) {
            self->clipboard_ = text;
            web_write_clipboard(text.c_str());  // (and the copy event that follows Ctrl+C takes it)
        };
        callbacks.openUri = [](const std::string& uri) { web_open(uri.c_str()); };
        callbacks.locationChanged = [](const std::string& path) { web_set_hash(path.c_str()); };
        callbacks.hapticFeedback = [](HapticFeedbackType type) {
            using T = HapticFeedbackType;  // (a press is felt longer than a tick)
            bool strong = type == T::LongPress || type == T::Confirm || type == T::Reject || type == T::GestureThresholdActivate;
            web_vibrate(strong ? 25 : 8);
        };
    }

    void started(ComposeHost& h) override {
        if (char* hash = web_take_hash()) {  // the address's #fragment: a deep link
            h.openDeepLink(hash);
            std::free(hash);
        }
    }

    void composed(ComposeHost& h, bool, int64_t) override {
        if (bool on = h.keepScreenOn(); on != keptOn_) {  // keepScreenOn: a screen wake lock while it is laid out
            keptOn_ = on;
            web_keep_on(on);
        }
    }

    // ---- What the page tells (web_install's) ---------------------------------------------

    void typed(const char* text) {
        if (textActive_ && *text) deliver({.type = EventType::Text, .text = text});
    }
    void pressBackspace() {  // (a phone keyboard's delete, which comes as no key)
        deliver(Event{.type = EventType::KeyDown, .time = now(), .key = Key::Backspace});
        deliver(Event{.type = EventType::KeyUp, .time = now(), .key = Key::Backspace});
    }
    void pasted(const char* text) {
        clipboard_ = text;
        if (heldPaste_) deliver(*std::exchange(heldPaste_, std::nullopt));
    }
    const char* clipboard() const { return clipboard_.c_str(); }
    void dropped(int kind, float x, float y, const char* data) {
        static constexpr EventType kinds[] = {EventType::DropBegin, EventType::DropMove, EventType::DropFile, EventType::DropText, EventType::DropComplete};
        Event e{.type = kinds[std::clamp(kind, 0, 4)], .time = now(), .x = x, .y = y};
        e.text = data;
        deliver(std::move(e));
    }
    void changed(int what) {
        if (what == 1) measure();
        if (what == 2) {
            if (char* hash = web_take_hash()) {
                core_->host().openDeepLink(hash);
                std::free(hash);
            }
            return wake();
        }
        deliver({.type = what == 1 ? EventType::Resized : EventType::MetricsChanged});
        if (what == 1) deliver({.type = EventType::MetricsChanged});
    }

private:
    std::unique_ptr<AppCore> core_;
    std::string title_ = "Kompoz";
    double cssWidth_ = 1, cssHeight_ = 1;  // the canvas's CSS size
    int pixelWidth_ = 1, pixelHeight_ = 1;  // its drawing buffer's
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context_ = 0;
    std::deque<Event> events_;
    bool inFrame_ = false, paused_ = false, keptOn_ = false, textActive_ = false;
    long timeout_ = 0;                // the coroutines' delay set (emscripten_set_timeout), 0: none
    int buttonsDown_ = 0;
    std::optional<Event> heldPaste_;  // Ctrl+V's press, until the paste event has the clipboard
    std::string clipboard_;

    double scale() const {
        if (auto v = option("KOMPOZ_SCALE"); v && std::atof(v->c_str()) > 0) return std::atof(v->c_str());
        return emscripten_get_device_pixel_ratio();
    }

    // The canvas's CSS size, its drawing buffer made the device's pixels of it.
    void measure() {
        emscripten_get_element_css_size("#canvas", &cssWidth_, &cssHeight_);
        cssWidth_ = std::max(1.0, cssWidth_), cssHeight_ = std::max(1.0, cssHeight_);
        pixelWidth_ = std::max(1, int(std::lround(cssWidth_ * scale()))), pixelHeight_ = std::max(1, int(std::lround(cssHeight_ * scale())));
        emscripten_set_canvas_element_size("#canvas", pixelWidth_, pixelHeight_);
    }

    // ---- The loop: the browser's animation frames, paused while there is nothing to draw ----

    // An event to the core now (between frames: a key's copy, say, done before the browser's copy
    // event asks for the text), else after the frame that is going on (waiting for the GPU).
    void deliver(Event e) {
        if (inFrame_) {
            events_.push_back(std::move(e));
        } else {
            for (const Event& queued : std::exchange(events_, {})) core_->handle(queued);
            core_->handle(e);
        }
        wake();
    }

    void wake() {
        if (paused_) {
            paused_ = false;
            emscripten_resume_main_loop();
        }
    }

    void iterate() {
        if (timeout_) emscripten_clear_timeout(std::exchange(timeout_, 0));
        for (const Event& e : std::exchange(events_, {})) core_->handle(e);
        inFrame_ = true;
        Next next = core_->frame();
        inFrame_ = false;
        if (next.quit) {
            emscripten_cancel_main_loop();
            core_->quit();
            return;
        }
        if (next.busy || !events_.empty()) return;
        // Nothing to draw until an event, a wake, or the next coroutine delay.
        paused_ = true;
        emscripten_pause_main_loop();
        if (next.wakeAt >= 0)
            timeout_ = emscripten_set_timeout([](void* p) { static_cast<WebPlatform*>(p)->timeout_ = 0, static_cast<WebPlatform*>(p)->wake(); },
                                              double(std::max<int64_t>(0, next.wakeAt - now())) / 1e6, this);
    }

    // ---- Input (Emscripten's html5 events) -----------------------------------------------

    static KeyModifiers held(bool shift, bool ctrl, bool alt, bool meta) { return {shift, ctrl || meta, alt}; }

    void listen() {
        const char* canvas = "#canvas";
        auto mouse = [](int type, const EmscriptenMouseEvent* m, void*) -> bool { return gPlatform->mouse(type, *m); };
        emscripten_set_mousedown_callback(canvas, nullptr, true, mouse);
        emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, mouse);
        emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, mouse);
        emscripten_set_mouseleave_callback(canvas, nullptr, true, mouse);
        auto touch = [](int type, const EmscriptenTouchEvent* t, void*) -> bool { return gPlatform->touch(type, *t); };
        emscripten_set_touchstart_callback(canvas, nullptr, true, touch);
        emscripten_set_touchmove_callback(canvas, nullptr, true, touch);
        emscripten_set_touchend_callback(canvas, nullptr, true, touch);
        emscripten_set_touchcancel_callback(canvas, nullptr, true, touch);
        emscripten_set_wheel_callback(canvas, nullptr, true, [](int, const EmscriptenWheelEvent* w, void*) -> bool { return gPlatform->wheel(*w); });
        auto key = [](int type, const EmscriptenKeyboardEvent* k, void*) -> bool { return gPlatform->key(type, *k); };
        emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, key);
        emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, key);
        auto focus = [](int type, const EmscriptenFocusEvent*, void*) -> bool {
            gPlatform->deliver({.type = type == EMSCRIPTEN_EVENT_FOCUS ? EventType::FocusGained : EventType::FocusLost});
            return false;
        };
        emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, focus);
        emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, focus);
        emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, [](int, const EmscriptenUiEvent*, void*) -> bool {
            gPlatform->changed(1);
            return false;
        });
        emscripten_set_visibilitychange_callback(nullptr, true, [](int, const EmscriptenVisibilityChangeEvent* v, void*) -> bool {
            gPlatform->deliver({.type = v->hidden ? EventType::Hidden : EventType::Shown});
            return false;
        });
    }

    // The mouse: pressed on the canvas (the textarea kept focused), moved and let go anywhere
    // while a button is down, leaving it.
    bool mouse(int type, const EmscriptenMouseEvent& m) {
        float x = float(m.targetX), y = float(m.targetY);
        if (type != EMSCRIPTEN_EVENT_MOUSEDOWN && type != EMSCRIPTEN_EVENT_MOUSELEAVE) {  // (the window's: from the canvas's corner)
            float left = 0, top = 0;
            web_canvas_offset(&left, &top);
            x = float(m.clientX) - left, y = float(m.clientY) - top;
        }
        Event e{.type = EventType::MouseMove, .time = now(), .x = x, .y = y};
        e.modifiers = held(m.shiftKey, m.ctrlKey, m.altKey, m.metaKey);
        switch (type) {
        case EMSCRIPTEN_EVENT_MOUSELEAVE:
            if (buttonsDown_ == 0) deliver({.type = EventType::PointerLeft});
            return false;
        case EMSCRIPTEN_EVENT_MOUSEMOVE:
            if (buttonsDown_ == 0 && (x < 0 || y < 0 || x >= cssWidth_ || y >= cssHeight_)) return false;  // (elsewhere on the page)
            deliver(e);
            return false;
        default: break;
        }
        static constexpr MouseButton buttons[] = {MouseButton::Left, MouseButton::Middle, MouseButton::Right, MouseButton::Back, MouseButton::Forward};
        if (m.button > 4) return false;
        bool down = type == EMSCRIPTEN_EVENT_MOUSEDOWN;
        if (!down && buttonsDown_ == 0) return false;  // (pressed elsewhere)
        buttonsDown_ = std::max(0, buttonsDown_ + (down ? 1 : -1));
        e.type = down ? EventType::MouseDown : EventType::MouseUp;
        e.button = buttons[m.button];
        if (down) web_focus_text();
        deliver(e);
        return down;  // (a press doesn't take the focus to the canvas, or select the page)
    }

    // Fingers on the canvas (the page neither scrolls nor makes mouse events of them).
    bool touch(int type, const EmscriptenTouchEvent& t) {
        EventType kind = type == EMSCRIPTEN_EVENT_TOUCHSTART ? EventType::TouchDown : type == EMSCRIPTEN_EVENT_TOUCHMOVE ? EventType::TouchMove : EventType::TouchUp;
        if (kind == EventType::TouchDown) web_focus_text();
        for (int i = 0; i < t.numTouches; ++i) {
            const EmscriptenTouchPoint& p = t.touches[i];
            if (!p.isChanged) continue;
            Event e{.type = kind, .time = now(), .x = float(p.targetX), .y = float(p.targetY)};
            e.pointer = p.identifier;
            deliver(e);
        }
        return true;
    }

    // The wheel in notches (a pixel's delta a hundredth, a line's a third, a page's 80, as SDL
    // tells them), up and right positive; with Ctrl (a touchpad's pinch, as browsers send it).
    bool wheel(const EmscriptenWheelEvent& w) {
        double k = w.deltaMode == DOM_DELTA_PIXEL ? 0.01 : w.deltaMode == DOM_DELTA_LINE ? 1 / 3.0 : 80;
        Event e{.type = EventType::Wheel, .time = now(), .x = float(w.mouse.targetX), .y = float(w.mouse.targetY)};
        e.dx = float(w.deltaX * k), e.dy = float(-w.deltaY * k);
        e.modifiers = held(w.mouse.shiftKey, w.mouse.ctrlKey, w.mouse.altKey, w.mouse.metaKey);
        deliver(e);
        return true;
    }

    // Keys: what the page does with one left to it where the textarea needs it (a character into
    // the field, an input method's, the clipboard's shortcuts), else kept from it.
    bool key(int type, const EmscriptenKeyboardEvent& k) {
        std::string_view name = k.key;
        bool shortcut = k.ctrlKey || k.metaKey;
        Event e{.type = type == EMSCRIPTEN_EVENT_KEYDOWN ? EventType::KeyDown : EventType::KeyUp, .time = now(), .key = keyOf(name)};
        e.modifiers = held(k.shiftKey, k.ctrlKey, k.altKey, k.metaKey);
        if (type != EMSCRIPTEN_EVENT_KEYDOWN) {
            deliver(e);
            return true;
        }
        if (shortcut && (name == "v" || name == "V")) {  // (its press held for the paste event's text)
            heldPaste_ = e;
            emscripten_set_timeout([](void*) { gPlatform->pasted(gPlatform->clipboard()); }, 100, nullptr);
            return false;
        }
        deliver(e);
        if (shortcut) return !(name == "c" || name == "C" || name == "x" || name == "X");  // (the copy event follows)
        if (e.key != Key::Unknown && e.key != Key::Space && !isCharacter(name)) return true;  // Enter, Tab, the arrows...
        if (isCharacter(name)) return !textActive_;
        return false;  // (an input method's, a dead key, a phone keyboard's "Unidentified")
    }
};

} // namespace

extern "C" {
EMSCRIPTEN_KEEPALIVE void kompoz_web_text(const char* text) { gPlatform->typed(text); }
EMSCRIPTEN_KEEPALIVE void kompoz_web_backspace() { gPlatform->pressBackspace(); }
EMSCRIPTEN_KEEPALIVE void kompoz_web_paste(const char* text) { gPlatform->pasted(text); }
EMSCRIPTEN_KEEPALIVE const char* kompoz_web_clipboard() { return gPlatform->clipboard(); }
EMSCRIPTEN_KEEPALIVE void kompoz_web_drop(int kind, float x, float y, const char* data) { gPlatform->dropped(kind, x, y, data); }
EMSCRIPTEN_KEEPALIVE void kompoz_web_changed(int what) { gPlatform->changed(what); }
}

int runWeb(int argc, char* argv[]) {
    auto* platform = new WebPlatform();  // (the page's for as long as it is open)
    return platform->start(argc, argv) ? 0 : 1;
}

} // namespace kompoz::platform
