#pragma once

#ifdef NATIVE_TEST
  #include "platform/NativePlatform.h"
#endif

#include "NFCTypes.h"
#include "DeductionTracker.h"
#include "DeductionPolicy.h"

// FreeRTOS headers expose the static-semaphore API to production builds only;
// NATIVE_TEST resolves freertos/*.h to no-op stubs without it.
#ifndef NATIVE_TEST
  #define FREERTOS_OS 1
  #include "freertos/FreeRTOS.h"
  #include "freertos/semphr.h"
  #undef FREERTOS_OS
#endif

// DeductionManager — stores pending filament usage deductions in NVS and applies
// them to writable NFC tags on scan. Middleware sends deductions via MQTT after
// prints; scanner stores them persistently and writes to tag when next scanned.
//
// Issue #329 lifecycle: pending -> in flight (claim) -> cleared only when the
// write reports success. A successful enqueue is NOT a completed write; NVS is
// settled from the SPOOL_UPDATED result correlated by request_id. Failed or
// aborted writes leave NVS untouched so the next scan retries.

class DeductionManager {
public:
    static DeductionManager& getInstance();

    // One-time init: statically allocates the tracker mutex (no new heap).
    // Must be called during setup() before NFC and HA tasks start. Returns
    // false if the mutex could not be created — deduction is then disabled
    // (every apply fails closed, NVS pending survives).
    bool begin();

    // Accumulate a deduction for a UID. Adds to any existing pending amount.
    void storePending(const char* uid, float grams);

    // Apply pending deduction to the tag currently presented. Claims the
    // pending snapshot, enqueues the appropriate write, and leaves NVS intact
    // until the write result settles the claim. Returns grams queued/applied
    // (0 if none, claimed elsewhere, or enqueue failed) — NOT grams whose
    // persistence is already complete.
    float applyIfPending(const char* uid, TagKind kind);

    // Apply a pending deduction synchronously via Spoolman (tag not on the
    // scanner). Same claim rules: while a tag write is in flight for this UID
    // this is a no-op; NVS settles only when Spoolman accepts the deduction.
    // Returns grams applied (0 if none, in flight, or Spoolman failed/unset).
    float applyViaSpoolmanIfPending(const char* uid);

    // Read pending amount without clearing (diagnostics)
    float getPending(const char* uid);

    // Shared apply core (see applyIfPending / applyViaSpoolmanIfPending).

    // Settle an in-flight claim from a SPOOL_UPDATED result. Only request_ids
    // matching an active deduction claim settle; everything else (0, unknown,
    // stale duplicates, non-deduction writes) is a no-op.
    void handleWriteResult(uint32_t request_id, bool success);

private:
    DeductionManager() = default;
    DeductionManager(const DeductionManager&) = delete;
    DeductionManager& operator=(const DeductionManager&) = delete;

    // Truncate UID to 15 chars for NVS key limit (16 including null terminator).
    // 7-byte UIDs (14 hex chars) fit fully. 8-byte UIDs (16 hex chars) lose last char.
    void makeNvsKey(const char* uid, char* out, size_t outSize);

    // NVS helpers — private so no caller can bypass claim/settlement.
    float readPendingLocked(const char* uid);
    void writePendingLocked(const char* uid, float grams);  // grams <= 0 removes key
    void clearPendingLocked(const char* uid);

    // Subtract the settled snapshot from the LATEST NVS total (which may have
    // grown while the write was in flight); remove the key only when nothing
    // remains.
    void settleSnapshotLocked(const char* uid, float snapshot);

    // Read-modify-write NVS settlement with UNBOUNDED mutex wait. Used only
    // on lossless paths (definitive write result, post-Spoolman settle).
    // Spoolman HTTP is always done outside the manager mutex, so these waits
    // are bounded by other tasks' NVS I/O, never by network I/O (#329 review).
    bool takeMutexForever();

    // Spoolman deduction outside the manager mutex. Call with NO lock held.
    // Returns grams accepted by Spoolman (incl. 0 g on an already-empty
    // spool); *accepted = false means transport failure, retry later.
    float spoolmanDeductOutsideLock(const char* uid, float pending, bool* accepted);

    bool takeMutex();
    void giveMutex();

    // Settle/release the active synchronous claim (mutex held). success=true
    // subtracts the snapshot from NVS; false leaves it for retry.
    void settleClaim(const char* uid, bool success);

    // Shared apply core. preferTag writes the presented tag; spoolmanOnly goes
    // straight to Spoolman. Returns queued/applied grams.
    float applyInternal(const char* uid, TagKind kind, bool preferTag);

    // Tag-write appliers. On successful enqueue they return >0 with
    // *enqueued = true (claim bound to *requestIdOut, NVS untouched).
    // Non-writable/terminal/Spoolman-direct outcomes return with *enqueued =
    // false and settle or abort the claim themselves.
    float applyOpenPrintTag(const char* uid, float pending, bool* enqueued, uint32_t* requestIdOut);
    float applyOpenTag3D(const char* uid, float pending, bool* enqueued, uint32_t* requestIdOut);

#ifndef NATIVE_TEST
    StaticSemaphore_t mutexBuffer_;
#endif
    SemaphoreHandle_t mutex_ = nullptr;
    DeductionTracker tracker_;
};
