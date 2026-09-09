#pragma once

// TagStateJson — shared MQTT tag/state JSON builder using ArduinoJson.
// Consolidates 6 duplicate publish paths into one struct + two builders.
// Stack-allocated StaticJsonDocument<512> — no heap, ~6% of task stack.

#include <ArduinoJson.h>
#include <cstdint>

struct TagStateFields {
    char uid[17];
    bool present;
    bool tag_data_valid;
    const char* tag_format;       // "tigertag", "openprinttag", "uid_only", "unknown"
    char material_type[32];
    char material_name[32];
    char color[8];                // "#RRGGBB"
    char manufacturer[64];
    float remaining_g;
    float initial_weight_g;
    int32_t spoolman_id;
    bool blank;
    // Optional — appended only when non-zero
    int16_t min_print_temp;
    int16_t max_print_temp;
    int16_t min_bed_temp;
    int16_t max_bed_temp;
    float density;
    float diameter_mm;
    uint32_t filament_length_m;
    const char* weight_source;    // "measured" = remaining_g is a real level;
                                  // "nominal" = it is the spool's sticker size and
                                  // never changes on-tag (middleware must not
                                  // baseline deductions from it — middleware#119);
                                  // NULL = tag makes no weight claim, field omitted
};

// Build full tag state JSON. Optional temp/density/diameter fields included only when non-zero.
inline size_t buildTagStateJson(char* out, size_t outSize, const TagStateFields& f) {
    StaticJsonDocument<512> doc;

    doc["uid"] = f.uid;
    doc["present"] = f.present;
    doc["tag_data_valid"] = f.tag_data_valid;
    doc["tag_format"] = f.tag_format ? f.tag_format : "unknown";
    doc["material_type"] = f.material_type;
    doc["material_name"] = f.material_name;
    doc["color"] = f.color;
    doc["manufacturer"] = f.manufacturer;
    doc["remaining_g"] = f.remaining_g;
    doc["initial_weight_g"] = f.initial_weight_g;
    doc["spoolman_id"] = f.spoolman_id;
    doc["blank"] = f.blank;

    if (f.min_print_temp != 0) doc["min_print_temp"] = f.min_print_temp;
    if (f.max_print_temp != 0) doc["max_print_temp"] = f.max_print_temp;
    if (f.min_bed_temp != 0)   doc["min_bed_temp"] = f.min_bed_temp;
    if (f.max_bed_temp != 0)   doc["max_bed_temp"] = f.max_bed_temp;
    if (f.density > 0.0f)           doc["density"] = f.density;
    if (f.diameter_mm > 0.0f)       doc["diameter_mm"] = f.diameter_mm;
    if (f.filament_length_m > 0)    doc["filament_length_m"] = f.filament_length_m;
    if (f.weight_source)            doc["weight_source"] = f.weight_source;

    return serializeJson(doc, out, outSize);
}

// Empty/removed tag state — present=false, all fields zeroed
inline size_t buildEmptyTagStateJson(char* out, size_t outSize) {
    StaticJsonDocument<256> doc;

    doc["uid"] = "";
    doc["present"] = false;
    doc["tag_data_valid"] = false;
    doc["tag_format"] = "unknown";
    doc["material_type"] = "";
    doc["material_name"] = "";
    doc["color"] = "";
    doc["manufacturer"] = "";
    doc["remaining_g"] = 0.0f;
    doc["initial_weight_g"] = 0.0f;
    doc["spoolman_id"] = -1;
    doc["blank"] = false;

    return serializeJson(doc, out, outSize);
}
