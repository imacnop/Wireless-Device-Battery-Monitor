#pragma once
// Keeps a level from wobbling (60 -> 62 -> 61) between readings: on battery it does not go up, while
// charging it does not go down, by a few points. A move the same way three readings in a row is real
// (a device charging without saying so, or the cell settling) and is taken. A change of charging state,
// a long gap or a bigger jump starts over. Plain C++ so it can be tested without Windows.
#include <cstdint>

namespace steady {

constexpr int kMaxStep = 3;                     // points of wobble that are smoothed away
constexpr int kConfirmReadings = 3;             // same-direction readings that make a move real
constexpr uint64_t kMaxGapMs = 30ull * 60 * 1000;

struct Level {
    int shown = -1;
    bool charging = false;
    uint64_t at = 0;  // when the last reading came in
    int pending = 0;  // readings in a row that went against the direction
};

// Returns the level to show for a fresh reading `pct` (0..100) and remembers it.
inline int Filter(Level& l, int pct, bool charging, uint64_t now) {
    const bool restart = l.shown < 0 || charging != l.charging || now - l.at > kMaxGapMs;
    l.at = now;
    l.charging = charging;
    if (restart) {
        l.shown = pct;
        l.pending = 0;
        return pct;
    }
    const int against = charging ? l.shown - pct : pct - l.shown;  // > 0: moving the wrong way
    if (against > 0 && against <= kMaxStep && ++l.pending < kConfirmReadings) return l.shown;
    l.shown = pct;
    l.pending = 0;
    return pct;
}

}  // namespace steady
