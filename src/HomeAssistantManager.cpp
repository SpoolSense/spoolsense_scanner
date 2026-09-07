// HomeAssistantManager — MQTT discovery and command handler for Home Assistant integration.
// Publishes spool state (UID, color, remaining weight) and subscribes to HA commands
// (write_tag, update_remaining). FreeRTOS task maintains MQTT connection with exponential backoff.

#include "HomeAssistantManager.h"
#include "TaskUtils.h"
#include "ApplicationManager.h"
#include "ConfigurationManager.h"
#include "ConversionUtils.h"
#include "DeductionManager.h"
#include "TagStateJson.h"
#include "LEDManager.h"
#include "LogBuffer.h"
#include "MemoryDiagnostics.h"
#include "SpoolmanManager.h"
#include "TrayDashboardTypes.h"

#ifndef NATIVE_TEST
  #include <Arduino.h>
  #include <WiFi.h>
  #include <ArduinoJson.h>
  #include <json.hpp>

  #include <esp_heap_caps.h>
  #include "NFCManager.h"
  #include "NFCTypes.h"
  #include "TigerTagParser.h"
  #include "esp_mac.h"
#else
  #include "platform/NativePlatform.h"
#endif

#include <cstring>

#ifndef NATIVE_TEST
using namespace io;
using namespace json;

struct ParsedHACommandPayload {
    char uid[32];
    char filament_type[16];
    char color[16];
    char manufacturer[64];
    float initial_weight_g;
    float remaining_g;
    int32_t spoolman_id;
    bool has_uid;
    bool has_filament_type;
    bool has_color;
    bool has_manufacturer;
    bool has_initial_weight_g;
    bool has_remaining_g;
    bool has_spoolman_id;
};

static void initParsedHACommandPayload(ParsedHACommandPayload& out) {
    memset(&out, 0, sizeof(out));
    out.initial_weight_g = 0.0f;
    out.remaining_g = 0.0f;
    out.spoolman_id = -1;
}

static bool readStringValue(json_reader& reader, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return false;
    out[0] = '\0';
    json_node_type node = reader.node_type();
    if (node != json_node_type::value &&
        node != json_node_type::value_part &&
        node != json_node_type::end_value_part) {
        return false;
    }

    size_t written = 0;
    auto append = [&]() {
        const char* value = reader.value();
        if (value == nullptr) return;
        while (*value != '\0' && written + 1 < outSize) {
            out[written++] = *value++;
        }
        out[written] = '\0';
    };

    append();
    if (node == json_node_type::value_part) {
        while (reader.read()) {
            json_node_type next = reader.node_type();
            if (next != json_node_type::value_part &&
                next != json_node_type::end_value_part) {
                return written > 0;
            }
            append();
            if (next == json_node_type::end_value_part) {
                break;
            }
        }
    }
    return written > 0;
}

static bool parseHACommandPayload(const char* payload, ParsedHACommandPayload& out) {
    initParsedHACommandPayload(out);
    const_buffer_stream stm((const uint8_t*)payload, strlen(payload));
    json_reader reader(stm);

    if (!reader.read() || reader.node_type() != json_node_type::object) {
        return false;
    }
    const unsigned rootDepth = reader.depth();

    while (reader.read()) {
        if (reader.node_type() == json_node_type::end_object && reader.depth() == rootDepth) {
            return true;
        }
        if (reader.node_type() != json_node_type::field || reader.depth() != rootDepth) {
            continue;
        }
        const char* field = reader.value();
        if (!reader.read()) {
            return false;
        }

        if (strcmp(field, "uid") == 0) {
            out.has_uid = readStringValue(reader, out.uid, sizeof(out.uid));
        } else if (strcmp(field, "filament_type") == 0) {
            out.has_filament_type = readStringValue(reader, out.filament_type, sizeof(out.filament_type));
        } else if (strcmp(field, "color") == 0) {
            out.has_color = readStringValue(reader, out.color, sizeof(out.color));
        } else if (strcmp(field, "manufacturer") == 0) {
            out.has_manufacturer = readStringValue(reader, out.manufacturer, sizeof(out.manufacturer));
        } else if (strcmp(field, "initial_weight_g") == 0 &&
                   reader.node_type() == json_node_type::value &&
                   (reader.value_type() == json_value_type::real || reader.value_type() == json_value_type::integer)) {
            out.initial_weight_g = static_cast<float>(reader.value_real());
            out.has_initial_weight_g = true;
        } else if (strcmp(field, "remaining_g") == 0 &&
                   reader.node_type() == json_node_type::value &&
                   (reader.value_type() == json_value_type::real || reader.value_type() == json_value_type::integer)) {
            out.remaining_g = static_cast<float>(reader.value_real());
            out.has_remaining_g = true;
        } else if (strcmp(field, "spoolman_id") == 0 &&
                   reader.node_type() == json_node_type::value &&
                   reader.value_type() == json_value_type::integer) {
            out.spoolman_id = static_cast<int32_t>(reader.value_int());
            out.has_spoolman_id = true;
        }
    }
    return true;
}
#endif

HomeAssistantManager& HomeAssistantManager::getInstance() {
    static HomeAssistantManager instance;
    return instance;
}

void HomeAssistantManager::getDeviceId(char* buf, size_t bufSize) {
    if (bufSize < 7) { buf[0] = '\0'; return; }
#ifndef NATIVE_TEST
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, bufSize, "%02x%02x%02x", mac[3], mac[4], mac[5]);
#else
    strncpy(buf, "aabb12", bufSize);
#endif
}

bool HomeAssistantManager::isConfigured() const {
#ifndef NATIVE_TEST
    auto& config = ConfigurationManager::getInstance();
    return config.getHAEnabled() && strlen(config.getHAMqttHost()) > 0;
#else
    return false;
#endif
}

bool HomeAssistantManager::isConnected() const {
    return connected_;
}

bool HomeAssistantManager::begin() {
    // Publish queue decouples ApplicationManager requests from MQTT task timing
    publishQueue = xQueueCreate(QUEUE_SIZE, sizeof(HAPublishRequest));
    if (publishQueue == nullptr) {
        Serial.println("HomeAssistantManager: Failed to create publish queue");
        return false;
    }
#ifndef NATIVE_TEST
    if (taskControlMutex == nullptr) {
        // Mutex protects task lifecycle (startTask/stopTask race conditions)
        taskControlMutex = xSemaphoreCreateMutex();
        if (taskControlMutex == nullptr) {
            Serial.println("HomeAssistantManager: Failed to create task control mutex");
            return false;
        }
    }
#endif

    // Device ID from MAC address suffix — immutable per device, used for MQTT client ID and topic prefix
    getDeviceId(deviceId_, sizeof(deviceId_));
    Serial.printf("HomeAssistantManager: Initialized (device_id=%s)\n", deviceId_);
    return true;
}

bool HomeAssistantManager::enqueuePublish(const HAPublishRequest& req) {
    if (publishQueue == nullptr) return false;
    if (xQueueSend(publishQueue, &req, 0) != pdTRUE) {
        Serial.printf("HomeAssistantManager: Queue full, dropped publish to %s\n", req.topic);
        return false;
    }
    return true;
}

#ifndef NATIVE_TEST

void HomeAssistantManager::startTask() {
    if (taskControlMutex == nullptr) {
        Serial.println("HomeAssistantManager: task control mutex not initialized");
        return;
    }
    if (xSemaphoreTake(taskControlMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("HomeAssistantManager: startTask mutex timeout");
        return;
    }

    auto& config = ConfigurationManager::getInstance();
    const char* host = config.getHAMqttHost();
    size_t hostLen = strlen(host);
    bool enabled = config.getHAEnabled();

    Serial.printf("HomeAssistantManager: Config snapshot enabled=%s host='%s' host_len=%u port=%u user_set=%s\n",
                  enabled ? "true" : "false",
                  host,
                  static_cast<unsigned>(hostLen),
                  static_cast<unsigned>(config.getHAMqttPort()),
                  strlen(config.getHAMqttUser()) > 0 ? "true" : "false");

    if (!isConfigured()) {
        Serial.printf("HomeAssistantManager: Not configured, skipping task start (enabled=%s, host_len=%u)\n",
                      enabled ? "true" : "false",
                      static_cast<unsigned>(hostLen));
        xSemaphoreGive(taskControlMutex);
        return;
    }

    if (taskHandle != nullptr) {
        Serial.println("HomeAssistantManager: Task already running");
        xSemaphoreGive(taskControlMutex);
        return;
    }

    // Reset task state for clean (re)start — exponential backoff timer and last reconnect attempt
    stopRequested_ = false;
    connected_ = false;
    lastMqttState_ = -1;
    reconnectDelay_ = 1000;  // initial backoff, doubles on each failure
    lastReconnectAttempt_ = 0;

    // Core 1 for MQTT task — keeps main WiFi/NFC/application logic on Core 0
    BaseType_t rc = createTaskWithAffinity(
        taskFunc,
        "HATask",
        TASK_STACK_SIZE,
        this,
        TASK_PRIORITY,
        &taskHandle,
        1
    );
    if (rc != pdPASS || taskHandle == nullptr) {
        size_t free8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        Serial.printf("HomeAssistantManager: Failed to start task (rc=%ld, free_heap=%u)\n",
                      static_cast<long>(rc),
                      static_cast<unsigned>(free8bit));
        taskHandle = nullptr;
        xSemaphoreGive(taskControlMutex);
        return;
    }

    Serial.printf("HomeAssistantManager: Task started (stack=%u, free_heap=%u)\n",
                  static_cast<unsigned>(TASK_STACK_SIZE),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
    xSemaphoreGive(taskControlMutex);
}

void HomeAssistantManager::taskFunc(void* param) {
    Serial.println("HomeAssistantManager: taskFunc entered");
    static_cast<HomeAssistantManager*>(param)->taskLoop();
}

void HomeAssistantManager::stopTask() {
    if (taskControlMutex == nullptr) return;
    if (xSemaphoreTake(taskControlMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("HomeAssistantManager: stopTask mutex timeout");
        return;
    }

    if (taskHandle == nullptr) {
        connected_ = false;
        stopRequested_ = false;
        xSemaphoreGive(taskControlMutex);
        return;
    }

    stopRequested_ = true;
    xSemaphoreGive(taskControlMutex);

    uint32_t startMs = millis();
    while (true) {
        if (xSemaphoreTake(taskControlMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool stopped = (taskHandle == nullptr);
            xSemaphoreGive(taskControlMutex);
            if (stopped) break;
        }
        if (millis() - startMs >= 5000) {
            Serial.println("HomeAssistantManager: stopTask timeout waiting for task exit");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool HomeAssistantManager::restartAndTestConnection(uint32_t timeoutMs, int* mqttStateOut) {
    stopTask();
    startTask();

    if (taskControlMutex != nullptr &&
        xSemaphoreTake(taskControlMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        bool started = (taskHandle != nullptr);
        xSemaphoreGive(taskControlMutex);
        if (!started) {
            if (mqttStateOut != nullptr) *mqttStateOut = getLastMqttState();
            return false;
        }
    }

    uint32_t startMs = millis();
    while (millis() - startMs < timeoutMs) {
        if (isConnected()) {
            if (mqttStateOut != nullptr) *mqttStateOut = 0;
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (mqttStateOut != nullptr) {
        *mqttStateOut = getLastMqttState();
    }
    return false;
}

void HomeAssistantManager::taskLoop() {
    Serial.printf("HomeAssistantManager: taskLoop entered (core=%d, wifi_status=%d)\n",
                  xPortGetCoreID(), static_cast<int>(WiFi.status()));
    auto& config = ConfigurationManager::getInstance();

    // One-time MQTT client config (buffer size, callback for incoming commands)
    mqttClient.setClient(wifiClient);
    mqttClient.setServer(config.getHAMqttHost(), config.getHAMqttPort());
    mqttClient.setBufferSize(2048);
    mqttClient.setCallback(mqttCallback);

    Serial.printf("HomeAssistantManager: Connecting to MQTT broker %s:%d\n",
                  config.getHAMqttHost(), config.getHAMqttPort());

    uint32_t lastHeartbeatMs = 0;
    while (true) {
        if (stopRequested_) {
            Serial.println("HomeAssistantManager: Stop requested");
            break;
        }

        MemoryDiagnostics::reportSelf(MemoryDiagnostics::Task::HA);
        uint32_t now = millis();

        // Main loop: reconnect if needed, drain publish queue, process MQTT
        if (!mqttClient.connected()) {
            if (now - lastReconnectAttempt_ >= reconnectDelay_) {
                lastReconnectAttempt_ = now;
                if (reconnect()) {
                    connected_ = true;
                    lastMqttState_ = 0;
                    reconnectDelay_ = 1000;  // reset exponential backoff on success
                    Serial.println("HomeAssistantManager: MQTT connected, publishing discovery/state");
                    // Reset discovery deduplication so reconnect publishes fresh entities
                    lastDiscoveryUid_[0] = '\0';
                    publishDiscovery();
                    subscribeCommands();
                    publishAvailability("online");
                    publishCurrentTagState();
                    Serial.println("HomeAssistantManager: Connected to MQTT broker");
                    LogBuffer::getInstance().logPrintf("MQTT connected\n");
                } else {
                    connected_ = false;
                    // Exponential backoff: 1s → 2s → 4s → ... up to MAX (avoid broker hammering)
                    reconnectDelay_ = (reconnectDelay_ < MAX_RECONNECT_DELAY)
                        ? reconnectDelay_ * 2 : MAX_RECONNECT_DELAY;
                    lastMqttState_ = mqttClient.state();
                    Serial.printf("HomeAssistantManager: MQTT connect failed, retry in %lums\n",
                                  reconnectDelay_);
                    LogBuffer::getInstance().logPrintf("MQTT connect failed, retry in %lums\n", reconnectDelay_);
                }
            }
        }

        // Process queued publishes from ApplicationManager (tag scans, writes, etc.)
        HAPublishRequest req;
        while (xQueueReceive(publishQueue, &req, 0) == pdTRUE) {
            if (mqttClient.connected()) {
                mqttClient.publish(req.topic, req.payload, req.retained);
                char tagStateTopic[64];
                char tagAttrsTopic[64];
                snprintf(tagStateTopic, sizeof(tagStateTopic), "spoolsense/%s/tag/state", deviceId_);
                snprintf(tagAttrsTopic, sizeof(tagAttrsTopic), "spoolsense/%s/tag/attributes", deviceId_);
                if (strcmp(req.topic, tagStateTopic) == 0) {
                    // Mirror state payload to attributes topic for unified spool context in HA
                    mqttClient.publish(tagAttrsTopic, req.payload, req.retained);
                    // Republish discovery only on UID change (command topics include UID, so discovery topics must too)
                    CurrentSpoolState state;
                    char currentUid[17] = {0};
                    if (NFCManager::getInstance().getCurrentSpoolState(state) &&
                        state.present && state.spool_id[0] != '\0') {
                        strncpy(currentUid, state.spool_id, sizeof(currentUid) - 1);
                    }
                    if (strcmp(currentUid, lastDiscoveryUid_) != 0) {
                        strncpy(lastDiscoveryUid_, currentUid, sizeof(lastDiscoveryUid_) - 1);
                        publishDiscovery();
                    }
                }
            }
        }

        if (mqttClient.connected()) {
            // Process incoming MQTT messages (commands from HA)
            mqttClient.loop();
        }

        // Main loop iteration: allow other tasks to run
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (mqttClient.connected()) {
        publishAvailability("offline");
        mqttClient.disconnect();
    }
    connected_ = false;

    if (taskControlMutex != nullptr &&
        xSemaphoreTake(taskControlMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        taskHandle = nullptr;
        stopRequested_ = false;
        xSemaphoreGive(taskControlMutex);
    } else {
        taskHandle = nullptr;
        stopRequested_ = false;
    }
    Serial.println("HomeAssistantManager: taskLoop exiting");
    vTaskDelete(nullptr);
}

bool HomeAssistantManager::reconnect() {
    auto& config = ConfigurationManager::getInstance();

    // Client ID is unique per device (MAC-based) for broker deduplication
    char clientId[32];
    snprintf(clientId, sizeof(clientId), "spoolsense_%s", deviceId_);

    // Last-Will-Testament: publish "offline" if client drops unexpectedly
    char lwtTopic[64];
    snprintf(lwtTopic, sizeof(lwtTopic), "spoolsense/%s/availability", deviceId_);

    bool result;
    if (strlen(config.getHAMqttUser()) > 0) {
        // Authenticated connection (username/password or cert-based)
        result = mqttClient.connect(clientId,
                                     config.getHAMqttUser(),
                                     config.getHAMqttPass(),
                                     lwtTopic, 0, true, "offline");
    } else {
        // Anonymous connection (open broker)
        result = mqttClient.connect(clientId, lwtTopic, 0, true, "offline");
    }

    if (!result) {
        lastMqttState_ = mqttClient.state();
        Serial.printf("HomeAssistantManager: reconnect failed (mqtt_state=%d wifi_status=%d host=%s port=%u)\n",
                      mqttClient.state(),
                      static_cast<int>(WiFi.status()),
                      config.getHAMqttHost(),
                      static_cast<unsigned>(config.getHAMqttPort()));
    }
    return result;
}

void HomeAssistantManager::publishAvailability(const char* state) {
    char topic[64];
    snprintf(topic, sizeof(topic), "spoolsense/%s/availability", deviceId_);
    mqttClient.publish(topic, state, true);
}

void HomeAssistantManager::subscribeCommands() {
    char topic[64];
    snprintf(topic, sizeof(topic), "spoolsense/%s/cmd/#", deviceId_);
    mqttClient.subscribe(topic);
    Serial.printf("HomeAssistantManager: Subscribed to %s\n", topic);
}

void HomeAssistantManager::publishDiscovery() {
    // Discovery payloads use abbreviated HA keys (~) to fit in 768-byte buffer.
    char baseTopic[48];
    snprintf(baseTopic, sizeof(baseTopic), "spoolsense/%s", deviceId_);
    // Include current UID in discovery so write commands are automatically scoped to the loaded spool
    char currentUid[64] = {0};
    CurrentSpoolState spoolState;
    if (NFCManager::getInstance().getCurrentSpoolState(spoolState) &&
        spoolState.present &&
        spoolState.spool_id[0] != '\0') {
        strncpy(currentUid, spoolState.spool_id, sizeof(currentUid) - 1);
    }
    char updateRemainingCmdTopic[128];
    char writeTagCmdTopic[128];
    if (currentUid[0] != '\0') {
        // Command topics include UID: prevents accidental commands to wrong tag on multi-scanner setup
        snprintf(updateRemainingCmdTopic, sizeof(updateRemainingCmdTopic),
                 "~/cmd/update_remaining/%s", currentUid);
        snprintf(writeTagCmdTopic, sizeof(writeTagCmdTopic),
                 "~/cmd/write_tag/%s", currentUid);
    } else {
        // No spool loaded: generic command topics (fallback)
        snprintf(updateRemainingCmdTopic, sizeof(updateRemainingCmdTopic),
                 "~/cmd/update_remaining");
        snprintf(writeTagCmdTopic, sizeof(writeTagCmdTopic),
                 "~/cmd/write_tag");
    }

    auto publishDiscoveryPayload = [&](const char* component, const char* objectId, const char* payload) {
        char discoveryTopic[128];
        snprintf(discoveryTopic, sizeof(discoveryTopic),
                 "homeassistant/%s/spoolsense_%s/%s/config",
                 component, deviceId_, objectId);
        size_t len = strlen(payload);
        bool ok = mqttClient.publish(discoveryTopic, payload, true);
        Serial.printf("HomeAssistantManager: Discovery %s -> %s (%u bytes)\n",
                      discoveryTopic, ok ? "OK" : "FAIL", (unsigned)len);
    };
    auto publishNumberEntity = [&](const char* objectId, const char* name, const char* valTpl,
                                   const char* cmdTopic, const char* cmdTpl, float minV, float maxV,
                                   float stepV, const char* unitOfMeas, const char* icon) {
        char payload[768];
        int written = snprintf(payload, sizeof(payload),
                               "{\"~\":\"%s\",\"name\":\"%s\","
                               "\"unique_id\":\"spoolsense_%s_%s\",\"obj_id\":\"spoolsense_%s_%s\","
                               "\"stat_t\":\"~/tag/state\",\"val_tpl\":\"%s\","
                               "\"json_attr_t\":\"~/tag/state\",\"json_attr_tpl\":\"{{ value_json }}\","
                               "\"cmd_t\":\"%s\",\"cmd_tpl\":\"%s\","
                               "\"avty_t\":\"~/availability\",\"min\":%.1f,\"max\":%.1f,\"step\":%.1f,\"mode\":\"box\","
                               "\"unit_of_meas\":\"%s\",\"ic\":\"%s\","
                               "\"dev\":{\"ids\":[\"spoolsense_%s\"]}}",
                               baseTopic, name,
                               deviceId_, objectId, deviceId_, objectId,
                               valTpl, cmdTopic, cmdTpl,
                               minV, maxV, stepV, unitOfMeas, icon, deviceId_);
        if (written < 0 || written >= (int)sizeof(payload)) {
            Serial.printf("HomeAssistantManager: Discovery payload too large for number/%s, skipping\n",
                          objectId);
            return;
        }
        publishDiscoveryPayload("number", objectId, payload);
    };
    auto publishSelectEntity = [&](const char* objectId, const char* name, const char* valTpl,
                                   const char* cmdTopic, const char* cmdTpl, const char* icon) {
        char payload[768];
        int written = snprintf(payload, sizeof(payload),
                               "{\"~\":\"%s\",\"name\":\"%s\","
                               "\"unique_id\":\"spoolsense_%s_%s\",\"obj_id\":\"spoolsense_%s_%s\","
                               "\"stat_t\":\"~/tag/state\",\"val_tpl\":\"%s\","
                               "\"json_attr_t\":\"~/tag/state\",\"json_attr_tpl\":\"{{ value_json }}\","
                               "\"cmd_t\":\"%s\",\"cmd_tpl\":\"%s\","
                               "\"avty_t\":\"~/availability\","
                               "\"options\":[\"PLA\",\"PETG\",\"ABS\",\"ASA\",\"TPU\",\"PC\",\"Nylon\",\"PVA\",\"HIPS\"],"
                               "\"ic\":\"%s\",\"dev\":{\"ids\":[\"spoolsense_%s\"]}}",
                               baseTopic, name,
                               deviceId_, objectId, deviceId_, objectId,
                               valTpl, cmdTopic, cmdTpl,
                               icon, deviceId_);
        if (written < 0 || written >= (int)sizeof(payload)) {
            Serial.printf("HomeAssistantManager: Discovery payload too large for select/%s, skipping\n",
                          objectId);
            return;
        }
        publishDiscoveryPayload("select", objectId, payload);
    };
    auto publishTextEntity = [&](const char* objectId, const char* name, const char* valTpl,
                                 const char* cmdTopic, const char* cmdTpl, const char* icon) {
        char payload[768];
        int written = snprintf(payload, sizeof(payload),
                               "{\"~\":\"%s\",\"name\":\"%s\","
                               "\"unique_id\":\"spoolsense_%s_%s\",\"obj_id\":\"spoolsense_%s_%s\","
                               "\"stat_t\":\"~/tag/state\",\"val_tpl\":\"%s\","
                               "\"json_attr_t\":\"~/tag/state\",\"json_attr_tpl\":\"{{ value_json }}\","
                               "\"cmd_t\":\"%s\",\"cmd_tpl\":\"%s\","
                               "\"avty_t\":\"~/availability\",\"ic\":\"%s\","
                               "\"dev\":{\"ids\":[\"spoolsense_%s\"]}}",
                               baseTopic, name,
                               deviceId_, objectId, deviceId_, objectId,
                               valTpl, cmdTopic, cmdTpl,
                               icon, deviceId_);
        if (written < 0 || written >= (int)sizeof(payload)) {
            Serial.printf("HomeAssistantManager: Discovery payload too large for text/%s, skipping\n",
                          objectId);
            return;
        }
        publishDiscoveryPayload("text", objectId, payload);
    };

    // Spool sensor: presence + tag metadata (color, weight, material) as attributes
    {
        char payload[768];
        int written = snprintf(payload, sizeof(payload),
                               "{\"~\":\"%s\",\"name\":\"Spool\","
                               "\"unique_id\":\"spoolsense_%s_spool\",\"obj_id\":\"spoolsense_%s_spool\","
                               "\"stat_t\":\"~/tag/state\","
                               "\"val_tpl\":\"{{ 'present' if value_json.present else 'not_present' }}\","
                               "\"avty_t\":\"~/availability\","
                               "\"json_attr_t\":\"~/tag/attributes\","
                               "\"ic\":\"mdi:printer-3d-nozzle\","
                               "\"dev\":{\"ids\":[\"spoolsense_%s\"],\"name\":\"SpoolSense Scanner\","
                               "\"mf\":\"SpoolSense\",\"sw\":\"%s\"}}",
                               baseTopic, deviceId_, deviceId_, deviceId_, DEVICE_VERSION);
        if (written >= 0 && written < (int)sizeof(payload)) {
            publishDiscoveryPayload("sensor", "spool", payload);
        } else {
            Serial.println("HomeAssistantManager: Discovery payload too large for sensor/spool, skipping");
        }
    }

    // Printer warning sensor (filament mismatch, temp warnings from PrusaLink)
    {
        char payload[512];
        int written = snprintf(payload, sizeof(payload),
                               "{\"name\":\"Printer Warning\","
                               "\"uniq_id\":\"spoolsense_%s_printer_warning\","
                               "\"stat_t\":\"%s/printer/warning\","
                               "\"val_tpl\":\"{{ value_json.warning }}\","
                               "\"json_attr_t\":\"%s/printer/warning\","
                               "\"avty_t\":\"%s/availability\","
                               "\"ic\":\"mdi:alert-circle\","
                               "\"ent_cat\":\"diagnostic\","
                               "\"dev\":{\"ids\":[\"spoolsense_%s\"]}}",
                               deviceId_, baseTopic, baseTopic, baseTopic, deviceId_);
        if (written >= 0 && written < (int)sizeof(payload)) {
            publishDiscoveryPayload("sensor", "printer_warning", payload);
        }
    }

    // UID is encoded in topic path (not payload) for clean command routing and deduplication
    char updateRemainingCmdTpl[128];
    snprintf(updateRemainingCmdTpl, sizeof(updateRemainingCmdTpl),
             "{\\\"remaining_g\\\": {{ value | float }}}");
    char writeInitialCmdTpl[128];
    snprintf(writeInitialCmdTpl, sizeof(writeInitialCmdTpl),
             "{\\\"initial_weight_g\\\": {{ value | float }}}");
    char writeSpoolmanCmdTpl[128];
    snprintf(writeSpoolmanCmdTpl, sizeof(writeSpoolmanCmdTpl),
             "{\\\"spoolman_id\\\": {{ value | int }}}");
    char writeMaterialCmdTpl[128];
    snprintf(writeMaterialCmdTpl, sizeof(writeMaterialCmdTpl),
             "{\\\"filament_type\\\": {{ value | tojson }}}");
    char writeManufacturerCmdTpl[128];
    snprintf(writeManufacturerCmdTpl, sizeof(writeManufacturerCmdTpl),
             "{\\\"manufacturer\\\": {{ value | tojson }}}");

    publishNumberEntity("set_remaining_weight", "Set Remaining Filament",
                        "{{ value_json.remaining_g | default(0) }}",
                        updateRemainingCmdTopic, updateRemainingCmdTpl,
                        0.0f, 5000.0f, 1.0f, "g", "mdi:weight-gram");
    publishNumberEntity("set_initial_weight", "Set Initial Spool Weight",
                        "{{ value_json.initial_weight_g | default(1000) }}",
                        writeTagCmdTopic, writeInitialCmdTpl,
                        0.0f, 5000.0f, 1.0f, "g", "mdi:scale");
    publishNumberEntity("set_spoolman_id", "Set Spoolman ID",
                        "{{ value_json.spoolman_id | default(-1) }}",
                        writeTagCmdTopic, writeSpoolmanCmdTpl,
                        -1.0f, 2000000.0f, 1.0f, "", "mdi:database");
    publishSelectEntity("set_material_type", "Set Material Type",
                        "{{ value_json.material_type | default('PLA') }}",
                        writeTagCmdTopic, writeMaterialCmdTpl,
                        "mdi:printer-3d-nozzle");
    publishTextEntity("set_manufacturer", "Set Manufacturer",
                      "{{ value_json.manufacturer | default('') }}",
                      writeTagCmdTopic, writeManufacturerCmdTpl,
                      "mdi:factory");

    Serial.println("HomeAssistantManager: Discovery payloads published");
}

void HomeAssistantManager::publishCurrentTagState() {
    CurrentSpoolState& spool = spoolScratch_;

    bool got = NFCManager::getInstance().getCurrentSpoolState(spool);
    char stateTopic[64];
    char attrsTopic[64];
    snprintf(stateTopic, sizeof(stateTopic), "spoolsense/%s/tag/state", deviceId_);
    snprintf(attrsTopic, sizeof(attrsTopic), "spoolsense/%s/tag/attributes", deviceId_);

    if (!got || !spool.present) {
        char json[256];
        buildEmptyTagStateJson(json, sizeof(json));
        mqttClient.publish(stateTopic, json, true);
        mqttClient.publish(attrsTopic, json, true);
        Serial.println("HomeAssistantManager: Published tag state (not present)");
        return;
    }

    // Extract tag-specific fields into shared struct
    TagStateFields f = {};
    strncpy(f.uid, spool.spool_id, sizeof(f.uid) - 1);
    f.present = true;
    f.tag_format = tagKindToMqttFormat(spool.kind);
    f.blank = spool.blank_tag_present;
    f.spoolman_id = -1;

    if (spool.kind == TagKind::TigerTag) {
        TigerTagData tt;
        if (NFCManager::getInstance().getLastTigerTagData(tt) && tt.valid) {
            f.tag_data_valid = true;
            strncpy(f.material_type, tt.material_name, sizeof(f.material_type) - 1);
            strncpy(f.material_name, tt.material_name, sizeof(f.material_name) - 1);
            strncpy(f.manufacturer, tt.brand_name, sizeof(f.manufacturer) - 1);
            snprintf(f.color, sizeof(f.color), "#%02X%02X%02X", tt.color_r, tt.color_g, tt.color_b);
            f.initial_weight_g = tt.weight_g;
            f.remaining_g = tt.weight_g;
        }
    } else if (spool.kind == TagKind::OpenTag3D) {
        opentag3d_t ot3d;
        if (NFCManager::getInstance().getLastOpenTag3DData(ot3d)) {
            f.tag_data_valid = true;
            strncpy(f.material_type, ot3d.base_material, sizeof(f.material_type) - 1);
            strncpy(f.material_name, ot3d.base_material, sizeof(f.material_name) - 1);
            strncpy(f.manufacturer, ot3d.manufacturer, sizeof(f.manufacturer) - 1);
            snprintf(f.color, sizeof(f.color), "#%02X%02X%02X",
                     ot3d.color_rgba[0][0], ot3d.color_rgba[0][1], ot3d.color_rgba[0][2]);
            float weight = (ot3d.has_extended && ot3d.measured_filament_weight_g > 0)
                           ? ot3d.measured_filament_weight_g : ot3d.target_weight_g;
            f.initial_weight_g = weight;
            f.remaining_g = weight;
        }
    } else if (spool.tag_data_valid) {
        // OpenPrintTag
        f.tag_data_valid = true;
        uint8_t matType = 0;
        opt_get_material_type(&spool.tag_data, &matType);
        strncpy(f.material_type, materialTypeToString(matType), sizeof(f.material_type) - 1);

        char customName[33] = {0};
        if (opt_get_material_name(&spool.tag_data, customName, sizeof(customName)) == OPT_OK
                && customName[0] != '\0') {
            strncpy(f.material_name, customName, sizeof(f.material_name) - 1);
        } else {
            strncpy(f.material_name, f.material_type, sizeof(f.material_name) - 1);
        }

        uint8_t color[4] = {0};
        opt_get_primary_color(&spool.tag_data, color);
        snprintf(f.color, sizeof(f.color), "#%02X%02X%02X", color[0], color[1], color[2]);

        opt_get_brand_name(&spool.tag_data, f.manufacturer, sizeof(f.manufacturer));
        opt_get_actual_full_weight(&spool.tag_data, &f.initial_weight_g);
        float consumed = 0.0f;
        opt_get_consumed_weight(&spool.tag_data, &consumed);
        f.remaining_g = f.initial_weight_g - consumed;
        if (f.remaining_g < 0) f.remaining_g = 0;
        opt_get_gp_spoolman_id(&spool.tag_data, &f.spoolman_id);
    }

    char json[512];
    buildTagStateJson(json, sizeof(json), f);
    mqttClient.publish(stateTopic, json, true);
    mqttClient.publish(attrsTopic, json, true);
    Serial.printf("HomeAssistantManager: Published current tag state uid=%s kind=%d\n",
                  spool.spool_id, (int)spool.kind);
}

// MQTT library callback: invoked by mqttClient.loop() on subscribed messages
// Static context — routes to instance's handleCommand for processing
void HomeAssistantManager::mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
    // Null-terminate payload buffer (MQTT payload is not null-terminated)
    char buf[1536];
    size_t copyLen = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
    memcpy(buf, payload, copyLen);
    buf[copyLen] = '\0';

    getInstance().handleCommand(topic, buf);
}

void HomeAssistantManager::handleCommand(const char* topic, const char* payload) {
    Serial.printf("HomeAssistantManager: Command received: %s payload=%s\n", topic, payload);

    // Parse command topic: spoolsense/{id}/cmd/{command}[/uid]
    // UID in topic allows discovery to publish uid-specific commands (safe multi-scanner mode)
    char cmdPrefix[64];
    snprintf(cmdPrefix, sizeof(cmdPrefix), "spoolsense/%s/cmd/", deviceId_);

    if (strncmp(topic, cmdPrefix, strlen(cmdPrefix)) != 0) {
        Serial.println("HomeAssistantManager: Unknown topic prefix");
        return;
    }
    const char* commandPath = topic + strlen(cmdPrefix);
    const char* slash = strchr(commandPath, '/');
    char command[32] = {0};
    const char* uidFromTopic = "";
    if (slash != nullptr) {
        size_t commandLen = static_cast<size_t>(slash - commandPath);
        if (commandLen >= sizeof(command)) commandLen = sizeof(command) - 1;
        memcpy(command, commandPath, commandLen);
        command[commandLen] = '\0';
        uidFromTopic = slash + 1;
    } else {
        strncpy(command, commandPath, sizeof(command) - 1);
    }
    if (strcmp(command, "response") == 0) {
        return;  // ignore echo of our own responses
    }

    // set_color: LED color from Spoolman (middleware sends, no tag validation needed)
    if (strcmp(command, "set_color") == 0) {
        if (strlen(payload) == 6) {
            uint8_t r, g, b;
            if (sscanf(payload, "%2hhx%2hhx%2hhx", &r, &g, &b) == 3) {
                Serial.printf("HomeAssistantManager: set_color #%s → RGB(%d,%d,%d)\n", payload, r, g, b);
                extern LEDManager ledManager;
                ledManager.showFilamentColor(r, g, b);
            }
        }
        return;
    }

    // low_spool: middleware/HA sends true/false to trigger LED breathing mid-print
    if (strcmp(command, "low_spool") == 0) {
        extern LEDManager ledManager;
        bool enable = (strcmp(payload, "true") == 0);
        // Use current LED target color (persists after tag removal)
        uint8_t r = 0, g = 0, b = 0;
        ledManager.getTargetColor(r, g, b);
        if (r == 0 && g == 0 && b == 0) { r = g = b = 0x33; }  // fallback dim white
        if (enable) {
            ledManager.breatheFilamentColor(r, g, b);
            LogBuffer::getInstance().logPrintf("Low spool: breathing enabled\n");
        } else {
            ledManager.showFilamentColor(r, g, b);
            LogBuffer::getInstance().logPrintf("Low spool: breathing disabled\n");
        }
        publishCommandResponse(command, true, nullptr);
        return;
    }

    // deduct: store filament usage deduction — tag may not be on scanner
    if (strcmp(command, "deduct") == 0) {
        if (strlen(uidFromTopic) == 0) {
            publishCommandResponse(command, false, "missing_uid_in_topic");
            return;
        }
        StaticJsonDocument<64> deductDoc;
        if (deserializeJson(deductDoc, payload)) {
            publishCommandResponse(command, false, "invalid_json");
            return;
        }
        float deductG = deductDoc["deduct_g"] | 0.0f;
        if (deductG <= 0.0f) {
            publishCommandResponse(command, false, "invalid_deduct_g");
            return;
        }

        DeductionManager::getInstance().storePending(uidFromTopic, deductG);

        // If tag is currently on scanner, apply immediately (write to tag or Spoolman)
        CurrentSpoolState deductSpool;
        if (NFCManager::getInstance().getCurrentSpoolState(deductSpool) &&
            deductSpool.present && strcasecmp(deductSpool.spool_id, uidFromTopic) == 0) {
            DeductionManager::getInstance().applyIfPending(deductSpool.spool_id, deductSpool.kind);
        } else if (SpoolmanManager::getInstance().isConfigured()) {
            // Tag not on scanner — try Spoolman direct (Bambu AMS use case)
            bool spOk = false;
            SpoolmanManager::getInstance().deductFromSpoolman(uidFromTopic, deductG, &spOk);
            if (spOk) {  // success includes a 0 g deduction on an already-empty spool
                DeductionManager::getInstance().clearPending(uidFromTopic);
            }
        }

        publishCommandResponse(command, true, nullptr);
        return;
    }

    // tray_update: HA sends full AMS tray state for dashboard display
    if (strcmp(command, "tray_update") == 0) {
        StaticJsonDocument<2048> trayDoc;
        if (deserializeJson(trayDoc, payload)) {
            publishCommandResponse(command, false, "invalid_json");
            return;
        }

        JsonArray arr = trayDoc.as<JsonArray>();
        if (arr.isNull()) {
            publishCommandResponse(command, false, "expected_array");
            return;
        }

        TrayDashboardState state = {};
        state.has_data = true;
        uint8_t count = 0;

        for (JsonObject obj : arr) {
            if (count >= MAX_TRAYS) break;

            TrayData& tray = state.trays[count];
            tray.tray_index = obj["tray_index"] | 0;
            tray.ams_id = obj["ams_id"] | 0;
            tray.weight_g = obj["weight_g"] | 0;
            tray.populated = true;

            const char* mat = obj["material"] | "";
            strncpy(tray.material, mat, sizeof(tray.material) - 1);
            tray.material[sizeof(tray.material) - 1] = '\0';

            const char* colorHex = obj["color"] | "333333";
            if (strlen(colorHex) == 6) {
                uint32_t c = strtoul(colorHex, nullptr, 16);
                tray.color[0] = (c >> 16) & 0xFF;
                tray.color[1] = (c >> 8) & 0xFF;
                tray.color[2] = c & 0xFF;
            }

            count++;
        }
        state.tray_count = count;

        ApplicationManager::getInstance().updateTrayDashboard(state);
        AppMessage msg = {};
        msg.type = AppMessageType::TRAY_UPDATE;
        ApplicationManager::getInstance().sendMessage(msg);

        publishCommandResponse(command, true, nullptr);
        return;
    }

    // tray_assign: loading blueprint assigns a scanned UID to a specific AMS tray
    if (strcmp(command, "tray_assign") == 0) {
        StaticJsonDocument<128> assignDoc;
        if (deserializeJson(assignDoc, payload)) {
            publishCommandResponse(command, false, "invalid_json");
            return;
        }

        uint8_t trayIndex = assignDoc["tray_index"] | 0;
        const char* uid = assignDoc["uid"] | "";
        int32_t spoolmanId = assignDoc["spoolman_id"] | -1;

        ApplicationManager& app = ApplicationManager::getInstance();
        app.pendingAssignTrayIndex_ = trayIndex;
        strncpy(app.pendingAssignUid_, uid, sizeof(app.pendingAssignUid_) - 1);
        app.pendingAssignUid_[sizeof(app.pendingAssignUid_) - 1] = '\0';
        app.pendingAssignSpoolmanId_ = spoolmanId;

        AppMessage msg = {};
        msg.type = AppMessageType::TRAY_ASSIGN;
        app.sendMessage(msg);

        publishCommandResponse(command, true, nullptr);
        return;
    }

    // deduct_tray: deduction blueprint sends tray_index + grams after print finishes
    if (strcmp(command, "deduct_tray") == 0) {
        StaticJsonDocument<128> deductDoc;
        if (deserializeJson(deductDoc, payload)) {
            publishCommandResponse(command, false, "invalid_json");
            return;
        }

        uint8_t trayIndex = deductDoc["tray_index"] | 0;
        float deductG = deductDoc["deduct_g"] | 0.0f;
        if (deductG <= 0.0f) {
            publishCommandResponse(command, false, "invalid_deduct_g");
            return;
        }

        // Resolve tray_index → UID from dashboard state
        const TrayDashboardState& dash = ApplicationManager::getInstance().getTrayDashboardState();
        const char* resolvedUid = nullptr;
        for (uint8_t i = 0; i < dash.tray_count; i++) {
            if (dash.trays[i].tray_index == trayIndex && strlen(dash.trays[i].uid) > 0) {
                resolvedUid = dash.trays[i].uid;
                break;
            }
        }

        if (resolvedUid == nullptr) {
            Serial.printf("HomeAssistantManager: deduct_tray — no UID for tray %d\n", trayIndex);
            publishCommandResponse(command, false, "no_uid_for_tray");
            return;
        }

        Serial.printf("HomeAssistantManager: deduct_tray — tray %d → UID %s, %.1fg\n",
                      trayIndex, resolvedUid, deductG);

        DeductionManager::getInstance().storePending(resolvedUid, deductG);

        // Try Spoolman direct deduction (spool not on scanner)
        if (SpoolmanManager::getInstance().isConfigured()) {
            bool spOk = false;
            SpoolmanManager::getInstance().deductFromSpoolman(resolvedUid, deductG, &spOk);
            if (spOk) {  // success includes a 0 g deduction on an already-empty spool
                DeductionManager::getInstance().clearPending(resolvedUid);
            }
        }

        // Update dashboard tray weight
        ApplicationManager& app = ApplicationManager::getInstance();
        for (uint8_t i = 0; i < app.getTrayDashboardState().tray_count; i++) {
            if (app.trayDashboardState_.trays[i].tray_index == trayIndex) {
                uint16_t currentWeight = app.trayDashboardState_.trays[i].weight_g;
                uint16_t deductWeight = static_cast<uint16_t>(deductG);
                app.trayDashboardState_.trays[i].weight_g =
                    (currentWeight > deductWeight) ? (currentWeight - deductWeight) : 0;
                // Trigger dashboard re-render
                AppMessage dashMsg = {};
                dashMsg.type = AppMessageType::TRAY_UPDATE;
                app.sendMessage(dashMsg);
                break;
            }
        }

        publishCommandResponse(command, true, nullptr);
        return;
    }

    // Parse command payload (JSON with optional uid, filament_type, color, etc.)
    ParsedHACommandPayload cmdPayload;
    if (!parseHACommandPayload(payload, cmdPayload)) {
        Serial.printf("HomeAssistantManager: JSON parse error\n");
        publishCommandResponse(command, false, "invalid_json");
        return;
    }

    // Require a loaded tag for write/update commands (tag present + valid state)
    CurrentSpoolState& spool = spoolScratch_;
    if (!NFCManager::getInstance().getCurrentSpoolState(spool) || !spool.present) {
        Serial.printf("HomeAssistantManager: Rejecting cmd '%s': no tag present\n", command);
        publishCommandResponse(command, false, "no_tag_present");
        return;
    }

    // Resolve UID: prefer payload, then topic, then reject (no UID = can't validate target)
    const char* uidFromPayload = cmdPayload.has_uid ? cmdPayload.uid : "";
    const char* uid = (strlen(uidFromPayload) > 0) ? uidFromPayload : uidFromTopic;
    if (strlen(uid) == 0) {
        Serial.printf("HomeAssistantManager: Rejecting cmd '%s': missing uid in payload/topic: %s\n", command, payload);
        publishCommandResponse(command, false, "missing_uid");
        return;
    }

    // UID mismatch: safeguard against stale commands (tag changed, discovery wasn't re-published)
    if (strcmp(uid, spool.spool_id) != 0) {
        Serial.printf("HomeAssistantManager: Rejecting cmd '%s': uid_mismatch expected=%s actual=%s\n",
                      command, uid, spool.spool_id);
        char errPayload[256];
        snprintf(errPayload, sizeof(errPayload),
                 "{\"command\":\"%s\",\"success\":false,\"error\":\"uid_mismatch\","
                 "\"expected\":\"%s\",\"actual\":\"%s\"}",
                 command, uid, spool.spool_id);
        char respTopic[64];
        snprintf(respTopic, sizeof(respTopic), "spoolsense/%s/cmd/response", deviceId_);
        mqttClient.publish(respTopic, errPayload, false);
        return;
    }

    if (strcmp(command, "write_tag") == 0) {
        // Partial tag updates from HA UI: use current tag values as defaults
        // (preserves fields the user didn't modify, so only changed fields are written)
        uint8_t currentMaterial = OPT_MATERIAL_TYPE_PLA;
        uint8_t currentColor[4] = {255, 255, 255, 255};
        char currentManufacturer[64] = {0};
        float currentFullWeight = 1000.0f;
        float currentRemaining = 1000.0f;
        int32_t currentSpoolmanId = -1;

        if (spool.tag_data_valid) {
            // Extract current field values from tag to serve as defaults for partial updates
            opt_get_material_type(&spool.tag_data, &currentMaterial);
            opt_get_primary_color(&spool.tag_data, currentColor);
            opt_get_brand_name(&spool.tag_data, currentManufacturer, sizeof(currentManufacturer));
            opt_get_actual_full_weight(&spool.tag_data, &currentFullWeight);
            float consumed = 0.0f;
            opt_get_consumed_weight(&spool.tag_data, &consumed);
            currentRemaining = currentFullWeight - consumed;
            if (currentRemaining < 0) currentRemaining = 0;
            opt_get_gp_spoolman_id(&spool.tag_data, &currentSpoolmanId);
        }

        AppMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = AppMessageType::HA_WRITE_TAG;
        strncpy(msg.payload.haWriteTag.expected_uid, uid,
                sizeof(msg.payload.haWriteTag.expected_uid) - 1);

        msg.payload.haWriteTag.material_type = currentMaterial;
        if (cmdPayload.has_filament_type) {
            msg.payload.haWriteTag.material_type = materialTypeFromString(cmdPayload.filament_type);
        }

        memcpy(msg.payload.haWriteTag.color, currentColor, sizeof(msg.payload.haWriteTag.color));
        if (cmdPayload.has_color) {
            parseHexColor(cmdPayload.color, msg.payload.haWriteTag.color);
        }

        strncpy(msg.payload.haWriteTag.manufacturer, currentManufacturer,
                sizeof(msg.payload.haWriteTag.manufacturer) - 1);
        if (cmdPayload.has_manufacturer) {
            strncpy(msg.payload.haWriteTag.manufacturer, cmdPayload.manufacturer,
                    sizeof(msg.payload.haWriteTag.manufacturer) - 1);
        }

        msg.payload.haWriteTag.initial_weight_g = currentFullWeight;
        if (cmdPayload.has_initial_weight_g) {
            msg.payload.haWriteTag.initial_weight_g = cmdPayload.initial_weight_g;
        }

        msg.payload.haWriteTag.remaining_g = currentRemaining;
        if (cmdPayload.has_remaining_g) {
            msg.payload.haWriteTag.remaining_g = cmdPayload.remaining_g;
        }

        msg.payload.haWriteTag.spoolman_id = currentSpoolmanId;
        if (cmdPayload.has_spoolman_id) {
            msg.payload.haWriteTag.spoolman_id = cmdPayload.spoolman_id;
        }

        bool queued = ApplicationManager::getInstance().sendMessage(msg, 50);
        if (!queued) {
            Serial.println("HomeAssistantManager: Failed to queue HA write_tag message");
            publishCommandResponse(command, false, "app_queue_full");
            return;
        }
        publishCommandResponse(command, true, nullptr);

    } else if (strcmp(command, "update_remaining") == 0) {
        // Lightweight spool update: just record consumed weight without full tag rewrite
        if (!cmdPayload.has_remaining_g || cmdPayload.remaining_g < 0.0f) {
            Serial.printf("HomeAssistantManager: update_remaining: missing or negative remaining_g in payload\n");
            publishCommandResponse(command, false, "missing_remaining_g");
            return;
        }
        float remainingG = cmdPayload.remaining_g;

        if (!spool.tag_data_valid) {
            Serial.printf("HomeAssistantManager: update_remaining: tag data unavailable for uid=%s\n", uid);
            publishCommandResponse(command, false, "tag_data_unavailable");
            return;
        }

        // Back-calculate consumed weight: consumed = full - remaining
        float fullWeight = 0.0f;
        opt_get_actual_full_weight(&spool.tag_data, &fullWeight);
        float consumed = fullWeight - remainingG;
        if (consumed < 0) consumed = 0;  // remaining can exceed initial if tag was re-wound
        Serial.printf("HomeAssistantManager: update_remaining uid=%s full=%.1f remaining=%.1f consumed=%.1f\n",
                      uid, fullWeight, remainingG, consumed);

        AppMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = AppMessageType::HA_UPDATE_REMAINING;
        strncpy(msg.payload.haUpdateRemaining.expected_uid, uid,
                sizeof(msg.payload.haUpdateRemaining.expected_uid) - 1);
        msg.payload.haUpdateRemaining.consumed_g = consumed;

        bool queued = ApplicationManager::getInstance().sendMessage(msg, 50);
        if (!queued) {
            Serial.println("HomeAssistantManager: Failed to queue HA update_remaining message");
            publishCommandResponse(command, false, "app_queue_full");
            return;
        }
        publishCommandResponse(command, true, nullptr);

    } else {
        publishCommandResponse(command, false, "unknown_command");
    }
}

void HomeAssistantManager::publishCommandResponse(const char* command, bool success, const char* error) {
    char respTopic[64];
    snprintf(respTopic, sizeof(respTopic), "spoolsense/%s/cmd/response", deviceId_);

    char respPayload[128];
    if (success) {
        snprintf(respPayload, sizeof(respPayload),
                 "{\"command\":\"%s\",\"success\":true}", command);
    } else {
        snprintf(respPayload, sizeof(respPayload),
                 "{\"command\":\"%s\",\"success\":false,\"error\":\"%s\"}",
                 command, error ? error : "unknown");
    }
    mqttClient.publish(respTopic, respPayload, false);
}


#endif // !NATIVE_TEST

int HomeAssistantManager::getLastMqttState() const {
#ifndef NATIVE_TEST
    return lastMqttState_;
#else
    return -1;
#endif
}

#ifdef NATIVE_TEST
void HomeAssistantManager::stopTask() {
}

bool HomeAssistantManager::restartAndTestConnection(uint32_t timeoutMs, int* mqttStateOut) {
    (void)timeoutMs;
    if (mqttStateOut != nullptr) *mqttStateOut = -1;
    return false;
}
#endif
