# Yaşam döngüsü ve kimlik

Bir composable kompozisyona girer, sıfır ya da daha fazla kez yeniden kompoze edilir ve çıkar. Bu sayfa bu yaşam döngüsünü, Kompoz'un bir çağrıyı ötekinden nasıl ayırdığını ve bir composable'ın atlanmasını neyin sağladığını anlatıyor.

## Bir composable'ın yaşam döngüsü

Kompoz arayüzünüzü ilk kez çalıştırdığında, çağrılan her composable'ı bir ağaca kaydeder: *kompozisyon*. O andan sonra bu ağaçtaki her çağrının bir yaşam döngüsü vardır:

1. İlk kez çağrıldığında **kompozisyona girer**. `remember` blokları değerlerini hesaplar, efektleri zamanlanır.
2. Okuduğu bir durum değiştiğinde ya da ebeveyni çalışıp argümanları değiştiğinde **yeniden kompoze edilir**. Hatırlanan değerler korunur.
3. Artık çağrılmadığında **kompozisyondan çıkar**: bir `if` yanlış oldu, bir liste öğesi gitti, ekrandan başka bir ekrana geçildi. Hatırlanan değerleri unutulur, `DisposableEffect` temizlikleri çalışır, coroutine'leri iptal edilir.

```kotlin
@Composable
fun Details(show: Boolean) {
    if (show) {
        // show doğru olunca girer, yanlış olunca çıkar.
        val opened = remember { mutableStateOf(0) }
        DisposableEffect(Unit) {
            // burada: girdi
            onDispose { /* burada: çıktı */ }
        }
        Text("Ayrıntılar")
    }
}
```

Girenler ve çıkanlar, kompozisyondan sonra Compose'un bildirdiği sırayla bildirilir. Önce çıkanlar unutulur, en son kompoze edilen ilk: `DisposableEffect` temizlikleri, anahtarı değişen efektlerinkiler de. Sonra girenler kompoze edildikleri sırayla hatırlanır: `DisposableEffect` ve `LaunchedEffect` başlar. Ardından `SideEffect`'ler çalışır. Türü `RememberObserver`'dan türeyen (composable'larınızın yanında bir C++ türü) hatırlanmış bir değere de bildirilir:

```kotlin
void connect();
void disconnect();

struct Connection : RememberObserver {
    void onRemembered() override { connect(); }
    void onForgotten() override { disconnect(); }
};

@Composable
fun Chat() {
    remember { Connection() }  // Chat kompozisyondayken bağlı
    Text("Sohbet")
}
```

Çıkıp geri gelen bir composable sıfırdan başlar: `remember` lambdasını yeniden çalıştırır. Bir değeri bunun ötesinde korumak için bir `SaveableStateHolder`'ın içinde `rememberSaveable` kullanın (gezinme ekranları için bunu yapar) ya da durumu `if`'in üstüne taşıyın.

## Kimlik: bir çağrı nereden geliyor

Hangi hatırlanan değerin hangi çağrıya ait olduğunu bilmek için Kompoz her çağrıyı recomposition'lar boyunca tanımak zorundadır. Compose'un derleyici eklentisi her çağrıya kaynaktaki yerinden bir anahtar verir; kompozc da aynısını C++'ın `std::source_location`'ı ile yapar:

- Her `@Composable fun`, son parametresi `CallSite site = CallSite::current()` olan bir C++ fonksiyonuna dönüşür. Varsayılan, composable'ın *çağrıldığı* yerde değerlendirilir, yani `site` çağrının dosyası, satırı ve sütunudur; kompozc'nin `#line`'ı onları sizin kaynağınızınkiler olarak tutar.
- Bir ebeveynin içinde aynı yerden yapılan çağrılar sıralarıyla ayırt edilir: o yerden yapılan ilk çağrı, ikinci çağrı ve böyle devam eder.
- `remember` de aynı şekilde bir `CallSite` alır; bu yüzden farklı yerlerdeki iki `remember` çağrısı farklı değerlerdir.

`site`'ı hiçbir zaman kendiniz geçirmezsiniz. Bir composable'ı C++'tan çağırırken de bunu varsayılan yapar.

## Scope'lar

Unit dönen bir `@Composable fun`, Compose'daki gibi *yeniden başlatılabilir* ve *atlanabilir* bir scope'tur:

- **Yeniden başlatılabilir** (restartable): okuduğu bir durum değişince ebeveyni değil, yalnız kendisi yeniden çalışır.
- **Atlanabilir** (skippable): ebeveyni çalıştığında argümanları geçen seferkine eşitse çalışmaz.

Bir composable lambda da kendine ait bir scope'tur: `Box`, `Column`, `Row`'un içeriği, bir `Scaffold`'un yuvaları, sizin `content: @Composable () -> Unit`'ınız. Yalnız lambdanın okuduğu değişince lambda tek başına yeniden çalışır. Daha sonra tek başına çalışabileceği için kullandığını Kotlin'in lambdaları gibi değerle yakalar.

```kotlin
@Composable
fun Section(title: String, content: @Composable () -> Unit) {
    Column(modifier = Modifier.padding(16.dp)) {
        Text(title, style = MaterialTheme.typography.titleMedium)
        content()
    }
}
```

Bir değer döndüren `@Composable fun` (`rememberSomething(): T`), Compose'daki gibi bir scope değildir: okudukları onu çağırana aittir.

## Atlamak

Bir scope; ebeveyni çalıştığında, argümanları önceki çağrıya eşitse ve okuduğu hiçbir şey o zamandan beri değişmemişse atlanır. Bunun için argümanların `==`'ı olması gerekir:

- Sayılar, string'ler, enum'lar ve data class'lar değerle karşılaştırılır; `operator==`'ı olan (`= default` yeter) C++ struct'ları da öyle.
- Bir durum kimlikle karşılaştırılır: değeri değişmiş olsa bile aynı durum eşittir. Scope yine de değer değişince yeniden kompoze edilir, çünkü onu okumuştur.
- Bir `Modifier` öğelerinin değerleriyle karşılaştırılır.
- Bir fonksiyon (örneğin `onClick` olarak verilen bir lambda) karşılaştırılamaz. Bir tane alan composable yeniden başlatılabilir ama hiçbir zaman atlanmaz; `==`'ı olmayan bir şeyin listesini alan da öyle.

```kotlin
data class Todo(val id: Int, val title: String, val done: Boolean = false)

@Composable
fun TodoRow(todo: Todo, todos: MutableState<List<Todo>>) {  // todo ve todos aynı kaldıkça atlanır
    Row(verticalAlignment = Alignment.CenterVertically) {
        Checkbox(checked = todo.done, onCheckedChange = { done ->
            todos.value = todos.value.map { if (it.id == todo.id) it.copy(done = done) else it }
        })
        Text(todo.title)
    }
}
```

Bir todo değişince liste yeniden kompoze edilir, diğer her `TodoRow` atlanır.

`Text` de atlanabilir. İçerik yerleştiren bileşenler, çağıranları çalışınca onunla birlikte çalışır; bu ucuzdur, çünkü yerleştirdikleri şey ancak girdileri değişince değişir.

## Döngülerdeki çağrılar

Bir döngünün içinde her yineleme aynı yerden çağırır, bu yüzden çağrılar sıralarıyla tanınır. Liste yalnız sonundan büyüdükçe bu işler. Öğeler araya eklendiğinde, kaldırıldığında ya da yeniden sıralandığında durum öğede değil, *konumda* kalırdı:

```kotlin nocheck
for (todo in todos.value) {
    TodoRow(todo, todos)   // başa ekleyin, her satırın hatırlanan durumu bir kayar
}
```

`key` her çağrıya sizin seçtiğiniz bir kimlik verir:

```kotlin
data class Todo(val id: Int, val title: String, val done: Boolean = false)

@Composable
fun TodoRow(todo: Todo, todos: MutableState<List<Todo>>) {
    Text(todo.title)
}

@Composable
fun TodoList(todos: MutableState<List<Todo>>) {
    Column {
        for (todo in todos.value) {
            key(todo.id) { TodoRow(todo, todos) }
        }
    }
}
```

Artık her satırın hatırlanan değerleri, efektleri ve düğümleri onun `id`'sini izler. Tembel listeler de aynı nedenle `items` çağrılarında anahtar alır (bkz. [Listeler ve ızgaralar](lists.md)).

## İçeriği taşımak

Aynı composable'ı ağaçta başka bir yerden çağırmak başka bir çağrıdır: durumu baştan başlar. `movableContentOf`, Compose'daki gibi durumu kendisiyle birlikte taşınan içerik yapar:

```kotlin
@Composable
fun Player(wide: Boolean) {
    val video = remember {
        movableContentOf {
            var position by remember { mutableStateOf(0) }   // taşınınca korunur
            Text("Oynatılıyor: $position")
        }
    }

    if (wide) {
        Row { video(); Text("Yorumlar") }
    } else {
        Column { video(); Text("Yorumlar") }
    }
}
```

`wide` değişince içeriğin hatırlanan değerleri, düğümleri ve çalışan efektleri yeniden yapılmak yerine sütundan satıra taşınır.

## Ayrıca bakın

- [Durum](state.md): `remember`, `rememberSaveable` ve durum tutucular.
- [Yan etkiler](side-effects.md): bir composable girince başlayan ve çıkınca duran efektler.
- [Aşamalar](phases.md): kompozisyondan sonra ne olur.
- [C++ ile yazmak](cpp.md): kompozc'nin yazdığı scope'lar, `composable(site, …)` ve `ComposableScope`, doğrudan C++ ile.
