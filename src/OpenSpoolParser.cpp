#include "OpenSpoolParser.h"
#include <ArduinoJson.h>
#include <cstring>

// Deserialize OpenSpool NDEF JSON payload into typed struct for rapid access
bool parseOpenSpool(const uint8_t* json, size_t len, OpenSpoolData& out) {
    memset(&out, 0, sizeof(out));
    out.valid = false;

    if (!json || len == 0) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) return false;

    const char* protocol = doc["protocol"] | "";
    if (strcmp(protocol, "openspool") != 0) return false;  // reject non-openspool payloads early

    strncpy(out.protocol, protocol, sizeof(out.protocol) - 1);
    strncpy(out.version, doc["version"] | "1.0", sizeof(out.version) - 1);
    strncpy(out.material, doc["type"] | "", sizeof(out.material) - 1);
    strncpy(out.color_hex, doc["color_hex"] | "", sizeof(out.color_hex) - 1);
    strncpy(out.brand, doc["brand"] | "", sizeof(out.brand) - 1);

    const char* minT = doc["min_temp"] | "0";
    const char* maxT = doc["max_temp"] | "0";
    out.min_temp = (int16_t)atoi(minT);
    out.max_temp = (int16_t)atoi(maxT);

    out.valid = true;
    return true;
}
