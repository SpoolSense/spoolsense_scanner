#include "HardwareNFCConnection.h"
#include <Arduino.h>
#include <cstring>

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
    HardwareNFCConnection* self = static_cast<HardwareNFCConnection*>(ctx);

    // On first page request, fill the entire cache using batched readMultipleBlocks calls.
    // Reading all 78 pages in one command exceeds the ICODE SLIX2 per-command limit and
    // causes the tag not to respond, triggering cascading 1-second spin-wait timeouts in
    // the SPI driver (BUSY pin) that together hit the 5-second task watchdog.
    // Batches of 16 blocks are within the tag's limit (~30ms per batch, ~150ms total)
    // vs. 78 individual readSingleBlock calls (~800ms), preventing tag drift failures.
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
    HardwareNFCConnection* self = static_cast<HardwareNFCConnection*>(ctx);
    ISO15693ErrorCode err = self->nfc_->writeSingleBlock(self->currentUid_, page, const_cast<uint8_t*>(data), 4);
    if (err != ISO15693_EC_OK) {
        Serial.printf("HardwareNFC: writeSingleBlock failed on page %d: %s (0x%02X)\n",
                 page, self->nfc_->strerror(err), (int)err);
        vTaskDelay(pdMS_TO_TICKS(5));  // yield even on failure to prevent starvation on retries
        return OPT_ERR_NFC_WRITE;
    }
    // Yield to FreeRTOS scheduler between block writes to prevent
    // RTOS starvation that causes NFC connection drops after ~5 blocks
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
    pinMode(PIN_PN5180_IRQ, INPUT);    // Interrupt (active HIGH)
    pinMode(PIN_PN5180_GPIO, INPUT);   // Card detection
    pinMode(PIN_PN5180_AUX, INPUT);    // Auxiliary/monitoring

    nfc_ = new PN5180ISO15693(PIN_PN5180_NSS, PIN_PN5180_BUSY, PIN_PN5180_RST,
                               PIN_PN5180_SCK, PIN_PN5180_MISO, PIN_PN5180_MOSI);
    iso14443a_ = new PN5180ISO14443(PIN_PN5180_NSS, PIN_PN5180_BUSY, PIN_PN5180_RST,
                                    PIN_PN5180_SCK, PIN_PN5180_MISO, PIN_PN5180_MOSI);

    Serial.println("HardwareNFCConnection: Starting PN5180...");
    Serial.printf("HardwareNFCConnection: Pins — NSS=%d BUSY=%d RST=%d SCK=%d MISO=%d MOSI=%d\n",
                  PIN_PN5180_NSS, PIN_PN5180_BUSY, PIN_PN5180_RST,
                  PIN_PN5180_SCK, PIN_PN5180_MISO, PIN_PN5180_MOSI);
    nfc_->begin();
    iso14443a_->begin();
    Serial.println("HardwareNFCConnection: SPI begin done, resetting...");
    Serial.printf("HardwareNFCConnection: BUSY pin=%d before reset\n", digitalRead(PIN_PN5180_BUSY));

    // Manual reset with debug (PN5180::reset() has no timeout)
    digitalWrite(PIN_PN5180_RST, LOW);
    delay(10);
    digitalWrite(PIN_PN5180_RST, HIGH);
    Serial.println("HardwareNFCConnection: RST pin released, waiting for boot...");

    // Wait for BUSY to go LOW with timeout (chip boots with RF subsystem)
    unsigned long start = millis();
    while (digitalRead(PIN_PN5180_BUSY) == HIGH) {
        if (millis() - start > 2000) {
            Serial.println("HardwareNFCConnection: TIMEOUT waiting for BUSY LOW after reset!");
            break;
        }
        delay(1);
    }
    Serial.printf("HardwareNFCConnection: BUSY went LOW after %lums\n", millis() - start);

    // Wait for IDLE IRQ with timeout
    start = millis();
    uint32_t irqStatus = 0;
    while (0 == (irqStatus & (1 << 2))) {  // IDLE_IRQ_STAT
        nfc_->readRegister(IRQ_STATUS, &irqStatus);
        if (millis() - start > 2000) {
            Serial.printf("HardwareNFCConnection: TIMEOUT waiting for IDLE IRQ! IRQ=0x%08lX\n", irqStatus);
            break;
        }
        delay(1);
    }
    Serial.printf("HardwareNFCConnection: IDLE IRQ after %lums, IRQ=0x%08lX\n", millis() - start, irqStatus);
    nfc_->clearIRQStatus(0xffffffff);
    Serial.println("HardwareNFCConnection: Reset complete");

    // Read firmware version
    uint8_t firmwareVersion[2];
    if (nfc_->readEEprom(PN5180_FIRMWARE_VERSION, firmwareVersion, 2)) {
        fw_[0] = firmwareVersion[0];
        fw_[1] = firmwareVersion[1];
        pn5180Ready_ = true;
        Serial.printf("HardwareNFCConnection: PN5180 firmware: %d.%d\n",
                      firmwareVersion[1], firmwareVersion[0]);
    } else {
        Serial.println("HardwareNFCConnection: Failed to read PN5180 firmware version");
    }

    // Setup RF for ISO15693
    if (!nfc_->setupRF()) {
        Serial.println("HardwareNFCConnection: Failed to setup RF");
        return false;
    }

    // Set up HAL for openprinttag
    hal_.read_page = halReadPage;
    hal_.write_page = halWritePage;
    hal_.is_present = nullptr;
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
    digitalWrite(PIN_PN5180_RST, LOW);
    delay(10);
    digitalWrite(PIN_PN5180_RST, HIGH);

    // Wait for BUSY to go LOW with timeout
    unsigned long start = millis();
    while (digitalRead(PIN_PN5180_BUSY) == HIGH) {
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

    // 1. Turn off RF field to cleanly tear down any previous config
    //    (handles switch between ISO15693 and ISO14443A configs)
    nfc_->setRF_off();

    // 2. Put state machine into Idle before loading new RF config
    nfc_->writeRegisterWithAndMask(SYSTEM_CONFIG, 0xfffffff8);  // Idle/StopCom

    // 3. Clear any pending IRQs from previous operations
    nfc_->clearIRQStatus(0xffffffff);

    // 4. Load ISO15693 RF config and turn on field
    if (!nfc_->loadRFConfig(0x0d, 0x8d)) return false;
    if (!nfc_->setRF_on()) return false;

    // 5. Transition to Transceive state for tag communication
    nfc_->writeRegisterWithOrMask(SYSTEM_CONFIG, 0x00000003);

    return true;
}

bool HardwareNFCConnection::detectTag(uint8_t* uid, uint8_t* uidLength) {
    if (!nfc_) {
        Serial.println("HardwareNFC: detectTag() called but nfc_ is null!");
        return false;
    }

    ISO15693ErrorCode err = nfc_->getInventory(uid);
    if (err == ISO15693_EC_OK) {
        // Reject phantom detections (all-zero UID = tag not fully powered)
        bool allZero = true;
        for (int i = 0; i < 8; i++) {
            if (uid[i] != 0) { allZero = false; break; }
        }
        if (allZero) return false;

        *uidLength = 8;  // ISO15693 uses 8-byte UID
        return true;
    }

    // ISO15693 not found — try ISO14443A (NTAG215, MIFARE Classic, etc.)
    if (iso14443a_) {
        iso14443a_->setupRF();
        // activateTypeA returns: response[0..1]=ATQA, response[2]=SAK, response[3..9]=UID
        uint8_t response[10] = {0};
        uint8_t uidLen = iso14443a_->activateTypeA(response, 1);
        if (uidLen >= 4) {
            // Check for invalid responses
            if ((response[0] == 0xFF && response[1] == 0xFF) ||
                (response[3] == 0x00 && response[4] == 0x00 && response[5] == 0x00 && response[6] == 0x00) ||
                (response[3] == 0xFF && response[4] == 0xFF && response[5] == 0xFF && response[6] == 0xFF)) {
                iso14443a_->mifareHalt();
            } else {
                // Store ATQA/SAK for tag classification
                lastATQA_ = (response[0] << 8) | response[1];
                lastSAK_ = response[2];
                // Copy UID (starts at offset 3)
                memcpy(uid, response + 3, uidLen);
                *uidLength = uidLen;
                iso14443a_->mifareHalt();
                return true;
            }
        }
    }

    // Log non-OK errors periodically (not every poll to avoid spam)
    static uint32_t errCount = 0;
    errCount++;
    if (errCount % 200 == 1) {
        Serial.printf("HardwareNFC: getInventory err=%d (%s) [count=%lu]\n",
                      (int)err, nfc_->strerror(err), errCount);
    }
    return false;
}

uint16_t HardwareNFCConnection::readISO14443Pages(uint8_t startPage, uint8_t pageCount,
                                                    uint8_t* buffer, uint16_t bufferSize) {
    if (!iso14443a_ || pageCount == 0 || buffer == nullptr) return 0;

    uint16_t totalBytes = pageCount * 4;
    if (totalBytes > bufferSize) return 0;

    // Reactivate the tag — use activateTypeA directly (readCardSerial halts the tag)
    iso14443a_->setupRF();
    uint8_t response[10] = {0};
    uint8_t uidLen = iso14443a_->activateTypeA(response, 1);
    if (uidLen < 4) {
        Serial.println("HardwareNFC: readISO14443Pages - tag reactivation failed");
        return 0;
    }

    // mifareBlockRead returns 16 bytes (4 pages) per call
    // Read in 4-page chunks starting from startPage
    uint16_t bytesRead = 0;
    for (uint8_t page = startPage; page < startPage + pageCount; page += 4) {
        uint8_t block[16] = {0};
        if (!iso14443a_->mifareBlockRead(page, block)) {
            Serial.printf("HardwareNFC: readISO14443Pages - read failed at page %d\n", page);
            iso14443a_->mifareHalt();
            return bytesRead;  // Return what we got so far
        }

        // Copy the pages we need from this 16-byte block
        uint8_t pagesInBlock = 4;
        uint8_t remaining = (startPage + pageCount) - page;
        if (remaining < 4) pagesInBlock = remaining;

        uint16_t copyBytes = pagesInBlock * 4;
        if (bytesRead + copyBytes > bufferSize) copyBytes = bufferSize - bytesRead;
        memcpy(buffer + bytesRead, block, copyBytes);
        bytesRead += copyBytes;
    }

    iso14443a_->mifareHalt();
    return bytesRead;
}

bool HardwareNFCConnection::writeISO14443Pages(uint8_t startPage, uint8_t pageCount,
                                                 const uint8_t* data, uint16_t dataLen) {
    if (!iso14443a_ || pageCount == 0 || data == nullptr) return false;

    uint16_t totalBytes = pageCount * 4;
    if (dataLen < totalBytes) return false;

    // Reactivate the tag
    iso14443a_->setupRF();
    uint8_t response[10] = {0};
    uint8_t uidLen = iso14443a_->activateTypeA(response, 1);
    if (uidLen < 4) {
        Serial.println("HardwareNFC: writeISO14443Pages - tag reactivation failed");
        return false;
    }

    // Write one page (4 bytes) at a time, with retry on failure
    unsigned long writeStart = millis();
    int retryCount = 0;
    for (uint8_t i = 0; i < pageCount; i++) {
        uint8_t page = startPage + i;
        bool pageOk = iso14443a_->mifareBlockWrite4(page, data + (i * 4));

        if (!pageOk) {
            // Retry: re-activate tag and try the same page again (up to 2 retries)
            for (int retry = 0; retry < 2 && !pageOk; retry++) {
                retryCount++;
                Serial.printf("HardwareNFC: writeISO14443Pages - retry %d for page %d\n", retry + 1, page);
                iso14443a_->mifareHalt();
                delay(10);
                iso14443a_->setupRF();
                uidLen = iso14443a_->activateTypeA(response, 1);
                if (uidLen < 4) {
                    Serial.printf("HardwareNFC: writeISO14443Pages - re-activation failed on retry %d\n", retry + 1);
                    continue;
                }
                pageOk = iso14443a_->mifareBlockWrite4(page, data + (i * 4));
            }

            if (!pageOk) {
                unsigned long elapsed = millis() - writeStart;
                Serial.printf("HardwareNFC: writeISO14443Pages - FAILED at page %d after retries (%lums, %d retries total)\n",
                              page, elapsed, retryCount);
                iso14443a_->mifareHalt();
                return false;
            }
        }
    }

    unsigned long elapsed = millis() - writeStart;
    iso14443a_->mifareHalt();
    Serial.printf("HardwareNFC: writeISO14443Pages - wrote %d pages starting at page %d (%lums, %d retries)\n",
                  pageCount, startPage, elapsed, retryCount);
    return true;
}

void HardwareNFCConnection::setCurrentUid(const uint8_t* uid, uint8_t length) {
    memcpy(currentUid_, uid, length < 8 ? length : 8);
    readCacheValid_ = false;
}

opt_nfc_hal_t* HardwareNFCConnection::getHal() {
    return &hal_;
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
    bool rfFieldOn = (rfStatus & 0x01);  // TX_RF_STATUS bit
    bool extFieldDet = (rfStatus & 0x02);  // RF_DET_STATUS bit

    #ifdef ENABLE_NFC_DEBUG_LOGS

        Serial.printf("HardwareNFC DIAG: IRQ=0x%08lX RF=0x%08lX SYS=0x%08lX\n",
                    irqStatus, rfStatus, sysStatus);
        Serial.printf("HardwareNFC DIAG: RF_field=%s ext_field=%s transceiver=%u\n",
                    rfFieldOn ? "ON" : "OFF",
                    extFieldDet ? "YES" : "NO",
                    transceiverState);

        // Step-by-step RF activation test
        Serial.println("HardwareNFC DIAG: --- RF activation test ---");

    #endif
    

    // Step 1: Reset and check
    nfc_->reset();
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        Serial.printf("HardwareNFC DIAG: After reset: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif


    // Step 2: Load RF config
    nfc_->loadRFConfig(0x0d, 0x8d);
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        Serial.printf("HardwareNFC DIAG: After loadRFConfig: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif


    // Step 3: Turn RF on
    nfc_->setRF_on();
    nfc_->readRegister(RF_STATUS, &rfStatus);
    nfc_->readRegister(IRQ_STATUS, &irqStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        Serial.printf("HardwareNFC DIAG: After setRF_on: RF=0x%08lX IRQ=0x%08lX field=%s\n",
                    rfStatus, irqStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif

    // Step 4: Wait and check again
    delay(50);
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        Serial.printf("HardwareNFC DIAG: After 50ms wait: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif

    // Step 5: Set transceive (like setupRF does) and check
    nfc_->writeRegisterWithAndMask(SYSTEM_CONFIG, 0xfffffff8);  // Idle
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        Serial.printf("HardwareNFC DIAG: After Idle cmd: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif


    nfc_->writeRegisterWithOrMask(SYSTEM_CONFIG, 0x00000003);  // Transceive
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        Serial.printf("HardwareNFC DIAG: After Transceive cmd: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");

        Serial.println("HardwareNFC DIAG: --- end test ---");
    #endif
}

bool HardwareNFCConnection::ntagGetVersion(uint8_t* versionOut) {
    if (!iso14443a_ || !versionOut) return false;

    uint8_t cmd = 0x60;
    if (!iso14443a_->sendData(&cmd, 1, 0x00)) return false;

    uint32_t rxStatus;
    uint16_t rxLen = 0;
    for (int i = 0; i < 10; i++) {
        delay(1);
        iso14443a_->readRegister(RX_STATUS, &rxStatus);
        rxLen = rxStatus & 0x000001ff;
        if (rxLen >= 8) break;
    }
    if (rxLen < 8) return false;

    return iso14443a_->readData(8, versionOut) != nullptr;
}
