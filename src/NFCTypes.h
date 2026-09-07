#ifndef NFC_TYPES_H
#define NFC_TYPES_H

#include <cstdint>
#include <ctime>
#include "NFCWriteTypes.h"
#include "openprinttag_lib.h"

enum class TagProtocol : uint8_t {
    ISO15693,
    ISO14443A,
    Unknown
};

enum class NtagVariant : uint8_t {
    Unknown = 0,
    NTAG213,          // 45 pages, 144 usable bytes
    NTAG215,          // 135 pages, 504 usable bytes
    NTAG216,          // 231 pages, 888 usable bytes
    UltralightEV1_48, // 20 pages, 48 usable bytes
    UltralightEV1_128 // 41 pages, 128 usable bytes
};

inline uint16_t ntagUsablePages(NtagVariant v) {
    switch (v) {
        case NtagVariant::NTAG213:          return 45;
        case NtagVariant::NTAG215:          return 135;
        case NtagVariant::NTAG216:          return 231;
        case NtagVariant::UltralightEV1_48: return 20;
        case NtagVariant::UltralightEV1_128:return 41;
        default:                            return 0;
    }
}

// Exclusive end of USER memory (first dynamic-lock/config page). Differs from
// ntagUsablePages, which counts every page on the die — payload reads must
// stop here or a malformed length pulls lock/config bytes into the payload.
inline uint16_t ntagUserMemoryEnd(NtagVariant v) {
    switch (v) {
        case NtagVariant::NTAG213:          return 40;   // user 4-39
        case NtagVariant::NTAG215:          return 130;  // user 4-129
        case NtagVariant::NTAG216:          return 226;  // user 4-225
        case NtagVariant::UltralightEV1_48: return 16;   // user 4-15
        case NtagVariant::UltralightEV1_128:return 36;   // user 4-35
        default:                            return 0;    // unknown — no clamp
    }
}

// NFC Forum Type 2 Capability Container (page 3): byte0 magic 0xE1,
// byte2 = data-area size / 8. Returns the EXCLUSIVE end page of user memory
// (4 + bytes/4), or 0 when the CC is absent or implausible. The declared size
// is trusted exactly — never floored — because clone chips declare honestly
// and padding past the die end aborts the whole read/write.
inline uint16_t ccUserMemoryEnd(const uint8_t* ccPage) {
    if (ccPage == nullptr || ccPage[0] != 0xE1) return 0;
    uint16_t endPage = 4 + ((uint16_t)ccPage[2] * 8) / 4;
    if (endPage < 12 || endPage > 231) return 0;  // below any real Type 2 chip, or past the largest die
    return endPage;
}

// Effective user-memory end: identified variant wins; otherwise the CC-declared
// size captured at classify time; 0 when neither is known.
inline uint16_t effectiveUserMemoryEnd(NtagVariant v, uint16_t ccEnd) {
    uint16_t e = ntagUserMemoryEnd(v);
    return e ? e : ccEnd;
}

inline const char* ntagVariantName(NtagVariant v) {
    switch (v) {
        case NtagVariant::NTAG213:          return "NTAG213";
        case NtagVariant::NTAG215:          return "NTAG215";
        case NtagVariant::NTAG216:          return "NTAG216";
        case NtagVariant::UltralightEV1_48: return "Ultralight EV1 48B";
        case NtagVariant::UltralightEV1_128:return "Ultralight EV1 128B";
        default:                            return "Unknown";
    }
}

enum class TagKind : uint8_t {
    OpenPrintTag,   // ordinal 0 — memset to zero produces safe default
    GenericUidTag,  // UID-only tag (e.g. NTAG215) — ISO14443A
    OpenTag3D,      // OpenTag3D format — ISO14443A
    TigerTag,       // TigerTag format — ISO14443A (NTAG213/215/216)
    BambuTag,       // Bambu Lab spool — MIFARE Classic (encrypted, UID-only)
    OpenSpoolTag,   // OpenSpool format — ISO14443A (NTAG215/216, NDEF JSON)
    BlankTag,
    Unsupported
};

struct TagScanResult {
    TagProtocol protocol;
    TagKind kind;
    NtagVariant variant;
    uint16_t cc_user_end;  // CC-declared user-memory end page; 0 unless variant was Unknown and page 3 held a valid CC
    char uid_hex[17];
    bool present;
    bool tag_data_valid;
};

struct CurrentSpoolState {
    bool present;
    bool blank_tag_present;
    TagKind kind;
    NtagVariant variant;
    uint16_t cc_user_end;  // CC-declared user-memory end page; 0 unless variant was Unknown and page 3 held a valid CC
    char spool_id[17];
    uint8_t uid[8];              // ISO15693 uses 8-byte UID
    uint8_t uid_length;
    opt_tag_t tag_data;          // Cached openprinttag data
    bool tag_data_valid;
};

// Recent spool entry for history tracking (RAM only)
struct RecentSpoolEntry {
    char spool_id[17];
    uint8_t material_type;
    uint8_t color[4];            // RGBA
    char manufacturer[33];
    int grams_remaining;
    time_t last_seen;  // Unix timestamp (seconds)
    bool valid;
    bool synced_to_spoolman;
    int32_t spoolman_id;
};

#endif // NFC_TYPES_H
