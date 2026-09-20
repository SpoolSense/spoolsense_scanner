#ifndef LCD_MANAGER_H
#define LCD_MANAGER_H

#include <cstdint>
#include <LiquidCrystal_I2C.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "LCDDisplayLogic.h"
#include "DisplayI.h"

struct ScreenMessage {
    char line1[17];
    char line2[17];
    char line3[17];
    char line4[17];
    uint8_t lineCount;
};

class LCDManager : public DisplayI {
public:
    LCDManager(uint8_t lcd_Addr, uint8_t lcd_cols, uint8_t lcd_rows);
    void begin();
    void updateScreen(const char* line1, const char* line2);
    void updateScreen(const char* line1, const char* line2, const char* line3, const char* line4);
    bool startTask();  // true only if the render task is live
    void setScreenTimeoutMs(uint32_t timeoutMs) override;

    // DisplayI interface
    void showText(const char* line1, const char* line2 = nullptr) override;
    void showText4(const char* line1, const char* line2,
                   const char* line3, const char* line4) override;
    void showSpool(const DisplaySpoolData& spool) override;
    void showKeypad(const char* digits) override;
    void showWriteResult(bool success, const char* format) override;

private:
    void processQueue();
    void taskLoop();
    static void taskFunc(void* param);

    LiquidCrystal_I2C _lcd;
    QueueHandle_t _messageQueue;
    TaskHandle_t _taskHandle;
    char _currentLine1[17] = {0};
    char _currentLine2[17] = {0};
    char _activeLine1[17] = {0};
    char _activeLine2[17] = {0};
    char _activeLine3[17] = {0};
    char _activeLine4[17] = {0};
    uint8_t _activeLineCount;
    uint8_t _currentPage;
    unsigned long _lastPageSwitchTimeMs;
    uint8_t _lcd_cols;
    unsigned long _lastChangeTime;
    bool _screenOff;
    uint32_t _screenTimeoutMs;
    LCDDisplayLogic _displayLogic;
    portMUX_TYPE _stateMux;
    static const uint32_t DEFAULT_SCREEN_TIMEOUT_MS = 15 * 60 * 1000; // 15 minutes
    static const uint32_t FOUR_LINE_PAGE_DURATION_MS = 5000;
};

#endif // LCD_MANAGER_H
