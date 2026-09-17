#ifndef HARDWARE_NFC_CONNECTION_RC522_H
#define HARDWARE_NFC_CONNECTION_RC522_H

#include "NFCConnectionI.h"

#include <MFRC522DriverPinSimple.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522v2.h>

// MFRC522/RC522 implementation of the reader-independent contract in
// NFCConnectionI.h. NFCManager.cpp calls only that interface, so this class
// fills the same role as HardwareNFCConnection (PN5180) and
// HardwareNFCConnectionPN532. Unlike the PN5180 backend, an RC522 supports
// ISO14443A only; the OpenPrintTag HAL therefore reports unsupported reads and
// writes instead of attempting ISO15693 operations.
class HardwareNFCConnectionRC522 : public NFCConnectionI {
public:
    HardwareNFCConnectionRC522();
    ~HardwareNFCConnectionRC522() override;

    bool begin() override;
    void reset() override;
    bool hardwareReset() override;
    bool setupRF() override;
    bool detectTag(uint8_t* uid, uint8_t* uidLength) override;
    uint8_t getLastSAK() const override { return lastSAK_; }
    uint16_t getLastATQA() const override { return lastATQA_; }
    bool ntagGetVersion(uint8_t* versionOut) override;
    bool mifareAuthenticate(uint8_t blockNo, uint8_t keyType, const uint8_t* key) override;
    bool mifareClassicRead(uint8_t blockNo, uint8_t* buffer) override;
    void setCurrentUid(const uint8_t* uid, uint8_t length) override;
    opt_nfc_hal_t* getHal() override;
    uint16_t readISO14443Pages(uint8_t startPage, uint8_t pageCount,
                               uint8_t* buffer, uint16_t bufferSize,
                               bool keepSession = false) override;
    void endTagSession() override;
    bool writeISO14443Pages(uint8_t startPage, uint8_t pageCount,
                            const uint8_t* data, uint16_t dataLen) override;
    void getReaderInfo(char* buf, size_t len) const override;
    void logDiagnostics() override;
    bool getDiagnosticSnapshot(ReaderDiagnostics& out) override;

private:
    // These objects are unique to the RC522 backend. The MFRC522v2 library
    // separates chip-select control, SPI transport, and the reader protocol
    // into three objects, while the PN5180 and PN532 backends use their own
    // libraries' reader objects directly.
    uint8_t pinRst_ = 0;
    uint8_t pinSs_ = 0;
    uint8_t pinSck_ = 0;
    uint8_t pinMosi_ = 0;
    uint8_t pinMiso_ = 0;

    MFRC522DriverPinSimple* chipSelect_ = nullptr;
    MFRC522DriverSPI* driver_ = nullptr;
    MFRC522* reader_ = nullptr;
    opt_nfc_hal_t hal_{};

    uint8_t currentUid_[8]{};
    uint8_t currentUidLen_ = 0;
    uint8_t lastSAK_ = 0;
    uint16_t lastATQA_ = 0;
    uint8_t versionReg_ = 0;
    bool ready_ = false;
    bool tagSessionActive_ = false;

    // RC522-specific helpers. reactivateTagUnlocked() performs WUPA + SELECT
    // and verifies that the UID still matches before a retry; its name also
    // records that the caller must already hold SharedSPIBus::Guard.
    bool reactivateTagUnlocked();

    // RC522-specific ownership cleanup for the three MFRC522v2 objects above.
    void releaseReader();

    // RC522-specific OpenPrintTag stubs. OpenPrintTag requires ISO15693, which
    // MFRC522 hardware cannot provide, so these callbacks fail explicitly.
    static opt_error_t unsupportedOpenPrintTagRead(void* ctx, uint8_t page, uint8_t* buffer);
    static opt_error_t unsupportedOpenPrintTagWrite(void* ctx, uint8_t page, const uint8_t* data);
};

#endif // HARDWARE_NFC_CONNECTION_RC522_H
