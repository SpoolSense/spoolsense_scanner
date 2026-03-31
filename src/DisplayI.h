#pragma once

#include <cstdint>

// Spool data passed to the display — enough for both LCD text and TFT graphics.
struct DisplaySpoolData {
    char brand[32];
    char material[16];
    char colorHex[8];        // "RRGGBB" no leading #
    float remainingWeight;   // grams
    float totalWeight;       // grams
    uint8_t tagType;         // 0=unknown, 1=OpenPrintTag, 2=TigerTag, 3=OpenTag3D, 4=Bambu, 5=NFC+
};

// Display interface — implemented by LCDManager and TFTManager.
// ApplicationManager calls these methods without knowing which display is attached.
class DisplayI {
public:
    virtual ~DisplayI() = default;

    // Text display (status messages, errors, etc.)
    virtual void showText(const char* line1, const char* line2 = nullptr) = 0;
    virtual void showText4(const char* line1, const char* line2,
                           const char* line3, const char* line4) = 0;

    // Spool scanned — LCD shows text, TFT shows graphic with color swatch
    virtual void showSpool(const DisplaySpoolData& spool) = 0;

    // Keypad tool entry — LCD shows text, TFT shows big tool number
    virtual void showKeypad(const char* digits) = 0;

    // Write result — LCD shows text, TFT shows checkmark/X graphic
    virtual void showWriteResult(bool success, const char* format) = 0;

    // Screen timeout
    virtual void setScreenTimeoutMs(uint32_t timeoutMs) = 0;

    // OTA support — TFT frees sprite to reclaim heap for SSL
    virtual void freeForOTA() {}
    virtual void updateOTAProgress(uint8_t percent) { (void)percent; }
    virtual void showOTAError(const char* error) { (void)error; }
};
