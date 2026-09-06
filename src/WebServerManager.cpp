#include "WebServerManager.h"
#include "TaskUtils.h"

#ifndef NATIVE_TEST

#include <WiFi.h>

#include <Arduino.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "MemoryDiagnostics.h"
// Text UI assets (HTML/CSS/JS) are served pre-gzipped from generated PROGMEM
// byte arrays; the readable source lives in src/*HTML.h / src/Shared*.h and is
// compiled into WebAssetsGz.h by scripts/gen_gzip_assets.py at build time.
#include "gen/WebAssetsGz.h"
#include "OpenPrintTagLogo.h"
#include "TigerTagLogo.h"
#include "OpenTag3DLogo.h"
#include "OpenSpoolLogo.h"
#include "LogBuffer.h"
#include "ConfigurationManager.h"
#include "NFCManager.h"
#include "U1Manager.h"
#include "DiagnosticsManager.h"
#include "NFCTypes.h"
#include "NFCWriteTypes.h"
#include "ApplicationManager.h"
#include "ConversionUtils.h"
#include "TigerTagParser.h"
#include "BambuTagParser.h"
#include "HomeAssistantManager.h"
#include "SpoolmanManager.h"
#include "DisplayI.h"

// Shared HTTP mutex — serializes all outbound HTTP requests across tasks
extern SemaphoreHandle_t g_httpMutex;
static constexpr TickType_t HTTP_MUTEX_TIMEOUT = pdMS_TO_TICKS(10000);
// Diagnostics polls from loopTask: prefer reporting "busy" over stalling the
// loop waiting for a sync or printer poll to finish.
static constexpr TickType_t DIAG_HTTP_MUTEX_TIMEOUT = pdMS_TO_TICKS(500);

extern "C" {
#include "openprinttag_lib.h"
}

// URL-encode a string for safe use in query parameters
static String urlEncode(const char* str) {
    String encoded;
    while (*str) {
        unsigned char c = (unsigned char)*str++;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += (char)c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            encoded += buf;
        }
    }
    return encoded;
}

// Tag kind enum to string for API responses
static const char* tagKindToString(TagKind kind) {
    switch (kind) {
        case TagKind::OpenPrintTag: return "OpenPrintTag";
        case TagKind::GenericUidTag: return "GenericUidTag";
        case TagKind::TigerTag:     return "TigerTag";
        case TagKind::OpenTag3D:    return "OpenTag3D";
        case TagKind::BambuTag:     return "BambuTag";
        case TagKind::OpenSpoolTag: return "OpenSpoolTag";
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
        const char* hostname = ConfigurationManager::getInstance().getHostname();
        if (MDNS.begin(hostname)) {
            MDNS.addService("http", "tcp", port);
            Serial.printf("WebServerManager: mDNS started (%s.local)\n", hostname);
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
    _server.on("/writer/openspool",    HTTP_GET, [this]() { handleOpenSpoolWriter(); });

    // Static assets
    _server.on("/css/shared.css",      HTTP_GET, [this]() { handleSharedCSS(); });
    _server.on("/js/shared.js",        HTTP_GET, [this]() { handleSharedJS(); });
    _server.on("/img/openprinttag.png", HTTP_GET, [this]() { handleOpenPrintTagLogo(); });
    _server.on("/img/tigertag.png",    HTTP_GET, [this]() { handleTigerTagLogo(); });
    _server.on("/img/opentag3d.png",   HTTP_GET, [this]() { handleOpenTag3DLogo(); });
    _server.on("/img/openspool.png",   HTTP_GET, [this]() { handleOpenSpoolLogo(); });

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
    _server.on("/api/diagnostics/session",        HTTP_POST, [this]() { handleApiSelfTestStart(); });
    _server.on("/api/diagnostics/session",        HTTP_GET,  [this]() { handleApiSelfTestStatus(); });
    _server.on("/api/diagnostics/session/input",  HTTP_POST, [this]() { handleApiSelfTestInput(); });
    _server.on("/api/diagnostics/session/cancel", HTTP_POST, [this]() { handleApiSelfTestCancel(); });
    _server.on("/api/diagnostics/report",         HTTP_GET,  [this]() { handleApiSelfTestReport(); });
    _server.on("/api/write-tag",       HTTP_POST, [this]() { handleApiWriteTag(); });
    _server.on("/api/format-tag",      HTTP_POST, [this]() { handleApiFormatTag(); });
    _server.on("/api/write-tigertag",  HTTP_POST, [this]() { handleApiWriteTigerTag(); });
    _server.on("/api/write-opentag3d", HTTP_POST, [this]() { handleApiWriteOpenTag3D(); });
    _server.on("/api/write-openspool", HTTP_POST, [this]() { handleApiWriteOpenSpool(); });
    _server.on("/api/register-uid",    HTTP_POST, [this]() { handleApiRegisterUid(); });
    _server.on("/api/spoolman/spools", HTTP_GET,  [this]() { handleApiSpoolmanSpools(); });
    _server.on("/api/spoolman/link",         HTTP_POST, [this]() { handleApiSpoolmanLink(); });
    _server.on("/api/spoolman/pending-link", HTTP_POST, [this]() { handleApiSpoolmanPendingLink(); });
    _server.on("/api/spoolman/pending-link", HTTP_GET, [this]() { handleApiSpoolmanPendingLink(); });
    _server.on("/api/u1/assign", HTTP_POST, [this]() { handleApiU1Assign(); });
    _server.on("/api/spoolman/find-vendor",     HTTP_GET,  [this]() { handleApiSpoolmanFindVendor(); });
    _server.on("/api/spoolman/find-filament",   HTTP_GET,  [this]() { handleApiSpoolmanFindFilament(); });
    _server.on("/api/spoolman/save-enrichment", HTTP_POST, [this]() { handleApiSpoolmanSaveEnrichment(); });

    // Log viewer
    _server.on("/logs",           HTTP_GET,  [this]() { handleLogViewer(); });
    _server.on("/api/logs",       HTTP_GET,  [this]() { handleApiLogs(); });
    _server.on("/api/logs/clear", HTTP_POST, [this]() { handleApiLogsClear(); });

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

    _server.onNotFound([this]() {
        if (_apMode) {
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

void WebServerManager::sendGzip(int code, const char* contentType,
                                const uint8_t* data, size_t len) {
    _server.sendHeader("Content-Encoding", "gzip");
    _server.send_P(code, contentType, reinterpret_cast<const char*>(data), len);
}

void WebServerManager::handleLanding() {
    sendGzip(200, "text/html", LANDING_HTML_GZ, LANDING_HTML_GZ_LEN);
}

void WebServerManager::handleReader() {
    sendGzip(200, "text/html", READER_HTML_GZ, READER_HTML_GZ_LEN);
}

void WebServerManager::handleOpenPrintTagWriter() {
    sendGzip(200, "text/html", OPENPRINTTAG_WRITER_HTML_GZ, OPENPRINTTAG_WRITER_HTML_GZ_LEN);
}

void WebServerManager::handleTigerTagWriter() {
    sendGzip(200, "text/html", TIGERTAG_WRITER_HTML_GZ, TIGERTAG_WRITER_HTML_GZ_LEN);
}

void WebServerManager::handleOpenTag3DWriter() {
    sendGzip(200, "text/html", OPENTAG3D_WRITER_HTML_GZ, OPENTAG3D_WRITER_HTML_GZ_LEN);
}

void WebServerManager::handleOpenSpoolWriter() {
    sendGzip(200, "text/html", OPENSPOOL_WRITER_HTML_GZ, OPENSPOOL_WRITER_HTML_GZ_LEN);
}

void WebServerManager::handleSharedCSS() {
    _server.sendHeader("Cache-Control", "no-store");
    sendGzip(200, "text/css", SHARED_CSS_GZ, SHARED_CSS_GZ_LEN);
}

void WebServerManager::handleSharedJS() {
    _server.sendHeader("Cache-Control", "no-store");
    sendGzip(200, "application/javascript", SHARED_JS_GZ, SHARED_JS_GZ_LEN);
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

void WebServerManager::handleOpenSpoolLogo() {
    _server.sendHeader("Cache-Control", "public, max-age=86400");
    _server.send_P(200, "image/jpeg", reinterpret_cast<const char*>(OPENSPOOL_LOGO_PNG), OPENSPOOL_LOGO_PNG_SIZE);
}

void WebServerManager::handleUpdatePage() {
    sendGzip(200, "text/html", UPDATE_HTML_GZ, UPDATE_HTML_GZ_LEN);
}

void WebServerManager::handleConfigPage() {
    sendGzip(200, "text/html", CONFIG_HTML_GZ, CONFIG_HTML_GZ_LEN);
}

void WebServerManager::handleTroubleshootingPage() {
    sendGzip(200, "text/html", TROUBLESHOOTING_HTML_GZ, TROUBLESHOOTING_HTML_GZ_LEN);
}

void WebServerManager::handleUIDRegistrationPage() {
    sendGzip(200, "text/html", UID_REGISTRATION_HTML_GZ, UID_REGISTRATION_HTML_GZ_LEN);
}

void WebServerManager::handleLogViewer() {
    sendGzip(200, "text/html", LOG_VIEWER_HTML_GZ, LOG_VIEWER_HTML_GZ_LEN);
}

void WebServerManager::handleApiLogs() {
    char* buf = (char*)malloc(4097);
    if (!buf) {
        _server.send(500, "text/plain", "out of memory");
        return;
    }
    LogBuffer::getInstance().getLog(buf, 4097);
    _server.send(200, "text/plain", buf);
    free(buf);
}

void WebServerManager::handleApiLogsClear() {
    LogBuffer::getInstance().clear();
    _server.send(200, "application/json", "{\"success\":true}");
}

void WebServerManager::handleApiRegisterUid() {
    Serial.println("WebServerManager: POST /api/register-uid received");

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

    if (xSemaphoreTake(g_httpMutex, HTTP_MUTEX_TIMEOUT) != pdTRUE) {
        sendError(503, "Busy — try again");
        return;
    }

    WiFiClient client;
    HTTPClient http;
    char url[256];
    String response;
    int code;

    // --- Step 1: Find or create vendor ---
    int vendorId = SpoolmanManager::getInstance().findVendorNoLock(manufacturer);
    if (vendorId == -2) {
        // Lookup failed — creating now could mint a duplicate vendor
        xSemaphoreGive(g_httpMutex);
        sendError(503, "Spoolman vendor lookup failed — try again");
        return;
    }

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

    // --- Step 2: Find or create filament (#134) ---
    int filamentId = enrichFindOrCreateFilament(client, http, baseUrl, material, color,
                                                 vendorId, density > 0 ? density : 1.24f,
                                                 diameter > 0 ? diameter : 1.75f,
                                                 bedTemp, extruderTemp, -1);
    if (filamentId < 0) {
        xSemaphoreGive(g_httpMutex);
        sendError(500, "Failed to find or create filament");
        return;
    }
    Serial.printf("register-uid: filament id=%d\n", filamentId);

    // --- Step 3: Find existing spool or create new (#135) ---
    char quotedUid[128];
    snprintf(quotedUid, sizeof(quotedUid), "\"%s\"", uid);

    float existingInitialWeight = 0.0f;
    String linkExtraJson, linkFilMaterial, linkFilColor;
    int spoolId = enrichFindSpoolByUid(client, http, baseUrl, uid, existingInitialWeight, linkExtraJson,
                                       linkFilMaterial, linkFilColor);
    if (spoolId == -2) {
        // Lookup failed (transport/parse) — creating here would duplicate an
        // existing spool we simply couldn't see (#218 family)
        xSemaphoreGive(g_httpMutex);
        sendError(503, "Spoolman lookup failed — try again");
        return;
    }

    if (spoolId > 0) {
        // Spool exists — update it instead of creating a duplicate
        Serial.printf("register-uid: found existing spool %d, updating\n", spoolId);
        float effInitial = initialWeight > 0 ? initialWeight : existingInitialWeight;
        if (!enrichUpdateSpool(client, http, baseUrl, spoolId, filamentId, remainingWeight, effInitial, "", "")) {
            xSemaphoreGive(g_httpMutex);
            sendError(500, "Failed to update existing spool");
            return;
        }
    } else {
        // No existing spool — create new
        StaticJsonDocument<512> spoolBody;
        spoolBody["filament_id"] = filamentId;
        spoolBody["initial_weight"] = initialWeight > 0 ? initialWeight : 1000.0f;
        if (remainingWeight > 0) spoolBody["remaining_weight"] = remainingWeight;

        JsonObject extra = spoolBody.createNestedObject("extra");
        extra["nfc_id"] = quotedUid;

        String spoolJson;
        serializeJson(spoolBody, spoolJson);
        Serial.printf("register-uid: creating spool: %s\n", spoolJson.c_str());

        snprintf(url, sizeof(url), "%s/api/v1/spool", baseUrl);
        http.begin(client, url);
        http.addHeader("Content-Type", "application/json");
        code = http.POST(spoolJson);
        if (code != 200 && code != 201) {
            response = http.getString();
            http.end();
            Serial.printf("register-uid: spool creation failed HTTP %d: %s\n", code, response.c_str());
            String errMsg = "Failed to create spool (HTTP " + String(code) + "): " + response;
            xSemaphoreGive(g_httpMutex);
            _server.send(500, "application/json", "{\"error\":\"" + errMsg + "\"}");
            return;
        }
        response = http.getString();
        http.end();

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

    xSemaphoreGive(g_httpMutex);
    _server.send(200, "application/json", resultJson);
    Serial.printf("WebServerManager: Registered UID %s as spool %d (filament %d)\n", uid, spoolId, filamentId);
}

// ---------------------------------------------------------------------------
// API: Diagnostics
// ---------------------------------------------------------------------------

void WebServerManager::handleApiSpoolmanSpools() {

    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    if (!baseUrl || strlen(baseUrl) == 0) {
        sendError(500, "Spoolman URL not configured");
        return;
    }

    if (xSemaphoreTake(g_httpMutex, HTTP_MUTEX_TIMEOUT) != pdTRUE) {
        sendError(503, "Busy — try again");
        return;
    }

    WiFiClient client;
    HTTPClient http;
    char url[256];
    snprintf(url, sizeof(url), "%s/api/v1/spool?archived=false", baseUrl);
    http.begin(client, url);
    // HTTP/1.0 — the raw stream is relayed verbatim, so chunked framing from a
    // reverse-proxied Spoolman would otherwise corrupt the JSON (c16bcfd rule)
    http.useHTTP10(true);
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
    xSemaphoreGive(g_httpMutex);
}

void WebServerManager::handleApiSpoolmanLink() {

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

    if (xSemaphoreTake(g_httpMutex, HTTP_MUTEX_TIMEOUT) != pdTRUE) {
        sendError(503, "Busy — try again");
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
        xSemaphoreGive(g_httpMutex);
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

    xSemaphoreGive(g_httpMutex);
    _server.send(200, "application/json", "{\"success\":true}");
}

void WebServerManager::handleApiU1Assign() {
    // Same task as the ApplicationManager dispatch loop (both run from loop()),
    // so calling the U1Manager directly is single-threaded by construction
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, _server.arg("plain"))) {
        sendError(400, "Invalid JSON");
        return;
    }
    int channel = doc["channel"] | -1;
    if (channel < 0 || channel > 3) {
        sendError(400, "channel must be 0-3");
        return;
    }
    if (!U1Manager::getInstance().hasStagedSpool()) {
        sendError(409, "Nothing staged — scan a tag first");
        return;
    }
    bool ok = U1Manager::getInstance().assignStagedToChannel((uint8_t)channel);
    if (!ok) {
        sendError(502, "U1 rejected the assignment — check Moonraker/printer");
        return;
    }
    char body[48];
    snprintf(body, sizeof(body), "{\"success\":true,\"channel\":%d}", channel);
    _server.send(200, "application/json", body);
}

void WebServerManager::handleApiSpoolmanPendingLink() {
    if (_server.method() == HTTP_GET) {
        // Link-only flow polls this. States: armed (countdown), consumed
        // (with the real PATCH outcome + the tag that took it), expired, idle.
        auto st = SpoolmanManager::getInstance().getPendingLinkStatus();
        const char* stateStr = "idle";
        switch (st.state) {
            case SpoolmanManager::PendingLinkState::Armed:    stateStr = "armed"; break;
            case SpoolmanManager::PendingLinkState::Consumed: stateStr = "consumed"; break;
            case SpoolmanManager::PendingLinkState::Expired:  stateStr = "expired"; break;
            default: break;
        }
        char body[160];
        snprintf(body, sizeof(body),
                 "{\"state\":\"%s\",\"spool_id\":%ld,\"remaining_ms\":%lu,\"link_ok\":%s,\"uid\":\"%s\"}",
                 stateStr, (long)st.spoolId, (unsigned long)st.remainingMs,
                 st.linkOk ? "true" : "false", st.uid);
        _server.send(200, "application/json", body);
        return;
    }

    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, _server.arg("plain"))) {
        sendError(400, "Invalid JSON");
        return;
    }

    int spoolId = doc["spool_id"] | -1;
    if (spoolId <= 0) {
        sendError(400, "spool_id required");
        return;
    }

    SpoolmanManager::getInstance().setPendingLink(spoolId);
    Serial.printf("WebServerManager: Pending link set for spool %d\n", spoolId);
    _server.send(200, "application/json", "{\"success\":true}");
}

// --- Self-test wizard (#253) ---------------------------------------------

void WebServerManager::handleApiSelfTestStart() {
    DiagnosticsManager::Options opts;  // defaults: network + stability on
    if (_server.hasArg("plain") && _server.arg("plain").length() > 0) {
        StaticJsonDocument<128> body;
        // A malformed body must not silently start a full session (network
        // checks + a scan pause window) — reject it instead.
        if (deserializeJson(body, _server.arg("plain"))) {
            sendError(400, "Invalid JSON");
            return;
        }
        if (body.containsKey("network"))   opts.network   = body["network"].as<bool>();
        if (body.containsKey("stability")) opts.stability = body["stability"].as<bool>();
    }
    if (!DiagnosticsManager::getInstance().startSession(opts)) {
        sendError(409, "A self-test is already running");
        return;
    }
    _server.send(200, "application/json", "{\"started\":true}");
}

void WebServerManager::handleApiSelfTestStatus() {
    DiagnosticsManager::Snapshot s;
    DiagnosticsManager::getInstance().getSnapshot(s);

    JsonDocument doc;
    doc["active"] = s.active;
    doc["overall"] = DiagnosticsManager::statusName(s.overall);
    doc["waiting_for_user"] = s.waiting_for_user;
    doc["prompt"] = s.stage_prompt;
    doc["stability_ran"] = s.stability_ran;
    doc["stability_score"] = s.stability_score;
    JsonArray arr = doc["results"].to<JsonArray>();
    for (uint8_t i = 0; i < s.result_count; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["test"]           = DiagnosticsManager::testName(s.results[i].test);
        o["status"]         = DiagnosticsManager::statusName(s.results[i].status);
        o["summary"]        = s.results[i].summary;
        o["recommendation"] = s.results[i].recommendation;
        o["code"]           = s.results[i].code;
        o["duration_ms"]    = s.results[i].duration_ms;
    }
    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

void WebServerManager::handleApiSelfTestInput() {
    DiagnosticsManager::getInstance().submitUserContinue();
    _server.send(200, "application/json", "{\"ok\":true}");
}

void WebServerManager::handleApiSelfTestCancel() {
    DiagnosticsManager::getInstance().cancelSession();
    _server.send(200, "application/json", "{\"ok\":true}");
}

void WebServerManager::handleApiSelfTestReport() {
    const size_t CAP = 4096;
    char* buf = (char*)malloc(CAP);
    if (!buf) {
        sendError(500, "out of memory");
        return;
    }
    DiagnosticsManager::getInstance().buildReport(buf, CAP);
    _server.send(200, "text/plain", buf);
    free(buf);
}

void WebServerManager::handleApiDiagnostics() {

    // 1536: task stack-hwm entries added on top of the original 1024 payload
    StaticJsonDocument<1536> doc;

    // Device ID
    char deviceId[8];
    HomeAssistantManager::getDeviceId(deviceId, sizeof(deviceId));
    doc["device_id"] = deviceId;
    doc["firmware_version"] = FIRMWARE_VERSION;
    // Answers "was that reboot a crash or a power blip?" remotely — the RAM log
    // buffer dies with the reboot, this survives as long as the board is up
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   doc["reset_reason"] = "poweron"; break;
        case ESP_RST_SW:        doc["reset_reason"] = "software"; break;
        case ESP_RST_PANIC:     doc["reset_reason"] = "panic"; break;
        case ESP_RST_INT_WDT:   doc["reset_reason"] = "int_wdt"; break;
        case ESP_RST_TASK_WDT:  doc["reset_reason"] = "task_wdt"; break;
        case ESP_RST_WDT:       doc["reset_reason"] = "wdt"; break;
        case ESP_RST_BROWNOUT:  doc["reset_reason"] = "brownout"; break;
        case ESP_RST_DEEPSLEEP: doc["reset_reason"] = "deepsleep"; break;
        case ESP_RST_EXT:       doc["reset_reason"] = "external"; break;
        default:                doc["reset_reason"] = "unknown"; break;
    }

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
        // Outbound HTTP is serialized by g_httpMutex; this endpoint used to skip
        // it, so opening the troubleshooting page during a Spoolman sync or
        // printer poll ran a second client concurrently. Wait only briefly —
        // the page auto-polls this from loopTask, so a long wait would stall the
        // loop worse than the request it is guarding. If the path is busy,
        // report that instead of a reachability verdict we did not measure.
        bool held = g_httpMutex &&
                    (xSemaphoreTake(g_httpMutex, DIAG_HTTP_MUTEX_TIMEOUT) == pdTRUE);
        if (g_httpMutex && !held) {
            spoolman["reachable"] = false;
            spoolman["check_skipped"] = true;
        } else {
            // Quick reachability check — GET /api/v1/info
            HTTPClient http;
            char infoUrl[160];
            snprintf(infoUrl, sizeof(infoUrl), "%s/api/v1/info", cfg.spoolman_url);
            http.begin(infoUrl);
            // Both timeouts: setTimeout is the read timeout, and the default
            // 5s connect timeout would otherwise dominate the loop stall.
            http.setConnectTimeout(2000);
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
            if (held) xSemaphoreGive(g_httpMutex);
        }
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
    memory["min_free_bytes"]  = (uint32_t)ESP.getMinFreeHeap();
    memory["internal_free_bytes"]     = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    memory["internal_min_free_bytes"] = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    memory["internal_largest_block"]  = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    memory["uptime_s"]        = (uint32_t)(millis() / 1000);

    JsonObject stacks = memory.createNestedObject("stack_hwm_bytes");
    MemoryDiagnostics::TaskStackStat stats[MemoryDiagnostics::MAX_TRACKED];
    size_t statCount = MemoryDiagnostics::collect(stats, MemoryDiagnostics::MAX_TRACKED);
    for (size_t i = 0; i < statCount; i++) {
        stacks[stats[i].name] = stats[i].stackHighWaterBytes;
    }

    if (doc.overflowed()) {
        LogBuffer::getInstance().logPrintf("WebServer: diagnostics JSON truncated — grow doc capacity\n");
    }

    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

// ---------------------------------------------------------------------------
// API: Config
// ---------------------------------------------------------------------------

void WebServerManager::handleApiGetConfig() {

    ConfigUpdate cfg;
    ConfigurationManager::getInstance().getCurrentConfig(cfg);

    StaticJsonDocument<896> doc;
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
    doc["hostname"] = cfg.hostname;
    doc["low_spool_threshold_g"] = cfg.low_spool_threshold_g;
    doc["bambu_dashboard"] = cfg.bambu_dashboard;
    doc["wifi_keep_awake"] = cfg.wifi_keep_awake;
    doc["u1_enabled"] = cfg.u1_enabled;
    doc["u1_channel"] = cfg.u1_channel;
    doc["u1_mode"] = cfg.u1_mode;
    doc["u1_auto_pick"] = cfg.u1_auto_pick;
    // led_pin: emit "" for the default sentinel so the web field shows empty
    if (cfg.led_pin == LED_PIN_DEFAULT) {
        doc["led_pin"] = "";
    } else {
        doc["led_pin"] = cfg.led_pin;
    }
    doc["tft_enabled"] = cfg.tft_enabled;
    doc["tft_driver"] = cfg.tft_driver;
    {
        // NFC pin overrides (#201): "" = board default; *_default carries the
        // effective default so the page can show it as the placeholder
        static const char* keys[6] = {"pin_nfc_rst","pin_nfc_nss","pin_nfc_busy",
                                      "pin_nfc_sck","pin_nfc_mosi","pin_nfc_miso"};
        auto& cm = ConfigurationManager::getInstance();
        for (int i = 0; i < 6; i++) {
            char defKey[24];
            snprintf(defKey, sizeof(defKey), "%s_default", keys[i]);
            if (cfg.nfc_pins[i] == LED_PIN_DEFAULT) doc[keys[i]] = "";
            else doc[keys[i]] = cfg.nfc_pins[i];
            doc[defKey] = cm.getNfcPin((NfcPinId)i);
        }
    }
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
    strncpy(update.tft_driver, doc["tft_driver"] | "st7789", sizeof(update.tft_driver) - 1);
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

    // Hostname: sanitize via shared helper (lowercase alphanum + hyphens, 1-32 chars)
    strncpy(update.hostname, doc["hostname"] | "spoolsense", sizeof(update.hostname) - 1);
    update.hostname[sizeof(update.hostname) - 1] = '\0';
    sanitizeHostname(update.hostname, sizeof(update.hostname));
    update.low_spool_threshold_g = doc["low_spool_threshold_g"] | (uint16_t)100;
    update.bambu_dashboard = doc["bambu_dashboard"] | 0;
    update.wifi_keep_awake = doc["wifi_keep_awake"] | 0;
    update.u1_enabled = doc["u1_enabled"] | (uint8_t)0;
    {
        uint8_t ch = doc["u1_channel"] | (uint8_t)0;
        update.u1_channel = (ch <= 3) ? ch : 0;  // clamp invalid client input
        uint8_t mode = doc["u1_mode"] | (uint8_t)0;
        update.u1_mode = (mode <= 1) ? mode : 0;
        update.u1_auto_pick = doc["u1_auto_pick"] | (uint8_t)1;
    }
    {
        // Sent as a string so empty (= board default) is distinguishable from GPIO 0.
        // Board-specific pin validation happens at the NVS boundary in saveToNVS.
        const char* ledPinStr = doc["led_pin"] | "";
        if (ledPinStr[0] == '\0') {
            update.led_pin = LED_PIN_DEFAULT;
        } else {
            // Strict parse: the whole string must be numeric, else fall back to
            // default — atoi("abc") would silently become GPIO 0.
            char* end = nullptr;
            long p = strtol(ledPinStr, &end, 10);
            bool numeric = (end != ledPinStr) && (*end == '\0');
            update.led_pin = (numeric && p >= 0 && p <= 254) ? (uint8_t)p : LED_PIN_DEFAULT;
        }
    }
    {
        // NFC pin overrides (#201): same string convention as led_pin
        static const char* keys[6] = {"pin_nfc_rst","pin_nfc_nss","pin_nfc_busy",
                                      "pin_nfc_sck","pin_nfc_mosi","pin_nfc_miso"};
        for (int i = 0; i < 6; i++) {
            const char* s = doc[keys[i]] | "";
            if (s[0] == '\0') {
                update.nfc_pins[i] = LED_PIN_DEFAULT;
                continue;
            }
            char* end = nullptr;
            long p = strtol(s, &end, 10);
            bool numeric = (end != s) && (*end == '\0');
            update.nfc_pins[i] = (numeric && p >= 0 && p <= 254) ? (uint8_t)p : LED_PIN_DEFAULT;
        }
    }

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

    // Wait for write queue to drain before restarting (#107)
    // Scan task must keep running to process queued writes
    for (int i = 0; i < 20 && !NFCManager::getInstance().isWriteQueueEmpty(); i++) {
        delay(100);
    }
    NFCManager::getInstance().pauseScanTask();
    delay(200);
    ESP.restart();
}

// ---------------------------------------------------------------------------
// API: Version
// ---------------------------------------------------------------------------

void WebServerManager::handleApiVersion() {
    StaticJsonDocument<128> doc;
    doc["version"] = FIRMWARE_VERSION;
    // Matches the PlatformIO env / release-asset naming for each target.
#if defined(BOARD_ESP32_C6)
    doc["board"] = "esp32c6";
#elif defined(BOARD_ESP32_C5)
    doc["board"] = "esp32c5";
#elif defined(BOARD_ESP32_C3)
    doc["board"] = "esp32c3";
#elif defined(BOARD_S3_DEVKITC)
    doc["board"] = "esp32s3devkitc";
#elif defined(BOARD_ESP32_S3)
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
    if (Update.hasError()) {
        NFCManager::getInstance().resumeScanTask();
        _server.send(500, "application/json", "{\"success\":false,\"error\":\"Update failed\"}");
    } else {
        _server.send(200, "application/json", "{\"success\":true}");
        Serial.println("OTA: Rebooting...");
        // Resume scan task briefly to drain any pending writes (#107)
        if (!NFCManager::getInstance().isWriteQueueEmpty()) {
            NFCManager::getInstance().resumeScanTask();
            for (int i = 0; i < 20 && !NFCManager::getInstance().isWriteQueueEmpty(); i++) {
                delay(100);
            }
            NFCManager::getInstance().pauseScanTask();
        }
        delay(200);
        ESP.restart();
    }
}

// ---------------------------------------------------------------------------
// API: Update from URL (ESP32 downloads .bin from GitHub)
// ---------------------------------------------------------------------------

void WebServerManager::handleApiUpdateFromUrl() {

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

    // OTA is locked to this project's GitHub releases. The update page only
    // ever posts browser_download_url values from the GitHub API, and the
    // HTTPS redirect target (objects.githubusercontent.com) is chosen by
    // GitHub, not the caller. Anything else gets rejected — without this,
    // anyone on the LAN could flash arbitrary firmware by URL.
    static const char OTA_ALLOWED_PREFIX[] = "https://github.com/SpoolSense/";
    if (strncmp(url, OTA_ALLOWED_PREFIX, sizeof(OTA_ALLOWED_PREFIX) - 1) != 0) {
        sendError(403, "OTA URL must be a SpoolSense GitHub release");
        return;
    }

    // Store URL and kick off background task
    strncpy(_otaUrl, url, sizeof(_otaUrl) - 1);
    _otaState = OtaState::DOWNLOADING;
    _otaProgress = 0;
    _otaError[0] = '\0';

    Serial.printf("OTA: Free heap before task: %u\n", ESP.getFreeHeap());
    BaseType_t created = createTaskWithAffinity(otaDownloadTask, "OTATask", 24576, this, 2, nullptr, 0);
    if (created != pdPASS) {
        _otaState = OtaState::IDLE;
        sendError(500, "Failed to start OTA task");
        return;
    }

    _server.send(200, "application/json", "{\"success\":true,\"status\":\"started\"}");
}

void WebServerManager::otaDownloadTask(void* param) {
    WebServerManager* self = static_cast<WebServerManager*>(param);

    Serial.printf("OTA: Downloading from %s\n", self->_otaUrl);

    // Pause NFC during OTA
    NFCManager::getInstance().pauseScanTask();

    // Stop display rendering and release any display buffers before the TLS
    // handshake (PSRAM boards free their persistent framebuffer; strip-
    // rendering boards reclaim an in-flight strip at most).
    if (self->_display) {
        self->_display->freeForOTA();
        Serial.printf("OTA: Free heap after display release: %u\n", ESP.getFreeHeap());
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

// ── Status serializers ──────────────────────────────────────

void WebServerManager::serializeTigerTagStatus(JsonDocument& doc) {
    TigerTagData tt;
    if (!NFCManager::getInstance().getLastTigerTagData(tt) || !tt.valid) return;

    JsonObject obj = doc.createNestedObject("tigertag");
    obj["material_id"] = tt.material_id;
    obj["material_name"] = tt.material_name;
    obj["brand_id"] = tt.brand_id;
    obj["brand_name"] = tt.brand_name;
    obj["weight_g"] = tt.weight_g;
    obj["diameter_mm"] = tt.diameter_mm;
    obj["aspect1_name"] = tt.aspect1_name;
    obj["aspect2_name"] = tt.aspect2_name;

    char colorHex[8];
    snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X", tt.color_r, tt.color_g, tt.color_b);
    obj["color_hex"] = colorHex;

    if (tt.nozzle_temp_min > 0) obj["nozzle_temp_min"] = tt.nozzle_temp_min;
    if (tt.nozzle_temp_max > 0) obj["nozzle_temp_max"] = tt.nozzle_temp_max;
    if (tt.bed_temp_min > 0) obj["bed_temp_min"] = tt.bed_temp_min;
    if (tt.bed_temp_max > 0) obj["bed_temp_max"] = tt.bed_temp_max;
    if (tt.dry_temp > 0) obj["dry_temp"] = tt.dry_temp;
    if (tt.dry_time_hours > 0) obj["dry_time_hours"] = tt.dry_time_hours;
}

void WebServerManager::serializeOpenTag3DStatus(JsonDocument& doc) {
    opentag3d_t ot3d;
    if (!NFCManager::getInstance().getLastOpenTag3DData(ot3d)) return;

    JsonObject obj = doc.createNestedObject("opentag3d");
    obj["tag_version"] = ot3d.tag_version;
    obj["base_material"] = ot3d.base_material;
    if (ot3d.material_modifiers[0]) obj["modifiers"] = ot3d.material_modifiers;
    obj["manufacturer"] = ot3d.manufacturer;
    if (ot3d.color_name[0]) obj["color_name"] = ot3d.color_name;

    char colorHex[8];
    snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X",
             ot3d.color_rgba[0][0], ot3d.color_rgba[0][1], ot3d.color_rgba[0][2]);
    obj["color_hex"] = colorHex;

    obj["target_weight_g"] = ot3d.target_weight_g;
    obj["diameter_mm"] = opentag3d_diameter_mm(&ot3d);
    if (ot3d.density_ugcm3 > 0) obj["density"] = opentag3d_density_gcc(&ot3d);

    uint16_t printTemp = (uint16_t)opentag3d_temp_c(ot3d.print_temp_encoded);
    uint16_t bedTemp = (uint16_t)opentag3d_temp_c(ot3d.bed_temp_encoded);
    if (printTemp > 0) obj["print_temp"] = printTemp;
    if (bedTemp > 0) obj["bed_temp"] = bedTemp;
    uint16_t chamberTemp = (uint16_t)opentag3d_temp_c(ot3d.chamber_temp_encoded);
    // v2 always carries the field (0 = no chamber, and the writer prefill must
    // be able to set 0); v1 has no chamber field, so omit it there.
    if (chamberTemp > 0 || opentag3d_major(ot3d.tag_version) >= 2) obj["chamber_temp"] = chamberTemp;

    if (ot3d.has_extended) {
        if (ot3d.sku[0]) obj["sku"] = ot3d.sku;
        if (ot3d.barcode > 0) {
            char bc[24];
            snprintf(bc, sizeof(bc), "%llu", (unsigned long long)ot3d.barcode);
            obj["barcode"] = bc;
        }
        if (ot3d.min_nozzle_diameter > 0) obj["min_nozzle_mm"] = ot3d.min_nozzle_diameter / 10.0f;
        if (ot3d.measured_filament_weight_g > 0) obj["measured_weight_g"] = ot3d.measured_filament_weight_g;
        if (ot3d.empty_spool_weight_g > 0) obj["empty_spool_g"] = ot3d.empty_spool_weight_g;
        if (ot3d.serial_number[0]) obj["serial_number"] = ot3d.serial_number;
        uint16_t minPrint = (uint16_t)opentag3d_temp_c(ot3d.min_print_temp_encoded);
        uint16_t maxPrint = (uint16_t)opentag3d_temp_c(ot3d.max_print_temp_encoded);
        uint16_t minBed = (uint16_t)opentag3d_temp_c(ot3d.min_bed_temp_encoded);
        uint16_t maxBed = (uint16_t)opentag3d_temp_c(ot3d.max_bed_temp_encoded);
        if (minPrint > 0) obj["min_print_temp"] = minPrint;
        if (maxPrint > 0) obj["max_print_temp"] = maxPrint;
        if (minBed > 0) obj["min_bed_temp"] = minBed;
        if (maxBed > 0) obj["max_bed_temp"] = maxBed;
        uint16_t dryTemp = (uint16_t)opentag3d_temp_c(ot3d.max_dry_temp_encoded);
        if (dryTemp > 0) obj["dry_temp"] = dryTemp;
        if (ot3d.dry_time_hours > 0) obj["dry_time_hours"] = ot3d.dry_time_hours;
    }
}

void WebServerManager::serializeOpenSpoolStatus(JsonDocument& doc) {
    OpenSpoolData os;
    if (!NFCManager::getInstance().getLastOpenSpoolData(os) || !os.valid) return;

    JsonObject obj = doc.createNestedObject("openspool");
    obj["brand"] = os.brand;
    obj["material"] = os.material;
    char colorHex[8];
    snprintf(colorHex, sizeof(colorHex), "#%s", os.color_hex);
    obj["color_hex"] = colorHex;
    obj["version"] = os.version;
    if (os.min_temp > 0) obj["min_temp"] = os.min_temp;
    if (os.max_temp > 0) obj["max_temp"] = os.max_temp;
}

void WebServerManager::serializeBambuTagStatus(JsonDocument& doc) {
    BambuTagData bt;
    if (!NFCManager::getInstance().getLastBambuTagData(bt) || !bt.valid) return;

    JsonObject obj = doc.createNestedObject("bambu");
    obj["filament_type"] = bt.filament_type;
    obj["material_variant"] = bt.material_variant;

    char colorHex[8];
    snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X", bt.color_r, bt.color_g, bt.color_b);
    obj["color_hex"] = colorHex;

    obj["weight_g"] = bt.weight_g;
    if (bt.diameter_mm > 0.0f) obj["diameter_mm"] = bt.diameter_mm;
    if (bt.hotend_min > 0) obj["hotend_min"] = bt.hotend_min;
    if (bt.hotend_max > 0) obj["hotend_max"] = bt.hotend_max;
    if (bt.bed_temp > 0) obj["bed_temp"] = bt.bed_temp;
    if (bt.drying_temp > 0) obj["drying_temp"] = bt.drying_temp;
    if (bt.drying_time > 0) obj["drying_time"] = bt.drying_time;
    if (bt.production_date[0]) obj["production_date"] = bt.production_date;
    if (bt.filament_length_m > 0) obj["filament_length_m"] = bt.filament_length_m;
}

void WebServerManager::serializeGenericUidStatus(JsonDocument& doc) {
    GenericTagSpoolInfo spoolInfo;
    NFCManager::getInstance().getGenericTagSpoolInfo(spoolInfo);
    if (!spoolInfo.valid) return;

    doc["material_name"] = spoolInfo.material_type;
    doc["manufacturer"] = spoolInfo.manufacturer;
    doc["color"] = spoolInfo.color_hex;
    doc["remaining_g"] = spoolInfo.remaining_weight_g;
    doc["spoolman_id"] = spoolInfo.spoolman_id;
    if (spoolInfo.extruder_temp > 0) doc["extruder_temp"] = spoolInfo.extruder_temp;
    if (spoolInfo.bed_temp > 0) doc["bed_temp"] = spoolInfo.bed_temp;
}

void WebServerManager::serializeOpenPrintTagStatus(JsonDocument& doc, const CurrentSpoolState& state) {
    if (!state.tag_data_valid) return;

    uint8_t mat_type = 0;
    opt_get_material_type(&state.tag_data, &mat_type);
    doc["material_type"] = mat_type;
    doc["material_name"] = materialTypeToString(mat_type);

    uint8_t color[4] = {0};
    if (opt_get_primary_color(&state.tag_data, color) == OPT_OK) {
        char colorHex[8];
        snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X", color[0], color[1], color[2]);
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

void WebServerManager::serializeEnrichment(JsonDocument& doc) {
    SmartTagEnrichment enrichment = ApplicationManager::getInstance().getSmartTagEnrichment();
    if (!enrichment.valid || doc.containsKey("spoolman")) return;

    JsonObject sp = doc.createNestedObject("spoolman");
    sp["spool_id"] = enrichment.spoolman_id;
    sp["remaining_g"] = enrichment.remaining_g;
    if (enrichment.bed_temp > 0) sp["bed_temp"] = enrichment.bed_temp;
    if (enrichment.extruder_temp > 0) sp["extruder_temp"] = enrichment.extruder_temp;
    if (enrichment.density > 0) sp["density"] = enrichment.density;
    if (enrichment.diameter_mm > 0) sp["diameter_mm"] = enrichment.diameter_mm;
}

// ── /api/status ─────────────────────────────────────────────

void WebServerManager::handleApiStatus() {

    CurrentSpoolState state;
    StaticJsonDocument<1536> doc;

    char deviceId[8];
    HomeAssistantManager::getDeviceId(deviceId, sizeof(deviceId));
    doc["device_id"] = deviceId;
    doc["firmware_version"] = FIRMWARE_VERSION;

    // U1 stage mode: surface the staged spool so the reader page can show
    // the channel picker with a live countdown
    {
        U1Manager::StagedState st = U1Manager::getInstance().getStagedState();
        if (st.active) {
            JsonObject staged = doc.createNestedObject("u1_staged");
            staged["remaining_ms"] = st.remainingMs;
            staged["vendor"] = st.vendor;
            staged["material"] = st.material;
            if (st.rgb >= 0) staged["rgb"] = st.rgb;
        } else {
            int8_t recent = U1Manager::getInstance().getRecentAssignChannel();
            if (recent >= 0) doc["u1_assigned"] = recent;
        }
    }

    if (NFCManager::getInstance().getCurrentSpoolState(state) && state.present) {
        doc["present"] = true;
        doc["uid"] = state.spool_id;
        doc["tag_data_valid"] = state.tag_data_valid;
        doc["tag_kind"] = tagKindToString(state.kind);
        if (state.variant != NtagVariant::Unknown) {
            doc["ntag_variant"] = ntagVariantName(state.variant);
            doc["ntag_pages"] = ntagUsablePages(state.variant);
        }

        switch (state.kind) {
            case TagKind::TigerTag:     serializeTigerTagStatus(doc); break;
            case TagKind::OpenTag3D:    serializeOpenTag3DStatus(doc); break;
            case TagKind::OpenSpoolTag: serializeOpenSpoolStatus(doc); break;
            case TagKind::GenericUidTag: serializeGenericUidStatus(doc); break;
            case TagKind::BambuTag:     serializeBambuTagStatus(doc); break;
            default:                    serializeOpenPrintTagStatus(doc, state); break;
        }

        serializeEnrichment(doc);
    } else {
        doc["present"] = false;
        doc["tag_data_valid"] = false;
    }

    serializeEnrichment(doc);

    String body;
    serializeJson(doc, body);
    _server.send(200, "application/json", body);
}

// ---------------------------------------------------------------------------
// API: Write OpenPrintTag
// ---------------------------------------------------------------------------

void WebServerManager::handleApiWriteTag() {
    Serial.println("WebServerManager: POST /api/write-tag received");

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
    if (uid[0] == '\0') {
        sendError(400, "uid required");
        return;
    }
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

    // Body: {"uid": "..."} — required, and the format is bound to that tag so
    // it cannot erase whichever tag happens to be on the scanner (#283).
    char uid[17] = {0};
    if (_server.hasArg("plain") && _server.arg("plain").length() > 2) {
        StaticJsonDocument<64> doc;
        if (deserializeJson(doc, _server.arg("plain")) == DeserializationError::Ok) {
            const char* u = doc["uid"] | "";
            strncpy(uid, u, sizeof(uid) - 1);
        }
    }

    if (uid[0] == '\0') {
        sendError(400, "uid required");
        return;
    }

    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = millis();
    req.type = NFCWriteType::FORMAT_NEW;
    strncpy(req.expected_spool_id, uid, sizeof(req.expected_spool_id) - 1);
    if (!NFCManager::getInstance().enqueueWrite(req)) {
        sendError(503, "Write queue full");
        return;
    }

    _server.send(200, "application/json", "{\"success\":true}");
}

// ---------------------------------------------------------------------------
// API: Write TigerTag
// ---------------------------------------------------------------------------

void WebServerManager::handleApiWriteTigerTag() {
    Serial.println("WebServerManager: POST /api/write-tigertag received");

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        sendError(400, "Invalid JSON");
        return;
    }

    const char* uid = doc["uid"] | "";
    if (uid[0] == '\0') {
        sendError(400, "uid required");
        return;
    }

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
    if (!NFCManager::getInstance().enqueueWrite(req)) {
        sendError(503, "Write queue full");
        return;
    }

    _server.send(200, "application/json", "{\"success\":true}");
}

// ---------------------------------------------------------------------------
// API: Write OpenTag3D
// ---------------------------------------------------------------------------

void WebServerManager::handleApiWriteOpenTag3D() {
    Serial.println("WebServerManager: POST /api/write-opentag3d received");

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        sendError(400, "Invalid JSON");
        return;
    }

    const char* uid = doc["uid"] | "";
    if (uid[0] == '\0') {
        sendError(400, "uid required");
        return;
    }

    opentag3d_t ot3d;
    memset(&ot3d, 0, sizeof(ot3d));

    ot3d.tag_version = doc["tag_version"] | (uint16_t)OT3D_SUPPORTED_VERSION;

    if (!opentag3d_can_encode(ot3d.tag_version)) {
        sendError(400, "Unsupported tag_version — this firmware writes v1.000 and v2.000");
        return;
    }

    // Never re-mint a newer-minor tag: the queued struct carries OUR version
    // stamp, so the encoder's write-time guard cannot see the conflict — the
    // tag on the reader is the only evidence. (The cache clears on removal.)
    opentag3d_t onReader;
    if (NFCManager::getInstance().getLastOpenTag3DData(onReader) &&
        !opentag3d_can_encode(onReader.tag_version)) {
        sendError(409, "Tag carries a newer OpenTag3D revision — rewriting would lose its data");
        return;
    }

    // A v2 NDEF is 252 padded bytes = 63 pages starting at page 4, so user
    // memory must reach page 67. Refuse up front when the tag on the reader is
    // known too small or cannot be identified — otherwise the queue accepts it,
    // the capacity check rejects it later on the NFC task, and the browser only
    // sees a verify timeout.
    if (opentag3d_major(ot3d.tag_version) >= 2) {
        CurrentSpoolState cur;
        if (NFCManager::getInstance().getCurrentSpoolState(cur) && cur.present) {
            uint16_t endPage = ntagUserMemoryEnd(cur.variant);
            if (endPage > 0 && endPage < 67) {
                sendError(400, "Tag too small for OpenTag3D v2.000 — needs NTAG215 or larger");
                return;
            }
            if (endPage == 0) {
                sendError(400, "Cannot verify tag size (unknown tag type) — remove and re-place the tag, then retry");
                return;
            }
        }
    }

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

    const char* sku = doc["sku"] | "";
    strncpy(ot3d.sku, sku, sizeof(ot3d.sku) - 1);

    if (doc["barcode"].is<const char*>()) {
        const char* bcStr = doc["barcode"] | "";
        char* bcEnd = NULL;
        ot3d.barcode = strtoull(bcStr, &bcEnd, 10);
        if (bcStr[0] != '\0' && (bcEnd == NULL || *bcEnd != '\0')) {
            sendError(400, "Barcode must be digits only");
            return;
        }
    } else {
        ot3d.barcode = doc["barcode"] | (uint64_t)0;  // numeric JSON from scripts/HA
    }
    if (ot3d.barcode > 99999999999999ULL) {  // GTIN caps at 14 digits (also under the 6-byte field max)
        sendError(400, "Barcode exceeds the 14-digit GTIN range");
        return;
    }

    uint16_t chamberTemp = doc["chamber_temp_c"] | (uint16_t)0;
    if (chamberTemp > 1275) {  // encoded max is 255 * 5 — beyond that the cast wraps
        sendError(400, "Chamber temperature exceeds 1275");
        return;
    }
    ot3d.chamber_temp_encoded = (uint8_t)(chamberTemp / 5);

    ot3d.min_nozzle_diameter = doc["min_nozzle_diameter"] | (uint8_t)0;

    // Always parse these — the old two-key gate silently dropped any of them
    // that arrived without serial_number/min_print_temp_c, and a v2 encode
    // writes the full 224-byte map either way.
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

    // has_extended only sizes v1 encodes (v2 always writes the full map).
    // Derive it from the parsed data, not key probes — an explicit v1 post
    // carrying only a URL or dry profile must still get the extended layout.
    ot3d.has_extended = (opentag3d_major(ot3d.tag_version) >= 2) ||
                        ot3d.serial_number[0] || ot3d.online_url[0] ||
                        ot3d.manufacture_year || ot3d.empty_spool_weight_g ||
                        ot3d.measured_filament_weight_g || ot3d.measured_filament_length_m ||
                        ot3d.max_dry_temp_encoded || ot3d.dry_time_hours ||
                        ot3d.min_print_temp_encoded || ot3d.max_print_temp_encoded ||
                        ot3d.min_bed_temp_encoded || ot3d.max_bed_temp_encoded ||
                        ot3d.min_volumetric_speed || ot3d.max_volumetric_speed ||
                        ot3d.target_volumetric_speed;

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
// API: Write OpenSpool
// ---------------------------------------------------------------------------

void WebServerManager::handleApiWriteOpenSpool() {
    Serial.println("WebServerManager: POST /api/write-openspool received");

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        sendError(400, "Invalid JSON");
        return;
    }

    const char* uid = doc["uid"] | "";
    if (uid[0] == '\0') {
        sendError(400, "uid required");
        return;
    }

    // Build the tag payload using ArduinoJson to properly escape all values
    StaticJsonDocument<256> tagDoc;
    tagDoc["protocol"] = doc["protocol"] | "openspool";
    tagDoc["version"] = doc["version"] | "1.0";
    tagDoc["type"] = doc["type"] | "PLA";
    tagDoc["color_hex"] = doc["color_hex"] | "FF0000";
    tagDoc["brand"] = doc["brand"] | "";
    tagDoc["min_temp"] = doc["min_temp"] | "210";
    tagDoc["max_temp"] = doc["max_temp"] | "230";

    char jsonPayload[256];
    size_t jsonLen = serializeJson(tagDoc, jsonPayload, sizeof(jsonPayload));
    if (jsonLen == 0 || jsonLen >= sizeof(jsonPayload)) {
        sendError(400, "Payload too large");
        return;
    }

    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = NFCManager::getInstance().generateRequestId();
    req.type = NFCWriteType::WRITE_OPENSPOOL;
    // Bind the write to the UID the page detected when Write was pressed,
    // exactly as the TigerTag and OpenTag3D handlers do. Without it
    // validateWriteUid() accepts whatever tag happens to be present, so a tag
    // swapped in after that detection receives this payload.
    strncpy(req.expected_spool_id, uid, sizeof(req.expected_spool_id) - 1);

    if (!NFCManager::getInstance().enqueueRawWrite(req, (const uint8_t*)jsonPayload, (size_t)jsonLen)) {
        sendError(503, "Write queue full or busy");
        return;
    }

    _server.send(200, "application/json", "{\"success\":true}");
}

// ---------------------------------------------------------------------------
// API: Spoolman enrichment — find-vendor, find-filament, save-enrichment
// ---------------------------------------------------------------------------

void WebServerManager::handleApiSpoolmanFindVendor() {
    String name = _server.arg("name");
    if (name.isEmpty()) {
        _server.send(400, "application/json", "{\"error\":\"name required\"}");
        return;
    }

    if (!SpoolmanManager::getInstance().isConfigured()) {
        _server.send(503, "application/json", "{\"error\":\"Spoolman not configured\"}");
        return;
    }

    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();

    if (xSemaphoreTake(g_httpMutex, HTTP_MUTEX_TIMEOUT) != pdTRUE) {
        sendError(503, "Busy — try again");
        return;
    }

    char matchedName[64];
    int vendorId = SpoolmanManager::getInstance().findVendorNoLock(name.c_str(), matchedName, sizeof(matchedName));
    xSemaphoreGive(g_httpMutex);

    if (vendorId == -2) {
        // Transient lookup failure — a false "not found" would steer the page
        // toward creating a duplicate vendor
        _server.send(503, "application/json", "{\"error\":\"Spoolman lookup failed\"}");
        return;
    }
    if (vendorId >= 0) {
        StaticJsonDocument<128> result;
        result["found"] = true;
        result["id"] = vendorId;
        result["name"] = matchedName;
        String out;
        serializeJson(result, out);
        _server.send(200, "application/json", out);
        return;
    }
    _server.send(200, "application/json", "{\"found\":false}");
}

void WebServerManager::handleApiSpoolmanFindFilament() {
    String vendorId = _server.arg("vendor_id");
    String material = _server.arg("material");
    String colorHex = _server.arg("color_hex");

    if (vendorId.isEmpty() || material.isEmpty()) {
        _server.send(400, "application/json", "{\"error\":\"vendor_id and material required\"}");
        return;
    }

    if (!SpoolmanManager::getInstance().isConfigured()) {
        _server.send(503, "application/json", "{\"error\":\"Spoolman not configured\"}");
        return;
    }

    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();

    if (xSemaphoreTake(g_httpMutex, HTTP_MUTEX_TIMEOUT) != pdTRUE) {
        sendError(503, "Busy — try again");
        return;
    }

    // Stream-match the filament list (constant memory), then fetch just the
    // matched filament (~700B) for the response payload. Empty color = wildcard.
    const char* colorCmp = colorHex.c_str();
    if (colorCmp[0] == '#') colorCmp++;
    int filamentId = SpoolmanManager::getInstance().findFilamentNoLock(
        atoi(vendorId.c_str()), material.c_str(), colorCmp, "");

    if (filamentId == -2) {
        xSemaphoreGive(g_httpMutex);
        // Transient failure — a false "not found" would steer the page toward
        // creating a duplicate filament
        _server.send(503, "application/json", "{\"error\":\"Spoolman lookup failed\"}");
        return;
    }
    if (filamentId < 0) {
        xSemaphoreGive(g_httpMutex);
        _server.send(200, "application/json", "{\"found\":false}");
        return;
    }

    WiFiClient client;
    HTTPClient http;
    char url[256];
    snprintf(url, sizeof(url), "%s/api/v1/filament/%d", baseUrl, filamentId);
    http.begin(client, url);
    http.setTimeout(5000);
    int code = http.GET();
    String resp = (code == 200) ? http.getString() : String();
    http.end();
    xSemaphoreGive(g_httpMutex);

    DynamicJsonDocument doc(2048);
    if (code != 200 || deserializeJson(doc, resp)) {
        _server.send(503, "application/json", "{\"error\":\"Spoolman lookup failed\"}");
        return;
    }

    StaticJsonDocument<256> result;
    result["found"] = true;
    result["id"] = filamentId;
    result["name"] = doc["name"] | "";
    result["material"] = doc["material"] | "";
    result["color_hex"] = doc["color_hex"] | "";
    String out;
    serializeJson(result, out);
    _server.send(200, "application/json", out);
}

// ── Enrichment save helpers ─────────────────────────────────

// Search Spoolman for a vendor by name, create if not found.
// confirmedId: -1 = search needed, -2 = user declined match, >0 = already confirmed
int WebServerManager::enrichFindOrCreateVendor(WiFiClient& client, HTTPClient& http,
                                                const char* baseUrl, const char* manufacturer, int confirmedId) {
    // confirmedId >= 0: user confirmed an existing vendor. -2: user explicitly
    // declined the suggested match — skip the search and create fresh (#218).
    // -1: no confirmation dialog happened — search then create.
    if (confirmedId >= 0 || manufacturer[0] == '\0') return (confirmedId >= 0) ? confirmedId : -1;
    bool declinedMatch = (confirmedId == -2);

    char url[256];
    int vendorId = -1;
    if (!declinedMatch) {
        vendorId = SpoolmanManager::getInstance().findVendorNoLock(manufacturer);
        if (vendorId == -2) return -2;  // lookup failed — caller must not create
        if (vendorId >= 0) return vendorId;
    }

    StaticJsonDocument<128> vBody;
    vBody["name"] = manufacturer;
    String vJson;
    serializeJson(vBody, vJson);
    snprintf(url, sizeof(url), "%s/api/v1/vendor", baseUrl);
    http.begin(client, url);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(vJson);
    if (code == 200 || code == 201) {
        String response = http.getString();
        StaticJsonDocument<256> vResp;
        if (!deserializeJson(vResp, response)) vendorId = vResp["id"] | -1;
    }
    http.end();
    return vendorId;
}

// Search Spoolman filaments by vendor, match material+color client-side (#92).
// Creates a new filament if no match found.
int WebServerManager::enrichFindOrCreateFilament(WiFiClient& client, HTTPClient& http,
                                                   const char* baseUrl, const char* material, const char* colorHex,
                                                   int vendorId, float density, float diameter,
                                                   int bedTemp, int nozzleTemp, int confirmedId) {
    // confirmedId >= 0: user confirmed an existing filament. -2: user explicitly
    // declined the suggested match — skip the search and create fresh (#218).
    // -1: no confirmation dialog happened — search then create.
    if (confirmedId >= 0 || material[0] == '\0') return (confirmedId >= 0) ? confirmedId : -1;
    bool declinedMatch = (confirmedId == -2);

    char url[256];
    int filamentId = -1;

    // Streaming search — constant memory, replaces an 8KB DOM + full-body String
    // (#92 client-side matching preserved). When the vendor is unknown, search
    // unfiltered rather than skipping: blind-creating here made vendorless
    // duplicates that ping-ponged with the sync path (#218). Caller holds
    // g_httpMutex, which the NoLock search requires.
    if (!declinedMatch) {
        const char* colorCmp = colorHex;
        if (colorCmp[0] == '#') colorCmp++;
        // Empty color = wildcard, matching the old DOM matcher's behavior
        filamentId = SpoolmanManager::getInstance().findFilamentNoLock(vendorId, material, colorCmp, material);
        if (filamentId == -2) return -2;  // lookup failed — caller must not create
        if (filamentId >= 0) return filamentId;
    }

    // Create new filament
    StaticJsonDocument<512> fBody;
    fBody["name"] = material;
    fBody["material"] = material;
    if (vendorId > 0) fBody["vendor_id"] = vendorId;
    if (density > 0) fBody["density"] = density;
    fBody["diameter"] = diameter;
    if (colorHex[0] != '\0') {
        const char* ch = colorHex;
        if (ch[0] == '#') ch++;
        fBody["color_hex"] = ch;
    }
    if (bedTemp > 0) fBody["settings_bed_temp"] = bedTemp;
    if (nozzleTemp > 0) fBody["settings_extruder_temp"] = nozzleTemp;
    String fJson;
    serializeJson(fBody, fJson);
    snprintf(url, sizeof(url), "%s/api/v1/filament", baseUrl);
    http.begin(client, url);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(fJson);
    if (code == 200 || code == 201) {
        String response = http.getString();
        StaticJsonDocument<256> fResp;
        if (!deserializeJson(fResp, response)) filamentId = fResp["id"] | -1;
    }
    http.end();
    return filamentId;
}

// Fetch all spools and match nfc_id client-side — Spoolman's extra_field filter is unreliable
int WebServerManager::enrichFindSpoolByUid(WiFiClient& client, HTTPClient& http,
                                            const char* baseUrl, const char* uid, float& outInitialWeight,
                                            String& outExtraJson, String& outFilamentMaterial,
                                            String& outFilamentColor) {
    outInitialWeight = 0.0f;
    outExtraJson = "";
    outFilamentMaterial = "";
    outFilamentColor = "";

    // Identity via THE resolver (#224): nfc_id match, then validated cache —
    // one precedence shared with sync/deduction/reader. The single-spool GET
    // below (~1KB) supplies the enrichment fields. Caller holds g_httpMutex.
    // Return contract: id >= 0 found; -1 no active spool (creating is correct);
    // -2 lookup failed (transient) — callers must abort, NOT create (#218 family)
    SpoolmanManager::SpoolResolution r = SpoolmanManager::getInstance().resolveSpoolByUidNoLock(uid);
    if (r.lookupFailed) return -2;
    if (r.spoolId < 0) return -1;
    int spoolId = r.spoolId;

    char url[256];
    snprintf(url, sizeof(url), "%s/api/v1/spool/%d", baseUrl, spoolId);
    http.begin(client, url);
    http.setTimeout(5000);
    int code = http.GET();
    if (code != 200) {
        http.end();
        return -2;
    }
    String response = http.getString();
    http.end();

    DynamicJsonDocument sDoc(3072);
    if (deserializeJson(sDoc, response)) return -2;
    if (sDoc["archived"] | false) return -1;  // search excludes archived, but a race is possible

    outInitialWeight = sDoc["initial_weight"] | 0.0f;
    // Current filament identity — lets the caller skip re-pointing filament_id
    // when the resolved filament is semantically the same (#218)
    outFilamentMaterial = (const char*)(sDoc["filament"]["material"] | "");
    outFilamentColor    = (const char*)(sDoc["filament"]["color_hex"] | "");
    // Capture existing extras — Spoolman PATCH replaces the whole extra map,
    // so any update that touches extras must merge
    serializeJson(sDoc["extra"], outExtraJson);
    return spoolId;
}

// PATCH existing spool — Spoolman uses used_weight (not remaining_weight) for weight tracking
bool WebServerManager::enrichUpdateSpool(WiFiClient& client, HTTPClient& http, const char* baseUrl,
                                           int spoolId, int filamentId, float remainingG, float existingInitialWeight,
                                           const char* existingExtraJson, const char* tagFormat) {
    char url[256];
    StaticJsonDocument<512> patch;
    // filamentId < 0 means "leave the spool's filament alone" — the caller
    // determined the resolved filament is semantically the same (#218)
    if (filamentId >= 0) {
        patch["filament_id"] = filamentId;
    }

    // Only touch extras when writing tag_format. Spoolman PATCH replaces the
    // entire extra map, so the existing extras (nfc_id, middleware fields like
    // MMU Gate) must be carried over or they get wiped. If the existing extras
    // fail to parse (or overflow the buffer), skip the extras portion entirely
    // rather than PATCHing a partial map — losing tag_format is harmless,
    // wiping nfc_id is not.
    StaticJsonDocument<768> existingExtra;
    if (tagFormat[0] != '\0') {
        DeserializationError extraErr = DeserializationError::Ok;
        if (existingExtraJson[0] != '\0') {
            extraErr = deserializeJson(existingExtra, existingExtraJson);
        }
        if (extraErr == DeserializationError::Ok) {
            char fmtJson[20];
            snprintf(fmtJson, sizeof(fmtJson), "\"%s\"", tagFormat);
            existingExtra["tag_format"] = fmtJson;
            patch["extra"] = existingExtra;
        } else {
            Serial.printf("WebServer: existing extras unparseable (%s) — skipping tag_format write\n",
                          extraErr.c_str());
        }
    }
    if (remainingG > 0) {
        float initialW = existingInitialWeight > 0 ? existingInitialWeight : 1000.0f;
        float usedW = initialW - remainingG;
        if (usedW < 0) usedW = 0;
        patch["initial_weight"] = initialW;
        patch["used_weight"] = usedW;
    }
    String pJson;
    serializeJson(patch, pJson);
    snprintf(url, sizeof(url), "%s/api/v1/spool/%d", baseUrl, spoolId);
    http.begin(client, url);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    int code = http.PATCH(pJson);
    http.end();
    return (code >= 200 && code < 300);
}

int WebServerManager::enrichCreateSpool(WiFiClient& client, HTTPClient& http, const char* baseUrl,
                                          int filamentId, float remainingG, const char* quotedUid,
                                          const char* tagFormat) {
    char url[256];
    StaticJsonDocument<512> sBody;
    sBody["filament_id"] = filamentId;
    float initialW = 1000.0f;
    sBody["initial_weight"] = initialW;
    if (remainingG > 0) {
        float usedW = initialW - remainingG;
        if (usedW < 0) usedW = 0;
        sBody["used_weight"] = usedW;
    }
    JsonObject extra = sBody.createNestedObject("extra");
    extra["nfc_id"] = quotedUid;
    char fmtJson[20];
    if (tagFormat[0] != '\0') {
        snprintf(fmtJson, sizeof(fmtJson), "\"%s\"", tagFormat);
        extra["tag_format"] = fmtJson;
    }
    String sJson;
    serializeJson(sBody, sJson);
    snprintf(url, sizeof(url), "%s/api/v1/spool", baseUrl);
    http.begin(client, url);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(sJson);
    int spoolId = -1;
    if (code == 200 || code == 201) {
        String response = http.getString();
        StaticJsonDocument<256> sResp;
        if (!deserializeJson(sResp, response)) spoolId = sResp["id"] | -1;
    }
    http.end();
    return spoolId;
}

// ── /api/spoolman/save-enrichment ───────────────────────────

void WebServerManager::handleApiSpoolmanSaveEnrichment() {

    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, _server.arg("plain"))) {
        _server.send(400, "application/json", "{\"error\":\"bad JSON\"}");
        return;
    }
    if (!SpoolmanManager::getInstance().isConfigured()) {
        _server.send(503, "application/json", "{\"error\":\"Spoolman not configured\"}");
        return;
    }

    const char* uid          = doc["uid"]          | "";
    const char* manufacturer = doc["manufacturer"] | "";
    const char* material     = doc["material"]     | "";
    const char* colorHex     = doc["color_hex"]    | "";
    float diameter   = doc["diameter_mm"]  | 1.75f;
    if (diameter <= 0) diameter = 1.75f;
    float density    = doc["density"]      | 0.0f;
    float remainingG = doc["remaining_g"]  | 0.0f;
    int   bedTemp    = doc["bed_temp"]     | 0;
    int   nozzleTemp = doc["nozzle_temp"]  | 0;
    int   confirmedVendorId   = doc["vendor_id"]   | -1;
    int   confirmedFilamentId = doc["filament_id"] | -1;

    // tag_format: lowercase alnum + underscore, canonical values from
    // tagKindToMqttFormat (openprinttag, tigertag, opentag3d, openspool, ...)
    char tagFormat[16] = "";
    {
        const char* tf = doc["tag_format"] | "";
        size_t n = 0;
        for (; tf[n] != '\0' && n < sizeof(tagFormat) - 1; n++) {
            char c = tf[n];
            bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            if (!ok) { n = 0; break; }
            tagFormat[n] = c;
        }
        tagFormat[n] = '\0';
    }

    if (uid[0] == '\0') {
        _server.send(400, "application/json", "{\"error\":\"uid required\"}");
        return;
    }

    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    if (xSemaphoreTake(g_httpMutex, HTTP_MUTEX_TIMEOUT) != pdTRUE) {
        sendError(503, "Busy — try again");
        return;
    }

    WiFiClient client;
    HTTPClient http;

    int vendorId = enrichFindOrCreateVendor(client, http, baseUrl, manufacturer, confirmedVendorId);
    if (vendorId == -2) {
        xSemaphoreGive(g_httpMutex);
        _server.send(503, "application/json", "{\"error\":\"Spoolman lookup failed — try again\"}");
        return;
    }
    int filamentId = enrichFindOrCreateFilament(client, http, baseUrl, material, colorHex,
                                                 vendorId, density, diameter, bedTemp, nozzleTemp, confirmedFilamentId);
    if (filamentId < 0) {
        xSemaphoreGive(g_httpMutex);
        _server.send(500, "application/json", "{\"error\":\"filament create failed\"}");
        return;
    }

    char quotedUid[130];
    snprintf(quotedUid, sizeof(quotedUid), "\"%s\"", uid);
    float existingInitialWeight = 0.0f;
    String existingExtraJson, existingFilMaterial, existingFilColor;
    int spoolId = enrichFindSpoolByUid(client, http, baseUrl, uid, existingInitialWeight, existingExtraJson,
                                       existingFilMaterial, existingFilColor);
    if (spoolId == -2) {
        // Lookup failed (transport/parse) — creating here would duplicate an
        // existing spool we simply couldn't see (#218 family)
        xSemaphoreGive(g_httpMutex);
        _server.send(503, "application/json", "{\"error\":\"Spoolman lookup failed — try again\"}");
        return;
    }

    if (spoolId > 0) {
        // Don't re-point the spool's filament when its current one already matches
        // the requested material+color — dedup can resolve a different id for the
        // same physical filament, and re-pointing churns with the sync path (#218)
        int patchFilamentId = filamentId;
        {
            const char* ec = existingFilColor.c_str();
            if (ec[0] == '#') ec++;
            const char* rc = colorHex;
            if (rc[0] == '#') rc++;
            bool sameMaterial = existingFilMaterial.length() > 0 &&
                                strcasecmp(existingFilMaterial.c_str(), material) == 0;
            bool sameColor = (rc[0] == '\0') ||
                             (strlen(ec) >= 6 && strncasecmp(ec, rc, 6) == 0);
            if (sameMaterial && sameColor) {
                patchFilamentId = -1;
            }
        }
        if (!enrichUpdateSpool(client, http, baseUrl, spoolId, patchFilamentId, remainingG, existingInitialWeight,
                               existingExtraJson.c_str(), tagFormat)) {
            xSemaphoreGive(g_httpMutex);
            _server.send(500, "application/json", "{\"error\":\"spool update failed\"}");
            return;
        }
    } else {
        spoolId = enrichCreateSpool(client, http, baseUrl, filamentId, remainingG, quotedUid, tagFormat);
    }

    StaticJsonDocument<128> result;
    result["success"] = spoolId > 0;
    result["spool_id"] = spoolId;
    result["filament_id"] = filamentId;
    String out;
    serializeJson(result, out);
    xSemaphoreGive(g_httpMutex);
    _server.send(200, "application/json", out);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void WebServerManager::sendError(int code, const char* msg) {
    // Escape quotes, backslashes, and control chars for valid JSON
    char escaped[192];
    size_t j = 0;
    for (size_t i = 0; msg[i] && j < sizeof(escaped) - 6; i++) {
        char c = msg[i];
        if (c == '"' || c == '\\') {
            escaped[j++] = '\\'; escaped[j++] = c;
        } else if (c == '\n') {
            escaped[j++] = '\\'; escaped[j++] = 'n';
        } else if (c == '\r') {
            escaped[j++] = '\\'; escaped[j++] = 'r';
        } else if (c == '\t') {
            escaped[j++] = '\\'; escaped[j++] = 't';
        } else if ((unsigned char)c < 0x20) {
            j += snprintf(escaped + j, sizeof(escaped) - j, "\\u%04X", (unsigned char)c);
        } else {
            escaped[j++] = c;
        }
    }
    escaped[j] = '\0';
    char body[192];
    snprintf(body, sizeof(body), "{\"success\":false,\"error\":\"%s\"}", escaped);
    _server.send(code, "application/json", body);
}

#endif // NATIVE_TEST
