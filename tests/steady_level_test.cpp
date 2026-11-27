// The steady level filter: wobble is smoothed, real moves and restarts go through.
#include "../src/steady_level.h"

#include <cassert>
#include <cstdio>

int main() {
    using namespace steady;
    const uint64_t min = 60 * 1000;
    Level l;
    assert(Filter(l, 60, false, 0) == 60);
    assert(Filter(l, 62, false, 1 * min) == 60);   // up on battery: wobble
    assert(Filter(l, 61, false, 2 * min) == 60);
    assert(Filter(l, 59, false, 3 * min) == 59);   // down is real
    assert(Filter(l, 70, false, 4 * min) == 70);   // a big jump is taken at once
    // A small rise that keeps coming back three times is real (charging without saying so).
    assert(Filter(l, 71, false, 5 * min) == 70);
    assert(Filter(l, 72, false, 6 * min) == 70);
    assert(Filter(l, 72, false, 7 * min) == 72);
    // Charging: does not go down.
    assert(Filter(l, 72, true, 8 * min) == 72);    // state change restarts
    assert(Filter(l, 71, true, 9 * min) == 72);
    assert(Filter(l, 74, true, 10 * min) == 74);
    // A long gap restarts.
    assert(Filter(l, 70, true, 10 * min + kMaxGapMs + 1) == 70);
    std::puts("Steady level tests passed");
}
