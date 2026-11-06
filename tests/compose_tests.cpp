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
    rule.onNodeWithText("Add").performClick();  // (an Icon's description is its node's text here)
    rule.onNodeWithText("Add").performClick();
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
