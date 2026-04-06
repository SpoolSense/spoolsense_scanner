#include "HardwareNFCConnectionPN532.h"
#include "openprinttag_adafruit_pn532.h"
#include "BoardPins.h"

#include <Arduino.h>
#include <SPI.h>

// Adafruit_PN532 uses a file-scope global packet buffer. After readPassiveTargetID,
// the InListPassiveTarget response populates it with ATQA (bytes 9-10) and SAK (byte 11).
extern byte pn532_packetbuffer[];

HardwareNFCConnectionPN532::HardwareNFCConnectionPN532() {
    memset(&hal_, 0, sizeof(hal_));
    memset(currentUid_, 0, sizeof(currentUid_));
}

HardwareNFCConnectionPN532::~HardwareNFCConnectionPN532() {
    delete pn532_;
}

bool HardwareNFCConnectionPN532::begin() {
    // Initialize SPI with explicit ESP32 pin mapping
    SPI.begin(PIN_PN532_SCK, PIN_PN532_MISO, PIN_PN532_MOSI, PIN_PN532_SS);

    pn532_ = new Adafruit_PN532(PIN_PN532_SS, &SPI);
    if (!pn532_) {
        Serial.println("PN532: Failed to allocate");
        return false;
    }

    pn532_->begin();

    // Read firmware version to verify communication
    uint32_t versiondata = pn532_->getFirmwareVersion();
    if (!versiondata) {
        Serial.println("PN532: No response — check wiring");
        delete pn532_;
        pn532_ = nullptr;
        return false;
    }

    // getFirmwareVersion() returns: IC<<24 | FW<<16 | Rev<<8 | Support
    fwMajor_ = (versiondata >> 16) & 0xFF;  // firmware version
    fwMinor_ = (versiondata >> 8) & 0xFF;   // firmware revision
    Serial.printf("PN532: Found IC=0x%02X firmware v%d.%d\n",
        (uint8_t)((versiondata >> 24) & 0xFF), fwMajor_, fwMinor_);

    // Configure the PN532 to read RFID tags
    if (!pn532_->SAMConfig()) {
        Serial.println("PN532: SAMConfig failed");
        delete pn532_;
        pn532_ = nullptr;
        return false;
    }

    // Set up openprinttag HAL
    hal_ = opt_create_adafruit_pn532_hal(pn532_);

    ready_ = true;
    Serial.println("PN532: Initialized (ISO14443A only)");
    return true;
}

void HardwareNFCConnectionPN532::reset() {
    if (pn532_) {
        pn532_->begin();
        if (!pn532_->SAMConfig()) {
            Serial.println("PN532: SAMConfig failed during reset");
        }
    }
}

bool HardwareNFCConnectionPN532::hardwareReset() {
    // Toggle RST pin for hardware reset
    pinMode(PIN_PN532_RST, OUTPUT);
    digitalWrite(PIN_PN532_RST, LOW);
    delay(10);
    digitalWrite(PIN_PN532_RST, HIGH);
    delay(50);  // PN532 boot time

    if (pn532_) {
        pn532_->begin();
        uint32_t ver = pn532_->getFirmwareVersion();
        if (!ver) return false;
        if (!pn532_->SAMConfig()) return false;
    }
    return true;
}

bool HardwareNFCConnectionPN532::setupRF() {
    // PN532 manages RF internally — no manual RF setup needed
    return ready_;
}

bool HardwareNFCConnectionPN532::detectTag(uint8_t* uid, uint8_t* uidLength) {
    if (!pn532_ || !ready_) return false;

    // Short timeout (100ms) to avoid blocking the scan loop
    uint8_t uidLen = 0;
    bool found = pn532_->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 100);
    if (!found || uidLen == 0) return false;

    *uidLength = uidLen;

    // Extract SAK/ATQA from the global packet buffer.
    // After readdata(pn532_packetbuffer, 20) in readDetectedPassiveTargetID:
    //   [7]    = tags found
    //   [9-10] = SENS_RES (ATQA) — big-endian in Adafruit's code
    //   [11]   = SEL_RES (SAK)
    //   [12]   = UID length
    //   [13+]  = UID bytes
    lastATQA_ = ((uint16_t)pn532_packetbuffer[9] << 8) | pn532_packetbuffer[10];
    lastSAK_ = pn532_packetbuffer[11];

    return true;
}

void HardwareNFCConnectionPN532::setCurrentUid(const uint8_t* uid, uint8_t length) {
    currentUidLen_ = (length <= sizeof(currentUid_)) ? length : sizeof(currentUid_);
    memcpy(currentUid_, uid, currentUidLen_);
}

opt_nfc_hal_t* HardwareNFCConnectionPN532::getHal() {
    return &hal_;
}

bool HardwareNFCConnectionPN532::reactivateTag() {
    if (!pn532_) return false;
    uint8_t uid[10];
    uint8_t uidLen = 0;
    if (!pn532_->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200))
        return false;
    // Verify same tag is still present (prevent silent cross-tag reads/writes)
    if (uidLen != currentUidLen_ || memcmp(uid, currentUid_, uidLen) != 0)
        return false;
    return true;
}

uint16_t HardwareNFCConnectionPN532::readISO14443Pages(
    uint8_t startPage, uint8_t pageCount, uint8_t* buffer, uint16_t bufferSize) {
    if (!pn532_ || !ready_) return 0;

    uint16_t totalBytes = (uint16_t)pageCount * 4;
    if (totalBytes > bufferSize) return 0;

    uint16_t bytesRead = 0;
    for (uint8_t i = 0; i < pageCount; i++) {
        uint8_t page = startPage + i;
        uint8_t pageBuf[4];

        // Try read, reactivate tag on failure and retry once
        if (!pn532_->mifareultralight_ReadPage(page, pageBuf)) {
            if (!reactivateTag()) return 0;
            if (!pn532_->mifareultralight_ReadPage(page, pageBuf)) {
                return 0;
            }
        }

        memcpy(buffer + (i * 4), pageBuf, 4);
        bytesRead += 4;
    }

    return bytesRead;
}

bool HardwareNFCConnectionPN532::writeISO14443Pages(
    uint8_t startPage, uint8_t pageCount, const uint8_t* data, uint16_t dataLen) {
    if (!pn532_ || !ready_) return false;

    uint16_t requiredLen = (uint16_t)pageCount * 4;
    if (dataLen < requiredLen) return false;

    for (uint8_t i = 0; i < pageCount; i++) {
        uint8_t page = startPage + i;
        const uint8_t* pageData = data + (i * 4);

        // Retry up to 2 times per page (matching PN5180 pattern)
        bool written = false;
        for (int attempt = 0; attempt < 3; attempt++) {
            if (pn532_->mifareultralight_WritePage(page, const_cast<uint8_t*>(pageData))) {
                written = true;
                break;
            }
            // Reactivate tag before retry
            if (!reactivateTag()) return false;
        }
        if (!written) return false;

        // Yield between writes to prevent FreeRTOS starvation
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    return true;
}

void HardwareNFCConnectionPN532::getReaderInfo(char* buf, size_t len) const {
    if (buf && len > 0) {
        if (!ready_) {
            snprintf(buf, len, "PN532 (not initialized)");
        } else {
            snprintf(buf, len, "PN532 v%d.%d", fwMajor_, fwMinor_);
        }
    }
}

void HardwareNFCConnectionPN532::logDiagnostics() {
    if (!pn532_ || !ready_) {
        Serial.println("PN532: Not initialized");
        return;
    }
    uint32_t ver = pn532_->getFirmwareVersion();
    if (ver) {
        Serial.printf("PN532: IC=0x%02X FW=%d.%d\n",
            (uint8_t)(ver >> 24), (uint8_t)(ver >> 16), (uint8_t)(ver >> 8));
    } else {
        Serial.println("PN532: No response during diagnostics");
    }
}

bool HardwareNFCConnectionPN532::ntagGetVersion(uint8_t* versionOut) {
    if (!pn532_ || !ready_ || !versionOut) return false;

    uint8_t cmd = 0x60;
    uint8_t response[8];
    uint8_t responseLength = sizeof(response);

    if (!pn532_->inDataExchange(&cmd, 1, response, &responseLength)) return false;
    if (responseLength < 8) return false;

    memcpy(versionOut, response, 8);
    return true;
}
