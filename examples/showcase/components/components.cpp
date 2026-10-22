// The components screen: every section, one under another.

#include "showcase.hpp"

void ComponentsScreen(Modifier modifier, CallSite site) {
    composable(site, modifier, [=] {
        auto scroll = rememberScrollState();
        Column(modifier.verticalScroll(scroll).padding(20, 8, 20, 20), {.verticalArrangement = Arrangement::spacedBy(16)},
               [&] {
                   PredictiveBackSection();
                   ShapesSection();
                   MaterialShapesSection();
                   DrawingSection();
                   PagerSection();
                   HorizontalGridSection();
                   BlendModeSection();
                   TileModeSection();
                   ColorSpaceSection();
                   CanvasSection();
                   BlurSection();
                   PointerPassSection();
                   ScrollableSection();
                   OverscrollSection();
                   BringIntoViewSection();
                   CustomIndicationSection();
                   TextToolbarSection();
                   ColorVectorSection();
                   ScrollbarSection();
#ifndef __ANDROID__  // a right click and a mouse: desktops (and browsers) only
                   ContextMenuSection();
                   PointerSection();
#endif
                   DrawersSection();
                   SharedElementSection();
                   SelectableTextSection();
                   NestedScrollSection();
                   PullToRefreshSection();
                   MultiTouchSection();
                   KeyboardSection();
                   TextSection();
                   TextMeasureSection();
                   LayoutSection();
                   FlexBoxSection();
                   GridSection();
                   PositionSection();
                   ZIndexSection();
                   DragAndDropSection();
                   AnimationsSection();
                   FlingSection();
                   ImagesSection();
                   AsyncImageSection();
                   CarouselSection();
                   SnapRowSection();
                   PlatformServicesSection();
                   ChatSection();
                   LayoutInfoSection();
                   ListDetailSection();
                   SupportingPaneSection();
                   ProgressSection();
                   StateFlowSection();
                   FlowSearchSection();
                   ChannelSection();
                   MovableContentSection();
                   LifecycleSection();
                   IconButtons();
                   IconGallery();
               });
    });
}
