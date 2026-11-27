#include <kompoz/animation.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/icons.hpp>
#include <kompoz/material.hpp>
#include <kompoz/strings.hpp>
#include <kompoz/tree.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace kompoz {

// ---- Building ---------------------------------------------------------------------------------

detail::TreeBuilder*& detail::treeBuilder() {
    thread_local TreeBuilder* current = nullptr;
    return current;
}

void detail::outsideTree(const char* function) {
    std::fprintf(stderr, "kompoz: %s() outside buildTree's block (buildTree { %s(…) })\n", function, function);
    std::abort();
}

void detail::addTreeElement(std::any data, size_t key, bool node, std::function<void()> generator) {
    TreeBuilder* b = treeBuilder();
    if (!b) outsideTree(node ? "addNode" : "addLeaf");
    std::shared_ptr<TreeElementBase> e = b->make(std::move(data));
    e->key = key;
    e->depth = b->depth;
    e->parentElement = b->parent;
    e->node = node;
    e->generator = std::move(generator);
    e->make = b->make;
    e->made.clear();
    b->into->push_back(std::move(e));
}

const std::vector<std::shared_ptr<detail::TreeElementBase>>& detail::TreeElementBase::childElements() {
    if (node && !childrenMade) {
        childrenMade = true;  // (first: a block that asks again doesn't run twice)
        TreeBuilder* parentBuilder = treeBuilder();
        TreeBuilder builder;
        // The children are of the tree's type: made as this one's builder made it.
        builder.make = make;
        builder.into = &made;
        builder.parent = weak_from_this();
        builder.depth = depth + 1;
        treeBuilder() = &builder;
        if (generator) generator();
        treeBuilder() = parentBuilder;
    }
    return made;
}

// ---- TreeState --------------------------------------------------------------------------------

struct TreeState::Data {
    MutableState<std::set<size_t>> open{std::set<size_t>{}};
    MutableState<std::set<size_t>> selected{std::set<size_t>{}};
    LazyListState list;
    size_t lead = 0, anchor = 0;  // the selection's moving end (the keys move it) and its fixed one (Shift)
    bool hasLead = false;
    int64_t lastClick = -1;       // a click's time and element: a second one soon on it is a double click
    size_t lastClicked = 0;
};

TreeState::TreeState() : data_(std::make_shared<Data>()) {}
const std::set<size_t>& TreeState::openNodes() const { return data_->open.get(); }
const std::set<size_t>& TreeState::selectedKeys() const { return data_->selected.get(); }
const LazyListState& TreeState::lazyListState() const { return data_->list; }
void TreeState::toggleKey(size_t key) const {
    std::set<size_t> open = data_->open.peek();
    if (!open.erase(key)) open.insert(key);
    data_->open.set(std::move(open));
}
void TreeState::openKeys(const std::set<size_t>& keys) const {
    std::set<size_t> open = data_->open.peek();
    open.insert(keys.begin(), keys.end());
    if (open != data_->open.peek()) data_->open.set(std::move(open));
}
void TreeState::closeKey(size_t key) const {
    std::set<size_t> open = data_->open.peek();
    if (open.erase(key)) data_->open.set(std::move(open));
}
void TreeState::setSelected(std::set<size_t> keys) const {
    if (keys != data_->selected.peek()) data_->selected.set(std::move(keys));
}
void TreeState::setOpen(std::set<size_t> keys) const {
    if (keys != data_->open.peek()) data_->open.set(std::move(keys));
}

SavedValue Saver<TreeState>::save(const TreeState& s) {
    auto keys = [](const std::set<size_t>& set) {
        SavedValue::List out;
        for (size_t k : set) out.push_back(int64_t(k));
        return SavedValue(std::move(out));
    };
    return SavedValue::List{keys(s.openNodes()), keys(s.selectedKeys()), Saver<LazyListState>::save(s.lazyListState())};
}
std::optional<TreeState> Saver<TreeState>::restore(const SavedValue& v) {
    if (!v.list() || v.list()->size() != 3) return std::nullopt;
    auto keys = [](const SavedValue& list) -> std::optional<std::set<size_t>> {
        if (!list.list()) return std::nullopt;
        std::set<size_t> out;
        for (const SavedValue& k : *list.list())
            if (auto n = k.integer()) out.insert(size_t(*n));
            else return std::nullopt;
        return out;
    };
    auto open = keys((*v.list())[0]), selected = keys((*v.list())[1]);
    auto list = Saver<LazyListState>::restore((*v.list())[2]);
    if (!open || !selected || !list) return std::nullopt;
    TreeState s;
    s.data_->open.set(*open);
    s.data_->selected.set(*selected);
    s.data_->list = *list;
    return s;
}

TreeState rememberTreeState(CallSite site) {
    return rememberSaveable([] { return TreeState(); }, site);
}

// ---- LazyTree ---------------------------------------------------------------------------------

namespace {

using Element = std::shared_ptr<detail::TreeElementBase>;

// The rows: the roots, and under each open node its children, depth first.
void flatten(const std::vector<Element>& elements, const std::set<size_t>& open, std::vector<Element>& out) {
    for (const Element& e : elements) {
        out.push_back(e);
        if (e->node && open.count(e->key)) flatten(e->childElements(), open, out);
    }
}

int indexOf(const std::vector<Element>& rows, size_t key) {
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i]->key == key) return int(i);
    return -1;
}

// What a tree's handlers need, current as of its last composition.
struct TreeLatest {
    std::vector<Element> rows;
    detail::LazyTreeOptions options;
    TreeState state;
    FocusRequester focus;
};

// The arrow before a row's content (a node's; a leaf's room is left empty).
constexpr float ArrowWidth = 24;

// Selects `key` as a click with these keys held does, in `mode`; told to onSelectionChange.
void select(TreeLatest& t, size_t key, bool ctrl, bool shift) {
    auto& d = *t.state.data_;
    SelectionMode mode = t.options.selectionMode;
    if (mode == SelectionMode::None) return;
    std::set<size_t> selected;
    if (mode == SelectionMode::Multiple && shift && d.hasLead) {  // the range from the anchor
        int a = indexOf(t.rows, d.anchor), b = indexOf(t.rows, key);
        if (a < 0) a = b;
        if (ctrl) selected = d.selected.peek();
        for (int i = std::min(a, b); i <= std::max(a, b) && i >= 0; ++i) selected.insert(t.rows[size_t(i)]->key);
    } else if (mode == SelectionMode::Multiple && ctrl) {  // one more, or one fewer
        selected = d.selected.peek();
        if (!selected.erase(key)) selected.insert(key);
        d.anchor = key;
    } else {
        selected = {key};
        d.anchor = key;
    }
    d.lead = key;
    d.hasLead = true;
    if (selected == d.selected.peek()) return;
    t.state.setSelected(selected);
    if (t.options.onSelectionChange) {
        std::vector<Element> chosen;
        for (const Element& e : t.rows)
            if (selected.count(e->key)) chosen.push_back(e);
        t.options.onSelectionChange(chosen);
    }
}

// Brings row `index` into view.
void reveal(const TreeState& state, int index) {
    const LazyListState& list = state.lazyListState();
    LazyListLayoutInfo info = list.layoutInfo();
    for (const LazyListItemInfo& item : info.visibleItemsInfo)
        if (item.index == index) {
            if (item.offset < info.viewportStartOffset) list.scrollBy(item.offset - info.viewportStartOffset);
            else if (item.offset + item.size > info.viewportEndOffset - info.afterContentPadding)
                list.scrollBy(item.offset + item.size - (info.viewportEndOffset - info.afterContentPadding));
            return;
        }
    list.scrollToItem(index);
}

// The tree's keys (it is focused as one): moving the selection, opening and closing.
bool treeKey(const std::shared_ptr<TreeLatest>& latest, const KeyEvent& e) {
    if (e.type != KeyEventType::KeyDown) return false;
    TreeLatest& t = *latest;
    auto& d = *t.state.data_;
    if (t.rows.empty()) return false;
    int at = d.hasLead ? indexOf(t.rows, d.lead) : -1;
    auto go = [&](int index) {
        index = std::clamp(index, 0, int(t.rows.size()) - 1);
        bool extend = e.isShiftPressed() && t.options.selectionMode == SelectionMode::Multiple;
        size_t anchor = d.anchor;
        select(t, t.rows[size_t(index)]->key, false, extend);
        if (extend) d.anchor = anchor;
        reveal(t.state, index);
        return true;
    };
    if (at < 0) {  // nothing chosen yet: the first row
        if (e.key == Key::Up || e.key == Key::Down || e.key == Key::Home || e.key == Key::End) return go(e.key == Key::End ? int(t.rows.size()) - 1 : 0);
        return false;
    }
    const Element& lead = t.rows[size_t(at)];
    bool open = lead->node && t.state.openNodes().count(lead->key);
    switch (e.key) {
    case Key::Up: return go(at - 1);
    case Key::Down: return go(at + 1);
    case Key::Home: return go(0);
    case Key::End: return go(int(t.rows.size()) - 1);
    case Key::Right:  // (the way into the tree: the left in a right-to-left one is said by the arrows the user sees)
        if (lead->node && !open) t.state.toggleKey(lead->key);
        else if (open && at + 1 < int(t.rows.size()) && t.rows[size_t(at + 1)]->depth > lead->depth) return go(at + 1);
        return true;
    case Key::Left:
        if (open) t.state.toggleKey(lead->key);
        else if (auto parent = lead->parentElement.lock()) return go(indexOf(t.rows, parent->key));
        return true;
    case Key::Enter:
        if (lead->node) t.state.toggleKey(lead->key);
        if (t.options.onElementDoubleClick) t.options.onElementDoubleClick(*lead);
        return true;
    case Key::A:
        if (!e.isCtrlPressed() || t.options.selectionMode != SelectionMode::Multiple) return false;
        {
            std::set<size_t> all;
            for (const Element& r : t.rows) all.insert(r->key);
            if (all != d.selected.peek()) {
                t.state.setSelected(all);
                if (t.options.onSelectionChange) t.options.onSelectionChange(t.rows);
            }
        }
        return true;
    default: return false;
    }
}

} // namespace

void detail::lazyTree(const std::vector<std::shared_ptr<TreeElementBase>>& roots, const Modifier& modifier, const LazyTreeOptions& options,
                      const std::function<void(const TreeElementBase&)>& content, CallSite site) {
    KompozableScope scope(site);
    TreeState own = rememberTreeState();
    TreeState state = options.state ? *options.state : own;
    auto latest = remember([] { return std::make_shared<TreeLatest>(); });
    FocusRequester focus = remember([] { return FocusRequester(); });
    std::vector<Element> rows;
    flatten(roots, state.openNodes(), rows);
    *latest = {rows, options, state, focus};
    bool rtl = LocalLayoutDirection.current() == LayoutDirection::Rtl;
    auto focused = remember([] { return mutableStateOf(false); });
    const Strings& strings = LocalStrings.current();
    const ColorScheme& cs = colorScheme();
    auto view = std::make_shared<std::vector<Element>>(rows);  // (the items' list, kept for when they are composed)

    // Each row: the arrow (a node's) and the content, indented by depth; a click selects it, a
    // second one soon after on it is a double click (and opens or closes a node).
    auto row = [latest, state, content, strings, cs, focused, rtl](const Element& e) {
        auto& d = *state.data_;
        bool selected = d.selected.get().count(e->key) > 0;
        bool open = e->node && state.openNodes().count(e->key);
        bool lead = focused.get() && d.hasLead && d.lead == e->key;
        auto turn = animateFloatAsState(open ? 90.f : 0.f, tween(150));
        // What the last press on the row held, when and where (a click there, on the arrow, opens or closes it).
        struct Press {
            KeyModifiers keys;
            int64_t at = -1;
            float x = 0, width = 0;
        };
        auto press = remember([] { return std::make_shared<Press>(); });
        Element element = e;
        std::vector<CustomAccessibilityAction> actions;
        if (e->node)
            actions.push_back({open ? strings.collapse : strings.expand, [state, key = e->key] {
                                   state.toggleKey(key);
                                   return true;
                               }});
        SemanticsProperties semantics{.stateDescription = e->node ? (open ? strings.expanded : strings.collapsed) : std::string()};
        semantics.checked = latest->options.selectionMode == SelectionMode::None ? std::optional<bool>() : std::optional<bool>(selected);
        semantics.customActions = std::move(actions);
        Color background = selected ? (focused.get() ? cs.secondaryContainer : cs.surfaceContainerHighest) : Color::Transparent;
        Modifier m = Modifier()
                         .animateItem()
                         .fillMaxWidth()
                         .heightIn(32)
                         .background(background, RoundedCornerShape(8))
                         .pointerInput(0,
                                       [press](PointerInputScope& scope) -> Task {
                                           while (true) {  // (only noting what each press held, for the click)
                                               PointerEvent ev = co_await scope.awaitPointerEvent();
                                               if (ev.type == PointerEventType::Press)
                                                   *press = {ev.keyboardModifiers, ev.change.uptimeMillis, ev.change.position.x, scope.size().width};
                                           }
                                       })
                         .focusProperties({.canFocus = false})
                         .clickable([latest, element, press, rtl] {
                             TreeLatest& t = *latest;
                             auto& d = *t.state.data_;
                             Press p = std::exchange(*press, Press{});
                             KeyModifiers keys = p.keys;
                             int64_t at = p.at;
                             t.focus.requestFocus();
                             // On a node's arrow (the row's one target, so that the arrow, small, takes
                             // no touches from the name beside it): opens or closes it.
                             float arrowStart = t.options.indentSize * float(element->depth), arrowEnd = arrowStart + ArrowWidth;
                             float x = rtl ? p.width - p.x : p.x;
                             if (element->node && at >= 0 && x >= arrowStart && x < arrowEnd) {
                                 t.state.toggleKey(element->key);
                                 return;
                             }
                             bool twice = at >= 0 && d.lastClick >= 0 && d.lastClicked == element->key && at - d.lastClick <= 300;
                             d.lastClick = twice ? -1 : at;
                             d.lastClicked = element->key;
                             select(t, element->key, keys.ctrl, keys.shift);
                             if (t.options.onElementClick) t.options.onElementClick(*element);
                             if (twice) {
                                 if (element->node) t.state.toggleKey(element->key);
                                 if (t.options.onElementDoubleClick) t.options.onElementDoubleClick(*element);
                             }
                         })
                         .semantics(semantics);
        if (lead) m = m.border(1, cs.primary, RoundedCornerShape(8));
        m = m.padding(latest->options.indentSize * float(e->depth), 0, 4, 0);
        detail::Row(m, RowStyle{.verticalAlignment = Alignment::CenterVertically}, [=] {
            Modifier arrow = Modifier().size(ArrowWidth);
            if (e->node) {
                // The arrow opens and closes it (pointing the way the rows go: mirrored right to left).
                detail::Box(arrow, Alignment::Center, [=] {
                                detail::Icon(Icons::AutoMirrored::Filled::KeyboardArrowRight, std::string(),
                                             Modifier().size(20).graphicsLayer([turn, rtl](GraphicsLayerScope& l) {
                                                 l.rotationZ = rtl ? -turn.value() : turn.value();  // (while drawing: not composed again)
                                             }),
                                             cs.onSurfaceVariant);
                            });
            } else {
                detail::Box(arrow, Content{});
            }
            content(*element);
        });
    };

    auto keys = [latest](const KeyEvent& e) { return treeKey(latest, e); };
    Modifier m = modifier.focusRequester(focus)
                     .onFocusChanged([focused](FocusState f) {
                         if (focused.peek() != f.isFocused) focused.set(f.isFocused);
                     })
                     .onKeyEvent(keys)
                     .focusable();
    kompoz::LazyColumn(m, kompoz::state = state.lazyListState(), [view, row] {
        kompoz::items(*view, [](const Element& e) { return e->key; }, [row](const Element& e) { row(e); });
    });
}

} // namespace kompoz
