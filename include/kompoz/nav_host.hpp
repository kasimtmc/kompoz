#pragma once

// Navigation between screens, as androidx.navigation.compose does it: a NavController keeps the
// back stack, NavHost shows its top entry and animates between entries.
//
//   auto nav = rememberNavController();
//   NavHost(nav, "home", Modifier().fillMaxSize(), [&](NavGraphBuilder& graph) {
//       graph.composable("home", [&](const NavBackStackEntry&) {
//           Button([nav] { nav.navigate("detail/42"); }, [] { Text("Aç"); });
//       });
//       graph.composable("detail/{id}?tab={tab}", [&](const NavBackStackEntry& entry) {
//           Text("Öğe " + std::to_string(entry.arguments().getInt("id").value_or(0)));
//       });
//   });
//
// Routes are paths with {name} parts and optional ?name={name} query parameters. The back button
// pops the stack while there is something to go back to. Each entry's rememberSaveable state
// (scroll positions too) is kept while it is on the back stack, or saved with saveState.
//
// Screens that belong together go in a nested graph, with a route of its own:
//
//   graph.navigation("username", "login", [&](NavGraphBuilder& login) {
//       login.composable("username", [&](const NavBackStackEntry& entry) {
//           auto form = viewModel<LoginViewModel>(*entry.parent());  // shared by the graph's screens
//       });
//       login.composable("password", ...);
//   });
//   nav.navigate("login");                                          // its start destination
//   nav.navigate("home", {.popUpTo = "login", .inclusive = true});  // the whole flow off the stack
//
// The graph has an entry on the back stack below its screens (its ViewModels live as long as
// it does); going back from its last screen takes it off too.
//
// Deep links: a destination can answer URIs, and links the app is opened with (Android
// intents, the desktop command line, the web address's #fragment) go there, with the start
// destination and the screens of the graphs on the way below it, so that back leads up:
//
//   graph.composable("item/{n}", {navDeepLink("kompoz://item/{n}")}, [&](const NavBackStackEntry& e) { ... });
//   nav.navigate("kompoz://item/42");  // a link from inside the app: on top of the stack
//
// Type-safe routes (Compose's @Serializable ones): a struct whose fields are the arguments.
// Its fields are path segments in order; std::optional ones are query parameters.
//
//   struct Profile {
//       std::string id;
//       std::optional<int> tab;
//       KOMPOZ_ROUTE(Profile, id, tab)  // the route "Profile/{id}?tab={tab}"
//   };
//   graph.composable<Profile>([&](const NavBackStackEntry& e) { Profile p = e.toRoute<Profile>(); });
//   nav.navigate(Profile{.id = "42"});
//
// Fields can be strings, numbers, bools, enums, and std::optionals of them.

#include "animated.hpp"
#include "lifecycle.hpp"
#include "runtime.hpp"
#include "named.hpp"
#include "viewmodel.hpp"

#include <cstdlib>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace kompoz {

// An entry's arguments: the {name} parts of its route, as navigated to.
class NavArguments {
public:
    std::optional<std::string> getString(std::string_view name) const;
    std::optional<int> getInt(std::string_view name) const;
    std::optional<long long> getLong(std::string_view name) const;
    std::optional<float> getFloat(std::string_view name) const;
    std::optional<bool> getBool(std::string_view name) const;  // true / false
    bool contains(std::string_view name) const { return values_.find(name) != values_.end(); }
    std::map<std::string, std::string, std::less<>>& values() { return values_; }
    const std::map<std::string, std::string, std::less<>>& values() const { return values_; }

private:
    std::map<std::string, std::string, std::less<>> values_;
};

// ---- Type-safe routes ----------------------------------------------------------------------

// In a route struct: its route's name (the struct's name with KOMPOZ_ROUTE) and argument fields.
#define KOMPOZ_ROUTE(Type, ...) KOMPOZ_NAMED_ROUTE(Type, #Type __VA_OPT__(, ) __VA_ARGS__)
#define KOMPOZ_NAMED_ROUTE(Type, name, ...)                                                                                \
    friend auto kompozRouteSpec(const Type*) {                                                                         \
        return ::kompoz::detail::routeSpec(name __VA_OPT__(, KOMPOZ_FOR_EACH(KOMPOZ_ROUTE_FIELD, Type, __VA_ARGS__))); \
    }
#define KOMPOZ_ROUTE_FIELD(Type, field) ::kompoz::detail::routeField(#field, &Type::field)
// KOMPOZ_FOR_EACH(m, T, a, b, c): m(T, a), m(T, b), m(T, c)
#define KOMPOZ_PARENS ()
#define KOMPOZ_EXPAND(...) KOMPOZ_EXPAND3(KOMPOZ_EXPAND3(KOMPOZ_EXPAND3(KOMPOZ_EXPAND3(__VA_ARGS__))))
#define KOMPOZ_EXPAND3(...) KOMPOZ_EXPAND2(KOMPOZ_EXPAND2(KOMPOZ_EXPAND2(KOMPOZ_EXPAND2(__VA_ARGS__))))
#define KOMPOZ_EXPAND2(...) KOMPOZ_EXPAND1(KOMPOZ_EXPAND1(KOMPOZ_EXPAND1(KOMPOZ_EXPAND1(__VA_ARGS__))))
#define KOMPOZ_EXPAND1(...) __VA_ARGS__
#define KOMPOZ_FOR_EACH(macro, T, ...) __VA_OPT__(KOMPOZ_EXPAND(KOMPOZ_FOR_EACH_NEXT(macro, T, __VA_ARGS__)))
#define KOMPOZ_FOR_EACH_NEXT(macro, T, first, ...) \
    macro(T, first) __VA_OPT__(, KOMPOZ_FOR_EACH_AGAIN KOMPOZ_PARENS(macro, T, __VA_ARGS__))
#define KOMPOZ_FOR_EACH_AGAIN() KOMPOZ_FOR_EACH_NEXT

namespace detail {

template <class C, class M>
struct RouteField {
    std::string_view name;
    M C::*member;
};
template <class C, class M>
constexpr RouteField<C, M> routeField(std::string_view name, M C::*member) {
    return {name, member};
}
template <class... F>
struct RouteSpec {
    std::string_view name;
    std::tuple<F...> fields;
};
template <class... F>
constexpr RouteSpec<F...> routeSpec(std::string_view name, F... fields) {
    return {name, {fields...}};
}

template <class T>
struct Optional : std::false_type {};
template <class T>
struct Optional<std::optional<T>> : std::true_type {
    using type = T;
};

std::string encodeRouteValue(std::string_view s);  // %XX for what a path can't hold as it is

template <class V>
std::string routeValue(const V& v) {
    if constexpr (std::is_same_v<V, std::string>) return v;
    else if constexpr (std::is_same_v<V, bool>) return v ? "true" : "false";
    else if constexpr (std::is_enum_v<V>) return std::to_string(static_cast<std::underlying_type_t<V>>(v));
    else if constexpr (std::is_integral_v<V> || std::is_floating_point_v<V>) return std::format("{}", v);
    else static_assert(sizeof(V) == 0, "route fields are strings, numbers, bools, enums or optionals of them");
}

} // namespace detail

// A struct with KOMPOZ_ROUTE.
template <class T>
concept TypedRoute = requires(const T* p) { kompozRouteSpec(p); };

// The route pattern of T: "Profile/{id}?tab={tab}".
template <TypedRoute T>
std::string routePattern() {
    auto spec = kompozRouteSpec(static_cast<const T*>(nullptr));
    std::string path(spec.name), query;
    std::apply([&](const auto&... f) {
        auto one = [&](const auto& field) {
            using M = std::remove_cvref_t<decltype(std::declval<T>().*(field.member))>;
            std::string param = "{" + std::string(field.name) + "}";
            if constexpr (detail::Optional<M>::value) query += (query.empty() ? "?" : "&") + std::string(field.name) + "=" + param;
            else path += "/" + param;
        };
        (one(f), ...);
    }, spec.fields);
    return path + query;
}

// The path to navigate to for a value of T: "Profile/42?tab=3".
template <TypedRoute T>
std::string routePath(const T& route) {
    auto spec = kompozRouteSpec(static_cast<const T*>(nullptr));
    std::string path(spec.name), query;
    std::apply([&](const auto&... f) {
        auto one = [&](const auto& field) {
            const auto& value = route.*(field.member);
            using M = std::remove_cvref_t<decltype(value)>;
            if constexpr (detail::Optional<M>::value) {
                if (value)
                    query += (query.empty() ? "?" : "&") + std::string(field.name) + "=" +
                             detail::encodeRouteValue(detail::routeValue(*value));
            } else {
                path += "/" + detail::encodeRouteValue(detail::routeValue(value));
            }
        };
        (one(f), ...);
    }, spec.fields);
    return path + query;
}

// An entry's arguments back as a T.
template <TypedRoute T>
T routeFrom(const NavArguments& args);

// A screen on the back stack, or a nested graph that screens above it belong to.
// Also a LifecycleOwner (the LocalLifecycleOwner of its screen): resumed while it is the screen
// shown, started while a transition to or from it runs, created on the back stack (or in a
// saved one), destroyed once popped and off the screen — the NavHost's own lifecycle at most.
// What an entry is of (Compose's NavDestination): its route pattern, and those of the graphs
// around it (innermost first).
struct NavDestination {
    std::string route;
    std::vector<std::string> graphs;
    // Compose's destination.hierarchy: this one, then the graphs it is in, innermost first
    // (destination.hierarchy.any { it.hasRoute<Tab>() }: whether a tab's graph is shown).
    std::vector<NavDestination> hierarchy() const {
        std::vector<NavDestination> out{*this};
        for (size_t i = 0; i < graphs.size(); ++i) out.push_back({graphs[i], {graphs.begin() + std::ptrdiff_t(i) + 1, graphs.end()}});
        return out;
    }
    template <TypedRoute T>
    bool hasRoute() const {
        return route == routePattern<T>();
    }
    friend bool operator==(const NavDestination&, const NavDestination&) = default;
};

class NavBackStackEntry : public ViewModelStoreOwner, public LifecycleOwner {
public:
    uint64_t id() const { return id_; }
    const std::string& route() const { return route_; }  // the destination's pattern ("detail/{id}")
    NavDestination destination() const;  // Compose's entry.destination (its route, its hierarchy)
    const std::string& path() const { return path_; }    // as navigated to ("detail/42")
    const NavArguments& arguments() const { return arguments_; }
    bool isGraph() const { return graph_; }
    // The entry of the graph this one is in (nothing at the top level). Its ViewModels are the
    // graph's; it is kept while this entry is, leaving animations too.
    const std::shared_ptr<const NavBackStackEntry>& parent() const { return parent_; }
    // This route, then those of the graphs around it, innermost first (Compose's
    // destination.hierarchy): whether a tab's graph is shown.
    std::vector<std::string> hierarchy() const;
    const std::shared_ptr<ViewModelStore>& viewModelStore() const override { return store_; }
    Lifecycle& lifecycle() const override { return lifecycle_; }
    // Type-safe routes: its arguments as a T, and whether it is a T's destination.
    template <TypedRoute T>
    T toRoute() const {
        return routeFrom<T>(arguments_);
    }
    template <TypedRoute T>
    bool hasRoute() const {
        return route_ == routePattern<T>();
    }

private:
    friend class NavController;
    uint64_t id_ = 0;
    std::string route_, path_;
    NavArguments arguments_;
    bool graph_ = false;
    std::shared_ptr<const NavBackStackEntry> parent_;
    std::shared_ptr<ViewModelStore> store_ = std::make_shared<ViewModelStore>();
    mutable Lifecycle lifecycle_;
};

// A URI pattern a destination answers: "kompoz://item/{n}", "https://example.com/search?q={q}".
// The scheme is matched regardless of case; {name} parts and ?name={name} parameters become
// the destination's arguments.
struct NavDeepLink {
    std::string uriPattern;
};
inline NavDeepLink navDeepLink(std::string uriPattern) { return {std::move(uriPattern)}; }
// Compose's navDeepLink { uriPattern = "…" }.
template <class F>
    requires std::is_invocable_v<F&, NavDeepLink&>
NavDeepLink navDeepLink(F&& build) {
    NavDeepLink link;
    build(link);
    return link;
}

// A deep link pattern for T under `basePath`: navDeepLink<Profile>("kompoz://profile") answers
// "kompoz://profile/{id}?tab={tab}".
template <TypedRoute T>
NavDeepLink navDeepLink(std::string basePath) {
    std::string pattern = routePattern<T>();
    std::string_view name = kompozRouteSpec(static_cast<const T*>(nullptr)).name;
    return {std::move(basePath) + pattern.substr(name.size())};
}

template <TypedRoute T>
T routeFrom(const NavArguments& args) {
    T route{};
    auto spec = kompozRouteSpec(static_cast<const T*>(nullptr));
    auto parse = [&]<class V>(std::string_view name, V*) -> std::optional<V> {
        if constexpr (std::is_same_v<V, std::string>) return args.getString(name);
        else if constexpr (std::is_same_v<V, bool>) return args.getBool(name);
        else if constexpr (std::is_floating_point_v<V>) {
            auto s = args.getString(name);
            if (!s || s->empty()) return std::nullopt;
            char* end = nullptr;
            double v = std::strtod(s->c_str(), &end);
            return end == s->c_str() + s->size() ? std::optional<V>(V(v)) : std::nullopt;
        } else {  // integers and enums
            auto v = args.getLong(name);
            return v ? std::optional<V>(static_cast<V>(*v)) : std::nullopt;
        }
    };
    std::apply([&](const auto&... f) {
        auto one = [&](const auto& field) {
            auto& value = route.*(field.member);
            using M = std::remove_cvref_t<decltype(value)>;
            if constexpr (detail::Optional<M>::value) {
                value = parse(field.name, static_cast<typename detail::Optional<M>::type*>(nullptr));
            } else if (auto v = parse(field.name, static_cast<M*>(nullptr))) {
                value = *v;
            }
        };
        (one(f), ...);
    }, spec.fields);
    return route;
}

struct NavOptions {
    std::optional<std::string> popUpTo;  // pop the stack down to this route (pattern or path) first...
    bool inclusive = false;              // ...and that entry too
    bool saveState = false;              // keep what is popped, for restoreState
    bool launchSingleTop = false;        // the destination already on top: no second copy
    bool restoreState = false;           // bring back the entries saved for this destination
};
// Compose's navigate(route) { popUpTo("home") { saveState = true }; launchSingleTop = true }: the
// options built in a block.
struct PopUpToBuilder {
    bool inclusive = false, saveState = false;
};
struct NavOptionsBuilder {
    bool launchSingleTop = false, restoreState = false;
    std::optional<std::string> popUpToRoute;
    PopUpToBuilder popUpToOptions;
    void popUpTo(std::string route) { popUpToRoute = std::move(route); }
    template <class F>
        requires std::is_invocable_v<F&, PopUpToBuilder&>
    void popUpTo(std::string route, F&& build) {
        popUpToRoute = std::move(route);
        build(popUpToOptions);
    }
    template <TypedRoute T>
    void popUpTo() { popUpToRoute = routePattern<T>(); }
    template <TypedRoute T, class F>
        requires std::is_invocable_v<F&, PopUpToBuilder&>
    void popUpTo(F&& build) {
        popUpTo(routePattern<T>(), std::forward<F>(build));
    }
    NavOptions options() const {
        return {popUpToRoute, popUpToOptions.inclusive, popUpToOptions.saveState, launchSingleTop, restoreState};
    }
};
// Compose's navOptions { popUpTo<Home> { saveState = true }; launchSingleTop = true }: the options, built
// once, for navigate(route, options).
template <class F>
    requires std::is_invocable_v<F&, NavOptionsBuilder&>
NavOptions navOptions(F&& build) {
    NavOptionsBuilder b;
    build(b);
    return b.options();
}

class NavController {
public:
    NavController();
    // Goes to `path`, which must match one of the NavHost's routes, or to the destination a
    // deep link URI leads to.
    void navigate(std::string path, const NavOptions& options = {}) const;
    template <class F>
        requires std::is_invocable_v<F&, NavOptionsBuilder&>
    void navigate(std::string path, F&& build) const {
        NavOptionsBuilder b;
        build(b);
        navigate(std::move(path), b.options());
    }
    // Opens a link as the app does when it is started with one: the back stack starts over
    // with the start destination, the graphs on the way and the destination. False if no
    // destination answers it (a URI, or a route's path).
    bool handleDeepLink(std::string_view link) const;
    // Type-safe routes.
    template <TypedRoute T>
    void navigate(const T& route, const NavOptions& options = {}) const {
        navigate(routePath(route), options);
    }
    template <TypedRoute T, class F>
        requires std::is_invocable_v<F&, NavOptionsBuilder&>
    void navigate(const T& route, F&& build) const {
        navigate(routePath(route), std::forward<F>(build));
    }
    template <TypedRoute T>
    std::shared_ptr<const NavBackStackEntry> getBackStackEntry() const {
        return getBackStackEntry(routePattern<T>());
    }
    template <TypedRoute T>
    bool popBackStack(bool inclusive, bool saveState = false) const {
        return popBackStack(routePattern<T>(), inclusive, saveState);
    }
    // Pops the top entry (and the graphs it leaves); false if it is the last screen.
    bool popBackStack() const;
    // Pops down to the topmost entry with this route (or path); false if there is none.
    bool popBackStack(std::string_view route, bool inclusive, bool saveState = false) const;
    bool navigateUp() const { return popBackStack(); }
    // Read in a composable, these recompose it when the stack changes.
    std::shared_ptr<const NavBackStackEntry> currentBackStackEntryAsState() const;
    std::shared_ptr<const NavBackStackEntry> currentBackStackEntry() const;  // without subscribing
    std::shared_ptr<const NavBackStackEntry> previousBackStackEntry() const;  // the screen below
    // The topmost entry with this route (or path), a graph's too; nothing if there is none.
    std::shared_ptr<const NavBackStackEntry> getBackStackEntry(std::string_view route) const;
    std::vector<std::shared_ptr<const NavBackStackEntry>> backStack() const;  // graphs' entries too
    bool canNavigateBack() const;  // more than one screen
    friend bool operator==(const NavController& a, const NavController& b) { return a.data_ == b.data_; }

    // Its back stack and the stacks saved with saveState, written out and read back
    // (Saver<NavController>): entries keep their ids, and so their rememberSaveable values;
    // their ViewModels start anew, with what they kept in their SavedStateHandles.
    SavedValue save() const;
    static NavController restore(const SavedValue& saved);

    struct Data;
    const std::shared_ptr<Data>& data() const { return data_; }  // NavHost's

private:
    std::shared_ptr<Data> data_;
};

template <>
struct Saver<NavController> {
    static SavedValue save(const NavController& nav) { return nav.save(); }
    static std::optional<NavController> restore(const SavedValue& saved) { return NavController::restore(saved); }
};

NavController rememberNavController(CallSite site = CallSite::current());

// NavHost's builder: the destinations, by route (unique in the whole NavHost).
class NavGraphBuilder {
public:
    void composable(std::string route, std::function<void(const NavBackStackEntry&)> content);
    void composable(std::string route, std::vector<NavDeepLink> deepLinks, std::function<void(const NavBackStackEntry&)> content);
    // A nested graph: navigating to `route` goes to `startDestination`, one of those `builder` adds.
    void navigation(std::string startDestination, std::string route, const std::function<void(NavGraphBuilder&)>& builder);
    // Type-safe routes: composable<Profile>(...), navigation<Login>(Username{}, ...).
    template <TypedRoute T>
    void composable(std::function<void(const NavBackStackEntry&)> content) {
        composable(routePattern<T>(), std::move(content));
    }
    template <TypedRoute T>
    void composable(std::vector<NavDeepLink> deepLinks, std::function<void(const NavBackStackEntry&)> content) {
        composable(routePattern<T>(), std::move(deepLinks), std::move(content));
    }
    template <TypedRoute T, TypedRoute S>
    void navigation(const S& startDestination, const std::function<void(NavGraphBuilder&)>& builder) {
        (void)startDestination;
        navigation(routePattern<S>(), routePattern<T>(), builder);
    }
    // The same by name, as Compose writes them: composable<Item>(deepLinks = listOf(…)) { entry -> … } and
    // navigation<ListGraph>(startDestination = ListAll) { … }.
    template <TypedRoute T, class... A>
        requires(detail::anyNamedArgument<A...>)
    void composable(A&&... args) {
        namespace n = detail::names;
        using P = detail::Params<n::DeepLinks, detail::BlockWith<const NavBackStackEntry&>>;
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: composable<T>(…) { entry -> … } takes its content");
        std::function<void(const NavBackStackEntry&)> content = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
        composable(routePattern<T>(), P::get<n::DeepLinks>(std::vector<NavDeepLink>(), args...), std::move(content));
    }
    template <TypedRoute T, class... A>
        requires(detail::anyNamedArgument<A...>)
    void navigation(A&&... args) {
        namespace n = detail::names;
        using P = detail::Params<n::StartDestination, detail::BlockWith<NavGraphBuilder&>>;
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: navigation<T>(startDestination) { … } takes its destinations");
        std::function<void(NavGraphBuilder&)> builder = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
        using S = std::remove_cvref_t<decltype(P::arg<n::StartDestination>(args...))>;
        navigation(routePattern<S>(), routePattern<T>(), builder);
    }
    // Compose's names: composable(route, deepLinks = listOf(…)) { entry -> … } and
    // navigation(startDestination = …, route = …) { … }.
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    void composable(A&&... args) {
        namespace n = detail::names;
        using P = detail::Params<n::Route, n::DeepLinks, detail::BlockWith<const NavBackStackEntry&>>;
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: composable(route) { entry -> … } takes its content");
        std::function<void(const NavBackStackEntry&)> content = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
        composable(std::string(P::arg<n::Route>(args...)), P::get<n::DeepLinks>(std::vector<NavDeepLink>(), args...), std::move(content));
    }
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    void navigation(A&&... args) {
        namespace n = detail::names;
        using P = detail::Params<n::StartDestination, n::Route, detail::BlockWith<NavGraphBuilder&>>;
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: navigation(startDestination, route) { … } takes its destinations");
        std::function<void(NavGraphBuilder&)> builder = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
        navigation(std::string(P::arg<n::StartDestination>(args...)), std::string(P::arg<n::Route>(args...)), builder);
    }

    struct Destination {
        std::string route;
        std::function<void(const NavBackStackEntry&)> content;  // none for a graph
        std::string startDestination;  // a graph's
        std::string parent;            // the route of the graph it is in ("": the NavHost's)
        std::vector<NavDeepLink> deepLinks;
        bool isGraph() const { return !content; }
    };
    const std::vector<Destination>& destinations() const { return destinations_; }  // nested ones too

private:
    std::vector<Destination> destinations_;
    std::string parent_;
};

// How entries come and go: navigating forward, and going back (Compose's defaults: a 700 ms
// cross-fade).
struct NavHostOptions {
    ContentTransform transition = togetherWith(fadeIn(0, tween(700)), fadeOut(0, tween(700)));
    ContentTransform popTransition = togetherWith(fadeIn(0, tween(700)), fadeOut(0, tween(700)));
    // Or, set, the transform for each navigation, from the entry leaving to the one coming (Compose's
    // enterTransition / exitTransition lambdas on their AnimatedContentTransitionScope).
    std::function<ContentTransform(const NavBackStackEntry& from, const NavBackStackEntry& to)> transitionFor, popTransitionFor;
};

namespace detail {
void NavHost(const NavController& navController, std::string startDestination, const Modifier& modifier,
             const std::function<void(NavGraphBuilder&)>& builder, const NavHostOptions& options = {}, CallSite site = CallSite::current());
template <TypedRoute S>
void NavHost(const NavController& navController, const S& startDestination, const Modifier& modifier,
             const std::function<void(NavGraphBuilder&)>& builder, const NavHostOptions& options = {}, CallSite site = CallSite::current()) {
    NavHost(navController, routePath(startDestination), modifier, builder, options, site);
}
// NavHost(nav, start, modifier, builder, options) as it was, or Compose's NavHost(navController = …,
// startDestination = …, modifier = …, enterTransition = { }, exitTransition = { }, popEnterTransition = { },
// popExitTransition = { }) { composable(…) { } }: the transitions' lambdas on their scope (initialState,
// targetState: the entries), the builder last.
struct NavHostImpl {
    static constexpr bool takesContentByName = true;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        using P = Params<names::NavController, names::StartDestination, names::Modifier, names::Content, names::Options, names::EnterTransition,
                         names::ExitTransition, names::PopEnterTransition, names::PopExitTransition, BlockWith<NavGraphBuilder&>>;
        static_assert(P::check<A...>());
        std::function<void(NavGraphBuilder&)> builder;
        if constexpr (P::blockGiven<A...>()) builder = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
        else builder = P::arg<names::Content>(args...);
        NavHostOptions options = P::get<names::Options>(NavHostOptions{}, args...);
        using Scope = AnimatedContentTransitionScope<NavBackStackEntry>;
        auto transform = [&](auto enterTag, auto exitTag, auto& into) {
            using Enter = decltype(enterTag);
            using Exit = decltype(exitTag);
            if constexpr (P::given<Enter, A...>() || P::given<Exit, A...>()) {
                std::function<EnterTransition(const Scope&)> enter;
                std::function<ExitTransition(const Scope&)> exit;
                if constexpr (P::given<Enter, A...>()) enter = P::arg<Enter>(args...);
                else enter = [](const Scope&) { return fadeIn(0, tween(700)); };
                if constexpr (P::given<Exit, A...>()) exit = P::arg<Exit>(args...);
                else exit = [](const Scope&) { return fadeOut(0, tween(700)); };
                into = [enter, exit](const NavBackStackEntry& from, const NavBackStackEntry& to) {
                    Scope scope{from, to};
                    return togetherWith(enter(scope), exit(scope));
                };
            }
        };
        transform(names::EnterTransition{}, names::ExitTransition{}, options.transitionFor);
        transform(names::PopEnterTransition{}, names::PopExitTransition{}, options.popTransitionFor);
        if (!options.popTransitionFor) options.popTransitionFor = options.transitionFor;  // (Compose's pop defaults to the forward ones)
        auto&& start = P::arg<names::StartDestination>(args...);
        using S = std::remove_cvref_t<decltype(start)>;
        if constexpr (std::is_convertible_v<S, std::string>)
            NavHost(P::arg<names::NavController>(args...), std::string(start), P::get<names::Modifier>(Modifier{}, args...), builder, options, site);
        else
            NavHost(P::arg<names::NavController>(args...), routePath(start), P::get<names::Modifier>(Modifier{}, args...), builder, options, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::NavHostImpl> NavHost;

} // namespace kompoz
