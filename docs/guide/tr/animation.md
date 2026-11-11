# Animasyon

Kompoz'da Compose'un animasyon API'leri var: bir hedefe doğru canlanan değerler, canlanarak girip çıkan içerik, birkaç değerin birlikte geçişleri ve hareketler ile coroutine'ler için alt düzey `Animatable`.

## API seçmek

| Ne istiyorsunuz | Kullanın |
|---|---|
| Hedefi değiştiğinde tek bir değeri canlandırmak | `animateFloatAsState`, `animateColorAsState` |
| İçeriği bir animasyonla göstermek ya da gizlemek | `AnimatedVisibility` |
| İçerik parçaları arasında geçiş yapmak | `AnimatedContent`, `Crossfade` |
| Bir boyut değişikliğini canlandırmak | `Modifier.animateContentSize()` |
| Tek bir durumdan birkaç değeri canlandırmak | `updateTransition` |
| Sonsuza kadar canlandırmak (nabızlar, dönen göstergeler) | `rememberInfiniteTransition()` |
| Bir animasyonu bir coroutine'den ya da bir hareketten yönetmek | `Animatable` |
| Bir öğeyi bir ekrandan ötekine taşımak | `SharedTransitionLayout` ve `sharedElement` |

## Bir değeri canlandırmak

`animate*AsState`, değeri hedefi her değiştiğinde o hedefe doğru canlanan bir durum döndürür (`AnimatedState<T>`, Compose'taki `State<T>`):

```kotlin
@Kompozable
fun LikeButton() {
    var liked by remember { mutableStateOf(false) }
    val scale by animateFloatAsState(if (liked) 1.2f else 1f, spring(Spring.DampingRatioMediumBouncy), label = "scale")
    val tint by animateColorAsState(if (liked) Color(0xFFE53935) else MaterialTheme.colorScheme.onSurfaceVariant, tween(300))

    IconButton(onClick = { liked = !liked }) {
        Icon(if (liked) Icons.Filled.Favorite else Icons.Filled.FavoriteBorder, contentDescription = "Like",
             modifier = Modifier.graphicsLayer { scaleX = scale; scaleY = scale }, tint = tint)
    }
}
```

Compose'taki gibi, değerin nerede okunduğu onu neyin izleyeceğini belirler. Bir composable'da değer olarak kullanılırsa (`val tint by …` ya da orada `.value`), animasyonun her karesinde o scope yeniden kompoze edilir, ebeveynleri değil. Çizim sırasında `.value` ile okunursa (`drawBehind`, `graphicsLayer { }`, bir `Canvas`) yalnızca o düğüm yeniden çizilir (çizdiklerinden hiçbiri görünmezken, kaydırılıp görüş dışına çıkmışken, kare istemez: geri geldiğinde zamanının getirdiği yerden sürer); yerleşim sırasında okunursa (`offset { }`, bir `layout`) yalnızca o yeniden yerleştirilir (bkz. [Aşamalar](phases.md#durum-okumalarını-ertelemek)). Hiçbir yerde okunmayan bir animasyon kare istemez. `targetValue` ve `isRunning` nereye gittiğini ve yolda olup olmadığını söyler. Bir composition local'e de böyle bir durum verilebilir, `LocalContentColor provides color`: o zaman onu sağlayan composable değil, yalnızca local'i okuyanlar onu izler.

İkinci argüman bir animasyon tanımıdır (`animationSpec`) ya da, Kompoz'a özgü, bir tween için milisaniye cinsinden bir süre; `label`, Compose'un araçları için verdiği ad.

Bir renk Compose'unki gibi canlanır (`Color.VectorConverter`): Oklab'da, açıklığı ve tonu boyunca; böylece siyahtan beyaza yarı yolda göze orta bir gridir (`0x636363`) ve aşan bir yay orada hedefinin ötesine geçer.

## Animasyon tanımları

| Tanım | Davranış |
|---|---|
| `spring(dampingRatio, stiffness)` | Fiziğe dayalı. Varsayılandır; hedef yolun ortasında değişirse hızını korur. `Spring.DampingRatioMediumBouncy`, `Spring.StiffnessLow` ve diğer sabitler Compose'unkilerdir. |
| `tween(durationMillis, delayMillis, easing)` | Bir yumuşatma eğrisi boyunca sabit bir süre: `FastOutSlowInEasing` (varsayılan), `LinearOutSlowInEasing`, `FastOutLinearInEasing`, `LinearEasing`. |
| `keyframes { durationMillis = 400; 1.3f at 100 using LinearEasing; 0.9f at 250 }` | Belirli zamanlarda belirli değerler, her biri oradan başlayan parçanın yumuşatmasıyla (`using`), Compose'un kurucusu gibi. |
| `repeatable(iterations, spec, mode)`, `infiniteRepeatable(spec, mode)` | Baştan başlayarak ya da tersine dönerek (`RepeatMode.Reverse`) tekrarlanan bir tanım. |
| `snap(delayMillis)` | Doğrudan hedefe atlar. |

Material bileşenleri `MaterialTheme.motionScheme`'in yaylarını kullanır; böylece Material 3'ün (ve `MaterialExpressiveTheme` ile M3 Expressive'in) söylediği gibi hareket ederler.

## AnimatedVisibility

`AnimatedVisibility`, `visible` true olduğunda içeriğini canlandırarak içeri, false olduğunda dışarı alır. Gizlendikten sonra içerik kompozisyondan çıkar.

```kotlin
@Kompozable
fun Details() {
    var open by remember { mutableStateOf(false) }
    Column {
        TextButton(onClick = { open = !open }) { Text(if (open) "Less" else "More") }
        AnimatedVisibility(visible = open, enter = fadeIn() + expandVertically(), exit = fadeOut() + shrinkVertically()) {
            Text("Here are the details.", modifier = Modifier.padding(16.dp))
        }
    }
}
```

Giriş geçişleri `fadeIn`, `slideInHorizontally`, `slideInVertically`, `scaleIn`, `expandIn`, `expandHorizontally` ve `expandVertically`'dir; çıkış geçişleri bunların karşılıkları olan `fadeOut`, `slideOut…`, `scaleOut` ve `shrink…`'tir. Bunları `+` ile birleştirin; kaymalar Compose'un tam boyut lambda'sını alır (`slideInVertically { it / 2 }`). `EnterTransition.None` ve `ExitTransition.None` hiçbir şeyi canlandırmaz; çocuklar kendilerini içeriğin `AnimatedVisibilityScope`'undan `animateEnterExit` ile canlandırdığında kullanılır.

Her geçiş belirtimini adıyla da alır (`fadeIn(animationSpec = tween(300))`, `slideInVertically(animationSpec = tween(450)) { it }`, `scaleOut(targetScale = 0.5f)`). İlk göründüğünde içeri gelmesi gereken bir satıra, Compose'un `visibleState`'i gibi, gizli yapılıp hedefi görünür olan bir `MutableTransitionState` verilir; `currentState` ve `isIdle` nerede olduğunu söyler:

```kotlin
@Kompozable
fun Arriving() {
    val shown = remember { MutableTransitionState(false).apply { targetState = true } }
    AnimatedVisibility(visibleState = shown, enter = fadeIn(animationSpec = tween(500)) + expandVertically()) {
        Box(modifier = Modifier.fillMaxWidth().height(48.dp)) {
            Box(modifier = Modifier.animateEnterExit(enter = slideInVertically { it }, exit = fadeOut()).matchParentSize())
            IconButton(onClick = { shown.targetState = false }) { Icon(Icons.Filled.Close, contentDescription = "Kaldır") }
        }
    }
    if (shown.isIdle && !shown.currentState) Text("Kaldırıldı")
}
```

Compose'taki gibi bir geçiş, içeriği yerleştirilip çizilirken okunur: çevresindeki composable geçiş başladığında ve bittiğinde yeniden oluşturulur, aradaki karelerde değil.

## AnimatedContent ve Crossfade

`AnimatedContent`, hedef durumu değiştiğinde içerikler arasında canlandırır: eski içerik çıkarken yenisi girer ve boyut ikisi arasında canlanır.

```kotlin
@Kompozable
fun Counter() {
    var count by remember { mutableStateOf(0) }
    Row(verticalAlignment = Alignment.CenterVertically) {
        IconButton(onClick = { count-- }) { Icon(Icons.Filled.Remove, contentDescription = "Less") }
        AnimatedContent(
            targetState = count,
            transitionSpec = {
                // yukarı sayarken yukarı, aşağı sayarken aşağı
                if (targetState > initialState) slideInVertically { it } togetherWith slideOutVertically { -it }
                else slideInVertically { -it } togetherWith slideOutVertically { it }
            },
            label = "count",
        ) { n ->
            Text("$n", style = MaterialTheme.typography.headlineMedium)
        }
        IconButton(onClick = { count++ }) { Icon(Icons.Filled.Add, contentDescription = "More") }
    }
}
```

İçerik lambda'sı gösterdiği durumu alır; çıkan içerik için bu eski durumdur: durum değişkeninden değil, bu argümandan çizin. `enter togetherWith exit` iki geçişi eşleştirir; ardından `using SizeTransform(clip = false)` boyutun nasıl canlandığını değiştirir. `transitionSpec` lambda'sı Compose'unki gibi `initialState` ve `targetState`'i okur.

`Crossfade` basit durumdur: içerikler arasında yumuşak geçiş yapar.

```kotlin
enum class Page { Home, Settings }

@Kompozable
fun PageContent(page: Page) {
    Crossfade(targetState = page, label = "page") { p ->
        when (p) {
            Page.Home -> Text("Home")
            Page.Settings -> Text("Settings")
        }
    }
}
```

## animateContentSize

`Modifier.animateContentSize()`, içeriği bir düğümü büyüttüğünde ya da küçülttüğünde düğümün boyutunu canlandırır:

```kotlin
@Kompozable
fun Expandable(text: String) {
    var expanded by remember { mutableStateOf(false) }
    Text(text, modifier = Modifier.clickable { expanded = !expanded }.animateContentSize(),
         maxLines = if (expanded) Int.MAX_VALUE else 2, overflow = TextOverflow.Ellipsis)
}
```

Boyut değişikliğini görebilmesi için zincirde boyut modifier'larından önce koyun.

## updateTransition

Birkaç değer tek bir duruma doğru birlikte canlandığında bir geçiş onları aynı adımda tutar ve hepsinin ne zaman vardığını söyler:

```kotlin
enum class BoxState { Collapsed, Expanded }

@Kompozable
fun TransitionBox() {
    var state by remember { mutableStateOf(BoxState.Collapsed) }
    val transition = updateTransition(targetState = state, label = "box")
    val size by transition.animateDp(label = "size") { if (it == BoxState.Expanded) 200.dp else 80.dp }
    val color by transition.animateColor(label = "color") {
        if (it == BoxState.Expanded) Color(0xFF6750A4) else Color(0xFFB58392)
    }
    Box(modifier = Modifier.size(size).background(color, RoundedCornerShape(16.dp)).clickable {
        state = if (state == BoxState.Expanded) BoxState.Collapsed else BoxState.Expanded
    })
}
```

`transition.currentState`, her değer vardığında `transition.targetState`'e yetişir; `transition.isRunning` hâlâ hareket eden bir değer olup olmadığını söyler. `animateDp` burada `animateFloat`'tır: bir dp bir float'tır.

## Sonsuz animasyonlar

`rememberInfiniteTransition()`, değerleri kompozisyonda kaldıkları sürece canlandırır:

```kotlin
@Kompozable
fun Pulse() {
    val pulse by rememberInfiniteTransition(label = "pulse").animateFloat(
        initialValue = 0.85f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(800), RepeatMode.Reverse), label = "scale")
    Box(modifier = Modifier.size(64.dp)
            .graphicsLayer { scaleX = pulse; scaleY = pulse }
            .background(MaterialTheme.colorScheme.primary, CircleShape))
}
```

`animateFloat` ve `animateColor`, Compose'unkiler gibi, gereken yerde okunacak bir durum döndürür: buradaki gibi çizim sırasında okunursa her karede yalnızca yeniden çizilir; bir kompozisyonda okunursa (ya da `float` olarak alınırsa) okuyan her karede yeniden kompoze edilir.

## Animatable

`Animatable`, coroutine'lerden canlandırdığınız bir değerdir: `animateTo` animasyon bitene kadar askıya alır, `snapTo` atlar, `stop` durdurur. Yeni bir `animateTo`, çalışan birinden hızını koruyarak devralır; bu da onu hareketlerle yönetilen animasyonların aracı yapar:

```kotlin
@Kompozable
fun Shake(error: Boolean) {
    val offset = remember { Animatable(0f) }
    LaunchedEffect(error) {
        if (error) {
            for (x in listOf(-12f, 12f, -8f, 8f, 0f)) offset.animateTo(x, tween(50))
        }
    }
    Text("Wrong password", modifier = Modifier.offset { IntOffset(offset.value.roundToInt(), 0) },
         color = MaterialTheme.colorScheme.error)
}
```

- `animateTo(target, spec, initialVelocity)` bir fırlatmanın hızından başlar; böylece bırakılan bir sürükleme doğal biçimde yaylanarak devam eder.
- `animateDecay(velocity, splineBasedDecay())` bir hızdan başlayıp durana kadar yavaşlar, bir fırlatma gibi.
- `updateBounds(lower, upper)` onu sınırlar içinde tutar.
- `value` gözlemlenebilirdir; akıcı hareket için onu yerleşim ya da çizim sırasında okuyun.

Sürüklemeyle birleştirmek için bkz. [Hareketler](gestures.md).

## Paylaşılan öğeler

Paylaşılan bir öğe, Compose'daki gibi bir içerikteki yerinden ötekindeki yerine taşınır ve yeniden boyutlanır:

```kotlin
@Kompozable
fun Gallery() {
    var details by remember { mutableStateOf(false) }
    SharedTransitionLayout(modifier = Modifier.fillMaxSize()) {
        AnimatedContent(targetState = details, label = "photo") { open ->
            Box(modifier = Modifier
                    .size(if (open) 280.dp else 72.dp)
                    .sharedElement(rememberSharedContentState(key = "photo"), animatedVisibilityScope = this@AnimatedContent)
                    .background(MaterialTheme.colorScheme.tertiary, RoundedCornerShape(16.dp))
                    .clickable { details = !details })
        }
    }
}
```

Çıkan ve giren içerikte aynı anahtara sahip öğeler, ikisi arasında canlanan ve her şeyin üstünde çizilen tek bir öğe olur. `sharedBounds`, içerikleri farklı olan öğeler içindir: ikisi de hareket eden sınırların içinde, birbirine geçerek gösterilir. Gezinmeyle birlikte paylaşılan öğeler ekranlar arasında taşınır.

## Ayrıca bakın

- [Aşamalar](phases.md): yeniden kompoze etmeden canlandırmak.
- [Hareketler](gestures.md): parmağı izleyen animasyonlar.
- [Grafik](graphics.md): canlandırılmış dönüşümler için `graphicsLayer`.
