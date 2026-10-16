// FlexBox: CSS's flexbox, measured as Compose 1.12's FlexBoxMeasurePolicy does (in dp).

#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace kompoz {

namespace {

struct FlexItem {
    LayoutNode* node = nullptr;
    FlexConfig config;
    float flexBase = 0, hypothetical = 0, target = 0;
    float minMain = 0;
    bool frozen = false;
    bool measured = false;
    float main = 0, cross = 0;  // as measured
    float mainPosition = 0, crossPosition = 0;
    float baseline = NAN;
};

struct FlexLine {
    size_t start = 0, end = 0;
    float mainSize = 0, crossSize = 0, crossStart = 0, maxAboveBaseline = 0;
};

// The box's axes: main along its direction, cross across it.
struct FlexAxes {
    bool horizontal;
    float mainMin, mainMax, crossMin, crossMax;
    FlexAxes(bool horizontal, const Constraints& c)
        : horizontal(horizontal), mainMin(horizontal ? c.minWidth : c.minHeight), mainMax(horizontal ? c.maxWidth : c.maxHeight),
          crossMin(horizontal ? c.minHeight : c.minWidth), crossMax(horizontal ? c.maxHeight : c.maxWidth) {}
    float mainOf(Size s) const { return horizontal ? s.width : s.height; }
    float crossOf(Size s) const { return horizontal ? s.height : s.width; }
    Constraints make(float mainLo, float mainHi, float crossLo, float crossHi) const {
        return horizontal ? Constraints{mainLo, mainHi, crossLo, crossHi} : Constraints{crossLo, crossHi, mainLo, mainHi};
    }
};

class FlexMeasure {
public:
    FlexMeasure(LayoutNode& node, const Constraints& c, const FlexBoxConfig& config)
        : node_(node), config_(config), axes_(config.direction == FlexDirection::Row || config.direction == FlexDirection::RowReverse, c),
          constraints_(c) {}

    Size measure() {
        if (node_.children.empty()) return {constraints_.minWidth, constraints_.minHeight};
        bool wrapping = config_.wrap != FlexWrap::NoWrap;
        mainGap_ = axes_.horizontal ? config_.columnGap : config_.rowGap;
        crossGap_ = axes_.horizontal ? config_.rowGap : config_.columnGap;
        upfront_ = config_.alignItems == FlexAlignItems::Stretch || config_.alignItems == FlexAlignItems::Baseline ||
                   (wrapping && config_.alignContent == FlexAlignContent::Stretch && axes_.crossMax < kInfinity);
        for (LayoutNode* child : node_.children) items_.push_back(createItem(child));
        std::stable_sort(items_.begin(), items_.end(), [](const FlexItem& a, const FlexItem& b) { return a.config.order < b.config.order; });

        float totalCross = buildLines(wrapping);
        if (lines_.size() == 1) lines_[0].crossSize = std::max(lines_[0].crossSize, axes_.crossMin);
        totalCross = stretchLines(totalCross);
        totalCross = measureItems(totalCross);
        // (Compose leaves the gaps out of the room here: a reversed or end-aligned wrap then sits
        // a gap's worth out of its box. With them in, it doesn't; the rest comes out the same.)
        float gaps = lines_.empty() ? 0 : float(lines_.size() - 1) * crossGap_;
        positionLines(std::max(totalCross + gaps, axes_.crossMin), totalCross);
        const FlexLine& outer = config_.wrap == FlexWrap::WrapReverse ? lines_.front() : lines_.back();
        totalCross = std::max(outer.crossStart + outer.crossSize, axes_.crossMin);
        float longest = axes_.mainMin;
        if (!lines_.empty()) {
            longest = 0;
            for (const FlexLine& line : lines_) longest = std::max(longest, line.mainSize);
        }
        float mainSize = std::clamp(std::max(longest, axes_.mainMin), axes_.mainMin, axes_.mainMax);
        float crossSize = std::clamp(totalCross, axes_.crossMin, axes_.crossMax);
        for (FlexLine& line : lines_) {
            positionOnMainAxis(line, mainSize);
            for (size_t i = line.start; i < line.end; ++i) {
                const FlexItem& item = items_[i];
                if (axes_.horizontal) item.node->place(item.mainPosition, item.crossPosition);
                else item.node->place(item.crossPosition, item.mainPosition);
            }
        }
        return axes_.horizontal ? Size{mainSize, crossSize} : Size{crossSize, mainSize};
    }

private:
    // Its natural main size (max: as it would like; min: Compose's min intrinsic, texts a word a line).
    float probeMain(LayoutNode* child, bool minimum) const {
        detail::ProbeMeasure probe;
        std::optional<detail::MinIntrinsicWidth> narrowest;
        if (minimum && axes_.horizontal) narrowest.emplace();
        return axes_.mainOf(child->measure({0, kInfinity, 0, kInfinity}));
    }

    FlexItem createItem(LayoutNode* child) {
        FlexItem item;
        item.node = child;
        item.config = child->parentData().flex.value_or(FlexConfig{});
        item.minMain = probeMain(child, true);
        const FlexBasis& basis = item.config.basis;
        if (basis.kind == FlexBasis::Kind::Dp) item.flexBase = basis.value;
        else if (basis.kind == FlexBasis::Kind::Percent && axes_.mainMax < kInfinity && !std::isnan(basis.value))
            item.flexBase = axes_.mainMax * basis.value;
        else item.flexBase = probeMain(child, false);
        item.hypothetical = std::max(item.flexBase, item.minMain);
        item.target = item.hypothetical;
        if (item.config.alignSelf == FlexAlignSelf::Baseline || item.config.alignSelf == FlexAlignSelf::Stretch) upfront_ = true;
        return item;
    }

    float buildLines(bool wrapping) {
        float lineSize = 0, crossPosition = 0, total = 0, remainingCross = axes_.crossMax;
        size_t start = 0;
        auto finish = [&](size_t end) {
            FlexLine line{start, end};
            processLine(line, lineSize - mainGap_, remainingCross);
            total += line.crossSize;
            line.crossStart = crossPosition;
            crossPosition += line.crossSize + crossGap_;
            remainingCross = std::max(0.f, remainingCross - (line.crossSize + crossGap_));
            lines_.push_back(line);
        };
        for (size_t i = 0; i < items_.size(); ++i) {
            const FlexItem& item = items_[i];
            if (wrapping && i > start && lineSize + item.hypothetical > axes_.mainMax) {
                finish(i);
                start = i;
                lineSize = item.hypothetical + mainGap_;
            } else {
                lineSize += item.hypothetical + mainGap_;
            }
        }
        if (start < items_.size()) finish(items_.size());
        return total;
    }

    void processLine(FlexLine& line, float hypotheticalSize, float remainingCross) {
        line.mainSize = resolveFlexibleLengths(line, hypotheticalSize);
        if (upfront_) lineCrossSize(line, remainingCross);
    }

    // Grows the items into the room left over, or shrinks them (by their base sizes) into what
    // there is, freezing those that hit their minimum and sharing the rest again.
    float resolveFlexibleLengths(const FlexLine& line, float hypotheticalSize) {
        size_t count = line.end - line.start;
        float totalGap = count > 0 ? float(count - 1) * mainGap_ : 0;
        float container = axes_.mainMax;
        if (container >= kInfinity) {
            float size = totalGap;
            for (size_t i = line.start; i < line.end; ++i) size += (items_[i].target = items_[i].hypothetical);
            return size;
        }
        bool growing = hypotheticalSize < container;
        int unfrozen = 0;
        float sumGrow = 0, sumScaledShrink = 0, sumBase = 0, sumFrozen = 0, sumFactors = 0;
        for (size_t i = line.start; i < line.end; ++i) {
            FlexItem& item = items_[i];
            float factor = growing ? item.config.grow : item.config.shrink;
            if (factor == 0 || (!growing && item.flexBase < item.hypothetical)) {
                item.target = item.hypothetical;
                item.frozen = true;
                sumFrozen += item.target;
            } else {
                item.frozen = false;
                ++unfrozen;
                sumBase += item.flexBase;
                sumFactors += factor;
                if (growing) sumGrow += item.config.grow;
                else sumScaledShrink += item.config.shrink * item.flexBase;
            }
        }
        float initialFree = container - totalGap - sumFrozen - sumBase, allocated = 0;
        auto freeSpace = [&] {
            float free = container - (totalGap + sumFrozen + sumBase);
            if (sumFactors < 1) {  // factors summing under 1 take that share of the room only
                float fractional = initialFree * sumFactors;
                if (std::abs(fractional) < std::abs(free)) free = fractional;
            }
            return free;
        };
        if (growing) {
            float free = freeSpace(), grow = sumGrow;
            for (size_t i = line.start; i < line.end; ++i) {
                FlexItem& item = items_[i];
                if (item.frozen) continue;
                float share = grow > 0 ? item.config.grow / grow : 0;
                float take = free * share;
                free -= take;
                grow -= item.config.grow;
                item.target = item.flexBase + take;
                allocated += item.target;
            }
        } else {
            bool again = true;
            for (size_t loop = 0; again && loop < count; ++loop) {
                again = false;
                allocated = 0;
                if (unfrozen == 0) break;
                float free = std::abs(freeSpace()), scaled = sumScaledShrink;
                for (size_t i = line.start; i < line.end; ++i) {
                    FlexItem& item = items_[i];
                    if (item.frozen) continue;
                    float itemScaled = item.config.shrink * item.flexBase;
                    float share = scaled > 0 ? itemScaled / scaled : 0;
                    float take = free * share, size = item.flexBase - take;
                    if (size < item.minMain) {  // as small as it goes: frozen, the others share the rest
                        item.frozen = true;
                        again = true;
                        free -= item.flexBase - item.minMain;
                        scaled -= itemScaled;
                        item.target = item.minMain;
                        --unfrozen;
                        sumFrozen += item.minMain;
                        sumBase -= item.flexBase;
                        sumScaledShrink -= itemScaled;
                        sumFactors -= item.config.shrink;
                    } else {
                        free -= take;
                        scaled -= itemScaled;
                        item.target = size;
                        allocated += size;
                    }
                }
            }
        }
        return totalGap + sumFrozen + allocated;
    }

    // A line's cross size before the items are measured for real (for stretching and baselines).
    void lineCrossSize(FlexLine& line, float remainingCross) {
        float size = 0, above = 0, below = 0;
        for (size_t i = line.start; i < line.end; ++i) {
            FlexItem& item = items_[i];
            float cross;
            if (item.config.alignSelf == FlexAlignSelf::Baseline ||
                (config_.alignItems == FlexAlignItems::Baseline && item.config.alignSelf == FlexAlignSelf::Auto)) {
                measureItem(item, 0, false, remainingCross);
                item.baseline = baselineOf(item);
                above = std::max(above, item.baseline);
                below = std::max(below, item.cross - item.baseline);
                cross = above + below;
            } else {
                detail::ProbeMeasure probe;
                Size s = item.node->measure(axes_.make(item.target, item.target, 0, kInfinity));
                cross = item.cross = axes_.crossOf(s);
            }
            size = std::max(size, cross);
        }
        line.maxAboveBaseline = above;
        line.crossSize = size;
    }

    float baselineOf(const FlexItem& item) const {
        if (!axes_.horizontal) return 0;  // baselines are horizontal lines: a column's items start their lines
        AlignmentLine line = item.config.baselineLine.value_or(config_.baselineLine.value_or(FirstBaseline));
        float at = item.node->alignmentLine(line);
        return std::isnan(at) ? item.cross : at;
    }

    // `align-content: stretch`: lines short of the box's least cross size share what's left.
    float stretchLines(float total) {
        if (config_.alignContent != FlexAlignContent::Stretch || axes_.crossMin >= kInfinity || lines_.size() < 2) return total;
        float spacing = float(lines_.size() - 1) * crossGap_;
        if (total + spacing >= axes_.crossMin) return total;
        float each = std::max(0.f, axes_.crossMin - total - spacing) / float(lines_.size()), at = 0;
        for (FlexLine& line : lines_) {
            line.crossStart = at;
            line.crossSize += each;
            at += line.crossSize + crossGap_;
            total += each;
        }
        return total;
    }

    void measureItem(FlexItem& item, float lineCross, bool stretch, float remainingCross) {
        Constraints c = stretch && lineCross > 0 ? axes_.make(item.target, item.target, std::min(lineCross, remainingCross),
                                                               std::min(lineCross, remainingCross))
                                                 : axes_.make(item.target, item.target, 0, remainingCross);
        Size s = item.node->measure(c);
        item.measured = true;
        item.main = axes_.mainOf(s);
        item.cross = axes_.crossOf(s);
    }

    float measureItems(float total) {
        float updated = total, remainingCross = axes_.crossMax;
        for (FlexLine& line : lines_) {
            float lineCross = upfront_ ? line.crossSize : 0;
            for (size_t i = line.start; i < line.end; ++i) {
                FlexItem& item = items_[i];
                if (item.measured) {
                    if (!upfront_) lineCross = std::max(lineCross, item.cross);
                    continue;
                }
                bool stretch = item.config.alignSelf == FlexAlignSelf::Stretch ||
                               (item.config.alignSelf == FlexAlignSelf::Auto &&
                                (config_.alignItems == FlexAlignItems::Stretch || config_.alignContent == FlexAlignContent::Stretch));
                measureItem(item, stretch ? line.crossSize : 0, stretch, remainingCross);
                if (!upfront_) lineCross = std::max(lineCross, item.cross);
                if (lines_.size() == 1) lineCross = std::max(lineCross, axes_.crossMin);
            }
            if (!upfront_) {
                line.crossSize = lineCross;
                updated += lineCross;
            }
            remainingCross = std::max(0.f, remainingCross - line.crossSize - crossGap_);
        }
        return upfront_ ? total : updated;
    }

    // The lines across the box by alignContent (WrapReverse: the last line first).
    void positionLines(float space, float total) {
        if (lines_.size() < 2) return;
        bool reverse = config_.wrap == FlexWrap::WrapReverse;
        float free = space - total - float(lines_.size() - 1) * crossGap_;
        float between = config_.alignContent == FlexAlignContent::SpaceAround    ? free / float(lines_.size())
                        : config_.alignContent == FlexAlignContent::SpaceBetween ? free / float(lines_.size() - 1)
                                                                                 : 0;
        float at = config_.alignContent == FlexAlignContent::End           ? (reverse ? 0 : free)
                   : config_.alignContent == FlexAlignContent::Center      ? free / 2
                   : config_.alignContent == FlexAlignContent::SpaceAround ? between / 2
                                                                           : (reverse ? free : 0);
        for (size_t k = 0; k < lines_.size(); ++k) {
            FlexLine& line = lines_[reverse ? lines_.size() - 1 - k : k];
            line.crossStart = at;
            at += line.crossSize + between + crossGap_;
        }
    }

    // A line's items along it by justifyContent, and each across it by its alignment.
    void positionOnMainAxis(const FlexLine& line, float containerMain) {
        size_t count = line.end - line.start;
        if (count == 0) return;
        bool reverse = config_.direction == FlexDirection::RowReverse || config_.direction == FlexDirection::ColumnReverse;
        float remaining = containerMain - line.mainSize;
        FlexJustifyContent j = config_.justifyContent;
        float between = j == FlexJustifyContent::SpaceAround    ? remaining / float(count)
                        : j == FlexJustifyContent::SpaceBetween ? (count > 1 ? remaining / float(count - 1) : 0)
                        : j == FlexJustifyContent::SpaceEvenly  ? remaining / float(count + 1)
                                                                : 0;
        float at = j == FlexJustifyContent::End            ? (reverse ? 0 : remaining)
                   : j == FlexJustifyContent::Center       ? remaining / 2
                   : j == FlexJustifyContent::SpaceAround  ? between / 2
                   : j == FlexJustifyContent::SpaceEvenly  ? between
                   : j == FlexJustifyContent::SpaceBetween ? (count == 1 && reverse ? remaining : 0)
                                                           : (reverse ? remaining : 0);
        for (size_t k = 0; k < count; ++k) {
            FlexItem& item = items_[reverse ? line.end - 1 - k : line.start + k];
            item.mainPosition = at;
            at += item.target + between + mainGap_;
            FlexAlignSelf align = item.config.alignSelf;
            if (align == FlexAlignSelf::Auto) {
                switch (config_.alignItems) {
                case FlexAlignItems::Start: align = FlexAlignSelf::Start; break;
                case FlexAlignItems::Center: align = FlexAlignSelf::Center; break;
                case FlexAlignItems::End: align = FlexAlignSelf::End; break;
                case FlexAlignItems::Stretch: align = FlexAlignSelf::Stretch; break;
                case FlexAlignItems::Baseline: align = FlexAlignSelf::Baseline; break;
                }
            }
            float offset = align == FlexAlignSelf::End      ? line.crossSize - item.cross
                           : align == FlexAlignSelf::Center ? (line.crossSize - item.cross) / 2
                           : align == FlexAlignSelf::Baseline && !std::isnan(item.baseline) ? line.maxAboveBaseline - item.baseline
                                                                                            : 0;
            item.crossPosition = line.crossStart + offset;
        }
    }

    LayoutNode& node_;
    const FlexBoxConfig& config_;
    FlexAxes axes_;
    Constraints constraints_;
    float mainGap_ = 0, crossGap_ = 0;
    bool upfront_ = false;
    std::vector<FlexItem> items_;
    std::vector<FlexLine> lines_;
};

void flexBox(const CallSite& site, const Modifier& modifier, std::function<FlexBoxConfig(const Constraints&)> config, const Content& content) {
    detail::Composer::require().emit(
        detail::keyOf(site),
        [&](LayoutNode& node) {
            node.modifier = modifier;
            node.measurePolicy = [config](LayoutNode& n, const Constraints& c) {
                FlexBoxConfig resolved = config(c);
                return FlexMeasure(n, c, resolved).measure();
            };
            node.drawContent = {};
        },
        content);
}

} // namespace

void FlexBox(const Modifier& modifier, const FlexBoxConfig& config, const Content& content, CallSite site) {
    flexBox(site, modifier, [config](const Constraints&) { return config; }, content);
}

void FlexBox(const Modifier& modifier, std::function<FlexBoxConfig(const Constraints&)> config, const Content& content, CallSite site) {
    flexBox(site, modifier, std::move(config), content);
}

} // namespace kompoz
