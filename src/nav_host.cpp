#include <kompoz/host.hpp>
#include <kompoz/nav_host.hpp>
#include <kompoz/viewmodel.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace kompoz {

namespace {

std::string decode(std::string_view s) {  // %XX escapes
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int v = 0;
            if (std::from_chars(s.data() + i + 1, s.data() + i + 3, v, 16).ec == std::errc{}) {
                out += char(v);
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

std::vector<std::string_view> split(std::string_view s, char by) {
    std::vector<std::string_view> parts;
    for (size_t start = 0;;) {
        size_t end = s.find(by, start);
        parts.push_back(s.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) return parts;
        start = end + 1;
    }
}

bool isParam(std::string_view part) { return part.size() >= 2 && part.front() == '{' && part.back() == '}'; }

// The arguments `path` gives `pattern`, or nothing if it doesn't match: the same segments, a
// {name} segment taking any one, ?name={name} parameters optional.
std::optional<NavArguments> match(std::string_view pattern, std::string_view path) {
    auto [patternPath, patternQuery] = [&] {
        size_t q = pattern.find('?');
        return std::pair{pattern.substr(0, q), q == std::string_view::npos ? std::string_view{} : pattern.substr(q + 1)};
    }();
    size_t q = path.find('?');
    std::string_view pathPart = path.substr(0, q), query = q == std::string_view::npos ? std::string_view{} : path.substr(q + 1);
    auto want = split(patternPath, '/'), got = split(pathPart, '/');
    if (want.size() != got.size()) return std::nullopt;
    NavArguments args;
    for (size_t i = 0; i < want.size(); ++i) {
        if (isParam(want[i])) {
            if (got[i].empty()) return std::nullopt;
            args.values()[std::string(want[i].substr(1, want[i].size() - 2))] = decode(got[i]);
        } else if (want[i] != got[i]) {
            return std::nullopt;
        }
    }
    if (!patternQuery.empty() && !query.empty()) {
        std::map<std::string, std::string, std::less<>> given;  // name -> value in the path
        for (std::string_view kv : split(query, '&')) {
            size_t eq = kv.find('=');
            if (eq != std::string_view::npos) given[decode(kv.substr(0, eq))] = decode(kv.substr(eq + 1));
        }
        for (std::string_view kv : split(patternQuery, '&')) {
            size_t eq = kv.find('=');
            if (eq == std::string_view::npos) continue;
            std::string_view name = kv.substr(0, eq), value = kv.substr(eq + 1);
            if (auto it = given.find(name); it != given.end() && isParam(value))
                args.values()[std::string(value.substr(1, value.size() - 2))] = it->second;
        }
    }
    return args;
}

std::string encode(std::string_view s) {  // what a path segment or parameter can't hold as it is
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            out += char(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// The path of `pattern` with these arguments ("item/{n}" and n = 42: "item/42"); query
// parameters only for the arguments there are.
std::string fill(std::string_view pattern, const NavArguments& args) {
    size_t q = pattern.find('?');
    std::string out;
    std::string_view path = pattern.substr(0, q);
    for (size_t i = 0; i < path.size();) {
        size_t open = path.find('{', i), close = open == std::string_view::npos ? open : path.find('}', open);
        if (close == std::string_view::npos) {
            out += path.substr(i);
            break;
        }
        out += path.substr(i, open - i);
        std::string_view name = path.substr(open + 1, close - open - 1);
        if (auto value = args.getString(name)) out += encode(*value);
        i = close + 1;
    }
    if (q == std::string_view::npos) return out;
    bool first = true;
    for (std::string_view kv : split(pattern.substr(q + 1), '&')) {
        size_t eq = kv.find('=');
        if (eq == std::string_view::npos) continue;
        std::string_view value = kv.substr(eq + 1);
        if (!isParam(value)) continue;
        auto given = args.getString(value.substr(1, value.size() - 2));
        if (!given) continue;
        out += first ? '?' : '&';
        out += kv.substr(0, eq);
        out += '=';
        out += encode(*given);
        first = false;
    }
    return out;
}

bool sameScheme(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

template <class T>
std::optional<T> parse(const std::optional<std::string>& s) {
    if (!s) return std::nullopt;
    T value{};
    auto [end, ec] = std::from_chars(s->data(), s->data() + s->size(), value);
    if (ec != std::errc{} || end != s->data() + s->size()) return std::nullopt;
    return value;
}

} // namespace

std::string detail::encodeRouteValue(std::string_view s) { return encode(s); }

std::optional<std::string> NavArguments::getString(std::string_view name) const {
    auto it = values_.find(name);
    return it == values_.end() ? std::nullopt : std::optional(it->second);
}
std::optional<int> NavArguments::getInt(std::string_view name) const { return parse<int>(getString(name)); }
std::optional<long long> NavArguments::getLong(std::string_view name) const { return parse<long long>(getString(name)); }
std::optional<float> NavArguments::getFloat(std::string_view name) const {
    auto s = getString(name);
    if (!s || s->empty()) return std::nullopt;
    char* end = nullptr;
    float v = std::strtof(s->c_str(), &end);
    return end == s->c_str() + s->size() ? std::optional(v) : std::nullopt;
}
std::optional<bool> NavArguments::getBool(std::string_view name) const {
    auto s = getString(name);
    if (s == "true") return true;
    if (s == "false") return false;
    return std::nullopt;
}

struct NavController::Data {
    using Entry = std::shared_ptr<const NavBackStackEntry>;
    using Entries = std::vector<Entry>;
    struct Node {  // one of the NavHost's destinations or graphs
        std::string route, start, parent;
        bool graph = false;
        std::vector<std::string> deepLinks;  // URI patterns
    };
    Entries entries;
    std::string start;     // the NavHost's start destination
    std::string reported;  // the path of the entry last reported shown (ComposeHost::reportLocation)
    MutableState<uint64_t> version = mutableStateOf(uint64_t(0));  // bumped on every change: readers recompose
    std::vector<Node> nodes;
    std::map<std::string, Entries, std::less<>> saved;  // saveState, by the route of the first
    std::vector<uint64_t> dropped;  // popped for good: NavHost forgets their saved state
    std::map<uint64_t, Entry> retiring;  // popped: destroyed (their ViewModels cleared) once they are gone from the screen
    std::vector<std::pair<Entry, LifecycleState>> lifecycles;  // the state the NavHost last wanted each entry in
    std::shared_ptr<LifecycleOwner> parentLifecycle;  // the NavHost's own: the entries' at most
    uint64_t nextId = 1;
    bool lastWasPop = false;
    std::unordered_map<uint64_t, float> zIndices;  // each shown entry's, as Compose's NavHost keeps them

    void changed() { version.set(version.peek() + 1); }
    void applyLifecycles() const {
        LifecycleState most = parentLifecycle ? parentLifecycle->lifecycle().currentState() : LifecycleState::Resumed;
        for (const auto& [e, state] : lifecycles) e->lifecycle().setCurrentState(std::min(state, most));
    }
    bool go(std::string path, const NavOptions& options);  // navigate() without telling the readers
    // Where a link leads (a URI one of the destinations answers, or a route's path): the
    // destination and the path to navigate to.
    std::optional<std::pair<const Node*, std::string>> resolve(std::string_view link) const {
        if (size_t colon = link.find("://"); colon != std::string_view::npos) {
            std::string_view scheme = link.substr(0, colon), rest = link.substr(colon + 3);
            rest = rest.substr(0, rest.find('#'));
            for (const Node& n : nodes)
                for (const std::string& pattern : n.deepLinks) {
                    size_t c = pattern.find("://");
                    if (c == std::string::npos || !sameScheme(std::string_view(pattern).substr(0, c), scheme)) continue;
                    if (auto args = match(std::string_view(pattern).substr(c + 3), rest)) return std::pair{&n, fill(n.route, *args)};
                }
            return std::nullopt;
        }
        for (const Node& n : nodes)
            if (match(n.route, link)) return std::pair{&n, std::string(link)};
        return std::nullopt;
    }
    bool deepLink(std::string_view link);  // handleDeepLink() without telling the readers
    const Node* node(std::string_view route) const {
        for (const Node& n : nodes)
            if (n.route == route) return &n;
        return nullptr;
    }
    size_t screens() const { return size_t(std::count_if(entries.begin(), entries.end(), [](const Entry& e) { return !e->isGraph(); })); }
    void drop(const Entries& gone) {
        for (const auto& e : gone) {
            dropped.push_back(e->id());
            retiring[e->id()] = e;
        }
    }
    // Takes graphs' entries left on top without a screen above them (but those of `around`),
    // putting them in front of `popped`, which stays in back stack order.
    void popEmptyGraphs(Entries& popped, const std::vector<std::string_view>& around = {}) {
        while (!entries.empty() && entries.back()->isGraph() &&
               std::find(around.begin(), around.end(), entries.back()->route()) == around.end()) {
            popped.insert(popped.begin(), entries.back());
            entries.pop_back();
        }
    }
    // Removes the entries above the topmost one with this route or path (and it, if inclusive).
    std::optional<Entries> popTo(std::string_view route, bool inclusive) {
        for (size_t i = entries.size(); i-- > 0;)
            if (entries[i]->route() == route || entries[i]->path() == route) {
                size_t from = inclusive ? i : i + 1;
                Entries popped(entries.begin() + std::ptrdiff_t(from), entries.end());
                entries.resize(from);
                return popped;
            }
        return std::nullopt;
    }
    void keep(Entries popped, bool saveState) {
        if (popped.empty()) return;
        if (!saveState) return drop(popped);
        std::string root = popped.front()->route();
        if (auto it = saved.find(root); it != saved.end()) drop(it->second);  // saved earlier: replaced
        saved[root] = std::move(popped);
    }
    Entry make(const std::string& route, std::string path, NavArguments arguments, bool graph, Entry parent) {
        auto entry = std::make_shared<NavBackStackEntry>();
        entry->id_ = nextId++;
        entry->route_ = route;
        entry->path_ = std::move(path);
        entry->arguments_ = std::move(arguments);
        entry->graph_ = graph;
        entry->parent_ = std::move(parent);
        return entry;
    }
};

NavController::NavController() : data_(std::make_shared<Data>()) {}

SavedValue NavController::save() const {
    const Data& d = *data_;
    auto entry = [](const NavBackStackEntry& e) {
        SavedValue::Map args;
        for (const auto& [name, value] : e.arguments_.values()) args[name] = value;
        SavedValue::Map m{{"id", int64_t(e.id_)},     {"route", e.route_}, {"path", e.path_},
                          {"args", std::move(args)}, {"graph", e.graph_}, {"parent", e.parent_ ? int64_t(e.parent_->id_) : int64_t(0)}};
        if (SavedValue viewModels = e.store_->saveState(); viewModels.map() && !viewModels.map()->empty())
            m["viewModels"] = std::move(viewModels);  // its ViewModels' SavedStateHandles
        return m;
    };
    SavedValue::List entries;
    for (const auto& e : d.entries) entries.push_back(entry(*e));
    SavedValue::Map saved;
    for (const auto& [route, stack] : d.saved) {
        SavedValue::List list;
        for (const auto& e : stack) list.push_back(entry(*e));
        saved[route] = std::move(list);
    }
    return SavedValue::Map{{"entries", std::move(entries)}, {"saved", std::move(saved)}, {"nextId", int64_t(d.nextId)}};
}

NavController NavController::restore(const SavedValue& saved) {
    NavController nav;
    Data& d = *nav.data_;
    std::unordered_map<uint64_t, std::shared_ptr<NavBackStackEntry>> byId;
    std::vector<std::pair<std::shared_ptr<NavBackStackEntry>, uint64_t>> parents;
    auto entry = [&](const SavedValue& v) -> std::shared_ptr<NavBackStackEntry> {
        auto id = v["id"].integer();
        if (!id || *id <= 0) return nullptr;
        if (auto it = byId.find(uint64_t(*id)); it != byId.end()) return it->second;
        auto e = std::make_shared<NavBackStackEntry>();
        e->id_ = uint64_t(*id);
        if (const std::string* route = v["route"].string()) e->route_ = *route;
        if (const std::string* path = v["path"].string()) e->path_ = *path;
        if (const SavedValue::Map* args = v["args"].map())
            for (const auto& [name, value] : *args)
                if (const std::string* s = value.string()) e->arguments_.values()[name] = *s;
        e->graph_ = v["graph"].boolean().value_or(false);
        e->store_->restoreState(v["viewModels"]);
        byId[e->id_] = e;
        parents.push_back({e, uint64_t(v["parent"].integer().value_or(0))});
        d.nextId = std::max(d.nextId, e->id_ + 1);
        return e;
    };
    if (const SavedValue::List* entries = saved["entries"].list())
        for (const SavedValue& v : *entries)
            if (auto e = entry(v)) d.entries.push_back(std::move(e));
    if (const SavedValue::Map* stacks = saved["saved"].map())
        for (const auto& [route, list] : *stacks)
            if (list.list())
                for (const SavedValue& v : *list.list())
                    if (auto e = entry(v)) d.saved[route].push_back(std::move(e));
    for (auto& [e, parent] : parents)
        if (auto it = byId.find(parent); parent && it != byId.end()) e->parent_ = it->second;
    d.nextId = std::max<uint64_t>(d.nextId, uint64_t(saved["nextId"].integer().value_or(1)));
    return nav;
}

NavController rememberNavController(CallSite site) {
    return rememberSaveable([] { return NavController(); }, site);
}

void NavController::navigate(std::string path, const NavOptions& options) const {
    if (data_->go(std::move(path), options)) data_->changed();
}

bool NavController::Data::go(std::string path, const NavOptions& options) {
    std::optional<NavArguments> args;
    const Node* target = nullptr;
    for (const Node& n : nodes)
        if ((args = match(n.route, path))) {
            target = &n;
            break;
        }
    if (!target) {
        if (path.find("://") != std::string::npos)
            if (auto link = resolve(path)) return go(std::move(link->second), options);  // a deep link from inside the app
        std::fprintf(stderr, "kompoz: navigate(\"%s\"): no such destination\n", path.c_str());
        return false;
    }
    // A graph's route leads to its start destination, through new entries of the graphs on the way.
    std::vector<const Node*> entered;
    const Node* dest = target;
    while (dest && dest->graph) {
        entered.push_back(dest);
        dest = node(dest->start);
    }
    if (!dest) {
        std::fprintf(stderr, "kompoz: navigate(\"%s\"): no start destination \"%s\" in the graph\n", path.c_str(),
                     entered.back()->start.c_str());
        return false;
    }
    if (options.popUpTo)
        if (auto popped = popTo(*options.popUpTo, options.inclusive)) keep(std::move(*popped), options.saveState);
    Entries orphans;  // graphs' entries left without screens, that the new top isn't in
    if (options.launchSingleTop && !entries.empty() && entries.back()->route() == dest->route) {
        // already there
    } else if (auto it = saved.find(target->route); options.restoreState && it != saved.end()) {
        std::vector<std::string_view> around;
        for (const NavBackStackEntry* e = it->second.front()->parent().get(); e; e = e->parent().get()) around.push_back(e->route());
        popEmptyGraphs(orphans, around);
        entries.insert(entries.end(), it->second.begin(), it->second.end());  // with their saved state
        saved.erase(it);
    } else {
        std::vector<const Node*> graphs;  // those the destination is in, outermost first
        std::vector<std::string_view> around;
        for (const Node* g = node(dest->parent); g; g = node(g->parent)) {
            graphs.insert(graphs.begin(), g);
            around.push_back(g->route);
        }
        popEmptyGraphs(orphans, around);
        // The graphs' entries the top of the stack is in are its too, outermost first, but for
        // a graph navigated to by its route: that one starts anew.
        Entries open;
        if (!entries.empty())
            for (Entry e = entries.back()->isGraph() ? entries.back() : entries.back()->parent(); e; e = e->parent())
                open.insert(open.begin(), e);
        Entry parent;
        bool fresh = false;
        for (size_t i = 0; i < graphs.size(); ++i) {
            const Node* g = graphs[i];
            fresh = fresh || i >= open.size() || open[i]->route() != g->route ||
                    std::find(entered.begin(), entered.end(), g) != entered.end();
            if (!fresh) {
                parent = open[i];
                continue;
            }
            entries.push_back(make(g->route, g == target ? path : g->route, *args, true, parent));
            parent = entries.back();
        }
        entries.push_back(make(dest->route, dest == target ? std::move(path) : dest->route, std::move(*args), false, parent));
    }
    drop(orphans);
    lastWasPop = false;
    return true;
}

// As an app opened with the link: the start destination, then each graph the destination is in
// (its start destination), then the destination, so that back leads up from it.
bool NavController::Data::deepLink(std::string_view link) {
    auto target = resolve(link);
    if (!target) return false;
    auto [node, path] = *target;
    drop(std::exchange(entries, {}));
    go(start, {});
    std::vector<const Node*> graphs;  // outermost first, the destination itself if it is one
    for (const Node* g = node->graph ? node : this->node(node->parent); g; g = this->node(g->parent)) graphs.insert(graphs.begin(), g);
    auto in = [&](const std::string& route) {  // the top of the stack is in that graph
        for (const NavBackStackEntry* e = entries.empty() ? nullptr : entries.back().get(); e; e = e->parent().get())
            if (e->route() == route) return true;
        return false;
    };
    NavArguments args = match(node->route, path).value_or(NavArguments{});  // the graphs' {name} parts too
    for (const Node* g : graphs)
        if (!in(g->route)) go(g == node ? path : fill(g->route, args), {});
    if (!node->graph && (entries.empty() || entries.back()->path() != path)) go(path, {});
    lastWasPop = false;
    return true;
}

bool NavController::handleDeepLink(std::string_view link) const {
    if (!data_->deepLink(link)) return false;
    data_->changed();
    return true;
}

bool NavController::popBackStack() const {
    Data& d = *data_;
    if (d.screens() <= 1) return false;
    Data::Entries popped{d.entries.back()};
    d.entries.pop_back();
    d.popEmptyGraphs(popped);
    d.drop(popped);
    d.lastWasPop = true;
    d.changed();
    return true;
}

bool NavController::popBackStack(std::string_view route, bool inclusive, bool saveState) const {
    Data& d = *data_;
    auto popped = d.popTo(route, inclusive);
    if (!popped) return false;
    d.popEmptyGraphs(*popped);
    d.keep(std::move(*popped), saveState);
    d.lastWasPop = true;
    d.changed();
    return true;
}

std::shared_ptr<const NavBackStackEntry> NavController::currentBackStackEntryAsState() const {
    data_->version.get();
    return currentBackStackEntry();
}
std::shared_ptr<const NavBackStackEntry> NavController::currentBackStackEntry() const {
    return data_->entries.empty() ? nullptr : data_->entries.back();
}
std::shared_ptr<const NavBackStackEntry> NavController::previousBackStackEntry() const {
    const auto& entries = data_->entries;
    for (size_t i = entries.size() - std::min<size_t>(entries.size(), 1); i-- > 0;)
        if (!entries[i]->isGraph()) return entries[i];
    return nullptr;
}
std::shared_ptr<const NavBackStackEntry> NavController::getBackStackEntry(std::string_view route) const {
    const auto& entries = data_->entries;
    for (size_t i = entries.size(); i-- > 0;)
        if (entries[i]->route() == route || entries[i]->path() == route) return entries[i];
    return nullptr;
}
std::vector<std::shared_ptr<const NavBackStackEntry>> NavController::backStack() const {
    data_->version.get();
    return data_->entries;
}
bool NavController::canNavigateBack() const {
    data_->version.get();
    return data_->screens() > 1;
}

std::vector<std::string> NavBackStackEntry::hierarchy() const {
    std::vector<std::string> routes;
    for (const NavBackStackEntry* e = this; e; e = e->parent_.get()) routes.push_back(e->route_);
    return routes;
}

void NavGraphBuilder::composable(std::string route, std::function<void(const NavBackStackEntry&)> content) {
    destinations_.push_back({std::move(route), std::move(content), {}, parent_, {}});
}

void NavGraphBuilder::composable(std::string route, std::vector<NavDeepLink> deepLinks,
                                 std::function<void(const NavBackStackEntry&)> content) {
    destinations_.push_back({std::move(route), std::move(content), {}, parent_, std::move(deepLinks)});
}

void NavGraphBuilder::navigation(std::string startDestination, std::string route,
                                 const std::function<void(NavGraphBuilder&)>& builder) {
    NavGraphBuilder nested;
    nested.parent_ = route;
    builder(nested);
    destinations_.push_back({std::move(route), {}, std::move(startDestination), parent_, {}});
    for (auto& dest : nested.destinations_) destinations_.push_back(std::move(dest));
}

void NavHost(const NavController& navController, std::string startDestination, const Modifier& modifier,
             const std::function<void(NavGraphBuilder&)>& builder, const NavHostOptions& options, CallSite site) {
    ComposableScope scope(site);
    NavGraphBuilder graph;
    builder(graph);
    NavController::Data& d = *navController.data();
    d.nodes.clear();
    for (const auto& dest : graph.destinations()) {
        std::vector<std::string> links;
        for (const NavDeepLink& link : dest.deepLinks) links.push_back(link.uriPattern);
        d.nodes.push_back({dest.route, dest.startDestination, dest.parent, dest.isGraph(), std::move(links)});
    }
    d.start = startDestination;
    // A link the app was opened with (or one sent to it since) that a destination here answers;
    // else, the first time, the start destination.
    ComposeHost& host = detail::Composer::require().host();
    // While it is here, uriHandler() opens the links it answers in it.
    DisposableEffect(navController.data(), [&host, weak = std::weak_ptr<NavController::Data>(navController.data())]() -> std::function<void()> {
        const void* owner = weak.lock().get();
        host.addDeepLinkHandler(owner, [weak](const std::string& link) {
            auto data = weak.lock();
            return data && data->resolve(link).has_value();
        });
        return [&host, owner] { host.removeDeepLinkHandler(owner); };
    });
    if (auto link = host.takeDeepLink([&](const std::string& l) { return d.resolve(l).has_value(); })) d.deepLink(*link);
    else if (d.entries.empty() && d.nextId == 1) d.go(startDestination, {});
    SaveableStateHolder holder = rememberSaveableStateHolder();
    for (uint64_t id : std::exchange(d.dropped, {})) holder.removeState(std::to_string(id));
    d.version.get();  // recompose when the stack changes
    std::shared_ptr<const NavBackStackEntry> current;  // the screen on top (graphs' entries are below theirs)
    for (auto it = d.entries.rbegin(); it != d.entries.rend() && !current; ++it)
        if (!(*it)->isGraph()) current = *it;
    if (current && current->path() != d.reported) host.reportLocation(d.reported = current->path());
    // Back pops; its gesture (Android 14+) shows the screen below coming in as the pop
    // transition has it, as far as the gesture has gone (Compose's NavHost seeks its transition);
    // done, it pops, and the transition carries on from there.
    auto gesture = remember([] { return mutableStateOf(-1.f); });  // the gesture's progress; -1: none
    detail::backHandler({
        .onBack = [navController, gesture] {
            gesture.set(-1);
            navController.popBackStack();
        },
        .onStarted = [data = navController.data(), gesture](const BackEventCompat& e) {
            data->lastWasPop = true;  // the transition seeked is a pop's
            gesture.set(e.progress);
        },
        .onProgressed = [gesture](const BackEventCompat& e) { gesture.set(e.progress); },
        .onCancelled = [gesture] { gesture.set(-1); },
        .enabled = d.screens() > 1,
    });
    float progress = gesture.get();
    std::optional<std::shared_ptr<const NavBackStackEntry>> below;
    if (progress >= 0)
        if (auto entry = navController.previousBackStackEntry()) below = entry;
    std::vector<uint64_t> shown;  // entries composed this time, and their graphs' (one leaving keeps its ViewModels until it is gone)
    AnimatedContent<std::shared_ptr<const NavBackStackEntry>>(
        current,
        [&](const AnimatedContentScope&, const std::shared_ptr<const NavBackStackEntry>& entry) {
            if (!entry) return;
            for (const auto& dest : graph.destinations())
                if (!dest.isGraph() && dest.route == entry->route()) {
                    for (const NavBackStackEntry* e = entry.get(); e; e = e->parent().get()) shown.push_back(e->id());
                    holder.SaveableStateProvider(std::to_string(entry->id()), [&] {
                        std::shared_ptr<LifecycleOwner> owner = std::const_pointer_cast<NavBackStackEntry>(entry);
                        CompositionLocalProvider({detail::LocalViewModelStore.provides(entry->viewModelStore()), LocalLifecycleOwner.provides(owner)},
                                                 [&] { dest.content(*entry); });
                    });
                    return;
                }
        },
        modifier,
        // Going back the entry comes in under the one leaving, forward over it (Compose's NavHost:
        // one z-index less or more than the entry before).
        [&d, &options](const std::shared_ptr<const NavBackStackEntry>& initial, const std::shared_ptr<const NavBackStackEntry>& target) {
            uint64_t from = initial ? initial->id() : 0, to = target ? target->id() : 0;
            float z = d.zIndices[from];
            if (to != from) z += d.lastWasPop ? -1.f : 1.f;
            d.zIndices[to] = z;
            return (d.lastWasPop ? options.popTransition : options.transition).withTargetContentZIndex(z);
        },
        {.seekState = below, .seekFraction = progress});
    // Lifecycles: the screen shown resumed — started while a transition runs —, the ones still
    // leaving started, the rest of the back stack (and the saved stacks) created; applied after
    // the composition, and again whenever the NavHost's own lifecycle moves.
    auto has = [](const std::vector<uint64_t>& ids, uint64_t id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
    std::vector<uint64_t> top;
    for (const NavBackStackEntry* e = current.get(); e; e = e->parent().get()) top.push_back(e->id());
    bool moving = std::any_of(shown.begin(), shown.end(), [&](uint64_t id) { return !has(top, id); });
    d.lifecycles.clear();
    for (const auto& e : d.entries)
        d.lifecycles.push_back({e, has(top, e->id()) ? (moving ? LifecycleState::Started : LifecycleState::Resumed)
                                   : has(shown, e->id()) ? LifecycleState::Started : LifecycleState::Created});
    for (const auto& [route, stack] : d.saved)
        for (const auto& e : stack) d.lifecycles.push_back({e, LifecycleState::Created});
    std::vector<NavController::Data::Entry> gone;  // popped and off the screen: destroyed, their ViewModels cleared
    for (auto it = d.retiring.begin(); it != d.retiring.end();) {
        if (has(shown, it->first)) {
            d.lifecycles.push_back({it->second, LifecycleState::Started});
            ++it;
            continue;
        }
        gone.push_back(it->second);
        it = d.retiring.erase(it);
    }
    auto parent = currentLifecycleOwner();
    d.parentLifecycle = parent;
    struct Key {
        std::shared_ptr<NavController::Data> data;
        std::shared_ptr<LifecycleOwner> parent;
        bool operator==(const Key&) const = default;
    };
    DisposableEffect(Key{navController.data(), parent}, [weak = std::weak_ptr(navController.data()), parent]() -> std::function<void()> {
        auto token = parent->lifecycle().addObserver([weak](LifecycleEvent) {
            if (auto data = weak.lock()) data->applyLifecycles();
        });
        return [weak, token] {  // the NavHost is gone: its screens are no longer shown
            if (auto data = weak.lock())
                for (const auto& [e, state] : data->lifecycles)
                    e->lifecycle().setCurrentState(std::min(e->lifecycle().currentState(), LifecycleState::Created));
        };
    });
    SideEffect([data = navController.data(), gone] {
        data->applyLifecycles();
        for (const auto& e : gone) {
            e->lifecycle().setCurrentState(LifecycleState::Destroyed);
            e->viewModelStore()->clear();
        }
    });
}

} // namespace kompoz
