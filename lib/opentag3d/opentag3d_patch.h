#ifndef OPENTAG3D_PATCH_H
#define OPENTAG3D_PATCH_H

#include <stdint.h>
#include "opentag3d_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 'OT3P' — marks a staging buffer as a patch envelope rather than the bare
 * opentag3d_t that DeductionManager still queues. */
#define OT3D_PATCH_MAGIC 0x4F543350u

#define OT3D_MODE_PRESERVE 0
#define OT3D_MODE_REPLACE  1

typedef enum {
    /* Core */
    OT3D_F_BASE_MATERIAL = 0,
    OT3D_F_MATERIAL_MODIFIERS,
    OT3D_F_MANUFACTURER,
    OT3D_F_COLOR_NAME,
    OT3D_F_COLOR0_RGB,
    OT3D_F_COLOR0_ALPHA,
    OT3D_F_COLOR1,
    OT3D_F_COLOR2,
    OT3D_F_COLOR3,
    OT3D_F_DIAMETER_UM,
    OT3D_F_TARGET_WEIGHT_G,
    OT3D_F_PRINT_TEMP,
    OT3D_F_BED_TEMP,
    OT3D_F_DENSITY,
    OT3D_F_TRANSMISSION_DISTANCE,
    /* Extended — first extended id, keep contiguous */
    OT3D_F_ONLINE_URL,
    OT3D_F_SERIAL_NUMBER,
    OT3D_F_MANUFACTURE_YEAR,
    OT3D_F_MANUFACTURE_MONTH,
    OT3D_F_MANUFACTURE_DAY,
    OT3D_F_MANUFACTURE_HOUR,
    OT3D_F_MANUFACTURE_MINUTE,
    OT3D_F_MANUFACTURE_SECOND,
    OT3D_F_SPOOL_CORE_DIAMETER,
    OT3D_F_MFI_TEMP,
    OT3D_F_MFI_LOAD,
    OT3D_F_MFI_VALUE,
    OT3D_F_MEASURED_TOLERANCE,
    OT3D_F_EMPTY_SPOOL_WEIGHT_G,
    OT3D_F_MEASURED_WEIGHT_G,
    OT3D_F_MEASURED_LENGTH_M,
    OT3D_F_MAX_DRY_TEMP,
    OT3D_F_DRY_TIME_HOURS,
    OT3D_F_MIN_PRINT_TEMP,
    OT3D_F_MAX_PRINT_TEMP,
    OT3D_F_MIN_BED_TEMP,
    OT3D_F_MAX_BED_TEMP,
    OT3D_F_MIN_VOLUMETRIC_SPEED,
    OT3D_F_MAX_VOLUMETRIC_SPEED,
    OT3D_F_TARGET_VOLUMETRIC_SPEED,
    /* v2-only — first v2-only id, keep contiguous and last */
    OT3D_F_BARCODE,
    OT3D_F_SKU,
    OT3D_F_CHAMBER_TEMP,
    OT3D_F_MIN_NOZZLE_DIAMETER,
    OT3D_F_COUNT
} ot3d_field_t;

#define OT3D_F_FIRST_EXTENDED OT3D_F_ONLINE_URL
#define OT3D_F_FIRST_V2_ONLY  OT3D_F_BARCODE

/* Bits [FIRST_EXTENDED, COUNT) — v2-only fields live in the extended half of
 * the v2 map too, so they count as extended for v1 has_extended reasoning. */
#define OT3D_EXT_FIELD_MASK \
    (((1ULL << (OT3D_F_COUNT - OT3D_F_FIRST_EXTENDED)) - 1ULL) << OT3D_F_FIRST_EXTENDED)

#define OT3D_V2_ONLY_FIELD_MASK \
    (((1ULL << (OT3D_F_COUNT - OT3D_F_FIRST_V2_ONLY)) - 1ULL) << OT3D_F_FIRST_V2_ONLY)

typedef struct {
    uint32_t    magic;
    uint8_t     mode;
    uint8_t     confirm_replace;   /* was pad[0] — replacing a written tag */
    uint8_t     pad[2];
    uint64_t    mask;
    opentag3d_t values;
} ot3d_patch_t;

static inline void ot3d_mask_set(uint64_t *m, ot3d_field_t f) { *m |= 1ULL << (unsigned)f; }
static inline int  ot3d_mask_get(uint64_t m, ot3d_field_t f)  { return (int)((m >> (unsigned)f) & 1ULL); }

/* Copies from patch onto base only the fields whose presence bit is set.
 * tag_version and has_extended are NOT patchable here — the caller owns
 * layout decisions. */
void opentag3d_apply_patch(opentag3d_t *base, const opentag3d_t *patch, uint64_t mask);

int opentag3d_patch_touches_extended(uint64_t mask);
int opentag3d_patch_touches_v2_only(uint64_t mask);

#ifdef __cplusplus
}
#endif

#endif /* OPENTAG3D_PATCH_H */
