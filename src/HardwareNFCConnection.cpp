#include "HardwareNFCConnection.h"
#include "ConfigurationManager.h"
#include "LogBuffer.h"
#include <Arduino.h>
#include <cstring>
#include <esp_task_wdt.h>

// NFCScanTask feeds the task watchdog once per scan-loop iteration, but a
// single iteration can legally grind for minutes when RF operations fail
// slowly (each BUSY/IRQ wait is bounded at 1-2s) across a multi-block read
// or write ladder — marginal coupling wedges the transceiver and every block
// burns seconds. Feed the watchdog per block so it catches true hangs, not
// slow RF failure grinds (task_wdt reboot loop, bench 2026-07-06). Harmless
// no-op (ESP_ERR_NOT_FOUND) when called from tasks not subscribed to the TWDT.
static inline void feedTaskWatchdog() {
    esp_task_wdt_reset();
}

// PN5180 ISO15693 NFC reader with ISO14443A fallback. Handles tag detection,
// multi-block read/write caching (prevents watchdog timeout), and RF state management.

// BUSY-handshake timeout sink (#293): the wedge lines are the only record of
// WHICH handshake stalled the bus, so mirror them into the web log ring — but
// rate-limited, because a dead reader re-wedges on every recovery cycle and
// would scroll everything else out of the 4KB ring. Serial keeps every line;
// the ring gets the first and every 20th, with a running count.
static void pn5180TimeoutToLogBuffer(const char* phase) {
    static uint32_t count = 0;
    count++;
    if (count % 20 == 1) {
        LogBuffer::getInstance().logPrintf("PN5180: TIMEOUT %s (occurrence %lu)\n",
                                           phase, (unsigned long)count);
    } else {
        Serial.printf("PN5180: TIMEOUT %s\n", phase);
    }
}

//#define ENABLE_NFC_DEBUG_LOGS

HardwareNFCConnection::HardwareNFCConnection() {
    memset(currentUid_, 0, sizeof(currentUid_));
    memset(&hal_, 0, sizeof(hal_));
}

HardwareNFCConnection::~HardwareNFCConnection() {
    delete nfc_;
    delete iso14443a_;
}

opt_error_t HardwareNFCConnection::halReadPage(void* ctx, uint8_t page, uint8_t* buffer) {
    feedTaskWatchdog();
    HardwareNFCConnection* self = static_cast<HardwareNFCConnection*>(ctx);

    // Batch read on first page request to avoid watchdog timeout: ICODE SLIX2 has a per-command
    // limit (exceeds at 78 pages), causing BUSY pin spin-wait cascades that together exceed the
    // 5-second FreeRTOS watchdog. Batches of 16 blocks (~30ms each) stay within tag limits
    // and complete in ~150ms total, vs 78 individual calls (~800ms) that trigger timeout.
    if (!self->readCacheValid_) {
        static constexpr uint8_t BATCH_SIZE = 16;
        for (uint8_t start = 0; start < READ_CACHE_PAGES; start += BATCH_SIZE) {
            uint8_t count = (start + BATCH_SIZE <= READ_CACHE_PAGES)
                                ? BATCH_SIZE
                                : (READ_CACHE_PAGES - start);
            ISO15693ErrorCode rc = self->nfc_->readMultipleBlocks(
                self->currentUid_, start, count,
                self->readCache_ + start * 4, 4);
            if (rc != ISO15693_EC_OK) {
                Serial.printf("HardwareNFC: readMultipleBlocks err=%d (%s) [block=%d]\n",
                              (int)rc, self->nfc_->strerror(rc), start);
                return OPT_ERR_NFC_READ;
            }
        }
        self->readCacheValid_ = true;
    }

    if (page >= READ_CACHE_PAGES) return OPT_ERR_NFC_READ;
    memcpy(buffer, self->readCache_ + page * 4, 4);
    return OPT_OK;
}

opt_error_t HardwareNFCConnection::halWritePage(void* ctx, uint8_t page, const uint8_t* data) {
    feedTaskWatchdog();
    HardwareNFCConnection* self = static_cast<HardwareNFCConnection*>(ctx);
    ISO15693ErrorCode err = self->nfc_->writeSingleBlock(self->currentUid_, page, const_cast<uint8_t*>(data), 4);
    if (err != ISO15693_EC_OK) {
        Serial.printf("HardwareNFC: writeSingleBlock failed on page %d: %s (0x%02X)\n",
                 page, self->nfc_->strerror(err), (int)err);
        vTaskDelay(pdMS_TO_TICKS(5));  // prevent task starvation on retry loop that fails fast
        return OPT_ERR_NFC_WRITE;
    }
    // SPI contention: PN5180 RF communication blocks when other tasks consume CPU; stagger
    // writes with 5ms yields to allow higher-priority tasks and RF recovery between blocks.
    vTaskDelay(pdMS_TO_TICKS(5));
    return OPT_OK;
}

void HardwareNFCConnection::getReaderInfo(char* buf, size_t len) const {
    if (buf && len > 0) {
        snprintf(buf, len, "PN5180 v%d.%d", fw_[1], fw_[0]);
    }
}

bool HardwareNFCConnection::begin() {
    // Configure additional input pins for future use
    // Shared-SPI boards use explicit -1 sentinels for signals that are not
    // connected, preserving scarce GPIOs for the TFT control lines.
    if (PIN_PN5180_IRQ >= 0) pinMode(PIN_PN5180_IRQ, INPUT);
    if (PIN_PN5180_GPIO >= 0) pinMode(PIN_PN5180_GPIO, INPUT);
    if (PIN_PN5180_AUX >= 0) pinMode(PIN_PN5180_AUX, INPUT);

    // Runtime pin overrides (#201): board defaults unless remapped in config
    {
        auto& cfg = ConfigurationManager::getInstance();
        pinRst_  = cfg.getNfcPin(NfcPinId::Rst);
        pinNss_  = cfg.getNfcPin(NfcPinId::Nss);
        pinBusy_ = cfg.getNfcPin(NfcPinId::Busy);
        pinSck_  = cfg.getNfcPin(NfcPinId::Sck);
        pinMosi_ = cfg.getNfcPin(NfcPinId::Mosi);
        pinMiso_ = cfg.getNfcPin(NfcPinId::Miso);
    }

    nfc_ = new PN5180ISO15693(pinNss_, pinBusy_, pinRst_,
                               pinSck_, pinMiso_, pinMosi_);
    iso14443a_ = new PN5180ISO14443(pinNss_, pinBusy_, pinRst_,
                                    pinSck_, pinMiso_, pinMosi_);

    // Boot lines go through LogBuffer (Serial + web /logs ring) so a reader
    // that dies in the field can be diagnosed without a USB cable (#293).
    auto& logBuf = LogBuffer::getInstance();
    logBuf.logPrintf("HardwareNFCConnection: Starting PN5180...\n");
    logBuf.logPrintf("HardwareNFCConnection: Pins — NSS=%d BUSY=%d RST=%d SCK=%d MISO=%d MOSI=%d\n",
                  pinNss_, pinBusy_, pinRst_,
                  pinSck_, pinMiso_, pinMosi_);
    // Bench diagnostics: confirms whether a PN5180_SPI_HZ build-flag override
    // reached this image. "Requested" because Arduino-ESP32 derives a hardware
    // divider from this — e.g. 7 MHz actually runs at ~6.67 MHz on the wire.
    logBuf.logPrintf("HardwareNFCConnection: PN5180 requested SPI clock %lu Hz\n",
                  (unsigned long)PN5180_SPI_HZ);
    PN5180::setTimeoutLogSink(pn5180TimeoutToLogBuffer);
    nfc_->begin();
    iso14443a_->begin();
    logBuf.logPrintf("HardwareNFCConnection: SPI begin done, resetting...\n");
    logBuf.logPrintf("HardwareNFCConnection: BUSY pin=%d before reset\n", digitalRead(pinBusy_));

    // Manual reset with timeout: PN5180::reset() library call has no timeout, blocking on hung chips
    digitalWrite(pinRst_, LOW);
    delay(10);
    digitalWrite(pinRst_, HIGH);
    logBuf.logPrintf("HardwareNFCConnection: RST pin released, waiting for boot...\n");

    // BUSY signal indicates RF subsystem boot state; timeout detects dead/unreliable chips
    unsigned long start = millis();
    while (digitalRead(pinBusy_) == HIGH) {
        if (millis() - start > 2000) {
            logBuf.logPrintf("HardwareNFCConnection: TIMEOUT waiting for BUSY LOW after reset!\n");
            break;
        }
        delay(1);
    }
    logBuf.logPrintf("HardwareNFCConnection: BUSY went LOW after %lums\n", millis() - start);

    // IDLE_IRQ_STAT (bit 2) confirms RF initialization complete; without it, transceive fails
    start = millis();
    uint32_t irqStatus = 0;
    while (0 == (irqStatus & (1 << 2))) {
        nfc_->readRegister(IRQ_STATUS, &irqStatus);
        if (millis() - start > 2000) {
            logBuf.logPrintf("HardwareNFCConnection: TIMEOUT waiting for IDLE IRQ! IRQ=0x%08lX\n", irqStatus);
            break;
        }
        delay(1);
    }
    logBuf.logPrintf("HardwareNFCConnection: IDLE IRQ after %lums, IRQ=0x%08lX\n", millis() - start, irqStatus);
    nfc_->clearIRQStatus(0xffffffff);  // clear IDLE_IRQ and all pending flags before transceive
    logBuf.logPrintf("HardwareNFCConnection: Reset complete\n");

    // Firmware version stored at EEPROM address; used for debugging and feature detection
    uint8_t firmwareVersion[2];
    if (nfc_->readEEprom(PN5180_FIRMWARE_VERSION, firmwareVersion, 2)) {
        fw_[0] = firmwareVersion[0];
        fw_[1] = firmwareVersion[1];
        pn5180Ready_ = true;
        logBuf.logPrintf("HardwareNFCConnection: PN5180 firmware: %d.%d\n",
                      firmwareVersion[1], firmwareVersion[0]);
    } else {
        logBuf.logPrintf("HardwareNFCConnection: Failed to read PN5180 firmware version\n");
    }

    // Load RF config enables ISO15693 field; failure here is terminal
    if (!nfc_->setupRF()) {
        logBuf.logPrintf("HardwareNFCConnection: Failed to setup RF\n");
        return false;
    }

    // HAL interface bridges between openprinttag library and PN5180 driver
    hal_.read_page = halReadPage;
    hal_.write_page = halWritePage;
    hal_.is_present = nullptr;  // unused for ISO15693 tags
    hal_.user_ctx = this;

    Serial.println("HardwareNFCConnection: Initialized successfully");
    return true;
}

void HardwareNFCConnection::reset() {
    if (nfc_) {
        nfc_->reset();
    }
}

bool HardwareNFCConnection::hardwareReset() {
    if (!nfc_) return false;

    Serial.println("HardwareNFC: hardwareReset() - toggling RST pin");

    // Toggle RST pin to force full hardware reset
    digitalWrite(pinRst_, LOW);
    delay(10);
    digitalWrite(pinRst_, HIGH);
    PN5180::clearBusWedged();  // fresh chip — release the fail-fast latch

    // Wait for BUSY to go LOW with timeout
    unsigned long start = millis();
    while (digitalRead(pinBusy_) == HIGH) {
        if (millis() - start > 2000) {
            Serial.println("HardwareNFC: hardwareReset TIMEOUT waiting for BUSY LOW");
            return false;
        }
        delay(1);
    }

    // Wait for IDLE IRQ with timeout
    start = millis();
    uint32_t irqStatus = 0;
    while (0 == (irqStatus & IDLE_IRQ_STAT)) {
        nfc_->readRegister(IRQ_STATUS, &irqStatus);
        if (millis() - start > 2000) {
            Serial.printf("HardwareNFC: hardwareReset TIMEOUT waiting for IDLE IRQ, IRQ=0x%08lX\n", irqStatus);
            return false;
        }
        delay(1);
    }
    nfc_->clearIRQStatus(0xffffffff);

    // Re-setup RF
    return setupRF();
}

bool HardwareNFCConnection::setupRF() {
    if (!nfc_) return false;

    // RF reconfiguration sequence: turn off field to clear stale state from previous protocol
    // (matters when switching between ISO15693 and ISO14443A detection loops)
    nfc_->setRF_off();

    // SYSTEM_CONFIG bits [2:0] control state machine: mask to 000 = Idle/StopCom (safe state)
    nfc_->writeRegisterWithAndMask(SYSTEM_CONFIG, 0xfffffff8);

    // Clear all pending IRQs before loading new RF config to avoid false triggers
    nfc_->clearIRQStatus(0xffffffff);

    // Config 0x0d/0x8d = ISO15693 (15.4kHz tuning); returns false on comms failure only
    if (!nfc_->loadRFConfig(0x0d, 0x8d)) return false;
    if (!nfc_->setRF_on()) return false;

    // Set SYSTEM_CONFIG bits [2:0] to 011 = Transceive mode (required for tag reads/writes)
    nfc_->writeRegisterWithOrMask(SYSTEM_CONFIG, 0x00000003);

    return true;
}

bool HardwareNFCConnection::detectTag(uint8_t* uid, uint8_t* uidLength) {
    tagSessionActive_ = false;  // clear on every detect cycle
    if (!nfc_) {
        Serial.println("HardwareNFC: detectTag() called but nfc_ is null!");
        return false;
    }

    ISO15693ErrorCode err = nfc_->getInventory(uid);
    if (err == ISO15693_EC_OK) {
        // All-zero UID = tag RF-powered but not booted; reject to avoid double-reads during activation
        bool allZero = true;
        for (int i = 0; i < 8; i++) {
            if (uid[i] != 0) { allZero = false; break; }
        }
        if (allZero) return false;

        *uidLength = 8;  // ISO15693 spec: 8-byte UID
        return true;
    }

    // Fallback to ISO14443A for 4-byte and 7-byte tags (NTAG, MIFARE, etc.)
    // Dual-protocol scanner allows PN5180 (ISO15693) + PN532 (ISO14443A only) to coexist
    if (iso14443a_) {
        iso14443a_->setupRF();
        // activateTypeA populates response: [0..1]=ATQA, [2]=SAK, [3..9]=UID (4, 7, or 10 bytes)
        uint8_t response[10] = {0};
        uint8_t uidLen = iso14443a_->activateTypeA(response, 1);
        if (uidLen >= 4) {
            // Reject spurious activations: ATQA/SAK all 0xFF or UID all 0x00 or 0xFF (transceiver noise)
            if ((response[0] == 0xFF && response[1] == 0xFF) ||
                (response[3] == 0x00 && response[4] == 0x00 && response[5] == 0x00 && response[6] == 0x00) ||
                (response[3] == 0xFF && response[4] == 0xFF && response[5] == 0xFF && response[6] == 0xFF)) {
                iso14443a_->mifareHalt();
            } else {
                // Store ATQA/SAK for tag classification (used to distinguish NTAG/MIFARE/etc)
                lastATQA_ = (response[0] << 8) | response[1];
                lastSAK_ = response[2];
                memcpy(uid, response + 3, uidLen);
                *uidLength = uidLen;

                // Capture GET_VERSION while tag is active (before halt kills the session).
                // SAK 0x00 = NTAG/Ultralight family; MIFARE Classic doesn't support GET_VERSION.
                lastVersionValid_ = false;
                if (lastSAK_ == 0x00) {
                    uint8_t cmd = 0x60;
                    if (iso14443a_->sendData(&cmd, 1, 0x00)) {
                        uint32_t rxStatus;
                        uint16_t rxLen = 0;
                        for (int i = 0; i < 10; i++) {
                            delay(1);
                            iso14443a_->readRegister(RX_STATUS, &rxStatus);
                            rxLen = rxStatus & 0x000001ff;
                            if (rxLen >= 8) break;
                        }
                        if (rxLen >= 8 && iso14443a_->readData(8, lastVersion_)) {
                            lastVersionValid_ = true;
                        }
                    }
                }

                // Don't halt — keep tag active for subsequent page reads.
                // readISO14443Pages will skip re-activation when tagSessionActive_ is set.
                tagSessionActive_ = true;
                return true;
            }
        }
    }

    // Rate-limit errors: getInventory fails every poll when no tag present; log only 1 in 200
    static uint32_t errCount = 0;
    errCount++;
    if (errCount % 200 == 1) {
        Serial.printf("HardwareNFC: getInventory err=%d (%s) [count=%lu]\n",
                      (int)err, nfc_->strerror(err), errCount);
    }
    return false;
}

void HardwareNFCConnection::endTagSession() {
    if (tagSessionActive_ && iso14443a_) {
        iso14443a_->mifareHalt();
        tagSessionActive_ = false;
    }
}

uint16_t HardwareNFCConnection::readISO14443Pages(uint8_t startPage, uint8_t pageCount,
                                                    uint8_t* buffer, uint16_t bufferSize, bool keepSession) {
    if (!iso14443a_ || pageCount == 0 || buffer == nullptr) return 0;

    uint16_t totalBytes = pageCount * 4;
    if (totalBytes > bufferSize) return 0;

    if (tagSessionActive_) {
        // Tag already active from detectTag — skip redundant setupRF + activateTypeA
    } else {
        // Tag was halted or this is a standalone read — must re-activate
        iso14443a_->setupRF();
        uint8_t response[10] = {0};
        uint8_t uidLen = iso14443a_->activateTypeA(response, 1);
        if (uidLen < 4) {
            Serial.println("HardwareNFC: readISO14443Pages - tag reactivation failed");
            return 0;
        }
    }

    // mifareBlockRead reads 16 bytes (4 pages) per call; read in 4-page chunks starting from startPage
    uint16_t bytesRead = 0;
    extern volatile uint32_t g_nfcScanPhasePage;
    for (uint8_t page = startPage; page < startPage + pageCount; page += 4) {
        feedTaskWatchdog();
        g_nfcScanPhasePage = page;
        uint8_t block[16] = {0};
        if (!iso14443a_->mifareBlockRead(page, block)) {
            Serial.printf("HardwareNFC: readISO14443Pages - read failed at page %d\n", page);
            iso14443a_->mifareHalt();
            tagSessionActive_ = false;
            return bytesRead;  // partial read acceptable; caller can retry
        }

        // Compute pages in this block: fewer than 4 on last iteration
        uint8_t pagesInBlock = 4;
        uint8_t remaining = (startPage + pageCount) - page;
        if (remaining < 4) pagesInBlock = remaining;

        uint16_t copyBytes = pagesInBlock * 4;
        if (bytesRead + copyBytes > bufferSize) copyBytes = bufferSize - bytesRead;  // buffer bounds check
        memcpy(buffer + bytesRead, block, copyBytes);
        bytesRead += copyBytes;
    }

    if (!keepSession) {
        iso14443a_->mifareHalt();
        tagSessionActive_ = false;
    }
    return bytesRead;
}

bool HardwareNFCConnection::writeISO14443Pages(uint8_t startPage, uint8_t pageCount,
                                                 const uint8_t* data, uint16_t dataLen) {
    if (!iso14443a_ || pageCount == 0 || data == nullptr) return false;

    uint16_t totalBytes = pageCount * 4;
    if (dataLen < totalBytes) return false;

    uint8_t response[10] = {0};
    uint8_t uidLen = 0;

    if (!tagSessionActive_) {
        // Tag was halted — must reactivate before writing
        iso14443a_->setupRF();
        uidLen = iso14443a_->activateTypeA(response, 1);
        if (uidLen < 4) {
            Serial.println("HardwareNFC: writeISO14443Pages - tag reactivation failed");
            return false;
        }
        tagSessionActive_ = true;
    }

    // Write per-page with retry: ISO14443A tags lose activation after errors; retry with re-activate
    unsigned long writeStart = millis();
    int retryCount = 0;
    for (uint8_t i = 0; i < pageCount; i++) {
        feedTaskWatchdog();
        uint8_t page = startPage + i;
        bool pageOk = iso14443a_->mifareBlockWrite4(page, data + (i * 4));

        if (!pageOk) {
            // A tag that NAKed from RF noise is still ACTIVE — a plain resend
            // usually recovers in ~5ms. Only tear the session down (halt +
            // re-activate) if the resend also fails; the teardown costs 15+ms
            // of RF churn at exactly the moment coupling is marginal.
            retryCount++;
            Serial.printf("HardwareNFC: writeISO14443Pages - resend for page %d\n", page);
            delay(5);  // let the field settle before retrying
            pageOk = iso14443a_->mifareBlockWrite4(page, data + (i * 4));

            for (int retry = 0; retry < 2 && !pageOk; retry++) {
                retryCount++;
                Serial.printf("HardwareNFC: writeISO14443Pages - retry %d for page %d\n", retry + 1, page);
                iso14443a_->mifareHalt();
                tagSessionActive_ = false;
                delay(10);  // RF settle time before re-activate
                iso14443a_->setupRF();
                uidLen = iso14443a_->activateTypeA(response, 1);
                if (uidLen < 4) {
                    Serial.printf("HardwareNFC: writeISO14443Pages - re-activation failed on retry %d\n", retry + 1);
                    continue;
                }
                tagSessionActive_ = true;
                pageOk = iso14443a_->mifareBlockWrite4(page, data + (i * 4));
            }

            if (!pageOk) {
                unsigned long elapsed = millis() - writeStart;
                Serial.printf("HardwareNFC: writeISO14443Pages - FAILED at page %d after retries (%lums, %d retries total)\n",
                              page, elapsed, retryCount);
                iso14443a_->mifareHalt();
                tagSessionActive_ = false;
                return false;  // total write failure; clean exit for caller to retry entire sequence
            }
        }
    }

    unsigned long elapsed = millis() - writeStart;
    iso14443a_->mifareHalt();
    tagSessionActive_ = false;  // halted — readers/writers must reactivate (readback verify relies on this)
    Serial.printf("HardwareNFC: writeISO14443Pages - wrote %d pages starting at page %d (%lums, %d retries)\n",
                  pageCount, startPage, elapsed, retryCount);
    return true;
}

void HardwareNFCConnection::setCurrentUid(const uint8_t* uid, uint8_t length) {
    memcpy(currentUid_, uid, length < 8 ? length : 8);
    readCacheValid_ = false;  // new tag invalidates cached page reads
}

opt_nfc_hal_t* HardwareNFCConnection::getHal() {
    return &hal_;
}

bool HardwareNFCConnection::getDiagnosticSnapshot(ReaderDiagnostics& out) {
    memset(&out, 0, sizeof(out));
    getReaderInfo(out.reader_name, sizeof(out.reader_name));
    out.initialized = pn5180Ready_;
    out.fw_major = fw_[1];
    out.fw_minor = fw_[0];
    out.has_registers = true;
    out.bus_wedged = PN5180::isBusWedged();
    // A wedged bus fails every transaction fast — its register values are
    // meaningless, so only sample them when the bus is live.
    if (nfc_ && !out.bus_wedged) {
        uint32_t irq = 0, rf = 0, sys = 0;
        nfc_->readRegister(IRQ_STATUS, &irq);
        nfc_->readRegister(RF_STATUS, &rf);
        nfc_->readRegister(SYSTEM_STATUS, &sys);
        out.irq_status = irq;
        out.rf_status = rf;
        out.system_status = sys;
    }
    return true;
}

void HardwareNFCConnection::logDiagnostics() {
    if (!nfc_) {
        Serial.println("HardwareNFC DIAG: nfc_ is null!");
        return;
    }

    uint32_t irqStatus, rfStatus, sysStatus;
    nfc_->readRegister(IRQ_STATUS, &irqStatus);
    nfc_->readRegister(RF_STATUS, &rfStatus);
    nfc_->readRegister(SYSTEM_STATUS, &sysStatus);

    uint8_t transceiverState = (rfStatus >> 24) & 0x07;
    bool rfFieldOn = (rfStatus & 0x01);  // TX_RF_STATUS bit = field active
    bool extFieldDet = (rfStatus & 0x02);  // RF_DET_STATUS bit = external field detected

    #ifdef ENABLE_NFC_DEBUG_LOGS

        Serial.printf("HardwareNFC DIAG: IRQ=0x%08lX RF=0x%08lX SYS=0x%08lX\n",
                    irqStatus, rfStatus, sysStatus);
        Serial.printf("HardwareNFC DIAG: RF_field=%s ext_field=%s transceiver=%u\n",
                    rfFieldOn ? "ON" : "OFF",
                    extFieldDet ? "YES" : "NO",
                    transceiverState);

        // Step-by-step RF activation test to verify state machine sequencing
        Serial.println("HardwareNFC DIAG: --- RF activation test ---");

    #endif

    // Verify RF state transitions through init sequence (matches begin/setupRF logic)
    nfc_->reset();
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        Serial.printf("HardwareNFC DIAG: After reset: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif

    nfc_->loadRFConfig(0x0d, 0x8d);  // ISO15693 tuning
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        Serial.printf("HardwareNFC DIAG: After loadRFConfig: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif

    nfc_->setRF_on();
    nfc_->readRegister(RF_STATUS, &rfStatus);
    nfc_->readRegister(IRQ_STATUS, &irqStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        Serial.printf("HardwareNFC DIAG: After setRF_on: RF=0x%08lX IRQ=0x%08lX field=%s\n",
                    rfStatus, irqStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif

    delay(50);  // RF field stabilization
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        Serial.printf("HardwareNFC DIAG: After 50ms wait: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif

    // Transition to Transceive state (bits [2:0] = 011)
    nfc_->writeRegisterWithAndMask(SYSTEM_CONFIG, 0xfffffff8);
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        Serial.printf("HardwareNFC DIAG: After Idle cmd: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif

    nfc_->writeRegisterWithOrMask(SYSTEM_CONFIG, 0x00000003);
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        Serial.printf("HardwareNFC DIAG: After Transceive cmd: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");

        Serial.println("HardwareNFC DIAG: --- end test ---");
    #endif
}

bool HardwareNFCConnection::mifareAuthenticate(uint8_t blockNo, uint8_t keyType, const uint8_t* key) {
    if (!iso14443a_) return false;
    return iso14443a_->mifareAuthenticate(blockNo, keyType, key, currentUid_, 4);
}

bool HardwareNFCConnection::mifareClassicRead(uint8_t blockNo, uint8_t* buffer) {
    if (!iso14443a_) return false;
    return iso14443a_->mifareBlockRead(blockNo, buffer);
}

bool HardwareNFCConnection::ntagGetVersion(uint8_t* versionOut) {
    // Returns cached version from detectTag() — GET_VERSION must be sent while the tag
    // is in ACTIVE state (after SELECT, before HALT). Can't send it here because the
    // tag is already halted by the time classifyTag() runs.
    if (!versionOut || !lastVersionValid_) return false;
    memcpy(versionOut, lastVersion_, 8);
    return true;
}
