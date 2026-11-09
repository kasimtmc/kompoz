// The Material screen: its sections, one under another (here and in the other files of material/).

#include "showcase.hpp"

@Composable
fun MaterialScreen(modifier: Modifier, colorSource: MutableState<ColorSource>, nav: NavController) {
    val scroll = rememberScrollState()
    Column(modifier = modifier.verticalScroll(scroll).padding(20.dp, 8.dp, 20.dp, 20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        SearchSection()
        TooltipSection()
        DropdownSection()
        DatePickerSection()
        TimePickerSection()
        InteractionSection()
        AnchoredDraggableSection()
        RippleSection()
        SelectableRowsSection()
        SemanticsSection()
        DynamicColorSection(colorSource)
        InsetsSection()
        ImeNestedScrollSection()
        FrameRateSection()
        TextFieldsSection()
        AnimateItemSection()
        ButtonsSection()
        ChipsSection()
        SelectionControlsSection()
        TabsSection()
        ListItemSection()
        DialogsSection(nav)
        FabMenuSection()
        ShortNavigationBarSection()
        WideNavigationRailSection()
        FloatingToolbarsSection()
        CardsSection()
    }
}
