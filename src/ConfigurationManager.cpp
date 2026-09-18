#include "ConfigurationManager.h"
#include "UserConfig.h"
#include "DeviceConfig.h"
#include "BoardPins.h"
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
static const char* NVS_KEY_U1_ON         = "u1_on";
static const char* NVS_KEY_U1_CHANNEL    = "u1_channel";
static const char* NVS_KEY_U1_MODE       = "u1_mode";
static const char* NVS_KEY_U1_AUTOPICK   = "u1_autopick";
static const char* NVS_KEY_LED_PIN       = "led_pin";
// NfcPinId order: rst, nss, busy, sck, mosi, miso
static const char* NVS_KEYS_NFC_PIN[6] = {
    "pin_nfc_rst", "pin_nfc_nss", "pin_nfc_busy",
    "pin_nfc_sck", "pin_nfc_mosi", "pin_nfc_miso",
};
static const int16_t NFC_PIN_DEFAULTS[6] = {
    PIN_PN5180_RST, PIN_PN5180_NSS, PIN_PN5180_BUSY,
    PIN_PN5180_SCK, PIN_PN5180_MOSI, PIN_PN5180_MISO,
};
static const char* NFC_PIN_NAMES[6] = { "RST", "NSS", "BUSY", "SCK", "MOSI", "MISO" };

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

// Board-level GPIO validity — shared by every runtime pin override (#201,
// #203). Blocklists are selected by the same BOARD_* flags as BoardPins.h.
static bool boardPinUsable(uint8_t pin) {
    bool valid;
#if defined(BOARD_SEEED_XIAO_ESP32_C6)
    // XIAO ESP32-C6: only the D0-D10 pads are routed out. GPIO3/14 are the
    // internal antenna switch (3 = RF enable, active low; 14 = int/ext select),
    // GPIO15 is the onboard LED on a strap pin — none are safe as overrides.
    switch (pin) {
        case 0: case 1: case 2: case 16: case 17: case 18:
        case 19: case 20: case 21: case 22: case 23:
            valid = true; break;
        default:
            valid = false; break;
    }
#elif defined(BOARD_ESP32_C6)
    // C6 DevKitC-1: reject USB (12/13) and straps (4/5/9/15). GPIO8 is the
    // onboard WS2812 and is safe once reset-time strap sampling has completed.
    switch (pin) {
        case 0: case 1: case 2: case 3: case 6: case 7: case 8:
        case 10: case 11: case 16: case 17: case 18: case 19:
        case 20: case 21: case 22: case 23:
            valid = true; break;
        default:
            valid = false; break;
    }
#elif defined(BOARD_ESP32_C5)
    // C5 DevKitC-1 v1.2: reject USB (13/14), straps (2/3/7/25/26/28), and
    // GPIO15 because it is not exposed on every module/board revision.
    switch (pin) {
        case 0: case 1: case 4: case 5: case 6: case 8: case 9:
        case 10: case 11: case 12: case 23: case 24: case 27:
            valid = true; break;
        default:
            valid = false; break;
    }
#elif defined(BOARD_ESP32_C3)
    // C3 exposes only GPIO 0-21; flash (12-17), USB-serial (18-19) and straps
    // (2,8,9) are unusable, so validate against the known-good allowlist.
    switch (pin) {
        case 0: case 1: case 3: case 4: case 5: case 6: case 7:
        case 10: case 20: case 21:
            valid = true; break;
        default:
            valid = false; break;
    }
#elif defined(BOARD_ESP32_S3)
    // S3: reject straps/USB-JTAG (0,3,19,20), SPI flash (26-32), straps (45,46).
    valid = !(pin == 0 || pin == 3 || pin == 19 || pin == 20 ||
              (pin >= 26 && pin <= 32) || pin == 45 || pin == 46 || pin > 48);
  #if defined(BOARD_S3_DEVKITC)
    // N16R8 octal PSRAM additionally claims GPIO 33-37.
    if (pin >= 33 && pin <= 37) valid = false;
  #endif
#else
    // ESP32 WROOM: reject straps (0,2,12,15), SPI flash (6-11), input-only (34-39).
    valid = !(pin == 0 || pin == 2 || (pin >= 6 && pin <= 11) || pin == 12 ||
              pin == 15 || (pin >= 34 && pin <= 39) || pin > 39);
#endif
    return valid;
}

// Reject GPIOs that can't safely drive the status LED on the CURRENT board and
// fall back to the default sentinel (LED_PIN_DEFAULT -> PIN_STATUS_LED). Blocklists
// are selected by the same BOARD_* flags as BoardPins.h. Used as an NVS boundary
// guard on both read (loadFromNVS) and write (saveToNVS), same as sanitizeHostname.
static uint8_t sanitizeLedPin(uint8_t pin) {
    if (pin == LED_PIN_DEFAULT) {
        return LED_PIN_DEFAULT;  // no override requested
    }

    // NFC reader pins are always in use — driving NeoPixel on the SPI bus or a
    // control line breaks scanning outright. PN532 and RC522 alias the PN5180
    // pins on every board, so this set covers all supported readers.
    static const int16_t nfcPins[] = {
        PIN_PN5180_NSS, PIN_PN5180_SCK, PIN_PN5180_MISO, PIN_PN5180_MOSI,
        PIN_PN5180_RST, PIN_PN5180_BUSY, PIN_PN5180_GPIO, PIN_PN5180_IRQ,
        PIN_PN5180_AUX,
    };
    for (size_t i = 0; i < sizeof(nfcPins) / sizeof(nfcPins[0]); i++) {
        if (nfcPins[i] >= 0 && pin == (uint8_t)nfcPins[i]) {
            Serial.printf("ConfigurationManager: led_pin %u is an NFC reader pin, using default GPIO %u\n",
                          (unsigned)pin, (unsigned)PIN_STATUS_LED);
            return LED_PIN_DEFAULT;
        }
    }

    if (!boardPinUsable(pin)) {
        Serial.printf("ConfigurationManager: led_pin %u invalid for this board, using default GPIO %u\n",
                      (unsigned)pin, (unsigned)PIN_STATUS_LED);
        return LED_PIN_DEFAULT;
    }
    return pin;
}

// Feature-owned pins (LCD, TFT, keypad) are only off-limits when that feature
// is enabled — on the WROOM the TFT DC pin doubles as the default status LED,
// so unconditional blocking would reject the board default itself. Called at
// save time where the update's own feature flags are authoritative.
static uint8_t rejectFeaturePins(uint8_t pin, bool lcdOn, bool tftOn, bool keypadOn) {
    if (pin == LED_PIN_DEFAULT) return pin;

    struct FeaturePin { int16_t pin; bool active; const char* what; };
    const FeaturePin featurePins[] = {
        { PIN_LCD_SDA,     lcdOn,    "LCD" },
        { PIN_LCD_SCL,     lcdOn,    "LCD" },
        { PIN_TFT_MOSI,    tftOn,    "TFT" },
        { PIN_TFT_SCLK,    tftOn,    "TFT" },
        { PIN_TFT_CS,      tftOn,    "TFT" },
        { PIN_TFT_DC,      tftOn,    "TFT" },
        { PIN_TFT_RST,     tftOn,    "TFT" },
        { PIN_TFT_BL,      tftOn,    "TFT" },
        { PIN_KEYPAD_ROW1, keypadOn, "keypad" },
        { PIN_KEYPAD_ROW2, keypadOn, "keypad" },
        { PIN_KEYPAD_ROW3, keypadOn, "keypad" },
        { PIN_KEYPAD_ROW4, keypadOn, "keypad" },
        { PIN_KEYPAD_COL1, keypadOn, "keypad" },
        { PIN_KEYPAD_COL2, keypadOn, "keypad" },
        { PIN_KEYPAD_COL3, keypadOn, "keypad" },
    };
    for (size_t i = 0; i < sizeof(featurePins) / sizeof(featurePins[0]); i++) {
        const FeaturePin& f = featurePins[i];
        if (f.active && f.pin >= 0 && pin == (uint8_t)f.pin) {
            Serial.printf("ConfigurationManager: led_pin %u is in use by the enabled %s, using default GPIO %u\n",
                          (unsigned)pin, f.what, (unsigned)PIN_STATUS_LED);
            return LED_PIN_DEFAULT;
        }
    }
    return pin;
}

// Validate an NFC pin override SET (#201). Per-pin: board-usable or revert to
// sentinel. Then set-wide: the six EFFECTIVE pins (override-or-default) must
// be distinct and must not collide with enabled-feature pins or the status
// LED; any collision reverts the whole set — a half-applied remap is a wiring
// short waiting to happen, and defaults are always safe.
static void sanitizeNfcPinSet(uint8_t pins[6], bool lcdOn, bool tftOn, bool keypadOn,
                              uint8_t effectiveLedPin) {
    for (int i = 0; i < 6; i++) {
        if (pins[i] == LED_PIN_DEFAULT) continue;
        if (!boardPinUsable(pins[i])) {
            Serial.printf("ConfigurationManager: NFC %s pin %u invalid for this board, using default\n",
                          NFC_PIN_NAMES[i], (unsigned)pins[i]);
            pins[i] = LED_PIN_DEFAULT;
        }
    }

    int16_t effective[6];
    for (int i = 0; i < 6; i++) {
        effective[i] = (pins[i] == LED_PIN_DEFAULT) ? NFC_PIN_DEFAULTS[i] : (int16_t)pins[i];
    }

    bool conflict = false;
#if defined(BOARD_SHARED_SPI)
    // LovyanGFX performs the single Arduino SPI.begin() for the whole bus using
    // the board's compile-time pins (SCK/MOSI, and MISO routed to the NFC MISO).
    // Arduino's SPIClass::begin() ignores a later begin(), so while TFT is
    // enabled a runtime NFC remap of any bus line cannot re-route it — reject
    // remaps that would diverge from the physical shared bus.
    if (tftOn && (effective[(int)NfcPinId::Sck]  != PIN_TFT_SCLK ||
                  effective[(int)NfcPinId::Mosi] != PIN_TFT_MOSI ||
                  effective[(int)NfcPinId::Miso] != PIN_PN5180_MISO)) {
        Serial.println("ConfigurationManager: shared-SPI NFC SCK/MOSI/MISO must match the TFT bus");
        conflict = true;
    }
#endif
    for (int i = 0; i < 6 && !conflict; i++) {
        for (int j = i + 1; j < 6; j++) {
            if (effective[i] >= 0 && effective[i] == effective[j]) {
                Serial.printf("ConfigurationManager: NFC pin conflict — %s and %s both GPIO %d\n",
                              NFC_PIN_NAMES[i], NFC_PIN_NAMES[j], (int)effective[i]);
                conflict = true;
                break;
            }
        }
    }

    struct OtherPin { int16_t pin; bool active; const char* what; };
    const OtherPin others[] = {
        { PIN_LCD_SDA,     lcdOn,    "LCD" },
        { PIN_LCD_SCL,     lcdOn,    "LCD" },
        { PIN_TFT_MOSI,    tftOn,    "TFT" },
        { PIN_TFT_SCLK,    tftOn,    "TFT" },
        { PIN_TFT_CS,      tftOn,    "TFT" },
        { PIN_TFT_DC,      tftOn,    "TFT" },
        { PIN_TFT_RST,     tftOn,    "TFT" },
        { PIN_TFT_BL,      tftOn,    "TFT" },
        { PIN_KEYPAD_ROW1, keypadOn, "keypad" },
        { PIN_KEYPAD_ROW2, keypadOn, "keypad" },
        { PIN_KEYPAD_ROW3, keypadOn, "keypad" },
        { PIN_KEYPAD_ROW4, keypadOn, "keypad" },
        { PIN_KEYPAD_COL1, keypadOn, "keypad" },
        { PIN_KEYPAD_COL2, keypadOn, "keypad" },
        { PIN_KEYPAD_COL3, keypadOn, "keypad" },
        { (int16_t)effectiveLedPin, true, "status LED" },
    };
    for (int i = 0; i < 6 && !conflict; i++) {
        if (pins[i] == LED_PIN_DEFAULT) continue;  // defaults pre-date the features; only police overrides
        for (const auto& o : others) {
            if (o.active && o.pin >= 0 && effective[i] == o.pin) {
                Serial.printf("ConfigurationManager: NFC %s pin %d is in use by the enabled %s\n",
                              NFC_PIN_NAMES[i], (int)effective[i], o.what);
                conflict = true;
                break;
            }
        }
    }

    if (conflict) {
        Serial.println("ConfigurationManager: reverting ALL NFC pin overrides to board defaults");
        for (int i = 0; i < 6; i++) pins[i] = LED_PIN_DEFAULT;
    }
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

#if defined(BOARD_NO_TFT)
    _tftEnabled = false;
#endif
#if defined(BOARD_NO_KEYPAD)
    _keypadEnabled = false;
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
#if defined(BOARD_NO_TFT)
    // C5/C6 dedicate their only general-purpose SPI host to NFC.
    _tftEnabled = false;
#endif
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
    if (prefs.isKey(NVS_KEY_U1_ON)) {
        _u1Enabled = prefs.getBool(NVS_KEY_U1_ON, false);
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_U1_CHANNEL)) {
        uint8_t ch = prefs.getUChar(NVS_KEY_U1_CHANNEL, 0);
        _u1Channel = (ch <= 3) ? ch : 0;  // clamp invalid values from NVS
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_U1_MODE)) {
        uint8_t mode = prefs.getUChar(NVS_KEY_U1_MODE, 0);
        _u1Mode = (mode <= 1) ? mode : 0;
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_U1_AUTOPICK)) {
        _u1AutoPick = prefs.getBool(NVS_KEY_U1_AUTOPICK, true);
        anyOverride = true;
    }
    if (prefs.isKey(NVS_KEY_LED_PIN)) {
        _ledPin = sanitizeLedPin(prefs.getUChar(NVS_KEY_LED_PIN, LED_PIN_DEFAULT));
        anyOverride = true;
    }

    {
        bool anyNfcPin = false;
        for (int i = 0; i < 6; i++) {
            if (prefs.isKey(NVS_KEYS_NFC_PIN[i])) {
                _nfcPins[i] = prefs.getUChar(NVS_KEYS_NFC_PIN[i], LED_PIN_DEFAULT);
                anyNfcPin = true;
            }
        }
        if (anyNfcPin) {
            sanitizeNfcPinSet(_nfcPins, _lcdEnabled, _tftEnabled, _keypadEnabled, getLedPin());
            anyOverride = true;
        }
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

bool ConfigurationManager::isU1Enabled() const {
    return _u1Enabled;
}

uint8_t ConfigurationManager::getU1Channel() const {
    return _u1Channel;
}

bool ConfigurationManager::isU1StageMode() const {
    return _u1Mode == 1;
}

bool ConfigurationManager::isU1AutoPickEnabled() const {
    return _u1AutoPick;
}

uint8_t ConfigurationManager::getLedPin() const {
    return (_ledPin == LED_PIN_DEFAULT) ? PIN_STATUS_LED : _ledPin;
}

uint8_t ConfigurationManager::getNfcPin(NfcPinId id) const {
    uint8_t i = (uint8_t)id;
    if (i >= 6) return 0;
    if (_nfcPins[i] != LED_PIN_DEFAULT) return _nfcPins[i];
    return (uint8_t)NFC_PIN_DEFAULTS[i];
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
    out.u1_enabled = _u1Enabled ? 1 : 0;
    out.u1_channel = _u1Channel;
    out.u1_mode = _u1Mode;
    out.u1_auto_pick = _u1AutoPick ? 1 : 0;
    out.led_pin = _ledPin;
    memcpy(out.nfc_pins, _nfcPins, sizeof(out.nfc_pins));
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
#if defined(BOARD_NO_KEYPAD)
    prefs.putUChar(NVS_KEY_KEYPAD_ON, 0);
#else
    prefs.putUChar(NVS_KEY_KEYPAD_ON, update.keypad_enabled);
#endif
#if defined(BOARD_NO_TFT)
    prefs.putUChar(NVS_KEY_TFT_ON, 0);
#else
    prefs.putUChar(NVS_KEY_TFT_ON, update.tft_enabled);
#endif
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
    prefs.putBool(NVS_KEY_U1_ON, update.u1_enabled != 0);
    prefs.putUChar(NVS_KEY_U1_CHANNEL, (update.u1_channel <= 3) ? update.u1_channel : 0);
    prefs.putUChar(NVS_KEY_U1_MODE, (update.u1_mode <= 1) ? update.u1_mode : 0);
    prefs.putBool(NVS_KEY_U1_AUTOPICK, update.u1_auto_pick != 0);
#if defined(BOARD_NO_TFT)
    constexpr bool tftOn = false;
#else
    const bool tftOn = update.tft_enabled != 0;
#endif
#if defined(BOARD_NO_KEYPAD)
    constexpr bool keypadOn = false;
#else
    const bool keypadOn = update.keypad_enabled != 0;
#endif
    uint8_t savedLedPin;
    {
        uint8_t ledPin = sanitizeLedPin(update.led_pin);
        ledPin = rejectFeaturePins(ledPin, update.lcd_enabled != 0,
                                   tftOn, keypadOn);
        prefs.putUChar(NVS_KEY_LED_PIN, ledPin);
        savedLedPin = ledPin;
    }
    {
        uint8_t pins[6];
        memcpy(pins, update.nfc_pins, sizeof(pins));
        uint8_t effLed = (savedLedPin == LED_PIN_DEFAULT) ? PIN_STATUS_LED : savedLedPin;
        sanitizeNfcPinSet(pins, update.lcd_enabled != 0, tftOn, keypadOn, effLed);
        for (int i = 0; i < 6; i++) {
            prefs.putUChar(NVS_KEYS_NFC_PIN[i], pins[i]);
        }
    }

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
