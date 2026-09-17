#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Compact picker record. Field sizes cover the Spoolman data the pickers and
// fillFromSpoolman actually read; longer strings are truncated at refresh.
struct CachedSpool {
    int32_t id;
    float remaining_g;
    float initial_g;       // filament.weight
    float density;
    float diameter;
    uint16_t extruder_temp;  // settings_extruder_temp
    uint16_t bed_temp;       // settings_bed_temp
    char vendor[24];
    char material[16];
    char name[32];
    char color_hex[10];      // RRGGBB or RRGGBBAA + NUL
};

// Appends src to out as JSON string content (no quotes), escaping ", \ and
// control chars. Returns chars written, or (size_t)-1 on overflow.
inline size_t spoolCacheEscape(const char* src, char* out, size_t outSize) {
    size_t w = 0;
    for (const char* p = src; *p; p++) {
        char c = *p;
        const char* rep = nullptr;
        char buf[8];
        if (c == '"') rep = "\\\"";
        else if (c == '\\') rep = "\\\\";
        else if ((unsigned char)c < 0x20) { snprintf(buf, sizeof(buf), "\\u%04x", c); rep = buf; }
        if (rep) {
            size_t rl = strlen(rep);
            if (w + rl >= outSize) return (size_t)-1;
            memcpy(out + w, rep, rl); w += rl;
        } else {
            if (w + 1 >= outSize) return (size_t)-1;
            out[w++] = c;
        }
    }
    if (w >= outSize) return (size_t)-1;
    out[w] = '\0';
    return w;
}

// Emits one record as a Spoolman-shaped JSON object. Returns bytes written,
// or 0 on overflow (out[0] set to NUL).
inline size_t spoolCacheEmitJson(const CachedSpool& s, char* out, size_t outSize) {
    char vendor[2 * sizeof(s.vendor)];
    char material[2 * sizeof(s.material)];
    char name[2 * sizeof(s.name)];
    char colorHex[2 * sizeof(s.color_hex)];
    if (spoolCacheEscape(s.vendor, vendor, sizeof(vendor)) == (size_t)-1) vendor[0] = '\0';
    if (spoolCacheEscape(s.material, material, sizeof(material)) == (size_t)-1) material[0] = '\0';
    if (spoolCacheEscape(s.name, name, sizeof(name)) == (size_t)-1) name[0] = '\0';
    if (spoolCacheEscape(s.color_hex, colorHex, sizeof(colorHex)) == (size_t)-1) colorHex[0] = '\0';

    int w = snprintf(out, outSize,
        "{\"id\":%ld,\"remaining_weight\":%.4g,\"filament\":{\"name\":\"%s\","
        "\"material\":\"%s\",\"color_hex\":\"%s\",\"weight\":%.4g,\"density\":%.4g,"
        "\"diameter\":%.4g,\"settings_extruder_temp\":%u,\"settings_bed_temp\":%u,"
        "\"vendor\":{\"name\":\"%s\"}}}",
        (long)s.id, s.remaining_g, name, material, colorHex,
        s.initial_g, s.density, s.diameter,
        (unsigned)s.extruder_temp, (unsigned)s.bed_temp, vendor);
    if (w < 0 || (size_t)w >= outSize) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)w;
}
