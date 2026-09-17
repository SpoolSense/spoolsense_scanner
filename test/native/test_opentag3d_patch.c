/* Native tests for the OpenTag3D patch envelope and merge.
 * Plain C, PASS/FAIL per check, non-zero exit on any failure. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "opentag3d_lib.h"
#include "opentag3d_patch.h"

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

/* A baseline with every interesting field set to a DISTINCT value.
 * Use a mechanical scheme: assign constrained fields values in their ranges,
 * then assign unconstrained fields distinct values outside those ranges.
 * This catches struct member swaps that a value comparison would hide. */
static void build_rich_baseline(opentag3d_t *t) {
    memset(t, 0, sizeof(*t));
    t->tag_version = 2003;
    strcpy(t->base_material, "PLA");
    strcpy(t->manufacturer, "Acme");
    strcpy(t->color_name, "Teal");
    /* Constrained fields first: manufacture_* have specific ranges. */
    t->manufacture_month = 1;    /* 1-12 */
    t->manufacture_day = 6;      /* 1-31 */
    t->manufacture_hour = 9;     /* 0-23 */
    t->manufacture_minute = 11;  /* 0-59 */
    t->manufacture_second = 12;  /* 0-59 */
    /* Unconstrained uint8_t fields: use values 13..46 (no collision with constrained). */
    t->color_rgba[0][0] = 13; t->color_rgba[0][1] = 14;
    t->color_rgba[0][2] = 15; t->color_rgba[0][3] = 16;
    t->color_rgba[1][0] = 17; t->color_rgba[1][1] = 18;
    t->color_rgba[1][2] = 19; t->color_rgba[1][3] = 20;
    t->color_rgba[2][0] = 21; t->color_rgba[2][1] = 22;
    t->color_rgba[2][2] = 23; t->color_rgba[2][3] = 24;
    t->color_rgba[3][0] = 25; t->color_rgba[3][1] = 26;
    t->color_rgba[3][2] = 27; t->color_rgba[3][3] = 28;
    t->print_temp_encoded = 29;
    t->bed_temp_encoded = 30;
    t->diameter_um = 1750;
    t->target_weight_g = 1000;
    t->density_ugcm3 = 1300;
    t->transmission_distance = 77;
    t->has_extended = 1;
    strcpy(t->online_url, "acme.example/f1");
    strcpy(t->serial_number, "SN-0001");
    t->manufacture_year = 2025;
    t->spool_core_diameter_mm = 31;
    t->mfi_temp_encoded = 32;
    t->mfi_load = 33;
    t->mfi_value = 34;
    t->measured_tolerance_um = 35;
    t->empty_spool_weight_g = 180;
    t->measured_filament_weight_g = 998;
    t->measured_filament_length_m = 330;
    t->max_dry_temp_encoded = 36;
    t->dry_time_hours = 37;
    t->min_print_temp_encoded = 38;
    t->max_print_temp_encoded = 39;
    t->min_bed_temp_encoded = 40;
    t->max_bed_temp_encoded = 41;
    t->min_volumetric_speed = 42;
    t->max_volumetric_speed = 43;
    t->target_volumetric_speed = 44;
    t->barcode = 123456789012ULL;
    strcpy(t->sku, "ACME-PLA-TEAL");
    t->chamber_temp_encoded = 45;
    t->min_nozzle_diameter = 46;
}

/* Field table: all 44 patchable fields with offset, size, and test value.
 * Also used for baseline uniqueness checking. */
static const struct {
    ot3d_field_t id;
    size_t offset;
    size_t size;
    uint8_t patch_value;
} field_specs[] = {
    { OT3D_F_BASE_MATERIAL, offsetof(opentag3d_t, base_material), sizeof(((opentag3d_t*)0)->base_material), 42 },
    { OT3D_F_MATERIAL_MODIFIERS, offsetof(opentag3d_t, material_modifiers), sizeof(((opentag3d_t*)0)->material_modifiers), 43 },
    { OT3D_F_MANUFACTURER, offsetof(opentag3d_t, manufacturer), sizeof(((opentag3d_t*)0)->manufacturer), 44 },
    { OT3D_F_COLOR_NAME, offsetof(opentag3d_t, color_name), sizeof(((opentag3d_t*)0)->color_name), 45 },
    { OT3D_F_COLOR0_RGB, offsetof(opentag3d_t, color_rgba[0][0]), 3, 100 },
    { OT3D_F_COLOR0_ALPHA, offsetof(opentag3d_t, color_rgba[0][3]), 1, 101 },
    { OT3D_F_COLOR1, offsetof(opentag3d_t, color_rgba[1][0]), 4, 102 },
    { OT3D_F_COLOR2, offsetof(opentag3d_t, color_rgba[2][0]), 4, 103 },
    { OT3D_F_COLOR3, offsetof(opentag3d_t, color_rgba[3][0]), 4, 104 },
    { OT3D_F_DIAMETER_UM, offsetof(opentag3d_t, diameter_um), sizeof(((opentag3d_t*)0)->diameter_um), 46 },
    { OT3D_F_TARGET_WEIGHT_G, offsetof(opentag3d_t, target_weight_g), sizeof(((opentag3d_t*)0)->target_weight_g), 47 },
    { OT3D_F_PRINT_TEMP, offsetof(opentag3d_t, print_temp_encoded), 1, 105 },
    { OT3D_F_BED_TEMP, offsetof(opentag3d_t, bed_temp_encoded), 1, 106 },
    { OT3D_F_DENSITY, offsetof(opentag3d_t, density_ugcm3), sizeof(((opentag3d_t*)0)->density_ugcm3), 48 },
    { OT3D_F_TRANSMISSION_DISTANCE, offsetof(opentag3d_t, transmission_distance), sizeof(((opentag3d_t*)0)->transmission_distance), 49 },
    { OT3D_F_ONLINE_URL, offsetof(opentag3d_t, online_url), sizeof(((opentag3d_t*)0)->online_url), 50 },
    { OT3D_F_SERIAL_NUMBER, offsetof(opentag3d_t, serial_number), sizeof(((opentag3d_t*)0)->serial_number), 51 },
    { OT3D_F_MANUFACTURE_YEAR, offsetof(opentag3d_t, manufacture_year), sizeof(((opentag3d_t*)0)->manufacture_year), 52 },
    { OT3D_F_MANUFACTURE_MONTH, offsetof(opentag3d_t, manufacture_month), 1, 107 },
    { OT3D_F_MANUFACTURE_DAY, offsetof(opentag3d_t, manufacture_day), 1, 108 },
    { OT3D_F_MANUFACTURE_HOUR, offsetof(opentag3d_t, manufacture_hour), 1, 109 },
    { OT3D_F_MANUFACTURE_MINUTE, offsetof(opentag3d_t, manufacture_minute), 1, 110 },
    { OT3D_F_MANUFACTURE_SECOND, offsetof(opentag3d_t, manufacture_second), 1, 111 },
    { OT3D_F_SPOOL_CORE_DIAMETER, offsetof(opentag3d_t, spool_core_diameter_mm), 1, 112 },
    { OT3D_F_MFI_TEMP, offsetof(opentag3d_t, mfi_temp_encoded), 1, 113 },
    { OT3D_F_MFI_LOAD, offsetof(opentag3d_t, mfi_load), 1, 114 },
    { OT3D_F_MFI_VALUE, offsetof(opentag3d_t, mfi_value), 1, 115 },
    { OT3D_F_MEASURED_TOLERANCE, offsetof(opentag3d_t, measured_tolerance_um), 1, 116 },
    { OT3D_F_EMPTY_SPOOL_WEIGHT_G, offsetof(opentag3d_t, empty_spool_weight_g), sizeof(((opentag3d_t*)0)->empty_spool_weight_g), 53 },
    { OT3D_F_MEASURED_WEIGHT_G, offsetof(opentag3d_t, measured_filament_weight_g), sizeof(((opentag3d_t*)0)->measured_filament_weight_g), 54 },
    { OT3D_F_MEASURED_LENGTH_M, offsetof(opentag3d_t, measured_filament_length_m), sizeof(((opentag3d_t*)0)->measured_filament_length_m), 55 },
    { OT3D_F_MAX_DRY_TEMP, offsetof(opentag3d_t, max_dry_temp_encoded), 1, 117 },
    { OT3D_F_DRY_TIME_HOURS, offsetof(opentag3d_t, dry_time_hours), 1, 118 },
    { OT3D_F_MIN_PRINT_TEMP, offsetof(opentag3d_t, min_print_temp_encoded), 1, 119 },
    { OT3D_F_MAX_PRINT_TEMP, offsetof(opentag3d_t, max_print_temp_encoded), 1, 120 },
    { OT3D_F_MIN_BED_TEMP, offsetof(opentag3d_t, min_bed_temp_encoded), 1, 121 },
    { OT3D_F_MAX_BED_TEMP, offsetof(opentag3d_t, max_bed_temp_encoded), 1, 122 },
    { OT3D_F_MIN_VOLUMETRIC_SPEED, offsetof(opentag3d_t, min_volumetric_speed), 1, 123 },
    { OT3D_F_MAX_VOLUMETRIC_SPEED, offsetof(opentag3d_t, max_volumetric_speed), 1, 124 },
    { OT3D_F_TARGET_VOLUMETRIC_SPEED, offsetof(opentag3d_t, target_volumetric_speed), 1, 125 },
    { OT3D_F_BARCODE, offsetof(opentag3d_t, barcode), sizeof(((opentag3d_t*)0)->barcode), 56 },
    { OT3D_F_SKU, offsetof(opentag3d_t, sku), sizeof(((opentag3d_t*)0)->sku), 57 },
    { OT3D_F_CHAMBER_TEMP, offsetof(opentag3d_t, chamber_temp_encoded), 1, 126 },
    { OT3D_F_MIN_NOZZLE_DIAMETER, offsetof(opentag3d_t, min_nozzle_diameter), 1, 127 },
};
static const int num_field_specs = sizeof(field_specs) / sizeof(field_specs[0]);

int main(void) {
    printf("OpenTag3D patch tests\n");

    /* A: empty mask changes nothing. */
    {
        opentag3d_t base, before, patch;
        build_rich_baseline(&base);
        before = base;
        memset(&patch, 0, sizeof(patch));
        opentag3d_apply_patch(&base, &patch, 0);
        CHECK(memcmp(&base, &before, sizeof(base)) == 0, "A: empty mask is a no-op");
    }

    /* B: one present field applies; every neighbour survives. */
    {
        opentag3d_t base, patch;
        uint64_t mask = 0;
        build_rich_baseline(&base);
        memset(&patch, 0, sizeof(patch));
        patch.target_weight_g = 750;
        ot3d_mask_set(&mask, OT3D_F_TARGET_WEIGHT_G);
        opentag3d_apply_patch(&base, &patch, mask);
        CHECK(base.target_weight_g == 750, "B: present field applied");
        CHECK(base.manufacture_year == 2025, "B: manufacture_year survived");
        CHECK(base.mfi_load == 33, "B: mfi_load survived");
        CHECK(base.color_rgba[0][3] == 16, "B: alpha survived");
        CHECK(base.color_rgba[2][0] == 21, "B: colour 2 survived");
        CHECK(base.measured_filament_length_m == 330, "B: length survived");
        CHECK(base.spool_core_diameter_mm == 31, "B: spool core survived");
        CHECK(base.min_volumetric_speed == 42, "B: min vso survived");
        CHECK(strcmp(base.online_url, "acme.example/f1") == 0, "B: url survived");
        CHECK(base.transmission_distance == 77, "B: transmission distance survived");
        CHECK(base.tag_version == 2003, "B: version untouched by a field patch");
    }

    /* C: an explicit zero is honoured, not treated as absent. */
    {
        opentag3d_t base, patch;
        uint64_t mask = 0;
        build_rich_baseline(&base);
        memset(&patch, 0, sizeof(patch));
        patch.dry_time_hours = 0;
        ot3d_mask_set(&mask, OT3D_F_DRY_TIME_HOURS);
        opentag3d_apply_patch(&base, &patch, mask);
        CHECK(base.dry_time_hours == 0, "C: explicit zero clears the field");
        CHECK(base.max_dry_temp_encoded == 36, "C: neighbouring dry temp survived");
    }

    /* D: an explicit empty string clears a string field. */
    {
        opentag3d_t base, patch;
        uint64_t mask = 0;
        build_rich_baseline(&base);
        memset(&patch, 0, sizeof(patch));
        patch.color_name[0] = '\0';
        ot3d_mask_set(&mask, OT3D_F_COLOR_NAME);
        opentag3d_apply_patch(&base, &patch, mask);
        CHECK(base.color_name[0] == '\0', "D: explicit empty string clears");
        CHECK(strcmp(base.manufacturer, "Acme") == 0, "D: manufacturer survived");
    }

    /* E: RGB and alpha are separate fields. */
    {
        opentag3d_t base, patch;
        uint64_t mask = 0;
        build_rich_baseline(&base);
        memset(&patch, 0, sizeof(patch));
        patch.color_rgba[0][0] = 0xAA;
        patch.color_rgba[0][1] = 0xBB;
        patch.color_rgba[0][2] = 0xCC;
        ot3d_mask_set(&mask, OT3D_F_COLOR0_RGB);
        opentag3d_apply_patch(&base, &patch, mask);
        CHECK(base.color_rgba[0][0] == 0xAA, "E: rgb applied");
        CHECK(base.color_rgba[0][3] == 16, "E: alpha not implied by rgb edit");
    }

    /* F: field-group masks classify correctly. */
    {
        uint64_t m = 0;
        ot3d_mask_set(&m, OT3D_F_MFI_LOAD);
        CHECK(opentag3d_patch_touches_extended(m) == 1, "F: mfi_load is extended");
        CHECK(opentag3d_patch_touches_v2_only(m) == 0, "F: mfi_load is not v2-only");

        m = 0;
        ot3d_mask_set(&m, OT3D_F_SKU);
        CHECK(opentag3d_patch_touches_v2_only(m) == 1, "F: sku is v2-only");

        m = 0;
        ot3d_mask_set(&m, OT3D_F_TARGET_WEIGHT_G);
        CHECK(opentag3d_patch_touches_extended(m) == 0, "F: target weight is core");
    }

    /* G: every declared field id round-trips through the mask helpers. */
    {
        int ok = 1;
        for (int f = 0; f < OT3D_F_COUNT; f++) {
            uint64_t m = 0;
            ot3d_mask_set(&m, (ot3d_field_t)f);
            if (!ot3d_mask_get(m, (ot3d_field_t)f)) ok = 0;
        }
        CHECK(ok, "G: all field ids set and read back");
        CHECK(OT3D_F_COUNT <= 64, "G: field count fits a 64-bit mask");
    }

    /* H: the envelope fits the NFC staging buffer. */
    {
        CHECK(sizeof(ot3d_patch_t) <= 320, "H: envelope fits RAW_WRITE_BUFFER_SIZE");
        CHECK(sizeof(ot3d_patch_t) != sizeof(opentag3d_t),
              "H: envelope is distinguishable from a bare struct by size");
    }

    /* Z: baseline uniqueness — all single-byte patchable fields have distinct values.
     * Derived from the field_specs table to guarantee no field is omitted. */
    {
        uint8_t values[64];
        int count = 0;

        opentag3d_t base;
        build_rich_baseline(&base);

        for (int i = 0; i < num_field_specs; i++) {
            if (field_specs[i].size == 1) {
                values[count++] = *((uint8_t*)(&base) + field_specs[i].offset);
            }
        }

        for (int i = 0; i < count; i++) {
            for (int j = i + 1; j < count; j++) {
                if (values[i] == values[j]) {
                    printf("  FAIL  Z: baseline collision at indices %d and %d (both %d)\n", i, j, values[i]);
                    failures++;
                    goto z_done;
                }
            }
        }
        printf("  PASS  Z: all %d single-byte baseline values are distinct\n", count);
        z_done:;
    }

    /* I: table-driven comprehensive field coverage — all 44 ids.
     * Each field tested in two directions: APPLIED and SURVIVED.
     * Multi-byte fields tested with all bytes filled explicitly. */
    {
        CHECK(num_field_specs == OT3D_F_COUNT, "I: field table has exactly OT3D_F_COUNT entries");

        for (int f = 0; f < num_field_specs; f++) {
            opentag3d_t base, patch, before;
            uint64_t mask = 0;
            uint8_t expected[64];

            build_rich_baseline(&base);
            before = base;
            memset(&patch, 0, sizeof(patch));

            ot3d_mask_set(&mask, field_specs[f].id);

            CHECK(field_specs[f].size <= sizeof(expected), "I: field size fits the expected buffer");
            memset(expected, field_specs[f].patch_value, field_specs[f].size);
            memcpy((uint8_t*)&patch + field_specs[f].offset, expected, field_specs[f].size);

            opentag3d_apply_patch(&base, &patch, mask);

            int applied_ok = memcmp((uint8_t*)&base + field_specs[f].offset, expected, field_specs[f].size) == 0;
            char buf[128];
            snprintf(buf, sizeof(buf), "I: field %d applied", f);
            CHECK(applied_ok, buf);

            for (int i = 0; i < (int)sizeof(opentag3d_t); i++) {
                uint8_t before_byte = ((uint8_t*)&before)[i];
                uint8_t base_byte = ((uint8_t*)&base)[i];
                int in_modified = (i >= (int)field_specs[f].offset && i < (int)(field_specs[f].offset + field_specs[f].size));
                if (!in_modified && before_byte != base_byte) {
                    snprintf(buf, sizeof(buf), "I: field %d survived (byte %d changed)", f, i);
                    CHECK(0, buf);
                    goto next_field;
                }
            }
            snprintf(buf, sizeof(buf), "I: field %d survived", f);
            CHECK(1, buf);
            next_field:;
        }
    }

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
