#pragma once

// What the platform does for the app, as Compose's LocalUriHandler,
// LocalSoftwareKeyboardController, LocalHapticFeedback and LocalClipboardManager. Get one
// inside a composition (like focusManager()) and keep it for callbacks:
//
//   UriHandler links = uriHandler();
//   TextButton([links] { links.openUri("https://kotlinlang.org"); }, [] { Text("Kotlin"); });

#include <coroutine>
#include <optional>
#include <string>

namespace kompoz {

class ComposeHost;

// Opens a link: one of the app's own (a deep link a NavHost in the composition answers, like
// kompoz://item/7) in the app itself; http(s) in the browser, others (mailto:, tel:) in the
// app that takes them.
class UriHandler {
public:
    explicit UriHandler(ComposeHost* host) : host_(host) {}
    void openUri(const std::string& uri) const;

private:
    ComposeHost* host_;
};
UriHandler uriHandler();

// Hides or shows the software keyboard of the focused text field, which keeps the focus (a
// tap on it brings the keyboard back): KeyboardActions{.onDone = [k] { k.hide(); }}. Nothing
// to hide where there is a hardware keyboard only.
class SoftwareKeyboardController {
public:
    explicit SoftwareKeyboardController(ComposeHost* host) : host_(host) {}
    void show() const;
    void hide() const;

private:
    ComposeHost* host_;
};
SoftwareKeyboardController softwareKeyboardController();

// A short vibration that goes with a gesture, as the system does it (Android's
// performHapticFeedback, so the user's touch feedback setting holds; the browser's vibrate()
// on the web; nothing on desktops). Long clicks, long presses that select text and moving a
// selection handle do it themselves.
enum class HapticFeedbackType {
    LongPress,
    TextHandleMove,
    Confirm,
    Reject,
    ContextClick,
    GestureEnd,
    GestureThresholdActivate,
    KeyboardTap,
    SegmentTick,
    SegmentFrequentTick,
    ToggleOn,
    ToggleOff,
    VirtualKey,
};
class HapticFeedback {
public:
    explicit HapticFeedback(ComposeHost* host) : host_(host) {}
    void performHapticFeedback(HapticFeedbackType type) const;

private:
    ComposeHost* host_;
};
HapticFeedback hapticFeedback();

// The system clipboard's text.
class ClipboardManager {
public:
    explicit ClipboardManager(ComposeHost* host) : host_(host) {}
    void setText(const std::string& text) const;
    std::string getText() const;
    bool hasText() const { return !getText().empty(); }

private:
    ComposeHost* host_;
};
ClipboardManager clipboardManager();

// What is on the clipboard (Compose's ClipEntry): plain text.
struct ClipEntry {
    std::string text;
    static ClipEntry withPlainText(std::string text) { return {std::move(text)}; }
    const std::string& getPlainText() const { return text; }
    friend bool operator==(const ClipEntry&, const ClipEntry&) = default;
};
// The system clipboard as Compose 1.8 has it (Compose's Clipboard, LocalClipboard.current):
// written and read in a coroutine (done at once here):
//   Clipboard clip = clipboard();
//   scope.launch([clip, link]() -> Task { co_await clip.setClipEntry(ClipEntry::withPlainText(link)); });
//   scope.launch([clip, field]() -> Task {
//       if (std::optional<ClipEntry> e = co_await clip.getClipEntry()) field.setTextAndPlaceCursorAtEnd(e->text);
//   });
class Clipboard {
public:
    explicit Clipboard(ComposeHost* host) : host_(host) {}
    // co_await-ed: what it gives back, at once.
    template <class T>
    struct Ready {
        T value;
        bool await_ready() const noexcept { return true; }
        void await_suspend(std::coroutine_handle<>) const noexcept {}
        T await_resume() { return std::move(value); }
    };
    struct Done {
        bool await_ready() const noexcept { return true; }
        void await_suspend(std::coroutine_handle<>) const noexcept {}
        void await_resume() const noexcept {}
    };
    Ready<std::optional<ClipEntry>> getClipEntry() const;  // nothing when it is empty
    Done setClipEntry(std::optional<ClipEntry> entry) const;  // nothing: empties it

private:
    ComposeHost* host_;
};
Clipboard clipboard();

// The platform's autofill (Compose's AutofillManager, LocalAutofillManager.current). Text fields
// are filled by it (a password manager; Android 8+), better with a contentType semantics; when a
// form is sent, commit() lets it offer to keep what was typed, and cancel() drops the form.
//   Button([autofill = autofillManager()] { autofill.commit(); signIn(); }, [] { Text("Giriş"); });
class AutofillManager {
public:
    explicit AutofillManager(ComposeHost* host) : host_(host) {}
    void commit() const;
    void cancel() const;

private:
    ComposeHost* host_;
};
AutofillManager autofillManager();

} // namespace kompoz
