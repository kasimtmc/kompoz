# Compose'dan gelenler için

Jetpack Compose'u biliyorsanız Kompoz'u zaten biliyorsunuz: aynı model, çoğunlukla aynı isimler. Bu sayfa Kotlin'deki yapıların C++'a nasıl karşılık geldiğini gösteriyor.

Kompoz, Material 3'ün ölçülerine ve Android'in fling eğrisine kadar Compose'un davranışını yakından izler. Değişen dildir: C++'ta compiler plugin, isimli argümanlar ve parantez dışında sondaki lambdalar yok; bu yüzden her birinin bir C++ biçimi var. Aşağıdaki birkaç kalıbı öğrendikten sonra Compose'un belgeleri neredeyse kelimesi kelimesine geçerli olur.

## Composable fonksiyonlar

Kotlin'de `@Composable` ve Compose compiler plugin'i her çağrıya kimliğini verir. C++'ta bir composable sıradan bir fonksiyondur. Onu kompozisyon içinden çağırmak yeterlidir:

```kotlin
@Composable
fun Greeting(name: String) {
    Text("Hello, $name!")
}
```

```cpp
void Greeting(const std::string& name) {
    Text("Hello, " + name + "!");
}
```

Böyle sıradan bir fonksiyonun kendine ait bir scope'u yoktur: okuduğu durum onu hangi composable çağırdıysa ona aittir ve durum değişince o composable yeniden çalışır. Küçük yardımcıların çoğu için bu yeterlidir.

Bir Kotlin composable'ı gibi kendi başına yeniden kompoze olması ve argümanları değişmediğinde atlanması gereken bir bileşen için, gövdesini `composable` ile yazın ve bir `CallSite` alın:

```cpp
void Greeting(const std::string& name, CallSite site = CallSite::current()) {
    composable(site, name, [=] {
        Text("Hello, " + name + "!");
    });
}
```

- `CallSite`, `std::source_location`'dır. Varsayılan argümanı `Greeting`'in nereden çağrıldığını kaydeder; çağrının ebeveyni içindeki sırasıyla birlikte bu, çağrının kimliğidir; Kotlin'de compiler plugin'in hesapladığı da budur.
- Lambdadan önceki argümanlar atlamaya karar verir: geçen seferkilere eşit (`==`) olduklarında ve gövdenin okuduğu hiçbir şey değişmediğinde gövde çalışmaz.
- Gövde değerle yakalar (`[=]`), çünkü daha sonra, çağıranı olmadan, kendi başına yeniden çalışabilir.

`Column` ve `Card` gibi içerik lambdası alan composable'lar, Compose'un *inline* composable'larıdır: scope'suz bir kimlik için `ComposableScope scope(site);` kullanırlar ve içeriklerinde okunan durum çağırana aittir. [Yaşam döngüsü ve kimlik](lifecycle.md) sayfasına bakın.

## Parametreler

Kotlin'in isimli ve varsayılan parametreleri, C++'ta designated initializer'larla doldurulan *seçenek struct'larına* dönüşür:

```kotlin
Text("Title", fontSize = 24.sp, maxLines = 1)
OutlinedTextField(value, onValueChange, label = { Text("Email") }, singleLine = true)
```

```cpp body
#! std::string value;
#! std::function<void(std::string)> onValueChange;
Text("Title", Modifier(), {.fontSize = 24, .maxLines = 1});
OutlinedTextField(value, onValueChange, Modifier(), {.singleLine = true, .label = "Email"});
```

Designated initializer'lar, alanların struct'ta tanımlandığı sırayı izlemek zorundadır. Başlık dosyaları onları mantıklı bir sırayla listeler, karıştırdığınızda da derleyiciniz söyler.

Bileşenlerin çoğu şu sırayla alır: zorunlu argümanlar, `Modifier`, seçenek struct'ı, sonra içerik lambdası. Overload'lar ihtiyacınız olmayanı dışarıda bırakır: hem `Button(onClick, content)` hem `Button(onClick, modifier, style, content)` vardır.

## Sondaki lambdalar

Kotlin'in sondaki lambdası, son argüman olarak parantezin içine girer:

```kotlin
Column(Modifier.padding(16.dp)) {
    Text("One")
    Text("Two")
}
```

```cpp body
Column(Modifier().padding(16), [&] {
    Text("One");
    Text("Two");
});
```

İçerik lambdaları çağrı sırasında hemen çalışır, bu yüzden onlar için `[&]` güvenlidir. `onClick` gibi callback'ler daha sonra, kompozisyondan sonra, onları oluşturan fonksiyon dönmüşken çalışır: kullandıklarını **değerle** yakalayın.

```cpp
void LikeButton() {
    auto likes = remember([] { return mutableStateOf(0); });
    Button([likes] { likes.set(likes.get() + 1); }, [&] {   // callback: değerle
        Text(std::to_string(likes.get()) + " likes");        // içerik: [&] sorun değil
    });
}
```

`LazyColumn`'unki gibi bir scope alan lambdalar onu receiver olarak değil, parametre olarak alır: `LazyColumn(modifier, [&](LazyListScope& list) { list.items(...); })`.

## Durum

```kotlin
var count by remember { mutableStateOf(0) }
count++
```

```cpp body
auto count = remember([] { return mutableStateOf(0); });
count.set(count.get() + 1);
```

C++'ta property delegate'leri yoktur, bu yüzden bir durum `get()` ile okunur ve `set()` ile yazılır. `MutableState<T>` bir handle'dır: kopyalar aynı değeri paylaşır; callback'lerin onu değerle yakalayabilmesi bundandır. `update` değeri yerinde değiştirir:

```cpp body
auto todos = remember([] { return mutableStateOf(std::vector<std::string>{}); });
todos.update([](std::vector<std::string>& list) { list.push_back("Buy milk"); });
```

`remember(key, init)`, `rememberSaveable`, `derivedStateOf`, `mutableStateListOf`, `rememberUpdatedState` ve `produceState` Compose'daki isimlerini taşır. [Durum](state.md) sayfasına bakın.

## Modifier'lar

```kotlin
Modifier.fillMaxWidth().padding(16.dp).background(Color.Red, RoundedCornerShape(8.dp))
```

```cpp body
Box(Modifier().fillMaxWidth().padding(16).background(Color::hex(0xE53935), RoundedCornerShape(8)));
```

Zincir `Modifier()`'dan başlar. Sıra Compose'daki gibi önemlidir. [Modifier'lar](modifiers.md) sayfasına bakın.

## Birimler

Kotlin'deki `16.dp` yalnızca `16`'dır: her uzunluk dp cinsinden bir `float`'tur. Yazı boyutları `TextUnit`'tir; bir sp sayısından dönüşür (`.fontSize = 24`) ya da `TextUnit::sp(24)` ve `TextUnit::em(1.5f)` olarak yazılır. Süreler `std::chrono` kullanır: `delay(300ms)`, `debounce(300ms)`. Bir `Color` 0 ile 1 arasında float'lar tutar (`Color{1, 0, 0, 1}`) ya da onaltılık bir değerden gelir (`Color::hex(0xE53935)`, `Color::hex(0x000000, 0.5f)`); `colors::Black`, `White` ve `Transparent` isimlidir, `color.copy(0.38f)` de Compose'un `copy(alpha = …)`'su gibi alfayı değiştirir.

## Coroutine'ler

Kotlin'in coroutine'leri, `Task` döndüren C++20 coroutine'lerine dönüşür. Compose'dakiler gibi UI thread'inde, kare saatine göre çalışırlar.

```kotlin
LaunchedEffect(userId) {
    delay(1000)
    user = repository.load(userId)
}
val scope = rememberCoroutineScope()
Button(onClick = { scope.launch { listState.animateScrollToItem(0) } }) { Text("Top") }
```

```cpp body
#! int userId = 0;
using namespace std::chrono_literals;
auto user = remember([] { return mutableStateOf(std::string()); });
LaunchedEffect(userId, [user, userId]() -> Task {
    co_await delay(1000ms);
    user.set("user " + std::to_string(userId));
});
auto list = rememberLazyListState();
auto scope = rememberCoroutineScope();
Button([scope, list] { scope.launch([list]() -> Task { co_await list.animateScrollToItem(0); }); },
       [] { Text("Top"); });
```

`suspend` fonksiyonlar, `co_await` ettiğiniz coroutine'lerdir. `Flow`, `StateFlow` ve `SharedFlow` operatörleriyle birlikte vardır (`map`, `filter`, `debounce`, `flatMapLatest`, `stateIn`…) ve `collectAsState()` kompozisyonda birini okur. [Yan etkiler](side-effects.md) ve [Mimari](architecture.md) sayfalarına bakın.

## Composition local'ler

```kotlin
val color = LocalContentColor.current
CompositionLocalProvider(LocalContentColor provides Color.Red) { … }
```

```cpp body
Color color = LocalContentColor.current();
CompositionLocalProvider({LocalContentColor.provides(Color::hex(0xE53935))}, [&] { Text("Red"); });
```

`MaterialTheme.colorScheme`, `MaterialTheme.typography` ve `MaterialTheme.motionScheme`, `colorScheme()`, `typography()` ve `motionScheme()`'dır. Tema genelinde bir `Shapes` yoktur: bileşenlerde Material'ın şekilleri yerleşiktir; bir bileşen şekil alıyorsa kendi şeklinizi verirsiniz (`RoundedCornerShape(12)`, `CircleShape`). [CompositionLocal](composition-local.md) sayfasına bakın.

## Hızlı başvuru

| Compose | Kompoz |
|---|---|
| `@Composable fun F()` | `void F()`, ya da `composable(site, args…, [=] { … })` ile `void F(CallSite site = CallSite::current())` |
| `remember { x }`, `remember(key) { x }` | `remember([] { return x; })`, `remember(key, [] { return x; })` |
| `var v by mutableStateOf(0)` | `auto v = mutableStateOf(0);` `v.get()`, `v.set(1)` |
| `key(id) { … }` | `key(id, [&] { … })` |
| `Modifier.padding(8.dp)` | `Modifier().padding(8)` |
| İsimli argümanlar | Designated initializer'lar: `{.maxLines = 1}` |
| `LaunchedEffect(k) { … }` | `LaunchedEffect(k, [=]() -> Task { … })` |
| `scope.launch { … }` | `scope.launch([=]() -> Task { … })` |
| `DisposableEffect(k) { onDispose { … } }` | `DisposableEffect(k, [=] { return [=] { … }; })` |
| `LocalX.current` | `LocalX.current()` |
| `MaterialTheme.colorScheme.primary` | `colorScheme().primary` |
| `LazyColumn { items(list) { … } }` | `LazyColumn(Modifier(), [&](LazyListScope& l) { l.items(list, [](const T& x) { … }); })` |
| `val vm: MyViewModel = viewModel()` | `auto vm = viewModel<MyViewModel>();` |
| `@Preview` | `KOMPOZ_PREVIEW(Name) { … }` |
| `createComposeRule()` | `kompoz::testing::ComposeTestRule` |

README'deki daha uzun tablo gezinmeyi, animasyonu, hareketleri, metin alanlarını ve fazlasını kapsar; [API sayfası](api.md) da her Compose API'sini Kompoz'daki durumuyla listeler.

## Neler farklı

- **Compiler plugin yok.** Kimlik çağrı noktalarından gelir; bu yüzden aynı satırdaki iki çağrı sıralarıyla ayırt edilir. Öğeleri yer değiştiren döngülerde `key` kullanın ([Yaşam döngüsü](lifecycle.md) sayfasına bakın).
- **`by` delegasyonu yok.** Durumlar `get()` ile okunur, `set()` ile yazılır.
- **Atlamak `==` gerektirir.** Bir composable ancak argümanları eşit çıkarsa atlanır. `std::function` karşılaştırılamaz; bu yüzden onu alan bir composable yeniden başlatılabilir ama hiç atlanmaz.
- **Platformlar.** Kompoz Linux, Windows, Android ve web'de SDL3 üzerinden çizer; birlikte çalışılacak Android View'ları yoktur.
