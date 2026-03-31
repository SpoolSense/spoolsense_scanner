#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "BoardPins.h"
#include "TFTConfig.h"
#include "DisplayI.h"

// ---------------------------------------------------------------------------
// TFTSpoolData — the data the TFT needs after a scan
// ---------------------------------------------------------------------------
struct TFTSpoolData {
    char brand[32];        // e.g. "eSUN"
    char material[16];     // e.g. "PLA"
    char name[48];         // e.g. "eSUN PLA+ White"
    char colorHex[8];      // e.g. "FFFFFF"  (no leading #)
    float remainingWeight; // grams remaining
    float totalWeight;     // grams total (for % bar)
    uint8_t tagType;       // TAG_TYPE_* constants below
};

// Tag type constants — one icon per type
#define TAG_TYPE_UNKNOWN      0
#define TAG_TYPE_OPENPRINTTAG 1
#define TAG_TYPE_TIGERTAG     2
#define TAG_TYPE_OPENTAG3D    3
#define TAG_TYPE_BAMBU        4
#define TAG_TYPE_NFC_PLAIN    5

// ---------------------------------------------------------------------------
// Display states
// ---------------------------------------------------------------------------
enum class TFTState {
    Boot,
    WifiConnecting,
    Ready,
    SpoolScanned,
    Writing,
    WriteResult,
    KeypadEntry,
    Error
};

// ---------------------------------------------------------------------------
// Internal message queued to the TFT task
// ---------------------------------------------------------------------------
struct TFTMessage {
    TFTState state;
    TFTSpoolData spool;    // valid when state == SpoolScanned
    char statusText[48];   // used for Boot/Ready/Error/Writing/KeypadEntry
    char statusText2[48];  // second line for generic status display
    bool writeSuccess;     // used for WriteResult
};

// ---------------------------------------------------------------------------
// TFTManager
// ---------------------------------------------------------------------------
class TFTManager : public DisplayI {
public:
    TFTManager();

    void begin();
    void startTask();

    // --- Call these from the main task, same callers that call LCDManager ---
    void showBoot(const char* version);
    void showWifiConnecting();
    void showWifiConnected(const char* ip);
    void showReady();
    void showSpoolScanned(const TFTSpoolData& spool);
    void showWriting(const char* tagFormat);
    void showWriteResult(bool success, const char* tagFormat) override;
    void showKeypadEntry(const char* toolNumber);
    void showError(const char* msg);

    // OTA support — free sprite heap, render progress directly to panel
    void freeForOTA() override;
    void updateOTAProgress(uint8_t percent) override;
    void showOTAError(const char* error) override;

    void setScreenTimeoutMs(uint32_t timeoutMs) override;

    // DisplayI interface
    void showText(const char* line1, const char* line2 = nullptr) override;
    void showText4(const char* line1, const char* line2,
                   const char* line3, const char* line4) override;
    void showSpool(const DisplaySpoolData& spool) override;
    void showKeypad(const char* digits) override;

private:
    static void taskFunc(void* param);
    void taskLoop();
    void processQueue();

    // --- Rendering ---
    void renderBoot(const char* version);
    void renderReady();
    void renderSpoolScanned(const TFTSpoolData& spool);
    void renderStatus(const char* line1, const char* line2 = nullptr);
    void renderWriteResult(bool success, const char* tagFormat);
    void renderKeypadEntry(const char* toolNumber);

    // --- Drawing helpers ---
    void drawSpool(int cx, int cy, int outerR, int innerR, uint32_t fillColor);
    void drawWeightBar(int x, int y, int w, int h, float remaining, float total);
    void drawTagIcon(uint8_t tagType, int x, int y);
    uint32_t hexToRgb(const char* hex);
    uint32_t dimColor(uint32_t color, uint8_t brightness); // for low-spool breathing

    LGFX _tft;
    LGFX_Sprite _sprite; // full-screen sprite for flicker-free rendering

    QueueHandle_t _messageQueue;
    TaskHandle_t _taskHandle;

    uint32_t _screenTimeoutMs;
    unsigned long _lastActivityMs;
    bool _screenOff;

    portMUX_TYPE _stateMux;

    // Breathing animation state (low spool)
    uint8_t _breathBrightness;
    int8_t _breathDirection;
    unsigned long _lastBreathMs;
    bool _isBreathing;
    uint32_t _breathColor;

    static constexpr uint32_t DEFAULT_SCREEN_TIMEOUT_MS = 30000;
    static constexpr uint32_t BREATH_STEP_MS = 20;
};
