// The places screen: a BottomSheetScaffold over a map.

#include "showcase.hpp"

val placeNames = listOf("Kadıköy İskelesi", "Moda Sahili", "Yeldeğirmeni", "Fenerbahçe Parkı", "Bağdat Caddesi", "Haydarpaşa Garı",
                        "Kalamış Marina", "Süreyya Operası", "Kuşdili Çayırı", "Fikirtepe")

// BottomSheetScaffold: a pretend map, and a sheet of nearby places peeking over it. Dragged up
// the sheet opens, and then its list scrolls; its buttons move it from code.
@Composable
fun PlacesScreen(modifier: Modifier) {
    val cs = MaterialTheme.colorScheme
    val state = rememberBottomSheetScaffoldState()
    val sheet = state.bottomSheetState
    val scope = rememberCoroutineScope()
    BottomSheetScaffold(
        sheetContent = {
            Column(modifier = Modifier.fillMaxWidth().padding(20.dp, 0.dp)) {
                Text("Yakındaki yerler", style = MaterialTheme.typography.titleLarge)
                Text("Yukarı sürükleyin: önce sayfa açılır, sonra liste kayar.",
                     color = cs.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
                Row(modifier = Modifier.padding(0.dp, 12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilledTonalButton(onClick = { scope.launch { sheet.expand() } }) { Text("Aç") }
                    OutlinedButton(onClick = { scope.launch { sheet.partialExpand() } }) { Text("Daralt") }
                }
            }
            LazyColumn(modifier = Modifier.fillMaxWidth().height(420.dp), contentPadding = PaddingValues(8.dp, 0.dp, 8.dp, 8.dp)) {
                items(30) { i ->
                    ListItem(headlineContent = { Text(placeNames[i % 10]) },
                             supportingContent = { Text("%.1f km · %d dk yürüme".format(0.3 + 0.27 * i, 4 + 3 * i)) },
                             leadingContent = { Icon(Icons.Filled.Place, contentDescription = "Yer") },
                             colors = ListItemDefaults.colors(containerColor = Color.Transparent))
                }
            }
        },
        modifier = Modifier.fillMaxSize(), scaffoldState = state, sheetPeekHeight = 128.dp,
    ) { padding ->
        Box(modifier = Modifier.fillMaxSize()) {
            Canvas(modifier = Modifier.fillMaxSize()) {  // streets, a park and the sea
                drawRect(cs.surfaceContainerHighest)
                drawRect(Color(0x734F8FBF), topLeft = Offset(0f, size.height * 0.62f), size = Size(size.width, size.height * 0.38f))
                drawRoundRect(Color(0x806FAF6F), topLeft = Offset(size.width * 0.55f, size.height * 0.12f),
                              size = Size(size.width * 0.3f, size.height * 0.2f), cornerRadius = CornerRadius(24f))
                for (i in 1 until 6) {
                    drawLine(cs.outlineVariant, start = Offset(size.width * i / 6, 0f), end = Offset(size.width * i / 6 - 40, size.height), strokeWidth = 6f)
                    drawLine(cs.outlineVariant, start = Offset(0f, size.height * i / 8), end = Offset(size.width, size.height * i / 8 + 30), strokeWidth = 4f)
                }
                for (p in listOf(Offset(0.3f, 0.3f), Offset(0.7f, 0.45f), Offset(0.45f, 0.55f), Offset(0.2f, 0.5f))) {
                    drawCircle(cs.error, radius = 10f, center = Offset(size.width * p.x, size.height * p.y))
                    drawCircle(Color.White, radius = 4f, center = Offset(size.width * p.x, size.height * p.y))
                }
            }
            val where = if (sheet.currentValue == SheetValue.Expanded) "açık" else "kısmen açık"
            Box(modifier = Modifier.padding(padding).padding(16.dp).background(cs.surface.copy(alpha = 0.9f), RoundedCornerShape(16.dp)).padding(12.dp, 8.dp)) {
                Text("Sayfa: $where", style = MaterialTheme.typography.labelLarge)
            }
        }
    }
}
