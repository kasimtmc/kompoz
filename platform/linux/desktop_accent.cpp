#include "../common/desktop_accent.hpp"

#include "libdbus.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace kompoz::platform {

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
constexpr const char* kSchemeKey = "color-scheme";  // 0 no preference, 1 dark, 2 light
constexpr const char* kWmNamespace = "org.gnome.desktop.wm.preferences";
constexpr const char* kLayoutKey = "button-layout";  // "appmenu:close", "close,minimize:"

void unwrap(LibDBus& d, DBusMessageIter* it) {
    while (d.message_iter_get_arg_type(it) == kTypeVariant) {
        DBusMessageIter inner;
        d.message_iter_recurse(it, &inner);
        *it = inner;
    }
}

// An accent color from a value: (ddd), red, green and blue in 0..1, maybe inside variants (Read
// wraps it in one more than ReadOne). Out of range: the desktop has none.
std::optional<std::optional<Color>> accentIn(LibDBus& d, DBusMessageIter* it) {
    unwrap(d, it);
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

// Whether the desktop prefers dark from a color-scheme value (u); none: no preference.
std::optional<std::optional<bool>> schemeIn(LibDBus& d, DBusMessageIter* it) {
    unwrap(d, it);
    if (d.message_iter_get_arg_type(it) != kTypeUint32) return std::nullopt;
    uint32_t scheme = 0;
    d.message_iter_get_basic(it, &scheme);
    return scheme == 1 ? std::optional(true) : scheme == 2 ? std::optional(false) : std::optional<bool>{};
}

// A string value (s).
std::optional<std::string> stringIn(LibDBus& d, DBusMessageIter* it) {
    unwrap(d, it);
    if (d.message_iter_get_arg_type(it) != kTypeString) return std::nullopt;
    const char* text = nullptr;
    d.message_iter_get_basic(it, &text);
    return std::string(text ? text : "");
}

} // namespace

struct DesktopAccent::State {
    LibDBus dbus;
    Connection* connection = nullptr;
    platform::Notifier notifier;
    std::atomic<bool> running{true};
    mutable std::mutex mutex;
    std::optional<Color> accent;
    std::optional<bool> dark;
    std::optional<std::string> layout;
    bool accentWanted = true;  // (KOMPOZ_NO_DESKTOP_ACCENT: the scheme only)
    std::thread thread;

    // A key's value, as it is now or as it changed.
    void take(std::string_view ns, std::string_view key, DBusMessageIter* it) {
        if (ns == kWmNamespace) {
            std::optional<std::string> value = key == kLayoutKey ? stringIn(dbus, it) : std::nullopt;
            if (!value) return;
            std::lock_guard lock(mutex);
            layout = *value;
        } else if (key == kKey && accentWanted) {
            if (auto value = accentIn(dbus, it)) {
                std::lock_guard lock(mutex);
                accent = *value;
            } else {
                return;
            }
        } else if (key == kSchemeKey) {
            if (auto value = schemeIn(dbus, it)) {
                std::lock_guard lock(mutex);
                dark = *value;
            } else {
                return;
            }
        } else {
            return;
        }
        if (notifier.notify) notifier.notify();
    }

    // A key's value as it is now: ReadOne (portal version 2+), else Read.
    void readNow(const char* ns, const char* key) {
        for (const char* method : {"ReadOne", "Read"}) {
            Message* call = dbus.message_new_method_call(kPortal, kPath, kSettings, method);
            if (!call) return;
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
            if (dbus.message_iter_init(reply, &it)) take(ns, key, &it);
            dbus.message_unref(reply);
            return;
        }
    }

    void run() {
        if (accentWanted) readNow(kNamespace, kKey);
        readNow(kNamespace, kSchemeKey);
        readNow(kWmNamespace, kLayoutKey);
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
                            if (dbus.message_iter_next(&it)) take(ns, key, &it);
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
    s.accentWanted = !std::getenv("KOMPOZ_NO_DESKTOP_ACCENT");
    if (!s.dbus.load()) return;
    DBusError error;
    s.dbus.error_init(&error);
    s.connection = s.dbus.bus_get_private(kSessionBus, &error);  // its own: SDL's shared one is SDL's
    s.dbus.error_free(&error);
    if (!s.connection) return;
    s.dbus.connection_set_exit_on_disconnect(s.connection, 0);
    s.dbus.error_init(&error);
    for (const char* ns : {kNamespace, kWmNamespace}) {
        std::string rule = std::string("type='signal',interface='") + kSettings + "',member='SettingChanged',arg0='" + ns + "'";
        s.dbus.bus_add_match(s.connection, rule.c_str(), &error);
        s.dbus.error_free(&error);
    }
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

std::optional<bool> DesktopAccent::darkScheme() const {
    std::lock_guard lock(state_->mutex);
    return state_->dark;
}

std::optional<std::string> DesktopAccent::buttonLayout() const {
    std::lock_guard lock(state_->mutex);
    return state_->layout;
}

} // namespace kompoz::platform
