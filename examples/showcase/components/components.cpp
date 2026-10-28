// The components screen: every section, one under another.

#include "showcase.hpp"

void ComponentsScreen(Modifier modifier, CallSite site) {
    composable(site, modifier, [=] {
        // A LazyColumn, as Compose has a long list: only the sections in view (and a little past
        // them) are composed, laid out and drawn; one scrolled away leaves the composition.
        LazyColumn(modifier, {.contentPadding = {20, 8, 20, 20}, .spacing = 16}, [&](LazyListScope& list) {
            list.item("PredictiveBackSection", [] { PredictiveBackSection(); });
            list.item("ShapesSection", [] { ShapesSection(); });
            list.item("MaterialShapesSection", [] { MaterialShapesSection(); });
            list.item("DrawingSection", [] { DrawingSection(); });
            list.item("PagerSection", [] { PagerSection(); });
            list.item("HorizontalGridSection", [] { HorizontalGridSection(); });
            list.item("BlendModeSection", [] { BlendModeSection(); });
            list.item("TileModeSection", [] { TileModeSection(); });
            list.item("ColorSpaceSection", [] { ColorSpaceSection(); });
            list.item("CanvasSection", [] { CanvasSection(); });
            list.item("BlurSection", [] { BlurSection(); });
            list.item("PointerPassSection", [] { PointerPassSection(); });
            list.item("ScrollableSection", [] { ScrollableSection(); });
            list.item("OverscrollSection", [] { OverscrollSection(); });
            list.item("BringIntoViewSection", [] { BringIntoViewSection(); });
            list.item("CustomIndicationSection", [] { CustomIndicationSection(); });
            list.item("TextToolbarSection", [] { TextToolbarSection(); });
            list.item("ColorVectorSection", [] { ColorVectorSection(); });
            list.item("ScrollbarSection", [] { ScrollbarSection(); });
#ifndef __ANDROID__  // a right click and a mouse: desktops (and browsers) only
            list.item("ContextMenuSection", [] { ContextMenuSection(); });
            list.item("PointerSection", [] { PointerSection(); });
#endif
            list.item("DrawersSection", [] { DrawersSection(); });
            list.item("SharedElementSection", [] { SharedElementSection(); });
            list.item("SelectableTextSection", [] { SelectableTextSection(); });
            list.item("NestedScrollSection", [] { NestedScrollSection(); });
            list.item("PullToRefreshSection", [] { PullToRefreshSection(); });
            list.item("MultiTouchSection", [] { MultiTouchSection(); });
            list.item("KeyboardSection", [] { KeyboardSection(); });
            list.item("TextSection", [] { TextSection(); });
            list.item("TextMeasureSection", [] { TextMeasureSection(); });
            list.item("LayoutSection", [] { LayoutSection(); });
            list.item("FlexBoxSection", [] { FlexBoxSection(); });
            list.item("GridSection", [] { GridSection(); });
            list.item("PositionSection", [] { PositionSection(); });
            list.item("ZIndexSection", [] { ZIndexSection(); });
            list.item("DragAndDropSection", [] { DragAndDropSection(); });
            list.item("AnimationsSection", [] { AnimationsSection(); });
            list.item("FlingSection", [] { FlingSection(); });
            list.item("ImagesSection", [] { ImagesSection(); });
            list.item("AsyncImageSection", [] { AsyncImageSection(); });
            list.item("CarouselSection", [] { CarouselSection(); });
            list.item("SnapRowSection", [] { SnapRowSection(); });
            list.item("PlatformServicesSection", [] { PlatformServicesSection(); });
            list.item("ChatSection", [] { ChatSection(); });
            list.item("LayoutInfoSection", [] { LayoutInfoSection(); });
            list.item("ListDetailSection", [] { ListDetailSection(); });
            list.item("SupportingPaneSection", [] { SupportingPaneSection(); });
            list.item("ProgressSection", [] { ProgressSection(); });
            list.item("StateFlowSection", [] { StateFlowSection(); });
            list.item("FlowSearchSection", [] { FlowSearchSection(); });
            list.item("ChannelSection", [] { ChannelSection(); });
            list.item("MovableContentSection", [] { MovableContentSection(); });
            list.item("LifecycleSection", [] { LifecycleSection(); });
            list.item("IconButtons", [] { IconButtons(); });
            list.item("IconGallery", [] { IconGallery(); });
        });
    });
}
