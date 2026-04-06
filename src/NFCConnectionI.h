#ifndef NFC_CONNECTION_I_H
#define NFC_CONNECTION_I_H

#include <cstdint>
#include <cstring>
#include "openprinttag_lib.h"

// Interface for NFC hardware abstraction
// Allows injection of test stubs without #ifdef guards
class NFCConnectionI {
public:
    virtual ~NFCConnectionI() = default;

    // Hardware initialization
    virtual bool begin() = 0;
    virtual void reset() = 0;
    virtual bool setupRF() = 0;

    // Full hardware reset + RF re-init for recovery from bad chip state
    virtual bool hardwareReset() = 0;

    // Tag detection - returns true if tag found, populates uid/uidLength
    virtual bool detectTag(uint8_t* uid, uint8_t* uidLength) = 0;

    // ISO14443A tag identification (valid after detectTag returns true for 14443A)
    // SAK byte identifies chip type: 0x00=NTAG/Ultralight, 0x08=MIFARE Classic 1K
    virtual uint8_t getLastSAK() const { return 0; }
    virtual uint16_t getLastATQA() const { return 0; }

    // NTAG GET_VERSION (0x60) — returns 8-byte version info for NTAG/Ultralight EV1.
    // versionOut must point to an 8-byte buffer. Returns true on success.
    virtual bool ntagGetVersion(uint8_t* versionOut) { return false; }

    // Set current UID for addressed read/write commands
    virtual void setCurrentUid(const uint8_t* uid, uint8_t length) = 0;

    // Get HAL for openprinttag library operations
    virtual opt_nfc_hal_t* getHal() = 0;

    // Read ISO14443A tag pages (NTAG213/215/216). Reactivates tag if needed.
    // Returns number of bytes read, or 0 on failure.
    virtual uint16_t readISO14443Pages(uint8_t startPage, uint8_t pageCount, uint8_t* buffer, uint16_t bufferSize) = 0;

    // Write ISO14443A tag pages (NTAG213/215/216). Writes 4 bytes per page.
    // Returns true if all pages written successfully.
    virtual bool writeISO14443Pages(uint8_t startPage, uint8_t pageCount, const uint8_t* data, uint16_t dataLen) = 0;

    // Reader identification for diagnostics (e.g. "PN5180 v3.4", "PN532 v1.6")
    virtual void getReaderInfo(char* buf, size_t len) const {
        if (buf && len > 0) { strncpy(buf, "unknown", len - 1); buf[len - 1] = '\0'; }
    }

    // Log hardware-specific diagnostic info (register dumps, etc.). Default: no-op.
    virtual void logDiagnostics() {}
};

#endif // NFC_CONNECTION_I_H
