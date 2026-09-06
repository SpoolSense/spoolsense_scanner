// DeductionManager.cpp — Pending filament deduction storage (NVS) and tag write-back.
// Middleware sends usage deductions via MQTT after prints. Scanner stores them in NVS
// and applies to writable tags (OpenPrintTag, OpenTag3D) on the next scan.

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

void DeductionManager::makeNvsKey(const char* uid, char* out, size_t outSize) {
    // NVS key limit: 15 usable chars (16 including null terminator)
    size_t maxLen = (outSize - 1 < 15) ? outSize - 1 : 15;
    strncpy(out, uid, maxLen);
    out[maxLen] = '\0';
}

void DeductionManager::storePending(const char* uid, float grams) {
#ifndef NATIVE_TEST
    char key[16];
    makeNvsKey(uid, key, sizeof(key));

    Preferences prefs;
    prefs.begin("deductions", false);  // RW mode
    float current = prefs.getFloat(key, 0.0f);
    float total = current + grams;
    prefs.putFloat(key, total);
    prefs.end();

    Serial.printf("DeductionManager: Stored %.1fg pending for %s (total: %.1fg)\n", grams, uid, total);
#endif
}

float DeductionManager::getPending(const char* uid) {
#ifndef NATIVE_TEST
    char key[16];
    makeNvsKey(uid, key, sizeof(key));

    Preferences prefs;
    prefs.begin("deductions", true);  // RO mode
    float value = prefs.getFloat(key, 0.0f);
    prefs.end();
    return value;
#else
    return 0.0f;
#endif
}

void DeductionManager::clearPending(const char* uid) {
#ifndef NATIVE_TEST
    char key[16];
    makeNvsKey(uid, key, sizeof(key));

    Preferences prefs;
    prefs.begin("deductions", false);
    prefs.remove(key);
    prefs.end();

    Serial.printf("DeductionManager: Cleared pending deduction for %s\n", uid);
#endif
}

// ── Apply logic ─────────────────────────────────────────────

static float applyOpenPrintTag(const char* uid, float pending) {
#ifndef NATIVE_TEST
    CurrentSpoolState state;
    if (!NFCManager::getInstance().getCurrentSpoolState(state)) return 0.0f;
    if (!state.tag_data_valid) return 0.0f;

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

    if (!NFCManager::getInstance().enqueueWrite(req)) {
        Serial.printf("DeductionManager: Write queue full — deduction kept in NVS for retry\n");
        return 0.0f;  // don't clear NVS, retry on next scan
    }

    DeductionManager::getInstance().clearPending(uid);
    Serial.printf("DeductionManager: Applied %.1fg deduction to OpenPrintTag %s (%.1fg remaining)\n",
                  deduction, uid, remaining - deduction);
    return deduction;
#else
    return 0.0f;
#endif
}

static float applyOpenTag3D(const char* uid, float pending) {
#ifndef NATIVE_TEST
    opentag3d_t ot3d;
    if (!NFCManager::getInstance().getLastOpenTag3DData(ot3d)) {
        Serial.printf("DeductionManager: No cached OpenTag3D data for %s\n", uid);
        return 0.0f;
    }

    // Cases where the tag itself must not be written: a newer minor format
    // (re-encoding would zero fields it defines), or a v2 tag without a
    // measured weight — v2 defines the weight field as the NOMINAL spool size,
    // and deducting from it would corrupt the tag's identity. Route the grams
    // to Spoolman when configured; with no Spoolman the deduction is dropped
    // (loudly) rather than retried forever.
    const char* skipReason = NULL;
    if (!opentag3d_can_encode(ot3d.tag_version)) {
        skipReason = "newer minor format";
    } else if (opentag3d_major(ot3d.tag_version) >= 2 && ot3d.measured_filament_weight_g == 0) {
        skipReason = "v2 tag has no measured weight (target weight is nominal)";
    }
    if (skipReason) {
        Serial.printf("DeductionManager: OpenTag3D %s (v%u) — %s; tag left untouched\n",
                      uid, ot3d.tag_version, skipReason);
        if (SpoolmanManager::getInstance().isConfigured()) {
            bool spOk = false;
            float spDeducted = SpoolmanManager::getInstance().deductFromSpoolman(uid, pending, &spOk);
            if (spOk) {  // success includes a 0 g deduction on an already-empty spool
                DeductionManager::getInstance().clearPending(uid);
                return spDeducted;
            }
            return 0.0f;  // Spoolman may be down — keep pending and retry next scan
        }
        // No writable target anywhere — terminal, mirroring the non-writable-kind
        // branch: an every-scan retry would loop forever and only log to serial.
        LogBuffer::getInstance().logPrintf("Deduction %.1fg dropped: OpenTag3D %s %s, no Spoolman\n",
                                           pending, uid, skipReason);
        Serial.printf("DeductionManager: %s and Spoolman not configured — clearing %.1fg pending\n",
                      skipReason, pending);
        DeductionManager::getInstance().clearPending(uid);
        return 0.0f;
    }

    // Use measured weight if available, otherwise target weight
    float remaining = (ot3d.measured_filament_weight_g > 0)
                      ? ot3d.measured_filament_weight_g
                      : (float)ot3d.target_weight_g;

    float deduction = (pending > remaining) ? remaining : pending;

    // Subtract from the weight field the tag uses (round to avoid truncation loss)
    if (ot3d.measured_filament_weight_g > 0) {
        int newMeasured = (int)lroundf(ot3d.measured_filament_weight_g - deduction);
        ot3d.measured_filament_weight_g = (newMeasured > 0) ? (uint16_t)newMeasured : 0;
    } else {
        int newTarget = (int)lroundf(ot3d.target_weight_g - deduction);
        ot3d.target_weight_g = (newTarget > 0) ? (uint16_t)newTarget : 0;
    }

    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = NFCManager::getInstance().generateRequestId();
    req.type = NFCWriteType::WRITE_OPENTAG3D;
    strncpy(req.expected_spool_id, uid, sizeof(req.expected_spool_id) - 1);

    if (!NFCManager::getInstance().enqueueRawWrite(req, (const uint8_t*)&ot3d, sizeof(ot3d))) {
        Serial.printf("DeductionManager: Write queue full — deduction kept in NVS for retry\n");
        return 0.0f;
    }

    DeductionManager::getInstance().clearPending(uid);
    Serial.printf("DeductionManager: Applied %.1fg deduction to OpenTag3D %s (%.1fg remaining)\n",
                  deduction, uid, remaining - deduction);
    return deduction;
#else
    return 0.0f;
#endif
}

float DeductionManager::applyIfPending(const char* uid, TagKind kind) {
    float pending = getPending(uid);
    if (pending <= 0.0f) return 0.0f;

    float deducted = 0.0f;
    switch (kind) {
        case TagKind::OpenPrintTag:
            deducted = applyOpenPrintTag(uid, pending);
            break;
        case TagKind::OpenTag3D:
            deducted = applyOpenTag3D(uid, pending);
            break;
        default:
            // Tag can't accept weight writes — try Spoolman direct if configured
            if (SpoolmanManager::getInstance().isConfigured()) {
                bool spOk = false;
                float spDeducted = SpoolmanManager::getInstance().deductFromSpoolman(uid, pending, &spOk);
                if (spOk) {  // success includes a 0 g deduction on an already-empty spool
                    clearPending(uid);
                    deducted = spDeducted;
                }
                // On transport failure, keep in NVS for retry on next scan
            } else {
                Serial.printf("DeductionManager: Tag type %d has no weight writes and Spoolman not configured — clearing\n", (int)kind);
                clearPending(uid);
            }
            break;
    }

    // Clear only happens inside apply functions after successful enqueue,
    // or above for non-writable tags. Don't clear here — if enqueue failed,
    // deduction stays in NVS for next scan attempt.
    return deducted;
}
