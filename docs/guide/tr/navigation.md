# Gezinme

`androidx.navigation.compose`'un yaptığı gibi, bir geri yığınıyla ekranlar arasında gezinmek: bir `NavController` yığını tutar, bir `NavHost` en üstteki girdiyi gösterir ve girdiler arasında animasyon yapar.

## Bir NavHost

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

- `rememberNavController()` controller'ı oluşturur. Kaydedilebilirdir: süreç sonlandırıldıktan sonra geri yığını geri gelir.
- `NavHost(nav, startDestination, modifier, builder)` hedefleri tanımlar ve güncel olanı gösterir.
- `graph.composable(route, content)` bir hedef ekler. İçeriği `NavBackStackEntry`'yi alır.
- `nav.navigate(path)` bir hedefi yığına iter; `nav.popBackStack()` geri gider.

Sistemin geri butonu ve hareketi (masaüstünde Escape de) geri gidilecek bir şey olduğu sürece yığından çıkarır.

## Rotalar ve argümanlar

Bir rota, `{name}` parçaları ve isteğe bağlı sorgu parametreleri olan bir yoldur: `"detail/{id}?tab={tab}"`. Değerleri doldurarak gezinin (`"detail/42?tab=2"`) ve onları girdinin argümanlarından `getString`, `getInt`, `getLong`, `getFloat` ve `getBool` ile okuyun; her biri bir `std::optional` döndürür.

### Tür güvenli rotalar

Compose'un `@Serializable` rotaları gibi, bir struct da rota olabilir. Zorunlu alanları yol parçaları, `std::optional` alanları sorgu parametreleridir:

```cpp
struct Profile {
    std::string id;
    std::optional<int> tab;
    KOMPOZ_ROUTE(Profile, id, tab)   // "Profile/{id}?tab={tab}" rotası
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

Alanlar string, sayı, bool, enum ve bunların `std::optional`'ları olabilir. `nav.getBackStackEntry<T>()`, `nav.popBackStack<T>(inclusive)` ve `entry.hasRoute<T>()` de türlerle çalışır.

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

```cpp body
#! NavController nav;
#! std::string tab;
nav.navigate(tab, {.popUpTo = "home", .saveState = true, .launchSingleTop = true, .restoreState = true});
```

Bir giriş akışından sonra onu yığından temizleyin:

```cpp body
#! NavController nav;
nav.navigate("home", {.popUpTo = "login", .inclusive = true});
```

## Güncel hedef

`nav.currentBackStackEntryAsState()` en üstteki girdiyi döndürür ve o değiştiğinde yeniden kompoze eder: bir gezinme çubuğunun seçili öğesini vurgulamak için kullanın.

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

## Geri yığınındaki durum

Her girdi, geri yığınında durduğu sürece kaydırma konumları dahil `rememberSaveable` durumunu korur: geri gitmek ekranı bıraktığınız gibi gösterir. Her girdi aynı zamanda bir `ViewModel` sahibidir: bir hedefin içindeki `viewModel<T>()` o girdininkini verir ve girdi kalıcı olarak yığından çıkarıldığında temizlenir. Bkz. [Mimari](architecture.md).

## İç içe grafikler

Birbirine ait ekranlar, örneğin bir oturum açma akışı, kendi rotası olan iç içe bir grafiğe konur. Grafiğin rotasına gezinmek onun başlangıç hedefini açar:

```cpp
#! class LoginViewModel : public ViewModel { public: MutableStateFlow<std::string> user{std::string()}; };
void Graphs(NavController nav) {
    NavHost(nav, "home", Modifier().fillMaxSize(), [&](NavGraphBuilder& graph) {
        graph.composable("home", [nav](const NavBackStackEntry&) {
            Button([nav] { nav.navigate("login"); }, [] { Text("Sign in"); });
        });
        graph.navigation("username", "login", [&](NavGraphBuilder& login) {
            login.composable("username", [nav](const NavBackStackEntry& e) {
                auto form = viewModel<LoginViewModel>(*e.parent());   // grafiğin ekranlarınca paylaşılır
                Button([nav] { nav.navigate("password"); }, [] { Text("Next"); });
            });
            login.composable("password", [nav](const NavBackStackEntry& e) {
                auto form = viewModel<LoginViewModel>(*e.parent());   // aynısı
                Button([nav] { nav.navigate("home", {.popUpTo = "login", .inclusive = true}); }, [] { Text("Done"); });
            });
        });
    });
}
```

Grafiğin geri yığınında, ekranlarının altında kendine ait bir girdisi vardır; böylece onun `ViewModel`'ları akış sürdüğü kadar yaşar.

## Derin bağlantılar

Bir hedef URI'lara yanıt verebilir. Uygulama bir bağlantıyla açıldığında `NavHost` o hedefe gider; başlangıç hedefini ve yol üzerindeki grafiklerin ekranlarını onun altına koyar ki geri tuşu yukarı götürsün:

```cpp body
#! NavController nav;
NavHost(nav, "home", Modifier().fillMaxSize(), [&](NavGraphBuilder& graph) {
    graph.composable("home", [](const NavBackStackEntry&) { Text("Home"); });
    graph.composable("item/{n}", {navDeepLink("kompoz://item/{n}")}, [](const NavBackStackEntry& e) {
        Text("Item " + std::to_string(e.arguments().getInt("n").value_or(0)));
    });
});
```

Bağlantıların geldiği yerler:

- **Android**: bir `VIEW` intent'i. Manifest'e şema için bir `intent-filter` ekleyin ve uygulama açıkken gelen bağlantıları, vitrinin yaptığı gibi `MainActivity.onNewIntent`'ten `KompozInput.onNewIntent`'e iletin.
- **Masaüstü**: komut satırı, `./myapp kompoz://item/42`.
- **Web**: adresin `#` kısmı, `app.html#item/42`. Güncel ekran da oraya yazılır; böylece sayfayı yenilemek aynı ekranı açar.

Uygulamanın içinden `nav.navigate("kompoz://item/42")` bir bağlantıyı yığının en üstüne açar; `nav.handleDeepLink(link)` ise uygulama o bağlantıyla açılmış gibi oraya gider. Tür güvenli rotalar `navDeepLink<T>("kompoz://profile")` alır.

## Geçişler

`NavHostOptions`, hedefler arasındaki animasyonu bir `AnimatedContent` dönüşümü olarak belirler: biri ileri gezinmek, biri geri gitmek için:

```cpp body
#! NavController nav;
NavHost(nav, "home", Modifier().fillMaxSize(), [&](NavGraphBuilder& graph) {
    graph.composable("home", [](const NavBackStackEntry&) { Text("Home"); });
}, {.transition = togetherWith(slideInHorizontally(1), slideOutHorizontally(-0.3f)),
    .popTransition = togetherWith(slideInHorizontally(-0.3f), slideOutHorizontally(1))});
```

Varsayılan, bir çapraz geçiştir (cross-fade).

## Geri tuşunu ele almak

`BackHandler(enabled, onBack)`, `enabled` olduğu sürece geri butonunu, hareketini ya da Escape'i üstlenir; en son kompoze edilen etkin olan kazanır. Ekrandan önce kapanması gereken şeyler için kullanın, örneğin bir seçim modu:

```cpp
void Selectable() {
    auto selecting = remember([] { return mutableStateOf(false); });
    BackHandler(selecting.get(), [selecting] { selecting.set(false); });
    Text(selecting.get() ? "Selecting (back to stop)" : "Long press to select",
         Modifier().combinedClickable([] {}, [selecting] { selecting.set(true); }));
}
```

`PredictiveBackHandler(enabled, onBack)`, Android'in tahmine dayalı geri hareketini gerçekleştiği anda izler: coroutine'i hareketin ilerleyişini veren bir Flow alır, böylece arayüz parmakla birlikte hareket edebilir; hareketten vazgeçilirse coroutine iptal edilir. Popup'lar, çekmeceler ve alt sayfalar geri tuşunu kendileri ele alır.

## Ayrıca bakın

- [Mimari](architecture.md): hedef başına `ViewModel`'lar.
- [Animasyon](animation.md): içerik dönüşümleri ve paylaşılan öğeler.
- [Durum](state.md): `rememberSaveable`'ın geri yığınında neyi sakladığı.
