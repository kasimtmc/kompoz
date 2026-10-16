#pragma once

// What the app runs on, to adapt to (Compose's UiMediaScope, mediaQuery and derivedMediaQuery):
// the window's size, the most precise pointer there is, the keyboard, a microphone and a camera,
// how far away the screen is seen, a foldable's posture.
//
//   if (mediaQuery([](const UiMediaScope& m) { return m.pointerPrecision() == PointerPrecision::Fine; }))
//       Text("Üzerine gelince ayrıntı görünür");
//   // Recomposed only when the answer changes, not as the window is resized:
//   auto wide = derivedMediaQuery([](const UiMediaScope& m) { return m.windowWidth() >= 600; });
//   if (wide.get()) TwoPanes(); else OnePane();

#include "graphics.hpp"
#include "runtime.hpp"
#include "state.hpp"

#include <functional>
#include <memory>
#include <type_traits>

namespace kompoz {

// How the window lies on a foldable: flat (and every other device), across a horizontal fold
// (Tabletop, like a laptop) or a vertical one (Book).
enum class Posture { Flat, Tabletop, Book };
// The most precise pointing device there is: Fine (a mouse, a trackpad, a stylus), Coarse (a
// touch screen), Blunt (a game controller's stick), or None.
enum class PointerPrecision { None, Blunt, Coarse, Fine };
// A physical keyboard if one is attached, else the on-screen one while it shows, else none.
enum class KeyboardKind { None, Virtual, Physical };
// How far the screen is seen from: Near (phones, tablets, laptops, desktops), Medium (cars, a
// docked tablet), Far (TVs).
enum class ViewingDistance { Near, Medium, Far };

// What the platform knows of the device (ComposeHost::setMediaInfo).
struct MediaInfo {
    PointerPrecision pointerPrecision = PointerPrecision::None;
    bool physicalKeyboard = false, hasMicrophone = false, hasCamera = false;
    ViewingDistance viewingDistance = ViewingDistance::Near;
    Posture windowPosture = Posture::Flat;
    friend bool operator==(const MediaInfo&, const MediaInfo&) = default;
};

// Each of these is observable on its own: what reads only the pointer isn't recomposed as the
// window is resized. Copies share it.
class UiMediaScope {
public:
    UiMediaScope() : data_(std::make_shared<Data>()) {}
    Posture windowPosture() const { return data_->info.get().windowPosture; }
    float windowWidth() const { return data_->width.get(); }  // dp; changes often: query it in derivedMediaQuery
    float windowHeight() const { return data_->height.get(); }
    PointerPrecision pointerPrecision() const { return data_->info.get().pointerPrecision; }
    KeyboardKind keyboardKind() const {
        const MediaInfo& info = data_->info.get();
        return info.physicalKeyboard ? KeyboardKind::Physical : data_->imeVisible.get() ? KeyboardKind::Virtual : KeyboardKind::None;
    }
    bool hasMicrophone() const { return data_->info.get().hasMicrophone; }
    bool hasCamera() const { return data_->info.get().hasCamera; }
    ViewingDistance viewingDistance() const { return data_->info.get().viewingDistance; }

    // The host's: what changed is told to what read it.
    void update(Size window, bool imeVisible, const MediaInfo& info) const {
        if (data_->width.peek() != window.width) data_->width.set(window.width);
        if (data_->height.peek() != window.height) data_->height.set(window.height);
        if (data_->imeVisible.peek() != imeVisible) data_->imeVisible.set(imeVisible);
        if (!(data_->info.peek() == info)) data_->info.set(info);
    }

    friend bool operator==(const UiMediaScope& a, const UiMediaScope& b) { return a.data_ == b.data_; }

private:
    struct Data {
        MutableState<float> width{0.f}, height{0.f};
        MutableState<bool> imeVisible{false};
        MutableState<MediaInfo> info{MediaInfo{}};
    };
    std::shared_ptr<Data> data_;
};

// Provided by the host at the root.
inline const CompositionLocal<UiMediaScope> LocalUiMediaScope{UiMediaScope()};

// `query` against the app's media now; recomposed when what it read changes.
template <class F>
auto mediaQuery(F&& query) -> std::invoke_result_t<F&, const UiMediaScope&> {
    return query(LocalUiMediaScope.current());
}

// The same as a state that changes only when the answer does (Compose's derivedMediaQuery): for
// queries of the window's size, which changes with every step of a resize.
template <class F>
auto derivedMediaQuery(F query, CallSite site = CallSite::current())
    -> DerivedState<std::decay_t<std::invoke_result_t<F&, const UiMediaScope&>>> {
    using T = std::decay_t<std::invoke_result_t<F&, const UiMediaScope&>>;
    ComposableScope scope(site);
    UiMediaScope media = LocalUiMediaScope.current();
    // The latest query (Compose's rememberUpdatedState), asked again when what it read changes.
    auto latest = remember([] { return std::make_shared<std::function<T(const UiMediaScope&)>>(); });
    *latest = std::move(query);
    return remember(media, [&] { return derivedStateOf([media, latest] { return (*latest)(media); }); });
}

} // namespace kompoz
