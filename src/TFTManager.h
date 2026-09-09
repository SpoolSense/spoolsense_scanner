#pragma once

#if defined(BOARD_NO_TFT)

#include <Arduino.h>
#include "DisplayI.h"

// C5/C6 have one general-purpose SPI bus, reserved for NFC. Keeping a tiny
// compile-time stub lets shared application code remain board-agnostic while
// the build omits LovyanGFX and all TFT implementation objects.
enum class TFTDriver : uint8_t { ST7789, GC9A01, ILI9341, ILI9488, ST7796 };

class TFTManager : public DisplayI {
public:
    explicit TFTManager(TFTDriver = TFTDriver::ST7789) {}
    void begin() {}
    void startTask() {}
    void showBoot(const char*) {}
    void showWifiConnecting() {}
    void showWifiConnected(const char*) {}
    void showReady() {}
    void showSpoolScanned(const DisplaySpoolData&) {}
    void showWriting(const char*) {}
    void showWriteResult(bool, const char*) override {}
    void showKeypadEntry(const char*) {}
    void showError(const char*) {}
    void freeForOTA() override {}
    void updateOTAProgress(uint8_t) override {}
    void showOTAError(const char*) override {}
    void setScreenTimeoutMs(uint32_t) override {}
    void showText(const char*, const char* = nullptr) override {}
    void showText4(const char*, const char*, const char*, const char*) override {}
    void showSpool(const DisplaySpoolData&) override {}
    void showKeypad(const char*) override {}
    void showTrayDashboard(const TrayDashboardState&) override {}
};

#else

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "BoardPins.h"
#include "TFTConfig.h"
#include "DisplayI.h"
#include "TrayDashboardTypes.h"
#include "TFTDashboard.h"
#include "TFTLandscapeLayout.h"

// Tag type constants — one icon per type
#define TAG_TYPE_UNKNOWN      0
#define TAG_TYPE_OPENPRINTTAG 1
#define TAG_TYPE_TIGERTAG     2
#define TAG_TYPE_OPENTAG3D    3
#define TAG_TYPE_BAMBU        4
#define TAG_TYPE_NFC_PLAIN    5
#define TAG_TYPE_OPENSPOOL    6

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
    Error,
    TrayDashboard
};

// ---------------------------------------------------------------------------
// Internal message queued to the TFT task
// ---------------------------------------------------------------------------
struct TFTMessage {
    TFTState state;
    DisplaySpoolData spool; // valid when state == SpoolScanned
    char statusText[48];   // used for Boot/Ready/Error/Writing/KeypadEntry
    char statusText2[48];  // second line for generic status display
    bool writeSuccess;     // used for WriteResult
    TrayDashboardState dashboardState; // valid when state == TrayDashboard
};

// ---------------------------------------------------------------------------
// TFTManager
// ---------------------------------------------------------------------------
class TFTManager : public DisplayI {
public:
    TFTManager(TFTDriver driver = TFTDriver::ST7789);

    void begin();
    void startTask();

    // --- Call these from the main task, same callers that call LCDManager ---
    void showBoot(const char* version);
    void showWifiConnecting();
    void showWifiConnected(const char* ip);
    void showReady();
    void showSpoolScanned(const DisplaySpoolData& spool);
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
    void showTrayDashboard(const TrayDashboardState& state) override;

private:
    static void taskFunc(void* param);
    void taskLoop();
    void processQueue();

    // --- Rendering (bool renderers report a dropped frame, see render240Frame) ---
    bool renderBoot(const char* version);
    bool renderReady();
    bool renderSpoolScanned(const DisplaySpoolData& spool);
    bool renderSpoolScannedLandscape(const DisplaySpoolData& spool);  // ILI9488 480x320
    bool renderReadyLandscape();                                      // ILI9488 idle screen
    bool renderTextLandscape(const char* l1, const char* l2, uint32_t l1Color);  // ILI9488 status text
    bool renderLandscapeFrame(const DisplaySpoolData* spool);         // shared backend (nullptr=idle)
    void refreshStatusBar();                                          // periodic header-only update
    bool renderStatus(const char* line1, const char* line2 = nullptr);
    bool renderWriteResult(bool success, const char* tagFormat);
    bool renderKeypadEntry(const char* toolNumber);
    bool renderTrayDashboard(const TrayDashboardState& state);

    // Shared 240x240 backend: fills the background, runs drawBody over the
    // frame, and pushes it at the panel-aware origin. With a persistent full
    // framebuffer (PSRAM boards) that is one pass; otherwise a transient
    // 240x32 16-bit strip is drawn band-by-band. Bodies draw the full virtual
    // 240x240 frame shifted by -yOffset and must not read canvas dimensions
    // for layout (the canvas may be a strip band). Returns false if the frame
    // was dropped because the strip could not be allocated.
    template <typename DrawFn> bool render240Frame(DrawFn&& drawBody);

    // 240x240 frame bodies — same (canvas, yOffset) contract as drawLandscape*.
    void drawBoot240(LGFX_Sprite& canvas, int yOffset, const char* version);
    void drawReady240(LGFX_Sprite& canvas, int yOffset);
    void drawSpool240(LGFX_Sprite& canvas, int yOffset, const DisplaySpoolData& spool);
    void drawStatus240(LGFX_Sprite& canvas, int yOffset, const char* line1, const char* line2);
    void drawWriteResult240(LGFX_Sprite& canvas, int yOffset, bool success, const char* tagFormat);
    void drawKeypad240(LGFX_Sprite& canvas, int yOffset, const char* toolNumber);

    // --- Drawing helpers (y arrives already band-translated by the caller) ---
    void drawWeightBar(LGFX_Sprite& canvas, int x, int y, int w, int h,
                       float remaining, float total);
    void drawTagIcon(LGFX_Sprite& canvas, uint8_t tagType, int x, int y);
    void blitCanvas();  // push _sprite at the panel-aware (centered on wide panels) origin
    void clearWideGutters();  // blank the panel around the centered 240x240 area
    // Draw the full 480x320 landscape layout into any canvas, shifting all Y by
    // -yOffset (so one function fills a full sprite or a strip band).
    void drawLandscapeSpool(LGFX_Sprite& canvas, int yOffset, const DisplaySpoolData& spool);
    void drawLandscapeReady(LGFX_Sprite& canvas, int yOffset);                 // idle body
    void drawLandscapeText(LGFX_Sprite& canvas, int yOffset,
                           const char* line1, const char* line2, uint32_t line1Color);  // status text body
    void drawStatusBar(LGFX_Sprite& canvas, int yOffset);                       // shared top bar
    // Tinted 3D spool image: coil takes `tint` (brightness-gated), reel stays
    // neutral. (cx,cy) is the screen-space center; yOffset is the strip band.
    // `size` scales the source map (nearest-neighbor); SPOOL_IMG_W = native.
    void drawSpoolImage(LGFX_Sprite& canvas, int cx, int cy, uint32_t tint, int yOffset,
                        int size = 0);
    void drawWifiBars(LGFX_Sprite& canvas, int x, int y, int rssi, bool connected);
    void drawWifiIcon240(LGFX_Sprite& canvas, int yOffset);  // signal bars in the 240x240 header's top-right
    uint32_t hexToRgb(const char* hex);
    uint32_t dimColor(uint32_t color, uint8_t brightness); // for low-spool breathing

    LGFX _tft;
    LGFX_Sprite _sprite;
    // Transient render canvas (strip bands, landscape strips, status bar).
    // Member-owned rather than stack-local so freeForOTA can reclaim its
    // buffer: vTaskDelete does not unwind the render task's stack, so a kill
    // landing mid-frame would otherwise leak the allocation.
    LGFX_Sprite _strip;
    TFTDriver _driver;
    TFTDashboard _dashboard;

    QueueHandle_t _messageQueue;
    TaskHandle_t _taskHandle;
    // freeForOTA sets this to park the render task at a clean point (no bus
    // guard held, no transient sprite allocated) before deleting it —
    // vTaskDelete does not unwind the victim's stack.
    volatile bool _stopRequested = false;
    bool _began = false;  // panel + bus initialized; render task must not start otherwise
    bool _fullFrame = false;  // persistent 240x240 _sprite exists; false = strip rendering
    bool _wide = false;   // panel larger than 240x240 (ILI9488) — landscape-capable
    int  _blitOx = 0;     // centered-blit origin for the 240x240 sprite on wide panels
    int  _blitOy = 0;
    TFTState _currentState = TFTState::Boot;   // last-rendered state (drives idle status refresh)
    unsigned long _lastStatusRefreshMs = 0;    // throttles the periodic status-bar redraw

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
    static constexpr uint32_t STATUS_REFRESH_MS = 4000;  // idle status-bar redraw interval
};

#endif // BOARD_NO_TFT
