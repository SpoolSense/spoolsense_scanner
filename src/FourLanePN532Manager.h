#pragma once

#if defined(SPOOLSENSE_4LANE_PN532)

#include <Arduino.h>
#include <Adafruit_PN532.h>
#include "FourLanePins.h"

class FourLanePN532Manager {
public:
    static constexpr uint8_t LANE_COUNT = FourLanePins::LANE_COUNT;
    using LaneEventCallback = void (*)(uint8_t lane, bool present, const uint8_t* uid, uint8_t uidLength);

    // SPI pins are centralized in include/FourLanePins.h.

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

    // Per-reader CS pins are centralized in include/FourLanePins.h.
    static constexpr uint8_t REMOVE_MISS_THRESHOLD = 3;

    LaneState lanes_[LANE_COUNT];
    uint8_t nextLane_ = 0;
    LaneEventCallback eventCallback_ = nullptr;

    void pollLane(uint8_t laneIndex);
    static void printUid(const uint8_t* uid, uint8_t uidLength);
};

#endif // SPOOLSENSE_4LANE_PN532
