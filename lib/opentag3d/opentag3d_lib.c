#include "opentag3d_lib.h"
#include "opentag3d_v2_map.h"
#include <string.h>

/* Read big-endian uint16 from buffer */
static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

/* Read big-endian uint48 (6 bytes) from buffer */
static uint64_t read_u48(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 6; i++) v = (v << 8) | p[i];
    return v;
}

/* Copy fixed-length field to null-terminated string, trimming trailing spaces */
static void read_str(const uint8_t *src, size_t src_len, char *dst, size_t dst_size) {
    size_t copy = src_len;
    if (copy >= dst_size) copy = dst_size - 1;
    memcpy(dst, src, copy);
    dst[copy] = '\0';
    /* Trim trailing spaces */
    while (copy > 0 && dst[copy - 1] == ' ') {
        dst[--copy] = '\0';
    }
}

/* v2.000 field mapping — every struct member reads from its OT3D_V2_OFF_*
 * constant; opentag3d_encode_v2 mirrors this list exactly. Caller has already
 * verified len >= OT3D_V2_MAP_SIZE. */
static void opentag3d_decode_v2_fields(const uint8_t *payload, opentag3d_t *out) {
    out->has_extended = 1;  /* v2 has no core/extended split */

    read_str(payload + OT3D_V2_OFF_MATERIAL, OT3D_V2_LEN_MATERIAL, out->base_material, sizeof(out->base_material));
    read_str(payload + OT3D_V2_OFF_MATERIAL_MOD, OT3D_V2_LEN_MATERIAL_MOD, out->material_modifiers, sizeof(out->material_modifiers));
    read_str(payload + OT3D_V2_OFF_MANUFACTURER, OT3D_V2_LEN_MANUFACTURER, out->manufacturer, sizeof(out->manufacturer));
    read_str(payload + OT3D_V2_OFF_COLOR_NAME, OT3D_V2_LEN_COLOR_NAME, out->color_name, sizeof(out->color_name));

    memcpy(out->color_rgba[0], payload + OT3D_V2_OFF_COLOR_1, 4);
    memcpy(out->color_rgba[1], payload + OT3D_V2_OFF_COLOR_2, 4);
    memcpy(out->color_rgba[2], payload + OT3D_V2_OFF_COLOR_3, 4);
    memcpy(out->color_rgba[3], payload + OT3D_V2_OFF_COLOR_4, 4);

    read_str(payload + OT3D_V2_OFF_SERIAL, OT3D_V2_LEN_SERIAL, out->serial_number, sizeof(out->serial_number));
    read_str(payload + OT3D_V2_OFF_SKU, OT3D_V2_LEN_SKU, out->sku, sizeof(out->sku));
    out->barcode = read_u48(payload + OT3D_V2_OFF_BARCODE);

    out->manufacture_year   = read_u16(payload + OT3D_V2_OFF_MFG_DATE);
    out->manufacture_month  = payload[OT3D_V2_OFF_MFG_DATE + 2];
    out->manufacture_day    = payload[OT3D_V2_OFF_MFG_DATE + 3];
    out->manufacture_hour   = payload[OT3D_V2_OFF_MFG_TIME + 0];
    out->manufacture_minute = payload[OT3D_V2_OFF_MFG_TIME + 1];
    out->manufacture_second = payload[OT3D_V2_OFF_MFG_TIME + 2];

    out->diameter_um            = read_u16(payload + OT3D_V2_OFF_DIAMETER);
    out->measured_tolerance_um  = payload[OT3D_V2_OFF_TOLERANCE];
    out->min_nozzle_diameter    = payload[OT3D_V2_OFF_NOZZLE_DIAMETER];
    out->print_temp_encoded     = payload[OT3D_V2_OFF_PRINT_TEMP];
    out->min_print_temp_encoded = payload[OT3D_V2_OFF_MIN_PRINT_TEMP];
    out->max_print_temp_encoded = payload[OT3D_V2_OFF_MAX_PRINT_TEMP];
    out->chamber_temp_encoded   = payload[OT3D_V2_OFF_CHAMBER_TEMP];
    out->bed_temp_encoded       = payload[OT3D_V2_OFF_BED_TEMP];
    out->min_bed_temp_encoded   = payload[OT3D_V2_OFF_MIN_BED_TEMP];
    out->max_bed_temp_encoded   = payload[OT3D_V2_OFF_MAX_BED_TEMP];
    out->target_volumetric_speed = payload[OT3D_V2_OFF_TARGET_VSO];
    out->min_volumetric_speed    = payload[OT3D_V2_OFF_MIN_VSO];
    out->max_volumetric_speed    = payload[OT3D_V2_OFF_MAX_VSO];
    out->max_dry_temp_encoded    = payload[OT3D_V2_OFF_MAX_DRY_TEMP];
    out->dry_time_hours          = payload[OT3D_V2_OFF_DRY_TIME];

    out->density_ugcm3            = read_u16(payload + OT3D_V2_OFF_DENSITY);
    out->target_weight_g          = read_u16(payload + OT3D_V2_OFF_WEIGHT);
    out->empty_spool_weight_g     = read_u16(payload + OT3D_V2_OFF_EMPTY_SPOOL_WEIGHT);
    out->measured_filament_length_m = read_u16(payload + OT3D_V2_OFF_MEASURED_LENGTH);
    out->measured_filament_weight_g = read_u16(payload + OT3D_V2_OFF_MEASURED_WEIGHT);
    out->spool_core_diameter_mm   = payload[OT3D_V2_OFF_SPOOL_CORE_DIAMETER];
    out->transmission_distance    = payload[OT3D_V2_OFF_TD];
    out->mfi_temp_encoded         = payload[OT3D_V2_OFF_MFI_TEMP];
    out->mfi_load                 = payload[OT3D_V2_OFF_MFI_LOAD];
    out->mfi_value                = payload[OT3D_V2_OFF_MFI_VALUE];
    read_str(payload + OT3D_V2_OFF_DATA_URL, OT3D_V2_LEN_DATA_URL, out->online_url, sizeof(out->online_url));
}

opentag3d_result_t opentag3d_decode(const uint8_t *payload, size_t len, opentag3d_t *out) {
    if (out == NULL || payload == NULL) return OT3D_PARSE_ERROR;
    memset(out, 0, sizeof(opentag3d_t));

    /* Need at least the version field */
    if (len < 2) return OT3D_PARSE_ERROR;

    /* Read the version BEFORE any gate so callers can log it even on error */
    out->tag_version = read_u16(payload + 0x00);
    uint16_t major = out->tag_version / 1000;

    opentag3d_result_t version_result = OT3D_OK;

    if (major >= 3) {
        return OT3D_VERSION_ERROR;
    }

    if (major == 2) {
        /* ---- v2.000 path ---- */
        if (out->tag_version > OT3D_SUPPORTED_V2) version_result = OT3D_VERSION_WARNING;

        /* v2 payloads are always the full 224-byte map */
        if (len < OT3D_V2_MAP_SIZE) return OT3D_PARSE_ERROR;

        opentag3d_decode_v2_fields(payload, out);
        return version_result;
    }

    /* ---- major <= 1: legacy v1.000 path (also handles version 0 tags) ---- */
    if (out->tag_version > OT3D_SUPPORTED_V1) version_result = OT3D_VERSION_WARNING;

    /* Need enough data for core fields */
    if (len < OT3D_CORE_SIZE) return OT3D_PARSE_ERROR;

    /* Core fields at fixed offsets */
    read_str(payload + 0x02, 5, out->base_material, sizeof(out->base_material));
    read_str(payload + 0x07, 5, out->material_modifiers, sizeof(out->material_modifiers));
    /* 0x0C - 0x1A: reserved/padding in spec */
    read_str(payload + 0x1B, 16, out->manufacturer, sizeof(out->manufacturer));
    read_str(payload + 0x2B, 32, out->color_name, sizeof(out->color_name));

    /* 4 RGBA colors */
    memcpy(out->color_rgba[0], payload + 0x4B, 4);
    memcpy(out->color_rgba[1], payload + 0x4F, 4);
    memcpy(out->color_rgba[2], payload + 0x53, 4);
    memcpy(out->color_rgba[3], payload + 0x57, 4);

    out->diameter_um            = read_u16(payload + 0x5C);
    out->target_weight_g        = read_u16(payload + 0x5E);
    out->print_temp_encoded     = payload[0x60];
    out->bed_temp_encoded       = payload[0x61];
    out->density_ugcm3          = read_u16(payload + 0x62);
    out->transmission_distance  = read_u16(payload + 0x64);

    /* Extended fields — parse if we have enough data */
    if (len >= OT3D_EXTENDED_MIN) {
        out->has_extended = 1;
        read_str(payload + 0x70, 32, out->online_url, sizeof(out->online_url));
        read_str(payload + 0x90, 16, out->serial_number, sizeof(out->serial_number));

        out->manufacture_year   = read_u16(payload + 0xA0);
        out->manufacture_month  = payload[0xA2];
        out->manufacture_day    = payload[0xA3];
        out->manufacture_hour   = payload[0xA4];
        out->manufacture_minute = payload[0xA5];
        out->manufacture_second = payload[0xA6];

        out->spool_core_diameter_mm = payload[0xA7];
        out->mfi_temp_encoded       = payload[0xA8];
        out->mfi_load               = payload[0xA9];
        out->mfi_value              = payload[0xAA];
        out->measured_tolerance_um  = payload[0xAB];
        out->empty_spool_weight_g   = read_u16(payload + 0xAC);
        out->measured_filament_weight_g = read_u16(payload + 0xAE);
        out->measured_filament_length_m = read_u16(payload + 0xB0);
        out->max_dry_temp_encoded   = payload[0xB2];
        out->dry_time_hours         = payload[0xB3];
        out->min_print_temp_encoded = payload[0xB4];
        out->max_print_temp_encoded = payload[0xB5];
        out->min_bed_temp_encoded   = payload[0xB6];
        out->max_bed_temp_encoded   = payload[0xB7];
        out->min_volumetric_speed   = payload[0xB8];
        out->max_volumetric_speed   = payload[0xB9];
        out->target_volumetric_speed = payload[0xBA];
    }

    return version_result;
}

/* Write big-endian uint16 to buffer */
static void write_u16(uint8_t *p, uint16_t val) {
    p[0] = (uint8_t)(val >> 8);
    p[1] = (uint8_t)(val & 0xFF);
}

/* Write fixed-length field, pad with spaces */
static void write_str(const char *src, uint8_t *dst, size_t field_len) {
    size_t slen = strlen(src);
    if (slen > field_len) slen = field_len;
    memcpy(dst, src, slen);
    if (slen < field_len) {
        memset(dst + slen, ' ', field_len - slen);
    }
}

/* Write big-endian uint48 (6 bytes) to buffer */
static void write_u48(uint8_t *p, uint64_t val) {
    for (int i = 5; i >= 0; i--) { p[i] = (uint8_t)(val & 0xFF); val >>= 8; }
}

/* v2.000 encode: exact mirror of the v2 decode mapping above — every struct
 * field lands at the same OT3D_V2_OFF_* constant the decoder reads it from. */
static int opentag3d_encode_v2(const opentag3d_t *tag, uint8_t *buf, size_t buflen) {
    if (buflen < OT3D_V2_MAP_SIZE) return -1;
    memset(buf, 0, buflen);  /* whole caller buffer, matching the v1 path's contract */

    write_u16(buf + OT3D_V2_OFF_TAG_VERSION, tag->tag_version);
    write_str(tag->base_material, buf + OT3D_V2_OFF_MATERIAL, OT3D_V2_LEN_MATERIAL);
    write_str(tag->material_modifiers, buf + OT3D_V2_OFF_MATERIAL_MOD, OT3D_V2_LEN_MATERIAL_MOD);
    write_str(tag->manufacturer, buf + OT3D_V2_OFF_MANUFACTURER, OT3D_V2_LEN_MANUFACTURER);
    write_str(tag->color_name, buf + OT3D_V2_OFF_COLOR_NAME, OT3D_V2_LEN_COLOR_NAME);

    memcpy(buf + OT3D_V2_OFF_COLOR_1, tag->color_rgba[0], 4);
    memcpy(buf + OT3D_V2_OFF_COLOR_2, tag->color_rgba[1], 4);
    memcpy(buf + OT3D_V2_OFF_COLOR_3, tag->color_rgba[2], 4);
    memcpy(buf + OT3D_V2_OFF_COLOR_4, tag->color_rgba[3], 4);

    write_str(tag->serial_number, buf + OT3D_V2_OFF_SERIAL, OT3D_V2_LEN_SERIAL);
    write_str(tag->sku, buf + OT3D_V2_OFF_SKU, OT3D_V2_LEN_SKU);
    write_u48(buf + OT3D_V2_OFF_BARCODE, tag->barcode);

    write_u16(buf + OT3D_V2_OFF_MFG_DATE, tag->manufacture_year);
    buf[OT3D_V2_OFF_MFG_DATE + 2] = tag->manufacture_month;
    buf[OT3D_V2_OFF_MFG_DATE + 3] = tag->manufacture_day;
    buf[OT3D_V2_OFF_MFG_TIME + 0] = tag->manufacture_hour;
    buf[OT3D_V2_OFF_MFG_TIME + 1] = tag->manufacture_minute;
    buf[OT3D_V2_OFF_MFG_TIME + 2] = tag->manufacture_second;

    write_u16(buf + OT3D_V2_OFF_DIAMETER, tag->diameter_um);
    buf[OT3D_V2_OFF_TOLERANCE] = tag->measured_tolerance_um;
    buf[OT3D_V2_OFF_NOZZLE_DIAMETER] = tag->min_nozzle_diameter;
    buf[OT3D_V2_OFF_PRINT_TEMP] = tag->print_temp_encoded;
    buf[OT3D_V2_OFF_MIN_PRINT_TEMP] = tag->min_print_temp_encoded;
    buf[OT3D_V2_OFF_MAX_PRINT_TEMP] = tag->max_print_temp_encoded;
    buf[OT3D_V2_OFF_CHAMBER_TEMP] = tag->chamber_temp_encoded;
    buf[OT3D_V2_OFF_BED_TEMP] = tag->bed_temp_encoded;
    buf[OT3D_V2_OFF_MIN_BED_TEMP] = tag->min_bed_temp_encoded;
    buf[OT3D_V2_OFF_MAX_BED_TEMP] = tag->max_bed_temp_encoded;
    buf[OT3D_V2_OFF_TARGET_VSO] = tag->target_volumetric_speed;
    buf[OT3D_V2_OFF_MIN_VSO] = tag->min_volumetric_speed;
    buf[OT3D_V2_OFF_MAX_VSO] = tag->max_volumetric_speed;
    buf[OT3D_V2_OFF_MAX_DRY_TEMP] = tag->max_dry_temp_encoded;
    buf[OT3D_V2_OFF_DRY_TIME] = tag->dry_time_hours;

    write_u16(buf + OT3D_V2_OFF_DENSITY, tag->density_ugcm3);
    write_u16(buf + OT3D_V2_OFF_WEIGHT, tag->target_weight_g);
    write_u16(buf + OT3D_V2_OFF_EMPTY_SPOOL_WEIGHT, tag->empty_spool_weight_g);
    write_u16(buf + OT3D_V2_OFF_MEASURED_LENGTH, tag->measured_filament_length_m);
    write_u16(buf + OT3D_V2_OFF_MEASURED_WEIGHT, tag->measured_filament_weight_g);
    buf[OT3D_V2_OFF_SPOOL_CORE_DIAMETER] = tag->spool_core_diameter_mm;
    /* transmission_distance is uint16_t in the struct but 1 byte on-tag; the
     * spec caps TD at 25.0mm (250), so clamp to avoid silent byte truncation. */
    buf[OT3D_V2_OFF_TD] = (uint8_t)(tag->transmission_distance > 250 ? 250 : tag->transmission_distance);
    buf[OT3D_V2_OFF_MFI_TEMP] = tag->mfi_temp_encoded;
    buf[OT3D_V2_OFF_MFI_LOAD] = tag->mfi_load;
    buf[OT3D_V2_OFF_MFI_VALUE] = tag->mfi_value;
    write_str(tag->online_url, buf + OT3D_V2_OFF_DATA_URL, OT3D_V2_LEN_DATA_URL);

    return OT3D_V2_MAP_SIZE;
}

/* v1.000 encode (also handles legacy version-0 tags). */
static int opentag3d_encode_v1(const opentag3d_t *tag, uint8_t *buf, size_t buflen) {
    if (buflen < OT3D_CORE_SIZE) return -1;

    memset(buf, 0, buflen);

    /* Version */
    write_u16(buf + 0x00, tag->tag_version);

    /* Core fields */
    write_str(tag->base_material, buf + 0x02, 5);
    write_str(tag->material_modifiers, buf + 0x07, 5);
    /* 0x0C - 0x1A: zero (reserved) */
    write_str(tag->manufacturer, buf + 0x1B, 16);
    write_str(tag->color_name, buf + 0x2B, 32);

    /* 4 RGBA colors */
    memcpy(buf + 0x4B, tag->color_rgba[0], 4);
    memcpy(buf + 0x4F, tag->color_rgba[1], 4);
    memcpy(buf + 0x53, tag->color_rgba[2], 4);
    memcpy(buf + 0x57, tag->color_rgba[3], 4);

    write_u16(buf + 0x5C, tag->diameter_um);
    write_u16(buf + 0x5E, tag->target_weight_g);
    buf[0x60] = tag->print_temp_encoded;
    buf[0x61] = tag->bed_temp_encoded;
    write_u16(buf + 0x62, tag->density_ugcm3);
    write_u16(buf + 0x64, tag->transmission_distance);

    int written = OT3D_CORE_SIZE;

    /* Extended fields if buffer is large enough */
    if (buflen >= OT3D_EXTENDED_MIN) {
        write_str(tag->online_url, buf + 0x70, 32);
        write_str(tag->serial_number, buf + 0x90, 16);

        write_u16(buf + 0xA0, tag->manufacture_year);
        buf[0xA2] = tag->manufacture_month;
        buf[0xA3] = tag->manufacture_day;
        buf[0xA4] = tag->manufacture_hour;
        buf[0xA5] = tag->manufacture_minute;
        buf[0xA6] = tag->manufacture_second;

        buf[0xA7] = tag->spool_core_diameter_mm;
        buf[0xA8] = tag->mfi_temp_encoded;
        buf[0xA9] = tag->mfi_load;
        buf[0xAA] = tag->mfi_value;
        buf[0xAB] = tag->measured_tolerance_um;
        write_u16(buf + 0xAC, tag->empty_spool_weight_g);
        write_u16(buf + 0xAE, tag->measured_filament_weight_g);
        write_u16(buf + 0xB0, tag->measured_filament_length_m);
        buf[0xB2] = tag->max_dry_temp_encoded;
        buf[0xB3] = tag->dry_time_hours;
        buf[0xB4] = tag->min_print_temp_encoded;
        buf[0xB5] = tag->max_print_temp_encoded;
        buf[0xB6] = tag->min_bed_temp_encoded;
        buf[0xB7] = tag->max_bed_temp_encoded;
        buf[0xB8] = tag->min_volumetric_speed;
        buf[0xB9] = tag->max_volumetric_speed;
        buf[0xBA] = tag->target_volumetric_speed;

        written = OT3D_EXTENDED_MIN;
    }

    return written;
}

/* Version-aware dispatcher: a struct's tag_version decides the on-tag layout.
 * Structs decoded from a v1 tag re-encode as v1; structs stamped 2xxx encode
 * as v2. That is how deduction write-back preserves a tag's version.
 * Minor-ahead versions are refused: rebuilding the full map from our
 * 2.000/1.000 knowledge would zero any bytes a newer minor defines while
 * keeping the higher version stamp. */
int opentag3d_encode(const opentag3d_t *tag, uint8_t *buf, size_t buflen) {
    if (tag == NULL || buf == NULL) return -1;
    if (!opentag3d_can_encode(tag->tag_version)) return -1;
    if (opentag3d_major(tag->tag_version) == 2) return opentag3d_encode_v2(tag, buf, buflen);
    return opentag3d_encode_v1(tag, buf, buflen);
}
