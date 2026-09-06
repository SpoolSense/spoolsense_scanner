/* Native tests for the OpenTag3D v2.000 decoder + version dispatch.
 *
 * Covers: v2 nominal parse (every mapped field), version-warning/error
 * gating, v1.000 regression, legacy version-0 passthrough, and the
 * encode guard that refuses v2 stamps. Plain C, PASS/FAIL per check,
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
    printf("=== OpenTag3D v2 decoder + dispatch ===\n");
    opentag3d_t out;
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
    memset(&src, 0, sizeof(src));
    src.tag_version = 2000;
    int n = opentag3d_encode(&src, buf, OT3D_EXTENDED_MIN);
    CHECK(n == -1, "I: encode refuses v2 stamp");

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

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}