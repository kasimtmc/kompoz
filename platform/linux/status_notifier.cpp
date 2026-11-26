#include "status_notifier.hpp"

#include "libdbus.hpp"

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>

namespace kompoz::platform {

namespace {

using namespace dbus;

constexpr const char* kItem = "org.kde.StatusNotifierItem";
constexpr const char* kWatcher = "org.kde.StatusNotifierWatcher";
constexpr const char* kMenu = "com.canonical.dbusmenu";
constexpr const char* kProperties = "org.freedesktop.DBus.Properties";
constexpr const char* kIntrospectable = "org.freedesktop.DBus.Introspectable";
constexpr std::string_view kItemPrefix = "/StatusNotifierItem/";
constexpr std::string_view kMenuPrefix = "/MenuBar/";

// One icon as the bus's thread shows it: what its request had at the last sync.
struct Icon {
    uint64_t id = 0;
    std::string tooltip;
    int width = 0, height = 0;
    std::vector<uint8_t> argb;  // the spec's: ARGB32, network byte order
    // The menu, flattened: item ids from 1 (0 the root), each one's parent and children.
    struct Item {
        int id = 0, parent = 0;
        std::string label;
        bool enabled = true, separator = false;
        int toggle = -1;  // -1: none, 0/1: a check box's state
        std::vector<int> children;
    };
    std::vector<Item> items;  // items[0]: the root
    uint32_t revision = 1;
};

// An action the user took, for the platform's thread: the request's id and the item (0: the icon),
// and for the icon's, where the host said it was clicked (screen coordinates; none: (0, 0), as
// hosts that don't know send).
struct Action {
    uint64_t request = 0;
    int item = 0;
    std::optional<IntOffset> at;
};

std::vector<uint8_t> argbOf(const ImageBitmap& image) {
    std::vector<uint8_t> out;
    if (image.empty() || !image.pixels()) return out;
    int w = image.width(), h = image.height();
    const uint8_t* rgba = image.pixels();
    out.resize(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
        out[i * 4] = rgba[i * 4 + 3];  // A R G B, big-endian
        out[i * 4 + 1] = rgba[i * 4];
        out[i * 4 + 2] = rgba[i * 4 + 1];
        out[i * 4 + 3] = rgba[i * 4 + 2];
    }
    return out;
}

void flatten(const std::vector<MenuBarItem>& items, int parent, std::vector<Icon::Item>& out) {
    for (const MenuBarItem& m : items) {
        Icon::Item it{.id = int(out.size()), .parent = parent, .label = m.text, .enabled = m.enabled, .separator = m.separator};
        if (m.checked) it.toggle = *m.checked ? 1 : 0;
        // (the spec's mnemonic: _ marks one, __ is an underscore)
        std::string label;
        for (char c : it.label) label += c == '_' ? std::string("__") : std::string(1, c);
        it.label = label;
        out.push_back(it);
        out[size_t(parent)].children.push_back(it.id);
        if (!m.submenu.empty()) flatten(m.submenu, it.id, out);
    }
}

// The MenuBarItem a flattened id names, walking the same order flatten took.
const MenuBarItem* itemAt(const std::vector<MenuBarItem>& items, int& counter, int id) {
    for (const MenuBarItem& m : items) {
        if (++counter == id) return &m;
        if (!m.submenu.empty())
            if (const MenuBarItem* found = itemAt(m.submenu, counter, id)) return found;
    }
    return nullptr;
}

} // namespace

struct StatusNotifier::State {
    Lib d;
    bool loaded = false;
    std::function<void()> wakePlatform;
    int wakeFd = -1;
    std::atomic<bool> running{true}, watcher{false};
    std::thread thread;
    Connection* bus = nullptr;  // the bus's thread's own
    std::string uniqueName;
    // The platform's thread's, for the bus's thread: under the mutex.
    std::mutex mutex;
    std::vector<Icon> icons;
    bool changed = false;              // icons to (re)register or signal
    std::vector<uint64_t> registered;  // the bus's thread's: the icons the watcher was told of
    std::vector<Action> actions;
    // The requests themselves (the platform's thread's only): their callbacks.
    std::vector<std::shared_ptr<kompoz::detail::TrayRequest>> requests;

    void wake() const {
        if (wakeFd < 0) return;
        uint64_t one = 1;
        [[maybe_unused]] ssize_t n = write(wakeFd, &one, sizeof one);
    }

    static std::string itemPath(uint64_t id) { return std::string(kItemPrefix) + std::to_string(id); }
    static std::string menuPath(uint64_t id) { return std::string(kMenuPrefix) + std::to_string(id); }
    Icon* iconAt(std::string_view path, std::string_view prefix) {
        if (!path.starts_with(prefix)) return nullptr;
        uint64_t id = std::strtoull(std::string(path.substr(prefix.size())).c_str(), nullptr, 10);
        for (Icon& i : icons)
            if (i.id == id) return &i;
        return nullptr;
    }

    // ---- the bus ----

    Message* call(const char* destination, const char* path, const char* interface, const char* method,
                  const std::function<void(Writer&)>& args = {}) {
        Message* m = d.message_new_method_call(destination, path, interface, method);
        if (!m) return nullptr;
        if (args) {
            Writer w(d, m);
            args(w);
        }
        Error error;
        d.error_init(&error);
        Message* reply = d.connection_send_with_reply_and_block(bus, m, 2000, &error);
        d.error_free(&error);
        d.message_unref(m);
        return reply;
    }

    void signal(const std::string& path, const char* interface, const char* member, const std::function<void(Writer&)>& args = {}) {
        Message* m = d.message_new_signal(path.c_str(), interface, member);
        if (!m) return;
        if (args) {
            Writer w(d, m);
            args(w);
        }
        d.connection_send(bus, m, nullptr);
        d.message_unref(m);
    }

    bool watcherThere() {
        Message* reply = call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
                              [](Writer& w) { w.str(kWatcher); });
        if (!reply) return false;
        bool there = Reader(d, reply).boolean();
        d.message_unref(reply);
        return there;
    }

    void registerIcon(uint64_t id) {
        std::string path = itemPath(id);
        // The watcher takes a service name or, as here, an object path on the caller's connection.
        if (Message* reply = call(kWatcher, "/StatusNotifierWatcher", kWatcher, "RegisterStatusNotifierItem",
                                  [&](Writer& w) { w.str(path); }))
            d.message_unref(reply);
    }

    // What changed since: new icons registered, the others told to read theirs again.
    void publish() {
        std::vector<Icon> now;
        {
            std::lock_guard lock(mutex);
            if (!changed) return;
            changed = false;
            now = icons;
        }
        bool there = watcherThere();
        watcher = there;
        std::erase_if(registered, [&](uint64_t id) {
            return std::none_of(now.begin(), now.end(), [id](const Icon& i) { return i.id == id; });
        });
        for (const Icon& i : now) {
            bool known = std::find(registered.begin(), registered.end(), i.id) != registered.end();
            if (!known) {
                if (there) {
                    registerIcon(i.id);
                    registered.push_back(i.id);
                }
                continue;
            }
            for (const char* member : {"NewIcon", "NewToolTip", "NewTitle"}) signal(itemPath(i.id), kItem, member);
            uint32_t revision = i.revision;
            signal(menuPath(i.id), kMenu, "LayoutUpdated", [&](Writer& w) {
                w.u32(revision);
                w.i32(0);
            });
        }
        d.connection_flush(bus);
    }

    // ---- StatusNotifierItem's properties ----

    void variantOf(Writer& w, const Icon& icon, std::string_view name) {
        auto str = [&](const std::string& v) { w.open(kTypeVariant, "s", [&](Writer& x) { x.str(v); }); };
        if (name == "Category") str("ApplicationStatus");
        else if (name == "Id") str("kompoz-" + std::to_string(getpid()) + "-" + std::to_string(icon.id));
        else if (name == "Title") str(icon.tooltip);
        else if (name == "Status") str("Active");
        else if (name == "IconName" || name == "OverlayIconName" || name == "AttentionIconName" || name == "IconThemePath") str("");
        else if (name == "WindowId") w.open(kTypeVariant, "i", [](Writer& x) { x.i32(0); });
        else if (name == "ItemIsMenu") w.open(kTypeVariant, "b", [](Writer& x) { x.boolean(false); });
        else if (name == "Menu") w.open(kTypeVariant, "o", [&](Writer& x) { x.path(menuPath(icon.id)); });
        else if (name == "IconPixmap") w.open(kTypeVariant, "a(iiay)", [&](Writer& x) { pixmaps(x, icon); });
        else if (name == "OverlayIconPixmap" || name == "AttentionIconPixmap") w.open(kTypeVariant, "a(iiay)", [&](Writer& x) {
            x.open(kTypeArray, "(iiay)", [](Writer&) {});
        });
        else if (name == "ToolTip") w.open(kTypeVariant, "(sa(iiay)ss)", [&](Writer& x) {
            x.open(kTypeStruct, nullptr, [&](Writer& s) {
                s.str("");
                s.open(kTypeArray, "(iiay)", [](Writer&) {});
                s.str(icon.tooltip);
                s.str("");
            });
        });
    }
    void pixmaps(Writer& w, const Icon& icon) {
        w.open(kTypeArray, "(iiay)", [&](Writer& a) {
            if (icon.argb.empty()) return;
            a.open(kTypeStruct, nullptr, [&](Writer& s) {
                s.i32(icon.width);
                s.i32(icon.height);
                s.open(kTypeArray, "y", [&](Writer& bytes) {
                    for (uint8_t b : icon.argb) bytes.basic(kTypeByte, &b);
                });
            });
        });
    }
    static constexpr const char* kItemProperties[] = {"Category", "Id", "Title", "Status", "WindowId", "IconName", "IconPixmap",
                                                      "OverlayIconName", "OverlayIconPixmap", "AttentionIconName",
                                                      "AttentionIconPixmap", "ToolTip", "ItemIsMenu", "Menu", "IconThemePath"};

    // ---- dbusmenu ----

    void layout(Writer& w, const Icon& icon, int id, int depth) {
        const Icon::Item& it = icon.items[size_t(id)];
        w.open(kTypeStruct, nullptr, [&](Writer& s) {
            s.i32(id);
            s.open(kTypeArray, "{sv}", [&](Writer& props) { itemProperties(props, it); });
            s.open(kTypeArray, "v", [&](Writer& kids) {
                if (depth == 0) return;
                for (int child : it.children)
                    kids.open(kTypeVariant, "(ia{sv}av)", [&](Writer& v) { layout(v, icon, child, depth < 0 ? -1 : depth - 1); });
            });
        });
    }
    void itemProperties(Writer& props, const Icon::Item& it) {
        auto entry = [&](const char* key, const char* sig, const std::function<void(Writer&)>& value) {
            props.open(kTypeDictEntry, nullptr, [&](Writer& e) {
                e.str(key);
                e.open(kTypeVariant, sig, value);
            });
        };
        if (it.id == 0) {
            entry("children-display", "s", [](Writer& x) { x.str("submenu"); });
            return;
        }
        if (it.separator) {
            entry("type", "s", [](Writer& x) { x.str("separator"); });
            return;
        }
        entry("label", "s", [&](Writer& x) { x.str(it.label); });
        // Send both transitions explicitly. Some menu hosts retain an earlier disabled value
        // when a later layout omits the property (even though the spec defaults to true).
        entry("enabled", "b", [&](Writer& x) { x.boolean(it.enabled); });
        if (it.toggle >= 0) {
            entry("toggle-type", "s", [](Writer& x) { x.str("checkmark"); });
            entry("toggle-state", "i", [&](Writer& x) { x.i32(it.toggle); });
        }
        if (!it.children.empty()) entry("children-display", "s", [](Writer& x) { x.str("submenu"); });
    }

    void push(uint64_t request, int item, std::optional<IntOffset> at = std::nullopt) {
        // handle() already holds mutex while dispatching the DBus method call.
        // Locking it again here would deadlock every tray activation and menu click.
        actions.push_back({request, item, at});
        if (wakePlatform) wakePlatform();
    }

    // ---- method calls ----

    void reply(Message* call, const std::function<void(Writer&)>& args = {}) {
        if (d.message_get_no_reply(call)) return;
        Message* r = d.message_new_method_return(call);
        if (!r) return;
        if (args) {
            Writer w(d, r);
            args(w);
        }
        d.connection_send(bus, r, nullptr);
        d.message_unref(r);
    }
    void fail(Message* call, const char* name, const char* message) {
        Message* r = d.message_new_error(call, name, message);
        if (!r) return;
        d.connection_send(bus, r, nullptr);
        d.message_unref(r);
    }

    void handle(Message* m) {
        std::string_view path = d.message_get_path(m) ? d.message_get_path(m) : "";
        std::string_view interface = d.message_get_interface(m) ? d.message_get_interface(m) : "";
        std::string_view member = d.message_get_member(m) ? d.message_get_member(m) : "";
        std::lock_guard lock(mutex);
        Icon* item = iconAt(path, kItemPrefix);
        Icon* menu = item ? nullptr : iconAt(path, kMenuPrefix);
        if (!item && !menu) return fail(m, "org.freedesktop.DBus.Error.UnknownObject", "no such object");
        Icon& icon = item ? *item : *menu;
        if (interface == kIntrospectable && member == "Introspect")
            return reply(m, [&](Writer& w) { w.str(item ? kItemXml : kMenuXml); });
        if (interface == kProperties) {
            Reader r(d, m);
            std::string of = r.str(), name = member == "GetAll" ? "" : r.str();
            if (item && of == kItem) {
                if (member == "Get") return reply(m, [&](Writer& w) { variantOf(w, icon, name); });
                if (member == "GetAll")
                    return reply(m, [&](Writer& w) {
                        w.open(kTypeArray, "{sv}", [&](Writer& a) {
                            for (const char* p : kItemProperties)
                                a.open(kTypeDictEntry, nullptr, [&](Writer& e) {
                                    e.str(p);
                                    variantOf(e, icon, p);
                                });
                        });
                    });
            }
            if (menu && of == kMenu) {
                auto value = [&](Writer& w, std::string_view p) {
                    if (p == "Version") w.open(kTypeVariant, "u", [](Writer& x) { x.u32(3); });
                    else if (p == "TextDirection") w.open(kTypeVariant, "s", [](Writer& x) { x.str("ltr"); });
                    else if (p == "Status") w.open(kTypeVariant, "s", [](Writer& x) { x.str("normal"); });
                    else w.open(kTypeVariant, "as", [](Writer& x) { x.open(kTypeArray, "s", [](Writer&) {}); });
                };
                if (member == "Get") return reply(m, [&](Writer& w) { value(w, name); });
                if (member == "GetAll")
                    return reply(m, [&](Writer& w) {
                        w.open(kTypeArray, "{sv}", [&](Writer& a) {
                            for (const char* p : {"Version", "TextDirection", "Status", "IconThemePath"})
                                a.open(kTypeDictEntry, nullptr, [&](Writer& e) {
                                    e.str(p);
                                    value(e, p);
                                });
                        });
                    });
            }
            if (member == "Set") return fail(m, "org.freedesktop.DBus.Error.PropertyReadOnly", "read only");
            return reply(m, [](Writer& w) { w.open(kTypeArray, "{sv}", [](Writer&) {}); });
        }
        if (item && interface == kItem) {
            // Activate (a click), SecondaryActivate (a middle click): the request's onAction. Scroll and
            // ContextMenu (the host shows the menu itself from Menu) do nothing here. Its x, y: where
            // the host has the icon (KDE's), a popup's anchor.
            if (member == "Activate" || member == "SecondaryActivate") {
                Reader r(d, m);
                int x = r.i32(), y = r.i32();
                push(icon.id, 0, x || y ? std::optional(IntOffset{x, y}) : std::nullopt);
            }
            return reply(m);
        }
        if (menu && interface == kMenu) {
            Reader r(d, m);
            if (member == "GetLayout") {
                int parent = r.i32(), depth = r.i32();
                if (parent < 0 || size_t(parent) >= icon.items.size()) parent = 0;
                uint32_t revision = icon.revision;
                return reply(m, [&](Writer& w) {
                    w.u32(revision);
                    layout(w, icon, parent, depth);
                });
            }
            if (member == "GetGroupProperties") {
                std::vector<int> ids;
                Reader list = r.enter();
                while (list.type() == kTypeInt32) ids.push_back(list.i32());
                if (ids.empty())
                    for (const Icon::Item& it : icon.items) ids.push_back(it.id);
                return reply(m, [&](Writer& w) {
                    w.open(kTypeArray, "(ia{sv})", [&](Writer& a) {
                        for (int id : ids) {
                            if (id < 0 || size_t(id) >= icon.items.size()) continue;
                            a.open(kTypeStruct, nullptr, [&](Writer& s) {
                                s.i32(id);
                                s.open(kTypeArray, "{sv}", [&](Writer& props) { itemProperties(props, icon.items[size_t(id)]); });
                            });
                        }
                    });
                });
            }
            if (member == "GetProperty") {
                int id = r.i32();
                std::string name = r.str();
                (void)name;
                return reply(m, [&](Writer& w) { w.open(kTypeVariant, "s", [&](Writer& x) { x.str(size_t(id) < icon.items.size() ? icon.items[size_t(id)].label : ""); }); });
            }
            if (member == "Event" || member == "EventGroup") {
                auto event = [&](int id, const std::string& kind) {
                    if (kind == "clicked" && id > 0 && size_t(id) < icon.items.size()) push(icon.id, id);
                };
                if (member == "Event") {
                    int id = r.i32();
                    event(id, r.str());
                    return reply(m);
                }
                Reader list = r.enter();
                while (list.type() == kTypeStruct) {
                    Reader e = list.enter();
                    int id = e.i32();
                    event(id, e.str());
                }
                return reply(m, [](Writer& w) { w.open(kTypeArray, "i", [](Writer&) {}); });
            }
            if (member == "AboutToShow") return reply(m, [](Writer& w) { w.boolean(false); });
            if (member == "AboutToShowGroup")
                return reply(m, [](Writer& w) {
                    w.open(kTypeArray, "i", [](Writer&) {});
                    w.open(kTypeArray, "i", [](Writer&) {});
                });
        }
        fail(m, "org.freedesktop.DBus.Error.UnknownMethod", "unknown method");
    }

    static constexpr const char* kItemXml = R"xml(<node><interface name="org.kde.StatusNotifierItem">
<property name="Category" type="s" access="read"/><property name="Id" type="s" access="read"/>
<property name="Title" type="s" access="read"/><property name="Status" type="s" access="read"/>
<property name="WindowId" type="i" access="read"/><property name="IconName" type="s" access="read"/>
<property name="IconPixmap" type="a(iiay)" access="read"/><property name="OverlayIconName" type="s" access="read"/>
<property name="OverlayIconPixmap" type="a(iiay)" access="read"/><property name="AttentionIconName" type="s" access="read"/>
<property name="AttentionIconPixmap" type="a(iiay)" access="read"/><property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
<property name="ItemIsMenu" type="b" access="read"/><property name="Menu" type="o" access="read"/>
<property name="IconThemePath" type="s" access="read"/>
<method name="ContextMenu"><arg name="x" type="i" direction="in"/><arg name="y" type="i" direction="in"/></method>
<method name="Activate"><arg name="x" type="i" direction="in"/><arg name="y" type="i" direction="in"/></method>
<method name="SecondaryActivate"><arg name="x" type="i" direction="in"/><arg name="y" type="i" direction="in"/></method>
<method name="Scroll"><arg name="delta" type="i" direction="in"/><arg name="orientation" type="s" direction="in"/></method>
<signal name="NewTitle"/><signal name="NewIcon"/><signal name="NewAttentionIcon"/><signal name="NewOverlayIcon"/>
<signal name="NewToolTip"/><signal name="NewStatus"><arg name="status" type="s"/></signal>
</interface></node>)xml";
    static constexpr const char* kMenuXml = R"xml(<node><interface name="com.canonical.dbusmenu">
<property name="Version" type="u" access="read"/><property name="TextDirection" type="s" access="read"/>
<property name="Status" type="s" access="read"/><property name="IconThemePath" type="as" access="read"/>
<method name="GetLayout"><arg type="i" name="parentId" direction="in"/><arg type="i" name="recursionDepth" direction="in"/>
<arg type="as" name="propertyNames" direction="in"/><arg type="u" name="revision" direction="out"/>
<arg type="(ia{sv}av)" name="layout" direction="out"/></method>
<method name="GetGroupProperties"><arg type="ai" name="ids" direction="in"/><arg type="as" name="propertyNames" direction="in"/>
<arg type="a(ia{sv})" name="properties" direction="out"/></method>
<method name="GetProperty"><arg type="i" name="id" direction="in"/><arg type="s" name="name" direction="in"/>
<arg type="v" name="value" direction="out"/></method>
<method name="Event"><arg type="i" name="id" direction="in"/><arg type="s" name="eventId" direction="in"/>
<arg type="v" name="data" direction="in"/><arg type="u" name="timestamp" direction="in"/></method>
<method name="EventGroup"><arg type="a(isvu)" name="events" direction="in"/><arg type="ai" name="idErrors" direction="out"/></method>
<method name="AboutToShow"><arg type="i" name="id" direction="in"/><arg type="b" name="needUpdate" direction="out"/></method>
<method name="AboutToShowGroup"><arg type="ai" name="ids" direction="in"/><arg type="ai" name="updatesNeeded" direction="out"/>
<arg type="ai" name="idErrors" direction="out"/></method>
<signal name="ItemsPropertiesUpdated"><arg type="a(ia{sv})" name="updatedProps"/><arg type="a(ias)" name="removedProps"/></signal>
<signal name="LayoutUpdated"><arg type="u" name="revision"/><arg type="i" name="parent"/></signal>
</interface></node>)xml";

    // Until something is there to read (or the platform's thread wakes it); false: stopping.
    bool wait() {
        int fd = -1;
        d.connection_get_unix_fd(bus, &fd);
        pollfd fds[2] = {{fd, POLLIN, 0}, {wakeFd, POLLIN, 0}};
        poll(fds, 2, -1);
        uint64_t count;
        if (fds[1].revents & POLLIN) [[maybe_unused]] ssize_t n = read(wakeFd, &count, sizeof count);
        return running.load();
    }

    void run() {
        Error error;
        d.error_init(&error);
        bus = d.bus_get_private(kSessionBus, &error);
        d.error_free(&error);
        if (!bus) return;
        d.connection_set_exit_on_disconnect(bus, 0);
        uniqueName = d.bus_get_unique_name(bus);
        // A tray started (or restarted) after the app: its icons registered with it again.
        d.error_init(&error);
        d.bus_add_match(bus, "type='signal',interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='org.kde.StatusNotifierWatcher'", &error);
        d.error_free(&error);
        watcher = watcherThere();
        while (running.load()) {
            publish();
            if (!d.connection_read_write(bus, 0)) break;  // the bus went away
            while (Message* m = d.connection_pop_message(bus)) {
                if (d.message_get_type(m) == kMessageMethodCall) handle(m);
                else if (d.message_is_signal(m, "org.freedesktop.DBus", "NameOwnerChanged")) {
                    registered.clear();
                    std::lock_guard lock(mutex);
                    changed = true;
                }
                d.message_unref(m);
            }
            d.connection_flush(bus);
            if (!wait()) break;
        }
    }
};

StatusNotifier::StatusNotifier(std::function<void()> wake) : state_(std::make_shared<State>()) {
    State& s = *state_;
    s.wakePlatform = std::move(wake);
    if (const char* env = std::getenv("KOMPOZ_TRAY"); env && std::string_view(env) == "0") return;
    if (!(s.loaded = s.d.load())) return;
    s.wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (s.wakeFd < 0) return;
    s.thread = std::thread([&s] { s.run(); });
}

StatusNotifier::~StatusNotifier() {
    State& s = *state_;
    s.running = false;
    s.wake();
    if (s.thread.joinable()) s.thread.join();
    if (s.bus) {
        s.d.connection_close(s.bus);
        s.d.connection_unref(s.bus);
    }
    if (s.wakeFd >= 0) close(s.wakeFd);
}

bool StatusNotifier::available() const { return state_->thread.joinable() && state_->watcher.load(); }

void StatusNotifier::sync(const std::vector<std::shared_ptr<kompoz::detail::TrayRequest>>& requests) {
    State& s = *state_;
    s.requests = requests;
    if (!s.thread.joinable()) return;
    {
        std::lock_guard lock(s.mutex);
        std::vector<Icon> icons;
        for (const auto& r : requests) {
            Icon icon{.id = r->id, .tooltip = r->tooltip, .width = r->icon.width(), .height = r->icon.height(), .argb = argbOf(r->icon)};
            icon.items.push_back({});  // the root
            flatten(r->menu, 0, icon.items);
            for (const Icon& old : s.icons)
                if (old.id == icon.id) icon.revision = old.revision + 1;
            icons.push_back(std::move(icon));
        }
        s.icons = std::move(icons);
        s.changed = true;
    }
    s.wake();
}

void StatusNotifier::takeActions() {
    State& s = *state_;
    std::vector<Action> actions;
    {
        std::lock_guard lock(s.mutex);
        actions.swap(s.actions);
    }
    for (const Action& a : actions)
        for (const auto& r : s.requests) {
            if (r->id != a.request) continue;
            if (a.item == 0) {
                if (r->onAction) r->onAction(a.at);
            } else {
                int counter = 0;
                if (const MenuBarItem* item = itemAt(r->menu, counter, a.item); item && item->enabled && item->onClick) item->onClick();
            }
        }
}

} // namespace kompoz::platform
