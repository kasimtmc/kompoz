#include "desktop_accent.hpp"

#include "libdbus.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace kompoz::sdl {

namespace {

using namespace dbus;
using DBusError = dbus::Error;
using DBusMessageIter = dbus::Iter;
using LibDBus = dbus::Lib;

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
    platform::Notifier notifier;
    std::atomic<bool> running{true};
    mutable std::mutex mutex;
    std::optional<Color> accent;
    std::thread thread;

    void tell(std::optional<Color> value) {
        {
            std::lock_guard lock(mutex);
            accent = value;
        }
        if (notifier.notify) notifier.notify();
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

DesktopAccent::DesktopAccent(platform::Notifier notifier) : state_(std::make_shared<State>()) {
    State& s = *state_;
    s.notifier = std::move(notifier);
    if (std::getenv("KOMPOZ_NO_DESKTOP_ACCENT") || !s.dbus.load()) return;
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
    s.thread = std::thread([&s] {
        s.run();
        if (s.notifier.threadDone) s.notifier.threadDone();
    });
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

bool DesktopAccent::listening() const { return state_->thread.joinable(); }

std::optional<Color> DesktopAccent::accent() const {
    std::lock_guard lock(state_->mutex);
    return state_->accent;
}

} // namespace kompoz::sdl
