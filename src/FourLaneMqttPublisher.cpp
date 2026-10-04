#if defined(SPOOLSENSE_4LANE_PN532)

#include "FourLaneMqttPublisher.h"

#include <WiFi.h>
#include <Preferences.h>
#include <cstring>

bool FourLaneMqttPublisher::begin(const char* baseDeviceId) {
    if (baseDeviceId == nullptr || strlen(baseDeviceId) != 6) {
        Serial.println("FourLaneMQTT: invalid base device ID");
        return false;
    }

    strncpy(baseDeviceId_, baseDeviceId, sizeof(baseDeviceId_) - 1);

    configured_ = loadConfig();
    if (!configured_) {
        Serial.println("FourLaneMQTT: stock SpoolSense Wi-Fi/MQTT settings not found in NVS");
        Serial.println("FourLaneMQTT: serial dry-run remains active");
        return false;
    }

    mqttClient_.setClient(wifiClient_);
    mqttClient_.setServer(mqttHost_, mqttPort_);
    mqttClient_.setBufferSize(1024);

    Serial.printf("FourLaneMQTT: config loaded SSID='%s' MQTT=%s:%u auth=%s\n",
                  wifiSsid_, mqttHost_, static_cast<unsigned>(mqttPort_),
                  mqttUser_[0] != '\0' ? "yes" : "no");

    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSsid_, wifiPass_);
    lastWifiAttemptMs_ = millis();
    Serial.println("FourLaneMQTT: Wi-Fi connect started");
    return true;
}

bool FourLaneMqttPublisher::loadConfig() {
    Preferences prefs;
    if (!prefs.begin("spoolsense", true)) {
        Serial.println("FourLaneMQTT: unable to open NVS namespace 'spoolsense'");
        return false;
    }

    const bool hasSsid = prefs.isKey("wifi_ssid");
    const bool hasHost = prefs.isKey("mqtt_host");

    if (hasSsid) prefs.getString("wifi_ssid", wifiSsid_, sizeof(wifiSsid_));
    if (prefs.isKey("wifi_pass")) prefs.getString("wifi_pass", wifiPass_, sizeof(wifiPass_));
    if (hasHost) prefs.getString("mqtt_host", mqttHost_, sizeof(mqttHost_));
    if (prefs.isKey("mqtt_port")) mqttPort_ = prefs.getUShort("mqtt_port", 1883);
    if (prefs.isKey("mqtt_user")) prefs.getString("mqtt_user", mqttUser_, sizeof(mqttUser_));
    if (prefs.isKey("mqtt_pass")) prefs.getString("mqtt_pass", mqttPass_, sizeof(mqttPass_));

    prefs.end();

    return hasSsid && hasHost && wifiSsid_[0] != '\0' && mqttHost_[0] != '\0';
}

void FourLaneMqttPublisher::loop() {
    if (!configured_) return;

    ensureWifi();
    if (WiFi.status() != WL_CONNECTED) return;

    ensureMqtt();
    if (!mqttClient_.connected()) return;

    mqttClient_.loop();
    flushPending();
}

void FourLaneMqttPublisher::ensureWifi() {
    if (WiFi.status() == WL_CONNECTED) return;

    const uint32_t now = millis();
    if (now - lastWifiAttemptMs_ < 10000) return;

    lastWifiAttemptMs_ = now;
    Serial.println("FourLaneMQTT: retrying Wi-Fi");
    WiFi.disconnect();
    WiFi.begin(wifiSsid_, wifiPass_);
}

void FourLaneMqttPublisher::ensureMqtt() {
    if (mqttClient_.connected()) return;

    const uint32_t now = millis();
    if (now - lastMqttAttemptMs_ < 3000) return;
    lastMqttAttemptMs_ = now;

    char clientId[40] = {0};
    snprintf(clientId, sizeof(clientId), "spoolsense_%s_4lane", baseDeviceId_);

    char lwtTopic[64] = {0};
    snprintf(lwtTopic, sizeof(lwtTopic), "spoolsense/%s/availability", baseDeviceId_);

    bool connected = false;
    if (mqttUser_[0] != '\0') {
        connected = mqttClient_.connect(clientId, mqttUser_, mqttPass_,
                                        lwtTopic, 0, true, "offline");
    } else {
        connected = mqttClient_.connect(clientId, lwtTopic, 0, true, "offline");
    }

    if (!connected) {
        Serial.printf("FourLaneMQTT: MQTT connect failed state=%d\n", mqttClient_.state());
        return;
    }

    mqttClient_.publish(lwtTopic, "online", true);
    Serial.printf("FourLaneMQTT: MQTT connected as %s\n", clientId);
    flushPending();
}

bool FourLaneMqttPublisher::queueState(uint8_t lane, const char* topic, const char* payload) {
    if (lane < 1 || lane > LANE_COUNT || topic == nullptr || payload == nullptr) {
        return false;
    }

    PendingState& state = pending_[lane - 1];
    strncpy(state.topic, topic, sizeof(state.topic) - 1);
    state.topic[sizeof(state.topic) - 1] = '\0';
    strncpy(state.payload, payload, sizeof(state.payload) - 1);
    state.payload[sizeof(state.payload) - 1] = '\0';
    state.valid = true;
    state.dirty = true;

    if (mqttClient_.connected()) {
        flushPending();
    }
    return true;
}

void FourLaneMqttPublisher::flushPending() {
    if (!mqttClient_.connected()) return;

    for (uint8_t i = 0; i < LANE_COUNT; ++i) {
        PendingState& state = pending_[i];
        if (!state.valid || !state.dirty) continue;

        if (mqttClient_.publish(state.topic, state.payload, true)) {
            state.dirty = false;
            Serial.printf("VSCAN L%u MQTT PUBLISHED=%s\n", i + 1, state.topic);
        } else {
            Serial.printf("VSCAN L%u MQTT PUBLISH FAILED=%s\n", i + 1, state.topic);
            return;
        }
    }
}

#endif // SPOOLSENSE_4LANE_PN532
