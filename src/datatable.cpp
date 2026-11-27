#include <kompoz/datatable.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/icons.hpp>
#include <kompoz/media_query.hpp>
#include <kompoz/strings.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cmath>

namespace kompoz {

// ---- DataTableState ---------------------------------------------------------------------------

struct DataTableState::Data {
    LazyListState list;
    ScrollState horizontal{0.f};
    MutableState<std::vector<std::optional<float>>> userWidths{std::vector<std::optional<float>>{}};
    // From the last layout: each column's width and start (from the table's start), all of them.
    std::vector<float> widths, starts;
    float total = 0;
    // Each column's widest cell seen yet (its padding in), the header's apart: Wrap columns are
    // as wide, Flex ones no narrower than their header.
    std::vector<float> wrap, header;
    MutableState<int> grew{0};      // a cell wider than its Wrap column was: laid out again
    MutableState<int> widened{0};   // the columns' widths changed: the rows laid out again
};

DataTableState::DataTableState() : data_(std::make_shared<Data>()) {}
const LazyListState& DataTableState::listState() const { return data_->list; }
const ScrollState& DataTableState::horizontalScroll() const { return data_->horizontal; }
std::optional<float> DataTableState::columnWidth(int index) const {
    const auto& w = data_->userWidths.get();
    return index >= 0 && size_t(index) < w.size() ? w[size_t(index)] : std::nullopt;
}
void DataTableState::setColumnWidth(int index, float width) const {
    if (index < 0) return;
    auto w = data_->userWidths.peek();
    if (w.size() <= size_t(index)) w.resize(size_t(index) + 1);
    w[size_t(index)] = std::max(DataTableDefaults::MinColumnWidth, width);
    data_->userWidths.set(std::move(w));
}
void DataTableState::resetColumnWidths() const { data_->userWidths.set({}); }

SavedValue Saver<DataTableState>::save(const DataTableState& s) {
    SavedValue::List widths;
    for (const auto& w : s.data_->userWidths.peek()) widths.push_back(w ? double(*w) : -1.0);
    return SavedValue::List{Saver<LazyListState>::save(s.listState()), double(s.horizontalScroll().value()), SavedValue(std::move(widths))};
}
std::optional<DataTableState> Saver<DataTableState>::restore(const SavedValue& v) {
    if (!v.list() || v.list()->size() != 3) return std::nullopt;
    auto list = Saver<LazyListState>::restore((*v.list())[0]);
    auto across = (*v.list())[1].number();
    const SavedValue& widths = (*v.list())[2];
    if (!list || !across || !widths.list()) return std::nullopt;
    DataTableState s;
    s.data_->list = *list;
    s.data_->horizontal = ScrollState(float(*across));
    std::vector<std::optional<float>> w;
    for (const SavedValue& x : *widths.list()) {
        auto n = x.number();
        if (!n) return std::nullopt;
        w.push_back(*n >= 0 ? std::optional<float>(float(*n)) : std::nullopt);
    }
    s.data_->userWidths.set(std::move(w));
    return s;
}

DataTableState rememberDataTableState(CallSite site) {
    return rememberSaveable([] { return DataTableState(); }, site);
}

// ---- The table --------------------------------------------------------------------------------

namespace {

// What a table's rows and cells need of it.
struct TableContext {
    DataTableState state;
    std::vector<DataColumnSpec> columns;
    detail::DataTableOptions options;
    bool rtl = false;
};
const KompozitionLocal<std::shared_ptr<TableContext>> LocalDataTable{nullptr};

// Marks a cell's node with the alignment it was given (its column's otherwise).
struct CellElement final : ModifierElement {
    std::optional<HorizontalAlignment> alignment;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const CellElement*>(&other);
        return o && o->alignment.has_value() == alignment.has_value() && (!alignment || o->alignment->bias == alignment->bias);
    }
};
// Marks the backdrop of a row's frozen cells.
struct BackdropElement final : ModifierElement {
    bool equals(const ModifierElement& other) const override { return dynamic_cast<const BackdropElement*>(&other) != nullptr; }
};

// The widths the columns take in `width` dp.
void layOutColumns(DataTableState::Data& d, const std::vector<DataColumnSpec>& columns, float width) {
    size_t n = columns.size();
    d.wrap.resize(n, 0.f);
    d.header.resize(n, 0.f);
    const auto& user = d.userWidths.get();
    std::vector<float> w(n, 0.f);
    float taken = 0, weights = 0;
    for (size_t i = 0; i < n; ++i) {
        const TableColumnWidth& c = columns[i].width;
        if (i < user.size() && user[i]) w[i] = *user[i];
        else if (c.kind == TableColumnWidth::Kind::Fixed) w[i] = c.value;
        else if (c.kind == TableColumnWidth::Kind::Fraction) w[i] = width * c.value;
        else if (c.kind == TableColumnWidth::Kind::Wrap) w[i] = std::max(d.wrap[i], d.header[i]);
        else {
            weights += c.value;
            continue;
        }
        taken += w[i];
    }
    float left = std::max(0.f, width - taken);
    for (size_t i = 0; i < n; ++i)
        if (columns[i].width.kind == TableColumnWidth::Kind::Flex && !(i < user.size() && user[i]))
            w[i] = std::max({weights > 0 ? left * columns[i].width.value / weights : 0.f, d.header[i], d.wrap[i], DataTableDefaults::MinColumnWidth});
    std::vector<float> starts(n, 0.f);
    float x = 0;
    for (size_t i = 0; i < n; ++i) starts[i] = x, x += w[i];
    if (w != d.widths || starts != d.starts) {
        d.widths = std::move(w);
        d.starts = std::move(starts);
        d.widened.set(d.widened.peek() + 1);  // (the rows read it: measured again)
    }
    d.total = x;
}

// A row's (or the header's) cells under their columns, its frozen ones (and their backdrop) where
// the scroll leaves them; `header`: its children after the cells are the columns' resize handles.
MeasurePolicy rowPolicy(std::shared_ptr<TableContext> table, float minHeight, bool header) {
    return [table, minHeight, header](LayoutNode& node, const Constraints& c) {
        DataTableState::Data& d = *table->state.data_;
        d.widened.get();  // (laid out again when the widths change)
        size_t n = table->columns.size();
        int frozen = std::clamp(table->options.frozenColumns, 0, int(n));
        float scroll = frozen > 0 ? d.horizontal.value() : 0.f;  // (read here: only laid out again as it scrolls)
        bool probe = detail::isProbeMeasure();
        std::vector<LayoutNode*> cells, handles;
        LayoutNode* backdrop = nullptr;
        for (LayoutNode* child : node.children) {
            if (child->modifier.find<BackdropElement>()) backdrop = child;
            else if (header && cells.size() >= n) handles.push_back(child);
            else cells.push_back(child);
        }
        float height = minHeight;
        std::vector<Size> sizes(cells.size());
        for (size_t i = 0; i < cells.size() && i < n; ++i) {
            float width = i < d.widths.size() ? d.widths[i] : 0.f;
            bool wraps = table->columns[i].width.kind == TableColumnWidth::Kind::Wrap;
            bool flexes = table->columns[i].width.kind == TableColumnWidth::Kind::Flex;
            // A Wrap or Flex column's cells (and a header) at their own widths, to learn them (a Flex one
            // is no narrower than its widest cell: none wraps when the table overflows); the others' fit the column.
            bool learns = wraps || flexes || header;
            Size s = cells[i]->measure(learns ? Constraints{0, kInfinity, 0, kInfinity} : Constraints{0, width, 0, kInfinity});
            if (!probe && learns) {
                float& seen = header ? d.header[i] : d.wrap[i];
                if (s.width > seen + 0.5f) {
                    seen = s.width;
                    if (wraps || flexes) d.grew.set(d.grew.peek() + 1);
                }
            }
            if (s.width > width) s = cells[i]->measure(Constraints{0, width, 0, kInfinity});
            sizes[i] = s;
            height = std::max(height, s.height);
        }
        float total = d.total;
        for (size_t i = 0; i < cells.size() && i < n; ++i) {
            float width = i < d.widths.size() ? d.widths[i] : 0.f, start = i < d.starts.size() ? d.starts[i] : 0.f;
            HorizontalAlignment align = table->columns[i].alignment;
            if (auto* e = cells[i]->modifier.find<CellElement>(); e && e->alignment) align = *e->alignment;
            float x = start + (width - sizes[i].width) / 2 * (1 + align.bias);
            bool fixed = int(i) < frozen;
            cells[i]->place(x + (fixed ? scroll : 0), (height - sizes[i].height) / 2, fixed ? 2 : 0);
        }
        if (backdrop) {
            float width = frozen > 0 && size_t(frozen - 1) < d.starts.size() ? d.starts[size_t(frozen - 1)] + d.widths[size_t(frozen - 1)] : 0.f;
            backdrop->measure(Constraints::fixed(width, std::max(0.f, height - 1)));  // (the row's divider under it shows)
            backdrop->place(scroll, 0, 1);
        }
        for (size_t i = 0; i < handles.size() && i < n && i < d.widths.size(); ++i) {
            Size s = handles[i]->measure(Constraints{0, kInfinity, height, height});
            bool fixed = int(i) < frozen;
            handles[i]->place(d.starts[i] + d.widths[i] - s.width / 2 + (fixed ? scroll : 0), 0, 3);
        }
        return c.constrain(Size{total, height});
    };
}

void emit(const Modifier& modifier, MeasurePolicy policy, const Content& content, CallSite site) {
    detail::Kompozer::require().emit(
        detail::keyOf(site),
        [&](LayoutNode& node) {
            node.modifier = modifier;
            node.measurePolicy = policy;
            node.drawContent = {};
        },
        content);
}

// The backdrop under a row's frozen cells: the row's own color over the table's.
void backdrop(const TableContext& table, Color color) {
    Color under = table.options.backgroundColor.value_or(colorScheme().surface);
    detail::Box(Modifier().then(std::make_shared<BackdropElement>()).background(under).background(color), Content{});
}

// The header: each column's, sorted ones with their arrow, a click sorting those with onSort; a
// handle at each resizable column's end that the user drags to widen it.
void tableHeader(const std::shared_ptr<TableContext>& table) {
    const ColorScheme& cs = colorScheme();
    const Strings& strings = LocalStrings.current();
    const auto& o = table->options;
    Color background = o.headerBackgroundColor.value_or(o.backgroundColor.value_or(cs.surface));
    bool fine = mediaQuery([](const UiMediaScope& m) { return m.pointerPrecision() == PointerPrecision::Fine; });
    float reach = fine ? 8 : 24;
    Modifier m = Modifier().background(background).drawBehind([line = cs.outlineVariant](DrawScope& draw) {
        draw.drawLine(line, Offset{0, draw.size().height - 0.5f}, Offset{draw.size().width, draw.size().height - 0.5f}, 1);
    });
    emit(m, rowPolicy(table, o.headerHeight, true),
         [&] {
             for (size_t i = 0; i < table->columns.size(); ++i) {
                 const DataColumnSpec& column = table->columns[i];
                 bool sorted = o.sortColumnIndex == int(i);
                 Modifier cellModifier = Modifier().semantics({.stateDescription = sorted ? (o.sortAscending ? strings.sortedAscending : strings.sortedDescending) : std::string(),
                                                               .heading = true});
                 if (column.onSort)
                     cellModifier = cellModifier.clickable([onSort = column.onSort, i, sorted, ascending = o.sortAscending] {
                         onSort(int(i), sorted ? !ascending : true);
                     });
                 key(int(i), [&] {
                     detail::Box(cellModifier.heightIn(o.headerHeight).padding(o.horizontalPadding, 0), Alignment::CenterStart, [=] {
                         detail::Row(Modifier(), detail::RowStyle{.horizontalArrangement = Arrangement::spacedBy(4), .verticalAlignment = Alignment::CenterVertically}, [=] {
                             KompozitionLocalProvider({LocalTextStyle.provides(typography().titleSmall)}, column.header);
                             if (sorted)
                                 detail::Icon(o.sortAscending ? Icons::Filled::KeyboardArrowUp : Icons::Filled::KeyboardArrowDown, std::string(),
                                              Modifier().size(18), colorScheme().onSurfaceVariant);
                         });
                     });
                 });
             }
             for (size_t i = 0; i < table->columns.size(); ++i) {  // the resize handles
                 key(int(i), [&] {
                     if (!table->columns[i].resizable) return detail::Box(Modifier().width(0), Content{});
                     DataTableState state = table->state;
                     bool rtl = table->rtl;
                     detail::Box(Modifier()
                                     .width(reach)
                                     .fillMaxHeight()
                                     .cursorForHorizontalResize()
                                     .pointerInput(std::tuple<int, bool>(int(i), rtl),
                                                   [state, i, rtl](PointerInputScope& scope) -> Task {
                                                       DragGestureHandlers h;
                                                       h.onDrag = [state, i, rtl](PointerInputChange& change, Offset amount) {
                                                           change.consume();
                                                           const auto& widths = state.data_->widths;
                                                           float now = state.columnWidth(int(i)).value_or(i < widths.size() ? widths[i] : 0.f);
                                                           state.setColumnWidth(int(i), now + (rtl ? -amount.x : amount.x));
                                                       };
                                                       co_await detectDragGestures(scope, h);
                                                   }),
                                 Content{});
                 });
             }
             backdrop(*table, background);
         },
         CallSite::current());
}

// Lays out the header above the rows, as wide as the columns.
Size stackPolicy(std::shared_ptr<TableContext> table, LayoutNode& node, const Constraints& c) {
    DataTableState::Data& d = *table->state.data_;
    d.widened.get();
    float width = std::max(d.total, c.minWidth);
    float height = c.hasBoundedHeight() ? c.maxHeight : 0;
    Size head = node.children.empty() ? Size{} : node.children[0]->measure(Constraints{width, width, 0, kInfinity});
    if (!node.children.empty()) node.children[0]->place(0, 0, 1);
    if (node.children.size() > 1) {
        float rest = c.hasBoundedHeight() ? std::max(0.f, height - head.height) : kInfinity;
        Size body = node.children[1]->measure(Constraints{width, width, 0, rest});
        node.children[1]->place(0, head.height);
        if (!c.hasBoundedHeight()) height = head.height + body.height;
    }
    return c.constrain(Size{width, height});
}

} // namespace

bool& detail::inDataTableBlock() {
    thread_local bool inside = false;
    return inside;
}

void detail::dataTable(std::vector<DataColumnSpec> columns, const Modifier& modifier, const DataTableOptions& options,
                       const std::function<void(LazyListScope&)>& content, CallSite site) {
    KompozableScope scope(site);
    DataTableState own = rememberDataTableState();
    DataTableState state = options.state ? *options.state : own;
    auto table = remember([] { return std::make_shared<TableContext>(); });
    table->state = state;
    table->columns = std::move(columns);
    table->options = options;
    table->rtl = LocalLayoutDirection.current() == LayoutDirection::Rtl;
    std::shared_ptr<TableContext> t = table;
    KompozitionLocalProvider({LocalDataTable.provides(t)}, [&] {
        // The table's width decides its columns'; the header and the rows, as wide as those,
        // scroll sideways together.
        emit(modifier.background(options.backgroundColor.value_or(colorScheme().surface)).clipToBounds(),
             [t](LayoutNode& node, const Constraints& c) {
                 DataTableState::Data& d = *t->state.data_;
                 d.grew.get();  // (a Wrap column widened: laid out again)
                 float width = c.hasBoundedWidth() ? c.maxWidth : c.minWidth;
                 layOutColumns(d, t->columns, width);
                 Size s{width, c.hasBoundedHeight() ? c.maxHeight : c.minHeight};
                 for (LayoutNode* child : node.children) {
                     s = child->measure(Constraints{width, width, c.minHeight, c.maxHeight});
                     child->place(0, 0);
                 }
                 return c.constrain(Size{width, s.height});
             },
             [&] {
                 detail::Box(Modifier().horizontalScroll(state.horizontalScroll()), [t, content] {
                     emit(Modifier(), [t](LayoutNode& node, const Constraints& c) { return stackPolicy(t, node, c); },
                          [t, content] {
                              tableHeader(t);
                              kompoz::LazyColumn(Modifier(), kompoz::state = t->state.listState(), [content] {
                                  LazyListScope& scope = *detail::lazyReceiver().list;
                                  bool saved = std::exchange(detail::inDataTableBlock(), true);
                                  content(scope);
                                  detail::inDataTableBlock() = saved;
                              });
                          },
                          CallSite::current());
                 });
             },
             CallSite::current());
    });
}

void detail::dataTableRow(bool selected, std::function<void()> onClick, const Content& cells, CallSite site) {
    if (inDataTableBlock()) {  // in the table's block: a row of its own
        LazyListScope* list = lazyReceiver().list;
        if (!list) outsideLazyBlock("row");
        list->item([selected, onClick, cells, site] { dataTableRow(selected, onClick, cells, site); });
        return;
    }
    KompozableScope scope(site);
    std::shared_ptr<TableContext> table = LocalDataTable.current();
    if (!table) outsideLazyBlock("row");
    const ColorScheme& cs = colorScheme();
    Color background = selected ? cs.secondaryContainer : Color::Transparent;
    Modifier m = Modifier().background(background).drawBehind([line = cs.outlineVariant](DrawScope& draw) {
        draw.drawLine(line, Offset{0, draw.size().height - 0.5f}, Offset{draw.size().width, draw.size().height - 0.5f}, 1);
    });
    if (onClick) m = m.clickable(onClick);
    m = m.semantics({.checked = selected, .mergeDescendants = true});
    emit(m, rowPolicy(table, table->options.rowHeight, false),
         [&] {
             cells();
             backdrop(*table, background);
         },
         site);
}

void detail::dataTableCell(std::optional<HorizontalAlignment> alignment, const Content& content, CallSite site) {
    KompozableScope scope(site);
    std::shared_ptr<TableContext> table = LocalDataTable.current();
    float padding = table ? table->options.horizontalPadding : DataTableDefaults::HorizontalPadding;
    auto marker = std::make_shared<CellElement>();
    marker->alignment = alignment;
    detail::Box(Modifier().then(marker).padding(padding, 0), content);
}

} // namespace kompoz
