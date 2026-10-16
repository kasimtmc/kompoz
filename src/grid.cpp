// Grid: CSS's grid, laid out as Compose 1.12's GridMeasurePolicy does (in dp).

#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace kompoz {

namespace {

struct GridCell {
    LayoutNode* node = nullptr;
    int row = 0, column = 0, rowSpan = 1, columnSpan = 1;
    Alignment alignment = Alignment::TopStart;
};

// A 1-based index (negative: from the end of the configured tracks) as 0-based; -1: unset, or
// before the first track.
int zeroBased(int index, int count) {
    if (index == 0) return -1;
    if (index > 0) return index - 1;
    return std::max(-1, count + index);
}

// Where each child goes: where it asks, else the first free cells from where the last one placed
// automatically went, along the flow, onto a new row (column) past the configured ones.
std::vector<GridCell> placeCells(const LayoutNode& node, const GridConfig& config, int& rowCount, int& columnCount) {
    const int explicitColumns = int(config.columns.size()), explicitRows = int(config.rows.size());
    const bool byRow = config.flow == GridFlow::Row;
    rowCount = explicitRows;
    columnCount = explicitColumns;
    std::unordered_set<uint32_t> occupied;
    auto key = [](int row, int column) { return uint32_t(row) << 16 | uint32_t(column); };
    auto taken = [&](int row, int column, int rowSpan, int columnSpan) {
        if (row + rowSpan > kGridMaxIndex || column + columnSpan > kGridMaxIndex) return true;
        for (int r = row; r < row + rowSpan; ++r)
            for (int c = column; c < column + columnSpan; ++c)
                if (occupied.count(key(r, c))) return true;
        return false;
    };
    int cursorRow = 0, cursorColumn = 0;
    std::vector<GridCell> cells;
    cells.reserve(node.children.size());
    for (LayoutNode* child : node.children) {
        GridCell cell;
        cell.node = child;
        int row = -1, column = -1;
        if (std::optional<GridItemConfig> item = child->parentData().grid) {
            cell.alignment = item->alignment;
            int askedRow = item->row, askedColumn = item->column;
            cell.rowSpan = item->rowSpan;
            cell.columnSpan = item->columnSpan;
            if (!item->area.empty()) {  // a named area; an unknown one goes where there's room
                auto area = std::find_if(config.areas.begin(), config.areas.end(), [&](const GridArea& a) { return a.name == item->area; });
                bool known = area != config.areas.end();
                askedRow = known ? area->row : 0;
                askedColumn = known ? area->column : 0;
                cell.rowSpan = known ? std::max(1, area->rowSpan) : 1;
                cell.columnSpan = known ? std::max(1, area->columnSpan) : 1;
            }
            row = zeroBased(askedRow, explicitRows);
            column = zeroBased(askedColumn, explicitColumns);
        }
        int finalRow = -1, finalColumn = -1;
        if (row >= 0 && column >= 0) {  // where asked, over another or not
            finalRow = row;
            finalColumn = column;
        } else if (row >= 0) {  // the first free column of the row
            finalRow = row;
            for (int c = byRow && row == cursorRow ? cursorColumn : 0; c < kGridMaxIndex; ++c)
                if (!taken(row, c, cell.rowSpan, cell.columnSpan)) {
                    finalColumn = c;
                    break;
                }
        } else if (column >= 0) {  // the first free row of the column
            finalColumn = column;
            for (int r = !byRow && column == cursorColumn ? cursorRow : 0; r < kGridMaxIndex; ++r)
                if (!taken(r, column, cell.rowSpan, cell.columnSpan)) {
                    finalRow = r;
                    break;
                }
        } else {
            int r = cursorRow, c = cursorColumn;
            while (r < kGridMaxIndex && c < kGridMaxIndex) {
                // Past the configured tracks: on to the next row (column), unless it doesn't fit
                // even at the start, when it makes the grid wider (taller).
                if (byRow && explicitColumns > 0 && c + cell.columnSpan > explicitColumns && c > 0) {
                    c = 0;
                    ++r;
                    continue;
                }
                if (!byRow && explicitRows > 0 && r + cell.rowSpan > explicitRows && r > 0) {
                    r = 0;
                    ++c;
                    continue;
                }
                if (!taken(r, c, cell.rowSpan, cell.columnSpan)) {
                    finalRow = r;
                    finalColumn = c;
                    break;
                }
                if (byRow && ++c > kGridMaxIndex) {
                    c = 0;
                    ++r;
                } else if (!byRow && ++r > kGridMaxIndex) {
                    r = 0;
                    ++c;
                }
            }
        }
        cell.row = std::max(0, finalRow);  // no room left: over the first cell
        cell.column = std::max(0, finalColumn);
        for (int r = cell.row; r < cell.row + cell.rowSpan; ++r)
            for (int c = cell.column; c < cell.column + cell.columnSpan; ++c) occupied.insert(key(r, c));
        rowCount = std::max(rowCount, cell.row + cell.rowSpan);
        columnCount = std::max(columnCount, cell.column + cell.columnSpan);
        if (row < 0 && column < 0) {  // only what went where there was room moves the cursor
            cursorRow = byRow ? cell.row : cell.row + cell.rowSpan;
            cursorColumn = byRow ? cell.column + cell.columnSpan : cell.column;
        }
        cells.push_back(cell);
    }
    // (Compose sorts them top start to bottom end to place, and so draw, them in that order;
    // here children are drawn as composed.)
    return cells;
}

class GridMeasure {
public:
    GridMeasure(LayoutNode& node, const Constraints& c, const GridConfig& config) : node_(node), constraints_(c), config_(config) {}

    Size measure() {
        int rowCount = 0, columnCount = 0;
        cells_ = placeCells(node_, config_, rowCount, columnCount);
        columnGap_ = std::max(0.f, config_.columnGap);
        rowGap_ = std::max(0.f, config_.rowGap);
        // Columns first: how tall a text is depends on how wide its column is.
        widths_.assign(columnCount, 0);
        float width = sizeTracks(false, config_.columns, widths_, constraints_.maxWidth, columnGap_);
        heights_.assign(rowCount, 0);
        float height = sizeTracks(true, config_.rows, heights_, constraints_.maxHeight, rowGap_);
        width += float(std::max(0, columnCount - 1)) * columnGap_;
        height += float(std::max(0, rowCount - 1)) * rowGap_;

        std::vector<float> left = offsets(widths_, columnGap_), top = offsets(heights_, rowGap_);
        for (GridCell& cell : cells_) {
            Size space{spanned(widths_, cell.column, cell.columnSpan, columnGap_), spanned(heights_, cell.row, cell.rowSpan, rowGap_)};
            Size size = cell.node->measure({0, space.width, 0, space.height});
            Offset at = cell.alignment.align(size, space);
            cell.node->place(left[cell.column] + at.x, top[cell.row] + at.y);
        }
        return {std::clamp(width, constraints_.minWidth, constraints_.maxWidth), std::clamp(height, constraints_.minHeight, constraints_.maxHeight)};
    }

private:
    // Its natural width (min: a word a line), or its height at `width`.
    static float naturalWidth(LayoutNode* node, bool minimum) {
        detail::ProbeMeasure probe;
        std::optional<detail::MinIntrinsicWidth> narrowest;
        if (minimum) narrowest.emplace();
        return node->measure({0, kInfinity, 0, kInfinity}).width;
    }
    static float naturalHeight(LayoutNode* node, float width) {
        detail::ProbeMeasure probe;
        return node->measure({0, width, 0, kInfinity}).height;
    }

    static float spanned(const std::vector<float>& sizes, int start, int span, float gap) {
        int end = std::min(start + span, int(sizes.size()));
        float total = 0;
        for (int i = start; i < end; ++i) total += sizes[i];
        return total + float(std::max(0, end - start - 1)) * gap;
    }

    static std::vector<float> offsets(const std::vector<float>& sizes, float gap) {
        std::vector<float> result(sizes.size());
        float at = 0;
        for (size_t i = 0; i < sizes.size(); ++i) {
            result[i] = at;
            at += sizes[i] + gap;
        }
        return result;
    }

    // The tallest (widest) of the track's one-track items: min or max content.
    float content(bool rows, int track, bool minimum) const {
        float most = 0;
        for (const GridCell& cell : cells_) {
            if ((rows ? cell.row : cell.column) != track || (rows ? cell.rowSpan : cell.columnSpan) != 1) continue;
            float size = rows ? naturalHeight(cell.node, spanned(widths_, cell.column, cell.columnSpan, columnGap_))
                              : naturalWidth(cell.node, minimum);
            most = std::max(most, size);
        }
        return most;
    }

    // Each track's size along one axis; returns their sum. Fixed and percentage tracks are what
    // they say; content tracks as big as their items; Auto ones from min towards max content in
    // what room is left; then flexible ones (from min content, or MinMax's minimum) share the rest.
    float sizeTracks(bool rows, const std::vector<GridTrackSize>& specs, std::vector<float>& sizes, float available, float gap) {
        const int count = int(sizes.size());
        if (count == 0) return 0;
        auto spec = [&](int i) { return i < int(specs.size()) ? specs[i] : GridTrackSize::Auto(); };
        const bool bounded = available < kInfinity;
        const float room = bounded ? std::max(0.f, available - float(count - 1) * gap) : kInfinity;
        float totalWeight = 0;
        std::vector<int> autos;
        std::vector<float> autoMax(count, 0);
        using Kind = GridTrackSize::Kind;
        for (int i = 0; i < count; ++i) {
            GridTrackSize s = spec(i);
            switch (s.kind) {
            case Kind::Fixed: sizes[i] = s.value; break;
            // (In an unbounded grid, a percentage of it is the content's size.)
            case Kind::Percentage: sizes[i] = bounded ? s.value * room : content(rows, i, false); break;
            case Kind::Flex:
                totalWeight += s.value;
                sizes[i] = content(rows, i, true);
                break;
            case Kind::MinContent: sizes[i] = content(rows, i, true); break;
            case Kind::MaxContent: sizes[i] = content(rows, i, false); break;
            case Kind::Auto:
                if (bounded) {
                    sizes[i] = content(rows, i, true);
                    autoMax[i] = content(rows, i, false);
                    autos.push_back(i);
                } else {
                    sizes[i] = content(rows, i, false);
                }
                break;
            case Kind::MinMax:
                totalWeight += s.weight;
                sizes[i] = s.value;
                break;
            }
        }

        // An item across tracks grows the ones that can grow evenly by what it lacks.
        auto growable = [&](int i) { return spec(i).kind != Kind::Fixed && spec(i).kind != Kind::Percentage; };
        for (const GridCell& cell : cells_) {
            int start = rows ? cell.row : cell.column, span = rows ? cell.rowSpan : cell.columnSpan;
            if (span <= 1) continue;
            int end = std::min(start + span, count), growing = 0;
            for (int i = start; i < end; ++i) growing += growable(i);
            float need = rows ? naturalHeight(cell.node, spanned(widths_, cell.column, cell.columnSpan, columnGap_)) : naturalWidth(cell.node, false);
            float lack = need - spanned(sizes, start, span, gap);
            if (lack <= 0 || growing == 0) continue;
            for (int i = start; i < end; ++i)
                if (growable(i)) sizes[i] += lack / float(growing);
        }

        auto used = [&] {
            float total = 0;
            for (float s : sizes) total += s;
            return total;
        };
        if (bounded && !autos.empty()) {
            float left = room - used(), potential = 0;
            for (int i : autos) potential += std::max(0.f, autoMax[i] - sizes[i]);
            if (left > 0 && potential > 0)
                for (int i : autos) sizes[i] += std::max(0.f, autoMax[i] - sizes[i]) * std::min(1.f, left / potential);
        }

        float total = used();
        float left = bounded ? std::max(0.f, room - total) : 0;
        if (totalWeight > 0 && left > 0) {
            for (int i = 0; i < count; ++i) {
                GridTrackSize s = spec(i);
                float weight = s.kind == Kind::Flex ? s.value : s.kind == Kind::MinMax ? s.weight : 0;
                if (weight > 0) sizes[i] += weight / totalWeight * left;
            }
            total += left;
        }
        return total;
    }

    LayoutNode& node_;
    Constraints constraints_;
    const GridConfig& config_;
    std::vector<GridCell> cells_;
    std::vector<float> widths_, heights_;
    float columnGap_ = 0, rowGap_ = 0;
};

void grid(const CallSite& site, const Modifier& modifier, std::function<GridConfig(const Constraints&)> config, const Content& content) {
    detail::Composer::require().emit(
        detail::keyOf(site),
        [&](LayoutNode& node) {
            node.modifier = modifier;
            node.measurePolicy = [config](LayoutNode& n, const Constraints& c) {
                GridConfig resolved = config(c);
                return GridMeasure(n, c, resolved).measure();
            };
            node.drawContent = {};
        },
        content);
}

} // namespace

void Grid(const Modifier& modifier, const GridConfig& config, const Content& content, CallSite site) {
    grid(site, modifier, [config](const Constraints&) { return config; }, content);
}

void Grid(const Modifier& modifier, std::function<GridConfig(const Constraints&)> config, const Content& content, CallSite site) {
    grid(site, modifier, std::move(config), content);
}

} // namespace kompoz
