// Components screen: icons, state flows, movable content, channels, the lifecycle, platform services, a chat.

#include "showcase.hpp"

@Kompozable
fun IconGallery() {
    val icons = listOf("Add" to Icons.Filled.Add, "ArrowBack" to Icons.Filled.ArrowBack, "Check" to Icons.Filled.Check, "Close" to Icons.Filled.Close,
                       "Delete" to Icons.Filled.Delete, "Edit" to Icons.Filled.Edit, "Favorite" to Icons.Filled.Favorite, "FavoriteBorder" to Icons.Filled.FavoriteBorder,
                       "Home" to Icons.Filled.Home, "Info" to Icons.Filled.Info, "Menu" to Icons.Filled.Menu, "MoreVert" to Icons.Filled.MoreVert,
                       "Notifications" to Icons.Filled.Notifications, "Person" to Icons.Filled.Person, "Search" to Icons.Filled.Search, "Settings" to Icons.Filled.Settings,
                       "Share" to Icons.Filled.Share, "Star" to Icons.Filled.Star, "Email" to Icons.Filled.Email, "Lock" to Icons.Filled.Lock,
                       "Place" to Icons.Filled.Place, "Refresh" to Icons.Filled.Refresh, "Visibility" to Icons.Filled.Visibility, "DarkMode" to Icons.Filled.DarkMode,
                       "PlayArrow" to Icons.Filled.PlayArrow, "Pause" to Icons.Filled.Pause, "List" to Icons.Filled.List, "CheckCircle" to Icons.Filled.CheckCircle,
                       "AccountCircle" to Icons.Filled.AccountCircle, "KeyboardArrowUp" to Icons.Filled.KeyboardArrowUp)
    Section("Icon") {
        for (row in 0 until icons.size step 5) {
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                for (i in row until min(icons.size, row + 5)) {
                    val (name, icon) = icons[i]
                    Column(modifier = Modifier.width(60.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                        Icon(icon, contentDescription = name)
                        Text(name, fontSize = 9.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                }
            }
        }
    }
}

@Kompozable
fun IconButtons() {
    var liked by remember { mutableStateOf(false) }
    Section("IconButton") {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = { liked = !liked }) {
                Icon(if (liked) Icons.Filled.Favorite else Icons.Filled.FavoriteBorder, contentDescription = "Beğen",
                     tint = if (liked) MaterialTheme.colorScheme.error else LocalContentColor.current)
            }
            FilledIconButton(onClick = {}) { Icon(Icons.Filled.Add, contentDescription = "Ekle") }
            FilledTonalIconButton(onClick = {}) { Icon(Icons.Filled.Edit, contentDescription = "Düzenle") }
            IconButton(onClick = {}, enabled = false) { Icon(Icons.Filled.Delete, contentDescription = "Sil") }
            OutlinedIconButton(onClick = {}) { Icon(Icons.Filled.Share, contentDescription = "Paylaş") }
            Icon(Icons.Filled.Star, contentDescription = "Büyük yıldız", modifier = Modifier.size(40.dp), tint = MaterialTheme.colorScheme.primary)
        }
        // The toggles: each keeps its own on/off; `checked` picks the colors.
        var starred by remember { mutableStateOf(false) }
        var bell by remember { mutableStateOf(true) }
        var pinned by remember { mutableStateOf(false) }
        var locked by remember { mutableStateOf(false) }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            IconToggleButton(checked = starred, onCheckedChange = { starred = it }) { Icon(Icons.Filled.Star, contentDescription = "Yıldızla") }
            FilledIconToggleButton(checked = bell, onCheckedChange = { bell = it }) { Icon(Icons.Filled.Notifications, contentDescription = "Bildirimler") }
            FilledTonalIconToggleButton(checked = pinned, onCheckedChange = { pinned = it }) { Icon(Icons.Filled.Place, contentDescription = "Sabitle") }
            OutlinedIconToggleButton(checked = locked, onCheckedChange = { locked = it }) { Icon(Icons.Filled.Lock, contentDescription = "Kilitle") }
            FilledIconToggleButton(checked = true, onCheckedChange = {}, enabled = false) { Icon(Icons.Filled.Check, contentDescription = "Seçili") }
        }
        // Under the toggles: beside them (48 dp of room each) it would be squeezed on a phone.
        Text((if (bell) "Bildirim açık" else "Bildirim kapalı") + (if (locked) ", kilitli" else ""), style = MaterialTheme.typography.bodySmall)
    }
}

// A ViewModel: the screen's state as StateFlows, set by work on a worker thread; the screen
// reads them with collectAsState(). It belongs to the Components entry of the NavHost, so a
// download goes on while another tab is shown, and stops when the entry is gone. (The web has
// no threads: there the same work runs in viewModelScope, on the UI thread.) Threads are the
// platform's: this class stays C++.
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

@Kompozable
fun StateFlowSection() {
    val vm = viewModel<DownloadViewModel>()
    val progress by vm.progress.collectAsState()
    val status by vm.status.collectAsState()
    Section("StateFlow · collectAsState · viewModel()") {
        Text("İş bir iş parçacığında yürür ve StateFlow'ları günceller; ekran onları collectAsState() ile okur. " +
                 "ViewModel bu ekranın gezinme girdisine ait: başka sekmeye geçip dönünce indirme sürmüş olur.",
             color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        LinearProgressIndicator(progress = { progress }, modifier = Modifier.fillMaxWidth().padding(0.dp, 12.dp))
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Button(onClick = { vm.start() }) { Text("İndir") }
            OutlinedButton(onClick = { vm.cancel() }) { Text("İptal") }
            Text(status, modifier = Modifier.weight(1f), textAlign = TextAlign.End, style = MaterialTheme.typography.bodyMedium)
        }
    }
}

// What a city search gives: searching, or the cities found.
data class CityResults(val searching: Boolean, val cities: List<String>)

// A search as Compose apps write it with flows: the query waits until typing stops
// (debounce), each query is searched (a pretend 600 ms lookup; a newer query cancels it:
// flatMapLatest), and the results are a StateFlow the screen reads (stateIn). The query lives in
// the SavedStateHandle: Android ending the app in the background and bringing it back finds it
// still typed (and searched).
class CitySearchViewModel(private val state: SavedStateHandle) : ViewModel() {
    val query: StateFlow<String> = state.getStateFlow("query", "")
    // One-off events for the screen (a SharedFlow: nothing is kept for when nobody listens).
    val events = MutableSharedFlow<String>()
    // Searched only while the screen collects the results, and five seconds after it stopped.
    val results: StateFlow<CityResults> = query.debounce(300).flatMapLatest { search(it) }
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), CityResults(false, emptyList()))

    fun setQuery(q: String) {
        state["query"] = q
    }

    private fun search(q: String): Flow<CityResults> = flow {
        val all = listOf("Adana", "Ankara", "Antalya", "Bursa", "Çanakkale", "Denizli", "Diyarbakır",
                         "Edirne", "Erzurum", "Eskişehir", "Gaziantep", "İstanbul", "İzmir", "Kayseri",
                         "Konya", "Malatya", "Mardin", "Samsun", "Trabzon", "Van")
        if (q.isEmpty()) {
            emit(CityResults(false, emptyList()))
        } else {
            emit(CityResults(true, emptyList()))
            delay(600)
            val found = all.filter { it.lowercase().contains(q.lowercase()) }  // ASCII letters; enough for a demo
            events.tryEmit("\"$q\": ${found.size} şehir")
            emit(CityResults(false, found))
        }
    }
}

@Kompozable
fun FlowSearchSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val vm = viewModel<CitySearchViewModel>()
    val query by vm.query.collectAsState()
    // Collected only while the screen is at least started: in the background (or on another
    // tab) it lets go, and five seconds later stateIn stops searching.
    val results by vm.results.collectAsStateWithLifecycle()
    var lastEvent by remember { mutableStateOf("") }
    LaunchedEffect(vm) {  // the events, while this is shown
        vm.events.collect { lastEvent = it }
    }
    Section("Flow · debounce · flatMapLatest · stateIn · SharedFlow") {
        Text("Yazmayı bırakınca (300 ms) aranır; yeni bir sorgu süren aramayı iptal eder. Sonuçlar ViewModel'de " +
                 "bir StateFlow (WhileSubscribed: yalnızca ekran dinlerken aranır); biten arama bir SharedFlow olayı yayınlar. " +
                 "Sorgu SavedStateHandle'da: uygulama arka planda kapatılıp geri gelince yazılı kalır.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        OutlinedTextField(value = query, onValueChange = { vm.setQuery(it) }, modifier = Modifier.fillMaxWidth(),
                          singleLine = true, placeholder = { Text("örn. an") }, label = { Text("Şehir ara") })
        if (results.searching) Text("aranıyor…", color = cs.primary, style = typography.bodyMedium)
        else if (query.isNotEmpty() && results.cities.isEmpty()) Text("bulunamadı", style = typography.bodyMedium)
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            for (city in results.cities) SuggestionChip(onClick = {}, label = { Text(city) })
        }
        if (lastEvent.isNotEmpty())
            Text("Son olay (SharedFlow): $lastEvent", color = cs.onSurfaceVariant, style = typography.bodySmall)
    }
}

// movableContentOf: a card with a count and a running timer moves between a Row and a Column
// without starting over.
@Kompozable
fun MovableContentSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var sideBySide by remember { mutableStateOf(true) }
    val card = remember {
        movableContentOf {
            var taps by remember { mutableStateOf(0) }
            var seconds by remember { mutableStateOf(0) }
            LaunchedEffect(Unit) {  // keeps running wherever the card goes
                while (true) {
                    delay(1000)
                    seconds++
                }
            }
            Column(modifier = Modifier.background(cs.primaryContainer, RoundedCornerShape(12.dp)).clickable { taps++ }.padding(12.dp)) {
                Text("Dokunuş: $taps", color = cs.onPrimaryContainer, style = typography.titleMedium)
                Text("$seconds sn önce başladı", color = cs.onPrimaryContainer, style = typography.bodySmall)
            }
        }
    }
    val other: @Kompozable () -> Unit = {
        Box(modifier = Modifier.background(cs.surfaceContainerHighest, RoundedCornerShape(12.dp)).padding(12.dp)) {
            Text("Öbür içerik", color = cs.onSurfaceVariant, style = typography.bodyMedium)
        }
    }
    Section("movableContentOf") {
        Text("Kart Row ile Column arasında taşınır: dokunuş sayısı ve çalışan zamanlayıcısı baştan başlamaz.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        FilledTonalButton(onClick = { sideBySide = !sideBySide }) { Text(if (sideBySide) "Alt alta diz" else "Yan yana diz") }
        if (sideBySide)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                card()
                other()
            }
        else
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                other()
                card()
            }
    }
}

// A Channel between a button and a slow kitchen: each order made once, in turn, at most three
// waiting (trySend says no when it is full); produce counts down from a coroutine.
@Kompozable
fun ChannelSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val orders = remember { Channel<Int>(3) }
    var kitchen by remember { mutableStateOf("Mutfak boş") }
    var waiting by remember { mutableStateOf(0) }
    var next by remember { mutableStateOf(1) }
    var refused by remember { mutableStateOf(false) }
    var countdown by remember { mutableStateOf("") }
    val scope = rememberCoroutineScope()
    LaunchedEffect(Unit) {  // one order at a time
        orders.consumeEach { order ->
            waiting--
            kitchen = "Sipariş $order hazırlanıyor…"
            delay(1500)
            kitchen = "Sipariş $order hazır"
        }
    }
    Section("Channel · trySend · receive · produce") {
        Text("Mutfak siparişleri sırayla ve birer kez hazırlar; kanalda en çok 3 sipariş bekler, dolunca trySend reddeder.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Button(onClick = {
                val taken = orders.trySend(next).isSuccess
                refused = !taken
                if (taken) {
                    next++
                    waiting++
                }
            }) { Text("Sipariş ver") }
            Text("Bekleyen: $waiting")
        }
        Text(kitchen, style = typography.titleMedium)
        if (refused) Text("Kanal dolu: sipariş alınmadı", color = cs.error, style = typography.bodySmall)
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedButton(onClick = {
                scope.launch {
                    val numbers = scope.produce<Int> {
                        for (i in 5 downTo 1) {
                            send(i)
                            delay(600)
                        }
                    }
                    numbers.consumeEach { countdown = it.toString() }
                    countdown = "Başla!"
                }
            }) { Text("Geri say (produce)") }
            Text(countdown, color = cs.primary, style = typography.titleLarge)
        }
    }
}

// The screen's lifecycle (its navigation entry's, capped by the app's): an observer that logs
// the events, a "player" that plays only while it is resumed (LifecycleResumeEffect), and a
// clock flow collected with collectAsStateWithLifecycle — in the background it stops. The
// observer's clock and handle are C++'s: this class stays C++.
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

@Kompozable
fun LifecycleSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val owner = LocalLifecycleOwner.current
    val vm = viewModel<LifecycleViewModel>()
    vm.watch(owner.lifecycle)
    val state by owner.lifecycle.currentStateAsState()
    var playing by remember { mutableStateOf(false) }
    var position by remember { mutableStateOf(0f) }
    var inView by remember { mutableStateOf(false) }  // (and only while it shows, as a player plays)
    LifecycleResumeEffect(Unit) {
        playing = true
        onPauseOrDispose { playing = false }  // paused, or left the composition
    }
    LaunchedEffect(playing && inView) {
        while (playing && inView) {
            delay(50)
            position = if (position + 0.004f >= 1f) 0f else position + 0.004f
        }
    }
    val seconds by vm.seconds.collectAsStateWithLifecycle()
    val starts by vm.clockStarts.collectAsState()
    val resumes by vm.resumes.collectAsState()
    val events by vm.events.collectAsState()
    Section("Lifecycle · LifecycleResumeEffect · collectAsStateWithLifecycle") {
        val name = when (state) {
            Lifecycle.State.DESTROYED -> "DESTROYED"
            Lifecycle.State.INITIALIZED -> "INITIALIZED"
            Lifecycle.State.CREATED -> "CREATED"
            Lifecycle.State.STARTED -> "STARTED"
            else -> "RESUMED"
        }
        Text("Masaüstünde pencere odağı kaybedince STARTED, simge durumuna küçültülünce CREATED olur; Android'de uygulama " +
                 "arka plana geçince. Başka sekmeye geçince bu ekranın girdisi CREATED'a iner.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Text("Bu ekran: $name · öne gelme: $resumes", modifier = Modifier.padding(0.dp, 8.dp, 0.dp, 0.dp), style = typography.titleSmall)
        Row(modifier = Modifier.fillMaxWidth().padding(0.dp, 8.dp).onVisibilityChanged(minFractionVisible = 0f) { inView = it },
            horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(if (playing) Icons.Filled.PlayArrow else Icons.Filled.Pause, contentDescription = if (playing) "Oynatılıyor" else "Duraklatıldı")
            LinearProgressIndicator(progress = { position }, modifier = Modifier.weight(1f))
            Text(if (playing) "oynatılıyor" else "duraklatıldı", style = typography.bodyMedium)
        }
        Text("Saat akışı: $seconds sn (akış $starts kez başladı; arka plandayken toplanmaz, dönünce yeniden başlar)", style = typography.bodyMedium)
        Column(modifier = Modifier.fillMaxWidth().padding(0.dp, 8.dp, 0.dp, 0.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).padding(12.dp)) {
            for (e in events) Text(e, fontFamily = FontFamily.Monospace, style = typography.bodySmall)
        }
    }
}

// A chat message; `mine`: sent from this side.
data class ChatMessage(val id: Int, val text: String, val mine: Boolean)

// The opening conversation, newest first.
fun openingMessages(): List<ChatMessage> {
    val lines = listOf("Selam!", "Merhaba, nasılsın?", "İyiyim, Kompoz'u deniyorum.", "Nasıl gidiyor?",
                       "Liste ters: en yenisi altta.", "reverseLayout = true ile mi?", "Evet, sohbetler gibi.")
    return lines.mapIndexed { i, line -> ChatMessage(i, line, i % 2 == 0) }.reversed()
}

// A chat: LazyColumn(reverseLayout) keeps the newest message (item 0) at the bottom; a new one
// comes in there, and the list glides back down to it if it was scrolled up.
@Kompozable
fun ChatSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var messages by remember { mutableStateOf(openingMessages()) }
    var draft by remember { mutableStateOf("") }
    val list = rememberLazyListState()
    val scope = rememberCoroutineScope()
    val add: (String, Boolean) -> Unit = { text, mine ->
        val id = if (messages.isEmpty()) 0 else messages.first().id + 1
        messages = listOf(ChatMessage(id, text, mine)) + messages
    }
    val send: () -> Unit = {
        if (draft.isNotEmpty()) {
            add(draft, true)
            draft = ""
            scope.launch {
                list.animateScrollToItem(0)
                delay(700)
                add("👍", false)  // an answer
                list.animateScrollToItem(0)
            }
        }
    }
    Section("Sohbet · reverseLayout · animateScrollToItem") {
        Text("İlk öğe altta durur, yenisi oraya gelir; yukarı kaydırılmışsa liste kayarak en yeniye döner.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        LazyColumn(modifier = Modifier.fillMaxWidth().height(240.dp).background(cs.surfaceContainerLow, RoundedCornerShape(12.dp)),
                   state = list, contentPadding = PaddingValues(12.dp, 8.dp), verticalArrangement = Arrangement.spacedBy(6.dp), reverseLayout = true) {
            items(messages, key = { it.id }) { m ->
                Box(modifier = Modifier.fillMaxWidth().animateItem(), contentAlignment = if (m.mine) Alignment.CenterEnd else Alignment.CenterStart) {
                    Box(modifier = Modifier
                            .widthIn(0.dp, 260.dp)
                            .background(if (m.mine) cs.primary else cs.surfaceContainerHighest, RoundedCornerShape(16.dp))
                            .padding(12.dp, 8.dp)) { Text(m.text, color = if (m.mine) cs.onPrimary else cs.onSurface) }
                }
            }
        }
        Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(value = draft, onValueChange = { draft = it }, modifier = Modifier.weight(1f),
                              singleLine = true, placeholder = { Text("Mesaj") }, keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
                              keyboardActions = KeyboardActions(onSend = { send() }))
            IconButton(onClick = send) { Icon(Icons.AutoMirrored.Filled.Send, contentDescription = "Gönder") }
        }
    }
}

// Platform services: links (the browser, or this app's own kompoz:// address through the
// system), the software keyboard, haptic feedback and the clipboard.
// The primes below `limit` (a sieve), counted on a worker thread, and how long it took.
struct PrimeCount {
    int count = 0;
    int64_t millis = 0;
};
TaskOf<PrimeCount> countPrimesInBackground(int limit) {
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
    co_return PrimeCount{count, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()};
}

@Kompozable
fun PlatformServicesSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val links = LocalUriHandler.current
    val keyboard = LocalSoftwareKeyboardController.current
    val haptics = LocalHapticFeedback.current
    val clipboard = LocalClipboardManager.current
    val board = LocalClipboard.current  // Compose 1.8's
    val scope = rememberCoroutineScope()
    var screenOn by remember { mutableStateOf(false) }
    var sensitive by remember { mutableStateOf(false) }
    var card by remember { mutableStateOf("4111 1111 1111 1111") }
    var email by remember { mutableStateOf("") }
    val password = rememberTextFieldState()
    var signedIn by remember { mutableStateOf("") }
    val autofill = LocalAutofillManager.current
    var note by remember { mutableStateOf("Kompoz") }
    var pasted by remember { mutableStateOf("") }
    Section("Platform · bağlantı · klavye · titreşim · pano") {
        Text("LocalUriHandler, LocalSoftwareKeyboardController, LocalHapticFeedback, LocalClipboardManager: Compose'daki " +
                 "gibi. kompoz:// adresini uygulamanın kendi NavHost'u tanıdığı için doğrudan içeride açar. " +
                 "Titreşim Android'de sistemin dokunma geri bildirimi ayarına uyar; uzun basışlar ve seçim tutamaçları " +
                 "kendileri titreşir.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { links.openUri("https://developer.android.com/compose") }) { Text("Compose belgeleri") }
            OutlinedButton(onClick = { links.openUri("kompoz://item/7") }) { Text("kompoz://item/7") }
        }
        OutlinedTextField(value = note, onValueChange = { note = it }, modifier = Modifier.fillMaxWidth(), singleLine = true,
                          label = { Text("Not (Bitti klavyeyi gizler)") },
                          keyboardActions = KeyboardActions(onDone = { keyboard?.hide() }))
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            TextButton(onClick = { keyboard?.hide() }) { Text("Klavyeyi gizle") }
            TextButton(onClick = { keyboard?.show() }) { Text("Klavyeyi göster") }
        }
        // keepScreenOn: the screen stays on while the switch is on (a Box with it is laid out).
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Switch(checked = screenOn, onCheckedChange = { screenOn = it })
            Text("Ekranı açık tut (keepScreenOn)", modifier = Modifier.weight(1f))
            if (screenOn) Box(modifier = Modifier.size(1.dp).keepScreenOn())
        }
        // sensitiveContent: while the switch is on, the card field hides the window from screen
        // shares and recordings (Android 15+: they show it blank).
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Switch(checked = sensitive, onCheckedChange = { sensitive = it })
            Text("Hassas içerik (sensitiveContent)", modifier = Modifier.weight(1f))
        }
        OutlinedTextField(value = card, onValueChange = { card = it }, modifier = Modifier.fillMaxWidth().sensitiveContent(sensitive),
                          singleLine = true, label = { Text(if (sensitive) "Kart numarası (paylaşımda gizli)" else "Kart numarası") })
        // Autofill: a password manager fills the fields marked with a contentType, and offers to
        // keep them when the form is sent (commit).
        Text("Giriş formu (autofill): parola yöneticisi doldurur, Giriş'te kaydetmeyi önerir.", color = cs.onSurfaceVariant, style = typography.bodyMedium)
        OutlinedTextField(value = email, onValueChange = { email = it },
                          modifier = Modifier.fillMaxWidth().semantics { contentType = ContentType.Username + ContentType.EmailAddress },
                          singleLine = true, label = { Text("E-posta") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Email))
        OutlinedSecureTextField(state = password, modifier = Modifier.fillMaxWidth().semantics { contentType = ContentType.Password }, label = { Text("Parola") })
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Button(onClick = {
                autofill?.commit()
                signedIn = if (email.isEmpty()) "" else "Giriş: $email"
            }) { Text("Giriş yap") }
            TextButton(onClick = {
                autofill?.cancel()
                email = ""
                password.setTextAndPlaceCursorAtEnd("")
                signedIn = ""
            }) { Text("Temizle") }
            Text(signedIn, modifier = Modifier.weight(1f), style = typography.bodyMedium)
        }
        // LocalViewConfiguration: the platform's gesture thresholds (Android's own there).
        val gestures = LocalViewConfiguration.current
        Text("ViewConfiguration: uzun basma ${gestures.longPressTimeoutMillis} ms · çift dokunma ${gestures.doubleTapTimeoutMillis} ms · " +
                 "eşik ${"%.1f".format(gestures.touchSlop)} dp · en hızlı fırlatma ${gestures.maximumFlingVelocity.roundToInt()} dp/s",
             color = cs.onSurfaceVariant, style = typography.bodySmall)
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            for ((type, label) in listOf(HapticFeedbackType.LongPress to "Uzun basma", HapticFeedbackType.Confirm to "Onay",
                                         HapticFeedbackType.Reject to "Ret", HapticFeedbackType.SegmentTick to "Tık", HapticFeedbackType.ToggleOn to "Açma"))
                FilledTonalButton(onClick = { haptics.performHapticFeedback(type) }) { Text(label) }
        }
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = { clipboard.setText(note) }) { Text("Notu kopyala") }
            OutlinedButton(onClick = { pasted = if (clipboard.hasText()) clipboard.getText() else "(boş)" }) { Text("Panoda ne var?") }
            // Clipboard / ClipEntry, in a coroutine: the entry into the note.
            OutlinedButton(onClick = {
                scope.launch {
                    val entry = board.getClipEntry()
                    if (entry != null) note = entry.getPlainText()
                    pasted = if (entry != null) "getClipEntry: " + entry.getPlainText() else "getClipEntry: boş"
                }
            }) { Text("Notun yerine yapıştır") }
        }
        if (pasted.isNotEmpty()) Text("Panoda: $pasted", color = cs.onSurfaceVariant, style = typography.bodySmall)
        // withBackground (Compose's withContext(Dispatchers.Default)) in a TaskOf: the primes
        // below thirty million counted on a worker thread; the spinner keeps turning meanwhile.
        var primes by remember { mutableStateOf("—") }
        var counting by remember { mutableStateOf(false) }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            FilledTonalButton(onClick = {
                if (!counting) {
                    counting = true
                    scope.launch {
                        val result = countPrimesInBackground(30000000)
                        primes = "${result.count} asal (${result.millis} ms)"
                        counting = false
                    }
                }
            }) { Text("Arkada hesapla") }
            if (counting) CircularProgressIndicator(modifier = Modifier.size(24.dp))
            Text(primes, style = typography.bodyMedium)
        }
    }
}
