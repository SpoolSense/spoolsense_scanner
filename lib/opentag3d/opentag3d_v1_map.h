#ifndef OPENTAG3D_V1_MAP_H
#define OPENTAG3D_V1_MAP_H

/* OpenTag3D spec v1.000 memory map, derived from the field offsets the v1
 * decoder/encoder used as magic numbers before issue #328. v2 records use
 * opentag3d_v2_map.h; this header is the single source of truth for v1.
 *
 * Layout: core fields end at OT3D_V1_OFF_TRANSMISSION + 2 (0x66). The gap
 * 0x66..0x6F is reserved on-tag. Extended fields start at 0x70 and the last
 * one (target volumetric speed) ends at 0xBB, the canonical extended length.
 */

#define OT3D_V1_OFF_TAG_VERSION         0x00  /* int, unit=version, scaling=0.001, required */
#define OT3D_V1_LEN_TAG_VERSION         2
#define OT3D_V1_OFF_MATERIAL            0x02  /* utf8, required */
#define OT3D_V1_LEN_MATERIAL            5
#define OT3D_V1_OFF_MATERIAL_MOD        0x07  /* utf8 */
#define OT3D_V1_LEN_MATERIAL_MOD        5
/* 0x0C..0x1A: reserved/padding in the v1 spec — never written by the encoder,
 * never patched; a raw patch must preserve these bytes verbatim. */
#define OT3D_V1_OFF_MANUFACTURER        0x1B  /* utf8, required */
#define OT3D_V1_LEN_MANUFACTURER        16
#define OT3D_V1_OFF_COLOR_NAME          0x2B  /* utf8 */
#define OT3D_V1_LEN_COLOR_NAME          32
#define OT3D_V1_OFF_COLOR_1             0x4B  /* rgba, required */
#define OT3D_V1_LEN_COLOR_1             4
#define OT3D_V1_OFF_COLOR_2             0x4F  /* rgba */
#define OT3D_V1_LEN_COLOR_2             4
#define OT3D_V1_OFF_COLOR_3             0x53  /* rgba */
#define OT3D_V1_LEN_COLOR_3             4
#define OT3D_V1_OFF_COLOR_4             0x57  /* rgba */
#define OT3D_V1_LEN_COLOR_4             4
#define OT3D_V1_OFF_DIAMETER            0x5C  /* int, unit=um, scaling=0.001, required */
#define OT3D_V1_LEN_DIAMETER            2
#define OT3D_V1_OFF_WEIGHT              0x5E  /* int, unit=g, required */
#define OT3D_V1_LEN_WEIGHT              2
#define OT3D_V1_OFF_PRINT_TEMP          0x60  /* int, unit=C, scaling=5, required */
#define OT3D_V1_LEN_PRINT_TEMP          1
#define OT3D_V1_OFF_BED_TEMP            0x61  /* int, unit=C, scaling=5, required */
#define OT3D_V1_LEN_BED_TEMP            1
#define OT3D_V1_OFF_DENSITY             0x62  /* int, unit=g/cm3, scaling=0.001, required */
#define OT3D_V1_LEN_DENSITY             2
#define OT3D_V1_OFF_TRANSMISSION        0x64  /* int, unit=mm, scaling=0.1 (2 bytes on v1) */
#define OT3D_V1_LEN_TRANSMISSION        2

/* Extended block (present from OT3D_EXTENDED_START = 0x70 on). */
#define OT3D_V1_OFF_DATA_URL            0x70  /* ascii */
#define OT3D_V1_LEN_DATA_URL            32
#define OT3D_V1_OFF_SERIAL              0x90  /* utf8 (16 bytes on v1; v2 stores 32) */
#define OT3D_V1_LEN_SERIAL              16
#define OT3D_V1_OFF_MFG_DATE            0xA0  /* date: u16 year, u8 month, u8 day */
#define OT3D_V1_LEN_MFG_DATE            4
#define OT3D_V1_OFF_MFG_TIME            0xA4  /* time: u8 hour, minute, second (UTC) */
#define OT3D_V1_LEN_MFG_TIME            3
#define OT3D_V1_OFF_SPOOL_CORE_DIAMETER 0xA7  /* int, unit=mm */
#define OT3D_V1_LEN_SPOOL_CORE_DIAMETER 1
#define OT3D_V1_OFF_MFI_TEMP            0xA8  /* int, unit=C, scaling=5 */
#define OT3D_V1_LEN_MFI_TEMP            1
#define OT3D_V1_OFF_MFI_LOAD            0xA9  /* int, unit=g, scaling=10 */
#define OT3D_V1_LEN_MFI_LOAD            1
#define OT3D_V1_OFF_MFI_VALUE           0xAA  /* int, unit=g/min */
#define OT3D_V1_LEN_MFI_VALUE           1
#define OT3D_V1_OFF_TOLERANCE           0xAB  /* int, unit=mm, scaling=0.01 */
#define OT3D_V1_LEN_TOLERANCE           1
#define OT3D_V1_OFF_EMPTY_SPOOL_WEIGHT  0xAC  /* int, unit=g */
#define OT3D_V1_LEN_EMPTY_SPOOL_WEIGHT  2
#define OT3D_V1_OFF_MEASURED_WEIGHT     0xAE  /* int, unit=g */
#define OT3D_V1_LEN_MEASURED_WEIGHT     2
#define OT3D_V1_OFF_MEASURED_LENGTH     0xB0  /* int, unit=m */
#define OT3D_V1_LEN_MEASURED_LENGTH     2
#define OT3D_V1_OFF_MAX_DRY_TEMP        0xB2  /* int, unit=C, scaling=5 */
#define OT3D_V1_LEN_MAX_DRY_TEMP        1
#define OT3D_V1_OFF_DRY_TIME            0xB3  /* int, unit=hr */
#define OT3D_V1_LEN_DRY_TIME            1
#define OT3D_V1_OFF_MIN_PRINT_TEMP      0xB4  /* int, unit=C, scaling=5 */
#define OT3D_V1_LEN_MIN_PRINT_TEMP      1
#define OT3D_V1_OFF_MAX_PRINT_TEMP      0xB5  /* int, unit=C, scaling=5 */
#define OT3D_V1_LEN_MAX_PRINT_TEMP      1
#define OT3D_V1_OFF_MIN_BED_TEMP        0xB6  /* int, unit=C, scaling=5 */
#define OT3D_V1_LEN_MIN_BED_TEMP        1
#define OT3D_V1_OFF_MAX_BED_TEMP        0xB7  /* int, unit=C, scaling=5 */
#define OT3D_V1_LEN_MAX_BED_TEMP        1
#define OT3D_V1_OFF_MIN_VSO             0xB8  /* int, unit=mm3/s */
#define OT3D_V1_LEN_MIN_VSO             1
#define OT3D_V1_OFF_MAX_VSO             0xB9  /* int, unit=mm3/s */
#define OT3D_V1_LEN_MAX_VSO             1
#define OT3D_V1_OFF_TARGET_VSO          0xBA  /* int, unit=mm3/s */
#define OT3D_V1_LEN_TARGET_VSO          1

#endif /* OPENTAG3D_V1_MAP_H */
