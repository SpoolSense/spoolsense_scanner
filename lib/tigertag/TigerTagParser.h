#ifndef TIGER_TAG_PARSER_H
#define TIGER_TAG_PARSER_H

#include <cstdint>

// Known TigerTag version IDs (page 4, first 4 bytes)
// These come from id_version.json — the app may write newer IDs not yet in the public database.
#define TIGERTAG_V10            0x5BF04674  // TigerTag V1.0 (from public DB)
#define TIGERTAG_V10_MAKER      0x5BF59264  // TigerTag V1.0 Maker (observed from app)
#define TIGERTAG_INIT_V10       0x6C2B2DF1  // TigerTag Init V1.0
#define TIGERTAG_PLUS_V10       0xBC0A5927  // TigerTag+ V1.0

// Type field values (byte 12 in TigerTag layout; byte 13 is diameter)
#define TIGERTAG_TYPE_FILAMENT  0x8E
#define TIGERTAG_TYPE_RESIN     0xAD

// Parsed TigerTag data
struct TigerTagData {
    bool valid;

    uint16_t material_id;
    uint16_t brand_id;
    uint8_t  diameter_id;
    uint8_t  aspect1_id;
    uint8_t  aspect2_id;
    uint8_t  type_id;          // 0x8E = Filament, 0xAD = Resin
    uint8_t  unit_id;

    uint8_t  color_r, color_g, color_b, color_a;  // Primary RGBA
    uint16_t weight_g;         // Weight in grams (from 3-byte measure field)

    uint16_t nozzle_temp_min;
    uint16_t nozzle_temp_max;
    uint8_t  bed_temp_min;
    uint8_t  bed_temp_max;
    uint8_t  dry_temp;
    uint8_t  dry_time_hours;

    uint16_t transmission_distance_x10;  // Value * 10 for HueForge

    // Resolved strings from lookup tables
    const char* material_name;   // e.g. "PLA", "ABS"
    const char* brand_name;      // e.g. "Polymaker", "Sunlu"
    const char* aspect1_name;    // e.g. "Silk", "Matt"
    const char* aspect2_name;
    float diameter_mm;           // e.g. 1.75, 2.85
};

// Check if raw page data (starting from page 4) contains TigerTag magic.
// data must be at least 4 bytes.
bool tigerTagCheckMagic(const uint8_t* data, uint16_t dataLen);

// Strict identity: the first four bytes are a published TigerTag version ID.
// tigerTagCheckMagic also accepts a layout-only match, which is right for
// reading but lets another format's raw bytes pass — a writer that must not
// overwrite a foreign tag unasked uses this one.
bool tigerTagHasKnownVersion(const uint8_t* data, uint16_t dataLen);

// Parse TigerTag data from raw page bytes (pages 4-23, 80 bytes).
// Returns a TigerTagData struct with resolved lookup names.
TigerTagData tigerTagParse(const uint8_t* data, uint16_t dataLen);

// Lookup helpers (return "Unknown" for unrecognized IDs)
const char* tigerTagMaterialName(uint16_t id);
const char* tigerTagBrandName(uint16_t id);
const char* tigerTagAspectName(uint8_t id);
const char* tigerTagUnitName(uint8_t id);
float tigerTagDiameterMm(uint8_t id);

#endif // TIGER_TAG_PARSER_H
