// @Kompozable functions written in Kotlin's syntax, translated by kompozc before the C++
// compiler sees this file (kompozable in CMakeLists.txt); the tests check what they do.

#include <kompoz/kompoz.hpp>
#include <kompoz/testing.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace kompoz;
using kompoz::testing::KompozTestRule;

namespace {

int failures = 0;
std::vector<std::pair<const char*, void (*)()>>& registry() {
    static std::vector<std::pair<const char*, void (*)()>> tests;
    return tests;
}
struct Register {
    Register(const char* name, void (*fn)()) { registry().emplace_back(name, fn); }
};

#define TEST(name)                                \
    static void name();                           \
    static Register register_##name(#name, name); \
    static void name()

#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) {                                                                      \
            std::fprintf(stderr, "  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                     \
        }                                                                                   \
    } while (0)

}  // namespace

@Kompozable
fun Counter(start: Int = 0) {
    var count by remember { mutableStateOf(start) }
    Column {
        Text("Count: $count")
        Button({ count++ }) { Text("+") }
    }
}

TEST(a_delegated_state_counts) {
    KompozTestRule rule([] { Counter(); });
    CHECK(rule.onNodeWithText("Count: 0").exists());
    rule.onNodeWithText("+").performClick();
    rule.onNodeWithText("+").performClick();
    CHECK(rule.onNodeWithText("Count: 2").exists());
}

// Named arguments in any order, defaults left out, a trailing lambda for the last parameter.
@Kompozable
fun Labeled(label: String, value: Int = 7, suffix: String = "", content: @Kompozable () -> Unit) {
    Row {
        Text("$label=$value$suffix")
        content()
    }
}

@Kompozable
fun NamedArguments() {
    Column {
        Labeled(suffix = "!", label = "a") { Text("içerik") }
        Labeled("b", 3) { }
    }
}

TEST(named_arguments_go_in_place) {
    KompozTestRule rule([] { NamedArguments(); });
    CHECK(rule.onNodeWithText("a=7!").exists());
    CHECK(rule.onNodeWithText("içerik").exists());
    CHECK(rule.onNodeWithText("b=3").exists());
}

@Kompozable
fun Branches(n: Int) {
    Column {
        for (i in 0 until n) {
            Text("satır $i")
        }
        for (i in 10 downTo 8 step 2) Text("geri $i")
        if (n > 2) Text("çok") else Text("az")
        val word = when (n) {
            0 -> "sıfır"
            1, 2 -> "biraz"
            in 3..5 -> "orta"
            else -> "bol"
        }
        Text(word)
        Text(if (n % 2 == 0) "çift" else "tek")
    }
}

TEST(loops_ifs_and_whens) {
    KompozTestRule rule([] { Branches(4); });
    CHECK(rule.onNodeWithText("satır 0").exists() && rule.onNodeWithText("satır 3").exists());
    CHECK(!rule.onNodeWithText("satır 4").exists());
    CHECK(rule.onNodeWithText("geri 10").exists() && rule.onNodeWithText("geri 8").exists() && !rule.onNodeWithText("geri 9").exists());
    CHECK(rule.onNodeWithText("çok").exists() && rule.onNodeWithText("orta").exists() && rule.onNodeWithText("çift").exists());
    KompozTestRule none([] { Branches(0); });
    CHECK(!none.onNodeWithText("satır 0").exists() && none.onNodeWithText("sıfır").exists() && none.onNodeWithText("az").exists());
}

@Kompozable
fun Templates() {
    val ratio = 2.5f
    val whole = 3f
    val name = "Kompoz"
    Text("${name.length} harf, $ratio, $whole, ${ratio * 2}", modifier = Modifier.testTag("t"))
}

TEST(string_templates_write_numbers_as_kotlin_does) {
    KompozTestRule rule([] { Templates(); });
    CHECK(rule.onNodeWithTag("t").text() == "6 harf, 2.5, 3.0, 5.0");
}

@Kompozable
fun Collections() {
    val names = listOf("ayla", "ali", "veli")
    Column {
        names.filter { it.startsWith("a") }.forEach { Text(it.uppercase()) }
        Text(names.map { it.length }.joinToString("+"), modifier = Modifier.Companion.testTag("lengths"))
        val text = """
            bir
              iki
        """.trimIndent()
        Text(text, modifier = Modifier.testTag("indent"))
        val maybe: String? = null
        val some: String? = "dört"
        Text(maybe?.length?.toString() ?: "yok")
        Text("var: ${some?.length ?: 0}")
        for ((i, name) in names.withIndex()) {
            if (i == 2) Text("son: $name")
        }
    }
}

TEST(kotlin_standard_functions) {
    KompozTestRule rule([] { Collections(); });
    CHECK(rule.onNodeWithText("AYLA").exists() && rule.onNodeWithText("ALI").exists() && !rule.onNodeWithText("VELI").exists());
    CHECK(rule.onNodeWithTag("lengths").text() == "4+3+4");
    CHECK(rule.onNodeWithTag("indent").text() == "bir\n  iki");
    CHECK(rule.onNodeWithText("yok").exists() && rule.onNodeWithText("var: 4").exists() && rule.onNodeWithText("son: veli").exists());
}

@Kompozable
fun Ticker() {
    var ticks by remember { mutableStateOf(0) }
    LaunchedEffect(Unit) {
        while (ticks < 3) {
            delay(100)
            ticks += 1
        }
    }
    Text("tik $ticks")
}

TEST(launched_effect_is_a_coroutine) {
    KompozTestRule rule([] { Ticker(); });
    rule.advanceTimeBy(150);
    CHECK(rule.onNodeWithText("tik 1").exists());
    rule.advanceTimeBy(500);
    CHECK(rule.onNodeWithText("tik 3").exists());
}

data class Note(val id: Int, val title: String, val pinned: Boolean = false, val tags: List<String> = listOf())

@Kompozable
fun NoteRow(note: Note) {
    Text("${note.id}: ${note.title}${if (note.pinned) " *" else ""}")
}

@Kompozable
fun Notes() {
    val a = Note(title = "alışveriş", id = 1)
    val b = a.copy(pinned = true, title = "iş")
    Column {
        NoteRow(a)
        NoteRow(note = b)
        Text("$b", modifier = Modifier.testTag("str"))
        Text(if (a == b.copy(pinned = false, title = "alışveriş")) "eşit" else "farklı")
    }
}

TEST(data_classes) {
    KompozTestRule rule([] { Notes(); });
    CHECK(rule.onNodeWithText("1: alışveriş").exists() && rule.onNodeWithText("1: iş *").exists());
    CHECK(rule.onNodeWithTag("str").text() == "Note(id=1, title=iş, pinned=true, tags=[])");
    CHECK(rule.onNodeWithText("eşit").exists());
}

// The arguments a composable needs (onClick, value, checked, ...) by their names too.
@Kompozable
fun NamedHeads() {
    var on by remember { mutableStateOf(false) }
    var text by remember { mutableStateOf("") }
    Column {
        Checkbox(checked = on, onCheckedChange = { on = it }, modifier = Modifier.testTag("kutu"))
        TextButton(onClick = { text = "basıldı" }) { Text(text = "düğme") }
        TextField(value = text, onValueChange = { text = it }, modifier = Modifier.testTag("alan"))
        Icon(imageVector = Icons.Filled.Star, contentDescription = "yıldız")
        Text(text = if (on) "açık" else "kapalı")
        Text("değer: $text")
    }
}

TEST(required_arguments_by_name) {
    KompozTestRule rule([] { NamedHeads(); });
    CHECK(rule.onNodeWithText("kapalı").exists());
    rule.onNodeWithText("düğme").performClick();
    CHECK(rule.onNodeWithText("değer: basıldı").exists());
    rule.onNodeWithTag("alan").performTextInput("!");
    CHECK(rule.onNodeWithText("değer: basıldı!").exists());
    rule.onNodeWithTag("kutu").performClick();
    CHECK(rule.onNodeWithText("açık").exists());
}

// A C++ struct (of this file, of a header it includes, of Kompoz's) made with named arguments in
// any order: designated initializers in its fields' order.
struct Size2 {
    int width = 0;
    int height = 0;
};

@Kompozable
fun StructsByName() {
    val s = Size2(height = 3, width = 2)
    Column {
        Text("${s.width}x${s.height}", modifier = Modifier.testTag("boyut"))
        Text("stil", style = TextStyle(maxLines = 1, fontSize = 30.sp), modifier = Modifier.testTag("stil"))
    }
}

TEST(structs_by_name) {
    KompozTestRule rule([] { StructsByName(); });
    CHECK(rule.onNodeWithTag("boyut").text() == "2x3");
    CHECK(rule.onNodeWithTag("stil").bounds().height > 30);
}

// The user's own Compose code, as written for Android (textSize() is theirs too).
@Kompozable
fun textSize(): TextUnit = 18.sp

@Kompozable
fun AddTextDialog(
    colors: ColorScheme,
    title: String,
    backTitle: String,
    addTitle: String,
    onDismissRequest: () -> Unit,
    onCancel: () -> Unit,
    onSave: (String) -> Unit
) {
    Dialog(
        onDismissRequest = { onDismissRequest() },
        properties = DialogProperties(dismissOnBackPress = true, dismissOnClickOutside = true)
    ) {
        var isBlank by remember { mutableStateOf(false) }
        Card(
            modifier = Modifier
                .background(
                    brush = Brush.linearGradient(
                        listOf(
                            colors.primaryContainer.copy(alpha = 0.6f),
                            colors.tertiaryContainer.copy(alpha = 0.6f)
                        )
                    ),
                    shape = RoundedCornerShape(28.dp)
                )
                .fillMaxWidth()
                .fillMaxHeight(0.3f)
                .padding(4.dp),
            colors = CardDefaults.cardColors(containerColor = Color.Transparent),
            shape = RoundedCornerShape(28.dp)
        ) {
            var content by remember { mutableStateOf("") }
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(6.dp),
                verticalArrangement = Arrangement.SpaceEvenly,
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                TextField (
                    modifier = Modifier
                        .fillMaxWidth()
                        .fillMaxHeight(0.8f)
                        .border(width = 1.dp, color = colors.onBackground.copy(alpha = 0.4f), shape = RoundedCornerShape(24.dp)),
                    value = content,
                    onValueChange = {
                        content= it
                    },
                    textStyle = TextStyle(fontSize = textSize(), color = colors.onBackground),
                    shape = RoundedCornerShape(24.dp),
                    colors = TextFieldDefaults.colors(
                        focusedContainerColor = Color.Transparent,
                        unfocusedContainerColor = Color.Transparent,
                        disabledContainerColor = Color.Transparent,
                        errorContainerColor = Color.Transparent,
                        focusedPlaceholderColor = Color.Transparent,
                        unfocusedPlaceholderColor = Color.Transparent,
                        unfocusedIndicatorColor = Color.Transparent,
                        focusedIndicatorColor = Color.Transparent
                    )
                )
                Row {
                    TextButton(
                        onClick = { onCancel() },
                        colors = ButtonDefaults.textButtonColors(
                            containerColor = colors.primaryContainer.copy(alpha = 0.2f),
                            contentColor = colors.onPrimaryContainer
                        ),
                        modifier = Modifier.Companion
                            .width(140.dp)
                            .padding(8.dp),
                    ) {
                        Text(text = backTitle, fontSize = textSize())
                    }
                    TextButton(
                        onClick =
                            {
                                onSave(content)
                            },
                        colors = ButtonDefaults.textButtonColors(
                            containerColor = colors.primaryContainer.copy(alpha = 0.2f),
                            contentColor = colors.onPrimaryContainer
                        ),
                        modifier = Modifier
                            .width(140.dp)
                            .padding(8.dp),
                    ) {
                        Text(text = addTitle, fontSize = textSize())
                    }
                }
            }
        }
    }
}

@Kompozable
fun DialogHost(saved: (String) -> Unit) {
    var open by remember { mutableStateOf(true) }
    if (open) {
        AddTextDialog(
            colors = MaterialTheme.colorScheme,
            title = "",
            backTitle = "Vazgeç",
            addTitle = "Ekle",
            onDismissRequest = { open = false },
            onCancel = { open = false },
            onSave = {
                saved(it)
                open = false
            }
        )
    }
}

TEST(the_users_add_text_dialog) {
    auto saved = std::make_shared<std::string>();
    KompozTestRule rule([saved] { DialogHost([saved](std::string s) { *saved = s; }); });
    CHECK(rule.onNodeWithText("Vazgeç").exists() && rule.onNodeWithText("Ekle").exists());
    *saved = "-";
    rule.onNodeWithText("Ekle").performClick();
    CHECK(saved->empty());  // (nothing typed)
    CHECK(!rule.onNodeWithText("Ekle").exists());
    KompozTestRule cancel([saved] { DialogHost([saved](std::string s) { *saved = s; }); });
    *saved = "-";
    cancel.onNodeWithText("Vazgeç").performClick();
    CHECK(*saved == "-" && !cancel.onNodeWithText("Vazgeç").exists());
}

// Compose's Scaffold sample, as the user wrote it: topAppBarColors imported (called without TopAppBarDefaults),
// the icon from an object of the app's own.
object AppIcons {
    val Add = Icons.Default.Add
}

@Kompozable
fun ScaffoldExample() {
    var presses by remember { mutableIntStateOf(0) }

    Scaffold(
        topBar = {
            TopAppBar(
                colors = topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer,
                    titleContentColor = MaterialTheme.colorScheme.primary,
                ),
                title = {
                    Text("Top app bar")
                }
            )
        },
        bottomBar = {
            BottomAppBar(
                containerColor = MaterialTheme.colorScheme.primaryContainer,
                contentColor = MaterialTheme.colorScheme.primary,
            ) {
                Text(
                    modifier = Modifier
                        .fillMaxWidth(),
                    textAlign = TextAlign.Center,
                    text = "Bottom app bar",
                )
            }
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { presses++ }) {
                Icon(painter = rememberVectorPainter(AppIcons.Add), contentDescription = "Add")
            }
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .padding(innerPadding),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Text(
                modifier = Modifier.padding(8.dp),
                text =
                """
                    This is an example of a scaffold. It uses the Scaffold composable's parameters to create a screen with a simple top app bar, bottom app bar, and floating action button.

                    It also contains some basic inner content, such as this text.

                    You have pressed the floating action button $presses times.
                """.trimIndent(),
            )
        }
    }
}

// Kotlin's objects with members (an app's singletons): vals (const too), a var, a private one, functions with a
// default, an init block that runs first; Kotlin's String + any value.
object Tally {
    const val Step = 2
    var total = 0
    private val label = "toplam"
    fun add(n: Int = Step): Int {
        total += n
        return total
    }
    fun describe() = label + " " + total
    init {
        total = 10
    }
}

@Kompozable
fun ObjectForms() {
    var shown by remember { mutableStateOf(Tally.describe()) }
    Column {
        Text(shown)
        Button(onClick = {
            Tally.add()
            shown = Tally.describe() + " / adım " + Tally.Step + " / " + 1.5f
        }) { Text("Ekle") }
    }
}

TEST(objects_with_members) {
    KompozTestRule rule([] { ObjectForms(); });
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("toplam 10").exists());
    rule.onNodeWithText("Ekle").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("toplam 12 / adım 2 / 1.5").exists());
}

// Kotlin's sealed classes and interfaces: nested subclasses (a data object, a data class) and the file's own; when
// with is (the subject seen as the T: smart casts), if (x is T && …), as?, == and toString.
sealed class LoadState {
    data object Loading : LoadState()
    data class Ready(val items: List<String>) : LoadState()
}

data class Broken(val message: String) : LoadState()

// (another with subclasses of the same names: each its own)
sealed class SaveState {
    data object Loading : SaveState()
    data class Ready(val count: Int) : SaveState()
}

sealed interface Figure

data class Disc(val r: Float) : Figure

object Speck : Figure

@Kompozable
fun LoadView(state: LoadState) {
    when (state) {
        is LoadState.Loading -> Text("yükleniyor")
        is LoadState.Ready -> Text("${state.items.size} öğe")
        is Broken -> Text("hata: " + state.message)
    }
}

fun describe(figure: Figure): String = when (figure) {
    is Disc -> "disk " + figure.r
    is Speck -> "nokta"
}

fun area(figure: Figure): Float = if (figure is Disc && figure.r > 0f) figure.r * figure.r else 0f

@Kompozable
fun SealedForms() {
    var state by remember { mutableStateOf<LoadState>(LoadState.Loading) }
    Column {
        LoadView(state)
        Text(describe(Disc(2f)) + " " + area(Speck) + " " + area(Disc(3f)) + " " + (state == LoadState.Loading) + " " + state)
        Text("kayıt " + SaveState.Ready(count = 3) + " " + SaveState.Loading)
        val broken = state as? Broken
        if (broken != null) Text("ayrıntı: " + broken.message)
        Button(onClick = { state = LoadState.Ready(listOf("a", "b")) }) { Text("Yükle") }
        Button(onClick = { state = Broken("yok") }) { Text("Boz") }
    }
}

TEST(sealed_classes_and_is) {
    KompozTestRule rule([] { SealedForms(); });
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("yükleniyor").exists());
    std::string line = rule.onNodeWithText("disk ", true).text();
    CHECK(line == "disk 2.0 0.0 9.0 true Loading" || (std::printf("satır: %s\n", line.c_str()), false));
    CHECK(rule.onNodeWithText("kayıt Ready(count=3) Loading").exists());
    rule.onNodeWithText("Yükle").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("2 öğe").exists() && rule.onNodeWithText("disk 2.0 0.0 9.0 false Ready(items=[a, b])").exists());
    rule.onNodeWithText("Boz").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("hata: yok").exists() && rule.onNodeWithText("ayrıntı: yok").exists());
}

TEST(composes_scaffold_sample) {
    KompozTestRule rule([] { ScaffoldExample(); });
    CHECK(rule.onNodeWithText("Top app bar").exists() && rule.onNodeWithText("Bottom app bar").exists());
    CHECK(rule.onNodeWithText("You have pressed the floating action button 0 times.", true).exists());
    rule.onNodeWithContentDescription("Add").performClick();
    rule.onNodeWithContentDescription("Add").performClick();
    CHECK(rule.onNodeWithText("You have pressed the floating action button 2 times.", true).exists());
    CHECK(rule.onNodeWithText("This is an example of a scaffold.", true).text().starts_with("This is an example"));  // (trimIndent: no indent)
}

// From kompozable_tests_other.cpp, as from another Kotlin file of the module.
@Kompozable
fun OtherFile() {
    Column {
        StickerRow(suffix = "!", sticker = Sticker(count = 3, label = "yeni"))
        StickerRow(Sticker("eski"))
    }
}

TEST(another_files_composables) {
    KompozTestRule rule([] { OtherFile(); });
    CHECK(rule.onNodeWithText("yeni:3!").exists() && rule.onNodeWithText("eski:0").exists());
}

// From this file's C++ too: a file of the target still in C++ calls the module's Kotlin (the
// other file's function and data class) with no header, as the showcase's files do.
TEST(cpp_calls_the_modules_kotlin) {
    KompozTestRule rule([] { StickerRow(Sticker{"çıkartma", 3}, "", "!"); });
    CHECK(rule.onNodeWithText("çıkartma:3!").exists());
}

// Kotlin's String.format with Java's specifiers, more of its collection functions, x++ as a value
// on a state, ListItemDefaults.colors and a constexpr constructor called by place (CornerRadius(24f)).
@Kompozable
fun KotlinHelpers() {
    var nextId by remember { mutableStateOf(4) }
    val ids = remember { mutableStateListOf(1, 2, 3) }
    val words = listOf("elma", "armut", "erik", "ayva", "kiraz")
    val (a, rest) = words.partition { it.startsWith("a") }
    val byLetter = words.groupBy { it.first() }
    val best = words.maxByOrNull { it.length }
    Column {
        Text("%.1f km · %03d dk · %-4s| · %x".format(1.25f, 7, "ab", 255))
        Text("ilk ${words.indexOfFirst { it.startsWith("e") }} son ${words.indexOfLast { it.startsWith("e") }}")
        Text("a: ${a.joinToString()} · öteki: ${rest.size} · gruplar: ${byLetter.size} · en uzun: $best")
        Text("azalan: ${words.sortedByDescending { it.length }.first()} · parça: ${words.chunked(2).size} · tek: ${words.distinctBy { it.first() }.size}")
        Text("eşle: ${words.associateWith { it.length }.size} · düz: ${listOf(listOf(1, 2), listOf(3)).flatMap { it }.sum()}")
        Button(onClick = { ids.add(nextId++) }) { Text("Ekle") }
        Button(onClick = { ids.removeIf { it % 2 == 0 } }) { Text("Çiftleri sil") }
        Text("kimlikler: ${ids.joinToString(",")} · sonraki: $nextId")
        ListItem(headlineContent = { Text("Saydam satır") }, colors = ListItemDefaults.colors(containerColor = Color.Transparent))
        Canvas(modifier = Modifier.size(40.dp)) { drawRoundRect(Color.Red, cornerRadius = CornerRadius(24f)) }
    }
}

TEST(kotlin_helpers_through_kompozc) {
    KompozTestRule rule([] { KotlinHelpers(); });
    CHECK(rule.onNodeWithText("1.2 km · 007 dk · ab  | · ff").exists() || rule.onNodeWithText("1.3 km · 007 dk · ab  | · ff").exists());
    CHECK(rule.onNodeWithText("ilk 0 son 2").exists());
    CHECK(rule.onNodeWithText("a: armut, ayva · öteki: 3 · gruplar: 3 · en uzun: armut").exists());
    CHECK(rule.onNodeWithText("azalan: armut · parça: 3 · tek: 3").exists());
    CHECK(rule.onNodeWithText("eşle: 5 · düz: 6").exists());
    rule.onNodeWithText("Ekle").performClick();
    rule.onNodeWithText("Ekle").performClick();
    CHECK(rule.onNodeWithText("kimlikler: 1,2,3,4,5 · sonraki: 6").exists());
    rule.onNodeWithText("Çiftleri sil").performClick();
    CHECK(rule.onNodeWithText("kimlikler: 1,3,5 · sonraki: 6").exists());
    CHECK(rule.onNodeWithText("Saydam satır").exists());
}

// What Compose code says most: a lazy list's items, DisposableEffect, providing a local, a themed surface.
struct Mail {
    std::string from;  // (no ==: a list of it is never compared, so its composable isn't skipped)
};

@Kompozable
fun Inbox(mails: List<Mail>, log: (String) -> Unit) {
    DisposableEffect(mails.size) {
        log("açıldı ${mails.size}")
        onDispose { log("kapandı") }
    }
    MaterialTheme(colorScheme = darkColorScheme()) {
        Surface(shape = RoundedCornerShape(8.dp), color = MaterialTheme.colorScheme.surface) {
            KompozitionLocalProvider(LocalContentColor provides Color.Red) {
                LazyColumn(modifier = Modifier.height(300.dp)) {
                    item { Text("Gelen kutusu") }
                    itemsIndexed(mails) { index, mail -> Text("$index: ${mail.from}", color = LocalContentColor.current) }
                }
            }
        }
    }
}

TEST(lazy_items_effects_and_locals) {
    auto log = std::make_shared<std::vector<std::string>>();
    auto show = mutableStateOf(true);
    KompozTestRule rule([log, show] {
        if (show.get()) Inbox({Mail{"ayşe"}, Mail{"ali"}}, [log](std::string s) { log->push_back(std::move(s)); });
    });
    CHECK(rule.onNodeWithText("Gelen kutusu").exists() && rule.onNodeWithText("1: ali").exists());
    show.set(false);
    rule.waitForIdle();
    CHECK((*log == std::vector<std::string>{"açıldı 2", "kapandı"}));
}

// ListItem as Compose writes it: its slots by name; an interactive one's headline is its block.
@Kompozable
fun Settings() {
    var wifi by remember { mutableStateOf(false) }
    var opened by remember { mutableStateOf(0) }
    Column {
        ListItem(headlineContent = { Text("Wi-Fi") }, supportingContent = { Text(if (wifi) "Açık" else "Kapalı") })
        ListItem(checked = wifi, onCheckedChange = { wifi = it }) { Text("Wi-Fi'yi aç") }
        ListItem(onClick = { opened++ }, supportingContent = { Text("$opened kez") }) { Text("Hakkında") }
    }
}

TEST(list_items_by_name) {
    KompozTestRule rule([] { Settings(); });
    CHECK(rule.onNodeWithText("Kapalı").exists() && rule.onNodeWithText("0 kez").exists());
    rule.onNodeWithText("Wi-Fi'yi aç").performClick();
    rule.onNodeWithText("Hakkında").performClick();
    CHECK(rule.onNodeWithText("Açık").exists() && rule.onNodeWithText("1 kez").exists());
}

// A lazy list's items with Compose's key = { … }: an item's state follows it when the list changes.
data class Row2(val id: Int, val label: String)

@Kompozable
fun KeyedRow(row: Row2) {
    var taps by remember { mutableStateOf(0) }
    Text("${row.label}:$taps", modifier = Modifier.clickable { taps++ })
}

@Kompozable
fun KeyedList(rows: List<Row2>) {
    LazyColumn(modifier = Modifier.height(400.dp)) {
        item(key = "başlık") { Text("Satırlar") }
        items(rows, key = { it.id }) { row -> KeyedRow(row) }
        item { Text(if (rows.size > 1) "çok" else "${Int.MAX_VALUE > 0}") }
    }
}

TEST(lazy_keys_by_name) {
    auto rows = mutableStateOf(std::vector<Row2>{Row2{1, "bir"}, Row2{2, "iki"}});
    KompozTestRule rule([rows] { KeyedList(rows.get()); });
    rule.onNodeWithText("iki:0").performClick();
    CHECK(rule.onNodeWithText("iki:1").exists());
    rows.set({Row2{2, "iki"}});  // the first one gone: "iki" keeps its taps
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("iki:1").exists() && rule.onNodeWithText("true").exists());
}

// Compose's state forms: a getter read as a property, update { it.add(…) } in place, a list made
// item by item, a policy without a type, a saver by name.
struct Town {
    std::string name;
};
const auto townSaver = mapSaver<Town>([](const Town& t) { return SavedValue::Map{{"name", t.name}}; },
                                      [](const SavedValue::Map& m) -> std::optional<Town> {
                                          auto n = m.at("name").string();
                                          return n ? std::optional(Town{*n}) : std::nullopt;
                                      });

@Kompozable
fun StateForms() {
    val list = rememberLazyListState()
    val todos = remember { mutableStateOf(listOf("bir")) }
    val tags = remember { mutableStateListOf("a", "b") }
    var runs by remember { mutableStateOf(0, neverEqualPolicy()) }
    val town by rememberSaveable(saver = townSaver) { mutableStateOf(Town("İzmir")) }
    Column {
        Text("ilk görünen ${list.firstVisibleItemIndex}")
        Text("yapılacak ${todos.value.size}")
        Text("etiket ${tags.size}")
        Text("${town.name} $runs")
        Button(onClick = {
            todos.update { it.add("iki") }
            tags.add("c")
            runs = runs
        }) { Text("ekle") }
    }
}

TEST(state_forms_as_compose_writes_them) {
    KompozTestRule rule([] { StateForms(); });
    CHECK(rule.onNodeWithText("ilk görünen 0").exists() && rule.onNodeWithText("yapılacak 1").exists());
    CHECK(rule.onNodeWithText("etiket 2").exists() && rule.onNodeWithText("İzmir 0").exists());
    rule.onNodeWithText("ekle").performClick();
    CHECK(rule.onNodeWithText("yapılacak 2").exists() && rule.onNodeWithText("etiket 3").exists());
}

// In a coroutine, a call that suspends (a TaskOf<T> of yours, a Deferred's await()) is awaited,
// any other gives its value at once; produceState's block sets `value`.
TaskOf<int> slowDouble(int x) {
    co_await delay(std::chrono::milliseconds(200));
    co_return x * 2;
}

@Kompozable
fun Awaits() {
    val produced by produceState(0, 21) { value = slowDouble(21) }
    var both by remember { mutableStateOf("…") }
    val scope = rememberCoroutineScope()
    LaunchedEffect(Unit) {
        val a = scope.async { slowDouble(1) }
        val b = scope.async { slowDouble(2) }
        both = "${a.await() + b.await()} ${"x".uppercase()}"
    }
    Column {
        Text("üretilen $produced")
        Text("ikisi $both")
    }
}

// Kotlin's interfaces: abstract and default properties and functions, one extending another, implemented by
// a class (a constructor's override val), a ViewModel, a data class, an object and an object expression; a
// fun interface made from a lambda; is / as on them, == (a data class's: equal values).
interface Titled {
    val name: String
    fun title(): String = "[$name]"
}

interface Shelf : Titled {
    val items: List<String>
    var picks: Int
    fun count(): Int
    fun describe() = title() + " " + count()
}

class FakeShelf(override val name: String = "sahte") : Shelf {
    override val items = listOf("a", "b")
    override var picks = 0
    override fun count() = items.size
}

object EmptyShelf : Shelf {
    override val name = "boş"
    override val items: List<String> = emptyList()
    override var picks = 0
    override fun count() = 0
}

data class Tag(override val name: String) : Titled

class ShelfViewModel : ViewModel(), Titled {
    override val name: String get() = "raf"
    override fun title() = "<$name>"
}

object NewEmblem : Emblem {
    override val text = "yeni"
}

data class CountEmblem(override val text: String) : Emblem

fun interface Check {
    fun test(text: String): Boolean
}

fun titleOf(named: Titled): String = named.title()

fun kindOf(t: Titled): String = when (t) {
    is Tag -> "etiket " + t.name
    is FakeShelf -> "raf " + t.items.size + " " + (t as? Shelf)?.count()
    else -> "başka"
}

@Kompozable
fun InterfaceForms() {
    val vm = viewModel<ShelfViewModel>()
    val nonEmpty = Check { it.isNotEmpty() }
    val anonymous = object : Titled { override val name = "adsız" }
    var shelf by remember { mutableStateOf<Shelf>(FakeShelf()) }
    Column {
        Text(shelf.describe())
        Text(titleOf(vm) + " " + titleOf(anonymous) + " " + titleOf(EmptyShelf) + " " + titleOf(Tag("etiket")) + " " + EmptyShelf.title())
        Text("geçerli " + nonEmpty.test("x") + " " + nonEmpty.test(""))
        Text(NewEmblem.shown() + CountEmblem("3").shown())
        Text(kindOf(Tag("x")) + "; " + kindOf(FakeShelf()) + "; " + kindOf(vm))
        Text(if (shelf is FakeShelf) "sahte: " + (shelf as FakeShelf).items + " " + shelf.picks else "başka " + (shelf == EmptyShelf))
        val tags: List<Titled> = listOf(Tag("x"), Tag("x"))
        Text("eşit " + (tags[0] == tags[1]) + " " + (tags[0] is Tag) + " " + (tags[0] as Tag) + " " + (tags[0] is FakeShelf))
        Button(onClick = { shelf.picks++ ; shelf = shelf }) { Text("Seç") }
        Button(onClick = { shelf = EmptyShelf }) { Text("Boşalt") }
    }
}

TEST(interfaces_as_kotlin_writes_them) {
    KompozTestRule rule([] { InterfaceForms(); });
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("[sahte] 2").exists());
    CHECK(rule.onNodeWithText("<raf> [adsız] [boş] [etiket] [boş]").exists());
    CHECK(rule.onNodeWithText("geçerli true false").exists() && rule.onNodeWithText("<yeni><3>").exists());
    CHECK(rule.onNodeWithText("etiket x; raf 2 2; başka").exists());
    CHECK(rule.onNodeWithText("sahte: [a, b] 0").exists());
    CHECK(rule.onNodeWithText("eşit true true Tag(name=x) false").exists());
    rule.onNodeWithText("Seç").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("sahte: [a, b] 1").exists());
    rule.onNodeWithText("Boşalt").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("[boş] 0").exists() && rule.onNodeWithText("başka true").exists());
}

// A class's properties by a delegate (var count by mutableStateOf(0) with a private set, by derivedStateOf, by lazy),
// with get() and set(v) (`field`), in a ViewModel, a plain state holder and an object; read from a composable.
class CounterViewModel : ViewModel() {
    var count by mutableStateOf(0)
        private set
    val doubled by derivedStateOf { count * 2 }
    val label: String get() = "sayaç $count"
    var step = 1
        set(value) { field = if (value > 0) value else 1 }

    fun increment() { count += step }
}

class FormState {
    var text by mutableStateOf("")
    val isValid get() = text.length >= 3
    val greeting by lazy { "merhaba" }
}

object Prefs {
    var dark by mutableStateOf(false)
    val mode get() = if (dark) "koyu" else "açık"
}

@Kompozable
fun DelegatedMembers() {
    val vm = viewModel<CounterViewModel>()
    val form = remember { FormState() }
    Column {
        Text("${vm.count} ${vm.doubled} ${vm.label}")
        Text("${form.text}|${form.isValid}|${form.greeting} ${Prefs.mode}")
        Button(onClick = { vm.increment() }) { Text("Artır") }
        Button(onClick = {
            vm.step = -4
            vm.step = vm.step + 2
            form.text = "abc"
            Prefs.dark = !Prefs.dark
        }) { Text("Adım") }
    }
}

TEST(class_properties_by_delegates_and_accessors) {
    KompozTestRule rule([] { DelegatedMembers(); });
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("0 0 sayaç 0").exists() && rule.onNodeWithText("|false|merhaba açık").exists());
    rule.onNodeWithText("Artır").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("1 2 sayaç 1").exists());
    rule.onNodeWithText("Adım").performClick();
    rule.onNodeWithText("Artır").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("4 8 sayaç 4").exists() && rule.onNodeWithText("abc|true|merhaba koyu").exists());
}

// suspend fun: a coroutine, awaited where it is called from one (a LaunchedEffect, another suspend fun): at the
// top level (its result written, or its expression's), a class's, an interface's implemented by a class.
suspend fun greet(name: String): String {
    delay(100)
    return "merhaba $name"
}

suspend fun twice(x: Int) = slowDouble(x) + slowDouble(x)

suspend fun shortWait() {
    delay(50)
}

class Loader {
    var loads = 0
    suspend fun load(n: Int): List<Int> {
        shortWait()
        loads++
        return listOf(1, 2, 3).take(n)
    }
}

interface Source {
    suspend fun next(): Int
}

class Counting : Source {
    var n = 0
    override suspend fun next(): Int {
        delay(10)
        n++
        return n
    }
}

@Kompozable
fun SuspendFunctions() {
    var text by remember { mutableStateOf("…") }
    LaunchedEffect(Unit) {
        val loader = Loader()
        val source: Source = Counting()
        val g = greet("dünya")
        val items = loader.load(2)
        source.next()
        text = g + " " + items + " " + twice(5) + " " + source.next() + " " + loader.loads
    }
    Text(text)
}

TEST(suspend_functions) {
    KompozTestRule rule([] { SuspendFunctions(); });
    CHECK(rule.onNodeWithText("…").exists());
    rule.advanceTimeBy(1500);
    CHECK(rule.onNodeWithText("merhaba dünya [1, 2] 20 2 1").exists() || (std::printf("metin: %s\n", rule.onNodeWithText("merhaba", true).text().c_str()), false));
}

// A data class's body: functions (one implementing an interface's), a property computed as it is made, a getter, a
// lazy one, an init block; copy() makes it again (its body's values anew), == and toString() over its fields only.
data class Receipt(val item: String, val price: Int, val count: Int = 1) : Titled {
    val total = price * count
    val isBig get() = total >= 100
    val label by lazy { "$item x$count" }
    override val name: String get() = item

    init {
        require(count > 0)
    }

    fun withTax(rate: Int): Int = total + total * rate / 100
    fun describe() = "$label = $total" + (if (isBig) " (büyük)" else "")
}

@Kompozable
fun ReceiptView() {
    val r = Receipt("kalem", 30)
    val more = r.copy(count = 4)
    Column {
        Text(r.describe() + " | " + more.describe() + " | " + more.withTax(10))
        Text("" + r + " " + (r == Receipt("kalem", 30)) + " " + titleOf(more))
    }
}

TEST(data_class_bodies) {
    KompozTestRule rule([] { ReceiptView(); });
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("kalem x1 = 30 | kalem x4 = 120 (büyük) | 132").exists());
    std::string second = rule.onNodeWithText("Receipt(", true).text();
    CHECK(second == "Receipt(item=kalem, price=30, count=1) true [kalem]" || (std::printf("satır: %s\n", second.c_str()), false));
}

// Abstract and open classes: abstract properties and functions, open ones overridden (super.describe()), a final one
// inherited; subclasses as their base (a List<Plate>), is / as on them.
abstract class Plate(val label: String) {
    abstract val corners: Int
    abstract fun area(): Float
    open fun describe() = "$label: $corners köşe, alan ${area()}"
    fun shout() = label.uppercase()
}

class Square(val side: Float) : Plate("kare") {
    override val corners = 4
    override fun area() = side * side
}

class Round(val r: Float) : Plate("daire") {
    override val corners: Int get() = 0
    override fun area() = 3f * r * r
    override fun describe() = "yuvarlak " + super.describe()
}

open class Stepper {
    var total = 0
    open fun step() = 1
    fun tick() { total += step() }
}

class DoubleStepper : Stepper() {
    override fun step() = 2
}

abstract class PageViewModel : ViewModel() {
    abstract fun heading(): String
    fun banner() = "<" + heading() + ">"
}

class StartViewModel : PageViewModel() {
    override fun heading() = "başlangıç"
}

@Kompozable
fun PlateForms() {
    val plates: List<Plate> = listOf(Square(2f), Round(1f))
    val stepper: Stepper = DoubleStepper()
    val page = viewModel<StartViewModel>()
    stepper.tick()
    stepper.tick()
    Column {
        for (p in plates) Text(p.describe())
        Text(page.banner())
        Text(plates[0].shout() + " " + (plates[1] is Round) + " " + (plates[0] is Round) + " " + (plates[0] as Square).side + " " + stepper.total)
    }
}

TEST(abstract_and_open_classes) {
    KompozTestRule rule([] { PlateForms(); });
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("kare: 4 köşe, alan 4.0").exists());
    CHECK(rule.onNodeWithText("yuvarlak daire: 0 köşe, alan 3.0").exists() && rule.onNodeWithText("<başlangıç>").exists());
    std::string line = rule.onNodeWithText("KARE", true).text();
    CHECK(line == "KARE true false 2.0 4" || (std::printf("satır: %s\n", line.c_str()), false));
}

// Generic classes, data classes and interfaces: their type arguments written or taken from the values, an interface's
// default function over its T, a class deriving from a generic one (super), a generic fun interface from a lambda.
interface Feed<T> {
    fun next(): T
    fun two(): List<T> = listOf(next(), next())  // (Kotlin's order: the first next() first)
}

class Countdown(var from: Int) : Feed<Int> {
    override fun next(): Int {
        from -= 1
        return from + 1
    }
}

class Repeater<T>(private val value: T) : Feed<T> {
    override fun next() = value
}

class Crate<T>(val value: T) {
    fun get(): T = value
    fun describe() = "Crate($value)"
    fun <R> convert(f: (T) -> R): Crate<R> = Crate(f(value))
}

fun <T> firstOr(items: List<T>, fallback: T): T = if (items.isEmpty()) fallback else items.first()

fun <T, R> mapAll(items: List<T>, transform: (T) -> R): List<R> = items.map { transform(it) }

fun <T> List<T>.second(): T = this[1]

fun <T> emptyBag(): List<T> = emptyList()

@Kompozable
fun <T> LabeledRow(items: List<T>, label: (T) -> String) {
    Row {
        for (x in items) Text(label(x))
    }
}

open class Holder<T>(val item: T) {
    open fun show() = "tutar: $item"
}

class NamedHolder(item: String) : Holder<String>(item) {
    override fun show() = "ad " + super.show()
}

data class Duo<A, B>(val first: A, val second: B) {
    fun swap() = Duo(second, first)
}

fun interface Mapper<T> {
    fun map(x: T): String
}

@Kompozable
fun GenericForms() {
    val box = Crate("kutu")
    val ints: Crate<Int> = Crate(3)
    val duo = Duo(1, "bir")
    val src: Feed<Int> = Countdown(3)
    val rep: Feed<String> = Repeater("a")
    val mapper = Mapper<Int> { "#$it" }
    Column {
        Text(box.get() + " " + ints.get() + " " + box.describe() + " " + Crate<String>("b").value)
        Text("" + duo + " " + duo.swap() + " " + (duo == Duo(1, "bir")))
        Text("" + src.two() + " " + rep.two() + " " + mapper.map(5) + " " + NamedHolder("x").show())
        Text("işlev " + firstOr(listOf(4, 5), 0) + " " + firstOr(emptyBag<Int>(), 7) + " " + mapAll(listOf(1, 2)) { it * 10 } + " " +
            listOf("a", "b").second() + " " + ints.convert { "#$it" }.get())
        LabeledRow(listOf(1, 2)) { "etiket $it" }
    }
}

TEST(generic_classes_and_interfaces) {
    KompozTestRule rule([] { GenericForms(); });
    rule.waitForIdle();
    std::string first = rule.onNodeWithText("kutu", true).text(), second = rule.onNodeWithText("bir", true).text();
    CHECK(first == "kutu 3 Crate(kutu) b" || (std::printf("satır: %s\n", first.c_str()), false));
    CHECK(second == "Duo(first=1, second=bir) Duo(first=bir, second=1) true" || (std::printf("satır: %s\n", second.c_str()), false));
    std::string line = rule.onNodeWithText("#5", true).text();
    CHECK(line == "[3, 2] [a, a] #5 ad tutar: x" || (std::printf("satır: %s\n", line.c_str()), false));
    std::string fn = rule.onNodeWithText("işlev", true).text();
    CHECK(fn == "işlev 4 7 [10, 20] b #3" || (std::printf("satır: %s\n", fn.c_str()), false));
    CHECK(rule.onNodeWithText("etiket 2").exists());
}

// Kotlin's order of evaluation, left to right: a call's arguments (named ones as written, not as declared), a
// string template's parts, an operator's operands, a variable read before what changes it.
class Clicker {
    var n = 0
    fun next(): Int {
        n++
        return n
    }
}

fun pairOf(a: Int, b: Int) = "$a,$b"

data class Spot(val x: Int, val y: Int)

@Kompozable
fun ShowPair(a: Int, b: Int) {
    Text("çift $a $b")
}

@Kompozable
fun OrderForms() {
    val c = remember { Clicker() }
    val list = listOf(c.next(), c.next(), c.next())
    val text = "${c.next()}-${c.next()}"
    val sum = c.next() * 10 + c.next()
    val pair = pairOf(c.next(), c.next())
    val spot = Spot(y = c.next(), x = c.next())
    var i = 0
    val read = pairOf(i, i++)
    Column {
        Text("sıra $list $text $sum $pair $spot $read")
        ShowPair(b = c.next(), a = c.next())
    }
}

TEST(kotlins_order_of_evaluation) {
    KompozTestRule rule([] { OrderForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("sıra", true).text();
    CHECK(line == "sıra [1, 2, 3] 4-5 67 8,9 Spot(x=11, y=10) 0,0" || (std::printf("satır: %s\n", line.c_str()), false));
    CHECK(rule.onNodeWithText("çift 13 12").exists());
}

// companion object: a class's, a data class's and an interface's static members: a const val, a var, a value of the
// class itself, functions; read by their names inside the class, as Name.member outside.
class Ledger(val id: Int) {
    fun label() = "$PREFIX$id"

    companion object {
        const val PREFIX = "hesap-"
        var made = 0
        fun create(): Ledger {
            made++
            return Ledger(made)
        }
    }
}

data class Money(val cents: Int) {
    companion object {
        val Zero = Money(0)
        fun of(lira: Int) = Money(lira * 100)
    }
}

interface Shade {
    val tone: String

    companion object {
        const val Default = "açık"
    }
}

@Kompozable
fun CompanionForms() {
    val first = remember { Ledger.create() }
    val second = remember { Ledger.create() }
    Text(first.label() + " " + second.label() + " " + Ledger.made + " " + Ledger.PREFIX + " " + Money.of(3) + " " + Money.Zero + " " + Shade.Default)
}

TEST(companion_objects) {
    KompozTestRule rule([] { CompanionForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("hesap-1", true).text();
    CHECK(line == "hesap-1 hesap-2 2 hesap- Money(cents=300) Money(cents=0) açık" || (std::printf("satır: %s\n", line.c_str()), false));
}

// Enum classes: one with a constructor (defaults, named arguments) and a body (a property, functions, a companion),
// one with a body only, one of entries alone; name, ordinal, entries, values(), valueOf(), "$x", when over them.
enum class Rank(val n: Int, val label: String = "seviye") {
    LOW(1), MID(n = 5, label = "orta"), HIGH(9);

    val doubled = n * 2
    fun next(): Rank = if (this == LOW) MID else HIGH
    fun describe() = "$name/$ordinal/$label/$doubled"

    companion object {
        fun of(n: Int) = Rank.entries.first { it.n >= n }
    }
}

enum class Turn { LEFT, RIGHT; fun flip() = if (this == LEFT) RIGHT else LEFT }

enum class Suit { HEART, SPADE }

@Kompozable
fun EnumForms() {
    val low = Rank.LOW
    Column {
        Text("rütbe " + low.describe() + " " + low.next() + " " + Rank.MID.describe() + " " + Rank.valueOf("HIGH").n + " " + Rank.entries.size + " " + Rank.of(4))
        Text("renk " + Turn.LEFT.flip() + " " + Suit.SPADE + " " + Suit.SPADE.name + " " + Suit.SPADE.ordinal + " " + Suit.values().size + " " +
            Suit.valueOf("HEART") + " " + when (low) {
                Rank.LOW -> "düşük"
                else -> "başka"
            })
        val nums = listOf(1, 2, 3, 4)
        Text("koşul " + nums.first { it > 1 } + " " + nums.last { it % 2 == 1 } + " " + nums.firstOrNull { it > 9 } + " " + nums.lastOrNull { it < 3 })
    }
}

TEST(enum_classes_with_bodies) {
    KompozTestRule rule([] { EnumForms(); });
    rule.waitForIdle();
    std::string first = rule.onNodeWithText("rütbe", true).text(), second = rule.onNodeWithText("renk", true).text();
    CHECK(first == "rütbe LOW/0/seviye/2 MID MID/1/orta/10 9 3 MID" || (std::printf("satır: %s\n", first.c_str()), false));
    CHECK(second == "renk RIGHT SPADE SPADE 1 2 HEART düşük" || (std::printf("satır: %s\n", second.c_str()), false));
    CHECK(rule.onNodeWithText("koşul 2 3 null 2").exists());
}

// Nested declarations: a data class, enums (entries alone, and with a constructor), an object, an interface and a
// class inside a class, named alone inside it and with it outside (Catalog.Entry, Catalog.Kind.FILM).
class Catalog {
    data class Entry(val title: String, val kind: Kind = Kind.BOOK)
    enum class Kind { BOOK, FILM }
    enum class Tier(val price: Int) { FREE(0), PAID(5) }
    object Defaults {
        const val limit = 2
    }
    interface Filter {
        fun keep(e: Entry): Boolean
    }
    class Shelf(val entries: List<Entry>) {
        fun count() = entries.size
    }

    private val items = listOf(Entry("a"), Entry("b", Kind.FILM), Entry("c"))
    fun films(): List<Entry> = items.filter { it.kind == Kind.FILM }
    fun shelf() = Shelf(items.take(Defaults.limit))
}

@Kompozable
fun NestedForms() {
    val catalog = remember { Catalog() }
    val entry = Catalog.Entry("d", Catalog.Kind.FILM)
    val books = object : Catalog.Filter {
        override fun keep(e: Catalog.Entry) = e.kind == Catalog.Kind.BOOK
    }
    Text("iç " + catalog.films().size + " " + catalog.shelf().count() + " " + entry + " " + Catalog.Tier.PAID.price + " " + Catalog.Kind.entries.size +
        " " + books.keep(entry) + " " + Catalog.Defaults.limit)
}

TEST(nested_declarations) {
    KompozTestRule rule([] { NestedForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("iç", true).text();
    CHECK(line == "iç 1 2 Entry(title=d, kind=FILM) 5 2 false 2" || (std::printf("satır: %s\n", line.c_str()), false));
}

// typealias: of a list, of function types (a lambda given one that gives a value gives it), a generic one and one of
// it, one naming a data class.
typealias Names = List<String>
typealias Pick = (String) -> Unit
typealias Format = (Int) -> String
typealias Cells<T> = List<List<T>>
typealias Board = Cells<Int>
typealias Stock = List<Receipt>

fun countNames(names: Names) = names.size
fun formatAll(xs: List<Int>, f: Format): Names = xs.map { f(it) }
fun total(grid: Cells<Int>) = grid.sumOf { it.sum() }

@Kompozable
fun AliasForms(onPick: Pick = {}) {
    val names: Names = listOf("a", "b")
    val board: Board = listOf(listOf(1, 2), listOf(3))
    val stock: Stock = listOf(Receipt("kalem", 30))
    Text("ad " + countNames(names) + " " + formatAll(listOf(1, 2)) { "#$it" } + " " + total(board) + " " + stock.size)
}

TEST(type_aliases) {
    KompozTestRule rule([] { AliasForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("ad ", true).text();
    CHECK(line == "ad 2 [#1, #2] 6 1" || (std::printf("satır: %s\n", line.c_str()), false));
}

// operator fun: a data class's +, -, unary -, ++, [], compareTo, contains (and += from plus), an extension's *, a
// class's invoke and plusAssign.
data class Vec2(val x: Int, val y: Int) {
    operator fun plus(o: Vec2) = Vec2(x + o.x, y + o.y)
    operator fun minus(o: Vec2) = Vec2(x - o.x, y - o.y)
    operator fun unaryMinus() = Vec2(-x, -y)
    operator fun inc() = Vec2(x + 1, y + 1)
    operator fun get(i: Int) = if (i == 0) x else y
    operator fun compareTo(o: Vec2) = (x + y) - (o.x + o.y)
    operator fun contains(v: Int) = v == x || v == y
}

operator fun Vec2.times(k: Int) = Vec2(x * k, y * k)

class Meter {
    var total = 0
    operator fun invoke(n: Int): Int {
        total += n
        return total
    }
    operator fun plusAssign(n: Int) {
        total += n
    }
}

@Kompozable
fun OperatorForms() {
    val a = Vec2(1, 2)
    val b = Vec2(3, 4)
    var c = a + b
    c += a
    c++
    val meter = Meter()
    meter += 5
    Text("işleç " + c + " " + (-a) + " " + (b - a) + " " + a * 3 + " " + a[1] + " " + (a < b) + " " + (2 in a) + " " + meter(10) + " " + meter.total)
}

TEST(operator_functions) {
    KompozTestRule rule([] { OperatorForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("işleç", true).text();
    CHECK(line == "işleç Vec2(x=6, y=9) Vec2(x=-1, y=-2) Vec2(x=2, y=2) Vec2(x=3, y=6) 2 true true 15 15" ||
          (std::printf("satır: %s\n", line.c_str()), false));
}

// Extension properties: a val on String, a generic one on List<T>, a var on a Kotlin class (its setter), one on a data
// class reading its fields; used before they are declared in the file.
@Kompozable
fun ExtensionPropertyForms() {
    val m = remember { Meter() }
    m.doubled = 8
    m.doubled += 2
    Text("eklenti " + "selam".shout + " " + listOf(1, 2, 3).second + " " + m.total + " " + m.doubled + " " + Vec2(3, 4).norm)
}

val String.shout: String get() = this + "!"

val <T> List<T>.second: T get() = this[1]

var Meter.doubled: Int
    get() = total * 2
    set(value) {
        total = value / 2
    }

val Vec2.norm get() = x * x + y * y

TEST(extension_properties) {
    KompozTestRule rule([] { ExtensionPropertyForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("eklenti", true).text();
    CHECK(line == "eklenti selam! 2 5 10 25" || (std::printf("satır: %s\n", line.c_str()), false));
}

// vararg: none, several, *list spread among them, after another parameter, a generic one.
fun sumAll(vararg xs: Int): Int = xs.sum()
fun joinAll(sep: String, vararg parts: String) = parts.joinToString(sep)
fun <T> bag(vararg items: T): List<T> = items

@Kompozable
fun VarargForms() {
    val more = listOf(4, 5)
    Text("çok " + sumAll() + " " + sumAll(1, 2, 3) + " " + sumAll(1, *more, 9) + " " + joinAll("-", "a", "b") + " " + bag("x", "y").size + " " + bag(0, *more, 6))
}

TEST(vararg_parameters) {
    KompozTestRule rule([] { VarargForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("çok", true).text();
    CHECK(line == "çok 0 6 19 a-b 2 [0, 4, 5, 6]" || (std::printf("satır: %s\n", line.c_str()), false));
}

// lateinit var: set later (in init, in a function), of a type without a default (a class made with arguments); read
// before it is set, it throws, as Kotlin's does.
class Session {
    lateinit var user: String
    lateinit var ledger: Ledger

    fun open(name: String) {
        user = name
        ledger = Ledger(7)
    }
}

@Kompozable
fun LateinitForms() {
    val session = remember { Session() }
    var early = "?"
    try {
        early = session.user
    } catch (e: Exception) {
        early = "henüz yok"
    }
    session.open("ayşe")
    Text("geç " + early + " " + session.user + " " + session.ledger.label())
}

TEST(lateinit_properties) {
    KompozTestRule rule([] { LateinitForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("geç", true).text();
    CHECK(line == "geç henüz yok ayşe hesap-7" || (std::printf("satır: %s\n", line.c_str()), false));
}

// try as a value: the body's last expression, or the catching block's (finally runs, giving nothing); throw,
// as a statement and as a value (?: throw); Kotlin's exceptions as they derive from one another (a
// NumberFormatException is an IllegalArgumentException), their message, rethrown as they are.
fun parsedOr(text: String, fallback: Int): Int = try {
    if (text.isEmpty()) throw IllegalArgumentException("boş")
    text.toInt()
} catch (e: IllegalArgumentException) {
    fallback
}

fun firstWord(line: String?): String = line?.split(" ")?.firstOrNull() ?: throw IllegalStateException("satır yok")

class Refused(val code: Int) : IllegalStateException("kod $code")

fun rethrown(): String {
    try {
        try {
            check(false) { "iç" }
        } catch (e: IllegalStateException) {
            throw e
        }
    } catch (e: RuntimeException) {
        return "yine " + e.message
    }
    return "?"
}

@Kompozable
fun TryForms() {
    var cleaned = 0
    val a = parsedOr("41", 0)
    val b = parsedOr("", -1)
    val c = parsedOr("12abc", -2)
    val d: String? = try { error("yok") } catch (e: IllegalStateException) { null } finally { cleaned++ }
    val f = listOf("7", "x").map { s -> try { s.toInt() * 2 } catch (e: NumberFormatException) { 0 } }
    val g = try { firstWord(null) } catch (e: Exception) { e.message ?: "?" }
    val h = try { listOf<Int>().first().toString() } catch (e: NoSuchElementException) { "$e" }
    val r = try { throw Refused(7) } catch (e: Refused) { "${e.code} ${e.message}" }
    val r2 = try { throw Refused(2) } catch (e: IllegalStateException) { "$e" }
    Text("dene $a $b $c ${d ?: "boş"} $cleaned ${f.joinToString(",")} $g ${firstWord("iki söz")} ${rethrown()} $h | $r | $r2")
}

TEST(try_as_a_value_and_throw) {
    KompozTestRule rule([] { TryForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("dene", true).text();
    CHECK(line == "dene 41 -1 -2 boş 1 14,0 satır yok iki yine iç NoSuchElementException: List is empty. | 7 kod 7 | Refused: kod 2" ||
          (std::printf("satır: %s\n", line.c_str()), false));
}

// Overloads: functions of one name taking different parameters, as Kotlin chooses between them: by the arguments'
// number and names, by a literal's type, the one needing no default first, else by their types (C++'s choice);
// at the top level, as extensions, composables, a class's and an interface's (overridden each).
fun describe(n: Int): String = "sayı $n"
fun describe(s: String): String = "yazı $s"
fun describe(a: Int, b: Int): String = "çift ${a + b}"
fun describe(label: String, times: Int = 2): String = label.repeat(times)
fun describe(flag: Boolean, onYes: () -> String): String = if (flag) onYes() else "hayır"

fun Int.tagged() = "i$this"
fun String.tagged() = "s$this"

@Kompozable
fun Chip(n: Int) {
    Text("çip sayı $n")
}

@Kompozable
fun Chip(text: String, suffix: String = "!") {
    Text("çip $text$suffix")
}

interface Sink {
    fun take(x: Int): String
    fun take(x: String): String
}

class Printer : Sink {
    override fun take(x: Int) = "p$x"
    override fun take(x: String) = "p'$x'"
    fun put(x: Int, times: Int = 1) = take(x * times)
    fun put(x: String) = take(x)
    fun put(x: String, times: Int = 2) = take(x.repeat(times))  // (put("b") is put(x: String): it needs no default)
    fun both() = put(2) + put("b") + put(2, times = 3) + put("c", 2)
}

@Kompozable
fun OverloadForms() {
    val count = 4
    val name = "ali"
    val sink: Sink = object : Sink {
        override fun take(x: Int) = "o$x"
        override fun take(x: String) = "o'$x'"
    }
    Text("aşırı " + describe(3) + "|" + describe("a") + "|" + describe(1, 2) + "|" + describe(label = "ab") + "|" + describe("c", 3) + "|" +
         describe(true) { "evet" } + "|" + describe(1.5) + "|" + describe(count) + "|" + describe(name) + "|" + 5.tagged() + name.tagged() + "|" +
         Printer().both() + "|" + sink.take(1) + sink.take("z") + "|" + Printer().put("q") + Printer().put("r", 3))
    Chip(7)
    Chip("a")
    Chip("b", suffix = "?")
}

TEST(overloads_by_arguments_and_types) {
    KompozTestRule rule([] { OverloadForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("aşırı", true).text();
    CHECK(line == "aşırı sayı 3|yazı a|çift 3|abab|ccc|evet|ondalık 1.5|sayı 4|yazı ali|i5sali|p2p'b'p6p'cc'|o1o'z'|p'q'p'rrr'" ||
          (std::printf("satır: %s\n", line.c_str()), false));
    CHECK(rule.onNodeWithText("çip sayı 7").exists() && rule.onNodeWithText("çip a!").exists() && rule.onNodeWithText("çip b?").exists());
}

// inner class: an object holding its outer one, made as outer.Counter(…) or Counter(…) in it; it reaches the outer
// one's members by their names (its own first, private ones too), the outer object as this@Scoreboard; an inner class's
// inner class reaches both.
class Scoreboard(val name: String) {
    var count = 0
    private var bumps = 0
    fun note(by: Int) {
        count += by
        bumps++
    }
    fun counter() = Counter(2)

    inner class Counter(val step: Int) {
        val name = "sayaç"
        fun bump() {
            note(step)
            count++
        }
        fun label() = "$name/${this@Scoreboard.name}:$count:$bumps"
        fun owner() = this@Scoreboard
        fun twin() = Counter(step * 10)
        fun take(n: Int) = step * n  // (named as Kotlin's take: its own, as Kotlin's member comes first)

        inner class Mark(val tag: String) {
            fun text() = "$tag$step$count"
        }
    }
}

fun innerReport(): String {
    val tally = Scoreboard("t")
    val c = tally.counter()
    c.bump()
    val d = tally.Counter(5)
    d.bump()
    val mark = d.Mark("m")
    return "iç sınıf " + c.label() + " " + d.twin().step + " " + (d.owner() == tally) + " " + mark.text() + " " + tally.count + " " + d.take(2)
}

@Kompozable
fun InnerForms() {
    Text(innerReport())
}

TEST(inner_classes_hold_their_outer_object) {
    KompozTestRule rule([] { InnerForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("iç sınıf", true).text();
    CHECK(line == "iç sınıf sayaç/t:9:2 50 true m59 9 10" || (std::printf("satır: %s\n", line.c_str()), false));
}

// An enum entry with a body of its own: what it overrides of its class's (an abstract fun, an open fun, an open val,
// toString), its parameters named its own way too; the class's own for the entries without one.
enum class Calc(val symbol: String) {
    PLUS("+") {
        override fun apply(a: Int, b: Int) = a + b
        override val loud = true
    },
    TIMES("*") {
        override fun apply(x: Int, y: Int): Int = x * y
        override fun describe() = "çarpı $symbol"
        override fun toString() = "kez"
    },
    MINUS("-") {
        override fun apply(a: Int, b: Int) = a - b
    };

    abstract fun apply(a: Int, b: Int): Int
    open fun describe() = "işlem $symbol"
    open val loud: Boolean = false
    fun shown() = "$this=${apply(6, 3)}${if (loud) "!" else ""}"
}

enum class Sign {
    POS { override fun of(n: Int) = n },
    NEG { override fun of(n: Int) = -n };
    abstract fun of(n: Int): Int
}

@Kompozable
fun EntryBodyForms() {
    Text("girdi " + Calc.entries.joinToString(" ") { it.shown() } + " " + Calc.TIMES.describe() + " " + Calc.MINUS.describe() + " " + Calc.PLUS.loud + " " +
         listOf(1, 2, 3).joinToString(", ", "[", "]", 2) + " " + "a/b/c.txt".substringAfterLast('/') + "a:b".substringBefore(":") +
         "x".substringAfter("-", "yok") + " " + Sign.NEG.of(4))
}

TEST(enum_entries_with_bodies_of_their_own) {
    KompozTestRule rule([] { EntryBodyForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("girdi", true).text();
    CHECK(line == "girdi PLUS=9! kez=18 MINUS=3 çarpı * işlem - true [1, 2, ...] c.txtayok -4" || (std::printf("satır: %s\n", line.c_str()), false));
}

// A sealed class with a constructor, members and an interface: each case gives its constructor's values
// (Screen("home"), by name too, defaults for the rest) and has its members (a value made from them, a function, an
// open one it overrides, the interface's property it implements); the sealed value reads and calls them through the
// case it holds, and is the interface too.
interface Headed {
    val title: String
}

sealed class Screen(val route: String, val depth: Int = 0) : Headed {
    object Home : Screen("home") {
        override val title = "Ana"
        override fun badge() = "!"
    }
    data class Detail(val id: Int) : Screen("detail/$id", depth = 1) {
        override val title get() = "Ayrıntı $id"
        override fun badge() = "#$id"
    }
    data object Options : Screen(route = "options") {
        override val title = "Ayarlar"
    }

    val upper = route.uppercase()
    open fun badge() = ""
    fun label() = "$title@$route${badge()}"
    fun isDeep() = depth > 0
    companion object {
        val start = "home"
    }
}

data class Lost(val path: String) : Screen("lost/$path") {
    override val title = "Kayıp"
}

fun screenReport(): String {
    val all = listOf<Screen>(Screen.Home, Screen.Detail(7), Screen.Options, Lost("x"))
    val named: Headed = Screen.Detail(2)
    val first: Headed = all[0]
    return all.joinToString(" ") { it.label() + ":" + it.depth + ":" + it.upper + ":" + it.isDeep() } + " " + named.title + " " +
        first.title + " " + Screen.Detail(3).route + " " + Screen.start
}

@Kompozable
fun ScreenForms() {
    Text("ekran " + screenReport())
}

TEST(sealed_classes_with_constructors_and_members) {
    KompozTestRule rule([] { ScreenForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("ekran", true).text();
    CHECK(line == "ekran Ana@home!:0:HOME:false Ayrıntı 7@detail/7#7:1:DETAIL/7:true Ayarlar@options:0:OPTIONS:false "
                  "Kayıp@lost/x:0:LOST/X:false Ayrıntı 2 Ana detail/3 home" ||
          (std::printf("satır: %s\n", line.c_str()), false));
}

// Declarations nested in a class that are a sealed class (its cases in it and beside it) or generic (a data class,
// a class, an interface).
class Wizard {
    sealed class Step(val index: Int) {
        object Start : Step(0)
        data class Middle(val note: String) : Step(1)
    }
    data class Finish(val total: Int) : Step(2)
    data class Slot<T>(val value: T)
    class Holder<T>(val item: T) {
        fun twice(): List<T> = listOf(item, item)
    }
    interface Source<T> {
        fun next(): T
    }
    fun steps(): List<Step> = listOf(Step.Start, Step.Middle("orta"), Finish(3))
    fun slot() = Slot(2.5)
}

fun wizardReport(): String {
    val kinds = Wizard().steps().joinToString(",") { s ->
        when (s) {
            is Wizard.Step.Start -> "baş${s.index}"
            is Wizard.Step.Middle -> "${s.note}${s.index}"
            is Wizard.Finish -> "son${s.total}${s.index}"
        }
    }
    val slot = Wizard.Slot("k")
    val holder = Wizard.Holder(4)
    val source = object : Wizard.Source<Int> {
        override fun next() = 9
    }
    return "sihirbaz $kinds ${slot.value} ${Wizard().slot().value} ${holder.twice()} ${source.next()}"
}

@Kompozable
fun WizardForms() {
    Text(wizardReport())
}

TEST(nested_sealed_and_generic_declarations) {
    KompozTestRule rule([] { WizardForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("sihirbaz", true).text();
    CHECK(line == "sihirbaz baş0,orta1,son32 k 2.5 [4, 4] 9" || (std::printf("satır: %s\n", line.c_str()), false));
}

// An inner class of a data class or an enum (values here: its object holds a copy of the outer value), and
// declarations nested in an enum.
data class Pad(val width: Int, val label: String) {
    inner class Cell(val index: Int) {
        fun text() = "$label[$index/$width]"
    }
    fun first() = Cell(0)
}

enum class Mood(val emoji: String) {
    HAPPY(":)"), SAD(":(");

    data class Note(val text: String)
    inner class Line(val text: String) {
        fun shown() = "$emoji $text ${this@Mood.name}"
    }
    fun line(text: String) = Line(text)
}

fun valueInners(): String {
    val pad = Pad(3, "p")
    return "değer " + pad.first().text() + " " + pad.Cell(2).text() + " " + Mood.SAD.line("eh").shown() + " " + Mood.HAPPY.Line("ya").shown() +
        " " + Mood.Note("n").text
}

@Kompozable
fun ValueInnerForms() {
    Text(valueInners())
}

TEST(inner_classes_of_values) {
    KompozTestRule rule([] { ValueInnerForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("değer", true).text();
    CHECK(line == "değer p[0/3] p[2/3] :( eh SAD :) ya HAPPY n" || (std::printf("satır: %s\n", line.c_str()), false));
}

// A fun interface's suspend fun: made from a lambda that is a coroutine (Fetcher { key -> delay(…); "…" }), giving its
// value or none.
fun interface Fetcher {
    suspend fun load(key: String): String
}

fun interface Pinger {
    suspend fun ping()
}

@Kompozable
fun SuspendLoaderForms() {
    var text by remember { mutableStateOf("…") }
    var pinged by remember { mutableStateOf(0) }
    val loader = remember {
        Fetcher { key ->
            delay(100)
            "yüklendi $key"
        }
    }
    val pinger = remember { Pinger { delay(10); pinged++ } }
    LaunchedEffect(Unit) {
        text = loader.load("a")
        pinger.ping()
    }
    Text("yükleyici $text $pinged")
}

TEST(fun_interfaces_with_suspend_funs) {
    KompozTestRule rule([] { SuspendLoaderForms(); });
    CHECK(rule.onNodeWithText("yükleyici … 0").exists());
    rule.advanceTimeBy(300);
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("yükleyici yüklendi a 1").exists() || (std::printf("satır: %s\n", rule.onNodeWithText("yükleyici", true).text().c_str()), false));
}

// Top-level vars and delegated properties: a var of the file (changed by a function), a val by lazy (made when
// first read), a var by mutableStateOf (state: what reads it recomposes when it changes).
var visits = 0
private var calls = 0
val greeting by lazy { "merhaba ${visits + 1}" }
var sharedCount by mutableStateOf(1)

fun visit(): Int {
    visits++
    calls += 2
    return visits * 10 + calls
}

@Kompozable
fun TopLevelForms() {
    Text("üst ${visit()} $greeting $sharedCount")
    Button(onClick = { sharedCount += 1 }) { Text("Artır") }
}

TEST(top_level_vars_and_delegated_properties) {
    KompozTestRule rule([] { TopLevelForms(); });
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("üst 12 merhaba 2 1").exists() || (std::printf("satır: %s\n", rule.onNodeWithText("üst", true).text().c_str()), false));
    rule.onNodeWithText("Artır").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("üst 24 merhaba 2 2").exists() || (std::printf("satır: %s\n", rule.onNodeWithText("üst", true).text().c_str()), false));
}

// A companion object implementing an interface: its members the class's (Widget.make(3)), the interface's with a
// body too (Widget.pair()), and the class's name as a value is the companion (sizes(Widget)).
interface Maker<T> {
    fun make(n: Int): T
    fun pair(): List<T> = listOf(make(1), make(2))
}

class Widget(val size: Int) {
    companion object : Maker<Widget> {
        override fun make(n: Int) = Widget(n * 10)
    }
}

fun sizes(maker: Maker<Widget>): List<Int> = maker.pair().map { it.size }

@Kompozable
fun CompanionInterfaceForms() {
    Text("eş ${Widget.make(3).size} ${Widget.pair().size} ${sizes(Widget)}")
}

TEST(companion_objects_implementing_interfaces) {
    KompozTestRule rule([] { CompanionInterfaceForms(); });
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("eş 30 2 [10, 20]").exists() || (std::printf("satır: %s\n", rule.onNodeWithText("eş", true).text().c_str()), false));
}

// A sealed class's cases that are classes (in it, and at the top level): handles made with its constructor's values
// (by name too) and members, an open one overridden, reached through `is` and `when` as the others are.
sealed class Outcome(val code: Int) {
    class Done(val value: String) : Outcome(200) {
        fun shout() = value.uppercase()
    }
    data class Waiting(val percent: Int) : Outcome(102)
    open fun summary() = "kod $code"
}

class Failed(val reason: String) : Outcome(code = 500) {
    override fun summary() = "hata $reason $code"
}

object Idle : Outcome(0) {
    override fun summary() = "boş"
}

fun outcomeReport(): String {
    val all = listOf<Outcome>(Outcome.Done("tamam"), Outcome.Waiting(40), Failed("bozuk"), Idle)
    return "sonuç " + all.joinToString(" ") { o ->
        when (o) {
            is Outcome.Done -> o.shout() + "/" + o.summary()
            is Outcome.Waiting -> "%${o.percent}/" + o.summary()
            is Failed -> o.summary()
            is Idle -> o.summary()
        }
    } + " " + all.map { it.code }
}

@Kompozable
fun OutcomeForms() {
    Text(outcomeReport())
}

TEST(sealed_classes_with_class_cases) {
    KompozTestRule rule([] { OutcomeForms(); });
    rule.waitForIdle();
    std::string line = rule.onNodeWithText("sonuç", true).text();
    CHECK(line == "sonuç TAMAM/kod 200 %40/kod 102 hata bozuk 500 boş [200, 102, 500, 0]" || (std::printf("satır: %s\n", line.c_str()), false));
}

TEST(coroutines_await_what_suspends) {
    KompozTestRule rule([] { Awaits(); });
    CHECK(rule.onNodeWithText("üretilen 0").exists() && rule.onNodeWithText("ikisi …").exists());
    rule.advanceTimeBy(400);
    CHECK(rule.onNodeWithText("üretilen 42").exists() && rule.onNodeWithText("ikisi 6 X").exists());
}

// A lambda with a receiver (Kotlin's T.() -> R): what its body doesn't declare is the receiver's,
// a local of the same name first, as in Kotlin; `this` is the receiver.
@Kompozable
fun Receivers(tint: Color) {
    val alpha = 0.5f
    Column {
        Box(modifier = Modifier.size(20.dp).graphicsLayer { this.alpha = alpha; rotationZ += 30f }) { Text("katman") }
        Canvas(modifier = Modifier.size(40.dp)) {
            drawRect(tint, Offset(0f, 0f), Size(size.width / 2, size.height))
            rotate(45f) { drawCircle(tint, center.x / 4) }
        }
    }
}

TEST(lambdas_with_receivers) {
    Color tint = Color(0xFF123456);
    KompozTestRule rule([=] { Receivers(tint); });
    testing::RecordingCanvas canvas;
    rule.host().draw(canvas);
    CHECK(std::count(canvas.layerAlphas.begin(), canvas.layerAlphas.end(), 0.5f) == 1);
    auto rect = std::find(canvas.rectColors.begin(), canvas.rectColors.end(), tint);
    CHECK(rect != canvas.rectColors.end() && canvas.rects[rect - canvas.rectColors.begin()].width == 20);
    CHECK(!canvas.transforms.empty());
}

// Top-level vals, as Compose's Color.kt and locals are written: defined before what uses them,
// wherever they stand in the file (or in another file of the module). content = … given by name.
data class Spacing(val small: Dp = 4.dp, val large: Dp = 16.dp)
data class Account(val name: String)

@Kompozable
fun SpacedTitle(title: String) {
    Text("$title ${LocalSpacing.current.large} $StickerGreeting")
}

val LocalSpacing = staticKompozitionLocalOf { Spacing() }
val LocalAccount = kompozitionLocalOf<Account> { error("LocalAccount was not provided") }
private val Purple = Color(0xFF6650A4)

@Kompozable
fun AppTheme(content: @Kompozable () -> Unit) {
    MaterialTheme(colorScheme = lightColorScheme(), content = content)
}

@Kompozable
fun Locals() {
    AppTheme {
        Column {
            SpacedTitle("geniş")
            KompozitionLocalProvider(LocalSpacing provides Spacing(2.dp, 8.dp), content = { SpacedTitle("dar") })
            KompozitionLocalProvider(LocalAccount provides Account("Ada")) {
                Box(modifier = Modifier.background(Purple), content = { Text(LocalAccount.current.name) })
            }
        }
    }
}

TEST(top_level_vals_and_content_by_name) {
    KompozTestRule rule([] { Locals(); });
    CHECK(rule.onNodeWithText("geniş 16.0 selam !").exists() && rule.onNodeWithText("dar 8.0 selam !").exists());
    CHECK(rule.onNodeWithText("Ada").exists());
    bool threw = false;
    try {
        LocalAccount.current();  // (no provider, outside a composition: its default)
    } catch (const kompoz::kt::IllegalStateException&) {  // (error()'s, as Kotlin's)
        threw = true;
    }
    CHECK(threw);
}

// Plain and extension functions: x.framed(c) is framed(x, c), and an extension's receiver is
// reached by its members' names, as a lambda's is; a function's type is its body's.
fun Modifier.framed(color: Color): Modifier =
    border(1.dp, color).padding(4.dp)

fun Modifier.tallerBy(extra: Dp) = layout { measurable, constraints ->
    val placeable = measurable.measure(constraints)
    layout(placeable.width, placeable.height + extra) {
        placeable.placeRelative(0f, extra)
    }
}

private fun label(count: Int): String {
    if (count == 1) return "one item"
    return "$count items"
}

fun shout(text: String) = text.uppercase() + "!"

@Kompozable
fun Extensions() {
    Column {
        Box(modifier = Modifier.testTag("framed").framed(Color.Red).tallerBy(8.dp)) { Text(label(1)) }
        Text(shout(label(3)), modifier = Modifier.semantics { heading() })
    }
}

TEST(plain_and_extension_functions) {
    KompozTestRule rule([] { Extensions(); });
    CHECK(rule.onNodeWithText("one item").exists() && rule.onNodeWithText("3 ITEMS!").exists());
    Rect box = rule.onNodeWithTag("framed").bounds(), text = rule.onNodeWithText("one item").bounds();
    // (a border takes no room: 4 dp of padding each side, 8 more above)
    CHECK(std::abs(box.height - (text.height + 16)) < 0.5f && std::abs(text.y - box.y - 12) < 0.5f);
    bool heading = false;
    for (const auto& n : rule.host().semanticsTree())
        if (n.label == "3 ITEMS!") heading = n.heading;
    CHECK(heading);
}

// Layouts as Compose writes them: FlexBox's and Grid's config = { … } on their scopes, flex { … },
// 1.fr, FlowRow's arrangements, BoxWithConstraints' maxWidth, mediaQuery { … }.
@Kompozable
fun ComposeLayouts() {
    Column {
        FlexBox(modifier = Modifier.width(200.dp).testTag("flex"), config = { columnGap(10.dp) }) {
            Box(modifier = Modifier.size(40.dp))
            Box(modifier = Modifier.height(40.dp).flex { grow(1f) }.testTag("grows"))
        }
        Grid(config = {
            column(50.dp)
            column(1.fr)
            gap(row = 4.dp, column = 10.dp)
        }, modifier = Modifier.width(200.dp)) {
            Box(modifier = Modifier.height(20.dp).fillMaxWidth().gridItem(row = 1, column = 2).testTag("cell"))
        }
        FlowRow(modifier = Modifier.width(100.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            for (i in 0 until 3) Box(modifier = Modifier.size(40.dp).testTag("flow$i"))
        }
        BoxWithConstraints(modifier = Modifier.width(150.dp)) {
            Text(if (maxWidth > 100.dp) "wide" else "narrow")
        }
        if (mediaQuery { windowWidth > 0.dp }) Text("has a window")
    }
}

TEST(layouts_as_compose_writes_them) {
    KompozTestRule rule([] { ComposeLayouts(); });
    Rect box = rule.onNodeWithTag("flex").bounds(), grown = rule.onNodeWithTag("grows").bounds();
    CHECK(std::abs(grown.x - box.x - 50) < 0.5f && std::abs(grown.width - 150) < 0.5f);
    Rect cell = rule.onNodeWithTag("cell").bounds();
    CHECK(std::abs(cell.x - 60) < 0.5f && std::abs(cell.width - 140) < 0.5f);
    CHECK(std::abs(rule.onNodeWithTag("flow1").bounds().x - 50) < 0.5f && std::abs(rule.onNodeWithTag("flow2").bounds().x) < 0.5f);
    CHECK(rule.onNodeWithText("wide").exists() && rule.onNodeWithText("has a window").exists());
}

// Compose's custom layouts: Layout's measure policy with layout(w, h) { … }, a var changed in
// forEach { } (an inline function's lambda reaches it by reference), SubkompozLayout.
@Kompozable
fun Staircase(indent: Dp, content: @Kompozable () -> Unit) {
    Layout(content = content, modifier = Modifier.testTag("stairs")) { measurables, constraints ->
        val placeables = measurables.map { it.measure(constraints.copy(minWidth = 0f, minHeight = 0f)) }
        var width = 0f
        var height = 0f
        placeables.forEachIndexed { i, p ->
            width = maxOf(width, i * indent + p.width)
            height += p.height
        }
        layout(width, height) {
            var x = 0f
            var y = 0f
            placeables.forEach { placeable ->
                placeable.placeRelative(x = x, y = y)
                x += indent
                y += placeable.height
            }
        }
    }
}

@Kompozable
fun Underlined(content: @Kompozable () -> Unit) {
    SubkompozLayout(modifier = Modifier.testTag("underlined")) { constraints ->
        val main = subkompoz("main", content).map { it.measure(constraints.copy(minWidth = 0f, minHeight = 0f)) }
        val width = main.maxOf { it.width }  // (composed as it is asked for: there on the first pass)
        val height = main.sumOf { it.height }
        val line = subkompoz("line", width) { Box(modifier = Modifier.size(width, 2.dp)) }.map { it.measure(constraints) }
        layout(width, height + 2f) {
            main.forEach { it.placeRelative(0f, 0f) }
            line.forEach { it.placeRelative(0f, height) }
        }
    }
}

@Kompozable
fun CustomLayouts() {
    Column {
        Staircase(24.dp) {
            Box(modifier = Modifier.size(20.dp).testTag("step0"))
            Box(modifier = Modifier.size(20.dp).testTag("step1"))
            Box(modifier = Modifier.size(20.dp).testTag("step2"))
        }
        Underlined { Box(modifier = Modifier.size(50.dp, 10.dp)) }
    }
}

TEST(custom_layouts_as_compose_writes_them) {
    KompozTestRule rule([] { CustomLayouts(); });
    Rect stairs = rule.onNodeWithTag("stairs").bounds(), last = rule.onNodeWithTag("step2").bounds();
    CHECK(std::abs(last.x - stairs.x - 48) < 0.5f && std::abs(last.y - stairs.y - 40) < 0.5f);
    CHECK(std::abs(stairs.width - 68) < 0.5f && std::abs(stairs.height - 60) < 0.5f);
    Rect under = rule.onNodeWithTag("underlined").bounds();
    CHECK(std::abs(under.width - 50) < 0.5f && std::abs(under.height - 12) < 0.5f);
}

// An object expression, object : NestedScrollConnection { override fun … }: the locals its functions
// use are held in it. A lazy grid's span = { GridItemSpan(maxLineSpan) }.
fun counting(counter: MutableState<Float>) = object : NestedScrollConnection {
    override fun onPreScroll(available: Offset, source: NestedScrollSource): Offset {
        counter.value += available.y
        return Offset(0f, available.y / 2)
    }
}

@Kompozable
fun SpannedGrid() {
    LazyVerticalGrid(columns = GridCells.Fixed(3), modifier = Modifier.width(300.dp).height(100.dp)) {
        item(span = { GridItemSpan(maxLineSpan) }) { Box(modifier = Modifier.fillMaxWidth().height(20.dp).testTag("header")) }
        items(listOf("a", "b"), span = { name -> GridItemSpan(if (name == "a") 2 else 1) }) { name ->
            Box(modifier = Modifier.fillMaxWidth().height(20.dp).testTag(name))
        }
    }
}

TEST(objects_and_spans) {
    auto counter = mutableStateOf(0.f);
    auto connection = counting(counter);
    Offset used = connection->onPreScroll({0, 10}, NestedScrollSource::UserInput);
    CHECK(counter.get() == 10 && used.y == 5);
    KompozTestRule rule([] { SpannedGrid(); });
    CHECK(std::abs(rule.onNodeWithTag("header").bounds().width - 300) < 0.5f && std::abs(rule.onNodeWithTag("a").bounds().width - 200) < 0.5f);
}

// Material as Compose writes it: chips with label = { }, toggleable(role = Role.Checkbox), a
// snackbar awaited with actionLabel, progress = { }, NavigationSuiteScaffold's items block.
@Kompozable
fun MaterialForms(undo: () -> Unit) {
    var unread by remember { mutableStateOf(false) }
    var muted by remember { mutableStateOf(false) }
    var tab by rememberSaveable { mutableStateOf(0) }
    val snackbars = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()
    Column {
        FilterChip(selected = unread, onClick = { unread = !unread }, label = { Text("Unread") }, modifier = Modifier.testTag("chip"))
        Row(modifier = Modifier.toggleable(value = muted, onValueChange = { muted = it }, role = Role.Checkbox).testTag("row")) {
            Checkbox(checked = muted, onCheckedChange = null)
            Text("Mute")
        }
        Button(onClick = {
            scope.launch {
                if (snackbars.showSnackbar("Deleted", actionLabel = "Undo") == SnackbarResult.ActionPerformed) undo()
            }
        }) { Text("Delete") }
        SnackbarHost(hostState = snackbars)
        LinearProgressIndicator(progress = { 0.4f }, modifier = Modifier.testTag("bar"))
        NavigationSuiteScaffold(
            navigationSuiteItems = {
                item(selected = tab == 0, onClick = { tab = 0 }, icon = { Icon(Icons.Filled.Home, contentDescription = null) }, label = { Text("Home") })
                item(selected = tab == 1, onClick = { tab = 1 }, icon = { Icon(Icons.Filled.Settings, contentDescription = null) }, label = { Text("Settings") })
            },
            modifier = Modifier.height(300.dp),
            layoutType = NavigationSuiteType.NavigationBar,
        ) {
            Text("Tab $tab")
        }
    }
}

TEST(material_as_compose_writes_it) {
    int undone = 0;
    KompozTestRule rule([&] { MaterialForms([&] { ++undone; }); }, {400, 800});
    rule.onNodeWithTag("chip").performClick();
    rule.onNodeWithTag("row").performClick();
    CHECK(rule.onNodeWithTag("bar").exists() && rule.onNodeWithText("Tab 0").exists());
    rule.onNodeWithText("Settings").performClick();
    CHECK(rule.onNodeWithText("Tab 1").exists());
    rule.onNodeWithText("Delete").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("Undo").exists());
    rule.onNodeWithText("Undo").performClick();
    rule.waitForIdle();
    CHECK(undone == 1);
}

// Theming as Compose writes it: an enum class in a when, a color scheme with its colors by name,
// Typography and TextStyle copies, MaterialTheme(typography = …), and a value-returning composable
// that remembers on its own (a group of its own, as Compose gives it) however often it is called.
enum class ThemeChoice { System, Light, Dark }

fun schemeFor(choice: ThemeChoice): ColorScheme = when (choice) {
    ThemeChoice.Dark -> darkColorScheme(primary = Color(0xFF9ECAFF))
    else -> lightColorScheme(primary = Color(0xFF0061A4), onPrimary = Color.White)
}

int gNextId = 0;
Shape gMedium;

@Kompozable
fun counted(): Int = remember { ++gNextId }

@Kompozable
fun Themed(choice: ThemeChoice) {
    val base = MaterialTheme.typography
    val type = base.copy(headlineSmall = base.headlineSmall.copy(fontFamily = FontFamily.Serif, fontSize = 30.sp))
    var showFirst by remember { mutableStateOf(true) }
    MaterialTheme(colorScheme = schemeFor(choice), typography = type, shapes = Shapes(medium = RoundedCornerShape(20.dp))) {
        Column {
            gMedium = MaterialTheme.shapes.medium
            Text("${MaterialTheme.typography.headlineSmall.size.toInt()}", modifier = Modifier.testTag("size"), color = MaterialTheme.colorScheme.primary)
            if (showFirst) Text("a ${counted()}")
            Text("b ${counted()}")
            Button(onClick = { showFirst = false }) { Text("hide") }
        }
    }
}

TEST(theming_as_compose_writes_it) {
    CHECK(schemeFor(ThemeChoice::Dark).primary == Color::argb(0xFF9ECAFF) && schemeFor(ThemeChoice::Light).onPrimary == Color::White);  // (Color(Long): AARRGGBB)
    KompozTestRule rule([] { Themed(ThemeChoice::System); });
    CHECK(rule.onNodeWithTag("size").text() == "30");
    CHECK(gMedium == RoundedCornerShape(20));
    CHECK(rule.onNodeWithText("a 1").exists() && rule.onNodeWithText("b 2").exists());
    rule.onNodeWithText("hide").performClick();
    // The first call gone, the second keeps what it remembered: each call a group of its own, not a
    // slot by order from the same site (which would hand b a's).
    CHECK(!rule.onNodeWithText("a 1").exists() && rule.onNodeWithText("b 2").exists());
}

// Text as Compose writes it: an annotated string built on its builder, a field with composable
// label and supporting text, a state field with lineLimits and a placeholder, a measurer in a Canvas.
fun openTerms() {}

@Kompozable
fun TextForms() {
    var email by remember { mutableStateOf("") }
    val message = rememberTextFieldState()
    val measurer = rememberTextMeasurer()
    Column {
        Text(buildAnnotatedString {
            append("accept the ")
            withLink(LinkAnnotation.Clickable("terms") { openTerms() }) { append("terms") }
            withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { append(" now") }
        })
        OutlinedTextField(value = email, onValueChange = { email = it }, singleLine = true, label = { Text("Email") }, supportingText = { Text("Never shared") })
        OutlinedTextField(state = message, lineLimits = TextFieldLineLimits.MultiLine(maxHeightInLines = 4), placeholder = { Text("Message") })
        SelectionContainer { Text("Order KMP-1") }
        Canvas(modifier = Modifier.size(40.dp)) {
            val label = measurer.measure("7%", style = TextStyle(fontSize = 24.sp))
            drawText(label, topLeft = Offset(center.x - label.size.width / 2, 0f))
        }
    }
}

TEST(text_as_compose_writes_it) {
    KompozTestRule rule([] { TextForms(); });
    CHECK(rule.onNodeWithText("accept the terms now", true).exists());
    CHECK(rule.onNodeWithText("Email").exists() && rule.onNodeWithText("Never shared").exists() && rule.onNodeWithText("Message").exists());
    CHECK(rule.onNodeWithText("Order KMP-1").exists());
}

// Graphics as Compose writes them: a Canvas drawing by name, a shape from a path, a cached drawing,
// a blur set in its scope, an ImageVector built with groups and paths, a vector painter's content.
val Ticket = GenericShape { size, _ ->
    moveTo(0f, 0f)
    lineTo(size.width, 0f)
    lineTo(size.width, size.height)
    lineTo(0f, size.height)
    close()
}

val BadgeIcon = ImageVector.Builder("Badge", 24.dp, 24.dp, 24f, 24f)
    .addPath(pathData = addPathNodes("M12,2a10,10 0 1,0 0.01,0z"), fill = Brush.solid(Color(0xFF1E88E5)))
    .group(rotation = -10f, pivotX = 12f, pivotY = 12f) {
        path(stroke = Brush.solid(Color.White), strokeLineWidth = 2f, strokeLineCap = StrokeCap.Round) {
            moveTo(7f, 12f)
            lineTo(10.5f, 15.5f)
            lineTo(17f, 9f)
        }
    }
    .build()

@Kompozable
fun Drawings() {
    val ink = Color.White.copy(alpha = 0.5f)
    Column {
        Canvas(modifier = Modifier.size(100.dp, 50.dp).testTag("tuval")) {
            drawCircle(ink, radius = 9f, center = Offset(20f, 25f), style = Stroke(width = 2f))
            rotate(30f, pivot = Offset(50f, 25f)) { drawRect(ink, topLeft = Offset(50f, 10f), size = Size(20f, 20f)) }
            drawLine(ink, start = Offset(0f, 0f), end = Offset(10f, 10f), strokeWidth = 3f, cap = StrokeCap.Round,
                     pathEffect = PathEffect.dashPathEffect(floatArrayOf(8f, 6f)))
        }
        Box(modifier = Modifier.size(64.dp).clip(Ticket).drawWithCache {
            val half = size.width / 2
            onDrawBehind { drawRect(ink, size = Size(half, size.height)) }
        })
        Box(modifier = Modifier.size(40.dp).blur { blurRadiusSpec = BlurRadiusSpec.radialGradient(0.dp, 12.dp) }.background(ink))
        Image(rememberVectorPainter(BadgeIcon), contentDescription = "Badge")
        val tick = rememberVectorPainter(24.dp, 24.dp, 24f, 24f) { _, _ ->
            Path(pathData = addPathNodes("M5,12l5,5l9,-10"), stroke = Brush.solid(Color.Black), strokeLineWidth = 2f)
        }
        Image(tick, contentDescription = "Done")
    }
}

TEST(graphics_as_compose_writes_it) {
    KompozTestRule rule([] { Drawings(); });
    CHECK(rule.onNodeWithTag("tuval").exists());
    CHECK(rule.onNodeWithContentDescription("Badge").exists());
    CHECK(rule.onNodeWithContentDescription("Done").exists());
    testing::RecordingCanvas canvas;
    rule.host().draw(canvas);
    CHECK(canvas.rects.size() >= 3 && !canvas.paths.empty());  // the circle and the rects; the line, the ticket and the vectors' paths
    CHECK(BadgeIcon.root().size() == 2 && Ticket.createOutline({10, 10}, LayoutDirection::Ltr).asPath().bounds().width == 10);
}

// Animation as Compose writes it: animated values by delegate, AnimatedVisibility and AnimatedContent with
// their named arguments and infix transforms, a transition's values, an Animatable in a coroutine,
// a shared element with this@AnimatedContent.
enum class Panel { Closed, Open }
val InkBlue = Color(0xFF1E88E5)   // Compose's Color(Long): AARRGGBB

@Kompozable
fun Animations(open: Boolean, count: Int) {
    val scale by animateFloatAsState(if (open) 1.2f else 1f, spring(Spring.DampingRatioMediumBouncy), label = "scale")
    val tint by animateColorAsState(if (open) Color.White else Color.Black, tween(300))
    val transition = updateTransition(targetState = if (open) Panel.Open else Panel.Closed, label = "panel")
    val size by transition.animateDp(label = "size") { if (it == Panel.Open) 200.dp else 80.dp }
    val pulse by rememberInfiniteTransition(label = "pulse").animateFloat(
        initialValue = 0.85f, targetValue = 1f, animationSpec = infiniteRepeatable(tween(800), RepeatMode.Reverse), label = "scale")
    val offset = remember { Animatable(0f) }
    LaunchedEffect(open) {
        if (open) {
            for (x in listOf(-12f, 12f, 0f)) offset.animateTo(x, tween(50))
        }
    }
    Column {
        Text("ölçek ${(scale * 10).roundToInt()}", modifier = Modifier.graphicsLayer { scaleX = pulse; scaleY = pulse }, color = tint)
        Box(modifier = Modifier.size(size).offset { IntOffset(offset.value.roundToInt(), 0) }.testTag("kutu"))
        AnimatedVisibility(visible = open, enter = fadeIn() + expandVertically(), exit = fadeOut() + shrinkVertically(), label = "details") {
            Text("ayrıntılar")
        }
        AnimatedContent(
            targetState = count,
            transitionSpec = {
                if (targetState > initialState) slideInVertically { it } togetherWith slideOutVertically { -it }
                else (slideInVertically { -it } togetherWith slideOutVertically { it }) using SizeTransform(clip = false)
            },
            label = "count",
        ) { n ->
            Text("sayı $n")
        }
        Crossfade(targetState = open, label = "page") { p -> Text(if (p) "açık" else "kapalı") }
        SharedTransitionLayout(modifier = Modifier.fillMaxWidth()) {
            AnimatedContent(targetState = open, label = "photo") { o ->
                Box(modifier = Modifier.size(if (o) 80.dp else 40.dp).testTag(if (o) "büyük" else "küçük")
                        .sharedElement(rememberSharedContentState(key = "photo"), animatedVisibilityScope = this@AnimatedContent))
            }
        }
    }
}

TEST(animation_as_compose_writes_it) {
    auto open = mutableStateOf(false);
    auto count = mutableStateOf(0);
    KompozTestRule rule([&] { Animations(open.get(), count.get()); });
    CHECK(rule.onNodeWithText("ölçek 10").exists() && rule.onNodeWithText("sayı 0").exists() && rule.onNodeWithText("kapalı").exists());
    CHECK(!rule.onNodeWithText("ayrıntılar").exists() && rule.onNodeWithTag("küçük").exists());
    CHECK(std::abs(rule.onNodeWithTag("kutu").bounds().width - 80) < 0.5f);
    open.set(true);
    count.set(1);
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("ölçek 12").exists() && rule.onNodeWithText("sayı 1").exists() && rule.onNodeWithText("açık").exists());
    CHECK(rule.onNodeWithText("ayrıntılar").exists() && rule.onNodeWithTag("büyük").exists());
    CHECK(std::abs(rule.onNodeWithTag("kutu").bounds().width - 200) < 0.5f);
    CHECK(InkBlue == Color::argb(0xFF1E88E5) && std::abs(InkBlue.blue() - 0xE5 / 255.f) < 0.01f);
}

// Gestures as Compose writes them: pointerInput { detectTapGestures(onTap = { }) }, the anchors builder with
// its infix `at`, draggable and dragAndDropTarget by name, a drag's points in a state list.
enum class DragValue { Start, End }

@Kompozable
fun Gestures(onEvent: (String) -> Unit) {
    var x by remember { mutableStateOf(0f) }
    val state = rememberAnchoredDraggableState(DragValue.Start)
    state.updateAnchors(DraggableAnchors {
        DragValue.Start at 0f
        DragValue.End at 120f
    })
    val points = remember { mutableStateListOf<Offset>() }
    Column {
        Box(modifier = Modifier.size(56.dp).testTag("tutamaç")
                .draggable(orientation = Orientation.Horizontal, state = rememberDraggableState { delta -> x = maxOf(0f, x + delta) }))
        Box(modifier = Modifier.size(56.dp).anchoredDraggable(state, Orientation.Horizontal).testTag("geçiş"))
        Box(modifier = Modifier.size(100.dp).testTag("dokun").pointerInput(Unit) {
            detectTapGestures(onTap = { onEvent("tap") }, onLongPress = { onEvent("long") })
        })
        Box(modifier = Modifier.size(100.dp).testTag("çiz").pointerInput(Unit) {
            detectDragGestures(onDragStart = { at -> points.clear(); points.add(at) }, onDrag = { change, _ -> points.add(change.position) })
        })
        Text("x ${x.roundToInt()} noktalar ${points.size} konum ${state.requireOffset().roundToInt()}")
        Box(modifier = Modifier.size(40.dp).dragAndDropTarget(shouldStartDragAndDrop = { it.hasMimeType("text/plain") },
                                                              target = DragAndDropTarget(onDrop = { e -> onEvent(e.text()); true })))
    }
}

TEST(gestures_as_compose_writes_it) {
    std::vector<std::string> log;
    KompozTestRule rule([&] { Gestures([&](std::string e) { log.push_back(std::move(e)); }); });
    CHECK(rule.onNodeWithText("x 0 noktalar 0 konum 0").exists());
    rule.swipe({10, 28}, {40, 28});
    int x = std::stoi(rule.onNodeWithText("x ", true).text().substr(2));
    CHECK(x > 20 && x <= 30);  // (30 dp dragged, less the touch slop)
    Offset tap = rule.onNodeWithTag("dokun").bounds().center();
    rule.host().pointerDown(tap);
    rule.host().pointerUp(tap);
    rule.waitForIdle();
    rule.advanceTimeBy(400);  // (onTap waits for no double tap when there is no onDoubleTap: at once)
    CHECK(!log.empty() && log.front() == "tap");
}

// Classes as Kotlin writes them: a ViewModel with a state flow and a function, a class with a primary
// constructor (val and plain parameters, a default), a property typed by its value, an init block, a
// flow built in a function typed Flow<T>, a saved state handle set by index, a lifecycle effect.
data class Todo(val id: Int, val title: String, val done: Boolean = false)

class TodoViewModel : ViewModel() {
    val items = MutableStateFlow(listOf(Todo(1, "Yaz"), Todo(2, "Gönder")))
    var toggles = 0

    fun toggle(id: Int, done: Boolean) {
        toggles++
        items.update { list -> list.map { if (it.id == id) it.copy(done = done) else it } }
    }

    override fun onCleared() {}
}

class Greeter(private val name: String, suffix: String, val punctuation: String = "!") {
    val greeting: String = "Merhaba, $name$suffix"   // (typed: it reads a plain parameter)
    var greeted = 0
    init { greeted = -1 }
    fun greet(): String {
        greeted++
        return greeting + punctuation
    }
    private fun hidden() = 1
}

fun ticks(): Flow<Int> = flow {
    for (i in 0 until 3) {
        emit(i)
        delay(10)
    }
}

class QueryViewModel(private val state: SavedStateHandle) : ViewModel() {
    val query: StateFlow<String> = state.getStateFlow("query", "")
    fun setQuery(q: String) { state["query"] = q }
}

class Player { var playing = false; fun play() { playing = true } fun pause() { playing = false } }

@Kompozable
fun TodoScreen(player: Player) {
    val vm = viewModel<TodoViewModel>()
    val items by vm.items.collectAsState()
    LazyColumn {
        items(items, key = { it.id }) { t ->
            Text(if (t.done) "✓ ${t.title}" else t.title, modifier = Modifier.clickable { vm.toggle(t.id, !t.done) })
        }
    }
    LifecycleResumeEffect(Unit) {
        player.play()
        onPauseOrDispose { player.pause() }
    }
}

TEST(classes_as_kotlin_writes_them) {
    Player player;
    KompozTestRule rule([&] { TodoScreen(player); });
    CHECK(rule.onNodeWithText("Yaz").exists());
    CHECK(player.kompoz_object().playing);  // (the composable copied the handle: the same object)
    rule.onNodeWithText("Yaz").performClick();
    CHECK(rule.onNodeWithText("✓ Yaz").exists());
    Greeter g("Ada", " Lovelace");
    CHECK(g.kompoz_object().greeting == "Merhaba, Ada Lovelace" && g.kompoz_object().greeted == -1);
    CHECK(g.greet() == "Merhaba, Ada Lovelace!" && g.kompoz_object().greeted == 0);
    Greeter same = g;  // a copy is the same object, as Kotlin's
    same.greet();
    CHECK(g.kompoz_object().greeted == 1 && same == g);
    CHECK(Greeter("Bo", "", "?").greet() == "Merhaba, Bo?");
    QueryViewModel q{SavedStateHandle()};
    q.setQuery("kompoz");
    CHECK(q.query.value() == "kompoz");
    (void)ticks();  // (it compiles: Flow<int> from flow { emit(i) })
}

// Navigation as Compose writes it: NavHost by name with a graph builder, typed routes from @Serializable,
// navigate with its options block, the back stack entry's destination, nested graphs, deep links, the
// host's transitions, BackHandler and combinedClickable by name.
@Serializable
data class Detail(val id: Int, val tab: String? = null)

@Serializable
object Start

@Kompozable
fun Navigator(nav: NavController) {
    val entry by nav.currentBackStackEntryAsState()
    val route = entry?.destination?.route
    var selecting by remember { mutableStateOf(false) }
    BackHandler(enabled = selecting) { selecting = false }
    Column {
        Text("rota ${route ?: "yok"}", modifier = Modifier.combinedClickable(onClick = {}, onLongClick = { selecting = true }))
        NavHost(navController = nav, startDestination = Start, modifier = Modifier.size(200.dp),
                enterTransition = { slideInHorizontally { it } }, exitTransition = { slideOutHorizontally { -it } }) {
            kompozable<Start> {
                Button(onClick = { nav.navigate(Detail(id = 7, tab = "b")) { launchSingleTop = true } }) { Text("Aç") }
            }
            kompozable<Detail> { backStackEntry ->
                val detail = backStackEntry.toRoute<Detail>()
                Text("ayrıntı ${detail.id} ${detail.tab ?: "-"}")
                Button(onClick = { nav.navigate(Start) { popUpTo<Start>(); launchSingleTop = true } }) { Text("Geri") }
            }
            navigation(startDestination = "a/{n}", route = "grup") {
                kompozable("a/{n}", deepLinks = listOf(navDeepLink { uriPattern = "kompoz://a/{n}" })) { e ->
                    Text("a ${e.arguments?.getInt("n") ?: 0}")
                }
            }
        }
    }
}

// A ViewModel parameter is the pointer its owner gives; a map's element is nullable, its keys and
// values are lists; an enum's ordinal; a struct in a C++ namespace (routes.Item(…)); segmented
// buttons and a staggered grid's header as Compose writes them.
namespace routes {
struct Item {
    int n = 0;
};
}  // namespace routes

enum class Side { Left, Right }

@Kompozable
fun SideLine(vm: TodoViewModel, side: Side) {
    val items by vm.items.collectAsState()
    val counts = mapOf(1 to 3)
    val n = counts[1] ?: 0
    val m = counts[2] ?: 0
    Text("öğe ${items.size} · ${side.ordinal} · $n $m · ${counts.keys.sum()} · ${counts.values.sum()} · ${routes.Item(7).n}")
}

@Kompozable
fun Mixed() {
    val vm = viewModel<TodoViewModel>()
    var pick by remember { mutableStateOf(0) }
    var bold by remember { mutableStateOf(false) }
    Column {
        SideLine(vm, Side.Right)
        SingleChoiceSegmentedButtonRow(modifier = Modifier.width(300.dp)) {
            val labels = listOf("Bir", "İki")
            for (i in 0 until 2) {
                SegmentedButton(selected = pick == i, onClick = { pick = i }, shape = SegmentedButtonDefaults.itemShape(i, 2)) { Text(labels[i]) }
            }
        }
        MultiChoiceSegmentedButtonRow { SegmentedButton(checked = bold, onCheckedChange = { bold = it }) { Text("Kalın") } }
        Text("seçim $pick ${if (bold) "kalın" else "ince"}")
        LazyVerticalStaggeredGrid(columns = StaggeredGridCells.Fixed(2), modifier = Modifier.size(200.dp)) {
            item(span = StaggeredGridItemSpan.FullLine) { Text("Başlık", modifier = Modifier.testTag("başlık")) }
            items(listOf("a", "b"), span = { StaggeredGridItemSpan.SingleLane }) { Text(it, modifier = Modifier.height(40.dp).testTag("kart$it")) }
        }
    }
}

TEST(view_models_maps_and_segments_as_kotlin_writes_them) {
    KompozTestRule rule([] { Mixed(); });
    CHECK(rule.onNodeWithText("öğe 2 · 1 · 3 0 · 1 · 3 · 7").exists());
    rule.onNodeWithText("İki").performClick();
    rule.onNodeWithText("Kalın").performClick();
    CHECK(rule.onNodeWithText("seçim 1 kalın").exists());
    Rect header = rule.onNodeWithTag("başlık").bounds(), a = rule.onNodeWithTag("karta").bounds(), b = rule.onNodeWithTag("kartb").bounds();
    CHECK(a.y >= header.y + header.height - 1 && std::abs(a.y - b.y) < 1 && a.x < b.x);  // (the header across both lanes)
}

// M3 Expressive's buttons as Compose writes them: a connected ButtonGroup (Modifier.animateWidth), a
// group from items with its overflowIndicator, a toggle button, a split button, a Label; a slider's
// valueRange and a RangeSlider's range value; a lambda or null, a value or null, ifEmpty.
@Kompozable
fun Expressive() {
    var last by remember { mutableStateOf("") }
    var bold by remember { mutableStateOf(false) }
    var open by remember { mutableStateOf(false) }
    var volume by remember { mutableStateOf(40f) }
    var price by remember { mutableStateOf(200f..800f) }
    val source = remember { MutableInteractionSource() }
    val stars = rememberSliderState(value = 3f, steps = 3, valueRange = 1f..5f)
    val hours = rememberRangeSliderState(activeRangeStart = 9f, activeRangeEnd = 17f, steps = 23, valueRange = 0f..24f)
    Column {
        ButtonGroup {
            Button(onClick = { last = "oynat" }, modifier = Modifier.animateWidth(source).testTag("oynat"), interactionSource = source) { Text("Oynat") }
        }
        ButtonGroup(
            overflowIndicator = { menuState ->
                IconButton(onClick = { if (menuState.isExpanded) menuState.dismiss() else menuState.show() }, modifier = Modifier.testTag("daha")) {
                    Icon(Icons.Filled.MoreVert, contentDescription = "Daha")
                }
            },
            modifier = Modifier.width(120.dp),
        ) {
            clickableItem(onClick = { last = "kopyala" }, label = "Kopyala")
            toggleableItem(checked = bold, onCheckedChange = { bold = it }, label = "Kalın")
            clickableItem(onClick = { last = "sil" }, label = "Sil", icon = { Icon(Icons.Filled.Delete, contentDescription = null) })
        }
        ToggleButton(checked = bold, onCheckedChange = { bold = it }, shapes = ButtonGroupDefaults.connectedLeadingButtonShapes()) { Text("Koyu") }
        SplitButtonLayout(
            leadingButton = { SplitButtonDefaults.LeadingButton(onClick = { last = "kaydet" }) { Text("Kaydet") } },
            trailingButton = {
                SplitButtonDefaults.TrailingButton(checked = open, onCheckedChange = { open = it }) { SplitButtonDefaults.TrailingButtonIcon(open) }
            },
        )
        Slider(value = volume, onValueChange = { volume = it }, valueRange = 0f..100f, interactionSource = source,
               thumb = { s -> Label(label = { PlainTooltip { Text("%.0f".format(volume)) } }, interactionSource = source) { SliderDefaults.Thumb(s) } },
               track = if (bold) { { s -> SliderDefaults.Track(s) } } else null)
        RangeSlider(value = price, onValueChange = { price = it }, valueRange = 0f..1000f, steps = 19)
        val names = listOf<String>()
        Text("son: ${last.ifEmpty { "yok" }} · ${price.start.roundToInt()}–${price.endInclusive.roundToInt()} · ${names.ifEmpty { listOf("kimse") }.first()} · " +
                 "${stars.value.roundToInt()} · ${hours.activeRangeEnd.roundToInt()} · ${if (bold) "kalın" else "ince"} · ${if (open) "açık" else "kapalı"}",
             fontWeight = if (bold) FontWeight.Bold else null)
    }
}

TEST(expressive_buttons_and_sliders_as_compose_writes_them) {
    KompozTestRule rule([] { Expressive(); });
    CHECK(rule.onNodeWithText("son: yok · 200–800 · kimse · 3 · 17 · ince · kapalı").exists());
    rule.onNodeWithTag("oynat").performClick();
    rule.onNodeWithText("Kaydet").performClick();
    rule.onNodeWithText("Koyu").performClick();
    CHECK(rule.onNodeWithText("son: kaydet · 200–800 · kimse · 3 · 17 · kalın · kapalı").exists());
    rule.onNodeWithTag("daha").performClick();
    rule.waitForIdle();
    auto sils = rule.onAllNodesWithText("Sil");  // the last one: the menu's (the group measures an unplaced copy of each item first)
    sils[sils.size() - 1].performClick();
    CHECK(rule.onNodeWithText("son: sil · 200–800 · kimse · 3 · 17 · kalın · kapalı").exists());
}

// Badges, a segmented list item, the loading indicator, an extended FAB's slots and a bottom app
// bar's insets as Compose writes them; a lazy list over a SnapshotStateList, shuffled and refilled.
@Kompozable
fun Containers() {
    val tasks = remember { mutableStateListOf(1, 2, 3) }
    var plan by remember { mutableStateOf(0) }
    var open by remember { mutableStateOf(true) }
    Column {
        Row {
            FilledTonalButton(onClick = { tasks.shuffle(Random(7)) }) { Text("Karıştır") }
            FilledTonalButton(onClick = { tasks.clear(); tasks.addAll(listOf(4, 5)) }) { Text("Doldur") }
        }
        LazyColumn(modifier = Modifier.height(200.dp)) {
            items(tasks, key = { it }) { Text("görev $it") }
        }
        Text("sıra: ${tasks.joinToString(",")}")
        BadgedBox(badge = { Badge { Text("8") } }) { Icon(Icons.Filled.Email, contentDescription = "Posta") }
        BadgedBox(badge = { Badge(modifier = Modifier.testTag("nokta")) }) { Icon(Icons.Filled.Notifications, contentDescription = "Bildirim") }
        for (i in 0 until 2) {
            SegmentedListItem(selected = plan == i, onClick = { plan = i }, shapes = ListItemDefaults.segmentedShapes(i, 2),
                              headlineContent = { Text("plan $i") }, supportingContent = { Text(if (plan == i) "seçili" else "") })
        }
        LoadingIndicator(modifier = Modifier.size(48.dp).testTag("yükleniyor"))
        LoadingIndicator(progress = 0.5f, modifier = Modifier.testTag("yarım"))
        SmallExtendedFloatingActionButton(onClick = { open = !open }, expanded = open, icon = { Icon(Icons.Filled.Add, contentDescription = null) },
                                          text = { Text("Yeni") }, modifier = Modifier.testTag("fab"))
        BottomAppBar(actions = { IconButton(onClick = { }) { Icon(Icons.Filled.Check, contentDescription = "Tamam") } },
                     windowInsets = WindowInsets(0.dp), modifier = Modifier.testTag("çubuk"))
    }
}

TEST(containers_as_compose_writes_them) {
    KompozTestRule rule([] { Containers(); });
    CHECK(rule.onNodeWithText("sıra: 1,2,3").exists() && rule.onNodeWithText("görev 2").exists());
    rule.onNodeWithText("Karıştır").performClick();
    std::string order = rule.onNodeWithText("sıra: ", true).text();
    CHECK(order != "sıra: 1,2,3" && order.size() == std::string("sıra: 1,2,3").size());
    rule.onNodeWithText("Doldur").performClick();
    CHECK(rule.onNodeWithText("sıra: 4,5").exists() && rule.onNodeWithText("görev 5").exists() && !rule.onNodeWithText("görev 1").exists());
    CHECK(rule.onNodeWithText("8").exists() && rule.onNodeWithTag("nokta").exists());
    rule.onNodeWithText("plan 1").performClick();
    CHECK(rule.onNodeWithText("seçili").exists());
    CHECK(rule.onNodeWithTag("yükleniyor").exists() && rule.onNodeWithTag("yarım").exists());
    Rect wide = rule.onNodeWithTag("fab").bounds();
    rule.onNodeWithTag("fab").performClick();
    rule.mainClock().advanceTimeBy(2000);
    CHECK(rule.onNodeWithTag("fab").bounds().width < wide.width && rule.onNodeWithText("Yeni").exists());
    CHECK(rule.onNodeWithTag("çubuk").bounds().height <= 80.5f);  // no inset padding
}

// Tabs, the FAB menu and a floating toolbar as Compose writes them; a lambda kept past its block
// reads the scope it was in (graphicsLayer { rotationZ = 45 * checkedProgress }: a copy of the scope).
@Kompozable
fun Navigation() {
    var tab by remember { mutableStateOf(0) }
    var open by remember { mutableStateOf(false) }
    var picked by remember { mutableStateOf("yok") }
    var more by remember { mutableStateOf(false) }
    Column {
        PrimaryTabRow(selectedTabIndex = tab) {
            for (i in 0 until 2) Tab(selected = tab == i, onClick = { tab = i }, text = { Text("sekme $i") }, icon = { Icon(Icons.Filled.Star, contentDescription = null) })
        }
        PrimaryScrollableTabRow(selectedTabIndex = tab, edgePadding = 0.dp) {
            for (i in 0 until 2) LeadingIconTab(selected = tab == i, onClick = { tab = i }, text = { Text("kayan $i") }, icon = { Icon(Icons.Filled.Home, contentDescription = null) })
        }
        Text("seçili $tab · $picked · ${if (more) "fazla" else "az"}")
        Box(modifier = Modifier.size(300.dp), contentAlignment = Alignment.BottomEnd) {
            FloatingActionButtonMenu(
                expanded = open,
                button = {
                    ToggleFloatingActionButton(checked = open, onCheckedChange = { open = it }, modifier = Modifier.testTag("aç")) {
                        Icon(Icons.Filled.Add, contentDescription = "Menü", modifier = Modifier.testTag("artı").graphicsLayer { rotationZ = 45 * checkedProgress })
                    }
                },
            ) {
                for (label in listOf("Not", "Konum")) {
                    FloatingActionButtonMenuItem(onClick = { picked = label; open = false }, text = { Text(label) }, icon = { Icon(Icons.Filled.Edit, contentDescription = null) })
                }
            }
        }
        HorizontalFloatingToolbar(expanded = more, colors = FloatingToolbarDefaults.vibrantFloatingToolbarColors(),
                                  leadingContent = { IconButton(onClick = { }) { Icon(Icons.Filled.Favorite, contentDescription = "Beğen") } }) {
            IconButton(onClick = { more = !more }, modifier = Modifier.testTag("daha")) { Icon(Icons.Filled.MoreVert, contentDescription = "Daha") }
        }
    }
}

TEST(tabs_and_fab_menu_as_compose_writes_them) {
    KompozTestRule rule([] { Navigation(); });
    CHECK(rule.onNodeWithText("seçili 0 · yok · az").exists() && rule.onNodeWithText("sekme 1").exists() && rule.onNodeWithText("kayan 1").exists());
    rule.onNodeWithText("kayan 1").performClick();
    rule.onNodeWithTag("daha").performClick();
    CHECK(rule.onNodeWithText("seçili 1 · yok · fazla").exists() && rule.onNodeWithContentDescription("Beğen").exists());
    rule.onNodeWithTag("aç").performClick();
    rule.mainClock().advanceTimeBy(1500);
    CHECK(rule.onNodeWithText("Konum").exists());
    rule.onNodeWithText("Konum").performClick();
    rule.mainClock().advanceTimeBy(1500);
    CHECK(rule.onNodeWithText("seçili 1 · Konum · fazla").exists());
}

// A text field's transformations as Compose writes them: InputTransformation.byValue { current, proposed -> },
// OutputTransformation { insert(…) } on the buffer, a text filtered by its characters, LocalFocusManager.
@Kompozable
fun Phone() {
    val phone = rememberTextFieldState("5551234567")
    var digits by remember { mutableStateOf("a1b2") }
    val focus = LocalFocusManager.current
    Column {
        OutlinedTextField(state = phone, modifier = Modifier.testTag("telefon"),
                          inputTransformation = InputTransformation.byValue { current, proposed -> if (proposed.all { it.isDigit() }) proposed else current }
                              .then(InputTransformation.maxLength(10)),
                          outputTransformation = OutputTransformation { for (at in listOf(8, 6, 3)) if (length > at) insert(at, " ") })
        Text("değer ${phone.text} · ${digits.filter { it.isDigit() }} · ${"a b".filter { !it.isWhitespace() }}")
        Button(onClick = { focus.clearFocus() }) { Text("Odağı bırak") }
    }
}

TEST(text_field_transformations_as_compose_writes_them) {
    KompozTestRule rule([] { Phone(); });
    CHECK(rule.onNodeWithText("değer 5551234567 · 12 · ab").exists());
    rule.onNodeWithTag("telefon").performTextInput("x9");  // the letter refused, the digit kept, then the length's limit
    CHECK(rule.onNodeWithText("değer 5551234567", true).exists());
}

// A swipe-to-dismiss box, a search bar with its state, tooltips, a date picker dialog and focus
// properties as Compose writes them; WindowInsets.statusBars read as a property, a nullable read after
// its null check, Pair(a, b), tween(easing = …).
@Kompozable
fun SectionsForms() {
    val tasks = remember { mutableStateListOf(1, 2) }
    val search = rememberSearchBarState()
    val query = rememberTextFieldState()
    var chosen by remember { mutableStateOf("yok") }
    var open by remember { mutableStateOf(false) }
    var picked by remember { mutableStateOf<LocalDate?>(null) }
    val requesters = remember { List(2) { FocusRequester() } }
    val scope = rememberCoroutineScope()
    val maybe: Pair<Int, Int>? = if (tasks.size > 1) Pair(3, 4) else null
    val alpha by animateFloatAsState(if (open) 1f else 0.5f, animationSpec = tween(durationMillis = 10, easing = LinearEasing))
    Column {
        for (id in tasks) {
            key(id) {
                val state = rememberSwipeToDismissBoxState(confirmValueChange = { tasks.remove(id); true })
                SwipeToDismissBox(state = state, backgroundContent = { Box(modifier = Modifier.fillMaxSize().background(Color.Red)) }) {
                    Text("görev $id", modifier = Modifier.fillMaxWidth().testTag("görev$id"))
                }
            }
        }
        SearchBar(state = search, inputField = {
            SearchBarDefaults.InputField(textFieldState = query, searchBarState = search, onSearch = { chosen = it },
                                         placeholder = { Text("Ara") }, leadingIcon = { Icon(Icons.Filled.Search, contentDescription = "Ara") })
        }, modifier = Modifier.testTag("arama"))
        TooltipBox(positionProvider = TooltipDefaults.rememberTooltipPositionProvider(), tooltip = { PlainTooltip(caretSize = TooltipDefaults.caretSize) { Text("İpucu") } },
                   state = rememberTooltipState()) {
            IconButton(onClick = { open = true }, modifier = Modifier.testTag("aç")) { Icon(Icons.Filled.Info, contentDescription = "Bilgi") }
        }
        if (open) {
            val state = rememberDatePickerState(picked)
            DatePickerDialog(onDismissRequest = { open = false }, confirmButton = { TextButton(onClick = { picked = state.selectedDate; open = false }) { Text("Tamam") } },
                             dismissButton = { TextButton(onClick = { open = false }) { Text("İptal") } }) { DatePicker(state = state) }
        }
        Row {
            for (i in 0 until 2) {
                OutlinedButton(onClick = { }, modifier = Modifier.focusRequester(requesters[i]).focusProperties { if (i == 1) canFocus = false }.testTag("odak$i")) { Text("düğme $i") }
            }
        }
        Text("tasks ${tasks.size} · ${chosen} · ${if (maybe != null) maybe.first + maybe.second else 0} · ${WindowInsets.statusBars.top.roundToInt()} · ${(alpha * 10).roundToInt()}",
             modifier = Modifier.testTag("özet"))
        Button(onClick = { scope.launch { requesters[1].requestFocus() } }) { Text("İkiye odaklan") }
    }
}

TEST(sections_forms_as_compose_writes_them) {
    KompozTestRule rule([] { SectionsForms(); });
    CHECK(rule.onNodeWithText("tasks 2 · yok · 7 · 0 · 5").exists() && rule.onNodeWithTag("görev1").exists() && rule.onNodeWithTag("arama").exists());
    rule.onNodeWithTag("aç").performClick();
    rule.mainClock().advanceTimeBy(500);
    CHECK(rule.onNodeWithText("Tamam").exists());
    CHECK(rule.onNodeWithText("tasks 2 · yok · 7 · 0 · 10").exists());  // (open: the tween went to 1)
    rule.onNodeWithText("İptal").performClick();
    rule.mainClock().advanceTimeBy(500);
    CHECK(rule.onNodeWithText("tasks 2 · yok · 7 · 0 · 5").exists() && rule.onNodeWithText("düğme 1").exists());
}

// Text styles as Compose writes them: a typography style's copy(brush = …, shadow = …, lineHeightStyle = …,
// textIndent = TextIndent(24.sp), fontFeatureSettings = …, lineBreak = …), basicMarquee by name, TextAutoSize.StepBased in sp,
// the measurer's style = a typography style.
@Kompozable
fun TextStyles() {
    val typography = MaterialTheme.typography
    val measurer = rememberTextMeasurer()
    val width = measurer.measure("Kompoz", style = typography.titleMedium).size.width
    Column {
        Text("degrade", style = typography.headlineSmall.copy(brush = Brush.horizontalGradient(listOf(Color.Red, Color.Blue))))
        Text("gölge", style = typography.titleLarge.copy(shadow = Shadow(Color.Black, Offset(2f, 3f), 4f)))
        Text("girinti", style = typography.bodyMedium.copy(textIndent = TextIndent(24.sp), lineBreak = LineBreak.Paragraph,
                                                           lineHeightStyle = LineHeightStyle(alignment = LineHeightStyle.Alignment.Top, trim = LineHeightStyle.Trim.None)))
        Text("küçük büyük", style = typography.titleMedium.copy(fontFeatureSettings = "smcp"))
        Text("kayan yazı kayan yazı kayan yazı kayan yazı kayan yazı", modifier = Modifier.width(120.dp).basicMarquee(iterations = Int.MAX_VALUE, velocity = 60.dp).testTag("kayan"))
        Text("sığ", modifier = Modifier.width(60.dp), maxLines = 1, autoSize = TextAutoSize.StepBased(8.sp, 40.sp, 1.sp))
        Text("ölçü ${if (width > 10f) "var" else "yok"}")
    }
}

TEST(text_styles_as_compose_writes_them) {
    KompozTestRule rule([] { TextStyles(); });
    CHECK(rule.onNodeWithText("degrade").exists() && rule.onNodeWithText("gölge").exists() && rule.onNodeWithText("girinti").exists());
    CHECK(rule.onNodeWithText("küçük büyük").exists() && rule.onNodeWithTag("kayan").exists() && rule.onNodeWithText("sığ").exists());
    CHECK(rule.onNodeWithText("ölçü var").exists());
}

// Drawing forms as Compose writes them: kotlin.math and PI, Rect(left, top, right, bottom) / Rect(offset, size),
// CornerSize(percent), a PathMeasure's length, withTransform's transform block, drawText(measurer, text, topLeft =,
// style =), record(layer, size =) { } with the layer's properties, onVisibilityChanged(minFractionVisible =) { },
// draggable2D(state, startDragImmediately =), rememberScrollable2DState's value, Offset.Zero, PointerIcon.Move.
@Kompozable
fun DrawingForms() {
    val measurer = rememberTextMeasurer()
    val layer = rememberGraphicsLayer()
    var seen by remember { mutableStateOf("gizli") }
    var pulled by remember { mutableStateOf(Offset.Zero) }
    val drag = rememberDraggable2DState { pulled = Offset(pulled.x + it.x, pulled.y + it.y) }
    val pan = rememberScrollable2DState { d -> Offset(min(d.x, 0f), max(d.y, 0f)) }
    Column {
        Canvas(modifier = Modifier.size(100.dp, 50.dp).testTag("biçimler")) {
            val wave = Path()
            for (i in 0..8) {
                val y = 25 + sin(i / 8f * 2 * PI.toFloat()) * 10
                if (i == 0) wave.moveTo(0f, y) else wave.lineTo(i * 12f, y)
            }
            val measure = PathMeasure()
            measure.setPath(wave, false)
            val half = Path()
            if (measure.getSegment(0f, measure.length / 2, half)) drawPath(half, Color.White, style = Stroke(width = 2f))
            val ring = Path()
            ring.addOval(Rect(Offset(60f, 10f), Size(30f, 30f)))
            ring.addRect(Rect(2f, 2f, 10f, 6f))
            drawPath(ring, Color.White)
            withTransform({
                translate(10f, 0f)
                rotate(45f, Offset(0f, 0f))
            }) { drawRect(Color.White, size = Size(10f, 10f)) }
            drawText(measurer, "ölçek", topLeft = Offset(2f, 30f), style = TextStyle(fontSize = 10.sp, color = Color.White))
        }
        Box(modifier = Modifier.size(40.dp, 40.dp).drawWithContent {
            record(layer, size = IntSize(40, 20)) { this@drawWithContent.drawContent() }
            drawLayer(layer)
            layer.topLeft = IntOffset(0, 20)
            layer.scaleY = -1f
            layer.alpha = 0.5f
            drawLayer(layer)
            layer.topLeft = IntOffset.Zero
            layer.scaleY = 1f
            layer.alpha = 1f
        }) { Box(modifier = Modifier.size(40.dp, 20.dp).background(Color.White)) }
        Box(modifier = Modifier
                .size(100.dp)
                .onVisibilityChanged(minFractionVisible = 0f) { seen = if (it) "görünür" else "gizli" }
                .draggable2D(drag, startDragImmediately = true)
                .scrollable2D(pan)
                .pointerHoverIcon(PointerIcon.Move)
                .clip(RoundedCornerShape(CornerSize(50), CornerSize(4.dp), CornerSize(4.dp), CornerSize(50)))
                .background(Color.White)
                .testTag("sürüklenir"))
        Text(seen)
        Text(if (pulled.x > 30f && pulled.y > 20f) "sürüklendi" else "duruyor")
        Text("pi ${PI.toFloat().toInt()} ${max(2, 3)} ${abs(-4f).toInt()} ${sqrt(16f).toInt()}")
    }
}

TEST(drawing_forms_as_compose_writes_them) {
    KompozTestRule rule([] { DrawingForms(); }, {400, 800});
    rule.waitForIdle();
    testing::RecordingCanvas canvas;
    rule.host().draw(canvas);
    CHECK(canvas.paths.size() >= 2 && !canvas.texts.empty() && canvas.texts[0] == "ölçek");
    CHECK(rule.onNodeWithText("pi 3 3 4 4").exists() && rule.onNodeWithText("görünür").exists() && rule.onNodeWithText("duruyor").exists());
    rule.swipe({50, 140}, {90, 170}, 10);  // the box at 90..190: from the press, the whole way
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("sürüklendi").exists());
}

// Input forms as Compose writes them: ContextMenuArea(items = { listOf(ContextMenuItem("…") { }) }) { }, TooltipArea(tooltip = { },
// delayMillis =, tooltipPlacement =) { }, Modifier.onClick(matcher =, keyboardModifiers = { }) { }, an event's changes.first(),
// transformable(state, canPan = { }), clickable(interactionSource = null, indication = null) { }, Popup(popupPositionProvider =
// object : PopupPositionProvider { … }, properties = PopupProperties(focusable = false)) { }, Offset arithmetic, DpOffset.
@Kompozable
fun InputForms() {
    var picked by remember { mutableStateOf("yok") }
    var clicks by remember { mutableStateOf(0) }
    var moved by remember { mutableStateOf(Offset.Zero) }
    var zoom by remember { mutableStateOf(1f) }
    val transform = rememberTransformableState { z, pan, _ ->
        zoom *= z
        moved += pan
    }
    Column {
        ContextMenuArea(items = { listOf(ContextMenuItem("Yenile") { picked = "Yenile" }, ContextMenuItem("Sil", {}, false)) }) {
            Box(modifier = Modifier.size(100.dp, 40.dp).testTag("alan"))
        }
        Text("seçim: $picked")
        TooltipArea(tooltip = { Text("ipucu", modifier = Modifier.testTag("ipucu")) }, delayMillis = 100,
                    tooltipPlacement = TooltipPlacement.ComponentRect(Alignment.BottomCenter, Alignment.BottomCenter, DpOffset(0.dp, 4.dp))) {
            Box(modifier = Modifier.size(100.dp, 40.dp).testTag("ipuçlu"))
        }
        Box(modifier = Modifier.size(100.dp, 40.dp).testTag("tıklanır")
                .onClick(matcher = PointerMatcher.mouse(PointerButton.Secondary)) { clicks++ }
                .onClick(keyboardModifiers = { it.isCtrlPressed }) { clicks += 10 }
                .onPointerEvent(PointerEventType.Move) { moved = it.changes.first().position }
                .transformable(transform, canPan = { zoom > 1f })
                .clickable(interactionSource = null, indication = null) { clicks += 100 })
        Text("tık: $clicks · ${moved.x.roundToInt()}")
        var open by remember { mutableStateOf(true) }
        if (open) {
            Popup(popupPositionProvider = object : PopupPositionProvider {
                override fun calculatePosition(anchorBounds: IntRect, windowSize: IntSize, layoutDirection: LayoutDirection, popupContentSize: IntSize): IntOffset {
                    return IntOffset(anchorBounds.left + 10, anchorBounds.bottom + popupContentSize.height)
                }
            }, onDismissRequest = { open = false }, properties = PopupProperties(focusable = false)) {
                Text("açılır", modifier = Modifier.testTag("açılır"))
            }
        }
    }
}

TEST(input_forms_as_compose_writes_them) {
    KompozTestRule rule([] { InputForms(); }, {400, 800});
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("seçim: yok").exists() && rule.onNodeWithTag("açılır").exists() && rule.host().overlayCount() == 1);
    CHECK(std::abs(rule.onNodeWithTag("açılır").bounds().x - 10) < 0.5f);
    Rect area = rule.onNodeWithTag("alan").bounds();
    CHECK(rule.host().secondaryClick({area.x + 50, area.y + 20}));
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("Yenile").exists());
    rule.onNodeWithText("Yenile").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("seçim: Yenile").exists());
    Rect tipped = rule.onNodeWithTag("ipuçlu").bounds();
    rule.host().pointerMove({tipped.x + 50, tipped.y + 20});
    rule.advanceTimeBy(300);
    CHECK(rule.onNodeWithTag("ipucu").exists());
    Rect clickable = rule.onNodeWithTag("tıklanır").bounds();
    rule.host().pointerMove({clickable.x + 50, clickable.y + 20});
    rule.waitForIdle();
    rule.host().mouseButton({clickable.x + 50, clickable.y + 20}, PointerButton::Secondary, true);
    rule.host().mouseButton({clickable.x + 50, clickable.y + 20}, PointerButton::Secondary, false);
    rule.advanceTimeBy(400);
    rule.onNodeWithTag("tıklanır").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("tık: 101 · 50").exists());
}

// State forms as Kotlin writes them: a Channel's trySend(…).isSuccess, consumeEach { order -> } suspending per element,
// scope.produce<Int> { send(i) }, a flow's collectAsStateWithLifecycle() as a member, PaddingValues(horizontal, vertical),
// list + element, Lifecycle.State names from LocalLifecycleOwner.current, a ViewConfiguration's timeouts in millis,
// semantics { contentType = … }, SharingStarted.WhileSubscribed(5000).
class TickViewModel : ViewModel() {
    val ticks: StateFlow<Int> = flow<Int> {
        var i = 0
        while (true) {
            emit(i++)
            delay(100)
        }
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), 0)
}

@Kompozable
fun KitchenForms() {
    val orders = remember { Channel<Int>(2) }
    var served by remember { mutableStateOf(listOf<Int>()) }
    var refused by remember { mutableStateOf(0) }
    var countdown by remember { mutableStateOf("") }
    val scope = rememberCoroutineScope()
    val vm = viewModel<TickViewModel>()
    val ticks by vm.ticks.collectAsStateWithLifecycle()
    val state by LocalLifecycleOwner.current.lifecycle.currentStateAsState()
    val gestures = LocalViewConfiguration.current
    LaunchedEffect(Unit) {
        orders.consumeEach { order ->
            delay(10)
            served = served + order
        }
    }
    Column(modifier = Modifier.padding(PaddingValues(12.dp, 8.dp))) {
        Box(modifier = Modifier.size(10.dp).semantics { contentType = ContentType.Username }.testTag("alan"))
        Button(onClick = { if (!orders.trySend(served.size + 1).isSuccess) refused++ }, modifier = Modifier.testTag("ver")) { Text("sipariş") }
        Button(onClick = {
            scope.launch {
                val numbers = scope.produce<Int> { for (i in 3 downTo 1) send(i) }
                numbers.consumeEach { countdown = countdown + it.toString() }
                countdown = countdown + "!"
            }
        }) { Text("say") }
        Text("servis ${served.size} · ret $refused · $countdown", modifier = Modifier.testTag("durum"))
        Text("durum ${if (state == Lifecycle.State.RESUMED || state == Lifecycle.State.STARTED) "açık" else "kapalı"} · ${gestures.longPressTimeoutMillis} ms · tik ${if (ticks >= 0) "var" else "yok"}")
    }
}

TEST(state_forms_as_kotlin_writes_them) {
    KompozTestRule rule([] { KitchenForms(); }, {400, 800});
    rule.host().setLifecycleState(LifecycleState::Resumed);
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("servis 0 · ret 0 · ").exists() && rule.onNodeWithText("durum açık · 400 ms · tik var").exists());
    CHECK(std::abs(rule.onNodeWithTag("alan").bounds().x - 12) < 0.5f && std::abs(rule.onNodeWithTag("alan").bounds().y - 8) < 0.5f);  // PaddingValues(12.dp, 8.dp)
    rule.onNodeWithTag("ver").performClick();
    rule.advanceTimeBy(50);
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("servis 1 · ret 0 · ").exists());
    rule.onNodeWithText("say").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("servis 1 · ret 0 · 321!").exists());
}

// Scroll forms as Compose writes them: try { } finally { } in a coroutine, AppBarRow(maxItemCount =) { clickableItem(onClick =,
// icon =, label =) }, AppBarWithSearch(state =, inputField =, navigationIcon =), Modifier.pullToRefresh by name with
// PullToRefreshDefaults.Indicator / IndicatorBox and LoadingIndicator(progress = { }), overscrollEffect = null and an
// OverscrollEffect? parameter, Animatable<Float, AnimationVector1D>.
@Kompozable
fun TiledRow(effect: OverscrollEffect?) {
    LazyRow(modifier = Modifier.height(40.dp), overscrollEffect = effect) { items(3) { Box(modifier = Modifier.size(40.dp)) } }
}

@Kompozable
fun ScrollForms(a: Animatable<Float, AnimationVector1D>) {
    var log by remember { mutableStateOf("") }
    var refreshing by remember { mutableStateOf(false) }
    val state = rememberPullToRefreshState()
    val search = rememberSearchBarState()
    val query = rememberTextFieldState()
    LaunchedEffect(Unit) {
        try {
            log = log + "içinde "
            a.snapTo(3f)
        } finally {
            log = log + "sonunda"
        }
    }
    Column {
        TopAppBar(title = { Text("belge") }, actions = {
            AppBarRow(maxItemCount = 2) {
                clickableItem(onClick = { log = log + " paylaş" }, icon = { Icon(Icons.Filled.Share, contentDescription = "Paylaş") }, label = "Paylaş")
                clickableItem(onClick = {}, icon = { Icon(Icons.Filled.Edit, contentDescription = "Düzenle") }, label = "Düzenle")
                clickableItem(onClick = {}, icon = { Icon(Icons.Filled.Delete, contentDescription = "Sil") }, label = "Sil")
            }
        })
        AppBarWithSearch(state = search,
                         inputField = { SearchBarDefaults.InputField(searchBarState = search, textFieldState = query, onSearch = {}, placeholder = { Text("ara") }) },
                         navigationIcon = { Icon(Icons.Filled.Menu, contentDescription = "Menü") })
        Box(modifier = Modifier.size(200.dp, 120.dp).pullToRefresh(isRefreshing = refreshing, state = state, onRefresh = { refreshing = true }, enabled = true).testTag("çekilir")) {
            PullToRefreshDefaults.Indicator(state = state, isRefreshing = refreshing, modifier = Modifier.align(Alignment.TopCenter))
            PullToRefreshDefaults.IndicatorBox(state = state, isRefreshing = refreshing, modifier = Modifier.align(Alignment.TopEnd), shape = CircleShape) {
                LoadingIndicator(progress = { min(1f, state.distanceFraction) }, modifier = Modifier.size(24.dp))
            }
        }
        TiledRow(rememberOverscrollEffect())
        TiledRow(null)
        Text("günlük: $log · ${a.value.roundToInt()}")
    }
}

TEST(scroll_forms_as_compose_writes_them) {
    Animatable<float> a(0.f);
    KompozTestRule rule([&] { ScrollForms(a); }, {400, 800});
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("günlük: içinde sonunda · 3").exists());
    CHECK(rule.onNodeWithContentDescription("Paylaş").exists() && rule.onNodeWithContentDescription("More options").exists());  // two slots: the rest in the menu
    rule.onNodeWithContentDescription("Paylaş").performClick();
    CHECK(rule.onNodeWithText("günlük: içinde sonunda paylaş · 3").exists());
    CHECK(rule.onNodeWithContentDescription("Menü").exists() && rule.onNodeWithTag("çekilir").exists());
}

// Layout forms as Compose writes them: ListDetailPaneScaffold(navigator =, listPane =, detailPane = { key -> }) with a
// VerticalDragHandle on Modifier.paneExpansionDraggable, onFirstVisible(minDurationMs =) { }, approachLayout(isMeasurementApproachInProgress =,
// approachMeasure =), Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally), spring<Float>(…), HorizontalPager(key = { }), a list
// indexed by a smart-cast nullable, "ab".repeat(2).
@Kompozable
fun LayoutForms() {
    val navigator = rememberListDetailPaneScaffoldNavigator<Int>()
    val expansion = rememberPaneExpansionState(listOf(PaneExpansionAnchor.Proportion(0.5f)))
    val cities = listOf("Ankara", "İzmir")
    var seen by remember { mutableStateOf(0) }
    val pages = listOf(3, 4)
    val pager = rememberPagerState(pageCount = { pages.size })
    val glide = spring<Float>(Spring.DampingRatioLowBouncy, Spring.StiffnessLow)
    val tagSize = remember { DeferredTargetAnimation<Size>(glide) }
    val scope = rememberCoroutineScope()
    Column {
        Box(modifier = Modifier.size(400.dp, 200.dp)) {
            ListDetailPaneScaffold(
                navigator = navigator,
                listPane = {
                    Column { for (i in 0 until 2) Text(cities[i], modifier = Modifier.clickable { navigator.navigateTo(ListDetailPaneScaffoldRole.Detail, i) }) }
                },
                detailPane = { city -> if (city == null) Text("seçin") else Text("şehir: ${cities[city]}") },
                listPaneWidth = 160.dp,
                paneExpansionState = expansion,
                paneExpansionDragHandle = { state -> VerticalDragHandle(modifier = Modifier.paneExpansionDraggable(state)) })
        }
        Row(modifier = Modifier.width(200.dp), horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally)) {
            Box(modifier = Modifier.size(20.dp).onFirstVisible(minDurationMs = 0) { seen++ }.testTag("nokta"))
            Box(modifier = Modifier.size(20.dp))
        }
        HorizontalPager(state = pager, modifier = Modifier.size(200.dp, 40.dp), key = { pages[it] }) { i -> Text("sayfa ${pages[i]}") }
        Box(modifier = Modifier
                .approachLayout(
                    isMeasurementApproachInProgress = { target ->
                        tagSize.updateTarget(target, scope)
                        !tagSize.isIdle()
                    },
                    approachMeasure = { s, measure, _, _ ->
                        val now = tagSize.updateTarget(s.lookaheadSize, scope)
                        measure(Constraints.fixed(now.width, now.height))
                        now
                    })
                .testTag("etiket")) { Text("ab".repeat(2)) }
        Text("görüldü $seen")
    }
}

TEST(layout_forms_as_compose_writes_them) {
    KompozTestRule rule([] { LayoutForms(); }, {400, 800});
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("Ankara").exists());
    rule.onNodeWithText("İzmir").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("şehir: İzmir").exists());
    CHECK(rule.onNodeWithText("abab").exists() && rule.onNodeWithText("sayfa 3").exists());
    CHECK(std::abs(rule.onNodeWithTag("nokta").bounds().x - 76) < 1);  // two 20 dp dots and their 8 dp gap, centered in 200
    rule.advanceTimeBy(100);
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("görüldü 1").exists());
}

// Graphics forms as Compose writes them: keyframes { } with `at` and `using`, spring(dampingRatio =, stiffness =),
// fadeIn(animationSpec =), slideInVertically(animationSpec =) { }, scaleOut(targetScale =), animateEnterExit(enter =, exit =),
// AnimatedVisibility(visibleState =) with MutableTransitionState(…).apply { targetState = … }, the wavy and contained
// indicators by name, an object : Painter(), Canvas(bitmap), ColorMatrix(floatArrayOf(…)), MaterialShapes.Heart, Offset.Unspecified,
// magnifier / shadow / paint / blur by name, drawRect(brush =, blendMode =), `and`, list - element, return@Label, Matrix().apply { },
// a top-level val's member (StripeVector.root) and a local of a function type called with its trailing lambda.
val StripeVector = ImageVector.Builder("stripe", 24.dp, 24.dp, 24f, 24f)
    .addPath(pathData = addPathNodes("M0,0h24v24h-24z"), fill = Brush.solid(Color.Red))
    .build()

@Kompozable
fun GraphicsForms() {
    val rows = listOf(1, 2, 3)
    var lens by remember { mutableStateOf(Offset.Unspecified) }
    var fed by remember { mutableStateOf(false) }
    val pulse = keyframes {
        durationMillis = 400
        1.5f at 100 using LinearEasing
        0.5f at 300
    }
    val bouncy = spring(dampingRatio = Spring.DampingRatioHighBouncy, stiffness = Spring.StiffnessLow)
    val shown = remember { MutableTransitionState(false).apply { targetState = true } }
    val half = remember {
        object : Painter() {
            override val intrinsicSize: Size get() = Size(20f, 10f)
            override fun DrawScope.onDraw() {
                drawRect(Color.Blue, size = Size(size.width / 2, size.height))
            }
        }
    }
    val bitmap = remember {
        val b = ImageBitmap(4, 4)
        val c = Canvas(b)
        c.drawRect(Rect(0f, 0f, 4f, 4f), Paint(color = Color.Red))
        b
    }
    val gray = ColorMatrix(floatArrayOf(0.5f, 0.5f, 0f, 0f, 0f))
    val doubled = Matrix().apply { scale(2f, 2f) }
    val label: @Kompozable (String, (Int) -> String) -> Unit = { name, format -> Text(name + format(7)) }
    LaunchedEffect(fed) {
        if (fed) return@LaunchedEffect
        fed = true
    }
    Column {
        Text("kare ${pulse.keyframes.size} yay ${bouncy.stiffness.toInt()} bit ${0xF0 and 0x3C} satır ${(rows - 2).size}")
        Text("gri ${(gray.values[1] * 10).toInt()} matris ${doubled.map(Offset(1f, 1f)).x.toInt()} akış $fed")
        label("ad") { "-$it" }
        AnimatedVisibility(visibleState = shown, enter = fadeIn(animationSpec = tween(100)) + expandVertically(animationSpec = tween(100)),
                           exit = fadeOut(animationSpec = tween(100))) {
            Box(modifier = Modifier.animateEnterExit(enter = slideInVertically(animationSpec = tween(100)) { it }, exit = scaleOut(targetScale = 0.5f))
                    .size(10.dp).testTag("giren"))
        }
        Text("durum ${shown.currentState}")
        LinearWavyProgressIndicator(progress = { 0.5f }, modifier = Modifier.width(100.dp).testTag("dalga"), amplitude = { 0.5f }, stopSize = 0.dp)
        CircularWavyProgressIndicator(trackColor = Color.Gray)
        ContainedLoadingIndicator(progress = { 0.3f }, modifier = Modifier.testTag("kap"), containerColor = Color.Yellow)
        Box(modifier = Modifier.size(20.dp).background(Color.Red, MaterialShapes.Heart.toShape()).testTag("kalp"))
        Box(modifier = Modifier.size(40.dp).magnifier(sourceCenter = { lens }, zoom = 2f, size = DpSize(40.dp, 20.dp)).testTag("büyüteç"))
        Box(modifier = Modifier.size(20.dp).shadow(elevation = 4.dp, shape = CircleShape, clip = false, ambientColor = Color.Red, spotColor = Color.Red))
        Box(modifier = Modifier.paint(painter = half).testTag("boya"))
        Box(modifier = Modifier.size(20.dp).blur(radius = 4.dp, edgeTreatment = BlurredEdgeTreatment.Unbounded, blurType = BlurType.Self))
        Canvas(modifier = Modifier.size(10.dp)) {
            drawRect(brush = Brush.verticalGradient(listOf(Color.Red, Color.Blue)), blendMode = BlendMode.SrcOver)
        }
        Image(bitmap = bitmap, contentDescription = "bit", colorFilter = ColorFilter.colorMatrix(gray))
        Image(painter = rememberVectorPainter(24.dp, 24.dp, 24f, 24f) { _, _ -> vector.RenderVectorGroup(StripeVector.root, mapOf()) },
              contentDescription = "şerit")
    }
}

TEST(graphics_forms_as_compose_writes_them) {
    KompozTestRule rule([] { GraphicsForms(); }, {400, 800});
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("kare 2 yay 200 bit 48 satır 2").exists());
    CHECK(rule.onNodeWithText("gri 5 matris 2 akış true").exists() && rule.onNodeWithText("ad-7").exists());
    rule.advanceTimeBy(500);
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("durum true").exists() && rule.onNodeWithTag("giren").exists());
    CHECK(std::abs(rule.onNodeWithTag("dalga").bounds().width - 100) < 0.5f && std::abs(rule.onNodeWithTag("kap").bounds().width - 48) < 0.5f);
    CHECK(std::abs(rule.onNodeWithTag("boya").bounds().width - 20) < 0.5f && std::abs(rule.onNodeWithTag("boya").bounds().height - 10) < 0.5f);
    CHECK(rule.onNodeWithTag("kalp").exists() && rule.onNodeWithTag("büyüteç").exists() && rule.onNodeWithContentDescription("şerit").exists());
}

// App shell forms as Compose writes them: destination.hierarchy.any { it.hasRoute<T>() }, destination?.hasRoute<T>(),
// navOptions { popUpTo<T> { saveState = true } }, kompozable<T>(deepLinks =) and navigation<T>(startDestination =),
// NavigationSuiteScaffold(navigationItems = { NavigationSuiteItem(…) }, navigationSuiteType =), MenuBar { Menu { … } },
// data.visuals.actionLabel and Compose's key names.
@Serializable
object Shell

@Serializable
object AccountGraph

@Serializable
data class Member(val n: Int)

@Kompozable
fun AppShellForms(nav: NavController) {
    val entry by nav.currentBackStackEntryAsState()
    val destination = entry?.destination
    val inAccount = destination?.hierarchy?.any { it.hasRoute<AccountGraph>() } == true
    val tabs = navOptions {
        popUpTo<Shell> { saveState = true }
        launchSingleTop = true
        restoreState = true
    }
    var dark by remember { mutableStateOf(false) }
    var size by remember { mutableStateOf(1) }
    val data = SnackbarData(message = "m", actionLabel = "Geri al")
    Column {
        MenuBar {
            Menu("Görünüm") {
                CheckboxItem("Koyu", checked = dark, shortcut = KeyShortcut(Key.D, ctrl = true), onCheckedChange = { dark = it })
                Separator()
                RadioButtonItem("Büyük", selected = size == 2, onClick = { size = 2 })
                Item("Geri", enabled = nav.previousBackStackEntry != null, shortcut = KeyShortcut(Key.DirectionLeft, alt = true)) { nav.popBackStack() }
            }
        }
        Text("koyu $dark boy $size eylem ${data.visuals.actionLabel?.length ?: 0}")
        NavigationSuiteScaffold(
            navigationItems = {
                NavigationSuiteItem(selected = !inAccount, onClick = { nav.navigate(Shell, tabs) },
                                    icon = { Icon(Icons.Filled.Home, contentDescription = null) }, label = { Text("Kabuk") })
                NavigationSuiteItem(selected = inAccount, onClick = { nav.navigate(AccountGraph, tabs) },
                                    icon = { Icon(Icons.Filled.Settings, contentDescription = null) }, label = { Text("Hesap") })
            },
            navigationSuiteType = NavigationSuiteType.NavigationRail,
            modifier = Modifier.weight(1f),
        ) {
            NavHost(navController = nav, startDestination = Shell) {
                kompozable<Shell> { Text(if (destination?.hasRoute<Shell>() == true) "kabuk ekranı" else "yolda") }
                navigation<AccountGraph>(startDestination = Member(0)) {
                    kompozable<Member>(deepLinks = listOf(navDeepLink<Member>("kompoz://member"))) { e -> Text("hesap ${e.toRoute<Member>().n}") }
                }
            }
        }
        Text(if (inAccount) "sekme hesap" else "sekme kabuk")
    }
}

TEST(app_shell_as_compose_writes_it) {
    NavController nav;
    KompozTestRule rule([&] { AppShellForms(nav); }, {600, 800});
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("kabuk ekranı").exists() && rule.onNodeWithText("sekme kabuk").exists());
    CHECK(rule.onNodeWithText("koyu false boy 1 eylem 7").exists());
    rule.onNodeWithText("Hesap").performClick();
    rule.waitForIdle();
    rule.advanceTimeBy(1000);
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("hesap 0").exists() && rule.onNodeWithText("sekme hesap").exists());
    rule.onNodeWithText("Görünüm").performClick();
    rule.waitForIdle();
    rule.onNodeWithText("Koyu").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("koyu true boy 1 eylem 7").exists());
    CHECK(nav.handleDeepLink("kompoz://member/5"));
    rule.waitForIdle();
    rule.advanceTimeBy(1000);
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("hesap 5").exists());
}

TEST(navigation_as_compose_writes_it) {
    NavController nav;
    KompozTestRule rule([&] { Navigator(nav); });
    rule.waitForIdle();  // (the entry read above the NavHost arrives with its first composition)
    CHECK(rule.onNodeWithText("Aç").exists());
    std::string rota = rule.onNodeWithText("rota ", true).text();
    CHECK(rota == "rota Start" || (std::printf("rota: %s\n", rota.c_str()), false));
    rule.onNodeWithText("Aç").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("ayrıntı 7 b").exists() && rule.onNodeWithText("rota Detail/{id}?tab={tab}").exists());
    rule.onNodeWithText("Geri").performClick();
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("Aç").exists());
    nav.navigate("kompoz://a/4");
    rule.waitForIdle();
    CHECK(rule.onNodeWithText("a 4").exists());
}

int main(int argc, char** argv) {
    for (auto& [name, fn] : registry()) {
        if (argc > 1 && std::string_view(name).find(argv[1]) == std::string_view::npos) continue;
        int before = failures;
        fn();
        std::printf("%s %s\n", failures == before ? "[ OK ]" : "[FAIL]", name);
    }
    std::printf("\n%zu tests, %d failed checks\n", registry().size(), failures);
    return failures == 0 ? 0 : 1;
}
