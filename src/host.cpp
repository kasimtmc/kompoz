#include <kompoz/animation.hpp>
#include <kompoz/coroutines.hpp>
#include <kompoz/display_list.hpp>
#include <kompoz/host.hpp>
#include <kompoz/nested_scroll.hpp>
#include <kompoz/viewmodel.hpp>

#include "text_layout.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace kompoz {

namespace {

// A wheel notch, as Compose's (Android's scaled scroll factor): 64 dp. Scrolled smoothly: the
// first 6 dp at once, the rest at 1 dp a millisecond, in at most 100 ms.
constexpr float kWheelStep = 64, kWheelThreshold = 6, kWheelSpeed = 1, kWheelMaxMillis = 100;
constexpr float kMinFlingSpeed = 150;  // dp/s
constexpr int64_t kTapTimeoutNanos = 100000000;  // Android ViewConfiguration.getTapTimeout(): a press in a scrollable shows after it

std::mutex gMutex;
std::vector<ComposeHost*> gHosts;
bool gKeepCheck = false;  // (detail::setKeepCheck: the tests'; an app's platform checks its own frames, KOMPOZ_KEEP_CHECK)
int gKeepMismatches = 0;
std::vector<std::function<void()>> gTasks;

template <class T>
const T* elementOf(const ModifierElement* e) {
    return dynamic_cast<const T*>(e);
}

int axisOf(Orientation o) { return o == Orientation::Vertical ? 0 : 1; }

const ScrollableElement* scrollableOf(const LayoutNode& node, int axis, size_t* index = nullptr) {
    const auto& elements = node.modifier.elements();
    for (size_t i = 0; i < elements.size(); ++i)
        if (auto* s = dynamic_cast<const ScrollableElement*>(elements[i].get()); s && axisOf(s->orientation()) == axis) {
            if (index) *index = i;
            return s;
        }
    return nullptr;
}

bool isTextField(const LayoutNode* node) {
    auto* f = node ? node->modifier.find<FocusableElement>() : nullptr;
    return f && f->onEdit;
}

// The text field in a text field's decoration.
LayoutNode* textFieldIn(LayoutNode& node) {
    if (isTextField(&node)) return &node;
    for (LayoutNode* child : node.children)
        if (child->isPlaced())
            if (LayoutNode* field = textFieldIn(*child)) return field;
    return nullptr;
}

Offset alongAxis(int axis, float v) { return axis == 0 ? Offset{0, v} : Offset{v, 0}; }
Size sizeOf(const Rect& r) { return {r.width, r.height}; }
float component(int axis, Offset o) { return axis == 0 ? o.y : o.x; }
float component(int axis, Velocity v) { return axis == 0 ? v.y : v.x; }
Velocity velocityAlong(int axis, float v) { return axis == 0 ? Velocity{0, v} : Velocity{v, 0}; }

Rect layerBoundsOf(const LayoutNode& node, const ModifierElement* element) {
    const auto& elements = node.modifier.elements();
    for (size_t i = 0; i < elements.size(); ++i)
        if (elements[i].get() == element) return node.layerBounds(i);
    return node.bounds();
}

// Where the node's element `index` takes taps: its bounds, grown to those of a
// minimumInteractiveComponentSize around it.
Rect hitBounds(const LayoutNode& node, size_t index) {
    Rect r = node.layerBounds(index);
    const auto& elements = node.modifier.elements();
    for (size_t j = index; j-- > 0;)
        if (elements[j]->expandsTouchArea()) {
            Rect room = node.layerBounds(j);
            float left = std::min(r.x, room.x), top = std::min(r.y, room.y);
            return {left, top, std::max(r.right(), room.right()) - left, std::max(r.bottom(), room.bottom()) - top};
        }
    return r;
}

// Where a node takes the focus (Compose's focus target's rect): the layer of its focusable,
// clickable or focus group — past an offset or padding before it — else its bounds.
Rect focusRect(const LayoutNode& node) {
    const auto& elements = node.modifier.elements();
    for (size_t i = 0; i < elements.size(); ++i) {
        const ModifierElement* e = elements[i].get();
        if (dynamic_cast<const FocusableElement*>(e) || dynamic_cast<const ClickableElement*>(e) || dynamic_cast<const FocusGroupElement*>(e))
            return node.layerBounds(i);
    }
    return node.bounds();
}

// Bounds too small to touch grown evenly to `min` each way (Compose's minimum touch target).
Rect touchTarget(const Rect& r, Size min) {
    float dx = std::max(0.f, (min.width - r.width) / 2), dy = std::max(0.f, (min.height - r.height) / 2);
    return {r.x - dx, r.y - dy, r.width + 2 * dx, r.height + 2 * dy};
}

Rect hitBoundsOf(const LayoutNode& node, const ModifierElement* element) {
    const auto& elements = node.modifier.elements();
    for (size_t i = 0; i < elements.size(); ++i)
        if (elements[i].get() == element) return hitBounds(node, i);
    return node.bounds();
}

} // namespace

void detail::notifyStateChanged(const void* key) {
    std::lock_guard lock(gMutex);
    for (auto* host : gHosts) host->stateChanged(key);
}

void detail::requestDraw() {
    std::lock_guard lock(gMutex);
    for (auto* host : gHosts) {
        host->requestRedraw();
        host->wake();
    }
}

void detail::requestLayout() {
    std::lock_guard lock(gMutex);
    for (auto* host : gHosts) {
        host->requestLayout();
        host->wake();
    }
}

void runOnUiThread(std::function<void()> task) {
    std::vector<ComposeHost*> hosts;
    {
        std::lock_guard lock(gMutex);
        gTasks.push_back(std::move(task));
        hosts = gHosts;
    }
    for (auto* host : hosts) host->wake();
}

ComposeHost::ComposeHost(TextMeasurer& measurer, Content content)
    : measurer_(measurer),
      content_(std::move(content)),
      composer_(*this),
      dispatcher_(std::make_unique<detail::Dispatcher>()),
      rootGroup_(std::make_unique<detail::Group>()),
      saveable_(std::make_shared<detail::SaveableRegistry>()) {
    saveable_->boundary = rootGroup_.get();
    saveable_->forgetsLeaving = true;
    dispatcher_->composer = &composer_;
    flings_ = std::make_unique<CoroutineScope>(*dispatcher_);
    rootGroup_->node = std::make_unique<LayoutNode>(this);
    rootGroup_->node->measurePolicy = [](LayoutNode& node, const Constraints& c) {
        for (auto* child : node.children) {
            child->measure(c.loosen());
            child->place(0, 0);
        }
        return Size{c.maxWidth, c.maxHeight};
    };
    composer_.setRoot(*rootGroup_, [this] {
        const SystemInsets& i = insets_;
        auto max = [](PaddingValues a, PaddingValues b) {
            return PaddingValues{std::max(a.start, b.start), std::max(a.top, b.top), std::max(a.end, b.end),
                                 std::max(a.bottom, b.bottom)};
        };
        PaddingValues safeDrawing = max(max(i.statusBars, i.navigationBars), max(i.ime, i.displayCutout));
        CompositionLocalProvider({LocalSystemInsets.provides(insets_), LocalWindowInsets.provides(safeDrawing),
                                  LocalSystemDarkTheme.provides(systemDark_), LocalLayoutDirection.provides(layoutDirection_),
                                  LocalStrings.provides(strings_),
                                  LocalWindowSize.provides(viewport_), LocalWindowInfo.provides(WindowInfo{windowFocused_, viewport_}),
                                  LocalViewConfiguration.provides(viewConfiguration_),
                                  LocalDensity.provides(density_),
                                  LocalDynamicTonalPalette.provides(dynamicPalette_),
                                  LocalWallpaperTonalPalette.provides(wallpaperPalette_), LocalUiMediaScope.provides(media_),
                                  detail::LocalSaveableRegistry.provides(saveable_)},
                                 content_);
    });
    std::lock_guard lock(gMutex);
    gHosts.push_back(this);
}

ComposeHost::~ComposeHost() {
    {
        std::lock_guard lock(gMutex);
        std::erase(gHosts, this);
    }
    flings_->cancel();
    lifecycleOwner_->lifecycle().setCurrentState(LifecycleState::Destroyed);  // paused, stopped, destroyed: told while things are still here
    if (viewModelStore_) viewModelStore_->clear();  // while the dispatcher their scopes use is still here
    rootGroup_.reset();  // runs DisposableEffect cleanups and destroys the node tree
}

const std::shared_ptr<ViewModelStore>& ComposeHost::viewModelStore() {
    if (!viewModelStore_) viewModelStore_ = std::make_shared<ViewModelStore>();
    return viewModelStore_;
}

// The rememberSaveable values and the app's ViewModels' SavedStateHandles (a navigation entry's
// go with the NavController that keeps the entry).
std::vector<uint8_t> ComposeHost::saveState() const {
    SavedValue::Map out{{"composition", saveable_->save()}};
    if (viewModelStore_)
        if (SavedValue viewModels = viewModelStore_->saveState(); viewModels.map() && !viewModels.map()->empty())
            out["viewModels"] = std::move(viewModels);
    return SavedValue(std::move(out)).encode();
}

void ComposeHost::restoreState(const std::vector<uint8_t>& bytes) {
    auto saved = SavedValue::decode(bytes);
    if (!saved) return;
    if (const SavedValue& composition = (*saved)["composition"]; composition.map()) {
        saveable_->restore(composition);
        viewModelStore()->restoreState((*saved)["viewModels"]);
    } else {
        saveable_->restore(*saved);  // written before ViewModels were saved too
    }
}

void ComposeHost::openDeepLink(std::string link) {
    deepLink_ = std::move(link);
    deepLinks_.set(deepLinks_.peek() + 1);
}

void ComposeHost::addDeepLinkHandler(const void* owner, std::function<bool(const std::string&)> accepts) {
    removeDeepLinkHandler(owner);
    deepLinkHandlers_.push_back({owner, std::move(accepts)});
}

void ComposeHost::removeDeepLinkHandler(const void* owner) {
    std::erase_if(deepLinkHandlers_, [owner](const auto& h) { return h.first == owner; });
}

void ComposeHost::openUri(const std::string& uri) {
    for (const auto& [owner, accepts] : deepLinkHandlers_)
        if (accepts(uri)) return openDeepLink(uri);
    if (callbacks_.openUri) callbacks_.openUri(uri);
}

std::optional<std::string> ComposeHost::takeDeepLink(const std::function<bool(const std::string&)>& accepts) {
    deepLinks_.get();
    if (!deepLink_ || !accepts(*deepLink_)) return std::nullopt;
    return std::exchange(deepLink_, std::nullopt);
}

void ComposeHost::setCallbacks(Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    // What was composed before the platform was listening.
    if (barsAppearance_ && callbacks_.systemBarsAppearance)
        callbacks_.systemBarsAppearance(barsAppearance_->first, barsAppearance_->second);
}

void ComposeHost::setViewportSize(Size size) {
    if (size == viewport_) return;
    // Shorter around the keyboard (a window that isn't edge to edge, where the keyboard is no
    // inset): the field typed in stays in view.
    if (size.height < viewport_.height && isTextField(focused_)) bringFocusedIntoView_ = true;
    viewport_ = size;
    media_.update(viewport_, insets_.imeVisible, mediaInfo_);
    requestLayout();
    invalidate();  // LocalWindowSize changed (only its readers recompose)
}

void ComposeHost::setSystemInsets(SystemInsets insets) {
    if (insets == insets_) return;
    if (!(insets.ime == insets_.ime) && isTextField(focused_)) bringFocusedIntoView_ = true;  // keep it above the keyboard
    insets_ = insets;
    media_.update(viewport_, insets_.imeVisible, mediaInfo_);
    invalidate();
}

void ComposeHost::setWindowInsets(PaddingValues insets) { setSystemInsets({.displayCutout = insets}); }

float ComposeHost::imeControlBy(float dp) {
    if (!imeControlling_) {
        auto* f = focused_ && isTextField(focused_) ? focused_->modifier.find<FocusableElement>() : nullptr;
        if (!callbacks_.imeControlBy || !f || f->readOnly) return 0;
        imeControlling_ = true;
        imeHeight_ = insets_.ime.bottom;
    }
    float next = std::max(0.f, imeHeight_ + dp);
    if (insets_.imeAnimating && insets_.imeAnimationTarget.bottom > 0) next = std::min(next, insets_.imeAnimationTarget.bottom);  // shown: its height
    float used = next - imeHeight_;
    imeHeight_ = next;
    if (used != 0) callbacks_.imeControlBy(used);
    return used;
}

void ComposeHost::imeControlEnd(float velocity) {
    if (!imeControlling_) return;
    imeControlling_ = false;
    if (callbacks_.imeControlEnd) callbacks_.imeControlEnd(velocity);
}

void ComposeHost::setDensity(Density density) {
    if (density == density_) return;
    density_ = density;
    requestLayout();  // text sizes may have changed
    invalidate();
}

void ComposeHost::setMediaInfo(const MediaInfo& info) {
    mediaInfo_ = info;
    media_.update(viewport_, insets_.imeVisible, mediaInfo_);  // what read it recomposes
}

void ComposeHost::setDynamicTonalPalette(std::optional<TonalPalette> palette) {
    if (palette == dynamicPalette_) return;
    dynamicPalette_ = std::move(palette);
    invalidate();
}

void ComposeHost::setWallpaperTonalPalette(std::optional<TonalPalette> palette) {
    if (palette == wallpaperPalette_) return;
    wallpaperPalette_ = std::move(palette);
    invalidate();
}

void ComposeHost::setSystemBarsAppearance(bool lightStatusBars, bool lightNavigationBars) {
    std::pair<bool, bool> appearance{lightStatusBars, lightNavigationBars};
    if (barsAppearance_ == appearance) return;
    barsAppearance_ = appearance;
    if (callbacks_.systemBarsAppearance) callbacks_.systemBarsAppearance(lightStatusBars, lightNavigationBars);
}

void ComposeHost::setSystemDarkTheme(bool dark) {
    if (dark == systemDark_) return;
    systemDark_ = dark;
    invalidate();
}

void ComposeHost::setViewConfiguration(const ViewConfiguration& configuration) {
    if (configuration == viewConfiguration_) return;
    viewConfiguration_ = configuration;
    invalidate();
}

void ComposeHost::setWindowFocused(bool focused) {
    if (focused == windowFocused_) return;
    windowFocused_ = focused;
    invalidate();
}

void ComposeHost::setLayoutDirection(LayoutDirection direction) {
    if (direction == layoutDirection_) return;
    layoutDirection_ = direction;
    invalidate();
}

void ComposeHost::setStrings(Strings strings) {
    if (strings == strings_) return;
    strings_ = std::move(strings);
    invalidate();
}

bool ComposeHost::frame(int64_t frameTimeNanos) {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard lock(gMutex);
        tasks.swap(gTasks);
    }
    for (auto& task : tasks) task();

    lastFrameTime_ = frameTimeNanos;
    dispatcher_->run(frameTimeNanos);  // coroutines: due delays, then frame waiters
    runClickTimers(frameTimeNanos);
    stepFling(frameTimeNanos);
    stepWheel(frameTimeNanos);
    if (std::exchange(layoutAnimating_, false)) {
        layoutDirty_ = true;
        for (const auto& handle : std::exchange(animatingLayouts_, {}))
            if (auto node = handle.lock(); node && *node) (*node)->invalidateMeasure();
    }

    // Composition, then layout. Layout may write state (a lazy list that needs more items,
    // a scaffold that measured its bars); then compose and lay out again before drawing.
    for (int pass = 0; pass < 4; ++pass) {
        std::vector<const void*> changed;
        {
            std::lock_guard lock(gMutex);
            changed.swap(pendingChanges_);
        }
        for (const void* key : changed) composer_.invalidateReaders(key);
        // State read outside composition (offset { }, graphicsLayer { }, drawBehind) only
        // needs layout and drawing again.
        if (!changed.empty()) layoutDirty_ = true;
        if (rootInvalid_.exchange(false)) {  // something global changed (density, theme, fonts): all of it again
            composer_.invalidateRoot();
            layoutEverything_ = true;
        }
        if (pass == 0) composer_.invalidateFrameScopes(frameTimeNanos);
        if (composer_.recompose(frameTimeNanos)) {
            layoutDirty_ = true;
            syncTextInput();
        }
        if (!layoutDirty_) break;
        layoutDirty_ = false;
        // Nothing measured or moved (a state read only while drawing): nothing new to report.
        if (layout()) reportPositions(frameTimeNanos);  // may write state: then another pass
        redraw_ = true;
        std::lock_guard lock(gMutex);
        if (pendingChanges_.empty()) break;
    }
    checkVisibilityTimers(frameTimeNanos);  // visible long enough by now, with nothing laid out again
    if (!unechoed_.empty()) syncTextInput();  // still waiting for the app: maybe it refused the edit
    // Ripples: one let go (or whose press ended some other way) fades; a faded one goes.
    bool rippling = false;
    std::erase_if(ripples_, [&](Ripple& r) {
        if (r.released < 0 && r.node != pressed_) r.released = frameTimeNanos;
        bool done = rippleDone(r.start, r.released, frameTimeNanos), moving = !done && rippleMoving(r.start, r.released, frameTimeNanos);
        if (done || moving || r.moved) r.node->invalidateDraw();  // (held after spreading: nothing moves)
        r.moved = moving;
        rippling = rippling || moving;
        return done;
    });
    if (rippling) redraw_ = true;
    animating_ = composer_.hasFrameScopes() || flingNode_ || wheel_ || rippling;
    if (redraw_ && mouseOver_ && !pointerDown_) updateHover(lastPosition_);  // content may have moved under a resting mouse
    if (gKeepCheck) checkKept();
    return std::exchange(redraw_, false);
}

void ComposeHost::checkKept() {
    if (!keepCheck_) {
        keepCheck_ = std::make_unique<std::pair<DisplayList, DisplayList>>(std::piecewise_construct, std::forward_as_tuple(measurer_),
                                                                          std::forward_as_tuple(measurer_));
        keepCheck_->first.keepNodes(true);
    }
    bool redraw = redraw_;  // (drawn for the check: no frame asked for by it)
    auto record = [&](DisplayList& list) {
        list.begin(viewport_, 1 / std::max(density_.density, 1e-3f));
        draw(list);
        list.end();
    };
    record(keepCheck_->first);
    record(keepCheck_->second);
    redraw_ = redraw;
    std::string why;
    if (!keepCheck_->first.sameFrame(keepCheck_->second, &why)) {
        ++gKeepMismatches;
        std::fprintf(stderr, "kompoz: keep check: %s\n", why.c_str());
    }
}

bool ComposeHost::hasPendingWork() const {
    if (rootInvalid_ || animating_ || layoutDirty_ || redraw_ || flingNode_ || layoutAnimating_ || lastFrameTime_ < drawingAnimatedUntil_ ||
        composer_.hasInvalidScopes() ||
        dispatcher_->hasFrameWaiters() || !unechoed_.empty())
        return true;
    for (const auto& [key, v] : visibility_)
        if (v.visibleSince >= 0) return true;  // a visibility to tell once it has lasted
    for (const auto& [key, r] : rects_)
        if (r.waiting()) return true;  // a rect to tell once it stays put
    if (detail::backgroundWorkInFlight() > 0) return true;  // its result comes back as a task
    std::lock_guard lock(gMutex);
    return !gTasks.empty() || !pendingChanges_.empty();
}

int64_t ComposeHost::nextTimerDeadline() const {
    int64_t next = dispatcher_->nextDeadline();
    for (int64_t t : {pressed_ ? longPressAt_ : -1, pressed_ ? delayedPressAt_ : -1, pendingClick_ ? pendingClickAt_ : -1,
                      longPressTrigger_ ? triggerLongPressAt_ : -1,
                      textLongPress_ ? textLongPressAt_ : -1, dndSource_ ? dndLongPressAt_ : -1})
        if (t >= 0 && (next < 0 || t < next)) next = t;
    return next;
}
bool ComposeHost::hasTimers() const {
    return dispatcher_->hasTimers() || (pressed_ && (longPressAt_ >= 0 || delayedPressAt_ >= 0)) || pendingClick_ ||
           (longPressTrigger_ && triggerLongPressAt_ >= 0) || (textLongPress_ && textLongPressAt_ >= 0) ||
           (dndSource_ && dndLongPressAt_ >= 0);
}

void ComposeHost::tellVisibility(Visibility& v, bool visible) {
    v.visibleSince = -1;
    v.reported = visible;
    if (v.onChanged) visibilityCalls_.push_back([f = v.onChanged, visible] { f(visible); });
    if (v.onFirst && visible && !v.first) {
        v.first = true;
        visibilityCalls_.push_back(v.onFirst);
    }
}

void ComposeHost::tellRect(RectReport& r, int64_t now) {
    r.told = r.latest;
    r.toldAt = now;
    if (r.callback) visibilityCalls_.push_back([f = r.callback, bounds = *r.latest] { f(bounds); });
}

void ComposeHost::checkVisibilityTimers(int64_t now) {
    for (auto& [key, v] : visibility_)
        if (v.visibleSince >= 0 && now - v.visibleSince >= v.minDurationNanos) tellVisibility(v, true);
    // Rects that stayed put long enough, or whose throttle lets them be told (again).
    for (auto& [key, r] : rects_)
        if (r.waiting() && ((r.debounceNanos > 0 && now - r.changedAt >= r.debounceNanos) ||
                            (r.throttleNanos > 0 && (r.toldAt < 0 || now - r.toldAt >= r.throttleNanos))))
            tellRect(r, now);
    for (auto& call : std::exchange(visibilityCalls_, {})) call();
}

namespace {
// What reportPositions acts on in an element, found out once per modifier (a node keeps its
// elements'): a bit each.
enum Reported : uint16_t {
    kScrollable = 1 << 0, kClickable = 1 << 1, kIndication = 1 << 2, kPositioned = 1 << 3, kLayoutBounds = 1 << 4, kKeepScreenOn = 1 << 5,
    kSensitive = 1 << 6, kFrameRate = 1 << 7, kFocusable = 1 << 8, kOverscroll = 1 << 9, kGestureExclusion = 1 << 10, kLayoutRect = 1 << 11,
    kVisibility = 1 << 12,
    kClips = 1 << 13,  // (cuts what is inside it: not acted on, followed)
};
uint16_t reported(const ModifierElement& e) {
    // (Only where there is something to do: a clickable's custom indication, a callback, ...)
    uint16_t r = 0;
    auto is = [&](bool yes, uint16_t bit) { r |= yes ? bit : 0; };
    is(dynamic_cast<const ScrollableElement*>(&e), kScrollable);
    auto* click = dynamic_cast<const ClickableElement*>(&e);
    is(click && click->enabled && click->indication.kind == Indication::Kind::Custom, kClickable);
    auto* indication = dynamic_cast<const IndicationElement*>(&e);
    is(indication && indication->indication.kind == Indication::Kind::Custom, kIndication);
    auto* positioned = dynamic_cast<const PositionedElement*>(&e);
    is(positioned && positioned->onPositioned, kPositioned);
    is(dynamic_cast<const LayoutBoundsElement*>(&e), kLayoutBounds);
    is(dynamic_cast<const KeepScreenOnElement*>(&e), kKeepScreenOn);
    auto* sensitive = dynamic_cast<const SensitiveContentElement*>(&e);
    is(sensitive && sensitive->sensitive, kSensitive);
    is(dynamic_cast<const FrameRateElement*>(&e), kFrameRate);
    auto* focusable = dynamic_cast<const FocusableElement*>(&e);
    is(focusable && focusable->onEdit, kFocusable);
    auto* overscroll = dynamic_cast<const OverscrollElement*>(&e);
    is(overscroll && overscroll->effect, kOverscroll);
    is(dynamic_cast<const SystemGestureExclusionElement*>(&e), kGestureExclusion);
    auto* rect = dynamic_cast<const LayoutRectElement*>(&e);
    is(rect && rect->callback, kLayoutRect);
    auto* visibility = dynamic_cast<const VisibilityElement*>(&e);
    is(visibility && (visibility->onChanged || visibility->onFirst), kVisibility);
    if (e.clipsContent()) r |= kClips;
    return r;
}
// The element as what its bit says it is; none if it isn't.
template <class T>
const T* as(const std::shared_ptr<const ModifierElement>& e, uint16_t kinds, uint16_t bit) {
    return kinds & bit ? static_cast<const T*>(e.get()) : nullptr;
}
} // namespace

void LayoutNode::checkReported() {
    const auto& elements = modifier.elements();
    if (!std::ranges::equal(reportCheckedFor_, elements, {}, &std::shared_ptr<const ModifierElement>::get,
                            &std::shared_ptr<const ModifierElement>::get)) {
        reportCheckedFor_ = elements;
        reported_.resize(elements.size());
        for (size_t i = 0; i < elements.size(); ++i) reported_[i] = reported(*elements[i]);
        reportsNothing_ = std::ranges::all_of(reported_, [](uint16_t r) { return (r & ~kClips) == 0; });
    }
    setReports(!reportsNothing_ || stretch || indicationNode);
}

bool LayoutNode::scrollableAt(size_t index) const { return index < reported_.size() && (reported_[index] & kScrollable); }

void ComposeHost::reportPositions(int64_t now) {
    std::vector<std::pair<std::function<void(const LayoutCoordinates&)>, LayoutCoordinates>> calls;
    for (auto& [key, v] : visibility_) v.seen = false;
    for (auto& [key, r] : rects_) r.seen = false;
    gestureExclusion_.clear();
    keepScreenOn_ = sensitiveContent_ = false;
    frameRate_ = 0;
    frameRateCategory_ = FrameRateCategory::Default;
    bool autofill = bool(callbacks_.autofillValue);
    for (auto& [id, field] : autofillFields_) field.seen = false;
    // The nodes walked into, for a coordinates' parents (made only when someone asks).
    struct Frame {
        LayoutNode* node;
        Rect clip;  // around it, before its own clips
        const Frame* up;
    };
    auto inParent = [](const LayoutNode& node, const Rect& b) {  // from where its parent's children go (scrolled along)
        Rect around = node.parent && !node.overlay ? node.parent->contentBounds() : Rect{};
        return Offset{b.x - around.x, b.y - around.y};
    };
    std::function<std::shared_ptr<const LayoutCoordinates>(const Frame*)> coordinatesOf = [&](const Frame* f) -> std::shared_ptr<const LayoutCoordinates> {
        if (!f) return nullptr;
        Rect b = f->node->bounds();
        return std::make_shared<const LayoutCoordinates>(b, b.intersect(f->clip), inParent(*f->node, b), coordinatesOf(f->up));
    };
    Rect window{0, 0, viewport_.width, viewport_.height};
    std::function<void(LayoutNode&, Rect, const Frame*)> walk = [&](LayoutNode& node, Rect clip, const Frame* up) {
        Frame self{&node, clip, up};
        const auto& elements = node.modifier.elements();
        const std::vector<uint16_t>& kinds = node.reported_;  // (what its elements are: found out as it was measured)
        if (node.reportsNothing_ && !node.stretch && !node.indicationNode) {
            for (size_t i = 0; i < elements.size(); ++i)
                if (kinds[i] & kClips) clip = clip.intersect(elements[i]->clipBounds(node.layerBounds(i)));
            for (LayoutNode* child : node.children)
                if (child->isPlaced() && child->reportedBelow_ > 0) walk(*child, clip, &self);  // (none to act on below: not walked)
            return;
        }
        // A scroll container with no overscroll effect of its own keeps a stretch on its node
        // (one that stops overscrolling lets it go, stretched or not).
        bool overscrolls = false;
        for (size_t i = 0; i < elements.size() && !overscrolls; ++i)
            if (auto* s = as<ScrollableElement>(elements[i], kinds[i], kScrollable)) overscrolls = s->overscrolls();
        if (!overscrolls)
            node.stretch.reset();
        else if (!node.stretch)
            node.stretch = std::make_shared<detail::StretchOverscroll>();
        // The node's custom indication (its first), whose node hears its interactions.
        const Indication* custom = nullptr;
        std::optional<MutableInteractionSource> heard;
        for (size_t i = 0; i < elements.size(); ++i) {
            if (auto* click = as<ClickableElement>(elements[i], kinds[i], kClickable);
                click && click->enabled && click->indication.kind == Indication::Kind::Custom) {
                custom = &click->indication, heard = sourceOf(node, click);
                break;
            }
            if (auto* ind = as<IndicationElement>(elements[i], kinds[i], kIndication); ind && ind->indication.kind == Indication::Kind::Custom) {
                custom = &ind->indication, heard = ind->source;
                break;
            }
        }
        syncIndicationNode(node, custom, heard);
        node.setReports(!node.reportsNothing_ || node.stretch || node.indicationNode);
        for (size_t i = 0; i < elements.size(); ++i) {
            if (auto* p = as<PositionedElement>(elements[i], kinds[i], kPositioned); p && p->onPositioned) {
                Rect b = node.layerBounds(i);
                LayoutCoordinates c(b, b.intersect(clip), inParent(node, b), coordinatesOf(up));
                // onPlaced only hears of its size and place in the parent.
                LayoutCoordinates key = p->inParentOnly ? LayoutCoordinates({0, 0, b.width, b.height}, {}, c.positionInParent()) : c;
                auto [it, fresh] = positioned_.try_emplace({&node, i}, key);
                if (fresh || !(it->second == key)) {
                    it->second = key;
                    calls.emplace_back(p->onPositioned, c);
                }
            }
            if (auto* l = as<LayoutBoundsElement>(elements[i], kinds[i], kLayoutBounds)) *l->holder.data_ = node.layerBounds(i);
            if (as<KeepScreenOnElement>(elements[i], kinds[i], kKeepScreenOn)) keepScreenOn_ = true;
            if (auto* sc = as<SensitiveContentElement>(elements[i], kinds[i], kSensitive); sc && sc->sensitive) sensitiveContent_ = true;
            if (auto* fr = as<FrameRateElement>(elements[i], kinds[i], kFrameRate)) {
                frameRate_ = std::max(frameRate_, fr->rate);
                frameRateCategory_ = std::max(frameRateCategory_, fr->category);
            }
            // Autofill hears of a text field coming into the layout and of its text changing.
            if (auto* f = as<FocusableElement>(elements[i], kinds[i], kFocusable); f && f->onEdit && autofill) {
                int id = semanticsIdOf(&node);
                auto [it, fresh] = autofillFields_.try_emplace(id, AutofillField{f->value});
                it->second.seen = true;
                if (fresh) {
                    if (callbacks_.autofillVisibility) callbacks_.autofillVisibility(id, true);
                } else if (it->second.value != f->value) {
                    it->second.value = f->value;
                    callbacks_.autofillValue(id, f->value);
                }
            }
            // Overscroll effects learn their size where they are drawn.
            if (auto* s = as<ScrollableElement>(elements[i], kinds[i], kScrollable))
                if (OverscrollEffect* effect = s->drawsOverscroll() ? node.overscrollOf(*s) : nullptr) effect->onRemeasured(sizeOf(node.layerBounds(i)));
            if (auto* o = as<OverscrollElement>(elements[i], kinds[i], kOverscroll); o && o->effect) o->effect->onRemeasured(sizeOf(node.layerBounds(i)));
            if (auto* g = as<SystemGestureExclusionElement>(elements[i], kinds[i], kGestureExclusion)) {
                Rect b = node.layerBounds(i), r = b;
                if (g->exclusion) {
                    Rect local = g->exclusion(LayoutCoordinates(b, b.intersect(clip), inParent(node, b), coordinatesOf(up)));
                    r = {b.x + local.x, b.y + local.y, local.width, local.height};
                }
                Rect shown = r.intersect(clip);
                if (shown.width > 0 && shown.height > 0) gestureExclusion_.push_back(shown);
            }
            if (auto* e = as<LayoutRectElement>(elements[i], kinds[i], kLayoutRect); e && e->callback) {
                Rect b = node.layerBounds(i);
                RelativeLayoutBounds bounds{b, b, window};
                RectReport& r = rects_[{&node, i}];
                r.seen = true;
                r.callback = e->callback;
                r.throttleNanos = e->throttleMillis * 1'000'000, r.debounceNanos = e->debounceMillis * 1'000'000;
                if (r.latest != bounds) {
                    r.latest = bounds;
                    r.changedAt = now;
                    if (r.throttleNanos == 0 && r.debounceNanos == 0) tellRect(r, now);  // with neither delay, at once
                }
            }
            if (auto* e = as<VisibilityElement>(elements[i], kinds[i], kVisibility); e && (e->onChanged || e->onFirst)) {
                // How much of it shows in the window (or its viewport), as Compose: more than the
                // fraction asked, or all of it.
                Rect b = node.layerBounds(i);
                std::optional<Rect> viewport = e->options.viewport ? e->options.viewport->bounds() : std::optional<Rect>(window);
                if (viewport) {
                    Rect in = b.intersect(*viewport);
                    float area = b.width * b.height;
                    float fraction = area > 0 ? std::max(0.f, in.width) * std::max(0.f, in.height) / area : 0.f;
                    bool visible = fraction > e->options.minFractionVisible || fraction >= 1;
                    Visibility& v = visibility_[{&node, i}];
                    v.seen = true;
                    v.onChanged = e->onChanged, v.onFirst = e->onFirst;
                    v.minDurationNanos = e->options.minDurationMs * 1'000'000;
                    if (visible != v.result) {
                        v.result = visible;
                        v.visibleSince = -1;
                        if (visible != v.reported) {
                            if (visible && v.minDurationNanos > 0) v.visibleSince = now;  // told once it has lasted
                            else tellVisibility(v, visible);
                        }
                    }
                }
            }
            if (kinds[i] & kClips) clip = clip.intersect(elements[i]->clipBounds(node.layerBounds(i)));  // scrollers, clipToBounds, clip(shape)
        }
        for (LayoutNode* child : node.children)
            if (child->isPlaced() && child->reportedBelow_ > 0) walk(*child, clip, &self);
    };
    if (root().reportedBelow_ > 0) walk(root(), window, nullptr);
    for (const auto& [popup, anchor] : overlays_)
        if (popup->reportedBelow_ > 0) walk(*popup, window, nullptr);
    // Those no longer laid out (left the layout, gone): not visible, at once.
    for (auto it = visibility_.begin(); it != visibility_.end();) {
        if (it->second.seen) {
            ++it;
            continue;
        }
        if (it->second.reported) tellVisibility(it->second, false);
        it = visibility_.erase(it);
    }
    std::erase_if(rects_, [](const auto& entry) { return !entry.second.seen; });  // gone: nothing to tell
    if (autofill) {  // text fields gone, and the one with the focus
        for (auto it = autofillFields_.begin(); it != autofillFields_.end();) {
            if (it->second.seen) {
                ++it;
                continue;
            }
            if (callbacks_.autofillVisibility) callbacks_.autofillVisibility(it->first, false);
            it = autofillFields_.erase(it);
        }
        int entered = isTextField(focused_) ? semanticsIdOf(focused_) : -1;
        if (entered != autofillEntered_ && callbacks_.autofillFocus) {
            if (autofillEntered_ >= 0) callbacks_.autofillFocus(autofillEntered_, {}, false);
            if (entered >= 0) callbacks_.autofillFocus(entered, focused_->bounds(), true);
        }
        autofillEntered_ = entered;
    }
    checkVisibilityTimers(now);
    for (auto& [callback, coordinates] : calls) callback(coordinates);
}

namespace {
bool gLayoutCheck = std::getenv("KOMPOZ_LAYOUT_CHECK") != nullptr;
const bool gFullLayout = std::getenv("KOMPOZ_FULL_LAYOUT") != nullptr;  // every node measured every time, as before
int gLayoutMismatches = 0;

void measureAllAgain(LayoutNode& node) {
    node.invalidateMeasure();
    for (auto* child : node.children) measureAllAgain(*child);
    for (auto* popup : node.overlays) measureAllAgain(*popup);
}

struct LaidOut {
    Rect bounds, content;
    bool placed;
    float zIndex;
};
void collectLayout(LayoutNode& node, std::unordered_map<const LayoutNode*, LaidOut>& out) {
    out[&node] = {node.bounds(), node.contentBounds(), node.isPlaced(), node.zIndex()};
    for (auto* child : node.children) collectLayout(*child, out);
    for (auto* popup : node.overlays) collectLayout(*popup, out);
}
bool sameRect(const Rect& a, const Rect& b) {
    auto same = [](float p, float q) { return p == q || std::abs(p - q) < 1e-3f || (std::isnan(p) && std::isnan(q)); };  // (inf == inf)
    return same(a.x, b.x) && same(a.y, b.y) && same(a.width, b.width) && same(a.height, b.height);
}
} // namespace

void detail::setLayoutCheck(bool on) { gLayoutCheck = on; }
void detail::setKeepCheck(bool on) { gKeepCheck = on; }
int64_t detail::frameTimeOnBeat(int64_t last, int64_t now, int64_t period) {
    if (last < 0 || period <= 0) return now;
    return last + std::max<int64_t>(0, (now - last + period / 2) / period) * period;
}
int detail::keepCheckMismatches() { return gKeepMismatches; }
int detail::layoutCheckMismatches() { return gLayoutMismatches; }

void ComposeHost::measureAndPlace() {
    LayoutNode& r = root();
    uint64_t before = detail::relaidOutCount();
    r.measure(Constraints::fixed(viewport_.width, viewport_.height));
    r.place(0, 0);
    r.updateAbsolutePosition({0, 0});
    if (detail::relaidOutCount() == before) {  // nothing measured or moved: the same popups, placed again
        for (auto& [popup, anchor] : overlays_) placeOverlay(*popup, *anchor);
        return;
    }
    overlays_.clear();
    if (detail::nodesWithPopups() > 0) layoutOverlays(r);  // (else no node has any: none to look for)
}

bool ComposeHost::layout() {
    LayoutNode& r = root();
    uint64_t before = detail::relaidOutCount();
    if (std::exchange(layoutEverything_, false) || gFullLayout) measureAllAgain(r);
    measureAndPlace();
    bool changed = detail::relaidOutCount() != before;
    // KOMPOZ_LAYOUT_CHECK (and the tests): every node measured again, which must come out the
    // same; a difference is a change the incremental layout missed. (Not when the layout wrote
    // state, an onSizeChanged say: the next pass lays out again with it, and is checked.)
    bool settled;
    {
        std::lock_guard lock(gMutex);
        settled = pendingChanges_.empty();
    }
    if (gLayoutCheck && settled) {
        // The walk after layout's counts and what it knows of each node, as worked out afresh.
        std::function<int(const LayoutNode&)> recount = [&](const LayoutNode& n) {
            int count = n.reports_ ? 1 : 0;
            for (const auto* list : {&n.children, &n.overlays})
                for (const LayoutNode* child : *list) count += recount(*child);
            bool known = !n.isPlaced() || std::ranges::equal(n.reportCheckedFor_, n.modifier.elements(), {},
                                                             &std::shared_ptr<const ModifierElement>::get,
                                                             &std::shared_ptr<const ModifierElement>::get);
            if (count != n.reportedBelow_ || !known || n.reports_ != (!n.reportsNothing_ || n.stretch || n.indicationNode)) {
                ++gLayoutMismatches;
                std::fprintf(stderr, "kompoz: layout check: node %p '%s' counted %d acted on in it, has %d%s\n", (const void*)&n,
                             n.semanticsText.c_str(), n.reportedBelow_, count, known ? "" : ", its elements unknown");
            }
            return count;
        };
        recount(r);
        std::unordered_map<const LayoutNode*, LaidOut> incremental, full;
        collectLayout(r, incremental);
        // The places as the incremental layout left them written down (some stale: worked out when
        // asked, written down when drawn), as they are again after, for what is drawn and hit next.
        struct Written {
            LayoutNode* node;
            Rect bounds, content;
            std::vector<Rect> layers;
            bool stale;
        };
        std::vector<Written> written;
        std::function<void(LayoutNode&)> keep = [&](LayoutNode& n) {
            Written& w = written.emplace_back(Written{&n, n.bounds_, n.contentBounds_, {}, n.childrenStale_});
            for (const auto& layer : n.layers_) w.layers.push_back(layer.bounds);
            for (const auto* list : {&n.children, &n.overlays})
                for (LayoutNode* child : *list) keep(*child);
        };
        keep(r);
        detail::MeasureEverything everything;  // as it was before layout was incremental
        measureAllAgain(r);
        measureAndPlace();
        collectLayout(r, full);
        for (const Written& w : written) {
            w.node->bounds_ = w.bounds, w.node->contentBounds_ = w.content;
            for (size_t i = 0; i < w.layers.size() && i < w.node->layers_.size(); ++i) w.node->layers_[i].bounds = w.layers[i];
            w.node->setChildrenStale(w.stale);
        }
        int reported = 0;
        for (const auto& [node, laid] : full) {
            auto it = incremental.find(node);
            if (it != incremental.end() && sameRect(it->second.bounds, laid.bounds) && sameRect(it->second.content, laid.content) &&
                it->second.placed == laid.placed && it->second.zIndex == laid.zIndex)
                continue;
            ++gLayoutMismatches;
            if (reported++ < 5) {
                Rect was = it != incremental.end() ? it->second.bounds : Rect{};
                std::fprintf(stderr, "kompoz: layout check: node %p '%s' was %.1f,%.1f %.1fx%.1f, is %.1f,%.1f %.1fx%.1f\n", (const void*)node,
                             node->semanticsText.c_str(), was.x, was.y, was.width, was.height, laid.bounds.x, laid.bounds.y, laid.bounds.width,
                             laid.bounds.height);
            }
        }
    }
    if (auto pending = std::exchange(pendingFocus_, nullptr)) {
        if (LayoutNode* target = findRequester(r, pending.get())) {
            std::vector<LayoutNode*> focusables;
            collectFocusables(*target, focusables);  // the node itself or the first focusable inside
            if (!focusables.empty()) setFocus(restoreFocus(focusables.front()));
        }
    }
    if (std::exchange(bringFocusedIntoView_, false) && focused_) bringIntoView(*focused_);
    if (std::exchange(focusDirty_, false)) notifyFocusChanged();
    return changed;
}

LayoutNode* ComposeHost::findRequester(LayoutNode& node, const FocusRequester::Data* data) {
    for (const auto& e : node.modifier.elements())
        if (auto* r = dynamic_cast<const FocusRequesterElement*>(e.get()); r && r->requester.data_.get() == data) return &node;
    for (auto* child : node.children)
        if (LayoutNode* found = findRequester(*child, data)) return found;
    for (auto* popup : node.overlays)
        if (LayoutNode* found = findRequester(*popup, data)) return found;
    return nullptr;
}

// Focus targets in reading order: text fields, focusable() and enabled clickables.
namespace {
// A focus target's properties: the nearest focusProperties on it or around it, up to the next
// focus target out (Compose's: a field's modifier is on the box around its text).
const FocusProperties* focusPropertiesOf(const LayoutNode& node) {
    for (const LayoutNode* n = &node; n; n = n->overlay ? nullptr : n->parent) {
        if (n != &node && (n->modifier.find<FocusableElement>() || n->modifier.find<ClickableElement>())) break;  // its own
        if (auto* p = n->modifier.find<FocusPropertiesElement>()) return &p->properties;
    }
    return nullptr;
}
bool canFocus(const LayoutNode& node) {
    auto* p = focusPropertiesOf(node);
    return !p || p->canFocus;
}
// A horizontal scrollable in a right-to-left layout: the finger and the scroll go the other way.
bool flipsWithFinger(const LayoutNode& node, int axis, const ScrollableElement& s) {
    return axis == 1 && node.rtl && s.followsLayoutDirection();
}
} // namespace

void ComposeHost::collectFocusables(LayoutNode& node, std::vector<LayoutNode*>& out) {
    for (const auto& e : node.modifier.elements())
        if (e->ignoresInput()) return;
    auto* click = node.modifier.find<ClickableElement>();
    if ((node.modifier.find<FocusableElement>() || (click && click->enabled)) && canFocus(node)) out.push_back(&node);
    for (auto* child : node.children)
        if (child->isPlaced()) collectFocusables(*child, out);
    for (auto* popup : node.overlays) collectFocusables(*popup, out);
}

void ComposeHost::requestFocus(const FocusRequester& requester) {
    pendingFocus_ = requester.data_;
    requestLayout();
    wake();
}

void ComposeHost::freeFocus(const FocusRequester& requester) {
    LayoutNode* node = findRequester(root(), requester.data_.get());
    if (!node || !focused_) return;
    std::vector<LayoutNode*> inside;
    collectFocusables(*node, inside);
    if (std::find(inside.begin(), inside.end(), focused_) != inside.end()) setFocus(nullptr);
}

bool ComposeHost::moveFocus(FocusDirection direction) {
    LayoutNode* popup = inputOverlay();  // a popup on top keeps the focus inside it
    // The focused node may say where this move goes (or that it stays).
    if (auto* f = focused_ ? focusPropertiesOf(*focused_) : nullptr) {
        const std::optional<FocusRequester>* to[] = {&f->next, &f->previous, &f->up, &f->down, &f->left, &f->right};
        if (const auto& target = *to[int(direction)]) {
            if (*target == FocusRequester::cancel()) return false;
            std::vector<LayoutNode*> inside;  // the node with that requester, or the first focusable in it
            if (LayoutNode* node = findRequester(popup ? *popup : root(), target->data_.get())) collectFocusables(*node, inside);
            if (!inside.empty()) {
                setFocus(restoreFocus(inside.front()));
                bringFocusedIntoView_ = true;
                requestLayout();
                return true;
            }
        }
    }
    std::vector<LayoutNode*> order;
    collectFocusables(popup ? *popup : root(), order);
    if (order.empty()) return false;
    auto it = std::find(order.begin(), order.end(), focused_);
    LayoutNode* next = nullptr;
    bool backwards = direction == FocusDirection::Previous || direction == FocusDirection::Up || direction == FocusDirection::Left;
    if (it == order.end()) {
        next = backwards ? order.back() : order.front();
    } else if (direction == FocusDirection::Next) {
        if (it + 1 != order.end()) next = *(it + 1);
    } else if (direction == FocusDirection::Previous) {
        if (it != order.begin()) next = *(it - 1);
    } else {
        next = nearestThatWay(order, focused_, direction);
    }
    if (!next) return false;
    setFocus(restoreFocus(next));
    bringFocusedIntoView_ = true;
    requestLayout();
    return true;
}

namespace {

// How far `to` is from `from` going `direction`, weighing the distance along it more than
// across it (as Android's FocusFinder does); nothing if it isn't that way.
std::optional<float> focusDistance(const Rect& from, const Rect& to, FocusDirection direction) {
    float major = 0, minor = 0;
    bool beyond = false;
    switch (direction) {
    case FocusDirection::Down:
        beyond = (from.y < to.y || from.bottom() <= to.y) && from.bottom() < to.bottom();
        major = to.y - from.bottom(), minor = to.center().x - from.center().x;
        break;
    case FocusDirection::Up:
        beyond = (from.bottom() > to.bottom() || from.y >= to.bottom()) && from.y > to.y;
        major = from.y - to.bottom(), minor = to.center().x - from.center().x;
        break;
    case FocusDirection::Right:
        beyond = (from.x < to.x || from.right() <= to.x) && from.right() < to.right();
        major = to.x - from.right(), minor = to.center().y - from.center().y;
        break;
    case FocusDirection::Left:
        beyond = (from.right() > to.right() || from.x >= to.right()) && from.x > to.x;
        major = from.x - to.right(), minor = to.center().y - from.center().y;
        break;
    default: break;
    }
    if (!beyond) return std::nullopt;
    major = std::max(0.f, major);
    return 13 * major * major + minor * minor;
}

bool isFocusGroup(const LayoutNode& node) { return node.modifier.find<FocusGroupElement>() != nullptr; }
LayoutNode* up(const LayoutNode* node) { return node->overlay ? nullptr : node->parent; }  // not out of a popup

// The innermost focus group around `node` (not itself), or none.
LayoutNode* groupAround(const LayoutNode* node) {
    for (LayoutNode* p = up(node); p; p = up(p))
        if (isFocusGroup(*p)) return p;
    return nullptr;
}
bool within(const LayoutNode* node, const LayoutNode* scope) {
    if (!scope) return true;
    for (const LayoutNode* p = node; p; p = up(p))
        if (p == scope) return true;
    return false;
}
// The outermost group around `node` inside `scope` (none: the node is right in it).
LayoutNode* groupIn(const LayoutNode* node, const LayoutNode* scope) {
    LayoutNode* found = nullptr;
    for (LayoutNode* p = up(node); p && p != scope; p = up(p))
        if (isFocusGroup(*p)) found = p;
    return found;
}

} // namespace

// Where an arrow key takes the focus (Compose's two-dimensional focus search): the nearest
// focusable that way in the focused one's innermost focus group; else, group by group outwards,
// the nearest target there — a focusable, or a whole group (by its bounds) entered at its
// focusable nearest that way (its first if none is). Without groups: the nearest of all.
LayoutNode* ComposeHost::nearestThatWay(const std::vector<LayoutNode*>& order, LayoutNode* focused, FocusDirection direction) {
    Rect from = focusRect(*focused);
    using Targets = std::vector<std::pair<LayoutNode*, Rect>>;
    auto nearest = [&](const Targets& targets) {
        LayoutNode* found = nullptr;
        float least = kInfinity;
        for (const auto& [node, rect] : targets)
            if (auto d = focusDistance(from, rect, direction); d && *d < least) least = *d, found = node;
        return found;
    };
    LayoutNode* scope = groupAround(focused);
    LayoutNode* searched = nullptr;  // the group looked in last: its focusables are done
    while (true) {
        Targets targets;
        std::vector<LayoutNode*> groups;
        for (LayoutNode* candidate : order) {
            if (candidate == focused || !within(candidate, scope)) continue;
            LayoutNode* group = groupIn(candidate, scope);
            if (!group) targets.push_back({candidate, focusRect(*candidate)});
            else if (group != searched && std::find(groups.begin(), groups.end(), group) == groups.end()) {
                groups.push_back(group);
                targets.push_back({group, focusRect(*group)});
            }
        }
        if (LayoutNode* found = nearest(targets)) {
            if (std::find(groups.begin(), groups.end(), found) == groups.end()) return found;
            Targets inside;
            for (LayoutNode* candidate : order)
                if (within(candidate, found)) inside.push_back({candidate, focusRect(*candidate)});
            LayoutNode* entered = nearest(inside);
            return entered ? entered : inside.empty() ? nullptr : inside.front().first;
        }
        if (!scope) return nullptr;
        searched = scope;
        scope = groupAround(scope);
    }
}

void ComposeHost::bringIntoView(LayoutNode& node) { bringIntoView(node, std::nullopt, 1); }

void ComposeHost::bringIntoView(LayoutNode& node, std::optional<Rect> rect, float fraction) {
    Rect target = rect.value_or(focusRect(node));
    for (LayoutNode* n = node.overlay ? nullptr : node.parent; n; n = n->overlay ? nullptr : n->parent) {
        const auto& elements = n->modifier.elements();
        for (size_t i = elements.size(); i-- > 0;) {
            auto* s = dynamic_cast<const ScrollableElement*>(elements[i].get());
            if (!s) continue;
            Rect view = n->layerBounds(i);  // the scrollable's viewport
            bool vertical = s->orientation() == Orientation::Vertical;
            float lo = vertical ? target.y : target.x, hi = vertical ? target.bottom() : target.right();
            float viewLo = vertical ? view.y : view.x, viewHi = vertical ? view.bottom() : view.right();
            // As far as its spec says (by default forward until its end shows, but never past its
            // start, or back to its start), along it: right to left, from the right.
            float sign = !vertical && n->rtl && s->followsLayoutDirection() ? -1.f : 1.f;  // right to left: scrolling on moves it right
            float delta = sign * s->bringIntoViewSpec.scrollDistance(sign > 0 ? lo - viewLo : viewHi - hi, hi - lo, viewHi - viewLo);
            if (delta == 0) continue;
            float asked = delta * fraction;
            float used = sign * s->scrollBy(sign * asked);
            // Those around it see the rect where this one is taking it (all the way, unless it
            // reached an end), not where it is part of the way there.
            (vertical ? target.y : target.x) -= std::abs(used - asked) < 0.01f ? delta : used;
        }
    }
}

// Tells onFocusChanged modifiers whose state changed: isFocused for the focused node,
// hasFocus for it and every node around it.
void ComposeHost::notifyFocusChanged() {
    std::vector<std::pair<std::function<void(FocusState)>, FocusState>> calls;
    auto visit = [&](auto&& self, LayoutNode& node) -> bool {
        bool contains = &node == focused_;
        for (auto* child : node.children) contains = self(self, *child) || contains;
        for (auto* popup : node.overlays) self(self, *popup);  // a popup's focus stays its own
        FocusState state{&node == focused_, contains};
        bool listening = false;
        for (const auto& e : node.modifier.elements())
            if (auto* f = dynamic_cast<const FocusChangedElement*>(e.get()); f && f->onFocusChanged) listening = true;
        if (listening) {
            FocusState before = focusStates_[&node];
            bool changed = !(before == state), concerned = changed || state.hasFocus;  // (onFocusEvent: the focus in it, or leaving)
            focusStates_[&node] = state;
            for (const auto& e : node.modifier.elements())
                if (auto* f = dynamic_cast<const FocusChangedElement*>(e.get()); f && f->onFocusChanged && (f->everyEvent ? concerned : changed))
                    calls.push_back({f->onFocusChanged, state});
        }
        return contains;
    };
    visit(visit, root());
    for (auto& [callback, state] : calls) callback(state);
}

// Popups after the main tree (their anchors have positions by then), each after its anchor's
// subtree, nested popups above the popup they were opened from.
void ComposeHost::layoutOverlays(LayoutNode& node) {
    for (auto* child : node.children) layoutOverlays(*child);
    for (auto* popup : node.overlays) {
        placeOverlay(*popup, node);
        overlays_.push_back({popup, &node});
        layoutOverlays(*popup);
    }
}

void ComposeHost::placeOverlay(LayoutNode& popup, LayoutNode& anchor) {
    const OverlaySpec& spec = *popup.overlay;
    Size size = popup.measure(spec.fullScreen ? Constraints::fixed(viewport_.width, viewport_.height)
                                              : Constraints{0, viewport_.width, 0, viewport_.height});
    Offset at;
    if (!spec.fullScreen)  // (asked every time: where it goes may follow more than its anchor)
        at = spec.position ? spec.position(anchor.bounds(), size, viewport_) : Offset{anchor.bounds().x, anchor.bounds().bottom()};
    popup.place(at.x, at.y);
    popup.updateAbsolutePosition({0, 0});
}

LayoutNode* ComposeHost::inputOverlay() const {
    for (auto it = overlays_.rbegin(); it != overlays_.rend(); ++it)
        if (it->first->overlay->focusable) return it->first;
    return nullptr;
}

struct ComposeHost::DragSession {
    std::shared_ptr<const DragAndDropTransferData> data;
    LayoutNode* source = nullptr;  // null once it left the composition
    Offset grab;                   // where it was grabbed, from its top left
    Offset pointer;
    std::vector<LayoutNode*> targets;  // the ones taking part, bottom to top
    LayoutNode* hovered = nullptr;
};

namespace {
const DragAndDropTargetElement* dropTargetOf(const LayoutNode& node) { return node.modifier.find<DragAndDropTargetElement>(); }
void collectDropTargets(LayoutNode& node, std::vector<LayoutNode*>& out) {  // bottom to top, as drawn
    if (dropTargetOf(node)) out.push_back(&node);
    for (auto* child : node.childrenInDrawOrder())
        if (child->isPlaced()) collectDropTargets(*child, out);
}
} // namespace

void ComposeHost::draw(GraphicsContext& canvas) {
    root().draw(canvas);
    for (auto& [popup, anchor] : overlays_) popup->draw(canvas);
    if (dnd_ && dnd_->source) {  // what is dragged, faded, under the pointer
        LayoutNode& source = *dnd_->source;
        Rect b = source.bounds();
        LayerTransform t;
        t.translationX = dnd_->pointer.x - dnd_->grab.x - b.x;
        t.translationY = dnd_->pointer.y - dnd_->grab.y - b.y;
        canvas.pushLayer(b, 0, 0.75f, t);
        source.settlePlace();  // (drawn on its own, out of the tree's order)
        source.draw(canvas);
        canvas.popLayer();
    }
    // Magnifiers on top of it all: Modifier::magnifier's (while their nodes live and carry it),
    // then the text one.
    std::erase_if(magnifiers_, [&](const std::weak_ptr<LayoutNode*>& weak) {
        auto handle = weak.lock();
        LayoutNode* node = handle ? *handle : nullptr;
        const MagnifierElement* e = nullptr;
        Rect b;  // where the element is (past what comes before it: padding)
        for (size_t i = 0; node && !e && i < node->modifier.elements().size(); ++i)
            if ((e = dynamic_cast<const MagnifierElement*>(node->modifier.elements()[i].get()))) b = node->layerBounds(i);
        if (!e) return true;
        Offset local = e->sourceCenter ? e->sourceCenter() : Offset{std::nanf(""), 0};
        if (std::isnan(local.x) || std::isnan(local.y) || !node->isPlaced()) return false;
        Offset source{b.x + local.x, b.y + local.y};
        Offset center{source.x, source.y - 42};
        if (e->options.magnifierCenter) {
            Offset c = e->options.magnifierCenter();
            center = {b.x + c.x, b.y + c.y};
        }
        drawMagnifier(canvas, source, center, e->options);
        return false;
    });
    if (textMagnifier_) {
        MagnifierOptions text;
        float half = text.size.width / 2;  // kept in the window across
        float x = std::clamp(textMagnifier_->x, half, std::max(half, viewport_.width - half));
        drawMagnifier(canvas, *textMagnifier_, {x, textMagnifier_->y - 42}, text);
    }
}

// The content around `source` zoomed into a rounded rect at `center`: the scene drawn again
// through a layer of the source's surroundings, scaled about its middle and moved there.
void ComposeHost::drawMagnifier(GraphicsContext& canvas, Offset source, Offset center, const MagnifierOptions& o) {
    float zoom = std::max(0.01f, o.zoom);
    Rect loupe{center.x - o.size.width / 2, center.y - o.size.height / 2, o.size.width, o.size.height};
    if (o.elevation > 0) canvas.drawShadow(loupe, o.cornerRadius, Shadow{Color{0, 0, 0, 0.3f}, {0, o.elevation / 2}, o.elevation * 2, 0}, false);
    Rect around{source.x - loupe.width / zoom / 2, source.y - loupe.height / zoom / 2, loupe.width / zoom, loupe.height / zoom};
    LayerTransform t;
    t.scaleX = t.scaleY = zoom;
    t.translationX = center.x - source.x;
    t.translationY = center.y - source.y;
    canvas.pushLayer(around, o.cornerRadius / zoom, 1, t);
    root().draw(canvas);
    canvas.popLayer();
}

ImageBitmap ComposeHost::renderImage(Size size, const std::function<void(GraphicsContext&)>& draw) const {
    return callbacks_.renderImage ? callbacks_.renderImage(size, draw) : ImageBitmap{};
}

void ComposeHost::showTextMagnifier(Offset source) {
    if (textMagnifier_ && textMagnifier_->x == source.x && textMagnifier_->y == source.y) return;
    textMagnifier_ = source;
    requestRedraw();
}
void ComposeHost::hideTextMagnifier() {
    if (!textMagnifier_) return;
    textMagnifier_.reset();
    requestRedraw();
}
void ComposeHost::registerMagnifier(LayoutNode& node) {
    LayoutNode* self = &node;
    if (std::none_of(magnifiers_.begin(), magnifiers_.end(), [&](const auto& w) {
            auto h = w.lock();
            return h && *h == self;
        }))
        magnifiers_.push_back(node.handle());
}

void MagnifierElement::attach(LayoutNode& node) const {
    if (node.host()) node.host()->registerMagnifier(node);
}

bool ComposeHost::startDragAndDrop(Offset p, int64_t time) {
    LayoutNode* source = std::exchange(dndSource_, nullptr);
    dndLongPressAt_ = -1;
    const auto* e = source ? source->modifier.find<DragAndDropSourceElement>() : nullptr;
    if (!e || !e->transferData) return false;
    Rect b = layerBoundsOf(*source, e);
    auto transferData = e->transferData;  // user code
    std::optional<DragAndDropTransferData> data = transferData({downPosition_.x - b.x, downPosition_.y - b.y});
    if (!data) return false;
    // The drag takes the gesture: no click (nothing is pressed), no scroll, handlers get a Cancel.
    cancelPointerTargets(time);
    unpress();
    pointerClaimed_ = true;
    tapFocus_ = {};
    textLongPress_ = longPressTrigger_ = nullptr;
    if (pointerType_ == PointerType::Touch) performHapticFeedback(HapticFeedbackType::LongPress);
    beginDragAndDrop(std::make_shared<const DragAndDropTransferData>(std::move(*data)), source,
                     {downPosition_.x - source->bounds().x, downPosition_.y - source->bounds().y}, p);
    return true;
}

void ComposeHost::beginDragAndDrop(std::shared_ptr<const DragAndDropTransferData> data, LayoutNode* source, Offset grab,
                                   std::optional<Offset> pointer) {
    dnd_ = std::make_unique<DragSession>();
    dnd_->data = std::move(data);
    dnd_->source = source;
    dnd_->grab = grab;
    dnd_->pointer = pointer.value_or(Offset{-1e9f, -1e9f});  // from outside: over nothing until it says where
    std::vector<LayoutNode*> all;
    collectDropTargets(root(), all);
    for (auto& [popup, anchor] : overlays_) collectDropTargets(*popup, all);
    auto eventFor = [this](LayoutNode& n) {
        Rect r = layerBoundsOf(n, dropTargetOf(n));
        return DragAndDropEvent{dnd_->data, {dnd_->pointer.x - r.x, dnd_->pointer.y - r.y}};
    };
    for (LayoutNode* n : all) {
        auto shouldStart = dropTargetOf(*n)->shouldStart;
        if (!shouldStart || shouldStart(eventFor(*n))) dnd_->targets.push_back(n);
    }
    for (LayoutNode* n : std::vector<LayoutNode*>(dnd_->targets))
        if (dnd_ && std::find(dnd_->targets.begin(), dnd_->targets.end(), n) != dnd_->targets.end())
            if (auto onStarted = dropTargetOf(*n)->target.onStarted) onStarted(eventFor(*n));
    if (dnd_ && pointer) moveDragAndDrop(*pointer);
    requestRedraw();
}

void ComposeHost::externalDragEntered(std::vector<std::string> mimeTypes) {
    if (dnd_) endDragAndDrop(false);
    auto data = std::make_shared<DragAndDropTransferData>();
    data->mimeTypes = std::move(mimeTypes);
    beginDragAndDrop(std::move(data), nullptr, {}, std::nullopt);
}

void ComposeHost::externalDragMoved(Offset position) {
    if (dnd_) moveDragAndDrop(position);
}

void ComposeHost::externalDrop(DragAndDropTransferData data, Offset position) {
    if (!dnd_) externalDragEntered(data.mimeTypes);
    if (!dnd_) return;
    dnd_->data = std::make_shared<const DragAndDropTransferData>(std::move(data));  // now it is known what it carries
    moveDragAndDrop(position);
    endDragAndDrop(true);
}

void ComposeHost::externalDragLeft() {
    if (dnd_) endDragAndDrop(false);
}

void ComposeHost::moveDragAndDrop(Offset p) {
    dnd_->pointer = p;
    requestRedraw();
    auto eventFor = [this](LayoutNode& n) {
        Rect r = layerBoundsOf(n, dropTargetOf(n));
        return DragAndDropEvent{dnd_->data, {dnd_->pointer.x - r.x, dnd_->pointer.y - r.y}};
    };
    LayoutNode* over = nullptr;  // the topmost one under the pointer
    for (auto it = dnd_->targets.rbegin(); it != dnd_->targets.rend() && !over; ++it)
        if (layerBoundsOf(**it, dropTargetOf(**it)).contains(p)) over = *it;
    if (over != dnd_->hovered) {
        if (LayoutNode* left = std::exchange(dnd_->hovered, over))
            if (auto onExited = dropTargetOf(*left)->target.onExited) onExited(eventFor(*left));
        if (dnd_ && over)
            if (auto onEntered = dropTargetOf(*over)->target.onEntered) onEntered(eventFor(*over));
    }
    if (dnd_ && dnd_->hovered)
        if (auto onMoved = dropTargetOf(*dnd_->hovered)->target.onMoved) onMoved(eventFor(*dnd_->hovered));
}

void ComposeHost::endDragAndDrop(bool drop) {
    std::unique_ptr<DragSession> s = std::move(dnd_);
    auto eventFor = [&s](LayoutNode& n, bool accepted) {
        Rect r = n.bounds();
        if (const auto* e = dropTargetOf(n)) r = n.boundsOf(e).value_or(r);
        return DragAndDropEvent{s->data, {s->pointer.x - r.x, s->pointer.y - r.y}, accepted};
    };
    bool accepted = false;
    if (s->hovered) {
        if (drop) {
            if (auto onDrop = dropTargetOf(*s->hovered)->target.onDrop) accepted = onDrop(eventFor(*s->hovered, false));
        } else if (auto onExited = dropTargetOf(*s->hovered)->target.onExited) {
            onExited(eventFor(*s->hovered, false));
        }
    }
    for (LayoutNode* n : s->targets)
        if (const auto* t = dropTargetOf(*n); t && t->target.onEnded) {
            auto onEnded = t->target.onEnded;
            onEnded(eventFor(*n, accepted));
        }
    requestRedraw();
}

void ComposeHost::invalidate() {
    rootInvalid_ = true;
    if (callbacks_.wake) callbacks_.wake();
}

// Called with the global host mutex held.
void ComposeHost::stateChanged(const void* key) {
    pendingChanges_.push_back(key);
    wake();
}

void ComposeHost::wake() {
    if (callbacks_.wake) callbacks_.wake();
}

void ComposeHost::requestLayout() {
    layoutDirty_ = true;
    layoutEverything_ = true;
}
void ComposeHost::requestLayoutFrame() {
    layoutAnimating_ = true;
    if (LayoutNode* node = detail::measuringNode()) animatingLayouts_.push_back(node->handle());
    else layoutEverything_ = true;
}

void ComposeHost::recordLayoutRead(const void* key, LayoutNode& node) {
    if (std::find(node.layoutReads_.begin(), node.layoutReads_.end(), key) != node.layoutReads_.end()) return;
    node.layoutReads_.push_back(key);
    layoutReaders_[key].push_back(&node);
}
void ComposeHost::forgetLayoutReads(LayoutNode& node) {
    for (const void* key : node.layoutReads_) {
        auto it = layoutReaders_.find(key);
        if (it == layoutReaders_.end()) continue;
        std::erase(it->second, &node);
        if (it->second.empty()) layoutReaders_.erase(it);
    }
    node.layoutReads_.clear();
}
void ComposeHost::invalidateLayoutReaders(const void* key) {
    auto it = layoutReaders_.find(key);
    if (it == layoutReaders_.end()) return;
    for (LayoutNode* node : it->second) node->invalidateMeasure();
    layoutDirty_ = true;
}
void ComposeHost::recordDrawRead(const void* key, LayoutNode& node) {
    if (std::find(node.drawReads_.begin(), node.drawReads_.end(), key) != node.drawReads_.end()) return;
    node.drawReads_.push_back(key);
    drawReaders_[key].push_back(&node);
}
void ComposeHost::forgetDrawReads(LayoutNode& node) {
    for (const void* key : node.drawReads_) {
        auto it = drawReaders_.find(key);
        if (it == drawReaders_.end()) continue;
        std::erase(it->second, &node);
        if (it->second.empty()) drawReaders_.erase(it);
    }
    node.drawReads_.clear();
}
void ComposeHost::invalidateDrawReaders(const void* key) {
    auto it = drawReaders_.find(key);
    if (it == drawReaders_.end()) return;
    for (LayoutNode* node : it->second) node->invalidateDraw();
    redraw_ = true;
}
ImageBitmap ComposeHost::loadImage(std::string_view path) {
    std::string key(path);
    if (auto it = images_.find(key); it != images_.end()) {
        it->second.used = ++imageUses_;
        return it->second.image;
    }
    ImageBitmap image = callbacks_.loadImage ? callbacks_.loadImage(path) : ImageBitmap{};
    if (image.empty()) return image;
    size_t bytes = size_t(image.width()) * size_t(image.height()) * 4;
    images_[key] = {image, bytes, ++imageUses_};
    imageBytes_ += bytes;
    while (imageBytes_ > kImageCacheBytes && images_.size() > 1) {  // the least recently used go (not the one just loaded)
        auto oldest = std::min_element(images_.begin(), images_.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
        imageBytes_ -= oldest->second.bytes;
        images_.erase(oldest);
    }
    return image;
}

void ComposeHost::requestRedraw() {
    // Asked as a node is drawn (an animation drawn on): the next frame draws that node again.
    if (LayoutNode* drawing = detail::drawingNode(); drawing && drawing->host() == this) drawing->invalidateDraw();
    redraw_ = true;
}

Rect ComposeHost::touchBoundsOf(const LayoutNode& node, const ModifierElement* element) const {
    Rect r = hitBoundsOf(node, element);
    return pointerType_ == PointerType::Touch ? touchTarget(r, viewConfiguration_.minimumTouchTargetSize) : r;
}

void ComposeHost::addNearTouchTarget(std::vector<Hit>& hits, Offset p) {
    auto tappable = [](const ModifierElement* e) {
        auto* click = elementOf<ClickableElement>(e);
        return (click && click->enabled) || elementOf<PointerInputElement>(e);
    };
    // Inside what was touched (a card, a list; what it holds was missed): a small target near
    // the touch takes it, as in Compose.
    LayoutNode* popup = inputOverlay();
    LayoutNode* within = !hits.empty() ? hits.front().node : popup ? popup : &root();
    Size min = viewConfiguration_.minimumTouchTargetSize;
    LayoutNode* best = nullptr;
    float bestDistance = 0;
    // Topmost first; what a clip cuts off can't be touched.
    std::function<void(LayoutNode&)> search = [&](LayoutNode& node) {
        const auto& elements = node.modifier.elements();
        for (size_t i = 0; i < elements.size(); ++i) {
            if (elements[i]->clipsContent() && !touchTarget(elements[i]->clipBounds(node.layerBounds(i)), min).contains(p)) return;
            if (elements[i]->ignoresInput()) return;  // leaving content
        }
        const auto& children = node.childrenInDrawOrder();
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            if ((*it)->isPlaced()) search(**it);
        if (&node == within) return;
        for (size_t i = 0; i < elements.size(); ++i) {
            if (!tappable(elements[i].get())) continue;
            Rect b = hitBounds(node, i);
            if (!touchTarget(b, min).contains(p)) continue;  // (a big one's is its bounds)
            float dx = std::max({0.f, b.x - p.x, p.x - b.right()}), dy = std::max({0.f, b.y - p.y, p.y - b.bottom()});
            if (!best || dx * dx + dy * dy < bestDistance) best = &node, bestDistance = dx * dx + dy * dy;
        }
    };
    search(*within);
    if (!best) return;
    std::vector<Hit> near;  // its elements, innermost first, as if it had been touched
    const auto& elements = best->modifier.elements();
    for (size_t i = elements.size(); i-- > 0;)
        if (tappable(elements[i].get())) near.push_back({best, elements[i].get()});
    hits.insert(hits.begin(), near.begin(), near.end());
}

std::vector<ComposeHost::Hit> ComposeHost::hitTest(Offset position) {
    std::vector<Hit> hits;
    // From the top: a focusable popup takes all input; a touchable one what lands on its own
    // interactive elements; the others let it through.
    for (auto it = overlays_.rbegin(); it != overlays_.rend(); ++it) {
        LayoutNode* popup = it->first;
        if (popup->overlay->focusable) {
            collectHits(*popup, position, hits);
            return hits;
        }
        if (popup->overlay->touchable && popup->bounds().contains(position)) {
            collectHits(*popup, position, hits);
            if (!hits.empty()) return hits;
        }
    }
    collectHits(root(), position, hits);
    return hits;
}

// Collects interactive elements under `p`, innermost first.
void ComposeHost::collectHits(LayoutNode& node, Offset p, std::vector<Hit>& out) {
    const auto& elements = node.modifier.elements();
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i]->clipsContent() && !elements[i]->clipBounds(node.layerBounds(i)).contains(p)) return;  // clipped away
        if (elements[i]->ignoresInput()) return;                                       // leaving content
    }

    const auto& children = node.childrenInDrawOrder();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (!(*it)->isPlaced()) continue;
        size_t before = out.size();
        collectHits(**it, p, out);
        if (out.size() != before) break;  // the topmost interactive child wins
    }
    for (size_t i = elements.size(); i-- > 0;) {
        const ModifierElement* e = elements[i].get();
        bool interactive = elementOf<ClickableElement>(e) || elementOf<ScrollableElement>(e) ||
                           elementOf<FocusableElement>(e) || elementOf<PointerInputElement>(e) ||
                           elementOf<TooltipTriggerElement>(e) || elementOf<ZoomElement>(e) || elementOf<DragAndDropSourceElement>(e) ||
                           elementOf<HoverableElement>(e) || elementOf<PointerHoverIconElement>(e) ||
                           elementOf<TextFieldDecorationElement>(e) || elementOf<ContextMenuElement>(e) ||
                           elementOf<PointerEventElement>(e);
        if (interactive && hitBounds(node, i).contains(p)) out.push_back({&node, e});
    }
}

void ComposeHost::deliverPass(std::vector<PointerTarget> targets, PointerEventType type, int64_t pointerId,
                              const std::shared_ptr<bool>& consumed, int64_t time, PointerEventPass pass) {
    for (auto it = targets.rbegin(); it != targets.rend(); ++it) deliver(*it, type, pointerId, consumed, time, pass);
}

void ComposeHost::deliver(PointerTarget& target, PointerEventType type, int64_t pointerId,
                          const std::shared_ptr<bool>& consumed, int64_t time, PointerEventPass pass) {
    LayoutNode& node = *target.node;
    detail::syncPointerInputs(node);  // the node may have recomposed since the press
    if (std::find(node.pointerInputs.begin(), node.pointerInputs.end(), target.state) == node.pointerInputs.end())
        return;  // its key changed: the old block is gone
    Rect b = layerBoundsOf(node, target.state->element.get());
    target.state->size = {b.width, b.height};
    PointerEvent e;
    e.type = type;
    e.keyboardModifiers = keyboardModifiers_;  // as Compose's (Shift and a click, say)
    e.mouse = pointerType_ == PointerType::Mouse;
    if (e.mouse && (type == PointerEventType::Press || type == PointerEventType::Release)) e.button = PointerButton::Primary;
    std::vector<TrackedPointer> fingers = pointers_;
    if (fingers.empty()) fingers.push_back({pointerId, lastPosition_, lastPosition_, false, true});
    for (const TrackedPointer& f : fingers) {
        PointerInputChange c;
        c.id = f.id;
        c.position = {f.position.x - b.x, f.position.y - b.y};
        c.previousPosition = {f.previous.x - b.x, f.previous.y - b.y};
        c.pressed = type != PointerEventType::Cancel && f.pressed;
        c.previousPressed = f.previousPressed;
        c.uptimeMillis = timeOr(time) / 1'000'000;
        c.touch = pointerType_ == PointerType::Touch;
        c.consumed_ = consumed;
        e.changes.push_back(c);
        if (f.id == pointerId) e.change = c;
    }
    auto state = target.state;  // keep it alive while its block runs
    state->dispatch(*dispatcher_, e, pass);
    requestRedraw();
}

ComposeHost::TrackedPointer* ComposeHost::trackedPointer(int64_t id) {
    for (auto& f : pointers_)
        if (f.id == id) return &f;
    return nullptr;
}

void ComposeHost::commitPointers() {
    for (auto& f : pointers_) {
        f.previous = f.position;
        f.previousPressed = f.pressed;
    }
}

void ComposeHost::cancelPointerTargets(int64_t time) {
    auto targets = std::move(pointerTargets_);
    pointerTargets_.clear();
    auto consumed = std::make_shared<bool>(false);
    int64_t id = pointers_.empty() ? 0 : pointers_.front().id;
    deliverPass(targets, PointerEventType::Cancel, id, consumed, time, PointerEventPass::Initial);
    for (auto& t : targets) deliver(t, PointerEventType::Cancel, id, consumed, time);
    deliverPass(targets, PointerEventType::Cancel, id, consumed, time, PointerEventPass::Final);
}

void ComposeHost::startInteraction(std::optional<ActiveInteraction>& slot, const std::optional<MutableInteractionSource>& source,
                                   const Interaction& start, const LayoutNode* node) {
    if (!source) return;
    slot = ActiveInteraction{*source, start, node};
    source->tryEmit(start);
}

void ComposeHost::endInteraction(std::optional<ActiveInteraction>& slot, Interaction (Interaction::*end)() const) {
    if (auto a = std::exchange(slot, std::nullopt)) a->source.tryEmit((a->start.*end)());
}

void ComposeHost::startRipple(const LayoutNode& node, const ClickableElement& click, Offset origin, int64_t time) {
    if (click.indication.kind != Indication::Kind::Ripple) return;
    ripples_.push_back({&node, origin, time});
    requestRedraw();
}

void ComposeHost::releaseRipples(const LayoutNode* node, int64_t time) {
    for (Ripple& r : ripples_)
        if (r.node == node && r.released < 0) r.released = time;
    requestRedraw();
}

std::optional<ComposeHost::RippleDraw> ComposeHost::rippleAt(const Rect& b, const Indication& indication, Offset origin, int64_t start,
                                                             int64_t released, int64_t now) {
    // Material's: from 0.3 of the larger side to the farthest corner, the center moving to the
    // middle, in 225 ms; faded in over 75 ms, out over 150 ms once let go and spread.
    float t = float(now - start);
    float spread = std::clamp(t / float(kRippleEnter), 0.f, 1.f);
    Offset middle{b.width / 2, b.height / 2};
    float to = indication.radius > 0 ? indication.radius : std::hypot(b.width, b.height) / 2;
    float from = std::min(to, std::max(b.width, b.height) * 0.3f);
    Offset o = indication.bounded ? origin : middle;
    float alpha = kRippleAlpha * std::clamp(t / float(kRippleFadeIn), 0.f, 1.f);
    if (released >= 0) {
        int64_t fade = std::max(released, start + kRippleEnter);
        alpha *= 1 - std::clamp(float(now - fade) / float(kRippleFadeOut), 0.f, 1.f);
    }
    if (!(alpha > 0)) return std::nullopt;
    return RippleDraw{{b.x + o.x + (middle.x - o.x) * spread, b.y + o.y + (middle.y - o.y) * spread},
                      lerp(from, to, FastOutSlowInEasing(spread)), alpha};
}

std::vector<ComposeHost::RippleDraw> ComposeHost::ripples(const LayoutNode& node, const Rect& b, const Indication& indication) const {
    std::vector<RippleDraw> out;
    for (const Ripple& r : ripples_)
        if (r.node == &node)
            if (auto d = rippleAt(b, indication, r.origin, r.start, r.released, lastFrameTime_)) out.push_back(*d);
    return out;
}

void ComposeHost::startDelayedPress(int64_t time) {
    if (!pressed_ || delayedPressAt_ < 0) return;
    delayedPressAt_ = -1;
    auto* click = pressed_->modifier.find<ClickableElement>();
    startInteraction(pressInteraction_, sourceOf(*pressed_, click), Interaction::press(delayedPressPoint_), pressed_);
    startRipple(*pressed_, *click, delayedPressPoint_, time);
}

void ComposeHost::unpress() {
    if (pressed_) releaseRipples(pressed_, lastFrameTime_);
    if (pressed_) {
        pressed_->invalidateDraw();
        std::exchange(pressed_, nullptr)->pressed = false;
    }
    endInteraction(pressInteraction_, &Interaction::cancel);
    longPressAt_ = triggerLongPressAt_ = textLongPressAt_ = -1;
    requestRedraw();
}

void ComposeHost::pointerDown(Offset p, int64_t time, int64_t pointerId, PointerType type) {
    dispatcher_->advanceTo(timeOr(time));
    if (swallowGesture_) return;  // a finger joining a press that only closed a popup
    if (pointerDown_) {           // another finger: it joins the gesture
        if (trackedPointer(pointerId)) return;
        pointers_.push_back({pointerId, p, p});
        unpress();  // two fingers are no tap
        tapFocus_ = {};
        if (handOverToPinch(time)) return;
        auto consumed = std::make_shared<bool>(false);
        auto targets = pointerTargets_;  // handlers run user code
        deliverPass(targets, PointerEventType::Press, pointerId, consumed, time, PointerEventPass::Initial);
        for (auto& t : targets) deliver(t, PointerEventType::Press, pointerId, consumed, time);
        deliverPass(targets, PointerEventType::Press, pointerId, consumed, time, PointerEventPass::Final);
        commitPointers();
        requestRedraw();
        return;
    }
    if (LayoutNode* popup = inputOverlay()) {
        const OverlaySpec& spec = *popup->overlay;
        if (!spec.fullScreen && spec.dismissOnOutsideClick && !popup->bounds().contains(p)) {
            swallowGesture_ = true;  // the press only closes the popup
            if (auto dismiss = spec.onDismissRequest) dismiss();
            requestRedraw();
            return;
        }
    }
    stopFling();  // touching a moving list stops it
    stopWheel();
    cancelPointerTargets(time);  // a gesture that never got its release
    pointerType_ = type;
    if (type == PointerType::Touch && mouseOver_) {
        mouseOver_ = false;
        updateHover(p);  // a finger hovers nothing: what the mouse did ends
    }
    endInteraction(dragInteraction_, &Interaction::cancel);
    setUserScrolled(nullptr, 0);
    pointers_ = {{pointerId, p, p}};
    pointerDown_ = true;
    pointerClaimed_ = longClicked_ = false;
    dragAxis_ = -1;
    samples_.clear();
    downPosition_ = lastPosition_ = p;
    firePointerEvent(PointerEventType::Press, p, type == PointerType::Mouse ? std::optional(PointerButton::Primary) : std::nullopt);
    unpress();
    dndSource_ = nullptr;
    dndLongPressAt_ = -1;
    scrollNodes_[0] = scrollNodes_[1] = nullptr;
    LayoutNode* focusTarget = nullptr;
    // Innermost first: pointerInput handlers get the press in order, and a clickable takes
    // (consumes) it unless a handler inside it already did.
    auto consumed = std::make_shared<bool>(false);
    std::vector<Hit> hits = hitTest(p);
    if (type == PointerType::Touch) addNearTouchTarget(hits, p);
    // The handlers under the press, found first (delivering may rebuild a node's list), each
    // marked with its hit; the Initial pass goes to them before anything else sees the press.
    std::vector<size_t> targetHit;
    for (size_t i = 0; i < hits.size(); ++i)
        if (auto* input = elementOf<PointerInputElement>(hits[i].element)) {
            detail::syncPointerInputs(*hits[i].node);
            for (const auto& state : hits[i].node->pointerInputs)
                if (state->element.get() == input) {
                    pointerTargets_.push_back({hits[i].node, state});
                    targetHit.push_back(i);
                }
        }
    deliverPass(pointerTargets_, PointerEventType::Press, pointerId, consumed, time, PointerEventPass::Initial);
    std::vector<PointerTarget> pressTargets = pointerTargets_;
    size_t delivered = 0;  // the Main pass, innermost first, as the hits come
    for (size_t hi = 0; hi < hits.size(); ++hi) {
        const Hit& hit = hits[hi];
        for (; delivered < pressTargets.size() && targetHit[delivered] == hi; ++delivered)
            deliver(pressTargets[delivered], PointerEventType::Press, pointerId, consumed, time);
        if (elementOf<TooltipTriggerElement>(hit.element) && triggerLongPressAt_ < 0) {
            longPressTrigger_ = hit.node;
            triggerLongPressAt_ = timeOr(time) + longPressNanos();
        }
        if (elementOf<DragAndDropSourceElement>(hit.element) && !dndSource_) {  // a finger holds it first, the mouse just drags
            dndSource_ = hit.node;
            if (type == PointerType::Touch) dndLongPressAt_ = timeOr(time) + longPressNanos();
        }
        if (auto* click = elementOf<ClickableElement>(hit.element); click && click->enabled && !pressed_ && !*consumed) {
            pressed_ = hit.node;
            clickOrder_ = delivered;
            *consumed = true;
            if (click->onLongClick) longPressAt_ = timeOr(time) + longPressNanos();
        }
        if (auto* s = elementOf<ScrollableElement>(hit.element); s && s->userScrollEnabled) {
            int axis = axisOf(s->orientation());
            if (!scrollNodes_[axis]) {
                scrollNodes_[axis] = hit.node;
                scrollOrder_[axis] = delivered;
            }
        }
        if (auto* f = elementOf<FocusableElement>(hit.element); f && f->onEdit && !focusTarget) focusTarget = hit.node;
    }
    deliverPass(pressTargets, PointerEventType::Press, pointerId, consumed, time, PointerEventPass::Final);
    // A text field's decoration (an icon) that nothing else took: a tap on the field.
    if (!focusTarget && !pressed_ && !*consumed)
        for (const Hit& hit : hits)
            if (elementOf<TextFieldDecorationElement>(hit.element)) {
                focusTarget = textFieldIn(*hit.node);
                break;
            }
    samples_.push_back({timeOr(time), 0});
    if (pendingClick_ && pendingClick_ != pressed_) fireClick(nullptr, time);  // another node: no double click
    secondPress_ = pendingClick_ && pendingClick_ == pressed_;
    if (pressed_) {
        pressed_->pressed = true;
        pressed_->invalidateDraw();
    }
    // The press, told to the clickable's interaction source (or a text field's).
    if (pressed_) {
        auto* click = pressed_->modifier.find<ClickableElement>();
        Rect b = layerBoundsOf(*pressed_, click);
        delayedPressPoint_ = {p.x - b.x, p.y - b.y};
        delayedPressAt_ = timeOr(time);
        // In something the finger scrolls: not until the tap timeout (it may be a scroll).
        if (type == PointerType::Touch && (scrollNodes_[0] || scrollNodes_[1])) delayedPressAt_ += kTapTimeoutNanos;
        else startDelayedPress(timeOr(time));
    } else if (focusTarget) {
        Rect b = focusTarget->bounds();
        startInteraction(pressInteraction_, focusTarget->modifier.find<FocusableElement>()->interactionSource,
                         Interaction::press({p.x - b.x, p.y - b.y}), focusTarget);
    }
    if (!focusTarget && focused_ && !isTextField(focused_)) setFocus(nullptr);  // pointing ends keyboard focus
    // A press outside the touch selection's handles and toolbar ends it; a long one on a text
    // field selects a word.
    auto inPopup = [](LayoutNode* n) {
        for (; n; n = n->parent)
            if (n->overlay) return true;
        return false;
    };
    if (touchSelection_) {
        auto hits = hitTest(p);
        if (hits.empty() || !inPopup(hits.front().node)) setTouchSelection(false);
    }
    textLongPress_ = type == PointerType::Touch ? focusTarget : nullptr;
    textLongPressAt_ = textLongPress_ ? timeOr(time) + longPressNanos() : -1;
    textLongPressPoint_ = p;
    mouseSelection_.reset();
    if (type == PointerType::Mouse && focusTarget && !pressed_ && startMouseSelection(*focusTarget, p, time)) {
        tapFocus_ = {};  // taken on the press: the release places nothing
    } else {
        // A text field takes the focus on the release, if this was a tap: swiping a page over a
        // field doesn't bring up the keyboard (Compose focuses on the tap too).
        tapFocus_ = {true, focusTarget, p, !focusTarget && !pressed_ && pointerTargets_.empty()};
    }
    commitPointers();
    requestRedraw();
}

void ComposeHost::selectWordAt(LayoutNode& field, Offset point) {
    auto* f = field.modifier.find<FocusableElement>();
    if (!f || focused_ != &field) return;
    int offset = imeState_.selectionEnd;
    if (f->offsetAt) {
        Rect b = layerBoundsOf(field, f);
        offset = std::clamp(f->offsetAt({point.x - b.x, point.y - b.y}), 0, int(imeState_.text.size()));
    }
    auto [from, to] = detail::wordAround(imeState_.text, offset);  // the word around it
    TextEditState s = imeState_;
    s.selectionStart = from;
    s.selectionEnd = to;
    s.compositionStart = s.compositionEnd = -1;
    applyEdit(s);
}

bool ComposeHost::startMouseSelection(LayoutNode& field, Offset p, int64_t time) {
    auto* f = field.modifier.find<FocusableElement>();
    if (!f || !f->offsetAt) return false;
    int64_t now = timeOr(time);
    // (Compose's ClicksCounter: press to press, within the double tap timeout.)
    int64_t timeout = std::chrono::duration_cast<std::chrono::nanoseconds>(viewConfiguration_.doubleTapTimeout).count();
    bool again = lastMousePressField_ == &field && lastMousePressTime_ >= 0 && now - lastMousePressTime_ < timeout &&
                 std::hypot(p.x - lastMousePressPoint_.x, p.y - lastMousePressPoint_.y) < viewConfiguration_.touchSlop;
    mouseClicks_ = again ? mouseClicks_ + 1 : 1;
    lastMousePressField_ = &field, lastMousePressTime_ = now, lastMousePressPoint_ = p;
    bool extend = keyboardModifiers_.shift && focused_ == &field && mouseClicks_ == 1;
    if (focused_ != &field) setFocus(&field, true);
    else showTextInput();  // a keyboard the user dismissed comes back
    if (focused_ != &field) return false;
    MouseTextSelection s{&field, 0, 0, std::min(mouseClicks_, 3)};
    if (extend) {  // Shift: from where the selection started
        s.anchorStart = s.anchorEnd = imeState_.selectionStart;
    } else {
        Rect b = layerBoundsOf(field, f);
        int offset = std::clamp(f->offsetAt({p.x - b.x, p.y - b.y}), 0, int(imeState_.text.size()));
        auto [from, to] = s.clicks == 1 ? std::pair{offset, offset} : s.clicks == 2 ? detail::wordAround(imeState_.text, offset) : detail::paragraphAround(imeState_.text, offset);
        s.anchorStart = from, s.anchorEnd = to;
    }
    mouseSelection_ = s;
    if (extend) {
        extendMouseSelection(p);
    } else {
        TextEditState e = imeState_;
        e.selectionStart = s.anchorStart, e.selectionEnd = s.anchorEnd;
        e.compositionStart = e.compositionEnd = -1;
        if (!(e == imeState_)) applyEdit(e);
    }
    return true;
}

void ComposeHost::extendMouseSelection(Offset p) {
    const MouseTextSelection& s = *mouseSelection_;
    auto* f = s.field && s.field == focused_ ? s.field->modifier.find<FocusableElement>() : nullptr;
    if (!f || !f->offsetAt) return;
    Rect b = layerBoundsOf(*s.field, f);
    int offset = std::clamp(f->offsetAt({p.x - b.x, p.y - b.y}), 0, int(imeState_.text.size()));
    // Words or paragraphs: the whole one reached; before the anchor, from the anchor's end back.
    auto [from, to] = s.clicks == 1 ? std::pair{offset, offset} : s.clicks == 2 ? detail::wordAround(imeState_.text, offset) : detail::paragraphAround(imeState_.text, offset);
    TextEditState e = imeState_;
    if (from < s.anchorStart) e.selectionStart = s.anchorEnd, e.selectionEnd = from;
    else e.selectionStart = s.anchorStart, e.selectionEnd = to;
    e.compositionStart = e.compositionEnd = -1;
    if (!(e == imeState_)) applyEdit(e);
}

void ComposeHost::applyTapFocus() {
    TapFocus tap = std::exchange(tapFocus_, {});
    if (!tap.armed) return;
    LayoutNode* field = tap.field;
    if (!field) {
        if (tap.clears) setFocus(nullptr);  // tapping empty space clears the focus
        return;
    }
    // The mouse's second click on a field, close by and soon: the word under it.
    int64_t now = dispatcher_->now();
    bool doubleClick = pointerType_ == PointerType::Mouse && lastTapField_ == field && lastTapTime_ >= 0 &&
                       now - lastTapTime_ < 400'000'000 && std::hypot(tap.at.x - lastTapPoint_.x, tap.at.y - lastTapPoint_.y) < viewConfiguration_.touchSlop;
    lastTapField_ = pointerType_ == PointerType::Mouse && !doubleClick ? field : nullptr;
    lastTapTime_ = now;
    lastTapPoint_ = tap.at;
    if (doubleClick && field == focused_) return selectWordAt(*field, tap.at);
    if (field == focused_)
        showTextInput();  // bring back a keyboard the user dismissed
    else
        setFocus(field, true);
    auto* f = field->modifier.find<FocusableElement>();
    if (f && f->offsetAt && focused_ == field) {  // the caret goes where the text was tapped
        Rect b = layerBoundsOf(*field, f);
        int offset = std::clamp(f->offsetAt({tap.at.x - b.x, tap.at.y - b.y}), 0, int(imeState_.text.size()));
        TextEditState s = imeState_;
        s.selectionStart = s.selectionEnd = offset;
        s.compositionStart = s.compositionEnd = -1;
        if (!(s == imeState_)) applyEdit(s);
        // A finger's tap on text: a caret handle to drag it with (Android's).
        if (pointerType_ == PointerType::Touch && !f->readOnly && !imeState_.text.empty()) setTouchSelection(true, false);
    }
}

void ComposeHost::pointerMove(Offset p, int64_t time, int64_t pointerId) {
    dispatcher_->advanceTo(timeOr(time));
    if (swallowGesture_) {
        lastPosition_ = p;
        return;
    }
    TrackedPointer* finger = pointerDown_ ? trackedPointer(pointerId) : nullptr;
    if (pointerDown_ && !finger) return;  // a finger that isn't part of the gesture
    if (finger) finger->position = p;
    if (finger && mouseSelection_) {  // selecting text: the move is the selection's
        lastPosition_ = p;
        extendMouseSelection(p);
        requestRedraw();
        return;
    }
    if (finger && finger != &pointers_.front()) {  // a following finger: to the handlers only
        if (dnd_) return;
        deliverMove(pointerId, p, time, -1);
        commitPointers();
        return;
    }
    if (dnd_) {  // a drag and drop under way takes every move
        moveDragAndDrop(p);
        lastPosition_ = p;
        commitPointers();
        return;
    }
    if (dndSource_ && pointerDown_ && pointers_.size() == 1 && dragAxis_ < 0 && !pointerClaimed_ &&
        std::hypot(p.x - downPosition_.x, p.y - downPosition_.y) > viewConfiguration_.touchSlop) {
        if (pointerType_ == PointerType::Mouse && startDragAndDrop(p, time)) {
            lastPosition_ = p;
            commitPointers();
            return;
        }
        dndSource_ = nullptr;  // a finger moved before holding long enough: a scroll or another drag
        dndLongPressAt_ = -1;
    }
    // Whoever moves past the touch slop first takes the gesture, innermost first: the handlers
    // inside the scrollable the pointer moves along, then that scrollable, then the handlers
    // around it. (A scrollable only goes before outer handlers when the move is mostly along it;
    // several fingers down aren't a scroll.)
    bool deciding = pointerDown_ && dragAxis_ < 0 && !pointerClaimed_ && pointers_.size() == 1;
    int axis = deciding ? scrollAxis(p, true) : -1;
    if (pointerDown_ && !pointerTargets_.empty())
        deliverMove(pointerId, p, time, axis);
    else if (axis >= 0)
        startDrag(axis, p, time);
    // Nobody took it: a scrollable also takes a move that is only partly along it.
    if (pointerDown_ && dragAxis_ < 0 && !pointerClaimed_ && pointers_.size() == 1)
        if (int a = scrollAxis(p, false); a >= 0) startDrag(a, p, time);
    if (pointerDown_ && dragAxis_ >= 0) {
        if (LayoutNode* scrolled = scrollNodes_[dragAxis_]) {  // null if it left the composition mid-drag
            float delta = component(dragAxis_, Offset{p.x - lastPosition_.x, p.y - lastPosition_.y});
            dragTravel_ += std::abs(delta);
            if (auto effect = pointerType_ == PointerType::Touch ? overscrollOf(*scrolled, dragAxis_) : nullptr) {
                int along = dragAxis_;  // a stretch goes back first, what nobody used stretches it
                effect->applyToScroll(alongAxis(along, delta), NestedScrollSource::UserInput, [&](Offset rest) {
                    return alongAxis(along, nestedScroll(*scrolled, along, component(along, rest), NestedScrollSource::UserInput));
                });
            } else {
                nestedScroll(*scrolled, dragAxis_, delta, NestedScrollSource::UserInput);
            }
        }
        int64_t t = timeOr(time);
        samples_.push_back({t, dragAxis_ == 0 ? p.y : p.x});
        std::erase_if(samples_, [&](const Sample& s) { return t - s.time > 100'000'000; });
    }
    lastPosition_ = p;
    if (pointerDown_) {
        commitPointers();
    } else {
        mouseOver_ = true;  // moving without a press: a mouse
        updateHover(p);
    }
    if (!pointerDown_ || pointers_.front().id == pointerId) firePointerEvent(PointerEventType::Move, p, std::nullopt);
}

void ComposeHost::deliverMove(int64_t pointerId, Offset p, int64_t time, int axis) {
    auto consumed = std::make_shared<bool>(false);
    auto targets = pointerTargets_;  // handlers run user code
    deliverPass(targets, PointerEventType::Move, pointerId, consumed, time, PointerEventPass::Initial);
    size_t claimer = targets.size();  // the first (innermost) handler that consumed the move
    auto deliverRange = [&](size_t from, size_t to) {
        for (size_t i = from; i < to; ++i) {
            bool before = *consumed;
            deliver(targets[i], PointerEventType::Move, pointerId, consumed, time);
            if (!before && *consumed && claimer == targets.size()) claimer = i;
        }
    };
    size_t inner = axis >= 0 ? std::min(scrollOrder_[axis], targets.size()) : targets.size();
    deliverRange(0, inner);
    if (axis >= 0 && !*consumed)
        startDrag(axis, p, time);  // the outer handlers never see this move: they get a Cancel
    else
        deliverRange(inner, targets.size());
    if (*consumed && dragAxis_ < 0) {  // a drag handler took the gesture
        pointerClaimed_ = true;
        unpress();
    }
    // Handlers inside the one that took the gesture saw the move unconsumed (there is no
    // second pass as in Compose): they get a Cancel, so a tap under a drag doesn't fire.
    if (claimer > 0 && claimer < targets.size()) {
        auto cancelled = std::make_shared<bool>(false);
        for (size_t i = 0; i < claimer; ++i) {
            for (PointerEventPass pass : {PointerEventPass::Initial, PointerEventPass::Main, PointerEventPass::Final})
                deliver(targets[i], PointerEventType::Cancel, pointerId, cancelled, time, pass);  // whichever it waits in
            std::erase_if(pointerTargets_, [&](const PointerTarget& t) { return t.state == targets[i].state; });
        }
        clickOrder_ = clickOrder_ > claimer ? clickOrder_ - claimer : 0;
    }
    deliverPass(pointerTargets_, PointerEventType::Move, pointerId, consumed, time, PointerEventPass::Final);
}

// The axis a scrollable under the pointer would scroll along now that it moved to `p`, or -1.
int ComposeHost::scrollAxis(Offset p, bool mostlyAlong) const {
    float dx = std::abs(p.x - downPosition_.x), dy = std::abs(p.y - downPosition_.y);
    if (scrollNodes_[0] && dy > viewConfiguration_.touchSlop && (dy >= dx || (!mostlyAlong && !scrollNodes_[1]))) return 0;
    if (scrollNodes_[1] && dx > viewConfiguration_.touchSlop && (dx > dy || (!mostlyAlong && !scrollNodes_[0]))) return 1;
    return -1;
}

// The page started scrolling under the first finger a moment ago and a second one lands over
// something that pinches (transformable): fingers rarely land together, so the pinch takes the
// gesture after all, the stretch the scroll pulled springs back.
bool ComposeHost::handOverToPinch(int64_t time) {
    if (dragAxis_ < 0 || pointers_.size() != 2 || timeOr(time) - dragStartTime_ > 250'000'000 || dragTravel_ > 48) return false;
    std::vector<PointerTarget> targets;
    for (const Hit& hit : hitTest(downPosition_)) {
        if (!elementOf<ZoomElement>(hit.element)) continue;
        detail::syncPointerInputs(*hit.node);
        for (const auto& state : hit.node->pointerInputs)  // the node's pointerInput handlers (the pinch's)
            targets.push_back({hit.node, state});
    }
    if (targets.empty()) return false;
    if (LayoutNode* scrolled = scrollNodes_[dragAxis_]; scrolled && pointerType_ == PointerType::Touch)
        if (auto effect = overscrollOf(*scrolled, dragAxis_))  // the drag ends without a fling: let go of a stretch
            flings_->launch([effect]() -> Task {
                co_await effect->applyToFling({}, [](Velocity) -> TaskOf<Velocity> { co_return Velocity{}; });
            });
    dragAxis_ = -1;
    setUserScrolled(nullptr, 0);
    endInteraction(dragInteraction_, &Interaction::cancel);
    pointerClaimed_ = false;
    pointerTargets_ = targets;
    auto consumed = std::make_shared<bool>(false);
    for (const TrackedPointer& finger : std::vector<TrackedPointer>(pointers_)) {  // both fingers press anew
        deliverPass(targets, PointerEventType::Press, finger.id, consumed, time, PointerEventPass::Initial);
        for (auto& t : targets) deliver(t, PointerEventType::Press, finger.id, consumed, time);
        deliverPass(targets, PointerEventType::Press, finger.id, consumed, time, PointerEventPass::Final);
    }
    commitPointers();
    requestRedraw();
    return true;
}

void ComposeHost::setUserScrolled(LayoutNode* node, int axis) {
    if (node == userScrolled_ && axis == userScrolledAxis_) return;
    if (userScrolled_)
        if (const ScrollableElement* s = scrollableOf(*userScrolled_, userScrolledAxis_)) s->scrollInProgress(false);
    userScrolled_ = node;
    userScrolledAxis_ = axis;
    if (node)
        if (const ScrollableElement* s = scrollableOf(*node, axis)) s->scrollInProgress(true);
}

void ComposeHost::startDrag(int axis, Offset p, int64_t time) {
    dragAxis_ = axis;
    setUserScrolled(scrollNodes_[axis], axis);
    if (LayoutNode* node = scrollNodes_[axis])
        if (const ScrollableElement* s = scrollableOf(*node, axis))
            startInteraction(dragInteraction_, s->interactionSource(), Interaction::dragStart(), node);
    dragStartTime_ = timeOr(time);
    dragTravel_ = 0;
    tapFocus_ = {};
    unpress();
    lastPosition_ = p;
    cancelPointerTargets(time);  // scrolling wins over handlers that didn't claim it
    samples_.clear();
}

// One scroll step of `node`'s scrollable along `axis` with its nested scroll parents: they are
// offered `delta` (finger direction) first, the scrollable uses what it can, then they get the
// rest. Returns what all of them used.
float ComposeHost::nestedScroll(LayoutNode& node, int axis, float delta, NestedScrollSource source) {
    size_t index = 0;
    const ScrollableElement* s = scrollableOf(node, axis, &index);
    if (!s) return 0;
    detail::NestedScrollChain chain(node, index);
    float pre = component(axis, chain.preScroll(alongAxis(axis, delta), source));
    // The content scrolls towards its end as the finger moves to its start (right to left: the other way).
    float sign = flipsWithFinger(node, axis, *s) ? -1.f : 1.f;
    float self = -sign * s->scrollBy(-sign * (delta - pre));
    float left = delta - pre - self;
    if (std::abs(left) < 1e-3f) left = 0;  // rounding, not a real leftover
    float post = component(axis, chain.postScroll(alongAxis(axis, self), alongAxis(axis, left), source));
    return pre + self + post;
}

void ComposeHost::pointerUp(Offset p, int64_t time, int64_t pointerId) {
    dispatcher_->advanceTo(timeOr(time));
    if (std::exchange(swallowGesture_, false)) {
        lastPosition_ = p;
        updateHover(p);
        return;
    }
    TrackedPointer* finger = trackedPointer(pointerId);
    if (pointerDown_ && !finger) return;  // a finger that isn't part of the gesture
    mouseSelection_.reset();
    if (finger && finger == &pointers_.front())
        firePointerEvent(PointerEventType::Release, p, pointerType_ == PointerType::Mouse ? std::optional(PointerButton::Primary) : std::nullopt);
    if (finger) {
        finger->position = p;
        finger->pressed = false;
    }
    dndSource_ = nullptr;
    dndLongPressAt_ = -1;
    if (dnd_) {
        if (finger && finger != &pointers_.front()) return;  // the dragging finger decides
        moveDragAndDrop(p);
        endDragAndDrop(true);
        pointers_.clear();
        pointerTargets_.clear();
        pointerDown_ = false;
        dragAxis_ = -1;
        scrollNodes_[0] = scrollNodes_[1] = nullptr;
        lastPosition_ = p;
        updateHover(p);
        requestRedraw();
        return;
    }
    if (pointers_.size() > 1) {  // one of several fingers lifts: the gesture goes on
        bool leading = finger == &pointers_.front();
        auto consumed = std::make_shared<bool>(false);
        auto targets = pointerTargets_;
        deliverPass(targets, PointerEventType::Release, pointerId, consumed, time, PointerEventPass::Initial);
        for (auto& t : targets) deliver(t, PointerEventType::Release, pointerId, consumed, time);
        deliverPass(targets, PointerEventType::Release, pointerId, consumed, time, PointerEventPass::Final);
        std::erase_if(pointers_, [&](const TrackedPointer& f) { return f.id == pointerId; });
        if (leading) {  // the next finger leads; a scroll goes on from where it is
            lastPosition_ = downPosition_ = pointers_.front().position;
            samples_.clear();
        }
        commitPointers();
        requestRedraw();
        return;
    }
    if (dragAxis_ < 0 && !pointerClaimed_)
        applyTapFocus();
    else
        tapFocus_ = {};
    // Handlers inside the pressed clickable see the release first; if none consumed it, the
    // click happens (and consumes it) before the outer handlers see it.
    auto consumed = std::make_shared<bool>(false);
    auto targets = std::move(pointerTargets_);
    pointerTargets_.clear();
    startDelayedPress(timeOr(time));  // a tap before the timeout: pressed, then let go
    LayoutNode* target = std::exchange(pressed_, nullptr);
    if (target) {
        target->pressed = false;
        target->invalidateDraw();
    }
    if (target) releaseRipples(target, timeOr(time));
    if (pressInteraction_) {  // let go where it was pressed: a release (before the click); slid off it: a cancel
        auto* click = target ? target->modifier.find<ClickableElement>() : nullptr;
        bool on = !click || touchBoundsOf(*target, click).contains(p);
        endInteraction(pressInteraction_, on ? &Interaction::release : &Interaction::cancel);
    }
    longPressAt_ = triggerLongPressAt_ = textLongPressAt_ = -1;
    deliverPass(targets, PointerEventType::Release, pointerId, consumed, time, PointerEventPass::Initial);
    for (size_t i = 0; i <= targets.size(); ++i) {
        if (i == clickOrder_ && target) {
            auto* click = target->modifier.find<ClickableElement>();
            if (!*consumed && !longClicked_ && dragAxis_ < 0 && click && click->enabled &&
                touchBoundsOf(*target, click).contains(p)) {
                *consumed = true;
                fireClick(target, time);
            }
        }
        if (i < targets.size()) deliver(targets[i], PointerEventType::Release, pointerId, consumed, time);
    }
    deliverPass(targets, PointerEventType::Release, pointerId, consumed, time, PointerEventPass::Final);
    pointers_.clear();
    endInteraction(dragInteraction_, &Interaction::stop);
    if (dragAxis_ >= 0 && scrollNodes_[dragAxis_]) {
        // Fling with the velocity of the last 100 ms of the drag (none after holding still).
        int64_t t = timeOr(time);
        samples_.push_back({t, dragAxis_ == 0 ? p.y : p.x});
        std::erase_if(samples_, [&](const Sample& s) { return t - s.time > 100'000'000; });
        float velocity = 0;  // finger direction
        if (samples_.size() >= 2) {
            const Sample& first = samples_.front();
            const Sample& last = samples_.back();
            float dt = float(last.time - first.time) / 1e9f;
            if (dt > 0.005f) velocity = std::clamp((last.position - first.position) / dt, -viewConfiguration_.maximumFlingVelocity, viewConfiguration_.maximumFlingVelocity);
        }
        // A touch fling goes through the overscroll effect: a stretch springs back instead, and
        // the end a fling hits bumps it.
        LayoutNode& dragged = *scrollNodes_[dragAxis_];
        if (auto effect = pointerType_ == PointerType::Touch ? overscrollOf(dragged, dragAxis_) : nullptr)
            flings_->launch([this, effect, node = dragged.handle(), axis = dragAxis_, velocity, time]() -> Task {
                co_await effect->applyToFling(velocityAlong(axis, velocity),
                                              [this, node, axis, time](Velocity v) { return performFling(node, axis, v, time); });
            });
        else
            startFling(dragged, dragAxis_, velocity, time);
    }
    if (!flingNode_) setUserScrolled(nullptr, 0);  // let go and not flinging on
    pointerDown_ = false;
    dragAxis_ = -1;
    scrollNodes_[0] = scrollNodes_[1] = nullptr;
    lastPosition_ = p;
    updateHover(p);
    requestRedraw();
}

// A click on `target`, or (target null) the pending click whose double-click wait is over.
void ComposeHost::fireClick(LayoutNode* target, int64_t time) {
    if (!target) {
        LayoutNode* pending = std::exchange(pendingClick_, nullptr);
        secondPress_ = false;
        auto* click = pending ? pending->modifier.find<ClickableElement>() : nullptr;
        if (click && click->onClick) {
            auto onClick = click->onClick;
            onClick();
        }
        return;
    }
    auto* click = target->modifier.find<ClickableElement>();
    if (!click) return;
    if (click->onDoubleClick) {
        if (pendingClick_ == target && secondPress_) {  // the second click in time
            pendingClick_ = nullptr;
            secondPress_ = false;
            auto onDoubleClick = click->onDoubleClick;
            onDoubleClick();
            return;
        }
        pendingClick_ = target;  // wait: it may become a double click
        pendingClickAt_ = timeOr(time) + 300'000'000;
        return;
    }
    if (click->onClick) {
        auto onClick = click->onClick;  // the callback may change what `click` points to
        onClick();
    }
}

void ComposeHost::runClickTimers(int64_t now) {
    // A finger held on a drag source: the drag starts (unless the page scrolled or a handler took it).
    if (dndSource_ && pointerDown_ && dndLongPressAt_ >= 0 && now >= dndLongPressAt_) {
        if (dragAxis_ < 0 && !pointerClaimed_ && pointers_.size() == 1) {
            startDragAndDrop(lastPosition_, now);
        } else {
            dndSource_ = nullptr;
            dndLongPressAt_ = -1;
        }
    }
    // A long press on a text field selects the word under it (touch selection).
    if (textLongPress_ && pointerDown_ && textLongPressAt_ >= 0 && now >= textLongPressAt_) {
        textLongPressAt_ = -1;
        LayoutNode* field = textLongPress_;
        if (auto* f = field->modifier.find<FocusableElement>(); f && f->onEdit) {
            tapFocus_ = {};  // the release doesn't move the caret
            longClicked_ = true;
            unpress();
            if (focused_ != field) setFocus(field, true);
            selectWordAt(*field, textLongPressPoint_);
            setTouchSelection(true);
            performHapticFeedback(HapticFeedbackType::LongPress);
        }
    }
    // A long press on a tooltip's anchor shows the tooltip instead of clicking (Compose takes
    // it in the initial pass, before the clickable inside).
    if (longPressTrigger_ && pointerDown_ && triggerLongPressAt_ >= 0 && now >= triggerLongPressAt_) {
        triggerLongPressAt_ = -1;
        auto* trigger = longPressTrigger_->modifier.find<TooltipTriggerElement>();
        if (trigger && (trigger->onLongPress || trigger->onLongPressAt)) {
            longClicked_ = true;  // the release clicks nothing
            tapFocus_ = {};
            unpress();
            Rect b = layerBoundsOf(*longPressTrigger_, trigger);
            Offset local{downPosition_.x - b.x, downPosition_.y - b.y};
            auto onLongPress = trigger->onLongPress;
            auto onLongPressAt = trigger->onLongPressAt;
            if (onLongPress) onLongPress();
            if (onLongPressAt) onLongPressAt(local);
        }
    }
    if (pressed_ && pointerDown_ && delayedPressAt_ >= 0 && now >= delayedPressAt_) startDelayedPress(now);
    if (pressed_ && pointerDown_ && longPressAt_ >= 0 && now >= longPressAt_) {
        longPressAt_ = -1;
        auto* click = pressed_->modifier.find<ClickableElement>();
        if (click && click->onLongClick) {
            longClicked_ = true;
            if (pendingClick_ == pressed_) pendingClick_ = nullptr;
            performHapticFeedback(HapticFeedbackType::LongPress);  // combinedClickable's hapticFeedbackEnabled
            auto onLongClick = click->onLongClick;
            onLongClick();
        }
    }
    bool secondPressing = secondPress_ && pointerDown_ && pressed_ == pendingClick_;
    if (pendingClick_ && !secondPressing && now >= pendingClickAt_) fireClick(nullptr, now);
}

// The parents get the velocity first, then the scrollable flings with the rest (or settles by
// itself), and the parents get what is left when that is over.
std::optional<float> ComposeHost::startFling(LayoutNode& node, int axis, float velocity, int64_t time) {
    size_t index = 0;
    const ScrollableElement* s = scrollableOf(node, axis, &index);
    if (!s) return 0.f;
    detail::NestedScrollChain chain(node, index);
    float left = velocity - component(axis, chain.preFling(velocityAlong(axis, velocity)));
    if (s->settle(flipsWithFinger(node, axis, *s) ? left : -left)) {
        chain.postFling(velocityAlong(axis, left), {});
    } else if (std::abs(left) > kMinFlingSpeed) {
        flingNode_ = &node;
        flingAxis_ = axis;
        flingVelocity_ = -left;
        flingStartVelocity_ = left;
        flingTime_ = timeOr(time);
        flingDistance_ = detail::splineFlingDistance(flingVelocity_);
        flingDuration_ = detail::splineFlingDurationMillis(flingVelocity_);
        flingElapsed_ = flingTravelled_ = 0;  // (still the user's scroll: the drag's node)
        return std::nullopt;
    } else {
        chain.postFling({}, velocityAlong(axis, left));  // too slow to fling: nothing hit an end
    }
    return 0.f;
}

std::shared_ptr<OverscrollEffect> ComposeHost::overscrollOf(LayoutNode& node, int axis) const {
    const ScrollableElement* s = scrollableOf(node, axis);
    if (!s) return nullptr;
    return s->overscrollEffect ? s->overscrollEffect : s->overscrolls() ? node.stretch : nullptr;
}

TaskOf<Velocity> ComposeHost::performFling(std::weak_ptr<LayoutNode*> node, int axis, Velocity velocity, int64_t time) {
    auto handle = node.lock();
    if (!handle || !*handle) co_return Velocity{};
    float given = component(axis, velocity);
    std::optional<float> left = startFling(**handle, axis, given, time);
    if (!left) left = co_await FlingEnd{this};
    co_return velocityAlong(axis, given - *left);
}

void ComposeHost::finishFling(float left) {
    auto waiter = std::exchange(flingWaiter_, {});
    if (!waiter.handle) return;
    flingLeft_ = left;
    if (auto job = waiter.job.lock()) dispatcher_->resumeNow(job, waiter.handle);
}

void ComposeHost::stepFling(int64_t now) {
    if (!flingNode_) return;
    float dt = std::clamp(float(now - flingTime_) / 1e9f, 0.f, 0.05f);
    flingTime_ = now;
    // Along Android's spline: where it should be now, and how fast it goes there.
    flingElapsed_ += dt * 1000;
    float t = flingDuration_ > 0 ? std::min(1.f, flingElapsed_ / flingDuration_) : 1.f;
    auto [gone, rate] = detail::splineFlingPosition(t);
    float delta = flingDistance_ * gone - flingTravelled_;  // towards the end
    flingTravelled_ += delta;
    float used = -nestedScroll(*flingNode_, flingAxis_, -delta, NestedScrollSource::SideEffect);
    flingVelocity_ = t < 1 ? rate * flingDistance_ / flingDuration_ * 1000 : 0;
    bool blocked = std::abs(delta - used) > 0.5f;  // nobody can go further
    if (!flingNode_ || !(blocked || t >= 1)) return;
    LayoutNode* node = std::exchange(flingNode_, nullptr);
    setUserScrolled(nullptr, 0);
    float left = blocked ? -flingVelocity_ : 0;  // finger direction
    flingVelocity_ = 0;
    size_t index = 0;
    if (scrollableOf(*node, flingAxis_, &index))  // the parents get what it had left when it hit an end, then its overscroll
        left -= component(flingAxis_, detail::NestedScrollChain(*node, index)
                                          .postFling(velocityAlong(flingAxis_, flingStartVelocity_ - left), velocityAlong(flingAxis_, left)));
    finishFling(left);
}

void ComposeHost::stopFling() {
    if (flingNode_) setUserScrolled(nullptr, 0);
    flingNode_ = nullptr;
    flingVelocity_ = 0;
    finishFling(0);
}

void ComposeHost::pointerExit() {
    if (hovered_) {
        hovered_->invalidateDraw();
        std::exchange(hovered_, nullptr)->hovered = false;
    }
    mouseOver_ = false;
    updateHoverInteractions(lastPosition_);  // ends them (Exit where the mouse was last)
    setHoveredTrigger(nullptr);
    if (cursor_ != Cursor::Default && callbacks_.cursor) callbacks_.cursor(cursor_ = Cursor::Default);
    requestRedraw();
}

bool ComposeHost::zoom(Offset p, float factor, float rotation) {
    if (!(factor > 0) || (factor == 1 && rotation == 0)) return false;
    for (const Hit& hit : hitTest(p))
        if (auto* z = elementOf<ZoomElement>(hit.element); z && z->onZoom) {
            Rect b = layerBoundsOf(*hit.node, z);
            auto onZoom = z->onZoom;  // user code may recompose the node
            onZoom(factor, rotation, {p.x - b.x, p.y - b.y});
            requestRedraw();
            return true;
        }
    return false;
}

void ComposeHost::scroll(Offset p, float wheelDelta, float horizontalWheelDelta) {
    stopFling();
    if (firePointerEvent(PointerEventType::Scroll, p, std::nullopt, {horizontalWheelDelta, -wheelDelta})) return;  // a handler took it
    for (int axis : {0, 1}) {
        float delta = axis == 0 ? -wheelDelta : horizontalWheelDelta;  // towards the end
        if (delta == 0) continue;
        for (const Hit& hit : hitTest(p)) {
            auto* s = elementOf<ScrollableElement>(hit.element);
            if (s && axisOf(s->orientation()) == axis && s->scrollsWithWheel() && s->userScrollEnabled) {
                float dp = -delta * kWheelStep;  // (finger direction)
                // A precise wheel (a touchpad, a free-spinning wheel: parts of notches) scrolls at once.
                if (delta != std::round(delta)) {
                    stopWheel();
                    nestedScroll(*hit.node, axis, dp, NestedScrollSource::Wheel);
                    s->settle(0);  // a pager goes to the nearest page
                    break;
                }
                int64_t now = lastFrameTime_;
                if (wheel_ && wheel_->node == hit.node && wheel_->axis == axis && (wheel_->target - wheel_->done) * dp > 0) {
                    wheel_->target += dp;  // on top of what is left, from where it is
                } else {
                    stopWheel();
                    wheel_ = WheelScroll{hit.node, axis, dp, 0, 0, now, 0};
                }
                // Started again from here: the threshold at once, then the rest in time.
                WheelScroll& w = *wheel_;
                float left = w.target - w.done;
                float first = std::abs(left) < kWheelThreshold ? left : std::copysign(kWheelThreshold, left);
                float used = nestedScroll(*hit.node, axis, first, NestedScrollSource::Wheel);
                w.done += first;
                if (std::abs(used - first) > 0.5f) {  // an end: nothing more to go
                    stopWheel();
                    s->settle(0);
                    break;
                }
                w.from = w.done;
                w.start = now;
                w.durationNanos = int64_t(std::min(kWheelMaxMillis, std::abs(w.target - w.done) / kWheelSpeed) * 1e6f);
                if (w.durationNanos <= 0) {
                    stopWheel();
                    s->settle(0);
                }
                break;
            }
        }
    }
}

void ComposeHost::stepWheel(int64_t now) {
    if (!wheel_) return;
    WheelScroll& w = *wheel_;
    float t = w.durationNanos > 0 ? std::clamp(float(now - w.start) / float(w.durationNanos), 0.f, 1.f) : 1.f;
    float at = w.from + (w.target - w.from) * t;
    float delta = at - w.done;
    if (std::abs(delta) >= 0.5f || (t >= 1 && delta != 0)) {
        LayoutNode* node = w.node;
        int axis = w.axis;
        float used = nestedScroll(*node, axis, delta, NestedScrollSource::Wheel);
        if (!wheel_) return;  // stopped meanwhile
        wheel_->done += delta;
        if (std::abs(used - delta) > 0.5f) t = 1;  // an end
    }
    if (t >= 1) stopWheel();
}

void ComposeHost::stopWheel() {
    if (!wheel_) return;
    LayoutNode* node = std::exchange(wheel_, std::nullopt)->node;
    size_t index = 0;
    if (const ScrollableElement* s = node ? scrollableOf(*node, 0, &index) : nullptr) s->settle(0);  // a pager goes to the nearest page
    if (const ScrollableElement* s = node ? scrollableOf(*node, 1, &index) : nullptr) s->settle(0);
}

// Hover interactions follow a mouse pointer only: the clickable it is over (hovered_) and every
// hoverable() under it.
void ComposeHost::updateHoverInteractions(Offset p) {
    LayoutNode* clickable = mouseOver_ ? hovered_ : nullptr;
    if (!hoverInteraction_ || hoverInteraction_->node != clickable) {
        endInteraction(hoverInteraction_, &Interaction::exit);
        if (auto* click = clickable ? clickable->modifier.find<ClickableElement>() : nullptr)
            startInteraction(hoverInteraction_, sourceOf(*clickable, click), Interaction::hoverEnter(), clickable);
    }
    std::vector<std::pair<const LayoutNode*, const HoverableElement*>> over;
    if (mouseOver_)
        for (const Hit& hit : hitTest(p))
            if (auto* h = elementOf<HoverableElement>(hit.element)) over.push_back({hit.node, h});
    // onPointerEvent's Enter and Exit: the nodes listening for them the mouse came over or left.
    std::vector<LayoutNode*> entered;
    if (mouseOver_)
        for (const Hit& hit : hitTest(p))
            if (auto* h = elementOf<PointerEventElement>(hit.element);
                h && (h->type == PointerEventType::Enter || h->type == PointerEventType::Exit) && std::ranges::find(entered, hit.node) == entered.end())
                entered.push_back(hit.node);
    std::vector<LayoutNode*> left = std::exchange(pointerEntered_, entered);
    for (LayoutNode* n : left)
        if (std::ranges::find(entered, n) == entered.end()) firePointerEvent(PointerEventType::Exit, p, std::nullopt, {}, n);
    for (LayoutNode* n : entered)
        if (std::ranges::find(left, n) == left.end()) firePointerEvent(PointerEventType::Enter, p, std::nullopt, {}, n);
    std::erase_if(hoverables_, [&](const ActiveInteraction& a) {
        if (std::ranges::any_of(over, [&](const auto& o) { return o.first == a.node && o.second->source == a.source; })) return false;
        a.source.tryEmit(a.start.exit());
        return true;
    });
    for (const auto& [node, h] : over)
        if (std::ranges::none_of(hoverables_, [&](const ActiveInteraction& a) { return a.node == node && a.source == h->source; })) {
            hoverables_.push_back({h->source, Interaction::hoverEnter(), node});
            h->source.tryEmit(hoverables_.back().start);
        }
}

void ComposeHost::updateHover(Offset p) {
    LayoutNode* hover = nullptr;
    // The pointer's look: the innermost say (an icon, a clickable's hand, a field's I-beam),
    // unless an icon around it overrides its descendants.
    std::optional<Cursor> cursorFound, overriding;
    for (const Hit& hit : hitTest(p)) {
        if (auto* click = elementOf<ClickableElement>(hit.element); click && click->enabled && !hover) {
            hover = hit.node;
            if (!cursorFound) cursorFound = Cursor::Pointer;
        }
        if (auto* f = elementOf<FocusableElement>(hit.element); f && f->onEdit && !cursorFound) cursorFound = Cursor::Text;  // a text field
        if (auto* icon = elementOf<PointerHoverIconElement>(hit.element)) {
            if (!cursorFound) cursorFound = icon->icon;
            if (icon->overrideDescendants) overriding = icon->icon;
        }
    }
    Cursor cursor = overriding ? *overriding : cursorFound.value_or(Cursor::Default);
    if (!mouseOver_) hover = nullptr;  // a finger lifted over it: no hover
    if (hover != hovered_) {
        if (hovered_) hovered_->hovered = false, hovered_->invalidateDraw();
        hovered_ = hover;
        if (hover) hover->hovered = true, hover->invalidateDraw();
        requestRedraw();
    }
    updateHoverInteractions(p);
    LayoutNode* trigger = nullptr;
    if (mouseOver_)  // (a finger hovers no tooltip's anchor either)
        for (const Hit& hit : hitTest(p))
            if (elementOf<TooltipTriggerElement>(hit.element)) {
                trigger = hit.node;
                break;
            }
    setHoveredTrigger(trigger);
    if (cursor != cursor_) {
        cursor_ = cursor;
        if (callbacks_.cursor) callbacks_.cursor(cursor);
    }
}

void ComposeHost::setHoveredTrigger(LayoutNode* trigger) {
    if (trigger == hoveredTrigger_) return;
    auto tell = [](LayoutNode* node, bool hovered) {
        auto* t = node ? node->modifier.find<TooltipTriggerElement>() : nullptr;
        if (t && t->onHoverChange) {
            auto onHoverChange = t->onHoverChange;
            onHoverChange(hovered);
        }
    };
    LayoutNode* previous = std::exchange(hoveredTrigger_, trigger);
    tell(previous, false);
    tell(trigger, true);
}

// Desktop keyboards deliver typed text; turn it into a full edit at the caret so every
// platform goes through the same textEdit() path.
void ComposeHost::textInput(std::string_view text) {
    if (!isTextField(focused_)) return;
    TextEditState s = imeState_;
    int from = std::min(s.selectionStart, s.selectionEnd), to = std::max(s.selectionStart, s.selectionEnd);
    s.text.replace(size_t(from), size_t(to - from), text);
    s.selectionStart = s.selectionEnd = from + int(text.size());
    s.compositionStart = s.compositionEnd = -1;
    textEdit(s);
}

void ComposeHost::textEdit(const TextEditState& proposed) {
    if (!focused_) return;
    auto* f = focused_->modifier.find<FocusableElement>();
    if (!f) return;
    // An input transformation may change what was typed (or refuse it): then that is the edit,
    // and the input method hears so once the field has it.
    TextEditState state = f->transformEdit && proposed.text != imeState_.text ? f->transformEdit(imeState_, proposed) : proposed;
    bool corrected = !(state == proposed);
    if (touchSelection_ && state.text != imeState_.text) setTouchSelection(false);  // typing ends it
    if (touchSelection_) requestLayout();  // the handles and the toolbar follow the selection
    if (state.text != imeState_.text) {
        if (unechoed_.empty()) unechoed_.push_back(f->value);
        unechoed_.push_back(state.text);
        if (unechoed_.size() > 32) unechoed_.erase(unechoed_.begin() + 1);
        lastEditAt_ = lastFrameTime_;
    }
    imeState_ = state;
    int length = int(imeState_.text.size());
    for (int* offset : {&imeState_.selectionStart, &imeState_.selectionEnd})
        *offset = std::clamp(*offset, 0, length);
    if (imeState_.compositionStart < 0 || imeState_.compositionEnd > length ||
        imeState_.compositionStart >= imeState_.compositionEnd)
        imeState_.compositionStart = imeState_.compositionEnd = -1;
    if (f->onEdit) {
        auto onEdit = f->onEdit;  // copy: the callback may trigger a recomposition later
        onEdit(imeState_);
    }
    if (corrected && callbacks_.textInputChanged) callbacks_.textInputChanged(imeState_);
    requestRedraw();
}

bool ComposeHost::keyDown(Key key, KeyModifiers modifiers) { return dispatchKey({key, KeyEventType::KeyDown, modifiers}); }

std::shared_ptr<void> ComposeHost::addKeyHandler(std::function<bool(const KeyEvent&)> handler) {
    auto h = std::make_shared<std::function<bool(const KeyEvent&)>>(std::move(handler));
    keyHandlers_.push_back(h);
    return h;
}
bool ComposeHost::keyUp(Key key, KeyModifiers modifiers) { return dispatchKey({key, KeyEventType::KeyUp, modifiers}); }

bool ComposeHost::dispatchKey(const KeyEvent& event) {
    if (dnd_ && event.key == Key::Escape) {  // the drag is called off
        if (event.type == KeyEventType::KeyDown) endDragAndDrop(false);
        return true;
    }
    if (event.type == KeyEventType::KeyDown) setTouchSelection(false);
    // The focused node and the nodes around it, innermost first (a popup's own window only).
    std::vector<LayoutNode*> path;
    for (LayoutNode* n = focused_; n; n = n->overlay ? nullptr : n->parent) path.push_back(n);
    std::vector<std::function<bool(const KeyEvent&)>> preview, bubble;  // copies: handlers run user code
    for (auto it = path.rbegin(); it != path.rend(); ++it)
        for (const auto& e : (*it)->modifier.elements())
            if (auto* k = dynamic_cast<const KeyEventElement*>(e.get()); k && k->preview && k->handler) preview.push_back(k->handler);
    for (LayoutNode* n : path) {
        const auto& elements = n->modifier.elements();
        for (size_t i = elements.size(); i-- > 0;)
            if (auto* k = dynamic_cast<const KeyEventElement*>(elements[i].get()); k && !k->preview && k->handler)
                bubble.push_back(k->handler);
    }
    bool down = event.type == KeyEventType::KeyDown;
    for (auto& handler : preview)
        if (handler(event)) return true;
    if (down && editKey(event.key, event.modifiers)) return true;
    for (auto& handler : bubble)
        if (handler(event)) return true;
    // The window's (menu shortcuts), the latest first.
    std::erase_if(keyHandlers_, [](const auto& w) { return w.expired(); });
    std::vector<std::shared_ptr<std::function<bool(const KeyEvent&)>>> window;
    for (auto it = keyHandlers_.rbegin(); it != keyHandlers_.rend(); ++it)
        if (auto h = it->lock()) window.push_back(std::move(h));
    for (auto& handler : window)
        if ((*handler)(event)) return true;
    return down && defaultKeyAction(event.key, event.modifiers);
}

bool ComposeHost::editKey(Key key, KeyModifiers modifiers) {
    if (!isTextField(focused_)) return false;
    auto* f = focused_->modifier.find<FocusableElement>();
    TextEditState s = imeState_;
    std::string_view text = s.text;
    int length = int(text.size());
    int from = std::min(s.selectionStart, s.selectionEnd), to = std::max(s.selectionStart, s.selectionEnd);
    // The caret moves, and Backspace / Delete take, whole clusters (an emoji family, a flag).
    auto previous = [&](int i) { return i > 0 ? int(utf8::previousCluster(text, size_t(i))) : 0; };
    auto next = [&](int i) { return i >= length ? length : int(utf8::nextCluster(text, size_t(i))); };
    auto caret = [&](int to, bool extend) {  // moves the caret, or extends the selection with shift
        s.selectionEnd = to;
        if (!extend) s.selectionStart = to;
        s.compositionStart = s.compositionEnd = -1;
        applyEdit(s);
    };
    auto erase = [&](int a, int b) {
        if (a == b) return;
        s.text.erase(size_t(a), size_t(b - a));
        s.selectionStart = s.selectionEnd = a;
        s.compositionStart = s.compositionEnd = -1;
        applyEdit(s);
    };
    // Words as a double click selects them (letters, digits, '_' and anything beyond ASCII), and
    // paragraphs (between new lines): Compose's KeyMapping moves and deletes by them with Ctrl.
    auto isWord = [&](int i) { return detail::isWordByte(text[size_t(i)]); };
    auto nextWord = [&](int i) {  // the end of the word at or after `i`
        while (i < length && !isWord(i)) ++i;
        while (i < length && isWord(i)) ++i;
        return i;
    };
    auto previousWord = [&](int i) {  // the start of the word at or before `i`
        while (i > 0 && !isWord(i - 1)) --i;
        while (i > 0 && isWord(i - 1)) --i;
        return i;
    };
    auto paragraphStart = [&](int i) {
        while (i > 0 && text[size_t(i - 1)] != '\n') --i;
        return i;
    };
    auto paragraphEnd = [&](int i) {
        while (i < length && text[size_t(i)] != '\n') ++i;
        return i;
    };
    auto eraseOr = [&](auto other) { from != to ? erase(from, to) : erase(std::min(to, other()), std::max(to, other())); };
    bool ctrl = modifiers.ctrl && !modifiers.alt;
    switch (key) {
    case Key::Backspace:
        if (modifiers.alt && !modifiers.ctrl) break;
        if (ctrl) eraseOr([&] { return previousWord(to); });  // with Shift too
        else erase(from == to ? previous(to) : from, to);
        return true;
    case Key::Delete:
        if (modifiers.alt && !modifiers.ctrl && !modifiers.shift)  // to the line's end
            eraseOr([&] { return f->moveCaret ? f->moveCaret(to, CaretMove::LineEnd) : length; });
        else if (ctrl && !modifiers.shift) eraseOr([&] { return nextWord(to); });
        else if (!modifiers.ctrl && !modifiers.alt) erase(from, from == to ? next(to) : to);
        else break;
        return true;
    case Key::H:  // Ctrl+H: Backspace
        if (!ctrl || modifiers.shift) break;
        erase(from == to ? previous(to) : from, to);
        return true;
    case Key::Left:
        if (ctrl) caret(previousWord(s.selectionEnd), modifiers.shift);
        else caret(!modifiers.shift && from != to ? from : previous(s.selectionEnd), modifiers.shift);
        return true;
    case Key::Right:
        if (ctrl) caret(nextWord(s.selectionEnd), modifiers.shift);
        else caret(!modifiers.shift && from != to ? to : next(s.selectionEnd), modifiers.shift);
        return true;
    case Key::Home:  // the line's start in a multi-line field (Ctrl: the text's)
        caret(f->moveCaret && !modifiers.ctrl ? f->moveCaret(s.selectionEnd, CaretMove::LineStart) : 0, modifiers.shift);
        return true;
    case Key::End:
        caret(f->moveCaret && !modifiers.ctrl ? f->moveCaret(s.selectionEnd, CaretMove::LineEnd) : length, modifiers.shift);
        return true;
    case Key::Up:
    case Key::Down:
        if (ctrl) {  // the paragraph's start or end (the one before's or after's, already there)
            if (key == Key::Up) caret(paragraphStart(from) == from && from > 0 ? paragraphStart(from - 1) : paragraphStart(from), modifiers.shift);
            else caret(paragraphEnd(to) == to && to < length ? paragraphEnd(to + 1) : paragraphEnd(to), modifiers.shift);
            return true;
        }
        // Between lines; a single-line field leaves them to focus movement.
        if (!f->moveCaret) break;
        caret(f->moveCaret(s.selectionEnd, key == Key::Up ? CaretMove::LineUp : CaretMove::LineDown), modifiers.shift);
        return true;
    case Key::A:
        if (!modifiers.ctrl) break;
        s.selectionStart = 0;
        s.selectionEnd = length;
        applyEdit(s);
        return true;
    case Key::C:
    case Key::X:
        if (!modifiers.ctrl) break;
        if (!f->allowCopy) return true;  // password fields
        if (from != to && callbacks_.setClipboard) callbacks_.setClipboard(std::string(text.substr(size_t(from), size_t(to - from))));
        if (key == Key::X) erase(from, to);
        return true;
    case Key::V:
        if (!modifiers.ctrl) break;
        if (callbacks_.getClipboard) textInput(callbacks_.getClipboard());
        return true;
    case Key::Enter:
        if (!f->singleLine) {  // a new line; the soft keyboard's action key still runs the action
            textInput("\n");
            return true;
        }
        if (!f->onImeAction) return false;
        imeAction(f->keyboardOptions.imeAction);
        return true;
    default: break;
    }
    // What a key types arrives as text input; the key itself stays with the field.
    bool typing = (key >= Key::A && key <= Key::Z) || (key >= Key::Zero && key <= Key::Nine) || key == Key::Space;
    return typing && !modifiers.ctrl && !modifiers.alt;
}

std::shared_ptr<ComposeHost::BackCallback> ComposeHost::backTarget() const {
    LayoutNode* popup = inputOverlay();
    std::shared_ptr<const void> scope = popup ? popup->backScope : nullptr;
    for (auto it = backHandlers_.rbegin(); it != backHandlers_.rend(); ++it)
        if ((*it)->enabled && (*it)->onBack && ((*it)->owned ? scope && (*it)->owner.lock() == scope : !popup)) return *it;
    return nullptr;
}

void ComposeHost::backStarted(const BackEventCompat& event) {
    backCancelled();  // one that never ended
    if (!inputOverlay() && focused_) return;  // cleared once the gesture is done
    backGesture_ = backTarget();  // none in a popup on top: it is dismissed once the gesture is done
    if (backGesture_ && backGesture_->onStarted) backGesture_->onStarted(event);
}

void ComposeHost::backProgressed(const BackEventCompat& event) {
    if (backGesture_ && backGesture_->onProgressed) backGesture_->onProgressed(event);
}

void ComposeHost::backCancelled() {
    if (auto handler = std::exchange(backGesture_, nullptr); handler && handler->onCancelled) handler->onCancelled();
}

bool ComposeHost::backInvoked() {
    if (auto handler = std::exchange(backGesture_, nullptr)) {
        if (handler == backTarget()) {
            auto onBack = handler->onBack;
            onBack();
            return true;
        }
        if (handler->onCancelled) handler->onCancelled();  // disabled meanwhile, or one came over it
    }
    return keyDown(Key::Escape);
}

bool ComposeHost::wantsBack() const {
    return inputOverlay() || focused_ || backTarget();
}

bool ComposeHost::defaultKeyAction(Key key, KeyModifiers modifiers) {
    switch (key) {
    case Key::Escape:
        if (LayoutNode* popup = inputOverlay()) {  // a popup on top gets back, even one that can't be dismissed
            if (auto handler = backTarget()) {  // first a back handler in it
                auto onBack = handler->onBack;
                onBack();
            } else if (auto dismiss = popup->overlay->onDismissRequest) {
                dismiss();
            }
            return true;
        }
        if (focused_) {
            setFocus(nullptr);
            return true;
        }
        if (auto handler = backTarget()) {
            auto onBack = handler->onBack;
            onBack();
            return true;
        }
        return false;
    case Key::Tab: return moveFocus(modifiers.shift ? FocusDirection::Previous : FocusDirection::Next);
    case Key::Up: return focused_ && moveFocus(FocusDirection::Up);
    case Key::Down: return focused_ && moveFocus(FocusDirection::Down);
    case Key::Left: return focused_ && moveFocus(FocusDirection::Left);
    case Key::Right: return focused_ && moveFocus(FocusDirection::Right);
    case Key::Enter:
    case Key::Space:
        if (auto* click = focused_ ? focused_->modifier.find<ClickableElement>() : nullptr; click && click->enabled) {
            Rect b = layerBoundsOf(*focused_, click);  // a ripple from the middle
            startRipple(*focused_, *click, {b.width / 2, b.height / 2}, lastFrameTime_);
            releaseRipples(focused_, lastFrameTime_);
            if (auto source = sourceOf(*focused_, click)) {  // a press and its release, then the click
                Interaction press = Interaction::press();
                source->tryEmit(press);
                source->tryEmit(press.release());
            }
            fireClick(focused_, -1);
            requestRedraw();
            return true;
        }
        return false;
    default: return false;
    }
}

void ComposeHost::imeAction(ImeAction action) {
    if (!focused_) return;
    auto* f = focused_->modifier.find<FocusableElement>();
    if (!f) return;
    if (action == ImeAction::Default) action = ImeAction::Done;  // single-line fields
    if (f->onImeAction) {
        auto onImeAction = f->onImeAction;
        onImeAction(action);
    }
}

void ComposeHost::setTouchSelection(bool on, bool toolbar) {
    if (touchSelection_ == on) return;
    touchSelection_ = on;
    auto* f = focused_ ? focused_->modifier.find<FocusableElement>() : nullptr;
    if (f && f->onTouchSelection) {
        auto onTouchSelection = f->onTouchSelection;
        onTouchSelection(on, toolbar);
    }
    requestRedraw();
}

void ComposeHost::setTextSelection(int start, int end) {
    if (!isTextField(focused_)) return;
    int length = int(imeState_.text.size());
    TextEditState s = imeState_;
    s.selectionStart = std::clamp(start, 0, length);
    s.selectionEnd = std::clamp(end, 0, length);
    s.compositionStart = s.compositionEnd = -1;
    if (!(s == imeState_)) applyEdit(s);
}

bool ComposeHost::firePointerEvent(PointerEventType type, Offset p, std::optional<PointerButton> button, Offset scrollDelta,
                                   LayoutNode* onlyNode) {
    struct Call {
        std::function<void(PointerEvent&)> onEvent;  // a copy: user code may recompose the node
        Rect bounds;
    };
    std::vector<Call> calls;
    auto take = [&](const LayoutNode& node, const ModifierElement* e) {
        if (auto* h = elementOf<PointerEventElement>(e); h && h->type == type && h->onEvent) calls.push_back({h->onEvent, layerBoundsOf(node, h)});
    };
    if (onlyNode) {
        for (const auto& e : onlyNode->modifier.elements()) take(*onlyNode, e.get());
    } else {
        for (const Hit& hit : hitTest(p)) take(*hit.node, hit.element);
    }
    auto consumed = std::make_shared<bool>(false);
    for (Call& call : calls) {
        PointerEvent e;
        e.type = type;
        e.keyboardModifiers = keyboardModifiers_;
        e.button = button;
        e.mouse = type == PointerEventType::Scroll || (button && *button != PointerButton::Primary) ||  // only mice have those
                  pointerType_ == PointerType::Mouse || mouseOver_;
        e.change.position = e.change.previousPosition = {p.x - call.bounds.x, p.y - call.bounds.y};
        e.change.pressed = e.change.previousPressed = pointerDown_;
        e.change.uptimeMillis = lastFrameTime_ / 1'000'000;
        e.change.scrollDelta = scrollDelta;
        e.change.consumed_ = consumed;
        e.changes = {e.change};
        call.onEvent(e);
    }
    return *consumed;
}

void ComposeHost::mouseButton(Offset p, PointerButton button, bool down) {
    if (button == PointerButton::Primary) return down ? pointerDown(p, -1, 0, PointerType::Mouse) : pointerUp(p);
    if (down) ++mouseButtonPresses_;
    firePointerEvent(down ? PointerEventType::Press : PointerEventType::Release, p, button);
}

bool ComposeHost::secondaryClick(Offset p) {
    for (const Hit& hit : hitTest(p)) {
        if (auto* menu = elementOf<ContextMenuElement>(hit.element); menu && menu->onOpen) {
            menu->onOpen(p);
            return true;
        }
        auto* f = elementOf<FocusableElement>(hit.element);
        if (!f || !f->onEdit || !f->onContextMenu) continue;
        LayoutNode* field = hit.node;
        if (focused_ != field) setFocus(field);
        if (focused_ != field) return false;
        if (f->offsetAt) {  // the caret to the click, unless that is in the selection
            Rect b = layerBoundsOf(*field, f);
            int offset = std::clamp(f->offsetAt({p.x - b.x, p.y - b.y}), 0, int(imeState_.text.size()));
            int a = std::min(imeState_.selectionStart, imeState_.selectionEnd), z = std::max(imeState_.selectionStart, imeState_.selectionEnd);
            if (offset < a || offset > z || a == z) {
                TextEditState s = imeState_;
                s.selectionStart = s.selectionEnd = offset;
                s.compositionStart = s.compositionEnd = -1;
                if (!(s == imeState_)) applyEdit(s);
            }
        }
        f->onContextMenu(p);
        requestRedraw();
        return true;
    }
    return false;
}

void ComposeHost::textAction(TextAction action) {
    if (!isTextField(focused_)) return;
    switch (action) {
    case TextAction::Cut: editKey(Key::X, {.ctrl = true}); break;  // the text changes: touch selection ends
    case TextAction::Copy: {
        editKey(Key::C, {.ctrl = true});
        if (!touchSelection_) break;  // a context menu's: the selection stays, as on a desktop
        int end = std::max(imeState_.selectionStart, imeState_.selectionEnd);
        setTextSelection(end, end);  // copied from the touch toolbar: back to a caret, as on Android
        setTouchSelection(false);
        break;
    }
    case TextAction::Paste: editKey(Key::V, {.ctrl = true}); break;
    case TextAction::SelectAll: setTextSelection(0, int(imeState_.text.size())); break;
    }
}

void ComposeHost::applyEdit(const TextEditState& state) {
    textEdit(state);
    if (callbacks_.textInputChanged) callbacks_.textInputChanged(imeState_);
}

namespace {
bool inside(const LayoutNode* node, const LayoutNode* ancestor) {
    for (; node; node = node->overlay ? nullptr : node->parent)
        if (node == ancestor) return true;
    return false;
}
} // namespace

LayoutNode* ComposeHost::restoreFocus(LayoutNode* next) {
    // The outermost focusRestorer around `next` that the focus is coming into from outside.
    LayoutNode* group = nullptr;
    for (LayoutNode* n = next; n; n = n->overlay ? nullptr : n->parent)
        if (n->modifier.find<FocusRestorerElement>() && !inside(focused_, n)) group = n;
    if (!group) return next;
    auto it = restoredFocus_.find(group);
    if (it != restoredFocus_.end()) {
        auto handle = it->second.lock();
        LayoutNode* saved = handle ? *handle : nullptr;
        if (saved && saved->isPlaced() && inside(saved, group) && canFocus(*saved)) return saved;
    }
    if (const auto& fallback = group->modifier.find<FocusRestorerElement>()->fallback)
        if (LayoutNode* target = findRequester(*group, fallback->data_.get())) {
            std::vector<LayoutNode*> focusables;
            collectFocusables(*target, focusables);
            if (!focusables.empty()) return focusables.front();
        }
    return next;
}

void ComposeHost::setFocus(LayoutNode* node, bool touched) {
    if (node == focused_ || (node && !canFocus(*node))) return;
    // Leaving focusRestorers: they keep what had it.
    for (LayoutNode* n = focused_; n; n = n->overlay ? nullptr : n->parent)
        if (n->modifier.find<FocusRestorerElement>() && !inside(node, n)) restoredFocus_[n] = focused_->handle();
    endInteraction(focusInteraction_, &Interaction::unfocus);
    setTouchSelection(false);
    focusDirty_ = true;
    if (node) bringFocusedIntoView_ = true;  // whatever focused it (a FocusRequester too), as Compose's focusables
    requestLayout();
    bool hadInput = isTextField(focused_);
    if (focused_) focused_->focused = false, focused_->invalidateDraw();
    focused_ = node;
    if (node) node->invalidateDraw();
    if (isTextField(node)) {
        node->focused = true;
        auto* f = node->modifier.find<FocusableElement>();
        imeState_ = fieldState(*f);
        auto onEdit = f->onEdit;  // so it doesn't show the caret or selection of its last focus
        onEdit(imeState_);
        showTextInput(touched);
    } else {
        if (node) node->focused = true;  // a button or focusable(): no keyboard
        imeState_ = {};
        if (hadInput && callbacks_.textInput) callbacks_.textInput(false, {}, imeState_, {});
    }
    if (node) {
        auto* f = node->modifier.find<FocusableElement>();
        auto* click = node->modifier.find<ClickableElement>();
        startInteraction(focusInteraction_, f && f->interactionSource ? f->interactionSource : click ? sourceOf(*node, click) : std::nullopt,
                         Interaction::focus(), node);
    }
    requestRedraw();
}

void ComposeHost::showTextInput(bool touched) {
    if (!isTextField(focused_) || !callbacks_.textInput) return;
    auto* f = focused_->modifier.find<FocusableElement>();
    if (f->readOnly) return callbacks_.textInput(false, {}, imeState_, {});  // nothing to type into
    ImeOptions options;
    static_cast<KeyboardOptions&>(options) = f->keyboardOptions;
    options.singleLine = f->singleLine;
    options.showKeyboard = touched || f->keyboardOptions.showKeyboardOnFocus;
    callbacks_.textInput(true, focused_->bounds(), imeState_, options);
}

void ComposeHost::hideSoftwareKeyboard() {
    if (isTextField(focused_) && callbacks_.hideSoftwareKeyboard) callbacks_.hideSoftwareKeyboard();
}

// When the app itself changes the focused field (clearing it after "submit", filtering input,
// ...), the platform input method still holds the old text and must be told.
void ComposeHost::syncTextInput() {
    auto* f = focused_ ? focused_->modifier.find<FocusableElement>() : nullptr;
    if (!f) return unechoed_.clear();
    bool sameSelection = !f->selection || TextRange{imeState_.selectionStart, imeState_.selectionEnd} == *f->selection;
    if (f->value == imeState_.text && sameSelection) return unechoed_.clear();  // the app caught up
    // Still one of the texts it had before what was typed: not caught up yet, not a change of
    // its own — unless it stays so (it refused the edit), then the input method goes back to it.
    // (A field with its selection in a TextFieldState is never behind: the state is set at once.)
    bool behind = !f->selection && std::find(unechoed_.begin(), unechoed_.end(), f->value) != unechoed_.end();
    if (behind && lastFrameTime_ - lastEditAt_ < 150'000'000) return;
    unechoed_.clear();
    imeState_ = fieldState(*f);
    if (f->selection && f->onEdit) {  // the field shows the selection the app set
        auto onEdit = f->onEdit;
        onEdit(imeState_);
    }
    if (callbacks_.textInputChanged) callbacks_.textInputChanged(imeState_);
}

TextEditState ComposeHost::fieldState(const FocusableElement& f) {
    if (!f.selection) return TextEditState::caretAtEnd(f.value);
    int length = int(f.value.size());
    return {f.value, std::clamp(f.selection->start, 0, length), std::clamp(f.selection->end, 0, length)};
}

// ---- Semantics -----------------------------------------------------------------------------

namespace {

const ScrollableElement* anyScrollable(const LayoutNode& node) {
    for (const auto& e : node.modifier.elements())
        if (auto* s = dynamic_cast<const ScrollableElement*>(e.get()); s && s->userScrollEnabled) return s;
    return nullptr;
}

bool semanticallyInteractive(const LayoutNode& n) {
    return n.modifier.find<ClickableElement>() || n.modifier.find<FocusableElement>();
}

// The words a node shows: its content description, else its text (not the "checked"-like
// state strings of toggles, which carry a role instead).
std::string ownLabel(const LayoutNode& n) {
    if (auto s = detail::semanticsOf(n.modifier)) {
        if (!s->contentDescription.empty()) return s->contentDescription;
        if (s->checked || s->clearDescendants) return {};
    }
    if (n.modifier.find<FocusableElement>()) return {};
    return n.semanticsText;
}

// What a clickable node says: everything inside it that isn't interactive on its own.
void mergedLabels(const LayoutNode& n, std::string& out) {
    for (auto* child : n.children) {
        if (!child->isPlaced() || semanticallyInteractive(*child)) continue;
        auto sem = detail::semanticsOf(child->modifier);
        if (sem && sem->hidden) continue;
        std::string label = ownLabel(*child);
        // An icon described like the text next to it is said once.
        bool repeated = !label.empty() && out.size() >= label.size() && out.compare(out.size() - label.size(), label.size(), label) == 0;
        if (!label.empty() && !repeated) out += (out.empty() ? "" : " ") + label;
        if (!sem || !sem->clearDescendants) mergedLabels(*child, out);
    }
}

} // namespace

std::optional<MutableInteractionSource> ComposeHost::sourceOf(LayoutNode& node, const ClickableElement* click) {
    if (!click) return std::nullopt;
    if (click->interactionSource || click->indication.kind != Indication::Kind::Custom) return click->interactionSource;
    if (!node.indicationSource) node.indicationSource.emplace();
    return node.indicationSource;
}

void ComposeHost::syncIndicationNode(LayoutNode& node, const Indication* indication, const std::optional<MutableInteractionSource>& source) {
    const IndicationNodeFactory* factory = indication && indication->kind == Indication::Kind::Custom ? indication->factory.get() : nullptr;
    if (!factory || !source) {
        node.indicationNode.reset();
        node.indicationFactory = nullptr;
        node.indicationHeard.reset();
        return;
    }
    if (factory == node.indicationFactory && node.indicationHeard == source && node.indicationNode) return;
    node.indicationNode = factory->create();
    node.indicationFactory = factory;
    node.indicationHeard = source;
    if (!node.indicationNode) return;
    node.indicationNode->scope_.emplace(*dispatcher_);
    node.indicationNode->scope_->launch([from = *source, weak = std::weak_ptr<IndicationNode>(node.indicationNode)]() -> Task {
        co_await from.interactions().collect([weak](const Interaction& interaction) {
            if (auto n = weak.lock()) n->onInteraction(interaction);
        });
    });
}

int ComposeHost::semanticsIdOf(LayoutNode* node) {
    auto [it, inserted] = semanticsIds_.try_emplace(node, nextSemanticsId_);
    if (inserted) ++nextSemanticsId_;
    return it->second;
}

std::vector<SemanticsNode> ComposeHost::semanticsTree() {
    std::vector<SemanticsNode> out;
    std::unordered_map<int, float> order;  // the nodes' traversal indexes (set ones)
    // `contentType`: the one around it (a text field's semantics go on its decoration).
    auto visit = [&](auto&& self, LayoutNode& n, int parent, bool interactiveOnly, const ContentType* contentType) -> void {
        auto* click = n.modifier.find<ClickableElement>();
        auto* focus = n.modifier.find<FocusableElement>();
        auto sem = detail::semanticsOf(n.modifier);
        if (sem && sem->hidden) return;  // nor anything inside
        if (sem && sem->contentType) contentType = &*sem->contentType;
        auto* scroll = anyScrollable(n);
        std::string label = ownLabel(n);
        bool interactive = click || focus;
        bool include = interactiveOnly ? interactive : (interactive || sem || scroll || !label.empty());
        int me = parent;
        if (include) {
            SemanticsNode s;
            s.id = semanticsIdOf(&n);
            s.parent = parent;
            s.bounds = n.bounds();
            s.label = label;
            s.clickable = click != nullptr;
            s.enabled = !click || click->enabled;
            s.scrollable = scroll != nullptr;
            s.focused = &n == focused_;  // a text field, or what Tab went to
            if (focus) {
                s.editable = true;
                s.value = focus->value;
                if (focus->onEdit && contentType) s.contentType = contentType->hints;
                KeyboardType type = focus->keyboardOptions.keyboardType;
                s.password = focus->onEdit && (type == KeyboardType::Password || type == KeyboardType::NumberPassword || !focus->allowCopy);
            }
            if (sem) {
                s.role = sem->role;
                s.checked = sem->checked;
                s.stateDescription = sem->stateDescription;
                s.heading = sem->heading;
                s.selectableGroup = sem->selectableGroup;
                s.range = sem->progressBarRangeInfo;
                s.settable = bool(sem->setProgress);
                s.liveRegion = sem->liveRegion;
                for (const auto& action : sem->customActions) s.customActions.push_back(action.label);
                order[s.id] = sem->traversalIndex;
            }
            bool clear = sem && sem->clearDescendants;
            if ((click || (sem && sem->mergeDescendants)) && !clear) mergedLabels(n, s.label);  // a button says its text
            out.push_back(std::move(s));
            me = out.back().id;
            if (clear) return;  // what is inside says nothing
        }
        // Inside a clickable (or merging) node only other interactive nodes stand on their own.
        bool merged = include && (click || (sem && sem->mergeDescendants));
        for (auto* child : n.children)
            if (child->isPlaced()) self(self, *child, me, interactiveOnly || merged, contentType);
    };
    LayoutNode* popup = inputOverlay();
    visit(visit, popup ? *popup : root(), -1, false, nullptr);
    if (std::none_of(order.begin(), order.end(), [](const auto& o) { return o.second != 0; })) return out;
    // Each node's children in traversal order (the lower index first), parents still first.
    std::unordered_map<int, std::vector<size_t>> children;
    for (size_t i = 0; i < out.size(); ++i) children[out[i].parent].push_back(i);
    std::vector<SemanticsNode> sorted;
    auto emit = [&](auto&& self, int parent) -> void {
        auto& kids = children[parent];
        std::stable_sort(kids.begin(), kids.end(), [&](size_t a, size_t b) {
            auto index = [&](size_t i) {
                auto it = order.find(out[i].id);
                return it == order.end() ? 0.f : it->second;
            };
            return index(a) < index(b);
        });
        for (size_t i : kids) {
            sorted.push_back(out[i]);
            self(self, out[i].id);
        }
    };
    emit(emit, -1);
    return sorted;
}

bool ComposeHost::performSemanticsAction(int id, SemanticsAction action, float argument) {
    LayoutNode* node = nullptr;
    for (auto& [n, i] : semanticsIds_)
        if (i == id) node = n;
    if (!node) return false;
    auto* click = node->modifier.find<ClickableElement>();
    switch (action) {
    case SemanticsAction::Click:
        if (node->modifier.find<FocusableElement>()) {
            if (focused_ == node) showTextInput();
            else setFocus(node, true);
            return true;
        }
        if (!click || !click->enabled || !click->onClick) return false;
        {
            auto onClick = click->onClick;
            onClick();
        }
        return true;
    case SemanticsAction::LongClick:
        if (!click || !click->onLongClick) return false;
        {
            auto onLongClick = click->onLongClick;
            onLongClick();
        }
        return true;
    case SemanticsAction::Focus:  // what Tab can go to
        if ((!node->modifier.find<FocusableElement>() && !(click && click->enabled)) || !canFocus(*node)) return false;
        setFocus(node);
        return true;
    case SemanticsAction::ScrollForward:
    case SemanticsAction::ScrollBackward:
        if (auto* s = anyScrollable(*node)) {
            Rect b = node->bounds();
            float page = (s->orientation() == Orientation::Vertical ? b.height : b.width) * 0.8f;
            bool moved = std::abs(s->scrollBy(action == SemanticsAction::ScrollForward ? page : -page)) > 0.5f;
            s->settle(0);  // a pager goes on to the nearest page
            return moved;
        }
        return false;
    case SemanticsAction::SetText:
        return false;  // (with its text: the other overload)
    case SemanticsAction::SetProgress:
    case SemanticsAction::CustomAction: {
        auto sem = detail::semanticsOf(node->modifier);
        if (!sem) return false;
        if (action == SemanticsAction::SetProgress) return sem->setProgress && sem->setProgress(argument);
        size_t index = size_t(std::max(0.f, argument));
        return index < sem->customActions.size() && sem->customActions[index].action && sem->customActions[index].action();
    }
    }
    return false;
}

bool ComposeHost::performSemanticsAction(int id, SemanticsAction action, const std::string& text) {
    if (action != SemanticsAction::SetText) return false;
    LayoutNode* node = nullptr;
    for (auto& [n, i] : semanticsIds_)
        if (i == id) node = n;
    auto* f = node ? node->modifier.find<FocusableElement>() : nullptr;
    if (!f || !f->onEdit || f->readOnly) return false;
    // As the app setting it (the input method takes it from the focused field, the caret at its end).
    TextEditState state{text, int(text.size()), int(text.size())};
    if (f->transformEdit) state = f->transformEdit(fieldState(*f), state);
    auto onEdit = f->onEdit;
    onEdit(state);
    return true;
}

void ComposeHost::autofillDone(bool commit) {
    if (callbacks_.autofillDone) callbacks_.autofillDone(commit);
}

void ComposeHost::nodeRemoved(LayoutNode* node) {
    forgetLayoutReads(*node);
    forgetDrawReads(*node);
    if (mouseSelection_ && mouseSelection_->field == node) mouseSelection_.reset();
    if (lastMousePressField_ == node) lastMousePressField_ = nullptr;
    std::erase(pointerEntered_, node);
    if (dndSource_ == node) dndSource_ = nullptr;
    if (dnd_) {
        if (dnd_->source == node) dnd_->source = nullptr;
        if (dnd_->hovered == node) dnd_->hovered = nullptr;
        std::erase(dnd_->targets, node);
    }
    semanticsIds_.erase(node);
    focusStates_.erase(node);
    restoredFocus_.erase(node);
    // Gone while told visible: not visible (told after this layout). Its other entries go too.
    for (auto it = visibility_.lower_bound({node, 0}); it != visibility_.end() && it->first.first == node;) {
        if (it->second.reported) {
            it->second.reported = false;
            if (it->second.onChanged) visibilityCalls_.push_back([f = it->second.onChanged] { f(false); });
        }
        it = visibility_.erase(it);
    }
    if (focused_ == node) focusDirty_ = true;
    std::erase_if(overlays_, [&](const auto& entry) { return entry.first == node || entry.second == node; });
    if (pressed_ == node) pressed_ = nullptr;
    if (pendingClick_ == node) pendingClick_ = nullptr;
    for (size_t i = pointerTargets_.size(); i-- > 0;)
        if (pointerTargets_[i].node == node) {
            pointerTargets_.erase(pointerTargets_.begin() + std::ptrdiff_t(i));
            if (i < clickOrder_) --clickOrder_;
        }
    if (hovered_ == node) hovered_ = nullptr;
    auto gone = [&](std::optional<ActiveInteraction>& slot, Interaction (Interaction::*end)() const) {
        if (slot && slot->node == node) endInteraction(slot, end);
    };
    gone(pressInteraction_, &Interaction::cancel);
    gone(hoverInteraction_, &Interaction::exit);
    gone(focusInteraction_, &Interaction::unfocus);
    gone(dragInteraction_, &Interaction::cancel);
    std::erase_if(ripples_, [&](const Ripple& r) { return r.node == node; });
    std::erase_if(positioned_, [&](const auto& entry) { return entry.first.first == node; });
    std::erase_if(hoverables_, [&](const ActiveInteraction& a) {
        if (a.node != node) return false;
        a.source.tryEmit(a.start.exit());
        return true;
    });
    if (longPressTrigger_ == node) longPressTrigger_ = nullptr;
    if (tapFocus_.field == node) tapFocus_ = {};
    if (textLongPress_ == node) textLongPress_ = nullptr;
    if (lastTapField_ == node) lastTapField_ = nullptr;
    if (focused_ == node) touchSelection_ = false;
    if (hoveredTrigger_ == node) hoveredTrigger_ = nullptr;
    for (auto*& n : scrollNodes_)
        if (n == node) n = nullptr;
    if (flingNode_ == node) stopFling();
    if (wheel_ && wheel_->node == node) wheel_.reset();
    if (userScrolled_ == node) setUserScrolled(nullptr, 0);
    if (focused_ == node) {
        focused_ = nullptr;
        imeState_ = {};
        if (callbacks_.textInput) callbacks_.textInput(false, {}, imeState_, {});
    }
}

} // namespace kompoz
