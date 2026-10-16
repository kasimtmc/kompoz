#include <kompoz/bidi.hpp>
#include <kompoz/graphics.hpp>

#include <algorithm>
#include <optional>

namespace kompoz::bidi {

Class classOf(char32_t c) {
    if (c < 0x80) {
        if (c >= '0' && c <= '9') return Class::EN;
        bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        return letter ? Class::L : Class::Neutral;
    }
    if (c == 0x200E) return Class::L;   // left-to-right mark
    if (c == 0x200F) return Class::R;   // right-to-left mark
    if (c == 0x061C) return Class::AL;  // Arabic letter mark
    if (c < 0x100) return c == 0xAA || c == 0xB5 || c == 0xBA || (c >= 0xC0 && c != 0xD7 && c != 0xF7) ? Class::L : Class::Neutral;
    if (c >= 0x0300 && c <= 0x036F) return Class::Neutral;  // combining marks (they follow their base)
    if ((c >= 0x0660 && c <= 0x0669) || c == 0x066B || c == 0x066C) return Class::AN;  // Arabic-Indic digits
    if (c >= 0x06F0 && c <= 0x06F9) return Class::EN;  // Persian digits
    if (c >= 0x0590 && c <= 0x05FF) return Class::R;   // Hebrew
    if (c >= 0x0600 && c <= 0x07BF) return Class::AL;  // Arabic, Syriac, Thaana
    if (c >= 0x07C0 && c <= 0x085F) return Class::R;   // N'Ko, Samaritan, Mandaic
    if (c >= 0x0860 && c <= 0x08FF) return Class::AL;
    if (c >= 0xFB1D && c <= 0xFB4F) return Class::R;
    if ((c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF)) return Class::AL;
    if (c >= 0x1EE00 && c <= 0x1EEFF) return Class::AL;
    if ((c >= 0x10800 && c <= 0x10FFF) || (c >= 0x1E800 && c <= 0x1EDFF)) return Class::R;
    // Punctuation, symbols, arrows, math, shapes, emoji, variation selectors, tags.
    if ((c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x303F) || (c >= 0xFE00 && c <= 0xFE6F) ||
        (c >= 0xFF00 && c <= 0xFF20) || (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65) ||
        (c >= 0x1F000 && c <= 0x1FBFF) || (c >= 0xE0000 && c <= 0xE007F))
        return Class::Neutral;
    return Class::L;
}

namespace {

constexpr char32_t kLRI = 0x2066, kRLI = 0x2067, kFSI = 0x2068, kPDI = 0x2069;  // isolates

struct Unit {  // a grapheme cluster
    int start, end;
    Class cls;
    char32_t first = 0;  // its first code point
    int level = 0;
    size_t match = 0;  // an isolate initiator's PDI (the paragraph's end if it has none)
};
bool initiator(const Unit& u) { return u.first == kLRI || u.first == kRLI || u.first == kFSI; }

// Bidi_Paired_Bracket: a bracket's pair and whether it opens (pair 0: not a bracket). U+2329 and
// U+232A count as U+3008 and U+3009, canonically the same.
struct Bracket {
    char32_t pair = 0;
    bool opens = false;
};
Bracket bracketOf(char32_t c) {
    static constexpr std::pair<char32_t, char32_t> kPairs[] = {
        {'(', ')'},       {'[', ']'},       {'{', '}'},       {0x2045, 0x2046}, {0x207D, 0x207E}, {0x208D, 0x208E},
        {0x2308, 0x2309}, {0x230A, 0x230B}, {0x2768, 0x2769}, {0x276A, 0x276B}, {0x276C, 0x276D}, {0x276E, 0x276F},
        {0x2770, 0x2771}, {0x2772, 0x2773}, {0x2774, 0x2775}, {0x27C5, 0x27C6}, {0x27E6, 0x27E7}, {0x27E8, 0x27E9},
        {0x27EA, 0x27EB}, {0x27EC, 0x27ED}, {0x27EE, 0x27EF}, {0x2983, 0x2984}, {0x2985, 0x2986}, {0x2987, 0x2988},
        {0x2989, 0x298A}, {0x298B, 0x298C}, {0x298D, 0x2990}, {0x298F, 0x298E}, {0x2991, 0x2992}, {0x2993, 0x2994},
        {0x2995, 0x2996}, {0x2997, 0x2998}, {0x29D8, 0x29D9}, {0x29DA, 0x29DB}, {0x29FC, 0x29FD}, {0x2E22, 0x2E23},
        {0x2E24, 0x2E25}, {0x2E26, 0x2E27}, {0x2E28, 0x2E29}, {0x3008, 0x3009}, {0x300A, 0x300B}, {0x300C, 0x300D},
        {0x300E, 0x300F}, {0x3010, 0x3011}, {0x3014, 0x3015}, {0x3016, 0x3017}, {0x3018, 0x3019}, {0x301A, 0x301B},
        {0xFE59, 0xFE5A}, {0xFE5B, 0xFE5C}, {0xFE5D, 0xFE5E}, {0xFF08, 0xFF09}, {0xFF3B, 0xFF3D}, {0xFF5B, 0xFF5D},
        {0xFF5F, 0xFF60}, {0xFF62, 0xFF63}};
    if (c == 0x2329) c = 0x3008;
    if (c == 0x232A) c = 0x3009;
    for (auto [open, close] : kPairs) {
        if (c == open) return {close, true};
        if (c == close) return {open, false};
    }
    return {};
}

// The first strong character's direction in [from, to), what nested isolates hold left out.
std::optional<bool> firstStrong(const std::vector<Unit>& units, size_t from, size_t to) {
    for (size_t i = from; i < to; ++i) {
        if (initiator(units[i])) {
            i = std::min(units[i].match, to);
            continue;
        }
        if (units[i].cls == Class::L) return false;
        if (units[i].cls == Class::R || units[i].cls == Class::AL) return true;
    }
    return std::nullopt;
}

// Resolves units[from, to) at an embedding level (its parity the direction): a nested isolate
// counts as one neutral here (its initiator and PDI take this level's resolution) and its
// content is resolved on its own, one or two levels up.
void resolve(std::vector<Unit>& units, size_t from, size_t to, int level) {
    std::vector<size_t> seq;  // what this level resolves, in order
    for (size_t i = from; i < to;) {
        seq.push_back(i);
        if (!initiator(units[i])) {
            ++i;
            continue;
        }
        size_t end = std::min(units[i].match, to);
        bool rtl = units[i].first == kRLI || (units[i].first == kFSI && firstStrong(units, i + 1, end).value_or(false));
        int inner = rtl ? (level % 2 ? level + 2 : level + 1) : (level % 2 ? level + 1 : level + 2);
        resolve(units, i + 1, end, inner);
        if (end < to) seq.push_back(end);  // its PDI
        i = end + 1;
    }
    bool rtl = level % 2 == 1;
    std::vector<Class> cls(seq.size());  // (an isolate's initiator and PDI are neutral by their class)
    for (size_t k = 0; k < seq.size(); ++k) cls[k] = units[seq[k]].cls;
    // Weak types: numbers after Arabic letters are Arabic numbers, after left-to-right text
    // they are left-to-right; Arabic letters are right-to-left from here on.
    Class strong = rtl ? Class::R : Class::L;
    for (Class& c : cls) {
        if (c == Class::L || c == Class::R || c == Class::AL) {
            strong = c;
            if (c == Class::AL) c = Class::R;
        } else if (c == Class::EN) {
            if (strong == Class::AL) c = Class::AN;
            else if (strong == Class::L) c = Class::L;
        }
    }
    // Bracket pairs (N0; BD16 finds them, among neutrals, with a stack of 63): a pair holding
    // text of this level's direction takes it; holding only the other direction's, that one if
    // the text before the pair goes that way too (numbers count as right-to-left).
    Class base = rtl ? Class::R : Class::L, other = rtl ? Class::L : Class::R;
    auto strongOf = [](Class c) { return c == Class::L ? Class::L : c == Class::Neutral ? Class::Neutral : Class::R; };
    std::vector<std::pair<size_t, size_t>> pairs;
    std::vector<std::pair<char32_t, size_t>> opened;  // each opening bracket's pair and place
    for (size_t k = 0; k < seq.size(); ++k) {
        if (cls[k] != Class::Neutral) continue;
        char32_t cp = units[seq[k]].first;
        Bracket b = bracketOf(cp);
        if (b.pair == 0) continue;
        if (b.opens) {
            if (opened.size() == 63) break;
            opened.push_back({b.pair, k});
            continue;
        }
        char32_t self = bracketOf(b.pair).pair;  // this closing bracket as its opening one names it
        for (size_t s = opened.size(); s-- > 0;)
            if (opened[s].first == self) {
                pairs.push_back({opened[s].second, k});
                opened.resize(s);
                break;
            }
    }
    std::sort(pairs.begin(), pairs.end());
    for (auto [open, close] : pairs) {
        bool same = false, opposite = false;
        for (size_t k = open + 1; k < close; ++k) {
            Class s = strongOf(cls[k]);
            same = same || s == base;
            opposite = opposite || s == other;
        }
        if (!same && !opposite) continue;  // left to the neutrals' rule
        Class before = base;
        for (size_t k = open; k-- > 0;)
            if (Class s = strongOf(cls[k]); s != Class::Neutral) {
                before = s;
                break;
            }
        cls[open] = cls[close] = same || before != other ? base : other;
    }
    // Neutrals between two of one direction (numbers count as right-to-left) take it, others
    // this level's.
    auto direction = [](Class c) { return c == Class::L ? Class::L : Class::R; };
    for (size_t i = 0; i < cls.size();) {
        if (cls[i] != Class::Neutral) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < cls.size() && cls[j] == Class::Neutral) ++j;
        Class before = i == 0 ? base : direction(cls[i - 1]), after = j == cls.size() ? base : direction(cls[j]);
        for (size_t k = i; k < j; ++k) cls[k] = before == after ? before : base;
        i = j;
    }
    // Levels (even goes left to right): what goes against this level's direction one up,
    // numbers in a left-to-right level two up.
    for (size_t k = 0; k < seq.size(); ++k) {
        Unit& u = units[seq[k]];
        if (cls[k] == Class::L) u.level = level + (rtl ? 1 : 0);
        else if (cls[k] == Class::R) u.level = level + (rtl ? 0 : 1);
        else u.level = level + (rtl ? 1 : 2);  // numbers
    }
}

} // namespace

bool firstStrongIsRtl(std::string_view s, bool fallback) {
    int isolates = 0;  // characters inside isolates don't count (UAX #9's P2)
    for (size_t i = 0; i < s.size();) {
        char32_t cp = utf8::next(s, i);
        if (cp == kLRI || cp == kRLI || cp == kFSI) {
            ++isolates;
            continue;
        }
        if (cp == kPDI) {
            if (isolates > 0) --isolates;
            continue;
        }
        if (isolates > 0) continue;
        Class c = classOf(cp);
        if (c == Class::L) return false;
        if (c == Class::R || c == Class::AL) return true;
    }
    return fallback;
}

std::vector<Run> visualRuns(std::string_view s, bool rtl) {
    std::vector<Unit> units;
    bool rightToLeft = false;
    for (size_t i = 0; i < s.size();) {
        size_t end = utf8::nextCluster(s, i), k = i;
        char32_t cp = utf8::next(s, k);
        Class c = classOf(cp);
        rightToLeft = rightToLeft || c == Class::R || c == Class::AL || c == Class::AN;
        units.push_back({int(i), int(end), c, cp});
        i = end;
    }
    if (!rtl && !rightToLeft) return {{0, int(s.size()), false}};
    if (units.empty()) return {{0, 0, rtl}};
    // Each isolate initiator's PDI.
    std::vector<size_t> open;
    for (size_t i = 0; i < units.size(); ++i) {
        units[i].match = units.size();
        if (initiator(units[i])) open.push_back(i);
        else if (units[i].first == kPDI && !open.empty()) {
            units[open.back()].match = i;
            open.pop_back();
        }
    }
    int baseLevel = rtl ? 1 : 0;
    resolve(units, 0, units.size(), baseLevel);
    // Spaces (and isolate marks among them) ending the line take the paragraph's level.
    for (size_t i = units.size(); i-- > 0;) {
        char c = s[size_t(units[i].start)];
        bool mark = units[i].first >= kLRI && units[i].first <= kPDI;
        if (c != ' ' && c != '\t' && !mark) break;
        units[i].level = baseLevel;
    }
    struct LevelRun {
        int start, end, level;
    };
    std::vector<LevelRun> runs;
    for (const Unit& u : units) {
        if (!runs.empty() && runs.back().level == u.level) runs.back().end = u.end;
        else runs.push_back({u.start, u.end, u.level});
    }
    // From the highest level down to the lowest odd one, reverse each stretch at that level or above.
    int highest = 0, lowest = 100;
    for (const LevelRun& r : runs) highest = std::max(highest, r.level), lowest = std::min(lowest, r.level);
    int lowestOdd = lowest % 2 ? lowest : lowest + 1;
    for (int level = highest; level >= lowestOdd; --level)
        for (size_t i = 0; i < runs.size();) {
            if (runs[i].level < level) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < runs.size() && runs[j].level >= level) ++j;
            std::reverse(runs.begin() + std::ptrdiff_t(i), runs.begin() + std::ptrdiff_t(j));
            i = j;
        }
    std::vector<Run> result;
    for (const LevelRun& r : runs) result.push_back({r.start, r.end, r.level % 2 == 1});
    return result;
}

} // namespace kompoz::bidi
