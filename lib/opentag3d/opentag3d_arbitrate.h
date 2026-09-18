#ifndef OPENTAG3D_ARBITRATE_H
#define OPENTAG3D_ARBITRATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the firmware found on the tag it is about to write. */
typedef enum {
    OT3D_TARGET_WRITTEN = 0,    /* readable, canonical OpenTag3D payload */
    OT3D_TARGET_BLANK,          /* no NDEF record at all — a new tag */
    OT3D_TARGET_UNREADABLE,     /* OpenTag3D record present but unusable */
    OT3D_TARGET_OTHER_FORMAT    /* a readable record of some other format */
} ot3d_target_state_t;

typedef enum {
    OT3D_DECIDE_MERGE = 0,
    OT3D_DECIDE_REPLACE,
    OT3D_DECIDE_REFUSE
} ot3d_write_decision_t;

/* The caller's request is advisory. What is actually on the tag decides.
 * A written tag is never replaced without an explicit confirmation, because
 * the data on it exists nowhere else and a readback cannot detect its loss. */
ot3d_write_decision_t ot3d_decide_write(ot3d_target_state_t target,
                                        int replace_requested,
                                        int replace_confirmed);

/* Positively establishes that a window of user memory (page 4 onward) holds
 * no NDEF message at all. OT3D_TARGET_BLANK is the only classification that
 * permits a wholesale write with no confirmation, so this must answer 1 only
 * when there is demonstrably nothing to lose: untouched (zeroed) memory, or
 * an NDEF TLV that declares a zero-length message. Anything else — including
 * a TLV header whose body is damaged or unparseable — answers 0, because a
 * record the scanner cannot read is not the same thing as no record. */
int ot3d_user_memory_is_blank(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* OPENTAG3D_ARBITRATE_H */
