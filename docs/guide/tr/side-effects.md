# Yan etkiler

Yan etki, bir composable'ın dışındaki dünyada yapılan bir değişikliktir: bir zamanlayıcı başlatmak, bir dinleyici kaydetmek, veri yüklemek. Composable'lar sizin kontrol etmediğiniz anlarda çalışır, bu yüzden etkiler onları belirli anlarda çalıştıran API'lerden geçer. Bu sayfa bu API'leri ve Kompoz'un coroutine'lerini anlatıyor.

## Coroutine'ler

Bir `LaunchedEffect` ya da `launch` bloğu, Kotlin'in suspend lambdası gibi bir coroutine'dir: kompozc onu bir C++20 coroutine'ine çevirir ve içindeki bekleyen her çağrıyı bekler:

```kotlin body
var seconds by remember { mutableStateOf(0) }
LaunchedEffect(Unit) {
    while (true) {
        delay(1000)
        seconds++
    }
}
Text("$seconds sn")
```

Coroutine'ler kare saatiyle sürülerek arayüz iş parçacığında çalışır, bu yüzden durumu doğrudan okuyup yazabilirler. Scope'ları bitince *iptal edilirler*: coroutine beklediği yerde yok edilir ve tuttuklarının yıkıcıları çalışır. Yukarıdaki döngünün sonsuz olabilmesinin nedeni budur: composable çıkınca durur.

Bir coroutine'de bekleyebileceğiniz şeyler:

| Çağrı | Compose'daki karşılığı | Neyi bekler |
|---|---|---|
| `delay(300)` | `delay(300)` | Kare saatine göre milisaniye olarak bir süre. |
| `awaitFrame()` | `withFrameNanos { }` | Bir sonraki kareyi; onun zamanını nanosaniye olarak verir. |
| `withBackground { work }` | `withContext(Dispatchers.IO) { }` | `work` bir işçi iş parçacığında çalışırken; onun döndürdüğünü verir. |
| `anim.animateTo(1f)` | `animateTo(1f)` | Bir animasyonun bitmesini (bkz. [Animasyon](animation.md)). |
| `TaskOf<T>` döndüren bir fonksiyon | bir `suspend fun` | Sonucunu vermesini. |

Sizin bekleyen bir fonksiyonunuz şimdilik C++ ile yazılır: `co_await` ve `co_return` kullanan, `TaskOf<T>` (ya da `Task`) döndüren bir coroutine. Bir coroutine bloğu onu Kotlin'in bir `suspend fun`'ı çağırdığı gibi çağırır, kompozc da onu bekler; çağıranın parçası olarak çalışır ve onunla birlikte iptal edilir:

```kotlin
TaskOf<std::string> loadName(int id) {
    co_await delay(std::chrono::milliseconds(300));
    co_return "Kullanıcı " + std::to_string(id);
}

@Composable
fun UserName(id: Int) {
    var name by remember { mutableStateOf("…") }
    LaunchedEffect(id) { name = loadName(id) }
    Text(name)
}
```

Bir coroutine bloğu, buradaki her lambda gibi kullandığını değerle yakalar; bu yüzden hiçbir zaman yok olmuş bir şeye başvuru tutmaz; yakaladıkları o çalıştığı sürece tutulur.

### İşler, async ve yapılandırılmış kapsamlar

`launch` bir `Job` döndürür: onu `cancel()` edebilir, `isActive()`, `isCompleted()` ya da `isCancelled()` diye sorabilir ya da bir coroutine'de `job.join()` ile bitmesini bekleyebilirsiniz. `async { … }` bir coroutine başlatır (bloğu bekleyen bir fonksiyon çağırır) ve bir `Deferred<T>` döndürür; onun `await()`'i değerini verir ya da attığını atar, böylece iki iş aynı anda yürüyebilir:

```kotlin
#! struct User { std::string name; }; struct Posts { int count = 0; };
#! TaskOf<User> loadUser(int) { co_return User{"Ada"}; }
#! TaskOf<Posts> loadPosts(int) { co_return Posts{3}; }
@Composable
fun Profile(id: Int) {
    var line by remember { mutableStateOf("…") }
    val scope = rememberCoroutineScope()
    LaunchedEffect(id) {
        val user = scope.async { loadUser(id) }
        val posts = scope.async { loadPosts(id) }  // aynı anda yükleniyor
        line = "${user.await().name}: ${posts.await().count}"
    }
    Text(line)
}
```

`coroutineScope`, Kotlin'deki gibi, kendine ait bir kapsamda başlatan bir blok çalıştırır (blok şimdilik C++ ile yazılır: `CoroutineScope`'u alan bir coroutine lambdası, bkz. [C++ ile yazmak](cpp.md)) ve ancak blok ve başlattığı her şey bitince devam eder; blok `TaskOf<T>` döndürüyorsa onun değeriyle. Bir çocuk hata verirse diğerleri iptal edilir ve çağrı onun attığını atar; onu bekleyen coroutine iptal edilirse hepsi iptal edilir. `supervisorScope` da aynıdır, ama çocukları kendi başlarına hata verir: bir çocuğun hatası bildirilir ve hiçbir şeyi iptal etmez.

`withTimeout(limit, block)` süre sınırı olan bir `coroutineScope`'tur: süre dolunca hepsi iptal edilir ve çağrı `TimeoutCancellationException` atar. `withTimeoutOrNull` bunun yerine boş bir `std::optional` verir (değer vermeyen bir blok için `false`). Yakalamadığı bir `CancellationException` ile biten bir coroutine hata vermiş değil iptal edilmiştir: bildirilmez.

### Ağır işler

Coroutine'ler arayüz iş parçacığında çalışır: birinin içindeki uzun bir hesap, başka her yerde olduğu gibi arayüzü dondurur. Dosya okumayı, kod çözmeyi ve ağır hesapları `withBackground { … }` ile (Compose'un `withContext(Dispatchers.IO) { … }`'i) bir işçi iş parçacığına verin, sonuçla arayüz iş parçacığına geri dönün:

```kotlin
#! std::string readFile(const std::string& path) { return path; }
@Composable
fun FileView(path: String) {
    var text by remember { mutableStateOf("") }
    LaunchedEffect(path) {
        text = withBackground { readFile(path) }
    }
    Text(text)
}
```

İşçi iş parçacığındaki iş sonucunu döndürür, durumu da arayüz iş parçacığına geri dönmüş coroutine ayarlar; iş durumları kendisi de yazabilir, onları ise yalnızca bir anlık görüntünün içinde okur (bkz. [Durum](state.md#anlık-görüntüler-ve-iş-parçacıkları)). Coroutine bu arada iptal edilirse iş yine sonuna kadar çalışır ve sonucu atılır. Web'de iş parçacığı yoktur, bu yüzden iş hemen çalışır.

## LaunchedEffect

`LaunchedEffect(key) { … }`, composable kompozisyona girdiğinde ve `key` her değiştiğinde bloğunu, bir coroutine'i, çalıştırır; önce bir önceki çalıştırmayı iptal eder. Composable çıkınca iptal edilir.

```kotlin
@Composable
fun Greeting(user: String, snackbar: SnackbarHostState) {
    LaunchedEffect(user) {
        snackbar.showSnackbar("Tekrar hoş geldin, $user")
    }
}
```

- Tam da efektin yeniden başlaması gerektiğinde değişen bir anahtar kullanın: bir id, bir URL. Bir kez çalışması gereken bir efekt için bir sabit kullanın: `LaunchedEffect(Unit)`.
- Anahtar `==` ile karşılaştırılabilmelidir. `LaunchedEffect`, `DisposableEffect` ve `produceState` Compose'daki gibi iki ya da üç anahtar da alır (`LaunchedEffect(userId, query) { … }`: biri değişince yeniden başlar); daha fazlası için bir `data class` kullanın.

## rememberCoroutineScope

`LaunchedEffect` işi kompozisyondan başlatır. İşi bir tıklama gibi bir olaydan başlatmak için `rememberCoroutineScope`'tan bir `CoroutineScope` kullanın. Onu yapan composable çıkınca coroutine'leri iptal edilir.

```kotlin
@Composable
fun ScrollUpButton(list: LazyListState) {
    val scope = rememberCoroutineScope()
    Button(onClick = { scope.launch { list.animateScrollToItem(0) } }) { Text("Başa") }
}
```

## DisposableEffect

Bir dinleyiciyi kaydedip kaydını silmek gibi temizlik gerektiren etkiler için `DisposableEffect(key) { … }`, kompozisyondan sonra, girdiğinde ya da `key` değiştiğinde bloğunu çalıştırır. `onDispose { … }`'u, çıktığında ya da yeni bir anahtar için yeniden çalışmadan önce çalışır:

```kotlin
#! struct Sensor { void subscribe(std::function<void(float)>) {} void unsubscribe() {} };
#! Sensor& sensor();
@Composable
fun Compass() {
    var heading by remember { mutableStateOf(0f) }
    DisposableEffect(Unit) {
        sensor().subscribe { h -> heading = h }
        onDispose { sensor().unsubscribe() }
    }
    Text("${heading.toInt()}°")
}
```

## SideEffect

`SideEffect { … }`, saran scope'un her başarılı kompozisyonundan sonra, kompozisyona girenlere bu bildirildikten sonra çalışır. Bir analitik kütüphanesi gibi Kompoz'un yönetmediği bir şeye, tarif ettiği arayüz kompoze edildikten sonra durumu iletmek için kullanın:

```kotlin
#! struct Analytics { void setUserType(const std::string&) {} };
#! Analytics& analytics();
@Composable
fun Shop(userType: String) {
    SideEffect { analytics().setUserType(userType) }
    Text("Shop")
}
```

## rememberUpdatedState

Bir sabite bağlanmış uzun süreli bir efekt, composable'ın parametreleri değişince yeniden başlamaz; bu yüzden ilk yakaladığı değerleri kullanmaya devam ederdi. `rememberUpdatedState` onun okuması için en son değeri tutar:

```kotlin
@Composable
fun SplashScreen(onTimeout: () -> Unit) {
    val currentOnTimeout by rememberUpdatedState(onTimeout)
    LaunchedEffect(Unit) {  // onTimeout değişince yeniden başlamaz
        delay(3000)
        currentOnTimeout()   // en sonuncusu
    }
    Text("Yükleniyor…")
}
```

## produceState

`produceState(initial, key) { … }`, bir coroutine'in sonucunu kompozisyonun okuduğu bir değere çevirir. Coroutine `value`'yu ayarlar ve `key` değişince yeniden başlatılır:

```kotlin
#! TaskOf<int> fetchCount(const std::string&) { co_return 3; }
@Composable
fun Downloads(url: String) {
    val count by produceState(0, url) {
        value = fetchCount(url)
    }
    Text("$count dosya")
}
```

## snapshotFlow

`snapshotFlow { … }.collect { … }` bir coroutine'in içinden durumları izler: sizi bloğun şu anki değeriyle çağırır, sonra bloğun okuduğu bir durum o değeri farklı bir değere her değiştirdiğinde yeniden. Coroutine iptal edilene kadar sürer.

```kotlin
@Composable
fun ScrollLogger(list: LazyListState) {
    LaunchedEffect(Unit) {
        snapshotFlow { list.firstVisibleItemIndex }.collect { first ->
            println("ilk görünen: $first")
        }
    }
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
