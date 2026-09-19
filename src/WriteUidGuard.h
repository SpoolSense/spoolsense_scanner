#pragma once

#include <cstring>

// Decide whether a queued write may proceed against the tag currently selected
// on the reader.
//
//   expectedUid     — UID the write was bound to when it was enqueued.
//                     Empty or null means "unbound": legacy behaviour, accept
//                     whatever tag is present.
//   selectedUidHex  — UID hex string of the tag selected on the reader during
//                     this scan cycle (uppercase, no separators).
//
// Bound writes fail closed: if no tag is selected we reject rather than guess.
// The comparison is exact — matching on a prefix is what allowed #276, where
// the scan-cooldown dedup let a same-batch tag sharing three UID bytes pass a
// check that had been made against the previous tag.
inline bool writeUidMatches(const char* expectedUid, const char* selectedUidHex) {
    if (expectedUid == nullptr || expectedUid[0] == '\0') {
        return true;   // unbound write — accept whatever is present
    }
    if (selectedUidHex == nullptr || selectedUidHex[0] == '\0') {
        return false;  // bound write with no selected tag — fail closed
    }
    return strcmp(expectedUid, selectedUidHex) == 0;
}

// True only when the tag's write-capacity size was measured for the exact
// tag now selected on the reader. There is no "unbound" case here — unlike
// writeUidMatches, an unmeasured or stale size is never safe to trust, so
// both sides must be present and match exactly (#316: the scan-cooldown
// dedup can leave a size measured for the previous tag in place while a
// different, smaller tag is selected).
inline bool capacityBoundToSelected(const char* capacityUid, const char* selectedUid) {
    if (capacityUid == nullptr || capacityUid[0] == '\0') {
        return false;
    }
    if (selectedUid == nullptr || selectedUid[0] == '\0') {
        return false;
    }
    return strcmp(capacityUid, selectedUid) == 0;
}

// Field-update and atomic writes (REMOVE_WEIGHT, CHANGE_COLOR, WRITE_ATOMIC)
// build their payload from currentSpool's CBOR before writing it to the
// selected tag, so a bound write must ALSO confirm that currentSpool describes
// the bound tag — the cooldown dedup can leave it holding the previous tag's
// data even when the right tag is selected, and writing that data would
// corrupt the new tag just as surely as writing to the wrong one.
//
//   currentSpoolUid — UID hex currentSpool's tag data was read from.
inline bool fieldUpdateWriteAllowed(const char* expectedUid,
                                    const char* selectedUidHex,
                                    const char* currentSpoolUid) {
    if (!writeUidMatches(expectedUid, selectedUidHex)) {
        return false;
    }
    if (expectedUid == nullptr || expectedUid[0] == '\0') {
        return true;   // unbound write — legacy accept-any behaviour
    }
    return currentSpoolUid != nullptr && strcmp(currentSpoolUid, expectedUid) == 0;
}

// OpenPrintTag lives on ISO15693 tags only, and their UID is always 8 bytes
// (a Type 2 tag's is 4 or 7). The OpenPrintTag writers reach the tag through a
// HAL that addresses a Type 2 tag from page 0 — its UID, lock and CC pages —
// so they must never run against one (#316).
inline bool uidHexIsIso15693(const char* uidHex) {
    return uidHex != nullptr && strlen(uidHex) == 16;
}
