#include "SpoolmanManager.h"
#include "TaskUtils.h"
#include "ConfigurationManager.h"
#include "ApplicationManager.h"
#include "MemoryDiagnostics.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <json.hpp>
#include "JsonPullHelpers.h"

#include <Arduino.h>
#ifndef NATIVE_TEST
#include <Preferences.h>
#endif
#include <cmath>
#include "openprinttag_lib.h"
#include "LogBuffer.h"

static constexpr size_t JSON_SMALL_CAPACITY = 256;
static constexpr size_t JSON_MEDIUM_CAPACITY = 768;
static constexpr size_t JSON_LARGE_CAPACITY = 2048;

using namespace io;
using namespace json;

static bool readIntValue(json_reader& reader, int& outValue) {
    if (reader.node_type() != json_node_type::value) {
        return false;
    }
    if (reader.value_type() == json_value_type::integer) {
        outValue = static_cast<int>(reader.value_int());
        return true;
    }
    if (reader.value_type() == json_value_type::real) {
        outValue = static_cast<int>(reader.value_real());
        return true;
    }
    return false;
}


static bool readStringValue(json_reader& reader, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) {
        return false;
    }
    out[0] = '\0';
    json_node_type node = reader.node_type();
    if (node != json_node_type::value &&
        node != json_node_type::value_part &&
        node != json_node_type::end_value_part) {
        return false;
    }

    size_t written = 0;
    auto append = [&]() {
        const char* v = reader.value();
        if (v == nullptr) return;
        while (*v != '\0' && written + 1 < outSize) {
            out[written++] = *v++;
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

static bool parseIdFromObject(const char* jsonText, int& outId) {
    outId = -1;
    const_buffer_stream stm((const uint8_t*)jsonText, strlen(jsonText));
    json_reader reader(stm);
    while (reader.read()) {
        if (reader.node_type() != json_node_type::field) continue;
        if (strcmp(reader.value(), "id") != 0) continue;
        if (reader.read() && readIntValue(reader, outId)) {
            return true;
        }
    }
    return false;
}

static bool parseFirstArrayItemId(const char* jsonText, int& outId) {
    outId = -1;
    const_buffer_stream stm((const uint8_t*)jsonText, strlen(jsonText));
    json_reader reader(stm);

    while (reader.read()) {
        if (reader.node_type() != json_node_type::object) continue;
        const unsigned objectDepth = reader.depth();
        while (reader.read()) {
            if (reader.node_type() == json_node_type::end_object && reader.depth() == objectDepth) {
                break;
            }
            if (reader.node_type() != json_node_type::field) continue;
            if (strcmp(reader.value(), "id") != 0) continue;
            if (reader.read() && readIntValue(reader, outId)) {
                return true;
            }
        }
    }
    return false;
}


// --- File-local HTTP helpers ---
// Persistent client + http objects — reuse TCP connection across requests.
// All Spoolman calls are serialized by httpMutex_ so no concurrent access.
// begin() internally calls end() + resets headers. setReuse(true) keeps TCP alive.
static WiFiClient spoolmanClient;
static HTTPClient spoolmanHttp;

static int httpGet(const char* path, String& response) {
    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    char url[256];
    snprintf(url, sizeof(url), "%s%s", baseUrl, path);
    spoolmanHttp.begin(spoolmanClient, url);
    spoolmanHttp.setReuse(false);
    int code = spoolmanHttp.GET();
    if (code > 0) {
        response = spoolmanHttp.getString();
    }
    spoolmanHttp.end();
    return code;
}

static int httpPost(const char* path, const char* body, String& response) {
    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    char url[256];
    snprintf(url, sizeof(url), "%s%s", baseUrl, path);
    spoolmanHttp.begin(spoolmanClient, url);
    spoolmanHttp.setReuse(false);
    spoolmanHttp.addHeader("Content-Type", "application/json");
    int code = spoolmanHttp.POST(body);
    if (code > 0) {
        response = spoolmanHttp.getString();
    }
    spoolmanHttp.end();
    return code;
}

static int httpPatch(const char* path, const char* body, String& response) {
    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    char url[256];
    snprintf(url, sizeof(url), "%s%s", baseUrl, path);
    spoolmanHttp.begin(spoolmanClient, url);
    spoolmanHttp.setReuse(false);
    spoolmanHttp.addHeader("Content-Type", "application/json");
    int code = spoolmanHttp.PATCH(body);
    if (code > 0) {
        response = spoolmanHttp.getString();
    }
    spoolmanHttp.end();
    return code;
}

// Streaming spool search by nfc_id over the given path (which may carry a
// query, e.g. "?filament.id=N"). Pull-parses the HTTP stream with constant
// memory — replaces an ArduinoJson filter parse whose filtered document still
// grew with spool count. Archived spools are skipped and the highest matching
// id wins, matching the filter version. Returns id >= 0 match, -1 not found,
// -2 transport/parse failure — callers must NOT create on -2.
static int streamFindSpoolByNfcId(const char* path, const char* uuid) {
    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    char url[256];
    snprintf(url, sizeof(url), "%s%s", baseUrl, path);

    WiFiClient streamClient;
    HTTPClient streamHttp;
    streamHttp.useHTTP10(true);
    streamHttp.begin(streamClient, url);
    streamHttp.setTimeout(10000);
    int code = streamHttp.GET();
    if (code != 200) {
        Serial.printf("SpoolmanManager: streamFind HTTP %d for %s\n", code, path);
        streamHttp.end();
        return -2;
    }

    HttpClientStream stm(*streamHttp.getStreamPtr());
    json_reader reader(stm);

    // nfc_id is stored double-quoted in Spoolman: "\"UUID\"" — compare both forms
    char quotedUuid[130];
    snprintf(quotedUuid, sizeof(quotedUuid), "\"%s\"", uuid);

    int bestMatchId = -1;
    bool sawAnyNode = false;
    bool parseError = false;
    bool docComplete = false;  // saw the outer array close — reader stops silently on malformed JSON
    bool inElement = false;
    int nestLevel = 0;   // containers nested INSIDE the current element
    bool inExtra = false;  // directly inside the element's top-level "extra" object
    int curId = -1;
    bool curArchived = false;
    char curNfcId[130];
    curNfcId[0] = '\0';

    while (reader.read()) {
        sawAnyNode = true;
        json_node_type nt = reader.node_type();
        if (nt == json_node_type::error) { parseError = true; break; }

        if (!inElement) {
            if (nt == json_node_type::object) {
                inElement = true;
                nestLevel = 0;
                inExtra = false;
                curId = -1;
                curArchived = false;
                curNfcId[0] = '\0';
            } else if (nt == json_node_type::end_array) {
                docComplete = true;
            }
            continue;
        }

        if (nt == json_node_type::object || nt == json_node_type::array) {
            nestLevel++;
            continue;
        }
        if (nt == json_node_type::end_object || nt == json_node_type::end_array) {
            if (nestLevel > 0) {
                nestLevel--;
                if (nestLevel == 0) inExtra = false;
                continue;
            }
            // Element complete — evaluate
            if (!curArchived && curId >= 0 &&
                (strcasecmp(curNfcId, uuid) == 0 || strcasecmp(curNfcId, quotedUuid) == 0)) {
                if (curId > bestMatchId) bestMatchId = curId;
            }
            inElement = false;
            continue;
        }

        if (nt == json_node_type::field) {
            char fieldName[16];
            const char* fv = reader.value();
            strncpy(fieldName, fv ? fv : "", sizeof(fieldName) - 1);
            fieldName[sizeof(fieldName) - 1] = '\0';
            bool topLevelField = (nestLevel == 0);
            bool fieldInExtra = (nestLevel == 1) && inExtra;
            if (!reader.read()) break;
            json_node_type vt = reader.node_type();
            if (vt == json_node_type::error) { parseError = true; break; }
            if (vt == json_node_type::object || vt == json_node_type::array) {
                // Field value is a container — count it so its closing brace
                // decrements instead of ending the element
                if (topLevelField && vt == json_node_type::object &&
                    strcmp(fieldName, "extra") == 0) {
                    inExtra = true;
                }
                nestLevel++;
                continue;
            }
            if (topLevelField) {
                if (strcmp(fieldName, "id") == 0) {
                    readIntValue(reader, curId);
                } else if (strcmp(fieldName, "archived") == 0) {
                    curArchived = (reader.value_type() == json_value_type::boolean) &&
                                  reader.value_bool();
                }
            } else if (fieldInExtra && strcmp(fieldName, "nfc_id") == 0) {
                readStringValue(reader, curNfcId, sizeof(curNfcId));
            }
        }
    }
    bool truncated = (reader.error() != json_error::none);
    streamHttp.end();

    // Parse errors and truncated streams must not read as "not found" — the
    // consumers create on not-found, and creating on a failed lookup mints
    // duplicate spools (#218 family)
    if (parseError || !sawAnyNode || truncated || !docComplete) {
        Serial.printf("SpoolmanManager: streamFind parse/transport failure for %s\n", path);
        return -2;
    }

    if (bestMatchId >= 0) {
        Serial.printf("SpoolmanManager: streamFind matched uuid=%s to spool id=%d\n", uuid, bestMatchId);
    }
    return bestMatchId >= 0 ? bestMatchId : -1;
}

// Streaming vendor search by exact name (case-insensitive) over /api/v1/vendor.
// The list is fetched unfiltered — Spoolman's ?name= filter does substring
// matching, so the exact match happens client-side either way, and streaming
// makes the list size irrelevant. On match, the vendor's canonical name is
// copied to outName when provided. Returns id >= 0 match, -1 not found,
// -2 transport/parse failure — callers must NOT create on -2.
static int streamFindVendorByName(const char* targetName, char* outName = nullptr,
                                  size_t outNameSize = 0) {
    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    char url[256];
    snprintf(url, sizeof(url), "%s/api/v1/vendor", baseUrl);

    WiFiClient streamClient;
    HTTPClient streamHttp;
    streamHttp.useHTTP10(true);
    streamHttp.begin(streamClient, url);
    streamHttp.setTimeout(10000);
    int code = streamHttp.GET();
    if (code != 200) {
        Serial.printf("SpoolmanManager: vendor list HTTP %d\n", code);
        streamHttp.end();
        return -2;
    }

    HttpClientStream stm(*streamHttp.getStreamPtr());
    json_reader reader(stm);

    int foundId = -1;
    bool sawAnyNode = false;
    bool parseError = false;
    bool docComplete = false;  // saw the outer array close — reader stops silently on malformed JSON
    bool inElement = false;
    int nestLevel = 0;
    int curId = -1;
    char curName[64];
    curName[0] = '\0';

    while (reader.read()) {
        sawAnyNode = true;
        json_node_type nt = reader.node_type();
        if (nt == json_node_type::error) { parseError = true; break; }

        if (!inElement) {
            if (nt == json_node_type::object) {
                inElement = true;
                nestLevel = 0;
                curId = -1;
                curName[0] = '\0';
            } else if (nt == json_node_type::end_array) {
                docComplete = true;
            }
            continue;
        }

        if (nt == json_node_type::object || nt == json_node_type::array) {
            nestLevel++;
            continue;
        }
        if (nt == json_node_type::end_object || nt == json_node_type::end_array) {
            if (nestLevel > 0) { nestLevel--; continue; }
            if (curId >= 0 && curName[0] != '\0' &&
                strcasecmp(curName, targetName) == 0) {
                foundId = curId;
                if (outName != nullptr && outNameSize > 0) {
                    strncpy(outName, curName, outNameSize - 1);
                    outName[outNameSize - 1] = '\0';
                }
                break;
            }
            inElement = false;
            continue;
        }

        if (nt == json_node_type::field && nestLevel == 0) {
            char fieldName[16];
            const char* fv = reader.value();
            strncpy(fieldName, fv ? fv : "", sizeof(fieldName) - 1);
            fieldName[sizeof(fieldName) - 1] = '\0';
            if (!reader.read()) break;
            json_node_type vt = reader.node_type();
            if (vt == json_node_type::error) { parseError = true; break; }
            if (vt == json_node_type::object || vt == json_node_type::array) {
                nestLevel++;
                continue;
            }
            if (strcmp(fieldName, "id") == 0) {
                readIntValue(reader, curId);
            } else if (strcmp(fieldName, "name") == 0) {
                readStringValue(reader, curName, sizeof(curName));
            }
        }
    }
    bool truncated = (reader.error() != json_error::none);
    streamHttp.end();

    if (parseError || !sawAnyNode || (foundId < 0 && (truncated || !docComplete))) {
        Serial.println("SpoolmanManager: vendor lookup parse/transport failure");
        return -2;
    }
    return foundId;
}

// --- File-local Spoolman API helpers ---

static const char* materialTypeToSpoolmanStr(uint8_t type) {
    switch (type) {
        case OPT_MATERIAL_TYPE_PLA:  return "PLA";
        case OPT_MATERIAL_TYPE_PETG: return "PETG";
        case OPT_MATERIAL_TYPE_TPU:  return "TPU";
        case OPT_MATERIAL_TYPE_ABS:  return "ABS";
        case OPT_MATERIAL_TYPE_ASA:  return "ASA";
        case OPT_MATERIAL_TYPE_PC:   return "PC";
        case OPT_MATERIAL_TYPE_PCTG: return "PCTG";
        case OPT_MATERIAL_TYPE_PP:   return "PP";
        case OPT_MATERIAL_TYPE_PA6:  return "PA6";
        case OPT_MATERIAL_TYPE_PA11: return "PA11";
        case OPT_MATERIAL_TYPE_PA12: return "PA12";
        case OPT_MATERIAL_TYPE_PA66: return "PA66";
        case OPT_MATERIAL_TYPE_CPE:  return "CPE";
        case OPT_MATERIAL_TYPE_TPE:  return "TPE";
        case OPT_MATERIAL_TYPE_HIPS: return "HIPS";
        case OPT_MATERIAL_TYPE_PHA:  return "PHA";
        case OPT_MATERIAL_TYPE_PET:  return "PET";
        case OPT_MATERIAL_TYPE_PEI:  return "PEI";
        case OPT_MATERIAL_TYPE_PBT:  return "PBT";
        case OPT_MATERIAL_TYPE_PVB:  return "PVB";
        case OPT_MATERIAL_TYPE_PVA:  return "PVA";
        case OPT_MATERIAL_TYPE_PEKK: return "PEKK";
        case OPT_MATERIAL_TYPE_PEEK: return "PEEK";
        case OPT_MATERIAL_TYPE_BVOH: return "BVOH";
        case OPT_MATERIAL_TYPE_TPC:  return "TPC";
        case OPT_MATERIAL_TYPE_PPS:  return "PPS";
        default: return "PLA";
    }
}

// ---------------------------------------------------------------------------
// Ensure required extra fields exist in Spoolman
// Runs once per boot, skipped if NVS version matches SPOOLMAN_FIELDS_VERSION.
// Bump the version constant when adding new required fields.
// ---------------------------------------------------------------------------

static constexpr uint8_t SPOOLMAN_FIELDS_VERSION = 2;  // v2: added spool nfc_link (#218 durable links)
static const char* NVS_KEY_FIELDS_V = "sp_fields_v";

struct ExtraFieldDef {
    const char* entity;   // "filament" or "spool"
    const char* key;
    const char* name;
};

static const ExtraFieldDef REQUIRED_EXTRA_FIELDS[] = {
    {"filament", "aspect",          "Aspect/Finish"},
    {"filament", "dry_temp",        "Dry Temp (C)"},
    {"filament", "dry_time_hours",  "Dry Time (hrs)"},
    {"spool",    "nfc_id",          "nfc_id"},
    {"spool",    "tag_format",      "Tag Format"},
    {"spool",    "active_toolhead", "active_toolhead"},
    {"spool",    "nfc_link",        "nfc_link"},
};
static constexpr size_t NUM_REQUIRED_FIELDS = sizeof(REQUIRED_EXTRA_FIELDS) / sizeof(REQUIRED_EXTRA_FIELDS[0]);

static bool extraFieldsVerified = false;

static bool ensureExtraFields() {
    if (extraFieldsVerified) return true;

#ifndef NATIVE_TEST
    // Check NVS version — skip API calls if already verified this firmware version
    {
        Preferences prefs;
        if (prefs.begin("spoolsense", true)) {  // read-only
            uint8_t stored = prefs.getUChar(NVS_KEY_FIELDS_V, 0);
            prefs.end();
            if (stored >= SPOOLMAN_FIELDS_VERSION) {
                extraFieldsVerified = true;
                return true;
            }
        }
    }
#endif

    Serial.println("SpoolmanManager: Verifying Spoolman extra fields...");

    bool allChecked = true;
    const char* entities[] = {"filament", "spool"};
    for (const char* entity : entities) {
        char path[48];
        snprintf(path, sizeof(path), "/api/v1/field/%s", entity);
        String response;
        int code = httpGet(path, response);
        if (code != 200) {
            Serial.printf("SpoolmanManager: Failed to get %s fields (code=%d), will retry next sync\n", entity, code);
            allChecked = false;
            continue;
        }

        // Check which required keys exist for this entity
        for (size_t i = 0; i < NUM_REQUIRED_FIELDS; i++) {
            const auto& f = REQUIRED_EXTRA_FIELDS[i];
            if (strcmp(f.entity, entity) != 0) continue;

            // Simple substring check — look for "key":"<fieldname>" in response
            char needle[48];
            snprintf(needle, sizeof(needle), "\"key\":\"%s\"", f.key);
            if (response.indexOf(needle) >= 0) continue;

            // Field missing — create it via POST /api/v1/field/{entity}/{key}
            char createPath[64];
            snprintf(createPath, sizeof(createPath), "/api/v1/field/%s/%s", f.entity, f.key);
            StaticJsonDocument<JSON_SMALL_CAPACITY> doc;
            doc["name"] = f.name;
            doc["field_type"] = "text";
            // nfc_id needs a default empty value for Spoolman queries
            if (strcmp(f.key, "nfc_id") == 0) {
                doc["default_value"] = "\"\"";
            }
            String body;
            serializeJson(doc, body);
            String createResp;
            int createCode = httpPost(createPath, body.c_str(), createResp);
            if (createCode == 200 || createCode == 201) {
                Serial.printf("SpoolmanManager: Created %s extra field '%s'\n", entity, f.key);
            } else {
                Serial.printf("SpoolmanManager: Failed to create %s field '%s' (code=%d): %s\n",
                              entity, f.key, createCode, createResp.c_str());
                allChecked = false;
            }
        }
    }

#ifndef NATIVE_TEST
    // Only store version if all entities were checked and all fields verified/created
    if (allChecked) {
        Preferences prefs;
        if (prefs.begin("spoolsense", false)) {  // read-write
            prefs.putUChar(NVS_KEY_FIELDS_V, SPOOLMAN_FIELDS_VERSION);
            prefs.end();
        }
        Serial.println("SpoolmanManager: Extra fields verified");
    } else {
        Serial.println("SpoolmanManager: Extra fields partially verified, will retry next sync");
    }
#endif

    extraFieldsVerified = allChecked;
    return allChecked;
}

// ---------------------------------------------------------------------------
// Vendor lookup / creation
// ---------------------------------------------------------------------------

static int findOrCreateVendor(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        name = "Unknown";
    }

    int id = streamFindVendorByName(name);
    if (id == -2) {
        // Lookup failed — don't create blindly, could be transient error
        Serial.printf("SpoolmanManager: Vendor lookup failed, cannot resolve '%s'\n", name);
        return -2;
    }
    if (id >= 0) {
        Serial.printf("SpoolmanManager: Found vendor '%s' id=%d\n", name, id);
        return id;
    }

    // Definitive miss — create new vendor
    int code;
    StaticJsonDocument<JSON_SMALL_CAPACITY> createDoc;
    createDoc["name"] = name;
    String body;
    serializeJson(createDoc, body);

    String createResp;
    code = httpPost("/api/v1/vendor", body.c_str(), createResp);
    if (code == 200 || code == 201) {
        if (parseIdFromObject(createResp.c_str(), id)) {
            Serial.printf("SpoolmanManager: Created vendor '%s' id=%d\n", name, id);
            return id;
        }
    }

    Serial.printf("SpoolmanManager: Failed to create vendor '%s', code=%d: %s\n", name, code, createResp.c_str());
    return -1;
}

// Find the first array item whose "material" field exactly matches the target.
// Client-side match: Spoolman's ?material= filter does substring matching (ABS matches PC-ABS).
// Match on material + color + name. Name includes variant (e.g. "PLA Silk" vs "PLA").
// Filaments with no name are treated as matching bare material.
// Streaming filament search over /api/v1/filament (per-vendor when vendorId > 0,
// unfiltered otherwise). Pull-parses the HTTP stream with constant memory —
// replaces an 8KB DOM plus a String holding the entire response body.
// Single pass captures both tiers of the #218 dedup: exact (material+color+name)
// wins, else first material+color match. Nested objects (vendor, extra) are
// skipped by depth guard; field order within an element doesn't matter.
// Returns id >= 0 match, -1 not found, -2 transport/parse failure — callers
// must NOT create on -2 or transient errors mint duplicate filaments.
static int streamFindFilament(int vendorId, const char* targetMaterial,
                              const char* targetColorHex, const char* targetName) {
    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    char url[256];
    if (vendorId > 0) {
        snprintf(url, sizeof(url), "%s/api/v1/filament?vendor_id=%d", baseUrl, vendorId);
    } else {
        snprintf(url, sizeof(url), "%s/api/v1/filament", baseUrl);
    }

    WiFiClient streamClient;
    HTTPClient streamHttp;
    streamHttp.useHTTP10(true);
    streamHttp.begin(streamClient, url);
    streamHttp.setTimeout(10000);
    if (streamHttp.GET() != 200) {
        streamHttp.end();
        return -2;
    }

    HttpClientStream stm(*streamHttp.getStreamPtr());
    json_reader reader(stm);

    // Empty target color = wildcard (enrichment pages may omit color)
    const bool colorWildcard = (targetColorHex[0] == '\0');

    int exactId = -1;
    int looseId = -1;  // first material+color match regardless of name
    bool sawAnyNode = false;
    bool parseError = false;
    bool docComplete = false;  // saw the outer array close — reader stops silently on malformed JSON
    bool inElement = false;
    int nestLevel = 0;  // containers nested INSIDE the current element (vendor, extra, ...)
    int curId = -1;
    char curMaterial[32], curColor[16], curName[64];
    curMaterial[0] = curColor[0] = curName[0] = '\0';

    // Nesting is tracked with an explicit counter instead of reader.depth()
    // comparisons — end_object depth semantics are an implementation detail we
    // refuse to depend on (a nested end_object must not close the element).
    while (reader.read()) {
        sawAnyNode = true;
        json_node_type nt = reader.node_type();
        if (nt == json_node_type::error) { parseError = true; break; }

        if (!inElement) {
            if (nt == json_node_type::object) {
                inElement = true;
                nestLevel = 0;
                curId = -1;
                curMaterial[0] = curColor[0] = curName[0] = '\0';
            } else if (nt == json_node_type::end_array) {
                docComplete = true;
            }
            continue;
        }

        if (nt == json_node_type::object || nt == json_node_type::array) {
            nestLevel++;
            continue;
        }
        if (nt == json_node_type::end_object || nt == json_node_type::end_array) {
            if (nestLevel > 0) { nestLevel--; continue; }
            // The element's own closing brace — evaluate against both tiers
            const char* color = curColor;
            if (color[0] == '#') color++;
            bool sameMaterial = (curMaterial[0] != '\0') &&
                                (strcasecmp(curMaterial, targetMaterial) == 0);
            // RGB-only compare — Spoolman color_hex may carry an alpha suffix
            bool sameColor = colorWildcard ||
                             ((strlen(color) >= 6) &&
                              (strncasecmp(color, targetColorHex, 6) == 0));
            if (sameMaterial && sameColor && curId >= 0) {
                if (looseId < 0) looseId = curId;
                bool exact;
                if (targetName[0] != '\0') {
                    const char* nameToCheck = (curName[0] != '\0') ? curName : curMaterial;
                    exact = (strcasecmp(nameToCheck, targetName) == 0);
                } else {
                    exact = (curName[0] == '\0') || (strcasecmp(curName, curMaterial) == 0);
                }
                if (exact) {
                    exactId = curId;
                    break;  // best possible match — stop streaming
                }
            }
            inElement = false;
            continue;
        }

        // Top-level fields of the element only (nestLevel 0); fields inside
        // vendor/extra arrive with nestLevel > 0 and are ignored
        if (nt == json_node_type::field && nestLevel == 0) {
            char fieldName[16];
            const char* fv = reader.value();
            strncpy(fieldName, fv ? fv : "", sizeof(fieldName) - 1);
            fieldName[sizeof(fieldName) - 1] = '\0';
            if (!reader.read()) break;
            json_node_type vt = reader.node_type();
            if (vt == json_node_type::error) { parseError = true; break; }
            if (vt == json_node_type::object || vt == json_node_type::array) {
                // Field value is a container (vendor, extra) — count it so its
                // closing brace decrements instead of ending the element
                nestLevel++;
                continue;
            }
            if (strcmp(fieldName, "id") == 0) {
                readIntValue(reader, curId);
            } else if (strcmp(fieldName, "material") == 0) {
                readStringValue(reader, curMaterial, sizeof(curMaterial));
            } else if (strcmp(fieldName, "color_hex") == 0) {
                readStringValue(reader, curColor, sizeof(curColor));
            } else if (strcmp(fieldName, "name") == 0) {
                readStringValue(reader, curName, sizeof(curName));
            }
        }
    }
    bool truncated = (reader.error() != json_error::none);
    streamHttp.end();

    // Parse errors and truncated streams must not read as "not found" — the
    // consumers create on not-found, and creating on a failed lookup mints
    // duplicates (#218 family)
    if (parseError || (!sawAnyNode) || (exactId < 0 && (truncated || !docComplete))) {
        Serial.println("SpoolmanManager: streamFindFilament parse/transport failure");
        return -2;
    }

    if (exactId >= 0) return exactId;
    if (looseId >= 0) {
        // User-named filaments ("Ship PLA Red") are the same physical filament;
        // name is display-only (#218)
        Serial.printf("SpoolmanManager: Name mismatch, matched filament id=%d by material+color\n", looseId);
        return looseId;
    }
    return -1;
}

static int16_t avgTemp(int16_t minT, int16_t maxT) {
    if (minT > 0 && maxT > 0) return (minT + maxT) / 2;
    if (maxT > 0) return maxT;
    if (minT > 0) return minT;
    return 0;
}

static int findOrCreateFilament(int vendorId, const SpoolmanSyncRequest& req) {
    const char* material = materialTypeToSpoolmanStr(req.material_type);

    char colorHex[7];
    snprintf(colorHex, sizeof(colorHex), "%02X%02X%02X", req.color[0], req.color[1], req.color[2]);

    // Name formula: "PLA Silk", "PETG CF", or bare "PLA"
    char filamentName[64];
    if (req.aspect[0] != '\0') {
        snprintf(filamentName, sizeof(filamentName), "%s %s", material, req.aspect);
    } else {
        strncpy(filamentName, material, sizeof(filamentName) - 1);
        filamentName[sizeof(filamentName) - 1] = '\0';
    }

    // Streaming search — Spoolman's ?material= filter is unreliable (#92), and the
    // old fetch-whole-list approach cost a body String plus an 8KB DOM per lookup
    {
        int id = streamFindFilament(vendorId, material, colorHex, filamentName);
        if (id == -2) {
            // Transient lookup failure — creating now would mint a duplicate of a
            // filament we simply couldn't see (#218 family)
            Serial.println("SpoolmanManager: Filament lookup failed — skipping create this sync");
            return -2;
        }
        if (id >= 0) {
            Serial.printf("SpoolmanManager: Found filament material=%s color=#%s id=%d\n", material, colorHex, id);

            // Fill in blank fields on existing filament — Spoolman is source of truth,
            // so only write values that are currently unset (null/0/empty).
            if (req.max_print_temp > 0 || req.min_print_temp > 0 || req.max_bed_temp > 0 ||
                req.min_bed_temp > 0 || req.aspect[0] != '\0') {
                char filPath[64];
                snprintf(filPath, sizeof(filPath), "/api/v1/filament/%d", id);
                String filResp;
                int filCode = httpGet(filPath, filResp);
                if (filCode == 200) {
                    DynamicJsonDocument filDoc(2048);
                    if (deserializeJson(filDoc, filResp) == DeserializationError::Ok) {
                        bool hasUpdate = false;
                        StaticJsonDocument<JSON_SMALL_CAPACITY> patchDoc;

                        int existingExtruder = filDoc["settings_extruder_temp"] | 0;
                        int16_t extruderAvg = avgTemp(req.min_print_temp, req.max_print_temp);
                        if (existingExtruder == 0 && extruderAvg > 0) {
                            patchDoc["settings_extruder_temp"] = extruderAvg;
                            hasUpdate = true;
                        }

                        int existingBed = filDoc["settings_bed_temp"] | 0;
                        int16_t bedAvg = avgTemp(req.min_bed_temp, req.max_bed_temp);
                        if (existingBed == 0 && bedAvg > 0) {
                            patchDoc["settings_bed_temp"] = bedAvg;
                            hasUpdate = true;
                        }

                        // Promote bare name to variant name if aspect now known
                        const char* existingName = filDoc["name"] | "";
                        if ((existingName[0] == '\0' || strcasecmp(existingName, material) == 0) &&
                            strcasecmp(filamentName, material) != 0) {
                            patchDoc["name"] = filamentName;
                            hasUpdate = true;
                        }

                        // Fill blank extra fields (aspect, dry temps)
                        JsonObject existingExtra = filDoc["extra"].as<JsonObject>();
                        bool extraChanged = false;
                        JsonObject patchExtra = patchDoc.createNestedObject("extra");

                        // Preserve existing extras
                        if (!existingExtra.isNull()) {
                            for (JsonPair kv : existingExtra) {
                                patchExtra[kv.key()] = kv.value();
                            }
                        }

                        const char* existingAspect = existingExtra["aspect"] | "";
                        if (existingAspect[0] == '\0' && req.aspect[0] != '\0') {
                            char buf[32]; snprintf(buf, sizeof(buf), "\"%s\"", req.aspect);
                            patchExtra["aspect"] = buf;
                            extraChanged = true;
                        }
                        const char* existingDryTemp = existingExtra["dry_temp"] | "";
                        if (existingDryTemp[0] == '\0' && req.dry_temp > 0) {
                            char buf[16]; snprintf(buf, sizeof(buf), "\"%d\"", req.dry_temp);
                            patchExtra["dry_temp"] = buf;
                            extraChanged = true;
                        }
                        const char* existingDryTime = existingExtra["dry_time_hours"] | "";
                        if (existingDryTime[0] == '\0' && req.dry_time_hours > 0) {
                            char buf[16]; snprintf(buf, sizeof(buf), "\"%d\"", req.dry_time_hours);
                            patchExtra["dry_time_hours"] = buf;
                            extraChanged = true;
                        }

                        if (!extraChanged) patchDoc.remove("extra");
                        if (extraChanged) hasUpdate = true;

                        if (hasUpdate) {
                            String patchBody;
                            serializeJson(patchDoc, patchBody);
                            String patchResp;
                            int patchCode = httpPatch(filPath, patchBody.c_str(), patchResp);
                            if (patchCode == 200) {
                                Serial.printf("SpoolmanManager: Updated filament id=%d with missing fields\n", id);
                            }
                        }
                    }
                }
            }

            return id;
        }
        Serial.printf("SpoolmanManager: No exact match for material=%s color=#%s, will create\n", material, colorHex);
    }

    // Create new filament

    StaticJsonDocument<JSON_MEDIUM_CAPACITY> createDoc;
    // Name formula: "material aspect" (e.g. "PLA Silk") or just "PLA" when no aspect
    createDoc["name"] = filamentName;
    createDoc["vendor_id"] = vendorId;
    createDoc["material"] = material;
    if (req.density > 0) createDoc["density"] = req.density;
    createDoc["diameter"] = (req.diameter > 0) ? req.diameter : 1.75f;
    if (req.initial_weight_g > 0) createDoc["weight"] = req.initial_weight_g;
    createDoc["color_hex"] = colorHex;

    // Spoolman built-in temperature fields — average min/max from tag
    int16_t extruderAvg = avgTemp(req.min_print_temp, req.max_print_temp);
    if (extruderAvg > 0) createDoc["settings_extruder_temp"] = extruderAvg;
    int16_t bedAvg = avgTemp(req.min_bed_temp, req.max_bed_temp);
    if (bedAvg > 0) createDoc["settings_bed_temp"] = bedAvg;

    // Extra fields — Spoolman requires values as JSON-encoded strings ("\"value\"")
    JsonObject filExtra = createDoc.createNestedObject("extra");
    if (req.aspect[0] != '\0') {
        char buf[32]; snprintf(buf, sizeof(buf), "\"%s\"", req.aspect);
        filExtra["aspect"] = buf;
    }
    if (req.dry_temp > 0) {
        char buf[16]; snprintf(buf, sizeof(buf), "\"%d\"", req.dry_temp);
        filExtra["dry_temp"] = buf;
    }
    if (req.dry_time_hours > 0) {
        char buf[16]; snprintf(buf, sizeof(buf), "\"%d\"", req.dry_time_hours);
        filExtra["dry_time_hours"] = buf;
    }

    String body;
    serializeJson(createDoc, body);

    String createResp;
    int code = httpPost("/api/v1/filament", body.c_str(), createResp);
    if (code == 200 || code == 201) {
        int id = -1;
        if (parseIdFromObject(createResp.c_str(), id)) {
            Serial.printf("SpoolmanManager: Created filament material=%s id=%d\n", material, id);
            return id;
        }
    }

    Serial.printf("SpoolmanManager: Failed to create filament, code=%d: %s\n", code, createResp.c_str());
    return -1;
}


static int createSpool(int filamentId, const SpoolmanSyncRequest& req) {
    char colorHex[7];
    snprintf(colorHex, sizeof(colorHex), "%02X%02X%02X", req.color[0], req.color[1], req.color[2]);

    StaticJsonDocument<JSON_MEDIUM_CAPACITY> doc;
    doc["filament_id"] = filamentId;
    // Spoolman 0.23.x rejects weight fields with value 0 — omit when not set
    if (req.remaining_weight_g > 0) doc["remaining_weight"] = req.remaining_weight_g;
    float initialWeight = req.initial_weight_g > 0 ? req.initial_weight_g : 1000.0f;
    doc["initial_weight"] = initialWeight;

    // Spoolman expects extra field values to be valid JSON — wrap the string in quotes
    char nfcIdJson[34];
    snprintf(nfcIdJson, sizeof(nfcIdJson), "\"%s\"", req.spool_id);
    JsonObject spoolExtra = doc.createNestedObject("extra");
    spoolExtra["nfc_id"] = nfcIdJson;
    if (req.tag_format[0] != '\0') {
        char buf[32]; snprintf(buf, sizeof(buf), "\"%s\"", req.tag_format);
        spoolExtra["tag_format"] = buf;
    }

    String body;
    serializeJson(doc, body);

    String response;
    int code = httpPost("/api/v1/spool", body.c_str(), response);

    if (code == 200 || code == 201) {
        int id = -1;
        if (parseIdFromObject(response.c_str(), id)) {
            Serial.printf("SpoolmanManager: Created spool for %s, id=%d\n", req.spool_id, id);
            LogBuffer::getInstance().logPrintf("Spoolman: Created spool %d for %s\n", id, req.spool_id);
            return id;
        }
        Serial.printf("SpoolmanManager: Created spool but failed to parse response\n");
        return -1;
    }

    Serial.printf("SpoolmanManager: Failed to create spool, code=%d\n", code);
    LogBuffer::getInstance().logPrintf("ERROR: Failed to create spool, HTTP %d\n", code);
    Serial.printf("  Request:  %s\n", body.c_str());
    Serial.printf("  Response: %s\n", response.c_str());
    return -1;
}



// Blank nfc_id on every active spool other than keepSpoolId. Re-linking a tag
// used to leave the UID on the old spool forever, so global search, enrichment,
// and the cache could each resolve a different "owner" for the same tag (#218).
static void clearNfcIdFromOtherSpools(const char* uuid, int keepSpoolId) {
    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    char url[256];
    snprintf(url, sizeof(url), "%s/api/v1/spool", baseUrl);

    WiFiClient streamClient;
    HTTPClient streamHttp;
    streamHttp.useHTTP10(true);
    streamHttp.begin(streamClient, url);
    streamHttp.setTimeout(10000);
    if (streamHttp.GET() != 200) {
        streamHttp.end();
        return;
    }

    // Pull the FULL extra map, not just nfc_id — Spoolman PATCH replaces the
    // whole extra object, so clearing nfc_id must carry the other extras along
    // (tag_format, middleware fields) or they get wiped
    JsonDocument filter;
    filter[0]["id"] = true;
    filter[0]["archived"] = true;
    filter[0]["extra"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, *streamHttp.getStreamPtr(),
                                                DeserializationOption::Filter(filter));
    streamHttp.end();
    if (err) return;

    char quotedUuid[130];
    snprintf(quotedUuid, sizeof(quotedUuid), "\"%s\"", uuid);

    size_t cleared = 0;
    for (JsonObject spool : doc.as<JsonArray>()) {
        if (cleared >= 8) break;  // safety bound; legacy messes get cleaned over multiple links
        if (spool["archived"] | false) continue;
        int id = spool["id"] | -1;
        if (id < 0 || id == keepSpoolId) continue;
        const char* nfcId = spool["extra"]["nfc_id"] | "";
        if (strcasecmp(nfcId, uuid) != 0 && strcasecmp(nfcId, quotedUuid) != 0) continue;

        StaticJsonDocument<768> patchDoc;
        JsonObject patchExtra = patchDoc.createNestedObject("extra");
        for (JsonPair kv : spool["extra"].as<JsonObject>()) {
            patchExtra[kv.key()] = kv.value();
        }
        patchExtra["nfc_id"] = "\"\"";
        patchExtra["nfc_link"] = "\"\"";  // unstamp: the durable link moved with the tag
        if (patchDoc.overflowed()) {
            Serial.printf("SpoolmanManager: Spool %d extras too large to merge — skipping nfc_id clear\n", id);
            continue;
        }

        String body;
        serializeJson(patchDoc, body);
        char path[64];
        snprintf(path, sizeof(path), "/api/v1/spool/%d", id);
        String resp;
        int code = httpPatch(path, body.c_str(), resp);
        Serial.printf("SpoolmanManager: Cleared stale nfc_id from spool %d (HTTP %d)\n", id, code);
        cleared++;
    }
}

static bool archiveSpool(int spoolId) {
    StaticJsonDocument<JSON_SMALL_CAPACITY> doc;
    doc["archived"] = true;

    String body;
    serializeJson(doc, body);

    char path[64];
    snprintf(path, sizeof(path), "/api/v1/spool/%d", spoolId);

    String response;
    int code = httpPatch(path, body.c_str(), response);
    if (code == 200) {
        Serial.printf("SpoolmanManager: Archived spool id=%d\n", spoolId);
        LogBuffer::getInstance().logPrintf("Spoolman: Archived spool %d\n", spoolId);
        return true;
    }

    Serial.printf("SpoolmanManager: Failed to archive spool id=%d, code=%d\n", spoolId, code);
    return false;
}

// How a sync should treat an existing spool that already carries this tag's nfc_id.
enum class SpoolReconcileAction {
    KeepSpool,           // same filament — normal update
    KeepSpoolWeightOnly, // filament id differs but material/color match — update weight, don't re-point filament (#218)
    ArchiveAndReplace,   // real filament change, or fresh spool detected via weight jump
};

// Decide whether the tag data represents a different physical spool than what
// Spoolman has. Filament id inequality alone is not a re-tag signal: dedup can
// resolve the same physical filament to a different id (user-named filaments,
// enrichment-created variants, vendor coerced by the tag format's brand field —
// #218). Only a real material or color change means the tag moved.
static SpoolReconcileAction reconcileSpool(int existingSpoolId,
                                           const SpoolmanManager::SpoolCore& core,
                                           int newFilamentId,
                                           const SpoolmanSyncRequest& req) {
    // The resolver already fetched this spool's snapshot (one bounded GET
    // feeds identity and reconciliation) — no second fetch here.

    // A user-linked spool (writer picker / explicit re-link) is pinned: never
    // auto-archived or re-pointed, weight still syncs. The tag's identity fields
    // are stale by definition once a user overrides them (#218).
    if (core.userLinked) {
        Serial.printf("SpoolmanManager: Spool %d is user-linked — keeping\n", existingSpoolId);
        return SpoolReconcileAction::KeepSpoolWeightOnly;
    }

    int oldFilamentId = core.filamentId;
    bool idDiffersButSameFilament = false;
    if (oldFilamentId >= 0 && newFilamentId >= 0 && oldFilamentId != newFilamentId) {
        const char* oldMaterial = core.filamentMaterial;
        const char* oldColor    = core.filamentColor;
        if (oldColor[0] == '#') oldColor++;
        const char* newMaterial = materialTypeToSpoolmanStr(req.material_type);
        char newColor[7];
        snprintf(newColor, sizeof(newColor), "%02X%02X%02X", req.color[0], req.color[1], req.color[2]);

        // Compare RGB only — Spoolman color_hex may carry an alpha suffix.
        // Archiving requires POSITIVE evidence of change: if the old filament is
        // missing material or color data, fail closed to keep (archive is the
        // destructive branch).
        bool comparable = (oldMaterial[0] != '\0') && (strlen(oldColor) >= 6);
        bool sameMaterial = comparable && (strcasecmp(oldMaterial, newMaterial) == 0);
        bool sameColor    = comparable && (strncasecmp(oldColor, newColor, 6) == 0);
        if (!comparable || (sameMaterial && sameColor)) {
            Serial.printf("SpoolmanManager: Filament id differs (%d -> %d) but %s — keeping spool %d\n",
                          oldFilamentId, newFilamentId,
                          comparable ? "material/color match" : "old filament data incomplete",
                          existingSpoolId);
            // Not a re-tag by filament identity — but still fall through to the
            // weight-jump check: same-looking filament on a fresh spool is the
            // classic tag-moved-to-new-spool case.
            idDiffersButSameFilament = true;
        } else {
            Serial.printf("SpoolmanManager: Filament changed (%d -> %d), will archive spool %d\n",
                          oldFilamentId, newFilamentId, existingSpoolId);
            LogBuffer::getInstance().logPrintf("Spoolman: Filament changed, archiving spool %d\n", existingSpoolId);
            return SpoolReconcileAction::ArchiveAndReplace;
        }
    }

    // Check for weight jump on a nearly empty spool.
    // This catches: pull tag off spent spool, put on fresh spool of same type.
    static constexpr float LOW_SPOOL_THRESHOLD_G = 100.0f;
    static constexpr float WEIGHT_JUMP_THRESHOLD_G = 500.0f;

    float oldRemaining = core.remainingWeight;
    if (oldRemaining >= 0.0f &&
        oldRemaining <= LOW_SPOOL_THRESHOLD_G &&
        req.remaining_weight_g > (oldRemaining + WEIGHT_JUMP_THRESHOLD_G)) {
        Serial.printf("SpoolmanManager: Weight jump detected (%.0fg -> %.0fg, old was low), will archive spool %d\n",
                      oldRemaining, req.remaining_weight_g, existingSpoolId);
        return SpoolReconcileAction::ArchiveAndReplace;
    }

    return idDiffersButSameFilament ? SpoolReconcileAction::KeepSpoolWeightOnly
                                    : SpoolReconcileAction::KeepSpool;
}

static bool updateSpool(int spoolId, int filamentId, float remainingWeight) {
    StaticJsonDocument<JSON_SMALL_CAPACITY> doc;
    // Only send remaining_weight when the tag actually has weight data.
    // Sending 0 would overwrite Spoolman's tracked weight for non-writable tags.
    if (remainingWeight > 0.0f) {
        doc["remaining_weight"] = remainingWeight;
    }
    if (filamentId >= 0) {
        doc["filament_id"] = filamentId;
    }

    // Weightless tag + weight-only action = nothing to send. Spoolman rejects
    // an empty PATCH with 422, and the failed sync would retry every detect
    // cycle (found on bench: user-linked OpenSpool tag with no weight data).
    if (doc.size() == 0) {
        Serial.printf("SpoolmanManager: Spool %d — nothing to update, skipping PATCH\n", spoolId);
        return true;
    }

    String body;
    serializeJson(doc, body);

    char path[64];
    snprintf(path, sizeof(path), "/api/v1/spool/%d", spoolId);

    String response;
    int code = httpPatch(path, body.c_str(), response);
    if (code == 200) {
        if (remainingWeight > 0.0f) {
            Serial.printf("SpoolmanManager: Updated spool id=%d, remaining=%.1fg\n", spoolId, remainingWeight);
            LogBuffer::getInstance().logPrintf("Spoolman: Spool %d, %.1fg remaining\n", spoolId, remainingWeight);
        } else {
            Serial.printf("SpoolmanManager: Updated spool id=%d (weight unchanged)\n", spoolId);
            LogBuffer::getInstance().logPrintf("Spoolman: Spool %d synced\n", spoolId);
        }
        return true;
    }

    Serial.printf("SpoolmanManager: Failed to update spool, code=%d\n", code);
    LogBuffer::getInstance().logPrintf("ERROR: Failed to update spool, HTTP %d\n", code);
    return false;
}

// --- SpoolmanManager class implementation ---

bool SpoolmanManager::getSpoolDetails(int32_t spoolmanId, SpoolDetails& outDetails) {
    // Initialize output structure
    memset(&outDetails, 0, sizeof(outDetails));
    outDetails.valid = false;
    outDetails.spoolman_id = -1;

    // Validate input
    if (spoolmanId <= 0) {
        Serial.printf("SpoolmanManager: getSpoolDetails - invalid spoolman_id=%d\n", spoolmanId);
        return false;
    }

    // Build API path
    char path[64];
    snprintf(path, sizeof(path), "/api/v1/spool/%d", spoolmanId);

    // Make HTTP GET request
    String response;
    int code = httpGet(path, response);

    if (code != 200) {
        Serial.printf("SpoolmanManager: getSpoolDetails(%d) returned HTTP %d\n", spoolmanId, code);
        return false;
    }

    Serial.printf("SpoolmanManager: getSpoolDetails(%d) response: %.200s%s\n",
                  spoolmanId, response.c_str(), response.length() > 200 ? "..." : "");

    // Single spool response is ~700 bytes — parse directly with ArduinoJson
    JsonDocument doc;
    if (deserializeJson(doc, response)) {
        Serial.printf("SpoolmanManager: getSpoolDetails(%d) JSON parse failed\n", spoolmanId);
        return false;
    }

    outDetails.spoolman_id = doc["id"] | -1;
    float usedWeight = doc["used_weight"] | 0.0f;
    outDetails.remaining_weight_g = doc["remaining_weight"] | 0.0f;
    outDetails.initial_weight_g = doc["initial_weight"] | 0.0f;

    JsonObject fil = doc["filament"];
    if (!fil.isNull()) {
        const char* material = fil["material"] | "";
        if (material[0] == '\0') material = fil["name"] | "";
        strncpy(outDetails.material_type, material, sizeof(outDetails.material_type) - 1);

        const char* colorHex = fil["color_hex"] | "";
        if (colorHex[0] == '#') {
            strncpy(outDetails.color_hex, colorHex, sizeof(outDetails.color_hex) - 1);
        } else if (colorHex[0] != '\0') {
            snprintf(outDetails.color_hex, sizeof(outDetails.color_hex), "#%s", colorHex);
        }

        outDetails.extruder_temp = fil["settings_extruder_temp"] | 0;
        outDetails.bed_temp = fil["settings_bed_temp"] | 0;
        outDetails.density = fil["density"] | 0.0f;
        outDetails.diameter_mm = fil["diameter"] | 0.0f;

        if (outDetails.initial_weight_g == 0.0f) {
            outDetails.initial_weight_g = fil["weight"] | 0.0f;
        }

        JsonObject vendor = fil["vendor"];
        if (!vendor.isNull()) {
            const char* vendorName = vendor["name"] | "";
            strncpy(outDetails.manufacturer, vendorName, sizeof(outDetails.manufacturer) - 1);
        }
    }

    if (outDetails.initial_weight_g == 0.0f) outDetails.initial_weight_g = 1000.0f;
    if (outDetails.remaining_weight_g == 0.0f) {
        float calculated = outDetails.initial_weight_g - usedWeight;
        outDetails.remaining_weight_g = calculated > 0.0f ? calculated : 0.0f;
    }

    bool hasId = outDetails.spoolman_id > 0;
    bool hasMaterial = outDetails.material_type[0] != '\0';
    outDetails.valid = hasId && hasMaterial;

    if (outDetails.valid) {
        Serial.printf("SpoolmanManager: getSpoolDetails(%d) success - %s %s, color=%s, %.1fg/%.1fg\n",
                      outDetails.spoolman_id,
                      outDetails.manufacturer,
                      outDetails.material_type,
                      outDetails.color_hex,
                      outDetails.remaining_weight_g,
                      outDetails.initial_weight_g);
    } else {
        Serial.printf("SpoolmanManager: getSpoolDetails(%d) incomplete - hasId=%d, hasMaterial=%d\n",
                      spoolmanId, hasId, hasMaterial);
    }

    return outDetails.valid;
}

SpoolmanManager& SpoolmanManager::getInstance() {
    static SpoolmanManager instance;
    return instance;
}

bool SpoolmanManager::begin(SemaphoreHandle_t httpMutex) {
    httpMutex_ = httpMutex;

    syncQueue = xQueueCreate(QUEUE_SIZE, sizeof(SpoolmanSyncRequest));
    if (syncQueue == nullptr) {
        Serial.println("SpoolmanManager: Failed to create queue");
        return false;
    }

    cacheMutex_ = xSemaphoreCreateMutex();
    if (cacheMutex_ == nullptr) {
        Serial.println("SpoolmanManager: Failed to create cache mutex");
        return false;
    }

    Serial.println("SpoolmanManager: Initialized");
    return true;
}

void SpoolmanManager::startTask() {
    if (taskHandle != nullptr) {
        return;
    }

    BaseType_t created = createTaskWithAffinity(
        taskFunc,
        "SpoolmanSync",
        TASK_STACK_SIZE,
        this,
        TASK_PRIORITY,
        &taskHandle,
        1  // Core 1
    );
    if (created == pdPASS) {
        Serial.println("SpoolmanManager: Task started");
    } else {
        taskHandle = nullptr;
        Serial.println("SpoolmanManager: ERROR — task creation failed");
    }
}

bool SpoolmanManager::enqueueSync(const SpoolmanSyncRequest& req) {
    if (syncQueue == nullptr) {
        return false;
    }
    return xQueueSend(syncQueue, &req, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool SpoolmanManager::isConfigured() const {
    return ConfigurationManager::getInstance().isSpoolmanEnabled() &&
           strlen(ConfigurationManager::getInstance().getSpoolmanURL()) > 0;
}

int32_t SpoolmanManager::lookupCachedSpoolmanId(const char* spoolId) const {
    if (spoolId == nullptr || spoolId[0] == '\0') {
        return -1;
    }
    if (xSemaphoreTake(cacheMutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        return -1;
    }
    int32_t spoolmanId = -1;
    for (size_t i = 0; i < (sizeof(spoolIdCache_) / sizeof(spoolIdCache_[0])); ++i) {
        if (spoolIdCache_[i].spool_id[0] == '\0') {
            continue;
        }
        if (strcmp(spoolIdCache_[i].spool_id, spoolId) == 0 && spoolIdCache_[i].spoolman_id > 0) {
            spoolmanId = spoolIdCache_[i].spoolman_id;
            break;
        }
    }
    xSemaphoreGive(cacheMutex_);
    return spoolmanId;
}

void SpoolmanManager::storeCachedSpoolmanId(const char* spoolId, int32_t spoolmanId) {
    if (spoolId == nullptr || spoolId[0] == '\0' || spoolmanId <= 0) {
        return;
    }
    if (xSemaphoreTake(cacheMutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    for (size_t i = 0; i < (sizeof(spoolIdCache_) / sizeof(spoolIdCache_[0])); ++i) {
        if (strcmp(spoolIdCache_[i].spool_id, spoolId) == 0) {
            spoolIdCache_[i].spoolman_id = spoolmanId;
            xSemaphoreGive(cacheMutex_);
            return;
        }
    }

    SpoolIdCacheEntry& slot = spoolIdCache_[spoolIdCacheWriteIndex_];
    strncpy(slot.spool_id, spoolId, sizeof(slot.spool_id) - 1);
    slot.spool_id[sizeof(slot.spool_id) - 1] = '\0';
    slot.spoolman_id = spoolmanId;
    spoolIdCacheWriteIndex_ = (spoolIdCacheWriteIndex_ + 1) % (sizeof(spoolIdCache_) / sizeof(spoolIdCache_[0]));
    xSemaphoreGive(cacheMutex_);
}

void SpoolmanManager::invalidateCachedSpoolmanId(const char* spoolId) {
    if (spoolId == nullptr || spoolId[0] == '\0') {
        return;
    }
    if (xSemaphoreTake(cacheMutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    for (size_t i = 0; i < (sizeof(spoolIdCache_) / sizeof(spoolIdCache_[0])); ++i) {
        if (strcmp(spoolIdCache_[i].spool_id, spoolId) == 0) {
            spoolIdCache_[i].spoolman_id = -1;  // Invalidate the entry
            break;
        }
    }
    // Also invalidate sync state cache to force a fresh PATCH on next scan
    for (size_t i = 0; i < (sizeof(syncStateCache_) / sizeof(syncStateCache_[0])); ++i) {
        if (strcmp(syncStateCache_[i].spool_id, spoolId) == 0) {
            syncStateCache_[i].spool_id[0] = '\0';
            break;
        }
    }
    xSemaphoreGive(cacheMutex_);
}

static constexpr float WEIGHT_EPSILON = 0.01f;  // 0.01g tolerance

static inline bool weightEqual(float a, float b) {
    return fabsf(a - b) < WEIGHT_EPSILON;
}

bool SpoolmanManager::isSyncCacheHit(const char* spoolId, int32_t spoolmanId, int32_t filamentId, float remainingWeight) {
    if (spoolId == nullptr || spoolId[0] == '\0') {
        return false;
    }
    if (xSemaphoreTake(cacheMutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }
    bool hit = false;
    for (size_t i = 0; i < (sizeof(syncStateCache_) / sizeof(syncStateCache_[0])); ++i) {
        if (syncStateCache_[i].spool_id[0] == '\0') {
            continue;
        }
        if (strcmp(syncStateCache_[i].spool_id, spoolId) != 0) {
            continue;
        }
        // Check TTL
        uint32_t elapsed = millis() - syncStateCache_[i].synced_at_ms;
        if (elapsed > SYNC_CACHE_TTL_MS) {
            break;
        }
        // Check if data matches (including spoolman_id)
        if (syncStateCache_[i].spoolman_id == spoolmanId &&
            syncStateCache_[i].filament_id == filamentId &&
            weightEqual(syncStateCache_[i].remaining_weight_g, remainingWeight)) {
            Serial.printf("SpoolmanManager: Sync cache hit for %s — skipping redundant update\n", spoolId);
            hit = true;
        }
        break;
    }
    xSemaphoreGive(cacheMutex_);
    return hit;
}

void SpoolmanManager::storeSyncState(const char* spoolId, int32_t spoolmanId, int32_t filamentId, float remainingWeight) {
    if (spoolId == nullptr || spoolId[0] == '\0' || spoolmanId <= 0) {
        return;
    }
    if (xSemaphoreTake(cacheMutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    // Update existing entry if present
    for (size_t i = 0; i < (sizeof(syncStateCache_) / sizeof(syncStateCache_[0])); ++i) {
        if (strcmp(syncStateCache_[i].spool_id, spoolId) == 0) {
            syncStateCache_[i].spoolman_id = spoolmanId;
            syncStateCache_[i].filament_id = filamentId;
            syncStateCache_[i].remaining_weight_g = remainingWeight;
            syncStateCache_[i].synced_at_ms = millis();
            xSemaphoreGive(cacheMutex_);
            return;
        }
    }
    // Write to next slot (ring buffer)
    SyncStateCache& slot = syncStateCache_[syncStateCacheWriteIndex_];
    strncpy(slot.spool_id, spoolId, sizeof(slot.spool_id) - 1);
    slot.spool_id[sizeof(slot.spool_id) - 1] = '\0';
    slot.spoolman_id = spoolmanId;
    slot.filament_id = filamentId;
    slot.remaining_weight_g = remainingWeight;
    slot.synced_at_ms = millis();
    syncStateCacheWriteIndex_ = (syncStateCacheWriteIndex_ + 1) % (sizeof(syncStateCache_) / sizeof(syncStateCache_[0]));
    xSemaphoreGive(cacheMutex_);
}

void SpoolmanManager::taskFunc(void* param) {
    SpoolmanManager* self = static_cast<SpoolmanManager*>(param);
    self->taskLoop();
}

void SpoolmanManager::taskLoop() {
    SpoolmanSyncRequest req;
    while (true) {
        if (xQueueReceive(syncQueue, &req, portMAX_DELAY) == pdTRUE) {
            MemoryDiagnostics::reportSelf(MemoryDiagnostics::Task::SpoolmanSync);
            if (!isConfigured()) {
                continue;
            }

            AppMessage msg;
            memset(&msg, 0, sizeof(msg));  // write-update branch fills only a few fields;
                                           // consumers read color_hex/temps/density from it
            msg.type = AppMessageType::SPOOLMAN_SYNCED;
            strncpy(msg.payload.spoolmanSynced.spool_id, req.spool_id,
                    sizeof(msg.payload.spoolmanSynced.spool_id) - 1);
            msg.payload.spoolmanSynced.spool_id[sizeof(msg.payload.spoolmanSynced.spool_id) - 1] = '\0';
            msg.payload.spoolmanSynced.is_uid_lookup = req.lookup_only;

            if (req.lookup_only) {
                Serial.printf("SpoolmanManager: UID lookup for %s\n", req.spool_id);
                SpoolDetails details = {};
                bool found = lookupSpoolByUid(req.spool_id, details);
                msg.payload.spoolmanSynced.success = found;
                msg.payload.spoolmanSynced.spoolman_id = found ? details.spoolman_id : -1;
                msg.payload.spoolmanSynced.kg_remaining = found ? details.remaining_weight_g / 1000.0f : 0.0f;
                msg.payload.spoolmanSynced.initial_weight_g = found ? details.initial_weight_g : 0.0f;
                strncpy(msg.payload.spoolmanSynced.material_name,
                        found ? details.material_type : "",
                        sizeof(msg.payload.spoolmanSynced.material_name) - 1);
                msg.payload.spoolmanSynced.material_name[sizeof(msg.payload.spoolmanSynced.material_name) - 1] = '\0';
                strncpy(msg.payload.spoolmanSynced.manufacturer,
                        found ? details.manufacturer : "",
                        sizeof(msg.payload.spoolmanSynced.manufacturer) - 1);
                msg.payload.spoolmanSynced.manufacturer[sizeof(msg.payload.spoolmanSynced.manufacturer) - 1] = '\0';
                strncpy(msg.payload.spoolmanSynced.color_hex,
                        found ? details.color_hex : "",
                        sizeof(msg.payload.spoolmanSynced.color_hex) - 1);
                msg.payload.spoolmanSynced.color_hex[sizeof(msg.payload.spoolmanSynced.color_hex) - 1] = '\0';
                msg.payload.spoolmanSynced.extruder_temp = found ? details.extruder_temp : 0;
                msg.payload.spoolmanSynced.bed_temp = found ? details.bed_temp : 0;
                msg.payload.spoolmanSynced.density = found ? details.density : 0.0f;
                msg.payload.spoolmanSynced.diameter_mm = found ? details.diameter_mm : 0.0f;
            } else {
                Serial.printf("SpoolmanManager: Syncing spool %s\n", req.spool_id);
                int resolvedSpoolmanId = -1;
                bool success = syncSpool(req, resolvedSpoolmanId);
                msg.payload.spoolmanSynced.success = success;
                msg.payload.spoolmanSynced.kg_remaining = req.remaining_weight_g / 1000.0f;
                msg.payload.spoolmanSynced.spoolman_id = resolvedSpoolmanId;
                strncpy(msg.payload.spoolmanSynced.material_name, req.material_name,
                        sizeof(msg.payload.spoolmanSynced.material_name) - 1);
                msg.payload.spoolmanSynced.material_name[sizeof(msg.payload.spoolmanSynced.material_name) - 1] = '\0';
            }

            ApplicationManager::getInstance().sendMessage(msg);
        }
    }
}

bool SpoolmanManager::lookupSpoolByUid(const char* uid, SpoolDetails& outDetails) {
    if (xSemaphoreTake(httpMutex_, HTTP_MUTEX_TIMEOUT) != pdTRUE) {
        Serial.println("SpoolmanManager: lookupSpoolByUid could not acquire HTTP mutex");
        return false;
    }

    SpoolResolution r = resolveSpoolByUidNoLock(uid);
    if (r.spoolId < 0) {
        Serial.printf("SpoolmanManager: UID lookup — no match for uid=%s%s\n",
                      uid, r.lookupFailed ? " (lookup failed)" : "");
        xSemaphoreGive(httpMutex_);
        return false;
    }
    int spoolmanId = r.spoolId;

    bool ok = getSpoolDetails(spoolmanId, outDetails);
    xSemaphoreGive(httpMutex_);

    if (ok) {
        Serial.printf("SpoolmanManager: UID lookup found spool %d — %s %.0fg\n",
                      spoolmanId, outDetails.material_type, outDetails.remaining_weight_g);
    }
    return ok;
}

void SpoolmanManager::setPendingLink(int32_t spoolId) {
    // Store timestamp before ID so that any reader seeing a valid ID is
    // guaranteed the timestamp is already set (happens-before).
    pendingLinkSetAt_.store(millis());
    pendingLinkSpoolId_.store(spoolId);
    Serial.printf("SpoolmanManager: Pending link set for spool %d\n", spoolId);
}

int SpoolmanManager::findFilamentNoLock(int vendorId, const char* material,
                                        const char* colorHex6, const char* name) {
    return streamFindFilament(vendorId, material, colorHex6, name ? name : "");
}

void SpoolmanManager::recordLinkResult(int32_t spoolId, bool ok, const char* uid) {
    if (xSemaphoreTake(cacheMutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;
    lastLinkResult_.spoolId = spoolId;
    lastLinkResult_.ok = ok;
    lastLinkResult_.atMs = millis();
    strncpy(lastLinkResult_.uid, uid ? uid : "", sizeof(lastLinkResult_.uid) - 1);
    lastLinkResult_.uid[sizeof(lastLinkResult_.uid) - 1] = '\0';
    xSemaphoreGive(cacheMutex_);
}

SpoolmanManager::PendingLinkStatus SpoolmanManager::getPendingLinkStatus() {
    PendingLinkStatus st;
    int32_t id = pendingLinkSpoolId_.load();
    if (id > 0) {
        uint32_t age = millis() - pendingLinkSetAt_.load();
        if (age < PENDING_LINK_TIMEOUT_MS) {
            st.state = PendingLinkState::Armed;
            st.spoolId = id;
            st.remainingMs = PENDING_LINK_TIMEOUT_MS - age;
        } else {
            // Still stored but past the window — a sync would discard it
            st.state = PendingLinkState::Expired;
            st.spoolId = id;
        }
        return st;
    }
    // No armed link: report the most recent consumption if it's fresh enough
    // for the UI's polling window to care about (2x the link window)
    if (xSemaphoreTake(cacheMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (lastLinkResult_.spoolId > 0 &&
            (millis() - lastLinkResult_.atMs) < (2 * PENDING_LINK_TIMEOUT_MS)) {
            st.state = PendingLinkState::Consumed;
            st.spoolId = lastLinkResult_.spoolId;
            st.linkOk = lastLinkResult_.ok;
            strncpy(st.uid, lastLinkResult_.uid, sizeof(st.uid) - 1);
            st.uid[sizeof(st.uid) - 1] = '\0';
        }
        xSemaphoreGive(cacheMutex_);
    }
    return st;
}

int SpoolmanManager::findVendorNoLock(const char* name, char* outName, size_t outNameSize) {
    return streamFindVendorByName(name, outName, outNameSize);
}

bool SpoolmanManager::fetchSpoolCore(int32_t spoolId, SpoolCore& out) {
    char path[64];
    snprintf(path, sizeof(path), "/api/v1/spool/%ld", (long)spoolId);
    String resp;
    int code = httpGet(path, resp);
    if (code == 404) {
        // Definitively gone — callers treat as unusable candidate, not failure
        out = SpoolCore{};
        out.archived = true;
        return true;
    }
    if (code != 200) return false;

    DynamicJsonDocument doc(3072);
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;

    out.archived = doc["archived"] | false;
    out.filamentId = doc["filament"]["id"] | -1;
    out.remainingWeight = doc["remaining_weight"] | -1.0f;
    const char* link = doc["extra"]["nfc_link"] | "";
    out.userLinked = (strstr(link, "user") != nullptr);
    const char* nfc = doc["extra"]["nfc_id"] | "";
    strncpy(out.nfcId, nfc, sizeof(out.nfcId) - 1);
    out.nfcId[sizeof(out.nfcId) - 1] = '\0';
    const char* mat = doc["filament"]["material"] | "";
    strncpy(out.filamentMaterial, mat, sizeof(out.filamentMaterial) - 1);
    out.filamentMaterial[sizeof(out.filamentMaterial) - 1] = '\0';
    const char* col = doc["filament"]["color_hex"] | "";
    strncpy(out.filamentColor, col, sizeof(out.filamentColor) - 1);
    out.filamentColor[sizeof(out.filamentColor) - 1] = '\0';
    return true;
}

// A candidate spool (from tag id or cache) is usable for this tag only if it
// still exists, is unarchived, and its stored nfc_id is empty or this tag's —
// a spool already claimed by a DIFFERENT tag means the reference is stale.
bool SpoolmanManager::spoolUsableForUid(const SpoolCore& core, const char* uid) {
    if (core.archived) return false;
    if (core.nfcId[0] == '\0') return true;
    char quoted[44];
    snprintf(quoted, sizeof(quoted), "\"%s\"", uid);
    return strcasecmp(core.nfcId, uid) == 0 || strcasecmp(core.nfcId, quoted) == 0;
}

SpoolmanManager::SpoolResolution SpoolmanManager::resolveSpoolByUidNoLock(const char* uid, int32_t tagSpoolmanId) {
    SpoolResolution res;
    if (uid == nullptr || uid[0] == '\0') return res;

    // 1. Global nfc_id match — the tag's durable identity in Spoolman
    int byNfc = streamFindSpoolByNfcId("/api/v1/spool", uid);
    if (byNfc == -2) {
        res.lookupFailed = true;
        return res;
    }
    if (byNfc >= 0) {
        res.spoolId = byNfc;
        res.source = SpoolResolution::Source::NfcId;
        if (fetchSpoolCore(byNfc, res.core)) {
            res.coreValid = true;
            res.filamentId = res.core.filamentId;
            res.userLinked = res.core.userLinked;
        }
        storeCachedSpoolmanId(uid, byNfc);
        return res;
    }

    // 2. Tag-stored spoolman_id — validated, never trusted blindly
    if (tagSpoolmanId > 0) {
        SpoolCore core;
        if (!fetchSpoolCore(tagSpoolmanId, core)) {
            res.lookupFailed = true;
            return res;
        }
        if (spoolUsableForUid(core, uid)) {
            res.spoolId = tagSpoolmanId;
            res.filamentId = core.filamentId;
            res.userLinked = core.userLinked;
            res.core = core;
            res.coreValid = true;
            res.source = SpoolResolution::Source::TagId;
            storeCachedSpoolmanId(uid, tagSpoolmanId);
            return res;
        }
        Serial.printf("SpoolmanManager: tag spoolman_id=%ld stale for uid=%s — ignoring\n",
                      (long)tagSpoolmanId, uid);
    }

    // 3. Cached uid→id — validated; the resolver is the single eviction point
    int32_t cached = lookupCachedSpoolmanId(uid);
    if (cached > 0 && cached != tagSpoolmanId) {
        SpoolCore core;
        if (!fetchSpoolCore(cached, core)) {
            res.lookupFailed = true;
            return res;
        }
        if (spoolUsableForUid(core, uid)) {
            res.spoolId = cached;
            res.filamentId = core.filamentId;
            res.userLinked = core.userLinked;
            res.core = core;
            res.coreValid = true;
            res.source = SpoolResolution::Source::Cache;
            return res;
        }
        invalidateCachedSpoolmanId(uid);
    }

    return res;  // clean not-found: creation is allowed
}

int SpoolmanManager::findSpoolIdByUidNoLock(const char* uid) {
    // Pass -2 (transport/parse failure) through unchanged: callers must NOT
    // treat a failed lookup as not-found, or transient errors create duplicates
    return streamFindSpoolByNfcId("/api/v1/spool", uid);
}

float SpoolmanManager::deductFromSpoolman(const char* uid, float grams, bool* success) {
    if (success) *success = false;
    if (!isConfigured()) return 0.0f;
    if (xSemaphoreTake(httpMutex_, HTTP_MUTEX_TIMEOUT) != pdTRUE) {
        Serial.println("SpoolmanManager: deductFromSpoolman — mutex timeout");
        return 0.0f;
    }

    SpoolResolution r = resolveSpoolByUidNoLock(uid);
    if (r.spoolId < 0) {
        Serial.printf("SpoolmanManager: deductFromSpoolman — spool not found for %s%s\n",
                      uid, r.lookupFailed ? " (lookup failed, retry later)" : "");
        xSemaphoreGive(httpMutex_);
        return 0.0f;
    }
    int spoolId = r.spoolId;

    // Get current remaining weight
    char path[64];
    snprintf(path, sizeof(path), "/api/v1/spool/%d", spoolId);
    String response;
    int code = httpGet(path, response);
    if (code != 200) {
        Serial.printf("SpoolmanManager: deductFromSpoolman — GET spool %d failed (HTTP %d)\n", spoolId, code);
        xSemaphoreGive(httpMutex_);
        return 0.0f;
    }

    StaticJsonDocument<JSON_SMALL_CAPACITY> doc;
    if (deserializeJson(doc, response)) {
        xSemaphoreGive(httpMutex_);
        return 0.0f;
    }

    if (doc["remaining_weight"].isNull()) {
        // Absent/null is NOT an empty spool — `| 0.0f` would read it as 0 and the
        // 0 g shortcut below would claim success, silently discarding the usage.
        Serial.printf("SpoolmanManager: spool %d has no remaining_weight — cannot deduct, keeping pending\n", spoolId);
        xSemaphoreGive(httpMutex_);
        return 0.0f;
    }
    float currentRemaining = doc["remaining_weight"] | 0.0f;
    float deduction = (grams > currentRemaining) ? currentRemaining : grams;
    float newRemaining = currentRemaining - deduction;
    if (newRemaining < 0.0f) newRemaining = 0.0f;

    if (deduction <= 0.0f) {
        // Spool is already at 0 g — the deduction is absorbed as nothing to
        // subtract. That is a SUCCESS (the pending record must clear), and
        // skipping the 0->0 PATCH saves the round-trip.
        Serial.printf("SpoolmanManager: spool %d already empty — 0g deducted\n", spoolId);
        xSemaphoreGive(httpMutex_);
        if (success) *success = true;
        return 0.0f;
    }

    // PATCH with new remaining weight
    StaticJsonDocument<JSON_SMALL_CAPACITY> patchDoc;
    patchDoc["remaining_weight"] = newRemaining;
    String body;
    serializeJson(patchDoc, body);

    code = httpPatch(path, body.c_str(), response);
    xSemaphoreGive(httpMutex_);

    if (code == 200) {
        Serial.printf("SpoolmanManager: Deducted %.1fg from spool %d (%.1fg -> %.1fg)\n",
                      deduction, spoolId, currentRemaining, newRemaining);
        LogBuffer::getInstance().logPrintf("Spoolman: Deducted %.1fg from spool %d\n", deduction, spoolId);
        if (success) *success = true;
        return deduction;
    }

    Serial.printf("SpoolmanManager: deductFromSpoolman — PATCH failed (HTTP %d)\n", code);
    LogBuffer::getInstance().logPrintf("ERROR: Spoolman deduction failed, HTTP %d\n", code);
    return 0.0f;
}

bool SpoolmanManager::syncSpool(const SpoolmanSyncRequest& req, int& resolvedSpoolmanId) {
    if (xSemaphoreTake(httpMutex_, HTTP_MUTEX_TIMEOUT) != pdTRUE) {
        Serial.println("SpoolmanManager: Could not acquire HTTP mutex");
        return false;
    }

    // Ensure Spoolman has required extra fields (runs once per boot)
    if (!ensureExtraFields()) {
        Serial.println("SpoolmanManager: Extra fields not ready, aborting sync");
        xSemaphoreGive(httpMutex_);
        return false;
    }

    resolvedSpoolmanId = -1;
    bool success = false;

    // If the writer pre-registered a spool to link, consume it and patch nfc_id before syncing.
    // This prevents auto-sync from creating a duplicate when a pre-selected spool exists.
    // Read timestamp before exchange: setPendingLink stores time before ID, so a valid ID
    // guarantees the timestamp is already set (no race window on the age check).
    uint32_t linkSetAt = pendingLinkSetAt_.load();
    int32_t linkSpoolId = pendingLinkSpoolId_.exchange(-1);
    int32_t justLinkedSpoolId = -1;  // explicit user pick this sync — never archive or re-point (#218)
    if (linkSpoolId > 0) {
        uint32_t age = millis() - linkSetAt;
        if (age < PENDING_LINK_TIMEOUT_MS) {
            char patchPath[48];
            snprintf(patchPath, sizeof(patchPath), "/api/v1/spool/%d", linkSpoolId);

            // Read-merge-write: PATCHing extra replaces the whole map, so carry the
            // spool's existing extras (tag_format, middleware fields) along with the
            // new nfc_id and the durable user-link stamp. Heap docs deliberately —
            // SpoolmanSync's measured stack floor is under 2KB.
            // 4096: a failed parse here would skip the merge and the PATCH would
            // wipe the spool's other extras — size generously (heap, transient)
            DynamicJsonDocument spoolDoc(4096);
            DynamicJsonDocument patchDoc(1024);
            JsonObject patchExtra = patchDoc.createNestedObject("extra");
            {
                String spoolResp;
                if (httpGet(patchPath, spoolResp) == 200) {
                    DeserializationError mergeErr = deserializeJson(spoolDoc, spoolResp);
                    if (mergeErr == DeserializationError::Ok) {
                        for (JsonPair kv : spoolDoc["extra"].as<JsonObject>()) {
                            patchExtra[kv.key()] = kv.value();
                        }
                    } else {
                        Serial.printf("SpoolmanManager: Link merge parse failed (%s) — spool %d extras may be replaced\n",
                                      mergeErr.c_str(), linkSpoolId);
                    }
                }
            }
            char quotedUid[130];
            snprintf(quotedUid, sizeof(quotedUid), "\"%s\"", req.spool_id);
            patchExtra["nfc_id"] = quotedUid;
            // Durable link (#218): a user-linked spool is never auto-archived or
            // re-pointed by sync; identity changes require another explicit link
            patchExtra["nfc_link"] = "\"user\"";

            String patchBody;
            serializeJson(patchDoc, patchBody);
            String patchResp;
            int patchCode = httpPatch(patchPath, patchBody.c_str(), patchResp);
            if (patchCode == 200) {
                storeCachedSpoolmanId(req.spool_id, linkSpoolId);
                justLinkedSpoolId = linkSpoolId;
                Serial.printf("SpoolmanManager: Linked nfc_id=%s to spool %d via pending link\n", req.spool_id, linkSpoolId);
                clearNfcIdFromOtherSpools(req.spool_id, linkSpoolId);
            } else {
                Serial.printf("SpoolmanManager: Pending link PATCH failed (HTTP %d) for spool %d\n", patchCode, linkSpoolId);
            }
            recordLinkResult(linkSpoolId, patchCode == 200, req.spool_id);
        } else {
            Serial.printf("SpoolmanManager: Pending link for spool %d expired (%ums old), discarding\n", linkSpoolId, age);
        }
    }

    // ── Identity resolution (#224) ──────────────────────────────────────
    // One resolver answers "which spool is this tag": nfc_id match first
    // (authoritative), then the tag-stored id validated against the live
    // spool, then the validated cache. Everything below acts on its verdict;
    // creation happens only on a clean not-found.
    // Tier 0: a spool the user picked THIS sync is authoritative — it must
    // never be outranked, even by a stale duplicate nfc_id claim that the
    // best-effort cleanup failed to clear (Codex finding on #224).
    SpoolResolution r;
    if (justLinkedSpoolId > 0) {
        r.spoolId = justLinkedSpoolId;
        r.userLinked = true;
        r.source = SpoolResolution::Source::UserPick;
        r.coreValid = fetchSpoolCore(justLinkedSpoolId, r.core);
        if (r.coreValid) r.filamentId = r.core.filamentId;
    } else {
        r = resolveSpoolByUidNoLock(req.spool_id, req.spoolman_id);
        if (r.lookupFailed) {
            Serial.println("SpoolmanManager: identity resolution failed — aborting sync this cycle");
            xSemaphoreGive(httpMutex_);
            return false;
        }
    }

    // Resolve filament identity — needed both to reconcile an existing spool
    // and to create a new one. -2 (transient lookup failure) must not create.
    int vendorId = findOrCreateVendor(req.manufacturer);
    int filamentId = (vendorId >= 0) ? findOrCreateFilament(vendorId, req) : -1;

    if (filamentId < 0) {
        if (r.spoolId >= 0) {
            // Spool is known but filament identity is unavailable this cycle —
            // sync the weight and leave the filament pointer untouched, matching
            // the old fast path's resilience
            Serial.println("SpoolmanManager: filament unresolved — weight-only sync");
            if (isSyncCacheHit(req.spool_id, r.spoolId, -1, req.remaining_weight_g)) {
                resolvedSpoolmanId = r.spoolId;
                xSemaphoreGive(httpMutex_);
                return true;
            }
            success = updateSpool(r.spoolId, -1, req.remaining_weight_g);
            if (success) {
                resolvedSpoolmanId = r.spoolId;
                storeCachedSpoolmanId(req.spool_id, resolvedSpoolmanId);
                storeSyncState(req.spool_id, resolvedSpoolmanId, -1, req.remaining_weight_g);
            }
            xSemaphoreGive(httpMutex_);
            return success;
        }
        Serial.println("SpoolmanManager: Failed to find/create vendor/filament");
        LogBuffer::getInstance().logPrintf("ERROR: Failed to find/create vendor/filament\n");
        xSemaphoreGive(httpMutex_);
        return false;
    }

    int spoolId = -1;
    if (r.spoolId >= 0) {
        // Existing spool — reconcile decides keep vs archive-and-replace.
        // A spool the user just picked this sync is never archived/re-pointed;
        // reconcileSpool itself honors the durable nfc_link stamp (#218).
        SpoolReconcileAction action;
        if (r.spoolId == justLinkedSpoolId || r.source == SpoolResolution::Source::UserPick) {
            action = SpoolReconcileAction::KeepSpoolWeightOnly;
        } else if (!r.coreValid) {
            // No trustworthy snapshot — archive needs positive evidence, keep
            action = SpoolReconcileAction::KeepSpool;
        } else {
            action = reconcileSpool(r.spoolId, r.core, filamentId, req);
        }

        if (action == SpoolReconcileAction::ArchiveAndReplace) {
            if (archiveSpool(r.spoolId)) {
                invalidateCachedSpoolmanId(req.spool_id);
                spoolId = createSpool(filamentId, req);
                success = (spoolId >= 0);
            } else {
                // Archive failed — reuse rather than mint a duplicate
                Serial.printf("SpoolmanManager: Archive failed, reusing spool %ld\n", (long)r.spoolId);
                spoolId = r.spoolId;
                success = updateSpool(spoolId, filamentId, req.remaining_weight_g);
            }
        } else {
            int syncFilamentId = (action == SpoolReconcileAction::KeepSpoolWeightOnly) ? -1 : filamentId;
            if (isSyncCacheHit(req.spool_id, r.spoolId, syncFilamentId, req.remaining_weight_g)) {
                resolvedSpoolmanId = r.spoolId;
                xSemaphoreGive(httpMutex_);
                return true;
            }
            spoolId = r.spoolId;
            success = updateSpool(spoolId, syncFilamentId, req.remaining_weight_g);
            if (success) {
                resolvedSpoolmanId = spoolId;
                storeCachedSpoolmanId(req.spool_id, resolvedSpoolmanId);
                storeSyncState(req.spool_id, resolvedSpoolmanId, syncFilamentId, req.remaining_weight_g);
            }
            xSemaphoreGive(httpMutex_);
            return success;
        }
    } else {
        // Clean not-found from the resolver — creating is correct.
        // TODO(#224 follow-up): when the resolver adopts a spool via the
        // TagId/Cache tiers and its nfc_id is empty, stamp it (merge-
        // preserving) so identity survives without the tag's id field.
        spoolId = createSpool(filamentId, req);
        success = (spoolId >= 0);
    }

    if (success && spoolId > 0) {
        resolvedSpoolmanId = spoolId;
        storeCachedSpoolmanId(req.spool_id, resolvedSpoolmanId);
        storeSyncState(req.spool_id, resolvedSpoolmanId, filamentId, req.remaining_weight_g);
    }

    xSemaphoreGive(httpMutex_);
    return success;
}
