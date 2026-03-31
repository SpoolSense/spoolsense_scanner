#include "WebServerManager.h"

#ifndef NATIVE_TEST

#include <Arduino.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "LandingHTML.h"
#include "OpenPrintTagWriterHTML.h"
#include "ReaderHTML.h"
#include "TigerTagWriterHTML.h"
#include "OpenTag3DWriterHTML.h"
#include "SharedCSS.h"
#include "SharedJS.h"
#include "ConfigHTML.h"
#include "TroubleshootingHTML.h"
#include "UIDRegistrationHTML.h"
#include "OpenPrintTagLogo.h"
#include "TigerTagLogo.h"
#include "OpenTag3DLogo.h"
#include "UpdateHTML.h"
#include "ConfigurationManager.h"
#include "NFCManager.h"
#include "NFCTypes.h"
#include "NFCWriteTypes.h"
#include "ApplicationManager.h"
#include "ConversionUtils.h"
#include "TigerTagParser.h"
#include "HomeAssistantManager.h"
#include "DisplayI.h"

extern "C" {
#include "openprinttag_lib.h"
}

// Tag kind enum to string for API responses
static const char* tagKindToString(TagKind kind) {
    switch (kind) {
        case TagKind::OpenPrintTag: return "OpenPrintTag";
        case TagKind::GenericUidTag: return "GenericUidTag";
        case TagKind::TigerTag:     return "TigerTag";
        case TagKind::OpenTag3D:    return "OpenTag3D";
        case TagKind::BambuTag:     return "BambuTag";
        case TagKind::BlankTag:     return "BlankTag";
        default:                    return "Unsupported";
    }
}

WebServerManager& WebServerManager::getInstance() {
    static WebServerManager instance;
    return instance;
}

bool WebServerManager::begin(bool apMode, uint16_t port) {
    _apMode = apMode;

    // mDNS — only in STA mode (AP uses fixed IP 192.168.4.1)
    if (!apMode) {
        if (MDNS.begin("spoolsense")) {
            MDNS.addService("http", "tcp", port);
            Serial.println("WebServerManager: mDNS started (spoolsense.local)");
        } else {
            Serial.println("WebServerManager: mDNS failed — reachable by IP only");
        }
    }

    // Pages
    _server.on("/",                    HTTP_GET, [this]() { handleLanding(); });
    _server.on("/reader",              HTTP_GET, [this]() { handleReader(); });
    _server.on("/writer/openprinttag", HTTP_GET, [this]() { handleOpenPrintTagWriter(); });
    _server.on("/writer/tigertag",     HTTP_GET, [this]() { handleTigerTagWriter(); });
    _server.on("/writer/opentag3d",    HTTP_GET, [this]() { handleOpenTag3DWriter(); });

    // Static assets
    _server.on("/css/shared.css",      HTTP_GET, [this]() { handleSharedCSS(); });
    _server.on("/js/shared.js",        HTTP_GET, [this]() { handleSharedJS(); });
    _server.on("/img/openprinttag.png", HTTP_GET, [this]() { handleOpenPrintTagLogo(); });
    _server.on("/img/tigertag.png",    HTTP_GET, [this]() { handleTigerTagLogo(); });
    _server.on("/img/opentag3d.png",   HTTP_GET, [this]() { handleOpenTag3DLogo(); });

    // Update page
    _server.on("/update",              HTTP_GET, [this]() { handleUpdatePage(); });
    _server.on("/config",              HTTP_GET, [this]() { handleConfigPage(); });
    _server.on("/troubleshooting",     HTTP_GET, [this]() { handleTroubleshootingPage(); });
    _server.on("/register/uid",        HTTP_GET, [this]() { handleUIDRegistrationPage(); });

    // API
    _server.on("/api/version",         HTTP_GET, [this]() { handleApiVersion(); });
    _server.on("/api/upload-firmware",  HTTP_POST,
        [this]() { handleApiUploadFirmwareComplete(); },
        [this]() { handleApiUploadFirmwareChunk(); });
    _server.on("/api/update-from-url", HTTP_POST, [this]() { handleApiUpdateFromUrl(); });
    _server.on("/api/ota-status",      HTTP_GET,  [this]() { handleApiOtaStatus(); });
    _server.on("/api/config",          HTTP_GET,  [this]() { handleApiGetConfig(); });
    _server.on("/api/config",          HTTP_POST, [this]() { handleApiPostConfig(); });
    _server.on("/api/status",          HTTP_GET,  [this]() { handleApiStatus(); });
    _server.on("/api/diagnostics",     HTTP_GET,  [this]() { handleApiDiagnostics(); });
    _server.on("/api/write-tag",       HTTP_POST, [this]() { handleApiWriteTag(); });
    _server.on("/api/format-tag",      HTTP_POST, [this]() { handleApiFormatTag(); });
    _server.on("/api/write-tigertag",  HTTP_POST, [this]() { handleApiWriteTigerTag(); });
    _server.on("/api/write-opentag3d", HTTP_POST, [this]() { handleApiWriteOpenTag3D(); });
    _server.on("/api/register-uid",    HTTP_POST, [this]() { handleApiRegisterUid(); });
    _server.on("/api/spoolman/spools", HTTP_GET,  [this]() { handleApiSpoolmanSpools(); });
    _server.on("/api/spoolman/link",   HTTP_POST, [this]() { handleApiSpoolmanLink(); });

    // Captive portal detection endpoints (AP mode)
    if (apMode) {
        // Android
        _server.on("/generate_204", HTTP_GET, [this]() {
            _server.sendHeader("Location", "http://192.168.4.1/config");
            _server.send(302, "text/plain", "");
        });
        // Apple
        _server.on("/hotspot-detect.html", HTTP_GET, [this]() {
            _server.sendHeader("Location", "http://192.168.4.1/config");
            _server.send(302, "text/plain", "");
        });
        // Windows
        _server.on("/connecttest.txt", HTTP_GET, [this]() {
            _server.sendHeader("Location", "http://192.168.4.1/config");
            _server.send(302, "text/plain", "");
        });
    }

    // Allow browser preflight requests (CORS) so the page can be tested
    // from a local file during development.
    _server.onNotFound([this]() {
        if (_server.method() == HTTP_OPTIONS) {
            _server.sendHeader("Access-Control-Allow-Origin", "*");
            _server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
            _server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
            _server.send(204);
        } else if (_apMode) {
            // Captive portal: redirect unknown requests to config page
            _server.sendHeader("Location", "http://192.168.4.1/config");
            _server.send(302, "text/plain", "Redirecting to setup...");
        } else {
            _server.send(404, "text/plain", "Not found");
        }
    });

    _server.begin();
    _initialized = true;
    Serial.printf("WebServerManager: HTTP server started on port %u\n", port);
    return true;
}

void WebServerManager::handleClient() {
    if (_initialized) {
        _server.handleClient();
    }
}

// ---------------------------------------------------------------------------
// Page handlers
// ---------------------------------------------------------------------------

void WebServerManager::handleLanding() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send_P(200, "text/html", LANDING_HTML);
}

void WebServerManager::handleReader() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send_P(200, "text/html", READER_HTML);
}

void WebServerManager::handleOpenPrintTagWriter() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send_P(200, "text/html", OPENPRINTTAG_WRITER_HTML);
}

void WebServerManager::handleTigerTagWriter() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send_P(200, "text/html", TIGERTAG_WRITER_HTML);
}

void WebServerManager::handleOpenTag3DWriter() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send_P(200, "text/html", OPENTAG3D_WRITER_HTML);
}

void WebServerManager::handleSharedCSS() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.sendHeader("Cache-Control", "public, max-age=86400");
    _server.send_P(200, "text/css", SHARED_CSS);
}

void WebServerManager::handleSharedJS() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.sendHeader("Cache-Control", "public, max-age=86400");
    _server.send_P(200, "application/javascript", SHARED_JS);
}

void WebServerManager::handleOpenPrintTagLogo() {
    _server.sendHeader("Cache-Control", "public, max-age=86400");
    _server.send_P(200, "image/png", reinterpret_cast<const char*>(OPENPRINTTAG_LOGO_PNG), OPENPRINTTAG_LOGO_PNG_LEN);
}

void WebServerManager::handleTigerTagLogo() {
    _server.sendHeader("Cache-Control", "public, max-age=86400");
    _server.send_P(200, "image/png", reinterpret_cast<const char*>(TIGERTAG_LOGO_PNG), TIGERTAG_LOGO_PNG_LEN);
}

void WebServerManager::handleOpenTag3DLogo() {
    _server.sendHeader("Cache-Control", "public, max-age=86400");
    _server.send_P(200, "image/png", reinterpret_cast<const char*>(OPENTAG3D_LOGO_PNG), OPENTAG3D_LOGO_PNG_LEN);
}

void WebServerManager::handleUpdatePage() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send_P(200, "text/html", UPDATE_HTML);
}

void WebServerManager::handleConfigPage() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send_P(200, "text/html", CONFIG_HTML);
}

void WebServerManager::handleTroubleshootingPage() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send_P(200, "text/html", TROUBLESHOOTING_HTML);
}

void WebServerManager::handleUIDRegistrationPage() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send_P(200, "text/html", UID_REGISTRATION_HTML);
}

void WebServerManager::handleApiRegisterUid() {
    Serial.println("WebServerManager: POST /api/register-uid received");
    _server.sendHeader("Access-Control-Allow-Origin", "*");

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        sendError(400, "Invalid JSON");
        return;
    }

    const char* uid = doc["uid"] | "";
    const char* manufacturer = doc["manufacturer"] | "";
    const char* material = doc["material"] | "PLA";
    const char* materialName = doc["material_name"] | "";
    const char* color = doc["color"] | "FF0000";
    float initialWeight = doc["initial_weight_g"] | 0.0f;
    float remainingWeight = doc["remaining_g"] | 0.0f;
    float density = doc["density"] | 0.0f;
    float diameter = doc["diameter_mm"] | 1.75f;
    int extruderTemp = doc["extruder_temp"] | 0;
    int bedTemp = doc["bed_temp"] | 0;

    if (strlen(uid) == 0) {
        sendError(400, "UID is required");
        return;
    }
    if (strlen(manufacturer) == 0) {
        sendError(400, "Manufacturer is required");
        return;
    }

    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    if (!baseUrl || strlen(baseUrl) == 0) {
        sendError(500, "Spoolman URL not configured");
        return;
    }

    WiFiClient client;
    HTTPClient http;
    char url[256];
    String response;
    int code;

    // --- Step 1: Find or create vendor ---
    int vendorId = -1;
    snprintf(url, sizeof(url), "%s/api/v1/vendor?name=%s", baseUrl, manufacturer);
    http.begin(client, url);
    code = http.GET();
    if (code == 200) {
        response = http.getString();
        StaticJsonDocument<2048> vendorDoc;
        if (!deserializeJson(vendorDoc, response)) {
            JsonArray arr = vendorDoc.as<JsonArray>();
            for (JsonObject v : arr) {
                if (strcasecmp(v["name"] | "", manufacturer) == 0) {
                    vendorId = v["id"] | -1;
                    break;
                }
            }
        }
    }
    http.end();

    if (vendorId < 0) {
        // Create vendor
        StaticJsonDocument<128> vendorBody;
        vendorBody["name"] = manufacturer;
        String vendorJson;
        serializeJson(vendorBody, vendorJson);

        snprintf(url, sizeof(url), "%s/api/v1/vendor", baseUrl);
        http.begin(client, url);
        http.addHeader("Content-Type", "application/json");
        code = http.POST(vendorJson);
        if (code == 200 || code == 201) {
            response = http.getString();
            StaticJsonDocument<512> vDoc;
            if (!deserializeJson(vDoc, response)) {
                vendorId = vDoc["id"] | -1;
            }
        }
        http.end();
    }

    // --- Step 2: Create filament ---
    StaticJsonDocument<512> filBody;
    filBody["name"] = strlen(materialName) > 0 ? materialName : material;
    filBody["material"] = material;
    if (vendorId > 0) filBody["vendor_id"] = vendorId;
    filBody["density"] = density > 0 ? density : 1.24f;
    filBody["diameter"] = diameter > 0 ? diameter : 1.75f;
    if (strlen(color) > 0) filBody["color_hex"] = color;
    if (extruderTemp > 0) filBody["settings_extruder_temp"] = extruderTemp;
    if (bedTemp > 0) filBody["settings_bed_temp"] = bedTemp;

    String filJson;
    serializeJson(filBody, filJson);
    Serial.printf("register-uid: filament payload: %s\n", filJson.c_str());

    snprintf(url, sizeof(url), "%s/api/v1/filament", baseUrl);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    code = http.POST(filJson);
    if (code != 200 && code != 201) {
        response = http.getString();
        http.end();
        char errMsg[128];
        snprintf(errMsg, sizeof(errMsg), "Failed to create filament (HTTP %d)", code);
        sendError(500, errMsg);
        return;
    }
    response = http.getString();
    http.end();

    int filamentId = -1;
    {
        StaticJsonDocument<1024> filDoc;
        if (!deserializeJson(filDoc, response)) {
            filamentId = filDoc["id"] | -1;
        }
    }
    if (filamentId < 0) {
        sendError(500, "Failed to parse filament ID from Spoolman response");
        return;
    }

    // --- Step 3: Create spool with nfc_id ---
    StaticJsonDocument<512> spoolBody;
    spoolBody["filament_id"] = filamentId;
    if (initialWeight > 0) spoolBody["initial_weight"] = initialWeight;
    if (remainingWeight > 0) spoolBody["remaining_weight"] = remainingWeight;

    JsonObject extra = spoolBody.createNestedObject("extra");
    // Spoolman extra fields require JSON-encoded string values (double-quoted)
    char quotedUid[128];
    snprintf(quotedUid, sizeof(quotedUid), "\"%s\"", uid);
    extra["nfc_id"] = quotedUid;

    String spoolJson;
    serializeJson(spoolBody, spoolJson);
    Serial.printf("register-uid: spool payload: %s\n", spoolJson.c_str());

    snprintf(url, sizeof(url), "%s/api/v1/spool", baseUrl);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    code = http.POST(spoolJson);
    if (code != 200 && code != 201) {
        response = http.getString();
        http.end();
        Serial.printf("register-uid: spool creation failed HTTP %d: %s\n", code, response.c_str());
        String errMsg = "Failed to create spool (HTTP " + String(code) + "): " + response;
        _server.send(500, "application/json", "{\"error\":\"" + errMsg + "\"}");
        return;
    }
    response = http.getString();
    http.end();

    int spoolId = -1;
    {
        StaticJsonDocument<1024> spoolDoc;
        if (!deserializeJson(spoolDoc, response)) {
            spoolId = spoolDoc["id"] | -1;
        }
    }

    // --- Success ---
    StaticJsonDocument<256> result;
    result["success"] = true;
    result["spool_id"] = spoolId;
    result["filament_id"] = filamentId;
    result["vendor_id"] = vendorId;
    result["uid"] = uid;
    String resultJson;
    serializeJson(result, resultJson);

    _server.send(200, "application/json", resultJson);
    Serial.printf("WebServerManager: Registered UID %s as spool %d (filament %d)\n", uid, spoolId, filamentId);
}

// ---------------------------------------------------------------------------
// API: Diagnostics
// ---------------------------------------------------------------------------

void WebServerManager::handleApiSpoolmanSpools() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");

    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    if (!baseUrl || strlen(baseUrl) == 0) {
        sendError(500, "Spoolman URL not configured");
        return;
    }

    WiFiClient client;
    HTTPClient http;
    char url[256];
    snprintf(url, sizeof(url), "%s/api/v1/spool?archived=false", baseUrl);
    http.begin(client, url);
    http.setTimeout(5000);
    int code = http.GET();

    if (code == 200) {
        // Stream the response directly — avoid buffering 25KB+ in heap
        WiFiClient* stream = http.getStreamPtr();
        int len = http.getSize();
        _server.setContentLength(len > 0 ? len : CONTENT_LENGTH_UNKNOWN);
        _server.send(200, "application/json", "");
        uint8_t buf[512];
        unsigned long lastData = millis();
        while (stream->available() || stream->connected()) {
            if (millis() - lastData > 10000) break;  // 10s timeout on stalled stream
            int avail = stream->available();
            if (avail > 0) {
                lastData = millis();
                int toRead = avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail;
                int bytesRead = stream->readBytes(buf, toRead);
                if (bytesRead > 0) {
                    _server.client().write(buf, bytesRead);
                }
            } else {
                delay(1);
            }
        }
    } else {
        char errMsg[64];
        snprintf(errMsg, sizeof(errMsg), "Spoolman returned HTTP %d", code);
        sendError(502, errMsg);
    }
    http.end();
}

void WebServerManager::handleApiSpoolmanLink() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        sendError(400, "Invalid JSON");
        return;
    }

    int newSpoolId = doc["spool_id"] | -1;
    const char* nfcId = doc["nfc_id"] | "";
    int oldSpoolId = doc["old_spool_id"] | -1;

    if (newSpoolId < 0 || strlen(nfcId) == 0) {
        sendError(400, "spool_id and nfc_id are required");
        return;
    }

    // Validate nfc_id — only hex characters, max 16 chars
    size_t nfcLen = strlen(nfcId);
    if (nfcLen > 16) {
        sendError(400, "nfc_id too long (max 16 chars)");
        return;
    }
    for (size_t i = 0; i < nfcLen; i++) {
        char c = nfcId[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
            sendError(400, "nfc_id must be hex characters only");
            return;
        }
    }

    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    if (!baseUrl || strlen(baseUrl) == 0) {
        sendError(500, "Spoolman URL not configured");
        return;
    }

    WiFiClient client;
    HTTPClient http;
    char url[256];
    String response;

    // Set nfc_id on new spool FIRST — confirm it works before clearing old
    char body[128];
    snprintf(body, sizeof(body), "{\"extra\":{\"nfc_id\":\"\\\"%s\\\"\"}}", nfcId);
    snprintf(url, sizeof(url), "%s/api/v1/spool/%d", baseUrl, newSpoolId);
    http.begin(client, url);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    int code = http.PATCH(body);
    response = http.getString();
    http.end();

    if (code != 200) {
        char errMsg[64];
        snprintf(errMsg, sizeof(errMsg), "Spoolman PATCH failed (HTTP %d)", code);
        sendError(502, errMsg);
        return;
    }

    Serial.printf("WebServerManager: Linked nfc_id=%s to spool %d\n", nfcId, newSpoolId);

    // Clear old spool's nfc_id AFTER new spool confirmed
    if (oldSpoolId > 0 && oldSpoolId != newSpoolId) {
        snprintf(url, sizeof(url), "%s/api/v1/spool/%d", baseUrl, oldSpoolId);
        http.begin(client, url);
        http.setTimeout(5000);
        http.addHeader("Content-Type", "application/json");
        int clearCode = http.PATCH("{\"extra\":{\"nfc_id\":\"\\\"\\\"\"}}");
        http.end();
        Serial.printf("WebServerManager: Cleared nfc_id from spool %d (HTTP %d)\n", oldSpoolId, clearCode);
    }

    _server.send(200, "application/json", "{\"success\":true}");
}

void WebServerManager::handleApiDiagnostics() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");

    StaticJsonDocument<1024> doc;

    // Device ID
    char deviceId[8];
    HomeAssistantManager::getDeviceId(deviceId, sizeof(deviceId));
    doc["device_id"] = deviceId;
    doc["firmware_version"] = FIRMWARE_VERSION;

    // WiFi
    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["connected"] = (WiFi.status() == WL_CONNECTED);
    if (WiFi.status() == WL_CONNECTED) {
        wifi["ssid"]     = WiFi.SSID();
        wifi["rssi_dbm"] = (int)WiFi.RSSI();
        wifi["ip"]       = WiFi.localIP().toString();
    }

    // MQTT
    ConfigUpdate cfg;
    ConfigurationManager::getInstance().getCurrentConfig(cfg);
    JsonObject mqtt = doc.createNestedObject("mqtt");
    bool mqttEnabled = (strlen(cfg.mqtt_host) > 0);
    mqtt["enabled"]   = mqttEnabled;
    mqtt["broker"]    = cfg.mqtt_host;
    mqtt["connected"] = HomeAssistantManager::getInstance().isConnected();

    // Spoolman
    JsonObject spoolman = doc.createNestedObject("spoolman");
    bool spoolmanEnabled = (cfg.spoolman_on != 0) && (strlen(cfg.spoolman_url) > 0);
    spoolman["enabled"] = spoolmanEnabled;
    spoolman["url"]     = cfg.spoolman_url;
    if (spoolmanEnabled) {
        // Quick reachability check — GET /api/v1/info
        HTTPClient http;
        char infoUrl[160];
        snprintf(infoUrl, sizeof(infoUrl), "%s/api/v1/info", cfg.spoolman_url);
        http.begin(infoUrl);
        http.setTimeout(3000);
        int code = http.GET();
        spoolman["reachable"] = (code == 200);
        if (code == 200) {
            // Extract version from response
            String body = http.getString();
            StaticJsonDocument<256> info;
            if (!deserializeJson(info, body) && info.containsKey("version")) {
                spoolman["version"] = info["version"].as<const char*>();
            }
        }
        http.end();
    } else {
        spoolman["reachable"] = false;
    }

    // NFC reader
    JsonObject nfc = doc.createNestedObject("nfc");
    char readerInfo[32] = {0};
    bool nfcOk = NFCManager::getInstance().getNfcReaderInfo(readerInfo, sizeof(readerInfo));
    nfc["ok"]     = nfcOk;
    nfc["reader"] = readerInfo;

    // Memory
    JsonObject memory = doc.createNestedObject("memory");
    uint32_t freeHeap  = (uint32_t)ESP.getFreeHeap();
    uint32_t totalHeap = (uint32_t)ESP.getHeapSize();
    memory["free_bytes"]      = freeHeap;
    memory["total_bytes"]     = totalHeap;
    memory["used_bytes"]      = totalHeap - freeHeap;
    memory["largest_block"]   = (uint32_t)ESP.getMaxAllocHeap();
    memory["uptime_s"]        = (uint32_t)(millis() / 1000);

    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

// ---------------------------------------------------------------------------
// API: Config
// ---------------------------------------------------------------------------

void WebServerManager::handleApiGetConfig() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");

    ConfigUpdate cfg;
    ConfigurationManager::getInstance().getCurrentConfig(cfg);

    StaticJsonDocument<768> doc;
    doc["wifi_ssid"] = cfg.wifi_ssid;
    doc["wifi_pass_set"] = (cfg.wifi_pass[0] != '\0');
    doc["mqtt_host"] = cfg.mqtt_host;
    doc["mqtt_port"] = cfg.mqtt_port;
    doc["mqtt_user"] = cfg.mqtt_user;
    doc["mqtt_pass_set"] = (cfg.mqtt_pass[0] != '\0');
    doc["spoolman_on"] = cfg.spoolman_on;
    doc["spoolman_url"] = cfg.spoolman_url;
    doc["auto_mode"] = cfg.auto_mode;
    doc["lcd_enabled"] = cfg.lcd_enabled;
    doc["led_enabled"] = cfg.led_enabled;
    doc["keypad_enabled"] = cfg.keypad_enabled;
    doc["moonraker_url"] = cfg.moonraker_url;
    doc["prusalink_on"] = cfg.prusalink_on;
    doc["prusalink_url"] = cfg.prusalink_url;
    doc["prusalink_key_set"] = (cfg.prusalink_api_key[0] != '\0');
    doc["nfc_reader"] = cfg.nfc_reader;
    doc["tft_enabled"] = cfg.tft_enabled;
    doc["ap_mode"] = _apMode;
    if (_apMode) {
        extern char g_apSSID[];
        doc["ap_ssid"] = g_apSSID;
    }

    String body;
    serializeJson(doc, body);
    _server.send(200, "application/json", body);
}

void WebServerManager::handleApiPostConfig() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        sendError(400, "Invalid JSON");
        return;
    }

    ConfigUpdate update;
    memset(&update, 0, sizeof(update));

    strncpy(update.wifi_ssid, doc["wifi_ssid"] | "", sizeof(update.wifi_ssid) - 1);
    strncpy(update.wifi_pass, doc["wifi_pass"] | "", sizeof(update.wifi_pass) - 1);
    strncpy(update.mqtt_host, doc["mqtt_host"] | "", sizeof(update.mqtt_host) - 1);
    update.mqtt_port = doc["mqtt_port"] | (uint16_t)1883;
    strncpy(update.mqtt_user, doc["mqtt_user"] | "", sizeof(update.mqtt_user) - 1);
    strncpy(update.mqtt_pass, doc["mqtt_pass"] | "", sizeof(update.mqtt_pass) - 1);
    update.spoolman_on = doc["spoolman_on"] | (uint8_t)0;
    strncpy(update.spoolman_url, doc["spoolman_url"] | "", sizeof(update.spoolman_url) - 1);
    update.auto_mode = doc["auto_mode"] | (uint8_t)0;
    update.lcd_enabled = doc["lcd_enabled"] | (uint8_t)0;
    update.led_enabled = doc["led_enabled"] | (uint8_t)0;
    update.keypad_enabled = doc["keypad_enabled"] | (uint8_t)0;
    update.tft_enabled = doc["tft_enabled"] | (uint8_t)0;
    // TFT and LCD share GPIO 22/23 on WROOM — auto-disable LCD when TFT enabled
    if (update.tft_enabled && update.lcd_enabled) {
        update.lcd_enabled = 0;
    }
    strncpy(update.moonraker_url, doc["moonraker_url"] | "", sizeof(update.moonraker_url) - 1);
    update.prusalink_on = doc["prusalink_on"] | (uint8_t)0;
    strncpy(update.prusalink_url, doc["prusalink_url"] | "", sizeof(update.prusalink_url) - 1);
    strncpy(update.prusalink_api_key, doc["prusalink_api_key"] | "", sizeof(update.prusalink_api_key) - 1);
    const char* nfcVal = doc["nfc_reader"] | "pn5180";
    if (strcmp(nfcVal, "pn532") != 0) nfcVal = "pn5180";  // only allow known values
    strncpy(update.nfc_reader, nfcVal, sizeof(update.nfc_reader) - 1);

    if (update.wifi_ssid[0] == '\0') {
        sendError(400, "WiFi SSID is required");
        return;
    }

    if (!ConfigurationManager::getInstance().saveToNVS(update)) {
        sendError(500, "Failed to save config");
        return;
    }

    Serial.println("WebServerManager: Config saved, rebooting...");
    _server.send(200, "application/json", "{\"success\":true}");

    // Suspend scan task to prevent new NFC writes, then wait for any
    // in-progress write to finish before restarting (#27)
    NFCManager::getInstance().pauseScanTask();
    delay(500);
    ESP.restart();
}

// ---------------------------------------------------------------------------
// API: Version
// ---------------------------------------------------------------------------

void WebServerManager::handleApiVersion() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    StaticJsonDocument<128> doc;
    doc["version"] = FIRMWARE_VERSION;
#ifdef BOARD_ESP32_S3
    doc["board"] = "esp32s3zero";
#else
    doc["board"] = "esp32dev";
#endif
    String body;
    serializeJson(doc, body);
    _server.send(200, "application/json", body);
}

// ---------------------------------------------------------------------------
// API: Firmware Upload (OTA)
// ---------------------------------------------------------------------------

void WebServerManager::handleApiUploadFirmwareChunk() {
    HTTPUpload& upload = _server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("OTA: Upload start: %s\n", upload.filename.c_str());
        // Pause NFC scan task during upload to prevent interference
        NFCManager::getInstance().pauseScanTask();
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("OTA: Success, %u bytes written\n", upload.totalSize);
        } else {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Serial.println("OTA: Upload aborted");
        Update.end();
        NFCManager::getInstance().resumeScanTask();
    }
}

void WebServerManager::handleApiUploadFirmwareComplete() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    if (Update.hasError()) {
        NFCManager::getInstance().resumeScanTask();
        _server.send(500, "application/json", "{\"success\":false,\"error\":\"Update failed\"}");
    } else {
        _server.send(200, "application/json", "{\"success\":true}");
        Serial.println("OTA: Rebooting...");
        // Scan task already paused during upload; wait for any in-flight NFC I/O (#27)
        delay(500);
        ESP.restart();
    }
}

// ---------------------------------------------------------------------------
// API: Update from URL (ESP32 downloads .bin from GitHub)
// ---------------------------------------------------------------------------

void WebServerManager::handleApiUpdateFromUrl() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");

    if (_otaState == OtaState::DOWNLOADING || _otaState == OtaState::FLASHING) {
        sendError(409, "Update already in progress");
        return;
    }

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        sendError(400, "Invalid JSON");
        return;
    }

    const char* url = doc["url"] | "";
    if (strlen(url) == 0) {
        sendError(400, "Missing url");
        return;
    }

    // Store URL and kick off background task
    strncpy(_otaUrl, url, sizeof(_otaUrl) - 1);
    _otaState = OtaState::DOWNLOADING;
    _otaProgress = 0;
    _otaError[0] = '\0';

    Serial.printf("OTA: Free heap before task: %u\n", ESP.getFreeHeap());
    xTaskCreatePinnedToCore(otaDownloadTask, "OTATask", 24576, this, 2, nullptr, 0);

    _server.send(200, "application/json", "{\"success\":true,\"status\":\"started\"}");
}

void WebServerManager::otaDownloadTask(void* param) {
    WebServerManager* self = static_cast<WebServerManager*>(param);

    Serial.printf("OTA: Downloading from %s\n", self->_otaUrl);

    // Pause NFC during OTA
    NFCManager::getInstance().pauseScanTask();

    // Free TFT sprite to reclaim ~57KB heap for SSL
    if (self->_display) {
        self->_display->freeForOTA();
        Serial.printf("OTA: Free heap after sprite release: %u\n", ESP.getFreeHeap());
    }

    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);
    http.begin(secureClient, self->_otaUrl);
    http.addHeader("Accept", "application/octet-stream");
    int httpCode = http.GET();

    if (httpCode != 200) {
        Serial.printf("OTA: Download failed, HTTP %d\n", httpCode);
        http.end();
        NFCManager::getInstance().resumeScanTask();
        snprintf(self->_otaError, sizeof(self->_otaError), "Download failed: HTTP %d", httpCode);
        self->_otaState = OtaState::FAILED;
        if (self->_display) {
            self->_display->showOTAError(self->_otaError);
        }
        vTaskDelete(nullptr);
        return;
    }

    int contentLength = http.getSize();
    Serial.printf("OTA: Content length: %d bytes\n", contentLength);
    Serial.printf("OTA: Free heap before Update.begin: %u\n", ESP.getFreeHeap());

    self->_otaState = OtaState::FLASHING;

    if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
        Serial.println("OTA: Update.begin() failed");
        Update.printError(Serial);
        http.end();
        NFCManager::getInstance().resumeScanTask();
        strncpy(self->_otaError, "Update.begin failed", sizeof(self->_otaError));
        self->_otaState = OtaState::FAILED;
        if (self->_display) {
            self->_display->showOTAError(self->_otaError);
        }
        vTaskDelete(nullptr);
        return;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    size_t written = 0;

    while (http.connected() && (contentLength <= 0 || written < (size_t)contentLength)) {
        size_t available = stream->available();
        if (available) {
            size_t toRead = (available > sizeof(buf)) ? sizeof(buf) : available;
            size_t bytesRead = stream->readBytes(buf, toRead);
            if (bytesRead > 0) {
                Update.write(buf, bytesRead);
                written += bytesRead;
                if (contentLength > 0) {
                    uint8_t newPct = (uint8_t)((written * 100) / contentLength);
                    if (newPct != self->_otaProgress) {
                        self->_otaProgress = newPct;
                        if (self->_display) {
                            self->_display->updateOTAProgress(newPct);
                        }
                    }
                }
            }
        }
        vTaskDelay(1);
    }

    http.end();

    if (Update.end(true)) {
        Serial.printf("OTA: Success, %u bytes written\n", written);
        self->_otaProgress = 100;
        self->_otaState = OtaState::SUCCESS;
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP.restart();
    } else {
        Serial.println("OTA: Update.end() failed");
        Update.printError(Serial);
        NFCManager::getInstance().resumeScanTask();
        strncpy(self->_otaError, "Update verification failed", sizeof(self->_otaError));
        self->_otaState = OtaState::FAILED;
        if (self->_display) {
            self->_display->showOTAError(self->_otaError);
        }
    }

    vTaskDelete(nullptr);
}

void WebServerManager::handleApiOtaStatus() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    StaticJsonDocument<128> doc;

    switch (_otaState) {
        case OtaState::IDLE:        doc["state"] = "idle"; break;
        case OtaState::DOWNLOADING: doc["state"] = "downloading"; break;
        case OtaState::FLASHING:    doc["state"] = "flashing"; break;
        case OtaState::SUCCESS:     doc["state"] = "success"; break;
        case OtaState::FAILED:      doc["state"] = "failed"; doc["error"] = _otaError; break;
    }
    doc["progress"] = _otaProgress;

    String body;
    serializeJson(doc, body);
    _server.send(200, "application/json", body);
}

// ---------------------------------------------------------------------------
// API: Status
// ---------------------------------------------------------------------------

void WebServerManager::handleApiStatus() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");

    CurrentSpoolState state;
    StaticJsonDocument<1024> doc;

    // Always include device ID and firmware version
    char deviceId[8];
    HomeAssistantManager::getDeviceId(deviceId, sizeof(deviceId));
    doc["device_id"] = deviceId;
    doc["firmware_version"] = FIRMWARE_VERSION;

    if (NFCManager::getInstance().getCurrentSpoolState(state) && state.present) {
        doc["present"] = true;
        doc["uid"] = state.spool_id;
        doc["tag_data_valid"] = state.tag_data_valid;
        doc["tag_kind"] = tagKindToString(state.kind);

        if (state.kind == TagKind::TigerTag) {
            // TigerTag — include parsed TigerTag data
            TigerTagData tt;
            if (NFCManager::getInstance().getLastTigerTagData(tt) && tt.valid) {
                JsonObject ttObj = doc.createNestedObject("tigertag");
                ttObj["material_id"] = tt.material_id;
                ttObj["material_name"] = tt.material_name;
                ttObj["brand_id"] = tt.brand_id;
                ttObj["brand_name"] = tt.brand_name;
                ttObj["weight_g"] = tt.weight_g;
                ttObj["diameter_mm"] = tt.diameter_mm;
                ttObj["aspect1_name"] = tt.aspect1_name;
                ttObj["aspect2_name"] = tt.aspect2_name;

                char colorHex[8];
                snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X",
                         tt.color_r, tt.color_g, tt.color_b);
                ttObj["color_hex"] = colorHex;

                if (tt.nozzle_temp_min > 0) ttObj["nozzle_temp_min"] = tt.nozzle_temp_min;
                if (tt.nozzle_temp_max > 0) ttObj["nozzle_temp_max"] = tt.nozzle_temp_max;
                if (tt.bed_temp_min > 0) ttObj["bed_temp_min"] = tt.bed_temp_min;
                if (tt.bed_temp_max > 0) ttObj["bed_temp_max"] = tt.bed_temp_max;
                if (tt.dry_temp > 0) ttObj["dry_temp"] = tt.dry_temp;
                if (tt.dry_time_hours > 0) ttObj["dry_time_hours"] = tt.dry_time_hours;
            }
        } else if (state.kind == TagKind::OpenTag3D) {
            // OpenTag3D — include parsed data
            opentag3d_t ot3d;
            if (NFCManager::getInstance().getLastOpenTag3DData(ot3d)) {
                JsonObject otObj = doc.createNestedObject("opentag3d");
                otObj["base_material"] = ot3d.base_material;
                if (ot3d.material_modifiers[0]) otObj["modifiers"] = ot3d.material_modifiers;
                otObj["manufacturer"] = ot3d.manufacturer;
                if (ot3d.color_name[0]) otObj["color_name"] = ot3d.color_name;

                char colorHex[8];
                snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X",
                         ot3d.color_rgba[0][0], ot3d.color_rgba[0][1], ot3d.color_rgba[0][2]);
                otObj["color_hex"] = colorHex;

                otObj["target_weight_g"] = ot3d.target_weight_g;
                otObj["diameter_mm"] = opentag3d_diameter_mm(&ot3d);
                if (ot3d.density_ugcm3 > 0) otObj["density"] = opentag3d_density_gcc(&ot3d);

                uint16_t printTemp = (uint16_t)opentag3d_temp_c(ot3d.print_temp_encoded);
                uint16_t bedTemp = (uint16_t)opentag3d_temp_c(ot3d.bed_temp_encoded);
                if (printTemp > 0) otObj["print_temp"] = printTemp;
                if (bedTemp > 0) otObj["bed_temp"] = bedTemp;

                if (ot3d.has_extended) {
                    if (ot3d.measured_filament_weight_g > 0) otObj["measured_weight_g"] = ot3d.measured_filament_weight_g;
                    if (ot3d.empty_spool_weight_g > 0) otObj["empty_spool_g"] = ot3d.empty_spool_weight_g;
                    if (ot3d.serial_number[0]) otObj["serial_number"] = ot3d.serial_number;
                    uint16_t minPrint = (uint16_t)opentag3d_temp_c(ot3d.min_print_temp_encoded);
                    uint16_t maxPrint = (uint16_t)opentag3d_temp_c(ot3d.max_print_temp_encoded);
                    uint16_t minBed = (uint16_t)opentag3d_temp_c(ot3d.min_bed_temp_encoded);
                    uint16_t maxBed = (uint16_t)opentag3d_temp_c(ot3d.max_bed_temp_encoded);
                    if (minPrint > 0) otObj["min_print_temp"] = minPrint;
                    if (maxPrint > 0) otObj["max_print_temp"] = maxPrint;
                    if (minBed > 0) otObj["min_bed_temp"] = minBed;
                    if (maxBed > 0) otObj["max_bed_temp"] = maxBed;
                    uint16_t dryTemp = (uint16_t)opentag3d_temp_c(ot3d.max_dry_temp_encoded);
                    if (dryTemp > 0) otObj["dry_temp"] = dryTemp;
                    if (ot3d.dry_time_hours > 0) otObj["dry_time_hours"] = ot3d.dry_time_hours;
                }
            }
        } else if (state.kind == TagKind::GenericUidTag) {
            // Generic UID tag — include resolved Spoolman data if available
            GenericTagSpoolInfo spoolInfo;
            NFCManager::getInstance().getGenericTagSpoolInfo(spoolInfo);
            if (spoolInfo.valid) {
                doc["material_name"] = spoolInfo.material_type;
                doc["manufacturer"] = spoolInfo.manufacturer;
                doc["color"] = spoolInfo.color_hex;
                doc["remaining_g"] = spoolInfo.remaining_weight_g;
                doc["spoolman_id"] = spoolInfo.spoolman_id;
                if (spoolInfo.extruder_temp > 0) doc["extruder_temp"] = spoolInfo.extruder_temp;
                if (spoolInfo.bed_temp > 0) doc["bed_temp"] = spoolInfo.bed_temp;
            }
        } else if (state.tag_data_valid) {
            // OpenPrintTag — include OPT fields
            uint8_t mat_type = 0;
            opt_get_material_type(&state.tag_data, &mat_type);
            doc["material_type"] = mat_type;
            doc["material_name"] = materialTypeToString(mat_type);

            uint8_t color[4] = {0};
            if (opt_get_primary_color(&state.tag_data, color) == OPT_OK) {
                char colorHex[8];
                snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X",
                         color[0], color[1], color[2]);
                doc["color"] = colorHex;
            } else {
                doc["color"] = (const char*)nullptr;
            }

            char manufacturer[33] = {0};
            opt_get_brand_name(&state.tag_data, manufacturer, sizeof(manufacturer));
            doc["manufacturer"] = manufacturer;

            float full_weight = 0.0f, consumed = 0.0f;
            opt_get_actual_full_weight(&state.tag_data, &full_weight);
            opt_get_consumed_weight(&state.tag_data, &consumed);
            doc["remaining_g"] = full_weight - consumed;
            doc["initial_weight_g"] = full_weight;

            int32_t spoolman_id = -1;
            opt_get_gp_spoolman_id(&state.tag_data, &spoolman_id);
            doc["spoolman_id"] = spoolman_id;

            float density = 0.0f;
            if (opt_get_density(&state.tag_data, &density) == OPT_OK && density > 0.0f)
                doc["density"] = density;

            float diameter = 0.0f;
            if (opt_get_filament_diameter(&state.tag_data, &diameter) == OPT_OK && diameter > 0.0f)
                doc["diameter_mm"] = diameter;

            char mat_name_custom[33] = {0};
            if (opt_get_material_name(&state.tag_data, mat_name_custom, sizeof(mat_name_custom)) == OPT_OK
                    && mat_name_custom[0] != '\0')
                doc["material_name"] = mat_name_custom;

            int16_t t = 0;
            if (opt_get_min_print_temp(&state.tag_data, &t) == OPT_OK && t != 0) doc["min_print_temp"] = t;
            if (opt_get_max_print_temp(&state.tag_data, &t) == OPT_OK && t != 0) doc["max_print_temp"] = t;
            if (opt_get_preheat_temp(&state.tag_data, &t) == OPT_OK && t != 0)   doc["preheat_temp"] = t;
            if (opt_get_min_bed_temp(&state.tag_data, &t) == OPT_OK && t != 0)   doc["min_bed_temp"] = t;
            if (opt_get_max_bed_temp(&state.tag_data, &t) == OPT_OK && t != 0)   doc["max_bed_temp"] = t;
        }
    } else {
        doc["present"] = false;
        doc["tag_data_valid"] = false;
    }

    String body;
    serializeJson(doc, body);
    _server.send(200, "application/json", body);
}

// ---------------------------------------------------------------------------
// API: Write OpenPrintTag
// ---------------------------------------------------------------------------

void WebServerManager::handleApiWriteTag() {
    Serial.println("WebServerManager: POST /api/write-tag received");
    _server.sendHeader("Access-Control-Allow-Origin", "*");

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        sendError(400, "Invalid JSON");
        return;
    }

    // Parse color — only when explicitly provided, reject invalid
    uint8_t color[4] = {0};
    bool hasValidColor = false;
    if (doc.containsKey("color")) {
        const char* colorStr = doc["color"] | "";
        if (parseHexColor(colorStr, color)) {
            hasValidColor = true;
        }
        // If color key present but invalid, skip — don't write zeros
    }

    // Parse material type — accept either integer or string name
    uint8_t mat_type = 0;
    if (doc["material_type"].is<int>()) {
        mat_type = doc["material_type"].as<uint8_t>();
    } else {
        mat_type = materialTypeFromString(doc["material_type"] | "PLA");
    }

    const char* uid        = doc["uid"] | "";
    const char* mfr        = doc["manufacturer"] | "";
    float initial_weight_g = doc["initial_weight_g"] | 0.0f;
    float remaining_g      = doc["remaining_g"] | 0.0f;
    int32_t spoolman_id    = doc["spoolman_id"] | -1;

    // Build atomic write fields — all provided fields written in a single NFC pass
    AtomicWriteFields fields;
    memset(&fields, 0, sizeof(fields));

    if (doc.containsKey("material_type")) {
        fields.has_material_type = true;
        fields.material_type = mat_type;
    }

    if (hasValidColor) {
        fields.has_color = true;
        memcpy(fields.color, color, 4);
    }

    if (doc.containsKey("initial_weight_g")) {
        fields.has_initial_weight = true;
        fields.initial_weight_g = initial_weight_g;
    }

    if (doc.containsKey("remaining_g")) {
        // Use provided initial_weight_g, or read from existing tag if not specified
        float base_weight = initial_weight_g;
        if (!doc.containsKey("initial_weight_g")) {
            CurrentSpoolState state;
            if (NFCManager::getInstance().getCurrentSpoolState(state) && state.tag_data_valid) {
                opt_get_actual_full_weight(&state.tag_data, &base_weight);
            }
        }
        float consumed_g = base_weight - remaining_g;
        if (consumed_g < 0.0f) consumed_g = 0.0f;
        fields.has_consumed_weight = true;
        fields.consumed_weight = consumed_g;
    }

    if (doc.containsKey("manufacturer") && mfr[0] != '\0') {
        fields.has_brand_name = true;
        strncpy(fields.brand_name, mfr, sizeof(fields.brand_name) - 1);
    }

    if (spoolman_id > 0) {
        fields.has_spoolman_id = true;
        fields.spoolman_id = spoolman_id;
    }

    float density = doc["density"] | 0.0f;
    if (density > 0.0f) {
        fields.has_density = true;
        fields.density = density;
    }

    float diameter_mm = doc["diameter_mm"] | 0.0f;
    if (diameter_mm > 0.0f) {
        fields.has_diameter = true;
        fields.diameter_mm = diameter_mm;
    }

    const char* mat_name = doc["material_name"] | "";
    if (mat_name[0] != '\0') {
        fields.has_material_name = true;
        strncpy(fields.material_name, mat_name, sizeof(fields.material_name) - 1);
    }

    struct { const char* key; bool AtomicWriteFields::*has; int16_t AtomicWriteFields::*val; } temps[] = {
        { "min_print_temp", &AtomicWriteFields::has_min_print_temp, &AtomicWriteFields::min_print_temp },
        { "max_print_temp", &AtomicWriteFields::has_max_print_temp, &AtomicWriteFields::max_print_temp },
        { "preheat_temp",   &AtomicWriteFields::has_preheat_temp,   &AtomicWriteFields::preheat_temp   },
        { "min_bed_temp",   &AtomicWriteFields::has_min_bed_temp,   &AtomicWriteFields::min_bed_temp   },
        { "max_bed_temp",   &AtomicWriteFields::has_max_bed_temp,   &AtomicWriteFields::max_bed_temp   },
    };
    for (const auto& entry : temps) {
        int v = doc[entry.key] | 0;
        if (v != 0) {
            fields.*entry.has = true;
            fields.*entry.val = static_cast<int16_t>(v);
        }
    }

    fields.pending = true;
    NFCManager::getInstance().setAtomicWriteFields(fields);

    // Enqueue single atomic write request
    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = millis();
    req.type = NFCWriteType::WRITE_ATOMIC;
    req.suppress_sync = 0;
    strncpy(req.expected_spool_id, uid, sizeof(req.expected_spool_id) - 1);

    if (!NFCManager::getInstance().enqueueWrite(req)) {
        NFCManager::getInstance().setAtomicWriteFields(AtomicWriteFields{});  // clear stale sidecar
        sendError(503, "Write queue full");
        return;
    }

    _server.send(200, "application/json", "{\"success\":true}");
}

// ---------------------------------------------------------------------------
// API: Format Tag
// ---------------------------------------------------------------------------

void WebServerManager::handleApiFormatTag() {
    Serial.println("WebServerManager: POST /api/format-tag received");
    _server.sendHeader("Access-Control-Allow-Origin", "*");

    // Optional body: {"uid": "..."} to validate tag before formatting
    char uid[17] = {0};
    if (_server.hasArg("plain") && _server.arg("plain").length() > 2) {
        StaticJsonDocument<64> doc;
        if (deserializeJson(doc, _server.arg("plain")) == DeserializationError::Ok) {
            const char* u = doc["uid"] | "";
            strncpy(uid, u, sizeof(uid) - 1);
        }
    }

    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = millis();
    req.type = NFCWriteType::FORMAT_NEW;
    strncpy(req.expected_spool_id, uid, sizeof(req.expected_spool_id) - 1);
    NFCManager::getInstance().enqueueWrite(req);

    _server.send(200, "application/json", "{\"success\":true}");
}

// ---------------------------------------------------------------------------
// API: Write TigerTag
// ---------------------------------------------------------------------------

void WebServerManager::handleApiWriteTigerTag() {
    Serial.println("WebServerManager: POST /api/write-tigertag received");
    _server.sendHeader("Access-Control-Allow-Origin", "*");

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        sendError(400, "Invalid JSON");
        return;
    }

    const char* uid = doc["uid"] | "";

    // Assemble 40-byte TigerTag binary layout (32 bytes data + 8 bytes padding)
    uint8_t payload[40];
    memset(payload, 0, sizeof(payload));

    // Version ID — V1.0 Maker (bytes 0-3)
    payload[0] = 0x5B; payload[1] = 0xF5; payload[2] = 0x92; payload[3] = 0x64;

    // Product ID — Maker/Offline (bytes 4-7)
    payload[4] = 0xFF; payload[5] = 0xFF; payload[6] = 0xFF; payload[7] = 0xFF;

    // Material ID (big-endian uint16, bytes 8-9)
    uint16_t materialId = doc["material_id"] | (uint16_t)38219;
    payload[8] = (materialId >> 8) & 0xFF;
    payload[9] = materialId & 0xFF;

    // Aspect IDs (bytes 10-11)
    payload[10] = doc["aspect1_id"] | (uint8_t)255;
    payload[11] = doc["aspect2_id"] | (uint8_t)255;

    // Type ID (byte 12) — 0x8E = Filament
    payload[12] = 0x8E;

    // Diameter ID (byte 13) — 56 = 1.75mm
    payload[13] = doc["diameter_id"] | (uint8_t)56;

    // Brand ID (big-endian uint16, bytes 14-15)
    uint16_t brandId = doc["brand_id"] | (uint16_t)65535;
    payload[14] = (brandId >> 8) & 0xFF;
    payload[15] = brandId & 0xFF;

    // Color RGBA (bytes 16-19)
    payload[16] = doc["color_r"] | (uint8_t)255;
    payload[17] = doc["color_g"] | (uint8_t)255;
    payload[18] = doc["color_b"] | (uint8_t)255;
    payload[19] = doc["color_a"] | (uint8_t)255;

    // Weight (big-endian 3 bytes, bytes 20-22)
    uint32_t weightG = doc["weight_g"] | (uint32_t)1000;
    payload[20] = (weightG >> 16) & 0xFF;
    payload[21] = (weightG >> 8) & 0xFF;
    payload[22] = weightG & 0xFF;

    // Unit ID (byte 23) — 21 = grams
    payload[23] = 21;

    // Nozzle temps (big-endian uint16, bytes 24-27)
    uint16_t nozzleMin = doc["nozzle_min"] | (uint16_t)0;
    uint16_t nozzleMax = doc["nozzle_max"] | (uint16_t)0;
    payload[24] = (nozzleMin >> 8) & 0xFF;
    payload[25] = nozzleMin & 0xFF;
    payload[26] = (nozzleMax >> 8) & 0xFF;
    payload[27] = nozzleMax & 0xFF;

    // Dry temp/time (bytes 28-29)
    payload[28] = doc["dry_temp"] | (uint8_t)0;
    payload[29] = doc["dry_time"] | (uint8_t)0;

    // Bed temps (bytes 30-31)
    payload[30] = doc["bed_min"] | (uint8_t)0;
    payload[31] = doc["bed_max"] | (uint8_t)0;

    // Enqueue write request
    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = millis();
    req.type = NFCWriteType::WRITE_TIGERTAG;
    strncpy(req.expected_spool_id, uid, sizeof(req.expected_spool_id) - 1);
    memcpy(req.data.tigertag_data, payload, 40);
    NFCManager::getInstance().enqueueWrite(req);

    _server.send(200, "application/json", "{\"success\":true}");
}

// ---------------------------------------------------------------------------
// API: Write OpenTag3D
// ---------------------------------------------------------------------------

void WebServerManager::handleApiWriteOpenTag3D() {
    Serial.println("WebServerManager: POST /api/write-opentag3d received");
    _server.sendHeader("Access-Control-Allow-Origin", "*");

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        sendError(400, "Invalid JSON");
        return;
    }

    const char* uid = doc["uid"] | "";

    opentag3d_t ot3d;
    memset(&ot3d, 0, sizeof(ot3d));

    ot3d.tag_version = doc["tag_version"] | (uint16_t)OT3D_SUPPORTED_VERSION;

    const char* baseMat = doc["base_material"] | "PLA";
    strncpy(ot3d.base_material, baseMat, sizeof(ot3d.base_material) - 1);

    const char* modifiers = doc["material_modifiers"] | "";
    strncpy(ot3d.material_modifiers, modifiers, sizeof(ot3d.material_modifiers) - 1);

    const char* mfr = doc["manufacturer"] | "";
    strncpy(ot3d.manufacturer, mfr, sizeof(ot3d.manufacturer) - 1);

    const char* colorName = doc["color_name"] | "";
    strncpy(ot3d.color_name, colorName, sizeof(ot3d.color_name) - 1);

    ot3d.color_rgba[0][0] = doc["color_r"] | (uint8_t)0;
    ot3d.color_rgba[0][1] = doc["color_g"] | (uint8_t)0;
    ot3d.color_rgba[0][2] = doc["color_b"] | (uint8_t)0;
    ot3d.color_rgba[0][3] = doc["color_a"] | (uint8_t)255;

    ot3d.diameter_um = doc["diameter_um"] | (uint16_t)1750;
    ot3d.target_weight_g = doc["target_weight_g"] | (uint16_t)1000;

    uint16_t printTemp = doc["print_temp_c"] | (uint16_t)0;
    uint16_t bedTemp = doc["bed_temp_c"] | (uint16_t)0;
    ot3d.print_temp_encoded = (uint8_t)(printTemp / 5);
    ot3d.bed_temp_encoded = (uint8_t)(bedTemp / 5);

    ot3d.density_ugcm3 = doc["density_ugcm3"] | (uint16_t)0;
    ot3d.transmission_distance = doc["transmission_distance"] | (uint16_t)0;

    if (doc.containsKey("serial_number") || doc.containsKey("min_print_temp_c")) {
        ot3d.has_extended = 1;

        const char* serial = doc["serial_number"] | "";
        strncpy(ot3d.serial_number, serial, sizeof(ot3d.serial_number) - 1);

        const char* url = doc["online_url"] | "";
        strncpy(ot3d.online_url, url, sizeof(ot3d.online_url) - 1);

        ot3d.manufacture_year = doc["manufacture_year"] | (uint16_t)0;
        ot3d.manufacture_month = doc["manufacture_month"] | (uint8_t)0;
        ot3d.manufacture_day = doc["manufacture_day"] | (uint8_t)0;

        ot3d.empty_spool_weight_g = doc["empty_spool_weight_g"] | (uint16_t)0;
        ot3d.measured_filament_weight_g = doc["measured_filament_weight_g"] | (uint16_t)0;
        ot3d.measured_filament_length_m = doc["measured_filament_length_m"] | (uint16_t)0;

        uint16_t maxDryTemp = doc["max_dry_temp_c"] | (uint16_t)0;
        ot3d.max_dry_temp_encoded = (uint8_t)(maxDryTemp / 5);
        ot3d.dry_time_hours = doc["dry_time_hours"] | (uint8_t)0;

        uint16_t minPrint = doc["min_print_temp_c"] | (uint16_t)0;
        uint16_t maxPrint = doc["max_print_temp_c"] | (uint16_t)0;
        uint16_t minBed = doc["min_bed_temp_c"] | (uint16_t)0;
        uint16_t maxBed = doc["max_bed_temp_c"] | (uint16_t)0;
        ot3d.min_print_temp_encoded = (uint8_t)(minPrint / 5);
        ot3d.max_print_temp_encoded = (uint8_t)(maxPrint / 5);
        ot3d.min_bed_temp_encoded = (uint8_t)(minBed / 5);
        ot3d.max_bed_temp_encoded = (uint8_t)(maxBed / 5);

        ot3d.min_volumetric_speed = doc["min_volumetric_speed"] | (uint8_t)0;
        ot3d.max_volumetric_speed = doc["max_volumetric_speed"] | (uint8_t)0;
        ot3d.target_volumetric_speed = doc["target_volumetric_speed"] | (uint8_t)0;
    }

    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = millis();
    req.type = NFCWriteType::WRITE_OPENTAG3D;
    strncpy(req.expected_spool_id, uid, sizeof(req.expected_spool_id) - 1);

    if (!NFCManager::getInstance().enqueueRawWrite(req, (const uint8_t*)&ot3d, sizeof(ot3d))) {
        sendError(503, "Write queue full or busy");
        return;
    }

    _server.send(200, "application/json", "{\"success\":true}");
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void WebServerManager::sendError(int code, const char* msg) {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    char body[96];
    snprintf(body, sizeof(body), "{\"success\":false,\"error\":\"%s\"}", msg);
    _server.send(code, "application/json", body);
}

#endif // NATIVE_TEST
