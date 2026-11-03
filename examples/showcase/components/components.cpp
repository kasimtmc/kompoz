// The components screen: every section, one under another.

#include "showcase.hpp"

void ComponentsScreen(Modifier modifier, CallSite site) {
    composable(site, modifier, [=] {
        // A LazyColumn, as Compose has a long list: only the sections in view (and a little past
        // them) are composed, laid out and drawn; one scrolled away leaves the composition.
        LazyColumn(modifier, contentPadding = PaddingValues{20, 8, 20, 20}, verticalArrangement = Arrangement::spacedBy(16)) {
            item("PredictiveBackSection", [] { PredictiveBackSection(); });
            item("ShapesSection", [] { ShapesSection(); });
            item("MaterialShapesSection", [] { MaterialShapesSection(); });
            item("DrawingSection", [] { DrawingSection(); });
            item("PagerSection", [] { PagerSection(); });
            item("HorizontalGridSection", [] { HorizontalGridSection(); });
            item("BlendModeSection", [] { BlendModeSection(); });
            item("TileModeSection", [] { TileModeSection(); });
            item("ColorSpaceSection", [] { ColorSpaceSection(); });
            item("CanvasSection", [] { CanvasSection(); });
            item("BlurSection", [] { BlurSection(); });
            item("PointerPassSection", [] { PointerPassSection(); });
            item("ScrollableSection", [] { ScrollableSection(); });
            item("OverscrollSection", [] { OverscrollSection(); });
            item("BringIntoViewSection", [] { BringIntoViewSection(); });
            item("CustomIndicationSection", [] { CustomIndicationSection(); });
            item("TextToolbarSection", [] { TextToolbarSection(); });
            item("ColorVectorSection", [] { ColorVectorSection(); });
            item("ScrollbarSection", [] { ScrollbarSection(); });
#ifndef __ANDROID__  // a right click and a mouse: desktops (and browsers) only
            item("ContextMenuSection", [] { ContextMenuSection(); });
            item("PointerSection", [] { PointerSection(); });
#endif
            item("DrawersSection", [] { DrawersSection(); });
            item("SharedElementSection", [] { SharedElementSection(); });
            item("SelectableTextSection", [] { SelectableTextSection(); });
            item("NestedScrollSection", [] { NestedScrollSection(); });
            item("PullToRefreshSection", [] { PullToRefreshSection(); });
            item("MultiTouchSection", [] { MultiTouchSection(); });
            item("KeyboardSection", [] { KeyboardSection(); });
            item("TextSection", [] { TextSection(); });
            item("TextMeasureSection", [] { TextMeasureSection(); });
            item("LayoutSection", [] { LayoutSection(); });
            item("FlexBoxSection", [] { FlexBoxSection(); });
            item("GridSection", [] { GridSection(); });
            item("PositionSection", [] { PositionSection(); });
            item("ZIndexSection", [] { ZIndexSection(); });
            item("DragAndDropSection", [] { DragAndDropSection(); });
            item("AnimationsSection", [] { AnimationsSection(); });
            item("FlingSection", [] { FlingSection(); });
            item("ImagesSection", [] { ImagesSection(); });
            item("AsyncImageSection", [] { AsyncImageSection(); });
            item("CarouselSection", [] { CarouselSection(); });
            item("SnapRowSection", [] { SnapRowSection(); });
            item("PlatformServicesSection", [] { PlatformServicesSection(); });
            item("ChatSection", [] { ChatSection(); });
            item("LayoutInfoSection", [] { LayoutInfoSection(); });
            item("ListDetailSection", [] { ListDetailSection(); });
            item("SupportingPaneSection", [] { SupportingPaneSection(); });
            item("ProgressSection", [] { ProgressSection(); });
            item("StateFlowSection", [] { StateFlowSection(); });
            item("FlowSearchSection", [] { FlowSearchSection(); });
            item("ChannelSection", [] { ChannelSection(); });
            item("MovableContentSection", [] { MovableContentSection(); });
            item("LifecycleSection", [] { LifecycleSection(); });
            item("IconButtons", [] { IconButtons(); });
            item("IconGallery", [] { IconGallery(); });
        };
    });
}
