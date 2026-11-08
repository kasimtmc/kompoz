# Mimari

Bir uygulamayı nasıl yapılandırırsınız: tek yönlü veri akışı, durum tutucular, `ViewModel`'lar, Flow'lar, kaydedilen durum ve yaşam döngüsü. Desenler, Android'in Compose uygulamaları için önerdikleriyle aynı, yalnızca C++'ta.

## Tek yönlü veri akışı

Durum aşağı akar, olaylar yukarı:

- Arayüz kendisine verilen durumu gösterir.
- Kullanıcı bir şey yaptığında arayüz durumu kendisi değiştirmez, bir *olay* (bir callback) bildirir.
- Durumun sahibi olan, olayın durumu nasıl değiştireceğine karar verir ve yeni durum yeniden aşağı akar.

```kotlin
data class TodoItem(val id: Int, val title: String, val done: Boolean = false)

// Durumsuz: öğeleri gösterir, kullanıcının ne yaptığını bildirir.
@Composable
fun TodoList(todos: List<TodoItem>, onToggle: (Int, Boolean) -> Unit) {
    LazyColumn(modifier = Modifier.fillMaxSize()) {
        items(todos, key = { it.id }) { t ->
            ListItem(
                headlineContent = { Text(t.title) },
                leadingContent = { Checkbox(checked = t.done, onCheckedChange = { onToggle(t.id, it) }) },
                modifier = Modifier.toggleable(value = t.done, onValueChange = { onToggle(t.id, it) }),
            )
        }
    }
}
```

Bu, composable'ları yeniden kullanmayı, önizlemeyi ve test etmeyi kolaylaştırır; her durum parçasının değiştiği tek bir yer bırakır.

## Durum nerede yaşar

| Durumun türü | Yeri |
|---|---|
| Başka hiçbir şeyin ihtiyaç duymadığı arayüz öğesi durumu (bu menü açık mı?) | Composable içinde `remember`. |
| Birkaç composable'ın paylaştığı durum | En yakın ortak atalarına taşınır (bkz. [Durum](state.md#durumu-yukarı-taşıma)). |
| Bir ekranın arayüz mantığı (form alanları, neyin seçili olduğu) | `remember` ile tutulan sade bir durum tutucu sınıf. |
| Arayüzden uzun yaşayan ekran durumu ve iş mantığı | Bir `ViewModel`. |
| Ekranlar arasında paylaşılan veri (bir repository, bir önbellek) | Kendi nesneleriniz; onlara ihtiyaç duyan `ViewModel`'lara verilir. |

## ViewModel

Bir `ViewModel`, Jetpack'teki gibi bir ekranın durumunu tutar ve işini yapar. En yakın sahibinde yaşar: bir gezinme girdisi (girdi kalıcı olarak geri yığınından çıkarıldığında temizlenir) ya da yoksa uygulama. Böylece onu gösteren composable'lar gelip gitse de ayakta kalır.

```kotlin continued
class TodoViewModel : ViewModel() {
    val items = MutableStateFlow(listOf(TodoItem(1, "Write docs"), TodoItem(2, "Ship")))

    fun toggle(id: Int, done: Boolean) {
        items.update { list -> list.map { if (it.id == id) it.copy(done = done) else it } }
    }

    override fun onCleared() { /* abonelikleri iptal et, dosyaları kapat */ }
}

@Composable
fun TodoScreen() {
    val vm = viewModel<TodoViewModel>()   // sahibi yaşadıkça hep aynısı
    val items by vm.items.collectAsState()
    TodoList(items) { id, done -> vm.toggle(id, done) }
}
```

- `viewModel<T>()` `ViewModel`'ı verir: ilk seferde oluşturulur, sonrasında hep aynısıdır (C++'ta paylaşılan bir işaretçi, Kotlin'de nesnenin kendisi olarak erişilir).
- `viewModelScope`, `ViewModel` temizlendiğinde iptal edilen bir `CoroutineScope`'tur; işlerini orada başlatın.
- `onCleared()`, sahibi ortadan kalktığında çalışır.
- İç içe bir gezinme grafiğinin ekranları, grafiğin girdisi üzerinden tek bir tanesini paylaşabilir: `viewModel<T>(*entry.parent())` (bkz. [Gezinme](navigation.md)).

Bir `ViewModel`'a bağımlılık vermek için fabrika alan aşırı yüklemeyi kullanın: `viewModel<T>(key, factory)`; fabrika onu bağımlılıklarıyla oluşturur (C++'ta `std::make_shared<T>(repository)`).

## Flow'lar

Bir `ViewModel` durumunu bir `StateFlow` olarak dışarı açar; arayüz onu `collectAsState()` ile okur ve değer değiştiğinde yeniden kompoze eder.

- `MutableStateFlow<T>` bir değer tutar. `setValue(v)` ve `update(f)` **herhangi bir thread'den** çağrılabilir; bu da onu worker thread'lerle arayüz arasındaki köprü yapar.
- `value` güncel değeri okur; `collect(f)` onu bir coroutine içinde izler.
- Kotlin'deki gibi birleştiricidir (conflated): zaten sahip olduğu değeri atamak hiçbir şey yapmaz.

`Flow<T>` soğuk bir akıştır; her toplayıcı için bir coroutine tarafından üretilir ve Kotlin'in operatörleriyle gelir: `map`, `filter`, `take`, `debounce`, `distinctUntilChanged`, `flatMapLatest`, `combine` ve daha fazlası. `stateIn` bir Flow'u bir scope'un tuttuğu bir `StateFlow`'a çevirir:

```kotlin
#! class Repository { fun search(q: String): Flow<List<String>> = flow { emit(listOf(q)) } }
class SearchViewModel(repo: Repository) : ViewModel() {
    val query = MutableStateFlow("")
    val results: StateFlow<List<String>> = query.debounce(300)
        .flatMapLatest { q -> repo.search(q) }
        .stateIn(viewModelScope, SharingStarted.Eagerly, emptyList())
}
```

`MutableSharedFlow<T>` tek seferlik olaylar (bir mesaj göstermek, gezinmek) için sıcak bir akıştır ve `tryEmit` herhangi bir thread'den çalışır. `shareIn` ve `stateIn`; `SharingStarted.Eagerly`, `Lazily` ya da `WhileSubscribed(5000)` alır; sonuncusu yukarı akışı yalnızca biri toplarken toplar.

Elle bir Flow oluşturmak:

```kotlin
fun ticks(): Flow<Int> = flow {
    var i = 0
    while (true) {
        emit(i++)
        delay(1000)
    }
}
```

Her şey arayüz thread'inde, toplayan coroutine içinde çalışır. Coroutine'i iptal etmek üreticiyi de durdurur.

## Kaydedilen durum

Android, arka plandaki bir uygulamanın sürecini sonlandırıp daha sonra geri getirebilir ve onun eskisi gibi görünmesini bekler. Bunu iki araç karşılar:

- Composable'larda `rememberSaveable` (bkz. [Durum](state.md#remembersaveable)).
- `ViewModel`'larda `SavedStateHandle`: kurucusu bir tane alan `ViewModel`, yeniden oluşturulduğunda onu eski değerleriyle birlikte alır.

```kotlin
class SearchScreenViewModel(private val state: SavedStateHandle) : ViewModel() {
    val query: StateFlow<String> = state.getStateFlow("query", "")
    fun setQuery(q: String) { state["query"] = q }
}
```

`viewModel<SearchScreenViewModel>()` handle'ı iletir. Değerler kaydedilebilir türlerden olmalıdır. Masaüstünde `KOMPOZ_STATE_FILE=file` çalıştırmalar arasında kaydedip geri yükler; böylece bunu telefon olmadan deneyebilirsiniz.

## Yaşam döngüsü

Uygulamanın ve her gezinme girdisinin, öne gelip arkaya gittikçe `Created`, `Started` ve `Resumed` arasında ilerleyen bir `Lifecycle`'ı vardır. Efektler, Jetpack'tekiler gibi onu izleyebilir:

```kotlin
#! class Player { fun play() {} fun pause() {} }
@Composable
fun Video(player: Player) {
    LifecycleResumeEffect(Unit) {
        player.play()
        onPauseOrDispose { player.pause() }   // artık resumed olmadığında
    }
}
```

`LifecycleStartEffect`, `LifecycleEventEffect(LifecycleEvent.OnStart, …)` ve `LocalLifecycleOwner.current` de vardır. `collectAsStateWithLifecycle(flow)` bir Flow'u yalnızca yaşam döngüsü en az started iken toplar; böylece arka planda abone sayılmaz.

## Ayrıca bakın

- [Durum](state.md): yerel durum ve durumu yukarı taşıma.
- [Yan etkiler](side-effects.md): coroutine'ler ve efektler.
- [Gezinme](navigation.md): her biri kendi `ViewModel`'larına sahip ekranlar.
- [Test](testing.md): durumsuz composable'ları ve `ViewModel`'ları test etmek.
