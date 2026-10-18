# Accessibility

Making your app usable with screen readers, keyboards and large text. Kompoz builds a semantics tree, as Compose does, that TalkBack reads on Android, screen readers read through AT-SPI on Linux, and tests query everywhere.

## The semantics tree

Alongside the UI tree, Kompoz keeps a *semantics* tree: what each element means to someone who can't see it. Material components fill it in for you: a `Button` is a button with its label, a `Checkbox` says whether it is checked, a `Slider` its value. TalkBack reads this tree on Android; [tests](testing.md) find nodes in it.

You need to add to it where meaning isn't on the screen as text: icons, images, custom controls.

## Describing images and icons

Give every icon and image that carries meaning a content description, and leave decorative ones empty:

```cpp body
IconButton([] {}, [] { Icon(Icons::Filled::Share, "Share"); });     // announced as "Share, button"
Icon(Icons::Filled::Star, "");                                      // decorative: not announced
Image(imageResource("images/chart.png"), "Sales grew 20% in March", Modifier().size(200));
```

An icon inside a button that also has a text label shouldn't repeat it: give the icon an empty description.

## The semantics modifier

`Modifier().semantics({…})` sets what a node means:

| Property | Use |
|---|---|
| `contentDescription` | What it is, when that isn't its text. |
| `role` | `SemanticsRole::Button`, `Checkbox`, `Switch`, `RadioButton`, `Tab`, `Image`, `Slider`. |
| `checked` | On or off, for toggles and selected tabs. |
| `stateDescription` | Its state in words: "Playing", "3 of 5". |
| `heading` | A heading, so screen reader users can jump between sections. |
| `mergeDescendants` | Read with everything inside it as one item. |
| `liveRegion` | Changes are announced: `LiveRegionMode::Polite` or `Assertive`. |
| `customActions` | Actions offered in TalkBack's actions menu. |
| `progressBarRangeInfo`, `setProgress` | A value in a range that services can read and set. |
| `traversalIndex` | The reading order among siblings, lower first. |

```cpp body
Text("Settings", Modifier().semantics({.heading = true}), {.font = typography().headlineSmall});
```

`Modifier().semantics("description")` is a shorthand for a content description alone.

## Merging

A screen reader moves from item to item. A row of an icon and two texts should be one item, read together, not three. Clickables merge their content already; for other groups, set `mergeDescendants`:

```cpp body
Row(Modifier().semantics({.mergeDescendants = true}).padding(16), [&] {
    Icon(Icons::Filled::Person, "");
    Column([&] {
        Text("Ada Lovelace");
        Text("Online");
    });
});   // read as "Ada Lovelace, Online"
```

`Modifier().clearAndSetSemantics({…})` replaces what is inside with what you set: a rating drawn as five stars becomes "Rating: 4 of 5". `Modifier().hideFromAccessibility()` hides a node and what is inside it.

```cpp body
Row(Modifier().clearAndSetSemantics({.contentDescription = "Rating: 4 of 5"}), [&] {
    for (int i = 0; i < 5; ++i) Icon(i < 4 ? Icons::Filled::Star : Icons::Filled::FavoriteBorder, "");
});
```

## Custom controls

A control you draw yourself must say what it is and how to use it. Prefer the modifiers that do this for you: `clickable`, `toggleable(value, onChange, role)`, `selectable(selected, onClick, role)` and `triStateToggleable` set the role, the state and the action:

```cpp
void StarToggle() {
    auto starred = remember([] { return mutableStateOf(false); });
    Icon(starred.get() ? Icons::Filled::Star : Icons::Filled::FavoriteBorder, "Starred",
         Modifier().toggleable(starred.get(), [starred](bool v) { starred.set(v); }, SemanticsRole::Switch).padding(12));
}
```

For a row that acts as one control, put the modifier on the row and pass an empty callback to the control inside it, so the whole row is one touch target and one item:

```cpp
void WifiRow() {
    auto on = remember([] { return mutableStateOf(true); });
    Row(Modifier().fillMaxWidth().toggleable(on.get(), [on](bool v) { on.set(v); }, SemanticsRole::Switch).padding(16),
        {.verticalAlignment = Alignment::CenterVertically}, [&] {
            Text("Wi-Fi", Modifier().weight(1));
            Switch(on.get(), {});
        });
}
```

A group of radio-button rows goes inside `Modifier().selectableGroup()`, so services know they belong together.

### Actions that need a gesture

Swipe-to-delete, long press menus and drag handles are hard to use with a screen reader. Offer the same actions as custom actions:

```cpp body
#! std::function<void()> archive, deleteItem;
Box(Modifier().semantics({.customActions = {{"Archive", [archive] { archive(); return true; }},
                                            {"Delete", [deleteItem] { deleteItem(); return true; }}}}));
```

### Values in a range

A custom slider reports its value and lets services set it:

```cpp body
#! auto level = mutableStateOf(0.5f);
Box(Modifier().semantics({.contentDescription = "Brightness", .role = SemanticsRole::Slider,
                          .progressBarRangeInfo = ProgressBarRangeInfo{level.get(), 0, 1},
                          .setProgress = [level](float v) { level.set(v); return true; }}));
```

## Announcing changes

A message that appears without moving focus, such as "Saved" or an error under a field, should be announced. Mark it as a live region:

```cpp body
#! bool saved = true;
if (saved) Text("Saved", Modifier().semantics({.liveRegion = LiveRegionMode::Polite}));
```

## Touch targets

Material components keep a touch target of at least 48 dp even when they look smaller, as Compose's do (`LocalMinimumInteractiveComponentSize`). For your own small controls, add `Modifier().minimumInteractiveComponentSize()`.

## Text size and contrast

Text sizes are in sp and follow the system's font size, also when it changes while the app runs. Let layouts grow with it: prefer a minimum height (`heightIn(56)`) over a fixed one for things with text. Material's color schemes give enough contrast between each `on…` color and its container; keep to those pairs.

## Keyboard

Every clickable and text field can be reached with Tab and used with Enter or Space, and the arrow keys move between focusable items. Keep custom controls focusable (`clickable` and `toggleable` are) and make sure focus order follows reading order. See [Gestures](gestures.md#focus).

## Platforms

On Android, TalkBack reads the semantics tree: labels, roles, states, actions, custom actions and live regions.

On Linux, screen readers such as Orca read it through AT-SPI. The app joins the accessibility bus when the desktop's accessibility is on, as Qt apps do (also when it is turned on while the app runs); `KOMPOZ_ATSPI=1` connects it anyway and `KOMPOZ_ATSPI=0` never. Screen readers get the same labels, roles, states, values and text; they can click, focus, scroll, set a slider's value or a text field's text and run custom actions; and they are told what changes: focus, checked and selected states, typed text, values, and live regions as announcements. A password field's text is read as dots.

Windows' UI Automation and macOS's NSAccessibility are not connected yet, so screen readers there don't see Kompoz apps.

## See also

- [Testing](testing.md): finding nodes by their semantics.
- [Material components](material.md): what they announce on their own.
- [Gestures](gestures.md): focus and keyboard input.
