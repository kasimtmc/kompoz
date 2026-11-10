#pragma once

// The JSON of the language server's messages (lsp.cpp) and of the build's command files: a value,
// read from text and written back, nothing more.

#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace kompozc {

class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json, std::less<>>;

    Json() = default;  // null
    Json(std::nullptr_t) {}
    Json(bool b) : v_(b) {}
    Json(int n) : v_(double(n)) {}
    Json(int64_t n) : v_(double(n)) {}
    Json(size_t n) : v_(double(n)) {}
    Json(double n) : v_(n) {}
    Json(const char* s) : v_(std::string(s)) {}
    Json(std::string s) : v_(std::move(s)) {}
    Json(std::string_view s) : v_(std::string(s)) {}
    Json(Array a) : v_(std::move(a)) {}
    Json(Object o) : v_(std::move(o)) {}

    bool isNull() const { return v_.index() == 0; }
    bool isBool() const { return v_.index() == 1; }
    bool isNumber() const { return v_.index() == 2; }
    bool isString() const { return v_.index() == 3; }
    bool isArray() const { return v_.index() == 4; }
    bool isObject() const { return v_.index() == 5; }

    bool boolean(bool otherwise = false) const { return isBool() ? std::get<bool>(v_) : otherwise; }
    double number(double otherwise = 0) const { return isNumber() ? std::get<double>(v_) : otherwise; }
    int integer(int otherwise = 0) const { return isNumber() ? int(std::get<double>(v_)) : otherwise; }
    const std::string& string() const {
        static const std::string none;
        return isString() ? std::get<std::string>(v_) : none;
    }
    const Array& array() const {
        static const Array none;
        return isArray() ? std::get<Array>(v_) : none;
    }
    Array& array() {
        if (!isArray()) v_ = Array();
        return std::get<Array>(v_);
    }
    const Object& object() const {
        static const Object none;
        return isObject() ? std::get<Object>(v_) : none;
    }
    Object& object() {
        if (!isObject()) v_ = Object();
        return std::get<Object>(v_);
    }

    // A member (null when there is none); set, it makes the value an object.
    const Json& operator[](std::string_view key) const {
        static const Json none;
        if (!isObject()) return none;
        auto it = std::get<Object>(v_).find(key);
        return it == std::get<Object>(v_).end() ? none : it->second;
    }
    Json& operator[](std::string_view key) {
        Object& o = object();
        auto it = o.find(key);
        if (it == o.end()) it = o.emplace(std::string(key), Json()).first;
        return it->second;
    }
    bool has(std::string_view key) const { return isObject() && std::get<Object>(v_).contains(key); }
    void erase(std::string_view key) {
        if (!isObject()) return;
        Object& o = std::get<Object>(v_);
        if (auto it = o.find(key); it != o.end()) o.erase(it);
    }

    std::string dump() const {
        std::string out;
        write(out);
        return out;
    }
    // Throws std::runtime_error on text that isn't JSON.
    static Json parse(std::string_view text) {
        size_t at = 0;
        Json v = read(text, at);
        skip(text, at);
        if (at != text.size()) throw std::runtime_error("json: text after the value");
        return v;
    }

    friend bool operator==(const Json& a, const Json& b) { return a.v_ == b.v_; }

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> v_;

    static void skip(std::string_view t, size_t& at) {
        while (at < t.size() && (t[at] == ' ' || t[at] == '\t' || t[at] == '\n' || t[at] == '\r')) ++at;
    }
    static void put(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }
    static uint32_t hex4(std::string_view t, size_t at) {
        if (at + 4 > t.size()) throw std::runtime_error("json: short \\u escape");
        uint32_t v = 0;
        for (size_t i = at; i < at + 4; ++i) {
            char c = t[i];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else throw std::runtime_error("json: bad \\u escape");
        }
        return v;
    }
    static std::string readString(std::string_view t, size_t& at) {
        ++at;  // "
        std::string s;
        while (at < t.size() && t[at] != '"') {
            char c = t[at++];
            if (c != '\\') {
                s += c;
                continue;
            }
            if (at >= t.size()) break;
            char e = t[at++];
            switch (e) {
            case 'n': s += '\n'; break;
            case 't': s += '\t'; break;
            case 'r': s += '\r'; break;
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case 'u': {
                uint32_t cp = hex4(t, at);
                at += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && at + 6 <= t.size() && t[at] == '\\' && t[at + 1] == 'u') {
                    uint32_t low = hex4(t, at + 2);
                    if (low >= 0xDC00 && low < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        at += 6;
                    }
                }
                put(s, cp);
                break;
            }
            default: s += e;  // " \ /
            }
        }
        if (at >= t.size()) throw std::runtime_error("json: unterminated string");
        ++at;
        return s;
    }
    static Json read(std::string_view t, size_t& at) {
        skip(t, at);
        if (at >= t.size()) throw std::runtime_error("json: no value");
        char c = t[at];
        if (c == '{') {
            Object o;
            ++at;
            skip(t, at);
            if (at < t.size() && t[at] == '}') {
                ++at;
                return o;
            }
            for (;;) {
                skip(t, at);
                if (at >= t.size() || t[at] != '"') throw std::runtime_error("json: expected a key");
                std::string key = readString(t, at);
                skip(t, at);
                if (at >= t.size() || t[at] != ':') throw std::runtime_error("json: expected ':'");
                ++at;
                o.insert_or_assign(std::move(key), read(t, at));
                skip(t, at);
                if (at < t.size() && t[at] == ',') {
                    ++at;
                    continue;
                }
                if (at < t.size() && t[at] == '}') {
                    ++at;
                    return o;
                }
                throw std::runtime_error("json: expected ',' or '}'");
            }
        }
        if (c == '[') {
            Array a;
            ++at;
            skip(t, at);
            if (at < t.size() && t[at] == ']') {
                ++at;
                return a;
            }
            for (;;) {
                a.push_back(read(t, at));
                skip(t, at);
                if (at < t.size() && t[at] == ',') {
                    ++at;
                    continue;
                }
                if (at < t.size() && t[at] == ']') {
                    ++at;
                    return a;
                }
                throw std::runtime_error("json: expected ',' or ']'");
            }
        }
        if (c == '"') return readString(t, at);
        if (t.substr(at, 4) == "true") {
            at += 4;
            return true;
        }
        if (t.substr(at, 5) == "false") {
            at += 5;
            return false;
        }
        if (t.substr(at, 4) == "null") {
            at += 4;
            return Json();
        }
        size_t b = at;
        while (at < t.size() && (std::string_view("+-.eE").find(t[at]) != std::string_view::npos || (t[at] >= '0' && t[at] <= '9'))) ++at;
        if (b == at) throw std::runtime_error("json: unexpected character");
        return std::stod(std::string(t.substr(b, at - b)));
    }
    static void writeString(std::string& out, const std::string& s) {
        out += '"';
        for (char c : s) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += c;
                }
            }
        }
        out += '"';
    }
    void write(std::string& out) const {
        switch (v_.index()) {
        case 0: out += "null"; break;
        case 1: out += std::get<bool>(v_) ? "true" : "false"; break;
        case 2: {
            double d = std::get<double>(v_);
            if (d == double(int64_t(d)) && d > -9e15 && d < 9e15) {
                out += std::to_string(int64_t(d));
            } else {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%.17g", d);
                out += buf;
            }
            break;
        }
        case 3: writeString(out, std::get<std::string>(v_)); break;
        case 4: {
            out += '[';
            bool first = true;
            for (const Json& v : std::get<Array>(v_)) {
                if (!std::exchange(first, false)) out += ',';
                v.write(out);
            }
            out += ']';
            break;
        }
        default: {
            out += '{';
            bool first = true;
            for (const auto& [k, v] : std::get<Object>(v_)) {
                if (!std::exchange(first, false)) out += ',';
                writeString(out, k);
                out += ':';
                v.write(out);
            }
            out += '}';
        }
        }
    }
};

}  // namespace kompozc
