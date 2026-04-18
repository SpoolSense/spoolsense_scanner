#include "ConfigurationManager.h"
#include "UserConfig.h"
#include "DeviceConfig.h"
#include <cstring>
#include <Arduino.h>

#ifndef NATIVE_TEST
#include <Preferences.h>
#endif

// ConfigurationManager — Singleton that loads compile-time and runtime (NVS) configuration.
// Boot sequence: loadFromDeviceConfig() first, then loadFromNVS() overrides per-key where present.
// NVS namespace and key names — must match spoolsense-installer nvs_keys.csv
static const char* NVS_NAMESPACE = "spoolsense";
static const char* NVS_KEY_WIFI_SSID      = "wifi_ssid";
static const char* NVS_KEY_WIFI_PASS      = "wifi_pass";
static const char* NVS_KEY_MQTT_HOST      = "mqtt_host";
static const char* NVS_KEY_MQTT_PORT      = "mqtt_port";
static const char* NVS_KEY_MQTT_USER      = "mqtt_user";
static const char* NVS_KEY_MQTT_PASS      = "mqtt_pass";
static const char* NVS_KEY_MQTT_PREFIX    = "mqtt_prefix";
static const char* NVS_KEY_SPOOLMAN_ON    = "spoolman_on";
static const char* NVS_KEY_SPOOLMAN_URL   = "spoolman_url";
static const char* NVS_KEY_AUTO_MODE      = "auto_mode";
static const char* NVS_KEY_LCD_ON         = "lcd_on";
static const char* NVS_KEY_LED_ON         = "led_on";
static const char* NVS_KEY_KEYPAD_ON      = "keypad_on";
static const char* NVS_KEY_TFT_ON         = "tft_on";
static const char* NVS_KEY_TFT_DRIVER     = "tft_driver";
static const char* NVS_KEY_MOONRAKER_URL  = "moonraker_url";
static const char* NVS_KEY_PRUSALINK_ON   = "prusalink_on";
static const char* NVS_KEY_PRUSALINK_URL  = "prusalink_url";
static const char* NVS_KEY_PRUSALINK_KEY  = "prusalink_key";
static const char* NVS_KEY_NFC_READER    = "nfc_reader";
static const char* NVS_KEY_HOSTNAME      = "hostname";
static const char* NVS_KEY_LOW_SPOOL     = "low_spool_g";
static const char* NVS_KEY_BAMBU_DASH    = "bambu_dash";
static const char* NVS_KEY_WIFI_AWAKE    = "wifi_awake";

// Sanitize hostname: enforce mDNS naming constraints (lowercase alphanum + hyphens,
// no leading/trailing hyphens) and reject empty strings to avoid boot-time errors.
// Used at NVS read/write boundaries and web config API to prevent invalid hostname propagation.
void sanitizeHostname(char* buf, size_t cap) {
    char out[33] = {0};
    size_t n = 0;
    for (size_t i = 0; buf[i] && n < 32; i++) {
        char c = buf[i];
        if (c >= 'A' && c <= 'Z') c = c + 32;  // normalize to lowercase
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
            out[n++] = c;
        }
    }
    while (n > 0 && out[0] == '-') { memmove(out, out + 1, n); n--; }
    while (n > 0 && out[n - 1] == '-') { out[--n] = '\0'; }
    if (n == 0) {
        strncpy(out, "spoolsense", sizeof(out) - 1);  // fallback for empty hostname
    }
    strncpy(buf, out, cap - 1);
    buf[cap - 1] = '\0';
}

ConfigurationManager& ConfigurationManager::getInstance() {
    static ConfigurationManager instance;
    return instance;
}

bool ConfigurationManager::begin() {
    if (_initialized) {
        return true;
    }

    // Layered config: compile-time defaults first, then NVS overrides per-key
    // (allows partial NVS config without losing compile-time settings)
    loadFromDeviceConfig();

#ifndef NATIVE_TEST
    if (loadFromNVS()) {
        Serial.println("ConfigurationManager: NVS config found, overrides applied");
    } else {
        Serial.println("ConfigurationManager: No NVS config, using compile-time defaults");
    }
#endif

    _initialized = true;
    return true;
}

void ConfigurationManager::loadFromDeviceConfig() {
    const DeviceConfig& cfg = getDeviceConfig();

    strncpy(_ssid, cfg.wifi.ssid, sizeof(_ssid) - 1);
    _ssid[sizeof(_ssid) - 1] = '\0';

    strncpy(_wifiPass, cfg.wifi.password, sizeof(_wifiPass) - 1);
    _wifiPass[sizeof(_wifiPass) - 1] = '\0';

    _spoolmanEnabled = cfg.spoolman.enabled;
    if (_spoolmanEnabled) {
        strncpy(_spoolmanUrl, cfg.spoolman.base_url, sizeof(_spoolmanUrl) - 1);
    } else {
        _spoolmanUrl[0] = '\0';
    }
    _spoolmanUrl[sizeof(_spoolmanUrl) - 1] = '\0';

    _pollIntervalMs = 10000;
    _lcdTimeoutMs = 15 * 60 * 1000;

    // Home Assistant is only enabled if broker address is non-empty (user actively configured)
    const bool haConfigured = cfg.mqtt.host != nullptr && cfg.mqtt.host[0] != '\0';
    _haEnabled = haConfigured;

    if (haConfigured) {
        strncpy(_haMqttHost, cfg.mqtt.host, sizeof(_haMqttHost) - 1);
    } else {
        _haMqttHost[0] = '\0';
    }
    _haMqttHost[sizeof(_haMqttHost) - 1] = '\0';

    _haMqttPort = static_cast<uint16_t>(cfg.mqtt.port);

    strncpy(_haMqttUser, cfg.mqtt.username, sizeof(_haMqttUser) - 1);
    _haMqttUser[sizeof(_haMqttUser) - 1] = '\0';

    strncpy(_haMqttPass, cfg.mqtt.password, sizeof(_haMqttPass) - 1);
    _haMqttPass[sizeof(_haMqttPass) - 1] = '\0';

    _automationMode = cfg.automation_mode;

    // Moonraker — not in DeviceConfig, configured via NVS/web UI
    _moonrakerUrl[0] = '\0';

    // PrusaLink defaults — not in DeviceConfig, disabled by default
    _prusaLinkEnabled = false;
    _prusaLinkUrl[0] = '\0';
    _prusaLinkApiKey[0] = '\0';

    // NFC reader default
    strncpy(_nfcReader, "pn5180", sizeof(_nfcReader) - 1);

    // Hostname default
    strncpy(_hostname, "spoolsense", sizeof(_hostname) - 1);
    _hostname[sizeof(_hostname) - 1] = '\0';

    // Optional hardware feature defaults from compile-time flags
    _lcdEnabled = cfg.peripherals.lcd_enabled;
    _ledEnabled = cfg.peripherals.status_led_enabled;
    _keypadEnabled = cfg.peripherals.keypad_enabled;
}

#ifndef NATIVE_TEST
bool ConfigurationManager::loadFromNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {  // read-only
        return false;
    }

    // Check if NVS has any SpoolSense config at all
    if (!prefs.isKey(NVS_KEY_WIFI_SSID)) {
        prefs.end();
        return false;
    }

    bool anyOverride = false;

    // Per-key loading: only override field if the key exists in NVS
    // (preserves compile-time defaults for keys not yet saved to NVS)
    if (prefs.isKey(NVS_KEY_WIFI_SSID)) {
        prefs.getString(NVS_KEY_WIFI_SSID, _ssid, sizeof(_ssid));
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_WIFI_PASS)) {
        prefs.getString(NVS_KEY_WIFI_PASS, _wifiPass, sizeof(_wifiPass));
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_MQTT_HOST)) {
        prefs.getString(NVS_KEY_MQTT_HOST, _haMqttHost, sizeof(_haMqttHost));
        _haEnabled = (_haMqttHost[0] != '\0');
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_MQTT_PORT)) {
        _haMqttPort = prefs.getUShort(NVS_KEY_MQTT_PORT, _haMqttPort);
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_MQTT_USER)) {
        prefs.getString(NVS_KEY_MQTT_USER, _haMqttUser, sizeof(_haMqttUser));
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_MQTT_PASS)) {
        prefs.getString(NVS_KEY_MQTT_PASS, _haMqttPass, sizeof(_haMqttPass));
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_SPOOLMAN_ON)) {
        _spoolmanEnabled = prefs.getBool(NVS_KEY_SPOOLMAN_ON, _spoolmanEnabled);
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_SPOOLMAN_URL)) {
        prefs.getString(NVS_KEY_SPOOLMAN_URL, _spoolmanUrl, sizeof(_spoolmanUrl));
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_AUTO_MODE)) {
        _automationMode = prefs.getUChar(NVS_KEY_AUTO_MODE, _automationMode);
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_PRUSALINK_ON)) {
        _prusaLinkEnabled = prefs.getBool(NVS_KEY_PRUSALINK_ON, false);
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_PRUSALINK_URL)) {
        prefs.getString(NVS_KEY_PRUSALINK_URL, _prusaLinkUrl, sizeof(_prusaLinkUrl));
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_PRUSALINK_KEY)) {
        prefs.getString(NVS_KEY_PRUSALINK_KEY, _prusaLinkApiKey, sizeof(_prusaLinkApiKey));
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_LCD_ON)) {
        _lcdEnabled = prefs.getUChar(NVS_KEY_LCD_ON, _lcdEnabled ? 1 : 0) != 0;
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_LED_ON)) {
        _ledEnabled = prefs.getUChar(NVS_KEY_LED_ON, _ledEnabled ? 1 : 0) != 0;
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_MOONRAKER_URL)) {
        prefs.getString(NVS_KEY_MOONRAKER_URL, _moonrakerUrl, sizeof(_moonrakerUrl));
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_KEYPAD_ON)) {
        _keypadEnabled = prefs.getUChar(NVS_KEY_KEYPAD_ON, _keypadEnabled ? 1 : 0) != 0;
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_TFT_ON)) {
        _tftEnabled = prefs.getUChar(NVS_KEY_TFT_ON, _tftEnabled ? 1 : 0) != 0;
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_TFT_DRIVER)) {
        prefs.getString(NVS_KEY_TFT_DRIVER, _tftDriver, sizeof(_tftDriver));
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_NFC_READER)) {
        prefs.getString(NVS_KEY_NFC_READER, _nfcReader, sizeof(_nfcReader));
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_HOSTNAME)) {
        prefs.getString(NVS_KEY_HOSTNAME, _hostname, sizeof(_hostname));
        sanitizeHostname(_hostname, sizeof(_hostname));
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_LOW_SPOOL)) {
        _lowSpoolThreshold = prefs.getUShort(NVS_KEY_LOW_SPOOL, 100);
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_BAMBU_DASH)) {
        _bambuDashboard = prefs.getBool(NVS_KEY_BAMBU_DASH, false);
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_WIFI_AWAKE)) {
        _wifiKeepAwake = prefs.getBool(NVS_KEY_WIFI_AWAKE, false);
        anyOverride = true;
    }

    prefs.end();
    return anyOverride;
}
#endif

const char* ConfigurationManager::getWiFiSSID() const {
    return _ssid;
}

const char* ConfigurationManager::getWiFiPassword() const {
    return _wifiPass;
}

const char* ConfigurationManager::getSpoolmanURL() const {
    return _spoolmanUrl;
}

bool ConfigurationManager::isSpoolmanEnabled() const {
    return _spoolmanEnabled;
}

uint32_t ConfigurationManager::getPollIntervalMs() const {
    return _pollIntervalMs;
}

uint32_t ConfigurationManager::getLcdTimeoutMs() const {
    return _lcdTimeoutMs;
}

bool ConfigurationManager::getHAEnabled() const {
    return _haEnabled;
}

const char* ConfigurationManager::getHAMqttHost() const {
    return _haMqttHost;
}

uint16_t ConfigurationManager::getHAMqttPort() const {
    return _haMqttPort;
}

const char* ConfigurationManager::getHAMqttUser() const {
    return _haMqttUser;
}

const char* ConfigurationManager::getHAMqttPass() const {
    return _haMqttPass;
}

uint8_t ConfigurationManager::getAutomationMode() const {
    return _automationMode;
}

bool ConfigurationManager::isPrusaLinkEnabled() const {
    return _prusaLinkEnabled && _prusaLinkUrl[0] != '\0' && _prusaLinkApiKey[0] != '\0';
}

const char* ConfigurationManager::getPrusaLinkURL() const {
    return _prusaLinkUrl;
}

const char* ConfigurationManager::getPrusaLinkAPIKey() const {
    return _prusaLinkApiKey;
}

bool ConfigurationManager::isLcdEnabled() const {
    return _lcdEnabled;
}

bool ConfigurationManager::isLedEnabled() const {
    return _ledEnabled;
}

bool ConfigurationManager::isKeypadEnabled() const {
    return _keypadEnabled;
}

bool ConfigurationManager::isTftEnabled() const {
    return _tftEnabled;
}

const char* ConfigurationManager::getTftDriver() const {
    return _tftDriver;
}

const char* ConfigurationManager::getMoonrakerURL() const {
    return _moonrakerUrl;
}

const char* ConfigurationManager::getNfcReader() const {
    return _nfcReader;
}

const char* ConfigurationManager::getHostname() const {
    return _hostname;
}

uint16_t ConfigurationManager::getLowSpoolThreshold() const {
    return _lowSpoolThreshold;
}

bool ConfigurationManager::isBambuDashboardEnabled() const {
    return _bambuDashboard;
}

bool ConfigurationManager::isWifiKeepAwakeEnabled() const {
    return _wifiKeepAwake;
}

void ConfigurationManager::getCurrentConfig(ConfigUpdate& out) const {
    memset(&out, 0, sizeof(out));
    strncpy(out.wifi_ssid, _ssid, sizeof(out.wifi_ssid) - 1);
    strncpy(out.wifi_pass, _wifiPass, sizeof(out.wifi_pass) - 1);
    strncpy(out.mqtt_host, _haMqttHost, sizeof(out.mqtt_host) - 1);
    out.mqtt_port = _haMqttPort;
    strncpy(out.mqtt_user, _haMqttUser, sizeof(out.mqtt_user) - 1);
    strncpy(out.mqtt_pass, _haMqttPass, sizeof(out.mqtt_pass) - 1);
    out.spoolman_on = _spoolmanEnabled ? 1 : 0;
    strncpy(out.spoolman_url, _spoolmanUrl, sizeof(out.spoolman_url) - 1);
    out.auto_mode = _automationMode;
    out.prusalink_on = _prusaLinkEnabled ? 1 : 0;
    strncpy(out.prusalink_url, _prusaLinkUrl, sizeof(out.prusalink_url) - 1);
    strncpy(out.prusalink_api_key, _prusaLinkApiKey, sizeof(out.prusalink_api_key) - 1);
    out.lcd_enabled = _lcdEnabled ? 1 : 0;
    out.led_enabled = _ledEnabled ? 1 : 0;
    out.keypad_enabled = _keypadEnabled ? 1 : 0;
    out.tft_enabled = _tftEnabled ? 1 : 0;
    strncpy(out.tft_driver, _tftDriver, sizeof(out.tft_driver) - 1);
    strncpy(out.moonraker_url, _moonrakerUrl, sizeof(out.moonraker_url) - 1);
    strncpy(out.nfc_reader, _nfcReader, sizeof(out.nfc_reader) - 1);
    strncpy(out.hostname, _hostname, sizeof(out.hostname) - 1);
    out.low_spool_threshold_g = _lowSpoolThreshold;
    out.bambu_dashboard = _bambuDashboard ? 1 : 0;
    out.wifi_keep_awake = _wifiKeepAwake ? 1 : 0;
}

#ifndef NATIVE_TEST
bool ConfigurationManager::saveToNVS(const ConfigUpdate& update) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {  // read-write
        Serial.println("ConfigurationManager: Failed to open NVS for writing");
        return false;
    }

    prefs.putString(NVS_KEY_WIFI_SSID, update.wifi_ssid);
    // Skip empty password (user didn't intend to change it via web UI)
    if (update.wifi_pass[0] != '\0') {
        prefs.putString(NVS_KEY_WIFI_PASS, update.wifi_pass);
    }
    prefs.putString(NVS_KEY_MQTT_HOST, update.mqtt_host);
    prefs.putUShort(NVS_KEY_MQTT_PORT, update.mqtt_port);
    prefs.putString(NVS_KEY_MQTT_USER, update.mqtt_user);
    // Skip empty MQTT password for same reason as WiFi password
    if (update.mqtt_pass[0] != '\0') {
        prefs.putString(NVS_KEY_MQTT_PASS, update.mqtt_pass);
    }
    prefs.putBool(NVS_KEY_SPOOLMAN_ON, update.spoolman_on != 0);
    prefs.putString(NVS_KEY_SPOOLMAN_URL, update.spoolman_url);
    prefs.putUChar(NVS_KEY_AUTO_MODE, update.auto_mode);
    prefs.putUChar(NVS_KEY_LCD_ON, update.lcd_enabled);
    prefs.putUChar(NVS_KEY_LED_ON, update.led_enabled);
    prefs.putUChar(NVS_KEY_KEYPAD_ON, update.keypad_enabled);
    prefs.putUChar(NVS_KEY_TFT_ON, update.tft_enabled);
    prefs.putString(NVS_KEY_TFT_DRIVER, update.tft_driver);
    prefs.putString(NVS_KEY_MOONRAKER_URL, update.moonraker_url);
    prefs.putBool(NVS_KEY_PRUSALINK_ON, update.prusalink_on != 0);
    prefs.putString(NVS_KEY_PRUSALINK_URL, update.prusalink_url);
    // Skip empty API key for same reason as passwords
    if (update.prusalink_api_key[0] != '\0') {
        prefs.putString(NVS_KEY_PRUSALINK_KEY, update.prusalink_api_key);
    }
    prefs.putString(NVS_KEY_NFC_READER, update.nfc_reader);
    char sanitizedHostname[33] = {0};
    strncpy(sanitizedHostname, update.hostname, sizeof(sanitizedHostname) - 1);
    sanitizeHostname(sanitizedHostname, sizeof(sanitizedHostname));  // enforce mDNS constraints before NVS write
    prefs.putString(NVS_KEY_HOSTNAME, sanitizedHostname);
    prefs.putUShort(NVS_KEY_LOW_SPOOL, update.low_spool_threshold_g);
    prefs.putBool(NVS_KEY_BAMBU_DASH, update.bambu_dashboard != 0);
    prefs.putBool(NVS_KEY_WIFI_AWAKE, update.wifi_keep_awake != 0);

    // Invalidate Spoolman enrichment cache on config change to force re-fetch
    // (config change could invalidate cached spool lookups)
    prefs.remove("sp_fields_v");

    prefs.end();
    Serial.println("ConfigurationManager: Config saved to NVS");
    return true;
}
#else
bool ConfigurationManager::saveToNVS(const ConfigUpdate&) {
    return true;  // No-op in native tests
}
#endif
