# Kompoz belgeleri

Kompoz, C++20 için Jetpack Compose tarzında deklaratif bir arayüz kütüphanesidir; Material 3 bileşenleriyle gelir ve tek kod tabanından Linux, Windows, Android ve web'de çalışır.

Compose'u biliyorsanız Kompoz'u da biliyorsunuz: aynı düşünme biçimi (composable fonksiyonlar, durum, recomposition, modifier'lar), aynı isimler ve aynı yazım. Bir composable, Kotlin yazımıyla bir `.cpp` dosyasına yazılan bir `@Kompozable fun`'dır; Kompoz'un derleyicisi kompozc onu derleme sırasında C++'a çevirir, Compose'un derleyici eklentisinin Kotlin'i yeniden yazdığı gibi; çevresindeki her şey C++'tır. Sürüm 0.1.1, Apache Lisansı 2.0 ile lisanslıdır.

![Vitrinin ana ekranı](../../screenshots/home.png)

```kotlin
@Kompozable
fun Counter() {
    var count by remember { mutableStateOf(0) }

    Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("Sayı: $count", style = MaterialTheme.typography.titleLarge)
        Button(onClick = { count++ }) { Text("Ekle") }
    }
}

kompoz::Application kompoz::createApplication(int, char**) {
    return {.title = "Demo", .content = [] { MaterialTheme(lightColorScheme(), [] { Counter(); }); }};
}
```

## Başlarken

- [Kurulum](setup.md): Kompoz'u derleyin, `kompozable` ile kendi CMake projenize ekleyin ve her platformda çalıştırın.
- [Eğitim](tutorial.md): tek satır metinden bir Material listesine, ilk uygulamayı adım adım kurun.
- [Compose'dan geliyorsanız](coming-from-compose.md): `.cpp` dosyalarında Kotlin kodu, kompozc'nin okudukları ve çevresindeki C++.
- [C++ ile yazmak](cpp.md): kompozc olmadan, Kompoz'un composable'larını doğrudan C++ ile yazmak.

## Temeller

- [Kompoz'da düşünmek](thinking.md): deklaratif arayüz, composable fonksiyonlar ve recomposition.
- [Durum](state.md): gözlemlenebilir durum, `remember`, `rememberSaveable`, durumu yukarı taşıma ve durum tutucular.
- [Yaşam döngüsü ve kimlik](lifecycle.md): çağrıların nasıl tanındığı ve neyin onları atlattığı.
- [Yan etkiler](side-effects.md): coroutine'ler, `LaunchedEffect`, `DisposableEffect` ve diğer efekt API'leri.
- [Aşamalar](phases.md): kompozisyon, yerleşim ve çizim; her birini nasıl ucuz tutacağınız.
- [KompozitionLocal](kompozition-local.md): değerleri ağaçta aşağı aktarmak ve yerleşik local'ler.

## Tasarım

- [Modifier'lar](modifiers.md): composable'ları süslemek, sıranın neden önemli olduğu ve kendi modifier'ınızı yazmak.
- [Yerleşimler](layouts.md): `Column`, `Row`, `Box`, akan yerleşimler, `FlexBox`, `Grid` ve uyarlanabilir yerleşimler.
- [Özel yerleşimler](custom-layouts.md): `Layout`, kısıtlar, intrinsic'ler ve `SubkompozLayout`.
- [Listeler ve ızgaralar](lists.md): tembel listeler ve ızgaralar, pager'lar, carousel'ler ve iç içe kaydırma.
- [Material bileşenleri](material.md): Material 3 bileşenlerinde bir tur.
- [Tema](theming.md): renk şemaları, koyu tema, dinamik renk, tipografi ve tasarım sistemleri.
- [Metin](text.md): metne stil vermek, fontlar, seçim ve metin alanları.
- [Grafik](graphics.md): `Canvas`, fırçalar, path'ler, şekiller, gölgeler, resimler ve vektörler.
- [Animasyon](animation.md): animasyonlu değerler, görünürlük, içerik, geçişler ve paylaşılan öğeler.
- [Hareketler ve girdi](gestures.md): tıklamalar, sürüklemeler, iki parmakla yakınlaştırma, klavye, odak ve sürükle-bırak.

## Uygulama

- [Mimari](architecture.md): tek yönlü veri akışı, `ViewModel`'ler, flow'lar ve kaydedilen durum.
- [Gezinme](navigation.md): ekranlar, geri yığını, tür güvenli rotalar ve derin bağlantılar.
- [Kaynaklar](resources.md): varlıklar (asset'ler), kütüphanenin sistem dilindeki metinleri ve platform servisleri.
- [Erişilebilirlik](accessibility.md): semantik, ekran okuyucular, dokunma hedefleri ve klavye.
- [Test](testing.md): sanal saatle, pencere açmadan arayüz testleri.
- [Araçlar](tools.md): önizlemeler, kaydedince yeniden derleme, betikle ekran görüntüleri ve profil çıkarma.

## Başvuru

- [API durumu](api.md): her Compose API'si ve Kompoz'daki durumu, ayrıca geliştirme günlüğü.

![Geniş pencerede, gezinme rayıyla Bileşenler ekranı](../../screenshots/components-wide.png)

![Dinamik 1](../../screenshots/dynamic-color-linux/d9.png)

![Dinamik 1](../../screenshots/dynamic-color-linux/d16_light.png)

[Daha fazlasını](dynamic_color_gallery.md) görmek için
