#pragma once

#if defined(SPOOLSENSE_4LANE_PN532)

#include <Arduino.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

class FourLaneMqttPublisher {
public:
    static constexpr uint8_t LANE_COUNT = 4;

    bool begin(const char* baseDeviceId);
    void loop();

    // Cache the latest retained state for a lane. If MQTT is connected it is
    // published immediately; otherwise it remains pending until reconnect.
    bool queueState(uint8_t lane, const char* topic, const char* payload);

    bool isConfigured() const { return configured_; }
    bool isConnected() { return mqttClient_.connected(); }

private:
    struct PendingState {
        char topic[96] = {0};
        char payload[384] = {0};
        bool valid = false;
        bool dirty = false;
    };

    bool loadConfig();
    void ensureWifi();
    void ensureMqtt();
    void flushPending();

    char baseDeviceId_[7] = {0};
    char wifiSsid_[33] = {0};
    char wifiPass_[65] = {0};
    char mqttHost_[128] = {0};
    uint16_t mqttPort_ = 1883;
    char mqttUser_[65] = {0};
    char mqttPass_[65] = {0};

    bool configured_ = false;
    uint32_t lastWifiAttemptMs_ = 0;
    uint32_t lastMqttAttemptMs_ = 0;

    WiFiClient wifiClient_;
    PubSubClient mqttClient_;
    PendingState pending_[LANE_COUNT];
};

#endif // SPOOLSENSE_4LANE_PN532
