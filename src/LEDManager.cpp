// LEDManager — WS2812B RGB(W) LED control with task-based animation and breathing.
// Board-specific color order: DevKitC uses GRB, standard S3 uses RGB, others use GRBW.
// Synchronous methods (showBooting, showReady, etc.) for pre-task setup; async state setters queue to FreeRTOS task.

#include "LEDManager.h"
#include "BoardPins.h"
#include "UserConfig.h"
#include "TaskUtils.h"

#include <cstdlib>
#include <cmath>

#ifndef NATIVE_TEST
#include <Arduino.h>
#include "MemoryDiagnostics.h"
#endif

#ifndef M_PI
#define M_PI 3.14159265f
#endif

#if defined(BOARD_SEEED_XIAO_ESP32_C6)
static constexpr uint8_t XIAO_LED_PWM_BITS = 8;
static constexpr uint32_t XIAO_LED_PWM_HZ = 5000;
#endif

#if defined(BOARD_SEEED_XIAO_ESP32_C6)
// XIAO ESP32-C6 has a simple active-low user LED on GPIO15, not a NeoPixel.
LEDManager::LEDManager()
    : _initialized(false), _taskStarted(false), _pixel(1, 0, NEO_GRB + NEO_KHZ800) {}
#elif defined(BOARD_S3_DEVKITC) || defined(BOARD_ESP32_C5) || defined(BOARD_ESP32_C6)
// DevKitC WS2812B requires GRB byte order instead of standard RGB
LEDManager::LEDManager()
    : _initialized(false), _taskStarted(false), _pixel(1, 0, NEO_GRB + NEO_KHZ800) {}
#elif defined(BOARD_ESP32_S3)
// Standard ESP32-S3 uses RGB byte order
LEDManager::LEDManager()
    : _initialized(false), _taskStarted(false), _pixel(1, 0, NEO_RGB + NEO_KHZ800) {}
#else
// Generic ESP32 uses RGBW (warm white channel)
LEDManager::LEDManager()
    : _initialized(false), _taskStarted(false), _pixel(1, 0, NEO_GRBW + NEO_KHZ800) {}
#endif

void LEDManager::begin(uint8_t pin) {
    _pin = pin;
#if defined(BOARD_SEEED_XIAO_ESP32_C6)
    _activeLowGpio = (pin == PIN_STATUS_LED);
    if (_activeLowGpio) {
        pinMode(pin, OUTPUT);
        ledcAttach(pin, XIAO_LED_PWM_HZ, XIAO_LED_PWM_BITS);
        ledcWrite(pin, 255);  // active-low user LED off
        _initialized = true;
        return;
    }
#endif

    // Reconfirm color order in case of pin conflicts that forced constructor re-init
#if defined(BOARD_S3_DEVKITC) || defined(BOARD_ESP32_C5) || defined(BOARD_ESP32_C6)
    _pixel.updateType(NEO_GRB + NEO_KHZ800);
#elif defined(BOARD_ESP32_S3)
    _pixel.updateType(NEO_RGB + NEO_KHZ800);
#else
    _pixel.updateType(NEO_GRBW + NEO_KHZ800);
#endif
    _pixel.setPin(pin);
    _pixel.begin();
    _pixel.setBrightness(64);  // safe default to avoid overcurrent on 5V rail
    _pixel.show();
    _initialized = true;
}

void LEDManager::startTask() {
    if (!_initialized) return;
#ifndef NATIVE_TEST
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) return;
    // Core 1 for animation task — keeps UI responsive on Core 0
    if (createTaskWithAffinity(ledTaskFunc, "LEDTask", 2048, this, 1, &_taskHandle, 1) == pdPASS) {
        _taskStarted = true;
    } else {
        _taskHandle = nullptr;
        Serial.println("LEDManager: ERROR — task creation failed");
    }
#endif
}

// ---------------------------------------------------------------------------
// Synchronous methods — direct pixel writes used during setup() before task
// ---------------------------------------------------------------------------

void LEDManager::showBooting() {
    if (!_initialized) return;
#if defined(BOARD_SEEED_XIAO_ESP32_C6)
    if (_activeLowGpio) {
        setPixelColor(255, 255, 255);
        return;
    }
#endif
#if defined(BOARD_ESP32_S3)
    // White via RGB channels
    _pixel.setPixelColor(0, _pixel.Color(255, 255, 255));
#else
    // White via W channel (more efficient, avoids heating RGB)
    _pixel.setPixelColor(0, _pixel.Color(0, 0, 0, 255));
#endif
    _pixel.show();
}

void LEDManager::showWifiConnected() {
    if (!_initialized) return;
    setPixelColor(0, 255, 255);  // cyan
}

void LEDManager::showWifiFailed() {
    if (!_initialized) return;
    setPixelColor(255, 0, 0);
}

void LEDManager::showReady() {
    if (!_initialized) return;
    setPixelColor(0, 0, 255);  // blue (system ready for scanning)
}

// ---------------------------------------------------------------------------
// Async state methods — set target, task applies it
// ---------------------------------------------------------------------------

void LEDManager::showOff() {
    if (!_initialized) return;
    setTarget(LEDMode::OFF, 0, 0, 0);
}

// Black (0,0,0) spool color is visually identical to LED off — users can't tell
// if spool is loaded. Substitute dim gray so the color state is always visible.
static void substituteBlack(uint8_t& r, uint8_t& g, uint8_t& b) {
    if (r == 0 && g == 0 && b == 0) { r = 0x33; g = 0x33; b = 0x33; }
}

void LEDManager::showFilamentColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!_initialized) return;
    substituteBlack(r, g, b);
    setTarget(LEDMode::SOLID, r, g, b);
}

void LEDManager::breatheFilamentColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!_initialized) return;
    substituteBlack(r, g, b);
    setTarget(LEDMode::BREATHING, r, g, b);
}

// ---------------------------------------------------------------------------
// Flash methods — queue a flash, task runs it then restores target
// ---------------------------------------------------------------------------

void LEDManager::flashTagDetected() {
    if (!_initialized) return;
    requestFlash(FlashType::TAG_DETECTED);
}

void LEDManager::flashParseFailed() {
    if (!_initialized) return;
    requestFlash(FlashType::PARSE_FAILED);
}

void LEDManager::flashWriteSuccess() {
    if (!_initialized) return;
    requestFlash(FlashType::WRITE_SUCCESS);
}

void LEDManager::flashWriteFailure() {
    if (!_initialized) return;
    requestFlash(FlashType::WRITE_FAILURE);
}

void LEDManager::flashWarning() {
    if (!_initialized) return;
    requestFlash(FlashType::WARNING);
}

// ---------------------------------------------------------------------------
// Hex utility
// ---------------------------------------------------------------------------

void LEDManager::setFilamentColorFromHex(const char* hex) {
    if (!_initialized || !hex) return;
    if (hex[0] == '#') hex++;
    char* endptr = nullptr;
    unsigned long color = strtoul(hex, &endptr, 16);
    if (endptr == hex) return;
    showFilamentColor((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

void LEDManager::getTargetColor(uint8_t& r, uint8_t& g, uint8_t& b) const {
    r = _target.r; g = _target.g; b = _target.b;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void LEDManager::setPixelColor(uint8_t r, uint8_t g, uint8_t b) {
#if defined(BOARD_SEEED_XIAO_ESP32_C6)
    if (_activeLowGpio) {
        uint8_t brightness = max(r, max(g, b));
        ledcWrite(_pin, 255 - brightness);
        return;
    }
#endif
    _pixel.setPixelColor(0, _pixel.Color(r, g, b, 0));
    _pixel.show();
}

void LEDManager::setPixelOff() {
#if defined(BOARD_SEEED_XIAO_ESP32_C6)
    if (_activeLowGpio) {
        ledcWrite(_pin, 255);
        return;
    }
#endif
    _pixel.setPixelColor(0, _pixel.Color(0, 0, 0, 0));
    _pixel.show();
}

void LEDManager::setTarget(LEDMode mode, uint8_t r, uint8_t g, uint8_t b) {
#ifndef NATIVE_TEST
    if (_taskStarted && _mutex) {
        // Queue state update to animation task via mutex + notify
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _target.mode = mode;
        _target.r = r;
        _target.g = g;
        _target.b = b;
        xSemaphoreGive(_mutex);
        if (_taskHandle) xTaskNotifyGive(_taskHandle);
        return;
    }
#endif
    // Pre-task: no task running yet, apply directly
    _target.mode = mode;
    _target.r = r;
    _target.g = g;
    _target.b = b;
    if (mode == LEDMode::OFF) {
        setPixelOff();
    } else {
        setPixelColor(r, g, b);
    }
}

void LEDManager::requestFlash(FlashType type) {
#ifndef NATIVE_TEST
    if (_taskStarted && _mutex) {
        // Queue flash request to animation task (task will run it then restore target state)
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _flashReq.type = type;
        _flashReq.pending = true;
        xSemaphoreGive(_mutex);
        if (_taskHandle) xTaskNotifyGive(_taskHandle);
        return;
    }
#endif
    // Pre-task: no task running yet, run flash synchronously (blocking)
    switch (type) {
        case FlashType::TAG_DETECTED:
            for (int i = 0; i < 3; i++) {
                setPixelColor(255, 255, 255);
                delay(80);
                setPixelOff();
                delay(80);
            }
            break;
        case FlashType::PARSE_FAILED:
            setPixelColor(255, 0, 0);
            delay(100);
            setPixelOff();
            break;
        case FlashType::WRITE_SUCCESS:
            setPixelColor(0, 255, 0);
            delay(100);
            break;
        case FlashType::WRITE_FAILURE:
            setPixelColor(255, 0, 0);
            delay(100);
            break;
        case FlashType::WARNING:
            for (int i = 0; i < 3; i++) {
                setPixelColor(255, 180, 0);
                delay(150);
                setPixelOff();
                delay(150);
            }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// FreeRTOS task
// ---------------------------------------------------------------------------

#ifndef NATIVE_TEST

void LEDManager::ledTaskFunc(void* param) {
    static_cast<LEDManager*>(param)->ledTaskLoop();
}

void LEDManager::runFlash(FlashType type) {
    switch (type) {
        case FlashType::TAG_DETECTED:
            for (int i = 0; i < 3; i++) {
                setPixelColor(255, 255, 255);
                vTaskDelay(pdMS_TO_TICKS(80));
                setPixelOff();
                vTaskDelay(pdMS_TO_TICKS(80));
            }
            break;
        case FlashType::PARSE_FAILED:
            setPixelColor(255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            setPixelOff();
            break;
        case FlashType::WRITE_SUCCESS:
            setPixelColor(0, 255, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        case FlashType::WRITE_FAILURE:
            setPixelColor(255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        case FlashType::WARNING:
            for (int i = 0; i < 3; i++) {
                setPixelColor(255, 180, 0);  // yellow/amber
                vTaskDelay(pdMS_TO_TICKS(150));
                setPixelOff();
                vTaskDelay(pdMS_TO_TICKS(150));
            }
            break;
        default:
            break;
    }
}

void LEDManager::ledTaskLoop() {
    for (;;) {
        // Determine wait time based on current mode
        bool isBreathing;
        {
            xSemaphoreTake(_mutex, portMAX_DELAY);
            isBreathing = (_target.mode == LEDMode::BREATHING) && !_flashReq.pending;
            xSemaphoreGive(_mutex);
        }

        // Wait for setTarget/requestFlash notification OR 10ms tick for breathing animation
        ulTaskNotifyTake(pdTRUE, isBreathing ? pdMS_TO_TICKS(10) : portMAX_DELAY);
        MemoryDiagnostics::reportSelf(MemoryDiagnostics::Task::LED);

        // Snapshot state from shared members (protected by mutex)
        xSemaphoreTake(_mutex, portMAX_DELAY);
        bool hasFlash = _flashReq.pending;
        FlashType flashType = _flashReq.type;
        if (hasFlash) _flashReq.pending = false;
        Target target = _target;
        xSemaphoreGive(_mutex);

        // Flash plays immediately, then we restore the target state
        if (hasFlash) {
            runFlash(flashType);
            xSemaphoreTake(_mutex, portMAX_DELAY);
            target = _target;
            xSemaphoreGive(_mutex);
        }

        // Apply target state (or next frame of breathing animation)
        switch (target.mode) {
            case LEDMode::OFF:
                setPixelOff();
                break;
            case LEDMode::SOLID:
                setPixelColor(target.r, target.g, target.b);
                break;
            case LEDMode::BREATHING: {
                // Sine-wave breathing: 200 steps × 10ms = 2s full cycle
                // t² curve gives faster fade-in, slower fade-out (feels more natural)
                static constexpr uint16_t BREATH_STEPS = 200;
                float t = sinf((float)_breathStep / (float)BREATH_STEPS * (float)M_PI);
                float brightness = t * t;
                setPixelColor(
                    (uint8_t)(target.r * brightness),
                    (uint8_t)(target.g * brightness),
                    (uint8_t)(target.b * brightness)
                );
                _breathStep = (_breathStep + 1) % BREATH_STEPS;
                break;
            }
        }
    }
}

#endif
