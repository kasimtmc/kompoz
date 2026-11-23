// The README's first example, built against the installed package, and a window of its own (Window) that the
// click opens, where the platform opens more than one (CI's headless runs shoot it: "@Merhaba: shot window.png").
#include <kompoz/kompoz.hpp>
#include <kompoz/blocks.hpp>
using namespace kompoz;

// greeting.cpp's, written in Kotlin's syntax (kompozc gives it this C++ signature).
void Greeting(const std::string& name, bool excited = false, CallSite site = CallSite::current());

void Counter() {
    auto count = remember([] { return mutableStateOf(0); });

    Column(Modifier().padding(16), verticalArrangement = Arrangement::spacedBy(8)) {
        Text("Count: " + std::to_string(count.get()), style = typography().titleLarge);
        Button([count] { count.set(count.get() + 1); }) { Text("Increment"); };
        Greeting("Kompoz", count.get() > 0);
    };
    if (count.get() > 0)
        Window(onCloseRequest = [count] { count.set(0); }, title = "Merhaba", kompoz::state = rememberWindowState(arg::size = DpSize(240, 120))) {
            Greeting("pencere", true);
        };
}

kompoz::Application kompoz::createApplication(int, char**) {
    return {.title = "Demo", .content = [] { MaterialTheme(lightColorScheme(), Counter); }};
}
