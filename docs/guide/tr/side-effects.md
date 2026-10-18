# Yan etkiler

Yan etki, bir composable'ın dışındaki dünyada yapılan bir değişikliktir: bir zamanlayıcı başlatmak, bir dinleyici kaydetmek, veri yüklemek. Composable'lar sizin kontrol etmediğiniz anlarda çalışır, bu yüzden etkiler onları belirli anlarda çalıştıran API'lerden geçer. Bu sayfa bu API'leri ve Kompoz'un coroutine'lerini anlatıyor.

## Coroutine'ler

Kompoz'un asenkron kodu C++20 coroutine'lerini kullanır. Burada bir coroutine, `Task` döndüren ve beklediği yerde `co_await` kullanan bir lambdadır:

```cpp body
using namespace std::chrono_literals;
auto seconds = remember([] { return mutableStateOf(0); });
LaunchedEffect(0, [seconds]() -> Task {
    while (true) {
        co_await delay(1s);
        seconds.set(seconds.get() + 1);
    }
});
Text(std::to_string(seconds.get()) + " s");
```

Coroutine'ler kare saatiyle sürülerek arayüz iş parçacığında çalışır, bu yüzden durumu doğrudan okuyup yazabilirler. Scope'ları bitince *iptal edilirler*: coroutine beklediği `co_await`'te yok edilir ve tuttuklarının yıkıcıları çalışır. Yukarıdaki döngünün sonsuz olabilmesinin nedeni budur: composable çıkınca durur.

Bekleyebileceğiniz şeyler:

| Bekleme | Compose'daki karşılığı | Neyi bekler |
|---|---|---|
| `co_await delay(300ms)` | `delay(300)` | Kare saatine göre bir süre. |
| `co_await awaitFrame()` | `withFrameNanos { }` | Bir sonraki kareyi; onun zamanını nanosaniye olarak döndürür. |
| `co_await withBackground(work)` | `withContext(Dispatchers.IO) { }` | `work` bir işçi iş parçacığında çalışırken; onun döndürdüğünü döndürür. |
| `co_await anim.animateTo(1.f)` | `animateTo(1f)` | Bir animasyonun bitmesini (bkz. [Animasyon](animation.md)). |
| `co_await f()`, `f` `TaskOf<T>` döndürüyorsa | bir `suspend fun` | Başka bir coroutine'in sonucunu vermesini. |

Bir değer geri veren coroutine `TaskOf<T>` döndürür ve `co_return` kullanır; başka bir coroutine onu bekler, o da çağıranın parçası olarak çalışır ve onunla birlikte iptal edilir:

```cpp
TaskOf<std::string> loadName(int id) {
    co_await delay(std::chrono::milliseconds(300));
    co_return "User " + std::to_string(id);
}

void UserName(int id) {
    auto name = remember([] { return mutableStateOf(std::string("…")); });
    LaunchedEffect(id, [id, name]() -> Task { name.set(co_await loadName(id)); });
    Text(name.get());
}
```

Bir coroutine'in yakaladıkları o çalıştığı sürece canlı tutulur, bu yüzden **değerle** yakalayın. Referansla yakalayan bir lambda onu yapan composable'dan uzun yaşar ve referansları boşa düşer.

### Ağır işler

Coroutine'ler arayüz iş parçacığında çalışır: birinin içindeki uzun bir hesap, başka her yerde olduğu gibi arayüzü dondurur. Dosya okumayı, kod çözmeyi ve ağır hesapları `withBackground` ile bir işçi iş parçacığına verin, sonuçla arayüz iş parçacığına geri dönün:

```cpp
#! std::string readFile(const std::string& path) { return path; }
void FileView(const std::string& path) {
    auto text = remember([] { return mutableStateOf(std::string()); });
    LaunchedEffect(path, [path, text]() -> Task {
        text.set(co_await withBackground([path] { return readFile(path); }));
    });
    Text(text.get());
}
```

İşçi iş parçacığındaki iş kompozisyon durumuna dokunmamalıdır; sonucunu döndürür, durumu da arayüz iş parçacığına geri dönmüş coroutine ayarlar. Coroutine bu arada iptal edilirse iş yine sonuna kadar çalışır ve sonucu atılır. Web'de iş parçacığı yoktur, bu yüzden iş hemen çalışır.

## LaunchedEffect

`LaunchedEffect(key, coroutine)`, composable kompozisyona girdiğinde ve `key` her değiştiğinde bir coroutine çalıştırır; önce bir önceki çalıştırmayı iptal eder. Composable çıkınca iptal edilir.

```cpp
void Greeting(const std::string& user, SnackbarHostState snackbar) {
    LaunchedEffect(user, [user, snackbar]() -> Task {
        co_await snackbar.showSnackbarAsync("Welcome back, " + user);
    });
}
```

- Tam da efektin yeniden başlaması gerektiğinde değişen bir anahtar kullanın: bir id, bir URL. Bir kez çalışması gereken bir efekt için Compose'un `LaunchedEffect(Unit)`'i gibi bir sabit (`0`) kullanın.
- Anahtar `==` ile karşılaştırılabilmelidir. Birden fazlası için bir `std::tuple` kullanın.

## rememberCoroutineScope

`LaunchedEffect` işi kompozisyondan başlatır. İşi bir tıklama gibi bir olaydan başlatmak için `rememberCoroutineScope`'tan bir `CoroutineScope` kullanın. Onu yapan composable çıkınca coroutine'leri iptal edilir.

```cpp
void ScrollUpButton(LazyListState list) {
    auto scope = rememberCoroutineScope();
    Button([scope, list] {
        scope.launch([list]() -> Task { co_await list.animateScrollToItem(0); });
    }, [] { Text("Top"); });
}
```

## DisposableEffect

Bir dinleyiciyi kaydedip kaydını silmek gibi temizlik gerektiren etkiler için `DisposableEffect(key, effect)`, kompozisyondan sonra, girdiğinde ya da `key` değiştiğinde `effect`'i çalıştırır. Döndürdüğü fonksiyon, çıktığında ya da yeni bir anahtar için yeniden çalışmadan önce çalışır:

```cpp
#! struct Sensor { void subscribe(std::function<void(float)>) {} void unsubscribe() {} };
#! Sensor& sensor();
void Compass() {
    auto heading = remember([] { return mutableStateOf(0.f); });
    DisposableEffect(0, [heading] {
        sensor().subscribe([heading](float h) { heading.set(h); });
        return [] { sensor().unsubscribe(); };   // Compose'un onDispose { }'u gibi
    });
    Text(std::to_string(int(heading.get())) + "°");
}
```

## SideEffect

`SideEffect(effect)`, saran scope'un her başarılı kompozisyonundan sonra çalışır. Bir analitik kütüphanesi gibi Kompoz'un yönetmediği bir şeye, tarif ettiği arayüz kompoze edildikten sonra durumu iletmek için kullanın:

```cpp
#! struct Analytics { void setUserType(const std::string&) {} };
#! Analytics& analytics();
void Shop(const std::string& userType) {
    SideEffect([userType] { analytics().setUserType(userType); });
    Text("Shop");
}
```

## rememberUpdatedState

Bir sabite bağlanmış uzun süreli bir efekt, composable'ın parametreleri değişince yeniden başlamaz; bu yüzden ilk yakaladığı değerleri kullanmaya devam ederdi. `rememberUpdatedState` onun okuması için en son değeri tutar:

```cpp
void SplashScreen(std::function<void()> onTimeout) {
    auto currentOnTimeout = rememberUpdatedState(onTimeout);
    LaunchedEffect(0, [currentOnTimeout]() -> Task {   // onTimeout değişince yeniden başlamaz
        co_await delay(std::chrono::seconds(3));
        currentOnTimeout.get()();                       // en sonuncusu
    });
    Text("Loading…");
}
```

## produceState

`produceState(initial, key, producer)`, bir coroutine'in sonucunu kompozisyonun okuduğu bir değere çevirir. Coroutine ayarlayacağı bir `MutableState` alır ve `key` değişince yeniden başlatılır:

```cpp
#! TaskOf<int> fetchCount(const std::string&) { co_return 3; }
void Downloads(const std::string& url) {
    int count = produceState(0, url, [url](MutableState<int> state) -> Task {
        state.set(co_await fetchCount(url));
    });
    Text(std::to_string(count) + " files");
}
```

## snapshotFlow

`snapshotFlow(block)` bir coroutine'in içinden durumları izler: sizi bloğun şu anki değeriyle çağırır, sonra bloğun okuduğu bir durum o değeri farklı bir değere her değiştirdiğinde yeniden. Coroutine iptal edilene kadar sürer.

```cpp
void ScrollLogger(LazyListState list) {
    LaunchedEffect(0, [list]() -> Task {
        co_await snapshotFlow([list] { return list.firstVisibleItemIndex(); }).collect([](int first) {
            std::printf("first visible: %d\n", first);
        });
    });
}
```

Durum değişikliklerine yeniden kompoze etmeden etkilerle (kayıt tutma, kaydetme, analitik) karşılık vermek için kullanın.

## Bir API seçmek

| Şunu istiyorsanız | Kullanın |
|---|---|
| Bir şey göründüğünde ya da bir değer değiştiğinde asenkron iş başlatmak | `LaunchedEffect` |
| Bir tıklamadan ya da başka bir olaydan asenkron iş başlatmak | `rememberCoroutineScope` |
| Bir şeyi kaydedip sonra kaydını silmek | `DisposableEffect` |
| Her kompozisyondan sonra durumu Kompoz'un dışındaki koda iletmek | `SideEffect` |
| Uzun süreli bir efektin içinde en son callback'i kullanmak | `rememberUpdatedState` |
| Bir coroutine'in sonucunu bir değere çevirmek | `produceState` |
| Bir coroutine'in içinde durum değişikliklerine karşılık vermek | `snapshotFlow` |

## Ayrıca bakın

- [Durum](state.md): efektlerin genellikle değiştirdiği şey.
- [Mimari](architecture.md): flow'lar, `ViewModel`'lar ve onların `viewModelScope()`'u.
- [Yaşam döngüsü ve kimlik](lifecycle.md): efektlerin ne zaman başlayıp durduğu.
