#pragma once

// Android keyboard/IME bridge used instead of SDL's (see dev.kompoz.KompozInput).

#include <kompoz/layout.hpp>
#include <kompoz/media_query.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kompoz::android_ime {

// The handlers run on the composition thread. Insets are in window pixels: left, top, right, bottom
// of the status bars, navigation bars, keyboard, display cutout and gesture edges.
void setHandlers(std::function<void(const TextEditState&)> onEdit, std::function<void(ImeAction)> onAction,
                 std::function<void(const std::array<float, 46>&)> onInsets, std::function<void(float)> onFontScale);

void attach();  // creates the input view, which also starts reporting window insets

void show(const TextEditState& state, Rect areaPx, const ImeOptions& options);
void update(const TextEditState& state);  // the app changed the text
void hide();
// HapticFeedbackType's value: the input view's performHapticFeedback with the matching constant.
void performHapticFeedback(int type);
// The keyboard moved by hand (imeNestedScroll; Android 11+): by `px` up (down if negative), then
// let go with a velocity (px/s, up positive).
void imeControlBy(float px);
void imeControlEnd(float velocity);
float fontScale();  // the system font size setting (1 = default)
// The device for mediaQuery (KompozInput.mediaInfo), and what to call when it changed (input
// devices coming and going, docking); on the composition thread.
std::optional<MediaInfo> mediaInfo();
void setMediaHandler(std::function<void()> onChanged);

// A glyph drawn by Android (color emoji): RGBA pixels, straight alpha; empty if it drew nothing.
struct PlatformGlyph {
    int width = 0, height = 0, left = 0, top = 0;
    float advance = 0;
    std::vector<uint8_t> rgba;
};
PlatformGlyph renderGlyph(const std::string& utf8, int px);

// Images, from any thread (worker threads attach to the VM): an image file's bytes decoded by
// BitmapFactory, and an http(s) GET's body (empty if it failed).
ImageBitmap decodeImage(const std::vector<uint8_t>& bytes, const ImageRequestSize& size);
std::vector<uint8_t> httpGet(const std::string& url);

// Saved instance state: the host's saved state, kept in the activity's Bundle; and what the
// activity was created again with (empty for a fresh start).
void setSavedState(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> takeRestoredState();

// Accessibility: Java asks for the semantics tree on the UI thread; the app thread publishes it
// (window pixels) while an accessibility service is on.
void setSystemBarsAppearance(bool lightStatusBars, bool lightNavigationBars);
// The wallpaper palette (Android 12+), none before.
std::optional<TonalPalette> dynamicPalette();
// android.view.ViewConfiguration's gesture thresholds, in dp at `scale` pixels per dp.
std::optional<ViewConfiguration> viewConfiguration(float scale);
bool accessibilityEnabled();
bool takePublishRequest();  // true once after accessibility came on
void publishSemantics(const std::vector<SemanticsNode>& nodes, float scale);
// SetProgress's argument is the value, CustomAction's the action's index.
void setSemanticsActionHandler(std::function<void(int id, SemanticsAction action, float argument)> handler);
// A text field's whole text set (autofill filling it, an accessibility service), on the composition thread.
void setTextActionHandler(std::function<void(int id, std::string text)> handler);
// Autofill (Android 8+): a text field (semantics id; dp bounds at `scale` pixels per dp) got the
// focus or lost it, its text changed, it came or went; the form was sent or dropped.
void autofillFocus(int id, const Rect& bounds, bool entered, float scale);
void autofillValue(int id, const std::string& text);
void autofillVisibility(int id, bool visible);
void autofillDone(bool commit);
// Links the app is opened with or sent while it runs (VIEW intents), on the composition thread.
void setDeepLinkHandler(std::function<void(std::string link)> handler);
// Back (Android 13+): the app's callback is there only while the app takes back (otherwise the
// system leaves the app, with its animation). Its events, on the composition thread: the
// gesture's start, steps and calling off (Android 14+; touch in window pixels), and back itself.
enum class BackPhase { Started, Progressed, Cancelled, Invoked };
void setBackEnabled(bool enabled);
// Marks the window's content sensitive (kept out of screen shares and recordings; Android 15+).
void setContentSensitive(bool sensitive);
// The refresh rate the app would like (preferredFrameRate): a rate (Hz; 0: none) and a category
// (FrameRateCategory: 0 none, 1 no preference, 2 normal, 3 high).
void setFrameRate(float rate, int category);
// The window's rects (dp, at `scale` pixels per dp) the system's edge swipes are kept off (Android 10+).
void setSystemGestureExclusion(const std::vector<Rect>& rects, float scale);
void setBackHandler(std::function<void(BackPhase, const BackEventCompat&)> handler);

} // namespace kompoz::android_ime
