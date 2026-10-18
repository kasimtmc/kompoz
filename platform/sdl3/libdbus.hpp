#pragma once

// The part of libdbus Kompoz uses (the desktop's accent color, the AT-SPI bridge), loaded at run time
// as SDL loads it: no build dependency, and nothing happens where libdbus isn't installed.

#include <dlfcn.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace kompoz::sdl::dbus {

struct Error {
    const char* name;
    const char* message;
    unsigned int dummy1 : 1, dummy2 : 1, dummy3 : 1, dummy4 : 1, dummy5 : 1;
    void* padding1;
};
struct Iter {
    alignas(void*) unsigned char opaque[128];  // (libdbus's is 72 bytes on 64-bit systems)
};
using Connection = void;
using Message = void;

constexpr int kSessionBus = 0;
constexpr int kTypeInvalid = 0, kTypeByte = 'y', kTypeBoolean = 'b', kTypeInt16 = 'n', kTypeInt32 = 'i', kTypeUint32 = 'u',
              kTypeInt64 = 'x', kTypeDouble = 'd', kTypeString = 's', kTypeObjectPath = 'o', kTypeSignature = 'g',
              kTypeArray = 'a', kTypeVariant = 'v', kTypeStruct = 'r', kTypeDictEntry = 'e';
constexpr int kMessageMethodCall = 1, kMessageMethodReturn = 2, kMessageError = 3, kMessageSignal = 4;

struct Lib {
    void* handle = nullptr;
    void (*error_init)(Error*);
    void (*error_free)(Error*);
    Connection* (*bus_get_private)(int, Error*);
    Connection* (*connection_open_private)(const char*, Error*);
    unsigned (*bus_register)(Connection*, Error*);
    const char* (*bus_get_unique_name)(Connection*);
    void (*bus_add_match)(Connection*, const char*, Error*);
    void (*connection_set_exit_on_disconnect)(Connection*, unsigned);
    void (*connection_close)(Connection*);
    void (*connection_unref)(Connection*);
    unsigned (*connection_read_write)(Connection*, int);
    Message* (*connection_pop_message)(Connection*);
    unsigned (*connection_send)(Connection*, Message*, uint32_t*);
    void (*connection_flush)(Connection*);
    unsigned (*connection_get_unix_fd)(Connection*, int*);
    int (*bus_request_name)(Connection*, const char*, unsigned, Error*);
    Message* (*connection_send_with_reply_and_block)(Connection*, Message*, int, Error*);
    Message* (*message_new_method_call)(const char*, const char*, const char*, const char*);
    Message* (*message_new_method_return)(Message*);
    Message* (*message_new_error)(Message*, const char*, const char*);
    Message* (*message_new_signal)(const char*, const char*, const char*);
    unsigned (*message_append_args)(Message*, int, ...);
    unsigned (*message_is_signal)(Message*, const char*, const char*);
    int (*message_get_type)(Message*);
    const char* (*message_get_path)(Message*);
    const char* (*message_get_interface)(Message*);
    const char* (*message_get_member)(Message*);
    unsigned (*message_get_no_reply)(Message*);
    void (*message_unref)(Message*);
    unsigned (*message_iter_init)(Message*, Iter*);
    void (*message_iter_init_append)(Message*, Iter*);
    int (*message_iter_get_arg_type)(Iter*);
    void (*message_iter_recurse)(Iter*, Iter*);
    void (*message_iter_get_basic)(Iter*, void*);
    unsigned (*message_iter_next)(Iter*);
    unsigned (*message_iter_append_basic)(Iter*, int, const void*);
    unsigned (*message_iter_open_container)(Iter*, int, const char*, Iter*);
    unsigned (*message_iter_close_container)(Iter*, Iter*);

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
        get(connection_open_private, "dbus_connection_open_private");
        get(bus_register, "dbus_bus_register");
        get(bus_get_unique_name, "dbus_bus_get_unique_name");
        get(bus_add_match, "dbus_bus_add_match");
        get(connection_set_exit_on_disconnect, "dbus_connection_set_exit_on_disconnect");
        get(connection_close, "dbus_connection_close");
        get(connection_unref, "dbus_connection_unref");
        get(connection_read_write, "dbus_connection_read_write");
        get(connection_pop_message, "dbus_connection_pop_message");
        get(connection_send, "dbus_connection_send");
        get(connection_flush, "dbus_connection_flush");
        get(connection_get_unix_fd, "dbus_connection_get_unix_fd");
        get(bus_request_name, "dbus_bus_request_name");
        get(connection_send_with_reply_and_block, "dbus_connection_send_with_reply_and_block");
        get(message_new_method_call, "dbus_message_new_method_call");
        get(message_new_method_return, "dbus_message_new_method_return");
        get(message_new_error, "dbus_message_new_error");
        get(message_new_signal, "dbus_message_new_signal");
        get(message_append_args, "dbus_message_append_args");
        get(message_is_signal, "dbus_message_is_signal");
        get(message_get_type, "dbus_message_get_type");
        get(message_get_path, "dbus_message_get_path");
        get(message_get_interface, "dbus_message_get_interface");
        get(message_get_member, "dbus_message_get_member");
        get(message_get_no_reply, "dbus_message_get_no_reply");
        get(message_unref, "dbus_message_unref");
        get(message_iter_init, "dbus_message_iter_init");
        get(message_iter_init_append, "dbus_message_iter_init_append");
        get(message_iter_get_arg_type, "dbus_message_iter_get_arg_type");
        get(message_iter_recurse, "dbus_message_iter_recurse");
        get(message_iter_get_basic, "dbus_message_iter_get_basic");
        get(message_iter_next, "dbus_message_iter_next");
        get(message_iter_append_basic, "dbus_message_iter_append_basic");
        get(message_iter_open_container, "dbus_message_iter_open_container");
        get(message_iter_close_container, "dbus_message_iter_close_container");
        if (!ok) dlclose(std::exchange(handle, nullptr));
        return ok;
    }
    ~Lib() {
        if (handle) dlclose(handle);
    }
};

// A message's arguments written in order; a container filled by a function given its writer.
struct Writer {
    Lib& d;
    Iter it{};
    explicit Writer(Lib& lib) : d(lib) {}
    Writer(Lib& lib, Message* m) : d(lib) { d.message_iter_init_append(m, &it); }
    void basic(int type, const void* value) { d.message_iter_append_basic(&it, type, value); }
    void str(const std::string& v) {
        const char* c = v.c_str();
        basic(kTypeString, &c);
    }
    void path(const std::string& v) {
        const char* c = v.c_str();
        basic(kTypeObjectPath, &c);
    }
    void i16(int16_t v) { basic(kTypeInt16, &v); }
    void i32(int32_t v) { basic(kTypeInt32, &v); }
    void u32(uint32_t v) { basic(kTypeUint32, &v); }
    void boolean(bool v) {
        uint32_t b = v;  // dbus_bool_t
        basic(kTypeBoolean, &b);
    }
    void dbl(double v) { basic(kTypeDouble, &v); }
    template <class Fill>
    void open(int type, const char* signature, Fill&& fill) {  // signature: an array's or a variant's contents
        Writer inner(d);
        d.message_iter_open_container(&it, type, signature, &inner.it);
        fill(inner);
        d.message_iter_close_container(&it, &inner.it);
    }
};

// A message's arguments read in order. One of another type than asked for reads as nothing and
// sets `ok` false.
struct Reader {
    Lib& d;
    Iter it{};
    bool ok = false;
    explicit Reader(Lib& lib) : d(lib) {}
    Reader(Lib& lib, Message* m) : d(lib) { ok = d.message_iter_init(m, &it); }
    int type() { return ok ? d.message_iter_get_arg_type(&it) : kTypeInvalid; }
    template <class T>
    T basic(int type) {
        T v{};
        if (this->type() != type) {
            ok = false;
            return v;
        }
        d.message_iter_get_basic(&it, &v);
        d.message_iter_next(&it);
        return v;
    }
    std::string str() {
        const char* s = basic<const char*>(kTypeString);
        return s ? s : "";
    }
    std::string path() {
        const char* s = basic<const char*>(kTypeObjectPath);
        return s ? s : "";
    }
    int32_t i32() { return basic<int32_t>(kTypeInt32); }
    uint32_t u32() { return basic<uint32_t>(kTypeUint32); }
    bool boolean() { return basic<uint32_t>(kTypeBoolean) != 0; }
    double dbl() { return basic<double>(kTypeDouble); }
    Reader enter() {  // into an array, a struct, a variant or a dictionary entry
        Reader inner(d);
        int t = type();
        if (t != kTypeArray && t != kTypeStruct && t != kTypeVariant && t != kTypeDictEntry) {
            ok = false;
            return inner;
        }
        d.message_iter_recurse(&it, &inner.it);
        inner.ok = true;
        d.message_iter_next(&it);
        return inner;
    }
};

} // namespace kompoz::sdl::dbus
