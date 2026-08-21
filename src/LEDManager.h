#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Adafruit_NeoPixel.h>

#ifndef NATIVE_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

enum class LEDMode : uint8_t { OFF, SOLID, BREATHING };

class LEDManager {
public:
    LEDManager();

    void begin(uint8_t pin);
    void startTask();  // Call once at end of setup()

    // Synchronous — safe to call before startTask() (setup phase)
    void showBooting();
    void showWifiConnected();
    void showWifiFailed();
    void showReady();

    // Async after startTask(); synchronous fallback before startTask()
    void showOff();
    void showFilamentColor(uint8_t r, uint8_t g, uint8_t b);
    void breatheFilamentColor(uint8_t r, uint8_t g, uint8_t b);

    // Set target FIRST, then request flash — flash completes, then restores target
    void flashTagDetected();    // 3× white 80ms on/off
    void flashParseFailed();    // 1× red 100ms
    void flashWriteSuccess();   // 1× green 100ms
    void flashWriteFailure();   // 1× red 100ms
    void flashWarning();        // 3× yellow 150ms on/off

    void setFilamentColorFromHex(const char* hex);
    void getTargetColor(uint8_t& r, uint8_t& g, uint8_t& b) const;

private:
    bool _initialized = false;
    bool _taskStarted = false;
    uint8_t _pin = 0;
    Adafruit_NeoPixel _pixel;
    uint16_t _breathStep = 0;  // Breathing animation phase (task-only)

    struct Target {
        LEDMode mode = LEDMode::OFF;
        uint8_t r = 0, g = 0, b = 0;
    };

    enum class FlashType : uint8_t {
        NONE,
        TAG_DETECTED,   // 3× white 80ms on/off
        PARSE_FAILED,   // 1× red 100ms
        WRITE_SUCCESS,  // 1× green 100ms
        WRITE_FAILURE,  // 1× red 100ms
        WARNING,        // 3× yellow 150ms on/off
    };

    struct FlashReq {
        FlashType type = FlashType::NONE;
        bool pending = false;
    };

    Target _target;
    FlashReq _flashReq;

#ifndef NATIVE_TEST
    SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _taskHandle = nullptr;

    static void ledTaskFunc(void* param);
    void ledTaskLoop();
    void runFlash(FlashType type);
#endif

    void setPixelColor(uint8_t r, uint8_t g, uint8_t b);
    void setPixelOff();
    void setTarget(LEDMode mode, uint8_t r, uint8_t g, uint8_t b);
    void requestFlash(FlashType type);
};

#endif
