// Material 3 exposed dropdown menu: a menu belonging to a text field.

#include <kompoz/animation.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/icons.hpp>
#include <kompoz/material.hpp>
#include <kompoz/state.hpp>

namespace kompoz {

namespace {

// The box's width, for its menu.
const CompositionLocal<float> LocalExposedDropdownWidth{0.f};

} // namespace

void ExposedDropdownMenuBox(bool expanded, std::function<void(bool)> onExpandedChange, const Modifier& modifier,
                            const Content& content, CallSite site) {
    ComposableScope scope(site);
    auto width = remember([] { return mutableStateOf(0.f); });
    auto latest = rememberUpdatedState(std::pair(expanded, std::move(onExpandedChange)));
    // A tap on the field (not one an icon button in it took) opens or closes the menu; the
    // field still takes the focus as usual.
    Box(modifier.onSizeChanged([width](Size s) { width.set(s.width); }).pointerInput(0, [latest](PointerInputScope& s) -> Task {
        co_await detectTapGestures(s, {.onTap = [latest](Offset) {
                                           const auto& [open, change] = latest.get();
                                           if (change) change(!open);
                                       }});
    }),
        [&] { CompositionLocalProvider({LocalExposedDropdownWidth.provides(width.get())}, content); });
}

void ExposedDropdownMenu(bool expanded, std::function<void()> onDismissRequest, const Content& content, const Modifier& modifier,
                         CallSite site) {
    ComposableScope scope(site);
    auto scroll = rememberScrollState();
    float maxHeight = std::max(160.f, LocalWindowSize.current().height / 2 - 64);
    DropdownMenu(expanded, std::move(onDismissRequest), content,
                 Modifier().width(LocalExposedDropdownWidth.current()).heightIn(0, maxHeight).verticalScroll(scroll).then(modifier));
}

namespace ExposedDropdownMenuDefaults {

void TrailingIcon(bool expanded, CallSite site) {
    ComposableScope scope(site);
    auto turn = animateFloatAsState(expanded ? 180.f : 0.f, tween(150));  // (read as it is drawn)
    Icon(Icons::Filled::ArrowDropDown, expanded ? LocalStrings.current().close : LocalStrings.current().open,
         Modifier().graphicsLayer([turn](GraphicsLayerScope& l) { l.rotationZ = turn.value(); }));
}

} // namespace ExposedDropdownMenuDefaults

} // namespace kompoz
