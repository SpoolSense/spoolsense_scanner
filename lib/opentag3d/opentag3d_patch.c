#include <string.h>
#include "opentag3d_patch.h"

#define APPLY(field_id, stmt) do { if (ot3d_mask_get(mask, field_id)) { stmt; } } while (0)
#define APPLY_STR(field_id, member) \
    APPLY(field_id, memcpy(base->member, patch->member, sizeof(base->member)))

void opentag3d_apply_patch(opentag3d_t *base, const opentag3d_t *patch, uint64_t mask) {
    if (!base || !patch || mask == 0) return;

    APPLY_STR(OT3D_F_BASE_MATERIAL,      base_material);
    APPLY_STR(OT3D_F_MATERIAL_MODIFIERS, material_modifiers);
    APPLY_STR(OT3D_F_MANUFACTURER,       manufacturer);
    APPLY_STR(OT3D_F_COLOR_NAME,         color_name);

    APPLY(OT3D_F_COLOR0_RGB, memcpy(base->color_rgba[0], patch->color_rgba[0], 3));
    APPLY(OT3D_F_COLOR0_ALPHA, base->color_rgba[0][3] = patch->color_rgba[0][3]);
    APPLY(OT3D_F_COLOR1, memcpy(base->color_rgba[1], patch->color_rgba[1], 4));
    APPLY(OT3D_F_COLOR2, memcpy(base->color_rgba[2], patch->color_rgba[2], 4));
    APPLY(OT3D_F_COLOR3, memcpy(base->color_rgba[3], patch->color_rgba[3], 4));

    APPLY(OT3D_F_DIAMETER_UM,           base->diameter_um = patch->diameter_um);
    APPLY(OT3D_F_TARGET_WEIGHT_G,       base->target_weight_g = patch->target_weight_g);
    APPLY(OT3D_F_PRINT_TEMP,            base->print_temp_encoded = patch->print_temp_encoded);
    APPLY(OT3D_F_BED_TEMP,              base->bed_temp_encoded = patch->bed_temp_encoded);
    APPLY(OT3D_F_DENSITY,               base->density_ugcm3 = patch->density_ugcm3);
    APPLY(OT3D_F_TRANSMISSION_DISTANCE, base->transmission_distance = patch->transmission_distance);

    APPLY_STR(OT3D_F_ONLINE_URL,    online_url);
    APPLY_STR(OT3D_F_SERIAL_NUMBER, serial_number);

    APPLY(OT3D_F_MANUFACTURE_YEAR,   base->manufacture_year = patch->manufacture_year);
    APPLY(OT3D_F_MANUFACTURE_MONTH,  base->manufacture_month = patch->manufacture_month);
    APPLY(OT3D_F_MANUFACTURE_DAY,    base->manufacture_day = patch->manufacture_day);
    APPLY(OT3D_F_MANUFACTURE_HOUR,   base->manufacture_hour = patch->manufacture_hour);
    APPLY(OT3D_F_MANUFACTURE_MINUTE, base->manufacture_minute = patch->manufacture_minute);
    APPLY(OT3D_F_MANUFACTURE_SECOND, base->manufacture_second = patch->manufacture_second);

    APPLY(OT3D_F_SPOOL_CORE_DIAMETER, base->spool_core_diameter_mm = patch->spool_core_diameter_mm);
    APPLY(OT3D_F_MFI_TEMP,            base->mfi_temp_encoded = patch->mfi_temp_encoded);
    APPLY(OT3D_F_MFI_LOAD,            base->mfi_load = patch->mfi_load);
    APPLY(OT3D_F_MFI_VALUE,           base->mfi_value = patch->mfi_value);
    APPLY(OT3D_F_MEASURED_TOLERANCE,  base->measured_tolerance_um = patch->measured_tolerance_um);

    APPLY(OT3D_F_EMPTY_SPOOL_WEIGHT_G, base->empty_spool_weight_g = patch->empty_spool_weight_g);
    APPLY(OT3D_F_MEASURED_WEIGHT_G,    base->measured_filament_weight_g = patch->measured_filament_weight_g);
    APPLY(OT3D_F_MEASURED_LENGTH_M,    base->measured_filament_length_m = patch->measured_filament_length_m);

    APPLY(OT3D_F_MAX_DRY_TEMP,   base->max_dry_temp_encoded = patch->max_dry_temp_encoded);
    APPLY(OT3D_F_DRY_TIME_HOURS, base->dry_time_hours = patch->dry_time_hours);
    APPLY(OT3D_F_MIN_PRINT_TEMP, base->min_print_temp_encoded = patch->min_print_temp_encoded);
    APPLY(OT3D_F_MAX_PRINT_TEMP, base->max_print_temp_encoded = patch->max_print_temp_encoded);
    APPLY(OT3D_F_MIN_BED_TEMP,   base->min_bed_temp_encoded = patch->min_bed_temp_encoded);
    APPLY(OT3D_F_MAX_BED_TEMP,   base->max_bed_temp_encoded = patch->max_bed_temp_encoded);

    APPLY(OT3D_F_MIN_VOLUMETRIC_SPEED,    base->min_volumetric_speed = patch->min_volumetric_speed);
    APPLY(OT3D_F_MAX_VOLUMETRIC_SPEED,    base->max_volumetric_speed = patch->max_volumetric_speed);
    APPLY(OT3D_F_TARGET_VOLUMETRIC_SPEED, base->target_volumetric_speed = patch->target_volumetric_speed);

    APPLY(OT3D_F_BARCODE, base->barcode = patch->barcode);
    APPLY_STR(OT3D_F_SKU, sku);
    APPLY(OT3D_F_CHAMBER_TEMP,         base->chamber_temp_encoded = patch->chamber_temp_encoded);
    APPLY(OT3D_F_MIN_NOZZLE_DIAMETER,  base->min_nozzle_diameter = patch->min_nozzle_diameter);
}

int opentag3d_patch_touches_extended(uint64_t mask) { return (mask & OT3D_EXT_FIELD_MASK) ? 1 : 0; }
int opentag3d_patch_touches_v2_only(uint64_t mask)  { return (mask & OT3D_V2_ONLY_FIELD_MASK) ? 1 : 0; }
