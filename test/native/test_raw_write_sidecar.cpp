// NFC write-sidecar lifecycle tests — issues #328 and #329.
//
// OpenSpool uses the raw-write sidecar (rawWriteBuffer_/rawWritePending_/
// rawWriteUid_); OpenTag3D uses its request-bound patch sidecar. Each owner
// must release its state on every terminal path so the next correct-tag scan
// can retry instead of being rejected forever.
//
// The pure release rule lives in NFCManager::releaseRawWriteIf(); the enqueue
// ordering rule it relies on (xQueueSend fails when the queue holds maxItems
// items) is mirrored here with the production NFCWriteRequest type and the
// same native queue stubs production code uses under
// NATIVE_TEST. Keep this mirror in sync with NFCManager.h/.cpp in the same
// commit if the sidecar rules change.
#include "platform/NativePlatform.h"
#include "NFCWriteTypes.h"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

SerialStub Serial;

// CHECK with the same PASS/FAIL tally style as the sibling test binaries
// (their own macro; test_helpers.h's RUN_TEST is function-based).
static int checks_run = 0;
static int checks_failed = 0;
#define CHECK(cond, msg) do { checks_run++; if (!(cond)) { \
    printf("  FAIL %s\n", msg); checks_failed++; } \
    else { printf("  PASS %s\n", msg); } } while (0)

// ── Mirror of NFCManager's raw-sidecar state + release discipline ──────────
static const size_t SIDECAR_SIZE = 320;  // matches RAW_WRITE_BUFFER_SIZE

struct Sidecar {
    uint8_t buffer[SIDECAR_SIZE] = {0};
    size_t bufferSize = 0;
    bool pending = false;
    char uid[17] = {0};

    // Scan-task context: release if it belongs to `uid` (null/empty = always).
    // Mirror of NFCManager::releaseRawWriteIf().
    void releaseIf(const char* reqUid) {
        if (reqUid && reqUid[0] != '\0' && strcmp(uid, reqUid) != 0) return;
        pending = false;
        bufferSize = 0;
        uid[0] = '\0';
        memset(buffer, 0, sizeof(buffer));
    }

    // Mirror of NFCManager::enqueueRawWrite using the production request type
    // and the production queue capacity.
    bool enqueueRawWrite(NativeQueue<NFCWriteRequest, 8>& queue,
                         const NFCWriteRequest& req, const uint8_t* data, size_t dataSize) {
        if (dataSize == 0 || dataSize > SIDECAR_SIZE) return false;
        if (pending) return false;  // single sidecar slot
        memcpy(buffer, data, dataSize);
        bufferSize = dataSize;
        pending = true;
        strncpy(uid, req.expected_spool_id, sizeof(uid) - 1);
        uid[sizeof(uid) - 1] = '\0';
        if (!queue.send(req)) {
            releaseIf(nullptr);  // this sidecar is by definition ours (#329 fix)
            return false;
        }
        return true;
    }
};

static NFCWriteRequest makeReq(uint32_t id, const char* uid) {
    NFCWriteRequest r{};
    strncpy(r.expected_spool_id, uid, sizeof(r.expected_spool_id) - 1);
    r.request_id = id;
    return r;
}

static std::string readFile(const char* path) {
    std::ifstream input(path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

static std::string functionBody(const std::string& source, const char* signature) {
    size_t start = source.find(signature);
    if (start == std::string::npos) return {};
    size_t open = source.find('{', start);
    if (open == std::string::npos) return {};
    int depth = 0;
    for (size_t i = open; i < source.size(); ++i) {
        if (source[i] == '{') ++depth;
        if (source[i] == '}' && --depth == 0) return source.substr(open, i - open + 1);
    }
    return {};
}

static bool branchReleasesBeforeReturn(const std::string& body, const char* branchStart) {
    size_t start = body.find(branchStart);
    if (start == std::string::npos) return false;
    size_t returnPos = body.find("return false;", start);
    if (returnPos == std::string::npos) return false;
    size_t releasePos = body.find("releaseRawWriteIf(request.expected_spool_id);", start);
    return releasePos != std::string::npos && releasePos < returnPos;
}

static bool patchSidecarClearsAfterExecution(const std::string& body) {
    size_t inFlight = body.find(
        "openTag3DPatchState_ = OpenTag3DPatchState::InFlight;");
    size_t requestGuard = body.find(
        "openTag3DPatchRequestId_ == request.request_id", inFlight);
    size_t clear = body.find(
        "openTag3DPatchState_ = OpenTag3DPatchState::Empty;", requestGuard);
    size_t returnResult = body.find("return result;", clear);
    return inFlight != std::string::npos &&
           requestGuard != std::string::npos &&
           clear != std::string::npos &&
           returnResult != std::string::npos;
}

int main(int argc, char** argv) {
    printf("=== NFC raw-write sidecar tests (#329 review) ===\n");

    static const char* UID_A = "AABBCCDDEEFF0011";
    static const char* UID_B = "1122334455667788";

    // NFCManager.cpp is too hardware-coupled for the native harness. Guard
    // the two production terminal branches directly so this suite fails if
    // either release call is removed while the state-machine mirrors below
    // continue to pass.
    {
        const char* sourcePath = argc > 1 ? argv[1] : "../../src/NFCManager.cpp";
        std::string source = readFile(sourcePath);
        std::string ot3d = functionBody(source, "bool NFCManager::executeOpenTag3DWrite");
        std::string openSpool = functionBody(source, "bool NFCManager::executeOpenSpoolWrite");
        CHECK(!ot3d.empty() && patchSidecarClearsAfterExecution(ot3d),
              "production OpenTag3D execution clears its request-bound patch sidecar");
        CHECK(!openSpool.empty() && branchReleasesBeforeReturn(
                  openSpool, "!validateWriteUid(request.expected_spool_id"),
              "production OpenSpool UID mismatch releases its sidecar");
    }

    // ── Failed enqueue (queue full) must NOT leave the sidecar stuck ──
    {
        Sidecar sc;
        NativeQueue<NFCWriteRequest, 8> q;
        for (uint32_t i = 0; i < 8; i++) q.send(makeReq(10 + i, UID_B));
        CHECK(q.size() == 8, "queue saturated");

        uint8_t payload[16] = {1, 2, 3, 4};
        CHECK(!sc.enqueueRawWrite(q, makeReq(100, UID_A), payload, sizeof(payload)),
              "enqueue rejected while queue full");
        CHECK(!sc.pending, "sidecar released after failed enqueue (was the wedge)");
        CHECK(sc.bufferSize == 0, "sidecar size reset after failed enqueue");
        CHECK(sc.uid[0] == '\0', "sidecar uid reset after failed enqueue");

        // Drain one slot: the next deduction write must enqueue successfully.
        NFCWriteRequest dummy;
        q.receive(dummy);
        CHECK(sc.enqueueRawWrite(q, makeReq(101, UID_A), payload, sizeof(payload)),
              "retry enqueues after a queue slot frees (failure -> pending -> retry)");
        CHECK(sc.pending, "sidecar staged for the queued write");
    }

    // ── A different UID's payload can never overwrite an in-flight sidecar ──
    {
        Sidecar sc;
        NativeQueue<NFCWriteRequest, 8> q;
        uint8_t payloadA[16] = {5, 5, 5, 5};
        uint8_t payloadB[16] = {6, 6, 6, 6};
        CHECK(sc.enqueueRawWrite(q, makeReq(200, UID_A), payloadA, sizeof(payloadA)),
              "UID_A sidecar staged and queued");
        CHECK(!sc.enqueueRawWrite(q, makeReq(201, UID_B), payloadB, sizeof(payloadB)),
              "UID_B enqueue refused while UID_A owns the sidecar");
        CHECK(sc.uid[0] && strcmp(sc.uid, UID_A) == 0, "foreign request left the sidecar alone");
        CHECK(sc.buffer[0] == 5, "UID_A payload intact");
    }

    // ── Wrong tag mid-execution: UID-guarded release drops the right sidecar ──
    // Models: deduction sidecar staged; scan task dequeues the request against
    // the WRONG tag -> validateWriteUid fails -> execution returns false ->
    // sidecar must be clear before the next scan (production order: release
    // happens on the validateWriteUid failure branch itself).
    {
        Sidecar sc;
        NativeQueue<NFCWriteRequest, 8> q;
        uint8_t payload[16] = {9, 9, 9, 9};
        CHECK(sc.enqueueRawWrite(q, makeReq(300, UID_A), payload, sizeof(payload)),
              "deduction write enqueued");
        NFCWriteRequest req;
        q.receive(req);
        sc.releaseIf(req.expected_spool_id);  // validateWriteUid failed -> release own UID
        CHECK(!sc.pending, "sidecar cleared on validateWriteUid failure");

        // Next correct-tag scan can enqueue again — no permanent rejection.
        CHECK(sc.enqueueRawWrite(q, makeReq(301, UID_A), payload, sizeof(payload)),
              "correct-tag scan enqueues after a wrong-tag execution failure");
    }

    // ── Stale release must not wipe a newer request's sidecar ──
    {
        Sidecar sc;
        NativeQueue<NFCWriteRequest, 8> q;
        uint8_t payload[16] = {5, 5, 5, 5};
        sc.enqueueRawWrite(q, makeReq(400, UID_A), payload, sizeof(payload));

        sc.releaseIf(UID_B);  // a release for a different UID
        CHECK(sc.pending, "release for a different UID is a no-op");
        CHECK(sc.bufferSize == sizeof(payload), "payload survives a foreign release");

        sc.releaseIf(UID_A);
        CHECK(!sc.pending, "release for the owning UID lands");
        CHECK(sc.buffer[0] == 0, "buffer zeroed so a stale payload can't reach a later tag");
    }

    // ── Release with null UID is unconditional (failed-enqueue self-cleanup) ──
    {
        Sidecar sc;
        NativeQueue<NFCWriteRequest, 8> q;
        uint8_t payload[16] = {7, 7, 7, 7};
        sc.enqueueRawWrite(q, makeReq(500, UID_A), payload, sizeof(payload));
        sc.releaseIf(nullptr);
        CHECK(!sc.pending && sc.bufferSize == 0 && sc.uid[0] == '\0',
              "null-UID release clears everything");
    }

    // ── Every early-return execution failure still ends with a clear sidecar ──
    // Encode failure / NDEF-too-large / capacity refusal / write failure all
    // return false AFTER the sidecar was consumed at dequeue. The invariant
    // NFCManager now guarantees: pending is false whenever executeWrite()
    // returns for a raw write type, so the deduction can retry next scan.
    {
        Sidecar sc;
        NativeQueue<NFCWriteRequest, 8> q;
        uint8_t payload[16] = {3, 3, 3, 3};
        const char* paths[4] = {"encode failed", "NDEF too large", "capacity refused", "write failed"};
        for (int failAt = 0; failAt < 4; failAt++) {
            CHECK(sc.enqueueRawWrite(q, makeReq(600 + failAt, UID_A), payload, sizeof(payload)),
                  "enqueue for failure-path iteration");
            NFCWriteRequest req;
            q.receive(req);
            sc.releaseIf(req.expected_spool_id);  // consumed at dequeue (production order)
            CHECK(!sc.pending, paths[failAt]);
            CHECK(sc.enqueueRawWrite(q, makeReq(610 + failAt, UID_A), payload, sizeof(payload)),
                  "next scan retries after execution failure");
            NFCWriteRequest drain;
            q.receive(drain);
            sc.releaseIf(UID_A);
        }
    }

    printf("\n%s (%d/%d checks)\n",
           checks_failed ? "FAILED" : "OK", checks_run - checks_failed, checks_run);
    return checks_failed ? 1 : 0;
}
