// DeductionManager.cpp — Pending filament deduction storage (NVS) and tag write-back.
// Middleware sends usage deductions via MQTT after prints. Scanner stores them in NVS
// and applies to writable tags (OpenPrintTag, OpenTag3D) on the next scan.
//
// Issue #329 lifecycle: a deduction is claimed (pending -> in flight) BEFORE any
// write is enqueued or Spoolman call is made. NVS settles only when the write
// result (SPOOL_UPDATED, correlated by request_id) or the synchronous Spoolman
// call reports success. Failures and aborts leave NVS untouched so the next
// scan retries, and a second concurrent apply for the same UID is rejected by
// the claim — no double deduction.

#include "DeductionManager.h"

#ifndef NATIVE_TEST
  #include <Preferences.h>
  #include <Arduino.h>
  #include "NFCManager.h"
  #include "SpoolmanManager.h"
  #include "LogBuffer.h"
  #include "opentag3d_lib.h"
#endif

DeductionManager& DeductionManager::getInstance() {
    static DeductionManager instance;
    return instance;
}

bool DeductionManager::begin() {
#ifndef NATIVE_TEST
    if (mutex_ == nullptr) {
        // Static allocation — honors the no-new-heap rule.
        mutex_ = xSemaphoreCreateMutexStatic(&mutexBuffer_);
    }
#endif
    return mutex_ != nullptr;
}

void DeductionManager::makeNvsKey(const char* uid, char* out, size_t outSize) {
    // NVS key limit: 15 usable chars (16 including null terminator)
    size_t maxLen = (outSize - 1 < 15) ? outSize - 1 : 15;
    strncpy(out, uid, maxLen);
    out[maxLen] = '\0';
}

// ── NVS helpers (call with the manager mutex held when another task may race) ──

float DeductionManager::readPendingLocked(const char* uid) {
#ifndef NATIVE_TEST
    char key[16];
    makeNvsKey(uid, key, sizeof(key));

    Preferences prefs;
    prefs.begin("deductions", true);  // RO mode
    float value = prefs.getFloat(key, 0.0f);
    prefs.end();
    return value;
#else
    (void)uid;
    return 0.0f;
#endif
}

void DeductionManager::writePendingLocked(const char* uid, float grams) {
#ifndef NATIVE_TEST
    char key[16];
    makeNvsKey(uid, key, sizeof(key));

    Preferences prefs;
    prefs.begin("deductions", false);  // RW mode
    if (grams > 0.0f) {
        prefs.putFloat(key, grams);
    } else {
        prefs.remove(key);
    }
    prefs.end();
#else
    (void)uid;
    (void)grams;
#endif
}

void DeductionManager::clearPendingLocked(const char* uid) {
    writePendingLocked(uid, 0.0f);
#ifndef NATIVE_TEST
    Serial.printf("DeductionManager: Cleared pending deduction for %s\n", uid);
#endif
}

// Subtract the settled snapshot from the LATEST NVS total — deductions added
// while the write was in flight survive — and remove the key only when no
// remainder exists.
void DeductionManager::settleSnapshotLocked(const char* uid, float snapshot) {
#ifndef NATIVE_TEST
    float latest = readPendingLocked(uid);
    float remainder = latest - snapshot;
    if (remainder > 0.0f) {
        writePendingLocked(uid, remainder);
        Serial.printf("DeductionManager: Settled %.1fg for %s (%.1fg added during write stays pending)\n",
                      snapshot, uid, remainder);
    } else {
        clearPendingLocked(uid);
    }
#else
    (void)uid;
    (void)snapshot;
#endif
}

bool DeductionManager::takeMutex() {
    return mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) == pdTRUE;
}

// Lossless settlement paths wait without timeout. Safe only because the
// mutex is never held across network I/O anymore (Spoolman calls run outside
// the lock), so the worst-case hold is one NVS read-modify-write.
bool DeductionManager::takeMutexForever() {
    return mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
}

void DeductionManager::giveMutex() {
    if (mutex_ != nullptr) xSemaphoreGive(mutex_);
}

// ── Public entry points ─────────────────────────────────────

void DeductionManager::storePending(const char* uid, float grams) {
#ifndef NATIVE_TEST
    // Unbounded wait: this persists user-visible deduction data, and a silent
    // drop here loses grams while MQTT reports success (#329 review). Safe
    // because no lock holder performs network I/O anymore — Spoolman calls run
    // outside the manager mutex — so the wait is bounded by NVS I/O only.
    if (!takeMutexForever()) {
        Serial.println("DeductionManager: storePending mutex missing — skipped (fail closed)");
        return;
    }
    float current = readPendingLocked(uid);
    float total = current + grams;
    writePendingLocked(uid, total);
    giveMutex();

    Serial.printf("DeductionManager: Stored %.1fg pending for %s (total: %.1fg)\n", grams, uid, total);
#endif
}

float DeductionManager::getPending(const char* uid) {
    if (!takeMutex()) {
        // Read-only diagnostic: an unlocked read can't corrupt state; at
        // worst it races with settlement and reports a stale value.
        return readPendingLocked(uid);
    }
    float value = readPendingLocked(uid);
    giveMutex();
    return value;
}

// ── Settlement from the NFC write result (#329) ─────────────

void DeductionManager::handleWriteResult(uint32_t request_id, bool success) {
#ifndef NATIVE_TEST
    if (request_id == 0) return;  // legacy/unowned result
    // This is the ONE definitive result event for this write — a timeout drop
    // would strand the claim (NVS pending vs. completed physical write). Wait
    // unbounded; no lock holder does network I/O, so the wait is NVS-bound.
    if (!takeMutexForever()) {
        Serial.println("DeductionManager: handleWriteResult mutex missing — settlement skipped (fail closed)");
        return;
    }
    float snapshot = 0.0f;
    if (tracker_.resolve(request_id, success, &snapshot)) {
        if (success) {
            Serial.printf("DeductionManager: Write %u verified — settling %.1fg for %s\n",
                          request_id, snapshot, tracker_.lastSettledUid());
            settleSnapshotLocked(tracker_.lastSettledUid(), snapshot);
        } else {
            Serial.printf("DeductionManager: Write %u failed — %.1fg stays pending for next scan\n",
                          request_id, snapshot);
        }
    }
    // Unknown/stale request ids: no-op (non-deduction writes share this event).
    giveMutex();
#endif
}

// ── Apply logic ─────────────────────────────────────────────

// Synchronous settlement of the active claim (Spoolman-direct or terminal
// drop). Call with the mutex held. success=true subtracts the snapshot from
// the latest NVS total; success=false leaves NVS untouched for retry.
void DeductionManager::settleClaim(const char* uid, bool success) {
    float snapshot = 0.0f;
    if (tracker_.resolveImmediate(uid, success, &snapshot) && success) {
        settleSnapshotLocked(uid, snapshot);
    }
}

float DeductionManager::applyOpenPrintTag(const char* uid, float pending,
                                          bool* enqueued, uint32_t* requestIdOut) {
    *enqueued = false;
#ifndef NATIVE_TEST
    CurrentSpoolState state;
    if (!NFCManager::getInstance().getCurrentSpoolState(state)) {
        tracker_.abortUid(uid);
        return 0.0f;
    }
    if (!state.tag_data_valid) {
        tracker_.abortUid(uid);
        return 0.0f;
    }

    float fullWeight = 0.0f, consumed = 0.0f;
    opt_get_actual_full_weight(&state.tag_data, &fullWeight);
    opt_get_consumed_weight(&state.tag_data, &consumed);
    float remaining = fullWeight - consumed;
    if (remaining < 0) remaining = 0;

    // Clamp: don't deduct more than what's on the tag
    float deduction = (pending > remaining) ? remaining : pending;

    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = NFCManager::getInstance().generateRequestId();
    req.type = NFCWriteType::REMOVE_WEIGHT;
    req.data.grams_to_remove = deduction;
    strncpy(req.expected_spool_id, uid, sizeof(req.expected_spool_id) - 1);

    tracker_.bind(uid, req.request_id);
    if (!NFCManager::getInstance().enqueueWrite(req)) {
        tracker_.abort(req.request_id);
        Serial.printf("DeductionManager: Write queue full — deduction kept in NVS for retry\n");
        return 0.0f;
    }

    // NVS stays intact — the claim settles in handleWriteResult() once the
    // write's SPOOL_UPDATED result reports verified success (#329).
    *enqueued = true;
    *requestIdOut = req.request_id;
    Serial.printf("DeductionManager: Queued %.1fg deduction to OpenPrintTag %s (%.1fg remaining, awaiting write result)\n",
                  deduction, uid, remaining - deduction);
    return deduction;
#else
    (void)uid; (void)pending; (void)requestIdOut;
    return 0.0f;
#endif
}

float DeductionManager::applyOpenTag3D(const char* uid, float pending,
                                       bool* enqueued, uint32_t* requestIdOut) {
    *enqueued = false;
#ifndef NATIVE_TEST
    opentag3d_t ot3d;
    if (!NFCManager::getInstance().getLastOpenTag3DData(ot3d)) {
        Serial.printf("DeductionManager: No cached OpenTag3D data for %s\n", uid);
        tracker_.abortUid(uid);  // read glitch — retry next scan
        return 0.0f;
    }

    // A v2 tag without a measured weight must not be written: v2 defines the
    // target weight field as the NOMINAL spool size,
    // and deducting from it would corrupt the tag's identity. The claim has
    // already been converted to Immediate by the caller; the Spoolman/terminal
    // settle runs OUTSIDE the manager mutex in applyInternal.

    if (opentag3d_major(ot3d.tag_version) >= 2 && ot3d.measured_filament_weight_g == 0) {
        Serial.printf("DeductionManager: OpenTag3D %s (v%u) — v2 tag has no measured weight (target weight is nominal); tag left untouched\n",
                      uid, ot3d.tag_version);
        tracker_.releaseToImmediate(uid);  // caller settles outside the lock
        return -1.0f;  // synchronous settle required
    }

    // Use measured weight if available, otherwise target weight
    float remaining = (ot3d.measured_filament_weight_g > 0)
                      ? ot3d.measured_filament_weight_g
                      : (float)ot3d.target_weight_g;

    float deduction = (pending > remaining) ? remaining : pending;

    // Subtract from the weight field the tag uses (round to avoid truncation loss)
    opentag3d_patch_t patch = {};
    if (ot3d.measured_filament_weight_g > 0) {
        int newMeasured = (int)lroundf(ot3d.measured_filament_weight_g - deduction);
        // Floor at 1 g: measured==0 reads as "never measured" (nominal) on the
        // next scan, which would freeze the Spoolman weight sync for this tag.
        patch.present = OT3D_PATCH_MEASURED_WEIGHT;
        patch.values.measured_filament_weight_g = (newMeasured > 0) ? (uint16_t)newMeasured : 1;
    } else {
        int newTarget = (int)lroundf(ot3d.target_weight_g - deduction);
        patch.present = OT3D_PATCH_TARGET_WEIGHT;
        patch.values.target_weight_g = (newTarget > 0) ? (uint16_t)newTarget : 0;
    }

    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = NFCManager::getInstance().generateRequestId();
    req.type = NFCWriteType::WRITE_OPENTAG3D;
    strncpy(req.expected_spool_id, uid, sizeof(req.expected_spool_id) - 1);

    tracker_.bind(uid, req.request_id);
    if (!NFCManager::getInstance().enqueueOpenTag3DPatch(req, patch)) {
        tracker_.abort(req.request_id);
        Serial.printf("DeductionManager: Write queue full — deduction kept in NVS for retry\n");
        return 0.0f;
    }

    // NVS stays intact until the write result settles the claim (#329).
    *enqueued = true;
    *requestIdOut = req.request_id;
    Serial.printf("DeductionManager: Queued %.1fg deduction to OpenTag3D %s (%.1fg remaining, awaiting write result)\n",
                  deduction, uid, remaining - deduction);
    return deduction;
#else
    (void)uid; (void)pending; (void)requestIdOut;
    return 0.0f;
#endif
}

// Shared settle for synchronous outcomes (Spoolman-direct apply, or a tag
// format the writer must not touch). Runs with NO manager lock held — the
// Spoolman HTTP can take seconds. Claim must already be Immediate state.
float DeductionManager::spoolmanDeductOutsideLock(const char* uid, float pending, bool* accepted) {
    *accepted = false;
#ifndef NATIVE_TEST
    if (!SpoolmanManager::getInstance().isConfigured()) return -1.0f;  // unavailable marker
    bool ok = false;
    float deducted = SpoolmanManager::getInstance().deductFromSpoolman(uid, pending, &ok);
    *accepted = ok;  // ok includes a 0 g deduction on an already-empty spool
    return deducted;
#else
    return 0.0f;
#endif
}

// Shared apply core. preferTag=true writes the presented tag when its kind
// accepts weight writes; preferTag=false (or a non-writable kind) goes
// straight to the synchronous Spoolman path. Callers must NOT hold the
// manager mutex. Returns grams queued/applied — 0 also means "already in
// flight" or "claim table full" (fail closed, NVS untouched).
//
// Lock discipline (#329 review): the manager mutex covers NVS I/O and tracker
// transitions ONLY. Spoolman HTTP (which holds the HTTP mutex for seconds)
// always runs with the manager mutex released, so storePending()/
// handleWriteResult() can never be starved into a silent drop.
float DeductionManager::applyInternal(const char* uid, TagKind kind, bool preferTag) {
#ifndef NATIVE_TEST
    if (mutex_ == nullptr) {
        Serial.println("DeductionManager: not initialized (begin() failed?) — apply skipped");
        return 0.0f;
    }
    bool tagWritable = preferTag &&
                       (kind == TagKind::OpenPrintTag || kind == TagKind::OpenTag3D);
    if (tagWritable) {
        // Tag path is lock-light (cached data + queue send), so a 200 ms
        // bounded wait keeps a stalled NVS apply from blocking the caller;
        // timeout fails closed with NVS untouched.
        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) != pdTRUE) {
            Serial.println("DeductionManager: apply mutex timeout — skipped (fail closed)");
            return 0.0f;
        }
    } else {
        // Synchronous path calls Spoolman (which can hold the HTTP mutex for
        // seconds) OUTSIDE this lock, but the caller still must not drop the
        // claim — wait unbounded; the lock itself is only ever held over NVS
        // I/O now (#329 review).
        if (!takeMutexForever()) {
            Serial.println("DeductionManager: apply mutex missing — skipped (fail closed)");
            return 0.0f;
        }
    }

    float pending = readPendingLocked(uid);
    if (pending <= 0.0f) {
        giveMutex();
        return 0.0f;
    }

    // Claim before any side effect. While a claim is active a second apply
    // for the same UID is rejected — this closes the double-deduction race
    // between the SPOOL_DETECTED handler and the MQTT deduct handler.
    if (!tracker_.claim(uid, pending,
                        tagWritable ? DeductionTracker::State::Unbound
                                    : DeductionTracker::State::Immediate)) {
        giveMutex();
        Serial.printf("DeductionManager: %.1fg for %s already in flight — duplicate apply skipped\n",
                      pending, uid);
        return 0.0f;
    }

    float applied = 0.0f;
    bool enqueued = false;
    uint32_t requestId = 0;
    bool synchronousSettle = !tagWritable;

    if (tagWritable) {
        switch (kind) {
            case TagKind::OpenPrintTag:
                applied = applyOpenPrintTag(uid, pending, &enqueued, &requestId);
                break;
            case TagKind::OpenTag3D:
                // Returns -1 when the tag format must not be touched: the
                // claim was converted to Immediate and the Spoolman settle
                // runs unlocked below.
                applied = applyOpenTag3D(uid, pending, &enqueued, &requestId);
                if (applied < 0.0f) {
                    applied = 0.0f;
                    synchronousSettle = true;
                }
                break;
            default:
                // Unreachable: non-writable kinds take the synchronous path.
                settleClaim(uid, false);
                break;
        }
    }

    giveMutex();

    if (synchronousSettle) {
        // Network I/O with NO manager lock held: storePending() and
        // handleWriteResult() stay responsive for the whole Spoolman call.
        bool accepted = false;
        float deducted = spoolmanDeductOutsideLock(uid, pending, &accepted);
        if (accepted) {
            // success includes a 0 g deduction on an already-empty spool
            if (!takeMutexForever()) return 0.0f;
            settleClaim(uid, true);
            giveMutex();
            applied = deducted;  // what Spoolman actually accepted (may clamp)
            return applied;
        }
        if (deducted < 0.0f) {
            if (!takeMutexForever()) return 0.0f;
            if (deductionConsumesWithoutSpoolman(preferTag)) {
                // A presented, non-writable tag has no future local target;
                // preserve the existing terminal-drop policy.
                Serial.printf("DeductionManager: no writable tag target and Spoolman not configured — clearing %.1fg pending for %s\n",
                              pending, uid);
                LogBuffer::getInstance().logPrintf("Deduction %.1fg dropped: %s has no weight writes, no Spoolman\n",
                                                   pending, uid);
                settleClaim(uid, true);
            } else {
                // The tag is merely absent. Keep the snapshot durable so a
                // later physical scan can apply the pending deduction.
                Serial.printf("DeductionManager: Spoolman not configured — %.1fg for %s stays pending for a later tag scan\n",
                              pending, uid);
                settleClaim(uid, false);
            }
            giveMutex();
        } else {
            if (!takeMutexForever()) return 0.0f;
            settleClaim(uid, false);  // transport failure — keep pending, retry next apply
            giveMutex();
        }
        return 0.0f;
    }

    return applied;
#else
    (void)uid; (void)kind; (void)preferTag;
    return 0.0f;
#endif
}

float DeductionManager::applyIfPending(const char* uid, TagKind kind) {
    return applyInternal(uid, kind, /*preferTag=*/true);
}

// Tag not on the scanner: settle via Spoolman directly. If Spoolman is
// unconfigured the pending amount stays durable for a later tag scan.
float DeductionManager::applyViaSpoolmanIfPending(const char* uid) {
    return applyInternal(uid, TagKind::Unsupported, /*preferTag=*/false);
}
