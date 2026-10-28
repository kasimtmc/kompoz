#pragma once

// ComposeHost ties a composition, its layout tree and input handling together.
// It knows nothing about windows or GPUs: a platform backend feeds it input events in dp,
// calls frame() and draw(), and gets callbacks for the few things only it can do.
// (Compose equivalents: ComposeScene on desktop, AndroidComposeView on Android.)

#include "gestures.hpp"
#include "layout.hpp"
#include "lifecycle.hpp"
#include "media_query.hpp"
#include "platform.hpp"
#include "runtime.hpp"
#include "strings.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kompoz {

namespace detail {
class Dispatcher;
}

class ViewModelStore;
class DisplayList;

namespace detail {
// Every layout checked against all of it measured again (KOMPOZ_LAYOUT_CHECK; the tests): the
// differences found so far.
void setLayoutCheck(bool on);
int layoutCheckMismatches();
// Every frame drawn twice after it is done, keeping nodes and afresh, the two compared
// (DisplayList::sameFrame; the tests): the frames that came out otherwise so far.
void setKeepCheck(bool on);
int keepCheckMismatches();
// A frame's time on the display's beat, as Android's Choreographer gives it: a whole number of
// refresh periods after the last frame's (`last`; none yet: negative), the nearest to `now`, so that
// what animates moves by equal steps however late in its period each frame starts. At most half a
// period from `now`, and never before `last` (frames within half a period of it share its time).
int64_t frameTimeOnBeat(int64_t last, int64_t now, int64_t period);
} // namespace detail

class ComposeHost {
public:
    struct Callbacks {
        std::function<void()> wake;  // any thread: schedule a frame
        // Show (or re-show) / hide the keyboard or IME for the focused field.
        std::function<void(bool active, Rect area, const TextEditState& state, const ImeOptions& options)> textInput;
        // The app changed the focused field's text itself (e.g. cleared it): resync the IME.
        std::function<void(const TextEditState& state)> textInputChanged;
        std::function<void(Cursor)> cursor;
        std::function<ImageBitmap(std::string_view path)> loadImage;  // decodes an image asset
        // Loads an image away from the UI thread (an asset or file path, or an http(s) URL) and
        // calls `done` with it, from any thread; empty if it couldn't be loaded. A big image may
        // be decoded smaller for `size` (imageSampleSize).
        std::function<void(const std::string& model, ImageRequestSize size, std::function<void(ImageBitmap)> done)> loadImageAsync;
        std::function<std::string()> getClipboard;
        std::function<void(const std::string&)> setClipboard;
        // Dark icons over light system bars (true) or light icons over dark ones.
        std::function<void(bool lightStatusBars, bool lightNavigationBars)> systemBarsAppearance;
        // The screen shown changed: a NavHost's top entry, as its path ("item/42"). The web puts
        // it in the address, so a reload or a shared link opens it again.
        std::function<void(const std::string& path)> locationChanged;
        std::function<void(const std::string& uri)> openUri;          // UriHandler
        std::function<void(HapticFeedbackType)> hapticFeedback;       // HapticFeedback
        std::function<void()> hideSoftwareKeyboard;                    // where there is one (Android)
        // The keyboard moved by hand (imeNestedScroll; Android 11+): by `dp` up (down if negative),
        // then let go with a velocity (dp/s, up positive) to finish it shown or hidden.
        std::function<void(float dp)> imeControlBy;
        std::function<void(float velocity)> imeControlEnd;
        // Draws `draw` (dp, from 0, 0) offscreen at the screen's density and returns the pixels,
        // straight alpha (GraphicsLayer::toImageBitmap).
        std::function<ImageBitmap(Size size, const std::function<void(GraphicsContext&)>& draw)> renderImage;
        // Autofill (a password manager), told after layout: a text field (by its semantics id)
        // got the focus (entered, with its bounds) or lost it; its text changed; it came into the
        // layout or went; the form was sent (commit) or dropped. It fills a field with
        // performSemanticsAction(id, SemanticsAction::SetText, text).
        std::function<void(int id, Rect bounds, bool entered)> autofillFocus;
        std::function<void(int id, const std::string& text)> autofillValue;
        std::function<void(int id, bool visible)> autofillVisibility;
        std::function<void(bool commit)> autofillDone;
        // An asset's bytes (vectorResource's XML): the app's assets (the APK's on Android), or a
        // file next to the binary; empty if there is none.
        std::function<std::vector<uint8_t>(std::string_view path)> readAsset;
    };

    ComposeHost(TextMeasurer& measurer, Content content);
    ~ComposeHost();
    ComposeHost(const ComposeHost&) = delete;
    ComposeHost& operator=(const ComposeHost&) = delete;

    void setCallbacks(Callbacks callbacks);
    void setViewportSize(Size size);
    Size viewportSize() const { return viewport_; }
    // What each kind of system UI covers (WindowInsets::statusBars() & co., LocalWindowInsets).
    void setSystemInsets(SystemInsets insets);
    // imeNestedScroll's: moves the keyboard by `dp` up (down if negative) under the finger, where
    // a text field has the focus and the platform can; returns how much it moved (none past its
    // bottom, or past its height once the platform said it). Then let go.
    float imeControlBy(float dp);
    void imeControlEnd(float velocity);
    bool imeControlling() const { return imeControlling_; }
    // For a platform that only knows a safe area (desktop, iOS): taken as the display cutout.
    void setWindowInsets(PaddingValues insets);
    void setSystemDarkTheme(bool dark);          // read via isSystemInDarkTheme()
    void setWindowFocused(bool focused);         // read via LocalWindowInfo
    // The platform's gesture thresholds: the host's own presses, drags and flings follow them,
    // and LocalViewConfiguration at the root gives them to pointerInput blocks.
    void setViewConfiguration(const ViewConfiguration& configuration);
    const ViewConfiguration& viewConfiguration() const { return viewConfiguration_; }
    // The app's direction (the system language's): LocalLayoutDirection at the root.
    void setLayoutDirection(LayoutDirection direction);
    // The library's texts in the system language (Strings::forLanguage): LocalStrings at the root.
    void setStrings(Strings strings);
    void setDensity(Density density);            // read via LocalDensity
    Density density() const { return density_; }
    // The system's wallpaper palette (Android 12+), read via dynamicLightColorScheme(); none elsewhere.
    void setDynamicTonalPalette(std::optional<TonalPalette> palette);
    // The wallpaper's own palette where the platform reads it, read via wallpaperLightColorScheme().
    void setWallpaperTonalPalette(std::optional<TonalPalette> palette);
    // The device as mediaQuery tells it (the pointer, the keyboard, a microphone and a camera,
    // the viewing distance, the posture); the window's size and the on-screen keyboard come from
    // setViewportSize and setSystemInsets.
    void setMediaInfo(const MediaInfo& info);
    const MediaInfo& mediaInfo() const { return mediaInfo_; }
    // SystemBarsAppearance(): passed on to the platform when it changes.
    void setSystemBarsAppearance(bool lightStatusBars, bool lightNavigationBars);

    // Runs posted tasks, recomposition and layout as needed. Returns true if a redraw is due.
    bool frame(int64_t frameTimeNanos);
    void draw(GraphicsContext& canvas);
    // True while there is work for the next frame (invalidated state, running animation, ...).
    bool hasPendingWork() const;

    // Input, in dp. Times are on the frame clock (-1: the last frame's time); they drive
    // fling velocity. Each finger has its own pointer id: the first one down leads (clicks,
    // scrolling); the others join its gesture (pinch, twist) until the last one lifts.
    // A finger or a mouse: a long press selects text, a tap shows a caret handle, for fingers;
    // a double click selects a word, for the mouse.
    enum class PointerType { Touch, Mouse };
    void pointerDown(Offset position, int64_t timeNanos = -1, int64_t pointerId = 0, PointerType type = PointerType::Touch);
    void pointerMove(Offset position, int64_t timeNanos = -1, int64_t pointerId = 0);
    void pointerUp(Offset position, int64_t timeNanos = -1, int64_t pointerId = 0);
    void pointerExit();
    PointerType pointerType() const { return pointerType_; }  // of the gesture under way, or the last one
    void scroll(Offset position, float wheelDelta, float horizontalWheelDelta = 0);
    // Zooms what is under `position` by `factor` and turns it by `rotation` degrees clockwise
    // (transformable): Ctrl+wheel, a touchpad pinch, Ctrl+Shift+wheel. Returns whether something took it.
    bool zoom(Offset position, float factor, float rotation = 0);
    void textInput(std::string_view text);  // typed text, inserted at the caret (desktop keyboards)
    void textEdit(const TextEditState& state);  // full state from an input method (Android IME)
    // Returns whether the key was used. It goes to onPreviewKeyEvent handlers, the focused text
    // field and onKeyEvent handlers (see KeyEventElement), then: Tab and the arrow keys move focus,
    // Enter and Space click a focused clickable, Escape (and Android's back button) closes the top
    // popup, clears focus or runs a BackHandler; unused, the platform does its default (leave the app).
    bool keyDown(Key key, KeyModifiers modifiers = {});
    // Keys the focused node and those around it left alone go to these, the latest first (a
    // window's menu shortcuts, MenuBar); each stays while the returned token is held.
    std::shared_ptr<void> addKeyHandler(std::function<bool(const KeyEvent&)> handler);
    bool keyUp(Key key, KeyModifiers modifiers = {});
    // Touch text selection: a long press on a text field selects the word under it and shows
    // the field's handles and toolbar, until the text changes, the focus moves, a key is
    // pressed or the user presses elsewhere. The handles and the toolbar use these.
    enum class TextAction { Cut, Copy, Paste, SelectAll };
    void setTextSelection(int start, int end);  // in the focused field, clamped
    void textAction(TextAction action);
    // A secondary (right) click: the innermost context menu under it opens there — a
    // ContextMenuArea's, or a text field's own after it takes the focus (the caret moves to the
    // click unless that is inside the selection). Returns whether one opened.
    bool secondaryClick(Offset position);
    // The mouse's other buttons (not the primary one, which is pointerDown/pointerUp): their
    // presses and releases go to onPointerEvent handlers (and Modifier::onClick).
    void mouseButton(Offset position, PointerButton button, bool down);
    int mouseButtonPresses() const { return mouseButtonPresses_; }  // of those buttons so far
    // The keys held down (Shift, Ctrl, Alt), as the platform last reported them: pointer events carry them.
    void setKeyboardModifiers(KeyModifiers modifiers) { keyboardModifiers_ = modifiers; }
    KeyModifiers keyboardModifiers() const { return keyboardModifiers_; }
    bool inTouchSelection() const { return touchSelection_; }
    // A drag from outside the app (files from a file manager, text from a browser), as the
    // platform reports it: it comes in (what it may carry is all that is known then), moves
    // over the window, is dropped (with what it does carry) or leaves. The dragAndDropTargets
    // take part as in a drag within the app.
    void externalDragEntered(std::vector<std::string> mimeTypes);
    void externalDragMoved(Offset position);
    void externalDrop(DragAndDropTransferData data, Offset position);
    void externalDragLeft();
    void setClipboardText(const std::string& text) {
        if (callbacks_.setClipboard) callbacks_.setClipboard(text);
    }
    std::string clipboardText() const { return callbacks_.getClipboard ? callbacks_.getClipboard() : std::string(); }
    // The platform services (platform.hpp).
    void openUri(const std::string& uri);  // the app's own links in it, others by the platform
    void performHapticFeedback(HapticFeedbackType type) {
        if (callbacks_.hapticFeedback) callbacks_.hapticFeedback(type);
    }
    void showSoftwareKeyboard() { showTextInput(); }
    void hideSoftwareKeyboard();
    // The software keyboard's action key (Done, Next, Search, ...).
    void imeAction(ImeAction action);

    // Focus control (FocusRequester, FocusManager). A request is resolved after composition,
    // so it may be made before the target node exists.
    void requestFocus(const FocusRequester& requester);
    void freeFocus(const FocusRequester& requester);
    void clearFocus() { setFocus(nullptr); }
    bool moveFocus(FocusDirection direction);
    static LayoutNode* nearestThatWay(const std::vector<LayoutNode*>& order, LayoutNode* focused, FocusDirection direction);

    // BackHandler and PredictiveBackHandler register here; the most recent enabled one gets back
    // (once a popup on top has closed and the focus is cleared).
    struct BackCallback {
        std::function<void()> onBack;  // back pressed, or a back gesture done
        // A back gesture's start, its steps and its calling off (PredictiveBackHandler); without
        // them a gesture counts only once it is done.
        std::function<void(const BackEventCompat&)> onStarted, onProgressed;
        std::function<void()> onCancelled;
        bool enabled = true;
        // Composed in a focusable popup (its LayoutNode::backScope): it gets back only while
        // that popup is on top. Otherwise only while no popup is.
        std::weak_ptr<const void> owner;
        bool owned = false;
    };
    void addBackHandler(std::shared_ptr<BackCallback> callback) { backHandlers_.push_back(std::move(callback)); }
    void removeBackHandler(const std::shared_ptr<BackCallback>& callback) { std::erase(backHandlers_, callback); }
    // The system's back gesture (Android 13+, which leaves the app with an animation of its own
    // unless the app takes back: see wantsBack). It goes to the back handler that would take a
    // press now; a popup on top or the focus waits for it to be done. backInvoked: the gesture
    // done, or back pressed without one (as Escape); false if nothing took it, and the platform
    // leaves the app.
    void backStarted(const BackEventCompat& event);
    void backProgressed(const BackEventCompat& event);
    void backCancelled();
    bool backInvoked();
    // Whether back would do something now: close a popup, clear the focus, go to a back handler.
    bool wantsBack() const;
    size_t overlayCount() const { return overlays_.size(); }

    // Accessibility: the current semantics tree (parents before children; a popup on top hides
    // what is below) and actions on its nodes.
    std::vector<SemanticsNode> semanticsTree();
    // Where the system's edge swipes are kept off (systemGestureExclusion), window dp, as of
    // the last layout (the parts in view): the platform passes them on when they change.
    const std::vector<Rect>& systemGestureExclusion() const { return gestureExclusion_; }
    // Whether a keepScreenOn is laid out, as of the last layout: the platform keeps the screen on.
    bool keepScreenOn() const { return keepScreenOn_; }
    // Whether something sensitive is laid out (sensitiveContent): the platform protects the window.
    bool sensitiveContent() const { return sensitiveContent_; }
    // The refresh rate the laid out nodes would like (preferredFrameRate): the fastest rate asked
    // for (0: none), and the highest category.
    float preferredFrameRate() const { return frameRate_; }
    FrameRateCategory preferredFrameRateCategory() const { return frameRateCategory_; }
    bool performSemanticsAction(int id, SemanticsAction action, float argument = 0);
    bool performSemanticsAction(int id, SemanticsAction action, const std::string& text);  // SetText
    void autofillDone(bool commit);  // AutofillManager::commit / cancel

    void invalidate();  // thread-safe: recompose from the root
    void stateChanged(const void* key);  // thread-safe: recompose the readers of a state
    void wake();                         // thread-safe: ask the platform for a frame
    void requestLayout();  // everything measured again
    void requestRedraw();
    // An animation read while drawing goes on until `frameTime`: until then there is work to do
    // (frames to draw it in), drawn or not.
    void animateDrawingUntil(int64_t frameTime);
    // What a node asked for as it drew, any of it showing (detail::DrawAsk): a redraw, and frames
    // until `until`.
    void redrawDrawn(int64_t until);
    // `draw` (dp, from 0, 0) as pixels, by the platform (Callbacks::renderImage); empty without one.
    ImageBitmap renderImage(Size size, const std::function<void(GraphicsContext&)>& draw) const;
    // The text magnifier (Android's, as Compose shows it while a selection handle or the cursor
    // is dragged by a finger): a loupe 42 dp above `source` (in the window) showing what is
    // there 1.25 times bigger. Gone with hideTextMagnifier().
    void showTextMagnifier(Offset source);
    void hideTextMagnifier();
    std::optional<Offset> textMagnifier() const { return textMagnifier_; }
    void registerMagnifier(LayoutNode& node);  // Modifier::magnifier's node, drawn on top while it lives

    LayoutNode& root() { return *rootGroup_->node; }
    // The app's lifecycle (LocalLifecycleOwner outside a navigation entry): resumed from the
    // start; the platform moves it as the window goes to the background and back.
    const std::shared_ptr<LifecycleOwner>& lifecycleOwner() const { return lifecycleOwner_; }
    Cursor cursor() const { return cursor_; }  // the mouse pointer's look now
    void setLifecycleState(LifecycleState state) { lifecycleOwner_->lifecycle().setCurrentState(state); }
    // The app's ViewModels: viewModel() outside a navigation entry (see viewmodel.hpp).
    const std::shared_ptr<ViewModelStore>& viewModelStore();
    // A link the app was opened with, or that was sent to it while it runs (a URI such as
    // "kompoz://item/42", or a route's path): the first NavHost with a destination for it goes
    // there (navigation's deep links). On the composition thread.
    void openDeepLink(std::string link);
    // NavHost's: the pending link, taken, if `accepts` it. Recomposes the caller when one comes.
    std::optional<std::string> takeDeepLink(const std::function<bool(const std::string&)>& accepts);
    // NavHosts in the composition say which links they answer, so that openUri opens the app's
    // own links in it instead of asking the system (no desktop knows the app's kompoz:// scheme).
    void addDeepLinkHandler(const void* owner, std::function<bool(const std::string&)> accepts);
    void removeDeepLinkHandler(const void* owner);
    // The rememberSaveable values that can be written out (see saved_value.hpp), as bytes the
    // platform keeps while it may end the process (Android's saved instance state); given back
    // to restoreState before the first frame, they come back. On the composition thread.
    std::vector<uint8_t> saveState() const;
    void restoreState(const std::vector<uint8_t>& bytes);
    void reportLocation(const std::string& path) {
        if (callbacks_.locationChanged) callbacks_.locationChanged(path);
    }
    detail::Dispatcher& dispatcher() { return *dispatcher_; }
    // When the next coroutine timer (delay) is due, -1 if none: the platform should wake
    // up then even if nothing else happens.
    int64_t nextTimerDeadline() const;
    bool hasTimers() const;
    int64_t frameTime() const { return lastFrameTime_; }
    // The ripples of a pressed clickable (drawn by it): where, how big and how strong now.
    static constexpr int64_t kRippleEnter = 225'000'000, kRippleFadeIn = 75'000'000, kRippleFadeOut = 150'000'000;
    static constexpr float kRippleAlpha = 0.12f;
    struct RippleDraw {
        Offset center;  // in the window
        float radius = 0, alpha = 0;
    };
    std::vector<RippleDraw> ripples(const LayoutNode& node, const Rect& bounds, const Indication& indication) const;
    // One ripple at `now` (frame time) in `bounds`, pressed at `origin` (in it) at `start` and let
    // go at `released` (-1: held); nothing once it has faded.
    static std::optional<RippleDraw> rippleAt(const Rect& bounds, const Indication& indication, Offset origin, int64_t start,
                                              int64_t released, int64_t now);
    static bool rippleDone(int64_t start, int64_t released, int64_t now) {
        return released >= 0 && now >= std::max(released, start + kRippleEnter) + kRippleFadeOut;
    }
    static bool rippleMoving(int64_t start, int64_t released, int64_t now) { return released >= 0 || now < start + kRippleEnter; }
    // A layout-driven animation (animateContentSize) needs another frame with layout: the node
    // measuring now is measured again then (none measuring: everything).
    void requestLayoutFrame();
    // The incremental layout's: the states each node's measure read, and their changes.
    void recordLayoutRead(const void* key, LayoutNode& node);
    void forgetLayoutReads(LayoutNode& node);
    void invalidateLayoutReaders(const void* key);
    // The same for drawing (what a frame that keeps nodes draws again).
    void recordDrawRead(const void* key, LayoutNode& node);
    void forgetDrawReads(LayoutNode& node);
    void invalidateDrawReaders(const void* key);
    const detail::RecompositionStats& stats() const { return composer_.stats(); }
    TextMeasurer& textMeasurer() { return measurer_; }
    // An image asset, decoded (imageResource): the same bitmap for the same path while it is among
    // those used last (up to kImageCacheBytes of pixels), as Android keeps its resources'.
    ImageBitmap loadImage(std::string_view path);
    static constexpr size_t kImageCacheBytes = 32u << 20;
    std::vector<uint8_t> readAsset(std::string_view path) const { return callbacks_.readAsset ? callbacks_.readAsset(path) : std::vector<uint8_t>{}; }
    const auto& imageLoader() const { return callbacks_.loadImageAsync; }
    LayoutNode* focusedNode() const { return focused_; }
    void bringIntoView(LayoutNode& node);  // scrolls the scrollables around it to show it
    // `rect` of it (absolute; unset: its focus rect) brought `fraction` of the way into view
    // (1: there), the innermost scrollable first. A BringIntoViewRequester's steps.
    void bringIntoView(LayoutNode& node, std::optional<Rect> rect, float fraction);

    // Called by LayoutNode's destructor.
    void nodeRemoved(LayoutNode* node);

private:
    struct Hit {
        LayoutNode* node;
        const ModifierElement* element;
    };
    std::vector<Hit> hitTest(Offset position);
    // A touch: the nearest smaller-than-minimum target inside what it touched (or anywhere, if it
    // touched nothing) whose minimum touch target holds it, put first.
    void addNearTouchTarget(std::vector<Hit>& hits, Offset p);
    // A clickable's bounds, for a finger grown to the minimum touch target.
    Rect touchBoundsOf(const LayoutNode& node, const ModifierElement* element) const;
    void setHoveredTrigger(LayoutNode* trigger);
    void collectHits(LayoutNode& node, Offset position, std::vector<Hit>& out);
    struct PointerTarget {
        LayoutNode* node;
        std::shared_ptr<detail::PointerInputState> state;
    };
    // Sends an event about `pointerId` to one pointerInput handler of the current gesture, with
    // every finger's change, in its local coordinates.
    void deliver(PointerTarget& target, PointerEventType type, int64_t pointerId, const std::shared_ptr<bool>& consumed,
                 int64_t time, PointerEventPass pass = PointerEventPass::Main);
    // The Initial or Final pass of an event: to the handlers waiting for it, outermost first
    // (`targets` are innermost first). The Main pass is each caller's own loop.
    void deliverPass(std::vector<PointerTarget> targets, PointerEventType type, int64_t pointerId,
                     const std::shared_ptr<bool>& consumed, int64_t time, PointerEventPass pass);
    // A move to the gesture's handlers, innermost first; `scrollAxis` >= 0: a scrollable that may
    // take the gesture once the handlers inside it passed on the move.
    void deliverMove(int64_t pointerId, Offset p, int64_t time, int scrollAxis);
    struct TrackedPointer {
        int64_t id;
        Offset position, previous;  // previous: as the last event told the handlers
        bool pressed = true, previousPressed = false;
    };
    TrackedPointer* trackedPointer(int64_t id);
    void commitPointers();  // after an event: what the next one compares against
    void cancelPointerTargets(int64_t time);
    void unpress();
    void fireClick(LayoutNode* target, int64_t time);
    void runClickTimers(int64_t now);
    // `touched`: by a tap (or accessibility's click), which brings up the software keyboard even
    // for a field that doesn't want it on focus.
    void setFocus(LayoutNode* node, bool touched = false);
    bool dispatchKey(const KeyEvent& event);
    bool editKey(Key key, KeyModifiers modifiers);  // the focused text field's editing keys
    bool defaultKeyAction(Key key, KeyModifiers modifiers);
    void applyEdit(const TextEditState& state);  // a host-side edit: tell the app and the input method
    void notifyFocusChanged();
    LayoutNode* findRequester(LayoutNode& node, const FocusRequester::Data* data);
    void collectFocusables(LayoutNode& node, std::vector<LayoutNode*>& out);
    void showTextInput(bool touched = true);
    void syncTextInput();
    static TextEditState fieldState(const FocusableElement& f);  // what the input method starts from
    // Texts the input method sent to the focused field that the app hasn't shown yet (and its
    // value before them): its state may come back a frame or two later (a StateFlow collected
    // as state), and until then its old value is no change of its own.
    std::vector<std::string> unechoed_;
    int64_t lastEditAt_ = 0;
    void updateHover(Offset position);
    bool layout();  // whether anything was measured afresh or moved
    void measureAndPlace();
    void layoutOverlays(LayoutNode& node);
    void placeOverlay(LayoutNode& popup, LayoutNode& anchor);
    LayoutNode* inputOverlay() const;  // the top focusable popup, if any
    // The back handler back goes to now: the most recent enabled one of the top popup (or of
    // the window, with none on top).
    std::shared_ptr<BackCallback> backTarget() const;
    int scrollAxis(Offset p, bool mostlyAlong) const;
    void startDrag(int axis, Offset p, int64_t time);
    float nestedScroll(LayoutNode& node, int axis, float delta, NestedScrollSource source);
    // Returns the velocity left at once (finger direction) when no fling goes on; none while one does.
    std::optional<float> startFling(LayoutNode& node, int axis, float velocity, int64_t time);
    void stepFling(int64_t now);
    void stopFling();
    // A mouse wheel's scroll on its way (Compose's MouseWheelScrollingLogic): `target` dp along
    // `axis` in all, `done` of it scrolled; animated linearly from `from` over `durationNanos`.
    struct WheelScroll {
        LayoutNode* node = nullptr;
        int axis = 0;
        float target = 0, done = 0, from = 0;
        int64_t start = 0, durationNanos = 0;
    };
    std::optional<WheelScroll> wheel_;
    void stepWheel(int64_t now);
    void stopWheel();
    // Overscroll: a touch drag on a scrollable goes through its overscroll effect, and so does
    // the fling after it, in a coroutine that waits for the host's fling (performFling).
    std::shared_ptr<OverscrollEffect> overscrollOf(LayoutNode& node, int axis) const;
    TaskOf<Velocity> performFling(std::weak_ptr<LayoutNode*> node, int axis, Velocity velocity, int64_t time);
    void finishFling(float left);  // the fling ended with `left` (finger direction): performFling goes on
    struct FlingEnd {
        ComposeHost* host;
        bool await_ready() const noexcept { return false; }
        template <class P>
        void await_suspend(std::coroutine_handle<P> h) {
            host->flingWaiter_ = {h, h.promise().job->weak_from_this()};
        }
        float await_resume() const noexcept { return host->flingLeft_; }
    };
    bool handOverToPinch(int64_t time);
    int64_t timeOr(int64_t t) const { return t >= 0 ? t : lastFrameTime_; }

    TextMeasurer& measurer_;
    Content content_;
    Callbacks callbacks_;
    detail::Composer composer_;
    std::unique_ptr<detail::Dispatcher> dispatcher_;
    std::unique_ptr<detail::Group> rootGroup_;  // owns the root node
    Size viewport_;
    SystemInsets insets_;
    bool imeControlling_ = false;
    float imeHeight_ = 0;  // dp: where the keyboard is (as it was moved by hand)
    bool systemDark_ = false;
    bool windowFocused_ = true;
    ViewConfiguration viewConfiguration_;
    int64_t longPressNanos() const { return std::chrono::nanoseconds(viewConfiguration_.longPressTimeout).count(); }
    LayoutDirection layoutDirection_ = LayoutDirection::Ltr;
    Strings strings_;
    Density density_;
    std::optional<TonalPalette> dynamicPalette_, wallpaperPalette_;
    UiMediaScope media_;  // LocalUiMediaScope
    MediaInfo mediaInfo_;
    std::optional<std::pair<bool, bool>> barsAppearance_;

    std::atomic<bool> rootInvalid_{false};
    std::vector<const void*> pendingChanges_;  // guarded by the global host mutex
    bool animating_ = false, layoutDirty_ = true, redraw_ = true, layoutAnimating_ = false;
    bool layoutEverything_ = true;  // next layout measures every node again
    std::vector<std::weak_ptr<LayoutNode*>> animatingLayouts_;  // measured again next frame
    std::unordered_map<const void*, std::vector<LayoutNode*>> layoutReaders_;
    std::unordered_map<const void*, std::vector<LayoutNode*>> drawReaders_;
    struct CachedImage {
        ImageBitmap image;
        size_t bytes = 0;
        uint64_t used = 0;  // (imageUses_ when last asked for)
    };
    std::unordered_map<std::string, CachedImage> images_;  // loadImage's
    size_t imageBytes_ = 0;
    uint64_t imageUses_ = 0;
    std::unique_ptr<std::pair<DisplayList, DisplayList>> keepCheck_;  // setKeepCheck's: keeping nodes, afresh
    void checkKept();

    std::vector<std::pair<LayoutNode*, LayoutNode*>> overlays_;  // popup, anchor; bottom to top
    std::vector<std::weak_ptr<std::function<bool(const KeyEvent&)>>> keyHandlers_;  // addKeyHandler's
    std::vector<std::shared_ptr<BackCallback>> backHandlers_;
    std::shared_ptr<BackCallback> backGesture_;  // the one a back gesture going on is for
    bool swallowGesture_ = false;
    std::shared_ptr<FocusRequester::Data> pendingFocus_;
    std::unordered_map<LayoutNode*, FocusState> focusStates_;  // last state told to onFocusChanged
    // focusRestorer nodes: what had the focus inside when it left them.
    std::unordered_map<LayoutNode*, std::weak_ptr<LayoutNode*>> restoredFocus_;
    LayoutNode* restoreFocus(LayoutNode* next);  // focus entering a focusRestorer: where it left it
    std::unordered_map<LayoutNode*, int> semanticsIds_;
    int nextSemanticsId_ = 1;
    bool focusDirty_ = false;  // a press outside a popup dismissed it: ignore until release
    bool bringFocusedIntoView_ = false;  // after the next layout (focus moved by keys, keyboard opened)
    std::vector<PointerTarget> pointerTargets_;  // pointerInput handlers of the gesture, innermost first
    std::vector<TrackedPointer> pointers_;       // fingers down, the leading one first
    size_t clickOrder_ = 0;     // how many of them are inside the pressed clickable
    bool pointerClaimed_ = false;  // a handler consumed a move: no scrolling, no click
    int64_t longPressAt_ = -1;     // combinedClickable: when the press becomes a long click
    // A press inside something the finger scrolls shows (its interaction, its ripple) only after
    // the tap timeout, unless it lets go first (Compose's TapIndicationDelay): when, and where.
    int64_t delayedPressAt_ = -1;
    Offset delayedPressPoint_;
    void startDelayedPress(int64_t time);
    bool longClicked_ = false;
    LayoutNode* pendingClick_ = nullptr;  // combinedClickable with onDoubleClick: waiting for a second click
    int64_t pendingClickAt_ = -1;
    bool secondPress_ = false;  // the pending click's node was pressed again in time
    LayoutNode* pressed_ = nullptr;
    LayoutNode* hovered_ = nullptr;
    // Interactions under way, told to the components' interaction sources: a start now, its
    // end later (the node may be gone by then).
    struct ActiveInteraction {
        MutableInteractionSource source;
        Interaction start;
        const LayoutNode* node = nullptr;
    };
    std::optional<ActiveInteraction> pressInteraction_, hoverInteraction_, focusInteraction_, dragInteraction_;
    std::vector<ActiveInteraction> hoverables_;  // hoverable()s under the mouse pointer
    bool mouseOver_ = false;                     // the pointer over the window is a mouse's (touch has no hover)
    struct Ripple {
        const LayoutNode* node = nullptr;
        Offset origin;  // where it was pressed, in the clickable
        int64_t start = 0, released = -1;
        bool moved = true;  // it moved last frame: this one draws where it stopped
    };
    std::vector<Ripple> ripples_;
    // onGloballyPositioned / onPlaced: what each was last told, by node and element.
    std::map<std::pair<const LayoutNode*, size_t>, LayoutCoordinates> positioned_;
    // onVisibilityChanged / onFirstVisible: what each was last found, told and waits to tell.
    struct Visibility {
        bool result = false, reported = false, first = false;  // first: onFirstVisible has been told
        int64_t visibleSince = -1;  // visible, waiting for minDurationMs before telling
        int64_t minDurationNanos = 0;
        std::function<void(bool)> onChanged;
        std::function<void()> onFirst;
        bool seen = false;  // found in this layout
    };
    std::map<std::pair<const LayoutNode*, size_t>, Visibility> visibility_;
    std::vector<std::function<void()>> visibilityCalls_;  // told after the walk (an exit, a timer)
    // onLayoutRectChanged: what each found last, what it told, and when.
    struct RectReport {
        std::function<void(const RelativeLayoutBounds&)> callback;
        int64_t throttleNanos = 0, debounceNanos = 0;
        std::optional<RelativeLayoutBounds> latest, told;
        int64_t changedAt = -1, toldAt = -1;
        bool seen = false;
        bool waiting() const { return latest && latest != told; }
    };
    std::map<std::pair<const LayoutNode*, size_t>, RectReport> rects_;
    std::vector<Rect> gestureExclusion_;
    bool keepScreenOn_ = false, sensitiveContent_ = false;
    float frameRate_ = 0;
    FrameRateCategory frameRateCategory_ = FrameRateCategory::Default;
    // Autofill: the text fields laid out (by semantics id) with the text the platform was last
    // told, and the one it was told has the focus.
    struct AutofillField {
        std::string value;
        bool seen = false;
    };
    std::unordered_map<int, AutofillField> autofillFields_;
    int autofillEntered_ = -1;
    int semanticsIdOf(LayoutNode* node);
    // A clickable's interactions go to its source, or (with a custom indication) to its node's own.
    std::optional<MutableInteractionSource> sourceOf(LayoutNode& node, const ClickableElement* click);
    // Makes (or drops) the node's custom indication node, hearing `source`.
    void syncIndicationNode(LayoutNode& node, const Indication* indication, const std::optional<MutableInteractionSource>& source);
    void tellRect(RectReport& r, int64_t now);
    void tellVisibility(Visibility& v, bool visible);
    void checkVisibilityTimers(int64_t now);
    void reportPositions(int64_t now = 0);
    void startRipple(const LayoutNode& node, const ClickableElement& click, Offset origin, int64_t time);
    void releaseRipples(const LayoutNode* node, int64_t time);
    static void startInteraction(std::optional<ActiveInteraction>& slot, const std::optional<MutableInteractionSource>& source,
                                 const Interaction& start, const LayoutNode* node);
    static void endInteraction(std::optional<ActiveInteraction>& slot, Interaction (Interaction::*end)() const);
    void updateHoverInteractions(Offset p);
    // Calls the onPointerEvent handlers of `type` under `p` (innermost first; only `onlyNode`'s
    // when given); returns whether one consumed the event.
    bool firePointerEvent(PointerEventType type, Offset p, std::optional<PointerButton> button, Offset scrollDelta = {},
                          LayoutNode* onlyNode = nullptr);
    std::vector<LayoutNode*> pointerEntered_;  // the nodes with Enter/Exit handlers the mouse is over
    KeyModifiers keyboardModifiers_;
    int mouseButtonPresses_ = 0;
    // What a press does to the focus once it turns out to be a tap: the text field under it
    // takes it (the caret where it was tapped), or empty space clears it. A drag does neither.
    struct TapFocus {
        bool armed = false;
        LayoutNode* field = nullptr;
        Offset at;
        bool clears = false;
    };
    TapFocus tapFocus_;
    void applyTapFocus();
    // The mouse selecting in a text field, from a press in it (Compose's): one click places the
    // caret, two take words, three paragraphs; dragging extends by them, Shift extends what was
    // selected. The clicks count while they come soon and close.
    struct MouseTextSelection {
        LayoutNode* field = nullptr;
        int anchorStart = 0, anchorEnd = 0;  // what the press took, kept while dragging
        int clicks = 1;
    };
    std::optional<MouseTextSelection> mouseSelection_;
    LayoutNode* lastMousePressField_ = nullptr;
    int64_t lastMousePressTime_ = -1;
    Offset lastMousePressPoint_;
    int mouseClicks_ = 0;
    bool startMouseSelection(LayoutNode& field, Offset p, int64_t time);
    void extendMouseSelection(Offset p);
    LayoutNode* textLongPress_ = nullptr;  // the text field under the press: a long press selects a word
    int64_t textLongPressAt_ = -1;
    Offset textLongPressPoint_;
    bool touchSelection_ = false;
    void setTouchSelection(bool on, bool toolbar = true);
    void selectWordAt(LayoutNode& field, Offset point);
    PointerType pointerType_ = PointerType::Touch;  // of the gesture under way
    int64_t lastTapTime_ = -1;                       // the mouse's last click on a text field, for double clicks
    Offset lastTapPoint_;
    LayoutNode* lastTapField_ = nullptr;
    // Drag and drop: the source under the press (a finger's long press or the mouse's drag
    // starts it), then the drag under way.
    struct DragSession;
    LayoutNode* dndSource_ = nullptr;
    int64_t dndLongPressAt_ = -1;
    std::unique_ptr<DragSession> dnd_;
    std::optional<Offset> textMagnifier_;
    std::vector<std::weak_ptr<LayoutNode*>> magnifiers_;  // nodes with a Modifier::magnifier
    void drawMagnifier(GraphicsContext& canvas, Offset source, Offset center, const MagnifierOptions& options);
    bool startDragAndDrop(Offset p, int64_t time);
    void beginDragAndDrop(std::shared_ptr<const DragAndDropTransferData> data, LayoutNode* source, Offset grab, std::optional<Offset> pointer);
    void moveDragAndDrop(Offset p);
    void endDragAndDrop(bool drop);
    LayoutNode* longPressTrigger_ = nullptr;  // the TooltipBox anchor under the press
    int64_t triggerLongPressAt_ = -1;
    LayoutNode* hoveredTrigger_ = nullptr;
    LayoutNode* focused_ = nullptr;
    LayoutNode* scrollNodes_[2] = {};  // innermost vertical / horizontal scrollable under the pointer
    size_t scrollOrder_[2] = {};       // how many of pointerTargets_ are inside each of them
    int dragAxis_ = -1;                // which of them is being dragged, -1: none yet
    // The scrollable the user moves (a drag, then the fling after it), told as it starts and ends.
    LayoutNode* userScrolled_ = nullptr;
    int userScrolledAxis_ = 0;
    void setUserScrolled(LayoutNode* node, int axis);
    struct Sample {
        int64_t time;
        float position;
    };
    std::vector<Sample> samples_;  // recent drag positions, for the fling velocity
    std::shared_ptr<ViewModelStore> viewModelStore_;
    struct AppLifecycle final : LifecycleOwner {
        mutable Lifecycle state{LifecycleState::Initialized};
        Lifecycle& lifecycle() const override { return state; }
    };
    std::shared_ptr<LifecycleOwner> lifecycleOwner_ = [] {
        auto owner = std::make_shared<AppLifecycle>();
        owner->state.setCurrentState(LifecycleState::Resumed);
        return owner;
    }();
    std::shared_ptr<detail::SaveableRegistry> saveable_;  // rememberSaveable outside any SaveableStateHolder
    std::optional<std::string> deepLink_;
    std::vector<std::pair<const void*, std::function<bool(const std::string&)>>> deepLinkHandlers_;
    MutableState<uint64_t> deepLinks_ = mutableStateOf(uint64_t(0));  // bumped with each one
    LayoutNode* flingNode_ = nullptr;
    int flingAxis_ = 0;
    float flingVelocity_ = 0;  // dp/s towards the end
    float flingStartVelocity_ = 0;  // finger direction, after the nested scroll parents took theirs
    int64_t flingTime_ = -1;
    // Along Android's fling spline: how far it goes (towards the end) and for how long, how long
    // it has gone and how far.
    float flingDistance_ = 0, flingDuration_ = 0, flingElapsed_ = 0, flingTravelled_ = 0;
    struct {
        std::coroutine_handle<> handle;
        std::weak_ptr<detail::JobState> job;
    } flingWaiter_;  // performFling waiting for the fling to end
    float flingLeft_ = 0;
    std::unique_ptr<CoroutineScope> flings_;  // the overscroll effects' applyToFling
    int64_t dragStartTime_ = -1;  // when the current drag claimed the gesture, and how far it went
    float dragTravel_ = 0;
    int64_t lastFrameTime_ = 0;
    int64_t drawingAnimatedUntil_ = 0;
    bool pointerDown_ = false;
    Offset downPosition_, lastPosition_;
    Cursor cursor_ = Cursor::Default;
    TextEditState imeState_;  // the focused field's state as the platform input method knows it
};

namespace detail {
// A back handler with the host's callbacks as given (BackHandler, PredictiveBackHandler, and
// components that follow the back gesture themselves); it belongs to the popup it is composed
// in. The callbacks are the latest composition's.
void backHandler(ComposeHost::BackCallback callbacks, CallSite site = CallSite::current());
} // namespace detail

} // namespace kompoz
