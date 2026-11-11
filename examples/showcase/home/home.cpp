// The home screen: the counter, the to-do list.

#include "showcase.hpp"

data class Todo(val id: Int, val title: String, val done: Boolean = false)

// Seconds since start, counted by a coroutine: LaunchedEffect starts it, leaving the
// composition cancels it. Only this scope recomposes every second.
@Kompozable
fun Uptime() {
    var seconds by remember { mutableStateOf(0) }
    LaunchedEffect(Unit) {
        while (true) {
            delay(1000)
            seconds++
        }
    }
    Text("$seconds sn önce açıldı", modifier = traced(),
         color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
}

@Kompozable
fun CounterCard() {
    var count by remember { mutableStateOf(0) }
    val progress by animateFloatAsState(count.coerceIn(0, 10) / 10f)
    val cs = MaterialTheme.colorScheme

    Card(modifier = traced(Modifier.fillMaxWidth())) {
        Column(modifier = Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text("Sayaç", style = MaterialTheme.typography.titleMedium)
            Text("$count", fontSize = 48.sp, fontWeight = FontWeight.Bold, color = cs.primary)
            Box(modifier = Modifier.fillMaxWidth().height(8.dp).background(cs.outlineVariant, RoundedCornerShape(4.dp))) {
                Box(modifier = Modifier.fillMaxWidth(progress).fillMaxHeight().background(cs.primary, RoundedCornerShape(4.dp)))
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { count-- }, enabled = count > 0) { Text("−") }
                Button(onClick = { count++ }) { Text("+ Arttır") }
                TextButton(onClick = { count = 0 }) { Text("Sıfırla") }
            }
        }
    }
}

// Skipped whenever the list recomposes but this item is unchanged.
@Kompozable
fun TodoRow(todo: Todo, todos: SnapshotStateList<Todo>) {
    val id = todo.id
    val setDone: (Boolean) -> Unit = { done ->
        val i = todos.indexOfFirst { it.id == id }
        if (i >= 0) todos[i] = todos[i].copy(done = done)
    }
    val remove = { todos.removeIf { it.id == id } }

    Row(modifier = traced(Modifier.fillMaxWidth().background(Color.Transparent, RoundedCornerShape(12.dp)).clickable { setDone(!todo.done) }),
        verticalAlignment = Alignment.CenterVertically) {
        Checkbox(checked = todo.done, onCheckedChange = setDone)
        Text(todo.title, modifier = Modifier.weight(1f),
             color = if (todo.done) MaterialTheme.colorScheme.onSurfaceVariant else LocalContentColor.current,
             textDecoration = if (todo.done) TextDecoration.LineThrough else TextDecoration.None)
        TextButton(onClick = remove) { Text("Sil") }
    }
}

@Kompozable
fun TodoCard() {
    val todos = remember {
        mutableStateListOf(Todo(1, "Compose runtime'ını incele", true), Todo(2, "Slot table yaz"), Todo(3, "Android'de çalıştır"))
    }
    var input by remember { mutableStateOf("") }
    var nextId by remember { mutableStateOf(4) }

    val add = {
        val title = input
        if (title.isNotBlank()) {
            todos.add(Todo(nextId++, title))
            input = ""
        }
    }
    val remaining = todos.count { !it.done }

    Card(modifier = traced(Modifier.fillMaxWidth())) {
        Column(modifier = Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("Yapılacaklar", modifier = Modifier.weight(1f), style = MaterialTheme.typography.titleMedium)
                Text("$remaining / ${todos.size} kaldı",
                     color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
            }
            Row(modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(value = input, onValueChange = { input = it },
                                  modifier = Modifier.weight(1f).testTag("todo-input"),
                                  singleLine = true, placeholder = { Text("örn. Slot table yaz") }, label = { Text("Yeni görev") },
                                  keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences),
                                  keyboardActions = KeyboardActions(onDone = { add() }))
                Button(onClick = add) { Text("Ekle") }
            }
            for (todo in todos) key(todo.id) { TodoRow(todo, todos) }
            if (todos.isEmpty())
                Text("Hepsi bitti!", modifier = Modifier.fillMaxWidth().padding(8.dp),
                     color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
        }
    }
}

// The list screen's FAB. derivedStateOf: this scope recomposes only when "scrolled or not"
// flips, not on every item that scrolls by.
@Kompozable
fun ScrollToTopButton(state: LazyListState) {
    val scrolled by remember { derivedStateOf { state.firstVisibleItemIndex > 0 } }
    val scope = rememberCoroutineScope()
    AnimatedVisibility(visible = scrolled, enter = fadeIn() + expandIn(), exit = fadeOut() + shrinkOut()) {
        FloatingActionButton(onClick = { scope.launch { state.scrollToItem(0) } }) { Icon(Icons.Filled.KeyboardArrowUp, contentDescription = "Başa dön") }
    }
}

@Kompozable
fun HomeScreen(modifier: Modifier) {
    val scroll = rememberScrollState()
    Column(modifier = modifier.verticalScroll(scroll).padding(20.dp, 8.dp, 20.dp, 96.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Card(modifier = Modifier.fillMaxWidth()) {
            Row(modifier = Modifier.padding(20.dp, 16.dp), horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Filled.Info, contentDescription = "Bilgi", modifier = Modifier.size(32.dp), tint = MaterialTheme.colorScheme.primary)
                Column(modifier = Modifier.weight(1f)) {
                    Text("C++20 ile Compose tarzı arayüz", style = MaterialTheme.typography.titleMedium)
                    Uptime()
                    WindowClassLine()
                    MediaLine()
                }
            }
        }
        CounterCard()
        TodoCard()
        Text("Kompoz · C++20", modifier = Modifier.fillMaxWidth(),
             fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
    }
}

// Previews (KOMPOZ_PREVIEW=CounterCardPreview ./build/showcase, or =* for all of them).
@Preview(widthDp = 400, heightDp = 220)
@Kompozable
fun CounterCardPreview() {
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) darkColorScheme() else lightColorScheme()) {
        Surface(modifier = Modifier.fillMaxSize()) { Box(modifier = Modifier.padding(16.dp)) { CounterCard() } }
    }
}

@Preview(widthDp = 400, heightDp = 360, uiMode = Configuration.UI_MODE_NIGHT_YES)
@Kompozable
fun TodoCardDarkPreview() {
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) darkColorScheme() else lightColorScheme()) {
        Surface(modifier = Modifier.fillMaxSize()) { Box(modifier = Modifier.padding(16.dp)) { TodoCard() } }
    }
}
