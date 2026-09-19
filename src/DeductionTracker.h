#ifndef DEDUCTION_TRACKER_H
#define DEDUCTION_TRACKER_H

#include <cstdint>
#include <cstring>

// DeductionTracker — fixed-capacity, heap-free state machine tracking pending
// filament deductions from claim to write settlement (issue #329).
//
// Lifecycle per UID:
//   claim   : pending -> in flight, snapshotting the grams being applied
//   bind    : attach the NFC write request_id once the write is generated
//   resolve : write reported success -> settled (caller subtracts the snapshot
//             from the latest NVS total); write failed -> back to pending
//   abort   : write never made it into the queue -> back to pending
//
// A second claim for the same UID while one is active (unbound or bound) is
// rejected — this is what stops the ApplicationManager / HomeAssistantManager
// double-deduction race. resolveImmediate()/abortUid() on untracked UIDs are
// no-ops so terminal/synchronous callers stay idempotent.
//
// Pure logic only — no FreeRTOS, no NVS, no heap. Thread safety is provided
// by the DeductionManager mutex wrapping every call here.

class DeductionTracker {
public:
    static constexpr size_t CAPACITY = 8;  // matches the NFC write queue depth

    enum class State : uint8_t {
        Free,       // slot unused
        Unbound,    // claimed; request_id not generated/known yet (tag-write path)
        Bound,      // claimed; waiting for the SPOOL_UPDATED result of request_id
        Immediate,  // non-tag target (Spoolman-direct / terminal): settles synchronously
    };

    struct Slot {
        char uid[17];      // 16 hex chars + null (matches NFCWriteRequest::expected_spool_id)
        float snapshot_g;  // grams captured at claim time
        uint32_t request_id;
        State state;
    };

    // Reserve a claim for `uid` snapshotting `snapshotGrams`. Returns false if
    // an ACTIVE claim for this UID already exists (duplicate apply — the
    // #329 double-deduction guard) or the table is full (fail closed).
    // Settled slots (Free state with leftover UID bookkeeping) are always
    // reusable, first by the same UID, then FIFO for a new UID. Snapshot grams
    // are stored as float — no int range limits.
    bool claim(const char* uid, float snapshotGrams, State initial = State::Unbound) {
        if (uid == nullptr || uid[0] == '\0' || snapshotGrams <= 0.0f) return false;
        if (initial == State::Free) return false;
        size_t settledSame = CAPACITY;
        size_t settledAny = CAPACITY;
        for (size_t i = 0; i < CAPACITY; i++) {
            if (slots_[i].state != State::Free) {
                if (uidMatch(slots_[i].uid, uid)) {
                    return false;  // claim already active — reject the duplicate
                }
            } else if (slots_[i].uid[0] == '\0') {
                initSlot(i, uid, snapshotGrams, initial);  // pristine slot
                return true;
            } else {
                if (settledAny == CAPACITY) settledAny = i;
                if (uidMatch(slots_[i].uid, uid) && settledSame == CAPACITY) settledSame = i;
            }
        }
        // No pristine slot: recycle a settled slot (same UID first, then the
        // oldest bookkeeping) so capacity always recovers after failures.
        size_t reuse = (settledSame < CAPACITY) ? settledSame : settledAny;
        if (reuse < CAPACITY) {
            initSlot(reuse, uid, snapshotGrams, initial);
            return true;
        }
        return false;  // all slots active on other UIDs — fail closed
    }

    // Attach the generated write request_id to the unbound claim for `uid`.
    void bind(const char* uid, uint32_t request_id) {
        if (request_id == 0) return;
        for (size_t i = 0; i < CAPACITY; i++) {
            if (slots_[i].state == State::Unbound && uidMatch(slots_[i].uid, uid)) {
                slots_[i].request_id = request_id;
                slots_[i].state = State::Bound;
                return;
            }
        }
    }

    // The claim took a synchronous terminal branch instead of a tag write
    // (e.g. OpenTag3D nominal format routing to Spoolman, or a terminal drop).
    void releaseToImmediate(const char* uid) {
        for (size_t i = 0; i < CAPACITY; i++) {
            if ((slots_[i].state == State::Unbound || slots_[i].state == State::Bound) &&
                uidMatch(slots_[i].uid, uid)) {
                slots_[i].request_id = 0;
                slots_[i].state = State::Immediate;
                return;
            }
        }
    }

    // The write for `request_id` reported its final result. If `success`,
    // the claim is settled: `snapshot` returns the grams to subtract from the
    // latest NVS total (caller does the read-modify-write; deductions added
    // meanwhile survive). On failure the claim simply returns to pending.
    // Unknown or stale request ids (including 0, unbound, or immediate slots)
    // are no-ops returning false.
    bool resolve(uint32_t request_id, bool success, float* snapshot) {
        if (request_id == 0 || snapshot == nullptr) return false;
        (void)success;  // NVS settlement is the caller's job; failure = no settle, claim freed
        for (size_t i = 0; i < CAPACITY; i++) {
            if (slots_[i].state == State::Bound && slots_[i].request_id == request_id) {
                *snapshot = slots_[i].snapshot_g;
                strncpy(lastSettledUid_, slots_[i].uid, sizeof(lastSettledUid_) - 1);
                lastSettledUid_[sizeof(lastSettledUid_) - 1] = '\0';
                clearSlot(i);
                return true;
            }
        }
        return false;
    }

    // Synchronous (non-tag-write) outcome for an immediate claim: success or
    // terminal-drop settles and frees the slot; failure returns the grams to
    // pending. Unknown UIDs are no-ops (idempotent).
    bool resolveImmediate(const char* uid, bool success, float* snapshot) {
        if (uid == nullptr || snapshot == nullptr) return false;
        for (size_t i = 0; i < CAPACITY; i++) {
            if (slots_[i].state == State::Immediate && uidMatch(slots_[i].uid, uid)) {
                *snapshot = slots_[i].snapshot_g;
                strncpy(lastSettledUid_, slots_[i].uid, sizeof(lastSettledUid_) - 1);
                lastSettledUid_[sizeof(lastSettledUid_) - 1] = '\0';
                clearSlot(i);
                (void)success;  // NVS settlement is the caller's job either way
                return true;
            }
        }
        return false;
    }

    // The write never entered the queue (queue full / preparation failure).
    // Release the bound claim so the deduction stays pending for next scan.
    void abort(uint32_t request_id) {
        if (request_id == 0) return;
        for (size_t i = 0; i < CAPACITY; i++) {
            if (slots_[i].state == State::Bound && slots_[i].request_id == request_id) {
                clearSlot(i);
                return;
            }
        }
    }
    // Release a claim that never got a request id (preparation failed before
    // bind). Immediate slots are left alone — synchronous callers settle them
    // via resolveImmediate().
    void abortUid(const char* uid) {
        if (uid == nullptr) return;
        for (size_t i = 0; i < CAPACITY; i++) {
            if (slots_[i].state == State::Unbound && uidMatch(slots_[i].uid, uid)) {
                clearSlot(i);
                return;
            }
        }
    }

    // Active tag-write claim (unbound or bound) for a UID. While one is
    // active the caller must NOT start a second apply for this UID.
    bool findActive(const char* uid, float* snapshot, uint32_t* requestId) const {
        if (uid == nullptr) return false;
        for (size_t i = 0; i < CAPACITY; i++) {
            if ((slots_[i].state == State::Unbound || slots_[i].state == State::Bound) &&
                uidMatch(slots_[i].uid, uid)) {
                if (snapshot) *snapshot = slots_[i].snapshot_g;
                if (requestId) *requestId = slots_[i].request_id;
                return true;
            }
        }
        return false;
    }

    bool hasAnyClaim(const char* uid) const {
        if (uid == nullptr) return false;
        for (size_t i = 0; i < CAPACITY; i++) {
            if (slots_[i].state != State::Free && uidMatch(slots_[i].uid, uid)) {
                return true;
            }
        }
        return false;
    }

    // UID of the claim most recently settled by resolve()/resolveImmediate().
    // The slot keeps its UID after clearing (only state resets) so this read,
    // and duplicate-detection in claim(), stay valid; the next claim of that
    // UID reuses the slot.
    const char* lastSettledUid() const { return lastSettledUid_; }

    // A reusable slot: free and holding no settled bookkeeping.
    static bool slotReusable(const Slot& s) {
        return s.state == State::Free && s.uid[0] == '\0';
    }

    size_t activeCount() const {
        size_t n = 0;
        for (size_t i = 0; i < CAPACITY; i++) {
            if (slots_[i].state != State::Free) n++;
        }
        return n;
    }

    // Slots in use, counting settled bookkeeping still pending reuse.
    size_t usedSlots() const {
        size_t n = 0;
        for (size_t i = 0; i < CAPACITY; i++) {
            if (!slotReusable(slots_[i])) n++;
        }
        return n;
    }

private:
    static bool uidMatch(const char* a, const char* b) { return strcmp(a, b) == 0; }

    void initSlot(size_t i, const char* uid, float grams, State state) {
        memset(&slots_[i], 0, sizeof(slots_[i]));  // resets request_id too
        strncpy(slots_[i].uid, uid, sizeof(slots_[i].uid) - 1);
        slots_[i].snapshot_g = grams;
        slots_[i].state = state;
    }

    // Settle/release: reset everything except the UID, which stays as
    // bookkeeping until the next claim of this (or, at capacity, another) UID.
    void clearSlot(size_t i) {
        slots_[i].snapshot_g = 0.0f;
        slots_[i].request_id = 0;
        slots_[i].state = State::Free;
    }

    // Default-initialized so every slot starts Free with an empty UID —
    // an uninitialized State byte would read as an active claim.
    Slot slots_[CAPACITY] = {};
    char lastSettledUid_[17] = {0};
};

#endif  // DEDUCTION_TRACKER_H
