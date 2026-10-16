// The Material screen: its sections, one under another (here and in the other files of material/).

#include "showcase.hpp"

void MaterialScreen(Modifier modifier, MutableState<bool> dynamicColor, NavController nav, CallSite site) {
    composable(site, modifier, dynamicColor, nav, [=] {
        auto scroll = rememberScrollState();
        Column(modifier.verticalScroll(scroll).padding(20, 8, 20, 20), {.verticalArrangement = Arrangement::spacedBy(16)},
               [&] {
                   SearchSection();
                   TooltipSection();
                   DropdownSection();
                   DatePickerSection();
                   TimePickerSection();
                   InteractionSection();
                   AnchoredDraggableSection();
                   RippleSection();
                   SelectableRowsSection();
                   SemanticsSection();
                   DynamicColorSection(dynamicColor);
                   InsetsSection();
                   ImeNestedScrollSection();
                   FrameRateSection();
                   TextFieldsSection();
                   AnimateItemSection();
                   ButtonsSection();
                   ChipsSection();
                   SelectionControlsSection();
                   TabsSection();
                   ListItemSection();
                   DialogsSection(nav);
                   FabMenuSection();
                   ShortNavigationBarSection();
                   WideNavigationRailSection();
                   FloatingToolbarsSection();
                   CardsSection();
               });
    });
}
