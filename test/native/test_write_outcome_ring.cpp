// Write-outcome ring slot reservation tests (#316 follow-up): an outcome slot
// is reserved the moment a polled write is ACCEPTED, not when it finishes, so
// a burst of later requests can never steal the result out from under a
// browser that is still polling for it. A Pending slot and a Done slot still
// inside its TTL must never be handed out; only an Empty slot or an expired
// Done slot may be reserved, and refusing the new write is the fallback.
#include "../../src/WriteOutcomeRing.h"
#include <cstdio>

static int failures = 0;

#define CHECK(cond, name)                              \
    do {                                               \
        if (cond) { printf("  PASS  %s\n", name); }    \
        else { printf("  FAIL  %s\n", name); failures++; } \
    } while (0)

using State = WriteOutcomeSlotState;

int main() {
    printf("=== Write outcome ring slot reservation tests ===\n");

    // a. All empty: nothing reserved yet, so the first slot is free.
    {
        State states[4] = {State::Empty, State::Empty, State::Empty, State::Empty};
        uint32_t completedMs[4] = {0, 0, 0, 0};
        int slot = writeOutcomeReserveSlot(states, completedMs, 4, 1000, 20000);
        CHECK(slot == 0, "all empty picks index 0");
    }

    // b. First Empty slot is index 3 — picked over any Done slot.
    {
        State states[5] = {State::Done, State::Done, State::Done, State::Empty, State::Done};
        uint32_t completedMs[5] = {100, 200, 300, 0, 400};
        int slot = writeOutcomeReserveSlot(states, completedMs, 5, 1000, 20000);
        CHECK(slot == 3, "first free slot (index 3) is picked over any done slot");
    }

    // c. No Empty, one Done expired — that index is reused.
    {
        State states[3] = {State::Done, State::Done, State::Done};
        uint32_t completedMs[3] = {5000, 29000, 29500};   // nowMs=30000 -> ages 25000/1000/500
        int slot = writeOutcomeReserveSlot(states, completedMs, 3, 30000, 20000);
        CHECK(slot == 0, "the single expired slot is reused");
    }

    // d. No Empty, two expired — the OLDER of the two is picked.
    {
        State states[2] = {State::Done, State::Done};
        uint32_t completedMs[2] = {5000, 10000};          // nowMs=50000 -> ages 45000/40000
        int slot = writeOutcomeReserveSlot(states, completedMs, 2, 50000, 20000);
        CHECK(slot == 0, "older of two expired slots is picked");
    }

    // e. No Empty, all Done but younger than the TTL — nothing may be taken.
    {
        State states[3] = {State::Done, State::Done, State::Done};
        uint32_t completedMs[3] = {49000, 48000, 49500};  // nowMs=50000 -> ages 1000/2000/500
        int slot = writeOutcomeReserveSlot(states, completedMs, 3, 50000, 20000);
        CHECK(slot == -1, "no slot may be taken when every Done result is still live");
    }

    // f. A Pending slot is never returned, even when its completedMs (stale
    // from a previous occupant, or simply zero) looks ancient — a younger but
    // already-expired Done slot next to it is reused instead.
    {
        State states[2] = {State::Pending, State::Done};
        uint32_t completedMs[2] = {0, 25000};             // nowMs=50000 -> Done age 25000 (expired)
        int slot = writeOutcomeReserveSlot(states, completedMs, 2, 50000, 20000);
        CHECK(slot == 1, "Pending slot is skipped in favor of the expired Done slot");
    }

    // g. Pending plus a still-live Done, no Empty — must refuse, never touch Pending.
    {
        State states[2] = {State::Pending, State::Done};
        uint32_t completedMs[2] = {0, 49000};             // nowMs=50000 -> Done age 1000 (live)
        int slot = writeOutcomeReserveSlot(states, completedMs, 2, 50000, 20000);
        CHECK(slot == -1, "Pending plus a live Done refuses rather than reserving Pending");
    }

    // h. millis() wrap: a Done slot completed just before the wrap is far
    // older than its raw completedMs value suggests once now has wrapped.
    {
        State states[1] = {State::Done};
        uint32_t completedMs[1] = {0xFFFFF000u};          // nowMs=1000 -> age 5096 (wraps)
        int slot = writeOutcomeReserveSlot(states, completedMs, 1, 1000, 4000);
        CHECK(slot == 0, "millis() wrap is handled by unsigned subtraction, wrapped slot is expired");
    }

    // i. Age exactly equal to the TTL counts as expired, not live.
    {
        State states[1] = {State::Done};
        uint32_t completedMs[1] = {30000};                // nowMs=50000 -> age exactly 20000
        int slot = writeOutcomeReserveSlot(states, completedMs, 1, 50000, 20000);
        CHECK(slot == 0, "age exactly equal to the TTL counts as expired");
    }

    // j. count == 0 must not crash and returns -1.
    {
        int slot = writeOutcomeReserveSlot(nullptr, nullptr, 0, 1000, 20000);
        CHECK(slot == -1, "zero-length ring returns -1 without crashing");
    }

    // writeRequestAbandoned: how long a queued request may wait for its tag.
    CHECK(!writeRequestAbandoned(5000, 1000, 10000), "wait below the limit is not abandoned");
    CHECK(writeRequestAbandoned(11000, 1000, 10000), "wait exactly at the limit is abandoned");
    CHECK(writeRequestAbandoned(60000, 1000, 10000), "wait well past the limit is abandoned");
    CHECK(!writeRequestAbandoned(2000, 0xFFFFF000u, 10000), "just after a millis() wrap, only ~6s passed: not abandoned");
    CHECK(writeRequestAbandoned(2000, 0xFFFFF000u, 5000), "across a millis() wrap the unsigned delta still expires");

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
