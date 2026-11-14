# Compose'dan geliyorsanız

Jetpack Compose'u biliyorsanız Kompoz'u zaten biliyorsunuz: aynı model, aynı adlar ve aynı yazım. Bir composable, Kotlin yazımıyla bir `.cpp` dosyasına yazılan bir `@Kompozable fun`'dır:

```kotlin
@Kompozable
fun Greeting(name: String, modifier: Modifier = Modifier) {
    Text("Merhaba, $name!", modifier = modifier.padding(16.dp), style = MaterialTheme.typography.titleLarge)
}
```

Compose'un kendi adını taşıyan adları Kompoz'da Kompoz'un adını taşır: `@Kompozable`, Compose'un `@Composable`'ıdır; `KompozitionLocal`, `kompozitionLocalOf`, `staticKompozitionLocalOf` ve `KompozitionLocalProvider`, onun `CompositionLocal` ailesidir; `SubkompozLayout` (`subkompoz(…)` ile) onun `SubcomposeLayout`'udur; `KompozTestRule` onun test kuralıdır; bir `NavHost`'ta `kompozable(route) { }` ve `kompozable<Route> { }`, navigasyonun `composable`'ıdır. Öteki bütün adlar Compose'unkilerdir.

Kompoz, Material 3'ün ölçülerine ve Android'in fling eğrisine kadar Compose'un davranışını yakından izler. Yeni olan yalnızca kodun durduğu yerdir: C++ kaynaklarında, C++'ın yanında.

## Nasıl çalışır

Compose'un derleyici eklentisi Kotlin'i Kotlin derleyicisi görmeden önce yeniden yazar; Kompoz'un derleyicisi kompozc da aynısını C++ için yapar. `kompozable`'a verilen bir hedefin kaynakları, C++ derleyicisi görmeden önce yeniden yazılır:

```cmake
add_executable(app main.cpp screens.cpp)
target_link_libraries(app PRIVATE kompoz::app)
kompozable(app)
```

Her kaynakta bir `@Kompozable fun` (ve bir `data class`) Kotlin'dir; dosyadaki geri kalan her şey C++'tır ve olduğu gibi geçer. Kotlin kısmında noktalı virgül, Kotlin'deki gibi isteğe bağlıdır: bir deyim onunla bitebilir de bitmeyebilir de. C++ derleyicisinin iletileri sizin kendi satırlarınızı gösterir. Bir hedefin kaynakları, Kotlin'deki gibi tek bir modüldür: bir dosyanın composable'ı başka bir dosyadan adlı argümanlarla çağrılır, başlık gerekmez.

Uygulamanın giriş noktası C++'tır; composable'larınızın çevresine yazdığınız her şey de öyle:

```kotlin continued
@Kompozable
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
@Kompozable
fun Labeled(label: String, value: Int = 0, onClick: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text("$label: $value", modifier = Modifier.weight(1f))
        TextButton(onClick = onClick) { Text("Daha fazla") }
    }
}

@Kompozable
fun Screen() {
    Column(modifier = Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Labeled(label = "İletiler", value = 3) { println("daha fazla") }
        Labeled("Taslaklar", onClick = {})
    }
}
```

Durum Kotlin'deki gibi devredilir; bir lambdanın parametresi `it` ya da adlıdır; `if` ve `when` değer verir:

```kotlin
@Kompozable
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

@Kompozable
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

Bir `sealed` sınıf ya da arayüz kapalı bir durumlar kümesidir, örneğin bir ekranın durumu; `is` hangisini tuttuğunu söyler, bir `when` dalında ya da bir `if`'teki `is`'ten sonra değer o durumdur (akıllı dönüşüm). Bütün durumları kapsayan bir `when`'in `else`'e ihtiyacı yoktur; `as` ve `as?` Kotlin'deki gibi dönüştürür:

```kotlin
sealed interface UiState {
    data object Loading : UiState
    data class Ready(val items: List<String>) : UiState
    data class Failed(val message: String) : UiState
}

@Kompozable
fun Screen(state: UiState) {
    when (state) {
        is UiState.Loading -> CircularProgressIndicator()
        is UiState.Ready -> LazyColumn { items(state.items) { Text(it) } }
        is UiState.Failed -> Text("Hata: ${state.message}")
    }
}
```

Arayüz, başka türlerin uyguladığı bir türdür: bir sınıf, bir `ViewModel`, bir data class, bir object, bir `object : Ad { … }` ifadesi. Gövdesiz işlevleri ve özellikleri soyuttur; onun bir değeri, yapıldığı nesnenin kendisidir (bir kopyası da aynı nesnedir), `is` ve `as` hangisi olduğunu söyler. Bir `fun interface` bir lambdadan da yapılır (`Check { it.isNotEmpty() }`):

```kotlin
interface Repository {
    val name: String
    fun items(): List<String>
    fun describe() = "$name: ${items().size}"
}

class FakeRepository(override val name: String = "sahte") : Repository {
    override fun items() = listOf("a", "b")
}

@Kompozable
fun Items(repo: Repository) {
    Text(repo.describe())
}
```

Bir sınıfın özelliği, bir composable'ın yereli gibi bir temsilciye bağlanabilir (`by`), kendi `get()` ve `set(value)`'su da olabilir. `private set` ile `var count by mutableStateOf(0)`, sınıfın değiştirdiği ve herkesin okuduğu bir durumdur; `by derivedStateOf { … }` ve `by lazy { … }` Kotlin'deki gibi çalışır:

```kotlin
class CounterViewModel : ViewModel() {
    var count by mutableStateOf(0)
        private set
    val isEven get() = count % 2 == 0

    fun increment() { count++ }
}
```

Sınıflar açık (`open`) ya da soyut (`abstract`) olabilir ve birbirinden türeyebilir (`override`, `super.describe()`). Bir data class'ın gövdesi olabilir: işlevler, alanlarından hesaplanan özellikler, `init`; `==`, `toString()` ve `copy`, Kotlin'deki gibi alanlarınınkidir. Sınıflar, data class'lar, arayüzler ve işlevler tür parametreli olabilir; tür argümanları yazılır ya da değerlerden çıkarılır:

```kotlin
abstract class Tile(val name: String) {
    abstract fun area(): Float
    open fun describe() = "$name: ${area()}"
}

class SquareTile(val side: Float) : Tile("kare") {
    override fun area() = side * side
}

data class Tagged<T>(val tag: String, val value: T) {
    fun show() = "$tag=$value"
}

fun <T, R> mapAll(items: List<T>, transform: (T) -> R): List<R> = items.map { transform(it) }

@Kompozable
fun Tiles() {
    Text(SquareTile(2f).describe() + " " + Tagged("n", 3).show() + " " + mapAll(listOf(1, 2)) { it * 10 })
}
```

Kurucusu ve gövdesi olan enum class'lar, `companion object`'ler, `operator fun`'lar, eklenti özellikleri, `typealias`, `vararg` ve `lateinit` Kotlin'deki gibi okunur; sınıfların içindeki sınıflar da:

```kotlin
enum class Tier(val rank: Int) {
    LOW(1), HIGH(9);
    fun next() = if (this == LOW) HIGH else LOW
}

data class Coins(val cents: Int) {
    operator fun plus(other: Coins) = Coins(cents + other.cents)
    companion object {
        val Zero = Coins(0)
    }
}

val Coins.lira get() = cents / 100

@Kompozable
fun Wallet() {
    val total = Coins(150) + Coins(250)
    Text("${Tier.LOW.next()} ${total.lira} ${Coins.Zero} ${Tier.entries.size}")
}
```

`try` bir ifadedir, `throw` da; Kotlin'in istisnaları ve onlardan türeyen kendi sınıflarınızla. İşlevler aynı adı paylaşabilir (aşırı yükleme); bir `inner class` dış nesnesini tutar; bir enum girdisinin kendi gövdesi olabilir:

```kotlin
class Refused(val code: Int) : IllegalStateException("refused $code")

fun parsed(text: String): Int = try { text.toInt() } catch (e: NumberFormatException) { -1 }
fun parsed(text: String, fallback: Int): Int = if (parsed(text) < 0) fallback else parsed(text)
fun checked(n: Int): Int = if (n >= 0) n else throw Refused(n)

enum class Op {
    PLUS { override fun apply(a: Int, b: Int) = a + b },
    TIMES { override fun apply(a: Int, b: Int) = a * b };
    abstract fun apply(a: Int, b: Int): Int
}

class Basket {
    var count by mutableStateOf(0)
    inner class Adder(val step: Int) {
        fun add() { count += step }
    }
}

@Kompozable
fun Checkout() {
    val basket = remember { Basket() }
    val code = try { checked(-3) } catch (e: Refused) { e.code }
    Text("${parsed("12")} ${parsed("x", 0)} ${Op.TIMES.apply(3, 4)} $code ${basket.count}")
    Button(onClick = { basket.Adder(2).add() }) { Text("Ekle") }
}
```

Bir sealed sınıfın kurucusu ve üyeleri olabilir; durumları kurucusuna değer verir ve üyelerine sahip olur. Üst düzey bir `var`, temsilcili olanı da, Kotlin'deki gibi okunur:

```kotlin
sealed class Screen(val route: String) {
    object Home : Screen("home")
    data class Detail(val id: Int) : Screen("detail/$id")
    fun label() = route.uppercase()
}

var opened by mutableStateOf(0)

@Kompozable
fun Router(screen: Screen) {
    Text("${screen.label()} $opened")
    Button(onClick = { opened++ }) { Text("Aç") }
}
```

Eşyordamlar: bir `LaunchedEffect` ya da `launch` bloğu bir eşyordamdır; içindeki `delay`, `animateScrollToItem`, sizin `suspend fun`'larınız ve öteki bekleyen çağrılar beklenir:

```kotlin
@Kompozable
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

Gerisi bildiğiniz gibi: `16.dp`, `14.sp`, `Modifier`'dan başlayan değiştirici zincirleri, `Color.Red`, `MaterialTheme.colorScheme.primary`, `LocalContentColor.current`, `KompozitionLocalProvider(LocalContentColor provides Color.Red) { … }`, `DisposableEffect(key) { …; onDispose { … } }`, `null`, `?.`, `?:` ve `!!`, aralıklar (`0 until n`, `1..10`, `10 downTo 0 step 2`) ve listeler üzerinde `for`, metin şablonları (sayılar Kotlin'in yazdığı gibi), `CardDefaults.cardColors(containerColor = …)`, `TextFieldDefaults.colors(…)` ve öteki renk nesneleri.

## Composable'larınızın çevresindeki C++

Kotlin'in türleri çevrilen kodda C++ türleridir: `String` `std::string`, `Int` `int`, `Float` `float`, `Boolean` `bool`, `List<T>` `std::vector<T>`, `T?` `std::optional<T>`, bir işlev türü `std::function` (`@Kompozable () -> Unit`: `Content`). Kendi C++ türleriniz adlarıyla kullanılır; sizin bir yapınız adlı argümanlarla istenen sırayla yapılır, alanları bildirildikleri sırayla atanır:

```kotlin
struct Point {
    float x = 0;
    float y = 0;
};

float distance(Point a, Point b);

@Kompozable
fun Path(points: List<Point>) {
    val start = Point(y = 4f, x = 2f)
    Text("${points.size} nokta, ilki ${distance(start, points.first())} uzakta")
}
```

C++ işlevleriniz ve üyeleri oldukları gibi, yerleriyle çağrılır. C++ bir composable'ı kompozc'nin ondan yaptığı işlev olarak çağırır: yukarıdaki `Greeting`, `void Greeting(const std::string& name, const Modifier& modifier = Modifier(), CallSite site = CallSite::current())`'tir.

Unit dönen bir `@Kompozable fun`, derleyici eklentisinin yaptığı gibi yeniden başlatılabilir ve atlanabilirdir: argümanları geçen seferkiyle eşit (`==`) olduğunda atlanır. Bir `data class`'ın `==`'su vardır; bir C++ yapısının ise bir taneye ihtiyacı vardır (`bool operator==(const Point&) const = default;`), yoksa composable'ı her seferinde çağıranıyla birlikte yeniden çalışır.

Lambdalar, Kotlin'inkiler gibi değerleri kopyalayarak yakalar: bir blok daha sonra kendi başına yeniden çalışabilir, bu yüzden hiçbir zaman yok olmuş bir şeye başvuru tutmaz. Bir lambda içinde değiştirilen bir `var`, değişkeni değil lambdanın kopyasını değiştirir: değişeni bir durumda (`mutableStateOf`) tutun, Compose da zaten bunu ister.

İfadeler Kotlin'deki gibi soldan sağa değerlendirilir: bir çağrının argümanları yazıldıkları sırayla (adlı olanlar da), bir metin şablonunun parçaları, bir işlecin işlenenleri. C++ bunların sırasını belirlemez; kompozc sıranın önemli olabileceği yerde onları sıralar, `listOf(next(), next())`'te ilk `next()` önce çalışır.

## kompozc'nin okudukları

kompozc, bir composable'ın gerek duyduğu Kotlin'i okur: `vararg`'lı düz, eklenti, tür parametreli, `operator` ve `suspend` işlevler (aynı adlı olanlar da), üst düzey `val`'ler ve `var`'lar (temsilcili olanlar da: `by lazy`, `by mutableStateOf`) ve eklenti özellikleri, `typealias`, data class'lar (gövdeleriyle de), enum class'lar (kurucu ve gövdeleriyle, girdilerinin kendi gövdeleriyle de), temsilcili (`by`), `lateinit` ve erişimcili özellikleri ve `companion object`'leriyle sınıflar (açık, soyut, tür parametreli; bir ViewModel, bir depo), arayüzler (tür parametreli de, `suspend fun`'lı `fun interface`'ler de), `sealed` sınıflar ile arayüzler (kurucu ve üyeleriyle; durumları data class, object ya da sınıf), `@Serializable` rotalar, üyeli olanlar da dahil object'ler (`object AppIcons { val Add = … }`: bir uygulamanın tekil nesneleri), sınıfların içinde bu bildirimler ve inner class'lar, Kotlin'in istisnalarıyla ifade olarak `try` ve `throw`. Okumadığı (bunu Kotlin'in satırında söyler) onun yanında C++ ile yazılır. Editörler böyle dosyaları kompozc'nin dil sunucusuyla okur (bkz. [Araçlar](tools.md#editörler)); derlemenin iletileri de Kotlin'in satırlarını gösterir.

kompozc olmadan Kompoz'un composable'ları C++ ile yazılır: [C++ ile yazmak](cpp.md), Kotlin'in her yapısının orada nasıl okunduğunu gösterir.

## Farklı olanlar

- **Platformlar.** Kompoz her şeyi kendisi çizer: Android'de kendi etkinliğinin bir görünümünde, Linux, Windows ve macOS'ta kendi pencerelerinde, web'de bir tuvalde; birlikte çalışılacak Android View'ları yoktur.
- **Kimlik.** Bir çağrının kimliği, kaynaktaki yeri ve ebeveyninin çağrıları arasındaki sırasıdır; derleyici eklentisinin çıkardığı gibi. Öğeleri yer değiştiren döngülerde `key` kullanın (bkz. [Yaşam döngüsü](lifecycle.md)).
- **API.** Compose'un ve Material 3'ün çoğu aynı adlarla vardır; [API sayfası](api.md) her Compose API'sini Kompoz'daki durumuyla listeler.
