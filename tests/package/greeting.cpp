// A @Kompozable function in Kotlin's syntax, translated by the installed kompozc
// (kompozable in CMakeLists.txt) and called from main.cpp's C++.
#include <kompoz/kompoz.hpp>

using namespace kompoz;

@Kompozable
fun Greeting(name: String, excited: Boolean = false) {
    Text(text = "Merhaba $name${if (excited) "!" else ""}", style = MaterialTheme.typography.titleMedium)
}
