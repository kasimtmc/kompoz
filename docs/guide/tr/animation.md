# Animasyon

Kompoz'da Compose'un animasyon API'leri var: bir hedefe doğru canlanan değerler, canlanarak girip çıkan içerik, birkaç değerin birlikte geçişleri ve hareketler ile coroutine'ler için alt düzey `Animatable`.

## API seçmek

| Ne istiyorsunuz | Kullanın |
|---|---|
| Hedefi değiştiğinde tek bir değeri canlandırmak | `animateFloatAsState`, `animateColorAsState` |
| İçeriği bir animasyonla göstermek ya da gizlemek | `AnimatedVisibility` |
| İçerik parçaları arasında geçiş yapmak | `AnimatedContent`, `Crossfade` |
| Bir boyut değişikliğini canlandırmak | `Modifier().animateContentSize()` |
| Tek bir durumdan birkaç değeri canlandırmak | `updateTransition` |
| Sonsuza kadar canlandırmak (nabızlar, dönen göstergeler) | `rememberInfiniteTransition()` |
| Bir animasyonu bir coroutine'den ya da bir hareketten yönetmek | `Animatable` |
| Bir öğeyi bir ekrandan ötekine taşımak | `SharedTransitionLayout` ve `sharedElement` |

## Bir değeri canlandırmak

`animate*AsState`, değeri hedefi her değiştiğinde o hedefe doğru canlanan bir durum döndürür (`AnimatedState<T>`, Compose'taki `State<T>`):

```cpp
void LikeButton() {
    auto liked = remember([] { return mutableStateOf(false); });
    auto scale = animateFloatAsState(liked.get() ? 1.2f : 1.f, spring(Spring::DampingRatioMediumBouncy));
    Color tint = animateColorAsState(liked.get() ? Color::hex(0xE53935) : colorScheme().onSurfaceVariant, 300);

    IconButton([liked] { liked.set(!liked.get()); }, [&] {
        Icon(liked.get() ? Icons::Filled::Favorite : Icons::Filled::FavoriteBorder, "Like",
             Modifier().graphicsLayer([scale](GraphicsLayerScope& l) { l.scaleX = l.scaleY = scale.value(); }), tint);
    });
}
```

Compose'taki gibi, değerin nerede okunduğu onu neyin izleyeceğini belirler. Bir composable'da değer olarak kullanılırsa (`Color tint = ...` ya da orada `.value()`), animasyonun her karesinde o scope yeniden kompoze edilir, ebeveynleri değil. Çizim sırasında `.value()` ile okunursa (`drawBehind`, `graphicsLayer { }`, bir `Canvas`) yalnızca o düğüm yeniden çizilir; yerleşim sırasında okunursa (`offset { }`, bir `layout`) yalnızca o yeniden yerleştirilir (bkz. [Aşamalar](phases.md#durum-okumalarını-ertelemek)). Hiçbir yerde okunmayan bir animasyon kare istemez. `targetValue()` ve `isRunning()` nereye gittiğini ve yolda olup olmadığını söyler. Bir composition local'e de böyle bir durum verilebilir, `LocalContentColor.provides(color)`: o zaman onu sağlayan composable değil, yalnızca local'i okuyanlar onu izler.

İkinci argüman bir animasyon tanımıdır ya da bir tween için milisaniye cinsinden bir süre.

Bir renk Compose'unki gibi canlanır (`Color.VectorConverter`): Oklab'da, açıklığı ve tonu boyunca; böylece siyahtan beyaza yarı yolda göze orta bir gridir (`0x636363`) ve aşan bir yay orada hedefinin ötesine geçer.

## Animasyon tanımları

| Tanım | Davranış |
|---|---|
| `spring(dampingRatio, stiffness)` | Fiziğe dayalı. Varsayılandır; hedef yolun ortasında değişirse hızını korur. `Spring::DampingRatioMediumBouncy`, `Spring::StiffnessLow` ve diğer sabitler Compose'unkilerdir. |
| `tween(durationMillis, delayMillis, easing)` | Bir yumuşatma eğrisi boyunca sabit bir süre: `FastOutSlowInEasing` (varsayılan), `LinearOutSlowInEasing`, `FastOutLinearInEasing`, `LinearEasing`. |
| `keyframes(durationMillis, {{atMillis, value}, …})` | Belirli zamanlarda belirli değerler. |
| `repeatable(iterations, spec, mode)`, `infiniteRepeatable(spec, mode)` | Baştan başlayarak ya da tersine dönerek (`RepeatMode::Reverse`) tekrarlanan bir tanım. |
| `snap(delayMillis)` | Doğrudan hedefe atlar. |

Material bileşenleri `motionScheme()`'in yaylarını kullanır; böylece Material 3'ün (ve `MaterialExpressiveTheme` ile M3 Expressive'in) söylediği gibi hareket ederler.

## AnimatedVisibility

`AnimatedVisibility`, `visible` true olduğunda içeriğini canlandırarak içeri, false olduğunda dışarı alır. Gizlendikten sonra içerik kompozisyondan çıkar.

```cpp
void Details() {
    auto open = remember([] { return mutableStateOf(false); });
    Column([&] {
        TextButton([open] { open.set(!open.get()); }, [&] { Text(open.get() ? "Less" : "More"); });
        AnimatedVisibility(open.get(), Modifier(), fadeIn() + expandVertically(), fadeOut() + shrinkVertically(), [] {
            Text("Here are the details.", Modifier().padding(16));
        });
    });
}
```

Giriş geçişleri `fadeIn`, `slideInHorizontally`, `slideInVertically`, `scaleIn`, `expandIn`, `expandHorizontally` ve `expandVertically`'dir; çıkış geçişleri bunların karşılıkları olan `fadeOut`, `slideOut…`, `scaleOut` ve `shrink…`'tir. Bunları `+` ile birleştirin. `EnterTransition::None` ve `ExitTransition::None` hiçbir şeyi canlandırmaz; çocuklar kendilerini içeriğin `AnimatedVisibilityScope`'undan `animateEnterExit` ile canlandırdığında kullanılır.

Compose'taki gibi bir geçiş, içeriği yerleştirilip çizilirken okunur: çevresindeki composable geçiş başladığında ve bittiğinde yeniden oluşturulur, aradaki karelerde değil.

## AnimatedContent ve Crossfade

`AnimatedContent`, hedef durumu değiştiğinde içerikler arasında canlandırır: eski içerik çıkarken yenisi girer ve boyut ikisi arasında canlanır.

```cpp
void Counter() {
    auto count = remember([] { return mutableStateOf(0); });
    Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
        IconButton([count] { count.set(count.get() - 1); }, [] { Icon(Icons::Filled::Remove, "Less"); });
        AnimatedContent<int>(count.get(), [](const int& n) {
            Text(std::to_string(n), {.font = typography().headlineMedium});
        }, Modifier(), [](const int& from, const int& to) {
            // yukarı sayarken yukarı, aşağı sayarken aşağı
            return togetherWith(slideInVertically(to > from ? 1 : -1), slideOutVertically(to > from ? -1 : 1));
        });
        IconButton([count] { count.set(count.get() + 1); }, [] { Icon(Icons::Filled::Add, "More"); });
    });
}
```

İçerik lambda'sı gösterdiği durumu alır; çıkan içerik için bu eski durumdur: durum değişkeninden değil, bu argümandan çizin. `togetherWith(enter, exit)` iki geçişi eşleştirir; `.withSizeTransform(SizeTransform{.clip = false})` boyutun nasıl canlandığını değiştirir.

`Crossfade` basit durumdur: içerikler arasında yumuşak geçiş yapar.

```cpp
enum class Page { Home, Settings };

void PageContent(Page page) {
    Crossfade<Page>(page, [](const Page& p) {
        if (p == Page::Home) Text("Home"); else Text("Settings");
    });
}
```

## animateContentSize

`Modifier().animateContentSize()`, içeriği bir düğümü büyüttüğünde ya da küçülttüğünde düğümün boyutunu canlandırır:

```cpp
void Expandable(const std::string& text) {
    auto expanded = remember([] { return mutableStateOf(false); });
    Text(text, Modifier().clickable([expanded] { expanded.set(!expanded.get()); }).animateContentSize(),
         {.maxLines = expanded.get() ? 0 : 2, .overflow = TextOverflow::Ellipsis});
}
```

Boyut değişikliğini görebilmesi için zincirde boyut modifier'larından önce koyun.

## updateTransition

Birkaç değer tek bir duruma doğru birlikte canlandığında bir geçiş onları aynı adımda tutar ve hepsinin ne zaman vardığını söyler:

```cpp
enum class BoxState { Collapsed, Expanded };

void TransitionBox() {
    auto state = remember([] { return mutableStateOf(BoxState::Collapsed); });
    auto t = updateTransition(state.get());
    float size = t.animateFloat([](BoxState s) { return s == BoxState::Expanded ? 200.f : 80.f; });
    Color color = t.animateColor([](BoxState s) {
        return s == BoxState::Expanded ? Color::hex(0x6750A4) : Color::hex(0xB58392);
    });
    Box(Modifier().size(size).background(color, 16).clickable([state] {
        state.set(state.get() == BoxState::Expanded ? BoxState::Collapsed : BoxState::Expanded);
    }));
}
```

`t.currentState()`, her değer vardığında `t.targetState()`'e yetişir; `t.isRunning()` hâlâ hareket eden bir değer olup olmadığını söyler.

## Sonsuz animasyonlar

`rememberInfiniteTransition()`, değerleri kompozisyonda kaldıkları sürece canlandırır:

```cpp
void Pulse() {
    auto pulse = rememberInfiniteTransition().animateFloatState(0.85f, 1.f, infiniteRepeatable(tween(800), RepeatMode::Reverse));
    Box(Modifier().size(64)
            .graphicsLayer([pulse](GraphicsLayerScope& l) { l.scaleX = l.scaleY = pulse.get(); })
            .background(colorScheme().primary, CircleShape));
}
```

`animateFloatState` ve `animateColorState`, gereken yerde okunacak bir durum döndürür: buradaki gibi çizim sırasında okunursa her karede yalnızca yeniden çizilir. `animateFloat` ve `animateColor` ise değeri doğrudan döndürür ve okuyanı her karede yeniden kompoze eder.

## Animatable

`Animatable<T>`, coroutine'lerden canlandırdığınız bir değerdir: `animateTo` animasyon bitene kadar askıya alır, `snapTo` atlar, `stop` durdurur. Yeni bir `animateTo`, çalışan birinden hızını koruyarak devralır; bu da onu hareketlerle yönetilen animasyonların aracı yapar:

```cpp
void Shake(bool error) {
    auto offset = remember([] { return Animatable<float>(0.f); });
    LaunchedEffect(error, [offset, error]() -> Task {
        if (!error) co_return;
        for (float x : {-12.f, 12.f, -8.f, 8.f, 0.f}) co_await offset.animateTo(x, tween(50));
    });
    Text("Wrong password", Modifier().offset([offset] { return Offset{offset.value(), 0}; }),
         {.color = colorScheme().error});
}
```

- `animateTo(target, spec, initialVelocity)` bir fırlatmanın hızından başlar; böylece bırakılan bir sürükleme doğal biçimde yaylanarak devam eder.
- `animateDecay(velocity, splineBasedDecay())` bir hızdan başlayıp durana kadar yavaşlar, bir fırlatma gibi.
- `updateBounds(lower, upper)` onu sınırlar içinde tutar.
- `value()` gözlemlenebilirdir; akıcı hareket için onu yerleşim ya da çizim sırasında okuyun.

Sürüklemeyle birleştirmek için bkz. [Hareketler](gestures.md).

## Paylaşılan öğeler

Paylaşılan bir öğe, Compose'daki gibi bir içerikteki yerinden ötekindeki yerine taşınır ve yeniden boyutlanır:

```cpp
void Gallery() {
    auto details = remember([] { return mutableStateOf(false); });
    SharedTransitionLayout(Modifier().fillMaxSize(), [&](const SharedTransitionScope& shared) {
        AnimatedContent<bool>(details.get(), [&, details](const AnimatedContentScope& scope, const bool& open) {
            Box(Modifier()
                    .size(open ? 280 : 72)
                    .sharedElement(shared.rememberSharedContentState("photo"), scope)
                    .background(colorScheme().tertiary, 16)
                    .clickable([details] { details.set(!details.get()); }));
        });
    });
}
```

Çıkan ve giren içerikte aynı anahtara sahip öğeler, ikisi arasında canlanan ve her şeyin üstünde çizilen tek bir öğe olur. `sharedBounds`, içerikleri farklı olan öğeler içindir: ikisi de hareket eden sınırların içinde, birbirine geçerek gösterilir. Gezinmeyle birlikte paylaşılan öğeler ekranlar arasında taşınır.

## Ayrıca bakın

- [Aşamalar](phases.md): yeniden kompoze etmeden canlandırmak.
- [Hareketler](gestures.md): parmağı izleyen animasyonlar.
- [Grafik](graphics.md): canlandırılmış dönüşümler için `graphicsLayer`.
