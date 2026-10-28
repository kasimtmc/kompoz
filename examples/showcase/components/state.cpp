// Components screen: icons, state flows, movable content, channels, the lifecycle, platform services, a chat.

#include "showcase.hpp"

void IconGallery(CallSite site) {
    composable(site, [=] {
        using namespace Icons::Filled;
        const std::vector<std::pair<const char*, const ImageVector*>> icons = {
            {"Add", &Add}, {"ArrowBack", &ArrowBack}, {"Check", &Check}, {"Close", &Close},
            {"Delete", &Delete}, {"Edit", &Edit}, {"Favorite", &Favorite}, {"FavoriteBorder", &FavoriteBorder},
            {"Home", &Home}, {"Info", &Info}, {"Menu", &Menu}, {"MoreVert", &MoreVert},
            {"Notifications", &Notifications}, {"Person", &Person}, {"Search", &Search}, {"Settings", &Settings},
            {"Share", &Share}, {"Star", &Star}, {"Email", &Email}, {"Lock", &Lock},
            {"Place", &Place}, {"Refresh", &Refresh}, {"Visibility", &Visibility}, {"DarkMode", &DarkMode},
            {"PlayArrow", &PlayArrow}, {"Pause", &Pause}, {"List", &List}, {"CheckCircle", &CheckCircle},
            {"AccountCircle", &AccountCircle}, {"KeyboardArrowUp", &KeyboardArrowUp}};
        Section("Icon", [&] {
            for (size_t row = 0; row < icons.size(); row += 5) {
                Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::SpaceBetween}, [&] {
                    for (size_t i = row; i < std::min(icons.size(), row + 5); ++i) {
                        Column(Modifier().width(60), {.horizontalAlignment = Alignment::CenterHorizontally}, [&] {
                            Icon(*icons[i].second, icons[i].first);
                            Text(icons[i].first, {.fontSize = 9, .color = colorScheme().onSurfaceVariant, .maxLines = 1,
                                                  .overflow = TextOverflow::Ellipsis});
                        });
                    }
                });
            }
        });
    });
}

void IconButtons(CallSite site) {
    composable(site, [=] {
        auto liked = remember([] { return mutableStateOf(false); });
        Section("IconButton", [&] {
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    IconButton([liked] { liked.set(!liked.get()); }, [&] {
                        Icon(liked.get() ? Icons::Filled::Favorite : Icons::Filled::FavoriteBorder, "Beğen", {},
                             liked.get() ? std::optional(colorScheme().error) : std::nullopt);
                    });
                    FilledIconButton([] {}, [] { Icon(Icons::Filled::Add, "Ekle"); });
                    FilledTonalIconButton([] {}, [] { Icon(Icons::Filled::Edit, "Düzenle"); });
                    IconButton([] {}, Modifier(), {.enabled = false}, [] { Icon(Icons::Filled::Delete, "Sil"); });
                    OutlinedIconButton([] {}, [] { Icon(Icons::Filled::Share, "Paylaş"); });
                    Icon(Icons::Filled::Star, "Büyük yıldız", Modifier().size(40), colorScheme().primary);
                });
            // The toggles: each keeps its own on/off; `checked` picks the colors.
            auto starred = remember([] { return mutableStateOf(false); });
            auto bell = remember([] { return mutableStateOf(true); });
            auto pinned = remember([] { return mutableStateOf(false); });
            auto locked = remember([] { return mutableStateOf(false); });
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    IconToggleButton(starred.get(), [starred](bool on) { starred.set(on); }, [] { Icon(Icons::Filled::Star, "Yıldızla"); });
                    FilledIconToggleButton(bell.get(), [bell](bool on) { bell.set(on); },
                                           [] { Icon(Icons::Filled::Notifications, "Bildirimler"); });
                    FilledTonalIconToggleButton(pinned.get(), [pinned](bool on) { pinned.set(on); },
                                                [] { Icon(Icons::Filled::Place, "Sabitle"); });
                    OutlinedIconToggleButton(locked.get(), [locked](bool on) { locked.set(on); },
                                             [] { Icon(Icons::Filled::Lock, "Kilitle"); });
                    FilledIconToggleButton(true, {}, Modifier(), {.enabled = false}, [] { Icon(Icons::Filled::Check, "Seçili"); });
                });
            // Under the toggles: beside them (48 dp of room each) it would be squeezed on a phone.
            Text(std::string(bell.get() ? "Bildirim açık" : "Bildirim kapalı") + (locked.get() ? ", kilitli" : ""),
                 {.font = typography().bodySmall});
        });
    });
}

// A ViewModel: the screen's state as StateFlows, set by work on a worker thread; the screen
// reads them with collectAsState(). It belongs to the Components entry of the NavHost, so a
// download goes on while another tab is shown, and stops when the entry is gone. (The web has
// no threads: there the same work runs in viewModelScope, on the UI thread.)
class DownloadViewModel : public ViewModel {
public:
    MutableStateFlow<float> progress{0.f};
    MutableStateFlow<std::string> status{"Hazır"};

    ~DownloadViewModel() override { cancel(); }
    void start() {
        cancel();
        cancelled_ = std::make_shared<std::atomic<bool>>(false);
        auto step = [progress = progress, status = status](int i) {
            progress.setValue(float(i) / 100.f);
            status.setValue(std::format("İndiriliyor… %{}", i));
        };
#ifdef __EMSCRIPTEN__
        viewModelScope().launch([step, status = status, cancelled = cancelled_]() -> Task {
            for (int i = 1; i <= 100 && !*cancelled; ++i) {
                co_await delay(std::chrono::milliseconds(30));
                step(i);
            }
            status.setValue(*cancelled ? "İptal edildi" : "Tamamlandı");
        });
#else
        worker_ = std::thread([step, status = status, cancelled = cancelled_] {
            for (int i = 1; i <= 100 && !*cancelled; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                step(i);
            }
            status.setValue(*cancelled ? "İptal edildi" : "Tamamlandı");
        });
#endif
    }
    void cancel() {
        *cancelled_ = true;
#ifndef __EMSCRIPTEN__
        if (worker_.joinable()) worker_.join();
#endif
    }

protected:
    void onCleared() override { cancel(); }

private:
#ifndef __EMSCRIPTEN__
    std::thread worker_;
#endif
    std::shared_ptr<std::atomic<bool>> cancelled_ = std::make_shared<std::atomic<bool>>(false);
};

void StateFlowSection(CallSite site) {
    composable(site, [=] {
        auto vm = viewModel<DownloadViewModel>();
        float progress = vm->progress.collectAsState();
        std::string status = vm->status.collectAsState();
        Section("StateFlow · collectAsState · viewModel()", [&] {
            Text("İş bir iş parçacığında yürür ve StateFlow'ları günceller; ekran onları collectAsState() ile okur. "
                 "ViewModel bu ekranın gezinme girdisine ait: başka sekmeye geçip dönünce indirme sürmüş olur.",
                 {.font = typography().bodyMedium, .color = colorScheme().onSurfaceVariant});
            LinearProgressIndicator(progress, Modifier().fillMaxWidth().padding(0, 12));
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    Button([vm] { vm->start(); }, [] { Text("İndir"); });
                    OutlinedButton([vm] { vm->cancel(); }, Modifier(), {}, [] { Text("İptal"); });
                    Text(status, Modifier().weight(1), {.font = typography().bodyMedium, .textAlign = TextAlign::End});
                });
        });
    });
}

// A search as Compose apps write it with flows: the query waits until typing stops
// (debounce), each query is searched (a pretend 600 ms lookup; a newer query cancels it:
// flatMapLatest), and the results are a StateFlow the screen reads (stateIn).
class CitySearchViewModel : public ViewModel {
public:
    struct Results {
        bool searching = false;
        std::vector<std::string> cities;
        friend bool operator==(const Results&, const Results&) = default;
    };
    // The query lives in the SavedStateHandle: Android ending the app in the background and
    // bringing it back finds it still typed (and searched).
    explicit CitySearchViewModel(SavedStateHandle state) : state_(state), query(state.getStateFlow<std::string>("query", "")) {}
    void setQuery(std::string q) { state_.set("query", std::move(q)); }

private:
    SavedStateHandle state_;

public:
    StateFlow<std::string> query;
    // One-off events for the screen (a SharedFlow: nothing is kept for when nobody listens).
    MutableSharedFlow<std::string> events;
    // Searched only while the screen collects the results, and five seconds after it stopped.
    StateFlow<Results> results = query.debounce(std::chrono::milliseconds(300))
                                     .flatMapLatest([this](const std::string& q) { return search(q); })
                                     .stateIn(viewModelScope(), SharingStarted::WhileSubscribed(std::chrono::milliseconds(5000)), Results{});

private:
    Flow<Results> search(std::string q) {
        return flow<Results>([q, events = events](FlowCollector<Results> emit) -> Task {
            static const char* const all[] = {"Adana", "Ankara", "Antalya", "Bursa", "Çanakkale", "Denizli", "Diyarbakır",
                                              "Edirne", "Erzurum", "Eskişehir", "Gaziantep", "İstanbul", "İzmir", "Kayseri",
                                              "Konya", "Malatya", "Mardin", "Samsun", "Trabzon", "Van"};
            if (q.empty()) {
                co_await emit({});
                co_return;
            }
            co_await emit({.searching = true});
            co_await delay(std::chrono::milliseconds(600));
            auto lower = [](std::string s) {  // ASCII letters; enough for a demo
                for (char& c : s)
                    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
                return s;
            };
            Results found;
            for (const char* city : all)
                if (lower(city).find(lower(q)) != std::string::npos) found.cities.push_back(city);
            events.tryEmit(std::format("\"{}\": {} şehir", q, found.cities.size()));
            co_await emit(found);
        });
    }
};

void FlowSearchSection(CallSite site) {
    composable(site, [=] {
        auto vm = viewModel<CitySearchViewModel>();
        std::string query = vm->query.collectAsState();
        // Collected only while the screen is at least started: in the background (or on another
        // tab) it lets go, and five seconds later stateIn stops searching.
        CitySearchViewModel::Results results = collectAsStateWithLifecycle(vm->results);
        auto lastEvent = remember([] { return mutableStateOf(std::string()); });
        LaunchedEffect(vm, [vm, lastEvent]() -> Task {  // the events, while this is shown
            co_await vm->events.collect([lastEvent](const std::string& e) { lastEvent.set(e); });
        });
        Section("Flow · debounce · flatMapLatest · stateIn · SharedFlow", [&] {
            Text("Yazmayı bırakınca (300 ms) aranır; yeni bir sorgu süren aramayı iptal eder. Sonuçlar ViewModel'de "
                 "bir StateFlow (WhileSubscribed: yalnızca ekran dinlerken aranır); biten arama bir SharedFlow olayı yayınlar. "
                 "Sorgu SavedStateHandle'da: uygulama arka planda kapatılıp geri gelince yazılı kalır.",
                 {.font = typography().bodyMedium, .color = colorScheme().onSurfaceVariant});
            OutlinedTextField(query, [vm](std::string v) { vm->setQuery(std::move(v)); }, Modifier().fillMaxWidth(),
                              {.singleLine = true, .placeholder = "örn. an", .label = "Şehir ara"});
            if (results.searching) Text("aranıyor…", {.font = typography().bodyMedium, .color = colorScheme().primary});
            else if (!query.empty() && results.cities.empty()) Text("bulunamadı", {.font = typography().bodyMedium});
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8},
                    [&] {
                        for (const std::string& city : results.cities) SuggestionChip([] {}, [&] { Text(city); });
                    });
            if (!lastEvent.get().empty())
                Text("Son olay (SharedFlow): " + lastEvent.get(), {.font = typography().bodySmall, .color = colorScheme().onSurfaceVariant});
        });
    });
}

// movableContentOf: a card with a count and a running timer moves between a Row and a Column
// without starting over.
void MovableContentSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto sideBySide = remember([] { return mutableStateOf(true); });
        auto card = remember([] {
            return movableContentOf([] {
                const ColorScheme& cs = colorScheme();
                auto taps = remember([] { return mutableStateOf(0); });
                auto seconds = remember([] { return mutableStateOf(0); });
                LaunchedEffect(0, [seconds]() -> Task {  // keeps running wherever the card goes
                    while (true) {
                        co_await delay(std::chrono::seconds(1));
                        seconds.set(seconds.peek() + 1);
                    }
                });
                Column(Modifier().background(cs.primaryContainer, 12).clickable([taps] { taps.set(taps.peek() + 1); }).padding(12), [&] {
                    Text(std::format("Dokunuş: {}", taps.get()), {.font = typography().titleMedium, .color = cs.onPrimaryContainer});
                    Text(std::format("{} sn önce başladı", seconds.get()), {.font = typography().bodySmall, .color = cs.onPrimaryContainer});
                });
            });
        });
        auto other = [&] {
            Box(Modifier().background(cs.surfaceContainerHighest, 12).padding(12), [&] {
                Text("Öbür içerik", {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            });
        };
        Section("movableContentOf", [&] {
            Text("Kart Row ile Column arasında taşınır: dokunuş sayısı ve çalışan zamanlayıcısı baştan başlamaz.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            FilledTonalButton([sideBySide] { sideBySide.set(!sideBySide.peek()); },
                              [&] { Text(sideBySide.get() ? "Alt alta diz" : "Yan yana diz"); });
            if (sideBySide.get())
                Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] { card(); other(); });
            else
                Column(Modifier(), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] { other(); card(); });
        });
    });
}

// A Channel between a button and a slow kitchen: each order made once, in turn, at most three
// waiting (trySend says no when it is full); produce counts down from a coroutine.
void ChannelSection(CallSite site) {
    composable(site, [=] {
        using namespace std::chrono_literals;
        const ColorScheme& cs = colorScheme();
        auto orders = remember([] { return Channel<int>(3); });
        auto kitchen = remember([] { return mutableStateOf(std::string("Mutfak boş")); });
        auto waiting = remember([] { return mutableStateOf(0); });
        auto next = remember([] { return mutableStateOf(1); });
        auto refused = remember([] { return mutableStateOf(false); });
        auto countdown = remember([] { return mutableStateOf(std::string()); });
        auto scope = rememberCoroutineScope();
        LaunchedEffect(0, [orders, kitchen, waiting]() -> Task {  // one order at a time
            while (std::optional<int> order = co_await orders.receive()) {
                waiting.set(waiting.peek() - 1);
                kitchen.set(std::format("Sipariş {} hazırlanıyor…", *order));
                co_await delay(1500ms);
                kitchen.set(std::format("Sipariş {} hazır", *order));
            }
        });
        Section("Channel · trySend · receive · produce", [&] {
            Text("Mutfak siparişleri sırayla ve birer kez hazırlar; kanalda en çok 3 sipariş bekler, dolunca trySend reddeder.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Button([orders, next, waiting, refused] {
                    bool taken = orders.trySend(next.peek());
                    refused.set(!taken);
                    if (taken) {
                        next.set(next.peek() + 1);
                        waiting.set(waiting.peek() + 1);
                    }
                }, [] { Text("Sipariş ver"); });
                Text(std::format("Bekleyen: {}", waiting.get()));
            });
            Text(kitchen.get(), {.font = typography().titleMedium});
            if (refused.get()) Text("Kanal dolu: sipariş alınmadı", {.font = typography().bodySmall, .color = cs.error});
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                OutlinedButton([scope, countdown] {
                    scope.launch([scope, countdown]() -> Task {
                        Channel<int> numbers = produce<int>(scope, [](Channel<int> out) -> Task {
                            for (int i = 5; i >= 1; --i) {
                                co_await out.send(i);
                                co_await delay(600ms);
                            }
                        });
                        co_await numbers.consumeEach([countdown](int i) { countdown.set(std::to_string(i)); });
                        countdown.set("Başla!");
                    });
                }, [] { Text("Geri say (produce)"); });
                Text(countdown.get(), {.font = typography().titleLarge, .color = cs.primary});
            });
        });
    });
}

// The screen's lifecycle (its navigation entry's, capped by the app's): an observer that logs
// the events, a "player" that plays only while it is resumed (LifecycleResumeEffect), and a
// clock flow collected with collectAsStateWithLifecycle — in the background it stops.
class LifecycleViewModel : public ViewModel {
public:
    MutableStateFlow<std::vector<std::string>> events{{}};
    MutableStateFlow<int> resumes{0}, clockStarts{0};
    // A clock as a flow, kept going only while someone collects it (WhileSubscribed).
    StateFlow<int> seconds = flow<int>([starts = clockStarts](FlowCollector<int> emit) -> Task {
                                 starts.setValue(starts.value() + 1);
                                 for (int s = 0;; ++s) {
                                     co_await emit(s);
                                     co_await delay(std::chrono::seconds(1));
                                 }
                             }).stateIn(viewModelScope(), SharingStarted::WhileSubscribed(std::chrono::milliseconds(0)), 0);

    // Watches the entry's lifecycle for as long as the entry (and so this ViewModel) is there,
    // shown or not: what happens while it is on another tab is logged too.
    void watch(Lifecycle& lifecycle) {
        if (watching_) return;
        watching_ = lifecycle.addObserver([events = events, resumes = resumes, since = std::chrono::steady_clock::now()](LifecycleEvent e) {
            static const char* const names[] = {"ON_CREATE", "ON_START", "ON_RESUME", "ON_PAUSE", "ON_STOP", "ON_DESTROY"};
            double at = std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
            auto list = events.value();
            list.push_back(std::format("{:6.1f} sn  {}", at, names[int(e)]));
            if (list.size() > 6) list.erase(list.begin());
            events.setValue(std::move(list));
            if (e == LifecycleEvent::OnResume) resumes.setValue(resumes.value() + 1);
        });
    }

private:
    std::shared_ptr<void> watching_;
};

void LifecycleSection(CallSite site) {
    composable(site, [=] {
        auto owner = currentLifecycleOwner();
        auto vm = viewModel<LifecycleViewModel>();
        vm->watch(owner->lifecycle());
        LifecycleState state = owner->lifecycle().currentStateAsState();
        auto playing = remember([] { return mutableStateOf(false); });
        auto position = remember([] { return mutableStateOf(0.f); });
        auto inView = remember([] { return mutableStateOf(false); });  // (and only while it shows, as a player plays)
        LifecycleResumeEffect(0, [playing]() -> std::function<void()> {
            playing.set(true);
            return [playing] { playing.set(false); };  // paused, or left the composition
        });
        LaunchedEffect(playing.get() && inView.get(), [playing, inView, position]() -> Task {
            while (playing.peek() && inView.peek()) {
                co_await delay(std::chrono::milliseconds(50));
                position.set(std::fmod(position.peek() + 0.004f, 1.f));
            }
        });
        int seconds = collectAsStateWithLifecycle(vm->seconds);
        int starts = vm->clockStarts.collectAsState();
        int resumes = vm->resumes.collectAsState();
        std::vector<std::string> events = vm->events.collectAsState();
        Section("Lifecycle · LifecycleResumeEffect · collectAsStateWithLifecycle", [&] {
            static const char* const names[] = {"DESTROYED", "INITIALIZED", "CREATED", "STARTED", "RESUMED"};
            Text("Masaüstünde pencere odağı kaybedince STARTED, simge durumuna küçültülünce CREATED olur; Android'de uygulama "
                 "arka plana geçince. Başka sekmeye geçince bu ekranın girdisi CREATED'a iner.",
                 {.font = typography().bodyMedium, .color = colorScheme().onSurfaceVariant});
            Text(std::format("Bu ekran: {} · öne gelme: {}", names[int(state)], resumes),
                 Modifier().padding(0, 8, 0, 0), {.font = typography().titleSmall});
            Row(Modifier().fillMaxWidth().padding(0, 8).onVisibilityChanged([inView](bool visible) { inView.set(visible); }, {.minFractionVisible = 0}),
                {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    Icon(playing.get() ? Icons::Filled::PlayArrow : Icons::Filled::Pause, playing.get() ? "Oynatılıyor" : "Duraklatıldı");
                    LinearProgressIndicator(position.get(), Modifier().weight(1));
                    Text(playing.get() ? "oynatılıyor" : "duraklatıldı", {.font = typography().bodyMedium});
                });
            Text(std::format("Saat akışı: {} sn (akış {} kez başladı; arka plandayken toplanmaz, dönünce yeniden başlar)", seconds, starts),
                 {.font = typography().bodyMedium});
            Column(Modifier().fillMaxWidth().padding(0, 8, 0, 0).background(colorScheme().surfaceContainerHigh, RoundedCornerShape(12)).padding(12),
                   [&] {
                       for (const std::string& e : events) Text(e, {.font = typography().bodySmall, .fontFamily = FontFamily::Monospace});
                   });
        });
    });
}

// A chat: LazyColumn(reverseLayout) keeps the newest message (item 0) at the bottom; a new one
// comes in there, and the list glides back down to it if it was scrolled up.
void ChatSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        struct Message {
            int id;
            std::string text;
            bool mine;
            bool operator==(const Message&) const = default;
        };
        auto messages = remember([] {
            std::vector<Message> v;  // newest first
            const char* lines[] = {"Selam!", "Merhaba, nasılsın?", "İyiyim, Kompoz'u deniyorum.", "Nasıl gidiyor?",
                                   "Liste ters: en yenisi altta.", "reverseLayout = true ile mi?", "Evet, sohbetler gibi."};
            for (int i = 0; i < 7; ++i) v.insert(v.begin(), Message{i, lines[i], i % 2 == 0});
            return std::make_shared<MutableState<std::vector<Message>>>(mutableStateOf(std::move(v)));
        });
        auto draft = remember([] { return mutableStateOf(std::string()); });
        auto list = rememberLazyListState();
        auto scope = rememberCoroutineScope();
        auto send = [messages, draft, list, scope] {
            if (draft.peek().empty()) return;
            auto add = [messages](std::string text, bool mine) {
                std::vector<Message> v = messages->peek();
                int id = v.empty() ? 0 : v.front().id + 1;
                v.insert(v.begin(), Message{id, std::move(text), mine});
                messages->set(std::move(v));
            };
            add(draft.peek(), true);
            draft.set("");
            scope.launch([list, add]() -> Task {
                co_await list.animateScrollToItem(0);
                co_await delay(std::chrono::milliseconds(700));
                add("👍", false);  // an answer
                co_await list.animateScrollToItem(0);
            });
        };
        Section("Sohbet · reverseLayout · animateScrollToItem", [&] {
            Text("İlk öğe altta durur, yenisi oraya gelir; yukarı kaydırılmışsa liste kayarak en yeniye döner.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            std::vector<Message> shown = messages->get();
            LazyColumn(Modifier().fillMaxWidth().height(240).background(cs.surfaceContainerLow, 12),
                       {.state = list, .contentPadding = {12, 8, 12, 8}, .spacing = 6, .reverseLayout = true},
                       [&](LazyListScope& items) {
                           items.items(shown, [](const Message& m) { return m.id; }, [cs](const Message& m) {
                               Box(Modifier().fillMaxWidth().animateItem(), m.mine ? Alignment::CenterEnd : Alignment::CenterStart, [&] {
                                   Box(Modifier()
                                           .widthIn(0, 260)
                                           .background(m.mine ? cs.primary : cs.surfaceContainerHighest, 16)
                                           .padding(12, 8),
                                       [&] { Text(m.text, {.color = m.mine ? cs.onPrimary : cs.onSurface}); });
                               });
                           });
                       });
            Row(Modifier().fillMaxWidth(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                OutlinedTextField(draft.get(), [draft](std::string v) { draft.set(std::move(v)); }, Modifier().weight(1),
                                  {.singleLine = true, .placeholder = "Mesaj", .keyboardOptions = {.imeAction = ImeAction::Send},
                                   .keyboardActions = {.onSend = send}});
                IconButton(send, [] { Icon(Icons::AutoMirrored::Filled::Send, "Gönder"); });
            });
        });
    });
}

// Platform services: links (the browser, or this app's own kompoz:// address through the
// system), the software keyboard, haptic feedback and the clipboard.
// The primes below `limit` (a sieve), counted on a worker thread; `millis`: how long it took.
TaskOf<int> countPrimesInBackground(int limit, int64_t* millis) {
    auto start = std::chrono::steady_clock::now();
    int count = co_await withBackground([limit] {
        std::vector<bool> composite(size_t(limit) + 1);
        int n = 0;
        for (int i = 2; i <= limit; ++i) {
            if (composite[size_t(i)]) continue;
            ++n;
            for (int64_t j = int64_t(i) * i; j <= limit; j += i) composite[size_t(j)] = true;
        }
        return n;
    });
    *millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    co_return count;
}

void PlatformServicesSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        UriHandler links = uriHandler();
        SoftwareKeyboardController keyboard = softwareKeyboardController();
        HapticFeedback haptics = hapticFeedback();
        ClipboardManager clipboard = clipboardManager();
        Clipboard board = kompoz::clipboard();  // Compose 1.8's LocalClipboard
        auto scope = rememberCoroutineScope();
        auto screenOn = remember([] { return mutableStateOf(false); });
        auto sensitive = remember([] { return mutableStateOf(false); });
        auto card = remember([] { return mutableStateOf(std::string("4111 1111 1111 1111")); });
        auto email = remember([] { return mutableStateOf(std::string()); });
        auto password = rememberTextFieldState();
        auto signedIn = remember([] { return mutableStateOf(std::string()); });
        AutofillManager autofill = autofillManager();
        auto note = remember([] { return mutableStateOf(std::string("Kompoz")); });
        auto pasted = remember([] { return mutableStateOf(std::string()); });
        Section("Platform · bağlantı · klavye · titreşim · pano", [&] {
            Text("uriHandler(), softwareKeyboardController(), hapticFeedback(), clipboardManager(): Compose'daki "
                 "LocalUriHandler & co. kompoz:// adresini uygulamanın kendi NavHost'u tanıdığı için doğrudan içeride açar. "
                 "Titreşim Android'de sistemin dokunma geri bildirimi ayarına uyar; uzun basışlar ve seçim tutamaçları "
                 "kendileri titreşir.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8}, [&] {
                OutlinedButton([links] { links.openUri("https://developer.android.com/compose"); }, [] { Text("Compose belgeleri"); });
                OutlinedButton([links] { links.openUri("kompoz://item/7"); }, [] { Text("kompoz://item/7"); });
            });
            OutlinedTextField(note.get(), [note](std::string v) { note.set(std::move(v)); }, Modifier().fillMaxWidth(),
                              {.singleLine = true,
                               .label = "Not (Bitti klavyeyi gizler)",
                               .keyboardActions = {.onDone = [keyboard] { keyboard.hide(); }}});
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8}, [&] {
                TextButton([keyboard] { keyboard.hide(); }, [] { Text("Klavyeyi gizle"); });
                TextButton([keyboard] { keyboard.show(); }, [] { Text("Klavyeyi göster"); });
            });
            // keepScreenOn: the screen stays on while the switch is on (a Box with it is laid out).
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Switch(screenOn.get(), [screenOn](bool on) { screenOn.set(on); });
                Text("Ekranı açık tut (keepScreenOn)", Modifier().weight(1));
                if (screenOn.get()) Box(Modifier().size(1).keepScreenOn());
            });
            // sensitiveContent: while the switch is on, the card field hides the window from screen
            // shares and recordings (Android 15+: they show it blank).
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Switch(sensitive.get(), [sensitive](bool on) { sensitive.set(on); });
                Text("Hassas içerik (sensitiveContent)", Modifier().weight(1));
            });
            OutlinedTextField(card.get(), [card](std::string v) { card.set(std::move(v)); },
                              Modifier().fillMaxWidth().sensitiveContent(sensitive.get()),
                              {.singleLine = true, .label = sensitive.get() ? "Kart numarası (paylaşımda gizli)" : "Kart numarası"});
            // Autofill: a password manager fills the fields marked with a contentType, and offers to
            // keep them when the form is sent (commit).
            Text("Giriş formu (autofill): parola yöneticisi doldurur, Giriş'te kaydetmeyi önerir.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            OutlinedTextField(email.get(), [email](std::string v) { email.set(std::move(v)); },
                              Modifier().fillMaxWidth().semantics({.contentType = ContentType::Username + ContentType::EmailAddress}),
                              {.singleLine = true, .label = "E-posta", .keyboardOptions = {.keyboardType = KeyboardType::Email}});
            OutlinedSecureTextField(password, Modifier().fillMaxWidth().semantics({.contentType = ContentType::Password}), {.label = "Parola"});
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Button([autofill, email, password, signedIn] {
                    autofill.commit();
                    signedIn.set(email.peek().empty() ? "" : "Giriş: " + email.peek());
                }, [] { Text("Giriş yap"); });
                TextButton([autofill, email, password, signedIn] {
                    autofill.cancel();
                    email.set("");
                    password.setTextAndPlaceCursorAtEnd("");
                    signedIn.set("");
                }, [] { Text("Temizle"); });
                Text(signedIn.get(), Modifier().weight(1), {.font = typography().bodyMedium});
            });
            // LocalViewConfiguration: the platform's gesture thresholds (Android's own there).
            const ViewConfiguration& gestures = LocalViewConfiguration.current();
            Text(std::format("ViewConfiguration: uzun basma {} ms · çift dokunma {} ms · eşik {:.1f} dp · en hızlı fırlatma {:.0f} dp/s",
                             gestures.longPressTimeout.count(), gestures.doubleTapTimeout.count(), gestures.touchSlop, gestures.maximumFlingVelocity),
                 {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8}, [&] {
                using H = HapticFeedbackType;
                for (auto [type, label] : {std::pair{H::LongPress, "Uzun basma"}, std::pair{H::Confirm, "Onay"},
                                           std::pair{H::Reject, "Ret"}, std::pair{H::SegmentTick, "Tık"}, std::pair{H::ToggleOn, "Açma"}})
                    FilledTonalButton([haptics, type] { haptics.performHapticFeedback(type); }, [label] { Text(label); });
            });
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8}, [&] {
                Button([clipboard, note] { clipboard.setText(note.peek()); }, [] { Text("Notu kopyala"); });
                OutlinedButton([clipboard, pasted] { pasted.set(clipboard.hasText() ? clipboard.getText() : "(boş)"); },
                               [] { Text("Panoda ne var?"); });
                // Clipboard / ClipEntry, in a coroutine: the entry into the note.
                OutlinedButton([board, scope, note, pasted] {
                    scope.launch([board, note, pasted]() -> Task {
                        std::optional<ClipEntry> entry = co_await board.getClipEntry();
                        if (entry) note.set(entry->getPlainText());
                        pasted.set(entry ? "getClipEntry: " + entry->getPlainText() : "getClipEntry: boş");
                    });
                }, [] { Text("Notun yerine yapıştır"); });
            });
            if (!pasted.get().empty()) Text("Panoda: " + pasted.get(), {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
            // withBackground (Compose's withContext(Dispatchers.Default)) in a TaskOf: the primes
            // below thirty million counted on a worker thread; the spinner keeps turning meanwhile.
            auto primes = remember([] { return mutableStateOf(std::string("—")); });
            auto counting = remember([] { return mutableStateOf(false); });
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                FilledTonalButton([scope, primes, counting] {
                    if (counting.peek()) return;
                    counting.set(true);
                    scope.launch([primes, counting]() -> Task {
                        int64_t started = 0;
                        int n = co_await countPrimesInBackground(30'000'000, &started);
                        primes.set(std::format("{} asal ({} ms)", n, started));
                        counting.set(false);
                    });
                }, [] { Text("Arkada hesapla"); });
                if (counting.get()) CircularProgressIndicator(Modifier().size(24));
                Text(primes.get(), {.font = typography().bodyMedium});
            });
        });
    });
}
