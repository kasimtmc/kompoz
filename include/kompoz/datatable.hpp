#pragma once

// A table of rows under a header of columns: a list of orders, files, people. Compose has none;
// this is the API of compose-data-table (Compose Multiplatform's, after Flutter's DataTable),
// with what big tables need on top: rows composed as they come into view, a header that stays
// as they scroll, columns frozen at the start as the rest scrolls sideways, columns the user
// widens by dragging their header's edge, sorting from the header.
//
//   DataTable(
//       columns = listOf(
//           DataColumn(width = TableColumnWidth.Fixed(64.dp)) { Text("No") },
//           DataColumn(onSort = { index, ascending -> sortBy(index, ascending) }) { Text("Ad") },
//           DataColumn(alignment = Alignment.End, width = TableColumnWidth.Wrap) { Text("Tutar") },
//       ),
//       sortColumnIndex = sortIndex, sortAscending = ascending, frozenColumns = 1,
//       modifier = Modifier.fillMaxSize(),
//   ) {
//       items(orders, key = { it.id }) { order ->
//           row(selected = order.id in chosen, onClick = { toggle(order.id) }) {
//               cell { Text("${order.no}") }
//               cell { Text(order.customer) }
//               cell { Text(order.total) }
//           }
//       }
//   }
//
// In the table's block, row { } adds one row and items(…) { … row { } } a row for each element
// (keyed, as a LazyColumn's items); in a row, cell { } a cell for each column, in order. A column
// is as wide as its TableColumnWidth says: Fixed, a Fraction of the table, Wrap (its widest cell
// seen yet, the header's included) or a share (Flex) of what the others leave, no narrower than
// its widest cell seen; dragged at its
// header's end edge (resizable = true), the width the user gave it.

#include "lazy.hpp"
#include "layout.hpp"
#include "named.hpp"
#include "runtime.hpp"
#include "saved_value.hpp"
#include "state.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace kompoz {

// How wide a column is (compose-data-table's TableColumnWidth).
struct TableColumnWidth {
    enum class Kind { Fixed, Flex, Wrap, Fraction };
    Kind kind = Kind::Flex;
    float value = 1;  // dp (Fixed), a weight (Flex), a part of the table's width (Fraction)

    static TableColumnWidth Fixed(float width) { return {Kind::Fixed, width}; }
    static TableColumnWidth Flex(float weight) { return {Kind::Flex, weight}; }
    static TableColumnWidth Fraction(float fraction) { return {Kind::Fraction, fraction}; }
    static const TableColumnWidth Wrap;
    friend bool operator==(const TableColumnWidth&, const TableColumnWidth&) = default;
};
inline const TableColumnWidth TableColumnWidth::Wrap{TableColumnWidth::Kind::Wrap, 0};

// A column: its header, how wide it is, where its cells' content sits, and what a click on its
// header does (sorting: told the column and the way, ascending first, then the other way).
struct DataColumnSpec {
    TableColumnWidth width = TableColumnWidth::Flex(1);
    HorizontalAlignment alignment = Alignment::Start;
    std::function<void(int columnIndex, bool ascending)> onSort;
    bool resizable = true;
    Content header;
};

namespace detail {
struct DataColumnImpl {
    using P = Params<names::Alignment, names::Width, names::OnSort, names::Resizable, Block>;
    template <class... A>
    static DataColumnSpec call(CallSite, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: DataColumn takes a block: its header");
        DataColumnSpec c;
        P::set<names::Alignment>(c.alignment, args...);
        P::set<names::Width>(c.width, args...);
        if constexpr (P::given<names::OnSort, A...>()) c.onSort = P::arg<names::OnSort>(args...);
        P::set<names::Resizable>(c.resizable, args...);
        c.header = Content(std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...)));
        return c;
    }
};
} // namespace detail
inline constexpr Composable<detail::DataColumnImpl> DataColumn;

// A table's scroll positions (down its rows, along its columns) and the widths the user gave
// its columns. Saved with the app's state.
class DataTableState {
public:
    DataTableState();
    const LazyListState& listState() const;   // its rows'
    const ScrollState& horizontalScroll() const;  // sideways, in dp
    // The width the user dragged column `index` to (dp), if any; set one, or forget them all.
    std::optional<float> columnWidth(int index) const;
    void setColumnWidth(int index, float width) const;
    void resetColumnWidths() const;
    friend bool operator==(const DataTableState& a, const DataTableState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};

template <>
struct Saver<DataTableState> {
    static SavedValue save(const DataTableState& s);
    static std::optional<DataTableState> restore(const SavedValue& v);
};

DataTableState rememberDataTableState(CallSite site = CallSite::current());

// The defaults (Material's data table measures).
struct DataTableDefaults {
    static constexpr float HeaderHeight = 56, RowHeight = 52, HorizontalPadding = 16;
    static constexpr float MinColumnWidth = 40;  // the least a drag leaves a column
};

namespace detail {

struct DataTableOptions {
    std::optional<DataTableState> state;
    int sortColumnIndex = -1;
    bool sortAscending = true;
    int frozenColumns = 0;
    float headerHeight = DataTableDefaults::HeaderHeight, rowHeight = DataTableDefaults::RowHeight;
    float horizontalPadding = DataTableDefaults::HorizontalPadding;
    std::optional<Color> headerBackgroundColor;
    // What the table is drawn on (the scheme's surface unless given): frozen cells cover what
    // scrolls under them with it.
    std::optional<Color> backgroundColor;
};
void dataTable(std::vector<DataColumnSpec> columns, const Modifier& modifier, const DataTableOptions& options,
               const std::function<void(LazyListScope&)>& content, CallSite site);

// While a table's block runs: row { } there adds a row (in an item's content it is the row).
bool& inDataTableBlock();
// row(selected = …, onClick = …) { cell { … }; … }
void dataTableRow(bool selected, std::function<void()> onClick, const Content& cells, CallSite site);
void dataTableCell(std::optional<HorizontalAlignment> alignment, const Content& content, CallSite site);

struct DataTableImpl {
    using P = Params<names::Columns, names::Modifier, names::State, names::SortColumnIndex, names::SortAscending, names::FrozenColumns,
                     names::HeaderHeight, names::RowHeight, names::HorizontalPadding, names::HeaderBackgroundColor,
                     names::BackgroundColor, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: DataTable takes a block: its rows (row { cell { … } }, items(…) { row { … } })");
        const auto& given = P::arg<names::Columns>(args...);
        std::vector<DataColumnSpec> columns(std::begin(given), std::end(given));
        DataTableOptions options;
        if constexpr (P::given<names::State, A...>()) options.state = P::arg<names::State>(args...);
        P::set<names::SortColumnIndex>(options.sortColumnIndex, args...);
        P::set<names::SortAscending>(options.sortAscending, args...);
        P::set<names::FrozenColumns>(options.frozenColumns, args...);
        P::set<names::HeaderHeight>(options.headerHeight, args...);
        P::set<names::RowHeight>(options.rowHeight, args...);
        P::set<names::HorizontalPadding>(options.horizontalPadding, args...);
        if constexpr (P::given<names::HeaderBackgroundColor, A...>()) options.headerBackgroundColor = Color(P::arg<names::HeaderBackgroundColor>(args...));
        if constexpr (P::given<names::BackgroundColor, A...>()) options.backgroundColor = Color(P::arg<names::BackgroundColor>(args...));
        auto&& block = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
        dataTable(std::move(columns), P::get<names::Modifier>(Modifier{}, args...), options, lazyContent<LazyListScope>(block), site);
    }
};

} // namespace detail

inline constexpr Composable<detail::DataTableImpl> DataTable;

namespace detail {
struct DataTableRowImpl {
    using P = Params<names::Selected, names::OnClick, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: row takes a block: its cells (cell { … })");
        std::function<void()> onClick;
        if constexpr (P::given<names::OnClick, A...>()) onClick = P::arg<names::OnClick>(args...);
        dataTableRow(P::get<names::Selected>(false, args...), std::move(onClick), Content(std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...))),
                     site);
    }
};
struct DataTableCellImpl {
    using P = Params<names::Alignment, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: cell takes a block: its content");
        std::optional<HorizontalAlignment> alignment;
        if constexpr (P::given<names::Alignment, A...>()) alignment = P::arg<names::Alignment>(args...);
        dataTableCell(alignment, Content(std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...))), site);
    }
};
} // namespace detail

// In a table: a row (selected: shown so, and said so to accessibility services; onClick: it can
// be clicked) of cells; in a row, a cell (its content where its column says, or `alignment`).
inline constexpr Composable<detail::DataTableRowImpl> row;
inline constexpr Composable<detail::DataTableCellImpl> cell;

} // namespace kompoz
