// Raw-write staging slot and request ids. The slot is shared by the web/app
// tasks (producers) and the NFC scan task (consumer); request ids come from
// several tasks.
#include "../../src/RawWriteSlot.h"
#include "../../src/RequestId.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } \
                              else { printf("  PASS %s\n", msg); } } while (0)

using Slot = RawWriteSlot<320>;

// Stage from inside a release that is still clearing: it must be refused, or
// the clear that follows wipes the new payload (old slot freed first).
struct StageDuringClear {
    static bool tried, accepted;
    template <typename S> static void midClear(S& s) {
        static const uint8_t p[2] = {9, 9};
        tried = true;
        if (s.stage(99, "ZZ", p, 2)) accepted = true;
    }
};
bool StageDuringClear::tried = false;
bool StageDuringClear::accepted = false;

static void testNoStageWhileClearing() {
    printf("-- release clears before it frees\n");
    RawWriteSlot<320, StageDuringClear> s;
    const uint8_t a[4] = {1, 2, 3, 4};
    s.stage(7, "AA", a, 4);
    s.release(7);
    CHECK(StageDuringClear::tried && !StageDuringClear::accepted,
          "a stage during the clear is refused");
    StageDuringClear::tried = StageDuringClear::accepted = false;
    s.stage(8, "AA", a, 4);
    s.consume(8, [](const uint8_t*, size_t) {});
    CHECK(StageDuringClear::tried && !StageDuringClear::accepted,
          "a stage during a consume's clear is refused");
}

static void testSlotBasics() {
    printf("-- slot basics\n");
    Slot s;
    const uint8_t a[4] = {1, 2, 3, 4};
    CHECK(!s.busy(), "starts empty");
    CHECK(!s.stage(0, "AA", a, 4), "request id 0 is refused");
    CHECK(!s.stage(7, "AA", a, 0), "empty payload is refused");
    uint8_t big[321] = {0};
    CHECK(!s.stage(7, "AA", big, sizeof(big)), "oversized payload is refused");
    CHECK(!s.busy(), "refused stage leaves the slot empty");

    CHECK(s.stage(7, "AA", a, 4), "stage succeeds");
    CHECK(s.busy(), "busy after stage");
    CHECK(s.ownedBy(7) && !s.ownedBy(8), "owner is the staging request");
    CHECK(!s.stage(8, "BB", a, 4), "second stage refused while busy");

    size_t seen = 0;
    uint8_t first = 0;
    CHECK(!s.consume(8, [&](const uint8_t* d, size_t n) { seen = n; first = d[0]; }),
          "another request cannot consume it");
    CHECK(s.busy() && seen == 0, "failed consume leaves the payload staged");
    CHECK(!s.release(8), "another request cannot release it");
    CHECK(s.busy(), "failed release leaves the payload staged");

    CHECK(s.consume(7, [&](const uint8_t* d, size_t n) { seen = n; first = d[0]; }),
          "owner consumes");
    CHECK(seen == 4 && first == 1, "consumer sees the staged bytes");
    CHECK(!s.busy(), "consume frees the slot");
    CHECK(!s.release(7), "release after consume is a no-op");

    CHECK(s.stage(9, "CC", a, 4), "stage again after consume");
    CHECK(s.release(9), "owner releases without consuming");
    CHECK(!s.busy(), "release frees the slot");
    CHECK(s.stage(10, "DD", a, 4) && s.releaseAny() && !s.busy(), "reboot path releases any owner");
    CHECK(!s.releaseAny(), "releaseAny on an empty slot is a no-op");

    CHECK(s.stage(11, "EE", a, 4), "stage for the uid check");
    CHECK(strcmp(s.uid(), "EE") == 0, "uid kept with the payload");
    s.release(11);
    CHECK(s.uid()[0] == '\0', "uid cleared on release");
}

// Producer stages a pattern unique to its request id; the consumer checks that
// every byte it reads belongs to the id it consumed. The old slot cleared its
// flag before its buffer, so a stage landing in between was wiped.
static void testSlotStress() {
    printf("-- slot two-thread stress\n");
    Slot s;
    std::atomic<bool> stop{false};
    std::atomic<uint32_t> staged{0}, consumed{0}, corrupt{0}, released{0};
    std::atomic<uint32_t> lastId{0};

    std::thread producer([&] {
        uint8_t buf[64];
        for (uint32_t id = 1; id < 200000 && !stop.load(); id++) {
            memset(buf, (uint8_t)(id & 0xFF), sizeof(buf));
            if (s.stage(id, "AABB", buf, sizeof(buf))) {
                staged++;
                lastId.store(id);
                // A failed enqueue releases its own slot (producer side)
                if ((id % 7) == 0 && s.release(id)) released++;
            }
        }
        stop.store(true);
    });
    std::thread consumer([&] {
        while (!stop.load() || s.busy()) {
            uint32_t id = lastId.load();
            if (id == 0) continue;
            s.consume(id, [&](const uint8_t* d, size_t n) {
                for (size_t i = 0; i < n; i++) {
                    if (d[i] != (uint8_t)(id & 0xFF)) { corrupt++; break; }
                }
                if (n != 64) corrupt++;
                consumed++;
            });
            // Terminal failure path on the scan task releases by id too
            if ((id % 11) == 0) s.release(id);
        }
    });
    producer.join();
    consumer.join();
    s.releaseAny();
    char msg[96];
    snprintf(msg, sizeof(msg), "no torn or wiped payload (%u staged, %u consumed)",
             (unsigned)staged.load(), (unsigned)consumed.load());
    CHECK(corrupt.load() == 0 && consumed.load() > 0, msg);
    CHECK(!s.busy(), "slot ends empty");
}

static void testRequestIds() {
    printf("-- request ids\n");
    std::atomic<uint32_t> c{0xFFFFFFFEu};
    uint32_t a = nextRequestId(c);
    uint32_t b = nextRequestId(c);
    uint32_t d = nextRequestId(c);
    CHECK(a == 0xFFFFFFFFu && b != 0 && d != 0 && b != d, "wrap skips 0");

    std::atomic<uint32_t> shared{1000};
    static uint32_t seen[4][20000];
    std::thread t[4];
    for (int k = 0; k < 4; k++) {
        t[k] = std::thread([&, k] { for (int i = 0; i < 20000; i++) seen[k][i] = nextRequestId(shared); });
    }
    for (auto& th : t) th.join();
    static uint8_t hit[90000] = {0};
    bool dup = false;
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < 20000; i++) {
            uint32_t v = seen[k][i] - 1000;
            if (v >= sizeof(hit) || hit[v]++) dup = true;
        }
    }
    CHECK(!dup, "four tasks never get the same id");
}

int main() {
    printf("=== Raw write slot + request id tests ===\n");
    testSlotBasics();
    testNoStageWhileClearing();
    testSlotStress();
    testRequestIds();
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
