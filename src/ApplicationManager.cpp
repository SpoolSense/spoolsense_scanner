// ApplicationManager.cpp — State machine and message queue dispatcher. Coordinates NFC tag
// detections, print lifecycle, Spoolman sync, HA publishing, keypad input, and display updates.
// Runs on the main loop (non-blocking, drain QUEUE_SIZE messages per cycle).

#include "ApplicationManager.h"
#include "UserConfig.h"
#include "ConversionUtils.h"
#include "TagStateJson.h"
#include "DeductionManager.h"
#ifndef NATIVE_TEST
  #include "NFCTypes.h"
  #include "NFCManager.h"
  #include "DisplayI.h"
  #include "SpoolmanManager.h"
  #include "ConfigurationManager.h"
  #include "HomeAssistantManager.h"
  #include "LEDManager.h"
  #include <Arduino.h>
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <Preferences.h>
  extern LEDManager ledManager;
#else
  #include "platform/NativePlatform.h"
  #include "FakeLCDManager.h"
  #include "TestNFCManager.h"
#endif
#include <cstring>

#ifdef NATIVE_TEST
// Unit tests: faster delays to reduce test execution time
static constexpr uint32_t TAG_REMOVED_STATUS_DELAY_MS = 25;
static constexpr uint32_t TYPE_REMAIN_DISPLAY_DELAY_MS = 25;
#else
// Production: 5s delay after tag removed before showing status screen (debounce rapid re-taps)
static constexpr uint32_t TAG_REMOVED_STATUS_DELAY_MS = 5000;
// Production: 5s delay for Type/Remain display (user reads the tag info first)
static constexpr uint32_t TYPE_REMAIN_DISPLAY_DELAY_MS = 5000;
#endif

// ── Singleton ────────────────────────────────────────────────────────────────

ApplicationManager& ApplicationManager::getInstance() {
    static ApplicationManager instance;
    return instance;
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

bool ApplicationManager::begin(DisplayI* display) {
    if (messageQueue != nullptr) {
        return true;  // Already initialized
    }

    display_ = display;

    messageQueue = xQueueCreate(QUEUE_SIZE, sizeof(AppMessage));
    if (messageQueue == nullptr) {
        Serial.println("ApplicationManager: Failed to create message queue");
        return false;
    }

    Serial.println("ApplicationManager: Message queue created");

#ifndef NATIVE_TEST
    // Load cached tray dashboard from NVS (display handled by main.cpp after full boot)
    bool dashEnabled = ConfigurationManager::getInstance().isBambuDashboardEnabled();
    if (dashEnabled) {
        Preferences prefs;
        prefs.begin("spoolsense", true);
        size_t len = prefs.getBytesLength("tray_dash");
        if (len == sizeof(TrayDashboardState)) {
            prefs.getBytes("tray_dash", &trayDashboardState_, sizeof(TrayDashboardState));
            Serial.printf("ApplicationManager: Loaded cached tray dashboard, %d trays\n",
                          trayDashboardState_.tray_count);
        }
        prefs.end();
    }
#endif

    return true;
}

bool ApplicationManager::sendMessage(const AppMessage& msg, uint32_t waitMs) {
    if (messageQueue == nullptr) {
        Serial.println("ApplicationManager: Queue not initialized");
        return false;
    }

    // Non-blocking (waitMs=0) or blocking with timeout; called from ISRs and task context
    TickType_t ticksToWait = (waitMs == 0) ? 0 : pdMS_TO_TICKS(waitMs);
    BaseType_t result = xQueueSend(messageQueue, &msg, ticksToWait);
    return result == pdTRUE;
}

void ApplicationManager::processMessages() {
    // Main loop: drain message queue at soft rate limit (QUEUE_SIZE cap prevents starvation)
    if (messageQueue == nullptr) {
        return;
    }

    AppMessage msg;
    size_t processed = 0;
    // Non-blocking receive: drain up to QUEUE_SIZE messages per loop cycle
    while (processed < QUEUE_SIZE && xQueueReceive(messageQueue, &msg, 0) == pdTRUE) {
        handleMessage(msg);
        processed++;
    }

    // Tag removed: delay before showing status screen (debounce rapid re-taps)
    if (pendingStatusAfterTagRemoved) {
        uint32_t elapsedMs = static_cast<uint32_t>(millis() - tagRemovedAtMs);
        if (elapsedMs >= TAG_REMOVED_STATUS_DELAY_MS) {
            // Don't overwrite display if user is typing a tool number — mask status screen behind keypad
            // TFT: keep spool data visible, let screen timeout handle dimming after 30s inactivity
#ifndef NATIVE_TEST
            bool isTft = ConfigurationManager::getInstance().isTftEnabled();
#else
            bool isTft = false;
#endif
            if (keypadBufferLen_ == 0 && !isTft) {
                showStatusScreen();
            }
            pendingStatusAfterTagRemoved = false;
        }
    }

    // Type/Remain: deferred display (user finishes reading tag info before material name shown)
    if (pendingTypeRemainDisplay && display_) {
        uint32_t elapsedMs = static_cast<uint32_t>(millis() - typeRemainScheduledAtMs);
        if (elapsedMs >= TYPE_REMAIN_DISPLAY_DELAY_MS) {
            char line1[17];
            char line2[17];
            snprintf(line1, sizeof(line1), "Type: %.10s", delayedDisplayMaterialName);
#ifndef NATIVE_TEST
            bool isKeypad = ConfigurationManager::getInstance().isKeypadEnabled();
#else
            bool isKeypad = false;
#endif
            if (isKeypad) {
                snprintf(line2, sizeof(line2), "%.0fg Tool#? #", delayedDisplayKgRemaining * 1000.0f);
            } else {
                snprintf(line2, sizeof(line2), "Remain: %.0fg", delayedDisplayKgRemaining * 1000.0f);
            }
            display_->showText(line1, line2);

            pendingTypeRemainDisplay = false;

            Serial.printf("ApplicationManager: Displayed delayed Type/Remain - Type: %s, Remain: %.0fg\n",
                         delayedDisplayMaterialName, delayedDisplayKgRemaining * 1000.0f);
        }
    }

    // Bambu dashboard revert: after scan interruption, return to dashboard
    if (dashboardRevertAt_ != 0) {
        uint32_t elapsedMs = static_cast<uint32_t>(millis() - dashboardRevertAt_);
        if (elapsedMs >= DASHBOARD_REVERT_DELAY_MS) {
            dashboardRevertAt_ = 0;
#ifndef NATIVE_TEST
            bool dashEnabled = ConfigurationManager::getInstance().isBambuDashboardEnabled();
#else
            bool dashEnabled = false;
#endif
            if (dashEnabled && trayDashboardState_.has_data && display_) {
                display_->showTrayDashboard(trayDashboardState_);
            }
        }
    }

}

// ── Display ─────────────────────────────────────────────────────────────────

void ApplicationManager::showStatusScreen() {
    // LCD idle screen: 3-indicator status (WiFi, Spoolman, Home Assistant) with ?/+/! symbols
    if (display_ == nullptr) {
        return;
    }

#ifndef NATIVE_TEST
    auto& config = ConfigurationManager::getInstance();

    // WiFi: ? = unconfigured, + = connected, ! = configured but disconnected
    char wifiInd;
    if (strlen(config.getWiFiSSID()) == 0) {
        wifiInd = '?';
    } else {
        wifiInd = (WiFi.status() == WL_CONNECTED) ? '+' : '!';
    }

    // Spoolman: ? = unconfigured, + = configured (assume OK when no URL check available)
    char smInd;
    if (strlen(config.getSpoolmanURL()) == 0) {
        smInd = '?';
    } else {
        smInd = '+';
    }

    // Home Assistant: ? = disabled, + = MQTT connected, ! = enabled but not connected
    char haInd;
    if (!config.getHAEnabled()) {
        haInd = '?';
    } else if (strlen(config.getHAMqttHost()) == 0) {
        haInd = '!';
    } else {
        haInd = HomeAssistantManager::getInstance().isConnected() ? '+' : '!';
    }
#else
    // Native tests: all integrations stubbed to ?
    char wifiInd = '?';
    char smInd = '?';
    char haInd = '?';
#endif

    char line1[17];
    char line2[17];
    snprintf(line1, sizeof(line1), "NFC+ Wifi%c", wifiInd);
    snprintf(line2, sizeof(line2), "SM%c MQTT%c", smInd, haInd);
    display_->showText(line1, line2);
}

void ApplicationManager::scheduleTypeRemainDisplay(const char* material_name, float kg_remaining) {
    // Deferred display: show material name + remaining weight after 5s (user reads tag data first)
    strncpy(delayedDisplayMaterialName, material_name, sizeof(delayedDisplayMaterialName) - 1);
    delayedDisplayMaterialName[sizeof(delayedDisplayMaterialName) - 1] = '\0';
    delayedDisplayKgRemaining = kg_remaining;
    pendingTypeRemainDisplay = true;
    typeRemainScheduledAtMs = millis();
}

// ── Message Dispatch ────────────────────────────────────────────────────────

void ApplicationManager::handleMessage(const AppMessage& msg) {
    // Route each message type to its handler (tag detections, print events, HA commands, etc.)
    switch (msg.type) {
        case AppMessageType::PRINT_STARTED:
            handlePrintStarted(msg);
            break;

        case AppMessageType::PRINT_ENDED:
            handlePrintEnded(msg);
            break;

        case AppMessageType::SPOOL_DETECTED:
            handleSpoolDetected(msg);
            break;

        case AppMessageType::SPOOL_UPDATED:
            handleSpoolUpdated(msg);
            break;

        case AppMessageType::BLANK_TAG_DETECTED:
            handleBlankTagDetected(msg);
            break;

        case AppMessageType::GENERIC_TAG_DETECTED:
            handleGenericTagDetected(msg);
            break;

        case AppMessageType::SPOOLMAN_SYNCED:
            handleSpoolmanSynced(msg);
            break;

        case AppMessageType::TAG_REMOVED:
            handleTagRemoved(msg);
            break;

        case AppMessageType::HA_WRITE_TAG:
            handleHAWriteTag(msg);
            break;

        case AppMessageType::HA_UPDATE_REMAINING:
            handleHAUpdateRemaining(msg);
            break;

        case AppMessageType::PRINTER_WARNING:
            handlePrinterWarning(msg);
            break;

        case AppMessageType::KEYPAD_DIGIT:
            handleKeypadDigit(msg);
            break;

        case AppMessageType::KEYPAD_CONFIRM:
            handleKeypadConfirm();
            break;

        case AppMessageType::KEYPAD_CANCEL:
            handleKeypadCancel();
            break;

        case AppMessageType::TRAY_UPDATE:
            handleTrayUpdate();
            break;

        case AppMessageType::TRAY_ASSIGN:
            handleTrayAssign();
            break;
    }
}

// ── Print Lifecycle ─────────────────────────────────────────────────────────

void ApplicationManager::handlePrintStarted(const AppMessage& msg) {
    Serial.printf("EVENT: PrintStarted - job_id=%d\n",
        msg.payload.printStarted.job_id);

    // Trigger fresh NFC spool detection (tag may have changed since last detection)
    NFCManager::getInstance().requestCurrentSpool();

    // Arm spool tracking: remember starting spool to detect mid-print changes and deduct at end
    currentState = AppState::MONITORING_PRINT;
    currentJobId = msg.payload.printStarted.job_id;
    startingSpoolId[0] = '\0';
    spoolChangedDuringPrint = false;

    if (display_) {
        char line2[17];
        snprintf(line2, sizeof(line2), "Job: %d", currentJobId);
        display_->showText("Print Started", line2);
    }

    // Publish printer state to HA
    {
        char json[96];
        snprintf(json, sizeof(json), "{\"state\":\"printing\",\"job_id\":%d}",
                 msg.payload.printStarted.job_id);
        publishToHA("printer/state", json, true);
    }
}

void ApplicationManager::handlePrintEnded(const AppMessage& msg) {
    const auto& pe = msg.payload.printEnded;
    Serial.printf("EVENT: PrintEnded - job_id=%d, filament=%.2fg, canceled=%s, tools=%d\n",
        pe.job_id, pe.filament_used_grams,
        pe.canceled ? "true" : "false", pe.tool_count);

    if (pe.tool_count > 1) {
        for (int i = 0; i < pe.tool_count; i++) {
            Serial.printf("  Tool %d: %.2fg\n", i, pe.filament_per_tool[i]);
        }
    }

    if (currentState == AppState::MONITORING_PRINT) {
        finishPrint(pe.filament_used_grams, pe.canceled);
    }
    currentState = AppState::IDLE;

    // Publish printer state to HA — include per-tool data if multi-head
    {
        char json[384];
        int n = snprintf(json, sizeof(json),
                 "{\"state\":\"idle\",\"last_job_id\":%d,\"filament_used_g\":%.1f",
                 pe.job_id, pe.filament_used_grams);

        // Multi-tool: append per-extruder filament usage (Bambu AMS / toolchanger)
        if (pe.tool_count > 1 && n > 0 && static_cast<size_t>(n) < sizeof(json) - 2) {
            n += snprintf(json + n, sizeof(json) - n, ",\"tool_count\":%d,\"per_tool\":[", pe.tool_count);
            for (int i = 0; i < pe.tool_count && static_cast<size_t>(n) < sizeof(json) - 10; i++) {
                if (i > 0) json[n++] = ',';
                n += snprintf(json + n, sizeof(json) - n, "%.1f", pe.filament_per_tool[i]);
            }
            if (static_cast<size_t>(n) < sizeof(json) - 2) {
                json[n++] = ']';
            }
        }

        // Defensive bounds check: snprintf can return ≥ sizeof(json) on truncation
        if (static_cast<size_t>(n) < sizeof(json) - 2) {
            json[n++] = '}';
            json[n] = '\0';
        }
        publishToHA("printer/state", json, true);
    }
}

// ── NFC Tag Detection ────────────────────────────────────────────────────────

void ApplicationManager::handleSpoolDetected(const AppMessage& msg) {
    Serial.printf("EVENT: SpoolDetected - spool_id=%s, material_type=%u, kg_remaining=%.3f\n",
        msg.payload.spoolDetected.spool_id,
        msg.payload.spoolDetected.material_type,
        msg.payload.spoolDetected.kg_remaining);

    // Clear prior Spoolman enrichment data (UID lookup results, temps, etc.)
    smartTagEnrichment_ = SmartTagEnrichment{};

    #ifndef NATIVE_TEST
    if (ConfigurationManager::getInstance().isLedEnabled()) {
        // Set target first so task restores it after the flash
        float remaining_g = msg.payload.spoolDetected.kg_remaining * 1000.0f;
        if (msg.payload.spoolDetected.has_color) {
            uint8_t r = msg.payload.spoolDetected.primary_color[0];
            uint8_t g = msg.payload.spoolDetected.primary_color[1];
            uint8_t b = msg.payload.spoolDetected.primary_color[2];
            if (remaining_g > 0.0f && remaining_g <= (float)ConfigurationManager::getInstance().getLowSpoolThreshold()) {
                ledManager.breatheFilamentColor(r, g, b);
            } else {
                ledManager.showFilamentColor(r, g, b);
            }
        } else {
            ledManager.showOff();
        }
        ledManager.flashTagDetected();
    }
    #endif

    // Spool tracking for print deduction: detect mid-print changes (multi-material, failed toolchanger)
    if (currentState == AppState::MONITORING_PRINT) {
        if (startingSpoolId[0] == '\0') {
            // First spool read of this print — anchor point for deduction
            strncpy(startingSpoolId, msg.payload.spoolDetected.spool_id, sizeof(startingSpoolId) - 1);
            startingSpoolId[sizeof(startingSpoolId) - 1] = '\0';
            Serial.printf("ApplicationManager: Captured starting spool: %s\n", startingSpoolId);
        } else if (strcmp(startingSpoolId, msg.payload.spoolDetected.spool_id) != 0) {
            // Different spool detected — flag so finishPrint() doesn't auto-update
            spoolChangedDuringPrint = true;
            Serial.printf("ApplicationManager: WARNING - Spool changed during print! Was %s, now %s\n",
                startingSpoolId, msg.payload.spoolDetected.spool_id);
        }
    }

    // Cancel pending deferred displays — new tag supersedes them
    pendingTypeRemainDisplay = false;
    pendingStatusAfterTagRemoved = false;

    // Display: suppress redundant spool updates via spool_id dedup
    if (display_ && strcmp(lastDisplayedSpoolId, msg.payload.spoolDetected.spool_id) != 0) {
        strncpy(lastDisplayedSpoolId, msg.payload.spoolDetected.spool_id, sizeof(lastDisplayedSpoolId) - 1);
        lastDisplayedSpoolId[sizeof(lastDisplayedSpoolId) - 1] = '\0';
        lastDisplayedBlankId[0] = '\0';

        const auto& s = msg.payload.spoolDetected;
        DisplaySpoolData spool{};
        strncpy(spool.brand, s.manufacturer, sizeof(spool.brand) - 1);
        strncpy(spool.material, s.material_name, sizeof(spool.material) - 1);
        snprintf(spool.colorHex, sizeof(spool.colorHex), "%02X%02X%02X",
                 s.primary_color[0], s.primary_color[1], s.primary_color[2]);
        spool.remainingWeight = s.kg_remaining * 1000.0f;
        spool.totalWeight = s.initial_weight_g;
        // Map tag format string to tag type constant
        if (strcmp(s.tag_format, "OpenPrintTag") == 0) spool.tagType = 1;
        else if (strcmp(s.tag_format, "TigerTag") == 0) spool.tagType = 2;
        else if (strcmp(s.tag_format, "OpenTag3D") == 0) spool.tagType = 3;
        else if (strcmp(s.tag_format, "bambu") == 0) spool.tagType = 4;
        else if (strcmp(s.tag_format, "OpenSpool") == 0) spool.tagType = 6;
        else spool.tagType = 0;
        display_->showSpool(spool);

#ifndef NATIVE_TEST
        if (ConfigurationManager::getInstance().isBambuDashboardEnabled() && trayDashboardState_.has_data) {
            dashboardRevertAt_ = millis();
        }
#endif
    } else if (display_) {
        Serial.printf("ApplicationManager: Skipping LCD update for already displayed spool %s\n", msg.payload.spoolDetected.spool_id);
    }

    // Apply any pending filament deductions from middleware (stored in NVS).
    // Enqueues a tag write — updated weight publishes after write completes and re-scan triggers.
#ifndef NATIVE_TEST
    {
        CurrentSpoolState deductState;
        if (NFCManager::getInstance().getCurrentSpoolState(deductState) && deductState.present) {
            float deducted = DeductionManager::getInstance().applyIfPending(
                deductState.spool_id, deductState.kind);
            if (deducted > 0.0f) {
                Serial.printf("ApplicationManager: Applied %.1fg pending deduction to %s\n",
                              deducted, deductState.spool_id);
            }
        }
    }
#endif

    // Publish tag state to HA via shared builder — single source of truth for JSON format
    {
        const auto& s = msg.payload.spoolDetected;
        TagStateFields f = {};
        strncpy(f.uid, s.spool_id, sizeof(f.uid) - 1);
        f.present = true;
        f.tag_data_valid = true;
        f.tag_format = "unknown";
#ifndef NATIVE_TEST
        CurrentSpoolState spoolState = {};
        if (NFCManager::getInstance().getCurrentSpoolState(spoolState))
            f.tag_format = tagKindToMqttFormat(spoolState.kind);
#endif
        strncpy(f.material_type, s.material_name, sizeof(f.material_type) - 1);
        strncpy(f.material_name, s.material_name, sizeof(f.material_name) - 1);
        snprintf(f.color, sizeof(f.color), "#%02X%02X%02X",
                 s.primary_color[0], s.primary_color[1], s.primary_color[2]);
        strncpy(f.manufacturer, s.manufacturer, sizeof(f.manufacturer) - 1);
        f.remaining_g = s.kg_remaining * 1000.0f;
        f.initial_weight_g = s.initial_weight_g;
        f.spoolman_id = s.spoolman_id;
        f.min_print_temp = s.min_print_temp;
        f.max_print_temp = s.max_print_temp;
        f.min_bed_temp = s.min_bed_temp;
        f.max_bed_temp = s.max_bed_temp;
        f.density = s.density;
        f.diameter_mm = s.diameter;

#ifndef NATIVE_TEST
        BambuTagData bambuData;
        if (spoolState.kind == TagKind::BambuTag && NFCManager::getInstance().getLastBambuTagData(bambuData)) {
            strncpy(f.material_name, bambuData.filament_type, sizeof(f.material_name) - 1);
            strncpy(f.material_type, bambuData.filament_type, sizeof(f.material_type) - 1);
            snprintf(f.color, sizeof(f.color), "#%02X%02X%02X",
                     bambuData.color_r, bambuData.color_g, bambuData.color_b);
            f.initial_weight_g = bambuData.weight_g;
            f.remaining_g = bambuData.weight_g;
            f.min_print_temp = bambuData.hotend_min;
            f.max_print_temp = bambuData.hotend_max;
            f.min_bed_temp = bambuData.bed_temp;
            f.max_bed_temp = bambuData.bed_temp;
            f.diameter_mm = bambuData.diameter_mm;
            f.filament_length_m = bambuData.filament_length_m;
        }
#endif

        char json[512];
        buildTagStateJson(json, sizeof(json), f);
        publishToHA("tag/state", json, true);
        strncpy(lastHAStateJson_, json, sizeof(lastHAStateJson_) - 1);
        lastHAStateJson_[sizeof(lastHAStateJson_) - 1] = '\0';
    }

#ifndef NATIVE_TEST
    // Spoolman sync: auto-update remaining weight (SELF_DIRECTED mode only)
    // Suppress flag can gate this per-tag (e.g., after batch write to Spoolman)
    if (automationMode == AutomationMode::SELF_DIRECTED &&
        SpoolmanManager::getInstance().isConfigured() &&
        !msg.payload.spoolDetected.suppress_spoolman_sync) {
        enqueueSpoolmanSync(msg.payload.spoolDetected);
    }
#endif

#ifndef NATIVE_TEST
    // Spoolman UID lookup: enrich smart tag display with database info (temps, density, diameter, etc.)
    // Read-only path: triggered in any mode, any tag that doesn't have suppress flag set
    if (SpoolmanManager::getInstance().isConfigured() &&
        !msg.payload.spoolDetected.suppress_spoolman_sync) {
        const char* fmt = msg.payload.spoolDetected.tag_format;
        // Check if this is a "smart tag" (format stores material data on tag, not generic UID)
        bool isSmart = (strcmp(fmt, "TigerTag") == 0 ||
                        strcmp(fmt, "OpenTag3D") == 0 ||
                        strcmp(fmt, "OpenSpool") == 0 ||
                        strcmp(fmt, "OpenPrintTag") == 0);
        if (isSmart) {
            SpoolmanSyncRequest req = {};
            strncpy(req.spool_id, msg.payload.spoolDetected.spool_id,
                    sizeof(req.spool_id) - 1);
            req.lookup_only = true;  // Read-only: enrich display, don't write back
            SpoolmanManager::getInstance().enqueueSync(req);
        }
    }
#endif
}

// ── Spoolman Integration ────────────────────────────────────────────────────

void ApplicationManager::handleSpoolUpdated(const AppMessage& msg) {
    Serial.printf("EVENT: SpoolUpdated - spool_id=%s, update_type=%u, success=%s\n",
        msg.payload.spoolUpdated.spool_id,
        msg.payload.spoolUpdated.update_type,
        msg.payload.spoolUpdated.success ? "true" : "false");

    // Fetch current spool material name from NFCManager state (for deferred display)
    char materialName[32] = {0};
    float kgRemaining = msg.payload.spoolUpdated.kg_remaining;

    CurrentSpoolState state;
    if (NFCManager::getInstance().getCurrentSpoolState(state) && state.tag_data_valid) {
        opt_get_material_name(&state.tag_data, materialName, sizeof(materialName));
    }
#ifndef NATIVE_TEST
    bool spoolmanConfigured = SpoolmanManager::getInstance().isConfigured();
#else
    bool spoolmanConfigured = false;
#endif

#ifndef NATIVE_TEST
    if (ConfigurationManager::getInstance().isLedEnabled()) {
        // Set target first, then flash — task restores target after flash completes
        if (state.tag_data_valid) {
            uint8_t color[4] = {0};
            if (opt_get_primary_color(&state.tag_data, color) == OPT_OK) {
                ledManager.showFilamentColor(color[0], color[1], color[2]);
            } else {
                ledManager.showOff();
            }
        } else {
            ledManager.showOff();
        }
        if (msg.payload.spoolUpdated.success) {
            ledManager.flashWriteSuccess();
        } else {
            ledManager.flashWriteFailure();
        }
    }
#endif

    if (display_) {
        if (msg.payload.spoolUpdated.success) {
            char line1[17];
            if (kgRemaining > 0.0f) {
                snprintf(line1, sizeof(line1), "Updated: %.0fg", kgRemaining * 1000.0f);
            } else {
                strncpy(line1, "Tag Updated!", sizeof(line1) - 1);
            }
            if (spoolmanConfigured) {
                // Show "syncing" — SPOOLMAN_SYNCED event will show final display + schedule Type/Remain
                display_->showText(line1, "Syncing Spoolman");
            } else {
                // No Spoolman: show final result immediately, then deferred Type/Remain
                char line2[17];
                snprintf(line2, sizeof(line2), "Remain: %.0fg",
                         kgRemaining * 1000.0f);
                display_->showText("Spool Updated!", line2);

                // Schedule Type/Remain display after 5 seconds (no Spoolman path)
                if (materialName[0] != '\0') {
                    scheduleTypeRemainDisplay(materialName, kgRemaining);
                }
            }
        } else {
            display_->showText("Spool Update", "Failed!");
        }
    }

#ifndef NATIVE_TEST
    // Avoid sync loops:
    // - FORMAT_NEW is followed by SPOOL_DETECTED, which already triggers sync
    // - WRITE_SPOOLMAN_ID would cause sync->writeback->sync feedback
    // - suppress_sync flag (used for batched writes like Mode B) prevents syncs
    const uint8_t updateType = msg.payload.spoolUpdated.update_type;
    const bool shouldSyncAfterUpdate =
        !msg.payload.spoolUpdated.suppress_sync &&
        updateType != static_cast<uint8_t>(NFCWriteType::FORMAT_NEW) &&
        updateType != static_cast<uint8_t>(NFCWriteType::WRITE_SPOOLMAN_ID);

    // Invalidate cache after WRITE_SPOOLMAN_ID so the next sync reads the fresh tag value
    if (msg.payload.spoolUpdated.success &&
        updateType == static_cast<uint8_t>(NFCWriteType::WRITE_SPOOLMAN_ID)) {
        SpoolmanManager::getInstance().invalidateCachedSpoolmanId(msg.payload.spoolUpdated.spool_id);
    }

    // After write: sync to Spoolman (SELF_DIRECTED mode) to broadcast updated weight immediately
    if (automationMode == AutomationMode::SELF_DIRECTED &&
        spoolmanConfigured && msg.payload.spoolUpdated.success && shouldSyncAfterUpdate) {
        // Reconstruct SpoolDetectedPayload from NFCManager state (has full tag data)
        CurrentSpoolState state;
        if (NFCManager::getInstance().getCurrentSpoolState(state) && state.tag_data_valid) {
            SpoolDetectedPayload fakeSpool;
            memset(&fakeSpool, 0, sizeof(fakeSpool));
            strncpy(fakeSpool.spool_id, msg.payload.spoolUpdated.spool_id, sizeof(fakeSpool.spool_id) - 1);
            opt_get_material_type(&state.tag_data, &fakeSpool.material_type);
            fakeSpool.kg_remaining = msg.payload.spoolUpdated.kg_remaining;
            opt_get_primary_color(&state.tag_data, fakeSpool.primary_color);
            if (opt_get_density(&state.tag_data, &fakeSpool.density) != OPT_OK) {
                fakeSpool.density = 0.0f;
            }
            if (opt_get_filament_diameter(&state.tag_data, &fakeSpool.diameter) != OPT_OK) {
                fakeSpool.diameter = 0.0f;
            }
            float full_weight = 0.0f;
            opt_get_actual_full_weight(&state.tag_data, &full_weight);
            fakeSpool.initial_weight_g = full_weight;
            if (opt_get_brand_name(&state.tag_data, fakeSpool.manufacturer, sizeof(fakeSpool.manufacturer)) != OPT_OK) {
                fakeSpool.manufacturer[0] = '\0';
            }
            int32_t tagSpoolmanId = -1;
            opt_get_gp_spoolman_id(&state.tag_data, &tagSpoolmanId);
            fakeSpool.spoolman_id = tagSpoolmanId;
            enqueueSpoolmanSync(fakeSpool);
        } else {
            // NFC state transiently unavailable right after write — defer via re-read
            // When SPOOL_DETECTED re-emits, it will trigger the normal sync path
            Serial.println("ApplicationManager: Spool update sync deferred; requesting fresh spool read");
            NFCManager::getInstance().requestCurrentSpool();
        }
    }
#endif
}

// ── Blank/Generic Tags ───────────────────────────────────────────────────────

void ApplicationManager::handleBlankTagDetected(const AppMessage& msg) {
    Serial.printf("EVENT: BlankTagDetected - spool_id=%s\n",
        msg.payload.blankTag.spool_id);
#ifndef NATIVE_TEST
    if (ConfigurationManager::getInstance().isLedEnabled()) {
        // Flash error: target will be restored by LED task after animation completes
        ledManager.showOff();
        ledManager.flashParseFailed();
    }
#endif

    // Cancel status screen timer — new tag event supersedes
    pendingStatusAfterTagRemoved = false;

    // Display: suppress redundant updates via dedup (same blank tag scanned again)
    if (display_ && strcmp(lastDisplayedBlankId, msg.payload.blankTag.spool_id) != 0) {
        strncpy(lastDisplayedBlankId, msg.payload.blankTag.spool_id, sizeof(lastDisplayedBlankId) - 1);
        lastDisplayedBlankId[sizeof(lastDisplayedBlankId) - 1] = '\0';
        lastDisplayedSpoolId[0] = '\0';  // Clear smart tag display — allow re-display if tag swapped

        display_->showText4("**** Spool ****", "*** Scanned ***", "Unknown Tag", "Use app to setup");

#ifndef NATIVE_TEST
        if (ConfigurationManager::getInstance().isBambuDashboardEnabled() && trayDashboardState_.has_data) {
            dashboardRevertAt_ = millis();
        }
#endif
    }

    // HA MQTT: publish blank tag detected
    {
        TagStateFields f = {};
        strncpy(f.uid, msg.payload.blankTag.spool_id, sizeof(f.uid) - 1);
        f.present = true;
        f.tag_format = "unknown";
        f.spoolman_id = -1;
        f.blank = true;

        char json[512];
        buildTagStateJson(json, sizeof(json), f);
        publishToHA("tag/state", json, true);
        strncpy(lastHAStateJson_, json, sizeof(lastHAStateJson_) - 1);
        lastHAStateJson_[sizeof(lastHAStateJson_) - 1] = '\0';
    }
}

void ApplicationManager::handleGenericTagDetected(const AppMessage& msg) {
    Serial.printf("EVENT: GenericTagDetected - uid=%s\n",
        msg.payload.genericTag.spool_id);
#ifndef NATIVE_TEST
    if (ConfigurationManager::getInstance().isLedEnabled()) {
        // Flash detection: target will be restored by LED task
        ledManager.showOff();
        ledManager.flashTagDetected();
    }
#endif

    // Cancel status screen timer — new tag event supersedes
    pendingStatusAfterTagRemoved = false;

    // Display: suppress redundant updates (dedup by UID)
    if (display_ && strcmp(lastDisplayedBlankId, msg.payload.genericTag.spool_id) != 0) {
        strncpy(lastDisplayedBlankId, msg.payload.genericTag.spool_id, sizeof(lastDisplayedBlankId) - 1);
        lastDisplayedBlankId[sizeof(lastDisplayedBlankId) - 1] = '\0';
        lastDisplayedSpoolId[0] = '\0';  // Clear smart tag display — allow re-display if tag swapped

        display_->showText4("**** Spool ****", "*** Scanned ***", "Generic Tag", "Checking Spoolman");

#ifndef NATIVE_TEST
        if (ConfigurationManager::getInstance().isBambuDashboardEnabled() && trayDashboardState_.has_data) {
            dashboardRevertAt_ = millis();
        }
#endif
    }

    // HA MQTT: publish generic tag (UID only, awaiting Spoolman lookup)
    {
        TagStateFields f = {};
        strncpy(f.uid, msg.payload.genericTag.spool_id, sizeof(f.uid) - 1);
        f.present = true;
        f.tag_format = "uid_only";
        f.spoolman_id = -1;

        char json[512];
        buildTagStateJson(json, sizeof(json), f);
        publishToHA("tag/state", json, true);
        strncpy(lastHAStateJson_, json, sizeof(lastHAStateJson_) - 1);
        lastHAStateJson_[sizeof(lastHAStateJson_) - 1] = '\0';
    }

    // Enqueue Spoolman UID lookup (result → SPOOLMAN_SYNCED event)
#ifndef NATIVE_TEST
    if (SpoolmanManager::getInstance().isConfigured()) {
        SpoolmanSyncRequest req = {};
        strncpy(req.spool_id, msg.payload.genericTag.spool_id, sizeof(req.spool_id) - 1);
        req.lookup_only = true;  // Read-only: enrich display, don't write back
        SpoolmanManager::getInstance().enqueueSync(req);
    }
#endif
}

void ApplicationManager::finishPrint(float gramsUsed, bool /*canceled*/) {
    // Deduction: auto-remove filament weight from tag after print (SELF_DIRECTED mode) or HA-controlled
    if (spoolChangedDuringPrint) {
        Serial.println("ApplicationManager: Spool changed during print - not updating weight");
        if (display_) {
            display_->showText("Spool changed!", "No update");
        }
        return;
    }

    if (startingSpoolId[0] == '\0') {
        Serial.println("ApplicationManager: No spool detected during print - not updating weight");
        if (display_) {
            display_->showText("No spool found", "No update");
        }
        return;
    }

    if (gramsUsed > 0) {
        Serial.printf("ApplicationManager: Updating spool %s - removing %.2fg\n",
            startingSpoolId, gramsUsed);

        // Auto-deduction: SELF_DIRECTED mode only (HA controlled mode requires explicit HA command)
        if (automationMode == AutomationMode::SELF_DIRECTED) {
            if (display_) {
                display_->showText("Updating spool..", "");
            }

            // Enqueue write with expected spool ID validation (prevent wrong tag updates)
            NFCWriteRequest request;
            request.request_id = NFCManager::getInstance().generateRequestId();
            request.type = NFCWriteType::REMOVE_WEIGHT;
            strncpy(request.expected_spool_id, startingSpoolId, sizeof(request.expected_spool_id) - 1);
            request.expected_spool_id[sizeof(request.expected_spool_id) - 1] = '\0';
            request.data.grams_to_remove = gramsUsed;

            NFCManager::getInstance().enqueueWrite(request);
        } else {
            // HA controlled: wait for explicit HA command (deduction via middleware)
            if (display_) {
                display_->showText("Print done", "HA controlled");
            }
        }
    } else {
        Serial.println("ApplicationManager: No filament used - not updating spool");
        if (display_) {
            display_->showText("Print done", "No filament used");
        }
    }
}

void ApplicationManager::handleSpoolmanSynced(const AppMessage& msg) {
    Serial.printf("EVENT: SpoolmanSynced - spool_id=%s, success=%s, spoolman_id=%d\n",
        msg.payload.spoolmanSynced.spool_id,
        msg.payload.spoolmanSynced.success ? "true" : "false",
        msg.payload.spoolmanSynced.spoolman_id);

    // Publish resolved spoolman_id to Klipper for toolchanger auto-assign — generic UID tags only.
    // Smart tags (TigerTag, OpenTag3D, OpenSpool, OpenPrintTag) carry their own data and follow
    // a separate write/enrichment flow; we deliberately skip the auto-publish there.
#ifndef NATIVE_TEST
    if (msg.payload.spoolmanSynced.success && msg.payload.spoolmanSynced.is_uid_lookup
            && msg.payload.spoolmanSynced.spoolman_id > 0) {
        CurrentSpoolState pubState;
        if (NFCManager::getInstance().getCurrentSpoolState(pubState)
                && pubState.kind == TagKind::GenericUidTag) {
            publishPendingSpool(msg.payload.spoolmanSynced.spoolman_id);
        }
    }
#endif

    char materialName[32] = {0};
    strncpy(materialName, msg.payload.spoolmanSynced.material_name, sizeof(materialName) - 1);
    float kgRemaining = msg.payload.spoolmanSynced.kg_remaining;

#ifndef NATIVE_TEST
    // Generic tag writeback: populate NFC tag with Spoolman data if lookup succeeded
    if (msg.payload.spoolmanSynced.is_uid_lookup && msg.payload.spoolmanSynced.success) {
        // Safety: verify tag still present and matches UID (may have been removed during Spoolman request)
        CurrentSpoolState spoolState;
        bool tagStillPresent = NFCManager::getInstance().getCurrentSpoolState(spoolState)
                               && spoolState.present
                               && spoolState.kind == TagKind::GenericUidTag
                               && strcmp(spoolState.spool_id, msg.payload.spoolmanSynced.spool_id) == 0;
        if (tagStillPresent) {
            // Populate writeback fields for WRITE_TAG operation (enqueued in web UI)
            GenericTagSpoolInfo info = {};
            strncpy(info.material_type, msg.payload.spoolmanSynced.material_name, sizeof(info.material_type) - 1);
            strncpy(info.manufacturer, msg.payload.spoolmanSynced.manufacturer, sizeof(info.manufacturer) - 1);
            strncpy(info.color_hex, msg.payload.spoolmanSynced.color_hex, sizeof(info.color_hex) - 1);
            info.remaining_weight_g = kgRemaining * 1000.0f;
            info.spoolman_id = msg.payload.spoolmanSynced.spoolman_id;
            info.extruder_temp = msg.payload.spoolmanSynced.extruder_temp;
            info.bed_temp = msg.payload.spoolmanSynced.bed_temp;
            info.valid = true;
            NFCManager::getInstance().setGenericTagSpoolInfo(info);
        }
    }
#endif

    // Display handling: differs for generic UID tags (show Spoolman data) vs smart tags (store enrichment)
    if (display_) {
        if (msg.payload.spoolmanSynced.success && msg.payload.spoolmanSynced.is_uid_lookup) {
            // Success on UID lookup: check if tag is generic or smart to decide display strategy
#ifndef NATIVE_TEST
            CurrentSpoolState state;
            bool gotState = NFCManager::getInstance().getCurrentSpoolState(state);
            bool isGeneric = !gotState || state.kind == TagKind::GenericUidTag;
#else
            bool isGeneric = true;
#endif
            if (isGeneric) {
                // Generic UID tag: no tag data — fill display with Spoolman data
                DisplaySpoolData spool{};
                strncpy(spool.brand, msg.payload.spoolmanSynced.manufacturer, sizeof(spool.brand) - 1);
                strncpy(spool.material, materialName, sizeof(spool.material) - 1);
                const char* colorSrc = msg.payload.spoolmanSynced.color_hex;
                if (colorSrc[0] == '#') colorSrc++;
                strncpy(spool.colorHex, colorSrc, sizeof(spool.colorHex) - 1);
                spool.remainingWeight = kgRemaining * 1000.0f;
                spool.totalWeight = msg.payload.spoolmanSynced.initial_weight_g;
                spool.tagType = 5;  // "Spoolman" pseudo-type for display badge
                display_->showSpool(spool);
            } else {
                // Smart tag: tag already has data (shown by handleSpoolDetected) — just cache enrichment for web UI
                smartTagEnrichment_.valid = true;
                smartTagEnrichment_.spoolman_id = msg.payload.spoolmanSynced.spoolman_id;
                smartTagEnrichment_.remaining_g = kgRemaining * 1000.0f;
                smartTagEnrichment_.bed_temp = msg.payload.spoolmanSynced.bed_temp;
                smartTagEnrichment_.extruder_temp = msg.payload.spoolmanSynced.extruder_temp;
                smartTagEnrichment_.density = msg.payload.spoolmanSynced.density;
                smartTagEnrichment_.diameter_mm = msg.payload.spoolmanSynced.diameter_mm;
                Serial.printf("ApplicationManager: Smart tag enrichment stored — spool %d, %.0fg remaining\n",
                              smartTagEnrichment_.spoolman_id, smartTagEnrichment_.remaining_g);
            }
        } else if (msg.payload.spoolmanSynced.success) {
            // Success on write update (not UID lookup): smart tag — display already set by handleSpoolDetected
        } else if (msg.payload.spoolmanSynced.is_uid_lookup) {
            // Lookup failed: generic tag not in Spoolman — show error for generic only
#ifndef NATIVE_TEST
            CurrentSpoolState state;
            bool gotState = NFCManager::getInstance().getCurrentSpoolState(state);
            bool isGeneric = !gotState || state.kind == TagKind::GenericUidTag;
#else
            bool isGeneric = true;
#endif
            if (isGeneric) {
                display_->showText("Generic Tag", "Not in Spoolman");
            }
            // Smart tag lookup failed: tag has its own data, already displayed — no change needed
        } else {
            // Write update failed: show error with remaining weight
            char line1[17];
            snprintf(line1, sizeof(line1), "Updated: %.0fg",
                     kgRemaining * 1000.0f);
            display_->showText(line1, "Spoolman Error");

            // Schedule Type/Remain display after 5 seconds even on error
            if (materialName[0] != '\0') {
                scheduleTypeRemainDisplay(materialName, kgRemaining);
            }
        }
    }

#ifndef NATIVE_TEST
    // Re-evaluate LED state using Spoolman weight — catches tags without weight data
    // (OpenSpool, GenericUID, TigerTag) where Spoolman knows the remaining weight
    if (msg.payload.spoolmanSynced.success &&
        ConfigurationManager::getInstance().isLedEnabled() &&
        msg.payload.spoolmanSynced.color_hex[0] != '\0') {
        const char* colorSrc = msg.payload.spoolmanSynced.color_hex;
        if (colorSrc[0] == '#') colorSrc++;
        unsigned int r = 0, g = 0, b = 0;
        if (sscanf(colorSrc, "%02x%02x%02x", &r, &g, &b) != 3) { r = g = b = 0; }
        float remaining_g = kgRemaining * 1000.0f;
        if (remaining_g > 0.0f && remaining_g <= (float)ConfigurationManager::getInstance().getLowSpoolThreshold()) {
            ledManager.breatheFilamentColor(r, g, b);
        } else {
            ledManager.showFilamentColor(r, g, b);
        }
    }

    // Cache sync result in NFCManager (used by web UI to mark "synced" on reader page)
    if (msg.payload.spoolmanSynced.success && !msg.payload.spoolmanSynced.is_uid_lookup) {
        NFCManager::getInstance().updateRecentSpoolSyncStatus(
            msg.payload.spoolmanSynced.spool_id, true);
    }

    // Write Spoolman ID back to tag if it's new or changed
    if (msg.payload.spoolmanSynced.success && msg.payload.spoolmanSynced.spoolman_id > 0) {
        CurrentSpoolState state;
        if (NFCManager::getInstance().getCurrentSpoolState(state) && state.tag_data_valid) {
            int32_t existingId = -1;
            opt_get_gp_spoolman_id(&state.tag_data, &existingId);
            if (existingId != msg.payload.spoolmanSynced.spoolman_id) {
                NFCWriteRequest request;
                request.request_id = millis();
                request.type = NFCWriteType::WRITE_SPOOLMAN_ID;
                strncpy(request.expected_spool_id, msg.payload.spoolmanSynced.spool_id,
                        sizeof(request.expected_spool_id) - 1);
                request.expected_spool_id[sizeof(request.expected_spool_id) - 1] = '\0';
                request.data.spoolman_id = msg.payload.spoolmanSynced.spoolman_id;
                NFCManager::getInstance().enqueueWrite(request);
                Serial.printf("ApplicationManager: Enqueued WRITE_SPOOLMAN_ID %d for spool %s\n",
                    msg.payload.spoolmanSynced.spoolman_id, msg.payload.spoolmanSynced.spool_id);
            }
        }
    }

    // Update dashboard tray weight if this UID matches a tray
    if (msg.payload.spoolmanSynced.success && msg.payload.spoolmanSynced.is_uid_lookup) {
        for (uint8_t i = 0; i < trayDashboardState_.tray_count; i++) {
            if (strlen(trayDashboardState_.trays[i].uid) > 0 &&
                strcasecmp(trayDashboardState_.trays[i].uid, msg.payload.spoolmanSynced.spool_id) == 0) {
                uint16_t weightG = static_cast<uint16_t>(msg.payload.spoolmanSynced.kg_remaining * 1000.0f);
                if (weightG != trayDashboardState_.trays[i].weight_g) {
                    trayDashboardState_.trays[i].weight_g = weightG;
                    Serial.printf("ApplicationManager: Dashboard tray %d weight updated to %dg\n",
                                  trayDashboardState_.trays[i].tray_index, weightG);
#ifndef NATIVE_TEST
                    Preferences prefs;
                    prefs.begin("spoolsense", false);
                    prefs.putBytes("tray_dash", &trayDashboardState_, sizeof(TrayDashboardState));
                    prefs.end();

                    bool dashEnabled = ConfigurationManager::getInstance().isBambuDashboardEnabled();
                    if (dashEnabled && display_) {
                        display_->showTrayDashboard(trayDashboardState_);
                    }
#endif
                }
                break;
            }
        }
    }
#endif
}

// ── Tag Lifecycle ───────────────────────────────────────────────────────────

void ApplicationManager::handleTagRemoved(const AppMessage& msg) {
    Serial.printf("EVENT: TagRemoved - spool_id=%s\n",
        msg.payload.tagRemoved.spool_id);

    // Persist smartTagEnrichment_ for reader page access after tag removal
    // (enrichment data is read-only, safe to keep across scan cycles)

    // LED: keep showing last filament color until next scan (user sees color change on tag swap)

    // Clear dedup state so next scan re-displays (even if same spool reinserted)
    lastDisplayedSpoolId[0] = '\0';
    lastDisplayedBlankId[0] = '\0';
#ifndef NATIVE_TEST
    NFCManager::getInstance().clearGenericTagSpoolInfo();
#endif
    // Arm deferred status screen: show status after 5s delay (debounce rapid re-taps)
    tagRemovedAtMs = millis();
    pendingStatusAfterTagRemoved = true;

    // HA MQTT: publish tag removed state by flipping present:false in cached JSON
    if (lastHAStateJson_[0] != '\0') {
        char removed[512];
        char* pos = strstr(lastHAStateJson_, "\"present\":true");
        if (pos) {
            // Replace "present":true with "present":false, keep rest of payload
            size_t prefix_len = pos - lastHAStateJson_;
            memcpy(removed, lastHAStateJson_, prefix_len);
            snprintf(removed + prefix_len, sizeof(removed) - prefix_len,
                     "\"present\":false%s",
                     pos + strlen("\"present\":true"));
            publishToHA("tag/state", removed, true);
        } else {
            publishToHA("tag/state", lastHAStateJson_, true);
        }
    } else {
        // Fallback: no prior state cached
        char emptyJson[256];
        buildEmptyTagStateJson(emptyJson, sizeof(emptyJson));
        publishToHA("tag/state", emptyJson, true);
    }
}

// ── Home Assistant Integration ───────────────────────────────────────────────

void ApplicationManager::handleHAWriteTag(const AppMessage& msg) {
    Serial.printf("EVENT: HAWriteTag - expected_uid=%s\n",
        msg.payload.haWriteTag.expected_uid);

    // Batch write: HA sends spool data as atomic update (temperature-gated in HA automation)
    const auto& p = msg.payload.haWriteTag;

    // Enqueue field-by-field write requests: NFCManager batches them and monitors for success

    // Material type
    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = NFCManager::getInstance().generateRequestId();
    req.type = NFCWriteType::CHANGE_FILAMENT_TYPE;
    strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
    req.data.new_material_type = p.material_type;
    NFCManager::getInstance().enqueueWrite(req);

    // Color
    memset(&req, 0, sizeof(req));
    req.request_id = NFCManager::getInstance().generateRequestId();
    req.type = NFCWriteType::CHANGE_COLOR;
    strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
    memcpy(req.data.new_color, p.color, 4);
    NFCManager::getInstance().enqueueWrite(req);

    // Brand name
    memset(&req, 0, sizeof(req));
    req.request_id = NFCManager::getInstance().generateRequestId();
    req.type = NFCWriteType::SET_BRAND_NAME;
    strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
    strncpy(req.data.brand_name, p.manufacturer, sizeof(req.data.brand_name) - 1);
    NFCManager::getInstance().enqueueWrite(req);

    // Remaining weight: compute consumed = initial - remaining (tag stores "used", not "remaining")
    if (p.initial_weight_g > 0) {
        float consumed = p.initial_weight_g - p.remaining_g;
        if (consumed < 0) consumed = 0;  // Guard against negative (can happen if tag re-wound)
        memset(&req, 0, sizeof(req));
        req.request_id = NFCManager::getInstance().generateRequestId();
        req.type = NFCWriteType::SET_CONSUMED_WEIGHT;
        strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
        req.data.consumed_weight = consumed;
        NFCManager::getInstance().enqueueWrite(req);
    }

    // Spoolman ID: optional write (HA may not have looked up ID yet)
    if (p.spoolman_id > 0) {
        memset(&req, 0, sizeof(req));
        req.request_id = NFCManager::getInstance().generateRequestId();
        req.type = NFCWriteType::WRITE_SPOOLMAN_ID;
        strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
        req.data.spoolman_id = p.spoolman_id;
        NFCManager::getInstance().enqueueWrite(req);
    }
}

void ApplicationManager::handleHAUpdateRemaining(const AppMessage& msg) {
    // HA-controlled weight update: used when print ends or mid-print deduction is requested
    Serial.printf("EVENT: HAUpdateRemaining - expected_uid=%s, consumed_g=%.2f\n",
        msg.payload.haUpdateRemaining.expected_uid,
        msg.payload.haUpdateRemaining.consumed_g);

    const auto& p = msg.payload.haUpdateRemaining;

    // HA computed consumed = initial - remaining on its side before sending this command
    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = NFCManager::getInstance().generateRequestId();
    req.type = NFCWriteType::SET_CONSUMED_WEIGHT;
    strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
    req.data.consumed_weight = p.consumed_g;
    NFCManager::getInstance().enqueueWrite(req);
}

// ── Spoolman Sync Helper ────────────────────────────────────────────────────

#ifndef NATIVE_TEST
void ApplicationManager::enqueueSpoolmanSync(const SpoolDetectedPayload& spool) {
    // Build Spoolman sync request from smart tag data
    SpoolmanSyncRequest req;
    memset(&req, 0, sizeof(req));
    strncpy(req.spool_id, spool.spool_id, sizeof(req.spool_id) - 1);
    req.material_type = spool.material_type;
    strncpy(req.manufacturer, spool.manufacturer, sizeof(req.manufacturer) - 1);
    memcpy(req.color, spool.primary_color, 4);
    req.remaining_weight_g = spool.kg_remaining * 1000.0f;
    req.initial_weight_g = spool.initial_weight_g;

    // Use tag density if available; fallback to material defaults if missing
    if (spool.density > 0.0f) {
        req.density = spool.density;
    } else {
        // Conservative defaults: PLA/PETG > ABS (accounts for different filament shrinkage)
        switch (spool.material_type) {
            case OPT_MATERIAL_TYPE_PLA:  req.density = 1.24f; break;
            case OPT_MATERIAL_TYPE_PETG: req.density = 1.27f; break;
            case OPT_MATERIAL_TYPE_ABS:  req.density = 1.04f; break;
            default: req.density = 1.20f; break;
        }
    }

    // Use tag diameter if available, otherwise default 1.75mm (standard filament)
    req.diameter = (spool.diameter > 0.0f) ? spool.diameter : 1.75f;

    req.spoolman_id = spool.spoolman_id;

    // Optional tag fields (may be 0/empty if not stored on tag)
    strncpy(req.material_name, spool.material_name, sizeof(req.material_name) - 1);
    req.min_print_temp = spool.min_print_temp;
    req.max_print_temp = spool.max_print_temp;
    req.min_bed_temp = spool.min_bed_temp;
    req.max_bed_temp = spool.max_bed_temp;

    strncpy(req.aspect, spool.aspect, sizeof(req.aspect) - 1);
    req.dry_temp = spool.dry_temp;
    req.dry_time_hours = spool.dry_time_hours;
    strncpy(req.tag_format, spool.tag_format, sizeof(req.tag_format) - 1);

    SpoolmanManager::getInstance().enqueueSync(req);
}
#endif

// ── Printer Integration ──────────────────────────────────────────────────────

void ApplicationManager::handlePrinterWarning(const AppMessage& msg) {
    // Printer mismatch alerts: filament name/ID, temperature exceedance (from middleware blueprints)
    const auto& w = msg.payload.printerWarning;
    Serial.printf("EVENT: PrinterWarning type=%s expected=%s actual=%s\n",
                  w.warning_type, w.expected, w.actual);

    // Show on LCD
    if (display_) {
        if (strcmp(w.warning_type, "filament_mismatch") == 0) {
            char line2[17];
            snprintf(line2, sizeof(line2), "%.3s!=%.3s WRONG!", w.expected, w.actual);
            display_->showText("WRONG FILAMENT!", line2);
        } else if (strcmp(w.warning_type, "temp_exceeds_max") == 0) {
            char line2[17];
            snprintf(line2, sizeof(line2), "%.0fC>%dC max", w.gcode_temp, w.tag_max_temp);
            display_->showText("TEMP WARNING!", line2);
        }
    }

#ifndef NATIVE_TEST
    if (ConfigurationManager::getInstance().isLedEnabled()) {
        ledManager.flashWarning();
    }
#endif

    // Publish to HA for automation / alerting
    char json[192];
    if (strcmp(w.warning_type, "filament_mismatch") == 0) {
        snprintf(json, sizeof(json),
                 "{\"warning\":\"%s\",\"expected\":\"%s\",\"loaded\":\"%s\"}",
                 w.warning_type, w.expected, w.actual);
    } else {
        snprintf(json, sizeof(json),
                 "{\"warning\":\"%s\",\"gcode_temp\":%.0f,\"tag_max\":%d}",
                 w.warning_type, w.gcode_temp, w.tag_max_temp);
    }
    publishToHA("printer/warning", json, false);
}

// ── MQTT Publish Helper ──────────────────────────────────────────────────────

void ApplicationManager::publishToHA(const char* topicSuffix, const char* payload, bool retained) {
    // HA MQTT: enqueue publish request (non-blocking; HomeAssistantManager handles batching)
#ifndef NATIVE_TEST
    auto& ha = HomeAssistantManager::getInstance();
    if (!ha.isConfigured()) return;

    HAPublishRequest req;
    memset(&req, 0, sizeof(req));

    char deviceId[7];
    HomeAssistantManager::getDeviceId(deviceId, sizeof(deviceId));
    snprintf(req.topic, sizeof(req.topic), "spoolsense/%s/%s", deviceId, topicSuffix);
    strncpy(req.payload, payload, sizeof(req.payload) - 1);
    req.retained = retained;  // Retained: broker keeps last value for new subscribers

    ha.enqueuePublish(req);
#else
    (void)topicSuffix;
    (void)payload;
    (void)retained;
#endif
}

// ── Keypad handlers ─────────────────────────────────────────────────────────

void ApplicationManager::handleKeypadDigit(const AppMessage& msg) {
    // Keypad input: accumulate tool number digits (0-9)
    char digit = msg.payload.keypadDigit.digit;
    Serial.printf("EVENT: KeypadDigit - '%c'\n", digit);
    pendingKeypadPrompt = false;  // User is typing — suppress status screen prompt

    // Append digit to buffer (capped at 16 chars)
    if (keypadBufferLen_ < sizeof(keypadBuffer_) - 1) {
        keypadBuffer_[keypadBufferLen_++] = digit;
        keypadBuffer_[keypadBufferLen_] = '\0';
    }

    if (display_) {
        display_->showKeypad(keypadBuffer_);
    }
}

void ApplicationManager::handleKeypadConfirm() {
    // Keypad confirm (#): send ASSIGN_SPOOL gcode to Moonraker with accumulated tool number
    Serial.println("EVENT: KeypadConfirm");

    if (keypadBufferLen_ == 0) {
        if (display_) display_->showText("No tool entered", "Type number + #");
        return;
    }

#ifndef NATIVE_TEST
    // Safety: require a recent spool scan (doesn't need to be currently present on reader)
    CurrentSpoolState state;
    bool hasScannedSpool = NFCManager::getInstance().getCurrentSpoolState(state) &&
                           (state.present || state.spool_id[0] != '\0');
    if (!hasScannedSpool) {
        if (display_) display_->showText("No spool scanned", "Scan tag first");
        keypadBuffer_[0] = '\0';
        keypadBufferLen_ = 0;
        return;
    }
#endif

    // Send ASSIGN_SPOOL to Moonraker (HTTP mutex acquired in sendAssignSpool)
    if (sendAssignSpool(keypadBuffer_)) {
        if (display_) {
            char line[17];
            snprintf(line, sizeof(line), "Assigned T%s", keypadBuffer_);
            display_->showText(line, "OK");
        }
    }

    // Clear buffer after completion
    keypadBuffer_[0] = '\0';
    keypadBufferLen_ = 0;
}

void ApplicationManager::handleKeypadCancel() {
    // Keypad cancel (*): clear accumulated digits
    Serial.println("EVENT: KeypadCancel");
    keypadBuffer_[0] = '\0';
    keypadBufferLen_ = 0;

    if (display_) {
        display_->showText("Tool entry", "Cleared");
    }
}

// ── Tray Dashboard ──────────────────────────────────────────────────────────

void ApplicationManager::updateTrayDashboard(const TrayDashboardState& state) {
    trayDashboardState_ = state;
}

const TrayDashboardState& ApplicationManager::getTrayDashboardState() const {
    return trayDashboardState_;
}

void ApplicationManager::handleTrayUpdate() {
#ifndef NATIVE_TEST
    // Persist to NVS
    Preferences prefs;
    prefs.begin("spoolsense", false);
    prefs.putBytes("tray_dash", &trayDashboardState_, sizeof(TrayDashboardState));
    prefs.end();
#endif

    Serial.printf("ApplicationManager: Tray dashboard updated, %d trays\n",
                  trayDashboardState_.tray_count);

#ifndef NATIVE_TEST
    bool dashEnabled = ConfigurationManager::getInstance().isBambuDashboardEnabled();
#else
    bool dashEnabled = false;
#endif
    if (dashEnabled && display_) {
        display_->showTrayDashboard(trayDashboardState_);
    }
}

void ApplicationManager::handleTrayAssign() {
    uint8_t idx = pendingAssignTrayIndex_;
    if (idx >= MAX_TRAYS) {
        Serial.printf("ApplicationManager: tray_assign rejected — index %d out of range\n", idx);
        return;
    }

    for (uint8_t i = 0; i < trayDashboardState_.tray_count; i++) {
        if (trayDashboardState_.trays[i].tray_index == idx) {
            strncpy(trayDashboardState_.trays[i].uid, pendingAssignUid_, sizeof(trayDashboardState_.trays[i].uid) - 1);
            trayDashboardState_.trays[i].uid[sizeof(trayDashboardState_.trays[i].uid) - 1] = '\0';
            trayDashboardState_.trays[i].spoolman_id = pendingAssignSpoolmanId_;

            Serial.printf("ApplicationManager: Tray %d assigned UID=%s spoolman_id=%d\n",
                          idx, pendingAssignUid_, pendingAssignSpoolmanId_);

#ifndef NATIVE_TEST
            Preferences prefs;
            prefs.begin("spoolsense", false);
            prefs.putBytes("tray_dash", &trayDashboardState_, sizeof(TrayDashboardState));
            prefs.end();

            if (strlen(pendingAssignUid_) > 0 && SpoolmanManager::getInstance().isConfigured()) {
                SpoolmanSyncRequest req = {};
                strncpy(req.spool_id, pendingAssignUid_, sizeof(req.spool_id) - 1);
                req.lookup_only = true;
                SpoolmanManager::getInstance().enqueueSync(req);
            }
#endif
            return;
        }
    }

    Serial.printf("ApplicationManager: tray_assign — index %d not in current dashboard, assignment ignored\n", idx);
}

int ApplicationManager::postGcodeScript(const char* gcode, uint32_t mutexTimeoutMs) {
#ifndef NATIVE_TEST
    const char* moonrakerUrl = ConfigurationManager::getInstance().getMoonrakerURL();
    if (!moonrakerUrl || moonrakerUrl[0] == '\0') return -1000;

    extern SemaphoreHandle_t g_httpMutex;
    if (g_httpMutex && xSemaphoreTake(g_httpMutex, pdMS_TO_TICKS(mutexTimeoutMs)) != pdTRUE) {
        return -1001;
    }

    char url[192];
    snprintf(url, sizeof(url), "%s/printer/gcode/script", moonrakerUrl);

    char postBody[320];
    snprintf(postBody, sizeof(postBody), "{\"script\":\"%s\"}", gcode);

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(1000);
    http.setTimeout(2000);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(postBody);
    http.end();

    if (g_httpMutex) xSemaphoreGive(g_httpMutex);
    return code;
#else
    (void)gcode; (void)mutexTimeoutMs;
    return 200;
#endif
}

bool ApplicationManager::sendAssignSpool(const char* toolNumber) {
    // Send ASSIGN_SPOOL TOOL=Tn gcode to Moonraker (Klipper-AFC integration, keypad path)
#ifndef NATIVE_TEST
    // Input validation: tool number must be digits-only (prevent GCode injection)
    for (const char* p = toolNumber; *p; p++) {
        if (*p < '0' || *p > '9') {
            Serial.printf("ApplicationManager: Invalid tool number '%s'\n", toolNumber);
            return false;
        }
    }

    char gcode[64];
    snprintf(gcode, sizeof(gcode), "ASSIGN_SPOOL TOOL=T%s", toolNumber);

    // Keypad is human-paced — wait up to 3s for the HTTP mutex
    int code = postGcodeScript(gcode, 3000);

    if (code == -1000) {
        Serial.println("ApplicationManager: Moonraker URL not configured — cannot assign spool");
        if (display_) display_->showText("Moonraker URL", "Not configured");
        return false;
    }
    if (code == -1001) {
        Serial.println("ApplicationManager: Could not acquire HTTP mutex for ASSIGN_SPOOL");
        if (display_) display_->showText("Assign failed", "HTTP busy");
        return false;
    }

    Serial.printf("ApplicationManager: ASSIGN_SPOOL T%s — HTTP %d\n", toolNumber, code);

    if (code != 200) {
        if (display_) display_->showText("Assign failed", "Check Moonraker");
        return false;
    }
    return true;
#else
    (void)toolNumber;
    return true;  // Native test: pretend success
#endif
}

bool ApplicationManager::publishPendingSpool(int spoolmanId) {
    // Publish scanned spool to Klipper via Moonraker — enables toolchanger auto-assign (Snapmaker U1 etc.)
    // Runs on every successful generic-UID Spoolman lookup, so this path must NOT block the dispatch loop.
#ifndef NATIVE_TEST
    if (spoolmanId <= 0) return false;

    // Backoff: skip if Moonraker was unreachable recently (avoid 1s connect + 2s timeout on every scan)
    uint32_t now = millis();
    if (moonrakerBackoffUntilMs_ != 0 && (int32_t)(now - moonrakerBackoffUntilMs_) < 0) {
        return false;
    }

    // Two SET_GCODE_VARIABLE calls in one script: spoolman ID + monotonic timestamp for macro-side TTL
    uint32_t ts = (uint32_t)(now / 1000);
    char gcode[192];
    snprintf(gcode, sizeof(gcode),
             "SET_GCODE_VARIABLE MACRO=SPOOLSENSE_STATE VARIABLE=pending_spool_id VALUE=%d\\n"
             "SET_GCODE_VARIABLE MACRO=SPOOLSENSE_STATE VARIABLE=pending_ts VALUE=%u",
             spoolmanId, (unsigned)ts);

    // Auto-publish runs on every scan — short mutex wait + skip-if-busy
    int code = postGcodeScript(gcode, 250);

    Serial.printf("ApplicationManager: publishPendingSpool id=%d — HTTP %d\n", spoolmanId, code);

    if (code == -1000 || code == -1001) {
        // No URL configured or mutex busy — neither warrants a backoff window
        return false;
    }
    if (code < 0) {
        // HTTPClient transport failure (connect refused, timeout, etc.) — back off so the next
        // scan doesn't re-block the dispatch loop on the same unreachable host
        moonrakerBackoffUntilMs_ = millis() + MOONRAKER_BACKOFF_MS;
        Serial.printf("ApplicationManager: Moonraker unreachable — backing off %u ms\n",
                      (unsigned)MOONRAKER_BACKOFF_MS);
        return false;
    }
    // Server responded — clear any prior backoff regardless of HTTP status
    moonrakerBackoffUntilMs_ = 0;
    return code == 200;
#else
    (void)spoolmanId;
    return true;
#endif
}
