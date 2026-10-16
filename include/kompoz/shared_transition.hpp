#pragma once

// Shared element transitions, as in Compose: an element in the content that leaves and one in
// the content that enters, with the same key, become one element that moves and resizes from
// the old place to the new one, drawn above everything else.
//
//   SharedTransitionLayout(Modifier(), [&](const SharedTransitionScope& shared) {
//       AnimatedContent<bool>(showDetails.get(), [&](const AnimatedContentScope& scope, const bool& details) {
//           Box(Modifier().size(details ? 200 : 48)
//                   .sharedElement(shared.rememberSharedContentState("photo"), scope)
//                   .background(color));
//       });
//   });
//
// While the new content enters, the element is laid out at the in-between size each frame and
// drawn at the in-between place; its match in the leaving content is hidden. sharedBounds
// shows both, cross-fading, each laid out at its own size and scaled to the moving bounds'
// width (Compose's default ScaleToBounds), so text in it doesn't reflow. When the bounds
// animation ends the element is drawn where it belongs; leaving content waits for it.

#include "animated.hpp"
#include "animation.hpp"
#include "layout.hpp"
#include "runtime.hpp"

#include <functional>
#include <memory>
#include <string>

namespace kompoz {

class ComposeHost;

namespace detail {
struct SharedRegistry;
struct SharedInstance;
} // namespace detail

// One shared element's identity: its key in the SharedTransitionLayout (Compose's
// rememberSharedContentState(key)).
class SharedContentState {
public:
    const std::string& key() const;
    bool isMatchFound() const;  // an element with the same key is in the content entering or leaving
    const std::shared_ptr<detail::SharedInstance>& instance() const { return instance_; }
    friend bool operator==(const SharedContentState& a, const SharedContentState& b) { return a.instance_ == b.instance_; }

private:
    friend class SharedTransitionScope;
    explicit SharedContentState(std::shared_ptr<detail::SharedInstance> instance) : instance_(std::move(instance)) {}
    std::shared_ptr<detail::SharedInstance> instance_;
};

class SharedTransitionScope {
public:
    explicit SharedTransitionScope(std::shared_ptr<detail::SharedRegistry> registry) : registry_(std::move(registry)) {}
    SharedContentState rememberSharedContentState(std::string key, CallSite site = CallSite::current()) const;
    bool isTransitionActive() const;  // some shared element is moving
    const std::shared_ptr<detail::SharedRegistry>& registry() const { return registry_; }
    friend bool operator==(const SharedTransitionScope& a, const SharedTransitionScope& b) { return a.registry_ == b.registry_; }

private:
    std::shared_ptr<detail::SharedRegistry> registry_;
};

// Hosts shared element transitions: elements of the content inside move between the places of
// their matches, drawn above the content.
void SharedTransitionLayout(const Modifier& modifier, const std::function<void(const SharedTransitionScope&)>& content,
                            CallSite site = CallSite::current());

// The default bounds animation (Compose's: a spring at medium-low stiffness).
inline AnimationSpec defaultBoundsTransform() { return spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow); }

} // namespace kompoz
