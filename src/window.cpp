#include <kompoz/host.hpp>
#include <kompoz/state.hpp>
#include <kompoz/window.hpp>

#include <atomic>

namespace kompoz {

struct WindowState::Data {
    MutableState<Size> size;
    MutableState<bool> minimized;
    MutableState<int> frontRequests = mutableStateOf(0);  // toFront's, counted: the app core does each once
};

WindowState::WindowState(Size size, bool isMinimized)
    : data_(std::make_shared<Data>(Data{mutableStateOf(size), mutableStateOf(isMinimized)})) {}
Size WindowState::size() const { return data_->size.get(); }
void WindowState::setSize(Size size) const { data_->size.set(size); }
bool WindowState::isMinimized() const { return data_->minimized.get(); }
void WindowState::setIsMinimized(bool minimized) const { data_->minimized.set(minimized); }
int WindowState::toFrontRequests() const {
    return data_->frontRequests.peek();
}
void WindowState::toFront() const {
    data_->minimized.set(false);
    data_->frontRequests.set(data_->frontRequests.peek() + 1);
}

WindowState detail::rememberWindowState(Size size, bool isMinimized, CallSite site) {
    return remember([&] { return WindowState(size, isMinimized); }, site);
}

void detail::Window(std::function<void()> onCloseRequest, std::optional<WindowState> state, bool visible, std::string title,
                    const Content& content, std::optional<WindowPopup> popup, CallSite site) {
    KompozableScope scope(site);
    static std::atomic<uint64_t> next{1};
    auto& request = remember([] {
        auto r = std::make_shared<WindowRequest>();
        r->id = next++;
        return r;
    });
    WindowState own = remember([] { return WindowState(); });
    KompozHost& host = Kompozer::require().host();
    // Kept up to date as the caller recomposes: the latest close request, content and composition
    // locals (the window's composition reads contentChanges, so it recomposes with them).
    bool changed = request->title != title || request->visible != visible;
    request->title = std::move(title);
    request->visible = visible;
    request->state = state.value_or(own);
    request->onCloseRequest = std::move(onCloseRequest);
    request->popup = std::move(popup);
    KompozitionLocalContext locals = currentKompozitionLocalContext();
    request->content = [locals, content] { KompozitionLocalProvider(locals, content); };
    SideEffect([request, changed, &host] {
        request->contentChanges.set(request->contentChanges.peek() + 1);
        if (changed) host.windowRequestChanged();
    });
    DisposableEffect(0, [request, &host]() -> std::function<void()> {
        host.addWindowRequest(request);
        return [request, &host] { host.removeWindowRequest(request.get()); };
    });
}

} // namespace kompoz
