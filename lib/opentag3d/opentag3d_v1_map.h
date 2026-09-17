#ifndef OPENTAG3D_V1_MAP_H
#define OPENTAG3D_V1_MAP_H

/* v1.00x on-tag byte map. Offsets are from the start of the OpenTag3D
 * payload (after the NDEF MIME record header). No spec JSON exists for
 * v1 (unlike v2's opentag3d-spec-v2.json); these constants are read
 * directly out of opentag3d_lib.c's original hand-written v1 decode and
 * encode paths, which agreed byte-for-byte.
 */

#define OT3D_V1_OFF_TAG_VERSION      0x00
#define OT3D_V1_LEN_TAG_VERSION      2
#define OT3D_V1_OFF_MATERIAL         0x02
#define OT3D_V1_LEN_MATERIAL         5
#define OT3D_V1_OFF_MATERIAL_MOD     0x07
#define OT3D_V1_LEN_MATERIAL_MOD     5
/* 0x0C-0x1A: reserved/padding */
#define OT3D_V1_OFF_MANUFACTURER     0x1B
#define OT3D_V1_LEN_MANUFACTURER     16
#define OT3D_V1_OFF_COLOR_NAME       0x2B
#define OT3D_V1_LEN_COLOR_NAME       32
#define OT3D_V1_OFF_COLOR_1          0x4B  /* rgba */
#define OT3D_V1_LEN_COLOR_1          4
#define OT3D_V1_OFF_COLOR_2          0x4F  /* rgba */
#define OT3D_V1_LEN_COLOR_2          4
#define OT3D_V1_OFF_COLOR_3          0x53  /* rgba */
#define OT3D_V1_LEN_COLOR_3          4
#define OT3D_V1_OFF_COLOR_4          0x57  /* rgba */
#define OT3D_V1_LEN_COLOR_4          4
#define OT3D_V1_OFF_DIAMETER         0x5C  /* unit=mm, scaling=0.001 */
#define OT3D_V1_LEN_DIAMETER         2
#define OT3D_V1_OFF_WEIGHT           0x5E  /* unit=g */
#define OT3D_V1_LEN_WEIGHT           2
#define OT3D_V1_OFF_PRINT_TEMP       0x60  /* unit=C, scaling=5 */
#define OT3D_V1_LEN_PRINT_TEMP       1
#define OT3D_V1_OFF_BED_TEMP         0x61  /* unit=C, scaling=5 */
#define OT3D_V1_LEN_BED_TEMP         1
#define OT3D_V1_OFF_DENSITY          0x62  /* unit=g/cm3, scaling=0.001 */
#define OT3D_V1_LEN_DENSITY          2
#define OT3D_V1_OFF_TD               0x64  /* unit=mm, scaling=0.1; u16, unlike v2's 1-byte TD */
#define OT3D_V1_LEN_TD               2
/* core ends here (OT3D_V1_CORE_SIZE, 0x66); 0x66-0x6F reserved/padding */

#define OT3D_V1_OFF_DATA_URL              0x70  /* ascii, extended-only */
#define OT3D_V1_LEN_DATA_URL              32
#define OT3D_V1_OFF_SERIAL                0x90  /* extended-only; 16 bytes, unlike v2's 32 */
#define OT3D_V1_LEN_SERIAL                16
#define OT3D_V1_OFF_MFG_DATE              0xA0  /* unit=YYYY,MM,DD, extended-only */
#define OT3D_V1_LEN_MFG_DATE              4
#define OT3D_V1_OFF_MFG_TIME              0xA4  /* unit=UTC hh:mm:ss, extended-only */
#define OT3D_V1_LEN_MFG_TIME              3
#define OT3D_V1_OFF_SPOOL_CORE_DIAMETER   0xA7  /* unit=mm, extended-only */
#define OT3D_V1_LEN_SPOOL_CORE_DIAMETER   1
#define OT3D_V1_OFF_MFI_TEMP              0xA8  /* unit=C, scaling=5, extended-only */
#define OT3D_V1_LEN_MFI_TEMP              1
#define OT3D_V1_OFF_MFI_LOAD               0xA9  /* unit=g, scaling=10, extended-only */
#define OT3D_V1_LEN_MFI_LOAD               1
#define OT3D_V1_OFF_MFI_VALUE             0xAA  /* unit=g/10min, extended-only */
#define OT3D_V1_LEN_MFI_VALUE             1
#define OT3D_V1_OFF_TOLERANCE             0xAB  /* unit=mm, extended-only */
#define OT3D_V1_LEN_TOLERANCE             1
#define OT3D_V1_OFF_EMPTY_SPOOL_WEIGHT    0xAC  /* unit=g, extended-only */
#define OT3D_V1_LEN_EMPTY_SPOOL_WEIGHT    2
#define OT3D_V1_OFF_MEASURED_WEIGHT       0xAE  /* unit=g, extended-only */
#define OT3D_V1_LEN_MEASURED_WEIGHT       2
#define OT3D_V1_OFF_MEASURED_LENGTH       0xB0  /* unit=m, extended-only */
#define OT3D_V1_LEN_MEASURED_LENGTH       2
#define OT3D_V1_OFF_MAX_DRY_TEMP          0xB2  /* unit=C, scaling=5, extended-only */
#define OT3D_V1_LEN_MAX_DRY_TEMP          1
#define OT3D_V1_OFF_DRY_TIME              0xB3  /* unit=hr, extended-only */
#define OT3D_V1_LEN_DRY_TIME              1
#define OT3D_V1_OFF_MIN_PRINT_TEMP        0xB4  /* unit=C, scaling=5, extended-only */
#define OT3D_V1_LEN_MIN_PRINT_TEMP        1
#define OT3D_V1_OFF_MAX_PRINT_TEMP        0xB5  /* unit=C, scaling=5, extended-only */
#define OT3D_V1_LEN_MAX_PRINT_TEMP        1
#define OT3D_V1_OFF_MIN_BED_TEMP          0xB6  /* unit=C, scaling=5, extended-only */
#define OT3D_V1_LEN_MIN_BED_TEMP          1
#define OT3D_V1_OFF_MAX_BED_TEMP          0xB7  /* unit=C, scaling=5, extended-only */
#define OT3D_V1_LEN_MAX_BED_TEMP          1
#define OT3D_V1_OFF_MIN_VSO               0xB8  /* unit=mm3/s, extended-only */
#define OT3D_V1_LEN_MIN_VSO               1
#define OT3D_V1_OFF_MAX_VSO               0xB9  /* unit=mm3/s, extended-only */
#define OT3D_V1_LEN_MAX_VSO               1
#define OT3D_V1_OFF_TARGET_VSO            0xBA  /* unit=mm3/s, extended-only */
#define OT3D_V1_LEN_TARGET_VSO            1
/* extended ends here (OT3D_V1_MAP_SIZE, 0xBB) */

#define OT3D_V1_CORE_SIZE            0x66
#define OT3D_V1_MAP_SIZE             0xBB

#endif /* OPENTAG3D_V1_MAP_H */
