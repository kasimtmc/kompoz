#include <kompoz/host.hpp>
#include <kompoz/shared_transition.hpp>
#include <kompoz/widgets.hpp>

#include <optional>
#include <vector>

namespace kompoz {

namespace detail {

struct SharedRegistry {
    ComposeHost* host = nullptr;
    std::vector<std::weak_ptr<SharedInstance>> instances;
    // Nodes drawn in the overlay while they say so (renderInSharedTransitionScopeOverlay).
    struct OverlayRender {
        std::weak_ptr<LayoutNode*> node;
        const ModifierElement* element = nullptr;
        std::function<bool()> when;  // unset: while a shared element moves
        float zIndex = 0;
    };
    std::vector<OverlayRender> renders;
    int64_t now() const { return host ? host->frameTime() : 0; }
    bool moving() const;
};

// One rememberSharedContentState: an element with a key, in content that enters or leaves.
struct SharedInstance {
    std::weak_ptr<SharedRegistry> registry;
    std::string key;
    std::shared_ptr<VisibilityState> visibility;  // of the content it is in
    AnimationSpec spec = defaultBoundsTransform();
    std::optional<Rect> placed;  // where it belongs (absolute), as last drawn
    // Moving to where it belongs, after its content started entering:
    int64_t start = -1;
    Rect from;
    std::optional<Rect> drawnAt;  // where it was drawn in the overlay
    bool settled = false;         // it moved already, or had nothing to move from
    bool wasVisible = true;
    // Its modifier element and the node carrying it: the overlay draws it from there.
    std::weak_ptr<LayoutNode*> node;
    const ModifierElement* element = nullptr;
    bool inOverlay = true;  // drawn in the overlay while it moves (else where it is)
    float zIndex = 0;       // its place among what the overlay draws
};

bool SharedRegistry::moving() const {
    for (const auto& weak : instances)
        if (auto i = weak.lock(); i && i->start >= 0) return true;
    return false;
}

} // namespace detail

namespace {

using detail::SharedInstance;
using detail::SharedRegistry;

bool isVisible(const SharedInstance& i) { return !i.visibility || i.visibility->visible; }

// The element with the same key in the content going the other way (the latest one).
std::shared_ptr<SharedInstance> partnerOf(const SharedRegistry& r, const SharedInstance& self) {
    std::shared_ptr<SharedInstance> found;
    for (const auto& weak : r.instances) {
        auto other = weak.lock();
        if (other && other.get() != &self && other->key == self.key && isVisible(*other) != isVisible(self)) found = other;
    }
    return found;
}

Rect lerpRect(const Rect& a, const Rect& b, float t) {
    return {lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.width, b.width, t), lerp(a.height, b.height, t)};
}

// An entering element starts moving on its first layout, from where its match is (or was
// drawn, if that one was itself still moving).
void maybeStart(const SharedRegistry& r, SharedInstance& self, int64_t now) {
    if (!isVisible(self)) {
        self.wasVisible = false;
        return;
    }
    if (!self.wasVisible) self.wasVisible = true, self.settled = false;
    if (self.start >= 0 || self.settled) return;
    auto partner = partnerOf(r, self);
    if (!self.visibility || !self.visibility->running || !partner || !partner->placed) {  // shown as it is
        self.settled = true;
        return;
    }
    self.start = now;
    self.from = partner->start >= 0 && partner->drawnAt ? *partner->drawnAt : *partner->placed;
}

// How far an entering element has moved (0..1), or nothing once it is where it belongs.
std::optional<float> progressOf(SharedInstance& self, int64_t now) {
    if (self.start < 0) return std::nullopt;
    bool finished = false;
    float t = self.spec.progress(float(now - self.start) / 1e6f, finished);
    if (finished) {
        self.start = -1;
        self.settled = true;
        self.drawnAt.reset();
        return std::nullopt;
    }
    return t;
}

// How a shared element is drawn this frame: where it belongs, not at all (its match is drawn
// instead), or in the overlay at a moving rect.
struct Look {
    enum Kind { InPlace, Hidden, Overlay } kind = InPlace;
    Rect at;
    float alpha = 1;
};

Look lookOf(const SharedRegistry& r, SharedInstance& self, bool bounds, const Rect& placed, int64_t now) {
    if (isVisible(self)) {
        auto t = progressOf(self, now);
        if (!t) return {};
        return {Look::Overlay, lerpRect(self.from, placed, *t), bounds ? *t : 1.f};
    }
    auto partner = partnerOf(r, self);
    if (!partner) return {};                  // nothing replaces it: it leaves as its content does
    if (!bounds) return {Look::Hidden};       // the entering one is drawn instead
    if (!partner->placed) return {};          // the very first frame: still where it was
    auto t = progressOf(*partner, now);
    if (!t) return {Look::Hidden};
    return {Look::Overlay, lerpRect(partner->from, *partner->placed, *t), 1 - *t};  // in its partner's bounds
}

// How much a content of `src` size is scaled into `dst` (Compose's ContentScale factor).
std::pair<float, float> boundsScale(Size src, Size dst, ContentScale scale) {
    if (src.width <= 0 || src.height <= 0) return {1, 1};
    float w = dst.width / src.width, h = dst.height / src.height;
    switch (scale) {
    case ContentScale::Fit: return {std::min(w, h), std::min(w, h)};
    case ContentScale::Crop: return {std::max(w, h), std::max(w, h)};
    case ContentScale::FillBounds: return {w, h};
    case ContentScale::Inside: return w >= 1 && h >= 1 ? std::pair{1.f, 1.f} : std::pair{std::min(w, h), std::min(w, h)};
    case ContentScale::None: return {1, 1};
    case ContentScale::FillWidth: return {w, w};
    case ContentScale::FillHeight: return {h, h};
    }
    return {w, w};
}

struct SharedElementModifier;
// Draws a moving shared element at `look` (from where it was laid out, `placed`): its own size
// scaled into the moving bounds (sharedBounds) or laid out at the in-between size and moved.
void drawMoving(GraphicsContext& canvas, const SharedElementModifier& element, const Look& look, const Rect& placed,
                const std::function<void()>& drawIt);

struct SharedElementModifier final : ModifierElement {
    std::shared_ptr<SharedInstance> instance;
    std::shared_ptr<detail::VisibilityState> visibility;
    bool bounds = false;  // sharedBounds: the leaving one is shown too, fading out
    SharedResizeMode resize;  // sharedBounds': scaled to the bounds or laid out at their size

    void attach(LayoutNode& node) const override {
        instance->node = node.handle();
        instance->element = this;
    }

    Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const override {
        contentOffset = {};
        Size target;
        {
            detail::InLayoutPass lookahead(detail::LayoutPass::Lookahead);  // where it goes
            target = next(c);
        }
        auto r = instance->registry.lock();
        if (!r) return target;
        int64_t now = r->now();
        maybeStart(*r, *instance, now);
        if (!isVisible(*instance) && instance->visibility)  // its match still moves: its content stays a little longer
            if (auto partner = partnerOf(*r, *instance); partner && partner->start >= 0)
                instance->visibility->holdUntil = std::max(instance->visibility->holdUntil, now + 100'000'000);
        // Laid out at the in-between size while it moves (the place: where it belongs, for now).
        Look look = lookOf(*r, *instance, bounds, instance->placed.value_or(Rect{0, 0, target.width, target.height}), now);
        if (look.kind == Look::Overlay && (!bounds || resize.remeasure)) {  // sharedBounds scales instead, unless it remeasures
            Size size{look.at.width, look.at.height};
            if (isVisible(*instance) && instance->start >= 0) {  // towards this layout's size, not the last one's
                bool finished = false;
                float t = instance->spec.progress(float(now - instance->start) / 1e6f, finished);
                size = {lerp(instance->from.width, target.width, t), lerp(instance->from.height, target.height, t)};
            }
            detail::InLayoutPass approach(detail::LayoutPass::Approach);
            next(Constraints::fixed(std::max(0.f, size.width), std::max(0.f, size.height)));
        }
        return target;  // the layout around it sees where it belongs
    }

    void draw(GraphicsContext& canvas, const Rect& b, const LayoutNode&, const std::function<void()>& drawContent) const override {
        auto r = instance->registry.lock();
        if (!r) return drawContent();
        instance->placed = b;
        Look look = lookOf(*r, *instance, bounds, b, r->now());
        if (look.kind == Look::InPlace) return drawContent();
        if (look.kind == Look::Overlay && !instance->inOverlay) {  // moving, but where it is in the content
            if (isVisible(*instance)) instance->drawnAt = look.at;
            drawMoving(canvas, *this, look, b, drawContent);
        }  // else: the overlay or nothing
    }

    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const SharedElementModifier*>(&other);
        return o && o->instance == instance && o->visibility == visibility && o->bounds == bounds && o->resize == resize;
    }
};

void drawMoving(GraphicsContext& canvas, const SharedElementModifier& element, const Look& look, const Rect& placed,
                const std::function<void()>& drawIt) {
    if (element.bounds && !element.resize.remeasure) {
        // Laid out at its own size, scaled into the moving bounds by the content scale and
        // placed by the alignment (Compose's ScaleToBounds; FillWidth: text doesn't reflow).
        auto [sx, sy] = boundsScale({placed.width, placed.height}, {look.at.width, look.at.height}, element.resize.contentScale);
        Offset at = element.resize.alignment.align({placed.width * sx, placed.height * sy}, {look.at.width, look.at.height});
        canvas.pushLayer(placed, 0, look.alpha,
                         {.scaleX = sx, .scaleY = sy, .translationX = look.at.x + at.x - placed.x, .translationY = look.at.y + at.y - placed.y,
                          .origin = {0, 0}});
    } else {
        // Laid out where it belongs at the in-between size; moved over to the in-between place.
        canvas.pushLayer({placed.x, placed.y, look.at.width, look.at.height}, 0, look.alpha,
                         {.translationX = look.at.x - placed.x, .translationY = look.at.y - placed.y});
    }
    drawIt();
    canvas.popLayer();
}

// On the SharedTransitionLayout: after its content, draws the moving shared elements above it,
// from their nodes (wherever they are, even in a parent faded out), and the nodes that are drawn
// there for now (renderInSharedTransitionScopeOverlay), ordered by their zIndexInOverlay.
struct SharedOverlayElement final : ModifierElement {
    std::shared_ptr<SharedRegistry> registry;
    void draw(GraphicsContext& canvas, const Rect&, const LayoutNode&, const std::function<void()>& drawContent) const override {
        // Where each one belongs this frame, before any is drawn: what one draws from its partner's
        // is the same however many times the frame is drawn (a magnifier's, a check's).
        for (const auto& weak : registry->instances)
            if (auto self = weak.lock(); self && self->element)
                if (auto handle = self->node.lock(); handle && *handle)
                    if (std::optional<Rect> placed = (*handle)->boundsOf(self->element)) self->placed = placed;
        drawContent();
        int64_t now = registry->now();
        bool moving = false;
        struct Item {
            float zIndex;
            LayoutNode* node;
            const ModifierElement* element;
            std::optional<Look> look;  // a moving shared element's (none: a node drawn there as it is)
            Rect placed;
        };
        std::vector<Item> items;
        for (const auto& weak : registry->instances) {
            auto self = weak.lock();
            if (!self || !self->element) continue;
            auto handle = self->node.lock();
            if (!handle || !*handle) continue;
            LayoutNode& node = **handle;
            std::optional<Rect> placed = node.boundsOf(self->element);
            if (!placed) continue;
            self->placed = placed;
            Look look = lookOf(*registry, *self, static_cast<const SharedElementModifier*>(self->element)->bounds, *placed, now);
            if (look.kind != Look::Overlay) continue;
            moving = true;
            if (!self->inOverlay) continue;  // drawn where it is
            if (isVisible(*self)) self->drawnAt = look.at;
            items.push_back({self->zIndex, &node, self->element, look, *placed});
        }
        std::erase_if(registry->renders, [](const auto& r) { return r.node.expired(); });
        for (const auto& r : registry->renders)
            if (auto handle = r.node.lock(); handle && *handle && (r.when ? r.when() : registry->moving()))
                items.push_back({r.zIndex, *handle, r.element, std::nullopt, {}});
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.zIndex < b.zIndex; });
        for (const Item& item : items) {
            if (item.look)
                drawMoving(canvas, *static_cast<const SharedElementModifier*>(item.element), *item.look, item.placed,
                           [&] { item.node->drawAfter(canvas, item.element); });
            else
                item.node->drawAfter(canvas, item.element);
        }
        if (moving && registry->host) registry->host->requestLayout();  // the next frame lays them out further along
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const SharedOverlayElement*>(&other);
        return o && o->registry == registry;
    }
};

// renderInSharedTransitionScopeOverlay(): not drawn where it is while the overlay draws it.
struct RenderInOverlayElement final : ModifierElement {
    std::shared_ptr<SharedRegistry> registry;
    std::function<bool()> when;
    float zIndex = 0;
    bool inOverlay() const { return when ? when() : registry->moving(); }
    void attach(LayoutNode& node) const override {
        auto handle = node.handle();
        auto it = std::find_if(registry->renders.begin(), registry->renders.end(), [&](const auto& r) { return r.node.lock() == handle.lock(); });
        SharedRegistry::OverlayRender& entry = it != registry->renders.end() ? *it : registry->renders.emplace_back();
        entry = {handle, this, when, zIndex};
    }
    void draw(GraphicsContext&, const Rect&, const LayoutNode&, const std::function<void()>& drawContent) const override {
        if (!inOverlay()) drawContent();
    }
};

Modifier shared(const Modifier& m, const SharedContentState& state, const AnimatedVisibilityScope& scope,
                const AnimationSpec& spec, bool bounds, SharedResizeMode resize = {}, bool inOverlay = true, float zIndex = 0) {
    state.instance()->visibility = scope.state();
    state.instance()->spec = spec;
    state.instance()->inOverlay = inOverlay;
    state.instance()->zIndex = zIndex;
    auto e = std::make_shared<SharedElementModifier>();
    e->instance = state.instance();
    e->visibility = scope.state();
    e->bounds = bounds;
    e->resize = resize;
    return m.then(std::move(e));
}

} // namespace

const std::string& SharedContentState::key() const { return instance_->key; }

bool SharedContentState::isMatchFound() const {
    auto r = instance_->registry.lock();
    return r && partnerOf(*r, *instance_) != nullptr;
}

SharedContentState SharedTransitionScope::rememberSharedContentState(std::string key, CallSite site) const {
    auto registry = registry_;
    auto& instance = remember(
        key,
        [&] {
            auto i = std::make_shared<SharedInstance>();
            i->key = key;
            i->registry = registry;
            std::erase_if(registry->instances, [](const auto& w) { return w.expired(); });
            registry->instances.push_back(i);
            return i;
        },
        site);
    return SharedContentState(instance);
}

bool SharedTransitionScope::isTransitionActive() const { return registry_->moving(); }

void SharedTransitionLayout(const Modifier& modifier, const std::function<void(const SharedTransitionScope&)>& content,
                            CallSite site) {
    ComposableScope scope(site);
    auto registry = remember([] { return std::make_shared<SharedRegistry>(); });
    registry->host = &detail::Composer::require().host();
    auto overlay = std::make_shared<SharedOverlayElement>();
    overlay->registry = registry;
    SharedTransitionScope shared(registry);
    Box(modifier.then(std::move(overlay)), [&] { content(shared); });
}

Modifier Modifier::sharedElement(const SharedContentState& state, const AnimatedVisibilityScope& scope) const {
    return shared(*this, state, scope, defaultBoundsTransform(), false);
}
Modifier Modifier::sharedElement(const SharedContentState& state, const AnimatedVisibilityScope& scope,
                                 const AnimationSpec& boundsTransform) const {
    return shared(*this, state, scope, boundsTransform, false);
}
Modifier Modifier::sharedBounds(const SharedContentState& state, const AnimatedVisibilityScope& scope) const {
    return shared(*this, state, scope, defaultBoundsTransform(), true);
}
Modifier Modifier::sharedBounds(const SharedContentState& state, const AnimatedVisibilityScope& scope,
                                const AnimationSpec& boundsTransform) const {
    return shared(*this, state, scope, boundsTransform, true);
}
Modifier Modifier::sharedBounds(const SharedContentState& state, const AnimatedVisibilityScope& scope, const AnimationSpec& boundsTransform,
                                const SharedResizeMode& resizeMode) const {
    return shared(*this, state, scope, boundsTransform, true, resizeMode);
}
Modifier Modifier::sharedElement(const SharedContentState& state, const AnimatedVisibilityScope& scope, const AnimationSpec& boundsTransform,
                                 bool renderInOverlayDuringTransition, float zIndexInOverlay) const {
    return shared(*this, state, scope, boundsTransform, false, {}, renderInOverlayDuringTransition, zIndexInOverlay);
}
Modifier Modifier::sharedBounds(const SharedContentState& state, const AnimatedVisibilityScope& scope, const AnimationSpec& boundsTransform,
                                const SharedResizeMode& resizeMode, bool renderInOverlayDuringTransition, float zIndexInOverlay) const {
    return shared(*this, state, scope, boundsTransform, true, resizeMode, renderInOverlayDuringTransition, zIndexInOverlay);
}
Modifier Modifier::renderInSharedTransitionScopeOverlay(const SharedTransitionScope& scope, std::function<bool()> renderInOverlay,
                                                        float zIndexInOverlay) const {
    auto e = std::make_shared<RenderInOverlayElement>();
    e->registry = scope.registry();
    e->when = std::move(renderInOverlay);
    e->zIndex = zIndexInOverlay;
    return then(std::move(e));
}

} // namespace kompoz
