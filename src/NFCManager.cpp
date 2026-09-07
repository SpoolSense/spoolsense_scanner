#include "NFCManager.h"
#include "opentag3d_v2_map.h"
#include <esp_system.h>
#include "ConversionUtils.h"
#include "WriteUidGuard.h"
#include "TigerTagParser.h"
#include "OpenSpoolParser.h"
#include "BambuKeyDeriver.h"
#include "BambuTagParser.h"
#ifndef NATIVE_TEST
  #include "ApplicationManager.h"
  #include "HardwareNFCConnection.h"
  #include "SpoolmanManager.h"
  #include "LogBuffer.h"
  #include "MemoryDiagnostics.h"
  #include "TaskUtils.h"
  #include <Arduino.h>
  #include <esp_arduino_version.h>
#else
  #include "platform/NativePlatform.h"
  #include "FakeLCDManager.h"
  #include "StubApplicationManager.h"
#endif
#include <cstring>
#include <time.h>


//#define DUMP_TAGS_TO_CONSOLE

#ifdef DUMP_TAGS_TO_CONSOLE
    #include <base64.hpp>
#endif

// Static member initialization
uint32_t NFCManager::s_write_request_id_counter = 1000;

NFCManager& NFCManager::getInstance() {
    static NFCManager instance;
    return instance;
}

bool NFCManager::begin() {
    // Create hardware connection if none was injected
    if (connection_ == nullptr) {
#ifndef NATIVE_TEST
        connection_ = new HardwareNFCConnection();
        ownsConnection_ = true;
#else
        // In native tests, connection must be injected via setConnection()
        Serial.println("NFCManager: No connection injected for native test");
        return false;
#endif
    }

    if (!connection_->begin()) {
        Serial.println("NFCManager: Failed to initialize connection");
        return false;
    }

    // Create FreeRTOS primitives
    writeQueue = xQueueCreate(8, sizeof(NFCWriteRequest));
    if (writeQueue == nullptr) {
        Serial.println("NFCManager: Failed to create write queue");
        return false;
    }

    tagMutex = xSemaphoreCreateMutex();
    if (tagMutex == nullptr) {
        Serial.println("NFCManager: Failed to create tag mutex");
        return false;
    }

    completedMutex = xSemaphoreCreateMutex();
    if (completedMutex == nullptr) {
        Serial.println("NFCManager: Failed to create completed mutex");
        return false;
    }

    // Initialize state
    memset(&currentSpool, 0, sizeof(currentSpool));
    memset(lastSeenUid, 0, sizeof(lastSeenUid));
    lastSeenUidLength = 0;
    lastSeenValid = false;
    memset(completedRequests, 0, sizeof(completedRequests));
    completedRequestsIndex = 0;
    memset(recentSpools, 0, sizeof(recentSpools));
    recentSpoolsCount = 0;
    suppressReDetection_ = false;
    suppressReDetectionUid_[0] = '\0';

    Serial.println("NFCManager: Initialized successfully");
    return true;
}

bool NFCManager::getCurrentSpoolState(CurrentSpoolState& out) {
    if (tagMutex == nullptr) {
        return false;
    }
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }
    out = currentSpool;
    xSemaphoreGive(tagMutex);
    return true;
}

bool NFCManager::getLastTigerTagData(TigerTagData& out) {
    if (tagMutex == nullptr) return false;
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    bool valid = lastTigerTagValid_;
    if (valid) out = lastTigerTag_;
    xSemaphoreGive(tagMutex);
    return valid;
}

bool NFCManager::getLastOpenTag3DData(opentag3d_t& out) {
    if (tagMutex == nullptr) return false;
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    bool valid = lastOpenTag3DValid_;
    if (valid) out = lastOpenTag3D_;
    xSemaphoreGive(tagMutex);
    return valid;
}

bool NFCManager::getLastOpenSpoolData(OpenSpoolData& out) {
    if (tagMutex == nullptr) return false;
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    bool valid = lastOpenSpoolValid_;
    if (valid) out = lastOpenSpool_;
    xSemaphoreGive(tagMutex);
    return valid;
}

bool NFCManager::getLastBambuTagData(BambuTagData& out) {
    if (tagMutex == nullptr) return false;
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    bool valid = lastBambuTagValid_;
    if (valid) out = lastBambuTag_;
    xSemaphoreGive(tagMutex);
    return valid;
}

void NFCManager::setGenericTagSpoolInfo(const GenericTagSpoolInfo& info) {
    if (tagMutex && xSemaphoreTake(tagMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        lastGenericTagSpoolInfo_ = info;
        xSemaphoreGive(tagMutex);
    }
}

void NFCManager::getGenericTagSpoolInfo(GenericTagSpoolInfo& out) {
    if (tagMutex && xSemaphoreTake(tagMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        out = lastGenericTagSpoolInfo_;
        xSemaphoreGive(tagMutex);
    } else {
        out = {};
    }
}

void NFCManager::clearGenericTagSpoolInfo() {
    if (tagMutex && xSemaphoreTake(tagMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        lastGenericTagSpoolInfo_ = {};
        xSemaphoreGive(tagMutex);
    }
}

bool NFCManager::getNfcReaderInfo(char* buf, size_t len) const {
    if (!connection_ || !buf || len == 0) return false;
    connection_->getReaderInfo(buf, len);
    return true;
}

void NFCManager::pauseScanTask() {
#ifndef NATIVE_TEST
    if (scanTaskHandle) {
        // The scan task is the only TWDT subscriber in this firmware (the
        // idle tasks are unsubscribed — sdkconfig on C5/C6, and our
        // reconfigure sets idle_core_mask=0 elsewhere). Suspending it without
        // unsubscribing left its watchdog entry aging, so anything that held
        // the pause longer than the 30s window — an OTA upload from a slow
        // client, in practice — got a deterministic task_wdt panic mid-flash
        // (#280). Unsubscribe before suspending; ESP_ERR_NOT_FOUND (already
        // unsubscribed) is fine.
        esp_task_wdt_delete(scanTaskHandle);
        vTaskSuspend(scanTaskHandle);
        Serial.println("NFCManager: Scan task paused");
    }
#endif
}

void NFCManager::resumeScanTask() {
#ifndef NATIVE_TEST
    if (scanTaskHandle) {
        vTaskResume(scanTaskHandle);
        // Re-subscribe the resumed task to the TWDT (see pauseScanTask). Its
        // entry restarts from now; the scan loop feeds it every iteration.
        esp_task_wdt_add(scanTaskHandle);
        Serial.println("NFCManager: Scan task resumed");
    }
#endif
}

bool NFCManager::waitForScanPaused(uint32_t timeoutMs) {
#ifndef NATIVE_TEST
    // If the scan task never started, there is nothing to pause — treat the
    // reader as immediately available to the caller.
    if (!scanTaskHandle) return true;
    uint32_t waited = 0;
    while (!scanPaused_ && waited < timeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }
    return scanPaused_;
#else
    return true;
#endif
}

void NFCManager::startScanTask() {
    BaseType_t created = createTaskWithAffinity(
        scanTaskFunc,
        "NFCScanTask",
        8192,
        this,
        1,
        &scanTaskHandle,
        1  // Run on core 1
    );
    if (created != pdPASS) {
        scanTaskHandle = nullptr;
        Serial.println("NFCManager: ERROR — scan task creation failed");
        return;
    }
    Serial.println("NFCManager: Scan task started");
    reportWdtPhaseIfCrashed();
}

void NFCManager::scanTaskFunc(void* param) {
    NFCManager* self = static_cast<NFCManager*>(param);
    self->scanLoop();
}

// ── Watchdog forensics ──────────────────────────────────────
// Where exactly is the scan task when the 30s task watchdog kills it?
// RTC-noinit survives the wdt reset, so the next boot reports the phase.
RTC_NOINIT_ATTR volatile uint32_t g_nfcScanPhase;
RTC_NOINIT_ATTR volatile uint32_t g_nfcScanPhasePage;
#define SCAN_PHASE(n) (g_nfcScanPhase = (n))
#define SCAN_PHASE_PAGE(p) (g_nfcScanPhasePage = (p))

void NFCManager::reportWdtPhaseIfCrashed() {
    if (esp_reset_reason() == ESP_RST_TASK_WDT) {
        Serial.printf("NFCManager: PREVIOUS BOOT DIED IN SCAN PHASE %lu (page marker %lu)\n",
                      (unsigned long)g_nfcScanPhase, (unsigned long)g_nfcScanPhasePage);
        LogBuffer::getInstance().logPrintf("WDT death: scan phase %lu page %lu\n",
                      (unsigned long)g_nfcScanPhase, (unsigned long)g_nfcScanPhasePage);
    }
    g_nfcScanPhase = 0;
    g_nfcScanPhasePage = 0;
}

// ── NDEF helpers ────────────────────────────────────────────

struct NdefRecord {
    const char* mimeType;    // pointer into pageData (not owned)
    uint8_t mimeLen;
    uint32_t payloadLen;
    uint16_t payloadOffset;  // offset from start of pageData
    bool found;
};

// Scan TLV records in pageData for an NDEF media-type record.
// Returns the first NDEF record found with TNF=media-type (0x02).
static NdefRecord findNdefMediaRecord(const uint8_t* pageData, uint16_t bytesRead) {
    NdefRecord rec = {};
    uint16_t pos = 0;
    while (pos < bytesRead) {
        uint8_t tlvType = pageData[pos++];
        if (tlvType == 0x00) continue;
        if (tlvType == 0xFE) break;
        if (pos >= bytesRead) break;

        uint16_t tlvLen = pageData[pos++];
        if (tlvLen == 0xFF) {
            if (pos + 2 > bytesRead) break;
            tlvLen = (uint16_t)(pageData[pos] << 8) | pageData[pos + 1];
            pos += 2;
        }

        if (tlvType != 0x03) { pos += tlvLen; continue; }

        uint16_t ndefStart = pos;
        if (ndefStart >= bytesRead) break;

        uint8_t ndefFlags = pageData[ndefStart];
        uint8_t tnf = ndefFlags & 0x07;
        if (tnf != 0x02 || ndefStart + 1 >= bytesRead) break;

        uint8_t typeLen = pageData[ndefStart + 1];
        bool sr = (ndefFlags & 0x10) != 0;
        uint16_t headerSize = 2 + (sr ? 1 : 4);
        if (ndefStart + headerSize + typeLen > bytesRead) break;

        uint32_t payloadLen = 0;
        if (sr) {
            payloadLen = pageData[ndefStart + 2];
        } else {
            payloadLen = ((uint32_t)pageData[ndefStart + 2] << 24) |
                         ((uint32_t)pageData[ndefStart + 3] << 16) |
                         ((uint32_t)pageData[ndefStart + 4] << 8) |
                         pageData[ndefStart + 5];
        }

        rec.mimeType = (const char*)(pageData + ndefStart + headerSize);
        rec.mimeLen = typeLen;
        rec.payloadLen = payloadLen;
        rec.payloadOffset = ndefStart + headerSize + typeLen;
        rec.found = true;
        break;
    }
    return rec;
}

// Read NDEF payload bytes, doing an extended page read if the initial 40-byte read
// didn't contain the full payload. Returns bytes copied into outBuf.
uint16_t NFCManager::readNdefPayload(const NdefRecord& rec, const uint8_t* pageData, uint16_t bytesRead,
                                      uint8_t* outBuf, uint16_t outBufSize, uint16_t maxPages) {
    if (!rec.found || rec.payloadLen == 0) return 0;

    uint16_t available = (rec.payloadOffset < bytesRead) ? bytesRead - rec.payloadOffset : 0;
    uint16_t needed = (rec.payloadLen > outBufSize) ? outBufSize : (uint16_t)rec.payloadLen;

    if (available >= rec.payloadLen) {
        memcpy(outBuf, pageData + rec.payloadOffset, needed);
        return needed;
    }

    // Extended read — payload spans beyond the initial 40-byte read
    uint8_t startPage = 4 + (rec.payloadOffset / 4);
    uint16_t pagesNeeded = (uint16_t)((rec.payloadLen + 3) / 4) + 1;
    if (pagesNeeded > 64) pagesNeeded = 64;  /* extBuf holds 256 B = 64 pages */
    // Never request past the tag's last usable page: NTAG READ rolls over to
    // page 0 beyond the end, which would silently corrupt the tail of an
    // over-asked payload (garbage payloadLen on a malformed tag)
    if (maxPages == 0) maxPages = 68;  // unknown variant: a v2 payload spans pages 4..66,
                                       // so 64 (the historical ceiling) truncated every v2
                                       // read after a failed GET_VERSION; 68 covers v2 and
                                       // extBuf still bounds one read at 64 pages
    if (startPage >= maxPages) return 0;
    if ((uint16_t)startPage + pagesNeeded > maxPages) {
        pagesNeeded = maxPages - startPage;
    }

    uint8_t extBuf[256] = {0};
    uint16_t extRead = connection_->readISO14443Pages(startPage, (uint8_t)pagesNeeded, extBuf, sizeof(extBuf));
    uint16_t offsetInPage = rec.payloadOffset % 4;
    if (extRead <= offsetInPage) return 0;

    uint16_t payloadBytes = extRead - offsetInPage;
    if (payloadBytes > rec.payloadLen) payloadBytes = (uint16_t)rec.payloadLen;
    if (payloadBytes > outBufSize) payloadBytes = outBufSize;
    memcpy(outBuf, extBuf + offsetInPage, payloadBytes);
    return payloadBytes;
}

// ── ISO14443A tag read + classification ─────────────────────
// Reads pages 4-13, tries TigerTag → OpenTag3D → OpenSpool → GenericUID.
// Updates currentSpool state and sends the appropriate message.

void NFCManager::readAndProcessISO14443Tag(const uint8_t* uid, uint8_t uidLength, const TagScanResult& scan) {
    bool isTigerTag = false;
    bool isOpenTag3D = false;
    bool isOpenSpool = false;
    TigerTagData tigerData;
    OpenSpoolData openSpoolData;
    opentag3d_t ot3dData;
    memset(&tigerData, 0, sizeof(tigerData));
    memset(&ot3dData, 0, sizeof(ot3dData));

    uint8_t pageData[40] = {0};
    SCAN_PHASE(20);
    uint16_t bytesRead = connection_->readISO14443Pages(4, 10, pageData, sizeof(pageData), true);
    SCAN_PHASE(21);

    // Try TigerTag first (binary magic at offset 0)
    if (bytesRead >= 14 && tigerTagCheckMagic(pageData, bytesRead)) {
        if (bytesRead >= 38) {
            tigerData = tigerTagParse(pageData, bytesRead);
            isTigerTag = tigerData.valid;
        } else {
            Serial.printf("NFCManager: TigerTag magic matched but only got %d bytes (need 38)\n", bytesRead);
        }
    }

    // Scan NDEF TLV for OpenTag3D or OpenSpool
    if (!isTigerTag && bytesRead >= 4) {
        NdefRecord rec = findNdefMediaRecord(pageData, bytesRead);
        if (rec.found) {
            const char* ot3dMime = OT3D_MIME_TYPE;
            if (rec.mimeLen == strlen(ot3dMime) && memcmp(rec.mimeType, ot3dMime, rec.mimeLen) == 0) {
                uint8_t payload[OT3D_V2_MAP_SIZE];
                SCAN_PHASE(22);
                uint16_t payloadBytes = readNdefPayload(rec, pageData, bytesRead, payload, sizeof(payload),
                                                        effectiveUserMemoryEnd(scan.variant, scan.cc_user_end));
                if (payloadBytes >= OT3D_CORE_SIZE) {
                    opentag3d_result_t res = opentag3d_decode(payload, payloadBytes, &ot3dData);
                    if (res == OT3D_OK || res == OT3D_VERSION_WARNING) {
                        isOpenTag3D = true;
                        if (res == OT3D_VERSION_WARNING) {
                            uint16_t known = (opentag3d_major(ot3dData.tag_version) >= 2)
                                             ? OT3D_SUPPORTED_V2 : OT3D_SUPPORTED_V1;
                            Serial.printf("NFCManager: OpenTag3D tag version %u ahead of supported %u — parsing anyway\n",
                                          ot3dData.tag_version, known);
                        }
                    } else if (res == OT3D_VERSION_ERROR) {
                        Serial.printf("NFCManager: OpenTag3D major version too new (%u) — cannot parse\n",
                                      ot3dData.tag_version);
                    }
                }
            }

            if (!isOpenTag3D) {
                const char* jsonMime = "application/json";
                if (rec.mimeLen == strlen(jsonMime) && memcmp(rec.mimeType, jsonMime, rec.mimeLen) == 0) {
                    uint8_t payload[256];
                    SCAN_PHASE(23);
                    uint16_t payloadBytes = readNdefPayload(rec, pageData, bytesRead, payload, sizeof(payload),
                                                            effectiveUserMemoryEnd(scan.variant, scan.cc_user_end));
                    if (payloadBytes > 0 && parseOpenSpool(payload, payloadBytes, openSpoolData)) {
                        isOpenSpool = true;
                    }
                }
            }
        }
    }

    // All reads done — halt tag if session is still active
    SCAN_PHASE(24);
    connection_->endTagSession();
    SCAN_PHASE(25);

    // Update shared state under mutex
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(currentSpool.spool_id, scan.uid_hex, sizeof(scan.uid_hex));
        memcpy(currentSpool.uid, uid, uidLength);
        currentSpool.uid_length = uidLength;
        currentSpool.present = true;
        currentSpool.blank_tag_present = false;
        currentSpool.variant = scan.variant;
        currentSpool.cc_user_end = scan.cc_user_end;
        memcpy(lastSeenUid, uid, uidLength);
        lastSeenUidLength = uidLength;
        lastSeenValid = true;
        lastSeenMs = millis();

        if (isTigerTag) {
            currentSpool.kind = TagKind::TigerTag;
            currentSpool.tag_data_valid = false;
            lastTigerTag_ = tigerData;
            lastTigerTagValid_ = true;
            lastOpenTag3DValid_ = false;
            Serial.printf("NFCManager: TigerTag detected — %s %s %s\n",
                          tigerData.brand_name, tigerData.material_name, tigerData.aspect1_name);
            LogBuffer::getInstance().logPrintf("TigerTag: %s %s %s\n",
                          tigerData.brand_name, tigerData.material_name, tigerData.aspect1_name);
        } else if (isOpenTag3D) {
            currentSpool.kind = TagKind::OpenTag3D;
            currentSpool.tag_data_valid = false;
            lastOpenTag3D_ = ot3dData;
            lastOpenTag3DValid_ = true;
            lastTigerTagValid_ = false;
            lastOpenSpoolValid_ = false;
            Serial.printf("NFCManager: OpenTag3D detected — %s %s %.2fmm %ug\n",
                          ot3dData.manufacturer, ot3dData.base_material,
                          opentag3d_diameter_mm(&ot3dData), ot3dData.target_weight_g);
            LogBuffer::getInstance().logPrintf("OpenTag3D: %s %s %.2fmm %ug\n",
                          ot3dData.manufacturer, ot3dData.base_material,
                          opentag3d_diameter_mm(&ot3dData), ot3dData.target_weight_g);
        } else if (isOpenSpool) {
            currentSpool.kind = TagKind::OpenSpoolTag;
            currentSpool.tag_data_valid = false;
            lastOpenSpool_ = openSpoolData;
            lastOpenSpoolValid_ = true;
            lastTigerTagValid_ = false;
            lastOpenTag3DValid_ = false;
            Serial.printf("NFCManager: OpenSpool detected — %s %s #%s\n",
                          openSpoolData.brand, openSpoolData.material, openSpoolData.color_hex);
            LogBuffer::getInstance().logPrintf("OpenSpool: %s %s #%s\n",
                          openSpoolData.brand, openSpoolData.material, openSpoolData.color_hex);
        } else {
            currentSpool.kind = TagKind::GenericUidTag;
            currentSpool.tag_data_valid = false;
            lastTigerTagValid_ = false;
            lastOpenTag3DValid_ = false;
            lastOpenSpoolValid_ = false;
        }
        xSemaphoreGive(tagMutex);
    } else {
        Serial.println("NFCManager: Could not acquire tagMutex");
    }

    // Send format-specific message
    if (isTigerTag) {
        sendTigerTagMessage(tigerData);
    } else if (isOpenTag3D) {
        sendOpenTag3DMessage(ot3dData);
    } else if (isOpenSpool) {
        sendOpenSpoolMessage(currentSpool.spool_id, openSpoolData);
    } else {
        sendGenericTagMessage();
    }
}

// ── Scan loop helpers ───────────────────────────────────────

bool NFCManager::prepareRF() {
    if (consecutiveFailures_ >= RESTART_THRESHOLD) {
        Serial.println("NFCManager: CRITICAL - too many consecutive failures, restarting ESP");
#ifndef NATIVE_TEST
        delay(100);
        ESP.restart();
#endif
    }
    if (consecutiveFailures_ > 0 && (consecutiveFailures_ % RECOVERY_THRESHOLD) == 0) {
        attemptRecovery();
    }

    // Full hardware reset only when no tag present — reset cuts RF, de-powering passive tags
    if (!lastSeenValid) {
        connection_->reset();
    }
    if (!connection_->setupRF()) {
        consecutiveFailures_++;
        Serial.printf("NFCManager: setupRF() failed (consecutive=%lu, lastSeenValid=%d)\n",
                      consecutiveFailures_, lastSeenValid ? 1 : 0);
        if (lastSeenValid) {
            Serial.println("NFCManager: clearing lastSeenValid to force hardware reset");
            lastSeenValid = false;
        }
        return false;
    }

    // Give tag time to power up after RF field reset
    if (!lastSeenValid) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    consecutiveFailures_ = 0;
    return true;
}

// Skip re-reading a tag we already processed, or one being actively written to
bool NFCManager::isSkippableDuplicate(const uint8_t* uid, uint8_t uidLength) {
    if (isDuplicateSpool(uid, uidLength)) return true;  // same tag as last scan cycle

    if (!suppressReDetection_) return false;  // no active write batch

    char uidHex[17];
    for (uint8_t i = 0; i < uidLength && i < 8; i++) {
        sprintf(uidHex + (i * 2), "%02X", uid[i]);
    }
    uidHex[uidLength * 2] = '\0';
    return strcmp(uidHex, suppressReDetectionUid_) == 0;
}

void NFCManager::recordSelectedUid(const uint8_t* uid, uint8_t uidLength) {
    if (uidLength > 8) uidLength = 8;   // selectedUidHex_ holds 8 bytes max
    for (uint8_t i = 0; i < uidLength; i++) {
        snprintf(selectedUidHex_ + (i * 2), 3, "%02X", uid[i]);
    }
    selectedUidHex_[uidLength * 2] = '\0';
}

void NFCManager::clearSelectedUid() {
    selectedUidHex_[0] = '\0';
}

void NFCManager::handleNewTag(uint8_t* uid, uint8_t uidLength) {
    Serial.println("NFCManager: New spool detected, reading tag...");
    SCAN_PHASE(10);
    TagScanResult scan = classifyTag(uid, uidLength);

    if (scan.kind == TagKind::BambuTag) {
        SCAN_PHASE(11);
        BambuTagData bambuData;
        bool readOk = readBambuTag(uid, uidLength, bambuData);

        if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            memcpy(currentSpool.spool_id, scan.uid_hex, sizeof(scan.uid_hex));
            memcpy(currentSpool.uid, uid, uidLength);
            currentSpool.uid_length = uidLength;
            currentSpool.present = true;
            currentSpool.blank_tag_present = false;
            currentSpool.kind = TagKind::BambuTag;
            currentSpool.variant = NtagVariant::Unknown;
            currentSpool.cc_user_end = 0;   // MIFARE Classic — no Type 2 CC
            currentSpool.tag_data_valid = readOk;
            lastBambuTagValid_ = readOk;
            if (readOk) lastBambuTag_ = bambuData;
            lastTigerTagValid_ = false;
            lastOpenTag3DValid_ = false;
            lastOpenSpoolValid_ = false;
            memcpy(lastSeenUid, uid, uidLength);
            lastSeenUidLength = uidLength;
            lastSeenValid = true;
            lastSeenMs = millis();
            xSemaphoreGive(tagMutex);
        }

        if (readOk) {
            Serial.printf("NFCManager: Bambu tag — %s, %ug, #%02X%02X%02X\n",
                          bambuData.filament_type, bambuData.weight_g,
                          bambuData.color_r, bambuData.color_g, bambuData.color_b);
            LogBuffer::getInstance().logPrintf("Bambu: %s %ug\n",
                          bambuData.filament_type, bambuData.weight_g);
            sendBambuDetectedMessage();
        } else {
            Serial.printf("NFCManager: Bambu tag — UID=%s (auth failed, no data)\n", scan.uid_hex);
            LogBuffer::getInstance().logPrintf("Bambu tag: %s (no data)\n", scan.uid_hex);
            sendGenericTagMessage();
        }
        return;
    }

    // ISO14443A: TigerTag, OpenTag3D, OpenSpool, or generic UID
    if (scan.kind == TagKind::GenericUidTag) {
        SCAN_PHASE(12);
        readAndProcessISO14443Tag(uid, uidLength, scan);
        return;
    }

    // ISO15693 (ICODE SLIX2): OpenPrintTag with retries — RF can be flaky at range
    bool readOk = false;
    for (int attempt = 0; attempt < 3 && !readOk; attempt++) {
        if (attempt > 0) {
            Serial.printf("NFCManager: Read attempt %d — resetting RF...\n", attempt + 1);
            connection_->reset();
            bool rfOk = connection_->setupRF();
            Serial.printf("NFCManager: setupRF after reset: %s\n", rfOk ? "OK" : "FAILED");
            vTaskDelay(pdMS_TO_TICKS(100));  // let RF stabilize after reset
        }
        Serial.printf("NFCManager: readAndParseTag attempt %d\n", attempt + 1);
        SCAN_PHASE(13);
        readOk = readAndParseTag(uid, uidLength);
        Serial.printf("NFCManager: readAndParseTag attempt %d: %s\n", attempt + 1, readOk ? "OK" : "FAILED");
    }

    if (readOk) return;

    // All retries failed — tag is present but unreadable, show as blank so web UI can format it
    Serial.println("NFCManager: readAndParseTag() failed after retries - treating as blank tag");
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (uint8_t i = 0; i < uidLength && i < 8; i++) {
            sprintf(currentSpool.spool_id + (i * 2), "%02X", uid[i]);
        }
        currentSpool.spool_id[uidLength * 2] = '\0';
        memcpy(currentSpool.uid, uid, uidLength);
        currentSpool.uid_length = uidLength;
        currentSpool.present = true;
        currentSpool.tag_data_valid = false;
        currentSpool.blank_tag_present = true;
        currentSpool.kind = TagKind::BlankTag;
        memcpy(lastSeenUid, uid, uidLength);
        lastSeenUidLength = uidLength;
        lastSeenValid = true;
        lastSeenMs = millis();
        xSemaphoreGive(tagMutex);
        sendBlankTagMessage();
    }
}

// Tag left the field — clear state and notify the system
void NFCManager::handleTagAbsent() {
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    if (lastSeenValid) {  // only send removal if we had a tag
        Serial.println("NFCManager: Tag removed");
        LogBuffer::getInstance().logPrintf("Tag removed: %s\n", currentSpool.spool_id);
        sendTagRemovedMessage();
    }
    currentSpool.present = false;
    currentSpool.blank_tag_present = false;
    currentSpool.variant = NtagVariant::Unknown;  // don't let the removed tag's identity
    currentSpool.cc_user_end = 0;                 // answer size checks for the next one
    lastSeenValid = false;
    lastTigerTagValid_ = false;
    lastOpenTag3DValid_ = false;
    suppressReDetection_ = false;       // allow fresh detection on next tag
    suppressReDetectionUid_[0] = '\0';
    clearSelectedUid();                 // no tag selected — bound writes fail closed
    xSemaphoreGive(tagMutex);
}

// ── Main scan loop ──────────────────────────────────────────

void NFCManager::scanLoop() {
    Serial.println("NFCManager: scanLoop() started, polling every 50ms");

#ifndef NATIVE_TEST
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    // Arduino initializes the TWDT before setup(). Its sdkconfig does not
    // subscribe idle tasks, so retain idle_core_mask=0 while changing only the
    // timeout/panic policy used by NFC crash forensics.
    const esp_task_wdt_config_t wdtConfig = {
        .timeout_ms = NFC_WDT_TIMEOUT_S * 1000U,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_err_t wdtErr = esp_task_wdt_reconfigure(&wdtConfig);
  #else
    esp_err_t wdtErr = esp_task_wdt_init(NFC_WDT_TIMEOUT_S, true);
  #endif
    if (wdtErr != ESP_OK) {
        Serial.printf("NFCManager: ERROR — TWDT configuration failed: %s (%d)\n",
                      esp_err_to_name(wdtErr), static_cast<int>(wdtErr));
    }
    wdtErr = esp_task_wdt_add(NULL);
    if (wdtErr != ESP_OK) {
        Serial.printf("NFCManager: ERROR — TWDT subscription failed: %s (%d)\n",
                      esp_err_to_name(wdtErr), static_cast<int>(wdtErr));
    }
#endif

    connection_->reset();
    connection_->setupRF();
    connection_->logDiagnostics();

    while (true) {
#ifndef NATIVE_TEST
        esp_task_wdt_reset();
        MemoryDiagnostics::reportSelf(MemoryDiagnostics::Task::NFCScan);
#endif
        // Cooperative diagnostic pause. This is a safe boundary — no tag
        // session or SPI transaction is in flight — so the self-test wizard
        // can take exclusive ownership of the reader without a mid-transaction
        // suspend. We keep feeding the watchdog while idle.
        if (diagPauseRequested_) {
            connection_->endTagSession();
            scanPaused_ = true;
            while (diagPauseRequested_) {
#ifndef NATIVE_TEST
                esp_task_wdt_reset();
#endif
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            scanPaused_ = false;
        }
        SCAN_PHASE(1);
        uint8_t uid[8];
        uint8_t uidLength = 0;

        SCAN_PHASE(2);
        if (!prepareRF()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        SCAN_PHASE(3);
        if (connection_->detectTag(uid, &uidLength)) {
            absentMisses_ = 0;
            if (!lastSeenValid || memcmp(uid, lastSeenUid, uidLength) != 0) {
                char uidHex[17]; uidHex[0] = '\0';
                for (uint8_t i = 0; i < uidLength; i++) snprintf(uidHex + i*2, 3, "%02X", uid[i]);
                Serial.printf("NFCManager: Tag detected! UID=%s\n", uidHex);
                LogBuffer::getInstance().logPrintf("Tag detected: UID=%s\n", uidHex);
            }
            connection_->setCurrentUid(uid, uidLength);
            // Bind pending writes to the tag we just selected, not to whatever
            // currentSpool still describes — the cooldown dedup below can skip
            // handleNewTag() and leave that stale (#276).
            recordSelectedUid(uid, uidLength);

            SCAN_PHASE(4);
            if (!isSkippableDuplicate(uid, uidLength)) {
                SCAN_PHASE(5);
                handleNewTag(uid, uidLength);
            }
            SCAN_PHASE(6);
            processWriteQueue();
        } else if (!lastSeenValid) {
            // Nothing was present — no debounce needed, keep state cleared
            SCAN_PHASE(7);
            handleTagAbsent();
        } else if (++absentMisses_ >= TAG_ABSENT_MISS_THRESHOLD) {
            SCAN_PHASE(8);
            // Debounce removal: single failed reads happen with stationary tags
            // (RF hiccups, marginal coupling). Only declare the tag gone after
            // several consecutive misses, or present/removed flaps every few
            // seconds and every subscriber (MQTT, HA, displays) flaps with it.
            handleTagAbsent();
            absentMisses_ = 0;
        }

        SCAN_PHASE(9);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void NFCManager::attemptRecovery() {
    Serial.println("NFCManager: WATCHDOG - attempting hardware recovery");
    if (connection_->hardwareReset()) {
        Serial.println("NFCManager: Hardware reset succeeded");
    } else {
        Serial.println("NFCManager: Hardware reset failed");
    }
}

bool NFCManager::readAndParseTag(uint8_t* uid, uint8_t uid_length) {
    // NFC I/O into local buffer — no mutex needed for hardware access
    opt_tag_t localTag;
    opt_init(&localTag);

    // OpenPrintTag uses ISO15693 (NFC-V) tags, designed for ICODE SLIX2 320B
    Serial.println("NFCManager: Reading tag data...");
    opt_nfc_hal_t* hal = connection_->getHal();
    opt_error_t err = opt_read_from_nfc(&localTag, hal, 0, 78);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to read tag data: %s\n", opt_error_str(err));
        return false;
    }

    Serial.println("NFCManager: Read successful, parsing NDEF...");
    err = opt_parse_ndef(&localTag);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to parse NDEF: %s\n", opt_error_str(err));
        return false;
    }

    // NFC I/O complete — take mutex only for the fast state copy
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("NFCManager: Could not acquire tagMutex for state update");
        return false;
    }

    currentSpool.tag_data = localTag;

    // Store UID as hex string
    for (uint8_t i = 0; i < uid_length && i < 8; i++) {
        sprintf(currentSpool.spool_id + (i * 2), "%02X", uid[i]);
    }
    currentSpool.spool_id[uid_length * 2] = '\0';

    memcpy(currentSpool.uid, uid, uid_length);
    currentSpool.uid_length = uid_length;

    currentSpool.present = true;
    currentSpool.tag_data_valid = true;
    currentSpool.variant = NtagVariant::Unknown;  // ISO15693 — no NTAG identity here
    currentSpool.cc_user_end = 0;

    addToRecentSpoolsLocked();

    Serial.printf("NFCManager: Parsed spool %s\n", currentSpool.spool_id);

    sendOpenPrintTagMessage();

    // Update dedup state
    memcpy(lastSeenUid, uid, uid_length);
    lastSeenUidLength = uid_length;
    lastSeenValid = true;
    lastSeenMs = millis();

    xSemaphoreGive(tagMutex);

    #ifdef DUMP_TAGS_TO_CONSOLE
        size_t encoded_max_len = ((sizeof(localTag.data) + 2) / 3) * 4 + 1;
        char* base64_output_buffer = new char[encoded_max_len];
        unsigned int encoded_len = encode_base64(localTag.data, sizeof(localTag.data), (unsigned char*)base64_output_buffer);
        base64_output_buffer[encoded_len] = '\0';
        Serial.println("NFCManager: Spool data:");
        Serial.println(base64_output_buffer);
    #endif

    return true;
}

bool NFCManager::formatNewSpool() {
    Serial.println("NFCManager: formatNewSpool() called");

    // All NFC I/O uses a local buffer — no mutex needed
    opt_tag_t localTag;
    opt_init(&localTag);

    // Format as empty tag with aux region for usage tracking
    // ISO15693 ICODE SLIX2 has 320 bytes, use 312 bytes with 32-byte aux region
    opt_error_t err = opt_format_empty_tag(&localTag, 312, 32);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to format empty tag: %s\n", opt_error_str(err));
        return false;
    }
    Serial.println("NFCManager: Tag formatted, setting defaults...");

    // Set default values for new spool
    // material_class is Required per OpenPrintTag spec
    err = opt_set_material_class(&localTag, OPT_MATERIAL_CLASS_FFF);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set material class: %s\n", opt_error_str(err));
        return false;
    }

    err = opt_set_material_type(&localTag, OPT_MATERIAL_TYPE_PLA);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set material type: %s\n", opt_error_str(err));
        return false;
    }

    err = opt_set_actual_full_weight(&localTag, 1000.0f);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set weight: %s\n", opt_error_str(err));
        return false;
    }

    uint8_t white[4] = {255, 255, 255, 255};
    err = opt_set_primary_color(&localTag, white);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set color: %s\n", opt_error_str(err));
        return false;
    }

    err = opt_set_consumed_weight(&localTag, 0.0f);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set consumed weight: %s\n", opt_error_str(err));
        return false;
    }

    err = opt_set_brand_name(&localTag, "Unknown");
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set brand name: %s\n", opt_error_str(err));
        return false;
    }

    Serial.println("NFCManager: Writing to NFC tag...");
    opt_nfc_hal_t* hal = connection_->getHal();
    err = opt_write_to_nfc(&localTag, hal);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to write formatted tag: %s\n", opt_error_str(err));
        return false;
    }

    // Re-read and verify with retries
    Serial.println("NFCManager: Verifying write...");
    const int maxRetries = 3;
    for (int retry = 0; retry < maxRetries; retry++) {
        vTaskDelay(pdMS_TO_TICKS(50 * (retry + 1)));  // 50ms, 100ms, 150ms

        err = opt_read_from_nfc(&localTag, hal, 0, 78);
        if (err == OPT_OK) {
            err = opt_parse_ndef(&localTag);
            if (err == OPT_OK) {
                // Verified — copy result into shared state under mutex
                if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                    Serial.println("NFCManager: Could not acquire tagMutex after format verify");
                    return false;
                }
                currentSpool.tag_data = localTag;
                currentSpool.tag_data_valid = true;
                currentSpool.blank_tag_present = false;
                currentSpool.kind = TagKind::OpenPrintTag;
                addToRecentSpoolsLocked();

                // Check if write queue is empty (no batched writes pending)
                NFCWriteRequest dummyReq;
                bool queueEmpty = (writeQueue == nullptr || xQueuePeek(writeQueue, &dummyReq, 0) != pdTRUE);

                if (queueEmpty) {
                    // No batched writes - send SpoolDetected immediately
                    sendOpenPrintTagMessage();
                    Serial.println("NFCManager: formatNewSpool() complete - verified (queue empty, sent SpoolDetected)");
                } else {
                    // Batched writes pending - set suppression flag
                    suppressReDetection_ = true;
                    strncpy(suppressReDetectionUid_, currentSpool.spool_id, sizeof(suppressReDetectionUid_) - 1);
                    suppressReDetectionUid_[sizeof(suppressReDetectionUid_) - 1] = '\0';
                    Serial.println("NFCManager: formatNewSpool() complete - verified (suppressing re-read for batched writes)");
                }

                xSemaphoreGive(tagMutex);
                return true;
            }
            Serial.printf("NFCManager: Parse failed on retry %d: %s\n", retry + 1, opt_error_str(err));
        } else {
            Serial.printf("NFCManager: Re-read failed on retry %d: %s\n", retry + 1, opt_error_str(err));
        }
    }

    // All retries failed - fall back to trusting the in-memory data
    Serial.println("NFCManager: Verification retries exhausted, trusting in-memory data");
    err = opt_parse_ndef(&localTag);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to parse in-memory data: %s\n", opt_error_str(err));
        return false;
    }

    // Copy unverified result into shared state under mutex
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("NFCManager: Could not acquire tagMutex after format fallback");
        return false;
    }
    currentSpool.tag_data = localTag;
    currentSpool.tag_data_valid = true;
    currentSpool.blank_tag_present = false;
    currentSpool.kind = TagKind::OpenPrintTag;
    addToRecentSpoolsLocked();

    // Check if write queue is empty (no batched writes pending)
    NFCWriteRequest dummyReq2;
    bool queueEmpty = (writeQueue == nullptr || xQueuePeek(writeQueue, &dummyReq2, 0) != pdTRUE);

    if (queueEmpty) {
        // No batched writes - send SpoolDetected immediately
        sendOpenPrintTagMessage();
        Serial.println("NFCManager: formatNewSpool() complete - unverified (queue empty, sent SpoolDetected)");
    } else {
        // Batched writes pending - set suppression flag
        suppressReDetection_ = true;
        strncpy(suppressReDetectionUid_, currentSpool.spool_id, sizeof(suppressReDetectionUid_) - 1);
        suppressReDetectionUid_[sizeof(suppressReDetectionUid_) - 1] = '\0';
        Serial.println("NFCManager: formatNewSpool() complete - unverified (suppressing re-read for batched writes)");
    }

    xSemaphoreGive(tagMutex);
    return true;
}

void NFCManager::sendOpenPrintTagMessage(bool suppress_spoolman_sync) {
    if (!currentSpool.tag_data_valid) {
        return;
    }

    AppMessage msg;
    memset(&msg, 0, sizeof(msg));  // every other SPOOL_DETECTED sender zeroes; an
                                   // uninitialized weight_is_nominal here randomly
                                   // suppressed the Spoolman weight sync
    msg.type = AppMessageType::SPOOL_DETECTED;

    // Copy spool ID
    strncpy(msg.payload.spoolDetected.spool_id, currentSpool.spool_id,
            sizeof(msg.payload.spoolDetected.spool_id) - 1);
    msg.payload.spoolDetected.spool_id[sizeof(msg.payload.spoolDetected.spool_id) - 1] = '\0';

    // Get material type
    uint8_t material_type = 0;
    if (opt_get_material_type(&currentSpool.tag_data, &material_type) == OPT_OK) {
        msg.payload.spoolDetected.material_type = material_type;
    } else {
        msg.payload.spoolDetected.material_type = 0;
    }

    // Calculate remaining weight in kg
    float full_weight = 0.0f;
    float consumed = 0.0f;
    opt_get_actual_full_weight(&currentSpool.tag_data, &full_weight);
    opt_get_consumed_weight(&currentSpool.tag_data, &consumed);
    msg.payload.spoolDetected.kg_remaining = (full_weight - consumed) / 1000.0f;

    // Get primary color
    if (opt_get_primary_color(&currentSpool.tag_data, msg.payload.spoolDetected.primary_color) != OPT_OK) {
        memset(msg.payload.spoolDetected.primary_color, 0, 4);
        msg.payload.spoolDetected.has_color = false;
    } else {
        msg.payload.spoolDetected.has_color = true;
    }

    // Get density (use 0 to signal "not available" to caller)
    if (opt_get_density(&currentSpool.tag_data, &msg.payload.spoolDetected.density) != OPT_OK) {
        msg.payload.spoolDetected.density = 0.0f;
    }

    // Get filament diameter
    if (opt_get_filament_diameter(&currentSpool.tag_data, &msg.payload.spoolDetected.diameter) != OPT_OK) {
        msg.payload.spoolDetected.diameter = 0.0f;
    }

    // Get initial (full) weight
    msg.payload.spoolDetected.initial_weight_g = full_weight;

    // Get manufacturer/brand name
    if (opt_get_brand_name(&currentSpool.tag_data, msg.payload.spoolDetected.manufacturer,
                           sizeof(msg.payload.spoolDetected.manufacturer)) != OPT_OK) {
        msg.payload.spoolDetected.manufacturer[0] = '\0';
    }

    // Get Spoolman ID from tag (if present)
    int32_t spoolman_id = -1;
    opt_get_gp_spoolman_id(&currentSpool.tag_data, &spoolman_id);
    msg.payload.spoolDetected.spoolman_id = spoolman_id;

    // Get temperatures
    int16_t t = 0;
    msg.payload.spoolDetected.min_print_temp = (opt_get_min_print_temp(&currentSpool.tag_data, &t) == OPT_OK) ? t : 0;
    msg.payload.spoolDetected.max_print_temp = (opt_get_max_print_temp(&currentSpool.tag_data, &t) == OPT_OK) ? t : 0;
    msg.payload.spoolDetected.min_bed_temp = (opt_get_min_bed_temp(&currentSpool.tag_data, &t) == OPT_OK) ? t : 0;
    msg.payload.spoolDetected.max_bed_temp = (opt_get_max_bed_temp(&currentSpool.tag_data, &t) == OPT_OK) ? t : 0;

    // OpenPrintTag doesn't have aspect or dry fields
    msg.payload.spoolDetected.aspect[0] = '\0';
    msg.payload.spoolDetected.dry_temp = 0;
    msg.payload.spoolDetected.dry_time_hours = 0;
    strncpy(msg.payload.spoolDetected.tag_format, "OpenPrintTag", sizeof(msg.payload.spoolDetected.tag_format) - 1);

    // Set suppress_spoolman_sync flag
    msg.payload.spoolDetected.suppress_spoolman_sync = suppress_spoolman_sync ? 1 : 0;

    // Get material name
    if (opt_get_material_name(&currentSpool.tag_data, msg.payload.spoolDetected.material_name,
                               sizeof(msg.payload.spoolDetected.material_name)) != OPT_OK) {
        // Use material type abbreviation as fallback
        const char* abbrev = opt_material_type_str((opt_material_type_t)material_type);
        if (abbrev) {
            strncpy(msg.payload.spoolDetected.material_name, abbrev,
                    sizeof(msg.payload.spoolDetected.material_name) - 1);
            msg.payload.spoolDetected.material_name[sizeof(msg.payload.spoolDetected.material_name) - 1] = '\0';
        } else {
            msg.payload.spoolDetected.material_name[0] = '\0';
        }
    }

    // Full payload dump for development/debug
    {
        const auto& s = msg.payload.spoolDetected;
        Serial.println("--- SpoolDetected payload ---");
        Serial.printf("  uid:          %s\n", s.spool_id);
        Serial.printf("  manufacturer: %s\n", s.manufacturer);
        Serial.printf("  material:     %s (type=%d)\n", s.material_name, s.material_type);
        Serial.printf("  color:        #%02X%02X%02X (has_color=%d)\n",
                      s.primary_color[0], s.primary_color[1], s.primary_color[2], s.has_color);
        Serial.printf("  weight:       %.1fg remaining / %.1fg initial\n",
                      s.kg_remaining * 1000.0f, s.initial_weight_g);
        Serial.printf("  density:      %.3f g/cm3  diameter: %.2fmm\n", s.density, s.diameter);
        Serial.printf("  spoolman_id:  %d  suppress_sync=%d\n",
                      s.spoolman_id, s.suppress_spoolman_sync);
        Serial.println("-----------------------------");
        LogBuffer::getInstance().logPrintf("OpenPrintTag: %s %s #%02X%02X%02X %.0fg\n",
            s.manufacturer, s.material_name,
            s.primary_color[0], s.primary_color[1], s.primary_color[2],
            s.kg_remaining * 1000.0f);
    }

    ApplicationManager::getInstance().sendMessage(msg);
}

void NFCManager::sendBambuDetectedMessage() {
    if (!lastBambuTagValid_) return;

    const BambuTagData& bt = lastBambuTag_;
    AppMessage msg;
    msg.type = AppMessageType::SPOOL_DETECTED;
    memset(&msg.payload.spoolDetected, 0, sizeof(msg.payload.spoolDetected));

    strncpy(msg.payload.spoolDetected.spool_id, currentSpool.spool_id,
            sizeof(msg.payload.spoolDetected.spool_id) - 1);

    msg.payload.spoolDetected.material_type = materialTypeFromString(bt.filament_type);
    strncpy(msg.payload.spoolDetected.material_name, bt.filament_type,
            sizeof(msg.payload.spoolDetected.material_name) - 1);

    msg.payload.spoolDetected.primary_color[0] = bt.color_r;
    msg.payload.spoolDetected.primary_color[1] = bt.color_g;
    msg.payload.spoolDetected.primary_color[2] = bt.color_b;
    msg.payload.spoolDetected.has_color = true;

    msg.payload.spoolDetected.kg_remaining = bt.weight_g / 1000.0f;
    msg.payload.spoolDetected.weight_is_nominal = true;  // Bambu block 5 is the static
                                                         // nominal size, not a level
    msg.payload.spoolDetected.initial_weight_g = bt.weight_g;
    msg.payload.spoolDetected.diameter = bt.diameter_mm;
    msg.payload.spoolDetected.min_print_temp = bt.hotend_min;
    msg.payload.spoolDetected.max_print_temp = bt.hotend_max;
    msg.payload.spoolDetected.min_bed_temp = bt.bed_temp;
    msg.payload.spoolDetected.dry_temp = bt.drying_temp;
    msg.payload.spoolDetected.dry_time_hours = bt.drying_time;
    msg.payload.spoolDetected.spoolman_id = -1;

    strncpy(msg.payload.spoolDetected.tag_format, "bambu",
            sizeof(msg.payload.spoolDetected.tag_format) - 1);

    Serial.printf("--- BambuDetected payload ---\n");
    Serial.printf("  uid:      %s\n", msg.payload.spoolDetected.spool_id);
    Serial.printf("  material: %s\n", bt.filament_type);
    Serial.printf("  color:    #%02X%02X%02X\n", bt.color_r, bt.color_g, bt.color_b);
    Serial.printf("  weight:   %ug  diameter: %.2fmm\n", bt.weight_g, bt.diameter_mm);
    Serial.printf("-----------------------------\n");

    ApplicationManager::getInstance().sendMessage(msg);
}

void NFCManager::sendBlankTagMessage() {
    AppMessage msg;
    msg.type = AppMessageType::BLANK_TAG_DETECTED;
    strncpy(msg.payload.blankTag.spool_id, currentSpool.spool_id,
            sizeof(msg.payload.blankTag.spool_id) - 1);
    msg.payload.blankTag.spool_id[sizeof(msg.payload.blankTag.spool_id) - 1] = '\0';

    ApplicationManager::getInstance().sendMessage(msg);
}

void NFCManager::sendGenericTagMessage() {
    AppMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AppMessageType::GENERIC_TAG_DETECTED;
    strncpy(msg.payload.genericTag.spool_id, currentSpool.spool_id,
            sizeof(msg.payload.genericTag.spool_id) - 1);
    msg.payload.genericTag.spool_id[sizeof(msg.payload.genericTag.spool_id) - 1] = '\0';
    ApplicationManager::getInstance().sendMessage(msg);
}

void NFCManager::sendTigerTagMessage(const TigerTagData& tt) {
    AppMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AppMessageType::SPOOL_DETECTED;

    auto& s = msg.payload.spoolDetected;

    strncpy(s.spool_id, currentSpool.spool_id, sizeof(s.spool_id) - 1);
    strncpy(s.manufacturer, tt.brand_name, sizeof(s.manufacturer) - 1);
    strncpy(s.material_name, tt.material_name, sizeof(s.material_name) - 1);

    // Map TigerTag material to closest OpenPrintTag type for Spoolman compat
    s.material_type = 0;  // Default PLA
    // Common mappings — TigerTag uses string names, we need the OPT enum
    if (strcmp(tt.material_name, "PLA") == 0 || strcmp(tt.material_name, "PLA+") == 0 ||
        strcmp(tt.material_name, "PLA-HS") == 0)       s.material_type = OPT_MATERIAL_TYPE_PLA;
    else if (strcmp(tt.material_name, "PETG") == 0 ||
             strcmp(tt.material_name, "PETG-HS") == 0)  s.material_type = OPT_MATERIAL_TYPE_PETG;
    else if (strcmp(tt.material_name, "TPU") == 0 ||
             strcmp(tt.material_name, "TPU-HS") == 0)   s.material_type = OPT_MATERIAL_TYPE_TPU;
    else if (strcmp(tt.material_name, "ABS") == 0)      s.material_type = OPT_MATERIAL_TYPE_ABS;
    else if (strcmp(tt.material_name, "ASA") == 0 ||
             strcmp(tt.material_name, "ASA+") == 0)     s.material_type = OPT_MATERIAL_TYPE_ASA;
    else if (strcmp(tt.material_name, "PC") == 0)       s.material_type = OPT_MATERIAL_TYPE_PC;
    else if (strcmp(tt.material_name, "PCTG") == 0)     s.material_type = OPT_MATERIAL_TYPE_PCTG;
    else if (strcmp(tt.material_name, "PP") == 0)       s.material_type = OPT_MATERIAL_TYPE_PP;
    else if (strcmp(tt.material_name, "PA6") == 0)      s.material_type = OPT_MATERIAL_TYPE_PA6;
    else if (strcmp(tt.material_name, "PA12") == 0)     s.material_type = OPT_MATERIAL_TYPE_PA12;
    else if (strcmp(tt.material_name, "HIPS") == 0)     s.material_type = OPT_MATERIAL_TYPE_HIPS;
    else if (strcmp(tt.material_name, "PVA") == 0)      s.material_type = OPT_MATERIAL_TYPE_PVA;
    else if (strcmp(tt.material_name, "PEEK") == 0)     s.material_type = OPT_MATERIAL_TYPE_PEEK;
    else if (strcmp(tt.material_name, "PEI") == 0)      s.material_type = OPT_MATERIAL_TYPE_PEI;
    else if (strcmp(tt.material_name, "TPE") == 0)      s.material_type = OPT_MATERIAL_TYPE_TPE;

    s.has_color = 1;
    s.primary_color[0] = tt.color_r;
    s.primary_color[1] = tt.color_g;
    s.primary_color[2] = tt.color_b;

    s.initial_weight_g = tt.weight_g;
    s.kg_remaining = tt.weight_g / 1000.0f;  // TigerTag has no consumed_weight, so remaining = initial
    s.weight_is_nominal = true;              // never sync that nominal into Spoolman as remaining

    s.density = getDefaultDensity(s.material_type);
    s.diameter = tt.diameter_mm > 0 ? tt.diameter_mm : 1.75f;

    s.min_print_temp = tt.nozzle_temp_min;
    s.max_print_temp = tt.nozzle_temp_max;
    s.min_bed_temp = tt.bed_temp_min;
    s.max_bed_temp = tt.bed_temp_max;
    s.dry_temp = tt.dry_temp;
    s.dry_time_hours = tt.dry_time_hours;

    // Aspect from TigerTag
    if (tt.aspect1_name && strcmp(tt.aspect1_name, "None") != 0 &&
        strcmp(tt.aspect1_name, "-") != 0 && strcmp(tt.aspect1_name, "Unknown") != 0) {
        strncpy(s.aspect, tt.aspect1_name, sizeof(s.aspect) - 1);
    }

    strncpy(s.tag_format, "TigerTag", sizeof(s.tag_format) - 1);

    s.spoolman_id = -1;
    s.suppress_spoolman_sync = 0;

    // Payload dump
    Serial.println("--- TigerTag SpoolDetected payload ---");
    Serial.printf("  uid:          %s\n", s.spool_id);
    Serial.printf("  brand:        %s\n", s.manufacturer);
    Serial.printf("  material:     %s (type=%d)\n", s.material_name, s.material_type);
    Serial.printf("  aspect:       %s\n", tt.aspect1_name);
    Serial.printf("  color:        #%02X%02X%02X (A=%d)\n", tt.color_r, tt.color_g, tt.color_b, tt.color_a);
    Serial.printf("  weight:       %dg\n", tt.weight_g);
    Serial.printf("  diameter:     %.2fmm\n", s.diameter);
    Serial.printf("  nozzle:       %d-%d°C\n", tt.nozzle_temp_min, tt.nozzle_temp_max);
    Serial.printf("  bed:          %d-%d°C\n", tt.bed_temp_min, tt.bed_temp_max);
    Serial.printf("  dry:          %d°C / %dh\n", tt.dry_temp, tt.dry_time_hours);
    Serial.println("--------------------------------------");
    LogBuffer::getInstance().logPrintf("  %s %s #%02X%02X%02X %dg\n",
        s.manufacturer, s.material_name, tt.color_r, tt.color_g, tt.color_b, tt.weight_g);

    ApplicationManager::getInstance().sendMessage(msg);
}

void NFCManager::sendOpenTag3DMessage(const opentag3d_t& ot3d) {
    AppMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AppMessageType::SPOOL_DETECTED;

    auto& s = msg.payload.spoolDetected;

    strncpy(s.spool_id, currentSpool.spool_id, sizeof(s.spool_id) - 1);
    strncpy(s.manufacturer, ot3d.manufacturer, sizeof(s.manufacturer) - 1);

    // Build descriptive material name: "Color Name Material" (e.g. "Blood Red PLA") or just "PLA"
    if (ot3d.color_name[0] != '\0') {
        snprintf(s.material_name, sizeof(s.material_name), "%s %s", ot3d.color_name, ot3d.base_material);
    } else if (ot3d.material_modifiers[0] != '\0') {
        snprintf(s.material_name, sizeof(s.material_name), "%s %s", ot3d.base_material, ot3d.material_modifiers);
    } else {
        strncpy(s.material_name, ot3d.base_material, sizeof(s.material_name) - 1);
    }

    // Map base_material string to closest OpenPrintTag type for Spoolman compat
    s.material_type = 0;  // Default PLA
    if (strcmp(ot3d.base_material, "PLA") == 0)        s.material_type = OPT_MATERIAL_TYPE_PLA;
    else if (strcmp(ot3d.base_material, "PETG") == 0)  s.material_type = OPT_MATERIAL_TYPE_PETG;
    else if (strcmp(ot3d.base_material, "TPU") == 0)   s.material_type = OPT_MATERIAL_TYPE_TPU;
    else if (strcmp(ot3d.base_material, "ABS") == 0)   s.material_type = OPT_MATERIAL_TYPE_ABS;
    else if (strcmp(ot3d.base_material, "ASA") == 0)   s.material_type = OPT_MATERIAL_TYPE_ASA;
    else if (strcmp(ot3d.base_material, "PC") == 0)    s.material_type = OPT_MATERIAL_TYPE_PC;
    else if (strcmp(ot3d.base_material, "PCTG") == 0)  s.material_type = OPT_MATERIAL_TYPE_PCTG;
    else if (strcmp(ot3d.base_material, "PP") == 0)    s.material_type = OPT_MATERIAL_TYPE_PP;
    else if (strcmp(ot3d.base_material, "PA6") == 0)   s.material_type = OPT_MATERIAL_TYPE_PA6;
    else if (strcmp(ot3d.base_material, "PA12") == 0)  s.material_type = OPT_MATERIAL_TYPE_PA12;
    else if (strcmp(ot3d.base_material, "HIPS") == 0)  s.material_type = OPT_MATERIAL_TYPE_HIPS;
    else if (strcmp(ot3d.base_material, "PVA") == 0)   s.material_type = OPT_MATERIAL_TYPE_PVA;
    else if (strcmp(ot3d.base_material, "PEEK") == 0)  s.material_type = OPT_MATERIAL_TYPE_PEEK;
    else if (strcmp(ot3d.base_material, "PEI") == 0)   s.material_type = OPT_MATERIAL_TYPE_PEI;
    else if (strcmp(ot3d.base_material, "TPE") == 0)   s.material_type = OPT_MATERIAL_TYPE_TPE;

    // Color from primary RGBA (first color slot)
    s.has_color = 1;
    s.primary_color[0] = ot3d.color_rgba[0][0];
    s.primary_color[1] = ot3d.color_rgba[0][1];
    s.primary_color[2] = ot3d.color_rgba[0][2];

    // Weight: prefer measured if extended data available, else target
    if (ot3d.has_extended && ot3d.measured_filament_weight_g > 0) {
        s.initial_weight_g = ot3d.measured_filament_weight_g;
        s.kg_remaining = ot3d.measured_filament_weight_g / 1000.0f;
    } else {
        s.initial_weight_g = ot3d.target_weight_g;
        s.kg_remaining = ot3d.target_weight_g / 1000.0f;
        // v2 defines this field as the spool's NOMINAL size, not a remaining
        // counter. Flag it so the Spoolman sync treats the tag as weightless —
        // otherwise the next scan's sync PATCHes the nominal back and undoes
        // the Spoolman-side deduction fallback.
        s.weight_is_nominal = (opentag3d_major(ot3d.tag_version) >= 2);
    }

    // Density from tag if non-zero, else fallback
    if (ot3d.density_ugcm3 > 0) {
        s.density = opentag3d_density_gcc(&ot3d);
    } else {
        s.density = getDefaultDensity(s.material_type);
    }

    s.diameter = opentag3d_diameter_mm(&ot3d);
    if (s.diameter <= 0) s.diameter = 1.75f;

    // Temps: use extended min/max if non-zero, otherwise fall back to core single value
    uint16_t corePrint = (uint16_t)opentag3d_temp_c(ot3d.print_temp_encoded);
    uint16_t coreBed = (uint16_t)opentag3d_temp_c(ot3d.bed_temp_encoded);
    s.min_print_temp = (ot3d.min_print_temp_encoded > 0) ? (uint16_t)opentag3d_temp_c(ot3d.min_print_temp_encoded) : corePrint;
    s.max_print_temp = (ot3d.max_print_temp_encoded > 0) ? (uint16_t)opentag3d_temp_c(ot3d.max_print_temp_encoded) : corePrint;
    s.min_bed_temp = (ot3d.min_bed_temp_encoded > 0) ? (uint16_t)opentag3d_temp_c(ot3d.min_bed_temp_encoded) : coreBed;
    s.max_bed_temp = (ot3d.max_bed_temp_encoded > 0) ? (uint16_t)opentag3d_temp_c(ot3d.max_bed_temp_encoded) : coreBed;
    if (ot3d.max_dry_temp_encoded > 0) {
        s.dry_temp = (uint8_t)opentag3d_temp_c(ot3d.max_dry_temp_encoded);
        s.dry_time_hours = ot3d.dry_time_hours;
    }

    // Material modifiers -> aspect field
    if (ot3d.material_modifiers[0] != '\0' && ot3d.material_modifiers[0] != ' ') {
        strncpy(s.aspect, ot3d.material_modifiers, sizeof(s.aspect) - 1);
    }

    strncpy(s.tag_format, "OpenTag3D", sizeof(s.tag_format) - 1);

    s.spoolman_id = -1;
    s.suppress_spoolman_sync = 0;

    // Payload dump
    Serial.println("--- OpenTag3D SpoolDetected payload ---");
    Serial.printf("  uid:          %s\n", s.spool_id);
    Serial.printf("  manufacturer: %s\n", s.manufacturer);
    Serial.printf("  material:     %s (type=%d)\n", s.material_name, s.material_type);
    Serial.printf("  modifiers:    %s\n", ot3d.material_modifiers);
    Serial.printf("  color:        #%02X%02X%02X (A=%d)\n",
                  ot3d.color_rgba[0][0], ot3d.color_rgba[0][1], ot3d.color_rgba[0][2], ot3d.color_rgba[0][3]);
    Serial.printf("  weight:       %.0fg\n", s.initial_weight_g);
    Serial.printf("  diameter:     %.2fmm\n", s.diameter);
    Serial.printf("  density:      %.4f g/cm³\n", s.density);
    Serial.printf("  nozzle:       %d-%d°C\n", s.min_print_temp, s.max_print_temp);
    Serial.printf("  bed:          %d-%d°C\n", s.min_bed_temp, s.max_bed_temp);
    if (ot3d.has_extended) {
        Serial.printf("  dry:          %d°C / %dh\n", s.dry_temp, s.dry_time_hours);
    }
    Serial.println("---------------------------------------");
    LogBuffer::getInstance().logPrintf("  %s %s #%02X%02X%02X %.0fg\n",
        s.manufacturer, s.material_name,
        ot3d.color_rgba[0][0], ot3d.color_rgba[0][1], ot3d.color_rgba[0][2], s.initial_weight_g);

    ApplicationManager::getInstance().sendMessage(msg);
}

void NFCManager::sendOpenSpoolMessage(const char* uid, const OpenSpoolData& os) {
    AppMessage msg;
    msg.type = AppMessageType::SPOOL_DETECTED;
    auto& s = msg.payload.spoolDetected;
    memset(&s, 0, sizeof(s));

    strncpy(s.spool_id, uid, sizeof(s.spool_id) - 1);
    strncpy(s.manufacturer, os.brand, sizeof(s.manufacturer) - 1);
    strncpy(s.material_name, os.material, sizeof(s.material_name) - 1);

    // Material type lookup
    s.material_type = 0;
    if (strcasecmp(os.material, "PLA") == 0) s.material_type = OPT_MATERIAL_TYPE_PLA;
    else if (strcasecmp(os.material, "PETG") == 0) s.material_type = OPT_MATERIAL_TYPE_PETG;
    else if (strcasecmp(os.material, "ABS") == 0) s.material_type = OPT_MATERIAL_TYPE_ABS;
    else if (strcasecmp(os.material, "ASA") == 0) s.material_type = OPT_MATERIAL_TYPE_ASA;
    else if (strcasecmp(os.material, "TPU") == 0) s.material_type = OPT_MATERIAL_TYPE_TPU;
    else if (strcasecmp(os.material, "PA") == 0 || strcasecmp(os.material, "Nylon") == 0) s.material_type = OPT_MATERIAL_TYPE_PA6;
    else if (strcasecmp(os.material, "PC") == 0) s.material_type = OPT_MATERIAL_TYPE_PC;

    // Parse color hex to RGB
    if (strlen(os.color_hex) == 6) {
        unsigned int r, g, b;
        if (sscanf(os.color_hex, "%02x%02x%02x", &r, &g, &b) == 3) {
            s.primary_color[0] = (uint8_t)r;
            s.primary_color[1] = (uint8_t)g;
            s.primary_color[2] = (uint8_t)b;
            s.primary_color[3] = 255;
            s.has_color = true;
        }
    }

    s.min_print_temp = os.min_temp;
    s.max_print_temp = os.max_temp;

    strncpy(s.tag_format, "OpenSpool", sizeof(s.tag_format) - 1);
    s.spoolman_id = -1;

    Serial.println("--- OpenSpool SpoolDetected payload ---");
    Serial.printf("  uid:          %s\n", s.spool_id);
    Serial.printf("  brand:        %s\n", s.manufacturer);
    Serial.printf("  material:     %s\n", s.material_name);
    Serial.printf("  color:        #%s\n", os.color_hex);
    Serial.printf("  nozzle:       %d-%d°C\n", s.min_print_temp, s.max_print_temp);
    Serial.printf("  version:      %s\n", os.version);
    Serial.println("---------------------------------------");
    LogBuffer::getInstance().logPrintf("  %s %s #%s\n", s.manufacturer, s.material_name, os.color_hex);

    ApplicationManager::getInstance().sendMessage(msg);
}

// Map GET_VERSION response byte 6 to NTAG variant
static NtagVariant mapStorageByte(uint8_t storage) {
    switch (storage) {
        case 0x0F: return NtagVariant::NTAG213;
        case 0x11: return NtagVariant::NTAG215;
        case 0x13: return NtagVariant::NTAG216;
        case 0x06: return NtagVariant::UltralightEV1_48;
        case 0x09: return NtagVariant::UltralightEV1_128;
        default:   return NtagVariant::Unknown;
    }
}

TagScanResult NFCManager::classifyTag(const uint8_t* uid, uint8_t uid_length) {
    TagScanResult result;
    result.present = true;
    result.tag_data_valid = false;
    result.variant = NtagVariant::Unknown;
    result.cc_user_end = 0;
    if (uid_length == 8) {
        result.protocol = TagProtocol::ISO15693;
        result.kind = TagKind::BlankTag;
    } else {
        result.protocol = TagProtocol::ISO14443A;
        uint8_t sak = connection_->getLastSAK();
        if (sak == 0x08 || sak == 0x18) {
            result.kind = TagKind::BambuTag;
            Serial.printf("NFCManager: MIFARE Classic detected (SAK=0x%02X) — treating as Bambu tag\n", sak);
        } else {
            result.kind = TagKind::GenericUidTag;
            // GET_VERSION identifies exact NTAG model — used for write capacity checks
            uint8_t version[8];
            if (connection_->ntagGetVersion(version)) {
                result.variant = mapStorageByte(version[6]);
                Serial.printf("NFCManager: %s detected (%d pages)\n",
                    ntagVariantName(result.variant), ntagUsablePages(result.variant));
                LogBuffer::getInstance().logPrintf("%s (%d pages)\n",
                    ntagVariantName(result.variant), ntagUsablePages(result.variant));
            }
            // Unidentified chip (failed GET_VERSION or unmapped storage byte):
            // the page-3 CC declares the data area honestly, manufacturer-
            // independently. Trust it exactly — never pad past the die end.
            if (result.variant == NtagVariant::Unknown) {
                uint8_t ccBuf[4] = {0};
                if (connection_->readISO14443Pages(3, 1, ccBuf, sizeof(ccBuf), true) >= 4) {  // keepSession — don't tear down the RF session the data read needs next
                    result.cc_user_end = ccUserMemoryEnd(ccBuf);
                }
                if (result.cc_user_end > 0) {
                    Serial.printf("NFCManager: unknown NTAG variant — CC declares %u user pages\n",
                                  (unsigned)(result.cc_user_end - 4));
                    LogBuffer::getInstance().logPrintf("Unknown NTAG — CC declares %u user pages\n",
                                  (unsigned)(result.cc_user_end - 4));
                } else {
                    Serial.println("NFCManager: unknown NTAG variant and no valid CC — conservative size limits apply");
                }
            }
        }
    }
    uint8_t len = uid_length < 8 ? uid_length : 8;
    for (uint8_t i = 0; i < len; i++) {
        snprintf(result.uid_hex + (i * 2), 3, "%02X", uid[i]);
    }
    result.uid_hex[len * 2] = '\0';
    return result;
}

void NFCManager::sendTagRemovedMessage() {
    AppMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AppMessageType::TAG_REMOVED;
    strncpy(msg.payload.tagRemoved.spool_id, currentSpool.spool_id,
            sizeof(msg.payload.tagRemoved.spool_id) - 1);
    msg.payload.tagRemoved.spool_id[sizeof(msg.payload.tagRemoved.spool_id) - 1] = '\0';
    msg.payload.tagRemoved.last_remaining_kg = 0.0f;
    msg.payload.tagRemoved.spoolman_id = -1;

    if (currentSpool.tag_data_valid) {
        float full_weight = 0.0f, consumed = 0.0f;
        opt_get_actual_full_weight(&currentSpool.tag_data, &full_weight);
        opt_get_consumed_weight(&currentSpool.tag_data, &consumed);
        msg.payload.tagRemoved.last_remaining_kg = (full_weight - consumed) / 1000.0f;

        int32_t smId = -1;
        opt_get_gp_spoolman_id(&currentSpool.tag_data, &smId);
        msg.payload.tagRemoved.spoolman_id = smId;
    }

    ApplicationManager::getInstance().sendMessage(msg);
}

bool NFCManager::enqueueRawWrite(const NFCWriteRequest& req, const uint8_t* data, size_t dataSize) {
    if (dataSize == 0 || dataSize > RAW_WRITE_BUFFER_SIZE) {
        return false;
    }
    if (rawWritePending_) {
        Serial.println("NFCManager: Raw write already pending");
        return false;
    }
    memcpy(rawWriteBuffer_, data, dataSize);
    rawWriteBufferSize_ = dataSize;
    rawWritePending_ = true;
    return enqueueWrite(req);
}

uint32_t NFCManager::generateRequestId() {
    return ++s_write_request_id_counter;
}

bool NFCManager::writeSpoolmanDataToTag(int32_t spoolman_id, const char* expected_spool_id) {
#ifdef NATIVE_TEST
    // In native tests, SpoolmanManager is not available
    Serial.println("NFCManager: writeSpoolmanDataToTag not supported in native tests");
    return false;
#else
    // 1. Validate input
    if (spoolman_id <= 0) {
        Serial.printf("NFCManager: Invalid spoolman_id: %d\n", spoolman_id);
        return false;
    }

    // 2. Fetch spool details from Spoolman
    SpoolDetails details;
    if (!SpoolmanManager::getInstance().getSpoolDetails(spoolman_id, details)) {
        Serial.printf("NFCManager: Failed to fetch Spoolman spool %d\n", spoolman_id);
        return false;
    }

    // 3. Validate weight data
    if (details.initial_weight_g <= 0 || details.remaining_weight_g < 0) {
        Serial.println("NFCManager: Invalid weight data from Spoolman");
        return false;
    }
    if (details.remaining_weight_g > details.initial_weight_g) {
        Serial.println("NFCManager: Remaining weight exceeds initial weight");
        return false;
    }

    // 4. Build complete tag in memory (use writeScratchTag_ to avoid stack overflow)
    opt_init(&writeScratchTag_);
    opt_error_t err = opt_format_empty_tag(&writeScratchTag_, 312, 32);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to format tag: %s\n", opt_error_str(err));
        return false;
    }

    // 5. Convert and populate fields
    uint8_t material_enum = materialTypeFromString(details.material_type);
    opt_set_material_type(&writeScratchTag_, material_enum);

    uint8_t rgba[4] = {255, 255, 255, 255};  // Default white
    parseHexColor(details.color_hex, rgba);  // Best effort
    opt_set_primary_color(&writeScratchTag_, rgba);

    opt_set_brand_name(&writeScratchTag_, details.manufacturer);
    opt_set_actual_full_weight(&writeScratchTag_, details.initial_weight_g);

    float consumed = details.initial_weight_g - details.remaining_weight_g;
    opt_set_consumed_weight(&writeScratchTag_, consumed);

    // Set defaults for missing fields
    float density = getDefaultDensity(material_enum);
    opt_set_density(&writeScratchTag_, density);
    opt_set_filament_diameter(&writeScratchTag_, 1.75f);

    // Write Spoolman ID to aux region
    opt_set_gp_spoolman_id(&writeScratchTag_, spoolman_id);

    // 6. Enqueue raw write
    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));  // CRITICAL: zero-init (per MEMORY.md)
    req.request_id = generateRequestId();
    req.type = NFCWriteType::WRITE_RAW_TAG;
    if (expected_spool_id != nullptr) {
        strncpy(req.expected_spool_id, expected_spool_id, sizeof(req.expected_spool_id) - 1);
    }

    bool queued = enqueueRawWrite(req, writeScratchTag_.data, writeScratchTag_.data_size);
    if (queued) {
        Serial.printf("NFCManager: Queued write for Spoolman spool %d\n", spoolman_id);
    }
    return queued;
#endif
}

bool NFCManager::enqueueWrite(const NFCWriteRequest& req) {
    if (writeQueue == nullptr) {
        return false;
    }

    // Check if already completed (duplicate)
    if (isRequestCompleted(req.request_id)) {
        return true;  // Already done, return success
    }

    bool queued = xQueueSend(writeQueue, &req, pdMS_TO_TICKS(100)) == pdTRUE;

    // If this is part of a batch (suppress_sync or FORMAT_NEW), set suppression flag
    if (queued && (req.suppress_sync || req.type == NFCWriteType::FORMAT_NEW)) {
        suppressReDetection_ = true;
        if (req.expected_spool_id[0] != '\0') {
            strncpy(suppressReDetectionUid_, req.expected_spool_id, sizeof(suppressReDetectionUid_) - 1);
            suppressReDetectionUid_[sizeof(suppressReDetectionUid_) - 1] = '\0';
        }
        // Track if this batch has suppress_sync
        if (req.suppress_sync) {
            batchHadSuppressSync_ = true;
        }
    }

    return queued;
}

void NFCManager::processWriteQueue() {
    if (writeQueue == nullptr || !currentSpool.present) {
        return;
    }

    NFCWriteRequest request;

    // Process at most one write per call — remaining writes get handled in
    // subsequent scan cycles (50ms apart), keeping tag detection responsive.
    while (xQueuePeek(writeQueue, &request, 0) == pdTRUE) {
        // Check if already completed — skip without counting as our one write
        if (isRequestCompleted(request.request_id)) {
            xQueueReceive(writeQueue, &request, 0);
            continue;
        }

        // Dequeue the request
        xQueueReceive(writeQueue, &request, 0);

        // executeWrite() manages its own mutex internally
        bool success = executeWrite(request);

        markRequestCompleted(request.request_id);

        // Snapshot spool info under mutex for the notification message
        char snapshotSpoolId[64];
        float snapshotRemainingGrams = 0;
        bool snapshotValid = false;
        if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            strncpy(snapshotSpoolId, currentSpool.spool_id, sizeof(snapshotSpoolId));
            snapshotSpoolId[sizeof(snapshotSpoolId) - 1] = '\0';
            if (currentSpool.tag_data_valid) {
                opt_get_remaining_weight(&currentSpool.tag_data, &snapshotRemainingGrams);
                snapshotValid = true;
            }
            xSemaphoreGive(tagMutex);
        }

        // Build and send the update message with snapshotted data
        AppMessage msg;
        msg.type = AppMessageType::SPOOL_UPDATED;
        strncpy(msg.payload.spoolUpdated.spool_id, snapshotSpoolId,
                sizeof(msg.payload.spoolUpdated.spool_id) - 1);
        msg.payload.spoolUpdated.spool_id[sizeof(msg.payload.spoolUpdated.spool_id) - 1] = '\0';
        msg.payload.spoolUpdated.update_type = static_cast<uint8_t>(request.type);
        msg.payload.spoolUpdated.success = success;
        msg.payload.spoolUpdated.suppress_sync = request.suppress_sync;
        msg.payload.spoolUpdated.kg_remaining = snapshotValid ? snapshotRemainingGrams / 1000.0f : 0;
        ApplicationManager::getInstance().sendMessage(msg);

        // One write per cycle — break to let scan loop run between writes
        break;
    }

    // Check if queue is now empty
    NFCWriteRequest peekReq;
    if (xQueuePeek(writeQueue, &peekReq, 0) != pdTRUE) {
        // Queue is empty - clear suppression flag
        if (suppressReDetection_) {
            bool hadSuppressSync = batchHadSuppressSync_;
            Serial.printf("NFCManager: Write queue empty, clearing suppression and sending SpoolDetected (suppress_spoolman_sync=%d)\n", hadSuppressSync);
            suppressReDetection_ = false;
            suppressReDetectionUid_[0] = '\0';
            batchHadSuppressSync_ = false;

            // Send SpoolDetected under mutex — sendOpenPrintTagMessage reads currentSpool.tag_data
            if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                sendOpenPrintTagMessage(hadSuppressSync);
                xSemaphoreGive(tagMutex);
            }
        }
    }
}

bool NFCManager::writeRawTag() {
    Serial.println("NFCManager: writeRawTag() called");

    // Copy raw data into a local opt_tag_t
    opt_tag_t localTag;
    opt_init(&localTag);
    memcpy(localTag.data, rawWriteBuffer_, rawWriteBufferSize_);
    localTag.data_size = rawWriteBufferSize_;
    localTag.initialized = true;

    // Write all pages to NFC tag
    Serial.println("NFCManager: Writing raw data to NFC tag...");
    opt_nfc_hal_t* hal = connection_->getHal();
    opt_error_t err = opt_write_to_nfc(&localTag, hal);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to write raw tag: %s\n", opt_error_str(err));
        rawWritePending_ = false;
        return false;
    }

    // Re-read and verify with retries
    Serial.println("NFCManager: Verifying raw write...");
    const int maxRetries = 3;
    for (int retry = 0; retry < maxRetries; retry++) {
        vTaskDelay(pdMS_TO_TICKS(50 * (retry + 1)));

        err = opt_read_from_nfc(&localTag, hal, 0, 78);
        if (err == OPT_OK) {
            err = opt_parse_ndef(&localTag);
            if (err == OPT_OK) {
                if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                    Serial.println("NFCManager: Could not acquire tagMutex after raw write verify");
                    rawWritePending_ = false;
                    return false;
                }
                currentSpool.tag_data = localTag;
                currentSpool.tag_data_valid = true;
                currentSpool.blank_tag_present = false;
                currentSpool.kind = TagKind::OpenPrintTag;
                lastSeenValid = false;  // Force re-detection on next scan
                addToRecentSpoolsLocked();
                sendOpenPrintTagMessage();
                xSemaphoreGive(tagMutex);

                rawWritePending_ = false;
                Serial.println("NFCManager: writeRawTag() complete - verified");
                return true;
            }
            Serial.printf("NFCManager: Parse failed on retry %d: %s\n", retry + 1, opt_error_str(err));
        } else {
            Serial.printf("NFCManager: Re-read failed on retry %d: %s\n", retry + 1, opt_error_str(err));
        }
    }

    // All retries failed - fall back to trusting in-memory data
    Serial.println("NFCManager: Verification retries exhausted, trusting in-memory data");
    err = opt_parse_ndef(&localTag);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to parse in-memory raw data: %s\n", opt_error_str(err));
        rawWritePending_ = false;
        return false;
    }

    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("NFCManager: Could not acquire tagMutex after raw write fallback");
        rawWritePending_ = false;
        return false;
    }
    currentSpool.tag_data = localTag;
    currentSpool.tag_data_valid = true;
    currentSpool.blank_tag_present = false;
    currentSpool.kind = TagKind::OpenPrintTag;
    lastSeenValid = false;
    addToRecentSpoolsLocked();
    sendOpenPrintTagMessage();
    xSemaphoreGive(tagMutex);

    rawWritePending_ = false;
    Serial.println("NFCManager: writeRawTag() complete - unverified");
    return true;
}

static bool isAuxOnlyWrite(const NFCWriteType type) {
    return type == NFCWriteType::REMOVE_WEIGHT ||
           type == NFCWriteType::SET_CONSUMED_WEIGHT ||
           type == NFCWriteType::WRITE_SPOOLMAN_ID;
}

static opt_error_t applyWriteUpdate(opt_tag_t& tag, const NFCWriteRequest& request) {
    switch (request.type) {
        case NFCWriteType::REMOVE_WEIGHT:
            return opt_add_consumed_weight(&tag, request.data.grams_to_remove);
        case NFCWriteType::CHANGE_COLOR:
            return opt_set_primary_color(&tag, request.data.new_color);
        case NFCWriteType::CHANGE_FILAMENT_TYPE:
            return opt_set_material_type(&tag, request.data.new_material_type);
        case NFCWriteType::SET_CONSUMED_WEIGHT:
            return opt_set_consumed_weight(&tag, request.data.consumed_weight);
        case NFCWriteType::SET_INITIAL_WEIGHT:
            return opt_set_actual_full_weight(&tag, request.data.consumed_weight);  // Reuse consumed_weight field for the initial weight value
        case NFCWriteType::SET_BRAND_NAME:
            return opt_set_brand_name(&tag, request.data.brand_name);
        case NFCWriteType::WRITE_SPOOLMAN_ID:
            return opt_set_gp_spoolman_id(&tag, request.data.spoolman_id);
        case NFCWriteType::SET_DENSITY:
            return opt_set_density(&tag, request.data.float_value);
        case NFCWriteType::SET_DIAMETER:
            return opt_set_filament_diameter(&tag, request.data.float_value);
        case NFCWriteType::SET_MATERIAL_NAME:
            return opt_set_material_name(&tag, request.data.material_name);
        case NFCWriteType::SET_MIN_PRINT_TEMP:
            return opt_set_min_print_temp(&tag, request.data.temp_celsius);
        case NFCWriteType::SET_MAX_PRINT_TEMP:
            return opt_set_max_print_temp(&tag, request.data.temp_celsius);
        case NFCWriteType::SET_PREHEAT_TEMP:
            return opt_set_preheat_temp(&tag, request.data.temp_celsius);
        case NFCWriteType::SET_MIN_BED_TEMP:
            return opt_set_min_bed_temp(&tag, request.data.temp_celsius);
        case NFCWriteType::SET_MAX_BED_TEMP:
            return opt_set_max_bed_temp(&tag, request.data.temp_celsius);
        default:
            return OPT_ERR_INVALID_PARAM;
    }
}

// ── Write helpers ────────────────────────────────────────────

// Build an NDEF TLV wrapper around a MIME-typed payload.
// Used by OpenTag3D and OpenSpool write paths. Pads to 4-byte page boundary.
// Returns total bytes written to outBuf, or 0 if buffer too small.
static uint16_t buildNdefTlv(const char* mimeType, const uint8_t* payload, uint16_t payloadLen,
                              uint8_t* outBuf, uint16_t outBufSize) {
    uint8_t mimeLen = (uint8_t)strlen(mimeType);
    bool sr = (payloadLen <= 255);                   // Short Record: 1-byte payload length
    uint8_t ndefHeaderSize = 2 + (sr ? 1 : 4);      // flags + typeLen + payloadLen(1 or 4)
    uint16_t ndefRecordLen = ndefHeaderSize + mimeLen + payloadLen;

    bool longTlv = (ndefRecordLen > 254);            // 3-byte TLV length if record > 254
    uint16_t tlvHeaderSize = 1 + (longTlv ? 3 : 1);
    uint16_t totalSize = tlvHeaderSize + ndefRecordLen + 1;  // +1 for 0xFE terminator
    uint16_t paddedSize = totalSize + ((4 - (totalSize % 4)) % 4);  // NTAG pages are 4 bytes

    if (paddedSize > outBufSize) return 0;

    uint16_t idx = 0;
    outBuf[idx++] = 0x03;
    if (longTlv) {
        outBuf[idx++] = 0xFF;
        outBuf[idx++] = (uint8_t)(ndefRecordLen >> 8);
        outBuf[idx++] = (uint8_t)(ndefRecordLen & 0xFF);
    } else {
        outBuf[idx++] = (uint8_t)ndefRecordLen;
    }

    uint8_t ndefFlags = 0xC0 | 0x02;  // MB + ME + TNF=media-type
    if (sr) ndefFlags |= 0x10;       // Short Record flag
    outBuf[idx++] = ndefFlags;
    outBuf[idx++] = mimeLen;
    if (sr) {
        outBuf[idx++] = (uint8_t)payloadLen;
    } else {
        outBuf[idx++] = (uint8_t)((payloadLen >> 24) & 0xFF);
        outBuf[idx++] = (uint8_t)((payloadLen >> 16) & 0xFF);
        outBuf[idx++] = (uint8_t)((payloadLen >> 8) & 0xFF);
        outBuf[idx++] = (uint8_t)(payloadLen & 0xFF);
    }

    memcpy(outBuf + idx, mimeType, mimeLen);
    idx += mimeLen;
    memcpy(outBuf + idx, payload, payloadLen);
    idx += payloadLen;
    outBuf[idx++] = 0xFE;
    while (idx < paddedSize) outBuf[idx++] = 0x00;
    return paddedSize;
}

bool NFCManager::validateWriteUid(const char* expectedUid, const char* writeType) {
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.printf("NFCManager: %s - could not acquire tagMutex\n", writeType);
        return false;
    }
    // Compare against the tag selected on the reader this scan cycle, not
    // currentSpool.spool_id — the scan-cooldown dedup can leave the latter
    // describing the previous tag while a different one is selected (#276).
    const bool ok = writeUidMatches(expectedUid, selectedUidHex_);
    xSemaphoreGive(tagMutex);
    if (!ok) {
        Serial.printf("NFCManager: %s rejected - UID mismatch (expected %s, selected %s)\n",
                      writeType, expectedUid,
                      selectedUidHex_[0] ? selectedUidHex_ : "<none>");
        return false;
    }
    return true;
}

void NFCManager::forceRescan() {
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        currentSpool.present = false;
        xSemaphoreGive(tagMutex);
    }
}

// ── Per-format write functions ──────────────────────────────

// Reject writes that exceed the tag's USER memory (requires prior GET_VERSION).
// Bounds against ntagUserMemoryEnd, not ntagUsablePages — the last pages of the
// die are dynamic-lock/CFG/PWD, and payload bytes written there can leave the
// tag password-protected with a password nobody knows. For an unidentified
// variant we fall back to the CC-declared size captured at classify time; the
// historical page<64 ceiling now applies only when the CC is also absent or
// invalid, so an honest clone gets its real capacity instead.
bool NFCManager::checkWriteCapacity(uint8_t startPage, uint8_t pageCount, const char* writeType) {
    uint16_t maxPages = effectiveUserMemoryEnd(currentSpool.variant, currentSpool.cc_user_end);
    if (maxPages == 0) maxPages = 64;
    if (startPage + pageCount > maxPages) {
        Serial.printf("NFCManager: %s rejected — needs %d pages (start=%d), tag has %d (%s)\n",
            writeType, pageCount, startPage, maxPages, ntagVariantName(currentSpool.variant));
        LogBuffer::getInstance().logPrintf("%s rejected: needs %d pages, tag has %d (%s)\n",
            writeType, pageCount, maxPages, ntagVariantName(currentSpool.variant));
        return false;
    }
    return true;
}

// Re-read a written region and compare — a tag can ACK a write that lands
// corrupted at weak coupling, and the 4-bit ACK carries no CRC so a noise
// byte can even read as success. Read goes straight to hardware (no cache on
// the ISO14443 path). (#167)
bool NFCManager::verifyISO14443Readback(uint8_t startPage, uint8_t pageCount, const uint8_t* expected) {
    uint8_t readback[256];
    uint16_t totalBytes = (uint16_t)pageCount * 4;
    if (totalBytes > sizeof(readback)) return false;
    uint16_t n = connection_->readISO14443Pages(startPage, pageCount, readback, sizeof(readback), false);
    if (n < totalBytes) {
        Serial.printf("NFCManager: verify read returned %u of %u bytes\n", n, totalBytes);
        return false;
    }
    return memcmp(readback, expected, totalBytes) == 0;
}

// Shared verify + one-recovery tail for the NDEF writers (OpenTag3D, OpenSpool).
bool NFCManager::ndefVerifyAndFinish(const char* what, uint8_t pagesNeeded,
                                     const uint8_t* ndefBuf, uint16_t ndefLen) {
    if (verifyISO14443Readback(4, pagesNeeded, ndefBuf)) {
        Serial.printf("NFCManager: %s succeeded (%u bytes, %u pages, verified)\n", what, ndefLen, pagesNeeded);
        LogBuffer::getInstance().logPrintf("Write %s: OK (%u bytes, verified)\n", what, ndefLen);
        forceRescan();
        return true;
    }

    Serial.printf("NFCManager: %s verify mismatch — rewrite retry\n", what);
    LogBuffer::getInstance().logPrintf("Write %s: verify mismatch, rewriting\n", what);
    if (connection_->writeISO14443Pages(4, pagesNeeded, ndefBuf, ndefLen)
            && verifyISO14443Readback(4, pagesNeeded, ndefBuf)) {
        Serial.printf("NFCManager: %s succeeded (recovery rewrite, verified)\n", what);
        LogBuffer::getInstance().logPrintf("Write %s: OK (rewrite, verified)\n", what);
        forceRescan();
        return true;
    }

    Serial.printf("NFCManager: %s verify failed after rewrite\n", what);
    LogBuffer::getInstance().logPrintf("Write %s: FAILED (verify)\n", what);
    return false;
}

// Shared tail for every successful TigerTag write path: verify, recover once
// with a full rewrite on mismatch, then fail loudly.
bool NFCManager::tigertagVerifyAndFinish(const NFCWriteRequest& request, const char* how) {
    if (verifyISO14443Readback(4, 10, request.data.tigertag_data)) {
        Serial.printf("NFCManager: WRITE_TIGERTAG succeeded (%s, verified)\n", how);
        LogBuffer::getInstance().logPrintf("Write TigerTag: OK (%s, verified)\n", how);
        forceRescan();
        return true;
    }

    Serial.println("NFCManager: WRITE_TIGERTAG verify mismatch — full rewrite retry");
    LogBuffer::getInstance().logPrintf("Write TigerTag: verify mismatch, rewriting\n");
    if (connection_->writeISO14443Pages(4, 10, request.data.tigertag_data, 40)
            && verifyISO14443Readback(4, 10, request.data.tigertag_data)) {
        Serial.println("NFCManager: WRITE_TIGERTAG succeeded (recovery rewrite, verified)");
        LogBuffer::getInstance().logPrintf("Write TigerTag: OK (rewrite, verified)\n");
        forceRescan();
        return true;
    }

    Serial.println("NFCManager: WRITE_TIGERTAG verify failed after rewrite");
    LogBuffer::getInstance().logPrintf("Write TigerTag: FAILED (verify)\n");
    return false;
}

bool NFCManager::executeTigerTagWrite(const NFCWriteRequest& request) {
    if (!validateWriteUid(request.expected_spool_id, "WRITE_TIGERTAG")) return false;
    if (!checkWriteCapacity(4, 10, "WRITE_TIGERTAG")) return false;

    // Pre-read current tag contents so we can write only the pages that actually changed.
    // TigerTag has a fixed binary layout across pages 4–13, so byte offsets never shift
    // and page-granular partial writes are safe.
    uint8_t current[40];
    uint16_t bytesRead = connection_->readISO14443Pages(4, 10, current, sizeof(current), true);
    if (bytesRead < 40) {
        Serial.printf("NFCManager: WRITE_TIGERTAG pre-read returned %u bytes, falling back to full write\n",
                      bytesRead);
        LogBuffer::getInstance().logPrintf("Write TigerTag: pre-read failed, full write\n");
        if (!connection_->writeISO14443Pages(4, 10, request.data.tigertag_data, 40)) {
            Serial.println("NFCManager: WRITE_TIGERTAG failed");
            LogBuffer::getInstance().logPrintf("Write TigerTag: FAILED\n");
            return false;
        }
        return tigertagVerifyAndFinish(request, "full write");
    }

    // Build contiguous runs of changed pages. 10 pages alternating → at most 5 runs.
    struct PageRun { uint8_t startPage; uint8_t pageCount; };
    PageRun runs[5];
    uint8_t runCount = 0;
    bool inRun = false;

    for (uint8_t i = 0; i < 10; ++i) {
        bool changed = memcmp(&request.data.tigertag_data[i * 4], &current[i * 4], 4) != 0;
        if (changed) {
            if (!inRun) {
                runs[runCount].startPage = 4 + i;
                runs[runCount].pageCount = 1;
                inRun = true;
            } else {
                runs[runCount].pageCount++;
            }
        } else if (inRun) {
            runCount++;
            inRun = false;
        }
    }
    if (inRun) runCount++;

    if (runCount == 0) {
        Serial.println("NFCManager: WRITE_TIGERTAG no changes, skipping write");
        LogBuffer::getInstance().logPrintf("Write TigerTag: no changes\n");
        return true;
    }

    uint8_t totalPages = 0;
    for (uint8_t r = 0; r < runCount; ++r) {
        const PageRun& run = runs[r];
        const uint8_t* data = request.data.tigertag_data + (run.startPage - 4) * 4;
        if (!connection_->writeISO14443Pages(run.startPage, run.pageCount, data, run.pageCount * 4)) {
            Serial.printf("NFCManager: WRITE_TIGERTAG run %u failed (page %u, %u pages), attempting full rewrite\n",
                          r, run.startPage, run.pageCount);
            LogBuffer::getInstance().logPrintf("Write TigerTag: run failed, full rewrite\n");
            if (connection_->writeISO14443Pages(4, 10, request.data.tigertag_data, 40)) {
                return tigertagVerifyAndFinish(request, "full rewrite");
            }
            Serial.println("NFCManager: WRITE_TIGERTAG full rewrite failed");
            LogBuffer::getInstance().logPrintf("Write TigerTag: FAILED\n");
            return false;
        }
        totalPages += run.pageCount;
    }

    Serial.printf("NFCManager: WRITE_TIGERTAG wrote %u/10 pages in %u run(s)\n", totalPages, runCount);
    return tigertagVerifyAndFinish(request, "partial write");
}

bool NFCManager::executeOpenTag3DWrite(const NFCWriteRequest& request) {
    if (!validateWriteUid(request.expected_spool_id, "WRITE_OPENTAG3D")) return false;

    if (!rawWritePending_ || rawWriteBufferSize_ < sizeof(opentag3d_t)) {
        Serial.println("NFCManager: WRITE_OPENTAG3D - no raw data available");
        return false;
    }

    opentag3d_t ot3d;
    memcpy(&ot3d, rawWriteBuffer_, sizeof(opentag3d_t));
    rawWritePending_ = false;

    size_t encodeSize = (opentag3d_major(ot3d.tag_version) >= 2) ? OT3D_V2_MAP_SIZE
                    : (ot3d.has_extended ? OT3D_EXTENDED_MIN : OT3D_CORE_SIZE);
    uint8_t payloadBuf[OT3D_V2_MAP_SIZE];
    int payloadLen = opentag3d_encode(&ot3d, payloadBuf, encodeSize);
    if (payloadLen <= 0) {
        Serial.println("NFCManager: WRITE_OPENTAG3D - encode failed");
        return false;
    }

    uint8_t ndefBuf[256];
    uint16_t ndefLen = buildNdefTlv(OT3D_MIME_TYPE, payloadBuf, (uint16_t)payloadLen, ndefBuf, sizeof(ndefBuf));
    if (ndefLen == 0) {
        Serial.printf("NFCManager: WRITE_OPENTAG3D - NDEF too large\n");
        return false;
    }

    uint8_t pagesNeeded = (uint8_t)(ndefLen / 4);
    Serial.printf("NFCManager: WRITE_OPENTAG3D - writing %u bytes (%u pages)\n", ndefLen, pagesNeeded);
    if (!checkWriteCapacity(4, pagesNeeded, "WRITE_OPENTAG3D")) return false;
    if (!connection_->writeISO14443Pages(4, pagesNeeded, ndefBuf, ndefLen)) {
        Serial.println("NFCManager: WRITE_OPENTAG3D failed");
        LogBuffer::getInstance().logPrintf("Write OpenTag3D: FAILED\n");
        return false;
    }
    return ndefVerifyAndFinish("OpenTag3D", pagesNeeded, ndefBuf, ndefLen);
}

bool NFCManager::executeOpenSpoolWrite(const NFCWriteRequest& request) {
    if (!validateWriteUid(request.expected_spool_id, "WRITE_OPENSPOOL")) return false;

    if (!rawWritePending_ || rawWriteBufferSize_ == 0) {
        Serial.println("NFCManager: WRITE_OPENSPOOL - no raw data available");
        return false;
    }

    const uint8_t* jsonPayload = rawWriteBuffer_;
    uint16_t payloadLen = (uint16_t)rawWriteBufferSize_;
    rawWritePending_ = false;

    uint8_t ndefBuf[256];
    uint16_t ndefLen = buildNdefTlv("application/json", jsonPayload, payloadLen, ndefBuf, sizeof(ndefBuf));
    if (ndefLen == 0) {
        Serial.printf("NFCManager: WRITE_OPENSPOOL - NDEF too large\n");
        return false;
    }

    uint8_t pagesNeeded = (uint8_t)(ndefLen / 4);
    Serial.printf("NFCManager: WRITE_OPENSPOOL - writing %u bytes (%u pages)\n", ndefLen, pagesNeeded);
    if (!checkWriteCapacity(4, pagesNeeded, "WRITE_OPENSPOOL")) return false;
    if (!connection_->writeISO14443Pages(4, pagesNeeded, ndefBuf, ndefLen)) {
        Serial.println("NFCManager: WRITE_OPENSPOOL failed");
        LogBuffer::getInstance().logPrintf("Write OpenSpool: FAILED\n");
        return false;
    }
    return ndefVerifyAndFinish("OpenSpool", pagesNeeded, ndefBuf, ndefLen);
}

bool NFCManager::executeAtomicWrite(const NFCWriteRequest& request) {
    if (!atomicWriteFields_.pending) {
        Serial.println("NFCManager: WRITE_ATOMIC - no atomic fields pending");
        return false;
    }

    AtomicWriteFields f = atomicWriteFields_;
    atomicWriteFields_.pending = false;

    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("NFCManager: WRITE_ATOMIC - could not acquire tagMutex");
        return false;
    }
    if (!currentSpool.tag_data_valid) {
        xSemaphoreGive(tagMutex);
        return false;
    }
    // Atomic writes rebuild the tag from currentSpool's CBOR, so the bound UID
    // must match both the selected tag and the tag that data came from (#276).
    if (!fieldUpdateWriteAllowed(request.expected_spool_id, selectedUidHex_, currentSpool.spool_id)) {
        Serial.printf("NFCManager: WRITE_ATOMIC rejected - UID mismatch (expected %s, selected %s, spool %s)\n",
            request.expected_spool_id,
            selectedUidHex_[0] ? selectedUidHex_ : "<none>", currentSpool.spool_id);
        xSemaphoreGive(tagMutex);
        return false;
    }

    uint8_t  cur_mat_type = 0;     opt_get_material_type(&currentSpool.tag_data, &cur_mat_type);
    uint8_t  cur_color[4] = {255,255,255,255}; opt_get_primary_color(&currentSpool.tag_data, cur_color);
    float    cur_full_wt = 1000.0f; opt_get_actual_full_weight(&currentSpool.tag_data, &cur_full_wt);
    float    cur_consumed = 0.0f;   opt_get_consumed_weight(&currentSpool.tag_data, &cur_consumed);
    char     cur_brand[33] = {0};   opt_get_brand_name(&currentSpool.tag_data, cur_brand, sizeof(cur_brand));
    float    cur_density = 0.0f;    opt_get_density(&currentSpool.tag_data, &cur_density);
    float    cur_diameter = 0.0f;   opt_get_filament_diameter(&currentSpool.tag_data, &cur_diameter);
    char     cur_mat_name[33] = {0}; opt_get_material_name(&currentSpool.tag_data, cur_mat_name, sizeof(cur_mat_name));
    int32_t  cur_sm_id = -1;        opt_get_gp_spoolman_id(&currentSpool.tag_data, &cur_sm_id);
    int16_t  cur_min_pt = 0, cur_max_pt = 0, cur_pre_t = 0, cur_min_bt = 0, cur_max_bt = 0;
    opt_get_min_print_temp(&currentSpool.tag_data, &cur_min_pt);
    opt_get_max_print_temp(&currentSpool.tag_data, &cur_max_pt);
    opt_get_preheat_temp(&currentSpool.tag_data, &cur_pre_t);
    opt_get_min_bed_temp(&currentSpool.tag_data, &cur_min_bt);
    opt_get_max_bed_temp(&currentSpool.tag_data, &cur_max_bt);
    xSemaphoreGive(tagMutex);

    opt_init(&writeScratchTag_);
    opt_error_t err = opt_format_empty_tag(&writeScratchTag_, 312, 32);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: WRITE_ATOMIC format failed: %s\n", opt_error_str(err));
        return false;
    }

    opt_set_material_class(&writeScratchTag_, OPT_MATERIAL_CLASS_FFF);

    #define ATOMIC_SET(name, call) do { \
        err = (call); \
        if (err != OPT_OK) { \
            Serial.printf("NFCManager: WRITE_ATOMIC " name " failed: %s\n", opt_error_str(err)); \
        } \
    } while(0)

    ATOMIC_SET("material_type",  opt_set_material_type(&writeScratchTag_, f.has_material_type ? f.material_type : cur_mat_type));
    ATOMIC_SET("color",          opt_set_primary_color(&writeScratchTag_, f.has_color ? f.color : cur_color));
    ATOMIC_SET("initial_weight", opt_set_actual_full_weight(&writeScratchTag_, f.has_initial_weight ? f.initial_weight_g : cur_full_wt));
    ATOMIC_SET("consumed_weight",opt_set_consumed_weight(&writeScratchTag_, f.has_consumed_weight ? f.consumed_weight : cur_consumed));
    if (f.has_brand_name || cur_brand[0] != '\0')
        ATOMIC_SET("brand_name", opt_set_brand_name(&writeScratchTag_, f.has_brand_name ? f.brand_name : cur_brand));
    if (f.has_density || cur_density > 0.0f)
        ATOMIC_SET("density",    opt_set_density(&writeScratchTag_, f.has_density ? f.density : cur_density));
    if (f.has_diameter || cur_diameter > 0.0f)
        ATOMIC_SET("diameter",   opt_set_filament_diameter(&writeScratchTag_, f.has_diameter ? f.diameter_mm : cur_diameter));
    if (f.has_material_name || cur_mat_name[0] != '\0')
        ATOMIC_SET("material_name", opt_set_material_name(&writeScratchTag_, f.has_material_name ? f.material_name : cur_mat_name));
    if (f.has_min_print_temp || cur_min_pt != 0)
        ATOMIC_SET("min_print_temp", opt_set_min_print_temp(&writeScratchTag_, f.has_min_print_temp ? f.min_print_temp : cur_min_pt));
    if (f.has_max_print_temp || cur_max_pt != 0)
        ATOMIC_SET("max_print_temp", opt_set_max_print_temp(&writeScratchTag_, f.has_max_print_temp ? f.max_print_temp : cur_max_pt));
    if (f.has_preheat_temp || cur_pre_t != 0)
        ATOMIC_SET("preheat_temp",   opt_set_preheat_temp(&writeScratchTag_, f.has_preheat_temp ? f.preheat_temp : cur_pre_t));
    if (f.has_min_bed_temp || cur_min_bt != 0)
        ATOMIC_SET("min_bed_temp",   opt_set_min_bed_temp(&writeScratchTag_, f.has_min_bed_temp ? f.min_bed_temp : cur_min_bt));
    if (f.has_max_bed_temp || cur_max_bt != 0)
        ATOMIC_SET("max_bed_temp",   opt_set_max_bed_temp(&writeScratchTag_, f.has_max_bed_temp ? f.max_bed_temp : cur_max_bt));
    int32_t sm_id = f.has_spoolman_id ? f.spoolman_id : cur_sm_id;
    if (sm_id > 0)
        ATOMIC_SET("spoolman_id",    opt_set_gp_spoolman_id(&writeScratchTag_, sm_id));
    #undef ATOMIC_SET

    Serial.println("NFCManager: WRITE_ATOMIC writing to NFC...");
    opt_nfc_hal_t* hal = connection_->getHal();
    err = opt_write_to_nfc(&writeScratchTag_, hal);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: WRITE_ATOMIC NFC write failed: %s\n", opt_error_str(err));
        return false;
    }

    bool verified = false;
    for (int retry = 0; retry < 3; retry++) {
        vTaskDelay(pdMS_TO_TICKS(50 * (retry + 1)));
        err = opt_read_from_nfc(&writeScratchTag_, hal, 0, 78);
        if (err == OPT_OK) {
            err = opt_parse_ndef(&writeScratchTag_);
            if (err == OPT_OK) { verified = true; break; }
        }
        Serial.printf("NFCManager: WRITE_ATOMIC verify retry %d: %s\n", retry + 1, opt_error_str(err));
    }
    if (!verified) {
        Serial.println("NFCManager: WRITE_ATOMIC verification failed");
        return false;
    }

    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("NFCManager: WRITE_ATOMIC succeeded but commit lock failed");
        return false;
    }
    currentSpool.tag_data = writeScratchTag_;
    currentSpool.tag_data_valid = true;
    currentSpool.blank_tag_present = false;
    currentSpool.kind = TagKind::OpenPrintTag;
    addToRecentSpoolsLocked();
    sendOpenPrintTagMessage();
    xSemaphoreGive(tagMutex);

    Serial.println("NFCManager: WRITE_ATOMIC complete");
    return true;
}

// ── executeWrite dispatcher ─────────────────────────────────

bool NFCManager::executeWrite(const NFCWriteRequest& request) {
    if (request.type == NFCWriteType::FORMAT_NEW) {
        if (!validateWriteUid(request.expected_spool_id, "FORMAT_NEW")) return false;
        return formatNewSpool();
    }

    if (request.type == NFCWriteType::WRITE_RAW_TAG) {
        if (!rawWritePending_) {
            Serial.println("NFCManager: WRITE_RAW_TAG - no raw data pending");
            return false;
        }
        if (!validateWriteUid(request.expected_spool_id, "WRITE_RAW_TAG")) {
            rawWritePending_ = false;
            return false;
        }
        return writeRawTag();
    }

    if (request.type == NFCWriteType::WRITE_TIGERTAG)  return executeTigerTagWrite(request);
    if (request.type == NFCWriteType::WRITE_OPENTAG3D) return executeOpenTag3DWrite(request);
    if (request.type == NFCWriteType::WRITE_OPENSPOOL)  return executeOpenSpoolWrite(request);
    if (request.type == NFCWriteType::WRITE_ATOMIC)     return executeAtomicWrite(request);

    // OpenPrintTag field-update writes (REMOVE_WEIGHT, CHANGE_COLOR, etc.)
    // These modify existing CBOR tag data in-place
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("NFCManager: executeWrite - could not acquire tagMutex");
        return false;
    }

    if (!currentSpool.tag_data_valid) {
        xSemaphoreGive(tagMutex);
        return false;
    }

    // Field updates mutate currentSpool's CBOR in place, so the bound UID must
    // match both the selected tag and the tag that data came from (#276).
    if (!fieldUpdateWriteAllowed(request.expected_spool_id, selectedUidHex_, currentSpool.spool_id)) {
        Serial.printf("NFCManager: write rejected - UID mismatch (expected %s, selected %s, spool %s)\n",
            request.expected_spool_id,
            selectedUidHex_[0] ? selectedUidHex_ : "<none>", currentSpool.spool_id);
        xSemaphoreGive(tagMutex);
        return false;
    }

    // Snapshot current tag into reusable scratch storage under mutex; do not mutate shared state until write succeeds.
    writeScratchTag_ = currentSpool.tag_data;
    xSemaphoreGive(tagMutex);

    // Preflight update on a copy to detect overflow before touching shared state.
    opt_tag_t& updatedTag = writeScratchTag_;
    opt_error_t err = applyWriteUpdate(updatedTag, request);
    if (err != OPT_OK) {
        if (err == OPT_ERR_REGION_OVERFLOW) {
            Serial.println("NFCManager: Region overflow during preflight update; keeping original tag data");
        } else {
            Serial.printf("NFCManager: Failed to update in-memory tag data: %s\n", opt_error_str(err));
        }
        return false;
    }

    // NFC I/O without mutex — only scan task touches the hardware
    opt_nfc_hal_t* hal = connection_->getHal();
    const bool auxOnlyWrite = isAuxOnlyWrite(request.type);
    if (auxOnlyWrite) {
        err = opt_write_aux_region(&updatedTag, hal);
    } else {
        err = opt_write_dirty_pages(&updatedTag, hal);
    }

    if (err != OPT_OK) {
        Serial.printf("NFCManager: Write failed (%s), attempting raw fallback\n", opt_error_str(err));

        // Fallback: write full binary image from updatedTag directly.
        // This avoids nesting another large opt_tag_t on NFCScanTask stack.
        err = opt_write_to_nfc(&updatedTag, hal);
        if (err == OPT_OK) {
            const int maxRetries = 3;
            for (int retry = 0; retry < maxRetries; retry++) {
                vTaskDelay(pdMS_TO_TICKS(50 * (retry + 1)));
                err = opt_read_from_nfc(&updatedTag, hal, 0, 78);
                if (err == OPT_OK) {
                    err = opt_parse_ndef(&updatedTag);
                    if (err == OPT_OK) {
                        break;
                    }
                }
            }
            if (err == OPT_OK) {
                if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                    Serial.println("NFCManager: executeWrite fallback succeeded but commit lock failed");
                    return false;
                }
                currentSpool.tag_data = updatedTag;
                currentSpool.tag_data_valid = true;
                currentSpool.blank_tag_present = false;
                currentSpool.kind = TagKind::OpenPrintTag;
                lastSeenValid = false;
                addToRecentSpoolsLocked();
                sendOpenPrintTagMessage();
                xSemaphoreGive(tagMutex);
                return true;
            }
        }

        Serial.printf("NFCManager: Raw fallback failed (%s)\n", opt_error_str(err));
        return false;
    }

    // Commit the successful update to shared state.
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("NFCManager: executeWrite - write succeeded but commit lock failed");
        return false;
    }

    currentSpool.tag_data = updatedTag;
    currentSpool.tag_data_valid = true;
    currentSpool.blank_tag_present = false;
    currentSpool.kind = TagKind::OpenPrintTag;
    xSemaphoreGive(tagMutex);

    switch (request.type) {
        case NFCWriteType::REMOVE_WEIGHT:
            Serial.printf("NFCManager: Removed %.2f grams from spool\n", request.data.grams_to_remove);
            break;
        case NFCWriteType::CHANGE_COLOR:
            Serial.printf("NFCManager: Changed color to RGBA(%u,%u,%u,%u)\n",
                request.data.new_color[0], request.data.new_color[1],
                request.data.new_color[2], request.data.new_color[3]);
            break;
        case NFCWriteType::CHANGE_FILAMENT_TYPE:
            Serial.printf("NFCManager: Changed material type to %u\n", request.data.new_material_type);
            break;
        case NFCWriteType::SET_CONSUMED_WEIGHT:
            Serial.printf("NFCManager: Set consumed weight to %.2f grams\n", request.data.consumed_weight);
            break;
        case NFCWriteType::SET_INITIAL_WEIGHT:
            Serial.printf("NFCManager: Set initial weight to %.2f grams\n", request.data.consumed_weight);
            break;
        case NFCWriteType::SET_BRAND_NAME:
            Serial.printf("NFCManager: Set brand name to %s\n", request.data.brand_name);
            break;
        case NFCWriteType::WRITE_SPOOLMAN_ID:
            Serial.printf("NFCManager: Wrote spoolman ID %d to tag\n", request.data.spoolman_id);
            break;
        case NFCWriteType::SET_DENSITY:
            Serial.printf("NFCManager: Set density to %.3f g/cm3\n", request.data.float_value);
            break;
        case NFCWriteType::SET_DIAMETER:
            Serial.printf("NFCManager: Set diameter to %.2f mm\n", request.data.float_value);
            break;
        case NFCWriteType::SET_MATERIAL_NAME:
            Serial.printf("NFCManager: Set material name to %s\n", request.data.material_name);
            break;
        case NFCWriteType::SET_MIN_PRINT_TEMP:
            Serial.printf("NFCManager: Set min print temp to %d C\n", request.data.temp_celsius);
            break;
        case NFCWriteType::SET_MAX_PRINT_TEMP:
            Serial.printf("NFCManager: Set max print temp to %d C\n", request.data.temp_celsius);
            break;
        case NFCWriteType::SET_PREHEAT_TEMP:
            Serial.printf("NFCManager: Set preheat temp to %d C\n", request.data.temp_celsius);
            break;
        case NFCWriteType::SET_MIN_BED_TEMP:
            Serial.printf("NFCManager: Set min bed temp to %d C\n", request.data.temp_celsius);
            break;
        case NFCWriteType::SET_MAX_BED_TEMP:
            Serial.printf("NFCManager: Set max bed temp to %d C\n", request.data.temp_celsius);
            break;
        default:
            break;
    }

    return true;
}

void NFCManager::sendSpoolUpdatedMessage(uint32_t request_id, NFCWriteType type, bool success) {
    (void)request_id;

    AppMessage msg;
    msg.type = AppMessageType::SPOOL_UPDATED;

    strncpy(msg.payload.spoolUpdated.spool_id, currentSpool.spool_id,
            sizeof(msg.payload.spoolUpdated.spool_id) - 1);
    msg.payload.spoolUpdated.spool_id[sizeof(msg.payload.spoolUpdated.spool_id) - 1] = '\0';

    msg.payload.spoolUpdated.update_type = static_cast<uint8_t>(type);
    msg.payload.spoolUpdated.success = success;
    msg.payload.spoolUpdated.suppress_sync = 0;  // Default: allow sync

    // Get remaining weight from tag data under mutex to prevent torn reads
    float remaining_grams = 0;
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (currentSpool.tag_data_valid) {
            opt_get_remaining_weight(&currentSpool.tag_data, &remaining_grams);
        }
        xSemaphoreGive(tagMutex);
    }
    msg.payload.spoolUpdated.kg_remaining = remaining_grams / 1000.0f;

    ApplicationManager::getInstance().sendMessage(msg);
}

bool NFCManager::isRequestCompleted(uint32_t request_id) {
    if (completedMutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(completedMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool found = false;
    for (size_t i = 0; i < COMPLETED_REQUESTS_SIZE; i++) {
        if (completedRequests[i] == request_id) {
            found = true;
            break;
        }
    }

    xSemaphoreGive(completedMutex);
    return found;
}

void NFCManager::markRequestCompleted(uint32_t request_id) {
    if (completedMutex == nullptr) {
        return;
    }

    if (xSemaphoreTake(completedMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    completedRequests[completedRequestsIndex] = request_id;
    completedRequestsIndex = (completedRequestsIndex + 1) % COMPLETED_REQUESTS_SIZE;

    xSemaphoreGive(completedMutex);
}

bool NFCManager::isDuplicateSpool(const uint8_t* uid, uint8_t uid_length) {
    // Benign race: reads lastSeenValid/lastSeenUid without mutex. These are only
    // written by the scan task (same task calling this), so no torn reads.
    // requestCurrentSpool() can clear lastSeenValid, but worst case we do
    // one extra NFC read — no correctness issue.

    // Even if lastSeenValid is false (cleared by setupRF recovery), check if the
    // current spool UID matches — avoids re-processing the same tag as GenericUid
    // after a TigerTag or OpenPrintTag was already parsed successfully.
    if (!lastSeenValid && currentSpool.present &&
        uid_length == currentSpool.uid_length &&
        memcmp(uid, currentSpool.uid, uid_length) == 0) {
        return true;
    }

    // Cooldown: suppress re-reads within 3s where first 3 bytes match.
    // Catches PN5180 partial UID reads (e.g., 04A651FFFFFFFF) that happen
    // when the tag is re-detected after a brief communication glitch.
    if (lastSeenUidLength > 0 && uid_length >= 3 && lastSeenUidLength >= 3 &&
        (millis() - lastSeenMs) < SCAN_COOLDOWN_MS &&
        memcmp(uid, lastSeenUid, 3) == 0) {
        Serial.println("NFCManager: Suppressed re-read (cooldown, partial UID match)");
        return true;
    }

    if (!lastSeenValid) {
        return false;
    }

    if (uid_length != lastSeenUidLength) {
        return false;
    }

    return memcmp(uid, lastSeenUid, uid_length) == 0;
}

void NFCManager::requestCurrentSpool() {
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        lastSeenValid = false;
        memset(lastSeenUid, 0, sizeof(lastSeenUid));
        lastSeenUidLength = 0;
        xSemaphoreGive(tagMutex);
    }
}

// Caller must hold tagMutex. FreeRTOS mutexes are not recursive and every
// call site already holds it around the currentSpool update this reads, so
// taking it here only ever timed out — the history silently stopped updating
// and each scan paid the timeout. Running under the caller's lock also closes
// the race against getRecentSpools(), which reads the array under the mutex
// while the tail of this function used to rewrite it unlocked.
void NFCManager::addToRecentSpoolsLocked() {
    if (!currentSpool.tag_data_valid) return;

    // Check if this spool already exists in recent list
    int existingIndex = -1;
    for (size_t i = 0; i < recentSpoolsCount; i++) {
        if (strcmp(recentSpools[i].spool_id, currentSpool.spool_id) == 0) {
            existingIndex = i;
            break;
        }
    }

    // Snapshot data under mutex
    RecentSpoolEntry newEntry;
    memset(&newEntry, 0, sizeof(newEntry));
    strncpy(newEntry.spool_id, currentSpool.spool_id, sizeof(newEntry.spool_id) - 1);

    opt_get_material_type(&currentSpool.tag_data, &newEntry.material_type);
    opt_get_primary_color(&currentSpool.tag_data, newEntry.color);

    if (opt_get_brand_name(&currentSpool.tag_data, newEntry.manufacturer, sizeof(newEntry.manufacturer)) != OPT_OK) {
        newEntry.manufacturer[0] = '\0';
    }

    float full_weight = 0.0f, consumed = 0.0f;
    opt_get_actual_full_weight(&currentSpool.tag_data, &full_weight);
    opt_get_consumed_weight(&currentSpool.tag_data, &consumed);
    newEntry.grams_remaining = (int)(full_weight - consumed);

    newEntry.last_seen = time(nullptr);
    newEntry.valid = true;
    newEntry.synced_to_spoolman = false;

    int32_t smId = -1;
    opt_get_gp_spoolman_id(&currentSpool.tag_data, &smId);
    newEntry.spoolman_id = smId;

    if (existingIndex >= 0) {
        // Spool exists - shift entries to remove it from current position
        for (size_t i = existingIndex; i > 0; i--) {
            recentSpools[i] = recentSpools[i - 1];
        }
    } else {
        // New spool - shift all entries down to make room
        size_t shiftCount = (recentSpoolsCount < MAX_RECENT_SPOOLS) ? recentSpoolsCount : (MAX_RECENT_SPOOLS - 1);
        for (size_t i = shiftCount; i > 0; i--) {
            recentSpools[i] = recentSpools[i - 1];
        }
        if (recentSpoolsCount < MAX_RECENT_SPOOLS) {
            recentSpoolsCount++;
        }
    }

    // Place new entry at front
    recentSpools[0] = newEntry;
}

bool NFCManager::scanOnce() {
    uint8_t uid[8];
    uint8_t uidLength = 0;
    bool result = false;

    connection_->reset();
    connection_->setupRF();

    if (connection_->detectTag(uid, &uidLength)) {
        connection_->setCurrentUid(uid, uidLength);
        recordSelectedUid(uid, uidLength);

        // Check if this is the same spool or if re-detection is suppressed
        bool shouldSkipReRead = isDuplicateSpool(uid, uidLength);

        if (suppressReDetection_) {
            char uidHex[17];
            for (uint8_t i = 0; i < uidLength && i < 8; i++) {
                sprintf(uidHex + (i * 2), "%02X", uid[i]);
            }
            uidHex[uidLength * 2] = '\0';

            if (strcmp(uidHex, suppressReDetectionUid_) == 0) {
                shouldSkipReRead = true;
            }
        }

        if (!shouldSkipReRead) {
            // readAndParseTag manages its own mutex internally
            if (readAndParseTag(uid, uidLength)) {
                result = true;
            } else {
// Blank tag detected — take mutex briefly for state update
bool blankStateCaptured = false;
if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (uint8_t i = 0; i < uidLength && i < 8; i++) {
        sprintf(currentSpool.spool_id + (i * 2), "%02X", uid[i]);
    }
    currentSpool.spool_id[uidLength * 2] = '\0';
    memcpy(currentSpool.uid, uid, uidLength);
    currentSpool.uid_length = uidLength;
    currentSpool.present = true;
    currentSpool.tag_data_valid = false;
    currentSpool.blank_tag_present = true;
    currentSpool.kind = TagKind::BlankTag;
    currentSpool.variant = NtagVariant::Unknown;  // ISO15693 — a previous ISO14443 tag's
    currentSpool.cc_user_end = 0;                 // identity must not leak into size gates
    memcpy(lastSeenUid, uid, uidLength);
    lastSeenUidLength = uidLength;
    lastSeenValid = true;
    lastSeenMs = millis();
    blankStateCaptured = true;
    xSemaphoreGive(tagMutex);
}

if (blankStateCaptured) {
    sendBlankTagMessage();
}
            }
        }
        processWriteQueue();
    } else {
        if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (lastSeenValid) {
                sendTagRemovedMessage();
            }
            currentSpool.present = false;
            currentSpool.blank_tag_present = false;
            currentSpool.cc_user_end = 0;
            lastSeenValid = false;

            // Clear suppression if tag removed
            suppressReDetection_ = false;
            suppressReDetectionUid_[0] = '\0';
            clearSelectedUid();

            xSemaphoreGive(tagMutex);
        }
    }
    return result;
}

size_t NFCManager::getRecentSpools(RecentSpoolEntry* entries, size_t maxEntries) {
    if (tagMutex == nullptr) {
        return 0;
    }

    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }

    size_t count = 0;
    for (size_t i = 0; i < recentSpoolsCount && count < maxEntries; i++) {
        if (recentSpools[i].valid) {
            // Skip the current spool if it matches
            if (currentSpool.present && strcmp(recentSpools[i].spool_id, currentSpool.spool_id) == 0) {
                continue;
            }
            entries[count++] = recentSpools[i];
        }
    }

    xSemaphoreGive(tagMutex);
    return count;
}

void NFCManager::updateRecentSpoolSyncStatus(const char* spool_id, bool synced) {
    if (tagMutex == nullptr || spool_id == nullptr) {
        return;
    }

    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    for (size_t i = 0; i < recentSpoolsCount; i++) {
        if (recentSpools[i].valid && strcmp(recentSpools[i].spool_id, spool_id) == 0) {
            recentSpools[i].synced_to_spoolman = synced;
            break;
        }
    }

    xSemaphoreGive(tagMutex);
}

bool NFCManager::isWriteQueueEmpty() const {
    if (!writeQueue) return true;
    return uxQueueMessagesWaiting(writeQueue) == 0;
}

bool NFCManager::readBambuTag(const uint8_t* uid, uint8_t uidLength, BambuTagData& out) {
    BambuKeys keys = deriveBambuKeys(uid, uidLength);
    // Zero-initialized: a block that never reads must not reach the parser as
    // stack garbage (it would publish random colour/weight/temperature).
    uint8_t blocks[BAMBU_BLOCK_COUNT][16] = {};
    bool blockOk[BAMBU_BLOCK_COUNT] = {};
    uint8_t lastAuthSector = 0xFF;

    for (uint8_t i = 0; i < BAMBU_BLOCK_COUNT; i++) {
        uint8_t blockNo = BAMBU_BLOCKS[i];
        uint8_t sector = blockNo / 4;
        bool failed = false;

        if (sector != lastAuthSector) {
            if (!connection_->mifareAuthenticate(blockNo, 0x60, keys.blockKey(blockNo))) {
                Serial.printf("NFCManager: Bambu auth failed on block %d (sector %d)\n", blockNo, sector);
                LogBuffer::getInstance().logPrintf("Bambu auth fail blk %d sec %d\n", blockNo, sector);
                failed = true;
            } else {
                lastAuthSector = sector;
            }
        }

        if (!failed) {
            if (!connection_->mifareClassicRead(blockNo, blocks[i])) {
                Serial.printf("NFCManager: Bambu block read failed on block %d\n", blockNo);
                memset(blocks[i], 0, 16);
                failed = true;
            } else {
                blockOk[i] = true;
                Serial.printf("NFCManager: Bambu block %d: ", blockNo);
                for (int b = 0; b < 16; b++) Serial.printf("%02X ", blocks[i][b]);
                Serial.println();
            }
        }

        // Blocks carrying identity and print-critical values must all be
        // present: a partial read would publish a plausible-looking spool with
        // the wrong colour, weight, or nozzle/bed temperatures — worse than no
        // read at all. Stop at the first essential failure instead of working
        // through the rest; on a shared SPI bus each further attempt can block
        // for the bus-guard timeout, and the whole read is discarded anyway.
        // The remaining blocks (4 unparsed, 13 date, 14 length, 16 extended
        // colour) degrade to zeros without misrepresenting the filament.
        if (failed) {
            if (bambuIsEssentialIndex(i)) {
                Serial.printf("NFCManager: Bambu essential block %d missing, discarding read\n", blockNo);
                LogBuffer::getInstance().logPrintf("Bambu incomplete read blk %d\n", blockNo);
                return false;
            }
            // Optional block: everything required is already in hand, so stop
            // rather than spend more time on a tag that is clearly struggling.
            Serial.printf("NFCManager: Bambu optional block %d missing, using partial data\n", blockNo);
            break;
        }
    }

    // parseBambuBlocks reports validity: a zeroed identity block is never
    // published as a spool.
    return bambuEssentialBlocksPresent(blockOk) && parseBambuBlocks(blocks, out);
}
