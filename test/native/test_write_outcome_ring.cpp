// Write-outcome ring slot selection tests (#316 follow-up): a browser polls
// for its result for up to 15 s, so eviction must prefer a free slot, then
// the oldest expired one, and only fall back to overwriting a live result
// as a last resort — which the caller must be able to detect and log.
#include "../../src/WriteOutcomeRing.h"
#include <cstdio>

static int failures = 0;

#define CHECK(cond, name)                              \
    do {                                               \
        if (cond) { printf("  PASS  %s\n", name); }    \
        else { printf("  FAIL  %s\n", name); failures++; } \
    } while (0)

int main() {
    printf("=== Write outcome ring slot selection tests ===\n");

    // a. Empty ring: nothing is done yet, so the first slot is free.
    {
        bool done[4] = {false, false, false, false};
        uint32_t completedMs[4] = {0, 0, 0, 0};
        WriteOutcomeSlotChoice choice = writeOutcomePickSlot(done, completedMs, 4, 1000, 20000);
        CHECK(choice.index == 0 && !choice.evictedLive, "empty ring picks index 0, not evicting a live result");
    }

    // b. Some slots done, first free slot is index 3.
    {
        bool done[5] = {true, true, true, false, true};
        uint32_t completedMs[5] = {100, 200, 300, 0, 400};
        WriteOutcomeSlotChoice choice = writeOutcomePickSlot(done, completedMs, 5, 1000, 20000);
        CHECK(choice.index == 3 && !choice.evictedLive, "first free slot (index 3) is picked over any done slot");
    }

    // c. All done, one slot older than the TTL and the rest young.
    {
        bool done[3] = {true, true, true};
        uint32_t completedMs[3] = {5000, 29000, 29500};   // nowMs=30000 -> ages 25000/1000/500
        uint32_t nowMs = 30000;
        WriteOutcomeSlotChoice choice = writeOutcomePickSlot(done, completedMs, 3, nowMs, 20000);
        CHECK(choice.index == 0 && !choice.evictedLive, "the single expired slot (oldest) is picked, not flagged as live eviction");
    }

    // d. All done, two expired slots — the OLDER of the two is picked.
    {
        bool done[2] = {true, true};
        uint32_t completedMs[2] = {5000, 10000};          // nowMs=50000 -> ages 45000/40000
        uint32_t nowMs = 50000;
        WriteOutcomeSlotChoice choice = writeOutcomePickSlot(done, completedMs, 2, nowMs, 20000);
        CHECK(choice.index == 0 && !choice.evictedLive, "older of two expired slots is picked");
    }

    // e. All done, all younger than the TTL — must evict a live result and say so.
    {
        bool done[3] = {true, true, true};
        uint32_t completedMs[3] = {49000, 48000, 49500};  // nowMs=50000 -> ages 1000/2000/500
        uint32_t nowMs = 50000;
        WriteOutcomeSlotChoice choice = writeOutcomePickSlot(done, completedMs, 3, nowMs, 20000);
        CHECK(choice.index == 1 && choice.evictedLive, "oldest of all-live slots is picked and flagged as a live eviction");
    }

    // f. millis() wrap: a slot completed just before the wrap is far older
    // than its raw completedMs value suggests once now has wrapped around.
    {
        bool done[2] = {true, true};
        uint32_t completedMs[2] = {0xFFFFF000u, 500};     // nowMs=1000 -> ages ~5096 / 500
        uint32_t nowMs = 1000;
        WriteOutcomeSlotChoice choice = writeOutcomePickSlot(done, completedMs, 2, nowMs, 20000);
        CHECK(choice.index == 0 && choice.evictedLive, "millis() wrap is handled by unsigned subtraction, wrapped slot is older");
    }

    // g. Age exactly equal to the TTL counts as expired, not live.
    {
        bool done[1] = {true};
        uint32_t completedMs[1] = {30000};                // nowMs=50000 -> age exactly 20000
        uint32_t nowMs = 50000;
        WriteOutcomeSlotChoice choice = writeOutcomePickSlot(done, completedMs, 1, nowMs, 20000);
        CHECK(choice.index == 0 && !choice.evictedLive, "age exactly equal to the TTL counts as expired");
    }

    // h. count == 0 must not crash and returns a harmless default.
    {
        WriteOutcomeSlotChoice choice = writeOutcomePickSlot(nullptr, nullptr, 0, 1000, 20000);
        CHECK(choice.index == 0 && !choice.evictedLive, "zero-length ring returns {0, false} without crashing");
    }

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
