#include "title_bar.hpp"

#include <kompoz/gestures.hpp>
#include <kompoz/icons.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>

namespace kompoz::platform {

namespace {

using Act = std::function<void(WindowAction, Offset)>;

constexpr float kHeight = 40, kButton = 28, kGap = 8;  // dp
const ImageVector kMinimize = Icons::detail::materialIcon("Filled.Minimize", "M6,19h12v2H6z");
const ImageVector kMaximize = Icons::detail::materialIcon("Filled.CropSquare", "M18,4H6C4.9,4 4,4.9 4,6v12c0,1.1 0.9,2 2,2h12c1.1,0 2,-0.9 2,-2V6C20,4.9 19.1,4 18,4zM18,18H6V6h12V18z");
const ImageVector kRestore = Icons::detail::materialIcon("Filled.FilterNone", "M3,5H1v16c0,1.1 0.9,2 2,2h16v-2H3V5zM21,1H7C5.9,1 5,1.9 5,3v14c0,1.1 0.9,2 2,2h14c1.1,0 2,-0.9 2,-2V3C23,1.9 22.1,1 21,1zM21,17H7V3h14V17z");

void Button(WindowButton button, bool maximized, const Act& act) {
    const Strings& s = LocalStrings.current();
    const ColorScheme& cs = colorScheme();
    WindowAction action = button == WindowButton::Close ? WindowAction::Close
                        : button == WindowButton::Minimize ? WindowAction::Minimize : WindowAction::ToggleMaximize;
    const ImageVector& icon = button == WindowButton::Close ? Icons::Filled::Close
                            : button == WindowButton::Minimize ? kMinimize : maximized ? kRestore : kMaximize;
    std::string label = button == WindowButton::Close ? s.close : button == WindowButton::Minimize ? s.minimize : maximized ? s.restore : s.maximize;
    Box(Modifier().size(kButton).clip(CircleShape).background(cs.surfaceContainerHighest).clickable([act, action] { act(action, {}); }),
        Alignment::Center, [&] { Icon(icon, label, Modifier().size(16), cs.onSurface); });
}

void Buttons(const std::vector<WindowButton>& buttons, bool maximized, const Act& act, Alignment side) {
    Row(Modifier().align(side).padding(kGap, 0), {.horizontalArrangement = Arrangement::spacedBy(kGap), .verticalAlignment = Alignment::CenterVertically},
        [&] {
            for (WindowButton b : buttons) key(int(b), [&] { Button(b, maximized, act); });
        });
}

void Bar(const TitleBar& bar, const std::string& title, const Act& act) {
    const ColorScheme& cs = colorScheme();
    bool focused = LocalWindowInfo.current().isWindowFocused;
    // The title in the middle of the window, clear of the buttons on either side.
    size_t most = std::max(bar.start.size(), bar.end.size());
    float side = kGap + float(most) * (kButton + kGap);
    Box(Modifier()
            .layoutId("titleBar")
            .fillMaxWidth()
            .height(kHeight)
            .background(cs.surfaceContainer)
            .pointerInput(0, [act](PointerInputScope& scope) -> Task {
                co_await detectDragGestures(scope, {.onDragStart = [act](Offset at) { act(WindowAction::Move, at); }});
            })
            .onClick([] {}, {.onDoubleClick = [act] { act(WindowAction::ToggleMaximize, {}); }})
            .onPointerEvent(PointerEventType::Press,
                            [act](PointerEvent& e) {
                                if (e.button == PointerButton::Secondary) act(WindowAction::Menu, e.change.position);
                            }),
        [&] {
            Text(title, Modifier().align(Alignment::Center).padding(side, 0),
                 {.font = typography().titleSmall, .color = focused ? cs.onSurface : cs.onSurfaceVariant, .maxLines = 1,
                  .overflow = TextOverflow::Ellipsis});
            Buttons(bar.start, bar.maximized, act, Alignment::CenterStart);
            Buttons(bar.end, bar.maximized, act, Alignment::CenterEnd);
        });
}

} // namespace

void WindowFrame(const std::optional<TitleBar>& bar, const std::string& title, const Act& act, const Content& content) {
    Layout(
        Modifier(),
        [&] {
            // (Each in a group of its own: the content stays what it was as the bar comes and goes.)
            key(0, [&] {
                if (!bar) return;
                bool dark = isSystemInDarkTheme();
                MaterialTheme(dark ? dynamicDarkColorScheme() : dynamicLightColorScheme(), [&] { Bar(*bar, title, act); });
            });
            key(1, content);
        },
        // The bar across the top, the content's nodes each in what is left, as the window holds them.
        [](const std::vector<Measurable>& children, const Constraints& c) {
            float top = 0;
            for (const Measurable& m : children)
                if (m.layoutId() == "titleBar") {
                    Placeable p = m.measure({c.maxWidth, c.maxWidth, 0, c.maxHeight});
                    p.place(0, 0);
                    top = p.height;
                }
            Constraints rest{0, c.maxWidth, 0, std::max(0.f, c.maxHeight - top)};
            for (const Measurable& m : children)
                if (m.layoutId() != "titleBar") m.measure(rest).place(0, top);
            return Size{c.maxWidth, c.maxHeight};
        });
}

} // namespace kompoz::platform
