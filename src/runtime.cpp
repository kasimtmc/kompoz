#include <kompoz/host.hpp>
#include <kompoz/runtime.hpp>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace kompoz::detail {

namespace {

thread_local Composer* gCurrent = nullptr;

// The layout nodes directly below `g`, in call order, looking through groups that don't
// emit a node themselves. Popup roots go to their own list: they don't take part in the layout.
void collectNodes(const Group& g, std::vector<LayoutNode*>& out, std::vector<LayoutNode*>& overlays) {
    for (const Group* child : g.order) {
        if (child->node)
            (child->node->overlay ? overlays : out).push_back(child->node.get());
        else
            collectNodes(*child, out, overlays);
    }
}

void rebuildChildren(Group& nodeGroup) {
    nodeGroup.node->children.clear();
    nodeGroup.node->overlays.clear();
    collectNodes(nodeGroup, nodeGroup.node->children, nodeGroup.node->overlays);
    for (auto* child : nodeGroup.node->children) child->parent = nodeGroup.node.get();
    for (auto* popup : nodeGroup.node->overlays) popup->parent = nodeGroup.node.get();
    nodeGroup.node->invalidateMeasure();
}

bool isInside(const Group& g, const Group& ancestor) {
    for (const Group* p = &g; p; p = p->parent)
        if (p == &ancestor) return true;
    return false;
}

} // namespace

uint64_t mixKey(uint64_t a, uint64_t b) {
    uint64_t h = a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return h;
}

uint64_t keyOf(const CallSite& site) {
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a over the file name
    for (const char* p = site.file_name(); *p; ++p) h = (h ^ static_cast<unsigned char>(*p)) * 0x100000001b3ULL;
    return mixKey(h, (uint64_t(site.line()) << 32) | site.column());
}

void recordRead(const void* key) {
    if (Composer* c = gCurrent; c && c->recording())
        c->recordRead(key);
    else if (LayoutNode* node = measuringNode(); node && node->host())  // read by a layout: changed, it's measured again
        node->host()->recordLayoutRead(key, *node);
}

Scope::~Scope() { composer.forget(*this); }

Group::~Group() {
    children.clear();  // descendants are forgotten first
    if (onForget) onForget();
    scope.reset();
    if (movable && movable->group == this) movable->group = nullptr;
}

Composer* Composer::current() { return gCurrent; }

Composer& Composer::require() {
    if (!gCurrent) {
        std::fputs("kompoz: composable called outside of a composition\n", stderr);
        std::abort();
    }
    return *gCurrent;
}

void Composer::setRoot(Group& root, std::function<void()> content) {
    root_ = &root;
    root.scope = std::make_unique<Scope>(*this, &root);
    root.scope->body = std::move(content);
    invalidateScope(*root.scope);
}

void Composer::invalidateRoot() {
    if (root_ && root_->scope) invalidateScope(*root_->scope);
}

void Composer::invalidateScope(Scope& scope) {
    if (scope.onInvalidated) {  // an observer (derived state): recompute instead of recompose
        auto onInvalidated = scope.onInvalidated;
        onInvalidated();
        return;
    }
    if (scope.invalid) return;
    scope.invalid = true;
    invalid_.push_back(&scope);
}

void Composer::invalidateReaders(const void* key, const Group* within) {
    if (!within) host_.invalidateLayoutReaders(key);
    auto it = readers_.find(key);
    if (it == readers_.end()) return;
    for (Scope* s : std::vector<Scope*>(it->second))
        if (!within || (s->group && isInside(*s->group, *within))) invalidateScope(*s);
}

void Composer::invalidateFrameScopes() {
    for (Scope* s : std::exchange(frameScopes_, {})) invalidateScope(*s);
}

void Composer::requestFrame() {
    if (currentScope_ && std::find(frameScopes_.begin(), frameScopes_.end(), currentScope_) == frameScopes_.end())
        frameScopes_.push_back(currentScope_);
}

bool Composer::recompose(int64_t frameTimeNanos) {
    if (invalid_.empty()) return false;
    Composer* previous = std::exchange(gCurrent, this);
    frameTime_ = frameTimeNanos;
    effects_.clear();
    ++stats_.recompositions;
    ++pass_;

    // Outermost first: running a scope also runs (or skips) the scopes inside it, which
    // clears their invalid flag, so they are not run twice.
    while (!invalid_.empty()) {
        auto it = std::min_element(invalid_.begin(), invalid_.end(),
                                   [](const Scope* a, const Scope* b) { return a->group->depth < b->group->depth; });
        Scope* s = *it;
        invalid_.erase(it);
        if (s->invalid) restart(*s);
    }
    parked_.clear();  // movable contents not called again: they leave the composition
    detail::settleSaveables();

    gCurrent = previous;
    // Effects run after the composition is complete, outside of it.
    auto effects = std::move(effects_);
    effects_.clear();
    for (auto& effect : effects) effect();
    return true;
}

// Runs a scope on its own, without its parent: the composition locals come from the
// providers above it, and the node it emits into gets its children list rebuilt.
void Composer::restart(Scope& scope) {
    groups_.clear();
    locals_ = localsAbove(*scope.group);
    currentScope_ = nullptr;
    run(scope);
    locals_.clear();  // what they hold (a nav entry's ViewModelStore) isn't the composer's to keep
    for (Group* p = scope.group->parent; p; p = p->parent) {
        if (p->node) {
            rebuildChildren(*p);
            break;
        }
    }
}

void Composer::run(Scope& scope) {
    Scope* outer = std::exchange(currentScope_, &scope);
    clearReads(scope);
    scope.invalid = false;
    enter(*scope.group);
    scope.body();
    endGroup();
    currentScope_ = outer;
    ++stats_.scopesRun;
}

void Composer::recordRead(const void* key) {
    Scope* s = currentScope_;
    if (!s || std::find(s->reads.begin(), s->reads.end(), key) != s->reads.end()) return;
    s->reads.push_back(key);
    readers_[key].push_back(s);
}

void Composer::observeReads(Scope& observer, const std::function<void()>& fn) {
    Composer* previous = std::exchange(gCurrent, this);
    Scope* outer = std::exchange(currentScope_, &observer);
    clearReads(observer);
    fn();
    currentScope_ = outer;
    gCurrent = previous;
}

void Composer::clearReads(Scope& scope) {
    for (const void* key : scope.reads) {
        auto it = readers_.find(key);
        if (it == readers_.end()) continue;
        std::erase(it->second, &scope);
        if (it->second.empty()) readers_.erase(it);
    }
    scope.reads.clear();
}

void Composer::forget(Scope& scope) {
    clearReads(scope);
    std::erase(invalid_, &scope);
    std::erase(frameScopes_, &scope);
    if (currentScope_ == &scope) currentScope_ = nullptr;
}

Group& Composer::beginGroup(uint64_t key) {
    Group& parent = *groups_.back();
    uint64_t id = mixKey(key, parent.occurrences[key]++);
    auto& child = parent.children[id];
    if (!child) {
        child = std::make_unique<Group>();
        child->parent = &parent;
        child->depth = parent.depth + 1;
        child->key = id;
    }
    child->visited = true;
    child->composedIn = pass_;
    parent.order.push_back(child.get());
    return *child;
}

namespace {
void setDepth(Group& g, int depth) {
    g.depth = depth;
    for (auto& [k, child] : g.children) setDepth(*child, depth + 1);
}
} // namespace

void Composer::park(std::unique_ptr<Group>& owner) {
    if (!owner) return;
    if (owner->movable && owner->movable->group == owner.get()) {
        owner->parent = nullptr;
        parked_.push_back(std::move(owner));
        return;
    }
    for (auto& [k, child] : owner->children) park(child);
}

std::unique_ptr<Group> Composer::takeMovable(MovableData& data) {
    Group* g = data.group;
    if (!g || g->composedIn == pass_) return nullptr;  // gone, or composed already in this composition
    if (!g->parent) {
        for (auto& parked : parked_)
            if (parked.get() == g) {
                std::unique_ptr<Group> out = std::move(parked);
                std::erase(parked_, nullptr);
                return out;
            }
        return nullptr;
    }
    // Still where it was (that place not composed yet): taken out, its old place's nodes redone.
    Group* old = g->parent;
    std::unique_ptr<Group> out;
    for (auto it = old->children.begin(); it != old->children.end(); ++it)
        if (it->second.get() == g) {
            out = std::move(it->second);
            old->children.erase(it);
            break;
        }
    std::erase(old->order, g);
    for (Group* p = old; p; p = p->parent)
        if (p->node) {
            rebuildChildren(*p);
            break;
        }
    return out;
}

void Composer::composeMovable(const std::shared_ptr<MovableData>& data) {
    Group& parent = *groups_.back();
    uint64_t key = mixKey(0x6d6f7661626c6521ULL, uint64_t(reinterpret_cast<uintptr_t>(data.get())));
    uint64_t id = mixKey(key, parent.occurrences[key]++);
    auto& child = parent.children[id];
    if (!child) {
        child = takeMovable(*data);  // composed somewhere else last time: brought here
        if (!child) {
            child = std::make_unique<Group>();
            if (!data->group) child->movable = data, data->group = child.get();
        }
        child->parent = &parent;
        child->key = id;
        setDepth(*child, parent.depth + 1);
    }
    child->visited = true;
    child->composedIn = pass_;
    parent.order.push_back(child.get());
    Content content = data->content;  // (it may be replaced while it runs)
    enter(*child);
    if (content) content();
    endGroup();
}

void Composer::enter(Group& g) {
    g.occurrences.clear();
    g.order.clear();
    for (auto& [k, child] : g.children) child->visited = false;
    groups_.push_back(&g);
}

void Composer::startGroup(uint64_t key) { enter(beginGroup(key)); }

void Composer::endGroup() {
    Group& g = *groups_.back();
    // Whatever was not called this time has left the composition. Skipped scopes count as
    // called: they keep their slots and nodes.
    for (auto it = g.children.begin(); it != g.children.end();) {
        if (it->second->visited) {
            ++it;
            continue;
        }
        park(it->second);  // movable contents inside may be called further on
        it = g.children.erase(it);
    }
    if (g.node) rebuildChildren(g);
    groups_.pop_back();
}

LayoutNode& Composer::emit(uint64_t key, const std::function<void(LayoutNode&)>& update, const Content& content) {
    startGroup(key);
    Group& g = group();
    if (!g.node) g.node = std::make_unique<LayoutNode>(&host_);
    LayoutNode& node = *g.node;
    ++node.compositions;
    update(node);  // (measured again: its group's end rebuilds its children)
    node.rtl = LocalLayoutDirection.current() == LayoutDirection::Rtl;
    if (content) content();
    endGroup();
    return node;
}

void Composer::provide(uint64_t key, std::vector<Provided> values, const Content& content) {
    std::erase_if(values, [&](const Provided& v) { return v.ifMissing && findLocal(v.key); });  // provided above already
    startGroup(key);
    Group& g = group();
    // A provider that now gives a different value recomposes the scopes below it that read it.
    for (const Provided& v : values) {
        auto old = std::find_if(g.provided.begin(), g.provided.end(), [&](const Provided& p) { return p.key == v.key; });
        if (old != g.provided.end() && !v.equals(old->value.get(), v.value.get())) invalidateReaders(v.key, &g);
    }
    g.provided = values;
    size_t mark = locals_.size();
    locals_.insert(locals_.end(), values.begin(), values.end());
    content();
    locals_.resize(mark);
    endGroup();
}

const void* Composer::findLocal(const void* key) const {
    for (auto it = locals_.rbegin(); it != locals_.rend(); ++it)
        if (it->key == key) return it->value.get();
    return nullptr;
}

std::vector<Provided> Composer::localsAbove(const Group& g) const {
    std::vector<const Group*> providers;
    for (const Group* p = g.parent; p; p = p->parent)
        if (!p->provided.empty()) providers.push_back(p);
    std::vector<Provided> locals;
    for (auto it = providers.rbegin(); it != providers.rend(); ++it)
        locals.insert(locals.end(), (*it)->provided.begin(), (*it)->provided.end());
    return locals;
}

const CompositionLocal<std::shared_ptr<SaveableRegistry>> LocalSaveableRegistry{nullptr};

uint64_t keyBelow(const Group* boundary) {
    uint64_t key = 1469598103934665603ull;
    for (const Group* g = &Composer::require().group(); g && g != boundary; g = g->parent) key = mixKey(key, g->key);
    return key;
}

SavedValue SaveableRegistry::save() const {
    SavedValue::Map out;
    for (const auto& [key, value] : restored) out[std::to_string(key)] = value;  // not back yet: kept for later
    for (const auto& [key, save] : savers) out[std::to_string(key)] = save();
    return out;
}

void SaveableRegistry::restore(const SavedValue& saved) {
    if (const SavedValue::Map* map = saved.map())
        for (const auto& [name, value] : *map) {
            uint64_t key = 0;
            auto [end, ec] = std::from_chars(name.data(), name.data() + name.size(), key);
            if (ec == std::errc{} && end == name.data() + name.size()) restored[key] = value;
        }
}

void SaveableRegistry::drop(uint64_t key) {
    values.erase(key);
    types.erase(key);
    savers.erase(key);
}

namespace {
thread_local std::vector<std::weak_ptr<SaveableRegistry>> gLeaving;  // registries with values whose composables left
}

void noteLeaving(const std::shared_ptr<SaveableRegistry>& registry, uint64_t key) {
    if (registry->leaving.empty()) gLeaving.push_back(registry);
    registry->leaving.push_back(key);
}

void settleSaveables() {
    for (const auto& weak : std::exchange(gLeaving, {}))
        if (auto r = weak.lock()) {
            if (r->present)  // its provider is still there: they are gone
                for (uint64_t key : r->leaving) r->drop(key);
            r->leaving.clear();  // (else kept for when it comes back, as Compose saves them)
        }
}

} // namespace kompoz::detail

namespace kompoz {

SavedValue SaveableStateHolder::save() const {
    SavedValue::Map out;
    for (const auto& [key, registry] : *registries_) out[key] = registry->save();
    return out;
}

SaveableStateHolder SaveableStateHolder::restore(const SavedValue& saved) {
    SaveableStateHolder holder;
    if (const SavedValue::Map* map = saved.map())
        for (const auto& [key, values] : *map) {
            auto registry = std::make_shared<detail::SaveableRegistry>();
            registry->restore(values);
            (*holder.registries_)[key] = std::move(registry);
        }
    return holder;
}

void SaveableStateHolder::SaveableStateProvider(const std::string& key, const Content& content, CallSite site) const {
    bool removed = std::find(removed_->begin(), removed_->end(), key) != removed_->end();
    std::shared_ptr<detail::SaveableRegistry> scratch;
    auto& registry = removed ? scratch : (*registries_)[key];
    if (!registry) registry = std::make_shared<detail::SaveableRegistry>();
    auto& c = detail::Composer::require();
    c.startGroup(detail::mixKey(detail::keyOf(site), std::hash<std::string>{}(key)));
    registry->boundary = &c.group();  // positions are counted from here down
    registry->present = true;
    struct Presence {  // marks the registry's provider gone as its group goes
        std::weak_ptr<detail::SaveableRegistry> registry;
        ~Presence() {
            if (auto r = registry.lock()) r->present = false;
        }
    };
    remember([] { return std::make_shared<Presence>(); })->registry = registry;
    CompositionLocalProvider({detail::LocalSaveableRegistry.provides(registry)}, content);
    c.endGroup();
}

} // namespace kompoz
