/* Native tests for the OpenTag3D patch envelope and merge.
 * Plain C, PASS/FAIL per check, non-zero exit on any failure. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
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

/* A baseline with every interesting field set to a distinctive value. */
static void build_rich_baseline(opentag3d_t *t) {
    memset(t, 0, sizeof(*t));
    t->tag_version = 2003;
    strcpy(t->base_material, "PLA");
    strcpy(t->manufacturer, "Acme");
    strcpy(t->color_name, "Teal");
    t->color_rgba[0][0] = 0x11; t->color_rgba[0][1] = 0x22;
    t->color_rgba[0][2] = 0x33; t->color_rgba[0][3] = 0x80;
    t->color_rgba[1][0] = 0x44;
    t->color_rgba[2][0] = 0x55;
    t->color_rgba[3][0] = 0x66;
    t->diameter_um = 1750;
    t->target_weight_g = 1000;
    t->print_temp_encoded = 42;   /* 210C */
    t->bed_temp_encoded = 12;     /* 60C  */
    t->density_ugcm3 = 1300;
    t->transmission_distance = 77;
    t->has_extended = 1;
    strcpy(t->online_url, "acme.example/f1");
    strcpy(t->serial_number, "SN-0001");
    t->manufacture_year = 2025;
    t->manufacture_month = 6;
    t->manufacture_day = 14;
    t->manufacture_hour = 9;
    t->manufacture_minute = 30;
    t->manufacture_second = 15;
    t->spool_core_diameter_mm = 52;
    t->mfi_temp_encoded = 44;
    t->mfi_load = 216;
    t->mfi_value = 3;
    t->measured_tolerance_um = 20;
    t->empty_spool_weight_g = 180;
    t->measured_filament_weight_g = 998;
    t->measured_filament_length_m = 330;
    t->max_dry_temp_encoded = 10;
    t->dry_time_hours = 8;
    t->min_print_temp_encoded = 38;
    t->max_print_temp_encoded = 46;
    t->min_bed_temp_encoded = 10;
    t->max_bed_temp_encoded = 14;
    t->min_volumetric_speed = 2;
    t->max_volumetric_speed = 18;
    t->target_volumetric_speed = 12;
    t->barcode = 123456789012ULL;
    strcpy(t->sku, "ACME-PLA-TEAL");
    t->chamber_temp_encoded = 8;
    t->min_nozzle_diameter = 4;
}

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
        CHECK(base.mfi_load == 216, "B: mfi_load survived");
        CHECK(base.color_rgba[0][3] == 0x80, "B: alpha survived");
        CHECK(base.color_rgba[2][0] == 0x55, "B: colour 3 survived");
        CHECK(base.measured_filament_length_m == 330, "B: length survived");
        CHECK(base.spool_core_diameter_mm == 52, "B: spool core survived");
        CHECK(base.min_volumetric_speed == 2, "B: min vso survived");
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
        CHECK(base.max_dry_temp_encoded == 10, "C: neighbouring dry temp survived");
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
        CHECK(base.color_rgba[0][3] == 0x80, "E: alpha not implied by rgb edit");
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

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
