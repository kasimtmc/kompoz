# Hareketler ve girdi

Dokunmaları, tıklamaları, sürüklemeleri, kaydırmaları, kıstırmaları, klavyeyi, odağı, üzerine gelmeyi ve sürükle-bırakı ele almak. Kompoz her platformda dokunma, fare, dokunmatik yüzey ve klavye girdisini alır.

## Tıklamalar

`Modifier.clickable(onClick)` en basit harekettir: Material'ın dalgalanma efekti, klavyeyle etkinleştirme (odaklıyken Enter ve Space) ve bir erişilebilirlik eylemiyle birlikte bir dokunma ya da tıklama.

```kotlin body
Text("Open", modifier = Modifier.clickable { /* … */ }.padding(16.dp))
```

`combinedClickable(onClick, onLongClick)` uzun basmayı da alır (seçenekleri üzerinden çift dokunmayı da). `ClickableOptions` tıklamayı kapatır (`enabled`), bir `interactionSource` paylaşır ya da `indication`'ı değiştirir: hiçbiri için `false`, sınırsız bir dalgalanma için `ripple(false, 24)`.

Tıklamaya tepki veren bileşenler (`Button`, `Card`, `ListItem`) kendi `onClick`'lerini alır; onlara `clickable` eklemezsiniz.

## Kaydırma ve sürükleme

Kaydırılan içerik için kaydırma modifier'larını ya da bir tembel listeyi kullanın (bkz. [Listeler ve ızgaralar](lists.md)): sürüklemeleri, Android'in eğrisiyle fırlatmaları, kenar esnemesini, fare tekerleğini ve dokunmatik yüzeyleri onlar halleder.

`Modifier.draggable(state, orientation)` tek eksen boyunca sürüklemeleri dp cinsinden farklar olarak bildirir. Kendisi hiçbir şeyi taşımaz: istediğinizi, genellikle yerleşim sırasında okunan bir offset ile siz taşırsınız:

```kotlin
@Kompozable
fun Slider1D() {
    var x by remember { mutableStateOf(0f) }
    Box(modifier = Modifier.fillMaxWidth().height(56.dp)) {
        Box(modifier = Modifier
                .offset { IntOffset(x.roundToInt(), 0) }
                .draggable(orientation = Orientation.Horizontal, state = rememberDraggableState { delta -> x = maxOf(0f, x + delta) })
                .size(56.dp)
                .background(MaterialTheme.colorScheme.primary, CircleShape))
    }
}
```

`draggable2D` sürüklemeleri iki yönde bildirir. `scrollable(state, orientation)` kaydırma modifier'larından daha alt düzeydir: kaydırma farklarını ve fırlatmaları bildirir, her birinden ne kadarını tüketeceğinize siz karar verirsiniz.

### Konumlar arasında yakalama

`anchoredDraggable` bir şeyi adlandırılmış konumlar arasında sürükler ve bırakıldığında, fırlatmanın hızıyla bunlardan birine yerleşir: alt sayfaların, çekmecelerin ve kaydırarak göstermenin yapı taşı.

```kotlin
enum class DragValue { Start, End }

@Kompozable
fun Toggle() {
    val state = rememberAnchoredDraggableState(DragValue.Start)
    state.updateAnchors(DraggableAnchors {
        DragValue.Start at 0f
        DragValue.End at 120f
    })
    Box(modifier = Modifier.width(176.dp).height(56.dp).background(MaterialTheme.colorScheme.surfaceContainerHigh, CircleShape)) {
        Box(modifier = Modifier
                .offset { IntOffset(state.requireOffset().roundToInt(), 0) }
                .anchoredDraggable(state, Orientation.Horizontal)
                .size(56.dp)
                .background(MaterialTheme.colorScheme.primary, CircleShape))
    }
}
```

`SwipeToDismissBox`, kaydırılarak atılan liste öğeleri için bunun üzerine kuruludur.

## Kıstırma, yakınlaştırma ve döndürme

`transformable`, çoklu dokunma hareketlerini yakınlaştırma, kaydırma ve döndürme değişikliklerine çevirir. Ctrl+tekerlek ve dokunmatik yüzeyde kıstırma da yakınlaştırır:

```kotlin
@Kompozable
fun ZoomableImage() {
    var scale by remember { mutableStateOf(1f) }
    val state = rememberTransformableState { zoom, _, _ -> scale = (scale * zoom).coerceIn(1f, 5f) }
    Image(imageResource("images/map.png"), contentDescription = "Map",
          modifier = Modifier.fillMaxSize()
              .graphicsLayer { scaleX = scale; scaleY = scale }
              .transformable(state))
}
```

## pointerInput

Kendi hareketleriniz için `Modifier.pointerInput(key) { }`, düğümün işaretçi olaylarını alan bir coroutine çalıştırır. Hareket algılayıcıları bunun içinde çalışır:

```kotlin
@Kompozable
fun TapTarget() {
    var last by remember { mutableStateOf("Nothing yet") }
    Box(modifier = Modifier.size(200.dp).background(MaterialTheme.colorScheme.secondaryContainer)
            .pointerInput(Unit) {
                detectTapGestures(onTap = { last = "Tap" }, onDoubleTap = { last = "Double tap" }, onLongPress = { last = "Long press" })
            },
        contentAlignment = Alignment.Center) { Text(last) }
}
```

| Algılayıcı | Tanıdığı |
|---|---|
| `detectTapGestures(s, {.onPress, .onTap, .onDoubleTap, .onLongPress})` | Dokunmalar, çift dokunmalar, uzun basmalar. |
| `detectDragGestures(s, {.onDragStart, .onDrag, .onDragEnd})` | Dokunma eşiği aşıldıktan sonra her yöndeki sürüklemeler. |
| `detectHorizontalDragGestures`, `detectVerticalDragGestures` | Tek eksen boyunca sürüklemeler. |
| `detectDragGesturesAfterLongPress` | Bir şeyi tutup kaldırmak için uzun basmayla başlayan bir sürükleme. |
| `detectTransformGestures(s, onGesture)` | Birkaç parmakla kıstırma, kaydırma ve döndürme. |

Blok `key` değiştiğinde yeniden başlar; bu yüzden ona bağlı olduğu şeyi yakalayan bir anahtar verin. Daha alt düzey işler için `awaitPointerEvent()` (blokta: Compose'un `awaitPointerEventScope { }`'u burada örtük) her olayı her işaretçinin değişikliğiyle birlikte verir; bir değişikliği tüketin (`change.consume()`) ki çevresindeki kaydırma ve tıklamalar ona dokunmasın. `awaitEachGesture` ve `waitForUpOrCancellation` bir hareketi yapılandırmaya yardım eder; `VelocityTracker` ise bir fırlatma için parmağın kalktığı anda ne kadar hızlı hareket ettiğini ölçer.

```kotlin
@Kompozable
fun Drawing() {
    val points = remember { mutableStateListOf<Offset>() }
    val ink = MaterialTheme.colorScheme.onSurface
    Canvas(modifier = Modifier.fillMaxSize().pointerInput(Unit) {
        detectDragGestures(onDragStart = { at -> points.clear(); points.add(at) }, onDrag = { change, _ -> points.add(change.position) })
    }) {
        drawPoints(points.toList(), PointMode.Polygon, ink, strokeWidth = 3f)
    }
}
```

## Klavye

`onKeyEvent` odaklı bir düğümde tuşları ele alır; bir tuşu ele aldığınızda `true` döndürün. `onPreviewKeyEvent` tuşları odaklı düğümden önce görür.

```kotlin body
#! fun submit() {}
Box(modifier = Modifier
        .onKeyEvent { e ->
            if (e.type == KeyEventType.KeyDown && e.key == Key.Enter && e.isCtrlPressed) {
                submit()
                true
            } else {
                false
            }
        }
        .focusable())
```

## Odak

Tab ve Shift+Tab klavye odağını odaklanabilir düğümler arasında taşır, ok tuşları da bir yöne taşır. Tıklanabilir bileşenler ve metin alanları zaten odaklanabilirdir; `Modifier.focusable()` herhangi bir düğümü odaklanabilir yapar.

- `FocusRequester` odağı koddan taşır: bir tane remember edin, `Modifier.focusRequester(r)` ile bağlayın, `r.requestFocus()`'u çağırın.
- `LocalFocusManager.current.clearFocus()` odağı kaldırır; bu yazılım klavyesini de gizler.
- `onFocusChanged([](FocusState s) { … })` bir düğümün odak kazandığını ya da kaybettiğini söyler.
- `focusProperties({.canFocus = false})` bir düğümü sıradan çıkarır; `next` ve diğer yönler odağın nereye gideceğini değiştirir.
- `focusGroup()` ve `focusRestorer()`, bir sıra chip gibi gruplar içinde odağın mantıklı biçimde hareket etmesini sağlar.

Klavye odağı, Compose Material 3'teki gibi bileşenin odak durum katmanıyla gösterilir.

## Üzerine gelme ve fare

```kotlin
@Kompozable
fun HoverCard() {
    val source = remember { MutableInteractionSource() }
    val hovered by source.collectIsHoveredAsState()
    Card(modifier = Modifier.hoverable(source).pointerHoverIcon(PointerIcon.Hand)) {
        Text(if (hovered) "Hovering" else "Point at me", modifier = Modifier.padding(24.dp))
    }
}
```

`hoverable(source)` farenin bir düğümün üzerine gelişini ve ayrılışını bildirir. `pointerHoverIcon(PointerIcon.Hand)` imleci değiştirir. Metin alanları ve seçimler sağ tıklamada bir bağlam menüsü gösterir.

Compose Multiplatform'un masaüstü API'leri adlarıyla burada: farenin girip çıkması, hareketi, basması ve tekerleği için `onPointerEvent(type) { event -> }` (`event.changes.first().consume()` tekerleğin sayfayı kaydırmasını önler), düğmeye ya da basılı tuşlara göre tıklamalar için `onClick(matcher = PointerMatcher.mouse(PointerButton.Secondary)) { }` ve `onClick(keyboardModifiers = { it.isCtrlPressed }) { }`, sağ tıklamada kendi menünüz için `ContextMenuArea`, fareyi izleyen bir ipucu için `TooltipArea`:

```kotlin
@Kompozable
fun DesktopMouse() {
    var last by remember { mutableStateOf("-") }
    Column {
        ContextMenuArea(items = { listOf(ContextMenuItem("Yenile") { last = "Yenile" }, ContextMenuItem("Adını kopyala") { last = "Adını kopyala" }) }) {
            Box(modifier = Modifier.size(200.dp, 80.dp).background(MaterialTheme.colorScheme.secondaryContainer))
        }
        TooltipArea(tooltip = { Text("İmlecin altında çıkar") }, delayMillis = 300) {
            Box(modifier = Modifier.size(200.dp, 40.dp).onClick(matcher = PointerMatcher.mouse(PointerButton.Secondary)) { last = "sağ tık" })
        }
        Text(last)
    }
}
```

Bir pencerenin menüleri Compose Multiplatform'un `MenuBar { Menu("…") { … } }`'ıdır, içeriğin üstünde bir şerit olarak çizilir: `Item`, `CheckboxItem`, `RadioButtonItem`, `Separator` ve alt menüler, her biri pencere tuşları alırken çalışan bir `KeyShortcut` ile (tuş adları Compose'unki gibi: `Key.DirectionLeft`):

```kotlin
@Kompozable
fun Menus() {
    var dark by remember { mutableStateOf(false) }
    var size by remember { mutableStateOf(1) }
    Column {
        MenuBar {
            Menu("Dosya") {
                Item("Aç", shortcut = KeyShortcut(Key.O, ctrl = true)) { }
                Menu("Son açılanlar") { Item("notlar.txt") { } }
            }
            Menu("Görünüm") {
                CheckboxItem("Koyu tema", checked = dark, shortcut = KeyShortcut(Key.D, ctrl = true), onCheckedChange = { dark = it })
                Separator()
                RadioButtonItem("Küçük", selected = size == 1, onClick = { size = 1 })
                RadioButtonItem("Büyük", selected = size == 2, onClick = { size = 2 })
            }
        }
        Text("Boy $size")
    }
}
```

## Etkileşimler

Bir `MutableInteractionSource`'a, verildiği bileşenlerin basmaları, üzerine gelmeleri, odakları ve sürüklemeleri bildirilir. Birinin sürüp sürmediğini `collectIsPressedAsState()`, `collectIsHoveredAsState()`, `collectIsFocusedAsState()` ve `collectIsDraggedAsState()` (`by` ile okunur) ile okuyun; bunlar değiştiğinde yeniden kompoze ederler.

```kotlin
@Kompozable
fun PressToRecord() {
    val source = remember { MutableInteractionSource() }
    val pressed by source.collectIsPressedAsState()
    Button(onClick = {}, interactionSource = source) { Text(if (pressed) "Recording…" else "Hold to record") }
}
```

`interactions()` etkileşimlerin akışını bir flow olarak verir. `Modifier.indication(source, ripple())` başka bir yerde olan etkileşimler için bir gösterge çizer.

## Sürükle-bırak

`dragAndDropSource` verilerle bir sürükleme başlatır; `dragAndDropTarget` uygulamanın içinden gelen bırakmaları ya da masaüstünde dışarıdan gelen dosyaları kabul eder:

```kotlin
@Kompozable
fun DropZone() {
    var dropped by remember { mutableStateOf("Drop text here") }
    Column {
        Text("Drag me", modifier = Modifier.padding(16.dp).dragAndDropSource { DragAndDropTransferData(text = "Hello") })
        Box(modifier = Modifier.size(200.dp).border(2.dp, MaterialTheme.colorScheme.outline, RoundedCornerShape(12.dp))
                .dragAndDropTarget(shouldStartDragAndDrop = { it.hasMimeType("text/plain") },
                                   target = DragAndDropTarget(onDrop = { e -> dropped = e.text(); true })),
            contentAlignment = Alignment.Center) { Text(dropped) }
    }
}
```

## Ayrıca bakın

- [Listeler ve ızgaralar](lists.md): kaydırma, fırlatma ve iç içe kaydırma.
- [Animasyon](animation.md): bırakıldığında canlanan hareketler için `Animatable`.
- [Erişilebilirlik](accessibility.md): özel hareketlere onlarsız da ulaşılabilmesini sağlamak.
