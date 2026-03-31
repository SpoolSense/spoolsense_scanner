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
#include "opentag3d_lib.h"

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
    void requestCurrentSpool();                      // Clear dedup to resend current spool
    bool scanOnce();                                 // Single scan cycle (for testing)
    bool getCurrentSpoolState(CurrentSpoolState& out);
    bool getLastTigerTagData(TigerTagData& out);
    bool getLastOpenTag3DData(opentag3d_t& out);
    // Returns reader identification string (e.g. "PN5180 v3.4", "PN532 v1.6")
    bool getNfcReaderInfo(char* buf, size_t len) const;
    void pauseScanTask();
    void resumeScanTask();

    // Dependency injection for testing
    void setConnection(NFCConnectionI* conn) { connection_ = conn; }
    void resetWriteState() {
        rawWritePending_ = false;
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

    // Scan task
    static void scanTaskFunc(void* param);
    void scanLoop();

    // Internal operations
    bool readAndParseTag(uint8_t* uid, uint8_t uid_length);
    bool formatNewSpool();
    TagScanResult classifyTag(const uint8_t* uid, uint8_t uid_length);
    void sendSpoolDetectedMessage(bool suppress_spoolman_sync = false);
    void sendBlankTagMessage();
    void sendGenericTagMessage();
    void sendTigerTagMessage(const TigerTagData& tt);
    void sendOpenTag3DMessage(const opentag3d_t& ot3d);
    void sendTagRemovedMessage();
    void processWriteQueue();
    bool executeWrite(const NFCWriteRequest& request);
    void sendSpoolUpdatedMessage(uint32_t request_id, NFCWriteType type, bool success);

    // Deduplication
    void markRequestCompleted(uint32_t request_id);
    bool isDuplicateSpool(const uint8_t* uid, uint8_t uid_length);
    uint32_t generateRequestId();

    // State
    CurrentSpoolState currentSpool;
    uint8_t lastSeenUid[8];      // ISO15693 uses 8-byte UID
    uint8_t lastSeenUidLength = 0;
    bool lastSeenValid = false;

    // Recent spools history (RAM only, most recent first)
    RecentSpoolEntry recentSpools[MAX_RECENT_SPOOLS];
    size_t recentSpoolsCount = 0;
    void addToRecentSpools();

    // Last parsed TigerTag data (retained for /api/status)
    TigerTagData lastTigerTag_;
    bool lastTigerTagValid_ = false;

    // Last parsed OpenTag3D data (retained for /api/status)
    opentag3d_t lastOpenTag3D_;
    bool lastOpenTag3DValid_ = false;

    // Resolved Spoolman data for the current generic UID tag
    GenericTagSpoolInfo lastGenericTagSpoolInfo_ = {};

    // Raw tag write sidecar buffer (filled by HTTP context, consumed by scan task)
    static constexpr size_t RAW_WRITE_BUFFER_SIZE = 320;
    uint8_t rawWriteBuffer_[RAW_WRITE_BUFFER_SIZE];
    size_t rawWriteBufferSize_ = 0;
    bool rawWritePending_ = false;
    bool writeRawTag();
    opt_tag_t writeScratchTag_;   // Reused by scan task write path to avoid large stack frames
    AtomicWriteFields atomicWriteFields_;  // Sidecar for WRITE_ATOMIC (filled by HTTP, consumed by scan task)

    // Write queue (FreeRTOS)
    QueueHandle_t writeQueue = nullptr;
    SemaphoreHandle_t tagMutex = nullptr;

    // Completed request tracking (circular buffer)
    static constexpr size_t COMPLETED_REQUESTS_SIZE = 32;
    uint32_t completedRequests[COMPLETED_REQUESTS_SIZE];
    size_t completedRequestsIndex = 0;
    SemaphoreHandle_t completedMutex = nullptr;
    static uint32_t s_write_request_id_counter;

    // Task handle
    TaskHandle_t scanTaskHandle = nullptr;

    // NFC watchdog: recovery after consecutive failures
    static constexpr uint32_t RECOVERY_THRESHOLD = 600;   // ~30s at 50ms/scan
    static constexpr uint32_t RESTART_THRESHOLD = 1200;    // ~60s at 50ms/scan
    static constexpr uint32_t NFC_WDT_TIMEOUT_S = 30;     // Task watchdog timeout
    uint32_t consecutiveFailures_ = 0;
    void attemptRecovery();

    // Write batch tracking: prevents tag re-reads during batched writes
    volatile bool suppressReDetection_ = false;
    char suppressReDetectionUid_[17] = {0};
    volatile bool batchHadSuppressSync_ = false;  // Track if any write in batch had suppress_sync
};

#endif // NFC_MANAGER_H
