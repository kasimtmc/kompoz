// A source with Kotlin in it: which lines Kate highlights as which language (kate_test.py).

#include "app.hpp"

#include <string>

using namespace kompoz;

/* A C++ comment over lines, which a Kotlin word at a line's start doesn't leave:
fun NotKotlin() {}
*/
std::string label(int n) {
    return std::to_string(n) + " items";
}

data class Item(val id: Int, val title: String, val done: Boolean = false)

@Kompozable
fun ItemRow(item: Item, onToggle: (Boolean) -> Unit) {
    Row(modifier = Modifier.fillMaxWidth().clickable { onToggle(!item.done) }) {
        Checkbox(checked = item.done, onCheckedChange = onToggle)
        // a comment in the Kotlin
        Text("${item.title} #$item.id", style = MaterialTheme.typography.bodyLarge)
    }
}
// a comment at a line's start, still the Kotlin's
private val Spacing = 8.dp

object Routes {
    const val Home = "home"
}

enum class Mode(val label: String) { Light("light"), Dark("dark") }

sealed class Screen(val route: String)

template <typename T>
struct Box {
    T value;
};

class Plain {
    int x = 0;
};

kompoz::Application kompoz::createApplication(int, char**) {
    return {.title = "Items"};
}
