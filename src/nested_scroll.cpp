#include <kompoz/host.hpp>
#include <kompoz/nested_scroll.hpp>
#include <kompoz/state.hpp>

namespace kompoz {

namespace {

Offset plus(Offset a, Offset b) { return {a.x + b.x, a.y + b.y}; }
Offset minus(Offset a, Offset b) { return {a.x - b.x, a.y - b.y}; }
Velocity plus(Velocity a, Velocity b) { return {a.x + b.x, a.y + b.y}; }
Velocity minus(Velocity a, Velocity b) { return {a.x - b.x, a.y - b.y}; }

class HandlerConnection final : public NestedScrollConnection {
public:
    explicit HandlerConnection(NestedScrollHandlers h) : handlers(std::move(h)) {}
    Offset onPreScroll(Offset available, NestedScrollSource source) override {
        return handlers.onPreScroll ? handlers.onPreScroll(available, source) : Offset{};
    }
    Offset onPostScroll(Offset consumed, Offset available, NestedScrollSource source) override {
        return handlers.onPostScroll ? handlers.onPostScroll(consumed, available, source) : Offset{};
    }
    Velocity onPreFling(Velocity available) override {
        return handlers.onPreFling ? handlers.onPreFling(available) : Velocity{};
    }
    Velocity onPostFling(Velocity consumed, Velocity available) override {
        return handlers.onPostFling ? handlers.onPostFling(consumed, available) : Velocity{};
    }
    NestedScrollHandlers handlers;
};

} // namespace

std::shared_ptr<NestedScrollConnection> nestedScrollConnection(NestedScrollHandlers handlers) {
    return std::make_shared<HandlerConnection>(std::move(handlers));
}

std::shared_ptr<NestedScrollConnection> rememberNestedScrollConnection(NestedScrollHandlers handlers, CallSite site) {
    auto& connection = remember([] { return std::make_shared<HandlerConnection>(NestedScrollHandlers{}); }, site);
    connection->handlers = std::move(handlers);
    return connection;
}

struct NestedScrollDispatcher::Data {
    std::weak_ptr<LayoutNode*> node;  // where its nestedScroll() modifier is
    const ModifierElement* element = nullptr;
};

NestedScrollDispatcher::NestedScrollDispatcher() : data_(std::make_shared<Data>()) {}

namespace {

// The dispatcher's parents, or none while its modifier isn't laid out.
std::optional<detail::NestedScrollChain> chainOf(const NestedScrollDispatcher::Data& d) {
    auto handle = d.node.lock();
    LayoutNode* node = handle ? *handle : nullptr;
    if (!node) return std::nullopt;
    const auto& elements = node->modifier.elements();
    for (size_t i = 0; i < elements.size(); ++i)
        if (elements[i].get() == d.element) return detail::NestedScrollChain(*node, i);
    return std::nullopt;
}

} // namespace

Offset NestedScrollDispatcher::dispatchPreScroll(Offset available, NestedScrollSource source) const {
    auto chain = chainOf(*data_);
    return chain ? chain->preScroll(available, source) : Offset{};
}

Offset NestedScrollDispatcher::dispatchPostScroll(Offset consumed, Offset available, NestedScrollSource source) const {
    auto chain = chainOf(*data_);
    return chain ? chain->postScroll(consumed, available, source) : Offset{};
}

Velocity NestedScrollDispatcher::dispatchPreFling(Velocity available) const {
    auto chain = chainOf(*data_);
    return chain ? chain->preFling(available) : Velocity{};
}

Velocity NestedScrollDispatcher::dispatchPostFling(Velocity consumed, Velocity available) const {
    auto chain = chainOf(*data_);
    return chain ? chain->postFling(consumed, available) : Velocity{};
}

void NestedScrollElement::attach(LayoutNode& node) const {
    if (!dispatcher) return;
    dispatcher->node = node.handle();
    dispatcher->element = this;
}

namespace {
// imeNestedScroll's connection: the host keeps the keyboard's state (a recomposition mid-drag
// makes a new one of these).
class ImeNestedScrollConnection final : public NestedScrollConnection {
public:
    explicit ImeNestedScrollConnection(ComposeHost& host) : host_(&host) {}
    Offset onPreScroll(Offset available, NestedScrollSource source) override {
        // Pushed down while the keyboard is under the finger: it goes down first.
        if (source != NestedScrollSource::UserInput || !host_->imeControlling() || available.y <= 0) return {};
        return {0, -host_->imeControlBy(-available.y)};
    }
    Offset onPostScroll(Offset, Offset available, NestedScrollSource source) override {
        // Pulled on up past the end: it comes up with the finger.
        if (source != NestedScrollSource::UserInput || available.y >= 0) return {};
        return {0, -host_->imeControlBy(-available.y)};
    }
    Velocity onPreFling(Velocity available) override {
        if (!host_->imeControlling()) return {};
        host_->imeControlEnd(-available.y);
        return available;  // the fling was the keyboard's
    }

private:
    ComposeHost* host_;
};
} // namespace

Modifier Modifier::imeNestedScroll() const {
    return nestedScroll(std::make_shared<ImeNestedScrollConnection>(detail::Composer::require().host()));
}

Modifier Modifier::nestedScroll(std::shared_ptr<NestedScrollConnection> connection) const {
    auto e = std::make_shared<NestedScrollElement>();
    e->connection = std::move(connection);
    return then(std::move(e));
}

Modifier Modifier::nestedScroll(std::shared_ptr<NestedScrollConnection> connection,
                                const NestedScrollDispatcher& dispatcher) const {
    auto e = std::make_shared<NestedScrollElement>();
    e->connection = std::move(connection);
    e->dispatcher = dispatcher.data_;
    return then(std::move(e));
}

namespace detail {

NestedScrollChain::NestedScrollChain(const LayoutNode& node, size_t index) {
    auto add = [&](const std::shared_ptr<const ModifierElement>& e, const LayoutNode& owner) {
        if (auto* n = dynamic_cast<const NestedScrollElement*>(e.get()); n && n->connection)
            links_.push_back({e, n->connection.get(), nullptr});
        else if (auto* s = dynamic_cast<const ScrollableElement*>(e.get()); s && s->userScrollEnabled)  // a disabled one takes none
            links_.push_back({e, nullptr, s, owner.rtl && s->orientation() == Orientation::Horizontal && s->followsLayoutDirection()});
    };
    const auto& own = node.modifier.elements();
    for (size_t i = std::min(index, own.size()); i-- > 0;) add(own[i], node);
    // Popups take part in their own window only.
    for (const LayoutNode* n = node.overlay ? nullptr : node.parent; n; n = n->overlay ? nullptr : n->parent) {
        const auto& elements = n->modifier.elements();
        for (size_t i = elements.size(); i-- > 0;) add(elements[i], *n);
    }
}

// Pre-scroll asks the outermost parent first; post-scroll the innermost.
Offset NestedScrollChain::preScroll(Offset available, NestedScrollSource source) const {
    Offset used;
    for (auto it = links_.rbegin(); it != links_.rend(); ++it)
        if (it->connection) used = plus(used, it->connection->onPreScroll(minus(available, used), source));
    return used;
}

Offset NestedScrollChain::postScroll(Offset consumed, Offset available, NestedScrollSource source) const {
    Offset used;
    for (const Link& link : links_) {
        Offset left = minus(available, used), took;
        if (link.connection) {
            took = link.connection->onPostScroll(plus(consumed, used), left, source);
        } else {
            // A scrollable parent scrolls with what is left along its axis (the scroll is
            // towards the end when the finger moves towards the start).
            bool vertical = link.scrollable->orientation() == Orientation::Vertical;
            float delta = vertical ? left.y : left.x;
            if (delta != 0) {
                float sign = link.reversed ? -1.f : 1.f;
                float scrolled = -sign * link.scrollable->scrollByNested(-sign * delta, source);
                took = vertical ? Offset{0, scrolled} : Offset{scrolled, 0};
            }
        }
        used = plus(used, took);
    }
    return used;
}

Velocity NestedScrollChain::preFling(Velocity available) const {
    Velocity used;
    for (auto it = links_.rbegin(); it != links_.rend(); ++it)
        if (it->connection) used = plus(used, it->connection->onPreFling(minus(available, used)));
    return used;
}

Velocity NestedScrollChain::postFling(Velocity consumed, Velocity available) const {
    Velocity used;
    for (const Link& link : links_) {
        Velocity left = minus(available, used), took;
        if (link.connection) {
            took = link.connection->onPostFling(plus(consumed, used), left);
        } else {
            // The scroll steps of the fling already reached it; one that settles by itself
            // (a pager) does that now.
            bool vertical = link.scrollable->orientation() == Orientation::Vertical;
            float v = vertical ? left.y : left.x;
            if (link.scrollable->settle(link.reversed ? v : -v)) took = vertical ? Velocity{0, v} : Velocity{v, 0};
        }
        used = plus(used, took);
    }
    return used;
}

} // namespace detail

} // namespace kompoz
