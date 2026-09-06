#ifndef OPENTAG3D_LIB_H
#define OPENTAG3D_LIB_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Spec version this library supports */
#define OT3D_SUPPORTED_MAJOR  2
#define OT3D_SUPPORTED_MINOR  0
#define OT3D_SUPPORTED_V1     1000  /* newest v1.x we can fully parse */
#define OT3D_SUPPORTED_V2     2000  /* newest v2.x we can fully parse */
#define OT3D_SUPPORTED_VERSION OT3D_SUPPORTED_V2

/* NDEF MIME type for detection */
#define OT3D_MIME_TYPE "application/opentag3d"

/* Minimum payload sizes */
#define OT3D_CORE_SIZE    0x66   /* 102 bytes — core fields through transmission distance */
#define OT3D_EXTENDED_MIN 0xBB   /* 187 bytes — includes all extended fields */

/* Result codes */
typedef enum {
    OT3D_OK = 0,
    OT3D_VERSION_WARNING,   /* Minor version ahead of reader — warn, parse anyway */
    OT3D_VERSION_ERROR,     /* Major version ahead of reader — do not parse */
    OT3D_PARSE_ERROR,       /* Data too short or invalid */
} opentag3d_result_t;

/* Parsed OpenTag3D data */
typedef struct {
    uint16_t tag_version;            /* Raw version number (e.g. 1000 = v1.000) */

    /* Core fields */
    char     base_material[6];       /* 5 bytes + null, e.g. "PLA  " */
    char     material_modifiers[6];  /* 5 bytes + null, optional */
    char     manufacturer[17];       /* 16 bytes + null */
    char     color_name[33];         /* 32 bytes + null, optional */
    uint8_t  color_rgba[4][4];       /* 4 colors, each RGBA */
    uint16_t diameter_um;            /* Micrometers (1750 = 1.75mm) */
    uint16_t target_weight_g;        /* Grams (total spool) */
    uint8_t  print_temp_encoded;     /* °C ÷ 5 */
    uint8_t  bed_temp_encoded;       /* °C ÷ 5 */
    uint16_t density_ugcm3;          /* NOTE: mg/cm³ on-tag despite the name —
                                        a u16 can't hold µg/cm³; writers encode
                                        round(g/cm³ * 1000). Divide by 1000. */
    uint16_t transmission_distance;  /* mm ÷ 0.1, optional */

    /* Extended fields (zero if not present) */
    uint8_t  has_extended;           /* Non-zero if extended fields were parsed */
    char     online_url[33];         /* 32 bytes + null, no https:// prefix */
    char     serial_number[33];      /* 32 bytes + null (v2 stores 32; v1 stores 16 — copies bound by sizeof) */
    uint16_t manufacture_year;
    uint8_t  manufacture_month;
    uint8_t  manufacture_day;
    uint8_t  manufacture_hour;
    uint8_t  manufacture_minute;
    uint8_t  manufacture_second;
    uint8_t  spool_core_diameter_mm;
    uint8_t  mfi_temp_encoded;       /* °C ÷ 5 */
    uint8_t  mfi_load;               /* g ÷ 10 */
    uint8_t  mfi_value;              /* g/10min ÷ 10 */
    uint8_t  measured_tolerance_um;
    uint16_t empty_spool_weight_g;
    uint16_t measured_filament_weight_g;
    uint16_t measured_filament_length_m;
    uint8_t  max_dry_temp_encoded;   /* °C ÷ 5 */
    uint8_t  dry_time_hours;
    uint8_t  min_print_temp_encoded; /* °C ÷ 5 */
    uint8_t  max_print_temp_encoded; /* °C ÷ 5 */
    uint8_t  min_bed_temp_encoded;   /* °C ÷ 5 */
    uint8_t  max_bed_temp_encoded;   /* °C ÷ 5 */
    uint8_t  min_volumetric_speed;   /* mm³/s */
    uint8_t  max_volumetric_speed;   /* mm³/s */
    uint8_t  target_volumetric_speed;/* mm³/s */

    /* Fields that exist only in the v2.000 memory map
     * (zero when a v1 tag was parsed) */
    char     sku[17];                  /* 16 bytes + null (v2) */
    uint64_t barcode;                  /* UPC13/GTIN, 6-byte big-endian int on tag (v2) */
    uint8_t  chamber_temp_encoded;     /* °C ÷ 5 — required field in v2 */
    uint8_t  min_nozzle_diameter;      /* mm ÷ 0.1 (v2) */
} opentag3d_t;

/**
 * Decode OpenTag3D binary payload into struct.
 * payload: raw bytes starting after the NDEF MIME type record header.
 * len: number of bytes available.
 * out: decoded data (zeroed first, then populated).
 *
 * Returns OT3D_OK on success, OT3D_VERSION_WARNING if minor version is ahead,
 * OT3D_VERSION_ERROR if major version is ahead, OT3D_PARSE_ERROR if too short.
 */
opentag3d_result_t opentag3d_decode(const uint8_t *payload, size_t len, opentag3d_t *out);

/**
 * Encode opentag3d_t struct to binary payload for writing.
 * buf: output buffer.
 * buflen: size of output buffer.
 *
 * Returns number of bytes written, or -1 on error.
 * Writes core fields only if buflen < OT3D_EXTENDED_MIN, otherwise writes all.
 */
int opentag3d_encode(const opentag3d_t *tag, uint8_t *buf, size_t buflen);

/* Inline helpers for decoded temperature/dimension values */
static inline float opentag3d_temp_c(uint8_t encoded) { return encoded * 5.0f; }
static inline float opentag3d_diameter_mm(const opentag3d_t *t) { return t->diameter_um / 1000.0f; }
static inline float opentag3d_density_gcc(const opentag3d_t *t) { return t->density_ugcm3 / 1000.0f; }
static inline float opentag3d_transmission_mm(const opentag3d_t *t) { return t->transmission_distance * 0.1f; }

#ifdef __cplusplus
}
#endif

#endif /* OPENTAG3D_LIB_H */
