#pragma once

// The showcase: a folder per screen (home, list, components, material, places), each screen's
// sections in files of their own, so that an edit rebuilds only its file. What the parts share.

#include <kompoz/kompoz.hpp>

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

// common.cpp
ColorScheme animateColorScheme(const ColorScheme& t, CallSite site = CallSite::current());
Color hueColor(int i);
void Section(const char* title, const Content& content, CallSite site = CallSite::current());
void DesktopNote(const char* text, CallSite site = CallSite::current());

// home/home.cpp
void ScrollToTopButton(LazyListState state, CallSite site = CallSite::current());
void HomeScreen(Modifier modifier, CallSite site = CallSite::current());

// list/list.cpp
void WindowClassLine(CallSite site = CallSite::current());
void MediaLine(CallSite site = CallSite::current());
void ListScreen(Modifier modifier, LazyListState state, NavController nav, std::shared_ptr<LikesViewModel> likes,
                CallSite site = CallSite::current());
void ItemScreen(int index, NavController nav, std::shared_ptr<LikesViewModel> vm, CallSite site = CallSite::current());

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
void ComponentsScreen(Modifier modifier, CallSite site = CallSite::current());

// material/sections.cpp
void SwipeableTask(int id, SnapshotStateList<int> tasks, CallSite site = CallSite::current());
void SearchSection(CallSite site = CallSite::current());
void TooltipSection(CallSite site = CallSite::current());
void DropdownSection(CallSite site = CallSite::current());
void InteractionSection(CallSite site = CallSite::current());
void AnchoredDraggableSection(CallSite site = CallSite::current());
void RippleSection(CallSite site = CallSite::current());
void SemanticsSection(CallSite site = CallSite::current());
void FocusPropertiesDemo(CallSite site = CallSite::current());
void SelectableRowsSection(CallSite site = CallSite::current());
void DatePickerSection(CallSite site = CallSite::current());
void TimePickerSection(CallSite site = CallSite::current());
// Where the theme's colors come from: the app's own scheme, the system's accent color, or the
// wallpaper. The library offers all three; the app (here its user) picks.
enum class ColorSource { App, System, Wallpaper };
void DynamicColorSection(MutableState<ColorSource> source, CallSite site = CallSite::current());
void ImeNestedScrollSection(CallSite site = CallSite::current());
void FrameRateSection(CallSite site = CallSite::current());
void InsetsSection(CallSite site = CallSite::current());

// material/text_fields.cpp
void TextFieldsSection(CallSite site = CallSite::current());

// material/controls.cpp
void ButtonsSection(CallSite site = CallSite::current());
void ChipsSection(CallSite site = CallSite::current());
void SelectionControlsSection(CallSite site = CallSite::current());

// material/containers.cpp
void AnimateItemSection(CallSite site = CallSite::current());
void ListItemSection(CallSite site = CallSite::current());
void DialogsSection(NavController nav, CallSite site = CallSite::current());
void CardsSection(CallSite site = CallSite::current());

// material/navigation.cpp
void TabsSection(CallSite site = CallSite::current());
void FabMenuSection(CallSite site = CallSite::current());
void ShortNavigationBarSection(CallSite site = CallSite::current());
void WideNavigationRailSection(CallSite site = CallSite::current());
void FloatingToolbarsSection(CallSite site = CallSite::current());

// places/places.cpp
void PlacesScreen(Modifier modifier, CallSite site = CallSite::current());

// material/material.cpp
void MaterialScreen(Modifier modifier, MutableState<ColorSource> colorSource, NavController nav, CallSite site = CallSite::current());
