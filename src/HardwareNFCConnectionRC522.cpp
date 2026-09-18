#include "HardwareNFCConnectionRC522.h"

#include "BoardPins.h"
#include "ConfigurationManager.h"
#include "SharedSPIBus.h"
#include "NfcSpiBus.h"

#include <Arduino.h>
#include <SPI.h>

// This file implements every hardware-facing operation declared by
// NFCConnectionI.h for an MFRC522/RC522 reader. NFCManager.cpp supplies the
// reader-independent tag classification and format parsing; this backend only
// performs discovery, raw NTAG page I/O, and MIFARE Classic authentication.
// HardwareNFCConnection.cpp and HardwareNFCConnectionPN532.cpp implement the
// same interface for the PN5180 and PN532 respectively.

namespace {
constexpr uint32_t RC522_SPI_HZ = 4000000;
constexpr uint8_t MAX_SUPPORTED_UID_LEN = 8;

const char* statusName(MFRC522::StatusCode status) {
    switch (status) {
        case MFRC522::StatusCode::STATUS_OK: return "OK";
        case MFRC522::StatusCode::STATUS_ERROR: return "communication error";
        case MFRC522::StatusCode::STATUS_COLLISION: return "collision";
        case MFRC522::StatusCode::STATUS_TIMEOUT: return "timeout/no tag";
        case MFRC522::StatusCode::STATUS_NO_ROOM: return "buffer too small";
        case MFRC522::StatusCode::STATUS_INTERNAL_ERROR: return "internal error";
        case MFRC522::StatusCode::STATUS_INVALID: return "invalid argument";
        case MFRC522::StatusCode::STATUS_CRC_WRONG: return "CRC mismatch";
        case MFRC522::StatusCode::STATUS_MIFARE_NACK: return "MIFARE NACK";
        default: return "unknown";
    }
}
}

HardwareNFCConnectionRC522::HardwareNFCConnectionRC522() {
    // RC522-specific: publish fail-fast HAL callbacks so an accidental
    // OpenPrintTag/ISO15693 request returns a defined error.
    hal_.read_page = unsupportedOpenPrintTagRead;
    hal_.write_page = unsupportedOpenPrintTagWrite;
    hal_.is_present = nullptr;
    hal_.user_ctx = this;
}

HardwareNFCConnectionRC522::~HardwareNFCConnectionRC522() {
    releaseReader();
}

void HardwareNFCConnectionRC522::releaseReader() {
    // RC522-specific: MFRC522v2 models these as three separately owned objects
    // whose references must remain valid for the reader object's lifetime.
    delete reader_;
    reader_ = nullptr;
    delete driver_;
    driver_ = nullptr;
    delete chipSelect_;
    chipSelect_ = nullptr;
    ready_ = false;
    tagSessionActive_ = false;
}

bool HardwareNFCConnectionRC522::begin() {
    // Implements NFCConnectionI::begin(). Pin values come from the same
    // ConfigurationManager slots used by PN5180/PN532; BUSY is not used.
    auto& cfg = ConfigurationManager::getInstance();
    pinRst_ = cfg.getNfcPin(NfcPinId::Rst);
    pinSs_ = cfg.getNfcPin(NfcPinId::Nss);
    pinSck_ = cfg.getNfcPin(NfcPinId::Sck);
    pinMosi_ = cfg.getNfcPin(NfcPinId::Mosi);
    pinMiso_ = cfg.getNfcPin(NfcPinId::Miso);

    Serial.printf("RC522: starting — RST=%u SS=%u SCK=%u MOSI=%u MISO=%u SPI=%lu Hz\n",
                  pinRst_, pinSs_, pinSck_, pinMosi_, pinMiso_,
                  static_cast<unsigned long>(RC522_SPI_HZ));

#if defined(BOARD_SHARED_SPI)
    if (!SharedSPIBus::begin(pinSck_, pinMiso_, pinMosi_, pinSs_, PIN_TFT_CS)) {
        Serial.println("RC522: shared SPI initialization failed");
        return false;
    }
    SPIClass* spiBus = &SharedSPIBus::bus();
#else
    // Arduino's global SPI is the TFT's host on the WROOM (VSPI) and the
    // S3-Zero (SPI2). Take the peripheral the PN5180 uses on this board.
    static SPIClass nfcSpi(PN5180_SPI_BUS);
    nfcSpi.begin(pinSck_, pinMiso_, pinMosi_, pinSs_);
    SPIClass* spiBus = &nfcSpi;
#endif

    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) {
        Serial.println("RC522: shared SPI lock timeout during initialization");
        return false;
    }

    releaseReader();

    // Most RC522 breakouts expose the MFRC522 NRSTPD pin as RST. Give the
    // chip a deterministic power-down/reset pulse before the library's soft reset.
    pinMode(pinRst_, OUTPUT);
    digitalWrite(pinRst_, LOW);
    delay(5);
    digitalWrite(pinRst_, HIGH);
    delay(50);

    chipSelect_ = new MFRC522DriverPinSimple(pinSs_);
    if (!chipSelect_) {
        Serial.println("RC522: failed to allocate chip-select driver");
        return false;
    }

    driver_ = new MFRC522DriverSPI(
        *chipSelect_, *spiBus, SPISettings(RC522_SPI_HZ, MSBFIRST, SPI_MODE0));
    if (!driver_) {
        Serial.println("RC522: failed to allocate SPI driver");
        releaseReader();
        return false;
    }

    reader_ = new MFRC522(*driver_);
    if (!reader_) {
        Serial.println("RC522: failed to allocate reader object");
        releaseReader();
        return false;
    }

    if (!reader_->PCD_Init()) {
        uint8_t observedVersion = static_cast<uint8_t>(reader_->PCD_GetVersion());
        Serial.printf("RC522: initialization failed — VersionReg=0x%02X; check 3.3V, GND, SS/SCK/MOSI/MISO, and RST\n",
                      observedVersion);
        releaseReader();
        return false;
    }

    versionReg_ = static_cast<uint8_t>(reader_->PCD_GetVersion());
    if (versionReg_ == 0x00 || versionReg_ == 0xFF) {
        Serial.printf("RC522: invalid version register 0x%02X — check wiring and 3.3V power\n",
                      versionReg_);
        releaseReader();
        return false;
    }

    reader_->PCD_AntennaOn();
    ready_ = true;
    Serial.printf("RC522: SPI communication OK — VersionReg=0x%02X, antenna gain=0x%02X\n",
                  versionReg_, static_cast<uint8_t>(reader_->PCD_GetAntennaGain()));
    Serial.println("RC522: initialized; polling for ISO14443A tags (OpenPrintTag/ISO15693 is unsupported)");
    return true;
}

void HardwareNFCConnectionRC522::reset() {
    // Implements NFCConnectionI::reset() using the MFRC522v2 soft-reset path.
    tagSessionActive_ = false;
    if (!reader_) return;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) {
        Serial.println("RC522: shared SPI lock timeout during reset");
        return;
    }
    reader_->PCD_StopCrypto1();
    // PCD_Init() performs the soft reset and then restores the timer,
    // modulation, CRC, and antenna registers cleared by that reset.
    ready_ = reader_->PCD_Init();
    if (ready_) {
        versionReg_ = static_cast<uint8_t>(reader_->PCD_GetVersion());
    } else {
        Serial.println("RC522: initialization failed during reset");
    }
    tagSessionActive_ = false;
}

bool HardwareNFCConnectionRC522::hardwareReset() {
    // Implements NFCConnectionI::hardwareReset(). Driving NRSTPD is specific
    // to the RC522 module; PCD_Init() then restores all reader registers.
    // Clear the previous state before any operation that can fail. Recovery
    // must not leave a stale active session or advertise a failed reader as
    // ready to NFCManager.
    ready_ = false;
    tagSessionActive_ = false;
    if (!reader_) return false;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) {
        Serial.println("RC522: shared SPI lock timeout during hardware reset");
        return false;
    }

    digitalWrite(pinRst_, LOW);
    delay(5);
    digitalWrite(pinRst_, HIGH);
    delay(50);
    if (!reader_->PCD_Init()) return false;

    versionReg_ = static_cast<uint8_t>(reader_->PCD_GetVersion());
    ready_ = versionReg_ != 0x00 && versionReg_ != 0xFF;
    if (ready_) reader_->PCD_AntennaOn();
    return ready_;
}

bool HardwareNFCConnectionRC522::setupRF() {
    // Implements NFCConnectionI::setupRF(). The RC522 library needs only the
    // antenna drivers enabled; PN5180 has a more involved RF configuration.
    if (!reader_ || !ready_) return false;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return false;
    reader_->PCD_AntennaOn();
    return true;
}

bool HardwareNFCConnectionRC522::detectTag(uint8_t* uid, uint8_t* uidLength) {
    // Implements NFCConnectionI::detectTag() with the RC522-specific
    // PICC_WakeupA/PICC_Select sequence. NFCManager later uses the saved SAK
    // and ATQA to distinguish NTAG/Ultralight from MIFARE Classic.
    if (!reader_ || !ready_ || !uid || !uidLength) return false;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) {
        Serial.println("RC522: tag poll skipped — shared SPI lock timeout");
        return false;
    }

    lastATQA_ = 0;
    lastSAK_ = 0;
    tagSessionActive_ = false;
    // A previous MIFARE Classic read may have left Crypto1 enabled. Clear it
    // before issuing WUPA/SELECT for the next scan cycle.
    reader_->PCD_StopCrypto1();

    byte atqa[2] = {0, 0};
    byte atqaLen = sizeof(atqa);
    MFRC522::StatusCode status = reader_->PICC_WakeupA(atqa, &atqaLen);
    if (status != MFRC522::StatusCode::STATUS_OK &&
        status != MFRC522::StatusCode::STATUS_COLLISION) {
        return false;
    }

    MFRC522::StatusCode selectStatus = reader_->PICC_Select(&reader_->uid);
    if (selectStatus != MFRC522::StatusCode::STATUS_OK) {
        Serial.printf("RC522: anticollision/select failed — %s (0x%02X)\n",
                      statusName(selectStatus), static_cast<uint8_t>(selectStatus));
        return false;
    }

    if (reader_->uid.size == 0 || reader_->uid.size > MAX_SUPPORTED_UID_LEN) {
        Serial.printf("RC522: unsupported UID length %u (firmware maximum is %u)\n",
                      reader_->uid.size, MAX_SUPPORTED_UID_LEN);
        reader_->PICC_HaltA();
        return false;
    }

    memcpy(uid, reader_->uid.uidByte, reader_->uid.size);
    *uidLength = reader_->uid.size;
    setCurrentUid(uid, *uidLength);
    lastSAK_ = reader_->uid.sak;
    if (atqaLen == 2) {
        lastATQA_ = (static_cast<uint16_t>(atqa[0]) << 8) | atqa[1];
    }
    tagSessionActive_ = true;
    return true;
}

void HardwareNFCConnectionRC522::setCurrentUid(const uint8_t* uid, uint8_t length) {
    // Implements NFCConnectionI::setCurrentUid(). In addition to keeping the
    // interface-level UID, MFRC522v2 requires its public uid field for Classic
    // authentication, which is unique to this backend's library integration.
    if (!uid || length == 0 || length > sizeof(currentUid_)) {
        currentUidLen_ = 0;
        return;
    }
    currentUidLen_ = length;
    memcpy(currentUid_, uid, length);
    if (reader_) {
        reader_->uid.size = length;
        memcpy(reader_->uid.uidByte, uid, length);
    }
}

bool HardwareNFCConnectionRC522::reactivateTagUnlocked() {
    // RC522-only retry helper (not part of NFCConnectionI). Re-selecting and
    // comparing the UID prevents a removed/replaced tag from receiving the
    // remainder of a multi-page read or write.
    if (!reader_ || currentUidLen_ == 0) return false;

    reader_->PCD_StopCrypto1();
    byte atqa[2] = {0, 0};
    byte atqaLen = sizeof(atqa);
    MFRC522::StatusCode status = reader_->PICC_WakeupA(atqa, &atqaLen);
    if (status != MFRC522::StatusCode::STATUS_OK &&
        status != MFRC522::StatusCode::STATUS_COLLISION) {
        return false;
    }

    MFRC522::Uid selected{};
    if (reader_->PICC_Select(&selected) != MFRC522::StatusCode::STATUS_OK) return false;
    if (selected.size != currentUidLen_ ||
        memcmp(selected.uidByte, currentUid_, currentUidLen_) != 0) {
        return false;
    }

    reader_->uid = selected;
    lastSAK_ = selected.sak;
    if (atqaLen == 2) {
        lastATQA_ = (static_cast<uint16_t>(atqa[0]) << 8) | atqa[1];
    }
    tagSessionActive_ = true;
    return true;
}

uint16_t HardwareNFCConnectionRC522::readISO14443Pages(
    uint8_t startPage, uint8_t pageCount, uint8_t* buffer,
    uint16_t bufferSize, bool keepSession) {
    // Implements NFCConnectionI::readISO14443Pages(). MFRC522 MIFARE_Read is
    // unique in returning four NTAG pages (16 bytes) per command plus CRC, so
    // this backend harvests the response in four-page chunks.
    if (!reader_ || !ready_ || !buffer || pageCount == 0) return 0;
    uint16_t totalBytes = static_cast<uint16_t>(pageCount) * 4;
    if (totalBytes > bufferSize) return 0;

    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return 0;

    if (!tagSessionActive_ && !reactivateTagUnlocked()) return 0;

    uint16_t bytesRead = 0;
    for (uint16_t pageOffset = 0; pageOffset < pageCount; pageOffset += 4) {
        byte response[18] = {0};
        byte responseLen = sizeof(response);
        uint8_t page = startPage + static_cast<uint8_t>(pageOffset);

        // A READ answers with 16 data bytes + 2 CRC bytes, and the library
        // reports all 18. It can also pass a shorter frame whose last two
        // bytes happen to be a valid CRC, which would put CRC bytes and
        // zeros into the page data — so anything but 18 is a failure.
        MFRC522::StatusCode status = reader_->MIFARE_Read(page, response, &responseLen);
        if (status != MFRC522::StatusCode::STATUS_OK || responseLen != sizeof(response)) {
            Serial.printf("RC522: page read failed at page %u — %s (0x%02X), %u bytes; reselecting tag\n",
                          page, statusName(status), static_cast<uint8_t>(status), responseLen);
            if (!reactivateTagUnlocked()) return 0;
            memset(response, 0, sizeof(response));
            responseLen = sizeof(response);
            status = reader_->MIFARE_Read(page, response, &responseLen);
            if (status != MFRC522::StatusCode::STATUS_OK || responseLen != sizeof(response)) {
                Serial.printf("RC522: page read retry failed at page %u — %s (0x%02X), %u bytes\n",
                              page, statusName(status), static_cast<uint8_t>(status), responseLen);
                return 0;
            }
        }

        uint16_t chunkBytes = static_cast<uint16_t>(pageCount - pageOffset) * 4;
        if (chunkBytes > 16) chunkBytes = 16;
        memcpy(buffer + pageOffset * 4, response, chunkBytes);
        bytesRead += chunkBytes;
    }

    if (!keepSession) {
        reader_->PICC_HaltA();
        tagSessionActive_ = false;
    }
    Serial.printf("RC522: read %u bytes from NTAG pages %u-%u\n",
                  bytesRead, startPage, startPage + pageCount - 1);
    return bytesRead;
}

bool HardwareNFCConnectionRC522::writeISO14443Pages(
    uint8_t startPage, uint8_t pageCount, const uint8_t* data, uint16_t dataLen) {
    // Implements NFCConnectionI::writeISO14443Pages() through the RC522
    // library's four-byte MIFARE_Ultralight_Write command.
    if (!reader_ || !ready_ || !data || pageCount == 0) return false;
    uint16_t requiredLen = static_cast<uint16_t>(pageCount) * 4;
    if (dataLen < requiredLen) return false;

    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return false;

    if (!tagSessionActive_ && !reactivateTagUnlocked()) return false;

    for (uint8_t i = 0; i < pageCount; i++) {
        bool written = false;
        for (uint8_t attempt = 0; attempt < 3; attempt++) {
            byte pageData[4];
            memcpy(pageData, data + static_cast<uint16_t>(i) * 4, sizeof(pageData));
            if (reader_->MIFARE_Ultralight_Write(startPage + i, pageData, sizeof(pageData)) ==
                MFRC522::StatusCode::STATUS_OK) {
                written = true;
                break;
            }
            if (!reactivateTagUnlocked()) return false;
        }
        if (!written) return false;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    tagSessionActive_ = true;
    return true;
}

void HardwareNFCConnectionRC522::endTagSession() {
    // Implements NFCConnectionI::endTagSession(). Stopping Crypto1 is an
    // RC522/MIFARE-specific cleanup required before returning to tag polling.
    if (!reader_ || !ready_) return;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return;
    reader_->PCD_StopCrypto1();
    if (tagSessionActive_) reader_->PICC_HaltA();
    tagSessionActive_ = false;
}

bool HardwareNFCConnectionRC522::ntagGetVersion(uint8_t* versionOut) {
    // Implements NFCConnectionI::ntagGetVersion(). MFRC522v2 has no dedicated
    // GET_VERSION wrapper, so constructing command 0x60, appending CRC_A, and
    // validating the response here is unique to the RC522 backend.
    if (!reader_ || !ready_ || !versionOut) return false;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return false;

    byte command[3] = {0x60, 0, 0};
    if (reader_->PCD_CalculateCRC(command, 1, command + 1) !=
        MFRC522::StatusCode::STATUS_OK) {
        return false;
    }

    byte response[10] = {0};
    byte responseLen = sizeof(response);
    MFRC522::StatusCode status = reader_->PCD_TransceiveData(
        command, sizeof(command), response, &responseLen, nullptr, 0, true);
    // 8 version bytes + 2 CRC bytes. A shorter valid-CRC frame would put a CRC
    // byte where the storage-size byte is read, and mis-size the tag.
    if (status != MFRC522::StatusCode::STATUS_OK || responseLen != sizeof(response)) {
        Serial.printf("RC522: NTAG GET_VERSION failed — %s (0x%02X), response bytes=%u\n",
                      statusName(status), static_cast<uint8_t>(status), responseLen);
        return false;
    }
    memcpy(versionOut, response, 8);
    Serial.printf("RC522: NTAG GET_VERSION response=");
    for (uint8_t i = 0; i < 8; i++) Serial.printf("%02X", versionOut[i]);
    Serial.println();
    return true;
}

bool HardwareNFCConnectionRC522::mifareAuthenticate(
    uint8_t blockNo, uint8_t keyType, const uint8_t* key) {
    // Implements NFCConnectionI::mifareAuthenticate() for Bambu tags using the
    // MFRC522v2 Crypto1 authentication primitive.
    if (!reader_ || !ready_ || !key || currentUidLen_ < 4) return false;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return false;

    MFRC522::MIFARE_Key mifareKey{};
    memcpy(mifareKey.keyByte, key, sizeof(mifareKey.keyByte));
    byte command = keyType == 0x61
        ? MFRC522::PICC_Command::PICC_CMD_MF_AUTH_KEY_B
        : MFRC522::PICC_Command::PICC_CMD_MF_AUTH_KEY_A;
    MFRC522::StatusCode status =
        reader_->PCD_Authenticate(command, blockNo, &mifareKey, &reader_->uid);
    if (status != MFRC522::StatusCode::STATUS_OK) {
        Serial.printf("RC522: MIFARE authentication failed for block %u — %s (0x%02X)\n",
                      blockNo, statusName(status), static_cast<uint8_t>(status));
        return false;
    }
    return true;
}

bool HardwareNFCConnectionRC522::mifareClassicRead(uint8_t blockNo, uint8_t* buffer) {
    // Implements NFCConnectionI::mifareClassicRead() for the 16-byte blocks
    // consumed by NFCManager::readBambuTag().
    if (!reader_ || !ready_ || !buffer) return false;
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) return false;

    byte response[18] = {0};
    byte responseLen = sizeof(response);
    MFRC522::StatusCode status = reader_->MIFARE_Read(blockNo, response, &responseLen);
    if (status != MFRC522::StatusCode::STATUS_OK || responseLen != sizeof(response)) {
        Serial.printf("RC522: MIFARE block %u read failed — %s (0x%02X), response bytes=%u\n",
                      blockNo, statusName(status), static_cast<uint8_t>(status), responseLen);
        return false;
    }
    memcpy(buffer, response, 16);
    return true;
}

opt_nfc_hal_t* HardwareNFCConnectionRC522::getHal() {
    // Implements NFCConnectionI::getHal(). This deliberately returns the two
    // unsupported callbacks below because RC522 cannot scan ISO15693 tags.
    return &hal_;
}

opt_error_t HardwareNFCConnectionRC522::unsupportedOpenPrintTagRead(
    void*, uint8_t, uint8_t*) {
    // RC522-only capability boundary: the chip has no ISO15693 radio mode.
    return OPT_ERR_NFC_READ;
}

opt_error_t HardwareNFCConnectionRC522::unsupportedOpenPrintTagWrite(
    void*, uint8_t, const uint8_t*) {
    // RC522-only capability boundary: the chip has no ISO15693 radio mode.
    return OPT_ERR_NFC_WRITE;
}

void HardwareNFCConnectionRC522::getReaderInfo(char* buf, size_t len) const {
    // Implements NFCConnectionI::getReaderInfo() for the diagnostics API/UI.
    if (!buf || len == 0) return;
    if (!ready_) {
        snprintf(buf, len, "RC522 (not initialized)");
    } else {
        snprintf(buf, len, "RC522 reg 0x%02X", versionReg_);
    }
}

void HardwareNFCConnectionRC522::logDiagnostics() {
    // Implements NFCConnectionI::logDiagnostics(). VersionReg and antenna gain
    // are MFRC522-specific health indicators.
    if (!reader_ || !ready_) {
        Serial.println("RC522: not initialized");
        return;
    }
    SharedSPIBus::Guard spiGuard;
    if (!spiGuard) {
        Serial.println("RC522: shared SPI lock timeout during diagnostics");
        return;
    }
    uint8_t version = static_cast<uint8_t>(reader_->PCD_GetVersion());
    Serial.printf("RC522: version register 0x%02X, antenna gain 0x%02X\n",
                  version, static_cast<uint8_t>(reader_->PCD_GetAntennaGain()));
}

bool HardwareNFCConnectionRC522::getDiagnosticSnapshot(ReaderDiagnostics& out) {
    // Implements NFCConnectionI::getDiagnosticSnapshot(). Unlike PN5180, the
    // RC522 backend does not expose the common PN5180 register-status block.
    memset(&out, 0, sizeof(out));
    getReaderInfo(out.reader_name, sizeof(out.reader_name));
    out.initialized = ready_;
    out.fw_major = versionReg_ >> 4;
    out.fw_minor = versionReg_ & 0x0F;
    out.has_registers = false;
    return true;
}
