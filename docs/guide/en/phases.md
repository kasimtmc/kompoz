# Phases

Each frame goes through three phases: composition decides *what* to show, layout decides *where*, drawing decides *how it looks*. Knowing which phase reads a state tells you how much work a change costs.

## The three phases

1. **Composition** runs your composable functions and turns them into a tree of layout nodes, each with its modifiers.
2. **Layout** measures each node with the constraints its parent gives it and places its children. A node is measured once per pass.
3. **Drawing** draws each node, its modifiers' backgrounds, borders and custom drawing, in order.

Then the frame is presented. A frame in which nothing visible changed is neither drawn nor presented.

The phases normally go in that order, but each can be skipped. When a state changes, Kompoz does only the phases that depend on it:

| Where the state was read | What runs again |
|---|---|
| In a composable's body | Composition of the scopes that read it, then layout and drawing of what changed. |
| While measuring or placing (`Modifier().offset([] { … })`, a `Layout`'s measure policy) | Layout and drawing; no composition. |
| While drawing (`graphicsLayer([] (GraphicsLayerScope&) { … })`, `drawBehind`, a `Canvas`) | Drawing of the nodes that read it; a layout pass looks, but measures nothing. |

## Deferring state reads

The later the phase that reads a changing value, the less work each change costs. This matters for values that change every frame: animations, scroll positions, drags.

Reading an animated value in composition recomposes on every frame:

```cpp
void Pulse() {
    auto pulse = rememberInfiniteTransition().animateFloatState(0.8f, 1.f, infiniteRepeatable(tween(600), RepeatMode::Reverse));
    // read while drawing: only drawn again each frame, never recomposed or measured
    Box(Modifier().size(48).graphicsLayer([pulse](GraphicsLayerScope& l) { l.scaleX = l.scaleY = pulse.get(); })
            .background(colorScheme().primary, CircleShape));
}
```

The lambda form of `graphicsLayer` is evaluated while drawing, so the scale changes without composition or layout. The value form, `graphicsLayer({.alpha = 0.5f})`, is read in composition, which is fine for values that don't change often.

The same goes for moving something. `offset(x, y)` takes values read in composition; `offset([] { … })` takes a lambda read during layout:

```cpp
void DraggableBall() {
    auto x = remember([] { return mutableStateOf(0.f); });
    Box(Modifier().offset([x] { return Offset{x.get(), 0}; })        // read during layout
            .draggable(rememberDraggableState([x](float delta) { x.set(x.get() + delta); }), Orientation::Horizontal)
            .size(56)
            .background(colorScheme().tertiary, CircleShape));
}
```

Dragging the ball changes `x` on every pointer move. Because only the layout phase read it, nothing recomposes: the ball is placed again and drawn.

## Incremental layout

Layout in Kompoz is incremental, as in Compose. A node measured again with the same constraints as last time, with nothing changed in it or below it, keeps its size and its children's positions without measuring them. Only nodes that were composed again, or that read a state that changed while they were measured, are measured again, along with the parents whose size depends on them.

You don't need to do anything for this. It does mean a change deep in a large tree costs little, as long as it doesn't change the size of what is around it.

`KOMPOZ_LAYOUT_CHECK=1` checks each incremental layout against a full one, and logs any difference; the tests always run with it.

## Drawing only what changed

Kompoz records each frame as a list of drawing commands and compares it with the frame before. Only the parts of the screen that changed are drawn again; the rest of the pixels are kept. On Kompoz's GPU renderer a scrolled list isn't drawn again either: its pixels are moved, and only the strip that comes into view is drawn, with what doesn't scroll along (a floating button over it). That works where what is under the list is one color, its own background or the screen's; over a gradient or an image the list is drawn again as it scrolls. You can see this with `KOMPOZ_DAMAGE_SHOW=1`, which outlines what was redrawn. See [Tools](tools.md#profiling-frames).

Nor is a node's drawing run again when nothing it draws from changed, as in Compose: the frame records again what the node and everything in it drew last time, moved as far as it moved (a list scrolled by), so a large screen with a small animation on it costs about as much to record as the animation. A node is drawn again when it is measured again, when a state its drawing read changes, when it asks for a redraw while it draws (what draws by the frame's time: an endless transition's state read in a `Canvas`, a progress indicator) and when it is pressed, hovered or focused or its ripple moves. So drawing code takes what changes from a state, as Compose's does: a plain value changed elsewhere and read while drawing isn't seen until something else draws the node again. `KOMPOZ_KEEP_CHECK=1` checks each frame against one drawing every node; the tests always do.

## Performance tips

### Do expensive work once

Anything a composable computes runs again on each recomposition. Compute it once with `remember`, keyed to its inputs:

```cpp
#! struct Contact { std::string name; friend bool operator==(const Contact&, const Contact&) = default; };
void Contacts(const std::vector<Contact>& contacts) {
    auto sorted = remember(contacts, [&] {   // sorted again only when the contacts change
        std::vector<Contact> s = contacts;
        std::ranges::sort(s, {}, &Contact::name);
        return s;
    });
    LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& list) {
        list.items(sorted, [](const Contact& c) { Text(c.name); });
    });
}
```

Better still, keep the sorted list in state or in a `ViewModel`, so it is sorted when it changes, not when the UI recomposes.

### Use keys in lazy lists

Without keys, a lazy list ties item state to positions, and inserting at the top recomposes every item. With keys, items that moved keep their composition. See [Lists and grids](lists.md).

### Use derivedStateOf for values that change less often than their inputs

A scroll position changes on every frame of a scroll, but "is the list scrolled at all" changes once. Reading the position recomposes on every frame; reading a `derivedStateOf` recomposes once. See [State](state.md#derivedstateof).

### Let skipping work

A scope is skipped only when its arguments compare equal. Pass values that compare cheaply and correctly: structs with `operator==`, states (which compare by identity) rather than copies of big containers. A composable that takes a `std::function` is never skipped.

### Don't write state you just read

Writing, during composition, a state that this composition already read makes Kompoz compose again at once, and doing it on every pass never settles. Update state in callbacks and effects, or compute the value instead of storing it.

## See also

- [Lifecycle and identity](lifecycle.md): scopes and skipping.
- [Custom layouts](custom-layouts.md): measuring and placing yourself.
- [Graphics](graphics.md): drawing and `graphicsLayer`.
- [Tools](tools.md): the profiler and the recomposition highlighter.
