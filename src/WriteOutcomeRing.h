#pragma once
#include <stddef.h>
#include <stdint.h>

// Slot state for the write-outcome ring. A slot is reserved the moment a
// write someone will poll for is ACCEPTED, not when it finishes — otherwise a
// burst of requests during that wait could steal every slot before this one's
// result is ever produced.
enum class WriteOutcomeSlotState : uint8_t { Empty = 0, Pending = 1, Done = 2 };

// Returns the slot to reserve for a newly accepted write, or -1 when none may
// be taken. Order: an Empty slot, else the oldest Done slot whose age has
// reached ttlMs. A Pending slot (result not produced yet) and a Done slot
// younger than the TTL (a browser may still be polling for it) are never
// returned — refusing the new write is recoverable, losing a result is not.
inline int writeOutcomeReserveSlot(const WriteOutcomeSlotState* states, const uint32_t* completedMs,
                                   size_t count, uint32_t nowMs, uint32_t ttlMs) {
    if (count == 0) {
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        if (states[i] == WriteOutcomeSlotState::Empty) {
            return (int)i;
        }
    }

    // No free slot — only an expired Done slot may be reused, and the oldest
    // of those. Unsigned subtraction keeps age correct across a millis() wrap.
    int oldestIndex = -1;
    uint32_t oldestAge = 0;
    for (size_t i = 0; i < count; i++) {
        if (states[i] != WriteOutcomeSlotState::Done) {
            continue;
        }
        uint32_t age = (uint32_t)(nowMs - completedMs[i]);
        if (age < ttlMs) {
            continue;
        }
        if (oldestIndex == -1 || age > oldestAge) {
            oldestAge = age;
            oldestIndex = (int)i;
        }
    }

    return oldestIndex;
}

// True once a queued request has waited at least abandonMs. Unsigned
// subtraction keeps it right across a millis() wrap.
inline bool writeRequestAbandoned(uint32_t nowMs, uint32_t enqueuedMs, uint32_t abandonMs) {
    return (uint32_t)(nowMs - enqueuedMs) >= abandonMs;
}
