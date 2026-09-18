#pragma once

#include "opentag3d_lib.h"
#include "opentag3d_patch.h"

// Decide the has_extended flag for an OpenTag3D write.
//
// The has_extended flag is a LAYOUT DECISION, not a user-supplied field — it
// determines whether a v1 tag encodes to 102 bytes (core-only) or 187 bytes
// (extended). The HTTP handler never sets it; we derive it from the write mode
// and the patch content.
//
//   mode                — OT3D_MODE_REPLACE or OT3D_MODE_PRESERVE
//   mask                — patch mask bitfield identifying which fields are set
//   baselineHasExtended — the has_extended flag of the tag being read (preserve mode)
//   baselineVersion     — the tag version (checked for v2; layout decision
//                         ignored when major version >= 2)
//
// Returns: 0 or 1, the has_extended flag to encode with.
//
// Replace mode: derive from patch. If the patch touches any extended field,
// the encoded layout must be 187 bytes or every extended value the caller
// supplied is silently dropped and readback verifies the loss.
//
// Preserve mode: derived from baseline + patch. An extended v1 tag stays
// extended even when the patch touches only core fields, and a core-only
// v1 tag grows only if the patch actually sets an extended field.
//
// V2 tags (major version >= 2) ignore this flag in the encode-size decision,
// so the value is computed for consistency but not used by encodeSize.
static inline uint8_t deriveOpenTag3DLayout(uint8_t mode, uint64_t mask,
                                             uint8_t baselineHasExtended,
                                             uint16_t baselineVersion __attribute__((unused))) {
    if (mode == OT3D_MODE_REPLACE) {
        // Replace: bare assignment from patch. Derive the flag so a patch
        // setting extended fields produces the extended layout. V2 tags ignore
        // this flag in encodeSize, but derive it for consistency.
        return opentag3d_patch_touches_extended(mask) ? 1 : 0;
    } else {
        // Preserve: baseline + patch. Extended baseline stays extended even
        // on a core-only edit, and core-only baseline grows only if the patch
        // sets an extended field. This logic applies to both v1 and v2 tags;
        // v2 tags ignore the flag but derive it for consistency.
        return (baselineHasExtended || opentag3d_patch_touches_extended(mask)) ? 1 : 0;
    }
}
