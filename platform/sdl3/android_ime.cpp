// JNI side of dev.kompoz.KompozInput. Java reports the complete editing state after every
// change; native code forwards it to the composition thread. Native code shows/hides the
// keyboard and pushes the app's text back when the app changed it.

#include "android_ime.hpp"

#include <algorithm>
#include <kompoz/runtime.hpp>

#include <SDL3/SDL.h>
#include <jni.h>

#include <atomic>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace kompoz::android_ime {
namespace {

jclass gClass = nullptr;
jmethodID gMediaInfo = nullptr;
jmethodID gShow = nullptr, gUpdate = nullptr, gHide = nullptr, gAttach = nullptr, gFontScale = nullptr,
          gRenderGlyph = nullptr, gDynamicPalette = nullptr, gSystemBars = nullptr, gDecodeImage = nullptr, gImageBounds = nullptr, gHaptic = nullptr, gImeControlBy = nullptr, gSetFrameRate = nullptr, gImeControlEnd = nullptr, gHttpGet = nullptr,
          gStateSaved = nullptr, gTakeRestoredState = nullptr, gSetBackEnabled = nullptr, gViewConfiguration = nullptr,
          gGestureExclusion = nullptr, gContentSensitive = nullptr, gAutofillFocus = nullptr, gAutofillValue = nullptr,
          gAutofillVisibility = nullptr, gAutofillDone = nullptr;

std::mutex gMutex;
std::function<void(const TextEditState&)> gOnEdit;
// The keyboard's showings, counted on the SDL thread: an edit made for one before the last
// (reported after the focus moved to another field) is dropped.
int gSession = 0;
std::function<void(ImeAction)> gOnAction;
std::function<void(const std::array<float, 46>&)> gOnInsets;
std::function<void(float)> gOnFontScale;
std::function<void()> gOnMedia;
std::function<void(int, SemanticsAction, float)> gOnSemanticsAction;
std::function<void(int, std::string)> gOnSetText;
std::function<void(std::string)> gOnDeepLink;
std::function<void(BackPhase, const BackEventCompat&)> gOnBack;

std::atomic<bool> gAccessibility{false};
std::atomic<bool> gPublishRequested{false};  // accessibility just came on: publish even if nothing changed
std::mutex gSemanticsMutex;
std::vector<jint> gSemanticsData;
std::vector<std::string> gLabels, gValues, gStates, gActions;  // gActions: each node's custom action labels, joined by \x1f
std::vector<std::string> gContentTypes;  // each node's autofill hints, joined by \x1f
std::vector<jfloat> gRanges;  // per node: has one (0/1), current, start, end
std::unordered_map<int, std::string> gLiveText;  // what live region nodes said when last published
jmethodID gSemanticsChanged = nullptr;

JNIEnv* env() { return static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv()); }

jbyteArray toBytes(JNIEnv* e, const std::string& s) {
    jbyteArray array = e->NewByteArray(jsize(s.size()));
    e->SetByteArrayRegion(array, 0, jsize(s.size()), reinterpret_cast<const jbyte*>(s.data()));
    return array;
}

// EditorInfo.inputType for the options (constants from android.text.InputType).
int inputType(const ImeOptions& o) {
    constexpr int Text = 1, Number = 2, Phone = 3, Uri = 0x10, Email = 0x20, Password = 0x80, NumberPassword = 0x10,
                  Decimal = 0x2000, CapCharacters = 0x1000, CapWords = 0x2000, CapSentences = 0x4000,
                  AutoCorrect = 0x8000, NoSuggestions = 0x80000, MultiLine = 0x20000;
    int type = Text;
    switch (o.keyboardType) {
    case KeyboardType::Text:
    case KeyboardType::Ascii: break;
    case KeyboardType::Number: type = Number; break;
    case KeyboardType::Decimal: type = Number | Decimal; break;
    case KeyboardType::Phone: type = Phone; break;
    case KeyboardType::Uri: type = Text | Uri; break;
    case KeyboardType::Email: type = Text | Email; break;
    case KeyboardType::Password: type = Text | Password; break;
    case KeyboardType::NumberPassword: type = Number | NumberPassword; break;
    }
    if ((type & 0xF) == Text && !(type & 0xFF0)) {  // plain text: capitalization and suggestions
        if (o.capitalization == KeyboardCapitalization::Characters) type |= CapCharacters;
        if (o.capitalization == KeyboardCapitalization::Words) type |= CapWords;
        if (o.capitalization == KeyboardCapitalization::Sentences) type |= CapSentences;
        type |= o.autoCorrect ? AutoCorrect : NoSuggestions;
        if (!o.singleLine) type |= MultiLine;
    }
    return type;
}

// EditorInfo.imeOptions action (IME_ACTION_*); Default is Done on a single-line field and a new
// line on a multi-line one (no action, IME_FLAG_NO_ENTER_ACTION).
int imeAction(ImeAction a, bool singleLine) {
    if (a == ImeAction::Default && !singleLine) return 1 | 0x40000000;
    switch (a) {
    case ImeAction::None: return 1;
    case ImeAction::Go: return 2;
    case ImeAction::Search: return 3;
    case ImeAction::Send: return 4;
    case ImeAction::Next: return 5;
    case ImeAction::Previous: return 7;
    case ImeAction::Default:
    case ImeAction::Done: return 6;
    }
    return 6;
}

ImeAction fromEditorAction(int code) {
    switch (code) {
    case 1: return ImeAction::None;
    case 2: return ImeAction::Go;
    case 3: return ImeAction::Search;
    case 4: return ImeAction::Send;
    case 5: return ImeAction::Next;
    case 7: return ImeAction::Previous;
    default: return ImeAction::Done;
    }
}

void checkException(JNIEnv* e) {
    if (e->ExceptionCheck()) {
        e->ExceptionDescribe();
        e->ExceptionClear();
    }
}

} // namespace

void setHandlers(std::function<void(const TextEditState&)> onEdit, std::function<void(ImeAction)> onAction,
                 std::function<void(const std::array<float, 46>&)> onInsets, std::function<void(float)> onFontScale) {
    std::lock_guard lock(gMutex);
    gOnEdit = std::move(onEdit);
    gOnAction = std::move(onAction);
    gOnInsets = std::move(onInsets);
    gOnFontScale = std::move(onFontScale);
}

void attach() {
    if (!gClass) return;
    JNIEnv* e = env();
    e->CallStaticVoidMethod(gClass, gAttach);
    checkException(e);
}

void show(const TextEditState& state, Rect area, const ImeOptions& options) {
    if (!gClass) return;
    JNIEnv* e = env();
    jbyteArray text = toBytes(e, state.text);
    e->CallStaticVoidMethod(gClass, gShow, text, jint(state.selectionStart), jint(state.selectionEnd), jint(area.x),
                            jint(area.y), jint(area.width), jint(area.height), jint(inputType(options)),
                            jint(imeAction(options.imeAction, options.singleLine)), jint(++gSession), jboolean(options.showKeyboard));
    e->DeleteLocalRef(text);
    checkException(e);
}

void update(const TextEditState& state) {
    if (!gClass) return;
    JNIEnv* e = env();
    jbyteArray text = toBytes(e, state.text);
    e->CallStaticVoidMethod(gClass, gUpdate, text, jint(state.selectionStart), jint(state.selectionEnd));
    e->DeleteLocalRef(text);
    checkException(e);
}

float fontScale() {
    if (!gClass || !gFontScale) return 1;
    JNIEnv* e = env();
    float scale = e->CallStaticFloatMethod(gClass, gFontScale);
    checkException(e);
    return scale > 0 ? scale : 1;
}

void setSystemBarsAppearance(bool lightStatusBars, bool lightNavigationBars) {
    if (!gClass || !gSystemBars) return;
    JNIEnv* e = env();
    e->CallStaticVoidMethod(gClass, gSystemBars, jboolean(lightStatusBars), jboolean(lightNavigationBars));
    checkException(e);
}

std::optional<MediaInfo> mediaInfo() {
    if (!gClass || !gMediaInfo) return std::nullopt;
    JNIEnv* e = env();
    auto result = static_cast<jintArray>(e->CallStaticObjectMethod(gClass, gMediaInfo));
    checkException(e);
    if (!result) return std::nullopt;
    jint v[5] = {};
    bool ok = e->GetArrayLength(result) == 5;
    if (ok) e->GetIntArrayRegion(result, 0, 5, v);
    e->DeleteLocalRef(result);
    if (!ok) return std::nullopt;
    const PointerPrecision pointers[] = {PointerPrecision::None, PointerPrecision::Blunt, PointerPrecision::Coarse, PointerPrecision::Fine};
    const ViewingDistance distances[] = {ViewingDistance::Near, ViewingDistance::Medium, ViewingDistance::Far};
    return MediaInfo{.pointerPrecision = pointers[std::clamp(int(v[0]), 0, 3)], .physicalKeyboard = v[1] != 0, .hasMicrophone = v[2] != 0,
                     .hasCamera = v[3] != 0, .viewingDistance = distances[std::clamp(int(v[4]), 0, 2)]};
}

void setMediaHandler(std::function<void()> onChanged) {
    std::lock_guard lock(gMutex);
    gOnMedia = std::move(onChanged);
}

std::optional<TonalPalette> dynamicPalette() {
    if (!gClass || !gDynamicPalette) return std::nullopt;
    JNIEnv* e = env();
    auto result = static_cast<jintArray>(e->CallStaticObjectMethod(gClass, gDynamicPalette));
    checkException(e);
    if (!result) return std::nullopt;
    std::vector<jint> argb(65);
    bool ok = e->GetArrayLength(result) == 65;
    if (ok) e->GetIntArrayRegion(result, 0, 65, argb.data());
    e->DeleteLocalRef(result);
    if (!ok) return std::nullopt;
    TonalPalette palette;
    std::array<Color, 13>* keys[] = {&palette.accent1, &palette.accent2, &palette.accent3, &palette.neutral1, &palette.neutral2};
    for (int k = 0; k < 5; ++k)
        for (int t = 0; t < 13; ++t) {
            uint32_t c = uint32_t(argb[size_t(k * 13 + t)]);
            (*keys[k])[size_t(t)] = Color::hex(c & 0xFFFFFF);
        }
    return palette;
}

bool accessibilityEnabled() { return gAccessibility; }
bool takePublishRequest() { return gPublishRequested.exchange(false); }

void setSemanticsActionHandler(std::function<void(int, SemanticsAction, float)> handler) {
    std::lock_guard lock(gMutex);
    gOnSemanticsAction = std::move(handler);
}

void setTextActionHandler(std::function<void(int, std::string)> handler) {
    std::lock_guard lock(gMutex);
    gOnSetText = std::move(handler);
}

void autofillFocus(int id, const Rect& bounds, bool entered, float scale) {
    if (!gClass || !gAutofillFocus) return;
    JNIEnv* e = env();
    e->CallStaticVoidMethod(gClass, gAutofillFocus, jint(id), jint(std::lround(bounds.x * scale)), jint(std::lround(bounds.y * scale)),
                            jint(std::lround(bounds.right() * scale)), jint(std::lround(bounds.bottom() * scale)), jboolean(entered));
    checkException(e);
}

void autofillValue(int id, const std::string& text) {
    if (!gClass || !gAutofillValue) return;
    JNIEnv* e = env();
    jbyteArray bytes = toBytes(e, text);
    e->CallStaticVoidMethod(gClass, gAutofillValue, jint(id), bytes);
    e->DeleteLocalRef(bytes);
    checkException(e);
}

void autofillVisibility(int id, bool visible) {
    if (!gClass || !gAutofillVisibility) return;
    JNIEnv* e = env();
    e->CallStaticVoidMethod(gClass, gAutofillVisibility, jint(id), jboolean(visible));
    checkException(e);
}

void autofillDone(bool commit) {
    if (!gClass || !gAutofillDone) return;
    JNIEnv* e = env();
    e->CallStaticVoidMethod(gClass, gAutofillDone, jboolean(commit));
    checkException(e);
}

void setDeepLinkHandler(std::function<void(std::string)> handler) {
    std::lock_guard lock(gMutex);
    gOnDeepLink = std::move(handler);
}

std::optional<ViewConfiguration> viewConfiguration(float scale) {
    if (!gClass || !gViewConfiguration || scale <= 0) return std::nullopt;
    JNIEnv* e = env();
    auto result = static_cast<jfloatArray>(e->CallStaticObjectMethod(gClass, gViewConfiguration));
    checkException(e);
    if (!result) return std::nullopt;
    float v[4] = {};
    if (e->GetArrayLength(result) >= 4) e->GetFloatArrayRegion(result, 0, 4, v);
    e->DeleteLocalRef(result);
    if (v[0] <= 0) return std::nullopt;
    ViewConfiguration c;
    c.longPressTimeout = std::chrono::milliseconds(int64_t(v[0]));
    c.doubleTapTimeout = std::chrono::milliseconds(int64_t(v[1]));
    c.touchSlop = v[2] / scale;
    c.maximumFlingVelocity = v[3] / scale;
    return c;
}

void setSystemGestureExclusion(const std::vector<Rect>& rects, float scale) {
    if (!gClass || !gGestureExclusion) return;
    std::vector<jint> values;
    for (const Rect& r : rects)
        values.insert(values.end(), {jint(std::lround(r.x * scale)), jint(std::lround(r.y * scale)), jint(std::lround(r.right() * scale)),
                                     jint(std::lround(r.bottom() * scale))});
    JNIEnv* e = env();
    jintArray array = e->NewIntArray(jsize(values.size()));
    e->SetIntArrayRegion(array, 0, jsize(values.size()), values.data());
    e->CallStaticVoidMethod(gClass, gGestureExclusion, array);
    e->DeleteLocalRef(array);
    checkException(e);
}

void setContentSensitive(bool sensitive) {
    if (!gClass || !gContentSensitive) return;
    JNIEnv* e = env();
    e->CallStaticVoidMethod(gClass, gContentSensitive, jboolean(sensitive));
    checkException(e);
}

void setFrameRate(float rate, int category) {
    if (!gClass || !gSetFrameRate) return;
    JNIEnv* e = env();
    e->CallStaticVoidMethod(gClass, gSetFrameRate, jfloat(rate), jint(category));
    checkException(e);
}

void setBackEnabled(bool enabled) {
    if (!gClass || !gSetBackEnabled) return;
    JNIEnv* e = env();
    e->CallStaticVoidMethod(gClass, gSetBackEnabled, jboolean(enabled));
    checkException(e);
}

void setBackHandler(std::function<void(BackPhase, const BackEventCompat&)> handler) {
    std::lock_guard lock(gMutex);
    gOnBack = std::move(handler);
}

void publishSemantics(const std::vector<SemanticsNode>& nodes, float scale) {
    std::vector<jint> data;
    std::vector<std::string> labels, values, states, actions, contentTypes;
    std::vector<jfloat> ranges;
    std::unordered_map<int, std::string> live;
    for (const SemanticsNode& n : nodes) {
        int flags = (n.clickable ? 1 : 0) | (n.enabled ? 2 : 0) | (n.focused ? 4 : 0) | (n.editable ? 8 : 0) |
                    (n.scrollable ? 16 : 0) | (n.heading ? 32 : 0) | (n.checked ? 64 : 0) | (n.checked.value_or(false) ? 128 : 0) |
                    (n.settable ? 256 : 0) | (n.password ? 512 : 0) | (n.selectableGroup ? 1024 : 0);
        data.insert(data.end(), {jint(n.id), jint(n.parent), jint(flags), jint(n.role), jint(std::lround(n.bounds.x * scale)),
                                 jint(std::lround(n.bounds.y * scale)), jint(std::lround(n.bounds.right() * scale)),
                                 jint(std::lround(n.bounds.bottom() * scale)), jint(n.liveRegion)});
        labels.push_back(n.label);
        values.push_back(n.value);
        states.push_back(n.stateDescription);
        std::string joined;
        for (const std::string& a : n.customActions) joined += (joined.empty() ? "" : "\x1f") + a;
        actions.push_back(std::move(joined));
        std::string hints;
        for (const std::string& h : n.contentType) hints += (hints.empty() ? "" : "\x1f") + h;
        contentTypes.push_back(std::move(hints));
        ranges.insert(ranges.end(), {n.range ? 1.f : 0.f, n.range ? n.range->current : 0, n.range ? n.range->rangeStart : 0,
                                     n.range ? n.range->rangeEnd : 0});
        if (n.liveRegion != LiveRegionMode::None) live[n.id] = n.label + '\x1f' + n.value + '\x1f' + n.stateDescription;
    }
    bool changed;
    std::vector<jint> spoken;  // live region nodes whose text changed: announced
    {
        std::lock_guard lock(gSemanticsMutex);
        changed = data != gSemanticsData || labels != gLabels || values != gValues || states != gStates || actions != gActions ||
                  ranges != gRanges || contentTypes != gContentTypes;
        for (const auto& [id, text] : live)
            if (auto it = gLiveText.find(id); it != gLiveText.end() && it->second != text) spoken.push_back(id);
        gLiveText = std::move(live);
        if (changed) {
            gSemanticsData = std::move(data);
            gLabels = std::move(labels);
            gValues = std::move(values);
            gStates = std::move(states);
            gActions = std::move(actions);
            gRanges = std::move(ranges);
            gContentTypes = std::move(contentTypes);
        }
    }
    if (changed && gClass && gSemanticsChanged) {
        JNIEnv* e = env();
        jintArray ids = e->NewIntArray(jsize(spoken.size()));
        e->SetIntArrayRegion(ids, 0, jsize(spoken.size()), spoken.data());
        e->CallStaticVoidMethod(gClass, gSemanticsChanged, ids);
        e->DeleteLocalRef(ids);
        checkException(e);
    }
}

PlatformGlyph renderGlyph(const std::string& utf8, int px) {
    PlatformGlyph g;
    if (!gClass || !gRenderGlyph) return g;
    JNIEnv* e = env();
    jbyteArray text = toBytes(e, utf8);
    auto result = static_cast<jintArray>(e->CallStaticObjectMethod(gClass, gRenderGlyph, text, jint(px)));
    e->DeleteLocalRef(text);
    checkException(e);
    if (!result) return g;
    jsize n = e->GetArrayLength(result);
    std::vector<jint> data(static_cast<size_t>(n));
    e->GetIntArrayRegion(result, 0, n, data.data());
    e->DeleteLocalRef(result);
    if (n < 5 || data[0] <= 0 || data[1] <= 0 || n < 5 + data[0] * data[1]) return g;
    g.width = data[0], g.height = data[1], g.left = data[2], g.top = data[3];
    g.advance = float(data[4]) / 64.f;
    g.rgba.resize(size_t(g.width) * size_t(g.height) * 4);
    for (size_t i = 0; i < size_t(g.width) * size_t(g.height); ++i) {  // ARGB ints -> RGBA bytes
        uint32_t argb = uint32_t(data[5 + i]);
        g.rgba[i * 4 + 0] = uint8_t(argb >> 16);
        g.rgba[i * 4 + 1] = uint8_t(argb >> 8);
        g.rgba[i * 4 + 2] = uint8_t(argb);
        g.rgba[i * 4 + 3] = uint8_t(argb >> 24);
    }
    return g;
}

ImageBitmap decodeImage(const std::vector<uint8_t>& bytes, const ImageRequestSize& size) {
    if (!gClass || !gDecodeImage || !gImageBounds || bytes.empty()) return {};
    JNIEnv* e = env();
    if (e->PushLocalFrame(8) != 0) return {};  // a worker thread never returns to Java to free them
    jbyteArray data = e->NewByteArray(jsize(bytes.size()));
    e->SetByteArrayRegion(data, 0, jsize(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
    // Its size first, then decoded scaled down by BitmapFactory's inSampleSize.
    int sample = 0;
    if (auto bounds = static_cast<jintArray>(e->CallStaticObjectMethod(gClass, gImageBounds, data)); bounds && e->GetArrayLength(bounds) == 2) {
        jint wh[2];
        e->GetIntArrayRegion(bounds, 0, 2, wh);
        if (wh[0] > 0 && wh[1] > 0) sample = imageSampleSize(wh[0], wh[1], size);
    }
    checkException(e);
    auto result = sample > 0 ? static_cast<jintArray>(e->CallStaticObjectMethod(gClass, gDecodeImage, data, jint(sample))) : nullptr;
    checkException(e);
    ImageBitmap image;
    if (result) {
        jsize n = e->GetArrayLength(result);
        if (jint* values = n >= 2 ? e->GetIntArrayElements(result, nullptr) : nullptr) {
            int w = values[0], h = values[1];
            if (w > 0 && h > 0 && n >= 2 + w * h) {
                std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4);
                for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {  // ARGB ints -> RGBA bytes
                    auto argb = uint32_t(values[2 + i]);
                    rgba[i * 4 + 0] = uint8_t(argb >> 16);
                    rgba[i * 4 + 1] = uint8_t(argb >> 8);
                    rgba[i * 4 + 2] = uint8_t(argb);
                    rgba[i * 4 + 3] = uint8_t(argb >> 24);
                }
                image = ImageBitmap::fromPixels(w, h, std::move(rgba));
            }
            e->ReleaseIntArrayElements(result, values, JNI_ABORT);
        }
    }
    e->PopLocalFrame(nullptr);
    return image;
}

void setSavedState(const std::vector<uint8_t>& bytes) {
    if (!gClass || !gStateSaved) return;
    JNIEnv* e = env();
    jbyteArray data = e->NewByteArray(jsize(bytes.size()));
    e->SetByteArrayRegion(data, 0, jsize(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
    e->CallStaticVoidMethod(gClass, gStateSaved, data);
    e->DeleteLocalRef(data);
    checkException(e);
}

std::vector<uint8_t> takeRestoredState() {
    if (!gClass || !gTakeRestoredState) return {};
    JNIEnv* e = env();
    auto result = static_cast<jbyteArray>(e->CallStaticObjectMethod(gClass, gTakeRestoredState));
    checkException(e);
    std::vector<uint8_t> bytes;
    if (result) {
        bytes.resize(size_t(e->GetArrayLength(result)));
        e->GetByteArrayRegion(result, 0, jsize(bytes.size()), reinterpret_cast<jbyte*>(bytes.data()));
        e->DeleteLocalRef(result);
    }
    return bytes;
}

std::vector<uint8_t> httpGet(const std::string& url) {
    if (!gClass || !gHttpGet) return {};
    JNIEnv* e = env();
    if (e->PushLocalFrame(8) != 0) return {};
    jstring jurl = e->NewStringUTF(url.c_str());
    auto result = static_cast<jbyteArray>(e->CallStaticObjectMethod(gClass, gHttpGet, jurl));
    checkException(e);
    std::vector<uint8_t> body;
    if (result) {
        body.resize(size_t(e->GetArrayLength(result)));
        e->GetByteArrayRegion(result, 0, jsize(body.size()), reinterpret_cast<jbyte*>(body.data()));
    }
    e->PopLocalFrame(nullptr);
    return body;
}

void hide() {
    if (!gClass) return;
    JNIEnv* e = env();
    e->CallStaticVoidMethod(gClass, gHide);
    checkException(e);
}

void performHapticFeedback(int type) {
    if (!gClass || !gHaptic) return;
    JNIEnv* e = env();
    e->CallStaticVoidMethod(gClass, gHaptic, jint(type));
    checkException(e);
}

void imeControlBy(float px) {
    if (!gClass || !gImeControlBy) return;
    JNIEnv* e = env();
    e->CallStaticVoidMethod(gClass, gImeControlBy, jfloat(px));
    checkException(e);
}

void imeControlEnd(float velocity) {
    if (!gClass || !gImeControlEnd) return;
    JNIEnv* e = env();
    e->CallStaticVoidMethod(gClass, gImeControlEnd, jfloat(velocity));
    checkException(e);
}

} // namespace kompoz::android_ime

using namespace kompoz;

extern "C" {

// Runs when SDLActivity loads libmain.so, on a Java thread that can see the app's classes
// (FindClass on SDL's native thread could not).
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* e = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass local = e->FindClass("dev/kompoz/KompozInput");
    if (!local) {
        e->ExceptionClear();
        SDL_Log("kompoz: dev.kompoz.KompozInput not found, no keyboard input");
        return JNI_VERSION_1_6;
    }
    android_ime::gClass = static_cast<jclass>(e->NewGlobalRef(local));
    e->DeleteLocalRef(local);
    android_ime::gShow = e->GetStaticMethodID(android_ime::gClass, "show", "([BIIIIIIIIIZ)V");
    android_ime::gUpdate = e->GetStaticMethodID(android_ime::gClass, "update", "([BII)V");
    android_ime::gHide = e->GetStaticMethodID(android_ime::gClass, "hide", "()V");
    android_ime::gAttach = e->GetStaticMethodID(android_ime::gClass, "attach", "()V");
    android_ime::gFontScale = e->GetStaticMethodID(android_ime::gClass, "fontScale", "()F");
    android_ime::gRenderGlyph = e->GetStaticMethodID(android_ime::gClass, "renderGlyph", "([BI)[I");
    android_ime::gDecodeImage = e->GetStaticMethodID(android_ime::gClass, "decodeImage", "([BI)[I");
    android_ime::gImageBounds = e->GetStaticMethodID(android_ime::gClass, "imageBounds", "([B)[I");
    android_ime::gHaptic = e->GetStaticMethodID(android_ime::gClass, "performHapticFeedback", "(I)V");
    android_ime::gImeControlBy = e->GetStaticMethodID(android_ime::gClass, "imeControlBy", "(F)V");
    android_ime::gImeControlEnd = e->GetStaticMethodID(android_ime::gClass, "imeControlEnd", "(F)V");
    android_ime::gHttpGet = e->GetStaticMethodID(android_ime::gClass, "httpGet", "(Ljava/lang/String;)[B");
    android_ime::gStateSaved = e->GetStaticMethodID(android_ime::gClass, "stateSaved", "([B)V");
    android_ime::gTakeRestoredState = e->GetStaticMethodID(android_ime::gClass, "takeRestoredState", "()[B");
    android_ime::gDynamicPalette = e->GetStaticMethodID(android_ime::gClass, "dynamicPalette", "()[I");
    android_ime::gMediaInfo = e->GetStaticMethodID(android_ime::gClass, "mediaInfo", "()[I");
    android_ime::gSystemBars = e->GetStaticMethodID(android_ime::gClass, "setSystemBarsAppearance", "(ZZ)V");
    android_ime::gSemanticsChanged = e->GetStaticMethodID(android_ime::gClass, "semanticsChanged", "([I)V");
    android_ime::gSetBackEnabled = e->GetStaticMethodID(android_ime::gClass, "setBackEnabled", "(Z)V");
    android_ime::gViewConfiguration = e->GetStaticMethodID(android_ime::gClass, "viewConfiguration", "()[F");
    android_ime::gGestureExclusion = e->GetStaticMethodID(android_ime::gClass, "setSystemGestureExclusion", "([I)V");
    android_ime::gContentSensitive = e->GetStaticMethodID(android_ime::gClass, "setContentSensitive", "(Z)V");
    android_ime::gSetFrameRate = e->GetStaticMethodID(android_ime::gClass, "setFrameRate", "(FI)V");
    android_ime::gAutofillFocus = e->GetStaticMethodID(android_ime::gClass, "autofillFocus", "(IIIIIZ)V");
    android_ime::gAutofillValue = e->GetStaticMethodID(android_ime::gClass, "autofillValue", "(I[B)V");
    android_ime::gAutofillVisibility = e->GetStaticMethodID(android_ime::gClass, "autofillVisibility", "(IZ)V");
    android_ime::gAutofillDone = e->GetStaticMethodID(android_ime::gClass, "autofillDone", "(Z)V");
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozInput_nativeOnEdit(JNIEnv* e, jclass, jint session, jbyteArray text, jint selStart,
                                                                jint selEnd, jint compStart, jint compEnd) {
    TextEditState state;
    jsize length = e->GetArrayLength(text);
    state.text.resize(size_t(length));
    e->GetByteArrayRegion(text, 0, length, reinterpret_cast<jbyte*>(state.text.data()));
    state.selectionStart = selStart;
    state.selectionEnd = selEnd;
    state.compositionStart = compStart;
    state.compositionEnd = compEnd;
    runOnUiThread([state = std::move(state), session] {
        if (session != android_ime::gSession) return;  // for the field before
        std::function<void(const TextEditState&)> handler;
        {
            std::lock_guard lock(android_ime::gMutex);
            handler = android_ime::gOnEdit;
        }
        if (handler) handler(state);
    });
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozInput_nativeOnInsets(JNIEnv* e, jclass, jintArray values) {
    std::array<jint, 46> raw{};
    if (!values || e->GetArrayLength(values) != 46) return;
    e->GetIntArrayRegion(values, 0, 46, raw.data());
    std::array<float, 46> insets;
    for (size_t i = 0; i < 46; ++i) insets[i] = float(raw[i]);
    runOnUiThread([insets] {
        std::function<void(const std::array<float, 46>&)> handler;
        {
            std::lock_guard lock(android_ime::gMutex);
            handler = android_ime::gOnInsets;
        }
        if (handler) handler(insets);
    });
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozInput_nativeSetAccessibilityEnabled(JNIEnv*, jclass, jboolean enabled) {
    bool was = android_ime::gAccessibility.exchange(enabled != 0);
    if (!enabled && was) {
        // Not kept up to date from now on: the next query (UI Automator connects for each dump)
        // must wait for a fresh tree instead of reading this one.
        std::lock_guard lock(android_ime::gSemanticsMutex);
        android_ime::gSemanticsData.clear();
        android_ime::gLabels.clear();
        android_ime::gValues.clear();
        android_ime::gStates.clear();
    }
    if (enabled && !was) {
        android_ime::gPublishRequested = true;
        runOnUiThread([] {});  // a frame, which publishes the tree
    }
}

JNIEXPORT jobjectArray JNICALL Java_dev_kompoz_KompozInput_nativeSemantics(JNIEnv* e, jclass) {
    std::lock_guard lock(android_ime::gSemanticsMutex);
    jclass objectClass = e->FindClass("java/lang/Object");
    jclass stringClass = e->FindClass("java/lang/String");
    jobjectArray result = e->NewObjectArray(7, objectClass, nullptr);
    jintArray data = e->NewIntArray(jsize(android_ime::gSemanticsData.size()));
    e->SetIntArrayRegion(data, 0, jsize(android_ime::gSemanticsData.size()), android_ime::gSemanticsData.data());
    e->SetObjectArrayElement(result, 0, data);
    e->DeleteLocalRef(data);
    // labels, values, states, custom actions (1-4); the autofill hints (6).
    const std::vector<std::string>* columns[] = {&android_ime::gLabels, &android_ime::gValues, &android_ime::gStates, &android_ime::gActions,
                                                 &android_ime::gContentTypes};
    for (int c = 0; c < 5; ++c) {
        jobjectArray strings = e->NewObjectArray(jsize(columns[c]->size()), stringClass, nullptr);
        for (size_t i = 0; i < columns[c]->size(); ++i) {
            // Through bytes: NewStringUTF wants modified UTF-8, which emoji would break.
            const std::string& s = (*columns[c])[i];
            jbyteArray bytes = android_ime::toBytes(e, s);
            jclass charsetClass = e->FindClass("java/nio/charset/StandardCharsets");
            jfieldID utf8Field = e->GetStaticFieldID(charsetClass, "UTF_8", "Ljava/nio/charset/Charset;");
            jobject utf8 = e->GetStaticObjectField(charsetClass, utf8Field);
            jmethodID ctor = e->GetMethodID(stringClass, "<init>", "([BLjava/nio/charset/Charset;)V");
            jobject str = e->NewObject(stringClass, ctor, bytes, utf8);
            e->SetObjectArrayElement(strings, jsize(i), str);
            e->DeleteLocalRef(str);
            e->DeleteLocalRef(utf8);
            e->DeleteLocalRef(charsetClass);
            e->DeleteLocalRef(bytes);
        }
        e->SetObjectArrayElement(result, c < 4 ? c + 1 : 6, strings);
        e->DeleteLocalRef(strings);
    }
    jfloatArray ranges = e->NewFloatArray(jsize(android_ime::gRanges.size()));
    e->SetFloatArrayRegion(ranges, 0, jsize(android_ime::gRanges.size()), android_ime::gRanges.data());
    e->SetObjectArrayElement(result, 5, ranges);
    e->DeleteLocalRef(ranges);
    return result;
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozInput_nativeSetText(JNIEnv* e, jclass, jint id, jbyteArray text) {
    std::string value(size_t(e->GetArrayLength(text)), '\0');
    e->GetByteArrayRegion(text, 0, jsize(value.size()), reinterpret_cast<jbyte*>(value.data()));
    runOnUiThread([id, value = std::move(value)] {
        std::function<void(int, std::string)> handler;
        {
            std::lock_guard lock(android_ime::gMutex);
            handler = android_ime::gOnSetText;
        }
        if (handler) handler(int(id), value);
    });
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozInput_nativePerformAction(JNIEnv*, jclass, jint id, jint action, jfloat argument) {
    runOnUiThread([id, action, argument] {
        std::function<void(int, SemanticsAction, float)> handler;
        {
            std::lock_guard lock(android_ime::gMutex);
            handler = android_ime::gOnSemanticsAction;
        }
        if (handler) handler(int(id), SemanticsAction(action), float(argument));
    });
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozInput_nativeOnDeepLink(JNIEnv* e, jclass, jstring link) {
    const char* chars = e->GetStringUTFChars(link, nullptr);
    std::string s = chars ? chars : "";
    if (chars) e->ReleaseStringUTFChars(link, chars);
    runOnUiThread([s] {
        std::function<void(std::string)> handler;
        {
            std::lock_guard lock(android_ime::gMutex);
            handler = android_ime::gOnDeepLink;
        }
        if (handler) handler(s);
    });
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozInput_nativeOnBack(JNIEnv*, jclass, jint phase, jfloat x, jfloat y, jfloat progress,
                                                                jint edge) {
    // BackEvent's EDGE_LEFT, EDGE_RIGHT, EDGE_NONE.
    BackEventCompat event{x, y, progress,
                          edge == 1 ? BackEventCompat::SwipeEdge::Right : edge == 2 ? BackEventCompat::SwipeEdge::None
                                                                             : BackEventCompat::SwipeEdge::Left};
    runOnUiThread([phase, event] {
        std::function<void(android_ime::BackPhase, const BackEventCompat&)> handler;
        {
            std::lock_guard lock(android_ime::gMutex);
            handler = android_ime::gOnBack;
        }
        if (handler) handler(android_ime::BackPhase(phase), event);
    });
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozInput_nativeOnFontScale(JNIEnv*, jclass, jfloat scale) {
    runOnUiThread([scale] {
        std::function<void(float)> handler;
        {
            std::lock_guard lock(android_ime::gMutex);
            handler = android_ime::gOnFontScale;
        }
        if (handler) handler(scale);
    });
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozInput_nativeOnMediaChanged(JNIEnv*, jclass) {
    runOnUiThread([] {
        std::function<void()> handler;
        {
            std::lock_guard lock(android_ime::gMutex);
            handler = android_ime::gOnMedia;
        }
        if (handler) handler();
    });
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozInput_nativeOnAction(JNIEnv*, jclass, jint code) {
    ImeAction action = android_ime::fromEditorAction(code);
    runOnUiThread([action] {
        std::function<void(ImeAction)> handler;
        {
            std::lock_guard lock(android_ime::gMutex);
            handler = android_ime::gOnAction;
        }
        if (handler) handler(action);
    });
}

} // extern "C"
