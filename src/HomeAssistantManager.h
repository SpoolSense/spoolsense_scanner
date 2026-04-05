#ifndef HOME_ASSISTANT_MANAGER_H
#define HOME_ASSISTANT_MANAGER_H

#include <cstdint>
#include <atomic>
#include "NFCTypes.h"

#ifndef NATIVE_TEST
  #include <freertos/FreeRTOS.h>
  #include <freertos/queue.h>
  #include <WiFiClient.h>
  #include <PubSubClient.h>
#else
  #include "platform/NativePlatform.h"
#endif

struct HAPublishRequest {
    char topic[96];
    char payload[384];
    bool retained;
};

class HomeAssistantManager {
public:
    static HomeAssistantManager& getInstance();

    bool begin();
    void startTask();
    void stopTask();
    bool restartAndTestConnection(uint32_t timeoutMs, int* mqttStateOut = nullptr);
    bool enqueuePublish(const HAPublishRequest& req);
    bool isConnected() const;
    bool isConfigured() const;
    int getLastMqttState() const;

    // Generate device ID from MAC (last 6 hex chars)
    static void getDeviceId(char* buf, size_t bufSize);

private:
    HomeAssistantManager() = default;
    HomeAssistantManager(const HomeAssistantManager&) = delete;
    HomeAssistantManager& operator=(const HomeAssistantManager&) = delete;

#ifndef NATIVE_TEST
    static void taskFunc(void* param);
    void taskLoop();
    bool reconnect();
    void publishDiscovery();
    void publishCurrentTagState();
    void publishAvailability(const char* state);
    void subscribeCommands();
    static void mqttCallback(char* topic, uint8_t* payload, unsigned int length);
    void handleCommand(const char* topic, const char* payload);
    void publishCommandResponse(const char* command, bool success, const char* error);

    WiFiClient wifiClient;
    PubSubClient mqttClient;
    TaskHandle_t taskHandle = nullptr;
    SemaphoreHandle_t taskControlMutex = nullptr;
    std::atomic<bool> stopRequested_{false};
    volatile int lastMqttState_ = -1;
#endif

    QueueHandle_t publishQueue = nullptr;
    std::atomic<bool> connected_{false};

    // Reconnect backoff
    uint32_t reconnectDelay_ = 1000;
    uint32_t lastReconnectAttempt_ = 0;
    static constexpr uint32_t MAX_RECONNECT_DELAY = 30000;

    static constexpr size_t QUEUE_SIZE = 12;
    static constexpr size_t TASK_STACK_SIZE = 8192;
    static constexpr UBaseType_t TASK_PRIORITY = 2;

    // Device ID cache
    char deviceId_[7] = {0}; // 6 hex chars + null
    CurrentSpoolState spoolScratch_;

    // Discovery dedup — only re-publish when UID changes
    char lastDiscoveryUid_[17] = {0};
};

#endif // HOME_ASSISTANT_MANAGER_H
