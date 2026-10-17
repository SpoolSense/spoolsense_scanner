// DeductionTracker tests — issue #329: a pending weight deduction must not be
// marked done before the tag write finishes. Pure-state regression tests for
// the claim -> in-flight -> settled lifecycle that DeductionManager wraps in
// its mutex + NVS read-modify-write.
#include "DeductionTracker.h"
#include "DeductionPolicy.h"
#include <cstdio>
#include <cstring>

static int failures = 0
    ;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } \
                              else { printf("  PASS %s\n", msg); } } while (0)

static const char* UID_A = "04A651B2C3D480";
static const char* UID_B = "04E9A7AD8F6180";

int main() {
    printf("=== Deduction tracker tests (#329) ===\n");

    // ── Duplicate same-UID claim is rejected while one is in flight ──
    {
        DeductionTracker t;
        CHECK(t.claim(UID_A, 25.0f), "first claim accepted");
        // The #329 race: SPOOL_DETECTED handler and MQTT deduct handler both
        // read 25 g pending. The loser must be rejected, not queue a 2nd write.
        CHECK(!t.claim(UID_A, 25.0f), "second claim for same UID rejected while in flight");
        CHECK(t.usedSlots() == 1, "only one slot used by duplicate attempts");
    }

    // ── Different UIDs get independent slots ──
    {
        DeductionTracker t;
        CHECK(t.claim(UID_A, 25.0f), "claim UID_A");
        CHECK(t.claim(UID_B, 10.0f), "claim UID_B independent of UID_A");
        float snap = 0.0f; uint32_t rid = 0;
        CHECK(t.findActive(UID_B, &snap, &rid) && snap == 10.0f,
              "findActive returns UID_B's own snapshot");
    }

    // ── Request-id correlation: bind -> resolve settles the right claim ──
    {
        DeductionTracker t;
        CHECK(t.claim(UID_A, 25.0f), "claim UID_A");
        CHECK(t.claim(UID_B, 30.0f), "claim UID_B");
        t.bind(UID_A, 101);
        t.bind(UID_B, 102);
        float snap = 0.0f;
        CHECK(t.resolve(101, true, &snap) && snap == 25.0f, "resolve(101) settles UID_A snapshot");
        CHECK(strcmp(t.lastSettledUid(), UID_A) == 0, "lastSettledUid is UID_A");
        CHECK(t.resolve(102, true, &snap) && snap == 30.0f, "resolve(102) settles UID_B snapshot");
        CHECK(t.activeCount() == 0, "all slots free after settlement");
    }

    // ── Unknown / stale / zero request ids never settle anything ──
    {
        DeductionTracker t;
        CHECK(t.claim(UID_A, 25.0f), "claim UID_A");
        t.bind(UID_A, 101);
        float snap = 0.0f;
        CHECK(!t.resolve(0, true, &snap), "request_id 0 is a no-op");
        CHECK(!t.resolve(999, true, &snap), "unknown request_id is a no-op");
        CHECK(t.activeCount() == 1, "no-op resolves leave the claim intact");
        CHECK(t.resolve(101, true, &snap), "matching result settles");
        CHECK(!t.resolve(101, true, &snap), "stale duplicate result is a no-op");
    }

    // ── Non-deduction write results must not clear a deduction claim ──
    // REMOVE_WEIGHT/WRITE_OPENTAG3D writes from other sources share the
    // SPOOL_UPDATED event; only claimed request_ids settle.
    {
        DeductionTracker t;
        CHECK(t.claim(UID_A, 25.0f), "claim UID_A");
        t.bind(UID_A, 101);
        float snap = 0.0f;
        // An unrelated web write completed with id 7 (never claimed).
        CHECK(!t.resolve(7, true, &snap), "unrelated write result does not settle the claim");
        CHECK(!t.resolve(7, false, &snap), "unrelated failure result does not release the claim");
        CHECK(t.findActive(UID_A, &snap, nullptr) && snap == 25.0f, "claim still active after unrelated results");
    }

    // ── Failure returns the claim to pending; retry on next scan ──
    {
        DeductionTracker t;
        CHECK(t.claim(UID_A, 25.0f), "claim UID_A");
        t.bind(UID_A, 101);
        float snap = 0.0f;
        CHECK(t.resolve(101, false, &snap), "failure resolve reports the snapshot");
        CHECK(snap == 25.0f, "snapshot preserved on failure (NVS untouched by design)");
        CHECK(t.activeCount() == 0, "failure frees the claim");
        CHECK(t.claim(UID_A, 25.0f), "retry claim on next scan succeeds");  // pending -> in flight again
    }

    // ── Enqueue abort: write never entered the queue ──
    {
        DeductionTracker t;
        CHECK(t.claim(UID_A, 25.0f), "claim UID_A");
        t.bind(UID_A, 101);
        t.abort(101);  // enqueueWrite() failed
        CHECK(t.activeCount() == 0, "abort releases the bound claim");
        float snap = 0.0f; uint32_t rid = 0;
        CHECK(!t.findActive(UID_A, &snap, &rid), "no active claim after abort");
        CHECK(t.claim(UID_A, 25.0f), "next scan can claim again after abort");
    }

    // ── Abort before bind (preparation failure) ──
    {
        DeductionTracker t;
        CHECK(t.claim(UID_A, 25.0f), "claim UID_A (unbound)");
        CHECK(t.findActive(UID_A, nullptr, nullptr), "unbound claim is active");
        t.abortUid(UID_A);  // e.g. no cached tag data before request id exists
        CHECK(t.activeCount() == 0, "abortUid releases the unbound claim");
    }

    // ── Snapshot settlement preserves grams added during a write ──
    // Models DeductionManager::settleSnapshotLocked: on success subtract the
    // captured snapshot from the LATEST total, remove the key only when no
    // remainder exists.
    {
        DeductionTracker t;
        float nvs = 0.0f;                       // stands in for the NVS value
        nvs += 25.0f;                           // storePending 25
        CHECK(t.claim(UID_A, nvs), "claim 25 g snapshot");
        t.bind(UID_A, 101);
        nvs += 10.0f;                           // new MQTT deduct while in flight -> 35
        float snap = 0.0f;
        CHECK(t.resolve(101, true, &snap), "write verified");
        float remainder = nvs - snap;           // 35 - 25 = 10
        nvs = (remainder > 0.0f) ? remainder : 0.0f;
        CHECK(nvs == 10.0f, "10 g added during the write stays pending");
        CHECK(!t.hasAnyClaim(UID_A), "settled claim is gone");
        CHECK(t.claim(UID_A, nvs), "remaining 10 g can claim on next scan");
    }

    // Full settlement removes the key (remainder 0).
    {
        DeductionTracker t;
        float nvs = 25.0f;
        CHECK(t.claim(UID_A, nvs), "claim 25");
        t.bind(UID_A, 101);
        float snap = 0.0f;
        CHECK(t.resolve(101, true, &snap), "settle");
        float remainder = nvs - snap;
        nvs = (remainder > 0.0f) ? remainder : 0.0f;
        CHECK(nvs == 0.0f, "exact settlement clears pending");
    }

    // ── Immediate (Spoolman-direct / terminal) claims ──
    {
        DeductionTracker t;
        CHECK(t.claim(UID_A, 12.0f, DeductionTracker::State::Immediate),
              "immediate claim for tag-absent Spoolman apply");
        CHECK(!t.claim(UID_A, 12.0f), "concurrent tag-apply rejected while immediate claim active");
        float snap = 0.0f;
        CHECK(t.resolveImmediate(UID_A, true, &snap) && snap == 12.0f,
              "Spoolman success settles immediate claim");
        CHECK(t.activeCount() == 0, "slot freed");
        // Failure path leaves NVS alone and frees the claim for retry.
        CHECK(t.claim(UID_A, 12.0f, DeductionTracker::State::Immediate), "claim again");
        CHECK(t.resolveImmediate(UID_A, false, &snap) && snap == 12.0f,
              "Spoolman failure releases claim (NVS untouched -> retry)");
        // Unknown UIDs are no-ops — terminal callers stay idempotent.
        CHECK(!t.resolveImmediate(UID_B, true, &snap), "resolveImmediate on untracked UID is a no-op");
        t.abortUid(UID_B);  // must not crash or free anything
    }

    // ── releaseToImmediate: OT3D nominal branch switches to Spoolman-direct ──
    {
        DeductionTracker t;
        CHECK(t.claim(UID_A, 20.0f), "claim unbound (tag path)");
        t.bind(UID_A, 101);
        float snap = 0.0f;
        CHECK(!t.resolveImmediate(UID_A, true, &snap), "bound claim is not immediate yet");
        t.releaseToImmediate(UID_A);
        CHECK(t.resolveImmediate(UID_A, true, &snap) && snap == 20.0f,
              "after release the synchronous outcome settles");
        // A late result for the old bound id must not double-settle.
        CHECK(!t.resolve(101, true, &snap), "late tag result after terminal settle is a no-op");
    }

    // ── Slot capacity: full table rejects, settled bookkeeping recovers ──
    {
        DeductionTracker t;
        char uid[17];
        for (size_t i = 0; i < DeductionTracker::CAPACITY; i++) {
            snprintf(uid, sizeof(uid), "04000000000000%02X", (unsigned)i);
            if (!t.claim(uid, 1.0f)) { failures++; printf("  FAIL claim slot %zu\n", i); }
        }
        CHECK(t.activeCount() == DeductionTracker::CAPACITY, "table full at capacity");
        CHECK(!t.claim("04FFFFFFFFFFFF99", 1.0f), "claim on a new UID rejected when all slots active");
        float snap = 0.0f;
        t.bind("0400000000000000", 500);
        CHECK(t.resolve(500, true, &snap), "settle one slot");
        CHECK(t.claim("04FFFFFFFFFFFF99", 1.0f), "capacity recovered after settlement");
    }

    // ── Settled bookkeeping never blocks a follow-up apply ──
    // After a failed tag write the slot keeps the UID as bookkeeping; a
    // Spoolman-direct retry on a no-tag scan (different initial state) must
    // still claim it.
    {
        DeductionTracker t;
        CHECK(t.claim(UID_A, 25.0f), "tag-path claim");
        t.bind(UID_A, 101);
        float snap = 0.0f;
        CHECK(t.resolve(101, false, &snap), "write failed");
        CHECK(t.claim(UID_A, 25.0f, DeductionTracker::State::Immediate),
              "immediate retry reuses the settled bookkeeping slot");
        CHECK(!t.claim(UID_A, 25.0f), "duplicate still rejected while retry active");
    }

    // ── Claim input hygiene ──
    {
        DeductionTracker t;
        CHECK(!t.claim(nullptr, 5.0f), "null UID rejected");
        CHECK(!t.claim("", 5.0f), "empty UID rejected");
        CHECK(!t.claim(UID_A, 0.0f), "zero grams not claimable");
        CHECK(!t.claim(UID_A, -3.0f), "negative grams not claimable");
        CHECK(t.activeCount() == 0, "hygiene rejects allocated nothing");
    }

    // ── Lock-discipline state invariants (#329) ──
    // DeductionManager runs Spoolman HTTP OUTSIDE the manager mutex. The
    // per-UID claim must therefore already be in Immediate state BEFORE the
    // unlocked call starts, so:
    //  (a) a concurrent storePending (which only touches NVS under the lock)
    //      can proceed while the Spoolman call is in flight — the claim table
    //      is never consulted under contention;
    //  (b) a write result for ANOTHER UID settling during our Spoolman call
    //      (different slot) can never corrupt or settle our claim.
    {
        DeductionTracker t;
        // applyViaSpoolmanIfPending claims Immediate synchronously.
        CHECK(t.claim(UID_A, 40.0f, DeductionTracker::State::Immediate),
              "spoolman-direct apply claims Immediate up front");
        // Tag write for a DIFFERENT uid is in flight and reports failure
        // mid-way through our (simulated) Spoolman call.
        CHECK(t.claim(UID_B, 15.0f), "tag claim for UID_B");
        t.bind(UID_B, 202);
        float snap = 0.0f;
        CHECK(t.resolve(202, false, &snap) && snap == 15.0f,
              "UID_B result settles during UID_A's unlocked Spoolman phase");
        // Our Immediate claim is untouched by uid-keyed ops for other UIDs.
        float ours = 0.0f;
        CHECK(t.resolveImmediate(UID_A, true, &ours) && ours == 40.0f,
              "UID_A immediate claim survived and settles after the Spoolman call");
        CHECK(!t.resolveImmediate(UID_A, true, &ours),
              "second settle of the same claim is a no-op (idempotent)");
    }

    // ── Duplicate claim rejected while the Immediate claim is mid-Spoolman ──
    {
        DeductionTracker t;
        CHECK(t.claim(UID_A, 40.0f, DeductionTracker::State::Immediate), "first apply claims");
        // MQTT retry arrives while the first Spoolman call is still in flight.
        CHECK(!t.claim(UID_A, 40.0f, DeductionTracker::State::Immediate),
              "duplicate immediate apply rejected mid-Spoolman");
        CHECK(!t.claim(UID_A, 40.0f), "duplicate tag-path apply also rejected mid-Spoolman");
        float snap = 0.0f;
        CHECK(t.resolveImmediate(UID_A, false, &snap), "failed Spoolman call frees the claim");
        CHECK(t.claim(UID_A, 40.0f, DeductionTracker::State::Immediate),
              "retry apply succeeds after failure released the claim");
    }

    // ── Spoolman-unavailable policy distinguishes absent vs presented tag ──
    {
        CHECK(!deductionConsumesWithoutSpoolman(false),
              "tag-absent apply stays pending when Spoolman is unavailable");
        CHECK(deductionConsumesWithoutSpoolman(true),
              "presented non-writable tag keeps terminal-drop policy");
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
