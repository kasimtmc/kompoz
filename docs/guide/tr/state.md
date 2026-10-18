# Durum

Durum, zamanla değişebilen ve arayüzün gösterdiği her değerdir. Bu sayfa Kompoz'un durumu nasıl izlediğini, durumu recomposition'lar ve yeniden başlatmalar boyunca nasıl koruyacağınızı ve nerede yaşaması gerektiğini anlatıyor.

## İzlenebilir durum

Kompoz, *izlenebilir* bir durum değiştiğinde yeniden kompoze eder. En temel türü `mutableStateOf` yapar, bir `MutableState<T>`:

```cpp body
auto name = mutableStateOf(std::string("Ada"));
Text(name.get());         // okumak, çalışan composable'ı abone yapar
name.set("Grace");        // yazmak, okuyanları yeniden kompoze eder
```

- `get()` değeri okur ve çalışan composable'ın ona bağlı olduğunu kaydeder.
- `set(v)` yeni bir değer saklar. `T` `==` ile karşılaştırılabiliyorsa ve değer aynıysa hiçbir şey olmaz; değilse onu okuyan her composable yeniden kompoze edilir.
- `update(f)` değeri yerinde değiştirir, kapsayıcılar için: `todos.update([](auto& v) { v.push_back(item); })`.
- `peek()` abone olmadan okur; değer değişince yeniden kompoze edilmemesi gereken kod için.

Düz bir değişken izlenebilir değildir: onu değiştirmek hiçbir şeyi yeniden kompoze etmez, yerel bir değişken de composable döndüğü anda yok olur.

`MutableState<T>` paylaşılan bir değere bir tutamaçtır (handle). Kopyalar ucuzdur ve hepsi aynı değeri görür; bu yüzden durumları elden ele geçirir, callback'lerde **değerle** yakalarsınız:

```cpp
void NameField() {
    auto name = remember([] { return mutableStateOf(std::string()); });
    OutlinedTextField(name.get(), [name](std::string v) { name.set(std::move(v)); });
}
```

## remember

Bir composable'ın gövdesi her recomposition'da çalışır, yani içinde oluşturulan bir durum her seferinde baştan başlar. `remember` bir değeri recomposition'lar boyunca korur:

```cpp
void Counter() {
    auto count = remember([] { return mutableStateOf(0); });   // bir kez yapılır
    Button([count] { count.set(count.get() + 1); }, [&] { Text(std::to_string(count.get())); });
}
```

Lambda, composable ilk kez kompoze edildiğinde çalışır; ondan sonra `remember` saklanan değeri döndürür. Composable kompozisyondan çıktığında (bir `if` yanlış olduğunda, bir liste öğesi kaldırıldığında) değer unutulur, geri gelirse yeniden yapılır.

`remember` yalnız durumlar için değil, her değer için çalışır. Pahalı bir nesneyi ya da kimliği önemli olan bir nesneyi, örneğin bir `MutableInteractionSource`'u veya bir `FocusRequester`'ı tutmak için kullanın.

### Bir anahtarla hatırlamak

`remember(key, init)`, `key` değişince değeri yeniden hesaplar:

```cpp
#! struct User { std::string first, last; };
void Greeting(const User& user) {
    auto fullName = remember(user.first + " " + user.last, [&] { return "Dear " + user.first + " " + user.last; });
    Text(fullName);
}
```

Anahtar `==` ile karşılaştırılabilen herhangi bir değer olabilir; birden fazlası için bir `std::tuple` ya da bir struct kullanın.

## rememberSaveable

`remember` bir değeri composable kompozisyonda kaldığı sürece korur. `rememberSaveable` onu ayrıca şu durumlarda da korur:

- composable bir `SaveableStateHolder`'ın içinde çıkıp geri geldiğinde; bir `NavHost`'un ekranlarına geri döndüğünüzde olduğu gibi;
- Android uygulamanın sürecini arka planda sonlandırıp geri getirdiğinde, masaüstünde de `KOMPOZ_STATE_FILE` ile çalıştırmalar arasında.

```cpp body
auto query = rememberSaveable([] { return mutableStateOf(std::string()); });
```

Sürecin sonlanmasından sağ çıkmak için değerin *kaydedilebilir* olması gerekir. Sayılar, `bool`, enum'lar, string'ler ve kaydedilebilir türlerin `std::vector`'ı ve `std::optional`'ı kaydedilebilirdir; kaydedilebilir bir değerin `MutableState`'i ve kütüphanenin kendi durum nesneleri de (kaydırma ve tembel liste durumları, `TextFieldState`, `NavController`) öyledir. Kendi türünüz için `Saver`'ı özelleştirin:

```cpp
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

void Pin() {
    auto where = rememberSaveable([] { return mutableStateOf(Point{}); });
    Text(std::to_string(where.get().x));
}
```

Ya da Compose'un `rememberSaveable(saver = …)`'ı gibi, `listSaver` veya `mapSaver` ile yapılmış bir saver verin:

```cpp
struct City {
    std::string name, country;
};

const auto citySaver = mapSaver<City>(
    [](const City& c) { return SavedValue::Map{{"name", c.name}, {"country", c.country}}; },
    [](const SavedValue::Map& m) -> std::optional<City> {
        auto name = m.at("name").string(), country = m.at("country").string();
        return name && country ? std::optional(City{*name, *country}) : std::nullopt;
    });

void CityScreen() {
    auto city = rememberSaveable(citySaver, [] { return mutableStateOf(City{"Madrid", "Spain"}); });
    Text(city.get().name);
}
```

Kaydedilebilir olmayan bir değeri de bir `SaveableStateHolder` korur, ama süreç sonlandıktan sonra baştan başlar.

## Diğer durum tutucular

### derivedStateOf

Bir durum başkalarından hesaplanıyorsa ve onlardan daha seyrek değişiyorsa, hesabı `derivedStateOf` ile sarın. Okuyanları girdilerinin her değişiminde değil, yalnız sonuç değişince yeniden kompoze edilir:

```cpp
void ScrollToTop() {
    auto list = rememberLazyListState();
    auto showButton = remember([list] { return derivedStateOf([list] { return list.firstVisibleItemIndex() > 0; }); });

    Box(Modifier().fillMaxSize(), [&] {
        LazyColumn(Modifier().fillMaxSize(), {.state = list}, [](LazyListScope& l) {
            l.items(100, [](int i) { Text("Row " + std::to_string(i)); });
        });
        if (showButton.get()) Text("Back to top");
    });
}
```

`derivedStateOf` olmadan `firstVisibleItemIndex()`'i doğrudan okumak, kaydırırken geçilen her öğede yeniden kompoze ederdi. Türetilmiş durumu `remember`'ın içinde oluşturun ki bir kez yapılsın ve girdilerini izleyebilsin.

### Durum listeleri ve haritaları

`mutableStateListOf` ve `mutableStateMapOf`, Compose'un `SnapshotStateList` ve `SnapshotStateMap`'i gibi izlenebilir kapsayıcılardır. Birini okumak abone yapar; öğe eklemek, kaldırmak ya da değiştirmek okuyanları yeniden kompoze eder:

```cpp
void Tags() {
    auto tags = remember([] { return mutableStateListOf<std::string>({"red", "green"}); });
    Column([&] {
        for (const std::string& tag : tags) Text(tag);
        Button([tags] { tags.add("blue"); }, [] { Text("Add"); });
    });
}
```

Listede `add`, `set`, `removeAt`, `removeIf`, `clear` ve `update`; haritada `put`, `remove`, `getOrDefault` ve benzerleri vardır. `update` ile değiştirilen bir `MutableState<std::vector<T>>` da aynı şekilde iş görür.

### rememberUpdatedState

Yeniden başlamaması gereken ama bir parametrenin en son değerini kullanması gereken uzun süreli bir efekt, onu `rememberUpdatedState` üzerinden okur. Bkz. [Yan etkiler](side-effects.md#rememberupdatedstate).

### Flow'lar

`StateFlow` ve diğer flow'lar da izlenebilirdir: `collectAsState()` birini, değişince yeniden kompoze eden bir değere çevirir. Bkz. [Mimari](architecture.md).

## Durumu yukarı taşıma

Kendi durumunu tutan bir composable'ı kullanmak kolaydır ama dışarıdan kontrol etmek zordur: çağıran durumu okuyamaz, ayarlayamaz, başkalarıyla paylaşamaz. *Durumu yukarı taşıma* (hoisting) durumu çağırana taşır: composable onun yerine değeri ve bir callback alır.

```cpp
// Durumsuz: `name`'i gösterir, düzenlemeleri bildirir. Yeniden kullanması, önizlemesi ve test etmesi kolay.
void NameInput(const std::string& name, std::function<void(std::string)> onNameChange) {
    OutlinedTextField(name, std::move(onNameChange), Modifier(), {.label = "Name"});
}

// Durumlu: durumun sahibidir ve onu aşağı verir.
void Profile() {
    auto name = rememberSaveable([] { return mutableStateOf(std::string()); });
    Column([&] {
        NameInput(name.get(), [name](std::string v) { name.set(std::move(v)); });
        Text("Hello, " + name.get());
    });
}
```

Compose'daki gibi temel kural:

- Durumu, onu okuyan ya da yazan her şeyin en alttaki ortak atasına taşıyın.
- Durumu değer olarak aşağı, olayları callback olarak yukarı geçirin (*tek yönlü veri akışı*).
- Yeniden kullanılabilir bileşenlerin durumsuz bir sürümünü sunun; işe yaradığında durumlu bir sarmalayıcı ekleyin.

### Durum tutucular

Bir ekranın durumu ve mantığı büyüdükçe onları bir sınıfta toplayın: bir *durum tutucu*. `remember` ile tutulan düz bir sınıf arayüz mantığına uyar (hangi panelin açık olduğu, bir formun alanları); ekrandan uzun yaşayan ve uygulamanın geri kalanıyla konuşan durum için `ViewModel` uygundur. Kompoz'un kendi bileşenleri de bunu yapar: `LazyListState`, `DrawerState` ve `SnackbarHostState`, `rememberLazyListState()` ve benzerlerinin yaptığı durum tutuculardır.

```cpp
class SearchState {
public:
    MutableState<std::string> query = mutableStateOf(std::string());
    MutableState<bool> expanded = mutableStateOf(false);
    void clear() const {
        query.set("");
        expanded.set(false);
    }
};

void SearchScreen() {
    auto& search = remember([] { return SearchState(); });
    Text(search.query.get());
}
```

`ViewModel`'lar için [Mimari](architecture.md) sayfasına bakın.

## Ayrıca bakın

- [Yan etkiler](side-effects.md): duruma karşılık çalışan işler ve coroutine'ler.
- [Yaşam döngüsü ve kimlik](lifecycle.md): hatırlanan değerlerin ne zaman korunup ne zaman unutulduğu.
- [Mimari](architecture.md): ViewModel'lar ve flow'lar.
