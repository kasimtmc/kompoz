# Navigation

Moving between screens with a back stack, as `androidx.navigation.compose` does: a `NavController` keeps the stack, a `NavHost` shows its top entry and animates between entries.

## A NavHost

```kotlin
@Composable
fun App() {
    val nav = rememberNavController()
    NavHost(navController = nav, startDestination = "home", modifier = Modifier.fillMaxSize()) {
        composable("home") {
            Button(onClick = { nav.navigate("detail/42") }) { Text("Open item 42") }
        }
        composable("detail/{id}") { backStackEntry ->
            val id = backStackEntry.arguments?.getInt("id") ?: 0
            Column {
                Text("Item $id")
                TextButton(onClick = { nav.popBackStack() }) { Text("Back") }
            }
        }
    }
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

As Compose's, a `@Serializable` data class (or object) is a route. Its required fields are path segments, its nullable fields query parameters:

```kotlin
@Serializable
data class Profile(val id: String, val tab: Int? = null)   // the route "Profile/{id}?tab={tab}"

@Serializable
object Home

@Composable
fun TypedApp() {
    val nav = rememberNavController()
    NavHost(navController = nav, startDestination = Home, modifier = Modifier.fillMaxSize()) {
        composable<Home> {
            Button(onClick = { nav.navigate(Profile(id = "ada")) }) { Text("Ada's profile") }
        }
        composable<Profile> { backStackEntry ->
            val profile = backStackEntry.toRoute<Profile>()
            Text("Profile of ${profile.id}, tab ${profile.tab ?: 0}")
        }
    }
}
```

Fields can be strings, numbers, bools, enums and nullable ones of them. `nav.getBackStackEntry<T>()`, `nav.popBackStack<T>(inclusive)` and `entry.hasRoute<T>()` work with types too.

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

```kotlin body
#! val nav = rememberNavController()
#! val tab = "search"
nav.navigate(tab) {
    popUpTo("home") { saveState = true }
    launchSingleTop = true
    restoreState = true
}
```

Which tab is shown is the destination's `hierarchy` (the destination, then the graphs it is in): a tab's nested graph counts with all its screens. `navOptions { }` builds the options once, for `navigate(route, options)`:

```kotlin
@Serializable
object HomeTab

@Serializable
object SettingsTab

@Composable
fun Tabs(nav: NavController) {
    val entry by nav.currentBackStackEntryAsState()
    val inSettings = entry?.destination?.hierarchy?.any { it.hasRoute<SettingsTab>() } == true
    val tabs = navOptions {
        popUpTo<HomeTab> { saveState = true }
        launchSingleTop = true
        restoreState = true
    }
    Row {
        TextButton(onClick = { nav.navigate(HomeTab, tabs) }, enabled = inSettings) { Text("Home") }
        TextButton(onClick = { nav.navigate(SettingsTab, tabs) }, enabled = !inSettings) { Text("Settings") }
    }
}
```

After a login flow, clear it from the stack:

```kotlin body
#! val nav = rememberNavController()
nav.navigate("home") { popUpTo("login") { inclusive = true } }
```

## The current destination

`nav.currentBackStackEntryAsState()` returns the top entry and recomposes when it changes: use it to highlight the selected item of a navigation bar.

```kotlin
@Composable
fun BottomBar(nav: NavController) {
    val entry by nav.currentBackStackEntryAsState()
    val route = entry?.destination?.route
    NavigationBar {
        NavigationBarItem(selected = route == "home", onClick = { nav.navigate("home") { launchSingleTop = true } },
                          icon = { Icon(Icons.Filled.Home, contentDescription = null) }, label = { Text("Home") })
        NavigationBarItem(selected = route == "search", onClick = { nav.navigate("search") { launchSingleTop = true } },
                          icon = { Icon(Icons.Filled.Search, contentDescription = null) }, label = { Text("Search") })
    }
}
```

## State on the back stack

Each entry keeps its `rememberSaveable` state, scroll positions included, while it is on the back stack: going back shows the screen as it was. Each entry is also a `ViewModel` owner: `viewModel<T>()` inside a destination gives that entry's, cleared when the entry is popped for good. See [Architecture](architecture.md).

## Nested graphs

Screens that belong together, such as a sign-in flow, go in a nested graph with a route of its own. Navigating to the graph's route opens its start destination:

```kotlin
#! class LoginViewModel : ViewModel() { val user = MutableStateFlow("") }
@Composable
fun Graphs(nav: NavController) {
    NavHost(navController = nav, startDestination = "home", modifier = Modifier.fillMaxSize()) {
        composable("home") {
            Button(onClick = { nav.navigate("login") }) { Text("Sign in") }
        }
        navigation(startDestination = "username", route = "login") {
            composable("username") { entry ->
                val form = viewModel<LoginViewModel>(entry.parent!!)   // shared by the graph's screens
                val user by form.user.collectAsState()
                Button(onClick = { nav.navigate("password") }) { Text("Next, $user") }
            }
            composable("password") { entry ->
                val form = viewModel<LoginViewModel>(entry.parent!!)   // the same one
                val user by form.user.collectAsState()
                Button(onClick = { nav.navigate("home") { popUpTo("login") { inclusive = true } } }) { Text("Done, $user") }
            }
        }
    }
}
```

The graph has an entry of its own on the back stack, below its screens, so its `ViewModel`s live as long as the flow does.

## Deep links

A destination can answer URIs. When the app is opened with a link, the `NavHost` goes to that destination, with the start destination and the screens of the graphs on the way below it, so that back leads up:

```kotlin body
#! val nav = rememberNavController()
NavHost(navController = nav, startDestination = "home", modifier = Modifier.fillMaxSize()) {
    composable("home") { Text("Home") }
    composable("item/{n}", deepLinks = listOf(navDeepLink { uriPattern = "kompoz://item/{n}" })) { e ->
        Text("Item ${e.arguments?.getInt("n") ?: 0}")
    }
}
```

Where links come from:

- **Android**: a `VIEW` intent. Add an `intent-filter` for the scheme to the manifest, and forward links that arrive while the app is open from `MainActivity.onNewIntent` to `KompozInput.onNewIntent`, as the showcase does.
- **Desktop**: the command line, `./myapp kompoz://item/42`.
- **Web**: the address's `#` part, `app.html#item/42`. The current screen is written there too, so reloading opens the same one.

From inside the app, `nav.navigate("kompoz://item/42")` follows a link onto the top of the stack, and `nav.handleDeepLink(link)` goes there as if the app had been opened with it. Type-safe routes take `navDeepLink<T>("kompoz://profile")`.

## Transitions

`NavHostOptions` sets the animation between destinations, as an `AnimatedContent` transform, one for navigating forward and one for going back:

```kotlin body
#! val nav = rememberNavController()
NavHost(navController = nav, startDestination = "home", modifier = Modifier.fillMaxSize(),
        enterTransition = { slideInHorizontally { it } }, exitTransition = { slideOutHorizontally { -it / 3 } },
        popEnterTransition = { slideInHorizontally { -it / 3 } }, popExitTransition = { slideOutHorizontally { it } }) {
    composable("home") { Text("Home") }
}
```

The default is a cross-fade.

## Back handling

`BackHandler(enabled, onBack)` takes the back button, gesture or Escape while `enabled`; the most recently composed enabled one wins. Use it for things that should close before the screen does, such as a selection mode:

```kotlin
@Composable
fun Selectable() {
    var selecting by remember { mutableStateOf(false) }
    BackHandler(enabled = selecting) { selecting = false }
    Text(if (selecting) "Selecting (back to stop)" else "Long press to select",
         modifier = Modifier.combinedClickable(onClick = {}, onLongClick = { selecting = true }))
}
```

`PredictiveBackHandler(enabled, onBack)` follows Android's predictive back gesture as it happens: its coroutine gets a flow of the gesture's progress, so the UI can move with the finger, and is cancelled if the gesture is called off. Popups, drawers and sheets handle back themselves.

## See also

- [Architecture](architecture.md): `ViewModel`s per destination.
- [Animation](animation.md): content transforms and shared elements.
- [State](state.md): what `rememberSaveable` keeps on the back stack.
