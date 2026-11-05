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
