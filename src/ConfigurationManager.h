#ifndef CONFIGURATION_MANAGER_H
#define CONFIGURATION_MANAGER_H

#include <cstdint>
#include <cstddef>

// Sanitize hostname in-place: lowercase alphanum + hyphens, no leading/trailing
// hyphens, falls back to "spoolsense" if empty. cap = buffer size including null.
void sanitizeHostname(char* buf, size_t cap);

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0-dev"
#endif
#define DEVICE_VERSION FIRMWARE_VERSION

struct ConfigUpdate {
    char wifi_ssid[64];
    char wifi_pass[64];
    char mqtt_host[128];
    uint16_t mqtt_port;
    char mqtt_user[64];
    char mqtt_pass[64];
    uint8_t spoolman_on;
    char spoolman_url[128];
    uint8_t auto_mode;
    uint8_t lcd_enabled;
    uint8_t led_enabled;
    uint8_t keypad_enabled;
    uint8_t tft_enabled;
    char tft_driver[8];   // "st7789" or "gc9a01"
    // Klipper / Moonraker
    char moonraker_url[128];
    // PrusaLink integration
    uint8_t prusalink_on;
    char prusalink_url[128];
    char prusalink_api_key[64];
    // NFC reader selection
    char nfc_reader[8];  // "pn5180" or "pn532"
    // mDNS / WiFi hostname
    char hostname[33];   // max 32 chars + null
    uint16_t low_spool_threshold_g;  // grams below which LED breathes (default 100)
    uint8_t bambu_dashboard;
    uint8_t wifi_keep_awake;  // disable WiFi modem sleep (better RSSI, more power)
};

class ConfigurationManager {
public:
    static ConfigurationManager& getInstance();

    bool begin();  // Load config from NVS (preferred) or compile-time defaults

    const char* getWiFiSSID() const;
    const char* getWiFiPassword() const;
    const char* getSpoolmanURL() const;
    bool isSpoolmanEnabled() const;
    uint32_t getPollIntervalMs() const;
    uint32_t getLcdTimeoutMs() const;

    // Home Assistant / MQTT configuration
    bool getHAEnabled() const;
    const char* getHAMqttHost() const;
    uint16_t getHAMqttPort() const;
    const char* getHAMqttUser() const;
    const char* getHAMqttPass() const;
    uint8_t getAutomationMode() const;

    // Klipper / Moonraker
    const char* getMoonrakerURL() const;

    // PrusaLink configuration
    bool isPrusaLinkEnabled() const;
    const char* getPrusaLinkURL() const;
    const char* getPrusaLinkAPIKey() const;

    // NFC reader selection (NVS, default "pn5180")
    const char* getNfcReader() const;

    // mDNS / WiFi hostname (NVS, default "spoolsense")
    const char* getHostname() const;

    // Optional hardware features (compile-time default, overridable via NVS)
    bool isLcdEnabled() const;
    bool isLedEnabled() const;
    bool isKeypadEnabled() const;
    bool isTftEnabled() const;
    const char* getTftDriver() const;

    // Low-spool threshold (grams) — LED breathes when remaining weight is at or below this
    uint16_t getLowSpoolThreshold() const;

    bool isBambuDashboardEnabled() const;

    // When true, firmware calls WiFi.setSleep(false) after WiFi.begin so the
    // radio stays fully powered. Trades idle current for better RSSI/latency.
    bool isWifiKeepAwakeEnabled() const;

    // Web config support
    void getCurrentConfig(ConfigUpdate& out) const;
    bool saveToNVS(const ConfigUpdate& update);

private:
    ConfigurationManager() = default;
    ConfigurationManager(const ConfigurationManager&) = delete;
    ConfigurationManager& operator=(const ConfigurationManager&) = delete;

    bool loadFromNVS();   // Try loading config from NVS partition
    void loadFromDeviceConfig();  // Fall back to compile-time defaults

    // In-memory cache
    char _ssid[64];
    char _wifiPass[64];
    char _spoolmanUrl[128];
    bool _spoolmanEnabled;
    uint32_t _pollIntervalMs;
    uint32_t _lcdTimeoutMs;

    // Home Assistant / MQTT config
    bool _haEnabled;
    char _haMqttHost[128];
    uint16_t _haMqttPort;
    char _haMqttUser[64];
    char _haMqttPass[64];
    uint8_t _automationMode;

    // Klipper / Moonraker
    char _moonrakerUrl[128] = {0};

    // PrusaLink config
    bool _prusaLinkEnabled = false;
    char _prusaLinkUrl[128] = {0};
    char _prusaLinkApiKey[64] = {0};

    // NFC reader selection
    char _nfcReader[8] = "pn5180";

    // mDNS / WiFi hostname
    char _hostname[33] = "spoolsense";

    // Optional hardware features
    bool _lcdEnabled = false;
    bool _ledEnabled = false;
    bool _keypadEnabled = false;
    bool _tftEnabled = false;
    char _tftDriver[8] = "st7789";
    uint16_t _lowSpoolThreshold = 100;  // grams
    bool _bambuDashboard = false;
    bool _wifiKeepAwake = false;

    bool _initialized = false;
};

#endif // CONFIGURATION_MANAGER_H
