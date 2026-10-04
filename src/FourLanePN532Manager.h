#pragma once

#if defined(SPOOLSENSE_4LANE_PN532)

#include <Arduino.h>
#include <Adafruit_PN532.h>

class FourLanePN532Manager {
public:
    static constexpr uint8_t LANE_COUNT = 4;
    using LaneEventCallback = void (*)(uint8_t lane, bool present, const uint8_t* uid, uint8_t uidLength);

    // ESP32 4-lane BoxTurtle prototype wiring.
    // GPIO16/17 are deliberately avoided because some ESP32 modules reserve
    // them for PSRAM; using them as CS can prevent the board from running.
    static constexpr uint8_t PIN_SCK  = 25;
    static constexpr uint8_t PIN_MISO = 26;
    static constexpr uint8_t PIN_MOSI = 27;

    bool begin();
    void poll();
    void setEventCallback(LaneEventCallback callback);

    bool isLaneReady(uint8_t lane) const;

private:
    struct LaneState {
        Adafruit_PN532* reader = nullptr;
        bool ready = false;
        bool present = false;
        uint8_t uid[10] = {0};
        uint8_t uidLength = 0;
        uint8_t absentMisses = 0;
    };

    // Per-reader chip-select lines. 32/33 are unused by PN532 mode and avoid
    // the GPIO16/17 PSRAM collision seen on the bench ESP32 module.
    static constexpr uint8_t CS_PINS[LANE_COUNT] = {14, 18, 32, 33};
    static constexpr uint8_t REMOVE_MISS_THRESHOLD = 3;

    LaneState lanes_[LANE_COUNT];
    uint8_t nextLane_ = 0;
    LaneEventCallback eventCallback_ = nullptr;

    void pollLane(uint8_t laneIndex);
    static void printUid(const uint8_t* uid, uint8_t uidLength);
};

#endif // SPOOLSENSE_4LANE_PN532
