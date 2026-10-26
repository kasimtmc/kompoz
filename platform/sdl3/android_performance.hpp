#pragma once

// ADPF on Android 13 and later (APerformanceHint): the system is told how long each frame's work
// took against the time a frame has, and gives the thread drawing the frames the cores and clock it
// needs to fit in it (left alone, a light load runs on a phone's small cores at a low clock, and a
// frame now and then misses the display's refresh).

#include <cstdint>

namespace kompoz::android_performance {

// A session for the calling thread aiming at `targetNs` a frame; nothing where there is no ADPF
// (before Android 13, or the device doesn't support it).
void start(int64_t targetNs);
void setTarget(int64_t targetNs);  // the display's refresh period changed
void report(int64_t workNs);       // a frame's work (not what it waited for)
void stop();

} // namespace kompoz::android_performance
