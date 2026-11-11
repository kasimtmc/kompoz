# Gezinme

`androidx.navigation.compose`'un yaptığı gibi, bir geri yığınıyla ekranlar arasında gezinmek: bir `NavController` yığını tutar, bir `NavHost` en üstteki girdiyi gösterir ve girdiler arasında animasyon yapar.

## Bir NavHost

```kotlin
@Kompozable
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

- `rememberNavController()` controller'ı oluşturur. Kaydedilebilirdir: süreç sonlandırıldıktan sonra geri yığını geri gelir.
- `NavHost(nav, startDestination, modifier, builder)` hedefleri tanımlar ve güncel olanı gösterir.
- `graph.composable(route, content)` bir hedef ekler. İçeriği `NavBackStackEntry`'yi alır.
- `nav.navigate(path)` bir hedefi yığına iter; `nav.popBackStack()` geri gider.

Sistemin geri butonu ve hareketi (masaüstünde Escape de) geri gidilecek bir şey olduğu sürece yığından çıkarır.

## Rotalar ve argümanlar

Bir rota, `{name}` parçaları ve isteğe bağlı sorgu parametreleri olan bir yoldur: `"detail/{id}?tab={tab}"`. Değerleri doldurarak gezinin (`"detail/42?tab=2"`) ve onları girdinin argümanlarından `getString`, `getInt`, `getLong`, `getFloat` ve `getBool` ile okuyun; her biri bir `std::optional` döndürür.

### Tür güvenli rotalar

Compose'daki gibi, `@Serializable` bir data class (ya da object) bir rotadır. Zorunlu alanları yol parçaları, null olabilen alanları sorgu parametreleridir:

```kotlin
@Serializable
data class Profile(val id: String, val tab: Int? = null)   // "Profile/{id}?tab={tab}" rotası

@Serializable
object Home

@Kompozable
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

Alanlar string, sayı, bool, enum ve bunların null olabilenleri olabilir. `nav.getBackStackEntry<T>()`, `nav.popBackStack<T>(inclusive)` ve `entry.hasRoute<T>()` de türlerle çalışır.

## Gezinme seçenekleri

`navigate`, `NavOptions` alır:

| Seçenek | Ne yapar |
|---|---|
| `popUpTo` | Önce yığını bu rotaya kadar boşaltır… |
| `inclusive` | …ve o girdiyi de çıkarır. |
| `launchSingleTop` | Hedef zaten en üstteyse ikinci bir kopya açmaz. |
| `saveState` | Çıkarılanların durumunu saklar… |
| `restoreState` | …ve ona yeniden gezinildiğinde geri getirir. |

Her sekmenin yığınını ve kaydırma konumunu koruyarak sekmeler arasında geçiş yapan alışılmış alt gezinme deseni:

```kotlin body
#! val nav = rememberNavController()
#! val tab = "search"
nav.navigate(tab) {
    popUpTo("home") { saveState = true }
    launchSingleTop = true
    restoreState = true
}
```

Hangi sekmenin gösterildiği hedefin `hierarchy`'sidir (hedef, sonra içinde bulunduğu grafikler): bir sekmenin iç içe grafiği bütün ekranlarıyla sayılır. `navOptions { }` seçenekleri bir kez, `navigate(route, options)` için kurar:

```kotlin
@Serializable
object HomeTab

@Serializable
object SettingsTab

@Kompozable
fun Tabs(nav: NavController) {
    val entry by nav.currentBackStackEntryAsState()
    val inSettings = entry?.destination?.hierarchy?.any { it.hasRoute<SettingsTab>() } == true
    val tabs = navOptions {
        popUpTo<HomeTab> { saveState = true }
        launchSingleTop = true
        restoreState = true
    }
    Row {
        TextButton(onClick = { nav.navigate(HomeTab, tabs) }, enabled = inSettings) { Text("Ana sayfa") }
        TextButton(onClick = { nav.navigate(SettingsTab, tabs) }, enabled = !inSettings) { Text("Ayarlar") }
    }
}
```

Bir giriş akışından sonra onu yığından temizleyin:

```kotlin body
#! val nav = rememberNavController()
nav.navigate("home") { popUpTo("login") { inclusive = true } }
```

## Güncel hedef

`nav.currentBackStackEntryAsState()` en üstteki girdiyi döndürür ve o değiştiğinde yeniden kompoze eder: bir gezinme çubuğunun seçili öğesini vurgulamak için kullanın.

```kotlin
@Kompozable
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

## Geri yığınındaki durum

Her girdi, geri yığınında durduğu sürece kaydırma konumları dahil `rememberSaveable` durumunu korur: geri gitmek ekranı bıraktığınız gibi gösterir. Her girdi aynı zamanda bir `ViewModel` sahibidir: bir hedefin içindeki `viewModel<T>()` o girdininkini verir ve girdi kalıcı olarak yığından çıkarıldığında temizlenir. Bkz. [Mimari](architecture.md).

## İç içe grafikler

Birbirine ait ekranlar, örneğin bir oturum açma akışı, kendi rotası olan iç içe bir grafiğe konur. Grafiğin rotasına gezinmek onun başlangıç hedefini açar:

```kotlin
#! class LoginViewModel : ViewModel() { val user = MutableStateFlow("") }
@Kompozable
fun Graphs(nav: NavController) {
    NavHost(navController = nav, startDestination = "home", modifier = Modifier.fillMaxSize()) {
        composable("home") {
            Button(onClick = { nav.navigate("login") }) { Text("Sign in") }
        }
        navigation(startDestination = "username", route = "login") {
            composable("username") { entry ->
                val form = viewModel<LoginViewModel>(entry.parent!!)   // grafiğin ekranlarınca paylaşılır
                val user by form.user.collectAsState()
                Button(onClick = { nav.navigate("password") }) { Text("Next, $user") }
            }
            composable("password") { entry ->
                val form = viewModel<LoginViewModel>(entry.parent!!)   // aynısı
                val user by form.user.collectAsState()
                Button(onClick = { nav.navigate("home") { popUpTo("login") { inclusive = true } } }) { Text("Done, $user") }
            }
        }
    }
}
```

Grafiğin geri yığınında, ekranlarının altında kendine ait bir girdisi vardır; böylece onun `ViewModel`'ları akış sürdüğü kadar yaşar.

## Derin bağlantılar

Bir hedef URI'lara yanıt verebilir. Uygulama bir bağlantıyla açıldığında `NavHost` o hedefe gider; başlangıç hedefini ve yol üzerindeki grafiklerin ekranlarını onun altına koyar ki geri tuşu yukarı götürsün:

```kotlin body
#! val nav = rememberNavController()
NavHost(navController = nav, startDestination = "home", modifier = Modifier.fillMaxSize()) {
    composable("home") { Text("Home") }
    composable("item/{n}", deepLinks = listOf(navDeepLink { uriPattern = "kompoz://item/{n}" })) { e ->
        Text("Item ${e.arguments?.getInt("n") ?: 0}")
    }
}
```

Bağlantıların geldiği yerler:

- **Android**: bir `VIEW` intent'i. Manifest'e şema için bir `intent-filter` ekleyin ve uygulama açıkken gelen bağlantıları, vitrinin yaptığı gibi `MainActivity.onNewIntent`'ten `KompozInput.onNewIntent`'e iletin.
- **Masaüstü**: komut satırı, `./myapp kompoz://item/42`.
- **Web**: adresin `#` kısmı, `app.html#item/42`. Güncel ekran da oraya yazılır; böylece sayfayı yenilemek aynı ekranı açar.

Uygulamanın içinden `nav.navigate("kompoz://item/42")` bir bağlantıyı yığının en üstüne açar; `nav.handleDeepLink(link)` ise uygulama o bağlantıyla açılmış gibi oraya gider. Tür güvenli rotalar `navDeepLink<T>("kompoz://profile")` alır.

## Geçişler

`NavHostOptions`, hedefler arasındaki animasyonu bir `AnimatedContent` dönüşümü olarak belirler: biri ileri gezinmek, biri geri gitmek için:

```kotlin body
#! val nav = rememberNavController()
NavHost(navController = nav, startDestination = "home", modifier = Modifier.fillMaxSize(),
        enterTransition = { slideInHorizontally { it } }, exitTransition = { slideOutHorizontally { -it / 3 } },
        popEnterTransition = { slideInHorizontally { -it / 3 } }, popExitTransition = { slideOutHorizontally { it } }) {
    composable("home") { Text("Home") }
}
```

Varsayılan, bir çapraz geçiştir (cross-fade).

## Geri tuşunu ele almak

`BackHandler(enabled, onBack)`, `enabled` olduğu sürece geri butonunu, hareketini ya da Escape'i üstlenir; en son kompoze edilen etkin olan kazanır. Ekrandan önce kapanması gereken şeyler için kullanın, örneğin bir seçim modu:

```kotlin
@Kompozable
fun Selectable() {
    var selecting by remember { mutableStateOf(false) }
    BackHandler(enabled = selecting) { selecting = false }
    Text(if (selecting) "Selecting (back to stop)" else "Long press to select",
         modifier = Modifier.combinedClickable(onClick = {}, onLongClick = { selecting = true }))
}
```

`PredictiveBackHandler(enabled, onBack)`, Android'in tahmine dayalı geri hareketini gerçekleştiği anda izler: coroutine'i hareketin ilerleyişini veren bir Flow alır, böylece arayüz parmakla birlikte hareket edebilir; hareketten vazgeçilirse coroutine iptal edilir. Popup'lar, çekmeceler ve alt sayfalar geri tuşunu kendileri ele alır.

## Ayrıca bakın

- [Mimari](architecture.md): hedef başına `ViewModel`'lar.
- [Animasyon](animation.md): içerik dönüşümleri ve paylaşılan öğeler.
- [Durum](state.md): `rememberSaveable`'ın geri yığınında neyi sakladığı.
