// CC sizing tests: size unidentified NFC chips (clone/unbranded NTAGs that
// fail or mis-answer GET_VERSION) from their Type 2 Capability Container
// (page 3) instead of the flat unknown-variant ceilings. Hard rule: NEVER
// floor or round up a CC-declared size — a 213-class clone honestly declares
// 144 bytes (end page 40) and forcing a larger minimum runs reads/writes
// past the die end.
#include "../../src/NFCTypes.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } \
                              else { printf("  PASS %s\n", msg); } } while (0)

int main() {
    printf("=== CC sizing tests ===\n");

    // Real NTAG213 CC: byte2 0x12 = 18 -> 144 B -> user pages 4..39.
    // Asserted EXACTLY 40 — the no-floor property: padding past the die end
    // aborts the whole read/write, so the declared size is trusted verbatim.
    const uint8_t cc213[4] = {0xE1, 0x10, 0x12, 0x00};
    CHECK(ccUserMemoryEnd(cc213) == 40, "NTAG213 CC declares exactly 40 pages (no-floor)");

    const uint8_t cc215[4] = {0xE1, 0x10, 0x3F, 0x00};
    CHECK(ccUserMemoryEnd(cc215) == 130, "NTAG215 CC (504 B) declares 130 pages");

    const uint8_t cc216[4] = {0xE1, 0x10, 0x6F, 0x00};
    CHECK(ccUserMemoryEnd(cc216) == 226, "NTAG216 CC (888 B) declares 226 pages");

    // Wrong magic is not a CC.
    const uint8_t badMagic00[4] = {0x00, 0x10, 0x12, 0x00};
    const uint8_t badMagicE2[4] = {0xE2, 0x10, 0x12, 0x00};
    CHECK(ccUserMemoryEnd(badMagic00) == 0, "magic 0x00 rejected");
    CHECK(ccUserMemoryEnd(badMagicE2) == 0, "magic 0xE2 rejected");

    // Implausible sizes rejected: byte2 0 -> 4 pages (below any real Type 2
    // chip); byte2 0xFF -> 2040 B / end 514 (past the largest die).
    const uint8_t ccZeroSize[4] = {0xE1, 0x10, 0x00, 0x00};
    const uint8_t ccHugeSize[4] = {0xE1, 0x10, 0xFF, 0x00};
    // byte2 0x70 -> 896 B / end 228: past NTAG216's USER end (226) into the
    // config-page window — a corrupt/dishonest CC must not bless writes there.
    const uint8_t ccPastUserEnd[4] = {0xE1, 0x10, 0x70, 0x00};
    CHECK(ccUserMemoryEnd(ccPastUserEnd) == 0, "byte2 0x70 rejected (past NTAG216 user end)");
    CHECK(ccUserMemoryEnd(ccZeroSize) == 0, "byte2 0 rejected (below plausibility)");
    CHECK(ccUserMemoryEnd(ccHugeSize) == 0, "byte2 0xFF rejected (past the largest die)");

    CHECK(ccUserMemoryEnd(nullptr) == 0, "null CC pointer yields no size");

    // Effective end: identified variant wins; otherwise the CC-declared size;
    // 0 when neither is known (flat fallback ceilings then apply).
    CHECK(effectiveUserMemoryEnd(NtagVariant::NTAG215, 226) == 130,
          "identified variant size wins over the CC value");
    CHECK(effectiveUserMemoryEnd(NtagVariant::Unknown, 226) == 226,
          "unknown variant falls back to the CC-declared size");
    CHECK(effectiveUserMemoryEnd(NtagVariant::Unknown, 0) == 0,
          "unknown variant without a CC has no size");

    if (failures == 0) printf("All CC sizing tests passed\n");
    else printf("%d test(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}