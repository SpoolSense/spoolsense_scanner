// OpenTag3D layout derivation tests — verifies that the has_extended flag
// is correctly computed for both replace-mode and preserve-mode writes.
#include "OpenTag3DLayout.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } \
                              else { printf("  PASS %s\n", msg); } } while (0)

int main() {
    printf("=== OpenTag3D layout derivation tests ===\n");

    // Replace mode: only the patch mask matters. If it touches an extended field,
    // the layout must be extended (187 bytes) or the caller's extended data is lost.

    // R1: replace mode with an extended field set (manufacture_year) → extended.
    {
        uint64_t mask = 0;
        ot3d_mask_set(&mask, OT3D_F_MANUFACTURE_YEAR);
        uint8_t result = deriveOpenTag3DLayout(OT3D_MODE_REPLACE, mask, 0, 1003);
        CHECK(result == 1, "R1: replace with extended field → extended layout");
    }

    // R2: replace mode with only core fields (target_weight_g) → core-only.
    {
        uint64_t mask = 0;
        ot3d_mask_set(&mask, OT3D_F_TARGET_WEIGHT_G);
        uint8_t result = deriveOpenTag3DLayout(OT3D_MODE_REPLACE, mask, 0, 1003);
        CHECK(result == 0, "R2: replace with core-only fields → core-only layout");
    }

    // R3: replace mode with no fields set (empty patch) → core-only.
    {
        uint64_t mask = 0;
        uint8_t result = deriveOpenTag3DLayout(OT3D_MODE_REPLACE, mask, 0, 1003);
        CHECK(result == 0, "R3: replace with empty patch → core-only layout");
    }

    // Preserve mode: baseline + patch. Extended baseline stays extended,
    // core-only baseline grows only if the patch sets extended fields.

    // P1: preserve mode on an extended baseline (has_extended=1) with only
    // core-only patch → stays extended (because the original was extended,
    // even though this edit doesn't touch extended fields).
    {
        uint64_t mask = 0;
        ot3d_mask_set(&mask, OT3D_F_TARGET_WEIGHT_G);
        uint8_t result = deriveOpenTag3DLayout(OT3D_MODE_PRESERVE, mask, 1, 1003);
        CHECK(result == 1, "P1: preserve extended baseline + core-only patch → extended");
    }

    // P2: preserve mode on a core-only baseline (has_extended=0) with only
    // core-only patch → stays core-only.
    {
        uint64_t mask = 0;
        ot3d_mask_set(&mask, OT3D_F_TARGET_WEIGHT_G);
        uint8_t result = deriveOpenTag3DLayout(OT3D_MODE_PRESERVE, mask, 0, 1003);
        CHECK(result == 0, "P2: preserve core baseline + core-only patch → core-only");
    }

    // P3: preserve mode on a core-only baseline (has_extended=0) with an
    // extended field patch → grows to extended.
    {
        uint64_t mask = 0;
        ot3d_mask_set(&mask, OT3D_F_MANUFACTURE_YEAR);
        uint8_t result = deriveOpenTag3DLayout(OT3D_MODE_PRESERVE, mask, 0, 1003);
        CHECK(result == 1, "P3: preserve core baseline + extended patch → extended");
    }

    // P4: preserve mode on an extended baseline with an extended field patch
    // → stays extended (obviously, baseline was already extended).
    {
        uint64_t mask = 0;
        ot3d_mask_set(&mask, OT3D_F_MFI_VALUE);
        uint8_t result = deriveOpenTag3DLayout(OT3D_MODE_PRESERVE, mask, 1, 1003);
        CHECK(result == 1, "P4: preserve extended baseline + extended patch → extended");
    }

    // V2 tags: the function computes a value for consistency (based on patch),
    // but encodeSize ignores it since v2 tags have fixed-size encoding.
    // We verify the function still computes something sensible for the v2 case.

    // V1: v2 tag (version >= 2000) in replace mode with extended field
    // → function returns extended (even though encodeSize will ignore it).
    {
        uint64_t mask = 0;
        ot3d_mask_set(&mask, OT3D_F_MANUFACTURE_YEAR);
        uint8_t result = deriveOpenTag3DLayout(OT3D_MODE_REPLACE, mask, 0, 2003);
        CHECK(result == 1, "V1: v2 replace with extended field → function says extended");
    }

    // V2: v2 tag in replace mode with core-only fields → core-only
    // (again, encodeSize will ignore this).
    {
        uint64_t mask = 0;
        ot3d_mask_set(&mask, OT3D_F_TARGET_WEIGHT_G);
        uint8_t result = deriveOpenTag3DLayout(OT3D_MODE_REPLACE, mask, 0, 2003);
        CHECK(result == 0, "V2: v2 replace with core fields → function says core-only");
    }

    // V3: v2 tag in preserve mode with extended baseline and core-only patch
    // → function derives extended-ness even though encodeSize won't use it.
    {
        uint64_t mask = 0;
        ot3d_mask_set(&mask, OT3D_F_TARGET_WEIGHT_G);
        uint8_t result = deriveOpenTag3DLayout(OT3D_MODE_PRESERVE, mask, 1, 2003);
        CHECK(result == 1, "V3: v2 preserve extended baseline → function says extended");
    }

    if (failures == 0) printf("All OpenTag3D layout tests passed\n");
    else printf("%d test(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
