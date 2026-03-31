#include "TFTManager.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Tag type icon bitmaps — 32x32 monochrome, stored in flash.
// Each is a const uint8_t array. 1 = foreground pixel, 0 = background.
// These are minimal symbolic icons — replace with your own designs.
//
// Format: row-major, 1 bit per pixel packed into bytes (32 bytes per row,
// but we use a simple 32x32 byte array here for clarity at the cost of
// ~1KB per icon. Fine for 5 icons = ~5KB flash.)
// ---------------------------------------------------------------------------

// Generic NFC icon (concentric arcs suggesting radio waves)
static const uint8_t ICON_NFC_PLAIN[32][32] PROGMEM = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0},
    {0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0},
    {0,0,0,0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,1,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,1,0,0,1,0,0,0,1,1,1,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,1,0,0,1,0,0,1,1,0,1,1,0,0,1,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,1,0,0,1,0,0,1,0,1,1,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,1,0,0,1,0,0,0,1,1,1,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,1,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,0,0,0,0},
    {0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0},
    {0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

// OpenPrintTag icon: "OPT" text in a rounded box
// (placeholder — same structure, you can replace with a proper design)
static const uint8_t ICON_OPENPRINTTAG[32][32] PROGMEM = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0},
    {0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0},
    {0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0},
    {0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,1,1,1,0,0,0,1,0,0,1,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,1,0,0,0,1,0,0,1,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,1,0,0,0,1,0,0,1,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,1,0,0,0,1,0,0,1,1,1,1,0,1,0,1,1,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,1,0,0,0,1,0,0,1,0,1,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,1,0,0,0,1,0,0,1,0,0,1,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,1,1,1,0,0,0,1,0,0,1,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,1,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,1,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0},
    {0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0},
    {0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0},
    {0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

// Reuse NFC_PLAIN icon for TigerTag, OpenTag3D, Bambu for now.
// Replace with distinct designs as desired.
#define ICON_TIGERTAG    ICON_NFC_PLAIN
#define ICON_OPENTAG3D   ICON_NFC_PLAIN
#define ICON_BAMBU       ICON_NFC_PLAIN

// ---------------------------------------------------------------------------
// Color constants
// ---------------------------------------------------------------------------
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
TFTManager::TFTManager()
    : _sprite(&_tft),
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
      _breathColor(0xFFFFFF) {}

// ---------------------------------------------------------------------------
// begin / startTask
// ---------------------------------------------------------------------------
void TFTManager::begin() {
    _tft.init();
    delay(100);  // Let display hardware stabilize after cold boot
    _tft.setRotation(0);
    _tft.setBrightness(255);
    _tft.fillScreen(COLOR_BG);

    _sprite.setColorDepth(8);  // 8-bit = 57.6KB vs 115KB at 16-bit
    if (!_sprite.createSprite(_tft.width(), _tft.height())) {
        Serial.println("TFTManager: WARNING — sprite allocation failed (low heap)");
    }

    _messageQueue = xQueueCreate(4, sizeof(TFTMessage));
    if (_messageQueue == nullptr) {
        Serial.println("TFTManager: WARNING — queue creation failed");
    }
    _lastActivityMs = millis();
}

void TFTManager::startTask() {
    xTaskCreatePinnedToCore(
        taskFunc,
        "TFTTask",
        8192,   // TFT + sprite rendering needs more stack than LCD
        this,
        1,
        &_taskHandle,
        0       // Core 0, same as LCDTask
    );
    Serial.println("TFTManager: Task started on core 0");
}

// ---------------------------------------------------------------------------
// Public show* methods — called from main task
// ---------------------------------------------------------------------------
void TFTManager::showBoot(const char* version) {
    TFTMessage msg{};
    msg.state = TFTState::Boot;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", version);
    xQueueSend(_messageQueue, &msg, 0);
}

void TFTManager::showWifiConnecting() {
    TFTMessage msg{};
    msg.state = TFTState::WifiConnecting;
    snprintf(msg.statusText, sizeof(msg.statusText), "WiFi");
    snprintf(msg.statusText2, sizeof(msg.statusText2), "Connecting...");
    xQueueSend(_messageQueue, &msg, 0);
}

void TFTManager::showWifiConnected(const char* ip) {
    TFTMessage msg{};
    msg.state = TFTState::WifiConnecting;
    snprintf(msg.statusText, sizeof(msg.statusText), "WiFi Connected");
    snprintf(msg.statusText2, sizeof(msg.statusText2), "%s", ip);
    xQueueSend(_messageQueue, &msg, 0);
}

void TFTManager::showReady() {
    TFTMessage msg{};
    msg.state = TFTState::Ready;
    xQueueSend(_messageQueue, &msg, 0);
}

void TFTManager::showSpoolScanned(const TFTSpoolData& spool) {
    TFTMessage msg{};
    msg.state = TFTState::SpoolScanned;
    msg.spool = spool;
    xQueueSend(_messageQueue, &msg, 0);
}

void TFTManager::showWriting(const char* tagFormat) {
    TFTMessage msg{};
    msg.state = TFTState::Writing;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", tagFormat);
    xQueueSend(_messageQueue, &msg, 0);
}

void TFTManager::showWriteResult(bool success, const char* tagFormat) {
    TFTMessage msg{};
    msg.state = TFTState::WriteResult;
    msg.writeSuccess = success;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", tagFormat);
    xQueueSend(_messageQueue, &msg, 0);
}

void TFTManager::showKeypadEntry(const char* toolNumber) {
    TFTMessage msg{};
    msg.state = TFTState::KeypadEntry;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", toolNumber);
    xQueueSend(_messageQueue, &msg, 0);
}

void TFTManager::showError(const char* errMsg) {
    TFTMessage msg{};
    msg.state = TFTState::Error;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", errMsg);
    xQueueSend(_messageQueue, &msg, 0);
}

void TFTManager::setScreenTimeoutMs(uint32_t timeoutMs) {
    taskENTER_CRITICAL(&_stateMux);
    _screenTimeoutMs = timeoutMs;
    _lastActivityMs = millis();
    bool wasOff = _screenOff;
    _screenOff = false;
    taskEXIT_CRITICAL(&_stateMux);
    if (wasOff) {
        _tft.setBrightness(255);
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
        processQueue();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void TFTManager::processQueue() {
    TFTMessage msg;
    if (xQueueReceive(_messageQueue, &msg, 0) == pdTRUE) {
        taskENTER_CRITICAL(&_stateMux);
        _lastActivityMs = millis();
        bool wasOff = _screenOff;
        _screenOff = false;
        _isBreathing = false;
        taskEXIT_CRITICAL(&_stateMux);

        if (wasOff) {
            _tft.setBrightness(255);
        }

        switch (msg.state) {
            case TFTState::Boot:
                renderBoot(msg.statusText);
                break;
            case TFTState::WifiConnecting:
                renderStatus(msg.statusText, msg.statusText2[0] ? msg.statusText2 : nullptr);
                break;
            case TFTState::Ready:
                renderReady();
                break;
            case TFTState::SpoolScanned:
                _isBreathing = (msg.spool.remainingWeight > 0 &&
                                msg.spool.remainingWeight <= 100.0f);
                if (_isBreathing) {
                    _breathColor = hexToRgb(msg.spool.colorHex);
                    _breathBrightness = 255;
                    _breathDirection = -1;
                }
                renderSpoolScanned(msg.spool);
                break;
            case TFTState::Writing:
                renderStatus("Writing...", msg.statusText);
                break;
            case TFTState::WriteResult:
                renderWriteResult(msg.writeSuccess, msg.statusText);
                break;
            case TFTState::KeypadEntry:
                renderKeypadEntry(msg.statusText);
                break;
            case TFTState::Error:
                renderStatus("Error", msg.statusText);
                break;
        }
    }

    // Breathing animation tick
    if (_isBreathing && (millis() - _lastBreathMs >= BREATH_STEP_MS)) {
        _lastBreathMs = millis();
        int16_t next = (int16_t)_breathBrightness + _breathDirection * 3;
        if (next <= 30)  { next = 30;  _breathDirection = 1; }
        if (next >= 255) { next = 255; _breathDirection = -1; }
        _breathBrightness = (uint8_t)next;
        _tft.setBrightness(_breathBrightness);
    }

    // Screen timeout
    uint32_t timeoutMs;
    unsigned long lastActivity;
    bool screenOff;
    taskENTER_CRITICAL(&_stateMux);
    timeoutMs   = _screenTimeoutMs;
    lastActivity = _lastActivityMs;
    screenOff   = _screenOff;
    taskEXIT_CRITICAL(&_stateMux);

    if (!screenOff && timeoutMs > 0 && (millis() - lastActivity >= timeoutMs)) {
        _tft.setBrightness(0);
        taskENTER_CRITICAL(&_stateMux);
        _screenOff = true;
        taskEXIT_CRITICAL(&_stateMux);
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void TFTManager::renderBoot(const char* version) {
    _sprite.fillScreen(COLOR_BG);

    // Centered logo text
    _sprite.setTextColor(COLOR_ACCENT);
    _sprite.setTextSize(3);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString("SpoolSense", _tft.width() / 2, _tft.height() / 2 - 20);

    _sprite.setTextColor(COLOR_SUBTEXT);
    _sprite.setTextSize(1);
    _sprite.drawString(version, _tft.width() / 2, _tft.height() / 2 + 20);

    _sprite.pushSprite(0, 0);
}

void TFTManager::renderReady() {
    _sprite.fillScreen(COLOR_BG);

    // Header bar
    _sprite.fillRect(0, 0, _tft.width(), 28, COLOR_HEADER_BG);
    _sprite.setTextColor(COLOR_ACCENT);
    _sprite.setTextSize(1);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString("SpoolSense", _tft.width() / 2, 14);

    // Idle spool — grey fill
    int cx = _tft.width() / 2;
    int cy = _tft.height() / 2 + 10;
    drawSpool(cx, cy, 70, 28, COLOR_SPOOL_RIM);

    // Prompt
    _sprite.setTextColor(COLOR_SUBTEXT);
    _sprite.setTextSize(1);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString("Tap a spool to scan", cx, _tft.height() - 16);

    _sprite.pushSprite(0, 0);
}

void TFTManager::renderSpoolScanned(const TFTSpoolData& spool) {
    _sprite.fillScreen(COLOR_BG);

    int W = _tft.width();   // 240
    int H = _tft.height();  // 240

    // Header bar
    _sprite.fillRect(0, 0, W, 28, COLOR_HEADER_BG);

    // Tag type icon top-left in header
    drawTagIcon(spool.tagType, 4, 2);

    // "SpoolSense" in header
    _sprite.setTextColor(COLOR_ACCENT);
    _sprite.setTextSize(1);
    _sprite.setTextDatum(ML_DATUM);
    _sprite.drawString("SpoolSense", 30, 14);

    // ---- Spool graphic ----
    uint32_t fillColor = hexToRgb(spool.colorHex);
    int cx = W / 2;
    int cy = 110;
    drawSpool(cx, cy, 68, 26, fillColor);

    // ---- Text area ----
    int textY = 190;
    _sprite.setTextDatum(MC_DATUM);

    // Brand + material
    char brandMat[48];
    snprintf(brandMat, sizeof(brandMat), "%s  %s", spool.brand, spool.material);
    _sprite.setTextColor(COLOR_TEXT);
    _sprite.setTextSize(1);
    _sprite.drawString(brandMat, cx, textY);

    // Weight bar
    if (spool.totalWeight > 0) {
        drawWeightBar(20, textY + 14, W - 40, 8,
                      spool.remainingWeight, spool.totalWeight);

        // Weight text
        char weightStr[32];
        snprintf(weightStr, sizeof(weightStr), "%.0fg / %.0fg",
                 spool.remainingWeight, spool.totalWeight);
        _sprite.setTextColor(COLOR_SUBTEXT);
        _sprite.setTextSize(1);
        _sprite.setTextDatum(MC_DATUM);
        _sprite.drawString(weightStr, cx, textY + 30);
    }

    _sprite.pushSprite(0, 0);
}

void TFTManager::renderStatus(const char* line1, const char* line2) {
    _sprite.fillScreen(COLOR_BG);

    _sprite.fillRect(0, 0, _tft.width(), 28, COLOR_HEADER_BG);
    _sprite.setTextColor(COLOR_ACCENT);
    _sprite.setTextSize(1);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString("SpoolSense", _tft.width() / 2, 14);

    int cy = _tft.height() / 2;
    _sprite.setTextColor(COLOR_TEXT);
    _sprite.setTextSize(2);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString(line1, _tft.width() / 2, line2 ? cy - 12 : cy);

    if (line2) {
        _sprite.setTextColor(COLOR_SUBTEXT);
        _sprite.setTextSize(1);
        _sprite.drawString(line2, _tft.width() / 2, cy + 12);
    }

    _sprite.pushSprite(0, 0);
}

void TFTManager::renderWriteResult(bool success, const char* tagFormat) {
    _sprite.fillScreen(COLOR_BG);
    _sprite.fillRect(0, 0, _tft.width(), 28, COLOR_HEADER_BG);
    _sprite.setTextColor(COLOR_ACCENT);
    _sprite.setTextSize(1);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString("SpoolSense", _tft.width() / 2, 14);

    int cx = _tft.width() / 2;
    int cy = _tft.height() / 2;

    if (success) {
        // Green checkmark circle
        _sprite.fillCircle(cx, cy - 20, 22, 0x00CC66);
        _sprite.setTextColor(COLOR_BG);
        _sprite.setTextSize(3);
        _sprite.setTextDatum(MC_DATUM);
        _sprite.drawString("OK", cx, cy - 20);
        _sprite.setTextColor(COLOR_TEXT);
        _sprite.setTextSize(1);
        _sprite.drawString("Write OK", cx, cy + 12);
        _sprite.setTextColor(COLOR_SUBTEXT);
        _sprite.drawString(tagFormat, cx, cy + 26);
    } else {
        _sprite.fillCircle(cx, cy - 20, 22, 0xFF4444);
        _sprite.setTextColor(COLOR_BG);
        _sprite.setTextSize(3);
        _sprite.setTextDatum(MC_DATUM);
        _sprite.drawString("X", cx, cy - 20);
        _sprite.setTextColor(COLOR_TEXT);
        _sprite.setTextSize(1);
        _sprite.drawString("Write failed", cx, cy + 12);
        _sprite.setTextColor(COLOR_SUBTEXT);
        _sprite.drawString(tagFormat, cx, cy + 26);
    }

    _sprite.pushSprite(0, 0);
}

void TFTManager::renderKeypadEntry(const char* toolNumber) {
    _sprite.fillScreen(COLOR_BG);
    _sprite.fillRect(0, 0, _tft.width(), 28, COLOR_HEADER_BG);
    _sprite.setTextColor(COLOR_ACCENT);
    _sprite.setTextSize(1);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString("SpoolSense", _tft.width() / 2, 14);

    int cx = _tft.width() / 2;
    _sprite.setTextColor(COLOR_SUBTEXT);
    _sprite.setTextSize(1);
    _sprite.drawString("Assign to tool:", cx, 100);

    _sprite.setTextColor(COLOR_TEXT);
    _sprite.setTextSize(4);
    _sprite.drawString(toolNumber, cx, 130);

    _sprite.setTextColor(COLOR_SUBTEXT);
    _sprite.setTextSize(1);
    _sprite.drawString("Press # to confirm", cx, 185);

    _sprite.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void TFTManager::drawSpool(int cx, int cy, int outerR, int innerR, uint32_t fillColor) {
    // Shadow
    _sprite.fillCircle(cx + 3, cy + 3, outerR, 0x111111);

    // Outer rim (dark grey ring)
    _sprite.fillCircle(cx, cy, outerR, COLOR_SPOOL_RIM);

    // Filament fill area (between rim and hub)
    // We fill the full inner area with filament color, then draw spokes over it
    _sprite.fillCircle(cx, cy, outerR - 5, fillColor);

    // Hub (inner dark circle)
    _sprite.fillCircle(cx, cy, innerR, COLOR_SPOOL_HUB);

    // Spokes — 6 evenly spaced
    for (int i = 0; i < 6; i++) {
        float angle = i * (M_PI / 3.0f);
        int x1 = cx + (int)(cos(angle) * (innerR - 2));
        int y1 = cy + (int)(sin(angle) * (innerR - 2));
        int x2 = cx + (int)(cos(angle) * (outerR - 8));
        int y2 = cy + (int)(sin(angle) * (outerR - 8));
        _sprite.drawLine(x1, y1, x2, y2, COLOR_SPOOL_RIM);
        _sprite.drawLine(x1+1, y1, x2+1, y2, COLOR_SPOOL_RIM); // 2px wide
    }

    // Rim outline
    _sprite.drawCircle(cx, cy, outerR, 0x666666);
    _sprite.drawCircle(cx, cy, outerR - 1, 0x555555);

    // Hub outline
    _sprite.drawCircle(cx, cy, innerR, 0x555555);

    // Hub centre dot
    _sprite.fillCircle(cx, cy, 4, 0x888888);
}

void TFTManager::drawWeightBar(int x, int y, int w, int h,
                                float remaining, float total) {
    float ratio = (total > 0) ? (remaining / total) : 0.0f;
    ratio = constrain(ratio, 0.0f, 1.0f);
    int filled = (int)(w * ratio);

    uint32_t barColor = (ratio <= 0.1f) ? COLOR_BAR_LOW : COLOR_BAR_FG;

    // Background
    _sprite.fillRoundRect(x, y, w, h, h / 2, COLOR_BAR_BG);
    // Fill
    if (filled > 0) {
        _sprite.fillRoundRect(x, y, filled, h, h / 2, barColor);
    }
    // Outline
    _sprite.drawRoundRect(x, y, w, h, h / 2, 0x555555);
}

void TFTManager::drawTagIcon(uint8_t tagType, int x, int y) {
    const uint8_t (*icon)[32] = nullptr;
    uint32_t fgColor = COLOR_ACCENT;

    switch (tagType) {
        case TAG_TYPE_OPENPRINTTAG:
            icon = ICON_OPENPRINTTAG;
            fgColor = 0x00BFFF;
            break;
        case TAG_TYPE_TIGERTAG:
            icon = ICON_TIGERTAG;
            fgColor = 0xFF8C00;
            break;
        case TAG_TYPE_OPENTAG3D:
            icon = ICON_OPENTAG3D;
            fgColor = 0x00CC66;
            break;
        case TAG_TYPE_BAMBU:
            icon = ICON_BAMBU;
            fgColor = 0x1DB954;
            break;
        case TAG_TYPE_NFC_PLAIN:
        default:
            icon = ICON_NFC_PLAIN;
            fgColor = COLOR_SUBTEXT;
            break;
    }

    if (!icon) return;

    // Draw 32x32 bitmap from PROGMEM — scale to 22x22 by skipping every ~3rd pixel
    for (int row = 0; row < 22; row++) {
        int srcRow = (row * 32) / 22;
        for (int col = 0; col < 22; col++) {
            int srcCol = (col * 32) / 22;
            uint8_t pixel = pgm_read_byte(&icon[srcRow][srcCol]);
            if (pixel) {
                _sprite.drawPixel(x + col, y + row, fgColor);
            }
        }
    }
}

uint32_t TFTManager::hexToRgb(const char* hex) {
    if (!hex || strlen(hex) < 6) return 0xCCCCCC;
    char buf[7];
    // Strip leading # if present
    const char* h = (hex[0] == '#') ? hex + 1 : hex;
    strncpy(buf, h, 6);
    buf[6] = '\0';
    unsigned long val = strtoul(buf, nullptr, 16);
    return (uint32_t)val;
}

// Reserved for future use: dim spool graphic color when tag removed
// (show last scanned spool at ~50% brightness to indicate "gone")
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
    xQueueSend(_messageQueue, &msg, 0);
}

void TFTManager::showText4(const char* line1, const char* line2,
                           const char* line3, const char* line4) {
    // TFT uses two lines — show line3 as primary, line4 as secondary
    TFTMessage msg{};
    msg.state = TFTState::WifiConnecting; // generic two-line status renderer
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", line3 ? line3 : (line1 ? line1 : ""));
    snprintf(msg.statusText2, sizeof(msg.statusText2), "%s", line4 ? line4 : (line2 ? line2 : ""));
    xQueueSend(_messageQueue, &msg, 0);
}

void TFTManager::showKeypad(const char* digits) {
    showKeypadEntry(digits && digits[0] ? digits : "_");
}

void TFTManager::showSpool(const DisplaySpoolData& spool) {
    TFTSpoolData tftSpool{};
    strncpy(tftSpool.brand, spool.brand, sizeof(tftSpool.brand) - 1);
    strncpy(tftSpool.material, spool.material, sizeof(tftSpool.material) - 1);
    snprintf(tftSpool.name, sizeof(tftSpool.name), "%s %s", spool.brand, spool.material);
    strncpy(tftSpool.colorHex, spool.colorHex, sizeof(tftSpool.colorHex) - 1);
    tftSpool.remainingWeight = spool.remainingWeight;
    tftSpool.totalWeight = spool.totalWeight;
    tftSpool.tagType = spool.tagType;
    showSpoolScanned(tftSpool);
}

// ---------------------------------------------------------------------------
// OTA support — free sprite for SSL heap, render directly to panel
// ---------------------------------------------------------------------------
void TFTManager::freeForOTA() {
    // Stop the TFT task so no queue processing conflicts with direct writes.
    // NOTE: The task and sprite are NOT restored after OTA failure — the device
    // is expected to reboot (success) or require manual reboot (failure).
    if (_taskHandle != nullptr) {
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
        Serial.println("TFTManager: Task stopped for OTA");
    }

    // Delete sprite to free 57.6KB heap
    _sprite.deleteSprite();
    Serial.printf("TFTManager: Sprite freed, heap now %u\n", ESP.getFreeHeap());

    // Wake screen if it was off
    _tft.setBrightness(255);

    // Draw OTA screen directly to panel (no sprite)
    _tft.fillScreen(COLOR_BG);

    // Header bar
    _tft.fillRect(0, 0, _tft.width(), 28, COLOR_HEADER_BG);
    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextSize(1);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("SpoolSense", _tft.width() / 2, 14);

    // "Updating..." text
    int cx = _tft.width() / 2;
    _tft.setTextColor(COLOR_TEXT);
    _tft.setTextSize(2);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("Updating...", cx, 80);

    // Progress bar outline (same position updateOTAProgress will fill)
    int barX = 20;
    int barY = 120;
    int barW = _tft.width() - 40;  // 200px on 240px display
    int barH = 20;
    _tft.drawRoundRect(barX, barY, barW, barH, barH / 2, 0x555555);

    // "0%" text
    _tft.setTextColor(COLOR_SUBTEXT);
    _tft.setTextSize(1);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("0%", cx, 155);
}

void TFTManager::updateOTAProgress(uint8_t percent) {
    if (percent > 100) percent = 100;

    int barX = 20;
    int barY = 120;
    int barW = _tft.width() - 40;  // 200px
    int barH = 20;
    int cx = _tft.width() / 2;

    // Fill progress bar
    int filled = (barW * percent) / 100;
    if (filled > 0) {
        _tft.fillRoundRect(barX, barY, filled, barH, barH / 2, COLOR_ACCENT);
    }

    // Clear and redraw percentage text
    _tft.fillRect(cx - 30, 148, 60, 16, COLOR_BG);
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%u%%", percent);
    _tft.setTextColor(COLOR_SUBTEXT);
    _tft.setTextSize(1);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString(pctStr, cx, 155);
}

void TFTManager::showOTAError(const char* error) {
    int cx = _tft.width() / 2;

    // Clear progress area
    _tft.fillRect(0, 60, _tft.width(), _tft.height() - 60, COLOR_BG);

    // Error icon
    _tft.fillCircle(cx, 100, 22, 0xFF4444);
    _tft.setTextColor(COLOR_BG);
    _tft.setTextSize(3);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("X", cx, 100);

    // "Update Failed" text
    _tft.setTextColor(COLOR_TEXT);
    _tft.setTextSize(1);
    _tft.drawString("Update Failed", cx, 135);

    // Error detail
    if (error && error[0]) {
        _tft.setTextColor(COLOR_SUBTEXT);
        _tft.drawString(error, cx, 155);
    }
}
