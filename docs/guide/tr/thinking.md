# Kompoz'da düşünmek

Kompoz deklaratiftir: arayüzün mevcut veriye göre nasıl görünmesi gerektiğini tarif edersiniz, veri değişince ekranı Kompoz günceller. Bu sayfa bu modeli ve onunla gelen kuralları anlatıyor.

## Deklaratif fikir

İmperatif bir araç takımında widget ağacını bir kez kurar, sonra olaylar oldukça onu değiştirirsiniz: etiketi bulursunuz, `setText` çağırırsınız, butonu gizlersiniz, bir satır eklersiniz. Her değişiklik sizin yazdığınız koddur ve bir ekranın ne kadar çok durumu varsa, bir geçişi unutup tutarsız bir şey göstermek o kadar kolaylaşır.

Kompoz'da veriyi arayüze çeviren fonksiyonlar yazarsınız. Veri değişince fonksiyonlar yeni veriyle yeniden çalışır ve ekranda neyin değiştiğini Kompoz kendisi bulur:

```cpp
void Inbox(int unread) {
    if (unread == 0) {
        Text("No new messages");
    } else {
        Text(std::to_string(unread) + " new messages", {.fontWeight = FontWeight::Bold});
    }
}
```

Etiketi "güncelleyen" bir adım yoktur: fonksiyon her zaman sonucun tamamını tarif eder, ekranı onunla uyumlu tutmak çatının işidir. Bu, Jetpack Compose, React ya da SwiftUI ile aynı modeldir.

## Composable fonksiyonlar

Composable fonksiyon, başka composable'ları çağırarak arayüz üreten bir C++ fonksiyonudur:

- Gösterdiği veriyi parametre olarak alır.
- Hiçbir şey döndürmez: `Text`, `Row`, `Button` gibi fonksiyonları çağırarak arayüz *üretir*.
- Her fonksiyon gibi başka composable'lardan, döngülerin ve koşulların içinden çağrılabilir.

```cpp
struct Contact {
    std::string name, phone;
};

void ContactList(const std::vector<Contact>& contacts) {
    Column([&] {
        for (const Contact& c : contacts) {
            Text(c.name, {.font = typography().bodyLarge});
            Text(c.phone, {.color = colorScheme().onSurfaceVariant});
        }
    });
}
```

Composable'lar Kompoz *kompoze ederken* çalışır: uygulama açılırken ve okudukları bir şey değiştiğinde yeniden. Birini kompozisyonun dışında çağırmak (örneğin bir tıklama işleyicisinden) işe yarar bir şey yapmaz; onun yerine durumu değiştirin, çağırmayı kompozisyona bırakın.

## Recomposition

Veri değişince Kompoz ona bağlı composable'ları yeniden çalıştırır: buna *recomposition* denir. Her şeyi baştan kurmaz:

- Bir durum, onu kimin okuduğunu hatırlar. Değiştiğinde yalnız onu okuyan composable'lar yeniden çalışır, ebeveynleri değil.
- Bir ebeveyn yeniden çalıştığında, argümanları geçen seferkine eşit olan ve değişen hiçbir şeyi okumamış bir çocuk *atlanır*: çalışmaz, ekrandaki bölümü olduğu gibi kalır.

```cpp
void NameTag(const std::string& name, CallSite site = CallSite::current()) {
    composable(site, name, [=] {   // `name` aynı kaldıkça atlanır
        Text("Hello, " + name);
    });
}

void Screen() {
    auto clicks = remember([] { return mutableStateOf(0); });
    Column([&] {
        NameTag("Ada");                                   // clicks değişince yeniden çalışmaz
        Text("Clicks: " + std::to_string(clicks.get()));  // Screen clicks'i okudu
        Button([clicks] { clicks.set(clicks.get() + 1); }, [] { Text("Click"); });
    });
}
```

`Screen` `clicks`'i okudu, bu yüzden her tıklamada yeniden çalışır. `NameTag` kendi başına bir scope'tur ve argümanı değişmedi, bu yüzden atlanır. Scope'ların ve atlamanın C++'ta nasıl işlediği için [Yaşam döngüsü ve kimlik](lifecycle.md) sayfasına bakın.

## Bundan çıkan kurallar

Composable'larınızın ne zaman ve ne sıklıkta çalışacağına Kompoz karar verdiği için, birkaç kural sonuçların doğru kalmasını sağlar.

### Composable'lar sık çalışabilir

Bir composable bir animasyonun her karesinde çalışabilir. Gövdesini hızlı tutun: dosya okuma, ağ çağrısı, ağır hesap olmasın. Pahalı işi `remember` ile bir kez yapın ya da kompozisyonun dışında bir coroutine'de yapın (bkz. [Yan etkiler](side-effects.md)), sonucu da durumda tutun.

### Composable'lar yan etkisiz olmalı

Bir composable'ı çalıştırmak yalnızca arayüzü tarif etmeli. Gövdesinin içinden dosya yazmak, istek göndermek ya da bir değişkeni değiştirmek her recomposition'da yeniden olur, üstelik sizin kontrol etmediğiniz bir sırayla.

```cpp nocheck
void Bad(int& counter) {
    counter++;              // her recomposition'da çalışır: yapmayın
    Text(std::to_string(counter));
}
```

Etkilerin yeri olay işleyicileri (`onClick` her tıklamada bir kez çalışır) ya da belirli anlarda çalışan `LaunchedEffect` ve `DisposableEffect` gibi efekt API'leridir.

### Composable'lar atlanabilir

Ebeveyni çalıştı diye bir composable'ın da çalışacağına güvenmeyin. Değişen bir durumu okumamışsa ve argümanları eşitse çalışmaz. Mutlaka olması gereken her şeyin yeri durum, olaylar ya da efektlerdir.

### Composable'lar sabit bir sırayla çalışmaz

Her composable'ı, bir başkasının önce çalışmış olmasına bağlı olmayacak şekilde yazın. Kompozisyon sırasında yazılan global değişkenlerle veri paylaşmak, içlerinden biri atlandığı anda bozulur.

## Arayüzü durum yönetir

Arayüz verinin bir fonksiyonu olduğuna göre, arayüzü değiştirmek veriyi değiştirmek demektir. Kompoz *durumu*, yani `mutableStateOf` ve diğer durum tutucularla yapılan değerleri izler ve onları okuyanları yeniden kompoze eder:

```cpp
void Toggle() {
    auto on = remember([] { return mutableStateOf(false); });
    Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
        Switch(on.get(), [on](bool v) { on.set(v); });
        Text(on.get() ? "On" : "Off");
    });
}
```

Veri argümanlarla aşağı, olaylar callback'lerle yukarı akar: `Switch` kendisi hiçbir şeyi değiştirmez, kullanıcının ne yaptığını çağırana bildirir, durumun ne olacağına çağıran karar verir. Bu *tek yönlü veri akışı*, Kompoz uygulamalarının nasıl yapılandırıldığının temelidir. Bkz. [Durum](state.md) ve [Mimari](architecture.md).

## Ayrıca bakın

- [Durum](state.md): durumu tutmak, hatırlamak ve yukarı taşımak.
- [Yaşam döngüsü ve kimlik](lifecycle.md): composable'ların kompozisyona nasıl girip çıktığı ve atlanmalarını neyin sağladığı.
- [Aşamalar](phases.md): kompozisyon, yerleşim ve çizim.
