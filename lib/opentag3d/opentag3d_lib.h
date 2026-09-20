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
#define OT3D_SUPPORTED_V1     1003  /* newest v1.x we can fully parse */
#define OT3D_SUPPORTED_V2     2003  /* newest v2.x we can fully parse */
#define OT3D_SUPPORTED_VERSION OT3D_SUPPORTED_V2

/* NDEF MIME type for detection */
#define OT3D_MIME_TYPE "application/opentag3d"

/* Minimum payload sizes (v2: OT3D_V2_MIN_SIZE in opentag3d_v2_map.h) */
#define OT3D_CORE_SIZE    0x66   /* 102 bytes — core fields through transmission distance */
#define OT3D_EXTENDED_START 0x70 /* 112 — first extended field; 0x66..0x6F is reserved */
#define OT3D_EXTENDED_MIN 0xBB   /* 187 bytes — includes all extended fields */

/* Hard ceiling for a raw-payload record that raw patching can grow into.
 * Sized so the NDEF TLV of a full record fits NTAG216-class user memory
 * (500-byte payload -> 532-byte padded TLV -> 133 pages from page 4). */
#define OT3D_MAX_PAYLOAD_SIZE 500

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
    uint64_t barcode;                  /* UPC13/GTIN, 6-byte big-endian int on tag (v2);
                                          declared before the char arrays so the u64
                                          lands on a natural boundary with no padding */
    char     sku[17];                  /* 16 bytes + null (v2) */
    uint8_t  chamber_temp_encoded;     /* °C ÷ 5 — required field in v2 */
    uint8_t  min_nozzle_diameter;      /* mm ÷ 0.1 (v2) */
} opentag3d_t;

/**
 * Decode OpenTag3D binary payload into struct.
 * payload: raw bytes starting after the NDEF MIME type record header.
 * len: number of bytes available.
 * out: decoded data (zeroed first, then populated).
 *
 * Once the two-byte version and at least one field byte are present, decoding
 * is bounded by len: every complete field in a short known-major record is
 * returned and fields beyond the cut stay zero. The NFC scan path separately
 * requires the RF read length to equal the NDEF record's declared length.
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
 * The struct's tag_version selects the on-tag layout: 2xxx encodes the full
 * v2 map (needs buflen >= OT3D_V2_MAP_SIZE), 1xxx/0 encodes the v1 layout
 * (core only if buflen < OT3D_EXTENDED_MIN, otherwise core+extended).
 *
 * Returns number of bytes written, or -1 on error — including any version
 * this build cannot re-encode faithfully (see opentag3d_can_encode).
 */
int opentag3d_encode(const opentag3d_t *tag, uint8_t *buf, size_t buflen);

/* Major spec revision of a raw version value (2000 → 2). */
static inline uint16_t opentag3d_major(uint16_t version) { return version / 1000; }

/* Non-zero when this build can re-encode a tag of the given version without
 * losing data. Newer minors (2.001+, 1.001+) define bytes we would zero, and
 * unknown majors have unknown layouts — both refuse. Single source of truth
 * for the encoder dispatcher and every write-side guard. */
static inline int opentag3d_can_encode(uint16_t version) {
    uint16_t major = version / 1000;
    if (major >= 3) return 0;
    if (major == 2) return version <= OT3D_SUPPORTED_V2;
    return version <= OT3D_SUPPORTED_V1;
}

/* Inline helpers for decoded temperature/dimension values */
static inline float opentag3d_temp_c(uint8_t encoded) { return encoded * 5.0f; }
static inline float opentag3d_diameter_mm(const opentag3d_t *t) { return t->diameter_um / 1000.0f; }
static inline float opentag3d_density_gcc(const opentag3d_t *t) { return t->density_ugcm3 / 1000.0f; }
static inline float opentag3d_transmission_mm(const opentag3d_t *t) { return t->transmission_distance * 0.1f; }

/* ── Raw-payload patch engine (issue #328) ─────────────────────────────
 * Instead of re-encoding a whole canonical record (which memsets and drops
 * reserved gaps, bytes past the canonical map, and unknown fields), a write
 * expresses field INTENT: a presence mask plus the desired values. Absent
 * bits preserve the baseline bytes verbatim; a present string/zero clears
 * that exact fixed-width range. The patcher works on the baseline record's
 * own major version, grows a short record only when a CHANGED field extends
 * past its end (zero-filling just the gap), and never shrinks. */

/* One bit per editable logical field/subfield. */
#define OT3D_PATCH_TAG_VERSION         (1ULL << 0)
#define OT3D_PATCH_BASE_MATERIAL       (1ULL << 1)
#define OT3D_PATCH_MATERIAL_MODIFIERS  (1ULL << 2)
#define OT3D_PATCH_MANUFACTURER        (1ULL << 3)
#define OT3D_PATCH_COLOR_NAME          (1ULL << 4)
#define OT3D_PATCH_COLOR_1             (1ULL << 5)
#define OT3D_PATCH_COLOR_2             (1ULL << 6)
#define OT3D_PATCH_COLOR_3             (1ULL << 7)
#define OT3D_PATCH_COLOR_4             (1ULL << 8)
#define OT3D_PATCH_DIAMETER            (1ULL << 9)
#define OT3D_PATCH_TARGET_WEIGHT       (1ULL << 10)
#define OT3D_PATCH_PRINT_TEMP          (1ULL << 11)
#define OT3D_PATCH_BED_TEMP            (1ULL << 12)
#define OT3D_PATCH_DENSITY             (1ULL << 13)
#define OT3D_PATCH_TRANSMISSION        (1ULL << 14)
#define OT3D_PATCH_ONLINE_URL          (1ULL << 15)
#define OT3D_PATCH_SERIAL              (1ULL << 16)
#define OT3D_PATCH_MFG_YEAR            (1ULL << 17)
#define OT3D_PATCH_MFG_MONTH           (1ULL << 18)
#define OT3D_PATCH_MFG_DAY             (1ULL << 19)
#define OT3D_PATCH_MFG_HOUR            (1ULL << 20)
#define OT3D_PATCH_MFG_MINUTE          (1ULL << 21)
#define OT3D_PATCH_MFG_SECOND          (1ULL << 22)
#define OT3D_PATCH_SPOOL_CORE          (1ULL << 23)
#define OT3D_PATCH_MFI_TEMP            (1ULL << 24)
#define OT3D_PATCH_MFI_LOAD            (1ULL << 25)
#define OT3D_PATCH_MFI_VALUE           (1ULL << 26)
#define OT3D_PATCH_TOLERANCE           (1ULL << 27)
#define OT3D_PATCH_EMPTY_SPOOL_WEIGHT  (1ULL << 28)
#define OT3D_PATCH_MEASURED_WEIGHT     (1ULL << 29)
#define OT3D_PATCH_MEASURED_LENGTH     (1ULL << 30)
#define OT3D_PATCH_MAX_DRY_TEMP        (1ULL << 31)
#define OT3D_PATCH_DRY_TIME            (1ULL << 32)
#define OT3D_PATCH_MIN_PRINT_TEMP      (1ULL << 33)
#define OT3D_PATCH_MAX_PRINT_TEMP      (1ULL << 34)
#define OT3D_PATCH_MIN_BED_TEMP        (1ULL << 35)
#define OT3D_PATCH_MAX_BED_TEMP        (1ULL << 36)
#define OT3D_PATCH_MIN_VSO             (1ULL << 37)
#define OT3D_PATCH_MAX_VSO             (1ULL << 38)
#define OT3D_PATCH_TARGET_VSO          (1ULL << 39)
#define OT3D_PATCH_BARCODE             (1ULL << 40)  /* v2 map only */
#define OT3D_PATCH_SKU                 (1ULL << 41)  /* v2 map only */
#define OT3D_PATCH_CHAMBER_TEMP        (1ULL << 42)  /* v2 map only */
#define OT3D_PATCH_MIN_NOZZLE          (1ULL << 43)  /* v2 map only */

/* Bits with no defined range in the v1 memory map: present on a v1 baseline
 * is a caller error, not a silent no-op. */
#define OT3D_PATCH_V2_ONLY_BITS (OT3D_PATCH_BARCODE | OT3D_PATCH_SKU | \
                                 OT3D_PATCH_CHAMBER_TEMP | OT3D_PATCH_MIN_NOZZLE)

typedef struct {
    uint64_t present;      /* bitmask of OT3D_PATCH_* bits actually supplied */
    opentag3d_t values;    /* desired values for the present fields */
} opentag3d_patch_t;

/**
 * Apply a field-intent patch to a raw OpenTag3D payload in place.
 * raw:       baseline record bytes (payload after the NDEF MIME header).
 * len:       in: baseline length; out: new length (only grows on success).
 * capacity:  writable capacity of raw (e.g. OT3D_MAX_PAYLOAD_SIZE).
 * patch:     presence mask + desired values.
 *
 * Preflights every change before mutating: unknown baseline major, present
 * v2-only fields on a v1 record, cross-major version relabeling, or a growth
 * past capacity all return an error with BOTH bytes and *len unchanged.
 * Present fields whose semantic value already matches the baseline are
 * no-ops — no padding rewrite, no growth.
 *
 * Returns OT3D_OK on success (zero changed fields is OK), OT3D_VERSION_ERROR
 * for an unknown major or illegal version relabel, OT3D_PARSE_ERROR for
 * invalid input or insufficient capacity.
 */
opentag3d_result_t opentag3d_patch_payload(uint8_t *raw, size_t *len, size_t capacity,
                                           const opentag3d_patch_t *patch);

#ifdef __cplusplus
}
#endif

#endif /* OPENTAG3D_LIB_H */
