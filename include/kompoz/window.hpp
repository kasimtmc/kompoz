#pragma once

// Windows of their own on the desktop, as Compose Desktop's Window: composed while they should be
// open, each with its own composition of `content` that shares the app's state and the composition
// locals where it is called (the theme), closed as it leaves the composition.
//
//   var shown by remember { mutableStateOf(true) }
//   if (shown) Window(onCloseRequest = { shown = false }, title = "Mesajlar",
//                     state = rememberWindowState(size = DpSize(400.dp, 860.dp))) {
//       MessagesScreen()
//   }
//
// The app's window closing closes them all. On Android and the web, which give an app one window,
// they aren't shown.

#include "graphics.hpp"
#include "named.hpp"
#include "runtime.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace kompoz {

// A window's state (Compose Desktop's WindowState): its size and whether it is minimized, both
// observable and set from code too (the window follows), the user's changes read back.
class WindowState {
public:
    explicit WindowState(Size size = {800, 600}, bool isMinimized = false);
    Size size() const;  // dp
    void setSize(Size size) const;
    bool isMinimized() const;
    void setIsMinimized(bool minimized) const;
    // The window to the front, restored if it is minimized, the keyboard's focus given to it
    // (Compose Desktop's window.toFront()).
    void toFront() const;
    int toFrontRequests() const;  // toFront's so far, read without observing (the platform's app core does each once)
    friend bool operator==(const WindowState& a, const WindowState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};

namespace detail {
WindowState rememberWindowState(Size size, bool isMinimized, CallSite site);
// rememberWindowState(size = DpSize(…), isMinimized = false), as Compose Desktop's.
struct RememberWindowStateImpl {
    using P = Params<names::Size, names::IsMinimized>;
    template <class... A>
    static WindowState call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        return rememberWindowState(P::get<names::Size>(Size{800, 600}, args...), P::get<names::IsMinimized>(false, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::RememberWindowStateImpl> rememberWindowState;

namespace detail {
// A window the composition asks for: what the platform's app core opens, keeps up to date and
// closes (KompozHost::windowRequests). Its content is composed in a host of its own.
struct WindowRequest {
    uint64_t id = 0;                       // the process's, for the window's life
    std::string title;
    bool visible = true;
    WindowState state;
    std::function<void()> onCloseRequest;  // the user closing it (its title bar's ×, Alt+F4)
    Content content;                       // with the caller's composition locals provided
    MutableState<int> contentChanges = mutableStateOf(0);  // read by the window's composition: its content recomposed
};

// (state: none given, one remembered here)
void Window(std::function<void()> onCloseRequest, std::optional<WindowState> state, bool visible, std::string title,
            const Content& content, CallSite site = CallSite::current());
struct WindowImpl {
    using P = Params<names::OnCloseRequest, names::State, names::Visible, names::Title, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onCloseRequest = P::arg<names::OnCloseRequest>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: Window takes a block: its content");
        Window(std::move(onCloseRequest), P::get<names::State>(std::optional<WindowState>(), args...), P::get<names::Visible>(true, args...),
               std::string(P::get<names::Title>(std::string("Untitled"), args...)), P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::WindowImpl> Window;

} // namespace kompoz
