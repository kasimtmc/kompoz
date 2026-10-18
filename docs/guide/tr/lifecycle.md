# Yaşam döngüsü ve kimlik

Bir composable kompozisyona girer, sıfır ya da daha fazla kez yeniden kompoze edilir ve çıkar. Bu sayfa bu yaşam döngüsünü, Kompoz'un bir derleyici eklentisi olmadan bir çağrıyı ötekinden nasıl ayırdığını ve bir composable'ın atlanmasını neyin sağladığını anlatıyor.

## Bir composable'ın yaşam döngüsü

Kompoz arayüzünüzü ilk kez çalıştırdığında, çağrılan her composable'ı bir ağaca kaydeder: *kompozisyon*. O andan sonra bu ağaçtaki her çağrının bir yaşam döngüsü vardır:

1. İlk kez çağrıldığında **kompozisyona girer**. `remember` blokları değerlerini hesaplar, efektleri zamanlanır.
2. Okuduğu bir durum değiştiğinde ya da ebeveyni çalışıp argümanları değiştiğinde **yeniden kompoze edilir**. Hatırlanan değerler korunur.
3. Artık çağrılmadığında **kompozisyondan çıkar**: bir `if` yanlış oldu, bir liste öğesi gitti, ekrandan başka bir ekrana geçildi. Hatırlanan değerleri unutulur, `DisposableEffect` temizlikleri çalışır, coroutine'leri iptal edilir.

```cpp
void Details(bool show) {
    if (show) {
        // show true olunca girer, false olunca çıkar.
        auto opened = remember([] { return mutableStateOf(0); });
        DisposableEffect(0, [] {
            // burada: girdi
            return [] { /* burada: çıktı */ };
        });
        Text("Details");
    }
}
```

Çıkıp geri gelen bir composable sıfırdan başlar: `remember` lambdasını yeniden çalıştırır. Bir değeri bunun ötesinde korumak için bir `SaveableStateHolder`'ın içinde `rememberSaveable` kullanın (gezinme ekranları için bunu yapar) ya da durumu `if`'in üstüne taşıyın.

## Kimlik: bir çağrı nereden geliyor

Hangi hatırlanan değerin hangi çağrıya ait olduğunu bilmek için Kompoz her çağrıyı recomposition'lar boyunca tanımak zorundadır. Jetpack Compose bunu bir derleyici eklentisiyle yapar. Kompoz ise çalışma zamanında, `std::source_location` ile yapar:

- Her composable son parametre olarak `CallSite site = CallSite::current()` alır. Varsayılan argüman composable'ın *çağrıldığı* yerde değerlendirilir, yani `site` çağrının dosyası, satırı ve sütunudur.
- Bir ebeveynin içinde aynı yerden yapılan çağrılar sıralarıyla ayırt edilir: o yerden yapılan ilk çağrı, ikinci çağrı ve böyle devam eder.

`site`'ı kendiniz geçirmezsiniz: bunu varsayılan argüman yapar. Onu yalnız kendi kimliği olması gereken bir composable tanımlarken yazarsınız:

```cpp
void Badge(int count, CallSite site = CallSite::current()) {
    composable(site, count, [=] {
        Text(std::to_string(count));
    });
}
```

`remember` de aynı şekilde bir `CallSite` alır; bu yüzden farklı satırlardaki iki `remember` çağrısı farklı değerlerdir.

### Düz bir fonksiyon nedir

`composable` ya da `ComposableScope` kullanmadan composable'ları çağıran düz bir C++ fonksiyonunun kendine ait bir kimliği yoktur. İçindeki çağrılar, fonksiyonu çağıran composable'ın içinde kendi çağrı yerleriyle tanınır. Bunda bir sorun yoktur, küçük yardımcıların çoğu böyle yazılır:

```cpp
void Header(const std::string& title) {      // düz bir fonksiyon
    Text(title, {.font = typography().headlineSmall});
    HorizontalDivider();
}
```

Düz bir fonksiyonun sahip olmadığı şey bir *scope*'tur: okuduğu durumlar onu saran en yakın scope'a aittir, onlar değişince o scope yeniden kompoze edilir; fonksiyonun kendisi de hiçbir zaman tek başına atlanmaz.

### Scope'lar

`composable(site, args…, body)`, *yeniden başlatılabilir* ve *atlanabilir* bir scope yapar; Kotlin'de sıradan bir `@Composable` fonksiyon neyse odur:

- **Yeniden başlatılabilir** (restartable): gövdenin okuduğu bir durum değişince ebeveyn değil, yalnız bu gövde yeniden çalışır.
- **Atlanabilir** (skippable): ebeveyn çalıştığında `args…` geçen seferkine eşitse gövde çalışmaz.

Gövde daha sonra tek başına çalışabileceği için değerle yakalamalıdır: `[=]`.

`ComposableScope scope(site);` bir composable'a scope yapmadan bir kimlik verir. İçerik lambdası alan bileşenler bunu kullanır; tıpkı Compose'da `Column` ve `Row`'un inline fonksiyonlar olması gibi: içerik lambdaları referansla yakalar ve tek başına yeniden çalıştırılamaz, bu yüzden okudukları durum çağırana aittir.

```cpp
void Section(const std::string& title, const Content& content, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    Column(Modifier().padding(16), [&] {
        Text(title, {.font = typography().titleMedium});
        content();
    });
}
```

## Atlamak

Bir scope; ebeveyni çalıştığında, argümanları önceki çağrıya eşitse ve okuduğu hiçbir şey o zamandan beri değişmemişse atlanır. Bunun için argümanların `==`'ı olması gerekir:

- Sayılar, string'ler, enum'lar ve `operator==`'ı olan (ya da `= default`) kendi struct'larınız değerle karşılaştırılır.
- Bir `MutableState` kimlikle karşılaştırılır: değeri değişmiş olsa bile aynı durum eşittir. Scope yine de değer değişince yeniden kompoze edilir, çünkü onu okumuştur.
- Bir `Modifier` öğelerinin değerleriyle karşılaştırılır.
- `std::function` karşılaştırılamaz. Bir tane alan composable yeniden başlatılabilir ama hiçbir zaman atlanmaz.

```cpp
struct Todo {
    int id = 0;
    std::string title;
    bool done = false;
    friend bool operator==(const Todo&, const Todo&) = default;
};

void TodoRow(const Todo& todo, MutableState<std::vector<Todo>> todos, CallSite site = CallSite::current()) {
    composable(site, todo, todos, [=] {   // todo ve todos aynı kaldıkça atlanır
        Row([&] {
            Checkbox(todo.done, [todos, id = todo.id](bool done) {
                todos.update([&](std::vector<Todo>& list) {
                    for (Todo& t : list)
                        if (t.id == id) t.done = done;
                });
            });
            Text(todo.title);
        });
    });
}
```

Bir todo değişince liste yeniden kompoze edilir, diğer her `TodoRow` atlanır.

`Text` de atlanabilir. İçerik lambdası alan bileşenlerin çoğu scope değildir ve çağıranlarıyla birlikte çalışır; bu ucuzdur, çünkü yerleştirdikleri şey ancak girdileri değişince değişir.

## Döngülerdeki çağrılar

Bir döngünün içinde her yineleme aynı yerden çağırır, bu yüzden çağrılar sıralarıyla tanınır. Liste yalnız sonundan büyüdükçe bu işler. Öğeler araya eklendiğinde, kaldırıldığında ya da yeniden sıralandığında durum öğede değil, *konumda* kalırdı:

```cpp nocheck
for (const Todo& todo : todos.get()) {
    TodoRow(todo, todos);   // başa ekleyin, her satırın hatırlanan durumu bir kayar
}
```

`key` her çağrıya sizin seçtiğiniz bir kimlik verir:

```cpp
#! struct Todo { int id = 0; std::string title; bool done = false; friend bool operator==(const Todo&, const Todo&) = default; };
#! void TodoRow(const Todo&, MutableState<std::vector<Todo>>, CallSite site = CallSite::current()) {}
void TodoList(MutableState<std::vector<Todo>> todos) {
    Column([&] {
        for (const Todo& todo : todos.get()) {
            key(todo.id, [&] { TodoRow(todo, todos); });
        }
    });
}
```

Artık her satırın hatırlanan değerleri, efektleri ve düğümleri onun `id`'sini izler. Tembel listeler de aynı nedenle `items` çağrılarında anahtar alır (bkz. [Listeler ve ızgaralar](lists.md)).

## İçeriği taşımak

Aynı composable'ı ağaçta başka bir yerden çağırmak başka bir çağrıdır: durumu baştan başlar. `movableContentOf`, Compose'daki gibi durumu kendisiyle birlikte taşınan içerik yapar:

```cpp
void Player(bool wide) {
    auto video = remember([] { return movableContentOf([] {
        auto position = remember([] { return mutableStateOf(0); });   // taşınınca korunur
        Text("Playing at " + std::to_string(position.get()));
    }); });

    if (wide) {
        Row([&] { video(); Text("Comments"); });
    } else {
        Column([&] { video(); Text("Comments"); });
    }
}
```

`wide` değişince içeriğin hatırlanan değerleri, düğümleri ve çalışan efektleri yeniden yapılmak yerine sütundan satıra taşınır.

## Ayrıca bakın

- [Durum](state.md): `remember`, `rememberSaveable` ve durum tutucular.
- [Yan etkiler](side-effects.md): bir composable girince başlayan ve çıkınca duran efektler.
- [Aşamalar](phases.md): kompozisyondan sonra ne olur.
