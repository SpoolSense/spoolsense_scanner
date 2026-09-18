#include "opentag3d_lib.h"
#include "opentag3d_v1_map.h"
#include "opentag3d_v2_map.h"
#include <string.h>

/* opentag3d_is_canonical_len() hardcodes 0xE0 instead of including this
 * header's OT3D_V2_MAP_SIZE — this pins the two together so the literal
 * cannot drift silently. */
_Static_assert(OT3D_V2_MAP_SIZE == 0xE0,
               "opentag3d_is_canonical_len hardcodes 0xE0 for the v2 map size");

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

/* v1.00x field descriptor: one entry per on-tag field, driving a single
 * decode loop and a single encode loop instead of two straight-line copies.
 * dst_size is only used for OT3D_V1_FT_STR (read_str's destination bound —
 * not always field len+1: v1's on-tag serial field is 16 bytes into a
 * struct member sized for v2's 32). */
typedef enum {
    OT3D_V1_FT_U8,
    OT3D_V1_FT_U16,
    OT3D_V1_FT_STR,
    OT3D_V1_FT_RAW,   /* fixed-length raw bytes, e.g. one RGBA color */
} ot3d_v1_ftype_t;

typedef struct {
    uint16_t offset;      /* on-tag byte offset */
    uint8_t  len;         /* on-tag field length */
    uint8_t  type;        /* ot3d_v1_ftype_t */
    uint16_t struct_off;  /* offsetof(opentag3d_t, field) */
    uint8_t  dst_size;    /* OT3D_V1_FT_STR only */
} ot3d_v1_fdesc_t;

/* Core fields and extended fields are two separate tables, not one table
 * sliced by a hand-counted index: OT3D_V1_NUM_CORE_FIELDS is derived from
 * OT3D_V1_CORE_FIELDS's own size, so inserting, removing, or reordering a
 * field can never desync a count from the table it counts. A field that
 * belongs in one table but was written into the other is still possible,
 * but that mistake is visible in the diff (which array gained an entry) —
 * not a silent one-line index change. tag_version itself is handled outside
 * both tables: it is read before the v1/v2 branch even happens. */
static const ot3d_v1_fdesc_t OT3D_V1_CORE_FIELDS[] = {
    { OT3D_V1_OFF_MATERIAL, OT3D_V1_LEN_MATERIAL, OT3D_V1_FT_STR,
      offsetof(opentag3d_t, base_material), sizeof(((opentag3d_t *)0)->base_material) },
    { OT3D_V1_OFF_MATERIAL_MOD, OT3D_V1_LEN_MATERIAL_MOD, OT3D_V1_FT_STR,
      offsetof(opentag3d_t, material_modifiers), sizeof(((opentag3d_t *)0)->material_modifiers) },
    { OT3D_V1_OFF_MANUFACTURER, OT3D_V1_LEN_MANUFACTURER, OT3D_V1_FT_STR,
      offsetof(opentag3d_t, manufacturer), sizeof(((opentag3d_t *)0)->manufacturer) },
    { OT3D_V1_OFF_COLOR_NAME, OT3D_V1_LEN_COLOR_NAME, OT3D_V1_FT_STR,
      offsetof(opentag3d_t, color_name), sizeof(((opentag3d_t *)0)->color_name) },
    { OT3D_V1_OFF_COLOR_1, OT3D_V1_LEN_COLOR_1, OT3D_V1_FT_RAW, offsetof(opentag3d_t, color_rgba[0]), 0 },
    { OT3D_V1_OFF_COLOR_2, OT3D_V1_LEN_COLOR_2, OT3D_V1_FT_RAW, offsetof(opentag3d_t, color_rgba[1]), 0 },
    { OT3D_V1_OFF_COLOR_3, OT3D_V1_LEN_COLOR_3, OT3D_V1_FT_RAW, offsetof(opentag3d_t, color_rgba[2]), 0 },
    { OT3D_V1_OFF_COLOR_4, OT3D_V1_LEN_COLOR_4, OT3D_V1_FT_RAW, offsetof(opentag3d_t, color_rgba[3]), 0 },
    { OT3D_V1_OFF_DIAMETER, OT3D_V1_LEN_DIAMETER, OT3D_V1_FT_U16, offsetof(opentag3d_t, diameter_um), 0 },
    { OT3D_V1_OFF_WEIGHT, OT3D_V1_LEN_WEIGHT, OT3D_V1_FT_U16, offsetof(opentag3d_t, target_weight_g), 0 },
    { OT3D_V1_OFF_PRINT_TEMP, OT3D_V1_LEN_PRINT_TEMP, OT3D_V1_FT_U8, offsetof(opentag3d_t, print_temp_encoded), 0 },
    { OT3D_V1_OFF_BED_TEMP, OT3D_V1_LEN_BED_TEMP, OT3D_V1_FT_U8, offsetof(opentag3d_t, bed_temp_encoded), 0 },
    { OT3D_V1_OFF_DENSITY, OT3D_V1_LEN_DENSITY, OT3D_V1_FT_U16, offsetof(opentag3d_t, density_ugcm3), 0 },
    { OT3D_V1_OFF_TD, OT3D_V1_LEN_TD, OT3D_V1_FT_U16, offsetof(opentag3d_t, transmission_distance), 0 },
};
#define OT3D_V1_NUM_CORE_FIELDS (sizeof(OT3D_V1_CORE_FIELDS) / sizeof(OT3D_V1_CORE_FIELDS[0]))

/* Compile-time guard: OT3D_V1_CORE_FIELDS must still end exactly where
 * OT3D_V1_CORE_SIZE says the core map ends. This is a pure macro comparison
 * (no reading of table contents, which is not a portable constant
 * expression), so it only pins the header's own TD constants -- it cannot
 * see whether a field is in the wrong table. That risk is instead closed by
 * construction: a core-only buffer only ever runs OT3D_V1_CORE_FIELDS, whose
 * length is derived by sizeof, never hand-counted. */
_Static_assert(OT3D_V1_OFF_TD + OT3D_V1_LEN_TD == OT3D_V1_CORE_SIZE,
    "v1 core map no longer ends with TD at OT3D_V1_CORE_SIZE");

static const ot3d_v1_fdesc_t OT3D_V1_EXTENDED_FIELDS[] = {
    { OT3D_V1_OFF_DATA_URL, OT3D_V1_LEN_DATA_URL, OT3D_V1_FT_STR,
      offsetof(opentag3d_t, online_url), sizeof(((opentag3d_t *)0)->online_url) },
    { OT3D_V1_OFF_SERIAL, OT3D_V1_LEN_SERIAL, OT3D_V1_FT_STR,
      offsetof(opentag3d_t, serial_number), sizeof(((opentag3d_t *)0)->serial_number) },
    { OT3D_V1_OFF_MFG_DATE, 2, OT3D_V1_FT_U16, offsetof(opentag3d_t, manufacture_year), 0 },
    { OT3D_V1_OFF_MFG_DATE + 2, 1, OT3D_V1_FT_U8, offsetof(opentag3d_t, manufacture_month), 0 },
    { OT3D_V1_OFF_MFG_DATE + 3, 1, OT3D_V1_FT_U8, offsetof(opentag3d_t, manufacture_day), 0 },
    { OT3D_V1_OFF_MFG_TIME + 0, 1, OT3D_V1_FT_U8, offsetof(opentag3d_t, manufacture_hour), 0 },
    { OT3D_V1_OFF_MFG_TIME + 1, 1, OT3D_V1_FT_U8, offsetof(opentag3d_t, manufacture_minute), 0 },
    { OT3D_V1_OFF_MFG_TIME + 2, 1, OT3D_V1_FT_U8, offsetof(opentag3d_t, manufacture_second), 0 },
    { OT3D_V1_OFF_SPOOL_CORE_DIAMETER, OT3D_V1_LEN_SPOOL_CORE_DIAMETER, OT3D_V1_FT_U8,
      offsetof(opentag3d_t, spool_core_diameter_mm), 0 },
    { OT3D_V1_OFF_MFI_TEMP, OT3D_V1_LEN_MFI_TEMP, OT3D_V1_FT_U8, offsetof(opentag3d_t, mfi_temp_encoded), 0 },
    { OT3D_V1_OFF_MFI_LOAD, OT3D_V1_LEN_MFI_LOAD, OT3D_V1_FT_U8, offsetof(opentag3d_t, mfi_load), 0 },
    { OT3D_V1_OFF_MFI_VALUE, OT3D_V1_LEN_MFI_VALUE, OT3D_V1_FT_U8, offsetof(opentag3d_t, mfi_value), 0 },
    { OT3D_V1_OFF_TOLERANCE, OT3D_V1_LEN_TOLERANCE, OT3D_V1_FT_U8, offsetof(opentag3d_t, measured_tolerance_um), 0 },
    { OT3D_V1_OFF_EMPTY_SPOOL_WEIGHT, OT3D_V1_LEN_EMPTY_SPOOL_WEIGHT, OT3D_V1_FT_U16,
      offsetof(opentag3d_t, empty_spool_weight_g), 0 },
    { OT3D_V1_OFF_MEASURED_WEIGHT, OT3D_V1_LEN_MEASURED_WEIGHT, OT3D_V1_FT_U16,
      offsetof(opentag3d_t, measured_filament_weight_g), 0 },
    { OT3D_V1_OFF_MEASURED_LENGTH, OT3D_V1_LEN_MEASURED_LENGTH, OT3D_V1_FT_U16,
      offsetof(opentag3d_t, measured_filament_length_m), 0 },
    { OT3D_V1_OFF_MAX_DRY_TEMP, OT3D_V1_LEN_MAX_DRY_TEMP, OT3D_V1_FT_U8, offsetof(opentag3d_t, max_dry_temp_encoded), 0 },
    { OT3D_V1_OFF_DRY_TIME, OT3D_V1_LEN_DRY_TIME, OT3D_V1_FT_U8, offsetof(opentag3d_t, dry_time_hours), 0 },
    { OT3D_V1_OFF_MIN_PRINT_TEMP, OT3D_V1_LEN_MIN_PRINT_TEMP, OT3D_V1_FT_U8, offsetof(opentag3d_t, min_print_temp_encoded), 0 },
    { OT3D_V1_OFF_MAX_PRINT_TEMP, OT3D_V1_LEN_MAX_PRINT_TEMP, OT3D_V1_FT_U8, offsetof(opentag3d_t, max_print_temp_encoded), 0 },
    { OT3D_V1_OFF_MIN_BED_TEMP, OT3D_V1_LEN_MIN_BED_TEMP, OT3D_V1_FT_U8, offsetof(opentag3d_t, min_bed_temp_encoded), 0 },
    { OT3D_V1_OFF_MAX_BED_TEMP, OT3D_V1_LEN_MAX_BED_TEMP, OT3D_V1_FT_U8, offsetof(opentag3d_t, max_bed_temp_encoded), 0 },
    { OT3D_V1_OFF_MIN_VSO, OT3D_V1_LEN_MIN_VSO, OT3D_V1_FT_U8, offsetof(opentag3d_t, min_volumetric_speed), 0 },
    { OT3D_V1_OFF_MAX_VSO, OT3D_V1_LEN_MAX_VSO, OT3D_V1_FT_U8, offsetof(opentag3d_t, max_volumetric_speed), 0 },
    { OT3D_V1_OFF_TARGET_VSO, OT3D_V1_LEN_TARGET_VSO, OT3D_V1_FT_U8, offsetof(opentag3d_t, target_volumetric_speed), 0 },
};
#define OT3D_V1_NUM_EXT_FIELDS (sizeof(OT3D_V1_EXTENDED_FIELDS) / sizeof(OT3D_V1_EXTENDED_FIELDS[0]))

/* Decode `count` fields of `fields` (OT3D_V1_CORE_FIELDS or
 * OT3D_V1_EXTENDED_FIELDS) from `payload` into `out`. */
static void opentag3d_decode_v1_field_list(const uint8_t *payload, opentag3d_t *out,
                                            const ot3d_v1_fdesc_t *fields, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const ot3d_v1_fdesc_t *f = &fields[i];
        uint8_t *dst = (uint8_t *)out + f->struct_off;
        switch (f->type) {
        case OT3D_V1_FT_U8:
            *dst = payload[f->offset];
            break;
        case OT3D_V1_FT_U16:
            *(uint16_t *)dst = read_u16(payload + f->offset);
            break;
        case OT3D_V1_FT_STR:
            read_str(payload + f->offset, f->len, (char *)dst, f->dst_size);
            break;
        case OT3D_V1_FT_RAW:
            memcpy(dst, payload + f->offset, f->len);
            break;
        }
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

    /* Core fields, then extended fields if we have enough data */
    opentag3d_decode_v1_field_list(payload, out, OT3D_V1_CORE_FIELDS, OT3D_V1_NUM_CORE_FIELDS);
    if (len >= OT3D_EXTENDED_MIN) {
        out->has_extended = 1;
        opentag3d_decode_v1_field_list(payload, out, OT3D_V1_EXTENDED_FIELDS, OT3D_V1_NUM_EXT_FIELDS);
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

/* Encode `count` fields of `fields` — the same tables and helper
 * opentag3d_decode_v1_field_list uses, run in the opposite direction. */
static void opentag3d_encode_v1_field_list(const opentag3d_t *tag, uint8_t *buf,
                                            const ot3d_v1_fdesc_t *fields, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const ot3d_v1_fdesc_t *f = &fields[i];
        const uint8_t *src = (const uint8_t *)tag + f->struct_off;
        switch (f->type) {
        case OT3D_V1_FT_U8:
            buf[f->offset] = *src;
            break;
        case OT3D_V1_FT_U16:
            write_u16(buf + f->offset, *(const uint16_t *)src);
            break;
        case OT3D_V1_FT_STR:
            write_str((const char *)src, buf + f->offset, f->len);
            break;
        case OT3D_V1_FT_RAW:
            memcpy(buf + f->offset, src, f->len);
            break;
        }
    }
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

    opentag3d_encode_v1_field_list(tag, buf, OT3D_V1_CORE_FIELDS, OT3D_V1_NUM_CORE_FIELDS);
    int written = OT3D_CORE_SIZE;
    if (buflen >= OT3D_EXTENDED_MIN) {
        opentag3d_encode_v1_field_list(tag, buf, OT3D_V1_EXTENDED_FIELDS, OT3D_V1_NUM_EXT_FIELDS);
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
