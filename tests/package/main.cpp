// The README's first example, built against the installed package.
#include <kompoz/kompoz.hpp>
using namespace kompoz;

void Counter() {
    auto count = remember([] { return mutableStateOf(0); });

    Column(Modifier().padding(16), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
        Text("Count: " + std::to_string(count.get()), {.font = typography().titleLarge});
        Button([count] { count.set(count.get() + 1); }, [] { Text("Increment"); });
    });
}

kompoz::Application kompoz::createApplication(int, char**) {
    return {.title = "Demo", .content = [] { MaterialTheme(lightColorScheme(), Counter); }};
}
