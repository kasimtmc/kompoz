// The home screen: the counter, the to-do list.

#include "showcase.hpp"

struct Todo {
    int id;
    std::string title;
    bool done = false;
    bool operator==(const Todo&) const = default;
};

// Seconds since start, counted by a coroutine: LaunchedEffect starts it, leaving the
// composition cancels it. Only this scope recomposes every second.
void Uptime(CallSite site = CallSite::current()) {
    composable(site, [=] {
        auto seconds = remember([] { return mutableStateOf(0); });
        LaunchedEffect(0, [seconds]() -> Task {
            while (true) {
                co_await delay(std::chrono::seconds(1));
                seconds.set(seconds.get() + 1);
            }
        });
        Text(std::format("{} sn önce açıldı", seconds.get()), traced(),
             {.font = typography().bodyMedium, .color = colorScheme().onSurfaceVariant});
    });
}

void CounterCard(CallSite site = CallSite::current()) {
    composable(site, [=] {
        auto count = remember([] { return mutableStateOf(0); });
        float progress = animateFloatAsState(float(std::clamp(count.get(), 0, 10)) / 10.f);
        const ColorScheme& cs = colorScheme();

        Card(traced(Modifier().fillMaxWidth()), [&] {
            Column(Modifier().padding(20), {.verticalArrangement = Arrangement::spacedBy(12)}, [&] {
                Text("Sayaç", {.font = typography().titleMedium});
                Text(std::to_string(count.get()), {.fontSize = 48, .fontWeight = FontWeight::Bold, .color = cs.primary});
                Box(Modifier().fillMaxWidth().height(8).background(cs.outlineVariant, 4), [&] {
                    Box(Modifier().fillMaxWidth(progress).fillMaxHeight().background(cs.primary, 4));
                });
                Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                    OutlinedButton([count] { count.set(count.get() - 1); }, Modifier(), {.enabled = count.get() > 0},
                                   [] { Text("−"); });
                    Button([count] { count.set(count.get() + 1); }, [] { Text("+ Arttır"); });
                    TextButton([count] { count.set(0); }, [] { Text("Sıfırla"); });
                });
            });
        });
    });
}

// Skipped whenever the list recomposes but this item is unchanged.
void TodoRow(const Todo& todo, MutableState<std::vector<Todo>> todos, CallSite site = CallSite::current()) {
    composable(site, todo, todos, [=] {
        int id = todo.id;
        auto setDone = [todos, id](bool done) {
            todos.update([&](auto& list) {
                for (auto& t : list)
                    if (t.id == id) t.done = done;
            });
        };
        auto remove = [todos, id] {
            todos.update([&](auto& list) { std::erase_if(list, [&](const Todo& t) { return t.id == id; }); });
        };

        Row(traced(Modifier().fillMaxWidth().background(colors::Transparent, 12).clickable([setDone, done = todo.done] {
                setDone(!done);
            })),
            {.verticalAlignment = Alignment::CenterVertically}, [&] {
                Checkbox(todo.done, setDone);
                Text(todo.title, Modifier().weight(1),
                     {.color = todo.done ? std::optional(colorScheme().onSurfaceVariant) : std::nullopt,
                      .textDecoration = todo.done ? TextDecoration::LineThrough : TextDecoration::None});
                TextButton(remove, [] { Text("Sil"); });
            });
    });
}

void TodoCard(CallSite site = CallSite::current()) {
    composable(site, [=] {
        auto todos = remember([] {
            return mutableStateOf(std::vector<Todo>{
                {1, "Compose runtime'ını incele", true}, {2, "Slot table yaz"}, {3, "Android'de çalıştır"}});
        });
        auto input = remember([] { return mutableStateOf(std::string{}); });
        auto nextId = remember([] { return std::make_shared<int>(4); });

        auto add = [todos, input, nextId] {
            std::string title = input.get();
            if (title.find_first_not_of(' ') == std::string::npos) return;
            todos.update([&](auto& list) { list.push_back({(*nextId)++, title}); });
            input.set("");
        };
        auto remaining = std::ranges::count_if(todos.get(), [](const Todo& t) { return !t.done; });

        Card(traced(Modifier().fillMaxWidth()), [&] {
            Column(Modifier().padding(20), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
                Row(Modifier().fillMaxWidth(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    Text("Yapılacaklar", Modifier().weight(1), {.font = typography().titleMedium});
                    Text(std::format("{} / {} kaldı", remaining, todos->size()),
                         {.font = typography().bodyMedium, .color = colorScheme().onSurfaceVariant});
                });
                Row(Modifier().fillMaxWidth(),
                    {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically},
                    [&] {
                        OutlinedTextField(input.get(), [input](std::string v) { input.set(std::move(v)); },
                                          Modifier().weight(1).testTag("todo-input"),
                                          {.singleLine = true, .placeholder = "örn. Slot table yaz", .onSubmit = add, .label = "Yeni görev",
                                           .keyboardOptions = {.capitalization = KeyboardCapitalization::Sentences}});
                        Button(add, [] { Text("Ekle"); });
                    });
                for (const Todo& todo : todos.get()) key(todo.id, [&] { TodoRow(todo, todos); });
                if (todos->empty())
                    Text("Hepsi bitti!", Modifier().fillMaxWidth().padding(8),
                         {.color = colorScheme().onSurfaceVariant, .textAlign = TextAlign::Center});
            });
        });
    });
}

// The list screen's FAB. derivedStateOf: this scope recomposes only when "scrolled or not"
// flips, not on every item that scrolls by.
void ScrollToTopButton(LazyListState state, CallSite site) {
    composable(site, state, [=] {
        auto scrolled = remember([state] { return derivedStateOf([state] { return state.firstVisibleItemIndex() > 0; }); });
        AnimatedVisibility(scrolled.get(), Modifier(), fadeIn() + expandIn(), fadeOut() + shrinkOut(), [&] {
            FloatingActionButton([state] { state.scrollToItem(0); },
                                 [] { Icon(Icons::Filled::KeyboardArrowUp, "Başa dön"); });
        });
    });
}

void HomeScreen(Modifier modifier, CallSite site) {
    composable(site, modifier, [=] {
        auto scroll = rememberScrollState();
        Column(modifier.verticalScroll(scroll).padding(20, 8, 20, 96), {.verticalArrangement = Arrangement::spacedBy(16)},
               [&] {
                   Card(Modifier().fillMaxWidth(), [&] {
                       Row(Modifier().padding(20, 16), {.horizontalArrangement = Arrangement::spacedBy(16),
                                                        .verticalAlignment = Alignment::CenterVertically},
                           [&] {
                               Icon(Icons::Filled::Info, "Bilgi", Modifier().size(32), colorScheme().primary);
                               Column(Modifier().weight(1), [&] {
                                   Text("C++20 ile Compose tarzı arayüz", {.font = typography().titleMedium});
                                   Uptime();
                                   WindowClassLine();
                                   MediaLine();
                               });
                           });
                   });
                   CounterCard();
                   TodoCard();
                   Text("Kompoz · C++20 · SDL3", Modifier().fillMaxWidth(),
                        {.fontSize = 12, .color = colorScheme().onSurfaceVariant, .textAlign = TextAlign::Center});
               });
    });
}

// Previews (KOMPOZ_PREVIEW=CounterCardPreview ./build/showcase, or =* for all of them).
KOMPOZ_PREVIEW(CounterCardPreview, .width = 400, .height = 220) {
    MaterialTheme(isSystemInDarkTheme() ? darkColorScheme() : lightColorScheme(), [] {
        Surface(Modifier().fillMaxSize(), [] { Box(Modifier().padding(16), [] { CounterCard(); }); });
    });
}
KOMPOZ_PREVIEW(TodoCardDarkPreview, .width = 400, .height = 360, .darkTheme = true) {
    MaterialTheme(isSystemInDarkTheme() ? darkColorScheme() : lightColorScheme(), [] {
        Surface(Modifier().fillMaxSize(), [] { Box(Modifier().padding(16), [] { TodoCard(); }); });
    });
}
