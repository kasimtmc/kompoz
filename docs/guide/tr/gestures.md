# Hareketler ve girdi

Dokunmaları, tıklamaları, sürüklemeleri, kaydırmaları, kıstırmaları, klavyeyi, odağı, üzerine gelmeyi ve sürükle-bırakı ele almak. Kompoz her platformda dokunma, fare, dokunmatik yüzey ve klavye girdisini alır.

## Tıklamalar

`Modifier().clickable(onClick)` en basit harekettir: Material'ın dalgalanma efekti, klavyeyle etkinleştirme (odaklıyken Enter ve Space) ve bir erişilebilirlik eylemiyle birlikte bir dokunma ya da tıklama.

```cpp body
Text("Open", Modifier().clickable([] { /* … */ }).padding(16));
```

`combinedClickable(onClick, onLongClick)` uzun basmayı da alır (seçenekleri üzerinden çift dokunmayı da). `ClickableOptions` tıklamayı kapatır (`enabled`), bir `interactionSource` paylaşır ya da `indication`'ı değiştirir: hiçbiri için `false`, sınırsız bir dalgalanma için `ripple(false, 24)`.

Tıklamaya tepki veren bileşenler (`Button`, `Card`, `ListItem`) kendi `onClick`'lerini alır; onlara `clickable` eklemezsiniz.

## Kaydırma ve sürükleme

Kaydırılan içerik için kaydırma modifier'larını ya da bir tembel listeyi kullanın (bkz. [Listeler ve ızgaralar](lists.md)): sürüklemeleri, Android'in eğrisiyle fırlatmaları, kenar esnemesini, fare tekerleğini ve dokunmatik yüzeyleri onlar halleder.

`Modifier().draggable(state, orientation)` tek eksen boyunca sürüklemeleri dp cinsinden farklar olarak bildirir. Kendisi hiçbir şeyi taşımaz: istediğinizi, genellikle yerleşim sırasında okunan bir offset ile siz taşırsınız:

```cpp
void Slider1D() {
    auto x = remember([] { return mutableStateOf(0.f); });
    Box(Modifier().fillMaxWidth().height(56), [&] {
        Box(Modifier()
                .offset([x] { return Offset{x.get(), 0}; })
                .draggable(rememberDraggableState([x](float delta) { x.set(std::max(0.f, x.get() + delta)); }),
                           Orientation::Horizontal)
                .size(56)
                .background(colorScheme().primary, CircleShape));
    });
}
```

`draggable2D` sürüklemeleri iki yönde bildirir. `scrollable(state, orientation)` kaydırma modifier'larından daha alt düzeydir: kaydırma farklarını ve fırlatmaları bildirir, her birinden ne kadarını tüketeceğinize siz karar verirsiniz.

### Konumlar arasında yakalama

`anchoredDraggable` bir şeyi adlandırılmış konumlar arasında sürükler ve bırakıldığında, fırlatmanın hızıyla bunlardan birine yerleşir: alt sayfaların, çekmecelerin ve kaydırarak göstermenin yapı taşı.

```cpp
enum class DragValue { Start, End };

void Toggle() {
    auto state = rememberAnchoredDraggableState(DragValue::Start);
    state.updateAnchors({{DragValue::Start, 0}, {DragValue::End, 120}});
    Box(Modifier().width(176).height(56).background(colorScheme().surfaceContainerHigh, CircleShape), [&] {
        Box(Modifier()
                .offset([state] { return Offset{state.requireOffset(), 0}; })
                .anchoredDraggable(state, Orientation::Horizontal)
                .size(56)
                .background(colorScheme().primary, CircleShape));
    });
}
```

`SwipeToDismissBox`, kaydırılarak atılan liste öğeleri için bunun üzerine kuruludur.

## Kıstırma, yakınlaştırma ve döndürme

`transformable`, çoklu dokunma hareketlerini yakınlaştırma, kaydırma ve döndürme değişikliklerine çevirir. Ctrl+tekerlek ve dokunmatik yüzeyde kıstırma da yakınlaştırır:

```cpp
void ZoomableImage() {
    auto scale = remember([] { return mutableStateOf(1.f); });
    auto state = rememberTransformableState([scale](float zoom, Offset, float) {
        scale.set(std::clamp(scale.get() * zoom, 1.f, 5.f));
    });
    Image(imageResource("images/map.png"), "Map",
          Modifier().fillMaxSize()
              .graphicsLayer([scale](GraphicsLayerScope& l) { l.scaleX = l.scaleY = scale.get(); })
              .transformable(state));
}
```

## pointerInput

Kendi hareketleriniz için `Modifier().pointerInput(key, block)`, düğümün işaretçi olaylarını alan bir coroutine çalıştırır. Hareket algılayıcıları bunun içinde çalışır:

```cpp
void TapTarget() {
    auto last = remember([] { return mutableStateOf(std::string("Nothing yet")); });
    Box(Modifier().size(200).background(colorScheme().secondaryContainer)
            .pointerInput(0, [last](PointerInputScope& s) -> Task {
                co_await detectTapGestures(s, {.onTap = [last](Offset) { last.set("Tap"); },
                                               .onDoubleTap = [last](Offset) { last.set("Double tap"); },
                                               .onLongPress = [last](Offset) { last.set("Long press"); }});
            }),
        Alignment::Center, [&] { Text(last.get()); });
}
```

| Algılayıcı | Tanıdığı |
|---|---|
| `detectTapGestures(s, {.onPress, .onTap, .onDoubleTap, .onLongPress})` | Dokunmalar, çift dokunmalar, uzun basmalar. |
| `detectDragGestures(s, {.onDragStart, .onDrag, .onDragEnd})` | Dokunma eşiği aşıldıktan sonra her yöndeki sürüklemeler. |
| `detectHorizontalDragGestures`, `detectVerticalDragGestures` | Tek eksen boyunca sürüklemeler. |
| `detectDragGesturesAfterLongPress` | Bir şeyi tutup kaldırmak için uzun basmayla başlayan bir sürükleme. |
| `detectTransformGestures(s, onGesture)` | Birkaç parmakla kıstırma, kaydırma ve döndürme. |

Blok `key` değiştiğinde yeniden başlar; bu yüzden ona bağlı olduğu şeyi yakalayan bir anahtar verin. Daha alt düzey işler için `co_await s.awaitPointerEvent()` her olayı her işaretçinin değişikliğiyle birlikte verir; bir değişikliği tüketin (`change.consume()`) ki çevresindeki kaydırma ve tıklamalar ona dokunmasın. `awaitEachGesture` ve `waitForUpOrCancellation` bir hareketi yapılandırmaya yardım eder; `VelocityTracker` ise bir fırlatma için parmağın kalktığı anda ne kadar hızlı hareket ettiğini ölçer.

```cpp
void Drawing() {
    auto points = remember([] { return mutableStateOf(std::vector<Offset>{}); });
    Color ink = colorScheme().onSurface;
    Canvas(Modifier().fillMaxSize().pointerInput(0, [points](PointerInputScope& s) -> Task {
        co_await detectDragGestures(s, {.onDragStart = [points](Offset at) { points.update([&](std::vector<Offset>& v) { v.assign(1, at); }); },
                                        .onDrag = [points](PointerInputChange& change, Offset) {
                                            points.update([&](std::vector<Offset>& v) { v.push_back(change.position); });
                                        }});
    }), [=](DrawScope& d) {
        d.drawPoints(points.get(), PointMode::Polygon, ink, 3);
    });
}
```

## Klavye

`onKeyEvent` odaklı bir düğümde tuşları ele alır; bir tuşu ele aldığınızda `true` döndürün. `onPreviewKeyEvent` tuşları odaklı düğümden önce görür.

```cpp body
#! std::function<void()> submit;
Box(Modifier()
        .onKeyEvent([submit](const KeyEvent& e) {
            if (e.type == KeyEventType::KeyDown && e.key == Key::Enter && e.isCtrlPressed()) {
                submit();
                return true;
            }
            return false;
        })
        .focusable());
```

## Odak

Tab ve Shift+Tab klavye odağını odaklanabilir düğümler arasında taşır, ok tuşları da bir yöne taşır. Tıklanabilir bileşenler ve metin alanları zaten odaklanabilirdir; `Modifier().focusable()` herhangi bir düğümü odaklanabilir yapar.

- `FocusRequester` odağı koddan taşır: bir tane remember edin, `Modifier().focusRequester(r)` ile bağlayın, `r.requestFocus()`'u çağırın.
- `focusManager().clearFocus()` odağı kaldırır; bu yazılım klavyesini de gizler.
- `onFocusChanged([](FocusState s) { … })` bir düğümün odak kazandığını ya da kaybettiğini söyler.
- `focusProperties({.canFocus = false})` bir düğümü sıradan çıkarır; `next` ve diğer yönler odağın nereye gideceğini değiştirir.
- `focusGroup()` ve `focusRestorer()`, bir sıra chip gibi gruplar içinde odağın mantıklı biçimde hareket etmesini sağlar.

Klavye odağı, Compose Material 3'teki gibi bileşenin odak durum katmanıyla gösterilir.

## Üzerine gelme ve fare

```cpp
void HoverCard() {
    auto source = remember([] { return MutableInteractionSource(); });
    bool hovered = source.collectIsHoveredAsState();
    Card(Modifier().hoverable(source).pointerHoverIcon(PointerIcon::Hand), [&] {
        Text(hovered ? "Hovering" : "Point at me", Modifier().padding(24));
    });
}
```

`hoverable(source)` farenin bir düğümün üzerine gelişini ve ayrılışını bildirir. `pointerHoverIcon(PointerIcon::Hand)` imleci değiştirir. Metin alanları ve seçimler sağ tıklamada bir bağlam menüsü gösterir.

## Etkileşimler

Bir `MutableInteractionSource`'a, verildiği bileşenlerin basmaları, üzerine gelmeleri, odakları ve sürüklemeleri bildirilir. Birinin sürüp sürmediğini `collectIsPressedAsState()`, `collectIsHoveredAsState()`, `collectIsFocusedAsState()` ve `collectIsDraggedAsState()` ile okuyun; bunlar değiştiğinde yeniden kompoze ederler.

```cpp
void PressToRecord() {
    auto source = remember([] { return MutableInteractionSource(); });
    bool pressed = source.collectIsPressedAsState();
    Button([] {}, Modifier(), {.interactionSource = source}, [&] { Text(pressed ? "Recording…" : "Hold to record"); });
}
```

`interactions()` etkileşimlerin akışını bir flow olarak verir. `Modifier().indication(source, ripple())` başka bir yerde olan etkileşimler için bir gösterge çizer.

## Sürükle-bırak

`dragAndDropSource` verilerle bir sürükleme başlatır; `dragAndDropTarget` uygulamanın içinden gelen bırakmaları ya da masaüstünde dışarıdan gelen dosyaları kabul eder:

```cpp
void DropZone() {
    auto dropped = remember([] { return mutableStateOf(std::string("Drop text here")); });
    Column([&] {
        Text("Drag me", Modifier().padding(16).dragAndDropSource([](Offset) {
            return std::optional(DragAndDropTransferData{.text = "Hello"});
        }));
        Box(Modifier().size(200).border(2, colorScheme().outline, 12)
                .dragAndDropTarget([](const DragAndDropEvent& e) { return e.hasMimeType("text/plain"); },
                                   {.onDrop = [dropped](const DragAndDropEvent& e) {
                                       dropped.set(e.text());
                                       return true;
                                   }}),
            Alignment::Center, [&] { Text(dropped.get()); });
    });
}
```

## Ayrıca bakın

- [Listeler ve ızgaralar](lists.md): kaydırma, fırlatma ve iç içe kaydırma.
- [Animasyon](animation.md): bırakıldığında canlanan hareketler için `Animatable`.
- [Erişilebilirlik](accessibility.md): özel hareketlere onlarsız da ulaşılabilmesini sağlamak.
