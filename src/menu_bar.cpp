// A window's menu bar, drawn in the window, with keyboard shortcuts.

#include <kompoz/coroutines.hpp>
#include <kompoz/host.hpp>
#include <kompoz/icons.hpp>
#include <kompoz/material.hpp>
#include <kompoz/state.hpp>

#include <algorithm>

namespace kompoz {

namespace {

constexpr float kBarHeight = 40;

std::string keyText(Key k) {
    if (k >= Key::A && k <= Key::Z) return std::string(1, char('A' + (int(k) - int(Key::A))));
    if (k >= Key::Zero && k <= Key::Nine) return std::string(1, char('0' + (int(k) - int(Key::Zero))));
    if (k >= Key::F1 && k <= Key::F12) return "F" + std::to_string(int(k) - int(Key::F1) + 1);
    switch (k) {
    case Key::Enter: return "Enter";
    case Key::Escape: return "Esc";
    case Key::Tab: return "Tab";
    case Key::Backspace: return "Backspace";
    case Key::Delete: return "Delete";
    case Key::Space: return "Space";
    case Key::Left: return "←";
    case Key::Right: return "→";
    case Key::Up: return "↑";
    case Key::Down: return "↓";
    case Key::Home: return "Home";
    case Key::End: return "End";
    case Key::PageUp: return "PageUp";
    case Key::PageDown: return "PageDown";
    default: return "?";
    }
}

// Runs the first enabled item (submenus too) whose shortcut the key is.
bool runShortcut(const std::vector<MenuBarItem>& items, const KeyEvent& e) {
    for (const MenuBarItem& item : items) {
        if (item.separator) continue;
        if (!item.submenu.empty() && runShortcut(item.submenu, e)) return true;
        if (item.enabled && item.shortcut && item.shortcut->matches(e) && item.onClick) {
            item.onClick();
            return true;
        }
    }
    return false;
}

// Where things were laid out, in the window: the bar, its titles, the open menu's items.
struct Places {
    Rect bar;
    std::vector<Rect> titles, items;
};

Rect windowRect(const LayoutCoordinates& c) {
    Offset at = c.positionInWindow();
    return {at.x, at.y, c.size().width, c.size().height};
}

// A row: the check's place (if the menu has any), the text, and at the end its shortcut or arrow
// (when `trailing`).
Size itemRow(const std::vector<Measurable>& children, const Constraints& c, bool trailing) {
    float x = 0, h = 0;
    std::vector<Placeable> placed;
    for (const Measurable& m : children) placed.push_back(m.measure({0, kInfinity, 0, kInfinity}));
    for (const Placeable& p : placed) h = std::max(h, p.height);
    h = std::max(h, c.minHeight);  // the row's least height, the parts centered in it
    float natural = 0;
    for (size_t i = 0; i < placed.size(); ++i) natural += placed[i].width + (i + 1 < placed.size() ? 12.f : 0.f);
    if (trailing) natural += 12;  // a little more room before the shortcut
    float width = std::max(c.minWidth, natural);
    for (size_t i = 0; i < placed.size(); ++i) {
        bool last = trailing && i + 1 == placed.size() && i > 0;
        float px = last ? width - placed[i].width : x;
        placed[i].place(px, (h - placed[i].height) / 2);
        x += placed[i].width + 12;
    }
    return {width, h};
}

} // namespace

bool KeyShortcut::matches(const KeyEvent& e) const {
    return e.type == KeyEventType::KeyDown && e.key == key && e.modifiers.ctrl == ctrl && e.modifiers.shift == shift &&
           e.modifiers.alt == alt;
}

std::string KeyShortcut::text() const {
    std::string s;
    if (ctrl) s += "Ctrl+";
    if (alt) s += "Alt+";
    if (shift) s += "Shift+";
    return s + keyText(key);
}

void MenuBar(const std::vector<MenuBarMenu>& menus, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    const Typography& t = typography();
    auto open = remember([] { return mutableStateOf(-1); });  // the open menu
    auto sub = remember([] { return mutableStateOf(-1); });   // its item whose submenu is open
    auto places = remember([] { return std::make_shared<Places>(); });
    // The shortcuts: the latest menus, kept for the window's key handler.
    auto latest = remember([] { return std::make_shared<std::vector<MenuBarMenu>>(); });
    *latest = menus;
    ComposeHost* host = &detail::Composer::require().host();
    DisposableEffect(0, [host, latest]() -> std::function<void()> {
        auto token = host->addKeyHandler([latest](const KeyEvent& e) {
            for (const MenuBarMenu& menu : *latest)
                if (runShortcut(menu.items, e)) return true;
            return false;
        });
        return [token] {};
    });
    places->titles.resize(menus.size());
    auto close = [open, sub] {
        open.set(-1);
        sub.set(-1);
    };

    // The titles: in the bar, and again over it while a menu is open (where the mouse moving to
    // another title switches menus).
    auto titles = [&](bool overlay) {
        for (size_t i = 0; i < menus.size(); ++i) {
            key(int(i), [&] {
                auto source = remember([] { return MutableInteractionSource(); });
                bool hovered = source.collectIsHoveredAsState();
                int index = int(i);
                if (overlay)
                    LaunchedEffect(hovered, [hovered, open, sub, index]() -> Task {
                        if (hovered && open.peek() >= 0 && open.peek() != index) {
                            open.set(index);
                            sub.set(-1);
                        }
                        co_return;
                    });
                bool isOpen = open.get() == index;
                Modifier m = Modifier().fillMaxHeight();
                if (!overlay) m = m.onGloballyPositioned([places, i](const LayoutCoordinates& c) { places->titles[i] = windowRect(c); });
                if (isOpen) m = m.background(cs.secondaryContainer, 8);
                m = m.hoverable(source).clickable([open, sub, index, isOpen] {
                    open.set(isOpen ? -1 : index);
                    sub.set(-1);
                });
                Box(m.padding(12, 0), Alignment::Center, [&] {
                    Text(menus[i].text, {.font = t.labelLarge, .color = isOpen ? cs.onSecondaryContainer : cs.onSurface});
                });
            });
        }
    };
    Row(modifier.fillMaxWidth()
            .height(kBarHeight)
            .background(cs.surfaceContainer)
            .onGloballyPositioned([places](const LayoutCoordinates& c) { places->bar = windowRect(c); })
            .padding(4, 4),
        {.horizontalArrangement = Arrangement::spacedBy(2), .verticalAlignment = Alignment::CenterVertically},
        [&] { titles(false); });

    int o = open.get();
    if (o < 0 || size_t(o) >= menus.size()) return;
    const std::vector<MenuBarItem>& items = menus[size_t(o)].items;
    places->items.resize(items.size());

    // The rows of a menu (its items' places kept when `record`).
    std::function<void(const std::vector<MenuBarItem>&, bool)> rows = [&](const std::vector<MenuBarItem>& list, bool record) {
        bool checks = std::any_of(list.begin(), list.end(), [](const MenuBarItem& i) { return i.checked.has_value(); });
        for (size_t j = 0; j < list.size(); ++j) {
            const MenuBarItem& item = list[j];
            if (item.separator) {
                Box(Modifier().padding(0, 4), [&] { HorizontalDivider(); });
                continue;
            }
            key(int(j), [&] {
                auto source = remember([] { return MutableInteractionSource(); });
                bool hovered = source.collectIsHoveredAsState();
                int index = int(j);
                bool hasSub = !item.submenu.empty();
                if (record)  // the mouse on an item opens its submenu, or closes another's
                    LaunchedEffect(hovered, [hovered, sub, index, hasSub]() -> Task {
                        if (hovered) sub.set(hasSub ? index : -1);
                        co_return;
                    });
                Color text = item.enabled ? cs.onSurface : cs.onSurface.copy(0.38f);
                Color quiet = item.enabled ? cs.onSurfaceVariant : cs.onSurface.copy(0.38f);
                Modifier m = Modifier().heightIn(36);
                if (record) m = m.onGloballyPositioned([places, j](const LayoutCoordinates& c) { places->items[j] = windowRect(c); });
                if (record && sub.get() == index) m = m.background(cs.onSurface.copy(0.08f));
                m = m.hoverable(source).clickable(
                    [close, sub, index, hasSub, run = item.onClick] {
                        if (hasSub) return sub.set(index);
                        close();
                        if (run) run();
                    },
                    item.enabled);
                Layout(m.padding(12, 0), [&] {
                    if (checks)
                        Box(Modifier().size(20), Alignment::Center, [&] {
                            if (item.checked.value_or(false)) Icon(Icons::Filled::Check, LocalStrings.current().checked, Modifier().size(18), quiet);
                        });
                    Text(item.text, {.font = t.bodyMedium, .color = text});
                    if (hasSub)
                        Icon(Icons::AutoMirrored::Filled::KeyboardArrowRight, LocalStrings.current().submenu, Modifier().size(18), quiet);
                    else if (item.shortcut)
                        Text(item.shortcut->text(), {.font = t.labelMedium, .color = quiet});
                }, [trailing = hasSub || item.shortcut.has_value()](const std::vector<Measurable>& children, const Constraints& c) {
                    return itemRow(children, c, trailing);
                });
            });
        }
    };
    auto card = [&](const Modifier& m, const std::vector<MenuBarItem>& list, bool record) {
        // A click on the card between its rows does nothing (it isn't outside).
        Layout(m.shadow(3, 4).background(cs.surfaceContainer, 4).clickable([] {}, ClickableOptions{.indication = false}).padding(0, 6),
               [&] { rows(list, record); }, detail::menuRows(320));
    };
    Size window = LocalWindowSize.current();
    Popup({.fullScreen = true, .onDismissRequest = close}, [&] {
        // Over everything: a click outside the menus closes them.
        Box(Modifier().fillMaxSize().clickable(close, ClickableOptions{.indication = false}), [&] {
            Row(Modifier()
                    .offset([places] { return Offset{places->bar.x + 4, places->bar.y + 4}; })
                    .size(std::max(0.f, places->bar.width - 8), kBarHeight - 8),
                {.horizontalArrangement = Arrangement::spacedBy(2), .verticalAlignment = Alignment::CenterVertically},
                [&] { titles(true); });
            // Each card placed knowing its size: the menu under its title, kept in the window; a
            // submenu beside its item, on the left when the right has no room (else against the edge).
            auto placed = [window](std::function<Offset(Size)> where) {
                return Modifier().layout([window, where](const MeasureNext& measure, const Constraints& c, Offset& at) {
                    Size s = measure({0, c.maxWidth, 0, c.maxHeight});
                    Offset p = where(s);
                    at = {std::clamp(p.x, 0.f, std::max(0.f, window.width - s.width)), std::clamp(p.y, 0.f, std::max(0.f, window.height - s.height))};
                    return s;
                });
            };
            card(placed([places, o](Size) { return Offset{places->titles[size_t(o)].x, places->bar.bottom()}; }), items, true);
            int s = sub.get();
            if (s >= 0 && size_t(s) < items.size() && !items[size_t(s)].submenu.empty())
                card(placed([places, s, window](Size size) {
                         Rect at = places->items[size_t(s)];
                         float x = at.right() + 2;
                         if (x + size.width > window.width) x = at.x - size.width - 2 >= 0 ? at.x - size.width - 2 : window.width - size.width;
                         return Offset{x, at.y - 6};
                     }),
                     items[size_t(s)].submenu, false);
        });
    });
}

} // namespace kompoz
