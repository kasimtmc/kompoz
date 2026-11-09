// Helpers every part uses: a section card, a desktop note, the theme animation, test colors.

#include "showcase.hpp"

// Every color animates when the theme changes. The calls need a stable position in the
// composition, which ComposableScope plus the fixed call order inside it provides.
ColorScheme animateColorScheme(const ColorScheme& t, CallSite site) {
    ComposableScope scope(site);
    // Every color gets its own animation; their call sites differ by order (positional memoization).
    return t.map([](Color c) { return animateColorAsState(c, 400); });
}

Color hueColor(int i) {
    float h = float((i * 47) % 360) / 60.f, s = 0.45f, v = 0.72f;
    float c = v * s, x = c * (1 - std::abs(std::fmod(h, 2.f) - 1)), m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 1) r = c, g = x;
    else if (h < 2) r = x, g = c;
    else if (h < 3) g = c, b = x;
    else if (h < 4) g = x, b = c;
    else if (h < 5) r = x, b = c;
    else r = c, b = x;
    return {r + m, g + m, b + m, 1};
}

// A small titled section of the components screen.
@Composable
fun Section(title: String, content: @Composable () -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            content()
        }
    }
}

// On a phone, above a section that needs what phones seldom have (a hardware keyboard): a warning.
@Composable
fun DesktopNote(text: String) {
    if (!kOnAndroid) return
    val cs = MaterialTheme.colorScheme
    Row(modifier = Modifier.fillMaxWidth().background(cs.tertiaryContainer, RoundedCornerShape(12.dp)).padding(12.dp, 8.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
        Icon(Icons.Filled.Info, contentDescription = "Uyarı", modifier = Modifier.size(18.dp), tint = cs.onTertiaryContainer)
        Text(text, modifier = Modifier.weight(1f), color = cs.onTertiaryContainer, style = MaterialTheme.typography.bodySmall)
    }
}
