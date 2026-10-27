# Material bileşenleri

Kompoz, Material 3'ü Compose Material 3 1.4'ün yaptığı gibi, Expressive bileşenler dahil uygular: aynı bileşenler, aynı ölçüler ve aynı varsayılanlar. Bu sayfa başlıcalarını gezdiriyor; hepsini durumlarıyla birlikte [API sayfası](api.md) listeliyor.

![Vitrinin Material ekranı, koyu tema](../../screenshots/material-dark.png)

Material bileşenleri renklerini, yazı tiplerini ve hareketlerini `MaterialTheme`'den okur, bu yüzden uygulamanızı bir `MaterialTheme`'e sarın. Bkz. [Tema](theming.md).

## Scaffold ve uygulama çubukları

`Scaffold` bir ekranı yerleştirir: içeriğin çevresinde bir üst çubuk, bir alt çubuk, bir yüzen eylem butonu ve bir snackbar host'u. İçerik, onu çubuklardan ve sistemin kenar boşluklarından uzak tutan iç boşluğu alır.

```cpp
void Inbox() {
    auto snackbars = rememberSnackbarHostState();
    Scaffold(Modifier(),
             {.topBar = [] { TopAppBar({.title = [] { Text("Inbox"); },
                                        .navigationIcon = [] { IconButton([] {}, [] { Icon(Icons::Filled::Menu, "Menu"); }); },
                                        .actions = [] { IconButton([] {}, [] { Icon(Icons::Filled::Search, "Search"); }); }}); },
              .floatingActionButton = [snackbars] {
                  FloatingActionButton([snackbars] { snackbars.showSnackbar("Draft saved"); }, [] { Icon(Icons::Filled::Edit, "Compose"); });
              },
              .snackbarHost = [snackbars] { SnackbarHost(snackbars); }},
             [](PaddingValues padding) {
                 LazyColumn(Modifier().fillMaxSize(), {.contentPadding = padding}, [](LazyListScope& l) {
                     l.items(30, [](int i) { ListItem({.headline = [i] { Text("Message " + std::to_string(i)); }}); });
                 });
             });
}
```

Üst uygulama çubukları Material'ın tanımladığı boyutlarda gelir: `TopAppBar`, `CenterAlignedTopAppBar`, `MediumTopAppBar` ve `LargeTopAppBar`. Bir kaydırma davranışıyla (`TopAppBarDefaults::enterAlwaysScrollBehavior()`, `exitUntilCollapsedScrollBehavior()`, `pinnedScrollBehavior()`) ve bu davranışın `nestedScrollConnection`'ı `Scaffold`'a verildiğinde, içerik kaydıkça daralır ve renk değiştirirler (bkz. [Listeler ve ızgaralar](lists.md#iç-içe-kaydırma)). `BottomAppBar` ve `BottomSheetScaffold` da var.

İçeriğin iç boşluğu, çubukların boyu değiştikçe (liste kaydıkça daralan bir üst çubuk) içerik yeniden oluşturulmadan onları izler: Compose'taki gibi `Modifier::padding`, `consumeWindowInsets` ve listelerin, pager'ların, carousel'lerin `contentPadding`'i onu yerleşim sırasında okur. Bir kenarı kendiniz okumak için `calculateTopPadding()` (ve `calculateStartPadding()`, `calculateEndPadding()`, `calculateBottomPadding()`) çağırın. Bir composable'da okunursa iç boşluk değiştiğinde o composable yeniden oluşturulur; bir yerleşimde okunursa o yerleşim yeniden yerleştirilir. Alanlar (`padding.top`) iç boşluğun içerik oluşturulduğu andaki değerini tutar.

## Gezinme

| Bileşen | Kullanım |
|---|---|
| `NavigationBar` + `NavigationBarItem` | Telefon ekranının altında üç ile beş arası hedef. |
| `ShortNavigationBar` | M3 Expressive'in daha kısa çubuğu. |
| `NavigationRail`, `WideNavigationRail` | Daha geniş bir pencerenin yanında hedefler. |
| `ModalNavigationDrawer`, `DismissibleNavigationDrawer`, `PermanentNavigationDrawer` | Hedeflerden oluşan bir çekmece. |
| `NavigationSuiteScaffold` | Pencerenin boyutuna göre bir çubuk, bir ray ya da bir çekmece seçer. |
| `PrimaryTabRow`, `SecondaryTabRow`, `PrimaryScrollableTabRow` + `Tab` | Sekmeler. |

Çoğu zaman istediğiniz `NavigationSuiteScaffold`'dur: Compose'un adaptive navigation suite'i gibi telefonda bir çubuk, tablet ya da masaüstü penceresinde bir ray gösterir.

```cpp
void App() {
    auto tab = rememberSaveable([] { return mutableStateOf(0); });
    NavigationSuiteScaffold([&](NavigationSuiteScope& nav) {
        nav.item(tab.get() == 0, [tab] { tab.set(0); }, Icons::Filled::Home, "Home");
        nav.item(tab.get() == 1, [tab] { tab.set(1); }, Icons::Filled::Search, "Search");
        nav.item(tab.get() == 2, [tab] { tab.set(2); }, Icons::Filled::Settings, "Settings");
    }, Modifier(), {}, [&] {
        Text("Tab " + std::to_string(tab.get()));
    });
}
```

Ekranlar arasında bir geri yığınıyla geçiş için bkz. [Gezinme](navigation.md).

## Butonlar

| Bileşen | Vurgu |
|---|---|
| `Button` | Dolgulu: ana eylem. |
| `FilledTonalButton` | Tonal: ana eylem olmayan önemli bir eylem. |
| `ElevatedButton`, `OutlinedButton` | Orta vurgu. |
| `TextButton` | Düşük vurgu, diyaloglarda ve kartlarda. |
| `IconButton`, `FilledIconButton`, `OutlinedIconButton`, `IconToggleButton`… | Tek başına bir ikon. |
| `FloatingActionButton`, `SmallFloatingActionButton`, `LargeFloatingActionButton`, `ExtendedFloatingActionButton` | Ekranın birincil eylemi. |
| `SingleChoiceSegmentedButtonRow` + `SegmentedButton` | Birkaç seçenek arasından bir seçim. |
| `ButtonGroup`, `SplitButtonLayout`, `ToggleButton` | M3 Expressive'in grupları, bölünmüş butonları ve açma/kapama butonları. |

```cpp body
Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
    Button([] {}, [] { Text("Save"); });
    OutlinedButton([] {}, [] { Text("Cancel"); });
    IconButton([] {}, [] { Icon(Icons::Filled::Favorite, "Like"); });
});
```

## Kartlar ve listeler

`Card`, `ElevatedCard` ve `OutlinedCard` ilgili içeriği gruplar; her birinin, ilk argüman olarak `onClick` alan tıklanabilir bir biçimi de vardır. `ListItem`, isteğe bağlı baş ve son içerikli bir, iki ya da üç satırlık bir satırdır:

```cpp body
OutlinedCard(Modifier().fillMaxWidth(), [&] {
    ListItem({.headline = [] { Text("Wi-Fi"); },
              .supporting = [] { Text("Connected"); },
              .leading = [] { Icon(Icons::Filled::Settings, ""); },
              .trailing = [] { Switch(true, [](bool) {}); }});
});
```

## Seçim kontrolleri

`Checkbox`, `TriStateCheckbox`, `RadioButton`, `Switch`, `Slider` ve `RangeSlider`. Durumsuzdurlar: verdiğiniz değeri gösterir ve değişiklikleri bir geri çağrıyla bildirirler.

```cpp
void Volume() {
    auto volume = remember([] { return mutableStateOf(0.5f); });
    auto muted = remember([] { return mutableStateOf(false); });
    Column([&] {
        Slider(volume.get(), [volume](float v) { volume.set(v); });
        Row(Modifier().toggleable(muted.get(), [muted](bool v) { muted.set(v); }, SemanticsRole::Checkbox),
            {.verticalAlignment = Alignment::CenterVertically}, [&] {
                Checkbox(muted.get(), {});   // onu bütün satır açıp kapatır
                Text("Mute");
            });
    });
}
```

Tıklamayı kendisi işleyen bir satırın içindeki kontrole, yukarıdaki gibi boş bir geri çağrı verin; böylece satır tek bir dokunma hedefi ve erişilebilirlik için tek bir öğe olur.

## Chip'ler

`AssistChip`, `FilterChip`, `InputChip` ve `SuggestionChip`:

```cpp body
#! auto selected = mutableStateOf(false);
FilterChip(selected.get(), [selected] { selected.set(!selected.get()); }, [] { Text("Unread"); });
```

## Metin alanları

Etiketli, yer tutuculu, ikonlu, önekli, sonekli, yardımcı metinli ve hatalı `TextField` ve `OutlinedTextField`; arama için `SearchBar` ve `DockedSearchBar`. Bkz. [Metin](text.md#metin-alanları).

## Diyaloglar, alt sayfalar ve menüler

Bunlar arayüzün geri kalanının üstünde görünür. Her biri çağırdığınız yerde kompoze edilir, böylece durumunu ve composition local'larını korur; Kompoz da onu en üste yerleştirir. Birini, bir durum öyle söylediği sürece onu çağırarak gösterin:

```cpp
void DeleteButton(std::function<void()> onDelete) {
    auto open = remember([] { return mutableStateOf(false); });
    TextButton([open] { open.set(true); }, [] { Text("Delete"); });
    if (open.get()) {
        AlertDialog([open] { open.set(false); },
                    {.confirmButton = [open, onDelete] { TextButton([open, onDelete] { open.set(false); onDelete(); }, [] { Text("Delete"); }); },
                     .dismissButton = [open] { TextButton([open] { open.set(false); }, [] { Text("Cancel"); }); },
                     .title = [] { Text("Delete this message?"); },
                     .text = [] { Text("It will be gone for good."); }});
    }
}
```

| Bileşen | Kullanım |
|---|---|
| `AlertDialog`, `BasicAlertDialog`, `Dialog` | Bir karar ister. |
| `ModalBottomSheet` | Alttan kayarak açılan, aşağı sürüklenerek kapatılan içerik. |
| `DropdownMenu` + `DropdownMenuItem` | Onu açan öğeye bağlı bir menü. |
| `ExposedDropdownMenuBox` | Seçeneklerden oluşan bir menüsü olan metin alanı. |
| `DatePicker`, `DateRangePicker`, `TimePicker`, `TimeInput` | Tek başına ya da `DatePickerDialog` / `TimePickerDialog` içinde tarihler ve saatler. |
| `TooltipBox` + `PlainTooltip` / `RichTooltip` | Uzun basınca ya da üzerine gelince bir ipucu. |

## Snackbar'lar

Bir `SnackbarHost`, bir `SnackbarHostState`'in mesajlarını gösterir. Bir geri çağrıdan `showSnackbar` ile bir tane gösterin ya da bir coroutine içinde sonucunu bekleyin:

```cpp body
#! std::function<void()> undo;
auto snackbars = rememberSnackbarHostState();
auto scope = rememberCoroutineScope();
Button([scope, snackbars, undo] {
    scope.launch([snackbars, undo]() -> Task {
        if (co_await snackbars.showSnackbarAsync("Message deleted", "Undo") == SnackbarResult::ActionPerformed) undo();
    });
}, [] { Text("Delete"); });
SnackbarHost(snackbars);
```

## İlerleme

İlerleme değeriyle ya da değersiz (belirsiz) `CircularProgressIndicator` ve `LinearProgressIndicator`; M3 Expressive'in `LoadingIndicator`, `ContainedLoadingIndicator` ve dalgalı göstergeleri. Belirsiz göstergeler çizim aşamasında canlanır, bu yüzden hiçbir şeyi yeniden kompoze etmezler.

```cpp body
#! float progress = 0.4f;
CircularProgressIndicator();               // belirsiz
LinearProgressIndicator(progress);          // %40
```

## Diğer bileşenler

Rozetler (`BadgedBox`, `Badge`), ayırıcılar (`HorizontalDivider`, `VerticalDivider`), carousel'ler (bkz. [Listeler ve ızgaralar](lists.md#carouseller)), çekip yenileme, kaydırarak kapatma (`SwipeToDismissBox`), yüzen araç çubukları ve daha fazlası. Tam listesi [API sayfasında](api.md).

## Ayrıca bakın

- [Tema](theming.md): renkler, tipografi ve koyu tema.
- [Gezinme](navigation.md): ekranlar ve geri yığını.
- [Metin](text.md): metin alanları ayrıntısıyla.
