// A @Composable function in Compose's syntax, translated by the installed kompozc
// (kompoz_compose in CMakeLists.txt) and called from main.cpp's C++.
#include <kompoz/kompoz.hpp>

using namespace kompoz;

@Composable
fun Greeting(name: String, excited: Boolean = false) {
    Text(text = "Merhaba $name${if (excited) "!" else ""}", style = MaterialTheme.typography.titleMedium)
}
