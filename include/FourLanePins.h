#pragma once

#include <stdint.h>

// Central pin map for ESP32-WROOM-32 four-lane PN532 mode.
// These are OUTPUT-capable CS pins. GPIO34/35 are input-only;
// GPIO12 is a boot strap pin and should not be used for CS.
namespace FourLanePins {
    static constexpr uint8_t LANE_COUNT = 4;

    // Shared HSPI bus
    static constexpr uint8_t SCK  = 25;
    static constexpr uint8_t MISO = 26;
    static constexpr uint8_t MOSI = 27;

    // Physical lane order: 1, 2, 3, 4.
    // Lane 2 moved from GPIO18 to GPIO13 for single-side wiring.
    static constexpr uint8_t CS[LANE_COUNT] = {14, 13, 32, 33};
}
