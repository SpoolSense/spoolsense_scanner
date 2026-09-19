#ifndef NFC_WRITE_TYPES_H
#define NFC_WRITE_TYPES_H

#include <cstdint>

// Write request types - shared between production and test code
// These have no external dependencies

enum class NFCWriteType : uint8_t {
    REMOVE_WEIGHT,        // Subtract grams from spool
    CHANGE_COLOR,         // Set new primary color
    CHANGE_FILAMENT_TYPE, // Set new material type
    SET_CONSUMED_WEIGHT,  // Set absolute consumed weight
    SET_BRAND_NAME,       // Set manufacturer name
    FORMAT_NEW,           // Format a blank tag with defaults
    WRITE_SPOOLMAN_ID,    // Write Spoolman spool ID to aux region
    WRITE_RAW_TAG,        // Write raw binary data to entire tag
    SET_INITIAL_WEIGHT,   // Set initial/full weight of spool
    SET_DENSITY,          // Set filament density (g/cm³)
    SET_DIAMETER,         // Set filament diameter (mm)
    SET_MATERIAL_NAME,    // Set custom material name string
    SET_MIN_PRINT_TEMP,   // Set minimum print temperature (°C)
    SET_MAX_PRINT_TEMP,   // Set maximum print temperature (°C)
    SET_PREHEAT_TEMP,     // Set preheat temperature (°C)
    SET_MIN_BED_TEMP,     // Set minimum bed temperature (°C)
    SET_MAX_BED_TEMP,     // Set maximum bed temperature (°C)
    WRITE_TIGERTAG,       // Write 40-byte TigerTag binary to NTAG pages 4-13
    WRITE_OPENTAG3D,      // Write NDEF-wrapped OpenTag3D payload to NTAG pages
    WRITE_OPENSPOOL,      // Write NDEF-wrapped OpenSpool JSON payload to NTAG pages
    WRITE_ATOMIC,         // Atomic single-pass write: build complete CBOR map, write once
};

struct NFCWriteRequest {
    uint32_t request_id;         // Unique ID for deduplication
    NFCWriteType type;
    uint8_t suppress_sync;       // If 1, don't trigger Spoolman sync (used for batched writes like Mode B)
    uint8_t wants_outcome;       // 1 = a caller will poll /api/status for this request's result
    uint32_t enqueued_ms;       // millis() when the request entered the queue
    char expected_spool_id[17];  // Only write if this spool is present (empty = any)
    union {
        float grams_to_remove;
        uint8_t new_color[4];       // RGBA
        uint8_t new_material_type;
        float consumed_weight;      // Absolute consumed weight in grams; reused for SET_INITIAL_WEIGHT
        float float_value;          // Generic float: density (g/cm³), diameter (mm)
        int16_t temp_celsius;       // Temperature in Celsius: print/bed/preheat temps
        char brand_name[33];        // Manufacturer name
        char material_name[33];     // Custom material name string
        int32_t spoolman_id;        // Spoolman spool ID for tag write-back
        uint8_t tigertag_data[40];  // TigerTag binary payload (pages 4-13)
    } data;
};

#endif // NFC_WRITE_TYPES_H
