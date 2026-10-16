#include "desktop_accent.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace kompoz::sdl {

namespace {

// The part of libdbus used here, loaded at run time (as SDL loads it): no build dependency.
struct DBusError {
    const char* name;
    const char* message;
    unsigned int dummy1 : 1, dummy2 : 1, dummy3 : 1, dummy4 : 1, dummy5 : 1;
    void* padding1;
};
struct DBusMessageIter {
    alignas(void*) unsigned char opaque[128];  // (libdbus's is 72 bytes on 64-bit systems)
};
using Connection = void;
using Message = void;
constexpr int kSessionBus = 0, kTypeInvalid = 0, kTypeString = 's', kTypeVariant = 'v', kTypeStruct = 'r', kTypeDouble = 'd';

struct LibDBus {
    void* handle = nullptr;
    void (*error_init)(DBusError*);
    void (*error_free)(DBusError*);
    Connection* (*bus_get_private)(int, DBusError*);
    void (*bus_add_match)(Connection*, const char*, DBusError*);
    void (*connection_set_exit_on_disconnect)(Connection*, unsigned);
    void (*connection_close)(Connection*);
    void (*connection_unref)(Connection*);
    unsigned (*connection_read_write)(Connection*, int);
    Message* (*connection_pop_message)(Connection*);
    Message* (*connection_send_with_reply_and_block)(Connection*, Message*, int, DBusError*);
    Message* (*message_new_method_call)(const char*, const char*, const char*, const char*);
    unsigned (*message_append_args)(Message*, int, ...);
    unsigned (*message_is_signal)(Message*, const char*, const char*);
    void (*message_unref)(Message*);
    unsigned (*message_iter_init)(Message*, DBusMessageIter*);
    int (*message_iter_get_arg_type)(DBusMessageIter*);
    void (*message_iter_recurse)(DBusMessageIter*, DBusMessageIter*);
    void (*message_iter_get_basic)(DBusMessageIter*, void*);
    unsigned (*message_iter_next)(DBusMessageIter*);

    bool load() {
        handle = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
        if (!handle) return false;
        bool ok = true;
        auto get = [&](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(handle, name));
            ok = ok && fn;
        };
        get(error_init, "dbus_error_init");
        get(error_free, "dbus_error_free");
        get(bus_get_private, "dbus_bus_get_private");
        get(bus_add_match, "dbus_bus_add_match");
        get(connection_set_exit_on_disconnect, "dbus_connection_set_exit_on_disconnect");
        get(connection_close, "dbus_connection_close");
        get(connection_unref, "dbus_connection_unref");
        get(connection_read_write, "dbus_connection_read_write");
        get(connection_pop_message, "dbus_connection_pop_message");
        get(connection_send_with_reply_and_block, "dbus_connection_send_with_reply_and_block");
        get(message_new_method_call, "dbus_message_new_method_call");
        get(message_append_args, "dbus_message_append_args");
        get(message_is_signal, "dbus_message_is_signal");
        get(message_unref, "dbus_message_unref");
        get(message_iter_init, "dbus_message_iter_init");
        get(message_iter_get_arg_type, "dbus_message_iter_get_arg_type");
        get(message_iter_recurse, "dbus_message_iter_recurse");
        get(message_iter_get_basic, "dbus_message_iter_get_basic");
        get(message_iter_next, "dbus_message_iter_next");
        if (!ok) dlclose(std::exchange(handle, nullptr));
        return ok;
    }
    ~LibDBus() {
        if (handle) dlclose(handle);
    }
};

constexpr const char* kPortal = "org.freedesktop.portal.Desktop";
constexpr const char* kPath = "/org/freedesktop/portal/desktop";
constexpr const char* kSettings = "org.freedesktop.portal.Settings";
constexpr const char* kNamespace = "org.freedesktop.appearance";
constexpr const char* kKey = "accent-color";

// An accent color from a value: (ddd), red, green and blue in 0..1, maybe inside variants (Read
// wraps it in one more than ReadOne). Out of range: the desktop has none.
std::optional<std::optional<Color>> accentIn(LibDBus& d, DBusMessageIter* it) {
    while (d.message_iter_get_arg_type(it) == kTypeVariant) {
        DBusMessageIter inner;
        d.message_iter_recurse(it, &inner);
        *it = inner;
    }
    if (d.message_iter_get_arg_type(it) != kTypeStruct) return std::nullopt;
    DBusMessageIter field;
    d.message_iter_recurse(it, &field);
    double rgb[3];
    for (int i = 0; i < 3; ++i) {
        if (d.message_iter_get_arg_type(&field) != kTypeDouble) return std::nullopt;
        d.message_iter_get_basic(&field, &rgb[i]);
        d.message_iter_next(&field);
    }
    for (double c : rgb)
        if (c < 0 || c > 1) return std::optional<Color>{};
    return std::optional<Color>{Color{float(rgb[0]), float(rgb[1]), float(rgb[2]), 1}};
}

} // namespace

struct DesktopAccent::State {
    LibDBus dbus;
    Connection* connection = nullptr;
    uint32_t eventType = 0;
    std::atomic<bool> running{true};
    mutable std::mutex mutex;
    std::optional<Color> accent;
    std::thread thread;

    void tell(std::optional<Color> value) {
        {
            std::lock_guard lock(mutex);
            accent = value;
        }
        SDL_Event e{};
        e.type = eventType;
        SDL_PushEvent(&e);
    }

    // The accent as it is now: ReadOne (portal version 2+), else Read.
    void readNow() {
        for (const char* method : {"ReadOne", "Read"}) {
            Message* call = dbus.message_new_method_call(kPortal, kPath, kSettings, method);
            if (!call) return;
            const char* ns = kNamespace;
            const char* key = kKey;
            dbus.message_append_args(call, kTypeString, &ns, kTypeString, &key, kTypeInvalid);
            DBusError error;
            dbus.error_init(&error);
            Message* reply = dbus.connection_send_with_reply_and_block(connection, call, 1000, &error);
            dbus.message_unref(call);
            bool failed = error.name != nullptr;
            dbus.error_free(&error);
            if (!reply) {
                if (failed) continue;  // no such method (an older portal), or no such key
                return;
            }
            DBusMessageIter it;
            if (dbus.message_iter_init(reply, &it))
                if (auto value = accentIn(dbus, &it)) tell(*value);
            dbus.message_unref(reply);
            return;
        }
    }

    void run() {
        readNow();
        while (running.load()) {
            if (!dbus.connection_read_write(connection, 200)) break;  // the bus went away
            while (Message* m = dbus.connection_pop_message(connection)) {
                if (dbus.message_is_signal(m, kSettings, "SettingChanged")) {
                    DBusMessageIter it;
                    const char *ns = nullptr, *key = nullptr;
                    if (dbus.message_iter_init(m, &it) && dbus.message_iter_get_arg_type(&it) == kTypeString) {
                        dbus.message_iter_get_basic(&it, &ns);
                        if (dbus.message_iter_next(&it) && dbus.message_iter_get_arg_type(&it) == kTypeString) {
                            dbus.message_iter_get_basic(&it, &key);
                            if (std::string_view(ns) == kNamespace && std::string_view(key) == kKey && dbus.message_iter_next(&it))
                                if (auto value = accentIn(dbus, &it)) tell(*value);
                        }
                    }
                }
                dbus.message_unref(m);
            }
        }
    }
};

DesktopAccent::DesktopAccent() : state_(std::make_shared<State>()) {
    State& s = *state_;
    if (SDL_getenv("KOMPOZ_NO_DESKTOP_ACCENT") || !s.dbus.load()) return;
    DBusError error;
    s.dbus.error_init(&error);
    s.connection = s.dbus.bus_get_private(kSessionBus, &error);  // its own: SDL's shared one is SDL's
    s.dbus.error_free(&error);
    if (!s.connection) return;
    s.dbus.connection_set_exit_on_disconnect(s.connection, 0);
    s.dbus.error_init(&error);
    std::string rule = std::string("type='signal',interface='") + kSettings + "',member='SettingChanged',arg0='" + kNamespace + "'";
    s.dbus.bus_add_match(s.connection, rule.c_str(), &error);
    s.dbus.error_free(&error);
    s.eventType = SDL_RegisterEvents(1);
    if (s.eventType == 0) return;
    s.thread = std::thread([&s] { s.run(); });
}

DesktopAccent::~DesktopAccent() {
    State& s = *state_;
    s.running = false;
    if (s.thread.joinable()) s.thread.join();
    if (s.connection) {
        s.dbus.connection_close(s.connection);
        s.dbus.connection_unref(s.connection);
    }
}

uint32_t DesktopAccent::eventType() const { return state_->eventType; }

std::optional<Color> DesktopAccent::accent() const {
    std::lock_guard lock(state_->mutex);
    return state_->accent;
}

} // namespace kompoz::sdl
