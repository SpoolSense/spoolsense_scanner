#include "opentag3d_lib.h"
#include "opentag3d_v2_map.h"
#include <string.h>

/* Decode a complete two-byte, big-endian integer. Bounds are checked by the
 * field-level caller first. Example: {0x06, 0xD6} becomes a 1750 um diameter. */
static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

/* Decode a complete six-byte, big-endian integer. Bounds are checked by the
 * field-level caller first. Example: a six-byte GTIN becomes tag.barcode. */
static uint64_t read_u48(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 6; i++) v = (v << 8) | p[i];
    return v;
}

/* Return the portion of a field that is actually present in the NDEF payload.
 * Every decode helper goes through this boundary check, so a truncated record
 * can populate the fields it contains without reading beyond its declared
 * payload length. Example: a 32-byte serial beginning at offset 76 has only
 * 24 available bytes when the NDEF payload length is 100. */
static size_t available_field_bytes(size_t payload_len, size_t offset, size_t field_len) {
    if (offset >= payload_len) return 0;
    size_t available = payload_len - offset;
    return available < field_len ? available : field_len;
}

/* Copy already-bounded text into a null-terminated C string and trim its
 * fixed-field space padding. Example: {'P','L','A',' ',' '} becomes "PLA". */
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

/* Read as much of a fixed-width text field as the declared NDEF payload
 * contains. A partial field is still null-terminated; an absent field leaves
 * dst unchanged (normally empty because the result struct is zeroed).
 * Example: a payload ending partway through serial_number returns its present
 * prefix rather than reading into bytes outside the NDEF record. */
static void read_str_field(const uint8_t *payload, size_t payload_len,
                           size_t offset, size_t field_len,
                           char *dst, size_t dst_size) {
    size_t available = available_field_bytes(payload_len, offset, field_len);
    if (available > 0) read_str(payload + offset, available, dst, dst_size);
}

/* Copy only the bytes of a fixed-width binary field that are inside the
 * declared NDEF payload. Missing bytes remain zero in the result struct.
 * Example: the four raw RGBA bytes {255, 166, 77, 255} populate one color. */
static void read_bytes_field(const uint8_t *payload, size_t payload_len,
                             size_t offset, size_t field_len, uint8_t *dst) {
    size_t available = available_field_bytes(payload_len, offset, field_len);
    if (available > 0) memcpy(dst, payload + offset, available);
}

/* Decode a one-byte numeric field only when that byte lies inside the declared
 * NDEF payload; otherwise leave dst unchanged. Example: 42 represents a
 * 210 C print temperature after applying the format's x5 scaling. */
static void read_u8_field(const uint8_t *payload, size_t payload_len,
                          size_t offset, uint8_t *dst) {
    if (available_field_bytes(payload_len, offset, sizeof(*dst)) == sizeof(*dst)) {
        *dst = payload[offset];
    }
}

/* Decode a two-byte, big-endian numeric field only when both bytes lie inside
 * the declared NDEF payload. Example: {0x03, 0xE8} becomes a 1000 g weight;
 * a payload ending after only 0x03 leaves the destination zero. */
static void read_u16_field(const uint8_t *payload, size_t payload_len,
                           size_t offset, uint16_t *dst) {
    if (available_field_bytes(payload_len, offset, sizeof(*dst)) == sizeof(*dst)) {
        *dst = read_u16(payload + offset);
    }
}

/* Decode a one-byte on-tag value into a wider uint16_t struct member, provided
 * the byte lies inside the declared NDEF payload. Example: v2 transmission
 * distance byte 118 is stored as uint16_t 118 for later 0.1 mm scaling. */
static void read_u8_as_u16_field(const uint8_t *payload, size_t payload_len,
                                 size_t offset, uint16_t *dst) {
    if (available_field_bytes(payload_len, offset, sizeof(uint8_t)) == sizeof(uint8_t)) {
        *dst = payload[offset];
    }
}

/* Decode a six-byte, big-endian numeric field only when the complete field is
 * inside the declared NDEF payload. Example: the v2 six-byte barcode field
 * can hold GTIN value 12345543210; a truncated barcode remains zero. */
static void read_u48_field(const uint8_t *payload, size_t payload_len,
                           size_t offset, size_t field_len, uint64_t *dst) {
    if (available_field_bytes(payload_len, offset, field_len) == field_len) {
        *dst = read_u48(payload + offset);
    }
}

/* v2.000 field mapping — every struct member reads from its OT3D_V2_OFF_*
 * constant; opentag3d_encode_v2 mirrors this list exactly. Fields wholly or
 * partially outside payload_len remain zero/empty. */
static void opentag3d_decode_v2_fields(const uint8_t *payload, size_t payload_len,
                                       opentag3d_t *out) {
    out->has_extended = 1;  /* v2 has no core/extended split */

    read_str_field(payload, payload_len, OT3D_V2_OFF_MATERIAL, OT3D_V2_LEN_MATERIAL, out->base_material, sizeof(out->base_material));
    read_str_field(payload, payload_len, OT3D_V2_OFF_MATERIAL_MOD, OT3D_V2_LEN_MATERIAL_MOD, out->material_modifiers, sizeof(out->material_modifiers));
    read_str_field(payload, payload_len, OT3D_V2_OFF_MANUFACTURER, OT3D_V2_LEN_MANUFACTURER, out->manufacturer, sizeof(out->manufacturer));
    read_str_field(payload, payload_len, OT3D_V2_OFF_COLOR_NAME, OT3D_V2_LEN_COLOR_NAME, out->color_name, sizeof(out->color_name));

    read_bytes_field(payload, payload_len, OT3D_V2_OFF_COLOR_1, OT3D_V2_LEN_COLOR_1, out->color_rgba[0]);
    read_bytes_field(payload, payload_len, OT3D_V2_OFF_COLOR_2, OT3D_V2_LEN_COLOR_2, out->color_rgba[1]);
    read_bytes_field(payload, payload_len, OT3D_V2_OFF_COLOR_3, OT3D_V2_LEN_COLOR_3, out->color_rgba[2]);
    read_bytes_field(payload, payload_len, OT3D_V2_OFF_COLOR_4, OT3D_V2_LEN_COLOR_4, out->color_rgba[3]);

    read_str_field(payload, payload_len, OT3D_V2_OFF_SERIAL, OT3D_V2_LEN_SERIAL, out->serial_number, sizeof(out->serial_number));
    read_str_field(payload, payload_len, OT3D_V2_OFF_SKU, OT3D_V2_LEN_SKU, out->sku, sizeof(out->sku));
    read_u48_field(payload, payload_len, OT3D_V2_OFF_BARCODE, OT3D_V2_LEN_BARCODE, &out->barcode);

    read_u16_field(payload, payload_len, OT3D_V2_OFF_MFG_DATE, &out->manufacture_year);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MFG_DATE + 2, &out->manufacture_month);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MFG_DATE + 3, &out->manufacture_day);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MFG_TIME + 0, &out->manufacture_hour);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MFG_TIME + 1, &out->manufacture_minute);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MFG_TIME + 2, &out->manufacture_second);

    read_u16_field(payload, payload_len, OT3D_V2_OFF_DIAMETER, &out->diameter_um);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_TOLERANCE, &out->measured_tolerance_um);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_NOZZLE_DIAMETER, &out->min_nozzle_diameter);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_PRINT_TEMP, &out->print_temp_encoded);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MIN_PRINT_TEMP, &out->min_print_temp_encoded);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MAX_PRINT_TEMP, &out->max_print_temp_encoded);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_CHAMBER_TEMP, &out->chamber_temp_encoded);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_BED_TEMP, &out->bed_temp_encoded);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MIN_BED_TEMP, &out->min_bed_temp_encoded);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MAX_BED_TEMP, &out->max_bed_temp_encoded);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_TARGET_VSO, &out->target_volumetric_speed);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MIN_VSO, &out->min_volumetric_speed);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MAX_VSO, &out->max_volumetric_speed);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MAX_DRY_TEMP, &out->max_dry_temp_encoded);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_DRY_TIME, &out->dry_time_hours);

    read_u16_field(payload, payload_len, OT3D_V2_OFF_DENSITY, &out->density_ugcm3);
    read_u16_field(payload, payload_len, OT3D_V2_OFF_WEIGHT, &out->target_weight_g);
    read_u16_field(payload, payload_len, OT3D_V2_OFF_EMPTY_SPOOL_WEIGHT, &out->empty_spool_weight_g);
    read_u16_field(payload, payload_len, OT3D_V2_OFF_MEASURED_LENGTH, &out->measured_filament_length_m);
    read_u16_field(payload, payload_len, OT3D_V2_OFF_MEASURED_WEIGHT, &out->measured_filament_weight_g);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_SPOOL_CORE_DIAMETER, &out->spool_core_diameter_mm);
    read_u8_as_u16_field(payload, payload_len, OT3D_V2_OFF_TD, &out->transmission_distance);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MFI_TEMP, &out->mfi_temp_encoded);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MFI_LOAD, &out->mfi_load);
    read_u8_field(payload, payload_len, OT3D_V2_OFF_MFI_VALUE, &out->mfi_value);
    read_str_field(payload, payload_len, OT3D_V2_OFF_DATA_URL, OT3D_V2_LEN_DATA_URL, out->online_url, sizeof(out->online_url));
}

opentag3d_result_t opentag3d_decode(const uint8_t *payload, size_t len, opentag3d_t *out) {
    if (out == NULL || payload == NULL) return OT3D_PARSE_ERROR;
    memset(out, 0, sizeof(opentag3d_t));

    /* The version is the only field required to select a memory map. */
    if (available_field_bytes(len, OT3D_V2_OFF_TAG_VERSION,
                              OT3D_V2_LEN_TAG_VERSION) != OT3D_V2_LEN_TAG_VERSION) {
        return OT3D_PARSE_ERROR;
    }

    /* Read the version BEFORE any gate so callers can log it even on error */
    out->tag_version = read_u16(payload + OT3D_V2_OFF_TAG_VERSION);
    uint16_t major = out->tag_version / 1000;

    opentag3d_result_t version_result = OT3D_OK;

    if (major >= 3) {
        return OT3D_VERSION_ERROR;
    }

    if (major == 2) {
        /* ---- v2.000 path ---- */
        if (out->tag_version > OT3D_SUPPORTED_V2) version_result = OT3D_VERSION_WARNING;

        opentag3d_decode_v2_fields(payload, len, out);
        return version_result;
    }

    /* ---- major <= 1: legacy v1.000 path (also handles version 0 tags) ---- */
    if (out->tag_version > OT3D_SUPPORTED_V1) version_result = OT3D_VERSION_WARNING;

    /* Legacy fields are populated only when their bytes are present. */
    read_str_field(payload, len, 0x02, 5, out->base_material, sizeof(out->base_material));
    read_str_field(payload, len, 0x07, 5, out->material_modifiers, sizeof(out->material_modifiers));
    /* 0x0C - 0x1A: reserved/padding in spec */
    read_str_field(payload, len, 0x1B, 16, out->manufacturer, sizeof(out->manufacturer));
    read_str_field(payload, len, 0x2B, 32, out->color_name, sizeof(out->color_name));

    /* 4 RGBA colors */
    read_bytes_field(payload, len, 0x4B, 4, out->color_rgba[0]);
    read_bytes_field(payload, len, 0x4F, 4, out->color_rgba[1]);
    read_bytes_field(payload, len, 0x53, 4, out->color_rgba[2]);
    read_bytes_field(payload, len, 0x57, 4, out->color_rgba[3]);

    read_u16_field(payload, len, 0x5C, &out->diameter_um);
    read_u16_field(payload, len, 0x5E, &out->target_weight_g);
    read_u8_field(payload, len, 0x60, &out->print_temp_encoded);
    read_u8_field(payload, len, 0x61, &out->bed_temp_encoded);
    read_u16_field(payload, len, 0x62, &out->density_ugcm3);
    read_u16_field(payload, len, 0x64, &out->transmission_distance);

    /* v1 extended data begins at its URL field. */
    out->has_extended = len > 0x70;
    read_str_field(payload, len, 0x70, 32, out->online_url, sizeof(out->online_url));
    read_str_field(payload, len, 0x90, 16, out->serial_number, sizeof(out->serial_number));

    read_u16_field(payload, len, 0xA0, &out->manufacture_year);
    read_u8_field(payload, len, 0xA2, &out->manufacture_month);
    read_u8_field(payload, len, 0xA3, &out->manufacture_day);
    read_u8_field(payload, len, 0xA4, &out->manufacture_hour);
    read_u8_field(payload, len, 0xA5, &out->manufacture_minute);
    read_u8_field(payload, len, 0xA6, &out->manufacture_second);

    read_u8_field(payload, len, 0xA7, &out->spool_core_diameter_mm);
    read_u8_field(payload, len, 0xA8, &out->mfi_temp_encoded);
    read_u8_field(payload, len, 0xA9, &out->mfi_load);
    read_u8_field(payload, len, 0xAA, &out->mfi_value);
    read_u8_field(payload, len, 0xAB, &out->measured_tolerance_um);
    read_u16_field(payload, len, 0xAC, &out->empty_spool_weight_g);
    read_u16_field(payload, len, 0xAE, &out->measured_filament_weight_g);
    read_u16_field(payload, len, 0xB0, &out->measured_filament_length_m);
    read_u8_field(payload, len, 0xB2, &out->max_dry_temp_encoded);
    read_u8_field(payload, len, 0xB3, &out->dry_time_hours);
    read_u8_field(payload, len, 0xB4, &out->min_print_temp_encoded);
    read_u8_field(payload, len, 0xB5, &out->max_print_temp_encoded);
    read_u8_field(payload, len, 0xB6, &out->min_bed_temp_encoded);
    read_u8_field(payload, len, 0xB7, &out->max_bed_temp_encoded);
    read_u8_field(payload, len, 0xB8, &out->min_volumetric_speed);
    read_u8_field(payload, len, 0xB9, &out->max_volumetric_speed);
    read_u8_field(payload, len, 0xBA, &out->target_volumetric_speed);

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
