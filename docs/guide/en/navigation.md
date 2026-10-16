# Navigation

Moving between screens with a back stack, as `androidx.navigation.compose` does: a `NavController` keeps the stack, a `NavHost` shows its top entry and animates between entries.

## A NavHost

```cpp
void App() {
    auto nav = rememberNavController();
    NavHost(nav, "home", Modifier().fillMaxSize(), [&](NavGraphBuilder& graph) {
        graph.composable("home", [nav](const NavBackStackEntry&) {
            Button([nav] { nav.navigate("detail/42"); }, [] { Text("Open item 42"); });
        });
        graph.composable("detail/{id}", [nav](const NavBackStackEntry& entry) {
            int id = entry.arguments().getInt("id").value_or(0);
            Column([&] {
                Text("Item " + std::to_string(id));
                TextButton([nav] { nav.popBackStack(); }, [] { Text("Back"); });
            });
        });
    });
}
```

- `rememberNavController()` makes the controller. It is saveable: the back stack comes back after the process was ended.
- `NavHost(nav, startDestination, modifier, builder)` declares the destinations and shows the current one.
- `graph.composable(route, content)` adds a destination. Its content gets the `NavBackStackEntry`.
- `nav.navigate(path)` pushes a destination; `nav.popBackStack()` goes back.

The system back button and gesture (and Escape on the desktop) pop the stack while there is something to go back to.

## Routes and arguments

A route is a path with `{name}` parts, and optional query parameters: `"detail/{id}?tab={tab}"`. Navigate with the values filled in (`"detail/42?tab=2"`), and read them from the entry's arguments with `getString`, `getInt`, `getLong`, `getFloat` and `getBool`, each an `std::optional`.

### Type-safe routes

As Compose's `@Serializable` routes, a struct can be a route. Its required fields are path segments, its `std::optional` fields query parameters:

```cpp
struct Profile {
    std::string id;
    std::optional<int> tab;
    KOMPOZ_ROUTE(Profile, id, tab)   // the route "Profile/{id}?tab={tab}"
};

struct Home {
    KOMPOZ_ROUTE(Home)
};

void TypedApp() {
    auto nav = rememberNavController();
    NavHost(nav, Home{}, Modifier().fillMaxSize(), [&](NavGraphBuilder& graph) {
        graph.composable<Home>([nav](const NavBackStackEntry&) {
            Button([nav] { nav.navigate(Profile{.id = "ada"}); }, [] { Text("Ada's profile"); });
        });
        graph.composable<Profile>([](const NavBackStackEntry& e) {
            Profile p = e.toRoute<Profile>();
            Text("Profile of " + p.id + ", tab " + std::to_string(p.tab.value_or(0)));
        });
    });
}
```

Fields can be strings, numbers, bools, enums and `std::optional`s of them. `nav.getBackStackEntry<T>()`, `nav.popBackStack<T>(inclusive)` and `entry.hasRoute<T>()` work with types too.

## Navigation options

`navigate` takes `NavOptions`:

| Option | What it does |
|---|---|
| `popUpTo` | Pops the stack down to this route first… |
| `inclusive` | …and that entry too. |
| `launchSingleTop` | No second copy if the destination is already on top. |
| `saveState` | Keeps the state of what is popped… |
| `restoreState` | …and brings it back when navigating to it again. |

The usual bottom-navigation pattern, switching between tabs while keeping each tab's stack and scroll position:

```cpp body
#! NavController nav;
#! std::string tab;
nav.navigate(tab, {.popUpTo = "home", .saveState = true, .launchSingleTop = true, .restoreState = true});
```

After a login flow, clear it from the stack:

```cpp body
#! NavController nav;
nav.navigate("home", {.popUpTo = "login", .inclusive = true});
```

## The current destination

`nav.currentBackStackEntryAsState()` returns the top entry and recomposes when it changes: use it to highlight the selected item of a navigation bar.

```cpp
void BottomBar(NavController nav) {
    auto entry = nav.currentBackStackEntryAsState();
    std::string route = entry ? entry->route() : "";
    NavigationBar(Modifier(), [&] {
        NavigationBarItem(route == "home", [nav] { nav.navigate("home", {.launchSingleTop = true}); }, Icons::Filled::Home, "Home");
        NavigationBarItem(route == "search", [nav] { nav.navigate("search", {.launchSingleTop = true}); }, Icons::Filled::Search, "Search");
    });
}
```

## State on the back stack

Each entry keeps its `rememberSaveable` state, scroll positions included, while it is on the back stack: going back shows the screen as it was. Each entry is also a `ViewModel` owner: `viewModel<T>()` inside a destination gives that entry's, cleared when the entry is popped for good. See [Architecture](architecture.md).

## Nested graphs

Screens that belong together, such as a sign-in flow, go in a nested graph with a route of its own. Navigating to the graph's route opens its start destination:

```cpp
#! class LoginViewModel : public ViewModel { public: MutableStateFlow<std::string> user{std::string()}; };
void Graphs(NavController nav) {
    NavHost(nav, "home", Modifier().fillMaxSize(), [&](NavGraphBuilder& graph) {
        graph.composable("home", [nav](const NavBackStackEntry&) {
            Button([nav] { nav.navigate("login"); }, [] { Text("Sign in"); });
        });
        graph.navigation("username", "login", [&](NavGraphBuilder& login) {
            login.composable("username", [nav](const NavBackStackEntry& e) {
                auto form = viewModel<LoginViewModel>(*e.parent());   // shared by the graph's screens
                Button([nav] { nav.navigate("password"); }, [] { Text("Next"); });
            });
            login.composable("password", [nav](const NavBackStackEntry& e) {
                auto form = viewModel<LoginViewModel>(*e.parent());   // the same one
                Button([nav] { nav.navigate("home", {.popUpTo = "login", .inclusive = true}); }, [] { Text("Done"); });
            });
        });
    });
}
```

The graph has an entry of its own on the back stack, below its screens, so its `ViewModel`s live as long as the flow does.

## Deep links

A destination can answer URIs. When the app is opened with a link, the `NavHost` goes to that destination, with the start destination and the screens of the graphs on the way below it, so that back leads up:

```cpp body
#! NavController nav;
NavHost(nav, "home", Modifier().fillMaxSize(), [&](NavGraphBuilder& graph) {
    graph.composable("home", [](const NavBackStackEntry&) { Text("Home"); });
    graph.composable("item/{n}", {navDeepLink("kompoz://item/{n}")}, [](const NavBackStackEntry& e) {
        Text("Item " + std::to_string(e.arguments().getInt("n").value_or(0)));
    });
});
```

Where links come from:

- **Android**: a `VIEW` intent. Add an `intent-filter` for the scheme to the manifest, and forward links that arrive while the app is open from `MainActivity.onNewIntent` to `KompozInput.onNewIntent`, as the showcase does.
- **Desktop**: the command line, `./myapp kompoz://item/42`.
- **Web**: the address's `#` part, `app.html#item/42`. The current screen is written there too, so reloading opens the same one.

From inside the app, `nav.navigate("kompoz://item/42")` follows a link onto the top of the stack, and `nav.handleDeepLink(link)` goes there as if the app had been opened with it. Type-safe routes take `navDeepLink<T>("kompoz://profile")`.

## Transitions

`NavHostOptions` sets the animation between destinations, as an `AnimatedContent` transform, one for navigating forward and one for going back:

```cpp body
#! NavController nav;
NavHost(nav, "home", Modifier().fillMaxSize(), [&](NavGraphBuilder& graph) {
    graph.composable("home", [](const NavBackStackEntry&) { Text("Home"); });
}, {.transition = togetherWith(slideInHorizontally(1), slideOutHorizontally(-0.3f)),
    .popTransition = togetherWith(slideInHorizontally(-0.3f), slideOutHorizontally(1))});
```

The default is a cross-fade.

## Back handling

`BackHandler(enabled, onBack)` takes the back button, gesture or Escape while `enabled`; the most recently composed enabled one wins. Use it for things that should close before the screen does, such as a selection mode:

```cpp
void Selectable() {
    auto selecting = remember([] { return mutableStateOf(false); });
    BackHandler(selecting.get(), [selecting] { selecting.set(false); });
    Text(selecting.get() ? "Selecting (back to stop)" : "Long press to select",
         Modifier().combinedClickable([] {}, [selecting] { selecting.set(true); }));
}
```

`PredictiveBackHandler(enabled, onBack)` follows Android's predictive back gesture as it happens: its coroutine gets a flow of the gesture's progress, so the UI can move with the finger, and is cancelled if the gesture is called off. Popups, drawers and sheets handle back themselves.

## See also

- [Architecture](architecture.md): `ViewModel`s per destination.
- [Animation](animation.md): content transforms and shared elements.
- [State](state.md): what `rememberSaveable` keeps on the back stack.
