#ifndef HARDWARE_NFC_CONNECTION_PN532_H
#define HARDWARE_NFC_CONNECTION_PN532_H

#include "NFCConnectionI.h"
#include "BoardPins.h"
#include <Adafruit_PN532.h>

// NFC connection using Adafruit PN532 hardware (ISO14443A only)
class HardwareNFCConnectionPN532 : public NFCConnectionI {
public:
    HardwareNFCConnectionPN532();
    ~HardwareNFCConnectionPN532() override;

    bool begin() override;
    void reset() override;
    bool hardwareReset() override;
    bool setupRF() override;
    bool detectTag(uint8_t* uid, uint8_t* uidLength) override;
    uint8_t getLastSAK() const override { return lastSAK_; }
    uint16_t getLastATQA() const override { return lastATQA_; }
    bool ntagGetVersion(uint8_t* versionOut) override;
    void setCurrentUid(const uint8_t* uid, uint8_t length) override;
    opt_nfc_hal_t* getHal() override;
    uint16_t readISO14443Pages(uint8_t startPage, uint8_t pageCount, uint8_t* buffer, uint16_t bufferSize) override;
    bool writeISO14443Pages(uint8_t startPage, uint8_t pageCount, const uint8_t* data, uint16_t dataLen) override;
    void getReaderInfo(char* buf, size_t len) const override;
    void logDiagnostics() override;

private:
    Adafruit_PN532* pn532_ = nullptr;
    opt_nfc_hal_t hal_;
    uint8_t currentUid_[10];
    uint8_t currentUidLen_ = 0;
    uint8_t lastSAK_ = 0;
    uint16_t lastATQA_ = 0;
    uint8_t fwMajor_ = 0;
    uint8_t fwMinor_ = 0;
    bool ready_ = false;

    // Reactivate the currently selected tag (needed before page reads after timeout)
    bool reactivateTag();
};

#endif // HARDWARE_NFC_CONNECTION_PN532_H
