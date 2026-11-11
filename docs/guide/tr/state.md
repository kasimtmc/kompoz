# Durum

Durum, zamanla değişebilen ve arayüzün gösterdiği her değerdir. Bu sayfa Kompoz'un durumu nasıl izlediğini, durumu recomposition'lar ve yeniden başlatmalar boyunca nasıl koruyacağınızı ve nerede yaşaması gerektiğini anlatıyor.

## İzlenebilir durum

Kompoz, *izlenebilir* bir durum değiştiğinde yeniden kompoze eder. En temel türü `mutableStateOf` yapar, bir `MutableState<T>`:

```kotlin body
val name = remember { mutableStateOf("Ada") }
Text(name.value)                                                // okumak çalışan composable'ı abone yapar
Button(onClick = { name.value = "Grace" }) { Text("Yeniden adlandır") }   // yazmak okuyanları yeniden kompoze eder
```

- `value`'yu okumak, çalışan composable'ın duruma bağlı olduğunu kaydeder.
- Ona yazmak yeni bir değer saklar. `T` `==` ile karşılaştırılabiliyorsa ve değer aynıysa hiçbir şey olmaz; değilse onu okuyan her composable yeniden kompoze edilir. Bir kural, Compose'un `SnapshotMutationPolicy`'si, bunu değiştirebilir: `mutableStateOf(v, neverEqualPolicy())` her yazmada, aynı değerde de, okuyucuları yeniden kompoze eder; varsayılan `structuralEqualityPolicy()`, yani `==`; sizin bir kuralınız C++'ta bir `SnapshotMutationPolicy<T>{[](const T& a, const T& b) { … }}`'dir (yakalamasız bir işlev). `derivedStateOf(policy, calc)` da bir kural alır.
- `update { }` değeri yerinde değiştirir, kapsayıcılar için: `todos.update { it.add(item) }`.
- `peek()` abone olmadan okur; değer değişince yeniden kompoze edilmemesi gereken kod için.

`var x by mutableStateOf(…)` ile (ya da `by remember { mutableStateOf(…) }`), Kotlin'deki gibi `x` durumun değerini okur ve yazar. Düz bir değişken izlenebilir değildir: onu değiştirmek hiçbir şeyi yeniden kompoze etmez, yerel bir değişken de composable döndüğü anda yok olur.

Bir durum paylaşılır: kopyaları aynı değeri görür; bu yüzden onu yakalayan bir lambda (burada her lambda gibi değerle) aynı durumu okur ve yazar:

```kotlin
@Composable
fun NameField() {
    var name by remember { mutableStateOf("") }
    OutlinedTextField(value = name, onValueChange = { name = it })
}
```

Bir durumu yalnızca okuyan bir composable, Compose'un salt okunur `State`'i olan bir `State<T>` alabilir: bir `MutableState<T>` ya da türetilmiş bir durum (`derivedStateOf`) ona dönüşür. `value`'su ve `peek()`'i vardır ama ona yazılamaz; hangi durum olduğuna göre karşılaştırılır, bu yüzden composable aynısı yeniden verildiğinde atlanır:

```kotlin
@Composable
fun Progress(fraction: State<Float>) {
    LinearProgressIndicator(fraction.value)
}
```

## remember

Bir composable'ın gövdesi her recomposition'da çalışır, yani içinde oluşturulan bir durum her seferinde baştan başlar. `remember` bir değeri recomposition'lar boyunca korur:

```kotlin
@Composable
fun Counter() {
    var count by remember { mutableStateOf(0) }   // bir kez yapılır
    Button(onClick = { count++ }) { Text("$count") }
}
```

Lambda, composable ilk kez kompoze edildiğinde çalışır; ondan sonra `remember` saklanan değeri döndürür. Composable kompozisyondan çıktığında (bir `if` yanlış olduğunda, bir liste öğesi kaldırıldığında) değer unutulur, geri gelirse yeniden yapılır.

`remember` yalnız durumlar için değil, her değer için çalışır. Pahalı bir nesneyi ya da kimliği önemli olan bir nesneyi, örneğin bir `MutableInteractionSource`'u veya bir `FocusRequester`'ı tutmak için kullanın.

### Bir anahtarla hatırlamak

`remember(key) { … }`, `key` değişince değeri yeniden hesaplar:

```kotlin
data class User(val first: String, val last: String)

@Composable
fun Greeting(user: User) {
    val fullName = remember(user.first, user.last) { "Sayın ${user.first} ${user.last}" }
    Text(fullName)
}
```

Anahtar `==` ile karşılaştırılabilen herhangi bir değer olabilir. Buradaki gibi iki ya da üç anahtar verilebilir; biri değiştiğinde değer yeniden hesaplanır. Daha fazlası için bir `data class` ya da bir `std::tuple` kullanın.

## rememberSaveable

`remember` bir değeri composable kompozisyonda kaldığı sürece korur. `rememberSaveable` onu ayrıca şu durumlarda da korur:

- composable bir `SaveableStateHolder`'ın içinde çıkıp geri geldiğinde; bir `NavHost`'un ekranlarına geri döndüğünüzde olduğu gibi;
- Android uygulamanın sürecini arka planda sonlandırıp geri getirdiğinde, masaüstünde de `KOMPOZ_STATE_FILE` ile çalıştırmalar arasında.

```kotlin body
var query by rememberSaveable { mutableStateOf("") }
Text(query)
```

Compose'un `rememberSaveable(inputs)`'u gibi önce girdiler verilirse, girdiler değiştiğinde değeri yeniden yapar; elindekini de kaydedilmiş olanı da bırakır: `rememberSaveable(userId) { mutableStateOf("") }`.

Sürecin sonlanmasından sağ çıkmak için değerin *kaydedilebilir* olması gerekir. Sayılar, `Boolean`, enum'lar, string'ler ve kaydedilebilir türlerin listeleri ve null olabilenleri kaydedilebilirdir; kaydedilebilir bir değerin durumları ve kütüphanenin kendi durum nesneleri de (kaydırma ve tembel liste durumları, `TextFieldState`, `NavController`) öyledir. Kendi türünüz için composable'larınızın yanında, C++'ta `Saver`'ı özelleştirin:

```kotlin
struct Point {
    float x = 0, y = 0;
};

template <>
struct kompoz::Saver<Point> {
    static SavedValue save(const Point& p) { return SavedValue::List{p.x, p.y}; }
    static std::optional<Point> restore(const SavedValue& v) {
        auto* list = v.list();
        if (!list || list->size() != 2) return std::nullopt;
        auto x = (*list)[0].number(), y = (*list)[1].number();
        if (!x || !y) return std::nullopt;
        return Point{float(*x), float(*y)};
    }
};

@Composable
fun Pin() {
    val where by rememberSaveable { mutableStateOf(Point()) }
    Text("${where.x}")
}
```

Ya da Compose'un `rememberSaveable(saver = …)`'ı gibi, `listSaver` veya `mapSaver` ile yapılmış bir saver verin:

```kotlin
struct City {
    std::string name, country;
};

const auto citySaver = mapSaver<City>(
    [](const City& c) { return SavedValue::Map{{"name", c.name}, {"country", c.country}}; },
    [](const SavedValue::Map& m) -> std::optional<City> {
        auto name = m.at("name").string(), country = m.at("country").string();
        return name && country ? std::optional(City{*name, *country}) : std::nullopt;
    });

@Composable
fun CityScreen() {
    val city by rememberSaveable(saver = citySaver) { mutableStateOf(City("Madrid", "Spain")) }
    Text(city.name)
}
```

Kaydedilebilir olmayan bir değeri de bir `SaveableStateHolder` korur, ama süreç sonlandıktan sonra baştan başlar.

## Diğer durum tutucular

### derivedStateOf

Bir durum başkalarından hesaplanıyorsa ve onlardan daha seyrek değişiyorsa, hesabı `derivedStateOf` ile sarın. Okuyanları girdilerinin her değişiminde değil, yalnız sonuç değişince yeniden kompoze edilir:

```kotlin
@Composable
fun ScrollToTop() {
    val list = rememberLazyListState()
    val showButton by remember { derivedStateOf { list.firstVisibleItemIndex > 0 } }

    Box(modifier = Modifier.fillMaxSize()) {
        LazyColumn(modifier = Modifier.fillMaxSize(), state = list) {
            items(100) { Text("Satır $it") }
        }
        if (showButton) Text("Başa dön")
    }
}
```

`derivedStateOf` olmadan `firstVisibleItemIndex`'i doğrudan okumak, kaydırırken geçilen her öğede yeniden kompoze ederdi. Türetilmiş durumu `remember`'ın içinde oluşturun ki bir kez yapılsın ve girdilerini izleyebilsin.

### Anlık görüntüler ve iş parçacıkları

Durumlar, Compose'daki gibi, *anlık görüntü* başına tutulur: kod her durumu, içinde çalıştığı anlık görüntüdeki haliyle görür. Hiçbirinin içinde değilse küresel anlık görüntüde çalışır; onun yazmaları okuyucularına bir sonraki karede bildirilir. Bir kompozisyon kendine ait bir anlık görüntüde çalışır: başka iş parçacıkları bu arada ne yazarsa yazsın tek ve tutarlı bir durum görür, onun yazdıkları da o bitince görülür.

`Snapshot.withMutableSnapshot` birkaç durumu birden yazar: hiçbir şey bir değişikliği ötekiler olmadan görmez. Bunlardan biri bu arada başka bir yerde, kuralının aynı saymadığı bir değere yazıldıysa `SnapshotApplyConflictException` atar ve yazdıklarının hiçbiri kalmaz:

```kotlin body
var from by remember { mutableStateOf(100) }
var to by remember { mutableStateOf(0) }
Button(onClick = {
    Snapshot.withMutableSnapshot {  // bir aktarım: hiç yarım görülmez
        from -= 10
        to += 10
    }
}) { Text("10 aktar") }
```

Arayüz iş parçacığında durumlar serbestçe okunur ve yazılır. Başka bir iş parçacığı onları yazabilir, okumayı ise bir anlık görüntünün içinde yapar: `Snapshot.takeSnapshot()` onların o anki halinin bir görünümünü verir; `enter { }`'inde okunur, `dispose()` onu bırakır. `takeMutableSnapshot()` içine yazılabilen bir tane verir, `apply()` ile uygulanır; bir başkasının içinde alınırsa ona uygulanır. Anlık görüntüler okuma ve yazma gözlemcileri alır; `Snapshot.registerApplyObserver` uygulanan her anlık görüntünün neyi değiştirdiğini duyar.

### Durum listeleri ve haritaları

`mutableStateListOf` ve `mutableStateMapOf`, Compose'un `SnapshotStateList` ve `SnapshotStateMap`'i gibi izlenebilir kapsayıcılardır. Birini okumak abone yapar; öğe eklemek, kaldırmak ya da değiştirmek okuyanları yeniden kompoze eder:

```kotlin
@Composable
fun Tags() {
    val tags = remember { mutableStateListOf("kırmızı", "yeşil") }
    Column {
        for (tag in tags) Text(tag)
        Button(onClick = { tags.add("mavi") }) { Text("Ekle") }
    }
}
```

Listede `add`, `set`, `removeAt`, `removeIf`, `clear` ve `update`; haritada `put`, `remove`, `getOrDefault` ve benzerleri vardır. `update` ile değiştirilen bir listenin durumu da aynı şekilde iş görür.

### rememberUpdatedState

Yeniden başlamaması gereken ama bir parametrenin en son değerini kullanması gereken uzun süreli bir efekt, onu `rememberUpdatedState` üzerinden okur. Bkz. [Yan etkiler](side-effects.md#rememberupdatedstate).

### Flow'lar

`StateFlow` ve diğer flow'lar da izlenebilirdir: `collectAsState()` birini, değişince yeniden kompoze eden bir değere çevirir. Bkz. [Mimari](architecture.md).

## Durumu yukarı taşıma

Kendi durumunu tutan bir composable'ı kullanmak kolaydır ama dışarıdan kontrol etmek zordur: çağıran durumu okuyamaz, ayarlayamaz, başkalarıyla paylaşamaz. *Durumu yukarı taşıma* (hoisting) durumu çağırana taşır: composable onun yerine değeri ve bir callback alır.

```kotlin
// Durumsuz: `name`'i gösterir, düzenlemeleri bildirir. Yeniden kullanmak, önizlemek ve sınamak kolay.
@Composable
fun NameInput(name: String, onNameChange: (String) -> Unit) {
    OutlinedTextField(value = name, onValueChange = onNameChange, singleLine = true)
}

// Durumlu: durumun sahibidir ve onu aşağı verir.
@Composable
fun Profile() {
    var name by rememberSaveable { mutableStateOf("") }
    Column {
        NameInput(name = name, onNameChange = { name = it })
        Text("Merhaba, $name")
    }
}
```

Compose'daki gibi temel kural:

- Durumu, onu okuyan ya da yazan her şeyin en alttaki ortak atasına taşıyın.
- Durumu değer olarak aşağı, olayları callback olarak yukarı geçirin (*tek yönlü veri akışı*).
- Yeniden kullanılabilir bileşenlerin durumsuz bir sürümünü sunun; işe yaradığında durumlu bir sarmalayıcı ekleyin.

### Durum tutucular

Bir ekranın durumu ve mantığı büyüdükçe onları bir sınıfta toplayın: bir *durum tutucu*. `remember` ile tutulan düz bir sınıf arayüz mantığına uyar (hangi panelin açık olduğu, bir formun alanları); ekrandan uzun yaşayan ve uygulamanın geri kalanıyla konuşan durum için `ViewModel` uygundur. Kompoz'un kendi bileşenleri de bunu yapar: `LazyListState`, `DrawerState` ve `SnackbarHostState`, `rememberLazyListState()` ve benzerlerinin yaptığı durum tutuculardır. Böyle bir sınıf composable'larınızın yanında Kotlin'deki gibi yazılır: `by mutableStateOf(…)` özellikleri birer durumdur, adlarıyla okunup yazılır ve birini okuyan composable o değişince yeniden oluşturulur:

```kotlin
class SearchState {
    var query by mutableStateOf("")
    var expanded by mutableStateOf(false)

    fun clear() {
        query = ""
        expanded = false
    }
}

@Composable
fun SearchScreen() {
    val search = remember { SearchState() }
    Text(search.query)
}
```

`ViewModel`'lar için [Mimari](architecture.md) sayfasına bakın.

## Ayrıca bakın

- [Yan etkiler](side-effects.md): duruma karşılık çalışan işler ve coroutine'ler.
- [Yaşam döngüsü ve kimlik](lifecycle.md): hatırlanan değerlerin ne zaman korunup ne zaman unutulduğu.
- [Mimari](architecture.md): ViewModel'lar ve flow'lar.
