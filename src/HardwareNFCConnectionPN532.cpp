#include "HardwareNFCConnectionPN532.h"
#include "ConfigurationManager.h"
#include "openprinttag_adafruit_pn532.h"
#include "BoardPins.h"
#include "SharedSPIBus.h"

#include <Arduino.h>
#include <SPI.h>

// PN532 ISO14443A NFC reader (4-byte and 7-byte tags: NTAG, MIFARE). Adafruit_PN532 stores response
// data in a file-scope global buffer; after readPassiveTargetID,
// ATQA and SAK extracted from bytes 9-10 and 11 respectively.
extern byte pn532_packetbuffer[];

HardwareNFCConnectionPN532::HardwareNFCConnectionPN532() {
    memset(&hal_, 0, sizeof(hal_));
    memset(currentUid_, 0, sizeof(currentUid_));
}

HardwareNFCConnectionPN532::~HardwareNFCConnectionPN532() {
    delete pn532_;
}

bool HardwareNFCConnectionPN532::begin() {
    // Runtime pin overrides (#201): PN532 shares the NFC pin slots (SS=NSS; BUSY unused).
    // BOARD_SHARED_SPI injects the same global bus used by the TFT.
    {
        auto& cfg = ConfigurationManager::getInstance();
        pinRst_ = cfg.getNfcPin(NfcPinId::Rst);
        pinSs_  = cfg.getNfcPin(NfcPinId::Nss);
        pinSck_ = cfg.getNfcPin(NfcPinId::Sck);
        pinMosi_ = cfg.getNfcPin(NfcPinId::Mosi);
        pinMiso_ = cfg.getNfcPin(NfcPinId::Miso);
    }
#if defined(BOARD_SHARED_SPI)
    if (!SharedSPIBus::begin(pinSck_, pinMiso_, pinMosi_, pinSs_, PIN_TFT_CS)) {
        Serial.println("PN532: shared SPI initialization failed");
        return false;
    }
    SPIClass* spiBus = &SharedSPIBus::bus();
#else
    SPI.begin(pinSck_, pinMiso_, pinMosi_, pinSs_);
    SPIClass* spiBus = &SPI;
#endif

    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) {
        Serial.println("PN532: shared SPI lock timeout during initialization");
        return false;
    }

    pn532_ = new Adafruit_PN532(pinSs_, spiBus);
    if (!pn532_) {
        Serial.println("PN532: Failed to allocate");
        return false;
    }

    pn532_->begin();

    // Firmware read proves SPI communication is working; loss of response indicates hardware failure
    uint32_t versiondata = pn532_->getFirmwareVersion();
    if (!versiondata) {
        Serial.println("PN532: No response — check wiring");
        delete pn532_;
        pn532_ = nullptr;
        return false;
    }

    // getFirmwareVersion layout: IC (chip ID) in [24:31], FW major in [16:23], minor in [8:15]
    fwMajor_ = (versiondata >> 16) & 0xFF;
    fwMinor_ = (versiondata >> 8) & 0xFF;
    Serial.printf("PN532: Found IC=0x%02X firmware v%d.%d\n",
        (uint8_t)((versiondata >> 24) & 0xFF), fwMajor_, fwMinor_);

    // SAMConfig activates Normal mode (bit 0) + enables AutoISO14443B; required before tag reads
    if (!pn532_->SAMConfig()) {
        Serial.println("PN532: SAMConfig failed");
        delete pn532_;
        pn532_ = nullptr;
        return false;
    }

    // Create HAL interface bridge to openprinttag library
    hal_ = opt_create_adafruit_pn532_hal(pn532_);

    ready_ = true;
    Serial.println("PN532: Initialized (ISO14443A only)");
    return true;
}

void HardwareNFCConnectionPN532::reset() {
    if (pn532_) {
        SharedSPIBus::Guard spiGuard;
        if (!spiGuard) {
            Serial.println("PN532: shared SPI lock timeout during reset");
            return;
        }
        // Soft reset: reinit SPI comms and re-enable detection mode
        pn532_->begin();
        if (!pn532_->SAMConfig()) {
            Serial.println("PN532: SAMConfig failed during reset");
        }
    }
}

bool HardwareNFCConnectionPN532::hardwareReset() {
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) {
        Serial.println("PN532: shared SPI lock timeout during hardware reset");
        return false;
    }

    // Toggle RST pin for hardware reset: forces state machine reboot
    pinMode(pinRst_, OUTPUT);
    digitalWrite(pinRst_, LOW);
    delay(10);
    digitalWrite(pinRst_, HIGH);
    delay(50);  // PN532 boot time before first SPI command

    if (pn532_) {
        pn532_->begin();
        uint32_t ver = pn532_->getFirmwareVersion();  // verify comms restored
        if (!ver) return false;
        if (!pn532_->SAMConfig()) return false;  // re-enable detection after hard reset
    }
    return true;
}

bool HardwareNFCConnectionPN532::setupRF() {
    // PN532 RF state is managed automatically by firmware; no manual config needed
    return ready_;
}

bool HardwareNFCConnectionPN532::detectTag(uint8_t* uid, uint8_t* uidLength) {
    if (!pn532_ || !ready_) return false;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return false;

    // 100ms timeout is safe compromise: detects tags fast enough for scan loop, but avoids
    // blocking on non-responsive tags or noise that causes readPassiveTargetID to hang
    uint8_t uidLen = 0;
    bool found = pn532_->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 100);
    if (!found || uidLen == 0) return false;

    *uidLength = uidLen;

    // Extract ATQA/SAK from Adafruit's global buffer after readPassiveTargetID parses the
    // InListPassiveTarget frame. pn532_packetbuffer is populated by readdata() inside the
    // Adafruit library; layout: [9-10]=ATQA (big-endian), [11]=SAK, [13+]=UID bytes
    lastATQA_ = ((uint16_t)pn532_packetbuffer[9] << 8) | pn532_packetbuffer[10];
    lastSAK_ = pn532_packetbuffer[11];

    return true;
}

void HardwareNFCConnectionPN532::setCurrentUid(const uint8_t* uid, uint8_t length) {
    currentUidLen_ = (length <= sizeof(currentUid_)) ? length : sizeof(currentUid_);
    memcpy(currentUid_, uid, currentUidLen_);  // track current tag for reactivation verification
}

opt_nfc_hal_t* HardwareNFCConnectionPN532::getHal() {
    return &hal_;
}

bool HardwareNFCConnectionPN532::reactivateTag() {
    if (!pn532_) return false;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return false;
    uint8_t uid[10];
    uint8_t uidLen = 0;
    if (!pn532_->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200))
        return false;
    // Verify same tag still present: guards against user swapping tags during multi-block ops
    // (cross-tag writes would corrupt different spool's filament type or tool assignment)
    if (uidLen != currentUidLen_ || memcmp(uid, currentUid_, uidLen) != 0)
        return false;
    return true;
}

uint16_t HardwareNFCConnectionPN532::readISO14443Pages(
    uint8_t startPage, uint8_t pageCount, uint8_t* buffer, uint16_t bufferSize, bool /*keepSession*/) {
    if (!pn532_ || !ready_) return 0;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return 0;

    uint16_t totalBytes = (uint16_t)pageCount * 4;
    if (totalBytes > bufferSize) return 0;

    // The NTAG READ command behind ntag2xx_ReadPage always returns
    // 16 bytes (4 pages); the library keeps 4 and leaves the whole response in
    // its file-scope packet buffer (frame: [7]=status, [8..23]=data), which
    // this file already scrapes for ATQA/SAK. Harvesting all 16 bytes per
    // round-trip cuts radio exchanges 4x — a 10-page classify read costs 3
    // exchanges instead of 10, a 50-page NDEF read 13 instead of 50 (#242).
    // Uses ntag2xx_ReadPage instead of mifareultralight_ReadPage: the old
    // method refuses pages >= 64, too small for a v2 OpenTag3D payload
    // (pages 4..66). Both send the identical radio command and fill the same
    // packet buffer, so the harvest below is unchanged.
    uint16_t bytesRead = 0;
    for (uint16_t i = 0; i < pageCount; i += 4) {
        uint8_t page = startPage + (uint8_t)i;
        uint8_t pageBuf[4];

        // Per-chunk retry: tag may lose activation on RF noise; reactivate and retry once
        if (!pn532_->ntag2xx_ReadPage(page, pageBuf)) {
            if (!reactivateTag()) return 0;  // reactivateTag also verifies tag hasn't changed
            if (!pn532_->ntag2xx_ReadPage(page, pageBuf)) {
                return 0;  // permanent failure after retry; caller can retry entire sequence
            }
        }

        // Copy up to 4 pages from the response frame, bounded by the caller's
        // request — never past it, so a READ that straddles the tag's last
        // page can't leak roll-over data into the result
        uint16_t chunkBytes = (uint16_t)(pageCount - i) * 4;
        if (chunkBytes > 16) chunkBytes = 16;
        memcpy(buffer + (i * 4), pn532_packetbuffer + 8, chunkBytes);
        bytesRead += chunkBytes;
    }

    return bytesRead;
}

bool HardwareNFCConnectionPN532::writeISO14443Pages(
    uint8_t startPage, uint8_t pageCount, const uint8_t* data, uint16_t dataLen) {
    if (!pn532_ || !ready_) return false;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return false;

    uint16_t requiredLen = (uint16_t)pageCount * 4;
    if (dataLen < requiredLen) return false;

    for (uint8_t i = 0; i < pageCount; i++) {
        uint8_t page = startPage + i;
        const uint8_t* pageData = data + (i * 4);

        // Retry up to 3 attempts per page: matches PN5180 reliability; reactivate before each retry
        bool written = false;
        for (int attempt = 0; attempt < 3; attempt++) {
            if (pn532_->ntag2xx_WritePage(page, const_cast<uint8_t*>(pageData))) {
                written = true;
                break;
            }
            // Tag loses activation on write error; reactivate and verify it's still the same tag
            if (!reactivateTag()) return false;
        }
        if (!written) return false;  // all 3 attempts failed; abort to prevent partial writes

        // Stagger writes to prevent command queue overflow and RF interference
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    return true;
}

void HardwareNFCConnectionPN532::getReaderInfo(char* buf, size_t len) const {
    if (buf && len > 0) {
        if (!ready_) {
            snprintf(buf, len, "PN532 (not initialized)");
        } else {
            snprintf(buf, len, "PN532 v%d.%d", fwMajor_, fwMinor_);  // cached at init
        }
    }
}

bool HardwareNFCConnectionPN532::getDiagnosticSnapshot(ReaderDiagnostics& out) {
    memset(&out, 0, sizeof(out));
    getReaderInfo(out.reader_name, sizeof(out.reader_name));
    out.initialized = ready_;
    out.fw_major = fwMajor_;
    out.fw_minor = fwMinor_;
    out.has_registers = false;   // firmware-managed radio; no register interface
    out.sam_config_ok = ready_;  // begin() sets ready_ only after SAMConfig() succeeds
    return true;
}

void HardwareNFCConnectionPN532::logDiagnostics() {
    if (!pn532_ || !ready_) {
        Serial.println("PN532: Not initialized");
        return;
    }
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) {
        Serial.println("PN532: shared SPI lock timeout during diagnostics");
        return;
    }
    // Re-read firmware version to verify SPI comms still working
    uint32_t ver = pn532_->getFirmwareVersion();
    if (ver) {
        Serial.printf("PN532: IC=0x%02X FW=%d.%d\n",
            (uint8_t)(ver >> 24), (uint8_t)(ver >> 16), (uint8_t)(ver >> 8));
    } else {
        Serial.println("PN532: No response during diagnostics — SPI bus may be hung");
    }
}

bool HardwareNFCConnectionPN532::mifareAuthenticate(uint8_t blockNo, uint8_t keyType, const uint8_t* key) {
    if (!pn532_ || !ready_) return false;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return false;
    uint8_t keyNumber = (keyType == 0x61) ? 1 : 0;
    return pn532_->mifareclassic_AuthenticateBlock(
        currentUid_, currentUidLen_, blockNo, keyNumber, const_cast<uint8_t*>(key));
}

bool HardwareNFCConnectionPN532::mifareClassicRead(uint8_t blockNo, uint8_t* buffer) {
    if (!pn532_ || !ready_) return false;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return false;
    return pn532_->mifareclassic_ReadDataBlock(blockNo, buffer);
}

bool HardwareNFCConnectionPN532::ntagGetVersion(uint8_t* versionOut) {
    if (!pn532_ || !ready_ || !versionOut) return false;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return false;

    uint8_t cmd = 0x60;  // NTAG GET_VERSION command
    uint8_t response[8];
    uint8_t responseLength = sizeof(response);

    if (!pn532_->inDataExchange(&cmd, 1, response, &responseLength)) return false;
    if (responseLength < 8) return false;

    memcpy(versionOut, response, 8);
    return true;
}
