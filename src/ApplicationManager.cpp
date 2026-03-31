#include "ApplicationManager.h"
#include "UserConfig.h"
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
  extern LEDManager ledManager;
#else
  #include "platform/NativePlatform.h"
  #include "FakeLCDManager.h"
  #include "TestNFCManager.h"
#endif
#include <cstring>

#ifdef NATIVE_TEST
static constexpr uint32_t TAG_REMOVED_STATUS_DELAY_MS = 25;
static constexpr uint32_t TYPE_REMAIN_DISPLAY_DELAY_MS = 25;
#else
static constexpr uint32_t TAG_REMOVED_STATUS_DELAY_MS = 5000;
static constexpr uint32_t TYPE_REMAIN_DISPLAY_DELAY_MS = 5000;
#endif

ApplicationManager& ApplicationManager::getInstance() {
    static ApplicationManager instance;
    return instance;
}

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
    return true;
}

bool ApplicationManager::sendMessage(const AppMessage& msg, uint32_t waitMs) {
    if (messageQueue == nullptr) {
        Serial.println("ApplicationManager: Queue not initialized");
        return false;
    }

    TickType_t ticksToWait = (waitMs == 0) ? 0 : pdMS_TO_TICKS(waitMs);
    BaseType_t result = xQueueSend(messageQueue, &msg, ticksToWait);
    return result == pdTRUE;
}

void ApplicationManager::processMessages() {
    if (messageQueue == nullptr) {
        return;
    }

    AppMessage msg;
    while (xQueueReceive(messageQueue, &msg, 0) == pdTRUE) {
        handleMessage(msg);
    }

    if (pendingStatusAfterTagRemoved) {
        uint32_t elapsedMs = static_cast<uint32_t>(millis() - tagRemovedAtMs);
        if (elapsedMs >= TAG_REMOVED_STATUS_DELAY_MS) {
            // Don't overwrite display if user is typing a tool number.
            // On TFT, keep showing spool data — screen timeout handles dimming.
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


    // Check for delayed Type/Remain display
    if (pendingTypeRemainDisplay && display_) {
        uint32_t elapsedMs = static_cast<uint32_t>(millis() - typeRemainScheduledAtMs);
        if (elapsedMs >= TYPE_REMAIN_DISPLAY_DELAY_MS) {
            char line1[17];
            char line2[17];
            snprintf(line1, sizeof(line1), "Type: %.10s", delayedDisplayMaterialName);
            if (ConfigurationManager::getInstance().isKeypadEnabled()) {
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

}

void ApplicationManager::showStatusScreen() {
    if (display_ == nullptr) {
        return;
    }

#ifndef NATIVE_TEST
    auto& config = ConfigurationManager::getInstance();

    char wifiInd;
    if (strlen(config.getWiFiSSID()) == 0) {
        wifiInd = '?';
    } else {
        wifiInd = (WiFi.status() == WL_CONNECTED) ? '+' : '!';
    }

    char smInd;
    if (strlen(config.getSpoolmanURL()) == 0) {
        smInd = '?';
    } else {
        smInd = '+';
    }

    char haInd;
    if (!config.getHAEnabled()) {
        haInd = '?';
    } else if (strlen(config.getHAMqttHost()) == 0) {
        haInd = '!';
    } else {
        haInd = HomeAssistantManager::getInstance().isConnected() ? '+' : '!';
    }
#else
    // Native tests do not initialize network integrations.
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
    strncpy(delayedDisplayMaterialName, material_name, sizeof(delayedDisplayMaterialName) - 1);
    delayedDisplayMaterialName[sizeof(delayedDisplayMaterialName) - 1] = '\0';
    delayedDisplayKgRemaining = kg_remaining;
    pendingTypeRemainDisplay = true;
    typeRemainScheduledAtMs = millis();
}

void ApplicationManager::handleMessage(const AppMessage& msg) {
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
    }
}

void ApplicationManager::handlePrintStarted(const AppMessage& msg) {
    Serial.printf("EVENT: PrintStarted - job_id=%d\n",
        msg.payload.printStarted.job_id);

    // Request fresh spool detection
    NFCManager::getInstance().requestCurrentSpool();

    // Transition to monitoring state
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

        if (static_cast<size_t>(n) < sizeof(json) - 2) {
            json[n++] = '}';
            json[n] = '\0';
        }
        publishToHA("printer/state", json, true);
    }
}

void ApplicationManager::handleSpoolDetected(const AppMessage& msg) {
    Serial.printf("EVENT: SpoolDetected - spool_id=%s, material_type=%u, kg_remaining=%.3f\n",
        msg.payload.spoolDetected.spool_id,
        msg.payload.spoolDetected.material_type,
        msg.payload.spoolDetected.kg_remaining);
    
    #ifndef NATIVE_TEST
    if (ConfigurationManager::getInstance().isLedEnabled()) {
        // Set target first so task restores it after the flash
        float remaining_g = msg.payload.spoolDetected.kg_remaining * 1000.0f;
        if (msg.payload.spoolDetected.has_color) {
            uint8_t r = msg.payload.spoolDetected.primary_color[0];
            uint8_t g = msg.payload.spoolDetected.primary_color[1];
            uint8_t b = msg.payload.spoolDetected.primary_color[2];
            if (remaining_g > 0.0f && remaining_g <= 100.0f) {
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

    if (currentState == AppState::MONITORING_PRINT) {
        if (startingSpoolId[0] == '\0') {
            // First spool detected during this print - capture it
            strncpy(startingSpoolId, msg.payload.spoolDetected.spool_id, sizeof(startingSpoolId) - 1);
            startingSpoolId[sizeof(startingSpoolId) - 1] = '\0';
            Serial.printf("ApplicationManager: Captured starting spool: %s\n", startingSpoolId);
        } else if (strcmp(startingSpoolId, msg.payload.spoolDetected.spool_id) != 0) {
            // Different spool detected during print
            spoolChangedDuringPrint = true;
            Serial.printf("ApplicationManager: WARNING - Spool changed during print! Was %s, now %s\n",
                startingSpoolId, msg.payload.spoolDetected.spool_id);
        }
    }

    // Clear any pending delayed Type/Remain display (new spool detected)
    pendingTypeRemainDisplay = false;
    pendingStatusAfterTagRemoved = false;

    // Update display with spool info (dedupe by spool_id)
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
        else spool.tagType = 0;
        display_->showSpool(spool);
    } else if (display_) {
        Serial.printf("ApplicationManager: Skipping LCD update for already displayed spool %s\n", msg.payload.spoolDetected.spool_id);
    }

    // Publish tag state to HA (always, regardless of mode)
    {
        const auto& s = msg.payload.spoolDetected;
        char colorHex[8];
        snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X",
                 s.primary_color[0], s.primary_color[1], s.primary_color[2]);
        char json[512];
        int len = snprintf(json, sizeof(json),
                 "{\"uid\":\"%s\",\"present\":true,\"tag_data_valid\":true,"
                 "\"material_type\":\"%s\","
                 "\"material_name\":\"%s\",\"color\":\"%s\",\"manufacturer\":\"%s\","
                 "\"remaining_g\":%.1f,\"initial_weight_g\":%.1f,\"spoolman_id\":%d,"
                 "\"blank\":false",
                 s.spool_id, s.material_name, s.material_name, colorHex,
                 s.manufacturer, s.kg_remaining * 1000.0f, s.initial_weight_g,
                 s.spoolman_id);
        if (s.min_print_temp != 0) len += snprintf(json + len, sizeof(json) - len, ",\"min_print_temp\":%d", s.min_print_temp);
        if (s.max_print_temp != 0) len += snprintf(json + len, sizeof(json) - len, ",\"max_print_temp\":%d", s.max_print_temp);
        if (s.min_bed_temp != 0)   len += snprintf(json + len, sizeof(json) - len, ",\"min_bed_temp\":%d", s.min_bed_temp);
        if (s.max_bed_temp != 0)   len += snprintf(json + len, sizeof(json) - len, ",\"max_bed_temp\":%d", s.max_bed_temp);
        if (s.density > 0.0f)      len += snprintf(json + len, sizeof(json) - len, ",\"density\":%.2f", s.density);
        if (s.diameter > 0.0f)     len += snprintf(json + len, sizeof(json) - len, ",\"diameter_mm\":%.2f", s.diameter);
        snprintf(json + len, sizeof(json) - len, "}");
        publishToHA("tag/state", json, true);
        strncpy(lastHAStateJson_, json, sizeof(lastHAStateJson_) - 1);
        lastHAStateJson_[sizeof(lastHAStateJson_) - 1] = '\0';
    }

#ifndef NATIVE_TEST
    // Trigger Spoolman sync if configured (only in SELF_DIRECTED mode)
    // Skip if suppress_spoolman_sync is set (e.g., after write_spoolman_spool batch)
    if (automationMode == AutomationMode::SELF_DIRECTED &&
        SpoolmanManager::getInstance().isConfigured() &&
        !msg.payload.spoolDetected.suppress_spoolman_sync) {
        enqueueSpoolmanSync(msg.payload.spoolDetected);
    }
#endif
}

void ApplicationManager::handleSpoolUpdated(const AppMessage& msg) {
    Serial.printf("EVENT: SpoolUpdated - spool_id=%s, update_type=%u, success=%s\n",
        msg.payload.spoolUpdated.spool_id,
        msg.payload.spoolUpdated.update_type,
        msg.payload.spoolUpdated.success ? "true" : "false");

    // Get current spool state for material name (needed for delayed display)
    char materialName[32] = {0};
    float kgRemaining = msg.payload.spoolUpdated.kg_remaining;

#ifndef NATIVE_TEST
    CurrentSpoolState state;
    if (NFCManager::getInstance().getCurrentSpoolState(state) && state.tag_data_valid) {
        opt_get_material_name(&state.tag_data, materialName, sizeof(materialName));
    }
    bool spoolmanConfigured = SpoolmanManager::getInstance().isConfigured();
#else
    // In test mode, also try to get material name for delayed display testing
    CurrentSpoolState state;
    if (NFCManager::getInstance().getCurrentSpoolState(state) && state.tag_data_valid) {
        opt_get_material_name(&state.tag_data, materialName, sizeof(materialName));
    }
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
            snprintf(line1, sizeof(line1), "Updated: %.0fg",
                     kgRemaining * 1000.0f);
            if (spoolmanConfigured) {
                display_->showText(line1, "Syncing Spoolman");
                // Type/Remain will be scheduled after SPOOLMAN_SYNCED
            } else {
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

    // Trigger Spoolman sync after relevant successful tag updates (only in SELF_DIRECTED mode)
    if (automationMode == AutomationMode::SELF_DIRECTED &&
        spoolmanConfigured && msg.payload.spoolUpdated.success && shouldSyncAfterUpdate) {
        // Read current spool state from NFCManager to get full tag data
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
            // NFC state may be temporarily unavailable right after a write.
            // Force a re-read so SPOOL_DETECTED re-emits with full tag data and triggers sync.
            Serial.println("ApplicationManager: Spool update sync deferred; requesting fresh spool read");
            NFCManager::getInstance().requestCurrentSpool();
        }
    }
#endif
}

void ApplicationManager::handleBlankTagDetected(const AppMessage& msg) {
    Serial.printf("EVENT: BlankTagDetected - spool_id=%s\n",
        msg.payload.blankTag.spool_id);
#ifndef NATIVE_TEST
    if (ConfigurationManager::getInstance().isLedEnabled()) {
        ledManager.showOff();          // target = OFF, restored after flash
        ledManager.flashParseFailed();
    }
#endif

    pendingStatusAfterTagRemoved = false;

    if (display_ && strcmp(lastDisplayedBlankId, msg.payload.blankTag.spool_id) != 0) {
        strncpy(lastDisplayedBlankId, msg.payload.blankTag.spool_id, sizeof(lastDisplayedBlankId) - 1);
        lastDisplayedBlankId[sizeof(lastDisplayedBlankId) - 1] = '\0';
        lastDisplayedSpoolId[0] = '\0';  // Clear so valid spool re-displays if swapped

        display_->showText4("**** Spool ****", "*** Scanned ***", "Unknown Tag", "Use app to setup");
    }

    // Publish blank tag state to HA
    {
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"uid\":\"%s\",\"present\":true,\"tag_data_valid\":false,"
                 "\"material_type\":\"\","
                 "\"material_name\":\"\",\"color\":\"\",\"manufacturer\":\"\","
                 "\"remaining_g\":0.0,\"initial_weight_g\":0.0,\"spoolman_id\":-1,"
                 "\"blank\":true}",
                 msg.payload.blankTag.spool_id);
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
        ledManager.showOff();          // target = OFF, restored after flash
        ledManager.flashTagDetected();
    }
#endif

    pendingStatusAfterTagRemoved = false;

    if (display_ && strcmp(lastDisplayedBlankId, msg.payload.genericTag.spool_id) != 0) {
        strncpy(lastDisplayedBlankId, msg.payload.genericTag.spool_id, sizeof(lastDisplayedBlankId) - 1);
        lastDisplayedBlankId[sizeof(lastDisplayedBlankId) - 1] = '\0';
        lastDisplayedSpoolId[0] = '\0';

        display_->showText4("**** Spool ****", "*** Scanned ***", "Generic Tag", "Checking Spoolman");
    }

    // Publish generic tag state to HA (pre-lookup)
    {
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"uid\":\"%s\",\"present\":true,\"tag_data_valid\":false,"
                 "\"material_type\":\"\",\"material_name\":\"\",\"color\":\"\","
                 "\"manufacturer\":\"\",\"remaining_g\":0.0,\"initial_weight_g\":0.0,"
                 "\"spoolman_id\":-1,\"blank\":false}",
                 msg.payload.genericTag.spool_id);
        publishToHA("tag/state", json, true);
        strncpy(lastHAStateJson_, json, sizeof(lastHAStateJson_) - 1);
        lastHAStateJson_[sizeof(lastHAStateJson_) - 1] = '\0';
    }

    // Trigger Spoolman UID lookup — result will arrive via SPOOLMAN_SYNCED
#ifndef NATIVE_TEST
    if (SpoolmanManager::getInstance().isConfigured()) {
        SpoolmanSyncRequest req = {};
        strncpy(req.spool_id, msg.payload.genericTag.spool_id, sizeof(req.spool_id) - 1);
        req.lookup_only = true;
        SpoolmanManager::getInstance().enqueueSync(req);
    }
#endif
}

void ApplicationManager::finishPrint(float gramsUsed, bool /*canceled*/) {
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

        // Only auto-update NFC tag in SELF_DIRECTED mode
        if (automationMode == AutomationMode::SELF_DIRECTED) {
            if (display_) {
                display_->showText("Updating spool..", "");
            }

            // Enqueue write request with expected spool ID
            NFCWriteRequest request;
            request.request_id = millis();  // Simple unique ID
            request.type = NFCWriteType::REMOVE_WEIGHT;
            strncpy(request.expected_spool_id, startingSpoolId, sizeof(request.expected_spool_id) - 1);
            request.expected_spool_id[sizeof(request.expected_spool_id) - 1] = '\0';
            request.data.grams_to_remove = gramsUsed;

            NFCManager::getInstance().enqueueWrite(request);
        } else {
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

    char materialName[32] = {0};
    strncpy(materialName, msg.payload.spoolmanSynced.material_name, sizeof(materialName) - 1);
    float kgRemaining = msg.payload.spoolmanSynced.kg_remaining;

#ifndef NATIVE_TEST
    if (msg.payload.spoolmanSynced.is_uid_lookup && msg.payload.spoolmanSynced.success) {
        // Validate the tag is still present and matches the lookup UID before
        // writing — the tag may have been removed while the Spoolman request was in flight
        CurrentSpoolState currentState;
        bool tagStillPresent = NFCManager::getInstance().getCurrentSpoolState(currentState)
                               && currentState.present
                               && currentState.kind == TagKind::GenericUidTag
                               && strcmp(currentState.spool_id, msg.payload.spoolmanSynced.spool_id) == 0;
        if (tagStillPresent) {
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

    if (display_) {
        if (msg.payload.spoolmanSynced.success && msg.payload.spoolmanSynced.is_uid_lookup) {
            // UID lookup — show spool graphic with Spoolman data (tag had no data)
            DisplaySpoolData spool{};
            strncpy(spool.brand, msg.payload.spoolmanSynced.manufacturer, sizeof(spool.brand) - 1);
            strncpy(spool.material, materialName, sizeof(spool.material) - 1);
            const char* colorSrc = msg.payload.spoolmanSynced.color_hex;
            if (colorSrc[0] == '#') colorSrc++;
            strncpy(spool.colorHex, colorSrc, sizeof(spool.colorHex) - 1);
            spool.remainingWeight = kgRemaining * 1000.0f;
            spool.totalWeight = msg.payload.spoolmanSynced.initial_weight_g;
            spool.tagType = 5;
            display_->showSpool(spool);
        } else if (msg.payload.spoolmanSynced.success) {
            // Smart tag — don't overwrite, handleSpoolDetected already showed correct data
        } else if (msg.payload.spoolmanSynced.is_uid_lookup) {
            display_->showText("Generic Tag", "Not in Spoolman");
        } else {
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
    // Update recent spool sync status
    if (msg.payload.spoolmanSynced.success) {
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
#endif
}

void ApplicationManager::handleTagRemoved(const AppMessage& msg) {
    Serial.printf("EVENT: TagRemoved - spool_id=%s\n",
        msg.payload.tagRemoved.spool_id);
    // LED intentionally not changed — keep showing last filament color until next scan

    // Clear displayed spool so next scan re-displays
    lastDisplayedSpoolId[0] = '\0';
    lastDisplayedBlankId[0] = '\0';
#ifndef NATIVE_TEST
    NFCManager::getInstance().clearGenericTagSpoolInfo();
#endif
    tagRemovedAtMs = millis();
    pendingStatusAfterTagRemoved = true;

    // Publish tag removed to HA — retain last spool data with present:false
    if (lastHAStateJson_[0] != '\0') {
        char removed[512];
        char* pos = strstr(lastHAStateJson_, "\"present\":true");
        if (pos) {
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
        publishToHA("tag/state",
                    "{\"uid\":\"\",\"present\":false,\"tag_data_valid\":false,"
                    "\"material_type\":\"\","
                    "\"material_name\":\"\",\"color\":\"\",\"manufacturer\":\"\","
                    "\"remaining_g\":0.0,\"initial_weight_g\":0.0,\"spoolman_id\":-1,"
                    "\"blank\":false}",
                    true);
    }
}

void ApplicationManager::handleHAWriteTag(const AppMessage& msg) {
    Serial.printf("EVENT: HAWriteTag - expected_uid=%s\n",
        msg.payload.haWriteTag.expected_uid);

    // Enqueue individual write requests for each field
    const auto& p = msg.payload.haWriteTag;

    // Material type
    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = millis();
    req.type = NFCWriteType::CHANGE_FILAMENT_TYPE;
    strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
    req.data.new_material_type = p.material_type;
    NFCManager::getInstance().enqueueWrite(req);

    // Color
    memset(&req, 0, sizeof(req));
    req.request_id = millis() + 1;
    req.type = NFCWriteType::CHANGE_COLOR;
    strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
    memcpy(req.data.new_color, p.color, 4);
    NFCManager::getInstance().enqueueWrite(req);

    // Brand name
    memset(&req, 0, sizeof(req));
    req.request_id = millis() + 2;
    req.type = NFCWriteType::SET_BRAND_NAME;
    strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
    strncpy(req.data.brand_name, p.manufacturer, sizeof(req.data.brand_name) - 1);
    NFCManager::getInstance().enqueueWrite(req);

    // Remaining weight (set consumed = initial - remaining)
    if (p.initial_weight_g > 0) {
        float consumed = p.initial_weight_g - p.remaining_g;
        if (consumed < 0) consumed = 0;
        memset(&req, 0, sizeof(req));
        req.request_id = millis() + 3;
        req.type = NFCWriteType::SET_CONSUMED_WEIGHT;
        strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
        req.data.consumed_weight = consumed;
        NFCManager::getInstance().enqueueWrite(req);
    }

    // Spoolman ID
    if (p.spoolman_id > 0) {
        memset(&req, 0, sizeof(req));
        req.request_id = millis() + 4;
        req.type = NFCWriteType::WRITE_SPOOLMAN_ID;
        strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
        req.data.spoolman_id = p.spoolman_id;
        NFCManager::getInstance().enqueueWrite(req);
    }
}

void ApplicationManager::handleHAUpdateRemaining(const AppMessage& msg) {
    Serial.printf("EVENT: HAUpdateRemaining - expected_uid=%s, remaining_g=%.2f\n",
        msg.payload.haUpdateRemaining.expected_uid,
        msg.payload.haUpdateRemaining.remaining_g);

    const auto& p = msg.payload.haUpdateRemaining;

    // Set consumed weight: we need the initial weight from the current tag
    // The NFC write will compute consumed = initial - remaining internally
    // We use SET_CONSUMED_WEIGHT with consumed = initial - remaining
    // But we don't know initial here. Use SET_CONSUMED_WEIGHT with a sentinel approach.
    // Actually, the HA manager will compute consumed before sending this message.
    // For now, treat remaining_g as a consumed weight to set.
    // The HA task should compute consumed = initial - remaining before sending.
    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = millis();
    req.type = NFCWriteType::SET_CONSUMED_WEIGHT;
    strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
    // remaining_g here is actually the consumed weight (computed by HA task)
    req.data.consumed_weight = p.remaining_g;
    NFCManager::getInstance().enqueueWrite(req);
}

#ifndef NATIVE_TEST
void ApplicationManager::enqueueSpoolmanSync(const SpoolDetectedPayload& spool) {
    SpoolmanSyncRequest req;
    memset(&req, 0, sizeof(req));
    strncpy(req.spool_id, spool.spool_id, sizeof(req.spool_id) - 1);
    req.material_type = spool.material_type;
    strncpy(req.manufacturer, spool.manufacturer, sizeof(req.manufacturer) - 1);
    memcpy(req.color, spool.primary_color, 4);
    req.remaining_weight_g = spool.kg_remaining * 1000.0f;
    req.initial_weight_g = spool.initial_weight_g;

    // Use tag density if available, otherwise use default
    if (spool.density > 0.0f) {
        req.density = spool.density;
    } else {
        // Default densities by material
        switch (spool.material_type) {
            case OPT_MATERIAL_TYPE_PLA:  req.density = 1.24f; break;
            case OPT_MATERIAL_TYPE_PETG: req.density = 1.27f; break;
            case OPT_MATERIAL_TYPE_ABS:  req.density = 1.04f; break;
            default: req.density = 1.20f; break;
        }
    }

    // Use tag diameter if available, otherwise default 1.75mm
    req.diameter = (spool.diameter > 0.0f) ? spool.diameter : 1.75f;

    req.spoolman_id = spool.spoolman_id;

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

void ApplicationManager::handlePrinterWarning(const AppMessage& msg) {
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

    // Publish to HA
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

void ApplicationManager::publishToHA(const char* topicSuffix, const char* payload, bool retained) {
#ifndef NATIVE_TEST
    auto& ha = HomeAssistantManager::getInstance();
    if (!ha.isConfigured()) return;

    HAPublishRequest req;
    memset(&req, 0, sizeof(req));

    char deviceId[7];
    HomeAssistantManager::getDeviceId(deviceId, sizeof(deviceId));
    snprintf(req.topic, sizeof(req.topic), "spoolsense/%s/%s", deviceId, topicSuffix);
    strncpy(req.payload, payload, sizeof(req.payload) - 1);
    req.retained = retained;

    ha.enqueuePublish(req);
#else
    (void)topicSuffix;
    (void)payload;
    (void)retained;
#endif
}

// ── Keypad handlers ─────────────────────────────────────────────────────────

void ApplicationManager::handleKeypadDigit(const AppMessage& msg) {
    char digit = msg.payload.keypadDigit.digit;
    Serial.printf("EVENT: KeypadDigit - '%c'\n", digit);
    pendingKeypadPrompt = false;  // Cancel prompt — user is already typing

    if (keypadBufferLen_ < sizeof(keypadBuffer_) - 1) {
        keypadBuffer_[keypadBufferLen_++] = digit;
        keypadBuffer_[keypadBufferLen_] = '\0';
    }

    if (display_) {
        display_->showKeypad(keypadBuffer_);
    }
}

void ApplicationManager::handleKeypadConfirm() {
    Serial.println("EVENT: KeypadConfirm");

    if (keypadBufferLen_ == 0) {
        if (display_) display_->showText("No tool entered", "Type number + #");
        return;
    }

#ifndef NATIVE_TEST
    // Check if a spool was recently scanned (doesn't need to still be on the reader)
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

    if (sendAssignSpool(keypadBuffer_)) {
        if (display_) {
            char line[17];
            snprintf(line, sizeof(line), "Assigned T%s", keypadBuffer_);
            display_->showText(line, "OK");
        }
    }

    keypadBuffer_[0] = '\0';
    keypadBufferLen_ = 0;
}

void ApplicationManager::handleKeypadCancel() {
    Serial.println("EVENT: KeypadCancel");
    keypadBuffer_[0] = '\0';
    keypadBufferLen_ = 0;

    if (display_) {
        display_->showText("Tool entry", "Cleared");
    }
}

bool ApplicationManager::sendAssignSpool(const char* toolNumber) {
#ifndef NATIVE_TEST
    // Validate tool number is digits-only (prevent GCode injection)
    for (const char* p = toolNumber; *p; p++) {
        if (*p < '0' || *p > '9') {
            Serial.printf("ApplicationManager: Invalid tool number '%s'\n", toolNumber);
            return false;
        }
    }

    const char* moonrakerUrl = ConfigurationManager::getInstance().getMoonrakerURL();
    if (!moonrakerUrl || moonrakerUrl[0] == '\0') {
        Serial.println("ApplicationManager: Moonraker URL not configured — cannot assign spool");
        if (display_) display_->showText("Moonraker URL", "Not configured");
        return false;
    }

    extern SemaphoreHandle_t g_httpMutex;
    if (g_httpMutex && xSemaphoreTake(g_httpMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        Serial.println("ApplicationManager: Could not acquire HTTP mutex for ASSIGN_SPOOL");
        if (display_) display_->showText("Assign failed", "HTTP busy");
        return false;
    }

    char url[192];
    snprintf(url, sizeof(url), "%s/printer/gcode/script", moonrakerUrl);

    char gcode[64];
    snprintf(gcode, sizeof(gcode), "ASSIGN_SPOOL TOOL=T%s", toolNumber);

    char postBody[96];
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

    Serial.printf("ApplicationManager: ASSIGN_SPOOL T%s — HTTP %d\n", toolNumber, code);

    if (code != 200) {
        if (display_) display_->showText("Assign failed", "Check Moonraker");
        return false;
    }
    return true;
#else
    (void)toolNumber;
    return true;
#endif
}
