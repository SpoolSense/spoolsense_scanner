/* Native tests for the OpenTag3D write-arbitration decision.
 * Plain C, PASS/FAIL per check, non-zero exit on any failure. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "opentag3d_arbitrate.h"
#include "opentag3d_lib.h"

static int failures = 0;

#define CHECK(cond, name)                                          \
    do {                                                           \
        if (cond) { printf("  PASS  %s\n", name); }                \
        else { printf("  FAIL  %s\n", name); failures++; }         \
    } while (0)

/* A 40-byte window of user memory starting at page 4, which is what the
 * firmware hands ot3d_user_memory_is_blank. */
static void window(uint8_t *buf, const uint8_t *prefix, uint16_t prefixLen) {
    memset(buf, 0, 40);
    if (prefixLen) memcpy(buf, prefix, prefixLen);
}

int main(void) {
    printf("OpenTag3D write-arbitration tests\n");

    /* A: a written tag always merges, even when replace was requested. */
    CHECK(ot3d_decide_write(OT3D_TARGET_WRITTEN, 0, 0) == OT3D_DECIDE_MERGE,
          "A: written tag, preserve requested -> merge");
    CHECK(ot3d_decide_write(OT3D_TARGET_WRITTEN, 1, 0) == OT3D_DECIDE_MERGE,
          "A: written tag, replace requested without confirm -> merge");

    /* B: replacing a written tag needs the explicit confirmation. */
    CHECK(ot3d_decide_write(OT3D_TARGET_WRITTEN, 1, 1) == OT3D_DECIDE_REPLACE,
          "B: written tag, replace + confirm -> replace");
    CHECK(ot3d_decide_write(OT3D_TARGET_WRITTEN, 0, 1) == OT3D_DECIDE_MERGE,
          "B: confirm without a replace request does not force replace");

    /* C: a blank tag is the new-tag case. */
    CHECK(ot3d_decide_write(OT3D_TARGET_BLANK, 1, 0) == OT3D_DECIDE_REPLACE,
          "C: blank tag, replace requested -> replace");
    CHECK(ot3d_decide_write(OT3D_TARGET_BLANK, 0, 0) == OT3D_DECIDE_REPLACE,
          "C: blank tag, preserve requested -> replace (nothing to merge onto)");

    /* D: anything unreadable is refused, never treated as blank. */
    CHECK(ot3d_decide_write(OT3D_TARGET_UNREADABLE, 0, 0) == OT3D_DECIDE_REFUSE,
          "D: unreadable tag, preserve -> refuse");
    CHECK(ot3d_decide_write(OT3D_TARGET_UNREADABLE, 1, 1) == OT3D_DECIDE_REFUSE,
          "D: unreadable tag, replace + confirm -> still refuse");
    CHECK(ot3d_decide_write(OT3D_TARGET_OTHER_FORMAT, 1, 1) == OT3D_DECIDE_REFUSE,
          "D: another tag format, replace + confirm -> refuse");

    /* E: blank detection. BLANK is the one classification that permits a
     * wholesale write with no confirmation, so a false positive here is what
     * destroys a spool's tag. Everything that is not positively empty must
     * come back non-blank. */
    {
        uint8_t buf[40];

        window(buf, NULL, 0);
        CHECK(ot3d_user_memory_is_blank(buf, 40) == 1,
              "E: factory-fresh zeroed user memory -> blank");

        static const uint8_t emptyNdefTerm[] = { 0x03, 0x00, 0xFE };
        window(buf, emptyNdefTerm, sizeof(emptyNdefTerm));
        CHECK(ot3d_user_memory_is_blank(buf, 40) == 1,
              "E: empty NDEF message + terminator -> blank");

        static const uint8_t emptyNdef[] = { 0x03, 0x00 };
        window(buf, emptyNdef, sizeof(emptyNdef));
        CHECK(ot3d_user_memory_is_blank(buf, 40) == 1,
              "E: empty NDEF message, no terminator -> blank");

        static const uint8_t termOnly[] = { 0xFE };
        window(buf, termOnly, sizeof(termOnly));
        CHECK(ot3d_user_memory_is_blank(buf, 40) == 1,
              "E: bare terminator -> blank");

        /* A real OpenTag3D tag: NDEF TLV, length 0x82, media-type record. */
        static const uint8_t ot3dTag[] = { 0x03, 0x82, 0xD2, 0x19, 0x66, 0x61, 0x70 };
        window(buf, ot3dTag, sizeof(ot3dTag));
        CHECK(ot3d_user_memory_is_blank(buf, 40) == 0,
              "E: written OpenTag3D payload -> not blank");

        /* The dangerous case: an NDEF TLV that is present but damaged. The
         * record scanner cannot find a usable record in it, and calling that
         * blank would erase whatever is really there. */
        static const uint8_t corruptTlv[] = { 0x03, 0xFF, 0x00 };
        window(buf, corruptTlv, sizeof(corruptTlv));
        CHECK(ot3d_user_memory_is_blank(buf, 40) == 0,
              "E: truncated long-form NDEF TLV -> not blank");

        static const uint8_t halfWritten[] = { 0x03, 0x82, 0x00, 0x00 };
        window(buf, halfWritten, sizeof(halfWritten));
        CHECK(ot3d_user_memory_is_blank(buf, 40) == 0,
              "E: NDEF TLV header with a zeroed body -> not blank");

        /* Declared empty, but bytes follow the terminator: something was
         * there before and only the header was rewritten. */
        static const uint8_t emptyThenData[] = { 0x03, 0x00, 0xFE, 0x00, 0x11 };
        window(buf, emptyThenData, sizeof(emptyThenData));
        CHECK(ot3d_user_memory_is_blank(buf, 40) == 0,
              "E: empty NDEF header with data after the terminator -> not blank");

        /* A NULL TLV pad ahead of a real record is still a written tag. */
        static const uint8_t padThenRecord[] = { 0x00, 0x03, 0x82, 0xD2 };
        window(buf, padThenRecord, sizeof(padThenRecord));
        CHECK(ot3d_user_memory_is_blank(buf, 40) == 0,
              "E: NULL TLV pad before a record -> not blank");

        /* Another format's NDEF message (a TNF=well-known URI record). */
        static const uint8_t uriRecord[] = { 0x03, 0x0D, 0xD1, 0x01, 0x09, 0x55 };
        window(buf, uriRecord, sizeof(uriRecord));
        CHECK(ot3d_user_memory_is_blank(buf, 40) == 0,
              "E: NDEF URI record -> not blank");

        /* Too small a window proves nothing, so it cannot prove blankness. */
        window(buf, NULL, 0);
        CHECK(ot3d_user_memory_is_blank(buf, 3) == 0,
              "E: window shorter than one page -> not blank");
        CHECK(ot3d_user_memory_is_blank(NULL, 40) == 0,
              "E: no buffer -> not blank");
    }

    /* F: the version to encode is resolved from the DECISION, never from the
     * mode the request arrived with. Minting a version when the request was
     * parsed converts a tag that the arbitration then rules a merge onto. */
    CHECK(ot3d_resolve_write_version(OT3D_DECIDE_MERGE, 0, 1003) == 1003,
          "F: merge with no requested version keeps the tag's v1 version");
    CHECK(ot3d_resolve_write_version(OT3D_DECIDE_MERGE, 0, 2003) == 2003,
          "F: merge with no requested version keeps the tag's v2 version");
    CHECK(ot3d_resolve_write_version(OT3D_DECIDE_MERGE, 2003, 1003) == 2003,
          "F: an explicitly requested version is still an explicit conversion");
    CHECK(ot3d_resolve_write_version(OT3D_DECIDE_REPLACE, 0, 0) == OT3D_SUPPORTED_VERSION,
          "F: replace with no requested version mints the supported one");
    CHECK(ot3d_resolve_write_version(OT3D_DECIDE_REPLACE, 1003, 0) == 1003,
          "F: replace honours an explicitly requested version");
    {
        /* The reported defect end to end: mode=replace, no tag_version, a
         * written v1 tag. Arbitration rules MERGE, so nothing may convert it. */
        ot3d_write_decision_t d = ot3d_decide_write(OT3D_TARGET_WRITTEN, 1, 0);
        CHECK(d == OT3D_DECIDE_MERGE && ot3d_resolve_write_version(d, 0, 1003) == 1003,
              "F: a replace-mode request ruled MERGE must not convert a v1 tag");
    }

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
