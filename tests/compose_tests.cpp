// @Composable functions written in Compose's own syntax, translated by kompozc before the C++
// compiler sees this file (kompoz_compose in CMakeLists.txt); the tests check what they do.

#include <kompoz/kompoz.hpp>
#include <kompoz/testing.hpp>

#include <cstdio>
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
