#include "TFTManager.h"
#include "ConfigurationManager.h"
#include "MemoryDiagnostics.h"
#include "SharedSPIBus.h"
#include "TaskUtils.h"
#include "HomeAssistantManager.h"
#include "PrinterManager.h"
#include "SpoolImage.h"
#include <WiFi.h>
#include <Arduino.h>

// TFT display controller (ST7789/GC9A01 240x240, ILI9341/ILI9488/ST7796). All screens
// render through sprites for flicker-free updates: PSRAM boards hold a
// persistent 16-bit full framebuffer, no-PSRAM boards draw each frame through
// a transient 16-bit strip band (see render240Frame / renderLandscapeFrame).
// Also manages screen timeout, low-spool breathing, and OTA progress. Task
// runs on core 0.

// Color constants for UI elements
// ────────────────────────────────────────────────────────────────────────
static const uint32_t COLOR_BG        = 0x000000;
static const uint32_t COLOR_HEADER_BG = 0x1A1A2E;
static const uint32_t COLOR_TEXT      = 0xFFFFFF;
static const uint32_t COLOR_SUBTEXT   = 0xAAAAAA;
static const uint32_t COLOR_SPOOL_RIM = 0x444444;
static const uint32_t COLOR_SPOOL_HUB = 0x222222;
static const uint32_t COLOR_BAR_BG    = 0x333333;
static const uint32_t COLOR_BAR_FG    = 0x00CC66;
static const uint32_t COLOR_BAR_LOW   = 0xFF4444;
static const uint32_t COLOR_ACCENT    = 0x4FC3F7;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
TFTManager::TFTManager(TFTDriver driver)
    : _tft(driver),
      _sprite(&_tft),
      _strip(&_tft),
      _messageQueue(nullptr),
      _taskHandle(nullptr),
      _screenTimeoutMs(DEFAULT_SCREEN_TIMEOUT_MS),
      _lastActivityMs(0),
      _screenOff(false),
      _stateMux(portMUX_INITIALIZER_UNLOCKED),
      _breathBrightness(255),
      _breathDirection(-1),
      _lastBreathMs(0),
      _isBreathing(false),
      _breathColor(0xFFFFFF),
      _driver(driver) {}

// ---------------------------------------------------------------------------
// begin / startTask
// ---------------------------------------------------------------------------
void TFTManager::begin() {
#if defined(BOARD_SHARED_SPI)
    const int16_t nfcCs = ConfigurationManager::getInstance().getNfcPin(NfcPinId::Nss);
    if (!SharedSPIBus::prepareChipSelects(nfcCs, PIN_TFT_CS)) {
        Serial.println("TFTManager: shared SPI chip-select setup failed");
        return;
    }
#endif
    SharedSPIBus::Guard busGuard;
    if (!busGuard) {
        Serial.println("TFTManager: shared SPI init lock timed out");
        return;
    }
    _tft.init();
#if defined(BOARD_SHARED_SPI)
    if (!SharedSPIBus::adoptInitializedBus(nfcCs, PIN_TFT_CS)) {
        Serial.println("TFTManager: shared SPI ownership setup failed");
        return;
    }
#endif
    delay(100);  // panel initialization delay
    _tft.setRotation(0);
    // Panel geometry: wide panels (ILI9488/ST7796 480x320) center the 240x240 sprite;
    // fillScreen below now clears the whole panel so no RAM-noise border shows.
    _wide = (_tft.width() > 240 || _tft.height() > 240);
    _blitOx = _wide ? (_tft.width()  - 240) / 2 : 0;
    _blitOy = _wide ? (_tft.height() - 240) / 2 : 0;
    _tft.setBrightness(255);
    _tft.fillScreen(COLOR_BG);

#ifdef BOARD_HAS_PSRAM
    // Persistent 16-bit full framebuffer in PSRAM (240x240x2 = 115KB). On
    // failure render240Frame falls back to strip rendering, same as no-PSRAM.
    _sprite.setPsram(true);
    _sprite.setColorDepth(16);
    _fullFrame = _sprite.createSprite(240, 240);
    if (!_fullFrame) {
        Serial.println("TFTManager: PSRAM sprite failed; using strip rendering");
    }
#endif
    // Without PSRAM no persistent framebuffer is held: render240Frame draws
    // each frame through a transient 240x32 16-bit strip (15.4KB during a
    // render vs 57.6KB held forever at 8-bit), keeping ~42KB of internal RAM
    // free for WiFi/TLS/JSON and giving these panels full 16-bit colour.

    _messageQueue = xQueueCreate(8, sizeof(TFTMessage));
    if (_messageQueue == nullptr) {
        Serial.println("TFTManager: WARNING — queue creation failed");
    }
    _lastActivityMs = millis();  // initialize timeout clock
    _began = true;
}

void TFTManager::startTask() {
    if (!_began) {
        // begin() bailed (e.g. shared-SPI bus setup failed) — do not spin up a
        // render task that would drive an uninitialized panel.
        Serial.println("TFTManager: not initialized; render task not started");
        return;
    }
    // 8192 bytes: TFT drawing operations + sprite manipulation use more stack than simple
    // I2C LCD operations; sprite creation/pushSprite are stack-heavy due to local buffers
    BaseType_t result = createTaskWithAffinity(
        taskFunc,
        "TFTTask",
        8192,
        this,
        1,      // priority 1 (low; allows other tasks to preempt during 20ms delays)
        &_taskHandle,
        0       // core 0: FreeRTOS main task and LCD also run here
    );
    if (result == pdPASS) {
        Serial.println("TFTManager: Task started on core 0");
    } else {
        Serial.println("TFTManager: WARNING — task creation failed");
        _taskHandle = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Public show* methods — called from main task
// ---------------------------------------------------------------------------
void TFTManager::showBoot(const char* version) {
    TFTMessage msg{};
    msg.state = TFTState::Boot;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", version);
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showWifiConnecting() {
    TFTMessage msg{};
    msg.state = TFTState::WifiConnecting;
    snprintf(msg.statusText, sizeof(msg.statusText), "WiFi");
    snprintf(msg.statusText2, sizeof(msg.statusText2), "Connecting...");
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showWifiConnected(const char* ip) {
    TFTMessage msg{};
    msg.state = TFTState::WifiConnecting;
    snprintf(msg.statusText, sizeof(msg.statusText), "WiFi Connected");
    snprintf(msg.statusText2, sizeof(msg.statusText2), "%s", ip);
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showReady() {
    TFTMessage msg{};
    msg.state = TFTState::Ready;
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showSpoolScanned(const DisplaySpoolData& spool) {
    TFTMessage msg{};
    msg.state = TFTState::SpoolScanned;
    msg.spool = spool;
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showWriting(const char* tagFormat) {
    TFTMessage msg{};
    msg.state = TFTState::Writing;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", tagFormat);
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showWriteResult(bool success, const char* tagFormat) {
    TFTMessage msg{};
    msg.state = TFTState::WriteResult;
    msg.writeSuccess = success;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", tagFormat);
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showKeypadEntry(const char* toolNumber) {
    TFTMessage msg{};
    msg.state = TFTState::KeypadEntry;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", toolNumber);
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showError(const char* errMsg) {
    TFTMessage msg{};
    msg.state = TFTState::Error;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", errMsg);
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::setScreenTimeoutMs(uint32_t timeoutMs) {
    // Update timeout config and wake screen if off; uses critical section to avoid race
    // with taskLoop checking _screenTimeoutMs during timeout calculation
    taskENTER_CRITICAL(&_stateMux);
    _screenTimeoutMs = timeoutMs;
    _lastActivityMs = millis();  // reset inactivity timer
    bool wasOff = _screenOff;
    _screenOff = false;
    taskEXIT_CRITICAL(&_stateMux);
    if (wasOff) {
        SharedSPIBus::Guard busGuard;
        if (busGuard) {
            _tft.setBrightness(255);  // wake screen immediately
        } else {
            Serial.println("TFTManager: shared SPI wake lock timed out");
        }
    }
}

// ---------------------------------------------------------------------------
// Task loop
// ---------------------------------------------------------------------------
void TFTManager::taskFunc(void* param) {
    TFTManager* self = static_cast<TFTManager*>(param);
    self->taskLoop();
}

void TFTManager::taskLoop() {
    while (true) {
        if (_stopRequested) {
            // Park at a clean point — the bus guard and transient strip are
            // released every iteration — so freeForOTA can delete this task
            // without stranding the shared-SPI mutex or a sprite buffer.
            vTaskSuspend(nullptr);
        }
        processQueue();
        MemoryDiagnostics::reportSelf(MemoryDiagnostics::Task::TFT);
        vTaskDelay(pdMS_TO_TICKS(20));  // 50 Hz update rate; allows other tasks to run between renders
    }
}

void TFTManager::processQueue() {
    TFTMessage msg;
    if (_messageQueue && xQueueReceive(_messageQueue, &msg, 0) == pdTRUE) {
        // Message received: wake screen, reset timeout, and render
        taskENTER_CRITICAL(&_stateMux);
        _lastActivityMs = millis();
        bool wasOff = _screenOff;
        _screenOff = false;
        _isBreathing = false;  // stop any prior breathing when new message arrives
        taskEXIT_CRITICAL(&_stateMux);

        SharedSPIBus::Guard busGuard;
        if (!busGuard) {
            Serial.println("TFTManager: shared SPI render lock timed out; frame dropped");
        } else {
            if (wasOff) {
                _tft.setBrightness(255);
            }

            bool rendered = true;
            switch (msg.state) {
                case TFTState::Boot:
                    if (tftIs480Wide(_driver))
                        rendered = renderTextLandscape("SpoolSense", msg.statusText, COLOR_ACCENT);
                    else
                        rendered = renderBoot(msg.statusText);
                    break;
                case TFTState::WifiConnecting:
                    if (tftIs480Wide(_driver))
                        rendered = renderTextLandscape(msg.statusText, msg.statusText2[0] ? msg.statusText2 : nullptr, COLOR_TEXT);
                    else
                        rendered = renderStatus(msg.statusText, msg.statusText2[0] ? msg.statusText2 : nullptr);
                    break;
                case TFTState::Ready:
                    if (tftIs480Wide(_driver)) {
                        rendered = renderReadyLandscape();
                    } else {
                        rendered = renderReady();
                    }
                    break;
                case TFTState::SpoolScanned:
                    if (tftIs480Wide(_driver)) {
                        rendered = renderSpoolScannedLandscape(msg.spool);
                    } else {
                        rendered = renderSpoolScanned(msg.spool);
                    }
                    // Breathing animation for low-weight spools (< 100g): visual
                    // cue for respool. Armed only for frames actually displayed —
                    // a dropped frame must not pulse whatever screen it left up.
                    if (rendered) {
                        _isBreathing = (msg.spool.remainingWeight > 0 &&
                                        msg.spool.remainingWeight <= (float)ConfigurationManager::getInstance().getLowSpoolThreshold());
                        if (_isBreathing) {
                            _breathColor = hexToRgb(msg.spool.colorHex);
                            _breathBrightness = 255;
                            _breathDirection = -1;  // start dimming
                        }
                    }
                    break;
                case TFTState::Writing:
                    if (tftIs480Wide(_driver))
                        rendered = renderTextLandscape("Writing...", msg.statusText, COLOR_TEXT);
                    else
                        rendered = renderStatus("Writing...", msg.statusText);
                    break;
                case TFTState::WriteResult:
                    if (tftIs480Wide(_driver))
                        rendered = renderTextLandscape(msg.writeSuccess ? "Success" : "Write Failed",
                                                       msg.statusText,
                                                       msg.writeSuccess ? COLOR_BAR_FG : COLOR_BAR_LOW);
                    else
                        rendered = renderWriteResult(msg.writeSuccess, msg.statusText);
                    break;
                case TFTState::KeypadEntry:
                    if (tftIs480Wide(_driver))
                        rendered = renderTextLandscape("Tool", msg.statusText, COLOR_ACCENT);
                    else
                        rendered = renderKeypadEntry(msg.statusText);
                    break;
                case TFTState::Error:
                    if (tftIs480Wide(_driver))
                        rendered = renderTextLandscape("Error", msg.statusText, COLOR_BAR_LOW);
                    else
                        rendered = renderStatus("Error", msg.statusText);
                    break;
                case TFTState::TrayDashboard:
                    rendered = renderTrayDashboard(msg.dashboardState);
                    break;
            }
            // Commit state only after a successful (guarded) render, so the idle
            // status-bar refresh never paints a header over a screen that failed
            // to render (frame dropped on a lock timeout or strip alloc failure).
            if (rendered) {
                _currentState = msg.state;
                _lastStatusRefreshMs = millis();
            }
        }
    }

    // Breathing animation: smooth pulse between 30 and 255 brightness; updates every BREATH_STEP_MS
    taskENTER_CRITICAL(&_stateMux);
    bool breathOff = _screenOff;   // a breath tick must not wake a timed-out screen
    taskEXIT_CRITICAL(&_stateMux);
    if (!breathOff && _isBreathing && (millis() - _lastBreathMs >= BREATH_STEP_MS)) {
        _lastBreathMs = millis();
        int16_t next = (int16_t)_breathBrightness + _breathDirection * 3;
        // Clamp bounds and reverse direction at limits
        if (next <= 30)  { next = 30;  _breathDirection = 1; }
        if (next >= 255) { next = 255; _breathDirection = -1; }
        _breathBrightness = (uint8_t)next;
        SharedSPIBus::Guard busGuard;
        if (busGuard) _tft.setBrightness(_breathBrightness);
    }

    // Keep the idle status bar live: on wide panels, while showing the Ready
    // screen, repaint just the header band every few seconds so WiFi/HA/printer
    // changes appear without a scan. (Cheap header-only strip; own bus guard.)
    if (_currentState == TFTState::Ready &&
        (millis() - _lastStatusRefreshMs >= STATUS_REFRESH_MS)) {
        _lastStatusRefreshMs = millis();
        if (_wide) {
            refreshStatusBar();
        } else {
            taskENTER_CRITICAL(&_stateMux);
            bool off = _screenOff;
            taskEXIT_CRITICAL(&_stateMux);
            if (!off) {
                SharedSPIBus::Guard g;  // no-op on separate-bus boards
                if (g) renderReady();   // repaints the header's WiFi icon
            }
        }
    }

    // Screen timeout: turn off after inactivity period if configured (timeoutMs > 0)
    uint32_t timeoutMs;
    unsigned long lastActivity;
    bool screenOff;
    taskENTER_CRITICAL(&_stateMux);
    timeoutMs   = _screenTimeoutMs;
    lastActivity = _lastActivityMs;
    screenOff   = _screenOff;
    taskEXIT_CRITICAL(&_stateMux);

    if (!screenOff && timeoutMs > 0 && (millis() - lastActivity >= timeoutMs)) {
        SharedSPIBus::Guard busGuard;
        if (busGuard) {
            _tft.setBrightness(0);
            taskENTER_CRITICAL(&_stateMux);
            _screenOff = true;
            taskEXIT_CRITICAL(&_stateMux);
        }
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// On wide panels clear the gutters around the centered 240x240 area so a
// prior full-screen landscape frame doesn't linger behind legacy views.
// (Runs inside processQueue's shared-SPI guard; no-op on 240x240 panels.)
void TFTManager::clearWideGutters() {
    if (!_wide) return;
    const int W = _tft.width(), H = _tft.height();
    if (_blitOx > 0) {
        _tft.fillRect(0, 0, _blitOx, H, COLOR_BG);
        _tft.fillRect(_blitOx + 240, 0, W - _blitOx - 240, H, COLOR_BG);
    }
    if (_blitOy > 0) {
        _tft.fillRect(_blitOx, 0, 240, _blitOy, COLOR_BG);
        _tft.fillRect(_blitOx, _blitOy + 240, 240, H - _blitOy - 240, COLOR_BG);
    }
}

void TFTManager::blitCanvas() {
    clearWideGutters();
    // 240x240 sprite pushed at the panel-aware origin: (0,0) on 240x240 panels,
    // centered on wide panels (ILI9488).
    _sprite.pushSprite(_blitOx, _blitOy);
}

// Shared 240x240 backend — same shape as renderLandscapeFrame: persistent full
// sprite when one exists, otherwise the transient 240x32 16-bit strip drawn
// band-by-band (bands overhanging 240 rows clip at the panel edge).
template <typename DrawFn>
bool TFTManager::render240Frame(DrawFn&& drawBody) {
    if (_fullFrame) {
        _sprite.fillScreen(COLOR_BG);
        drawBody(_sprite, 0);
        blitCanvas();
        return true;
    }

    const int STRIP_H = 32;
    _strip.setColorDepth(16);
    if (!_strip.createSprite(240, STRIP_H)) {
        Serial.println("TFT: 240 strip alloc failed");
        return false;
    }
    clearWideGutters();
    for (int bandY = 0; bandY < 240; bandY += STRIP_H) {
        _strip.fillScreen(COLOR_BG);
        drawBody(_strip, bandY);
        _strip.pushSprite(_blitOx, _blitOy + bandY);
    }
    _strip.deleteSprite();
    return true;
}

bool TFTManager::renderBoot(const char* version) {
    return render240Frame([&](LGFX_Sprite& c, int y) { drawBoot240(c, y, version); });
}

void TFTManager::drawBoot240(LGFX_Sprite& canvas, int yOffset, const char* version) {
    const int W = 240, H = 240;
    auto Y = [&](int y){ return y - yOffset; };

    canvas.setTextColor(COLOR_ACCENT);
    canvas.setTextSize(3);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString("SpoolSense", W / 2, Y(H / 2 - 20));

    canvas.setTextColor(COLOR_SUBTEXT);
    canvas.setTextSize(1);
    canvas.drawString(version, W / 2, Y(H / 2 + 20));
}

bool TFTManager::renderReady() {
    return render240Frame([&](LGFX_Sprite& c, int y) { drawReady240(c, y); });
}

void TFTManager::drawReady240(LGFX_Sprite& canvas, int yOffset) {
    const int W = 240, H = 240;
    auto Y = [&](int y){ return y - yOffset; };

    canvas.fillRect(0, Y(0), W, 28, COLOR_HEADER_BG);
    canvas.setTextColor(COLOR_ACCENT);
    canvas.setTextSize(1);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString("SpoolSense", W / 2, Y(14));
    drawWifiIcon240(canvas, yOffset);

    // Idle spool graphic: grey fill indicates no spool selected (waiting for tag)
    int cx = W / 2;
    int cy = H / 2 + 10;
    drawSpoolImage(canvas, cx, cy, 0x888888, yOffset, 150);

    canvas.setTextColor(COLOR_SUBTEXT);
    canvas.setTextSize(1);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString("Tap a spool to scan", cx, Y(H - 16));
}

bool TFTManager::renderSpoolScanned(const DisplaySpoolData& spool) {
    return render240Frame([&](LGFX_Sprite& c, int y) { drawSpool240(c, y, spool); });
}

void TFTManager::drawSpool240(LGFX_Sprite& canvas, int yOffset, const DisplaySpoolData& spool) {
    const int W = 240;
    auto Y = [&](int y){ return y - yOffset; };

    canvas.fillRect(0, Y(0), W, 28, COLOR_HEADER_BG);
    drawTagIcon(canvas, spool.tagType, 4, Y(2));
    canvas.setTextColor(COLOR_ACCENT);
    canvas.setTextSize(1);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString("SpoolSense", W / 2, Y(14));
    drawWifiIcon240(canvas, yOffset);

    uint32_t fillColor = hexToRgb(spool.colorHex);
    int cx = W / 2;
    drawSpoolImage(canvas, cx, 110, fillColor, yOffset, 150);

    int textY = 190;
    canvas.setTextDatum(MC_DATUM);

    char brandMat[48];
    snprintf(brandMat, sizeof(brandMat), "%s  %s", spool.brand, spool.material);
    canvas.setTextColor(COLOR_TEXT);
    canvas.setTextSize(1);
    canvas.drawString(brandMat, cx, Y(textY));

    if (spool.totalWeight > 0) {
        drawWeightBar(canvas, 20, Y(textY + 14), W - 40, 8,
                      spool.remainingWeight, spool.totalWeight);

        char weightStr[32];
        snprintf(weightStr, sizeof(weightStr), "%.0fg / %.0fg",
                 spool.remainingWeight, spool.totalWeight);
        canvas.setTextColor(COLOR_SUBTEXT);
        canvas.setTextSize(1);
        canvas.setTextDatum(MC_DATUM);
        canvas.drawString(weightStr, cx, Y(textY + 30));
    }
}

// Tinted 3D spool image. For each luminance pixel: brightness gates how much of
// the tint colour is applied, so the bright coil takes `tint` (with its wound
// shading) while the dark reel/hub stay neutral grey; luminance 0 = background
// (skipped -> shows the black panel). Integer math only (C6 has no FPU).
// yOffset lets one call fill a strip band; rows outside the canvas are skipped.
void TFTManager::drawSpoolImage(LGFX_Sprite& canvas, int cx, int cy, uint32_t tint, int yOffset,
                                int size) {
    const int w = size > 0 ? size : SPOOL_IMG_W;
    const int h = w;  // square source
    const int x0 = cx - w / 2, y0 = cy - h / 2;
    const int cr = (tint >> 16) & 0xFF, cg = (tint >> 8) & 0xFF, cb = tint & 0xFF;
    const int chH = canvas.height();
    for (int iy = 0; iy < h; iy++) {
        int destY = y0 + iy - yOffset;
        if (destY < 0 || destY >= chH) continue;   // outside this band/canvas
        const int srcY = iy * SPOOL_IMG_H / h;     // nearest-neighbor sample
        const uint8_t* row = &SPOOL_IMG_LUM[srcY * SPOOL_IMG_W];
        for (int ix = 0; ix < w; ix++) {
            int l = pgm_read_byte(&row[ix * SPOOL_IMG_W / w]);
            if (!l) continue;                       // transparent
            int t = (l - 60) * 256 / 140;           // tint strength 0..256
            if (t < 0) t = 0; else if (t > 256) t = 256;
            int r = ((t * cr + (256 - t) * 255) * l) / (255 * 256);
            int g = ((t * cg + (256 - t) * 255) * l) / (255 * 256);
            int b = ((t * cb + (256 - t) * 255) * l) / (255 * 256);
            canvas.drawPixel(x0 + ix, destY, canvas.color565(r, g, b));
        }
    }
}

// 4-bar WiFi signal indicator; baseline at y, bars grow upward.
void TFTManager::drawWifiBars(LGFX_Sprite& canvas, int x, int y, int rssi, bool connected) {
    int level = 0;
    if (connected) {
        if (rssi >= -55) level = 4;
        else if (rssi >= -67) level = 3;
        else if (rssi >= -78) level = 2;
        else level = 1;
    }
    const int bw = 3, gap = 2;
    for (int i = 0; i < 4; i++) {
        int h = 4 + i * 3;  // 4,7,10,13
        uint32_t col = (i < level) ? COLOR_ACCENT : COLOR_SPOOL_RIM;
        canvas.fillRect(x + i * (bw + gap), y - h, bw, h, col);
    }
}

// Same signal-bars indicator as the landscape status bar, sized for the
// 240x240 header (top-right corner). WiFi status is queried live (safe from
// the TFT task).
void TFTManager::drawWifiIcon240(LGFX_Sprite& canvas, int yOffset) {
    bool up = (WiFi.status() == WL_CONNECTED);
    drawWifiBars(canvas, 240 - 26, 22 - yOffset, up ? (int)WiFi.RSSI() : -127, up);
}

void TFTManager::drawStatusBar(LGFX_Sprite& canvas, int yOffset) {
    LandscapeLayout L = landscapeLayout(480, 320);
    auto Y = [&](int y){ return y - yOffset; };
    canvas.fillRect(0, Y(0), 480, L.headerH, COLOR_HEADER_BG);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(COLOR_ACCENT);
    canvas.setTextSize(1);
    canvas.setTextDatum(ML_DATUM);
    canvas.drawString("SpoolSense", 8, Y(L.headerH / 2));

    canvas.setFont(&fonts::Font0);  // compact labels for the status cluster
    int cy = Y(L.headerH / 2);
    int x = 480 - 8;  // right-to-left cursor
    // PRN only when the PrusaLink printer integration is enabled — that is what
    // PrinterManager::isConnected() actually reflects.
    if (ConfigurationManager::getInstance().isPrusaLinkEnabled()) {
        bool ok = PrinterManager::getInstance().isConnected();
        canvas.setTextDatum(MR_DATUM);
        canvas.setTextColor(ok ? COLOR_BAR_FG : COLOR_SPOOL_RIM);
        canvas.drawString("PRN", x, cy);
        x -= 30;
    }
    {
        bool ok = HomeAssistantManager::getInstance().isConnected();
        canvas.setTextDatum(MR_DATUM);
        canvas.setTextColor(ok ? COLOR_BAR_FG : COLOR_SPOOL_RIM);
        canvas.drawString("HA", x, cy);
        x -= 24;
    }
    bool wifiUp = (WiFi.status() == WL_CONNECTED);
    drawWifiBars(canvas, x - 20, Y(L.headerH - 6), wifiUp ? (int)WiFi.RSSI() : -127, wifiUp);
}

// Scanned-spool body (no header — the frame draws the shared status bar).
void TFTManager::drawLandscapeSpool(LGFX_Sprite& canvas, int yOffset,
                                    const DisplaySpoolData& spool) {
    const int W = 480, H = 320;
    LandscapeLayout L = landscapeLayout(W, H);
    auto Y = [&](int y){ return y - yOffset; };

    drawSpoolImage(canvas, L.spoolCx, L.spoolCy, hexToRgb(spool.colorHex), yOffset);

    // Tag-type badge (top-right of body, below the status bar)
    const char* tagLabel = nullptr; uint32_t tagColor = COLOR_SUBTEXT;
    switch (spool.tagType) {
        case TAG_TYPE_OPENPRINTTAG: tagLabel = "OpenPrintTag"; tagColor = 0x4FC3F7; break;
        case TAG_TYPE_TIGERTAG:     tagLabel = "TigerTag";     tagColor = 0xFF9800; break;
        case TAG_TYPE_OPENTAG3D:    tagLabel = "OpenTag3D";    tagColor = 0x4CAF50; break;
        case TAG_TYPE_BAMBU:        tagLabel = "Bambu";        tagColor = 0x1DB954; break;
        case TAG_TYPE_NFC_PLAIN:    tagLabel = "NFC+";         tagColor = 0x00BCD4; break;
        case TAG_TYPE_OPENSPOOL:    tagLabel = "OpenSpool";    tagColor = 0xE91E63; break;
    }
    if (tagLabel) {
        canvas.setFont(&fonts::FreeSans9pt7b);
        canvas.setTextSize(1);
        canvas.setTextDatum(MR_DATUM);
        canvas.setTextColor(tagColor);
        canvas.drawString(tagLabel, W - 8, Y(L.headerH + 14));
    }

    // Right column text
    canvas.setTextDatum(ML_DATUM);
    canvas.setTextSize(1);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextColor(COLOR_TEXT);
    canvas.drawString(spool.brand[0] ? spool.brand : "Unknown", L.textX, Y(L.brandY));
    char line[48];
    snprintf(line, sizeof(line), "%s  #%s", spool.material, spool.colorHex);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(COLOR_SUBTEXT);
    canvas.drawString(line, L.textX, Y(L.materialY));

    if (spool.totalWeight > 0) {
        snprintf(line, sizeof(line), "%.0fg / %.0fg",
                 spool.remainingWeight, spool.totalWeight);
        canvas.setFont(&fonts::FreeSansBold9pt7b);
        canvas.setTextColor(COLOR_TEXT);
        canvas.drawString(line, L.textX, Y(L.weightY));
        const LandscapeRect& b = L.weightBar;
        int filled = landscapeBarFill(b.w, spool.remainingWeight, spool.totalWeight);
        float ratio = spool.remainingWeight / spool.totalWeight;
        uint32_t barColor = (ratio <= 0.1f) ? COLOR_BAR_LOW : COLOR_BAR_FG;
        canvas.fillRoundRect(b.x, Y(b.y), b.w, b.h, b.h / 2, COLOR_BAR_BG);
        if (filled > 0) canvas.fillRoundRect(b.x, Y(b.y), filled, b.h, b.h / 2, barColor);
        canvas.drawRoundRect(b.x, Y(b.y), b.w, b.h, b.h / 2, 0x555555);
    }
}

// Idle body (no header): grey spool + prompt.
void TFTManager::drawLandscapeReady(LGFX_Sprite& canvas, int yOffset) {
    const int W = 480, H = 320;
    LandscapeLayout L = landscapeLayout(W, H);
    auto Y = [&](int y){ return y - yOffset; };
    drawSpoolImage(canvas, W / 2, L.spoolCy, 0x888888, yOffset);  // neutral grey idle spool
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(MC_DATUM);
    canvas.setTextColor(COLOR_SUBTEXT);
    canvas.setTextSize(1);
    canvas.drawString("Tap a spool to scan", W / 2, Y(H - 22));
}

// Shared landscape backend: PSRAM -> one full 480x320 16-bit sprite; no-PSRAM ->
// a reused 32-row RGB565 strip drawn band-by-band. spool==nullptr renders idle.
// Returns false if the frame was dropped because no sprite could be allocated.
bool TFTManager::renderLandscapeFrame(const DisplaySpoolData* spool) {
    const int W = 480, H = 320;

    if (psramFound()) {
        LGFX_Sprite full(&_tft);
        full.setPsram(true);
        full.setColorDepth(16);
        if (full.createSprite(W, H)) {
            full.fillScreen(COLOR_BG);
            drawStatusBar(full, 0);
            if (spool) drawLandscapeSpool(full, 0, *spool);
            else       drawLandscapeReady(full, 0);
            full.pushSprite(0, 0);
            full.deleteSprite();
            return true;
        }
        Serial.println("TFT: PSRAM landscape sprite failed, using strips");
    }

    const int STRIP_H = 32;
    _strip.setColorDepth(16);
    if (!_strip.createSprite(W, STRIP_H)) {
        Serial.println("TFT: landscape strip alloc failed");
        return false;
    }
    for (int bandY = 0; bandY < H; bandY += STRIP_H) {
        _strip.fillScreen(COLOR_BG);
        drawStatusBar(_strip, bandY);
        if (spool) drawLandscapeSpool(_strip, bandY, *spool);
        else       drawLandscapeReady(_strip, bandY);
        _strip.pushSprite(0, bandY);
    }
    _strip.deleteSprite();
    return true;
}

bool TFTManager::renderSpoolScannedLandscape(const DisplaySpoolData& spool) {
    return renderLandscapeFrame(&spool);
}

bool TFTManager::renderReadyLandscape() {
    return renderLandscapeFrame(nullptr);
}

// Centered status text body (scan-progress / write-result screens), FreeFonts.
void TFTManager::drawLandscapeText(LGFX_Sprite& canvas, int yOffset,
                                   const char* line1, const char* line2, uint32_t line1Color) {
    const int W = 480, H = 320;
    LandscapeLayout L = landscapeLayout(W, H);
    auto Y = [&](int y){ return y - yOffset; };
    int mid = L.headerH + (H - L.headerH) / 2;
    bool two = line2 && line2[0];
    canvas.setTextDatum(MC_DATUM);
    if (line1 && line1[0]) {
        canvas.setFont(&fonts::FreeSansBold12pt7b);
        canvas.setTextColor(line1Color);
        canvas.drawString(line1, W / 2, Y(mid - (two ? 16 : 0)));
    }
    if (two) {
        canvas.setFont(&fonts::FreeSans9pt7b);
        canvas.setTextColor(COLOR_SUBTEXT);
        canvas.drawString(line2, W / 2, Y(mid + 16));
    }
}

bool TFTManager::renderTextLandscape(const char* l1, const char* l2, uint32_t l1Color) {
    const int W = 480, H = 320;
    if (psramFound()) {
        LGFX_Sprite full(&_tft);
        full.setPsram(true);
        full.setColorDepth(16);
        if (full.createSprite(W, H)) {
            full.fillScreen(COLOR_BG);
            drawStatusBar(full, 0);
            drawLandscapeText(full, 0, l1, l2, l1Color);
            full.pushSprite(0, 0);
            full.deleteSprite();
            return true;
        }
        Serial.println("TFT: PSRAM text sprite failed, using strips");
    }
    const int STRIP_H = 32;
    _strip.setColorDepth(16);
    if (!_strip.createSprite(W, STRIP_H)) {
        Serial.println("TFT: landscape text strip alloc failed");
        return false;
    }
    for (int bandY = 0; bandY < H; bandY += STRIP_H) {
        _strip.fillScreen(COLOR_BG);
        drawStatusBar(_strip, bandY);
        drawLandscapeText(_strip, bandY, l1, l2, l1Color);
        _strip.pushSprite(0, bandY);
    }
    _strip.deleteSprite();
    return true;
}

// Repaint only the top status band (cheap, ~24KB strip) so idle status stays
// live without re-rendering the whole screen. Takes its own shared-SPI guard.
void TFTManager::refreshStatusBar() {
    taskENTER_CRITICAL(&_stateMux);
    bool off = _screenOff;
    taskEXIT_CRITICAL(&_stateMux);
    if (off) return;
    SharedSPIBus::Guard g;
    if (!g) return;
    LandscapeLayout L = landscapeLayout(480, 320);
    _strip.setColorDepth(16);
    if (!_strip.createSprite(480, L.headerH)) return;
    _strip.fillScreen(COLOR_BG);
    drawStatusBar(_strip, 0);
    _strip.pushSprite(0, 0);
    _strip.deleteSprite();
}

bool TFTManager::renderStatus(const char* line1, const char* line2) {
    return render240Frame([&](LGFX_Sprite& c, int y) { drawStatus240(c, y, line1, line2); });
}

void TFTManager::drawStatus240(LGFX_Sprite& canvas, int yOffset,
                               const char* line1, const char* line2) {
    const int W = 240, H = 240;
    auto Y = [&](int y){ return y - yOffset; };

    canvas.fillRect(0, Y(0), W, 28, COLOR_HEADER_BG);
    canvas.setTextColor(COLOR_ACCENT);
    canvas.setTextSize(1);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString("SpoolSense", W / 2, Y(14));
    drawWifiIcon240(canvas, yOffset);

    int cy = H / 2;
    canvas.setTextColor(COLOR_TEXT);
    canvas.setTextSize(2);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString(line1, W / 2, Y(line2 ? cy - 12 : cy));

    if (line2) {
        canvas.setTextColor(COLOR_SUBTEXT);
        canvas.setTextSize(1);
        canvas.drawString(line2, W / 2, Y(cy + 12));
    }
}

bool TFTManager::renderWriteResult(bool success, const char* tagFormat) {
    return render240Frame([&](LGFX_Sprite& c, int y) { drawWriteResult240(c, y, success, tagFormat); });
}

void TFTManager::drawWriteResult240(LGFX_Sprite& canvas, int yOffset,
                                    bool success, const char* tagFormat) {
    const int W = 240, H = 240;
    auto Y = [&](int y){ return y - yOffset; };

    canvas.fillRect(0, Y(0), W, 28, COLOR_HEADER_BG);
    canvas.setTextColor(COLOR_ACCENT);
    canvas.setTextSize(1);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString("SpoolSense", W / 2, Y(14));
    drawWifiIcon240(canvas, yOffset);

    int cx = W / 2;
    int cy = H / 2;

    canvas.fillCircle(cx, Y(cy - 20), 22, success ? 0x00CC66 : 0xFF4444);
    canvas.setTextColor(COLOR_BG);
    canvas.setTextSize(3);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString(success ? "OK" : "X", cx, Y(cy - 20));
    canvas.setTextColor(COLOR_TEXT);
    canvas.setTextSize(1);
    canvas.drawString(success ? "Write OK" : "Write failed", cx, Y(cy + 12));
    canvas.setTextColor(COLOR_SUBTEXT);
    canvas.drawString(tagFormat, cx, Y(cy + 26));
}

bool TFTManager::renderKeypadEntry(const char* toolNumber) {
    return render240Frame([&](LGFX_Sprite& c, int y) { drawKeypad240(c, y, toolNumber); });
}

void TFTManager::drawKeypad240(LGFX_Sprite& canvas, int yOffset, const char* toolNumber) {
    const int W = 240;
    auto Y = [&](int y){ return y - yOffset; };

    canvas.fillRect(0, Y(0), W, 28, COLOR_HEADER_BG);
    canvas.setTextColor(COLOR_ACCENT);
    canvas.setTextSize(1);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString("SpoolSense", W / 2, Y(14));
    drawWifiIcon240(canvas, yOffset);

    int cx = W / 2;
    canvas.setTextColor(COLOR_SUBTEXT);
    canvas.setTextSize(1);
    canvas.drawString("Assign to tool:", cx, Y(100));

    canvas.setTextColor(COLOR_TEXT);
    canvas.setTextSize(4);
    canvas.drawString(toolNumber, cx, Y(130));

    canvas.setTextColor(COLOR_SUBTEXT);
    canvas.setTextSize(1);
    canvas.drawString("Press # to confirm", cx, Y(185));
}

bool TFTManager::renderTrayDashboard(const TrayDashboardState& state) {
    return render240Frame([&](LGFX_Sprite& c, int y) { _dashboard.draw(c, y, state); });
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------


void TFTManager::drawWeightBar(LGFX_Sprite& canvas, int x, int y, int w, int h,
                                float remaining, float total) {
    float ratio = (total > 0) ? (remaining / total) : 0.0f;
    ratio = constrain(ratio, 0.0f, 1.0f);
    int filled = (int)(w * ratio);

    // Red bar when ≤10% remaining (visual alert for respool soon)
    uint32_t barColor = (ratio <= 0.1f) ? COLOR_BAR_LOW : COLOR_BAR_FG;

    canvas.fillRoundRect(x, y, w, h, h / 2, COLOR_BAR_BG);
    if (filled > 0) {
        canvas.fillRoundRect(x, y, filled, h, h / 2, barColor);
    }
    canvas.drawRoundRect(x, y, w, h, h / 2, 0x555555);
}

void TFTManager::drawTagIcon(LGFX_Sprite& canvas, uint8_t tagType, int x, int y) {
    const char* label = nullptr;
    uint32_t color = COLOR_SUBTEXT;

    // Each tag format has a unique color and label for at-a-glance identification
    switch (tagType) {
        case TAG_TYPE_OPENPRINTTAG:
            label = "OPT";
            color = 0x4FC3F7;  // cyan
            break;
        case TAG_TYPE_TIGERTAG:
            label = "TT";
            color = 0xFF9800;  // orange
            break;
        case TAG_TYPE_OPENTAG3D:
            label = "OT3D";
            color = 0x4CAF50;  // green
            break;
        case TAG_TYPE_BAMBU:
            label = "Bambu";
            color = 0x1DB954;  // Bambu's signature green
            break;
        case TAG_TYPE_NFC_PLAIN:
            label = "NFC+";
            color = 0x00BCD4;  // cyan (generic NFC)
            break;
        case TAG_TYPE_OPENSPOOL:
            label = "OS";
            color = 0xE91E63;  // pink
            break;
        default:
            return;  // unknown type; skip label
    }

    canvas.setTextColor(color);
    canvas.setTextSize(1);
    canvas.setTextDatum(TR_DATUM);  // top-right aligned
    canvas.drawString(label, x + 22, y);
}

uint32_t TFTManager::hexToRgb(const char* hex) {
    if (!hex || strlen(hex) < 6) return 0xCCCCCC;  // grey fallback on invalid input
    char buf[7];
    // Strip leading # if present (Spoolman color format: "#RRGGBB")
    const char* h = (hex[0] == '#') ? hex + 1 : hex;
    strncpy(buf, h, 6);
    buf[6] = '\0';
    unsigned long val = strtoul(buf, nullptr, 16);
    return (uint32_t)val;
}

// Blend color by scaling RGB components: used to dim spool graphic when tag leaves,
// showing last-scanned spool at reduced brightness to indicate "no longer present"
uint32_t TFTManager::dimColor(uint32_t color, uint8_t brightness) {
    uint8_t r = ((color >> 16) & 0xFF) * brightness / 255;
    uint8_t g = ((color >> 8)  & 0xFF) * brightness / 255;
    uint8_t b = ((color)       & 0xFF) * brightness / 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// DisplayI interface — renders text using the TFT status screen
void TFTManager::showText(const char* line1, const char* line2) {
    TFTMessage msg{};
    msg.state = TFTState::WifiConnecting; // generic two-line status renderer
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", line1 ? line1 : "");
    snprintf(msg.statusText2, sizeof(msg.statusText2), "%s", line2 ? line2 : "");
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showText4(const char* line1, const char* line2,
                           const char* line3, const char* line4) {
    // DisplayI interface bridging: 4-line LCD display → 2-line TFT; prioritize line3 (primary) + line4 (secondary)
    // Strip line1 LCD decoration (asterisk borders: "*text*") for clean TFT output
    TFTMessage msg{};
    msg.state = TFTState::WifiConnecting;
    if (line1 && line3) {
        // Remove LCD-specific frame: asterisks at start/end, leading spaces
        const char* clean = line1;
        while (*clean == '*' || *clean == ' ') clean++;
        size_t len = strlen(clean);
        while (len > 0 && (clean[len-1] == '*' || clean[len-1] == ' ')) len--;
        char stripped[48] = {};
        if (len > 0 && len < sizeof(stripped)) { memcpy(stripped, clean, len); stripped[len] = '\0'; }
        // Prepend context label if available
        if (stripped[0]) {
            snprintf(msg.statusText, sizeof(msg.statusText), "%s: %s", stripped, line3);
        } else {
            snprintf(msg.statusText, sizeof(msg.statusText), "%s", line3);
        }
    } else {
        snprintf(msg.statusText, sizeof(msg.statusText), "%s", line3 ? line3 : (line1 ? line1 : ""));
    }
    snprintf(msg.statusText2, sizeof(msg.statusText2), "%s", line4 ? line4 : (line2 ? line2 : ""));
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showKeypad(const char* digits) {
    showKeypadEntry(digits && digits[0] ? digits : "_");
}

void TFTManager::showTrayDashboard(const TrayDashboardState& state) {
    if (!_messageQueue) return;
    TFTMessage msg = {};
    msg.state = TFTState::TrayDashboard;
    msg.dashboardState = state;
    if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
        Serial.println("TFTManager: Queue full, dropping tray dashboard");
    }
}

void TFTManager::showSpool(const DisplaySpoolData& spool) {
    showSpoolScanned(spool);
}

// ---------------------------------------------------------------------------
// OTA support — free sprite for SSL heap, render directly to panel
// ---------------------------------------------------------------------------
void TFTManager::freeForOTA() {
    // Stop TFT task before OTA: queue messages from main task would race with direct
    // frame buffer writes. OTA may reboot (success) or hang (failure); sprite not restored.
    // Cooperative stop: ask the task to park at its clean point, then delete
    // it there. Deleting it mid-frame would strand whatever it held — the
    // shared-SPI mutex (dead OTA screen + dead NFC on shared-bus boards) and
    // any transient sprite — because vTaskDelete does not unwind the stack.
    // NFC is already paused, so the task cannot block on the bus for long;
    // the bound comfortably exceeds the guard's own acquire timeout.
    if (_taskHandle != nullptr) {
        _stopRequested = true;
        for (int i = 0; i < 500 && eTaskGetState(_taskHandle) != eSuspended; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (eTaskGetState(_taskHandle) != eSuspended) {
            Serial.println("TFTManager: render task did not park; deleting anyway");
        } else {
            Serial.println("TFTManager: Task stopped for OTA");
        }
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
    }

    // Reclaim the transient strip in case the task had to be deleted without
    // parking (safe no-op when it holds no buffer).
    _strip.deleteSprite();

    // Release the persistent framebuffer for the OTA TLS/HTTPS buffers (#59).
    // Strip-rendering boards hold no framebuffer between frames.
    if (_fullFrame) {
        _sprite.deleteSprite();
        _fullFrame = false;
        Serial.printf("TFTManager: Sprite freed, heap now %u\n", ESP.getFreeHeap());
    }

    SharedSPIBus::Guard busGuard;
    if (!busGuard) {
        Serial.println("TFTManager: shared SPI OTA-screen lock timed out");
        return;
    }

    // Wake screen for OTA progress display
    _tft.setBrightness(255);

    // Draw OTA screen directly to panel (bypass sprite to save time on each update)
    _tft.fillScreen(COLOR_BG);

    // Header bar
    _tft.fillRect(0, 0, _tft.width(), 28, COLOR_HEADER_BG);
    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextSize(1);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("SpoolSense", _tft.width() / 2, 14);

    // Status text
    int cx = _tft.width() / 2;
    _tft.setTextColor(COLOR_TEXT);
    _tft.setTextSize(2);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("Updating...", cx, 80);

    // Progress bar outline (updateOTAProgress will fill this region)
    int barX = 20;
    int barY = 120;
    int barW = _tft.width() - 40;  // 200px on 240px display
    int barH = 20;
    _tft.drawRoundRect(barX, barY, barW, barH, barH / 2, 0x555555);

    // Initial percentage
    _tft.setTextColor(COLOR_SUBTEXT);
    _tft.setTextSize(1);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("0%", cx, 155);
}

void TFTManager::updateOTAProgress(uint8_t percent) {
    if (percent > 100) percent = 100;

    SharedSPIBus::Guard busGuard;
    if (!busGuard) return;

    int barX = 20;
    int barY = 120;
    int barW = _tft.width() - 40;  // 200px
    int barH = 20;
    int cx = _tft.width() / 2;

    // Update filled portion of progress bar (left-to-right)
    int filled = (barW * percent) / 100;
    if (filled > 0) {
        _tft.fillRoundRect(barX, barY, filled, barH, barH / 2, COLOR_ACCENT);
    }

    // Erase old percentage text, redraw new value
    _tft.fillRect(cx - 30, 148, 60, 16, COLOR_BG);  // clear old text area
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%u%%", percent);
    _tft.setTextColor(COLOR_SUBTEXT);
    _tft.setTextSize(1);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString(pctStr, cx, 155);
}

void TFTManager::showOTAError(const char* error) {
    SharedSPIBus::Guard busGuard;
    if (!busGuard) return;

    int cx = _tft.width() / 2;

    // Clear progress bar and text area
    _tft.fillRect(0, 60, _tft.width(), _tft.height() - 60, COLOR_BG);

    // Error icon: red circle with X
    _tft.fillCircle(cx, 100, 22, 0xFF4444);
    _tft.setTextColor(COLOR_BG);
    _tft.setTextSize(3);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("X", cx, 100);

    // Error status
    _tft.setTextColor(COLOR_TEXT);
    _tft.setTextSize(1);
    _tft.drawString("Update Failed", cx, 135);

    // Error details (e.g., "Connection timeout", "Not enough space")
    if (error && error[0]) {
        _tft.setTextColor(COLOR_SUBTEXT);
        _tft.drawString(error, cx, 155);
    }
}
