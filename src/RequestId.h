#pragma once
#include <atomic>
#include <stdint.h>

// Write request ids are minted on several tasks (web, app, MQTT, deduction).
// Deductions settle by request id (#329), so two requests must never share one;
// 0 means "no request" and is never returned.
inline uint32_t nextRequestId(std::atomic<uint32_t>& counter) {
    uint32_t id;
    do {
        id = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    } while (id == 0);
    return id;
}
