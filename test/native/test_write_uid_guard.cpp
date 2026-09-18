// Write-UID guard tests — #276, where the scan-cooldown dedup let a bound
// write validate against a stale tag and then land on the tag actually
// selected on the reader.
#include "WriteUidGuard.h"
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } \
                              else { printf("  PASS %s\n", msg); } } while (0)

int main() {
    printf("=== Write UID guard tests ===\n");

    // Bound write, tag selected on the reader is the one the caller asked for.
    CHECK(writeUidMatches("04A651B2C3D480", "04A651B2C3D480"),
          "bound write to the selected tag is accepted");

    // The #276 regression. Two same-batch NTAGs sharing a 3-byte prefix:
    // the cooldown dedup skipped handleNewTag(), so the old code compared
    // against tag A while tag B was selected. Must now be rejected.
    CHECK(!writeUidMatches("04A651B2C3D480", "04A651FF99EE11"),
          "bound write rejected when a prefix-sharing tag is selected");

    // Wholly different tag.
    CHECK(!writeUidMatches("04A651B2C3D480", "1234567890ABCD"),
          "bound write rejected for an unrelated selected tag");

    // Fail closed: no tag selected (removed, or before first detect).
    CHECK(!writeUidMatches("04A651B2C3D480", ""),
          "bound write rejected when no tag is selected");
    CHECK(!writeUidMatches("04A651B2C3D480", nullptr),
          "bound write rejected when the selected UID is null");

    // Unbound legacy write — accepts whatever is present, unchanged behaviour.
    CHECK(writeUidMatches("", "04A651B2C3D480"),
          "unbound write accepts the selected tag");
    CHECK(writeUidMatches(nullptr, "04A651B2C3D480"),
          "null expected UID is treated as unbound");
    CHECK(writeUidMatches("", ""),
          "unbound write accepted even with no tag selected");

    // Comparison is exact — no prefix matching, either direction.
    CHECK(!writeUidMatches("04A651", "04A651B2C3D480"),
          "a truncated expected UID does not match by prefix");
    CHECK(!writeUidMatches("04A651B2C3D480", "04A651"),
          "a truncated selected UID does not match by prefix");

    // Field-update / atomic writes build their payload from currentSpool's
    // CBOR, so they additionally require currentSpool to describe the bound
    // tag — otherwise a cooldown-stale currentSpool would donate the wrong
    // tag's data even when the right tag is selected.
    CHECK(fieldUpdateWriteAllowed("04A651B2C3D480", "04A651B2C3D480", "04A651B2C3D480"),
          "field update allowed when selected tag and spool data both match");
    CHECK(!fieldUpdateWriteAllowed("04A651B2C3D480", "04A651FF99EE11", "04A651B2C3D480"),
          "field update rejected when a prefix-sharing tag is selected (the #276 swap)");
    CHECK(!fieldUpdateWriteAllowed("04A651FF99EE11", "04A651FF99EE11", "04A651B2C3D480"),
          "field update rejected when currentSpool still describes the previous tag");
    CHECK(!fieldUpdateWriteAllowed("04A651B2C3D480", "", "04A651B2C3D480"),
          "bound field update rejected when no tag is selected");
    CHECK(!fieldUpdateWriteAllowed("04A651B2C3D480", "04A651B2C3D480", ""),
          "bound field update rejected when currentSpool has no UID");
    CHECK(!fieldUpdateWriteAllowed("04A651B2C3D480", "04A651B2C3D480", nullptr),
          "bound field update rejected when currentSpool UID is null");
    CHECK(fieldUpdateWriteAllowed("", "04A651B2C3D480", "04A651FF99EE11"),
          "unbound field update keeps legacy accept-any behaviour");
    CHECK(fieldUpdateWriteAllowed(nullptr, "", ""),
          "null expected UID is treated as unbound for field updates");

    // capacityBoundToSelected — #316: checkWriteCapacity() must refuse to
    // trust a size measured for a different tag than the one now selected.
    // Unlike writeUidMatches, there is no "unbound" case: an unmeasured size
    // is never safe to use, so both sides must be present and match exactly.
    CHECK(capacityBoundToSelected("04A651B2C3D480", "04A651B2C3D480"),
          "capacity bound to selected tag when both UIDs match exactly");
    CHECK(!capacityBoundToSelected("04A651B2C3D480", "04A651FF99EE11"),
          "capacity not bound when only the first 3 bytes match (the #316 swap)");
    CHECK(!capacityBoundToSelected("", "04A651B2C3D480"),
          "capacity not bound when the measured UID is empty");
    CHECK(!capacityBoundToSelected("04A651B2C3D480", ""),
          "capacity not bound when no tag is selected");
    CHECK(!capacityBoundToSelected("", ""),
          "capacity not bound when both UIDs are empty");
    CHECK(!capacityBoundToSelected(nullptr, "04A651B2C3D480"),
          "capacity not bound when the measured UID is null");
    CHECK(!capacityBoundToSelected("04A651B2C3D480", nullptr),
          "capacity not bound when the selected UID is null");

    if (failures == 0) printf("All write UID guard tests passed\n");
    else printf("%d test(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
