#include "opentag3d_lib.h"
#include "opentag3d_v1_map.h"
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
 * constant; opentag3d_encode_v2 mirrors this list exactly. The caller has
 * verified payload_len >= OT3D_V2_MIN_SIZE; bytes past payload_len (the
 * reserved tail of a 216-byte manufacturer record) are never read. */
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

        /* Every v2 field lies inside the 224-byte map, so a record shorter
         * than the map can still decode every byte it actually contains via
         * the bounded field readers below. A record without even a version
         * was rejected above; only a 2-byte stub carries no decodable field.
         * (The old OT3D_V2_MIN_SIZE completeness gate existed to stop lossy
         * full rebuilds — raw patching (#328) no longer rebuilds, so partial
         * known-v2 records decode whatever they contain.) */
        if (len < OT3D_V2_OFF_TAG_VERSION + OT3D_V2_LEN_TAG_VERSION + 1) return OT3D_PARSE_ERROR;

        opentag3d_decode_v2_fields(payload, len, out);
        return version_result;
    }

    /* ---- major <= 1: legacy v1.000 path (also handles version 0 tags) ---- */
    if (out->tag_version > OT3D_SUPPORTED_V1) version_result = OT3D_VERSION_WARNING;

    /* Same bounded rule as v2: decode every complete field the record
     * actually contains. A 102-byte core-only record patched at
     * measured_weight (0xAE) can grow exactly through 0xAF instead of
     * jumping to the canonical 187-byte extended layout. */
    if (len < OT3D_V1_OFF_TAG_VERSION + OT3D_V1_LEN_TAG_VERSION + 1) return OT3D_PARSE_ERROR;

    read_str_field(payload, len, OT3D_V1_OFF_MATERIAL, OT3D_V1_LEN_MATERIAL, out->base_material, sizeof(out->base_material));
    read_str_field(payload, len, OT3D_V1_OFF_MATERIAL_MOD, OT3D_V1_LEN_MATERIAL_MOD, out->material_modifiers, sizeof(out->material_modifiers));
    /* 0x0C - 0x1A: reserved/padding in spec */
    read_str_field(payload, len, OT3D_V1_OFF_MANUFACTURER, OT3D_V1_LEN_MANUFACTURER, out->manufacturer, sizeof(out->manufacturer));
    read_str_field(payload, len, OT3D_V1_OFF_COLOR_NAME, OT3D_V1_LEN_COLOR_NAME, out->color_name, sizeof(out->color_name));

    /* 4 RGBA colors */
    read_bytes_field(payload, len, OT3D_V1_OFF_COLOR_1, OT3D_V1_LEN_COLOR_1, out->color_rgba[0]);
    read_bytes_field(payload, len, OT3D_V1_OFF_COLOR_2, OT3D_V1_LEN_COLOR_2, out->color_rgba[1]);
    read_bytes_field(payload, len, OT3D_V1_OFF_COLOR_3, OT3D_V1_LEN_COLOR_3, out->color_rgba[2]);
    read_bytes_field(payload, len, OT3D_V1_OFF_COLOR_4, OT3D_V1_LEN_COLOR_4, out->color_rgba[3]);

    read_u16_field(payload, len, OT3D_V1_OFF_DIAMETER, &out->diameter_um);
    read_u16_field(payload, len, OT3D_V1_OFF_WEIGHT, &out->target_weight_g);
    read_u8_field(payload, len, OT3D_V1_OFF_PRINT_TEMP, &out->print_temp_encoded);
    read_u8_field(payload, len, OT3D_V1_OFF_BED_TEMP, &out->bed_temp_encoded);
    read_u16_field(payload, len, OT3D_V1_OFF_DENSITY, &out->density_ugcm3);
    read_u16_field(payload, len, OT3D_V1_OFF_TRANSMISSION, &out->transmission_distance);

    /* has_extended once meant "the canonical 187-byte extended block is
     * present end-to-end". With bounded decoding it now means "any extended
     * field byte is present" — the record may cut through the block and still
     * expose the complete fields before the cut. */
    out->has_extended = len > OT3D_EXTENDED_START;

    read_str_field(payload, len, OT3D_V1_OFF_DATA_URL, OT3D_V1_LEN_DATA_URL, out->online_url, sizeof(out->online_url));
    read_str_field(payload, len, OT3D_V1_OFF_SERIAL, OT3D_V1_LEN_SERIAL, out->serial_number, sizeof(out->serial_number));

    read_u16_field(payload, len, OT3D_V1_OFF_MFG_DATE, &out->manufacture_year);
    read_u8_field(payload, len, OT3D_V1_OFF_MFG_DATE + 2, &out->manufacture_month);
    read_u8_field(payload, len, OT3D_V1_OFF_MFG_DATE + 3, &out->manufacture_day);
    read_u8_field(payload, len, OT3D_V1_OFF_MFG_TIME + 0, &out->manufacture_hour);
    read_u8_field(payload, len, OT3D_V1_OFF_MFG_TIME + 1, &out->manufacture_minute);
    read_u8_field(payload, len, OT3D_V1_OFF_MFG_TIME + 2, &out->manufacture_second);

    read_u8_field(payload, len, OT3D_V1_OFF_SPOOL_CORE_DIAMETER, &out->spool_core_diameter_mm);
    read_u8_field(payload, len, OT3D_V1_OFF_MFI_TEMP, &out->mfi_temp_encoded);
    read_u8_field(payload, len, OT3D_V1_OFF_MFI_LOAD, &out->mfi_load);
    read_u8_field(payload, len, OT3D_V1_OFF_MFI_VALUE, &out->mfi_value);
    read_u8_field(payload, len, OT3D_V1_OFF_TOLERANCE, &out->measured_tolerance_um);
    read_u16_field(payload, len, OT3D_V1_OFF_EMPTY_SPOOL_WEIGHT, &out->empty_spool_weight_g);
    read_u16_field(payload, len, OT3D_V1_OFF_MEASURED_WEIGHT, &out->measured_filament_weight_g);
    read_u16_field(payload, len, OT3D_V1_OFF_MEASURED_LENGTH, &out->measured_filament_length_m);
    read_u8_field(payload, len, OT3D_V1_OFF_MAX_DRY_TEMP, &out->max_dry_temp_encoded);
    read_u8_field(payload, len, OT3D_V1_OFF_DRY_TIME, &out->dry_time_hours);
    read_u8_field(payload, len, OT3D_V1_OFF_MIN_PRINT_TEMP, &out->min_print_temp_encoded);
    read_u8_field(payload, len, OT3D_V1_OFF_MAX_PRINT_TEMP, &out->max_print_temp_encoded);
    read_u8_field(payload, len, OT3D_V1_OFF_MIN_BED_TEMP, &out->min_bed_temp_encoded);
    read_u8_field(payload, len, OT3D_V1_OFF_MAX_BED_TEMP, &out->max_bed_temp_encoded);
    read_u8_field(payload, len, OT3D_V1_OFF_MIN_VSO, &out->min_volumetric_speed);
    read_u8_field(payload, len, OT3D_V1_OFF_MAX_VSO, &out->max_volumetric_speed);
    read_u8_field(payload, len, OT3D_V1_OFF_TARGET_VSO, &out->target_volumetric_speed);

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
    write_u16(buf + OT3D_V1_OFF_TAG_VERSION, tag->tag_version);

    /* Core fields */
    write_str(tag->base_material, buf + OT3D_V1_OFF_MATERIAL, OT3D_V1_LEN_MATERIAL);
    write_str(tag->material_modifiers, buf + OT3D_V1_OFF_MATERIAL_MOD, OT3D_V1_LEN_MATERIAL_MOD);
    /* 0x0C - 0x1A: zero (reserved) */
    write_str(tag->manufacturer, buf + OT3D_V1_OFF_MANUFACTURER, OT3D_V1_LEN_MANUFACTURER);
    write_str(tag->color_name, buf + OT3D_V1_OFF_COLOR_NAME, OT3D_V1_LEN_COLOR_NAME);

    /* 4 RGBA colors */
    memcpy(buf + OT3D_V1_OFF_COLOR_1, tag->color_rgba[0], 4);
    memcpy(buf + OT3D_V1_OFF_COLOR_2, tag->color_rgba[1], 4);
    memcpy(buf + OT3D_V1_OFF_COLOR_3, tag->color_rgba[2], 4);
    memcpy(buf + OT3D_V1_OFF_COLOR_4, tag->color_rgba[3], 4);

    write_u16(buf + OT3D_V1_OFF_DIAMETER, tag->diameter_um);
    write_u16(buf + OT3D_V1_OFF_WEIGHT, tag->target_weight_g);
    buf[OT3D_V1_OFF_PRINT_TEMP] = tag->print_temp_encoded;
    buf[OT3D_V1_OFF_BED_TEMP] = tag->bed_temp_encoded;
    write_u16(buf + OT3D_V1_OFF_DENSITY, tag->density_ugcm3);
    write_u16(buf + OT3D_V1_OFF_TRANSMISSION, tag->transmission_distance);

    int written = OT3D_CORE_SIZE;

    /* Extended fields if buffer is large enough */
    if (buflen >= OT3D_EXTENDED_MIN) {
        write_str(tag->online_url, buf + OT3D_V1_OFF_DATA_URL, OT3D_V1_LEN_DATA_URL);
        write_str(tag->serial_number, buf + OT3D_V1_OFF_SERIAL, OT3D_V1_LEN_SERIAL);

        write_u16(buf + OT3D_V1_OFF_MFG_DATE, tag->manufacture_year);
        buf[OT3D_V1_OFF_MFG_DATE + 2] = tag->manufacture_month;
        buf[OT3D_V1_OFF_MFG_DATE + 3] = tag->manufacture_day;
        buf[OT3D_V1_OFF_MFG_TIME + 0] = tag->manufacture_hour;
        buf[OT3D_V1_OFF_MFG_TIME + 1] = tag->manufacture_minute;
        buf[OT3D_V1_OFF_MFG_TIME + 2] = tag->manufacture_second;

        buf[OT3D_V1_OFF_SPOOL_CORE_DIAMETER] = tag->spool_core_diameter_mm;
        buf[OT3D_V1_OFF_MFI_TEMP] = tag->mfi_temp_encoded;
        buf[OT3D_V1_OFF_MFI_LOAD] = tag->mfi_load;
        buf[OT3D_V1_OFF_MFI_VALUE] = tag->mfi_value;
        buf[OT3D_V1_OFF_TOLERANCE] = tag->measured_tolerance_um;
        write_u16(buf + OT3D_V1_OFF_EMPTY_SPOOL_WEIGHT, tag->empty_spool_weight_g);
        write_u16(buf + OT3D_V1_OFF_MEASURED_WEIGHT, tag->measured_filament_weight_g);
        write_u16(buf + OT3D_V1_OFF_MEASURED_LENGTH, tag->measured_filament_length_m);
        buf[OT3D_V1_OFF_MAX_DRY_TEMP] = tag->max_dry_temp_encoded;
        buf[OT3D_V1_OFF_DRY_TIME] = tag->dry_time_hours;
        buf[OT3D_V1_OFF_MIN_PRINT_TEMP] = tag->min_print_temp_encoded;
        buf[OT3D_V1_OFF_MAX_PRINT_TEMP] = tag->max_print_temp_encoded;
        buf[OT3D_V1_OFF_MIN_BED_TEMP] = tag->min_bed_temp_encoded;
        buf[OT3D_V1_OFF_MAX_BED_TEMP] = tag->max_bed_temp_encoded;
        buf[OT3D_V1_OFF_MIN_VSO] = tag->min_volumetric_speed;
        buf[OT3D_V1_OFF_MAX_VSO] = tag->max_volumetric_speed;
        buf[OT3D_V1_OFF_TARGET_VSO] = tag->target_volumetric_speed;

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

/* ── Raw-payload patch engine (issue #328) ─────────────────────────────
 * A patch never rebuilds the record: it locates the changed fields using the
 * BASELINE record's own major version, zero-fills only a growth gap, and
 * writes only the byte ranges of fields whose semantic value actually
 * differs. Every reserved gap, trailing unknown byte, and untouched field is
 * preserved verbatim. All validation happens before the first mutation, so a
 * rejected patch leaves both the bytes and *len unchanged. */

/* Normalized semantic value of one string field for change detection.
 * dst must hold field_len+1 bytes; result is NUL, <= field_len chars. */
static void ot3d_norm_str(const char *s, size_t field_len, char *dst) {
    size_t n = 0;
    while (n < field_len && s[n] != '\0') n++;
    memcpy(dst, s, n);
    while (n > 0 && dst[n - 1] == ' ') n--;
    dst[n] = '\0';
}

typedef enum { OT3D_F_STR, OT3D_F_U8, OT3D_F_U16, OT3D_F_U48, OT3D_F_RGBA } ot3d_field_kind_t;

typedef struct {
    uint64_t bit;
    size_t off;
    size_t len;
    size_t off1;        /* v1 offset (len1 = len unless noted) */
    size_t len1;
    ot3d_field_kind_t kind;
    size_t member_off;  /* offsetof(opentag3d_t, member) */
} ot3d_field_t;

/* Member offsets are computed at compile time, never hand-written. */
#define OT3D_MEMOFF(m) ((size_t)offsetof(opentag3d_t, m))

/* One entry per presence bit. off/len are the v2 map ranges (authoritative
 * for major 2); off1/len1 are the v1 map ranges (authoritative for major <=1).
 * A 2-byte width applies to every U16 member (manufacture_year and
 * transmission_distance are uint16_t in the struct even where the v2 on-tag
 * field is 1 byte — the encoder/decoder widen/narrow). The len1==0 v2-only
 * entries never dispatch on a v1 baseline: opentag3d_patch_payload rejects
 * their presence bits before the field loop. */
#define OT3D_STR(bit_s, m, off2, len2, off1v) \
    { (bit_s), (off2), (len2), (off1v), (len2), OT3D_F_STR, OT3D_MEMOFF(m) }
#define OT3D_U8(bit_s, m, off2, off1v) \
    { (bit_s), (off2), 1, (off1v), 1, OT3D_F_U8, OT3D_MEMOFF(m) }
#define OT3D_U16(bit_s, m, off2, off1v) \
    { (bit_s), (off2), 2, (off1v), 2, OT3D_F_U16, OT3D_MEMOFF(m) }

static const ot3d_field_t ot3d_fields[] = {
    { OT3D_PATCH_TAG_VERSION,        OT3D_V2_OFF_TAG_VERSION, 2, OT3D_V1_OFF_TAG_VERSION, 2, OT3D_F_U16, OT3D_MEMOFF(tag_version) },
    OT3D_STR(OT3D_PATCH_BASE_MATERIAL,      base_material,            OT3D_V2_OFF_MATERIAL,            OT3D_V2_LEN_MATERIAL,            OT3D_V1_OFF_MATERIAL),
    OT3D_STR(OT3D_PATCH_MATERIAL_MODIFIERS, material_modifiers,       OT3D_V2_OFF_MATERIAL_MOD,        OT3D_V2_LEN_MATERIAL_MOD,        OT3D_V1_OFF_MATERIAL_MOD),
    OT3D_STR(OT3D_PATCH_MANUFACTURER,       manufacturer,             OT3D_V2_OFF_MANUFACTURER,        OT3D_V2_LEN_MANUFACTURER,        OT3D_V1_OFF_MANUFACTURER),
    OT3D_STR(OT3D_PATCH_COLOR_NAME,         color_name,               OT3D_V2_OFF_COLOR_NAME,          OT3D_V2_LEN_COLOR_NAME,          OT3D_V1_OFF_COLOR_NAME),
    { OT3D_PATCH_COLOR_1,            OT3D_V2_OFF_COLOR_1, 4, OT3D_V1_OFF_COLOR_1, 4, OT3D_F_RGBA, OT3D_MEMOFF(color_rgba[0]) },
    { OT3D_PATCH_COLOR_2,            OT3D_V2_OFF_COLOR_2, 4, OT3D_V1_OFF_COLOR_2, 4, OT3D_F_RGBA, OT3D_MEMOFF(color_rgba[1]) },
    { OT3D_PATCH_COLOR_3,            OT3D_V2_OFF_COLOR_3, 4, OT3D_V1_OFF_COLOR_3, 4, OT3D_F_RGBA, OT3D_MEMOFF(color_rgba[2]) },
    { OT3D_PATCH_COLOR_4,            OT3D_V2_OFF_COLOR_4, 4, OT3D_V1_OFF_COLOR_4, 4, OT3D_F_RGBA, OT3D_MEMOFF(color_rgba[3]) },
    OT3D_U16(OT3D_PATCH_DIAMETER,           diameter_um,              OT3D_V2_OFF_DIAMETER,            OT3D_V1_OFF_DIAMETER),
    OT3D_U16(OT3D_PATCH_TARGET_WEIGHT,      target_weight_g,          OT3D_V2_OFF_WEIGHT,              OT3D_V1_OFF_WEIGHT),
    OT3D_U8 (OT3D_PATCH_PRINT_TEMP,         print_temp_encoded,       OT3D_V2_OFF_PRINT_TEMP,          OT3D_V1_OFF_PRINT_TEMP),
    OT3D_U8 (OT3D_PATCH_BED_TEMP,           bed_temp_encoded,         OT3D_V2_OFF_BED_TEMP,            OT3D_V1_OFF_BED_TEMP),
    OT3D_U16(OT3D_PATCH_DENSITY,            density_ugcm3,            OT3D_V2_OFF_DENSITY,             OT3D_V1_OFF_DENSITY),
    { OT3D_PATCH_TRANSMISSION, OT3D_V2_OFF_TD, 1,
      OT3D_V1_OFF_TRANSMISSION, 2, OT3D_F_U16,
      OT3D_MEMOFF(transmission_distance) },
    OT3D_STR(OT3D_PATCH_ONLINE_URL,         online_url,               OT3D_V2_OFF_DATA_URL,            OT3D_V2_LEN_DATA_URL,            OT3D_V1_OFF_DATA_URL),
    { OT3D_PATCH_SERIAL,             OT3D_V2_OFF_SERIAL,   32, OT3D_V1_OFF_SERIAL, 16, OT3D_F_STR,  OT3D_MEMOFF(serial_number) },
    OT3D_U16(OT3D_PATCH_MFG_YEAR,           manufacture_year,         OT3D_V2_OFF_MFG_DATE,            OT3D_V1_OFF_MFG_DATE),
    OT3D_U8 (OT3D_PATCH_MFG_MONTH,          manufacture_month,        OT3D_V2_OFF_MFG_DATE + 2,        OT3D_V1_OFF_MFG_DATE + 2),
    OT3D_U8 (OT3D_PATCH_MFG_DAY,            manufacture_day,          OT3D_V2_OFF_MFG_DATE + 3,        OT3D_V1_OFF_MFG_DATE + 3),
    OT3D_U8 (OT3D_PATCH_MFG_HOUR,           manufacture_hour,         OT3D_V2_OFF_MFG_TIME,            OT3D_V1_OFF_MFG_TIME),
    OT3D_U8 (OT3D_PATCH_MFG_MINUTE,         manufacture_minute,       OT3D_V2_OFF_MFG_TIME + 1,        OT3D_V1_OFF_MFG_TIME + 1),
    OT3D_U8 (OT3D_PATCH_MFG_SECOND,         manufacture_second,       OT3D_V2_OFF_MFG_TIME + 2,        OT3D_V1_OFF_MFG_TIME + 2),
    OT3D_U8 (OT3D_PATCH_SPOOL_CORE,         spool_core_diameter_mm,   OT3D_V2_OFF_SPOOL_CORE_DIAMETER, OT3D_V1_OFF_SPOOL_CORE_DIAMETER),
    OT3D_U8 (OT3D_PATCH_MFI_TEMP,           mfi_temp_encoded,         OT3D_V2_OFF_MFI_TEMP,            OT3D_V1_OFF_MFI_TEMP),
    OT3D_U8 (OT3D_PATCH_MFI_LOAD,           mfi_load,                 OT3D_V2_OFF_MFI_LOAD,            OT3D_V1_OFF_MFI_LOAD),
    OT3D_U8 (OT3D_PATCH_MFI_VALUE,          mfi_value,                OT3D_V2_OFF_MFI_VALUE,           OT3D_V1_OFF_MFI_VALUE),
    OT3D_U8 (OT3D_PATCH_TOLERANCE,          measured_tolerance_um,    OT3D_V2_OFF_TOLERANCE,           OT3D_V1_OFF_TOLERANCE),
    OT3D_U16(OT3D_PATCH_EMPTY_SPOOL_WEIGHT, empty_spool_weight_g,     OT3D_V2_OFF_EMPTY_SPOOL_WEIGHT,  OT3D_V1_OFF_EMPTY_SPOOL_WEIGHT),
    OT3D_U16(OT3D_PATCH_MEASURED_WEIGHT,    measured_filament_weight_g, OT3D_V2_OFF_MEASURED_WEIGHT,   OT3D_V1_OFF_MEASURED_WEIGHT),
    OT3D_U16(OT3D_PATCH_MEASURED_LENGTH,    measured_filament_length_m, OT3D_V2_OFF_MEASURED_LENGTH,   OT3D_V1_OFF_MEASURED_LENGTH),
    OT3D_U8 (OT3D_PATCH_MAX_DRY_TEMP,       max_dry_temp_encoded,     OT3D_V2_OFF_MAX_DRY_TEMP,        OT3D_V1_OFF_MAX_DRY_TEMP),
    OT3D_U8 (OT3D_PATCH_DRY_TIME,           dry_time_hours,           OT3D_V2_OFF_DRY_TIME,            OT3D_V1_OFF_DRY_TIME),
    OT3D_U8 (OT3D_PATCH_MIN_PRINT_TEMP,     min_print_temp_encoded,   OT3D_V2_OFF_MIN_PRINT_TEMP,      OT3D_V1_OFF_MIN_PRINT_TEMP),
    OT3D_U8 (OT3D_PATCH_MAX_PRINT_TEMP,     max_print_temp_encoded,   OT3D_V2_OFF_MAX_PRINT_TEMP,      OT3D_V1_OFF_MAX_PRINT_TEMP),
    OT3D_U8 (OT3D_PATCH_MIN_BED_TEMP,       min_bed_temp_encoded,     OT3D_V2_OFF_MIN_BED_TEMP,        OT3D_V1_OFF_MIN_BED_TEMP),
    OT3D_U8 (OT3D_PATCH_MAX_BED_TEMP,       max_bed_temp_encoded,     OT3D_V2_OFF_MAX_BED_TEMP,        OT3D_V1_OFF_MAX_BED_TEMP),
    OT3D_U8 (OT3D_PATCH_MIN_VSO,            min_volumetric_speed,     OT3D_V2_OFF_MIN_VSO,             OT3D_V1_OFF_MIN_VSO),
    OT3D_U8 (OT3D_PATCH_MAX_VSO,            max_volumetric_speed,     OT3D_V2_OFF_MAX_VSO,             OT3D_V1_OFF_MAX_VSO),
    OT3D_U8 (OT3D_PATCH_TARGET_VSO,         target_volumetric_speed,  OT3D_V2_OFF_TARGET_VSO,          OT3D_V1_OFF_TARGET_VSO),
    { OT3D_PATCH_BARCODE,            OT3D_V2_OFF_BARCODE, 6, 0, 0, OT3D_F_U48, OT3D_MEMOFF(barcode) },             /* v2-only */
    OT3D_STR(OT3D_PATCH_SKU,                 sku,                    OT3D_V2_OFF_SKU,                 OT3D_V2_LEN_SKU,                 0),  /* v2-only */
    OT3D_U8 (OT3D_PATCH_CHAMBER_TEMP,       chamber_temp_encoded,     OT3D_V2_OFF_CHAMBER_TEMP,        0),  /* v2-only */
    OT3D_U8 (OT3D_PATCH_MIN_NOZZLE,         min_nozzle_diameter,      OT3D_V2_OFF_NOZZLE_DIAMETER,     0),  /* v2-only */
};

#define OT3D_FIELD_COUNT (sizeof(ot3d_fields) / sizeof(ot3d_fields[0]))

/* Effective on-tag range of one field descriptor (patch or decoded struct). */
typedef struct {
    const ot3d_field_t *f;
    const uint8_t *val;   /* &member of patch->values or &member of baseline decode */
    size_t off;
    size_t len;
} ot3d_span_t;

static void ot3d_span_for(const ot3d_field_t *f, int is_v2, const void *base, ot3d_span_t *sp) {
    sp->f = f;
    sp->val = (const uint8_t *)base + f->member_off;
    if (is_v2) { sp->off = f->off;  sp->len = f->len; }
    else       { sp->off = f->off1; sp->len = f->len1; }
}

/* Compare desired vs baseline semantic value. Returns 1 when they differ.
 * out_bytes (optional) receives the exact bytes to write (len bytes).
 * Integer members hold decoded host-native values, so equality is a plain
 * member comparison and the write side does the big-endian encoding. */
static int ot3d_field_differs(const ot3d_span_t *want, const ot3d_span_t *cur, uint8_t *out_bytes) {
    char wbuf[33], cbuf[33];
    switch (want->f->kind) {
    case OT3D_F_STR: {
        ot3d_norm_str((const char *)want->val, want->len, wbuf);
        ot3d_norm_str((const char *)cur->val, want->len, cbuf);
        if (strcmp(wbuf, cbuf) != 0) {
            if (out_bytes) {
                size_t n = strlen(wbuf);
                memset(out_bytes, ' ', want->len);
                memcpy(out_bytes, wbuf, n);
            }
            return 1;
        }
        return 0;
    }
    case OT3D_F_U8:
        if (*want->val != *cur->val) {
            if (out_bytes) out_bytes[0] = *want->val;
            return 1;
        }
        return 0;
    case OT3D_F_U16: {
        uint16_t w, c;
        memcpy(&w, want->val, sizeof(w));
        memcpy(&c, cur->val, sizeof(c));
        if (w != c) {
            if (out_bytes) {
                if (want->len == 1) out_bytes[0] = (uint8_t)w;
                else write_u16(out_bytes, w);
            }
            return 1;
        }
        return 0;
    }
    case OT3D_F_U48: {
        uint64_t w, c;
        memcpy(&w, want->val, sizeof(w));
        memcpy(&c, cur->val, sizeof(c));
        if (w != c) {
            if (out_bytes) write_u48(out_bytes, w);
            return 1;
        }
        return 0;
    }
    case OT3D_F_RGBA:
        if (memcmp(want->val, cur->val, 4) != 0) {
            if (out_bytes) memcpy(out_bytes, want->val, 4);
            return 1;
        }
        return 0;
    }
    return -1;
}

opentag3d_result_t opentag3d_patch_payload(uint8_t *raw, size_t *len, size_t capacity,
                                           const opentag3d_patch_t *patch) {
    if (raw == NULL || len == NULL || patch == NULL || *len == 0) return OT3D_PARSE_ERROR;
    if (capacity > OT3D_MAX_PAYLOAD_SIZE) capacity = OT3D_MAX_PAYLOAD_SIZE;
    if (*len > capacity) return OT3D_PARSE_ERROR;

    const uint64_t known_bits = (OT3D_PATCH_MIN_NOZZLE << 1) - 1;
    if ((patch->present & ~known_bits) != 0) return OT3D_PARSE_ERROR;

    /* Version selects the map; must be fully present on the baseline. */
    if (*len < OT3D_V2_OFF_TAG_VERSION + OT3D_V2_LEN_TAG_VERSION) return OT3D_PARSE_ERROR;
    uint16_t base_version = read_u16(raw + OT3D_V2_OFF_TAG_VERSION);
    uint16_t major = opentag3d_major(base_version);
    if (major >= 3) return OT3D_VERSION_ERROR;   /* unknown layout: read-only */
    const int is_v2 = (major == 2);
    if (!is_v2 && (patch->present & OT3D_PATCH_V2_ONLY_BITS) != 0) return OT3D_PARSE_ERROR;

    /* Work on a local comparison copy so the caller's patch is never mutated
     * (opentag3d_patch_t is ~450 B; copied once per call by the scan task).
     * The v2 TD byte is 1 byte wide, so the encoder's 250 clamp applies
     * before diffing — a desired value of 300 equals the on-tag 250 and must
     * not rewrite or flag a change. */
    opentag3d_patch_t local = *patch;
    if (is_v2 && (local.present & OT3D_PATCH_TRANSMISSION) && local.values.transmission_distance > 250) {
        local.values.transmission_distance = 250;
    }
    patch = &local;

    /* Decode the baseline for comparison — never for re-encode: every byte
     * outside a changed field's range stays exactly as the tag reported it,
     * including a same-major newer minor's version bytes and any layout an
     * older reader cannot name. */
    opentag3d_t baseline;
    opentag3d_result_t dres = opentag3d_decode(raw, *len, &baseline);
    if (dres != OT3D_OK && dres != OT3D_VERSION_WARNING) return dres;

    /* A present version bit may only relabel with the SAME value the tag
     * already carries; cross-major conversion and differing-version
     * relabeling are rejected. An absent bit preserves the raw version
     * bytes exactly. */
    if (patch->present & OT3D_PATCH_TAG_VERSION) {
        if (opentag3d_major(patch->values.tag_version) != major ||
            patch->values.tag_version != base_version) {
            return OT3D_VERSION_ERROR;
        }
    }

    /* Pass 1 — preflight: classify every present field, find the highest
     * changed-field end. No mutation happens until this loop succeeds. */
    size_t required_end = *len;
    for (size_t i = 0; i < OT3D_FIELD_COUNT; i++) {
        const ot3d_field_t *f = &ot3d_fields[i];
        if (!(patch->present & f->bit)) continue;
        ot3d_span_t want, cur;
        ot3d_span_for(f, is_v2, &patch->values, &want);
        ot3d_span_for(f, is_v2, &baseline, &cur);
        if (ot3d_field_differs(&want, &cur, NULL) != 1) continue;  /* 0 = no-op */
        if (want.off + want.len > required_end) required_end = want.off + want.len;
    }
    if (required_end > capacity) return OT3D_PARSE_ERROR;

    /* Pass 2 — apply: grow with one zero-fill, then write changed ranges.
     * Strings whose desired value is shorter than their fixed field rewrite
     * their exact padded range, per the presence contract. */
    if (required_end > *len) memset(raw + *len, 0, required_end - *len);
    for (size_t i = 0; i < OT3D_FIELD_COUNT; i++) {
        const ot3d_field_t *f = &ot3d_fields[i];
        if (!(patch->present & f->bit)) continue;
        ot3d_span_t want, cur;
        ot3d_span_for(f, is_v2, &patch->values, &want);
        ot3d_span_for(f, is_v2, &baseline, &cur);
        uint8_t bytes[33];
        if (ot3d_field_differs(&want, &cur, bytes) != 1) continue;
        memcpy(raw + want.off, bytes, want.len);
    }
    *len = required_end;
    return OT3D_OK;
}
