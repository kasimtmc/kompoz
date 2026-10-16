#include <kompoz/flow.hpp>
#include <kompoz/interaction.hpp>
#include <kompoz/runtime.hpp>

#include <algorithm>
#include <atomic>
#include <vector>

namespace kompoz {

namespace {
uint64_t nextId() {
    static std::atomic<uint64_t> next{1};
    return next++;
}
} // namespace

Interaction Interaction::press(Offset position) { return {Type::Press, nextId(), position}; }
Interaction Interaction::hoverEnter() { return {Type::HoverEnter, nextId(), {}}; }
Interaction Interaction::focus() { return {Type::Focus, nextId(), {}}; }
Interaction Interaction::dragStart() { return {Type::DragStart, nextId(), {}}; }

struct detail::InteractionData {
    MutableSharedFlow<Interaction> flow;
    struct Kind {
        std::vector<Interaction> going;  // started, not ended
        MutableState<bool> on{false};
    } presses, hovers, focuses, drags;
    std::vector<detail::PressTrack> tracks;  // followPresses()
};

MutableInteractionSource::MutableInteractionSource() : data_(std::make_shared<detail::InteractionData>()) {}
SharedFlow<Interaction> MutableInteractionSource::interactions() const { return data_->flow; }

void MutableInteractionSource::tryEmit(const Interaction& interaction) const {
    using T = Interaction::Type;
    auto& d = *data_;
    auto& kind = interaction.type == T::Press || interaction.type == T::Release || interaction.type == T::Cancel ? d.presses
                 : interaction.type == T::HoverEnter || interaction.type == T::HoverExit                      ? d.hovers
                 : interaction.type == T::Focus || interaction.type == T::Unfocus                             ? d.focuses
                                                                                                              : d.drags;
    if (interaction.isStart()) kind.going.push_back(interaction);
    else std::erase_if(kind.going, [&](const Interaction& i) { return i.id == interaction.id; });
    if (kind.on.peek() != !kind.going.empty()) kind.on.set(!kind.going.empty());
    d.flow.tryEmit(interaction);
}

bool MutableInteractionSource::collectIsPressedAsState() const { return data_->presses.on.get(); }
bool MutableInteractionSource::collectIsHoveredAsState() const { return data_->hovers.on.get(); }
bool MutableInteractionSource::collectIsFocusedAsState() const { return data_->focuses.on.get(); }
bool MutableInteractionSource::collectIsDraggedAsState() const { return data_->drags.on.get(); }
std::vector<detail::PressTrack> detail::followPresses(const MutableInteractionSource& source, int64_t now,
                                                     const std::function<bool(const PressTrack&)>& done) {
    auto& d = *source.data_;
    for (const Interaction& press : d.presses.going)
        if (std::ranges::none_of(d.tracks, [&](const PressTrack& t) { return t.id == press.id; }))
            d.tracks.push_back({press.id, press.pressPosition, now, -1});
    for (PressTrack& t : d.tracks)
        if (t.released < 0 && std::ranges::none_of(d.presses.going, [&](const Interaction& i) { return i.id == t.id; })) t.released = now;
    std::erase_if(d.tracks, done);
    return d.tracks;
}

Offset MutableInteractionSource::pressPosition() const {
    return data_->presses.going.empty() ? Offset{} : data_->presses.going.back().pressPosition;
}

} // namespace kompoz
