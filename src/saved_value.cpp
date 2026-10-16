#include <kompoz/saved_value.hpp>

#include <cstring>

namespace kompoz {

namespace {

// A tag byte, then: integers zigzag varints, doubles 8 bytes, strings and lists and maps a
// varint length, then their contents.
enum Tag : uint8_t { Null, False, True, Integer, Double, String, List, Map };

void putVarint(std::vector<uint8_t>& out, uint64_t v) {
    while (v >= 0x80) {
        out.push_back(uint8_t(v) | 0x80);
        v >>= 7;
    }
    out.push_back(uint8_t(v));
}

void putString(std::vector<uint8_t>& out, const std::string& s) {
    putVarint(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
}

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    int depth = 0;

    bool varint(uint64_t& v) {
        v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (p == end) return false;
            uint8_t b = *p++;
            v |= uint64_t(b & 0x7F) << shift;
            if (!(b & 0x80)) return true;
        }
        return false;
    }
    bool string(std::string& s) {
        uint64_t n = 0;
        if (!varint(n) || n > uint64_t(end - p)) return false;
        s.assign(reinterpret_cast<const char*>(p), size_t(n));
        p += n;
        return true;
    }
    std::optional<SavedValue> value() {
        if (p == end || ++depth > 64) return std::nullopt;
        struct Leave {
            int& d;
            ~Leave() { --d; }
        } leave{depth};
        switch (*p++) {
        case Null: return SavedValue();
        case False: return SavedValue(false);
        case True: return SavedValue(true);
        case Integer: {
            uint64_t z = 0;
            if (!varint(z)) return std::nullopt;
            return SavedValue(int64_t(z >> 1) ^ -int64_t(z & 1));
        }
        case Double: {
            if (end - p < 8) return std::nullopt;
            uint64_t bits = 0;
            for (int i = 0; i < 8; ++i) bits |= uint64_t(p[i]) << (8 * i);
            p += 8;
            double d;
            std::memcpy(&d, &bits, 8);
            return SavedValue(d);
        }
        case String: {
            std::string s;
            if (!string(s)) return std::nullopt;
            return SavedValue(std::move(s));
        }
        case List: {
            uint64_t n = 0;
            if (!varint(n) || n > uint64_t(end - p)) return std::nullopt;
            SavedValue::List list;
            for (uint64_t i = 0; i < n; ++i) {
                auto v = value();
                if (!v) return std::nullopt;
                list.push_back(std::move(*v));
            }
            return SavedValue(std::move(list));
        }
        case Map: {
            uint64_t n = 0;
            if (!varint(n) || n > uint64_t(end - p)) return std::nullopt;
            SavedValue::Map map;
            for (uint64_t i = 0; i < n; ++i) {
                std::string key;
                if (!string(key)) return std::nullopt;
                auto v = value();
                if (!v) return std::nullopt;
                map.emplace(std::move(key), std::move(*v));
            }
            return SavedValue(std::move(map));
        }
        default: return std::nullopt;
        }
    }
};

void encodeTo(const SavedValue& v, std::vector<uint8_t>& out) {
    if (v.isNull()) return out.push_back(Null);
    if (auto b = v.boolean()) return out.push_back(*b ? True : False);
    if (const std::string* s = v.string()) {
        out.push_back(String);
        return putString(out, *s);
    }
    if (const SavedValue::List* list = v.list()) {
        out.push_back(List);
        putVarint(out, list->size());
        for (const SavedValue& item : *list) encodeTo(item, out);
        return;
    }
    if (const SavedValue::Map* map = v.map()) {
        out.push_back(Map);
        putVarint(out, map->size());
        for (const auto& [key, item] : *map) {
            putString(out, key);
            encodeTo(item, out);
        }
        return;
    }
    if (auto n = v.integer()) {
        out.push_back(Integer);
        return putVarint(out, (uint64_t(*n) << 1) ^ uint64_t(*n >> 63));
    }
    double d = *v.number();
    uint64_t bits;
    std::memcpy(&bits, &d, 8);
    out.push_back(Double);
    for (int i = 0; i < 8; ++i) out.push_back(uint8_t(bits >> (8 * i)));
}

} // namespace

std::optional<bool> SavedValue::boolean() const {
    if (auto* b = std::get_if<bool>(&v_)) return *b;
    return std::nullopt;
}

std::optional<int64_t> SavedValue::integer() const {
    if (auto* n = std::get_if<int64_t>(&v_)) return *n;
    return std::nullopt;
}

std::optional<double> SavedValue::number() const {
    if (auto* d = std::get_if<double>(&v_)) return *d;
    if (auto* n = std::get_if<int64_t>(&v_)) return double(*n);
    return std::nullopt;
}

const SavedValue& SavedValue::operator[](const std::string& key) const {
    static const SavedValue none;
    if (const Map* m = map())
        if (auto it = m->find(key); it != m->end()) return it->second;
    return none;
}

bool operator==(const SavedValue& a, const SavedValue& b) {
    if (a.v_.index() != b.v_.index()) return false;
    if (const SavedValue::List* l = a.list()) return *l == *b.list();
    if (const SavedValue::Map* m = a.map()) return *m == *b.map();
    return a.v_ == b.v_;
}

std::vector<uint8_t> SavedValue::encode() const {
    std::vector<uint8_t> out{'K', 'Z', 1};  // a mark and a version
    encodeTo(*this, out);
    return out;
}

std::optional<SavedValue> SavedValue::decode(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 4 || bytes[0] != 'K' || bytes[1] != 'Z' || bytes[2] != 1) return std::nullopt;
    Reader r{bytes.data() + 3, bytes.data() + bytes.size()};
    auto v = r.value();
    if (!v || r.p != r.end) return std::nullopt;
    return v;
}

} // namespace kompoz
