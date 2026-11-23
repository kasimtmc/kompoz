#include "key_text.hpp"

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

#include <dlfcn.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>

namespace kompoz::platform {

namespace {

// libxkbcommon's functions this needs, loaded once (the library stays for the process).
struct Xkb {
    decltype(&xkb_context_new) contextNew = nullptr;
    decltype(&xkb_context_unref) contextUnref = nullptr;
    decltype(&xkb_keysym_to_utf8) keysymToUtf8 = nullptr;
    decltype(&xkb_compose_table_new_from_locale) tableNew = nullptr;
    decltype(&xkb_compose_table_unref) tableUnref = nullptr;
    decltype(&xkb_compose_state_new) stateNew = nullptr;
    decltype(&xkb_compose_state_unref) stateUnref = nullptr;
    decltype(&xkb_compose_state_feed) feed = nullptr;
    decltype(&xkb_compose_state_get_status) status = nullptr;
    decltype(&xkb_compose_state_get_utf8) utf8 = nullptr;
    decltype(&xkb_compose_state_reset) stateReset = nullptr;
    bool loaded = false;
};

const Xkb& xkb() {
    static const Xkb functions = [] {
        Xkb f;
        void* lib = dlopen("libxkbcommon.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return f;
        auto find = [&](auto& pointer, const char* name) {
            pointer = reinterpret_cast<std::remove_reference_t<decltype(pointer)>>(dlsym(lib, name));
            return pointer != nullptr;
        };
        f.loaded = find(f.contextNew, "xkb_context_new") && find(f.contextUnref, "xkb_context_unref") && find(f.keysymToUtf8, "xkb_keysym_to_utf8") &&
                   find(f.tableNew, "xkb_compose_table_new_from_locale") && find(f.tableUnref, "xkb_compose_table_unref") &&
                   find(f.stateNew, "xkb_compose_state_new") && find(f.stateUnref, "xkb_compose_state_unref") &&
                   find(f.feed, "xkb_compose_state_feed") && find(f.status, "xkb_compose_state_get_status") &&
                   find(f.utf8, "xkb_compose_state_get_utf8") && find(f.stateReset, "xkb_compose_state_reset");
        return f;
    }();
    return functions;
}

// The locale the compose table is the desktop's for, as libxkbcommon's documentation finds it.
const char* composeLocale() {
    for (const char* name : {"LC_ALL", "LC_CTYPE", "LANG"})
        if (const char* value = std::getenv(name); value && *value) return value;
    return "C";
}

// Text a keysym types: none for a control character (Backspace's, Enter's, Escape's).
std::string printable(std::string text) {
    if (text.empty() || uint8_t(text[0]) < 0x20 || text[0] == 0x7f) return {};
    return text;
}

}  // namespace

struct KeyText::Impl {
    xkb_context* context = nullptr;
    xkb_compose_table* table = nullptr;
    xkb_compose_state* state = nullptr;
};

KeyText::KeyText() : impl_(std::make_unique<Impl>()) {
    const Xkb& f = xkb();
    if (!f.loaded) return;
    impl_->context = f.contextNew(XKB_CONTEXT_NO_FLAGS);
    if (impl_->context) impl_->table = f.tableNew(impl_->context, composeLocale(), XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (impl_->table) impl_->state = f.stateNew(impl_->table, XKB_COMPOSE_STATE_NO_FLAGS);
}

KeyText::~KeyText() {
    const Xkb& f = xkb();
    if (impl_->state) f.stateUnref(impl_->state);
    if (impl_->table) f.tableUnref(impl_->table);
    if (impl_->context) f.contextUnref(impl_->context);
}

std::string KeyText::typed(uint32_t keysym, std::string_view fallback) {
    const Xkb& f = xkb();
    if (!f.loaded) return printable(std::string(fallback));
    auto text = [&](uint32_t sym) {
        char utf8[64] = {};
        int n = f.keysymToUtf8(sym, utf8, sizeof utf8);
        return printable(n > 0 ? std::string(utf8) : std::string());
    };
    if (!impl_->state || f.feed(impl_->state, keysym) == XKB_COMPOSE_FEED_IGNORED) return text(keysym);  // (Shift, Ctrl...)
    switch (f.status(impl_->state)) {
    case XKB_COMPOSE_COMPOSING: return {};
    case XKB_COMPOSE_COMPOSED: {
        char utf8[64] = {};
        f.utf8(impl_->state, utf8, sizeof utf8);
        f.stateReset(impl_->state);
        return printable(utf8);
    }
    case XKB_COMPOSE_CANCELLED: f.stateReset(impl_->state); return {};
    default: return text(keysym);  // (XKB_COMPOSE_NOTHING: no sequence)
    }
}

void KeyText::reset() {
    if (impl_->state) xkb().stateReset(impl_->state);
}

}  // namespace kompoz::platform
