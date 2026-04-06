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
    char uid_hex[17];   // null-terminated UID hex string (up to 8 bytes = 16 hex chars)
    bool present;
    bool tag_data_valid;
};

struct CurrentSpoolState {
    bool present;
    bool blank_tag_present;  // Deprecated: use kind == TagKind::BlankTag instead
    TagKind kind;
    NtagVariant variant;
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
