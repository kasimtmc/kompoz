# Compose'dan geliyorsanız

Jetpack Compose'u biliyorsanız Kompoz'u zaten biliyorsunuz: aynı model, aynı adlar ve aynı yazım. Bir composable, Compose'un kendi yazımıyla bir `.cpp` dosyasına yazılan bir `@Composable fun`'dır:

```kotlin
@Composable
fun Greeting(name: String, modifier: Modifier = Modifier) {
    Text("Merhaba, $name!", modifier = modifier.padding(16.dp), style = MaterialTheme.typography.titleLarge)
}
```

Kompoz, Material 3'ün ölçülerine ve Android'in fling eğrisine kadar Compose'un davranışını yakından izler. Yeni olan yalnızca kodun durduğu yerdir: C++ kaynaklarında, C++'ın yanında.

## Nasıl çalışır

Compose'un derleyici eklentisi Kotlin'i Kotlin derleyicisi görmeden önce yeniden yazar; Kompoz'un derleyicisi kompozc da aynısını C++ için yapar. `kompoz_compose`'a verilen bir hedefin kaynakları, C++ derleyicisi görmeden önce yeniden yazılır:

```cmake
add_executable(app main.cpp screens.cpp)
target_link_libraries(app PRIVATE kompoz::app)
kompoz_compose(app)
```

Her kaynakta bir `@Composable fun` (ve bir `data class`) Kotlin'dir; dosyadaki geri kalan her şey C++'tır ve olduğu gibi geçer. C++ derleyicisinin iletileri sizin kendi satırlarınızı gösterir. Bir hedefin kaynakları, Kotlin'deki gibi tek bir modüldür: bir dosyanın composable'ı başka bir dosyadan adlı argümanlarla çağrılır, başlık gerekmez.

Uygulamanın giriş noktası C++'tır; composable'larınızın çevresine yazdığınız her şey de öyle:

```kotlin continued
@Composable
fun App() {
    MaterialTheme(colorScheme = lightColorScheme()) {
        Greeting("Kompoz")
    }
}

kompoz::Application kompoz::createApplication(int, char**) {
    return {.title = "Merhaba", .content = [] { App(); }};
}
```

## Kotlin'deki gibi okunanlar

Argümanlar adla, istenen sırayla, varsayılanlar yazılmadan, sonuncusu için sondaki lambda; Kompoz'un composable'ları için de sizinkiler için de:

```kotlin
@Composable
fun Labeled(label: String, value: Int = 0, onClick: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text("$label: $value", modifier = Modifier.weight(1f))
        TextButton(onClick = onClick) { Text("Daha fazla") }
    }
}

@Composable
fun Screen() {
    Column(modifier = Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Labeled(label = "İletiler", value = 3) { println("daha fazla") }
        Labeled("Taslaklar", onClick = {})
    }
}
```

Durum Kotlin'deki gibi devredilir; bir lambdanın parametresi `it` ya da adlıdır; `if` ve `when` değer verir:

```kotlin
@Composable
fun Counter() {
    var count by remember { mutableStateOf(0) }
    var name by remember { mutableStateOf("") }
    Column {
        Text(if (count == 0) "Henüz değil" else "$count kez tıklandı")
        Button(onClick = { count++ }) { Text("Tıkla") }
        TextField(value = name, onValueChange = { name = it }, singleLine = true)
        Text(when {
            name.isBlank() -> "Kimsiniz?"
            name.length < 3 -> "Selam!"
            else -> "Merhaba, ${name.trim()}"
        })
    }
}
```

Listeler ve tembel yerleşimler, Kotlin'in standart işlevleriyle (`listOf`, `filter`, `map`, `joinToString`, `forEach`…):

```kotlin
data class Mail(val from: String, val subject: String, val unread: Boolean = false)

@Composable
fun Inbox(mails: List<Mail>) {
    val unread = mails.filter { it.unread }
    LazyColumn(contentPadding = PaddingValues(16.dp)) {
        item { Text("${unread.size} okunmamış", style = MaterialTheme.typography.titleMedium) }
        items(mails) { mail ->
            ListItem(
                headlineContent = { Text(mail.subject) },
                supportingContent = { Text(mail.from) },
            )
        }
    }
}
```

Bir `data class`'ın `==`'su, `toString()`'i ve `copy(…)`'si vardır; adlı argümanlarla istenen sırayla yapılır (`Mail(subject = "Selam", from = "Ayşe")`).

Eşyordamlar: bir `LaunchedEffect` ya da `launch` bloğu bir eşyordamdır; içindeki `delay`, `animateScrollToItem` ve öteki bekleyen çağrılar beklenir:

```kotlin
@Composable
fun Clock() {
    var seconds by remember { mutableStateOf(0) }
    LaunchedEffect(Unit) {
        while (true) {
            delay(1000)
            seconds++
        }
    }
    val list = rememberLazyListState()
    val scope = rememberCoroutineScope()
    Column {
        Text("$seconds sn")
        Button(onClick = { scope.launch { list.animateScrollToItem(0) } }) { Text("Başa") }
    }
}
```

Gerisi bildiğiniz gibi: `16.dp`, `14.sp`, `Modifier`'dan başlayan değiştirici zincirleri, `Color.Red`, `MaterialTheme.colorScheme.primary`, `LocalContentColor.current`, `CompositionLocalProvider(LocalContentColor provides Color.Red) { … }`, `DisposableEffect(key) { …; onDispose { … } }`, `null`, `?.`, `?:` ve `!!`, aralıklar (`0 until n`, `1..10`, `10 downTo 0 step 2`) ve listeler üzerinde `for`, metin şablonları (sayılar Kotlin'in yazdığı gibi), `CardDefaults.cardColors(containerColor = …)`, `TextFieldDefaults.colors(…)` ve öteki renk nesneleri.

## Composable'larınızın çevresindeki C++

Kotlin'in türleri çevrilen kodda C++ türleridir: `String` `std::string`, `Int` `int`, `Float` `float`, `Boolean` `bool`, `List<T>` `std::vector<T>`, `T?` `std::optional<T>`, bir işlev türü `std::function` (`@Composable () -> Unit`: `Content`). Kendi C++ türleriniz adlarıyla kullanılır; sizin bir yapınız adlı argümanlarla istenen sırayla yapılır, alanları bildirildikleri sırayla atanır:

```kotlin
struct Point {
    float x = 0;
    float y = 0;
};

float distance(Point a, Point b);

@Composable
fun Path(points: List<Point>) {
    val start = Point(y = 4f, x = 2f)
    Text("${points.size} nokta, ilki ${distance(start, points.first())} uzakta")
}
```

C++ işlevleriniz ve üyeleri oldukları gibi, yerleriyle çağrılır. C++ bir composable'ı kompozc'nin ondan yaptığı işlev olarak çağırır: yukarıdaki `Greeting`, `void Greeting(const std::string& name, const Modifier& modifier = Modifier(), CallSite site = CallSite::current())`'tir.

Unit dönen bir `@Composable fun`, derleyici eklentisinin yaptığı gibi yeniden başlatılabilir ve atlanabilirdir: argümanları geçen seferkiyle eşit (`==`) olduğunda atlanır. Bir `data class`'ın `==`'su vardır; bir C++ yapısının ise bir taneye ihtiyacı vardır (`bool operator==(const Point&) const = default;`), yoksa composable'ı her seferinde çağıranıyla birlikte yeniden çalışır.

Lambdalar, Kotlin'inkiler gibi değerleri kopyalayarak yakalar: bir blok daha sonra kendi başına yeniden çalışabilir, bu yüzden hiçbir zaman yok olmuş bir şeye başvuru tutmaz. Bir lambda içinde değiştirilen bir `var`, değişkeni değil lambdanın kopyasını değiştirir: değişeni bir durumda (`mutableStateOf`) tutun, Compose da zaten bunu ister.

## Henüz yok

kompozc, bir composable'ın gerek duyduğu Kotlin'i okur: composable'lar, düz ve eklenti işlevleri, üst düzey `val`'ler, data class'lar, enum class'lar, sınıflar (bir ViewModel, bir depo), `@Serializable` rotalar ve üyeli olanlar da dahil object'ler (`object AppIcons { val Add = … }`: bir uygulamanın tekil nesneleri). Henüz okumadıkları onun yanında C++ ile yazılır: `sealed` sınıflar ve `is`, arayüzler, `suspend fun`, bir sınıfın `by` özellikleri. Editörler böyle dosyaları kompozc'nin dil sunucusuyla okur (bkz. [Araçlar](tools.md#editörler)); derlemenin iletileri de Kotlin'in satırlarını gösterir.

kompozc olmadan Kompoz'un composable'ları C++ ile yazılır: [C++ ile yazmak](cpp.md), Kotlin'in her yapısının orada nasıl okunduğunu gösterir.

## Farklı olanlar

- **Platformlar.** Kompoz her şeyi kendisi çizer: Android'de kendi etkinliğinin bir görünümünde, Linux, Windows ve macOS'ta kendi pencerelerinde, web'de bir tuvalde; birlikte çalışılacak Android View'ları yoktur.
- **Kimlik.** Bir çağrının kimliği, kaynaktaki yeri ve ebeveyninin çağrıları arasındaki sırasıdır; derleyici eklentisinin çıkardığı gibi. Öğeleri yer değiştiren döngülerde `key` kullanın (bkz. [Yaşam döngüsü](lifecycle.md)).
- **API.** Compose'un ve Material 3'ün çoğu aynı adlarla vardır; [API sayfası](api.md) her Compose API'sini Kompoz'daki durumuyla listeler.
