#if defined(SPOOLSENSE_4LANE_PN532)

#include "FourLanePN532Manager.h"
#include <SPI.h>
#include <cstring>

constexpr uint8_t FourLanePN532Manager::CS_PINS[FourLanePN532Manager::LANE_COUNT];

// Match the proven Tarantula PN532 path: use an explicit HSPI controller on
// the custom 25/26/27 pins instead of the global default SPI object.
static SPIClass fourLanePn532Spi(HSPI);

bool FourLanePN532Manager::begin() {
    Serial.println("FourLanePN532: initializing shared HSPI readers");
    Serial.printf("  HSPI SCK=%u MISO=%u MOSI=%u\n",
                  PIN_SCK, PIN_MISO, PIN_MOSI);

    // All readers share the SPI data/clock lines. Every CS must be HIGH before
    // the bus starts so only the explicitly selected PN532 can drive MISO.
    for (uint8_t i = 0; i < LANE_COUNT; ++i) {
        pinMode(CS_PINS[i], OUTPUT);
        digitalWrite(CS_PINS[i], HIGH);
    }

    // RSTO on the ELECHOUSE PN532 V3 is a reader output, not an ESP32-driven
    // reset input. Leave it disconnected and initialize the bus directly.
    fourLanePn532Spi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);

    bool anyReady = false;
    for (uint8_t i = 0; i < LANE_COUNT; ++i) {
        Serial.printf("FourLanePN532: lane %u CS=%u init...\n", i + 1, CS_PINS[i]);
        lanes_[i].reader = new Adafruit_PN532(CS_PINS[i], &fourLanePn532Spi);

        if (lanes_[i].reader == nullptr) {
            Serial.printf("FourLanePN532: lane %u allocation FAILED\n", i + 1);
            continue;
        }

        lanes_[i].reader->begin();
        const uint32_t version = lanes_[i].reader->getFirmwareVersion();
        if (version == 0) {
            Serial.printf("FourLanePN532: lane %u reader NOT FOUND\n", i + 1);
            delete lanes_[i].reader;
            lanes_[i].reader = nullptr;
            continue;
        }

        if (!lanes_[i].reader->SAMConfig()) {
            Serial.printf("FourLanePN532: lane %u SAMConfig FAILED\n", i + 1);
            delete lanes_[i].reader;
            lanes_[i].reader = nullptr;
            continue;
        }

        lanes_[i].ready = true;
        const uint8_t ic = static_cast<uint8_t>(version >> 24);
        const uint8_t major = static_cast<uint8_t>(version >> 16);
        const uint8_t minor = static_cast<uint8_t>(version >> 8);
        Serial.printf("FourLanePN532: lane %u READY - IC=0x%02X FW=%u.%u\n",
                      i + 1, ic, major, minor);
        anyReady = true;
    }

    Serial.printf("FourLanePN532: init complete (%s)\n",
                  anyReady ? "at least one reader ready" : "NO READERS FOUND");
    return anyReady;
}

void FourLanePN532Manager::poll() {
    // One reader per call, always sequential. Adafruit_PN532 has a shared
    // file-scope packet buffer, so we deliberately never overlap operations.
    pollLane(nextLane_);
    nextLane_ = (nextLane_ + 1) % LANE_COUNT;
}

void FourLanePN532Manager::setEventCallback(LaneEventCallback callback) {
    eventCallback_ = callback;
}

bool FourLanePN532Manager::isLaneReady(uint8_t lane) const {
    if (lane < 1 || lane > LANE_COUNT) return false;
    return lanes_[lane - 1].ready;
}

void FourLanePN532Manager::pollLane(uint8_t laneIndex) {
    LaneState& lane = lanes_[laneIndex];
    if (!lane.ready || lane.reader == nullptr) return;

    uint8_t uid[10] = {0};
    uint8_t uidLength = 0;
    const bool found = lane.reader->readPassiveTargetID(
        PN532_MIFARE_ISO14443A, uid, &uidLength, 100);

    if (found && uidLength > 0) {
        lane.absentMisses = 0;

        const bool changed = !lane.present ||
                             lane.uidLength != uidLength ||
                             memcmp(lane.uid, uid, uidLength) != 0;

        if (changed) {
            lane.present = true;
            lane.uidLength = uidLength;
            memcpy(lane.uid, uid, uidLength);

            Serial.printf("LANE %u TAG UID=", laneIndex + 1);
            printUid(uid, uidLength);
            Serial.println();

            if (eventCallback_ != nullptr) {
                eventCallback_(laneIndex + 1, true, uid, uidLength);
            }
        }
        return;
    }

    if (!lane.present) return;

    if (++lane.absentMisses >= REMOVE_MISS_THRESHOLD) {
        lane.present = false;
        lane.absentMisses = 0;
        lane.uidLength = 0;
        memset(lane.uid, 0, sizeof(lane.uid));
        Serial.printf("LANE %u TAG REMOVED\n", laneIndex + 1);

        if (eventCallback_ != nullptr) {
            eventCallback_(laneIndex + 1, false, nullptr, 0);
        }
    }
}

void FourLanePN532Manager::printUid(const uint8_t* uid, uint8_t uidLength) {
    for (uint8_t i = 0; i < uidLength; ++i) {
        if (uid[i] < 0x10) Serial.print('0');
        Serial.print(uid[i], HEX);
    }
}

#endif // SPOOLSENSE_4LANE_PN532
