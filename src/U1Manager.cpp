// U1Manager.cpp — Snapmaker U1 direct-mode bridge implementation.
// All HTTP plumbing, JSON shaping, per-material defaults, and pending-augment
// state live here so ApplicationManager can stay focused on dispatch.

#include "U1Manager.h"

#ifndef NATIVE_TEST
  #include "ApplicationManager.h"  // SpoolDetectedPayload, SpoolmanSyncedPayload
  #include "NFCTypes.h"             // CurrentSpoolState, TagKind
  #include "ConfigurationManager.h"
#include "LogBuffer.h"
  #include "WebServerManager.h"
  #include <Arduino.h>
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <ArduinoJson.h>
  #include <freertos/FreeRTOS.h>
  #include <freertos/semphr.h>
  #include <cctype>
  #include <cstring>
  #include <cstdio>
  #include <cstdlib>

extern SemaphoreHandle_t g_httpMutex;

namespace {

// Per-material defaults for OpenSpool (no bed temp on tag) and any tag that's
// missing temps. Matches common slicer defaults (Orca/PrusaSlicer/Cura).
// Only applied when MAIN_TYPE matches one of the firmware's recognized types.
struct MaterialDefaults {
    const char* main_type;  // uppercase
    int hotend_min;
    int hotend_max;
    int bed_temp;
};

constexpr MaterialDefaults DEFAULTS[] = {
    { "PLA",  200, 220, 60  },
    { "PETG", 230, 250, 70  },
    { "ABS",  240, 260, 100 },
    { "ASA",  240, 260, 100 },
    { "TPU",  220, 240, 50  },
    { "PVA",  190, 210, 60  },
    { "PC",   260, 290, 110 },
    { "PA",   250, 280, 90  },  // Nylon
};

const MaterialDefaults* findDefaults(const char* mainType) {
    if (!mainType || mainType[0] == '\0') return nullptr;
    for (const auto& d : DEFAULTS) {
        if (strcmp(d.main_type, mainType) == 0) return &d;
    }
    return nullptr;
}

// Split a material name into MAIN_TYPE (uppercase) + SUB_TYPE on first space
// or hyphen. Examples:
//   "PLA Matte"  -> "PLA"  + "Matte"
//   "PETG-CF"    -> "PETG" + "CF"
//   "PLA"        -> "PLA"  + ""
//   "PA-CF Pro"  -> "PA"   + "CF Pro"
void splitMaterialName(const char* src, char* main, size_t mainCap,
                                          char* sub, size_t subCap) {
    if (!src || src[0] == '\0') {
        if (main && mainCap) main[0] = '\0';
        if (sub && subCap) sub[0] = '\0';
        return;
    }
    size_t i = 0;
    while (src[i] && src[i] != ' ' && src[i] != '-' && i + 1 < mainCap) {
        main[i] = (char)std::toupper((unsigned char)src[i]);
        i++;
    }
    main[i] = '\0';
    // Skip the separator
    if (src[i] == ' ' || src[i] == '-') i++;
    if (src[i] && sub && subCap > 0) {
        strncpy(sub, src + i, subCap - 1);
        sub[subCap - 1] = '\0';
    } else if (sub && subCap) {
        sub[0] = '\0';
    }
}

// Pack the spool UID hex string ("04A1B2C3") into the byte array used by the
// U1's CARD_UID field. Stops at non-hex or buffer end.
void packCardUid(const char* hex, uint8_t* out, uint8_t cap, uint8_t& outLen) {
    outLen = 0;
    if (!hex) return;
    size_t hexLen = strlen(hex);
    for (size_t i = 0; i + 1 < hexLen && outLen < cap; i += 2) {
        char buf[3] = { hex[i], hex[i + 1], 0 };
        char* end = nullptr;
        long byte = strtol(buf, &end, 16);
        if (end != buf + 2) break;  // non-hex char
        out[outLen++] = (uint8_t)byte;
    }
}

// Convert RGBA bytes to RGB_1 integer (R<<16 | G<<8 | B). Returns -1 if all
// channels are zero AND alpha is also zero — caller treats as "no color".
int rgbaToRgb1(const uint8_t rgba[4]) {
    if (rgba[0] == 0 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 0) return -1;
    return ((int)rgba[0] << 16) | ((int)rgba[1] << 8) | (int)rgba[2];
}

// Convert "#RRGGBB" or "RRGGBB" hex string to RGB_1 integer. Returns -1 on
// parse failure or empty input.
int hexColorToRgb1(const char* hex) {
    if (!hex || hex[0] == '\0') return -1;
    if (hex[0] == '#') hex++;
    unsigned int r = 0, g = 0, b = 0;
    if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) != 3) return -1;
    return (int)((r << 16) | (g << 8) | b);
}

// Apply firmware-recognized per-material defaults to any temp field that's
// still zero. Only fires if the MAIN_TYPE is one of the U1's protocol-mapped
// types — unrecognized types (PEEK, niche blends, etc.) leave zeros alone so
// users see the gap and can set explicit values in Spoolman or on the tag.
void applyMaterialDefaults(U1FilamentInfo& info) {
    const MaterialDefaults* d = findDefaults(info.main_type);
    if (!d) return;
    if (info.hotend_min_temp == 0) info.hotend_min_temp = d->hotend_min;
    if (info.hotend_max_temp == 0) info.hotend_max_temp = d->hotend_max;
    if (info.bed_temp == 0) info.bed_temp = d->bed_temp;
}

// Build U1 wire info from a smart-tag detection payload.
U1FilamentInfo buildFromDetection(const SpoolDetectedPayload& p) {
    U1FilamentInfo info;
    strncpy(info.vendor, p.manufacturer, sizeof(info.vendor) - 1);
    splitMaterialName(p.material_name, info.main_type, sizeof(info.main_type),
                                         info.sub_type, sizeof(info.sub_type));

    // TigerTag carries aspect (Silk/Wood/Matt) separately when material_name
    // is just the base type — promote aspect into SUB_TYPE if we don't have one.
    if (info.sub_type[0] == '\0' && p.aspect[0] != '\0') {
        strncpy(info.sub_type, p.aspect, sizeof(info.sub_type) - 1);
    }

    if (p.has_color) {
        info.rgb_1 = rgbaToRgb1(p.primary_color);
        info.alpha = p.primary_color[3] ? p.primary_color[3] : 255;
    }

    if (p.min_print_temp > 0) info.hotend_min_temp = p.min_print_temp;
    if (p.max_print_temp > 0) info.hotend_max_temp = p.max_print_temp;
    // U1 wants a single bed temp; prefer min, fall back to max — same convention
    // the firmware's own OpenSpool parser uses for OpenSpool tags.
    if (p.min_bed_temp > 0)      info.bed_temp = p.min_bed_temp;
    else if (p.max_bed_temp > 0) info.bed_temp = p.max_bed_temp;

    packCardUid(p.spool_id, info.card_uid, sizeof(info.card_uid), info.card_uid_len);
    applyMaterialDefaults(info);
    return info;
}

// Build U1 wire info from a Spoolman sync result. Used by the generic-UID-tag
// path where the tag itself carries no rich data — Spoolman is the only source.
U1FilamentInfo buildFromSpoolmanSync(const SpoolmanSyncedPayload& s) {
    U1FilamentInfo info;

    if (s.manufacturer[0] != '\0') {
        strncpy(info.vendor, s.manufacturer, sizeof(info.vendor) - 1);
    }
    splitMaterialName(s.material_name, info.main_type, sizeof(info.main_type),
                                         info.sub_type, sizeof(info.sub_type));

    if (s.color_hex[0] != '\0') {
        info.rgb_1 = hexColorToRgb1(s.color_hex);
        info.alpha = 255;
    }

    if (s.extruder_temp > 0) {
        info.hotend_min_temp = s.extruder_temp;
        info.hotend_max_temp = s.extruder_temp;  // single value -> use as both
    }
    if (s.bed_temp > 0) info.bed_temp = s.bed_temp;

    packCardUid(s.spool_id, info.card_uid, sizeof(info.card_uid), info.card_uid_len);
    applyMaterialDefaults(info);
    return info;
}

// Merge non-empty Spoolman fields onto a base U1FilamentInfo (POST 1's data for
// smart-tag augment). Returns true if anything actually changed — caller uses
// this to skip a redundant POST when Spoolman didn't add or correct anything.
bool overlaySpoolmanFields(U1FilamentInfo& info, const SpoolmanSyncedPayload& s) {
    bool changed = false;

    if (s.manufacturer[0] != '\0' && strcmp(s.manufacturer, info.vendor) != 0) {
        strncpy(info.vendor, s.manufacturer, sizeof(info.vendor) - 1);
        info.vendor[sizeof(info.vendor) - 1] = '\0';
        changed = true;
    }

    if (s.material_name[0] != '\0') {
        char newMain[sizeof(info.main_type)] = {0};
        char newSub[sizeof(info.sub_type)] = {0};
        splitMaterialName(s.material_name, newMain, sizeof(newMain),
                                              newSub, sizeof(newSub));
        if (newMain[0] != '\0' && strcmp(newMain, info.main_type) != 0) {
            strncpy(info.main_type, newMain, sizeof(info.main_type) - 1);
            info.main_type[sizeof(info.main_type) - 1] = '\0';
            changed = true;
        }
        if (newSub[0] != '\0' && strcmp(newSub, info.sub_type) != 0) {
            strncpy(info.sub_type, newSub, sizeof(info.sub_type) - 1);
            info.sub_type[sizeof(info.sub_type) - 1] = '\0';
            changed = true;
        }
    }

    if (s.color_hex[0] != '\0') {
        int newRgb = hexColorToRgb1(s.color_hex);
        if (newRgb >= 0 && newRgb != info.rgb_1) {
            info.rgb_1 = newRgb;
            info.alpha = 255;
            changed = true;
        }
    }

    if (s.extruder_temp > 0
            && (s.extruder_temp != info.hotend_min_temp
                || s.extruder_temp != info.hotend_max_temp)) {
        info.hotend_min_temp = s.extruder_temp;
        info.hotend_max_temp = s.extruder_temp;
        changed = true;
    }

    if (s.bed_temp > 0 && s.bed_temp != info.bed_temp) {
        info.bed_temp = s.bed_temp;
        changed = true;
    }

    return changed;
}

// Returns true if every U1-required field has a meaningful value. Used to
// decide whether a pending-augment registration is worth keeping.
bool isComplete(const U1FilamentInfo& info) {
    return info.vendor[0] != '\0'
        && info.main_type[0] != '\0'
        && info.rgb_1 >= 0
        && info.hotend_min_temp > 0
        && info.hotend_max_temp > 0
        && info.bed_temp > 0;
}

// Serialize and POST. Returns the HTTP response code, -1000 if Moonraker URL
// unset, -1001 if the HTTP mutex was busy, or a negative HTTPC_ERROR_* code on
// transport failure. Channel/enabled gating is the caller's responsibility.
int postFilamentDetectSet(uint8_t channel, const U1FilamentInfo& info) {
    auto& cfg = ConfigurationManager::getInstance();
    const char* moonrakerUrl = cfg.getMoonrakerURL();
    if (!moonrakerUrl || moonrakerUrl[0] == '\0') return -1000;

    if (g_httpMutex && xSemaphoreTake(g_httpMutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return -1001;
    }

    StaticJsonDocument<512> body;
    body["channel"] = channel;
    JsonObject infoObj = body.createNestedObject("info");

    if (info.vendor[0] != '\0')   infoObj["VENDOR"] = info.vendor;
    if (info.main_type[0] != '\0') infoObj["MAIN_TYPE"] = info.main_type;
    if (info.sub_type[0] != '\0')  infoObj["SUB_TYPE"] = info.sub_type;
    if (info.rgb_1 >= 0) {
        infoObj["RGB_1"] = (long)info.rgb_1;
        infoObj["ALPHA"] = info.alpha;
    }
    if (info.hotend_min_temp > 0) infoObj["HOTEND_MIN_TEMP"] = info.hotend_min_temp;
    if (info.hotend_max_temp > 0) infoObj["HOTEND_MAX_TEMP"] = info.hotend_max_temp;
    if (info.bed_temp > 0)         infoObj["BED_TEMP"] = info.bed_temp;
    if (info.card_uid_len > 0) {
        JsonArray uidArr = infoObj.createNestedArray("CARD_UID");
        for (uint8_t i = 0; i < info.card_uid_len; i++) {
            uidArr.add((int)info.card_uid[i]);
        }
    }

    String payload;
    serializeJson(body, payload);

    char url[192];
    snprintf(url, sizeof(url), "%s/printer/filament_detect/set", moonrakerUrl);

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(1000);
    http.setTimeout(2000);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(payload);
    http.end();

    if (g_httpMutex) xSemaphoreGive(g_httpMutex);
    return code;
}

}  // namespace

U1Manager& U1Manager::getInstance() {
    static U1Manager instance;
    return instance;
}

void U1Manager::stageSpool(const U1FilamentInfo& info, const char* uid) {
    autoPick_ = {};  // fresh baseline for the motion-sensor poller
    taskENTER_CRITICAL(&stagedMux_);
    staged_.active = true;
    staged_.expiresAtMs = millis() + STAGE_TTL_MS;
    strncpy(staged_.uid, uid ? uid : "", sizeof(staged_.uid) - 1);
    staged_.uid[sizeof(staged_.uid) - 1] = '\0';
    staged_.info = info;
    taskEXIT_CRITICAL(&stagedMux_);
    Serial.printf("U1Manager: staged spool uid=%s (%s %s) — awaiting channel pick\n",
                  staged_.uid, info.vendor, info.main_type);
}

void U1Manager::clearStaged() {
    taskENTER_CRITICAL(&stagedMux_);
    staged_.active = false;
    taskEXIT_CRITICAL(&stagedMux_);
}

bool U1Manager::hasStagedSpool() {
    taskENTER_CRITICAL(&stagedMux_);
    bool active = staged_.active &&
                  (int32_t)(millis() - staged_.expiresAtMs) < 0;
    taskEXIT_CRITICAL(&stagedMux_);
    return active;
}

U1Manager::StagedState U1Manager::getStagedState() {
    StagedState st;
    taskENTER_CRITICAL(&stagedMux_);
    if (staged_.active) {
        uint32_t now = millis();
        if ((int32_t)(now - staged_.expiresAtMs) < 0) {
            st.active = true;
            st.remainingMs = staged_.expiresAtMs - now;
            strncpy(st.vendor, staged_.info.vendor, sizeof(st.vendor) - 1);
            strncpy(st.material, staged_.info.main_type, sizeof(st.material) - 1);
            st.rgb = staged_.info.rgb_1;
        } else {
            staged_.active = false;  // lazy expiry
        }
    }
    taskEXIT_CRITICAL(&stagedMux_);
    return st;
}

bool U1Manager::assignStagedToChannel(uint8_t channel) {
    if (channel > 3) return false;

    // Copy out under the lock; POST outside it (HTTP under a critical section
    // would be catastrophic)
    U1FilamentInfo info;
    char uid[17] = {};
    bool valid = false;
    taskENTER_CRITICAL(&stagedMux_);
    if (staged_.active && (int32_t)(millis() - staged_.expiresAtMs) < 0) {
        info = staged_.info;
        memcpy(uid, staged_.uid, sizeof(uid));
        valid = true;
    }
    taskEXIT_CRITICAL(&stagedMux_);
    if (!valid) {
        Serial.println("U1Manager: assign requested but nothing staged (or expired)");
        return false;
    }

    int code = postFilamentDetectSet(channel, info);
    Serial.printf("U1Manager: assignStagedToChannel channel=%u — HTTP %d\n",
                  (unsigned)channel, code);
    if (code < 0 && code != -1000 && code != -1001) {
        moonrakerBackoffUntilMs_ = millis() + MOONRAKER_BACKOFF_MS;
        return false;
    }
    if (code < 0) return false;
    moonrakerBackoffUntilMs_ = 0;
    clearStaged();
    lastAssign_.channel = (int8_t)channel;
    lastAssign_.atMs = millis();
    memcpy(lastAssign_.uid, uid, sizeof(lastAssign_.uid));
    return true;
}

bool U1Manager::queryLaneSensors(bool loaded[4]) {
    auto& cfg = ConfigurationManager::getInstance();
    const char* moonrakerUrl = cfg.getMoonrakerURL();
    if (!moonrakerUrl || moonrakerUrl[0] == '\0') return false;

    if (g_httpMutex && xSemaphoreTake(g_httpMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;  // busy — try again next tick
    }

    // filament_motion_sensor e0_filament .. e3_filament (extended firmware
    // naming — needs one field confirmation against a live U1)
    char url[320];
    snprintf(url, sizeof(url),
             "%s/printer/objects/query?filament_motion_sensor%%20e0_filament"
             "&filament_motion_sensor%%20e1_filament"
             "&filament_motion_sensor%%20e2_filament"
             "&filament_motion_sensor%%20e3_filament",
             moonrakerUrl);

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(800);
    http.setTimeout(1500);
    http.begin(client, url);
    int code = http.GET();
    String resp = (code == 200) ? http.getString() : String();
    http.end();
    if (g_httpMutex) xSemaphoreGive(g_httpMutex);

    if (code != 200) {
        if (code < 0) moonrakerBackoffUntilMs_ = millis() + MOONRAKER_BACKOFF_MS;
        return false;
    }

    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;
    JsonObject status = doc["result"]["status"];
    if (status.isNull()) return false;

    const char* names[4] = {
        "filament_motion_sensor e0_filament", "filament_motion_sensor e1_filament",
        "filament_motion_sensor e2_filament", "filament_motion_sensor e3_filament"
    };
    for (int i = 0; i < 4; i++) {
        loaded[i] = status[names[i]]["filament_detected"] | false;
    }
    return true;
}

void U1Manager::loopTick() {
    if (WebServerManager::getInstance().otaExclusive()) return;
    auto& cfg = ConfigurationManager::getInstance();
    if (!cfg.isU1Enabled() || !cfg.isU1StageMode() || !cfg.isU1AutoPickEnabled()) return;
    if (!hasStagedSpool()) return;

    uint32_t now = millis();
    if (now - autoPick_.lastPollMs < AUTO_PICK_POLL_MS) return;
    autoPick_.lastPollMs = now;
    if (moonrakerBackoffUntilMs_ != 0 && (int32_t)(now - moonrakerBackoffUntilMs_) < 0) return;

    bool loaded[4] = {};
    if (!queryLaneSensors(loaded)) return;

    if (!autoPick_.baselineValid) {
        // Snapshot which lanes were already loaded at stage time — only an
        // empty→loaded transition after this point may claim the spool
        memcpy(autoPick_.baseline, loaded, sizeof(autoPick_.baseline));
        autoPick_.baselineValid = true;
        return;
    }

    for (int i = 0; i < 4; i++) {
        if (autoPick_.baseline[i]) continue;   // was loaded before the scan
        if (loaded[i]) {
            if (++autoPick_.detectCount[i] >= AUTO_PICK_DEBOUNCE) {
                Serial.printf("U1Manager: auto-pick — lane %d loaded, assigning staged spool\n", i);
                LogBuffer::getInstance().logPrintf("U1: auto-assigned tool %d\n", i);
                assignStagedToChannel((uint8_t)i);
                return;
            }
        } else {
            autoPick_.detectCount[i] = 0;      // flutter — restart debounce
        }
    }
}

int8_t U1Manager::getRecentAssignChannel() {
    if (lastAssign_.channel >= 0 && (millis() - lastAssign_.atMs) < 10000) {
        return lastAssign_.channel;
    }
    return -1;
}

void U1Manager::publishFromDetection(const SpoolDetectedPayload& payload) {
    auto& cfg = ConfigurationManager::getInstance();
    if (!cfg.isU1Enabled()) return;

    // Stage mode: hold the spool for a channel pick instead of posting.
    // Spoolman augment still applies — publishFromSpoolmanSync merges into
    // the staged info while it waits.
    if (cfg.isU1StageMode()) {
        U1FilamentInfo info = buildFromDetection(payload);
        stageSpool(info, payload.spool_id);
        if (cfg.isSpoolmanEnabled() && !isComplete(info)) {
            pendingAugment_.active = true;
            strncpy(pendingAugment_.uid, payload.spool_id, sizeof(pendingAugment_.uid) - 1);
            pendingAugment_.uid[sizeof(pendingAugment_.uid) - 1] = '\0';
            pendingAugment_.expiresAtMs = millis() + PENDING_AUGMENT_TTL_MS;
            pendingAugment_.postedInfo = info;
        } else {
            pendingAugment_.active = false;
        }
        return;
    }

    uint8_t channel = cfg.getU1Channel();
    if (channel > 3) return;  // belt-and-braces; loader already clamps

    uint32_t now = millis();
    if (moonrakerBackoffUntilMs_ != 0 && (int32_t)(now - moonrakerBackoffUntilMs_) < 0) {
        return;
    }

    U1FilamentInfo info = buildFromDetection(payload);
    int code = postFilamentDetectSet(channel, info);

    Serial.printf("U1Manager: publishFromDetection channel=%u uid=%s — HTTP %d\n",
                  (unsigned)channel, payload.spool_id, code);

    if (code == -1000 || code == -1001) {
        // Config or contention issue — neither warrants a backoff window
        return;
    }
    if (code < 0) {
        moonrakerBackoffUntilMs_ = millis() + MOONRAKER_BACKOFF_MS;
        Serial.printf("U1Manager: U1/Moonraker unreachable — backing off %u ms\n",
                      (unsigned)MOONRAKER_BACKOFF_MS);
        return;
    }
    moonrakerBackoffUntilMs_ = 0;

    // Register pending augment if POST 1 was incomplete and Spoolman is configured.
    // Cache the exact info we just sent — POST 2 will start from this and overlay
    // any non-empty Spoolman fields, so on-tag data is never overwritten by gaps
    // in the Spoolman record.
    if (cfg.isSpoolmanEnabled() && !isComplete(info)) {
        pendingAugment_.active = true;
        strncpy(pendingAugment_.uid, payload.spool_id, sizeof(pendingAugment_.uid) - 1);
        pendingAugment_.uid[sizeof(pendingAugment_.uid) - 1] = '\0';
        pendingAugment_.expiresAtMs = millis() + PENDING_AUGMENT_TTL_MS;
        pendingAugment_.postedInfo = info;
    } else {
        pendingAugment_.active = false;
    }
}

void U1Manager::publishFromSpoolmanSync(const SpoolmanSyncedPayload& sync,
                                          const CurrentSpoolState& state) {
    auto& cfg = ConfigurationManager::getInstance();
    if (!cfg.isU1Enabled()) return;
    if (!sync.success || sync.spoolman_id <= 0) return;

    uint8_t channel = cfg.getU1Channel();
    if (channel > 3) return;

    uint32_t now = millis();
    if (moonrakerBackoffUntilMs_ != 0 && (int32_t)(now - moonrakerBackoffUntilMs_) < 0) {
        return;
    }

    if (sync.is_uid_lookup) {
        // Generic UID tag (NFC+) — Spoolman is the only data source.
        // Defense: if the user removed the tag during the lookup (or swapped to a
        // different tag), skip the POST. Mirrors the writeback guard in the
        // ApplicationManager generic-tag-writeback path.
        if (!state.present
                || state.kind != TagKind::GenericUidTag
                || strcmp(state.spool_id, sync.spool_id) != 0) {
            return;
        }
        U1FilamentInfo info = buildFromSpoolmanSync(sync);
        if (cfg.isU1StageMode()) {
            // Stage mode: the lookup result IS the staged spool
            stageSpool(info, sync.spool_id);
            return;
        }
        int code = postFilamentDetectSet(channel, info);
        Serial.printf("U1Manager: publishFromSpoolmanSync(UID) channel=%u spool=%d — HTTP %d\n",
                      (unsigned)channel, sync.spoolman_id, code);
        if (code < 0 && code != -1000 && code != -1001) {
            moonrakerBackoffUntilMs_ = millis() + MOONRAKER_BACKOFF_MS;
        } else if (code >= 0) {
            moonrakerBackoffUntilMs_ = 0;
        }
        return;
    }

    // Smart-tag follow-up sync — augment POST 1's data with anything Spoolman
    // supplied that POST 1 didn't already have (or had with a different value).
    if (!pendingAugment_.active) return;
    // Bound the compare to the UID buffer size so a non-null-terminated producer
    // (defensive — both buffers are 17 bytes / 16 hex chars + null) can't make
    // strcmp walk off the end.
    if (strncmp(pendingAugment_.uid, sync.spool_id, sizeof(pendingAugment_.uid) - 1) != 0) {
        // Late sync for a previous (different) tag — ignore but DON'T clear the
        // current pending augment. A user scanning two tags within ~3s could have
        // an earlier tag's sync arrive first; clearing here would sabotage the
        // augment for the tag they just scanned.
        return;
    }
    if ((int32_t)(now - pendingAugment_.expiresAtMs) >= 0) {
        pendingAugment_.active = false;
        return;
    }

    // Start from POST 1's wire info, overlay non-empty Spoolman fields. Skip the
    // POST if nothing actually changed — Spoolman had no data POST 1 was missing.
    U1FilamentInfo merged = pendingAugment_.postedInfo;
    bool changed = overlaySpoolmanFields(merged, sync);

    pendingAugment_.active = false;  // single-shot regardless of outcome
    if (!changed) return;

    if (cfg.isU1StageMode()) {
        // Staged spool still waiting for its channel — refresh it in place so
        // the eventual assignment posts the augmented data
        taskENTER_CRITICAL(&stagedMux_);
        bool stillStaged = staged_.active &&
                           strncmp(staged_.uid, sync.spool_id, sizeof(staged_.uid) - 1) == 0;
        if (stillStaged) staged_.info = merged;
        taskEXIT_CRITICAL(&stagedMux_);
        if (stillStaged) {
            Serial.printf("U1Manager: staged spool augmented from Spoolman (spool %d)\n", sync.spoolman_id);
            return;
        }
        // Already assigned before the sync landed — re-post the augmented
        // data to the channel the user picked, mirroring fixed mode
        if (lastAssign_.channel >= 0 &&
            strncmp(lastAssign_.uid, sync.spool_id, sizeof(lastAssign_.uid) - 1) == 0 &&
            (millis() - lastAssign_.atMs) < PENDING_AUGMENT_TTL_MS) {
            int code2 = postFilamentDetectSet((uint8_t)lastAssign_.channel, merged);
            Serial.printf("U1Manager: augment after assign — channel=%d HTTP %d\n",
                          lastAssign_.channel, code2);
        }
        return;
    }

    int code = postFilamentDetectSet(channel, merged);
    Serial.printf("U1Manager: publishFromSpoolmanSync(augment) channel=%u spool=%d — HTTP %d\n",
                  (unsigned)channel, sync.spoolman_id, code);
    if (code < 0 && code != -1000 && code != -1001) {
        moonrakerBackoffUntilMs_ = millis() + MOONRAKER_BACKOFF_MS;
    } else if (code >= 0) {
        moonrakerBackoffUntilMs_ = 0;
    }
}

#else  // NATIVE_TEST

U1Manager& U1Manager::getInstance() {
    static U1Manager instance;
    return instance;
}
void U1Manager::publishFromDetection(const SpoolDetectedPayload&) {}
void U1Manager::publishFromSpoolmanSync(const SpoolmanSyncedPayload&,
                                          const CurrentSpoolState&) {}
U1Manager::StagedState U1Manager::getStagedState() { return {}; }
bool U1Manager::assignStagedToChannel(uint8_t) { return false; }
bool U1Manager::hasStagedSpool() { return false; }
void U1Manager::stageSpool(const U1FilamentInfo&, const char*) {}
void U1Manager::clearStaged() {}
void U1Manager::loopTick() {}
int8_t U1Manager::getRecentAssignChannel() { return -1; }
bool U1Manager::queryLaneSensors(bool[4]) { return false; }

#endif
