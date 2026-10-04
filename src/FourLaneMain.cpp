#if defined(SPOOLSENSE_4LANE_PN532)

#include <Arduino.h>
#include <cstring>
#include <esp_mac.h>

#include "FourLanePN532Manager.h"
#include "FourLaneMqttPublisher.h"
#include "TagStateJson.h"

// Dedicated bring-up entry point for the 4-lane PN532 prototype.
static FourLanePN532Manager fourLaneNfc;
static FourLaneMqttPublisher fourLaneMqtt;
static char baseDeviceId[7] = {0};

static void makeBaseDeviceId() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(baseDeviceId, sizeof(baseDeviceId), "%02x%02x%02x", mac[3], mac[4], mac[5]);
}

static void uidToHex(const uint8_t* uid, uint8_t uidLength, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return;
    out[0] = '\0';
    if (uid == nullptr) return;

    const size_t maxBytes = (outSize - 1) / 2;
    const size_t count = uidLength < maxBytes ? uidLength : maxBytes;
    for (size_t i = 0; i < count; ++i) {
        snprintf(out + (i * 2), outSize - (i * 2), "%02x", uid[i]);
    }
}

static void onLaneEvent(uint8_t lane, bool present, const uint8_t* uid, uint8_t uidLength) {
    char topic[96] = {0};
    char payload[384] = {0};
    snprintf(topic, sizeof(topic), "spoolsense/%s-L%u/tag/state", baseDeviceId, lane);

    if (present) {
        TagStateFields fields{};
        uidToHex(uid, uidLength, fields.uid, sizeof(fields.uid));
        fields.present = true;
        fields.tag_data_valid = false;
        fields.tag_format = "uid_only";
        fields.spoolman_id = -1;
        fields.blank = false;
        buildTagStateJson(payload, sizeof(payload), fields);
    } else {
        buildEmptyTagStateJson(payload, sizeof(payload));
    }

    // Keep the serial representation visible while bringing up real MQTT.
    Serial.printf("VSCAN L%u MQTT TOPIC=%s\n", lane, topic);
    Serial.printf("VSCAN L%u MQTT PAYLOAD=%s\n", lane, payload);

    fourLaneMqtt.queueState(lane, topic, payload);
}

void setup() {
    delay(500);
    Serial.begin(115200);
    delay(1000);

    makeBaseDeviceId();

    Serial.println();
    Serial.println("=== SpoolSense 4-Lane PN532 virtual-scanner MQTT bring-up ===");
    Serial.println("Uses stock SpoolSense Wi-Fi/MQTT settings from NVS; no credentials are compiled in.");
    Serial.println("Lane CS: L1=14 L2=18 L3=32 L4=33");
    Serial.printf("Base device ID: %s\n", baseDeviceId);
    for (uint8_t lane = 1; lane <= FourLanePN532Manager::LANE_COUNT; ++lane) {
        Serial.printf("Virtual scanner L%u: %s-L%u\n", lane, baseDeviceId, lane);
    }

    fourLaneMqtt.begin(baseDeviceId);
    fourLaneNfc.setEventCallback(onLaneEvent);

    if (!fourLaneNfc.begin()) {
        Serial.println("FourLanePN532: no readers initialized; continuing for diagnostics");
    }

    Serial.println("FourLanePN532: polling started");
}

void loop() {
    fourLaneMqtt.loop();
    fourLaneNfc.poll();
    fourLaneMqtt.loop();
    delay(2);
}

#endif // SPOOLSENSE_4LANE_PN532
