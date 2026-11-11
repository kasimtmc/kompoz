// The components screen: every section, one under another.

#include "showcase.hpp"

@Kompozable
fun ComponentsScreen(modifier: Modifier) {
    // A LazyColumn, as Compose has a long list: only the sections in view (and a little past
    // them) are composed, laid out and drawn; one scrolled away leaves the composition.
    LazyColumn(modifier = modifier, contentPadding = PaddingValues(20.dp, 8.dp, 20.dp, 20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        item(key = "PredictiveBackSection") { PredictiveBackSection() }
        item(key = "ShapesSection") { ShapesSection() }
        item(key = "MaterialShapesSection") { MaterialShapesSection() }
        item(key = "DrawingSection") { DrawingSection() }
        item(key = "PagerSection") { PagerSection() }
        item(key = "HorizontalGridSection") { HorizontalGridSection() }
        item(key = "BlendModeSection") { BlendModeSection() }
        item(key = "TileModeSection") { TileModeSection() }
        item(key = "ColorSpaceSection") { ColorSpaceSection() }
        item(key = "CanvasSection") { CanvasSection() }
        item(key = "BlurSection") { BlurSection() }
        item(key = "PointerPassSection") { PointerPassSection() }
        item(key = "ScrollableSection") { ScrollableSection() }
        item(key = "OverscrollSection") { OverscrollSection() }
        item(key = "BringIntoViewSection") { BringIntoViewSection() }
        item(key = "CustomIndicationSection") { CustomIndicationSection() }
        item(key = "TextToolbarSection") { TextToolbarSection() }
        item(key = "ColorVectorSection") { ColorVectorSection() }
        item(key = "ScrollbarSection") { ScrollbarSection() }
        if (!kOnAndroid) {  // a right click and a mouse: desktops (and browsers) only
            item(key = "ContextMenuSection") { ContextMenuSection() }
            item(key = "PointerSection") { PointerSection() }
        }
        item(key = "DrawersSection") { DrawersSection() }
        item(key = "SharedElementSection") { SharedElementSection() }
        item(key = "SelectableTextSection") { SelectableTextSection() }
        item(key = "NestedScrollSection") { NestedScrollSection() }
        item(key = "PullToRefreshSection") { PullToRefreshSection() }
        item(key = "MultiTouchSection") { MultiTouchSection() }
        item(key = "KeyboardSection") { KeyboardSection() }
        item(key = "TextSection") { TextSection() }
        item(key = "TextMeasureSection") { TextMeasureSection() }
        item(key = "LayoutSection") { LayoutSection() }
        item(key = "FlexBoxSection") { FlexBoxSection() }
        item(key = "GridSection") { GridSection() }
        item(key = "PositionSection") { PositionSection() }
        item(key = "ZIndexSection") { ZIndexSection() }
        item(key = "DragAndDropSection") { DragAndDropSection() }
        item(key = "AnimationsSection") { AnimationsSection() }
        item(key = "FlingSection") { FlingSection() }
        item(key = "ImagesSection") { ImagesSection() }
        item(key = "AsyncImageSection") { AsyncImageSection() }
        item(key = "CarouselSection") { CarouselSection() }
        item(key = "SnapRowSection") { SnapRowSection() }
        item(key = "PlatformServicesSection") { PlatformServicesSection() }
        item(key = "ChatSection") { ChatSection() }
        item(key = "LayoutInfoSection") { LayoutInfoSection() }
        item(key = "ListDetailSection") { ListDetailSection() }
        item(key = "SupportingPaneSection") { SupportingPaneSection() }
        item(key = "ProgressSection") { ProgressSection() }
        item(key = "StateFlowSection") { StateFlowSection() }
        item(key = "FlowSearchSection") { FlowSearchSection() }
        item(key = "ChannelSection") { ChannelSection() }
        item(key = "MovableContentSection") { MovableContentSection() }
        item(key = "LifecycleSection") { LifecycleSection() }
        item(key = "IconButtons") { IconButtons() }
        item(key = "IconGallery") { IconGallery() }
    }
}
