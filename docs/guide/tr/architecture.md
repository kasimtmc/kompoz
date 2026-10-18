# Mimari

Bir uygulamayı nasıl yapılandırırsınız: tek yönlü veri akışı, durum tutucular, `ViewModel`'lar, Flow'lar, kaydedilen durum ve yaşam döngüsü. Desenler, Android'in Compose uygulamaları için önerdikleriyle aynı, yalnızca C++'ta.

## Tek yönlü veri akışı

Durum aşağı akar, olaylar yukarı:

- Arayüz kendisine verilen durumu gösterir.
- Kullanıcı bir şey yaptığında arayüz durumu kendisi değiştirmez, bir *olay* (bir callback) bildirir.
- Durumun sahibi olan, olayın durumu nasıl değiştireceğine karar verir ve yeni durum yeniden aşağı akar.

```cpp
struct TodoItem {
    int id;
    std::string title;
    bool done;
    friend bool operator==(const TodoItem&, const TodoItem&) = default;
};

// Durumsuz: öğeleri gösterir, kullanıcının ne yaptığını bildirir.
void TodoList(const std::vector<TodoItem>& items, std::function<void(int id, bool done)> onToggle) {
    LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& list) {
        list.items(items, [](const TodoItem& t) { return t.id; }, [onToggle](const TodoItem& t) {
            ListItem(t.done, [onToggle, id = t.id](bool done) { onToggle(id, done); },
                     {.headline = [&] { Text(t.title); }, .leading = [&] { Checkbox(t.done, {}); }});
        });
    });
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

```cpp continued
class TodoViewModel : public ViewModel {
public:
    MutableStateFlow<std::vector<TodoItem>> items{std::vector<TodoItem>{{1, "Write docs", false}, {2, "Ship", false}}};

    void toggle(int id, bool done) {
        items.update([&](const std::vector<TodoItem>& list) {
            std::vector<TodoItem> next = list;
            for (TodoItem& t : next)
                if (t.id == id) t.done = done;
            return next;
        });
    }

protected:
    void onCleared() override { /* abonelikleri iptal et, dosyaları kapat */ }
};

void TodoScreen() {
    auto vm = viewModel<TodoViewModel>();   // sahibi yaşadıkça hep aynısı
    std::vector<TodoItem> items = vm->items.collectAsState();
    TodoList(items, [vm](int id, bool done) { vm->toggle(id, done); });
}
```

- `viewModel<T>()` bir `std::shared_ptr<T>` döndürür: ilk seferde oluşturulur, sonrasında hep aynısıdır. Callback'lerde onu değerle yakalayın.
- `viewModelScope()`, `ViewModel` temizlendiğinde iptal edilen bir `CoroutineScope`'tur; işlerini orada başlatın.
- `onCleared()`, sahibi ortadan kalktığında çalışır.
- İç içe bir gezinme grafiğinin ekranları, grafiğin girdisi üzerinden tek bir tanesini paylaşabilir: `viewModel<T>(*entry.parent())` (bkz. [Gezinme](navigation.md)).

Bir `ViewModel`'a bağımlılık vermek için fabrika alan aşırı yüklemeyi kullanın: `viewModel<T>(key, [&] { return std::make_shared<T>(repository); })`.

## Flow'lar

Bir `ViewModel` durumunu bir `StateFlow` olarak dışarı açar; arayüz onu `collectAsState()` ile okur ve değer değiştiğinde yeniden kompoze eder.

- `MutableStateFlow<T>` bir değer tutar. `setValue(v)` ve `update(f)` **herhangi bir thread'den** çağrılabilir; bu da onu worker thread'lerle arayüz arasındaki köprü yapar.
- `value()` güncel değeri okur; `collect(f)` onu bir coroutine içinde izler.
- Kotlin'deki gibi birleştiricidir (conflated): zaten sahip olduğu değeri atamak hiçbir şey yapmaz.

`Flow<T>` soğuk bir akıştır; her toplayıcı için bir coroutine tarafından üretilir ve Kotlin'in operatörleriyle gelir: `map`, `filter`, `take`, `debounce`, `distinctUntilChanged`, `flatMapLatest`, `combine` ve daha fazlası. `stateIn` bir Flow'u bir scope'un tuttuğu bir `StateFlow`'a çevirir:

```cpp
#! struct Repository { Flow<std::vector<std::string>> search(const std::string& q) { return flow<std::vector<std::string>>([q](FlowCollector<std::vector<std::string>> emit) -> Task { co_await emit({q}); }); } };
class SearchViewModel : public ViewModel {
public:
    explicit SearchViewModel(Repository& repo)
        : results(query.debounce(std::chrono::milliseconds(300))
                       .flatMapLatest([&repo](const std::string& q) { return repo.search(q); })
                       .stateIn(viewModelScope(), std::vector<std::string>{})) {}

    MutableStateFlow<std::string> query{std::string()};
    StateFlow<std::vector<std::string>> results;
};
```

`MutableSharedFlow<T>` tek seferlik olaylar (bir mesaj göstermek, gezinmek) için sıcak bir akıştır ve `tryEmit` herhangi bir thread'den çalışır. `shareIn` ve `stateIn`; `SharingStarted::Eagerly`, `Lazily` ya da `WhileSubscribed(5000ms)` alır; sonuncusu yukarı akışı yalnızca biri toplarken toplar.

Elle bir Flow oluşturmak:

```cpp
Flow<int> ticks() {
    return flow<int>([](FlowCollector<int> emit) -> Task {
        for (int i = 0;; ++i) {
            co_await emit(i);
            co_await delay(std::chrono::seconds(1));
        }
    });
}
```

Her şey arayüz thread'inde, toplayan coroutine içinde çalışır. Coroutine'i iptal etmek üreticiyi de durdurur.

## Kaydedilen durum

Android, arka plandaki bir uygulamanın sürecini sonlandırıp daha sonra geri getirebilir ve onun eskisi gibi görünmesini bekler. Bunu iki araç karşılar:

- Composable'larda `rememberSaveable` (bkz. [Durum](state.md#remembersaveable)).
- `ViewModel`'larda `SavedStateHandle`: kurucusu bir tane alan `ViewModel`, yeniden oluşturulduğunda onu eski değerleriyle birlikte alır.

```cpp
class SearchScreenViewModel : public ViewModel {
public:
    explicit SearchScreenViewModel(SavedStateHandle state) : state_(state), query(state.getStateFlow<std::string>("query", "")) {}
    void setQuery(const std::string& q) { state_.set("query", q); }
    StateFlow<std::string> query;

private:
    SavedStateHandle state_;
};
```

`viewModel<SearchScreenViewModel>()` handle'ı iletir. Değerler kaydedilebilir türlerden olmalıdır. Masaüstünde `KOMPOZ_STATE_FILE=file` çalıştırmalar arasında kaydedip geri yükler; böylece bunu telefon olmadan deneyebilirsiniz.

## Yaşam döngüsü

Uygulamanın ve her gezinme girdisinin, öne gelip arkaya gittikçe `Created`, `Started` ve `Resumed` arasında ilerleyen bir `Lifecycle`'ı vardır. Efektler, Jetpack'tekiler gibi onu izleyebilir:

```cpp
#! struct Player { void play() {} void pause() {} };
void Video(std::shared_ptr<Player> player) {
    LifecycleResumeEffect(0, [player] {
        player->play();
        return [player] { player->pause(); };   // artık resumed olmadığında
    });
}
```

`LifecycleStartEffect`, `LifecycleEventEffect(LifecycleEvent::OnStart, …)` ve `currentLifecycleOwner()` de vardır. `collectAsStateWithLifecycle(flow)` bir Flow'u yalnızca yaşam döngüsü en az started iken toplar; böylece arka planda abone sayılmaz.

## Ayrıca bakın

- [Durum](state.md): yerel durum ve durumu yukarı taşıma.
- [Yan etkiler](side-effects.md): coroutine'ler ve efektler.
- [Gezinme](navigation.md): her biri kendi `ViewModel`'larına sahip ekranlar.
- [Test](testing.md): durumsuz composable'ları ve `ViewModel`'ları test etmek.
