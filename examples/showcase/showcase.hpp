#pragma once

// The showcase: a folder per screen (home, list, components, material, places), each screen's
// sections in files of their own, so that an edit rebuilds only its file. What the parts share.

#include <kompoz/kompoz.hpp>
// (A file still written in C++ includes <kompoz/blocks.hpp> itself, for the block form Column(…) { … };
// one written in Compose's syntax through kompozc must not: the blocks are macros of those names.)

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <format>
#include <map>
#include <numbers>
#include <random>
#include <thread>
#include <vector>

using namespace kompoz;

inline const CompositionLocal<bool> LocalShowRecompositions{false};

// Outlines a node with the recompose highlighter while the switch in the header is on.
inline Modifier traced(Modifier m = {}) { return LocalShowRecompositions.current() ? m.recomposeHighlighter() : m; }

// The screens, as type-safe routes (named, so the addresses read "item/42", "list/all").
namespace routes {
struct Home {
    KOMPOZ_NAMED_ROUTE(Home, "home")
};
struct ListGraph {  // the list and its items: a nested graph
    KOMPOZ_NAMED_ROUTE(ListGraph, "list")
};
struct ListAll {
    KOMPOZ_NAMED_ROUTE(ListAll, "list/all")
};
struct Item {
    int n = 0;
    KOMPOZ_NAMED_ROUTE(Item, "item", n)
};
struct Components {
    KOMPOZ_NAMED_ROUTE(Components, "components")
};
struct Material {
    KOMPOZ_NAMED_ROUTE(Material, "material")
};
struct Places {  // BottomSheetScaffold over a map
    KOMPOZ_NAMED_ROUTE(Places, "places")
};
} // namespace routes

// The likes of the list's items. It belongs to the "list" navigation graph, so the list and the
// item screens share it, and it lives as long as the graph: through trips to other tabs too.
class LikesViewModel : public ViewModel {
public:
    MutableStateFlow<std::map<int, int>> likes{{}};  // item -> likes
    void like(int item) {
        likes.update([item](std::map<int, int> all) {
            ++all[item];
            return all;
        });
    }
};

// Where the app runs, for what differs (a phone has no hardware keyboard).
#ifdef __ANDROID__
inline constexpr bool kOnAndroid = true;
#else
inline constexpr bool kOnAndroid = false;
#endif

// common.cpp (Section and DesktopNote are @Composable there: the module declares them)
ColorScheme animateColorScheme(const ColorScheme& t, CallSite site = CallSite::current());
Color hueColor(int i);

// home/home.cpp: ScrollToTopButton and HomeScreen are @Composable there (the module declares them)

// list/list.cpp: WindowClassLine, MediaLine, ListScreen and ItemScreen are @Composable there (the module declares them)

// components/state.cpp
void IconGallery(CallSite site = CallSite::current());
void IconButtons(CallSite site = CallSite::current());
void StateFlowSection(CallSite site = CallSite::current());
void FlowSearchSection(CallSite site = CallSite::current());
void MovableContentSection(CallSite site = CallSite::current());
void ChannelSection(CallSite site = CallSite::current());
void LifecycleSection(CallSite site = CallSite::current());
void ChatSection(CallSite site = CallSite::current());
void PlatformServicesSection(CallSite site = CallSite::current());

// components/layout.cpp
void ListDetailSection(CallSite site = CallSite::current());
void SupportingPaneSection(CallSite site = CallSite::current());
void LayoutInfoSection(CallSite site = CallSite::current());
void TextMeasureSection(CallSite site = CallSite::current());
void PositionSection(CallSite site = CallSite::current());
void SnapRowSection(CallSite site = CallSite::current());
void CarouselSection(CallSite site = CallSite::current());
void HorizontalGridSection(CallSite site = CallSite::current());
void PagerSection(CallSite site = CallSite::current());
void ZIndexSection(CallSite site = CallSite::current());
void FlexBoxSection(CallSite site = CallSite::current());
void GridSection(CallSite site = CallSite::current());
void LayoutSection(CallSite site = CallSite::current());

// components/scroll.cpp
void FlingSection(CallSite site = CallSite::current());
void DrawersSection(CallSite site = CallSite::current());
void ScrollbarSection(CallSite site = CallSite::current());
void PredictiveBackSection(CallSite site = CallSite::current());
void PointerPassSection(CallSite site = CallSite::current());
void ScrollableSection(CallSite site = CallSite::current());
void OverscrollSection(CallSite site = CallSite::current());
void BringIntoViewSection(CallSite site = CallSite::current());
void NestedScrollSection(CallSite site = CallSite::current());
void PullToRefreshSection(CallSite site = CallSite::current());

// components/graphics.cpp
void ProgressSection(CallSite site = CallSite::current());
void AnimationsSection(CallSite site = CallSite::current());
void AsyncImageSection(CallSite site = CallSite::current());
void ImagesSection(CallSite site = CallSite::current());
void MaterialShapesSection(CallSite site = CallSite::current());
void ShapesSection(CallSite site = CallSite::current());
void SharedElementSection(CallSite site = CallSite::current());
void BlendModeSection(CallSite site = CallSite::current());
void TileModeSection(CallSite site = CallSite::current());
void ColorSpaceSection(CallSite site = CallSite::current());
void CanvasSection(CallSite site = CallSite::current());
void BlurSection(CallSite site = CallSite::current());
void ColorVectorSection(CallSite site = CallSite::current());

// components/drawing.cpp
void DrawingSection(CallSite site = CallSite::current());

// components/input.cpp
void SelectableTextSection(CallSite site = CallSite::current());
void ContextMenuSection(CallSite site = CallSite::current());
void PointerSection(CallSite site = CallSite::current());
void CustomIndicationSection(CallSite site = CallSite::current());
void TextToolbarSection(CallSite site = CallSite::current());
void MultiTouchSection(CallSite site = CallSite::current());
void KeyboardSection(CallSite site = CallSite::current());
void SelectableCard(int index, MutableState<int> selected, CallSite site = CallSite::current());
void DragAndDropSection(CallSite site = CallSite::current());

// components/text.cpp
void TextSection(CallSite site = CallSite::current());

// components/components.cpp

// material/sections.cpp: its sections (SearchSection … InsetsSection), SwipeableTask, FocusPropertiesDemo and
// DynamicColorSection are @Composable there (the module declares them).
// Where the theme's colors come from: the app's own scheme, the system's accent color, or the
// wallpaper. The library offers all three; the app (here its user) picks.
enum class ColorSource { App, System, Wallpaper };

// material/text_fields.cpp: TextFieldsSection is @Composable there (the module declares it)

// material/controls.cpp: ButtonsSection, ChipsSection and SelectionControlsSection are @Composable there (the module declares them)

// material/containers.cpp: AnimateItemSection, ListItemSection, DialogsSection and CardsSection are @Composable there
// (the module declares them)

// material/navigation.cpp: TabsSection, FabMenuSection, ShortNavigationBarSection, WideNavigationRailSection and
// FloatingToolbarsSection are @Composable there (the module declares them)

// places/places.cpp

// material/material.cpp
