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

// Runtime-configurable NFC reader pins (#201). Index order is fixed — NVS
// keys, config API fields, and the UI all follow it. PN532 uses the same six
// slots (SS=NSS; no BUSY — that slot is simply unused by the PN532 driver).
enum class NfcPinId : uint8_t { Rst = 0, Nss, Busy, Sck, Mosi, Miso, Count };

// led_pin sentinel: absent in NVS or 0xFF means "use the board default"
// (PIN_STATUS_LED). Any other value is a user GPIO override. (#203)
static constexpr uint8_t LED_PIN_DEFAULT = 0xFF;

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
    char tft_driver[8];   // "st7789", "gc9a01", "ili9341", "ili9488", or "st7796"
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
    // Snapmaker U1 direct-mode integration (extended firmware, Filament Detection: External)
    uint8_t u1_enabled;       // 0 = off, 1 = post to /printer/filament_detect/set on scans
    uint8_t u1_channel;       // 0..3 — toolhead this scanner is bound to (fixed mode)
    uint8_t u1_mode;          // 0 = fixed channel, 1 = stage (pick channel after each scan)
    uint8_t u1_auto_pick;     // stage mode: 1 = assign to the first lane that loads (default on)
    uint8_t led_pin;          // status LED GPIO override; LED_PIN_DEFAULT = board default (#203)
    uint8_t nfc_pins[6];      // NFC reader pin overrides (NfcPinId order); 0xFF = board default (#201)
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

    // Effective status-LED GPIO: user override when set and valid, else PIN_STATUS_LED (#203)
    uint8_t getLedPin() const;
    // Effective NFC reader pin: NVS override when set and valid, else the
    // board default from BoardPins.h. Consumers read these once at begin();
    // changes apply on the post-save reboot. (#201)
    uint8_t getNfcPin(NfcPinId id) const;

    // Low-spool threshold (grams) — LED breathes when remaining weight is at or below this
    uint16_t getLowSpoolThreshold() const;

    bool isBambuDashboardEnabled() const;

    // When true, firmware calls WiFi.setSleep(false) after WiFi.begin so the
    // radio stays fully powered. Trades idle current for better RSSI/latency.
    bool isWifiKeepAwakeEnabled() const;

    // Snapmaker U1 direct-mode (Phase 1 / fixed-channel)
    bool isU1Enabled() const;
    uint8_t getU1Channel() const;
    // Stage mode (#Phase 2): scans are held until the user picks a channel
    bool isU1StageMode() const;
    bool isU1AutoPickEnabled() const;

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

    // Snapmaker U1 direct-mode
    bool _u1Enabled = false;
    uint8_t _u1Channel = 0;
    uint8_t _u1Mode = 0;   // 0 = fixed, 1 = stage
    bool _u1AutoPick = true;  // stage mode: motion-sensor auto-assign

    uint8_t _ledPin = LED_PIN_DEFAULT;  // 0xFF = use board default (PIN_STATUS_LED)
    uint8_t _nfcPins[6] = { LED_PIN_DEFAULT, LED_PIN_DEFAULT, LED_PIN_DEFAULT,
                            LED_PIN_DEFAULT, LED_PIN_DEFAULT, LED_PIN_DEFAULT };

    bool _initialized = false;
};

#endif // CONFIGURATION_MANAGER_H
