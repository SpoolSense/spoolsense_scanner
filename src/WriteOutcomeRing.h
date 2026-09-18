#pragma once
#include <stddef.h>
#include <stdint.h>

// Slot choice for the write-outcome ring. A browser polls for its result for
// up to 15 s, so an outcome younger than the TTL may still be awaited: evict
// an empty slot first, then the oldest EXPIRED one, and only when every slot
// is still live the oldest overall — which the caller must log, because that
// is a result someone may never see.
struct WriteOutcomeSlotChoice {
    size_t index;
    bool   evictedLive;   // true when a not-yet-expired outcome was overwritten
};

inline WriteOutcomeSlotChoice writeOutcomePickSlot(const bool* done, const uint32_t* completedMs,
                                                   size_t count, uint32_t nowMs, uint32_t ttlMs) {
    if (count == 0) {
        return {0, false};
    }

    for (size_t i = 0; i < count; i++) {
        if (!done[i]) {
            return {i, false};
        }
    }

    // Every slot holds a completed outcome — evict the oldest one. Unsigned
    // subtraction keeps this correct across a millis() wrap.
    size_t oldestIndex = 0;
    uint32_t oldestAge = 0;
    for (size_t i = 0; i < count; i++) {
        uint32_t age = (uint32_t)(nowMs - completedMs[i]);
        if (i == 0 || age > oldestAge) {
            oldestAge = age;
            oldestIndex = i;
        }
    }

    return {oldestIndex, oldestAge < ttlMs};
}
