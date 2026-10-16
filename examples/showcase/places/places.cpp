// The places screen: a BottomSheetScaffold over a map.

#include "showcase.hpp"

// BottomSheetScaffold: a pretend map, and a sheet of nearby places peeking over it. Dragged up
// the sheet opens, and then its list scrolls; its buttons move it from code.
void PlacesScreen(Modifier modifier, CallSite site) {
    composable(site, modifier, [=] {
        const ColorScheme& cs = colorScheme();
        auto state = rememberBottomSheetScaffoldState();
        SheetState sheet = state.bottomSheetState;
        auto scope = rememberCoroutineScope();
        const char* names[] = {"Kadıköy İskelesi", "Moda Sahili", "Yeldeğirmeni", "Fenerbahçe Parkı", "Bağdat Caddesi", "Haydarpaşa Garı",
                               "Kalamış Marina", "Süreyya Operası", "Kuşdili Çayırı", "Fikirtepe"};
        auto sheetContent = [&] {
            Column(Modifier().fillMaxWidth().padding(20, 0), [&] {
                Text("Yakındaki yerler", {.font = typography().titleLarge});
                Text("Yukarı sürükleyin: önce sayfa açılır, sonra liste kayar.",
                     {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                Row(Modifier().padding(0, 12), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                    FilledTonalButton([scope, sheet] { scope.launch([sheet]() -> Task { co_await sheet.expand(); }); }, [] { Text("Aç"); });
                    OutlinedButton([scope, sheet] { scope.launch([sheet]() -> Task { co_await sheet.partialExpand(); }); },
                                   [] { Text("Daralt"); });
                });
            });
            LazyColumn(Modifier().fillMaxWidth().height(420), {.contentPadding = {8, 0, 8, 8}}, [&](LazyListScope& list) {
                list.items(30, [&](int i) {
                    ListItem({.headline = [&] { Text(names[i % 10]); },
                              .supporting = [i] { Text(std::format("{:.1f} km · {} dk yürüme", 0.3 + 0.27 * i, 4 + 3 * i)); },
                              .leading = [] { Icon(Icons::Filled::Place, "Yer"); },
                              .containerColor = colors::Transparent});
                });
            });
        };
        BottomSheetScaffold(sheetContent, Modifier().fillMaxSize(), {.scaffoldState = state, .sheetPeekHeight = 128}, [&](PaddingValues padding) {
            Box(Modifier().fillMaxSize(), [&] {
                Canvas(Modifier().fillMaxSize(), [cs](DrawScope& d) {  // streets, a park and the sea
                    Size s = d.size();
                    d.drawRect(cs.surfaceContainerHighest);
                    d.drawRect(Color::hex(0x4F8FBF, 0.45f), {0, s.height * 0.62f}, Size{s.width, s.height * 0.38f});
                    d.drawRoundRect(Color::hex(0x6FAF6F, 0.5f), 24, {s.width * 0.55f, s.height * 0.12f}, Size{s.width * 0.3f, s.height * 0.2f});
                    for (int i = 1; i < 6; ++i) {
                        d.drawLine(cs.outlineVariant, {s.width * float(i) / 6, 0}, {s.width * float(i) / 6 - 40, s.height}, 6);
                        d.drawLine(cs.outlineVariant, {0, s.height * float(i) / 8}, {s.width, s.height * float(i) / 8 + 30}, 4);
                    }
                    for (auto [x, y] : {std::pair{0.3f, 0.3f}, {0.7f, 0.45f}, {0.45f, 0.55f}, {0.2f, 0.5f}}) {
                        d.drawCircle(cs.error, 10.f, Offset{s.width * x, s.height * y});
                        d.drawCircle(colors::White, 4.f, Offset{s.width * x, s.height * y});
                    }
                });
                const char* where = sheet.currentValue() == SheetValue::Expanded ? "açık" : "kısmen açık";
                Box(Modifier().padding(padding).padding(16).background(cs.surface.copy(0.9f), 16).padding(12, 8),
                    [&] { Text(std::format("Sayfa: {}", where), {.font = typography().labelLarge}); });
            });
        });
    });
}
