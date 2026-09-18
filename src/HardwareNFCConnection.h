#ifndef HARDWARE_NFC_CONNECTION_H
#define HARDWARE_NFC_CONNECTION_H

#include "NFCConnectionI.h"
#include "BoardPins.h"
#include <PN5180.h>
#include <PN5180ISO15693.h>
#include <PN5180ISO14443.h>

// Production NFC connection using PN5180 hardware
class HardwareNFCConnection : public NFCConnectionI {
public:
    HardwareNFCConnection();
    ~HardwareNFCConnection() override;

    bool begin() override;
    void reset() override;
    bool hardwareReset() override;
    bool setupRF() override;
    bool detectTag(uint8_t* uid, uint8_t* uidLength) override;
    void setCurrentUid(const uint8_t* uid, uint8_t length) override;
    opt_nfc_hal_t* getHal() override;
    uint16_t readISO14443Pages(uint8_t startPage, uint8_t pageCount, uint8_t* buffer, uint16_t bufferSize, bool keepSession = false) override;
    void endTagSession() override;
    bool writeISO14443Pages(uint8_t startPage, uint8_t pageCount, const uint8_t* data, uint16_t dataLen) override;
    uint8_t getLastSAK() const override { return lastSAK_; }
    uint16_t getLastATQA() const override { return lastATQA_; }
    bool ntagGetVersion(uint8_t* versionOut) override;
    bool mifareAuthenticate(uint8_t blockNo, uint8_t keyType, const uint8_t* key) override;
    bool mifareClassicRead(uint8_t blockNo, uint8_t* buffer) override;
    void getReaderInfo(char* buf, size_t len) const override;
    // Diagnostics: log RF_STATUS, IRQ_STATUS, SYSTEM_STATUS registers
    void logDiagnostics() override;
    // Structured snapshot: firmware + RF/IRQ/SYSTEM registers + bus-wedged latch.
    bool getDiagnosticSnapshot(ReaderDiagnostics& out) override;
    // Returns PN5180 firmware version bytes (set during begin()). fw[0]=minor, fw[1]=major.
    void getPN5180FirmwareVersion(uint8_t fw[2]) const { fw[0] = fw_[0]; fw[1] = fw_[1]; }
    bool isPN5180Ready() const { return pn5180Ready_; }

private:
    // Effective NFC pins, captured once from ConfigurationManager at begin()
    // — runtime overrides (#201) apply on the post-save reboot
    uint8_t pinRst_ = 0, pinNss_ = 0, pinBusy_ = 0;
    uint8_t pinSck_ = 0, pinMosi_ = 0, pinMiso_ = 0;
    PN5180ISO15693* nfc_ = nullptr;
    PN5180ISO14443* iso14443a_ = nullptr;
    opt_nfc_hal_t hal_;
    uint8_t currentUid_[8];
    uint8_t currentUidLen_ = 0;

    // Read cache: filled by a single readMultipleBlocks call on first halReadPage
    // access, then served page-by-page. Invalidated when a new tag is presented.
    static constexpr uint8_t READ_CACHE_PAGES = 78;
    uint8_t readCache_[READ_CACHE_PAGES * 4];
    bool readCacheValid_ = false;

    // ISO14443A identification (populated by detectTag)
    uint8_t lastSAK_ = 0;
    uint16_t lastATQA_ = 0;

    // NTAG version (populated by detectTag while tag is active)
    uint8_t lastVersion_[8] = {0};
    bool lastVersionValid_ = false;

    // Tag session: true after activateTypeA succeeds in detectTag, cleared after halt
    // Allows readISO14443Pages to skip redundant setupRF + re-activation
    bool tagSessionActive_ = false;

    // PN5180 firmware version (populated during begin())
    uint8_t fw_[2] = {0, 0};
    bool pn5180Ready_ = false;

    // Static HAL callbacks
    static opt_error_t halReadPage(void* ctx, uint8_t page, uint8_t* buffer);
    static opt_error_t halWritePage(void* ctx, uint8_t page, const uint8_t* data);

    // A reactivation can bring up whichever tag is now in the field. Writing to a
    // tag other than the one detected corrupts a different spool's data and then
    // verifies the corruption successfully, so every reactivation must prove
    // identity. Mirrors HardwareNFCConnectionPN532::reactivateTag().
    bool activatedTagMatches(const uint8_t* response, uint8_t uidLen) const;
};

#endif // HARDWARE_NFC_CONNECTION_H
