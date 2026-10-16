#pragma once

// Right-to-left text (Arabic, Hebrew) in a line: a simplified Unicode bidirectional algorithm
// (UAX #9) with isolates and bracket pairs but no explicit embeddings or overrides. Characters
// are strong left-to-right, strong right-to-left, numbers or neutral; numbers after right-to-left
// letters keep their own left-to-right order inside them; a pair of brackets goes the level's
// way if what it holds does, else the other way if what it holds and what comes before it do
// (in "אב(גד) x" the closing bracket stays with the Hebrew); other neutrals between two runs of
// one direction take it and otherwise the level's. An isolate (LRI U+2066, RLI U+2067 or FSI
// U+2068 up to its PDI U+2069: "Yorum: \u2067أحمد\u2069 3") goes its own way inside and counts
// as one neutral outside, so what follows doesn't join it. The text layout and the backends
// share it, so a caret lands where the backend drew the character.

#include <string_view>
#include <vector>

namespace kompoz::bidi {

enum class Class { L, R, AL, EN, AN, Neutral };  // AL: Arabic letters (numbers after them are Arabic)
Class classOf(char32_t c);

// The direction of the first strong character, or `fallback` if there is none (Compose's
// TextDirection.ContentOrLtr / ContentOrRtl).
bool firstStrongIsRtl(std::string_view utf8, bool fallback = false);

// A stretch of one direction, as byte offsets into the line.
struct Run {
    int start = 0, end = 0;
    bool rtl = false;  // its characters go from right to left
};

// The line's runs from left to right on screen; the characters of an rtl run are shown in
// reverse. A line with no right-to-left text in a left-to-right paragraph is one run.
std::vector<Run> visualRuns(std::string_view utf8, bool rtl);

} // namespace kompoz::bidi
