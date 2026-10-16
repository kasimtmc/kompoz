#pragma once

// Previews (Compose's @Preview): a composable shown on its own, without the app around it. Write
// one next to what it shows, in the app's own sources:
//
//   KOMPOZ_PREVIEW(CounterCardPreview, .width = 360, .height = 220) {
//       MaterialTheme({}, [] { CounterCard(); });
//   }
//
// and start the app with KOMPOZ_PREVIEW=CounterCardPreview: its window shows only that, at that
// size. KOMPOZ_PREVIEW=* (or a name it doesn't know) shows them all, one under another. With
// scripts/dev.sh it comes back on every save; with SDL's offscreen driver and
// KOMPOZ_SCRIPT="wait 10; shot preview.png; quit" it is a picture. The text scale is the
// window's (KOMPOZ_FONT_SCALE=1.3), not a preview's own.
//
// A preview registers itself when the program starts: in a static library it needs the
// library's object linked in (as the app's own sources are).

#include "app.hpp"
#include "graphics.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace kompoz {

struct PreviewSpec {
    float width = 360, height = 640;  // dp: the frame it is shown in (the window, alone)
    std::optional<bool> darkTheme;    // isSystemInDarkTheme() inside it; unset: the system's
    std::optional<Color> background;  // behind it; unset: none (Compose's showBackground = false)
};

namespace detail {
struct Preview {
    const char* name;
    PreviewSpec spec;
    void (*content)();
};
std::vector<Preview>& previews();  // all registered, in no particular order
struct PreviewRegistration {
    explicit PreviewRegistration(const Preview& preview) { previews().push_back(preview); }
};
// What KOMPOZ_PREVIEW=`wanted` makes of the app (the platform's entry calls it): the preview of
// that name alone, its window that size; "*" or a name no preview has, all of them, by name.
void showPreview(Application& app, std::string_view wanted);
// One preview in its frame, as it is shown alone or in the list.
void PreviewFrame(const Preview& preview);
} // namespace detail

} // namespace kompoz

#define KOMPOZ_PREVIEW(name, ...)                                              \
    static void name();                                                        \
    static const ::kompoz::detail::PreviewRegistration name##Registration{     \
        {#name, ::kompoz::PreviewSpec{__VA_ARGS__}, &name}};                   \
    static void name()
