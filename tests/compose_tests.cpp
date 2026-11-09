// @Composable functions written in Compose's own syntax, translated by kompozc before the C++
// compiler sees this file (kompoz_compose in CMakeLists.txt); the tests check what they do.

#include <kompoz/kompoz.hpp>
#include <kompoz/testing.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace kompoz;
using kompoz::testing::ComposeTestRule;

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

@Composable
fun Counter(start: Int = 0) {
    var count by remember { mutableStateOf(start) }
    Column {
        Text("Count: $count")
        Button({ count++ }) { Text("+") }
    }
}

TEST(a_delegated_state_counts) {
    ComposeTestRule rule([] { Counter(); });
    CHECK(rule.onNodeWithText("Count: 0").exists());
    rule.onNodeWithText("+").performClick();
    rule.onNodeWithText("+").performClick();
    CHECK(rule.onNodeWithText("Count: 2").exists());
}

// Named arguments in any order, defaults left out, a trailing lambda for the last parameter.
@Composable
fun Labeled(label: String, value: Int = 7, suffix: String = "", content: @Composable () -> Unit) {
    Row {
        Text("$label=$value$suffix")
        content()
    }
}

@Composable
fun NamedArguments() {
    Column {
        Labeled(suffix = "!", label = "a") { Text("içerik") }
        Labeled("b", 3) { }
    }
}

TEST(named_arguments_go_in_place) {
    ComposeTestRule rule([] { NamedArguments(); });
    CHECK(rule.onNodeWithText("a=7!").exists());
    CHECK(rule.onNodeWithText("içerik").exists());
    CHECK(rule.onNodeWithText("b=3").exists());
}

@Composable
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
    ComposeTestRule rule([] { Branches(4); });
    CHECK(rule.onNodeWithText("satır 0").exists() && rule.onNodeWithText("satır 3").exists());
    CHECK(!rule.onNodeWithText("satır 4").exists());
    CHECK(rule.onNodeWithText("geri 10").exists() && rule.onNodeWithText("geri 8").exists() && !rule.onNodeWithText("geri 9").exists());
    CHECK(rule.onNodeWithText("çok").exists() && rule.onNodeWithText("orta").exists() && rule.onNodeWithText("çift").exists());
    ComposeTestRule none([] { Branches(0); });
    CHECK(!none.onNodeWithText("satır 0").exists() && none.onNodeWithText("sıfır").exists() && none.onNodeWithText("az").exists());
}

@Composable
fun Templates() {
    val ratio = 2.5f
    val whole = 3f
    val name = "Kompoz"
    Text("${name.length} harf, $ratio, $whole, ${ratio * 2}", modifier = Modifier.testTag("t"))
}

TEST(string_templates_write_numbers_as_kotlin_does) {
    ComposeTestRule rule([] { Templates(); });
    CHECK(rule.onNodeWithTag("t").text() == "6 harf, 2.5, 3.0, 5.0");
}

@Composable
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
    ComposeTestRule rule([] { Collections(); });
    CHECK(rule.onNodeWithText("AYLA").exists() && rule.onNodeWithText("ALI").exists() && !rule.onNodeWithText("VELI").exists());
    CHECK(rule.onNodeWithTag("lengths").text() == "4+3+4");
    CHECK(rule.onNodeWithTag("indent").text() == "bir\n  iki");
    CHECK(rule.onNodeWithText("yok").exists() && rule.onNodeWithText("var: 4").exists() && rule.onNodeWithText("son: veli").exists());
}

@Composable
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
    ComposeTestRule rule([] { Ticker(); });
    rule.advanceTimeBy(150);
    CHECK(rule.onNodeWithText("tik 1").exists());
    rule.advanceTimeBy(500);
    CHECK(rule.onNodeWithText("tik 3").exists());
}

data class Note(val id: Int, val title: String, val pinned: Boolean = false, val tags: List<String> = listOf())

@Composable
fun NoteRow(note: Note) {
    Text("${note.id}: ${note.title}${if (note.pinned) " *" else ""}")
}

@Composable
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
    ComposeTestRule rule([] { Notes(); });
    CHECK(rule.onNodeWithText("1: alışveriş").exists() && rule.onNodeWithText("1: iş *").exists());
    CHECK(rule.onNodeWithTag("str").text() == "Note(id=1, title=iş, pinned=true, tags=[])");
    CHECK(rule.onNodeWithText("eşit").exists());
}

// The arguments a composable needs (onClick, value, checked, ...) by their names too.
@Composable
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
    ComposeTestRule rule([] { NamedHeads(); });
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

@Composable
fun StructsByName() {
    val s = Size2(height = 3, width = 2)
    Column {
        Text("${s.width}x${s.height}", modifier = Modifier.testTag("boyut"))
        Text("stil", style = TextStyle(maxLines = 1, fontSize = 30.sp), modifier = Modifier.testTag("stil"))
    }
}

TEST(structs_by_name) {
    ComposeTestRule rule([] { StructsByName(); });
    CHECK(rule.onNodeWithTag("boyut").text() == "2x3");
    CHECK(rule.onNodeWithTag("stil").bounds().height > 30);
}

// The user's own Compose code, as written for Android (textSize() is theirs too).
@Composable
fun textSize(): TextUnit = 18.sp

@Composable
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

@Composable
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
    ComposeTestRule rule([saved] { DialogHost([saved](std::string s) { *saved = s; }); });
    CHECK(rule.onNodeWithText("Vazgeç").exists() && rule.onNodeWithText("Ekle").exists());
    *saved = "-";
    rule.onNodeWithText("Ekle").performClick();
    CHECK(saved->empty());  // (nothing typed)
    CHECK(!rule.onNodeWithText("Ekle").exists());
    ComposeTestRule cancel([saved] { DialogHost([saved](std::string s) { *saved = s; }); });
    *saved = "-";
    cancel.onNodeWithText("Vazgeç").performClick();
    CHECK(*saved == "-" && !cancel.onNodeWithText("Vazgeç").exists());
}

// Compose's Scaffold sample, as its documentation writes it (an icon of Kompoz's for its own).
@Composable
fun ScaffoldExample() {
    var presses by remember { mutableIntStateOf(0) }

    Scaffold(
        topBar = {
            TopAppBar(
                colors = TopAppBarDefaults.topAppBarColors(
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
                Icon(Icons.Default.Add, contentDescription = "Add")
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

TEST(composes_scaffold_sample) {
    ComposeTestRule rule([] { ScaffoldExample(); });
    CHECK(rule.onNodeWithText("Top app bar").exists() && rule.onNodeWithText("Bottom app bar").exists());
    CHECK(rule.onNodeWithText("You have pressed the floating action button 0 times.", true).exists());
    rule.onNodeWithContentDescription("Add").performClick();
    rule.onNodeWithContentDescription("Add").performClick();
    CHECK(rule.onNodeWithText("You have pressed the floating action button 2 times.", true).exists());
    CHECK(rule.onNodeWithText("This is an example of a scaffold.", true).text().starts_with("This is an example"));  // (trimIndent: no indent)
}

// From compose_tests_other.cpp, as from another Kotlin file of the module.
@Composable
fun OtherFile() {
    Column {
        StickerRow(suffix = "!", sticker = Sticker(count = 3, label = "yeni"))
        StickerRow(Sticker("eski"))
    }
}

TEST(another_files_composables) {
    ComposeTestRule rule([] { OtherFile(); });
    CHECK(rule.onNodeWithText("yeni:3!").exists() && rule.onNodeWithText("eski:0").exists());
}

// From this file's C++ too: a file of the target still in C++ calls the module's Kotlin (the
// other file's function and data class) with no header, as the showcase's files do.
TEST(cpp_calls_the_modules_kotlin) {
    ComposeTestRule rule([] { StickerRow(Sticker{"çıkartma", 3}, "", "!"); });
    CHECK(rule.onNodeWithText("çıkartma:3!").exists());
}

// Kotlin's String.format with Java's specifiers, more of its collection functions, x++ as a value
// on a state, ListItemDefaults.colors and a constexpr constructor called by place (CornerRadius(24f)).
@Composable
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
    ComposeTestRule rule([] { KotlinHelpers(); });
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

@Composable
fun Inbox(mails: List<Mail>, log: (String) -> Unit) {
    DisposableEffect(mails.size) {
        log("açıldı ${mails.size}")
        onDispose { log("kapandı") }
    }
    MaterialTheme(colorScheme = darkColorScheme()) {
        Surface(shape = RoundedCornerShape(8.dp), color = MaterialTheme.colorScheme.surface) {
            CompositionLocalProvider(LocalContentColor provides Color.Red) {
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
    ComposeTestRule rule([log, show] {
        if (show.get()) Inbox({Mail{"ayşe"}, Mail{"ali"}}, [log](std::string s) { log->push_back(std::move(s)); });
    });
    CHECK(rule.onNodeWithText("Gelen kutusu").exists() && rule.onNodeWithText("1: ali").exists());
    show.set(false);
    rule.waitForIdle();
    CHECK((*log == std::vector<std::string>{"açıldı 2", "kapandı"}));
}

// ListItem as Compose writes it: its slots by name; an interactive one's headline is its block.
@Composable
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
    ComposeTestRule rule([] { Settings(); });
    CHECK(rule.onNodeWithText("Kapalı").exists() && rule.onNodeWithText("0 kez").exists());
    rule.onNodeWithText("Wi-Fi'yi aç").performClick();
    rule.onNodeWithText("Hakkında").performClick();
    CHECK(rule.onNodeWithText("Açık").exists() && rule.onNodeWithText("1 kez").exists());
}

// A lazy list's items with Compose's key = { … }: an item's state follows it when the list changes.
data class Row2(val id: Int, val label: String)

@Composable
fun KeyedRow(row: Row2) {
    var taps by remember { mutableStateOf(0) }
    Text("${row.label}:$taps", modifier = Modifier.clickable { taps++ })
}

@Composable
fun KeyedList(rows: List<Row2>) {
    LazyColumn(modifier = Modifier.height(400.dp)) {
        item(key = "başlık") { Text("Satırlar") }
        items(rows, key = { it.id }) { row -> KeyedRow(row) }
        item { Text(if (rows.size > 1) "çok" else "${Int.MAX_VALUE > 0}") }
    }
}

TEST(lazy_keys_by_name) {
    auto rows = mutableStateOf(std::vector<Row2>{Row2{1, "bir"}, Row2{2, "iki"}});
    ComposeTestRule rule([rows] { KeyedList(rows.get()); });
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

@Composable
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
    ComposeTestRule rule([] { StateForms(); });
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

@Composable
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

TEST(coroutines_await_what_suspends) {
    ComposeTestRule rule([] { Awaits(); });
    CHECK(rule.onNodeWithText("üretilen 0").exists() && rule.onNodeWithText("ikisi …").exists());
    rule.advanceTimeBy(400);
    CHECK(rule.onNodeWithText("üretilen 42").exists() && rule.onNodeWithText("ikisi 6 X").exists());
}

// A lambda with a receiver (Kotlin's T.() -> R): what its body doesn't declare is the receiver's,
// a local of the same name first, as in Kotlin; `this` is the receiver.
@Composable
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
    ComposeTestRule rule([=] { Receivers(tint); });
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

@Composable
fun SpacedTitle(title: String) {
    Text("$title ${LocalSpacing.current.large} $StickerGreeting")
}

val LocalSpacing = staticCompositionLocalOf { Spacing() }
val LocalAccount = compositionLocalOf<Account> { error("LocalAccount was not provided") }
private val Purple = Color(0xFF6650A4)

@Composable
fun AppTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = lightColorScheme(), content = content)
}

@Composable
fun Locals() {
    AppTheme {
        Column {
            SpacedTitle("geniş")
            CompositionLocalProvider(LocalSpacing provides Spacing(2.dp, 8.dp), content = { SpacedTitle("dar") })
            CompositionLocalProvider(LocalAccount provides Account("Ada")) {
                Box(modifier = Modifier.background(Purple), content = { Text(LocalAccount.current.name) })
            }
        }
    }
}

TEST(top_level_vals_and_content_by_name) {
    ComposeTestRule rule([] { Locals(); });
    CHECK(rule.onNodeWithText("geniş 16.0 selam !").exists() && rule.onNodeWithText("dar 8.0 selam !").exists());
    CHECK(rule.onNodeWithText("Ada").exists());
    bool threw = false;
    try {
        LocalAccount.current();  // (no provider, outside a composition: its default)
    } catch (const std::logic_error&) {
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

@Composable
fun Extensions() {
    Column {
        Box(modifier = Modifier.testTag("framed").framed(Color.Red).tallerBy(8.dp)) { Text(label(1)) }
        Text(shout(label(3)), modifier = Modifier.semantics { heading() })
    }
}

TEST(plain_and_extension_functions) {
    ComposeTestRule rule([] { Extensions(); });
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
@Composable
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
    ComposeTestRule rule([] { ComposeLayouts(); });
    Rect box = rule.onNodeWithTag("flex").bounds(), grown = rule.onNodeWithTag("grows").bounds();
    CHECK(std::abs(grown.x - box.x - 50) < 0.5f && std::abs(grown.width - 150) < 0.5f);
    Rect cell = rule.onNodeWithTag("cell").bounds();
    CHECK(std::abs(cell.x - 60) < 0.5f && std::abs(cell.width - 140) < 0.5f);
    CHECK(std::abs(rule.onNodeWithTag("flow1").bounds().x - 50) < 0.5f && std::abs(rule.onNodeWithTag("flow2").bounds().x) < 0.5f);
    CHECK(rule.onNodeWithText("wide").exists() && rule.onNodeWithText("has a window").exists());
}

// Compose's custom layouts: Layout's measure policy with layout(w, h) { … }, a var changed in
// forEach { } (an inline function's lambda reaches it by reference), SubcomposeLayout.
@Composable
fun Staircase(indent: Dp, content: @Composable () -> Unit) {
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

@Composable
fun Underlined(content: @Composable () -> Unit) {
    SubcomposeLayout(modifier = Modifier.testTag("underlined")) { constraints ->
        val main = subcompose("main", content).map { it.measure(constraints.copy(minWidth = 0f, minHeight = 0f)) }
        val width = main.maxOf { it.width }  // (composed as it is asked for: there on the first pass)
        val height = main.sumOf { it.height }
        val line = subcompose("line", width) { Box(modifier = Modifier.size(width, 2.dp)) }.map { it.measure(constraints) }
        layout(width, height + 2f) {
            main.forEach { it.placeRelative(0f, 0f) }
            line.forEach { it.placeRelative(0f, height) }
        }
    }
}

@Composable
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
    ComposeTestRule rule([] { CustomLayouts(); });
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

@Composable
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
    ComposeTestRule rule([] { SpannedGrid(); });
    CHECK(std::abs(rule.onNodeWithTag("header").bounds().width - 300) < 0.5f && std::abs(rule.onNodeWithTag("a").bounds().width - 200) < 0.5f);
}

// Material as Compose writes it: chips with label = { }, toggleable(role = Role.Checkbox), a
// snackbar awaited with actionLabel, progress = { }, NavigationSuiteScaffold's items block.
@Composable
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
    ComposeTestRule rule([&] { MaterialForms([&] { ++undone; }); }, {400, 800});
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

@Composable
fun counted(): Int = remember { ++gNextId }

@Composable
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
    ComposeTestRule rule([] { Themed(ThemeChoice::System); });
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

@Composable
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
    ComposeTestRule rule([] { TextForms(); });
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

@Composable
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
    ComposeTestRule rule([] { Drawings(); });
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

@Composable
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
    ComposeTestRule rule([&] { Animations(open.get(), count.get()); });
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

@Composable
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
    ComposeTestRule rule([&] { Gestures([&](std::string e) { log.push_back(std::move(e)); }); });
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

@Composable
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
    ComposeTestRule rule([&] { TodoScreen(player); });
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

@Composable
fun Navigator(nav: NavController) {
    val entry by nav.currentBackStackEntryAsState()
    val route = entry?.destination?.route
    var selecting by remember { mutableStateOf(false) }
    BackHandler(enabled = selecting) { selecting = false }
    Column {
        Text("rota ${route ?: "yok"}", modifier = Modifier.combinedClickable(onClick = {}, onLongClick = { selecting = true }))
        NavHost(navController = nav, startDestination = Start, modifier = Modifier.size(200.dp),
                enterTransition = { slideInHorizontally { it } }, exitTransition = { slideOutHorizontally { -it } }) {
            composable<Start> {
                Button(onClick = { nav.navigate(Detail(id = 7, tab = "b")) { launchSingleTop = true } }) { Text("Aç") }
            }
            composable<Detail> { backStackEntry ->
                val detail = backStackEntry.toRoute<Detail>()
                Text("ayrıntı ${detail.id} ${detail.tab ?: "-"}")
                Button(onClick = { nav.navigate(Start) { popUpTo<Start>(); launchSingleTop = true } }) { Text("Geri") }
            }
            navigation(startDestination = "a/{n}", route = "grup") {
                composable("a/{n}", deepLinks = listOf(navDeepLink { uriPattern = "kompoz://a/{n}" })) { e ->
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

@Composable
fun SideLine(vm: TodoViewModel, side: Side) {
    val items by vm.items.collectAsState()
    val counts = mapOf(1 to 3)
    val n = counts[1] ?: 0
    val m = counts[2] ?: 0
    Text("öğe ${items.size} · ${side.ordinal} · $n $m · ${counts.keys.sum()} · ${counts.values.sum()} · ${routes.Item(7).n}")
}

@Composable
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
    ComposeTestRule rule([] { Mixed(); });
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
@Composable
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
    ComposeTestRule rule([] { Expressive(); });
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
@Composable
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
    ComposeTestRule rule([] { Containers(); });
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

TEST(navigation_as_compose_writes_it) {
    NavController nav;
    ComposeTestRule rule([&] { Navigator(nav); });
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
