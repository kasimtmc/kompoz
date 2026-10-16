#include <kompoz/preview.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cstdio>
#include <format>
#include <string>

namespace kompoz::detail {

std::vector<Preview>& previews() {
    static std::vector<Preview> all;  // (filled while the program starts, before main)
    return all;
}

void PreviewFrame(const Preview& preview) {
    const PreviewSpec& spec = preview.spec;
    CompositionLocalProvider({LocalSystemDarkTheme.provides(spec.darkTheme.value_or(isSystemInDarkTheme()))}, [&] {
        Modifier frame = Modifier().size(spec.width, spec.height);
        if (spec.background) frame = frame.background(*spec.background);
        Box(frame.clipToBounds(), [&] { preview.content(); });
    });
}

void showPreview(Application& app, std::string_view wanted) {
    std::vector<Preview> all = previews();
    std::sort(all.begin(), all.end(), [](const Preview& a, const Preview& b) { return std::string_view(a.name) < b.name; });
    auto one = std::find_if(all.begin(), all.end(), [&](const Preview& p) { return wanted == p.name; });
    if (one != all.end()) {
        Preview p = *one;
        app.title += std::format(" · {}", p.name);
        app.width = int(p.spec.width), app.height = int(p.spec.height);
        app.content = [p] { PreviewFrame(p); };
        return;
    }
    if (wanted != "*") {
        std::string names;
        for (const Preview& p : all) names += std::format(" {}", p.name);
        std::fprintf(stderr, "kompoz: no preview named \"%.*s\"; there are:%s\n", int(wanted.size()), wanted.data(),
                     names.empty() ? " none" : names.c_str());
    }
    // All of them, one under another: each one's name and size above its frame, outlined.
    float widest = 0;
    for (const Preview& p : all) widest = std::max(widest, p.spec.width);
    app.title += " · previews";
    app.width = std::max(app.width, int(widest) + 32);
    app.content = [all] {
        Column(Modifier().fillMaxSize().verticalScroll(rememberScrollState()).padding(16), [&] {
            if (all.empty()) Text("No previews (KOMPOZ_PREVIEW(name, ...) { ... } in the app's sources)");
            for (size_t i = 0; i < all.size(); ++i)
                key(i, [&] {
                    const Preview& p = all[i];
                    Text(std::format("{}  {:g} × {:g}", p.name, p.spec.width, p.spec.height), Modifier().padding(0, 0, 0, 4),
                         {.fontSize = 12, .color = Color::hex(0x5F5F5F)});
                    Box(Modifier().border(1, Color::hex(0xBDBDBD)).padding(1), [&] { PreviewFrame(p); });
                    Spacer(Modifier().height(24));
                });
        });
    };
}

} // namespace kompoz::detail
