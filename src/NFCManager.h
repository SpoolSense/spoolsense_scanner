#ifndef NFC_MANAGER_H
#define NFC_MANAGER_H

#ifdef NATIVE_TEST
  #include "platform/NativePlatform.h"
#else
  #include <freertos/FreeRTOS.h>
  #include <freertos/queue.h>
  #include <freertos/semphr.h>
  #include <esp_task_wdt.h>
#endif
#include "NFCTypes.h"
#include "NFCConnectionI.h"
#include "TigerTagParser.h"
#include "OpenSpoolParser.h"
#include "opentag3d_lib.h"
#include "opentag3d_patch.h"
#include "opentag3d_arbitrate.h"
#include "Type2WriteGuard.h"
#include "BambuTagParser.h"
#include "WriteOutcomeRing.h"
#include <atomic>

// Sidecar for WRITE_ATOMIC: filled by HTTP handler, consumed by scan task.
// All fields are optional — only those with has_* = true are applied.
struct AtomicWriteFields {
    bool has_material_type = false;
    uint8_t material_type = 0;
    bool has_color = false;
    uint8_t color[4] = {0};
    bool has_initial_weight = false;
    float initial_weight_g = 0;
    bool has_consumed_weight = false;
    float consumed_weight = 0;
    bool has_brand_name = false;
    char brand_name[33] = {0};
    bool has_spoolman_id = false;
    int32_t spoolman_id = 0;
    bool has_density = false;
    float density = 0;
    bool has_diameter = false;
    float diameter_mm = 0;
    bool has_material_name = false;
    char material_name[33] = {0};
    bool has_min_print_temp = false;
    int16_t min_print_temp = 0;
    bool has_max_print_temp = false;
    int16_t max_print_temp = 0;
    bool has_preheat_temp = false;
    int16_t preheat_temp = 0;
    bool has_min_bed_temp = false;
    int16_t min_bed_temp = 0;
    bool has_max_bed_temp = false;
    int16_t max_bed_temp = 0;
    volatile bool pending = false;
};

struct GenericTagSpoolInfo {
    char material_type[32];
    char manufacturer[64];
    char color_hex[8];
    float remaining_weight_g;
    int32_t spoolman_id;
    int16_t extruder_temp;
    int16_t bed_temp;
    bool valid;
};

class NFCManager {
public:
    static NFCManager& getInstance();
    bool begin();                                    // Init hardware + queues
    void startScanTask();                            // Start FreeRTOS scan task
    bool enqueueWrite(const NFCWriteRequest& req);   // Queue a write request
    bool enqueueRawWrite(const NFCWriteRequest& req, const uint8_t* data, size_t dataSize);
    void setAtomicWriteFields(const AtomicWriteFields& fields) { atomicWriteFields_ = fields; }
    bool writeSpoolmanDataToTag(int32_t spoolman_id, const char* expected_spool_id = nullptr);
    bool isRequestCompleted(uint32_t request_id);    // Check if request done
    uint32_t generateRequestId();                    // Monotonic request ID
    void requestCurrentSpool();                      // Clear dedup to resend current spool
    bool scanOnce();                                 // Single scan cycle (for testing)

    // Per-request write result, keyed by request_id AND uid so the browser
    // can confirm the outcome belongs to the tag it targeted (#316).
    struct WriteOutcome {
        uint32_t request_id = 0;
        char     uid[24] = {0};
        bool     done = false;
        bool     ok = false;
        char     error[48] = {0};
        uint32_t completed_ms = 0;
        // Reservation state: Empty (free), Pending (write accepted, no result
        // yet), Done (done == true). Kept consistent with `done` above —
        // other code and the JSON status path still read `done` directly.
        WriteOutcomeSlotState state = WriteOutcomeSlotState::Empty;
    };
    // request_id == 0 returns the most recently completed write, whatever it
    // is; otherwise looks that specific request up in the ring. A second
    // write (rescan-triggered Spoolman write-back, another tab, a deduction)
    // can complete before the browser polls again, so "most recent" alone
    // would clobber the outcome a caller is still waiting on (#316 review).
    bool getLastWriteOutcome(uint32_t request_id, WriteOutcome& out);

    bool getCurrentSpoolState(CurrentSpoolState& out);
    bool getLastTigerTagData(TigerTagData& out);
    // payloadLenOut, when given, receives the declared byte length of the
    // record this data was decoded from (0 if no tag has been scanned yet).
    bool getLastOpenTag3DData(opentag3d_t& out, uint16_t* payloadLenOut = nullptr);
    bool getLastOpenSpoolData(OpenSpoolData& out);
    bool getLastBambuTagData(BambuTagData& out);
    // Returns reader identification string (e.g. "PN5180 v3.4", "PN532 v1.6")
    bool getNfcReaderInfo(char* buf, size_t len) const;
    void pauseScanTask();
    void resumeScanTask();
    bool isWriteQueueEmpty() const;

    // Cooperative diagnostic pause for the self-test wizard (#253). Unlike
    // pauseScanTask()/vTaskSuspend, the scan task only stops at its safe loop
    // boundary (no SPI transaction in flight), so the wizard can take exclusive
    // ownership of the reader. Usage: requestScanPause(); waitForScanPaused();
    // ... drive getDiagConnection() ...; resumeScan();
    void requestScanPause() { diagPauseRequested_ = true; }
    void resumeScan()       { diagPauseRequested_ = false; }
    bool isScanPaused() const { return scanPaused_; }
    // Block until the scan task acknowledges the pause. Returns false on timeout.
    bool waitForScanPaused(uint32_t timeoutMs);
    // Reader connection — only safe to drive directly while isScanPaused() is
    // true; otherwise it races the scan loop on the SPI bus.
    NFCConnectionI* getDiagConnection() { return connection_; }

    // Dependency injection for testing
    void setConnection(NFCConnectionI* conn) { connection_ = conn; }
    void resetWriteState() {
        rawWriteSlot_ = RawWriteSlot::Empty;
        rawWriteBufferSize_ = 0;
        // Drain write queue
        if (writeQueue) {
            NFCWriteRequest dummy;
            while (xQueueReceive(writeQueue, &dummy, 0) == pdTRUE) {}
        }
    }

    // Generic tag resolved Spoolman data (set after UID lookup, cleared on tag remove)
    void setGenericTagSpoolInfo(const GenericTagSpoolInfo& info);
    void getGenericTagSpoolInfo(GenericTagSpoolInfo& out);
    void clearGenericTagSpoolInfo();

    // Recent spools history (RAM only)
    static constexpr size_t MAX_RECENT_SPOOLS = 10;
    size_t getRecentSpools(RecentSpoolEntry* entries, size_t maxEntries);
    void updateRecentSpoolSyncStatus(const char* spool_id, bool synced);

private:
    NFCManager() = default;
    NFCManager(const NFCManager&) = delete;
    NFCManager& operator=(const NFCManager&) = delete;

    // Hardware connection (injected or created internally)
    NFCConnectionI* connection_ = nullptr;
    bool ownsConnection_ = false;

    // Cooperative diagnostic pause handshake (self-test wizard, #253)
    volatile bool diagPauseRequested_ = false;
    volatile bool scanPaused_ = false;

    // Scan task
    static void scanTaskFunc(void* param);
    void scanLoop();
    bool prepareRF();
    bool isSkippableDuplicate(const uint8_t* uid, uint8_t uidLength);
    void recordSelectedUid(const uint8_t* uid, uint8_t uidLength);
    void clearSelectedUid();
    void handleNewTag(uint8_t* uid, uint8_t uidLength);
    void handleTagAbsent();

    // Internal operations
    bool readAndParseTag(uint8_t* uid, uint8_t uid_length);
    void readAndProcessISO14443Tag(const uint8_t* uid, uint8_t uidLength, const TagScanResult& scan);
    uint16_t readNdefPayload(const struct NdefRecord& rec, const uint8_t* pageData, uint16_t bytesRead,
                              uint8_t* outBuf, uint16_t outBufSize, uint16_t maxPages = 0);
    bool formatNewSpool();
    TagScanResult classifyTag(const uint8_t* uid, uint8_t uid_length);
    void sendOpenPrintTagMessage(bool suppress_spoolman_sync = false);
    void sendBlankTagMessage();
    void sendGenericTagMessage();
    void sendTigerTagMessage(const TigerTagData& tt);
    void sendOpenTag3DMessage(const opentag3d_t& ot3d);
    void sendOpenSpoolMessage(const char* uid, const OpenSpoolData& data);
    void sendTagRemovedMessage();
    void processWriteQueue();
    // Fills the request's Pending outcome slot with the result. Shared by the
    // normal recording site and the tag-removed abandon path (#316 review).
    // Caller must hold writeOutcomeMutex_.
    void completeWriteOutcome(const NFCWriteRequest& request, bool success, const char* reason);
    bool executeWrite(const NFCWriteRequest& request);
    // outTimedOut, when given, distinguishes a tagMutex acquisition timeout
    // from a genuine UID mismatch — callers that report a short error string
    // to the user need to tell "tag removed" from "reader busy" (#316 review).
    bool validateWriteUid(const char* expectedUid, const char* writeType, bool* outTimedOut = nullptr);
    bool checkWriteCapacity(uint8_t startPage, uint8_t pageCount, const char* writeType);
    // Shared page-count ceiling for checkWriteCapacity() and the
    // classifyOpenTag3DTarget() blank-region scan, so their fallback for an
    // unidentified variant with no CC (#316) cannot drift apart.
    uint16_t writablePageLimit() const;
    bool executeTigerTagWrite(const NFCWriteRequest& request);
    bool verifyISO14443Readback(uint8_t startPage, uint8_t pageCount, const uint8_t* expected);
    bool tigertagVerifyAndFinish(const NFCWriteRequest& request, const char* how);
    bool ndefVerifyAndFinish(const char* what, uint8_t pagesNeeded, const uint8_t* ndefBuf, uint16_t ndefLen);
    bool executeOpenTag3DWrite(const NFCWriteRequest& request);
    // Records the short reason processWriteQueue copies into the outcome ring
    // on failure, for the writers that have no macro of their own.
    void setWriteError(const char* msg);
    enum class BlankScan : uint8_t { Blank, DataFound, Unreadable };
    // Reads pages 14..writablePageLimit(): the first window (pages 4-13) was
    // already judged empty by the caller. Unreadable when the tag's size was
    // not measured for the selected tag or a chunk read comes back short.
    BlankScan scanRestOfUserMemory();
    // window = pages 4-13 already read by the caller (keepSession true).
    Type2Content classifyType2Content(Type2Format writer, const uint8_t* window, uint16_t windowLen);
    // Decodes the merge baseline out of a page window already read from the
    // tag in session. Fails closed (returns false) on any NDEF, MIME, length
    // or decode problem; callers must never fall back to zeroed fields on
    // failure. Takes the window rather than reading it so the classification
    // above it and the baseline below it describe the same read of the tag.
    bool readOpenTag3DBaseline(const uint8_t* pageData, uint16_t bytesRead,
                               opentag3d_t& out, uint16_t& payloadLenOut,
                               bool* completeReadOut = nullptr);
    // What is physically on the tag, which is what decides merge vs replace.
    // Only OT3D_TARGET_WRITTEN fills `out`.
    ot3d_target_state_t classifyOpenTag3DTarget(opentag3d_t& out);
    bool executeOpenSpoolWrite(const NFCWriteRequest& request);
    bool executeAtomicWrite(const NFCWriteRequest& request);
    bool readBambuTag(const uint8_t* uid, uint8_t uidLength, BambuTagData& out);
    void sendBambuDetectedMessage();
    void forceRescan();
    void sendSpoolUpdatedMessage(uint32_t request_id, NFCWriteType type, bool success);

    // Deduplication
    void markRequestCompleted(uint32_t request_id);
    bool isDuplicateSpool(const uint8_t* uid, uint8_t uid_length);


    // State
    CurrentSpoolState currentSpool;
    uint8_t lastSeenUid[8];      // ISO15693 uses 8-byte UID
    uint8_t lastSeenUidLength = 0;
    bool lastSeenValid = false;
    unsigned long lastSeenMs = 0;            // millis() when last tag was detected
    static constexpr unsigned long SCAN_COOLDOWN_MS = 3000;  // suppress re-reads within 3s
    // UID hex of the tag selected on the reader during the current scan cycle.
    // Scan-task-owned (written in scanLoop/scanOnce, read by validateWriteUid,
    // which only ever runs below processWriteQueue on that same task), so it
    // needs no mutex — same ownership rule as lastSeenUid. Unlike
    // currentSpool.spool_id this is refreshed even when the cooldown dedup
    // skips handleNewTag(), which is the whole point of #276.
    char selectedUidHex_[17] = {0};
    // UID hex of the tag currentSpool.variant/cc_user_end were measured for.
    // Set only where those two are assigned from a fresh scan, cleared
    // everywhere they're reset to Unknown/0 — checkWriteCapacity() and
    // classifyOpenTag3DTarget() must refuse to trust a size that wasn't
    // measured for the tag now selected (#316).
    char capacityUidHex_[17] = {0};
    uint8_t absentMisses_ = 0;               // consecutive failed detects while a tag was present
    static constexpr uint8_t TAG_ABSENT_MISS_THRESHOLD = 3;  // ~150ms at 50ms poll before declaring removal

    // Recent spools history (RAM only, most recent first)
    RecentSpoolEntry recentSpools[MAX_RECENT_SPOOLS];
    size_t recentSpoolsCount = 0;
    void addToRecentSpoolsLocked();  // caller must hold tagMutex

    // Last parsed TigerTag data (retained for /api/status)
    TigerTagData lastTigerTag_;
    bool lastTigerTagValid_ = false;

    // Last parsed OpenTag3D data (retained for /api/status)
    opentag3d_t lastOpenTag3D_;
    bool lastOpenTag3DValid_ = false;
    uint16_t lastOpenTag3DPayloadLen_ = 0;  // declared record length behind lastOpenTag3D_
    OpenSpoolData lastOpenSpool_;
    bool lastOpenSpoolValid_ = false;
    BambuTagData lastBambuTag_;
    bool lastBambuTagValid_ = false;

    // Resolved Spoolman data for the current generic UID tag
    GenericTagSpoolInfo lastGenericTagSpoolInfo_ = {};

    // Raw tag write sidecar buffer (filled by HTTP context, consumed by scan task)
    static constexpr size_t RAW_WRITE_BUFFER_SIZE = 320;
    static_assert(sizeof(ot3d_patch_t) <= RAW_WRITE_BUFFER_SIZE,
                  "OpenTag3D patch envelope exceeds the raw write staging buffer");
    // The slot stays claimed until the write it feeds has finished, not just
    // until its bytes have been copied out. A producer that staged against
    // state an in-flight write is about to change would overwrite that write's
    // result with values read before it ran, and the readback would verify the
    // stale data as success — so producers are refused while InFlight.
    enum class RawWriteSlot : uint8_t { Empty = 0, Staged, InFlight };
    uint8_t rawWriteBuffer_[RAW_WRITE_BUFFER_SIZE];
    size_t rawWriteBufferSize_ = 0;
    RawWriteSlot rawWriteSlot_ = RawWriteSlot::Empty;
    // Copies a staged payload out and marks the slot InFlight. failReason gets
    // a short string for the outcome ring on failure.
    bool claimStagedRawWrite(uint8_t* out, size_t outSize, size_t& outLen,
                             const char* writeType, const char** failReason);
    void releaseRawWriteSlot();
    // Returns the slot to Empty however the write ends. Every consumer arms one
    // immediately after a successful claim: the two slot-stranding bugs in this
    // area both came from clears placed by hand on individual exit paths.
    struct RawWriteSlotGuard {
        NFCManager& mgr;
        ~RawWriteSlotGuard() { mgr.releaseRawWriteSlot(); }
    };
    bool writeRawTag(const NFCWriteRequest& request);
    opt_tag_t writeScratchTag_;   // Reused by scan task write path to avoid large stack frames
    AtomicWriteFields atomicWriteFields_;  // Sidecar for WRITE_ATOMIC (filled by HTTP, consumed by scan task)

    // Write queue (FreeRTOS)
    QueueHandle_t writeQueue = nullptr;
    SemaphoreHandle_t tagMutex = nullptr;
    SemaphoreHandle_t stagingMutex = nullptr;   // guards rawWriteBuffer_/Size_/Slot_

    // Completed request tracking (circular buffer)
    static constexpr size_t COMPLETED_REQUESTS_SIZE = 32;
    uint32_t completedRequests[COMPLETED_REQUESTS_SIZE];
    size_t completedRequestsIndex = 0;
    SemaphoreHandle_t completedMutex = nullptr;
    static std::atomic<uint32_t> s_write_request_id_counter;

    // One slot per write-queue entry (xQueueCreate(8, ...)), and outcomes stay
    // protected for longer than the browser's 15 s polling deadline.
    static constexpr size_t   WRITE_OUTCOME_RING_SIZE = 8;
    static constexpr uint32_t WRITE_OUTCOME_TTL_MS = 20000;
    // Shorter than the browser's 15 s poll, so the page hears "tag removed"
    // instead of timing out.
    static constexpr uint32_t WRITE_ABANDON_MS = 10000;
    WriteOutcome writeOutcomeRing_[WRITE_OUTCOME_RING_SIZE];
    SemaphoreHandle_t writeOutcomeMutex_ = nullptr;
    uint32_t writeOutcomeLastIndex_ = 0;
    // Scan-task scratch: failure reason set by executeOpenTag3DWrite before
    // each `return false`, consumed into the ring once the write queue
    // consumer has the executor's result. Cleared before each request.
    char lastWriteErrorReason_[48] = {0};

    // Task handle
    TaskHandle_t scanTaskHandle = nullptr;

    // NFC watchdog: recovery after consecutive failures
    static constexpr uint32_t RECOVERY_THRESHOLD = 600;   // ~30s at 50ms/scan
    static constexpr uint32_t RESTART_THRESHOLD = 1200;    // ~60s at 50ms/scan
    static constexpr uint32_t NFC_WDT_TIMEOUT_S = 30;     // Task watchdog timeout
    uint32_t consecutiveFailures_ = 0;
    void attemptRecovery();
    void reportWdtPhaseIfCrashed();  // wdt forensics: print pre-reset scan phase

    // Write batch tracking: prevents tag re-reads during batched writes
    volatile bool suppressReDetection_ = false;
    char suppressReDetectionUid_[17] = {0};
    volatile bool batchHadSuppressSync_ = false;  // Track if any write in batch had suppress_sync
};

#endif // NFC_MANAGER_H
