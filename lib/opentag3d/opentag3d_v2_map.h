#ifndef OPENTAG3D_V2_MAP_H
#define OPENTAG3D_V2_MAP_H

/* OpenTag3D spec v2.000 core memory map, addresses 0x00..0xDF.
 * Offsets and lengths derived from opentag3d-spec-v2.json.
 * Trailing comments: type, unit, scaling, required (as per spec).
 */

#define OT3D_V2_VERSION  2000   /* v2.000, 3 implied decimals */
#define OT3D_V2_MAP_SIZE 0xE0   /* 224 bytes */

#define OT3D_V2_OFF_TAG_VERSION         0x00  /* int, unit=version, scaling=0.001, required */
#define OT3D_V2_LEN_TAG_VERSION         2
#define OT3D_V2_OFF_MATERIAL            0x02  /* utf8, required */
#define OT3D_V2_LEN_MATERIAL            5
#define OT3D_V2_OFF_MATERIAL_MOD        0x07  /* utf8 */
#define OT3D_V2_LEN_MATERIAL_MOD        5
#define OT3D_V2_OFF_MANUFACTURER        0x0C  /* utf8, required */
#define OT3D_V2_LEN_MANUFACTURER        16
#define OT3D_V2_OFF_COLOR_NAME          0x1C  /* utf8 */
#define OT3D_V2_LEN_COLOR_NAME          32
#define OT3D_V2_OFF_COLOR_1             0x3C  /* rgba, unit=RGBA, required */
#define OT3D_V2_LEN_COLOR_1             4
#define OT3D_V2_OFF_COLOR_2             0x40  /* rgba, unit=RGBA */
#define OT3D_V2_LEN_COLOR_2             4
#define OT3D_V2_OFF_COLOR_3             0x44  /* rgba, unit=RGBA */
#define OT3D_V2_LEN_COLOR_3             4
#define OT3D_V2_OFF_COLOR_4             0x48  /* rgba, unit=RGBA */
#define OT3D_V2_LEN_COLOR_4             4
#define OT3D_V2_OFF_SERIAL              0x4C  /* utf8 */
#define OT3D_V2_LEN_SERIAL              32
#define OT3D_V2_OFF_SKU                 0x6C  /* utf8 */
#define OT3D_V2_LEN_SKU                 16
#define OT3D_V2_OFF_BARCODE             0x7C  /* int */
#define OT3D_V2_LEN_BARCODE             6
#define OT3D_V2_OFF_MFG_DATE            0x84  /* date, unit=YYYY,MM,DD */
#define OT3D_V2_LEN_MFG_DATE            4
#define OT3D_V2_OFF_MFG_TIME            0x88  /* time, unit=UTC hh:mm:ss */
#define OT3D_V2_LEN_MFG_TIME            3
#define OT3D_V2_OFF_DIAMETER            0x8C  /* int, unit=mm, scaling=0.001, required */
#define OT3D_V2_LEN_DIAMETER            2
#define OT3D_V2_OFF_TOLERANCE           0x8E  /* int, unit=mm, scaling=0.01 */
#define OT3D_V2_LEN_TOLERANCE           1
#define OT3D_V2_OFF_NOZZLE_DIAMETER     0x8F  /* int, unit=mm, scaling=0.1 */
#define OT3D_V2_LEN_NOZZLE_DIAMETER     1
#define OT3D_V2_OFF_PRINT_TEMP          0x90  /* int, unit=C, scaling=5, required */
#define OT3D_V2_LEN_PRINT_TEMP          1
#define OT3D_V2_OFF_MIN_PRINT_TEMP      0x91  /* int, unit=C, scaling=5 */
#define OT3D_V2_LEN_MIN_PRINT_TEMP      1
#define OT3D_V2_OFF_MAX_PRINT_TEMP      0x92  /* int, unit=C, scaling=5 */
#define OT3D_V2_LEN_MAX_PRINT_TEMP      1
#define OT3D_V2_OFF_CHAMBER_TEMP        0x93  /* int, unit=C, scaling=5, required */
#define OT3D_V2_LEN_CHAMBER_TEMP        1
#define OT3D_V2_OFF_BED_TEMP            0x94  /* int, unit=C, scaling=5, required */
#define OT3D_V2_LEN_BED_TEMP            1
#define OT3D_V2_OFF_MIN_BED_TEMP        0x95  /* int, unit=C, scaling=5 */
#define OT3D_V2_LEN_MIN_BED_TEMP        1
#define OT3D_V2_OFF_MAX_BED_TEMP        0x96  /* int, unit=C, scaling=5 */
#define OT3D_V2_LEN_MAX_BED_TEMP        1
#define OT3D_V2_OFF_TARGET_VSO          0x97  /* int, unit=mm3/s */
#define OT3D_V2_LEN_TARGET_VSO          1
#define OT3D_V2_OFF_MIN_VSO             0x98  /* int, unit=mm3/s */
#define OT3D_V2_LEN_MIN_VSO             1
#define OT3D_V2_OFF_MAX_VSO             0x99  /* int, unit=mm3/s */
#define OT3D_V2_LEN_MAX_VSO             1
#define OT3D_V2_OFF_MAX_DRY_TEMP        0x9A  /* int, unit=C, scaling=5 */
#define OT3D_V2_LEN_MAX_DRY_TEMP        1
#define OT3D_V2_OFF_DRY_TIME            0x9B  /* int, unit=hr */
#define OT3D_V2_LEN_DRY_TIME            1
#define OT3D_V2_OFF_DENSITY             0x9C  /* int, unit=g/cm3, scaling=0.001, required */
#define OT3D_V2_LEN_DENSITY             2
#define OT3D_V2_OFF_WEIGHT              0x9E  /* int, unit=g, required */
#define OT3D_V2_LEN_WEIGHT              2
#define OT3D_V2_OFF_EMPTY_SPOOL_WEIGHT  0xA0  /* int, unit=g */
#define OT3D_V2_LEN_EMPTY_SPOOL_WEIGHT  2
#define OT3D_V2_OFF_MEASURED_LENGTH     0xA2  /* int, unit=m */
#define OT3D_V2_LEN_MEASURED_LENGTH     2
#define OT3D_V2_OFF_MEASURED_WEIGHT     0xA4  /* int, unit=g */
#define OT3D_V2_LEN_MEASURED_WEIGHT     2
#define OT3D_V2_OFF_SPOOL_CORE_DIAMETER 0xA6  /* int, unit=mm */
#define OT3D_V2_LEN_SPOOL_CORE_DIAMETER 1
#define OT3D_V2_OFF_TD                  0xA7  /* int, unit=mm, scaling=0.1 */
#define OT3D_V2_LEN_TD                  1
#define OT3D_V2_OFF_MFI_TEMP            0xA8  /* int, unit=C, scaling=5 */
#define OT3D_V2_LEN_MFI_TEMP            1
#define OT3D_V2_OFF_MFI_LOAD            0xA9  /* int, unit=g, scaling=10 */
#define OT3D_V2_LEN_MFI_LOAD            1
#define OT3D_V2_OFF_MFI_VALUE           0xAA  /* int, unit=g/min */
#define OT3D_V2_LEN_MFI_VALUE           1
#define OT3D_V2_OFF_DATA_URL            0xB8  /* ascii */
#define OT3D_V2_LEN_DATA_URL            32

#endif /* OPENTAG3D_V2_MAP_H */
