/* Native tests for the OpenTag3D v2.000 codec + version dispatch.
 *
 * Covers: v2 nominal parse (every mapped field), version-warning/error
 * gating, v1.000 regression, legacy version-0 passthrough, and the v2.000
 * encoder (full round-trip, raw byte spot-checks, version preservation,
 * future-major rejection, TD clamp). Plain C, PASS/FAIL per check,
 * non-zero exit on any failure. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "opentag3d_lib.h"
#include "opentag3d_v2_map.h"

static int failures = 0;

#define CHECK(cond, name)                                          \
    do {                                                           \
        if (cond) {                                                \
            printf("  PASS  %s\n", name);                          \
        } else {                                                   \
            printf("  FAIL  %s\n", name);                          \
            failures++;                                            \
        }                                                          \
    } while (0)

/* ---- payload-building helpers (big-endian, mirror the decoder) ---- */
static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v & 0xFF); }
static void put_u48(uint8_t *p, uint64_t v) { for (int i = 5; i >= 0; i--) { p[i] = (uint8_t)(v & 0xFF); v >>= 8; } }
static void put_str(uint8_t *p, const char *s, size_t field_len) {
    size_t n = strlen(s);
    if (n > field_len) n = field_len;
    memcpy(p, s, n);
    if (n < field_len) memset(p + n, ' ', field_len - n);
}

/* Serial is 32 non-space chars so it survives without trimming. */
#define V2_SERIAL "2024-01-23-ABCDEFGHIJKLMNOPQRSTU"

/* ---- test A/B helper: a full, valid 224-byte v2 map ---- */
static void build_v2_nominal(uint8_t *buf) {
    memset(buf, 0, OT3D_V2_MAP_SIZE);
    put_u16(buf + OT3D_V2_OFF_TAG_VERSION, 2000);
    put_str(buf + OT3D_V2_OFF_MATERIAL, "PLA", OT3D_V2_LEN_MATERIAL);
    put_str(buf + OT3D_V2_OFF_MATERIAL_MOD, "CF", OT3D_V2_LEN_MATERIAL_MOD);
    /* manufacturer padded with trailing spaces -> must trim */
    put_str(buf + OT3D_V2_OFF_MANUFACTURER, "Example Brand", OT3D_V2_LEN_MANUFACTURER);
    put_str(buf + OT3D_V2_OFF_COLOR_NAME, "Orange", OT3D_V2_LEN_COLOR_NAME);
    memcpy(buf + OT3D_V2_OFF_COLOR_1, (uint8_t[]){255, 166, 77, 255}, 4);
    memcpy(buf + OT3D_V2_OFF_COLOR_2, (uint8_t[]){1, 2, 3, 4}, 4);
    memcpy(buf + OT3D_V2_OFF_COLOR_3, (uint8_t[]){5, 6, 7, 8}, 4);
    memcpy(buf + OT3D_V2_OFF_COLOR_4, (uint8_t[]){9, 10, 11, 12}, 4);
    put_str(buf + OT3D_V2_OFF_SERIAL, V2_SERIAL, OT3D_V2_LEN_SERIAL);
    put_str(buf + OT3D_V2_OFF_SKU, "G00-A01", OT3D_V2_LEN_SKU);
    put_u48(buf + OT3D_V2_OFF_BARCODE, 12345543210ULL);
    put_u16(buf + OT3D_V2_OFF_MFG_DATE, 2024);
    buf[OT3D_V2_OFF_MFG_DATE + 2] = 1;
    buf[OT3D_V2_OFF_MFG_DATE + 3] = 23;
    buf[OT3D_V2_OFF_MFG_TIME + 0] = 10;
    buf[OT3D_V2_OFF_MFG_TIME + 1] = 30;
    buf[OT3D_V2_OFF_MFG_TIME + 2] = 45;
    put_u16(buf + OT3D_V2_OFF_DIAMETER, 1750);
    buf[OT3D_V2_OFF_TOLERANCE] = 10;
    buf[OT3D_V2_OFF_NOZZLE_DIAMETER] = 4;
    buf[OT3D_V2_OFF_PRINT_TEMP] = 42;
    buf[OT3D_V2_OFF_MIN_PRINT_TEMP] = 38;
    buf[OT3D_V2_OFF_MAX_PRINT_TEMP] = 45;
    buf[OT3D_V2_OFF_CHAMBER_TEMP] = 12;
    buf[OT3D_V2_OFF_BED_TEMP] = 16;
    buf[OT3D_V2_OFF_MIN_BED_TEMP] = 8;
    buf[OT3D_V2_OFF_MAX_BED_TEMP] = 12;
    buf[OT3D_V2_OFF_TARGET_VSO] = 80;
    buf[OT3D_V2_OFF_MIN_VSO] = 20;
    buf[OT3D_V2_OFF_MAX_VSO] = 120;
    buf[OT3D_V2_OFF_MAX_DRY_TEMP] = 10;
    buf[OT3D_V2_OFF_DRY_TIME] = 8;
    put_u16(buf + OT3D_V2_OFF_DENSITY, 1240);
    put_u16(buf + OT3D_V2_OFF_WEIGHT, 1000);
    put_u16(buf + OT3D_V2_OFF_EMPTY_SPOOL_WEIGHT, 105);
    put_u16(buf + OT3D_V2_OFF_MEASURED_LENGTH, 336);
    put_u16(buf + OT3D_V2_OFF_MEASURED_WEIGHT, 1002);
    buf[OT3D_V2_OFF_SPOOL_CORE_DIAMETER] = 100;
    buf[OT3D_V2_OFF_TD] = 118;
    buf[OT3D_V2_OFF_MFI_TEMP] = 210;
    buf[OT3D_V2_OFF_MFI_LOAD] = 216;
    buf[OT3D_V2_OFF_MFI_VALUE] = 63;
    put_str(buf + OT3D_V2_OFF_DATA_URL, "pfil.us?i=8078-RQSR", OT3D_V2_LEN_DATA_URL);
}

/* ---- test E helper: a full 187-byte v1.000 payload ---- */
static void build_v1_nominal(uint8_t *buf, uint16_t version) {
    memset(buf, 0, OT3D_EXTENDED_MIN);
    put_u16(buf + 0x00, version);
    put_str(buf + 0x02, "PLA", 5);
    put_str(buf + 0x07, "CF", 5);
    put_str(buf + 0x1B, "3D", 16);
    put_str(buf + 0x2B, "Orange", 32);
    memcpy(buf + 0x4B, (uint8_t[]){255, 166, 77, 255}, 4);
    put_u16(buf + 0x5C, 1750);          /* diameter */
    put_u16(buf + 0x5E, 1000);          /* target weight */
    buf[0x60] = 42;                      /* print temp */
    buf[0x61] = 16;                      /* bed temp */
    put_u16(buf + 0x62, 1240);           /* density */
    put_u16(buf + 0x64, 118);            /* transmission distance (u16 in v1) */
    put_str(buf + 0x70, "pfil.us?i=8078-RQSR", 32);
    put_str(buf + 0x90, "SN-V1", 16);
    put_u16(buf + 0xA0, 2024);
    buf[0xA2] = 1; buf[0xA3] = 23;
    buf[0xA4] = 10; buf[0xA5] = 30; buf[0xA6] = 45;
    buf[0xA7] = 100;                     /* spool core diameter */
    buf[0xA8] = 210; buf[0xA9] = 216; buf[0xAA] = 63;  /* mfi */
    buf[0xAB] = 10;                      /* tolerance */
    put_u16(buf + 0xAC, 105);            /* empty spool weight */
    put_u16(buf + 0xAE, 1002);           /* measured filament weight */
    put_u16(buf + 0xB0, 336);            /* measured filament length */
    buf[0xB2] = 10; buf[0xB3] = 8;       /* max dry temp, dry time */
    buf[0xB4] = 38; buf[0xB5] = 45; buf[0xB6] = 8; buf[0xB7] = 12;
    buf[0xB8] = 20; buf[0xB9] = 120; buf[0xBA] = 80;   /* vso min/max/target */
}

/* Assert every struct member populated by the v2 mapping. */
static void check_v2_fields(const opentag3d_t *o) {
    CHECK(o->has_extended == 1, "v2: has_extended == 1");
    CHECK(strcmp(o->base_material, "PLA") == 0, "v2: base_material");
    CHECK(strcmp(o->material_modifiers, "CF") == 0, "v2: material_modifiers");
    CHECK(strcmp(o->manufacturer, "Example Brand") == 0, "v2: manufacturer (trailing-space trimmed)");
    CHECK(strcmp(o->color_name, "Orange") == 0, "v2: color_name");
    CHECK(o->color_rgba[0][0] == 255 && o->color_rgba[0][1] == 166 &&
          o->color_rgba[0][2] == 77 && o->color_rgba[0][3] == 255, "v2: color_rgba[0]");
    CHECK(o->color_rgba[1][0] == 1 && o->color_rgba[1][3] == 4, "v2: color_rgba[1]");
    CHECK(o->color_rgba[2][0] == 5 && o->color_rgba[2][3] == 8, "v2: color_rgba[2]");
    CHECK(o->color_rgba[3][0] == 9 && o->color_rgba[3][3] == 12, "v2: color_rgba[3]");
    CHECK(strcmp(o->serial_number, V2_SERIAL) == 0 && strlen(o->serial_number) == 32, "v2: serial_number (32 chars)");
    CHECK(strcmp(o->sku, "G00-A01") == 0, "v2: sku");
    CHECK(o->barcode == 12345543210ULL, "v2: barcode (u48)");
    CHECK(o->manufacture_year == 2024 && o->manufacture_month == 1 && o->manufacture_day == 23, "v2: mfg date");
    CHECK(o->manufacture_hour == 10 && o->manufacture_minute == 30 && o->manufacture_second == 45, "v2: mfg time");
    CHECK(o->diameter_um == 1750, "v2: diameter");
    CHECK(o->measured_tolerance_um == 10, "v2: tolerance (raw)");
    CHECK(o->min_nozzle_diameter == 4, "v2: min_nozzle_diameter");
    CHECK(o->print_temp_encoded == 42, "v2: print_temp");
    CHECK(o->min_print_temp_encoded == 38 && o->max_print_temp_encoded == 45, "v2: print temp range");
    CHECK(o->chamber_temp_encoded == 12, "v2: chamber_temp");
    CHECK(o->bed_temp_encoded == 16, "v2: bed_temp");
    CHECK(o->min_bed_temp_encoded == 8 && o->max_bed_temp_encoded == 12, "v2: bed temp range");
    CHECK(o->target_volumetric_speed == 80, "v2: target vso");
    CHECK(o->min_volumetric_speed == 20 && o->max_volumetric_speed == 120, "v2: vso range");
    CHECK(o->max_dry_temp_encoded == 10 && o->dry_time_hours == 8, "v2: dry temp/time");
    CHECK(o->density_ugcm3 == 1240, "v2: density");
    CHECK(o->target_weight_g == 1000, "v2: target weight");
    CHECK(o->empty_spool_weight_g == 105, "v2: empty spool weight");
    CHECK(o->measured_filament_length_m == 336, "v2: measured length");
    CHECK(o->measured_filament_weight_g == 1002, "v2: measured weight");
    CHECK(o->spool_core_diameter_mm == 100, "v2: spool core diameter");
    CHECK(o->transmission_distance == 118, "v2: transmission distance (1 byte)");
    CHECK(o->mfi_temp_encoded == 210 && o->mfi_load == 216 && o->mfi_value == 63, "v2: mfi triple");
    CHECK(strcmp(o->online_url, "pfil.us?i=8078-RQSR") == 0, "v2: online_url");
}

int main(void) {
    printf("=== OpenTag3D v2 codec + dispatch ===\n");
    opentag3d_t out;
    opentag3d_t out2;
    uint8_t buf[OT3D_V2_MAP_SIZE];
    opentag3d_result_t r;

    /* A: v2 nominal — every mapped field */
    printf("[A] v2 nominal (224B, v2000)\n");
    build_v2_nominal(buf);
    r = opentag3d_decode(buf, OT3D_V2_MAP_SIZE, &out);
    CHECK(r == OT3D_OK, "A: result OT3D_OK");
    if (r == OT3D_OK) check_v2_fields(&out);

    /* B: v2 minor ahead — 2050 warns but still parses */
    printf("[B] v2 minor ahead (v2050)\n");
    build_v2_nominal(buf);
    put_u16(buf + OT3D_V2_OFF_TAG_VERSION, 2050);
    r = opentag3d_decode(buf, OT3D_V2_MAP_SIZE, &out);
    CHECK(r == OT3D_VERSION_WARNING, "B: result OT3D_VERSION_WARNING");
    CHECK(out.barcode == 12345543210ULL && out.has_extended == 1, "B: fields still parsed at 2050");

    /* C: v2 truncated — 223 bytes */
    printf("[C] v2 truncated (223B)\n");
    build_v2_nominal(buf);
    r = opentag3d_decode(buf, OT3D_V2_MAP_SIZE - 1, &out);
    CHECK(r == OT3D_PARSE_ERROR, "C: result OT3D_PARSE_ERROR");

    /* D: future major — 3000 rejected, version kept */
    printf("[D] future major (v3000)\n");
    memset(buf, 0, 8);
    put_u16(buf + 0x00, 3000);
    r = opentag3d_decode(buf, 8, &out);
    CHECK(r == OT3D_VERSION_ERROR, "D: result OT3D_VERSION_ERROR");
    CHECK(out.tag_version == 3000, "D: tag_version populated (3000)");

    /* E: v1 nominal (187B) — fields correct + v2-only members zero */
    printf("[E] v1 nominal (187B, v1000)\n");
    build_v1_nominal(buf, 1000);
    r = opentag3d_decode(buf, OT3D_EXTENDED_MIN, &out);
    CHECK(r == OT3D_OK, "E: result OT3D_OK");
    CHECK(out.has_extended == 1, "E: has_extended == 1");
    CHECK(strcmp(out.base_material, "PLA") == 0, "E: base_material");
    CHECK(out.diameter_um == 1750 && out.target_weight_g == 1000, "E: diameter+weight");
    CHECK(out.print_temp_encoded == 42 && out.bed_temp_encoded == 16, "E: temps");
    CHECK(out.density_ugcm3 == 1240, "E: density");
    CHECK(out.transmission_distance == 118, "E: transmission distance (u16)");
    CHECK(out.manufacture_year == 2024 && out.manufacture_month == 1 && out.manufacture_day == 23, "E: date");
    CHECK(strcmp(out.serial_number, "SN-V1") == 0, "E: serial (16B field)");
    CHECK(strcmp(out.online_url, "pfil.us?i=8078-RQSR") == 0, "E: online_url");
    CHECK(out.sku[0] == '\0', "E: v2-only sku empty");
    CHECK(out.barcode == 0, "E: v2-only barcode zero");
    CHECK(out.chamber_temp_encoded == 0, "E: v2-only chamber zero");
    CHECK(out.min_nozzle_diameter == 0, "E: v2-only nozzle zero");

    /* F: v1 core-only (102B) */
    printf("[F] v1 core-only (102B)\n");
    memset(buf, 0, OT3D_CORE_SIZE);
    put_u16(buf + 0x00, 1000);
    put_u16(buf + 0x5C, 1750);
    r = opentag3d_decode(buf, OT3D_CORE_SIZE, &out);
    CHECK(r == OT3D_OK, "F: result OT3D_OK");
    CHECK(out.has_extended == 0, "F: has_extended == 0");
    CHECK(out.diameter_um == 1750, "F: core diameter parsed");

    /* G: v1 minor ahead — 1001 warns */
    printf("[G] v1 minor ahead (v1001)\n");
    memset(buf, 0, OT3D_CORE_SIZE);
    put_u16(buf + 0x00, 1001);
    r = opentag3d_decode(buf, OT3D_CORE_SIZE, &out);
    CHECK(r == OT3D_VERSION_WARNING, "G: result OT3D_VERSION_WARNING");

    /* H: runt — 1 byte */
    printf("[H] runt (1B)\n");
    memset(buf, 0, 4);
    r = opentag3d_decode(buf, 1, &out);
    CHECK(r == OT3D_PARSE_ERROR, "H: result OT3D_PARSE_ERROR");

    /* I: encode guard + v1 round-trip */
    printf("[I] encode guard + v1 round-trip\n");
    opentag3d_t src;
    uint8_t small[OT3D_V2_MAP_SIZE - 1];
    memset(&src, 0, sizeof(src));
    src.tag_version = 2000;
    int n = opentag3d_encode(&src, small, sizeof(small));
    CHECK(n == -1, "I: encode refuses too-small v2 buffer (223B)");

    memset(&src, 0, sizeof(src));
    src.tag_version = 1000;
    src.diameter_um = 1750;
    src.target_weight_g = 1000;
    src.print_temp_encoded = 42;
    strcpy(src.manufacturer, "3D");
    strcpy(src.online_url, "pfil.us?i=8078-RQSR");
    strcpy(src.serial_number, "SN-ABC");
    n = opentag3d_encode(&src, buf, OT3D_EXTENDED_MIN);
    CHECK(n == OT3D_EXTENDED_MIN, "I: v1 encode returns EXTENDED_MIN");
    r = opentag3d_decode(buf, OT3D_EXTENDED_MIN, &out);
    CHECK(r == OT3D_OK, "I: round-trip decodes OK");
    CHECK(out.diameter_um == 1750 && out.target_weight_g == 1000 &&
          out.print_temp_encoded == 42, "I: round-trip numerics survive");
    CHECK(strcmp(out.manufacturer, "3D") == 0, "I: round-trip manufacturer survives");
    CHECK(strcmp(out.online_url, "pfil.us?i=8078-RQSR") == 0, "I: round-trip url survives");
    CHECK(strcmp(out.serial_number, "SN-ABC") == 0, "I: round-trip serial survives");

    /* J: legacy version 0 — parses via v1 path (documents preserved behavior) */
    printf("[J] legacy version 0 (102B)\n");
    memset(buf, 0, OT3D_CORE_SIZE);
    put_u16(buf + 0x00, 0);
    put_u16(buf + 0x5C, 1750);
    r = opentag3d_decode(buf, OT3D_CORE_SIZE, &out);
    CHECK(r == OT3D_OK, "J: version 0 parses via v1 path (OT3D_OK)");
    CHECK(out.tag_version == 0 && out.diameter_um == 1750, "J: version 0 fields parsed");

    /* K: v2 full round-trip — struct -> encode -> decode -> compare.
     * Every encodable member carries a distinct nonzero value. */
    printf("[K] v2 full round-trip\n");
    memset(&src, 0, sizeof(src));
    src.tag_version = 2000;
    strcpy(src.base_material, "ABS");
    strcpy(src.material_modifiers, "GF");
    strcpy(src.manufacturer, "Prusa Research");
    strcpy(src.color_name, "Graphite");
    memcpy(src.color_rgba[0], (uint8_t[]){200, 100, 50, 25}, 4);
    memcpy(src.color_rgba[1], (uint8_t[]){10, 20, 30, 40}, 4);
    memcpy(src.color_rgba[2], (uint8_t[]){111, 122, 133, 144}, 4);
    memcpy(src.color_rgba[3], (uint8_t[]){5, 6, 7, 8}, 4);
    strcpy(src.serial_number, V2_SERIAL);
    strcpy(src.sku, "SKU-K1");
    src.barcode = 12345543210ULL;
    src.manufacture_year = 2025; src.manufacture_month = 7; src.manufacture_day = 15;
    src.manufacture_hour = 12; src.manufacture_minute = 34; src.manufacture_second = 56;
    src.diameter_um = 285;
    src.measured_tolerance_um = 8;
    src.min_nozzle_diameter = 3;
    src.print_temp_encoded = 40; src.min_print_temp_encoded = 36; src.max_print_temp_encoded = 44;
    src.chamber_temp_encoded = 20;
    src.bed_temp_encoded = 15; src.min_bed_temp_encoded = 10; src.max_bed_temp_encoded = 14;
    src.target_volumetric_speed = 70; src.min_volumetric_speed = 25; src.max_volumetric_speed = 110;
    src.max_dry_temp_encoded = 11; src.dry_time_hours = 9;
    src.density_ugcm3 = 1300;
    src.target_weight_g = 800; src.empty_spool_weight_g = 120;
    src.measured_filament_length_m = 400; src.measured_filament_weight_g = 810;
    src.spool_core_diameter_mm = 55;
    src.transmission_distance = 118;
    src.mfi_temp_encoded = 200; src.mfi_load = 210; src.mfi_value = 60;
    strcpy(src.online_url, "pfil.us/roundtrip");

    n = opentag3d_encode(&src, buf, OT3D_V2_MAP_SIZE);
    CHECK(n == OT3D_V2_MAP_SIZE, "K: v2 encode returns MAP_SIZE");
    r = opentag3d_decode(buf, OT3D_V2_MAP_SIZE, &out);
    CHECK(r == OT3D_OK, "K: round-trip decodes OK");
    CHECK(out.has_extended == 1, "K: has_extended == 1");
    CHECK(strcmp(out.base_material, "ABS") == 0, "K: base_material");
    CHECK(strcmp(out.material_modifiers, "GF") == 0, "K: material_modifiers");
    CHECK(strcmp(out.manufacturer, "Prusa Research") == 0, "K: manufacturer");
    CHECK(strcmp(out.color_name, "Graphite") == 0, "K: color_name");
    CHECK(out.color_rgba[0][0] == 200 && out.color_rgba[0][1] == 100 &&
          out.color_rgba[0][2] == 50 && out.color_rgba[0][3] == 25, "K: color_rgba[0]");
    CHECK(out.color_rgba[1][0] == 10 && out.color_rgba[1][3] == 40, "K: color_rgba[1]");
    CHECK(out.color_rgba[2][0] == 111 && out.color_rgba[2][3] == 144, "K: color_rgba[2]");
    CHECK(out.color_rgba[3][0] == 5 && out.color_rgba[3][3] == 8, "K: color_rgba[3]");
    CHECK(strcmp(out.serial_number, V2_SERIAL) == 0 && strlen(out.serial_number) == 32, "K: serial (32B)");
    CHECK(strcmp(out.sku, "SKU-K1") == 0, "K: sku");
    CHECK(out.barcode == 12345543210ULL, "K: barcode (u48)");
    CHECK(out.manufacture_year == 2025 && out.manufacture_month == 7 && out.manufacture_day == 15, "K: date");
    CHECK(out.manufacture_hour == 12 && out.manufacture_minute == 34 && out.manufacture_second == 56, "K: time");
    CHECK(out.diameter_um == 285, "K: diameter");
    CHECK(out.measured_tolerance_um == 8, "K: tolerance");
    CHECK(out.min_nozzle_diameter == 3, "K: nozzle diameter");
    CHECK(out.print_temp_encoded == 40, "K: print temp");
    CHECK(out.min_print_temp_encoded == 36 && out.max_print_temp_encoded == 44, "K: print temp range");
    CHECK(out.chamber_temp_encoded == 20, "K: chamber temp");
    CHECK(out.bed_temp_encoded == 15, "K: bed temp");
    CHECK(out.min_bed_temp_encoded == 10 && out.max_bed_temp_encoded == 14, "K: bed temp range");
    CHECK(out.target_volumetric_speed == 70, "K: target vso");
    CHECK(out.min_volumetric_speed == 25 && out.max_volumetric_speed == 110, "K: vso range");
    CHECK(out.max_dry_temp_encoded == 11 && out.dry_time_hours == 9, "K: dry temp/time");
    CHECK(out.density_ugcm3 == 1300, "K: density");
    CHECK(out.target_weight_g == 800, "K: target weight");
    CHECK(out.empty_spool_weight_g == 120, "K: empty spool weight");
    CHECK(out.measured_filament_length_m == 400, "K: measured length");
    CHECK(out.measured_filament_weight_g == 810, "K: measured weight");
    CHECK(out.spool_core_diameter_mm == 55, "K: spool core diameter");
    CHECK(out.transmission_distance == 118, "K: transmission distance");
    CHECK(out.mfi_temp_encoded == 200 && out.mfi_load == 210 && out.mfi_value == 60, "K: mfi triple");
    CHECK(strcmp(out.online_url, "pfil.us/roundtrip") == 0, "K: online_url");

    /* L: raw byte spot-checks on the K-encoded buffer — guards against a
     * mirrored encode/decode bug that round-trips cleanly but is wrong on the wire. */
    printf("[L] v2 raw byte spot-checks\n");
    CHECK(buf[OT3D_V2_OFF_TAG_VERSION] == 0x07 && buf[OT3D_V2_OFF_TAG_VERSION + 1] == 0xD0,
          "L: tag_version bytes {07 D0} (2000)");
    CHECK(buf[OT3D_V2_OFF_CHAMBER_TEMP] == 20, "L: chamber byte == 20");
    CHECK(buf[OT3D_V2_OFF_WEIGHT] == (uint8_t)(800 >> 8) && buf[OT3D_V2_OFF_WEIGHT + 1] == (uint8_t)(800 & 0xFF),
          "L: weight bytes == BE(800)");
    {
        /* Hard literal, NOT put_u48 — comparing the encoder against a copy of
         * itself would pass even if both were little-endian. 12345543210 =
         * 0x02DFDA0A2A, big-endian in 6 bytes: */
        static const uint8_t want_bc[6] = {0x00, 0x02, 0xDF, 0xDA, 0x0A, 0x2A};
        CHECK(memcmp(buf + OT3D_V2_OFF_BARCODE, want_bc, 6) == 0, "L: barcode bytes == BE(12345543210)");
    }

    /* M: version preservation (deduction write-back keeps a tag's layout). */
    printf("[M] version preservation\n");
    build_v1_nominal(buf, 1000);
    r = opentag3d_decode(buf, OT3D_EXTENDED_MIN, &out);
    CHECK(r == OT3D_OK, "M: v1 decode OK");
    n = opentag3d_encode(&out, buf, OT3D_EXTENDED_MIN);   /* tag_version still 1000 -> v1 path */
    CHECK(n == OT3D_EXTENDED_MIN, "M: v1-stamped re-encode returns EXTENDED_MIN");
    r = opentag3d_decode(buf, OT3D_EXTENDED_MIN, &out2);
    CHECK(r == OT3D_OK, "M: re-encoded v1 decodes OK");
    CHECK(out2.tag_version == 1000, "M: re-encoded v1 keeps v1 version");
    CHECK(out2.diameter_um == 1750 && out2.target_weight_g == 1000 &&
          out2.transmission_distance == 118 && out2.density_ugcm3 == 1240, "M: v1 numerics survive");
    CHECK(strcmp(out2.serial_number, "SN-V1") == 0 && strcmp(out2.online_url, "pfil.us?i=8078-RQSR") == 0, "M: v1 strings survive");
    CHECK(out2.measured_filament_length_m == 336 && out2.measured_filament_weight_g == 1002, "M: v1 extended numerics survive");

    build_v2_nominal(buf);
    r = opentag3d_decode(buf, OT3D_V2_MAP_SIZE, &out);
    CHECK(r == OT3D_OK && out.tag_version == 2000, "M: v2 decode OK (version 2000)");
    n = opentag3d_encode(&out, buf, OT3D_V2_MAP_SIZE);     /* tag_version still 2000 -> v2 path */
    CHECK(n == OT3D_V2_MAP_SIZE, "M: v2-stamped re-encode returns MAP_SIZE");
    r = opentag3d_decode(buf, OT3D_V2_MAP_SIZE, &out2);
    CHECK(r == OT3D_OK, "M: re-encoded v2 decodes OK");
    CHECK(out2.barcode == 12345543210ULL && out2.chamber_temp_encoded == 12 &&
          out2.min_nozzle_diameter == 4 && out2.transmission_distance == 118, "M: v2-only fields survive");
    CHECK(strcmp(out2.sku, "G00-A01") == 0 && strcmp(out2.serial_number, V2_SERIAL) == 0, "M: v2 strings survive");

    /* N: future major refused — never write an unknown layout. */
    printf("[N] future major (3000)\n");
    memset(&src, 0, sizeof(src));
    src.tag_version = 3000;
    n = opentag3d_encode(&src, buf, OT3D_V2_MAP_SIZE);
    CHECK(n == -1, "N: encode refuses major >= 3 (v3000)");

    /* O: transmission distance clamped to the 25.0mm (250) spec cap. */
    printf("[O] transmission distance clamp\n");
    memset(&src, 0, sizeof(src));
    src.tag_version = 2000;
    src.transmission_distance = 400;
    n = opentag3d_encode(&src, buf, OT3D_V2_MAP_SIZE);
    CHECK(n == OT3D_V2_MAP_SIZE, "O: v2 encode returns MAP_SIZE");
    r = opentag3d_decode(buf, OT3D_V2_MAP_SIZE, &out);
    CHECK(out.transmission_distance == 250, "O: td 400 clamped to 250");

    /* P: minor-ahead encode refusal — a struct stamped 2001/1001 carries a
     * layout newer than our 2.000/1.000 knowledge; re-encoding from that
     * knowledge would zero its extra fields, so the dispatcher refuses it.
     * 2000 must still encode (regression guard). */
    printf("[P] minor-ahead encode refusal\n");
    memset(&src, 0, sizeof(src));
    src.tag_version = 2001;
    n = opentag3d_encode(&src, buf, OT3D_V2_MAP_SIZE);
    CHECK(n == -1, "P: encode refuses v2 minor ahead (2001)");
    memset(&src, 0, sizeof(src));
    src.tag_version = 1001;
    n = opentag3d_encode(&src, buf, OT3D_V2_MAP_SIZE);
    CHECK(n == -1, "P: encode refuses v1 minor ahead (1001)");
    memset(&src, 0, sizeof(src));
    src.tag_version = 2000;
    n = opentag3d_encode(&src, buf, OT3D_V2_MAP_SIZE);
    CHECK(n == OT3D_V2_MAP_SIZE, "P: v2 2000 still encodes (regression guard)");

    /* Q: the shared version predicates every write-side guard relies on. */
    printf("[Q] version predicate helpers\n");
    CHECK(opentag3d_major(2000) == 2 && opentag3d_major(1999) == 1 &&
          opentag3d_major(999) == 0, "Q: opentag3d_major splits correctly");
    CHECK(opentag3d_can_encode(1000) && opentag3d_can_encode(2000),
          "Q: can_encode accepts supported versions");
    CHECK(!opentag3d_can_encode(1001) && !opentag3d_can_encode(2001) &&
          !opentag3d_can_encode(3000), "Q: can_encode refuses minor-ahead and future majors");
    CHECK(opentag3d_can_encode(0), "Q: can_encode accepts legacy version 0 (v1 path)");

    /* R: header constants pinned to the SPEC'S literal addresses. Generated
     * directly from opentag3d-spec-v2.json, independently of the header —
     * a wrong OT3D_V2_OFF_* constant moves the codec AND every buffer-based
     * test together, and only these literals catch it. */
    printf("[R] spec literal pinning\n");
    CHECK(OT3D_V2_OFF_TAG_VERSION == 0x00 && OT3D_V2_LEN_TAG_VERSION == 2, "R: tag_version @0x00/2");
    CHECK(OT3D_V2_OFF_MATERIAL == 0x02 && OT3D_V2_LEN_MATERIAL == 5, "R: material @0x02/5");
    CHECK(OT3D_V2_OFF_MATERIAL_MOD == 0x07 && OT3D_V2_LEN_MATERIAL_MOD == 5, "R: material_mod @0x07/5");
    CHECK(OT3D_V2_OFF_MANUFACTURER == 0x0C && OT3D_V2_LEN_MANUFACTURER == 16, "R: manufacturer @0x0C/16");
    CHECK(OT3D_V2_OFF_COLOR_NAME == 0x1C && OT3D_V2_LEN_COLOR_NAME == 32, "R: color_name @0x1C/32");
    CHECK(OT3D_V2_OFF_COLOR_1 == 0x3C && OT3D_V2_LEN_COLOR_1 == 4, "R: color_1 @0x3C/4");
    CHECK(OT3D_V2_OFF_COLOR_2 == 0x40 && OT3D_V2_LEN_COLOR_2 == 4, "R: color_2 @0x40/4");
    CHECK(OT3D_V2_OFF_COLOR_3 == 0x44 && OT3D_V2_LEN_COLOR_3 == 4, "R: color_3 @0x44/4");
    CHECK(OT3D_V2_OFF_COLOR_4 == 0x48 && OT3D_V2_LEN_COLOR_4 == 4, "R: color_4 @0x48/4");
    CHECK(OT3D_V2_OFF_SERIAL == 0x4C && OT3D_V2_LEN_SERIAL == 32, "R: serial @0x4C/32");
    CHECK(OT3D_V2_OFF_SKU == 0x6C && OT3D_V2_LEN_SKU == 16, "R: sku @0x6C/16");
    CHECK(OT3D_V2_OFF_BARCODE == 0x7C && OT3D_V2_LEN_BARCODE == 6, "R: barcode @0x7C/6");
    CHECK(OT3D_V2_OFF_MFG_DATE == 0x84 && OT3D_V2_LEN_MFG_DATE == 4, "R: mfg_date @0x84/4");
    CHECK(OT3D_V2_OFF_MFG_TIME == 0x88 && OT3D_V2_LEN_MFG_TIME == 3, "R: mfg_time @0x88/3");
    CHECK(OT3D_V2_OFF_DIAMETER == 0x8C && OT3D_V2_LEN_DIAMETER == 2, "R: diameter @0x8C/2");
    CHECK(OT3D_V2_OFF_TOLERANCE == 0x8E && OT3D_V2_LEN_TOLERANCE == 1, "R: tolerance @0x8E/1");
    CHECK(OT3D_V2_OFF_NOZZLE_DIAMETER == 0x8F && OT3D_V2_LEN_NOZZLE_DIAMETER == 1, "R: nozzle_diameter @0x8F/1");
    CHECK(OT3D_V2_OFF_PRINT_TEMP == 0x90 && OT3D_V2_LEN_PRINT_TEMP == 1, "R: print_temp @0x90/1");
    CHECK(OT3D_V2_OFF_MIN_PRINT_TEMP == 0x91 && OT3D_V2_LEN_MIN_PRINT_TEMP == 1, "R: min_print_temp @0x91/1");
    CHECK(OT3D_V2_OFF_MAX_PRINT_TEMP == 0x92 && OT3D_V2_LEN_MAX_PRINT_TEMP == 1, "R: max_print_temp @0x92/1");
    CHECK(OT3D_V2_OFF_CHAMBER_TEMP == 0x93 && OT3D_V2_LEN_CHAMBER_TEMP == 1, "R: chamber_temp @0x93/1");
    CHECK(OT3D_V2_OFF_BED_TEMP == 0x94 && OT3D_V2_LEN_BED_TEMP == 1, "R: bed_temp @0x94/1");
    CHECK(OT3D_V2_OFF_MIN_BED_TEMP == 0x95 && OT3D_V2_LEN_MIN_BED_TEMP == 1, "R: min_bed_temp @0x95/1");
    CHECK(OT3D_V2_OFF_MAX_BED_TEMP == 0x96 && OT3D_V2_LEN_MAX_BED_TEMP == 1, "R: max_bed_temp @0x96/1");
    CHECK(OT3D_V2_OFF_TARGET_VSO == 0x97 && OT3D_V2_LEN_TARGET_VSO == 1, "R: target_vso @0x97/1");
    CHECK(OT3D_V2_OFF_MIN_VSO == 0x98 && OT3D_V2_LEN_MIN_VSO == 1, "R: min_vso @0x98/1");
    CHECK(OT3D_V2_OFF_MAX_VSO == 0x99 && OT3D_V2_LEN_MAX_VSO == 1, "R: max_vso @0x99/1");
    CHECK(OT3D_V2_OFF_MAX_DRY_TEMP == 0x9A && OT3D_V2_LEN_MAX_DRY_TEMP == 1, "R: max_dry_temp @0x9A/1");
    CHECK(OT3D_V2_OFF_DRY_TIME == 0x9B && OT3D_V2_LEN_DRY_TIME == 1, "R: dry_time @0x9B/1");
    CHECK(OT3D_V2_OFF_DENSITY == 0x9C && OT3D_V2_LEN_DENSITY == 2, "R: density @0x9C/2");
    CHECK(OT3D_V2_OFF_WEIGHT == 0x9E && OT3D_V2_LEN_WEIGHT == 2, "R: weight @0x9E/2");
    CHECK(OT3D_V2_OFF_EMPTY_SPOOL_WEIGHT == 0xA0 && OT3D_V2_LEN_EMPTY_SPOOL_WEIGHT == 2, "R: empty_spool_weight @0xA0/2");
    CHECK(OT3D_V2_OFF_MEASURED_LENGTH == 0xA2 && OT3D_V2_LEN_MEASURED_LENGTH == 2, "R: measured_length @0xA2/2");
    CHECK(OT3D_V2_OFF_MEASURED_WEIGHT == 0xA4 && OT3D_V2_LEN_MEASURED_WEIGHT == 2, "R: measured_weight @0xA4/2");
    CHECK(OT3D_V2_OFF_SPOOL_CORE_DIAMETER == 0xA6 && OT3D_V2_LEN_SPOOL_CORE_DIAMETER == 1, "R: spool_core_diameter @0xA6/1");
    CHECK(OT3D_V2_OFF_TD == 0xA7 && OT3D_V2_LEN_TD == 1, "R: td @0xA7/1");
    CHECK(OT3D_V2_OFF_MFI_TEMP == 0xA8 && OT3D_V2_LEN_MFI_TEMP == 1, "R: mfi_temp @0xA8/1");
    CHECK(OT3D_V2_OFF_MFI_LOAD == 0xA9 && OT3D_V2_LEN_MFI_LOAD == 1, "R: mfi_load @0xA9/1");
    CHECK(OT3D_V2_OFF_MFI_VALUE == 0xAA && OT3D_V2_LEN_MFI_VALUE == 1, "R: mfi_value @0xAA/1");
    CHECK(OT3D_V2_OFF_DATA_URL == 0xB8 && OT3D_V2_LEN_DATA_URL == 32, "R: data_url @0xB8/32");
    CHECK(OT3D_V2_MAP_SIZE == 0xE0 && OT3D_V2_VERSION == 2000, "R: map size + version constants");

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}