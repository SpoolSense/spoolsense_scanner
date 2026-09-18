#include "opentag3d_arbitrate.h"
#include "opentag3d_lib.h"   /* OT3D_SUPPORTED_VERSION */

ot3d_write_decision_t ot3d_decide_write(ot3d_target_state_t target,
                                        int replace_requested,
                                        int replace_confirmed) {
    switch (target) {
        case OT3D_TARGET_BLANK:
            /* Nothing to preserve — every write to a blank tag creates it. */
            return OT3D_DECIDE_REPLACE;
        case OT3D_TARGET_WRITTEN:
            return (replace_requested && replace_confirmed) ? OT3D_DECIDE_REPLACE
                                                            : OT3D_DECIDE_MERGE;
        case OT3D_TARGET_UNREADABLE:
        case OT3D_TARGET_OTHER_FORMAT:
        default:
            return OT3D_DECIDE_REFUSE;
    }
}

int ot3d_user_memory_is_blank(const uint8_t *data, uint16_t len) {
    /* One page proves nothing about an empty tag, and a short read is a read
     * failure, not evidence of blankness. */
    if (data == 0 || len < 4) return 0;

    uint16_t i = 0;

    /* 03 00 — an NDEF TLV declaring a zero-length message. This is how many
     * NTAGs ship "NDEF formatted but empty"; the message carries nothing, so
     * there is nothing to merge onto. A non-zero length byte means a message
     * is declared and its bytes matter, even if they are unparseable. */
    if (data[0] == 0x03 && data[1] == 0x00) i = 2;

    /* FE — the TLV terminator. Legal here either as the whole content or
     * immediately after an empty message. */
    if (i < len && data[i] == 0xFE) i++;

    /* Everything from here must be untouched memory. A single other byte
     * means something is present — possibly a damaged record, possibly
     * another format's data — and this function must not call that blank. */
    for (; i < len; i++) {
        if (data[i] != 0x00) return 0;
    }
    return 1;
}

uint16_t ot3d_resolve_write_version(ot3d_write_decision_t decision,
                                    uint16_t requested_version,
                                    uint16_t baseline_version) {
    if (requested_version) return requested_version;
    return (decision == OT3D_DECIDE_REPLACE) ? (uint16_t)OT3D_SUPPORTED_VERSION
                                             : baseline_version;
}
