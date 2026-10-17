#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Single staging slot for a raw tag payload, owned by one write request at a
// time. Producers (web/app tasks) stage; the NFC scan task consumes or
// releases. Every transition out of Ready goes through a compare-and-swap, and
// the payload is cleared BEFORE the slot is published Empty, so a stage on
// another task can never be wiped by a release that is still finishing.
// Test seam: runs while a release is clearing the payload (no-op in firmware).
struct NoRawWriteSlotHooks {
    template <typename Slot> static void midClear(Slot&) {}
};

template <size_t N, typename Hooks = NoRawWriteSlotHooks>
class RawWriteSlot {
public:
    // False if the slot is busy, the id is 0, or the payload is empty/too big.
    bool stage(uint32_t requestId, const char* uid, const uint8_t* data, size_t size) {
        if (requestId == 0 || data == nullptr || size == 0 || size > N) return false;
        uint8_t expected = Empty;
        if (!state_.compare_exchange_strong(expected, Staging, std::memory_order_acquire)) return false;
        memcpy(buf_, data, size);
        size_ = size;
        owner_ = requestId;
        size_t i = 0;
        for (; uid && uid[i] && i < sizeof(uid_) - 1; i++) uid_[i] = uid[i];
        uid_[i] = '\0';
        state_.store(Ready, std::memory_order_release);
        return true;
    }

    // Calls fn(data, size) while holding the slot, then frees it. Only the
    // owning request may consume.
    template <typename Fn>
    bool consume(uint32_t requestId, Fn fn) {
        if (!take(requestId, false)) return false;
        fn(static_cast<const uint8_t*>(buf_), size_);
        clearAndFree();
        return true;
    }

    bool release(uint32_t requestId) {
        if (!take(requestId, false)) return false;
        clearAndFree();
        return true;
    }

    // Reboot path: the scan task is gone, so whatever is staged will never run.
    bool releaseAny() {
        if (!take(0, true)) return false;
        clearAndFree();
        return true;
    }

    bool busy() const { return state_.load(std::memory_order_acquire) != Empty; }
    bool ownedBy(uint32_t requestId) const {
        return state_.load(std::memory_order_acquire) == Ready && owner_ == requestId;
    }
    const char* uid() const { return uid_; }

private:
    enum : uint8_t { Empty, Staging, Ready, Releasing };

    bool take(uint32_t requestId, bool any) {
        uint8_t expected = Ready;
        if (!state_.compare_exchange_strong(expected, Releasing, std::memory_order_acquire)) return false;
        if (!any && owner_ != requestId) {
            state_.store(Ready, std::memory_order_release);
            return false;
        }
        return true;
    }

    void clearAndFree() {
        memset(buf_, 0, sizeof(buf_));
        Hooks::midClear(*this);
        size_ = 0;
        owner_ = 0;
        uid_[0] = '\0';
        state_.store(Empty, std::memory_order_release);
    }

    std::atomic<uint8_t> state_{Empty};
    uint8_t buf_[N] = {0};
    size_t size_ = 0;
    uint32_t owner_ = 0;
    char uid_[17] = {0};
};
