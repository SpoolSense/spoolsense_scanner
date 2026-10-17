#include "LCDManager.h"
#include "MemoryDiagnostics.h"
#include "TaskUtils.h"
#include <Arduino.h>
#include <cstring>

// LCDManager — 16x2 I2C LCD display manager with 4-line paging, screen auto-off timeout,
// and message queue for thread-safe updates from ApplicationManager. Runs dedicated task on core 0.
// DisplayI interface for compatibility with optional TFT display backend.

LCDManager::LCDManager(uint8_t lcd_Addr, uint8_t lcd_cols, uint8_t lcd_rows)
    : _lcd(lcd_Addr, lcd_cols, lcd_rows), _lcd_cols(lcd_cols), _messageQueue(nullptr), _taskHandle(nullptr),
      _activeLineCount(2), _currentPage(0), _lastPageSwitchTimeMs(0), _lastChangeTime(0),
      _screenOff(false), _screenTimeoutMs(DEFAULT_SCREEN_TIMEOUT_MS), _stateMux(portMUX_INITIALIZER_UNLOCKED) {}

void LCDManager::begin() {
    _lcd.init();
    _lcd.backlight();
    _lcd.clear();
    _messageQueue = xQueueCreate(8, sizeof(ScreenMessage));  // async updates from other tasks
    _lastChangeTime = millis();
}

void LCDManager::updateScreen(const char* line1, const char* line2) {
    ScreenMessage msg;
    memset(&msg, 0, sizeof(msg));
    // truncate to display width (16 cols max for standard I2C LCD)
    size_t copyLen = (_lcd_cols < 16) ? _lcd_cols : 16;
    auto copyLine = [copyLen](char* dst, const char* src) {
        if (src == nullptr) {
            dst[0] = '\0';
            return;
        }
        strncpy(dst, src, copyLen);
        dst[copyLen] = '\0';
    };

    unsigned long nowMs = millis();
    LCDDisplayMessage displayMsg;
    // serialize display logic access from multiple callers
    taskENTER_CRITICAL(&_stateMux);
    displayMsg = _displayLogic.prepareTwoLineMessage(line1, line2, nowMs);
    taskEXIT_CRITICAL(&_stateMux);

    copyLine(msg.line1, displayMsg.line1);
    copyLine(msg.line2, displayMsg.line2);
    copyLine(msg.line3, displayMsg.line3);
    copyLine(msg.line4, displayMsg.line4);
    msg.lineCount = displayMsg.lineCount;

    // non-blocking queue send to LCDTask
    xQueueSend(_messageQueue, &msg, 0);
}

void LCDManager::updateScreen(const char* line1, const char* line2, const char* line3, const char* line4) {
    ScreenMessage msg;
    memset(&msg, 0, sizeof(msg));
    size_t copyLen = (_lcd_cols < 16) ? _lcd_cols : 16;
    auto copyLine = [copyLen](char* dst, const char* src) {
        if (src == nullptr) {
            dst[0] = '\0';
            return;
        }
        strncpy(dst, src, copyLen);
        dst[copyLen] = '\0';
    };

    LCDDisplayMessage displayMsg;
    // serialize display logic access from multiple callers
    taskENTER_CRITICAL(&_stateMux);
    displayMsg = _displayLogic.prepareFourLineMessage(line1, line2, line3, line4);
    taskEXIT_CRITICAL(&_stateMux);

    copyLine(msg.line1, displayMsg.line1);
    copyLine(msg.line2, displayMsg.line2);
    copyLine(msg.line3, displayMsg.line3);
    copyLine(msg.line4, displayMsg.line4);
    msg.lineCount = displayMsg.lineCount;

    // non-blocking queue send to LCDTask
    xQueueSend(_messageQueue, &msg, 0);
}

void LCDManager::setScreenTimeoutMs(uint32_t timeoutMs) {
    taskENTER_CRITICAL(&_stateMux);
    _screenTimeoutMs = timeoutMs;
    _lastChangeTime = millis();
    bool wasScreenOff = _screenOff;
    _screenOff = false;
    taskEXIT_CRITICAL(&_stateMux);

    // wake backlight if screen was off (allows runtime timeout reconfiguration)
    if (wasScreenOff) {
        _lcd.backlight();
    }
}

bool LCDManager::startTask() {
    BaseType_t created = createTaskWithAffinity(
        taskFunc,
        "LCDTask",
        2048,
        this,
        1,
        &_taskHandle,
        0  // core 0 avoids contention with BLE/WiFi on core 1
    );
    if (taskCreationSucceeded(created, &_taskHandle)) {
        Serial.println("LCDManager: Task started");
        return true;
    }
    _taskHandle = nullptr;
    Serial.println("LCDManager: ERROR — task creation failed");
    return false;
}

void LCDManager::taskFunc(void* param) {
    LCDManager* self = static_cast<LCDManager*>(param);
    self->taskLoop();
}

void LCDManager::taskLoop() {
    while (true) {
        processQueue();
        MemoryDiagnostics::reportSelf(MemoryDiagnostics::Task::LCD);
        vTaskDelay(pdMS_TO_TICKS(50));  // 20 Hz update rate (sufficient for human-readable display)
    }
}

void LCDManager::processQueue() {
    // render only changed lines to minimize I2C writes (expensive on 16x2 display)
    auto renderLines = [this](const char* line1, const char* line2) {
        bool changed = false;

        if (strcmp(line1, _currentLine1) != 0) {
            strncpy(_currentLine1, line1, sizeof(_currentLine1) - 1);
            _currentLine1[sizeof(_currentLine1) - 1] = '\0';
            _lcd.setCursor(0, 0);
            _lcd.print(_currentLine1);
            // clear rest of line (needed when new text is shorter than previous)
            for (size_t i = strlen(_currentLine1); i < _lcd_cols; ++i) {
                _lcd.print(" ");
            }
            changed = true;
        }

        if (strcmp(line2, _currentLine2) != 0) {
            strncpy(_currentLine2, line2, sizeof(_currentLine2) - 1);
            _currentLine2[sizeof(_currentLine2) - 1] = '\0';
            _lcd.setCursor(0, 1);
            _lcd.print(_currentLine2);
            // clear rest of line
            for (size_t i = strlen(_currentLine2); i < _lcd_cols; ++i) {
                _lcd.print(" ");
            }
            changed = true;
        }

        return changed;
    };

    // wake backlight and reset timeout on screen activity (message arrival)
    auto markScreenActivity = [this]() {
        bool wakeScreen = false;
        taskENTER_CRITICAL(&_stateMux);
        _lastChangeTime = millis();
        if (_screenOff) {
            _screenOff = false;
            wakeScreen = true;
        }
        taskEXIT_CRITICAL(&_stateMux);

        if (wakeScreen) {
            _lcd.backlight();
        }
    };

    ScreenMessage msg;
    if (xQueueReceive(_messageQueue, &msg, 0) == pdTRUE) {
        // detect new message by comparing all lines + line count
        uint8_t previousActiveLineCount = _activeLineCount;
        char previousLine1[17];
        char previousLine2[17];
        char previousLine3[17];
        char previousLine4[17];
        strncpy(previousLine1, _activeLine1, sizeof(previousLine1) - 1);
        strncpy(previousLine2, _activeLine2, sizeof(previousLine2) - 1);
        strncpy(previousLine3, _activeLine3, sizeof(previousLine3) - 1);
        strncpy(previousLine4, _activeLine4, sizeof(previousLine4) - 1);
        previousLine1[16] = '\0';
        previousLine2[16] = '\0';
        previousLine3[16] = '\0';
        previousLine4[16] = '\0';

        strncpy(_activeLine1, msg.line1, sizeof(_activeLine1) - 1);
        strncpy(_activeLine2, msg.line2, sizeof(_activeLine2) - 1);
        strncpy(_activeLine3, msg.line3, sizeof(_activeLine3) - 1);
        strncpy(_activeLine4, msg.line4, sizeof(_activeLine4) - 1);
        _activeLine1[16] = '\0';
        _activeLine2[16] = '\0';
        _activeLine3[16] = '\0';
        _activeLine4[16] = '\0';
        _activeLineCount = (msg.lineCount == 4) ? 4 : 2;
        _currentPage = 0;
        _lastPageSwitchTimeMs = millis();

        bool changed = renderLines(_activeLine1, _activeLine2);

        // detect actual content change (not just queue noise from identical updates)
        bool isNewMessage = (strcmp(previousLine1, _activeLine1) != 0 ||
                            strcmp(previousLine2, _activeLine2) != 0 ||
                            strcmp(previousLine3, _activeLine3) != 0 ||
                            strcmp(previousLine4, _activeLine4) != 0 ||
                            previousActiveLineCount != _activeLineCount);

        if (isNewMessage) {
            markScreenActivity();
        }

        // notify display logic of what was rendered (for UI animation state)
        if (changed || previousActiveLineCount != _activeLineCount) {
            unsigned long nowMs = millis();
            LCDDisplayMessage displayedMsg;
            strncpy(displayedMsg.line1, msg.line1, sizeof(displayedMsg.line1) - 1);
            strncpy(displayedMsg.line2, msg.line2, sizeof(displayedMsg.line2) - 1);
            strncpy(displayedMsg.line3, msg.line3, sizeof(displayedMsg.line3) - 1);
            strncpy(displayedMsg.line4, msg.line4, sizeof(displayedMsg.line4) - 1);
            displayedMsg.line1[sizeof(displayedMsg.line1) - 1] = '\0';
            displayedMsg.line2[sizeof(displayedMsg.line2) - 1] = '\0';
            displayedMsg.line3[sizeof(displayedMsg.line3) - 1] = '\0';
            displayedMsg.line4[sizeof(displayedMsg.line4) - 1] = '\0';
            displayedMsg.lineCount = _activeLineCount;
            taskENTER_CRITICAL(&_stateMux);
            _displayLogic.noteDisplayedMessage(displayedMsg, nowMs);
            taskEXIT_CRITICAL(&_stateMux);
        }
    }

    // auto-rotate 4-line messages (alternating pages every FOUR_LINE_PAGE_DURATION_MS)
    bool pageSwitched = false;
    if (_activeLineCount == 4 && (millis() - _lastPageSwitchTimeMs >= FOUR_LINE_PAGE_DURATION_MS)) {
        _currentPage = (_currentPage == 0) ? 1 : 0;
        _lastPageSwitchTimeMs = millis();
        pageSwitched = true;
    }

    if (pageSwitched) {
        if (_currentPage == 0) {
            renderLines(_activeLine1, _activeLine2);
        } else {
            renderLines(_activeLine3, _activeLine4);
        }
    }

    // auto-off timer (timeoutMs = 0 disables, screenOff = true means already off)
    uint32_t timeoutMs = 0;
    unsigned long lastChangeTime = 0;
    bool screenOff = false;
    taskENTER_CRITICAL(&_stateMux);
    timeoutMs = _screenTimeoutMs;
    lastChangeTime = _lastChangeTime;
    screenOff = _screenOff;
    taskEXIT_CRITICAL(&_stateMux);

    if (!screenOff && timeoutMs > 0 && (millis() - lastChangeTime >= timeoutMs)) {
        _lcd.noBacklight();
        taskENTER_CRITICAL(&_stateMux);
        _screenOff = true;
        taskEXIT_CRITICAL(&_stateMux);
    }
}

// ── DisplayI Interface (compatibility layer) ──────────────────────────────

// show 2 lines on display
void LCDManager::showText(const char* line1, const char* line2) {
    updateScreen(line1, line2 ? line2 : "");
}

// show 4 lines with paging
void LCDManager::showText4(const char* line1, const char* line2,
                           const char* line3, const char* line4) {
    updateScreen(line1, line2, line3, line4);
}

// keypad prompt with tool number
void LCDManager::showKeypad(const char* digits) {
    char line1[17];
    snprintf(line1, sizeof(line1), "Tool: T%s", digits && digits[0] ? digits : "_");
    updateScreen(line1, "# Confirm  * Clr");
}

// write success/failure with format string
void LCDManager::showWriteResult(bool success, const char* format) {
    char line2[17];
    snprintf(line2, sizeof(line2), "%s", format ? format : "");
    updateScreen(success ? "Write OK!" : "Write Failed!", line2);
}

// spool details from Spoolman lookup
void LCDManager::showSpool(const DisplaySpoolData& spool) {
    char line1[17];
    char line2[17];
    snprintf(line1, sizeof(line1), "%s %s", spool.brand, spool.material);
    snprintf(line2, sizeof(line2), "%.0fg/%.0fg", spool.remainingWeight, spool.totalWeight);
    updateScreen(line1, line2);
}
