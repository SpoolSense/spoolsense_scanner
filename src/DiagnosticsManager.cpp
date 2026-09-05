#include "DiagnosticsManager.h"
#include "DiagnosticsUtil.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <stdarg.h>

#include "NFCManager.h"
#include "MemoryDiagnostics.h"
#include "ConfigurationManager.h"
#include "HomeAssistantManager.h"
#include "LogBuffer.h"
#include "TaskUtils.h"

// Serializes all outbound HTTP/TLS (created in main.cpp, shared with the web +
// Spoolman + printer tasks). The network reachability checks below take it so a
// diagnostic request can't overlap a concurrent Spoolman/printer request.
extern SemaphoreHandle_t g_httpMutex;
static constexpr uint32_t DIAG_HTTP_MUTEX_MS = 5000;

// Worker task sizing. Low priority so it never starves the scan/web tasks.
// Pinned to core 1 (APP_CPU) alongside the scan/loop tasks — NOT core 0, which
// runs the WiFi/lwIP stack: the stability loop's ~735ms detectTag bursts would
// starve it there and freeze all HTTP to the board for the duration.
static constexpr uint32_t DIAG_TASK_STACK = 6144;
static constexpr UBaseType_t DIAG_TASK_PRIO = 1;
static constexpr int DIAG_TASK_CORE = 1;

// Stability run tuning.
static constexpr uint16_t STABILITY_DETECT_CYCLES = 100;
static constexpr uint8_t  STABILITY_READ_EVERY = 5;   // do a page read every Nth cycle
// Wall-clock cap: a good tag runs 100 cycles in ~5s, but each detect MISS
// blocks on the reader's internal RF timeout (~1s), so a missing/marginal tag
// would otherwise stretch to minutes. Bound the whole stage instead.
static constexpr uint32_t STABILITY_MAX_MS = 8000;
static constexpr uint32_t SCAN_PAUSE_TIMEOUT_MS = 3000;
static constexpr uint32_t USER_WAIT_TIMEOUT_MS = 60000;

DiagnosticsManager& DiagnosticsManager::getInstance() {
    static DiagnosticsManager instance;
    return instance;
}

void DiagnosticsManager::ensureLock() {
#ifndef NATIVE_TEST
    if (!lock_) lock_ = xSemaphoreCreateMutex();
#endif
}

void DiagnosticsManager::lock() {
#ifndef NATIVE_TEST
    if (lock_) xSemaphoreTake(lock_, portMAX_DELAY);
#endif
}

void DiagnosticsManager::unlock() {
#ifndef NATIVE_TEST
    if (lock_) xSemaphoreGive(lock_);
#endif
}

void DiagnosticsManager::addResult(DiagnosticTest t, DiagnosticStatus s, int32_t code,
                                   uint32_t duration_ms, const char* summary,
                                   const char* recommendation) {
    lock();
    if (resultCount_ < (uint8_t)DiagnosticTest::TEST_COUNT) {
        DiagnosticResult& r = results_[resultCount_++];
        r.test = t;
        r.status = s;
        r.code = code;
        r.duration_ms = duration_ms;
        snprintf(r.summary, sizeof(r.summary), "%s", summary ? summary : "");
        snprintf(r.recommendation, sizeof(r.recommendation), "%s", recommendation ? recommendation : "");
    }
    unlock();
}

void DiagnosticsManager::setPrompt(const char* p) {
    lock();
    snprintf(stagePrompt_, sizeof(stagePrompt_), "%s", p ? p : "");
    unlock();
}

bool DiagnosticsManager::waitForUser(uint32_t timeout_ms) {
    waitingForUser_ = true;
    userContinue_ = false;
    uint32_t waited = 0;
    while (!userContinue_ && !cancelRequested_ && waited < timeout_ms) {
#ifndef NATIVE_TEST
        vTaskDelay(pdMS_TO_TICKS(50));
#endif
        waited += 50;
    }
    waitingForUser_ = false;
    bool confirmed = userContinue_ && !cancelRequested_;
    userContinue_ = false;
    return confirmed;
}

bool DiagnosticsManager::startSession(const Options& opts) {
    ensureLock();

    // Check-and-claim under the lock so two near-simultaneous starts can't both
    // spawn a DiagTask (the volatile flags below are single-writer once claimed).
    lock();
    if (active_) {
        unlock();
        return false;
    }
    active_ = true;
    cancelRequested_ = false;
    userContinue_ = false;
    waitingForUser_ = false;
    opts_ = opts;
    resultCount_ = 0;
    stabilityScore_ = 0;
    stabilityRan_ = false;
    readerSnapValid_ = false;
    stagePrompt_[0] = '\0';
    unlock();

#ifndef NATIVE_TEST
    BaseType_t ok = createTaskWithAffinity(sessionTaskFunc, "DiagTask", DIAG_TASK_STACK,
                                           this, DIAG_TASK_PRIO, &taskHandle_, DIAG_TASK_CORE);
    if (ok != pdPASS) {
        active_ = false;
        return false;
    }
#endif
    return true;
}

void DiagnosticsManager::cancelSession() {
    cancelRequested_ = true;
}

void DiagnosticsManager::submitUserContinue() {
    userContinue_ = true;
}

void DiagnosticsManager::sessionTaskFunc(void* param) {
    DiagnosticsManager* self = static_cast<DiagnosticsManager*>(param);
    self->runSession();
#ifndef NATIVE_TEST
    self->taskHandle_ = nullptr;
    self->active_ = false;
    vTaskDelete(NULL);
#endif
}

void DiagnosticsManager::runSession() {
    // --- Stage 1: device / firmware / memory / tasks (no reader, no network) ---
    checkDeviceInfo();
    checkResetReason();
    checkHeapHealth();
    checkTaskStacks();

    // --- Stage 2: network reachability (optional) ---
    if (opts_.network && !cancelRequested_) {
        checkWifi();
        checkMqtt();
        checkSpoolman();
        checkPrinter();
    }

    if (cancelRequested_) return;

    // --- Stage 3: reader (always) + stability (optional) — under scan pause ---
    NFCManager& nfc = NFCManager::getInstance();
    nfc.requestScanPause();
    bool paused = nfc.waitForScanPaused(SCAN_PAUSE_TIMEOUT_MS);

    if (!paused) {
        addResult(DiagnosticTest::NFC_READER_INIT, DiagnosticStatus::WARNING, 0, 0,
                  "Could not pause the scan task to inspect the reader",
                  "Retry the self-test; if it persists the scan task may be stuck.");
        resumeScanAndWait(nfc);
        return;
    }

    NFCConnectionI* conn = nfc.getDiagConnection();
    if (conn) {
        readerSnapValid_ = conn->getDiagnosticSnapshot(readerSnap_);
    }

    checkReaderInit();
    checkReaderVersion();
    checkReaderRegisters();

    if (opts_.stability && !cancelRequested_) {
        setPrompt("Place ONE tag flat on the reader and keep it still, then continue.");
        if (waitForUser(USER_WAIT_TIMEOUT_MS)) {
            setPrompt("");
            runStabilityStage();
        } else {
            addResult(DiagnosticTest::TAG_DETECTION_STABILITY, DiagnosticStatus::SKIPPED, 0, 0,
                      "Stability test skipped — Continue not pressed in time",
                      "Re-run: place a tag on the reader, then click Continue in the popup.");
        }
    }

    resumeScanAndWait(nfc);
}

// Release the scan pause and block until the scan task has actually left its
// pause loop. The session's `active_` flag clears right after runSession()
// returns — without this ack, a back-to-back start could read the previous
// session's stale scanPaused_==true, skip its own pause handshake, and drive
// the reader concurrently with the just-resumed scan task.
void DiagnosticsManager::resumeScanAndWait(NFCManager& nfc) {
    nfc.resumeScan();
#ifndef NATIVE_TEST
    for (int i = 0; i < 100 && nfc.isScanPaused(); i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#endif
}

// --- Stage 1 checks -------------------------------------------------------

void DiagnosticsManager::checkDeviceInfo() {
#ifndef NATIVE_TEST
    char deviceId[8] = {0};
    HomeAssistantManager::getDeviceId(deviceId, sizeof(deviceId));
    const char* reader = ConfigurationManager::getInstance().getNfcReader();
    char summary[96];
    snprintf(summary, sizeof(summary), "Device %s, firmware %s, reader %s",
             deviceId, FIRMWARE_VERSION, reader ? reader : "?");
    addResult(DiagnosticTest::DEVICE_INFO, DiagnosticStatus::PASS, 0, 0, summary, "");
#endif
}

void DiagnosticsManager::checkResetReason() {
#ifndef NATIVE_TEST
    const char* reason = "unknown";
    DiagnosticStatus status = DiagnosticStatus::PASS;
    const char* rec = "";
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   reason = "power-on"; break;
        case ESP_RST_SW:        reason = "software"; break;
        case ESP_RST_EXT:       reason = "external"; break;
        case ESP_RST_DEEPSLEEP: reason = "deep-sleep"; break;
        case ESP_RST_PANIC:
            reason = "panic (crash)"; status = DiagnosticStatus::WARNING;
            rec = "Last boot followed a crash. Check /logs and report if it recurs.";
            break;
        case ESP_RST_TASK_WDT:
        case ESP_RST_INT_WDT:
        case ESP_RST_WDT:
            reason = "watchdog"; status = DiagnosticStatus::WARNING;
            rec = "Last boot followed a watchdog reset — a task stalled. Note what you were doing.";
            break;
        case ESP_RST_BROWNOUT:
            reason = "brownout"; status = DiagnosticStatus::WARNING;
            rec = "Power dipped too low. Use a better USB supply/cable and check 5V wiring to the reader.";
            break;
        default: break;
    }
    char summary[96];
    snprintf(summary, sizeof(summary), "Last reset: %s, up %lus", reason,
             (unsigned long)(millis() / 1000));
    addResult(DiagnosticTest::RESET_REASON, status, 0, 0, summary, rec);
#endif
}

void DiagnosticsManager::checkHeapHealth() {
#ifndef NATIVE_TEST
    uint32_t freeInt = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint32_t minInt  = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    DiagnosticStatus status = DiagnosticStatus::PASS;
    const char* rec = "";
    // TLS handshakes (Spoolman/OTA over HTTPS) need a sizable contiguous block.
    if (minInt < 20000) {
        status = DiagnosticStatus::WARNING;
        rec = "Free heap dipped low. Disable unused features (TFT/dashboard) if memory pressure causes resets.";
    } else if (largest < 45000) {
        status = DiagnosticStatus::WARNING;
        rec = "Largest free block is small — HTTPS/OTA may fail. Consider a reboot or disabling heavy features.";
    }
    char summary[96];
    snprintf(summary, sizeof(summary), "Heap free %lu, min-ever %lu, largest block %lu",
             (unsigned long)freeInt, (unsigned long)minInt, (unsigned long)largest);
    addResult(DiagnosticTest::HEAP_HEALTH, status, (int32_t)minInt, 0, summary, rec);
#endif
}

void DiagnosticsManager::checkTaskStacks() {
#ifndef NATIVE_TEST
    MemoryDiagnostics::TaskStackStat stats[MemoryDiagnostics::MAX_TRACKED];
    size_t n = MemoryDiagnostics::collect(stats, MemoryDiagnostics::MAX_TRACKED);
    uint32_t tightest = 0xFFFFFFFF;
    const char* tightestName = "?";
    for (size_t i = 0; i < n; i++) {
        if (stats[i].stackHighWaterBytes < tightest) {
            tightest = stats[i].stackHighWaterBytes;
            tightestName = stats[i].name;
        }
    }
    DiagnosticStatus status = DiagnosticStatus::PASS;
    const char* rec = "";
    if (n == 0) {
        status = DiagnosticStatus::WARNING;
        tightest = 0;
        rec = "No task stack data yet — run again after the board has been up a minute.";
    } else if (tightest < 512) {
        status = DiagnosticStatus::WARNING;
        rec = "A task is running close to its stack limit — report this with the task name below.";
    }
    char summary[96];
    snprintf(summary, sizeof(summary), "%u tasks tracked, tightest %s @ %lu bytes free",
             (unsigned)n, tightestName, (unsigned long)tightest);
    addResult(DiagnosticTest::TASK_STACKS, status, (int32_t)tightest, 0, summary, rec);
#endif
}

// --- Stage 2 network checks ----------------------------------------------

void DiagnosticsManager::checkWifi() {
#ifndef NATIVE_TEST
    if (WiFi.status() == WL_CONNECTED) {
        int rssi = (int)WiFi.RSSI();
        DiagnosticStatus status = DiagnosticStatus::PASS;
        const char* rec = "";
        if (rssi < -80) {
            status = DiagnosticStatus::WARNING;
            rec = "Weak WiFi. Move the scanner closer to the AP or add an access point — marginal WiFi causes sync drops.";
        }
        char summary[96];
        snprintf(summary, sizeof(summary), "Connected to %s, %d dBm, IP %s",
                 WiFi.SSID().c_str(), rssi, WiFi.localIP().toString().c_str());
        addResult(DiagnosticTest::WIFI_HEALTH, status, rssi, 0, summary, rec);
    } else {
        addResult(DiagnosticTest::WIFI_HEALTH, DiagnosticStatus::FAIL, 0, 0,
                  "WiFi not connected",
                  "Check credentials on the Config page; the scanner needs WiFi for Spoolman/HA.");
    }
#endif
}

void DiagnosticsManager::checkMqtt() {
#ifndef NATIVE_TEST
    ConfigUpdate cfg;
    ConfigurationManager::getInstance().getCurrentConfig(cfg);
    if (strlen(cfg.mqtt_host) == 0) {
        addResult(DiagnosticTest::MQTT_REACHABILITY, DiagnosticStatus::SKIPPED, 0, 0,
                  "MQTT not configured", "");
        return;
    }
    bool connected = HomeAssistantManager::getInstance().isConnected();
    char summary[96];
    snprintf(summary, sizeof(summary), "Broker %s: %s", cfg.mqtt_host,
             connected ? "connected" : "not connected");
    addResult(DiagnosticTest::MQTT_REACHABILITY,
              connected ? DiagnosticStatus::PASS : DiagnosticStatus::WARNING, 0, 0, summary,
              connected ? "" : "MQTT enabled but not connected — check broker host/credentials.");
#endif
}

void DiagnosticsManager::checkSpoolman() {
#ifndef NATIVE_TEST
    ConfigUpdate cfg;
    ConfigurationManager::getInstance().getCurrentConfig(cfg);
    bool enabled = (cfg.spoolman_on != 0) && (strlen(cfg.spoolman_url) > 0);
    if (!enabled) {
        addResult(DiagnosticTest::SPOOLMAN_REACHABILITY, DiagnosticStatus::SKIPPED, 0, 0,
                  "Spoolman not configured", "");
        return;
    }
    char safeUrl[128];
    diagRedactUrl(safeUrl, sizeof(safeUrl), cfg.spoolman_url);

    bool held = g_httpMutex && (xSemaphoreTake(g_httpMutex, pdMS_TO_TICKS(DIAG_HTTP_MUTEX_MS)) == pdTRUE);
    if (g_httpMutex && !held) {
        // Never do unserialized outbound HTTP — that's the overlap the mutex exists to prevent.
        addResult(DiagnosticTest::SPOOLMAN_REACHABILITY, DiagnosticStatus::WARNING, 0, 0,
                  "Skipped — another network operation was in flight",
                  "A Spoolman sync or printer poll held the connection; re-run the self-test.");
        return;
    }
    HTTPClient http;
    char infoUrl[160];
    snprintf(infoUrl, sizeof(infoUrl), "%s/api/v1/info", cfg.spoolman_url);
    http.begin(infoUrl);
    http.setTimeout(3000);
    uint32_t t0 = millis();
    int code = http.GET();
    uint32_t dt = millis() - t0;
    char version[24] = {0};
    if (code == 200) {
        String body = http.getString();
        StaticJsonDocument<256> info;
        if (!deserializeJson(info, body) && info.containsKey("version")) {
            snprintf(version, sizeof(version), "%s", info["version"].as<const char*>());
        }
    }
    http.end();
    if (held) xSemaphoreGive(g_httpMutex);

    char summary[96];
    if (code == 200) {
        snprintf(summary, sizeof(summary), "%s reachable (v%s, %lums)", safeUrl,
                 version[0] ? version : "?", (unsigned long)dt);
        addResult(DiagnosticTest::SPOOLMAN_REACHABILITY, DiagnosticStatus::PASS, code, dt, summary, "");
    } else {
        snprintf(summary, sizeof(summary), "%s returned HTTP %d", safeUrl, code);
        addResult(DiagnosticTest::SPOOLMAN_REACHABILITY, DiagnosticStatus::FAIL, code, dt, summary,
                  "Check the Spoolman URL/port and that Spoolman is running on your network.");
    }
#endif
}

void DiagnosticsManager::checkPrinter() {
#ifndef NATIVE_TEST
    ConfigurationManager& cm = ConfigurationManager::getInstance();
    const char* moonraker = cm.getMoonrakerURL();
    bool prusa = cm.isPrusaLinkEnabled();
    const char* prusaUrl = cm.getPrusaLinkURL();

    const char* baseUrl = nullptr;
    const char* path = nullptr;
    if (moonraker && strlen(moonraker) > 0) {
        baseUrl = moonraker; path = "/printer/info";
    } else if (prusa && prusaUrl && strlen(prusaUrl) > 0) {
        baseUrl = prusaUrl; path = "/api/version";
    }
    if (!baseUrl) {
        addResult(DiagnosticTest::PRINTER_REACHABILITY, DiagnosticStatus::SKIPPED, 0, 0,
                  "No printer integration configured", "");
        return;
    }
    char safeUrl[128];
    diagRedactUrl(safeUrl, sizeof(safeUrl), baseUrl);

    bool held = g_httpMutex && (xSemaphoreTake(g_httpMutex, pdMS_TO_TICKS(DIAG_HTTP_MUTEX_MS)) == pdTRUE);
    if (g_httpMutex && !held) {
        addResult(DiagnosticTest::PRINTER_REACHABILITY, DiagnosticStatus::WARNING, 0, 0,
                  "Skipped — another network operation was in flight",
                  "A Spoolman sync or printer poll held the connection; re-run the self-test.");
        return;
    }
    HTTPClient http;
    char url[192];
    snprintf(url, sizeof(url), "%s%s", baseUrl, path);
    http.begin(url);
    http.setTimeout(3000);
    uint32_t t0 = millis();
    int code = http.GET();
    uint32_t dt = millis() - t0;
    http.end();
    if (held) xSemaphoreGive(g_httpMutex);

    char summary[96];
    // 401 still proves reachability (PrusaLink wants an API key) — treat as reachable.
    bool reachable = (code > 0 && code < 500);
    if (reachable) {
        snprintf(summary, sizeof(summary), "%s reachable (HTTP %d, %lums)", safeUrl, code, (unsigned long)dt);
        addResult(DiagnosticTest::PRINTER_REACHABILITY, DiagnosticStatus::PASS, code, dt, summary, "");
    } else {
        snprintf(summary, sizeof(summary), "%s unreachable (HTTP %d)", safeUrl, code);
        addResult(DiagnosticTest::PRINTER_REACHABILITY, DiagnosticStatus::FAIL, code, dt, summary,
                  "Check the printer URL and that Moonraker/PrusaLink is reachable from this network.");
    }
#endif
}

// --- Stage 3 reader checks (scan paused) ---------------------------------

void DiagnosticsManager::checkReaderInit() {
    if (!readerSnapValid_) {
        addResult(DiagnosticTest::NFC_READER_INIT, DiagnosticStatus::FAIL, 0, 0,
                  "Reader did not respond to a status query",
                  "Check reader power (5V/3V3), ground, and SPI wiring (NSS/SCK/MISO/MOSI).");
        return;
    }
    if (readerSnap_.initialized) {
        char summary[96];
        snprintf(summary, sizeof(summary), "%s initialized", readerSnap_.reader_name);
        addResult(DiagnosticTest::NFC_READER_INIT, DiagnosticStatus::PASS, 0, 0, summary, "");
    } else {
        addResult(DiagnosticTest::NFC_READER_INIT, DiagnosticStatus::FAIL, 0, 0,
                  "Reader failed to initialize",
                  "Check reader power and wiring; confirm the configured reader matches the connected module.");
    }
}

void DiagnosticsManager::checkReaderVersion() {
    if (!readerSnapValid_) return;
    char summary[96];
    snprintf(summary, sizeof(summary), "Firmware v%u.%u", readerSnap_.fw_major, readerSnap_.fw_minor);
    // fw 0.0 usually means the version read failed even though the object exists.
    bool ok = (readerSnap_.fw_major != 0 || readerSnap_.fw_minor != 0);
    addResult(DiagnosticTest::NFC_READER_VERSION, ok ? DiagnosticStatus::PASS : DiagnosticStatus::WARNING,
              0, 0, summary,
              ok ? "" : "Firmware version read as 0.0 — SPI comms may be marginal; check wiring/supply.");
}

void DiagnosticsManager::checkReaderRegisters() {
    if (!readerSnapValid_) return;
    if (!readerSnap_.has_registers) {
        addResult(DiagnosticTest::NFC_REGISTER_HEALTH, DiagnosticStatus::PASS, 0, 0,
                  readerSnap_.sam_config_ok ? "SAMConfig OK (firmware-managed radio)"
                                            : "Reader radio firmware-managed",
                  "");
        return;
    }
    if (readerSnap_.bus_wedged) {
        addResult(DiagnosticTest::NFC_REGISTER_HEALTH, DiagnosticStatus::FAIL, 0, 0,
                  "PN5180 SPI bus is wedged (fail-fast latched)",
                  "A transaction hung. Power-cycle the board; if it recurs check 5V supply/decoupling and SPI wiring. /logs shows which BUSY handshake stalled.");
        return;
    }
    char summary[96];
    snprintf(summary, sizeof(summary), "RF=0x%08lX IRQ=0x%08lX SYS=0x%08lX",
             (unsigned long)readerSnap_.rf_status, (unsigned long)readerSnap_.irq_status,
             (unsigned long)readerSnap_.system_status);
    // All-ones on MISO means nothing drove the bus during the read — a dead,
    // unpowered, or miswired module — and must not report as healthy (#293).
    if (readerSnap_.rf_status == 0xFFFFFFFF || readerSnap_.system_status == 0xFFFFFFFF) {
        addResult(DiagnosticTest::NFC_REGISTER_HEALTH, DiagnosticStatus::FAIL, 0, 0, summary,
                  "Registers read all-ones — the reader is not responding on SPI. "
                  "Check module power and MISO wiring; if wiring is good the module is likely faulty.");
        return;
    }
    addResult(DiagnosticTest::NFC_REGISTER_HEALTH, DiagnosticStatus::PASS, 0, 0, summary, "");
}

// --- Stage 3 stability (scan paused, tag present) ------------------------

void DiagnosticsManager::runStabilityStage() {
#ifndef NATIVE_TEST
    NFCConnectionI* conn = NFCManager::getInstance().getDiagConnection();
    if (!conn) {
        addResult(DiagnosticTest::TAG_DETECTION_STABILITY, DiagnosticStatus::SKIPPED, 0, 0,
                  "No reader connection available", "");
        return;
    }

    NfcStabilityMetrics m = {};
    uint8_t baselineUid[8] = {0};
    uint8_t baselineLen = 0;
    bool haveBaseline = false;
    uint32_t latSum = 0;
    m.latency_min_ms = 0xFFFFFFFF;
    uint8_t readBuf[16];

    uint32_t t0 = millis();
    for (uint16_t i = 0; i < STABILITY_DETECT_CYCLES && !cancelRequested_
                         && (millis() - t0) < STABILITY_MAX_MS; i++) {
        uint8_t uid[8] = {0};
        uint8_t len = 0;
        // Mirror the scan loop's per-cycle RF preparation (its tag-present path
        // does setupRF() before each detectTag) — without it, the field state is
        // not re-armed after a read and subsequent detects mostly miss.
        conn->setupRF();
        uint32_t d0 = millis();
        bool ok = conn->detectTag(uid, &len);
        uint32_t dt = millis() - d0;
        m.detect_attempts++;

        if (ok && len > 0) {
            m.detect_success++;
            latSum += dt;
            if (dt < m.latency_min_ms) m.latency_min_ms = dt;
            if (dt > m.latency_max_ms) m.latency_max_ms = dt;
            if (!haveBaseline) {
                memcpy(baselineUid, uid, len);
                baselineLen = len;
                haveBaseline = true;
            } else if (len != baselineLen || memcmp(uid, baselineUid, len) != 0) {
                m.uid_mismatches++;
            }
            // Periodic read-stability probe. readISO14443Pages is ISO14443A/NTAG
            // only, so gate on an ISO14443A detect (ATQA set) — an ISO15693 tag
            // (OpenPrintTag) would otherwise fail every probe and be falsely
            // penalized. Require a full 4-page (16-byte) read to count.
            if ((i % STABILITY_READ_EVERY) == 0 && conn->getLastATQA() != 0) {
                conn->setCurrentUid(uid, len);
                m.read_attempts++;
                // keepSession=true: don't halt the tag between cycles.
                uint16_t got = conn->readISO14443Pages(4, 4, readBuf, sizeof(readBuf), true);
                if (got == sizeof(readBuf)) m.read_success++;
            }
        }
#ifndef NATIVE_TEST
        // Yield generously so the web server (shares core 1) stays responsive
        // during the run — the scan task uses a similar gap for the same reason.
        vTaskDelay(pdMS_TO_TICKS(15));
#endif
    }
    conn->endTagSession();
    uint32_t elapsed = millis() - t0;

    if (m.detect_success > 0) {
        m.latency_avg_ms = latSum / m.detect_success;
    } else {
        m.latency_min_ms = 0;
    }

    uint8_t score = diagComputeStabilityScore(m);
    NfcStabilityGrade grade = diagScoreGrade(score);
    stabilityScore_ = score;
    stabilityRan_ = true;

    DiagnosticStatus status = DiagnosticStatus::PASS;
    const char* rec = "";
    if (m.detect_success == 0) {
        status = DiagnosticStatus::FAIL;
        rec = "No reads succeeded. Reposition the tag flat over the antenna; check tag type is supported.";
    } else if (grade == NfcStabilityGrade::POOR || grade == NfcStabilityGrade::MARGINAL) {
        status = DiagnosticStatus::WARNING;
        rec = "Marginal coupling. Reduce the gap to the antenna, remove metal nearby, or check 5V supply to the reader.";
    }

    char redUid[24] = "none";
    if (haveBaseline) {
        char hex[17]; hex[0] = '\0';
        for (uint8_t k = 0; k < baselineLen && k < 8; k++) snprintf(hex + k*2, 3, "%02X", baselineUid[k]);
        diagRedactUid(redUid, sizeof(redUid), hex);
    }

    char summary[96];
    snprintf(summary, sizeof(summary), "Score %u (%s): %u/%u detects, %u/%u reads, UID %s",
             score, diagGradeName(grade), m.detect_success, m.detect_attempts,
             m.read_success, m.read_attempts, redUid);
    addResult(DiagnosticTest::TAG_DETECTION_STABILITY, status, score, elapsed, summary, rec);

    // Read-stability as its own line for clarity.
    char rsum[96];
    snprintf(rsum, sizeof(rsum), "%u/%u page reads OK, latency %lu/%lu/%lu ms (min/avg/max)",
             m.read_success, m.read_attempts,
             (unsigned long)m.latency_min_ms, (unsigned long)m.latency_avg_ms,
             (unsigned long)m.latency_max_ms);
    addResult(DiagnosticTest::TAG_READ_STABILITY,
              (m.read_attempts > 0 && m.read_success == 0) ? DiagnosticStatus::WARNING : DiagnosticStatus::PASS,
              0, 0, rsum,
              (m.read_attempts > 0 && m.read_success == 0)
                  ? "Detection works but page reads fail — often a marginal-coupling or tag-lock issue." : "");
#endif
}

// --- snapshot + report ----------------------------------------------------

void DiagnosticsManager::getSnapshot(Snapshot& out) {
    ensureLock();
    lock();
    out.active = active_;
    out.waiting_for_user = waitingForUser_;
    snprintf(out.stage_prompt, sizeof(out.stage_prompt), "%s", stagePrompt_);
    out.result_count = resultCount_;
    out.stability_score = stabilityScore_;
    out.stability_ran = stabilityRan_;
    DiagnosticStatus overall = DiagnosticStatus::PASS;
    for (uint8_t i = 0; i < resultCount_ && i < (uint8_t)DiagnosticTest::TEST_COUNT; i++) {
        out.results[i] = results_[i];
        overall = diagWorseStatus(overall, results_[i].status);
    }
    out.overall = (resultCount_ == 0) ? DiagnosticStatus::NOT_RUN : overall;
    unlock();
}

const char* DiagnosticsManager::testName(DiagnosticTest t) {
    switch (t) {
        case DiagnosticTest::DEVICE_INFO:             return "Device";
        case DiagnosticTest::RESET_REASON:            return "Last reset";
        case DiagnosticTest::HEAP_HEALTH:             return "Memory";
        case DiagnosticTest::TASK_STACKS:             return "Task stacks";
        case DiagnosticTest::NFC_READER_INIT:         return "Reader init";
        case DiagnosticTest::NFC_READER_VERSION:      return "Reader firmware";
        case DiagnosticTest::NFC_REGISTER_HEALTH:     return "Reader registers";
        case DiagnosticTest::TAG_DETECTION_STABILITY: return "Tag detection";
        case DiagnosticTest::TAG_READ_STABILITY:      return "Tag read";
        case DiagnosticTest::WIFI_HEALTH:             return "WiFi";
        case DiagnosticTest::MQTT_REACHABILITY:       return "MQTT";
        case DiagnosticTest::SPOOLMAN_REACHABILITY:   return "Spoolman";
        case DiagnosticTest::PRINTER_REACHABILITY:    return "Printer";
        default:                                      return "Test";
    }
}

const char* DiagnosticsManager::statusName(DiagnosticStatus s) {
    switch (s) {
        case DiagnosticStatus::PASS:    return "PASS";
        case DiagnosticStatus::WARNING: return "WARN";
        case DiagnosticStatus::FAIL:    return "FAIL";
        case DiagnosticStatus::SKIPPED: return "SKIP";
        default:                        return "----";
    }
}

// Append formatted text at buf[off], never writing past buflen-1 and never
// returning an offset >= buflen (so buf+off / buflen-off stay in-bounds on the
// next call even when a write truncates).
static size_t appendReport(char* buf, size_t buflen, size_t off, const char* fmt, ...) {
    if (off + 1 >= buflen) return off;  // no room left (keep space for NUL)
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + off, buflen - off, fmt, ap);
    va_end(ap);
    if (n < 0) return off;
    size_t written = (size_t)n;
    size_t room = buflen - off - 1;      // max that actually fit
    return off + (written < room ? written : room);
}

size_t DiagnosticsManager::buildReport(char* buf, size_t buflen) {
    if (!buf || buflen == 0) return 0;
    Snapshot s;
    getSnapshot(s);

    size_t off = 0;
    off = appendReport(buf, buflen, off, "SpoolSense self-test report (report-format v1)\n");
    off = appendReport(buf, buflen, off, "Overall: %s\n", statusName(s.overall));
    if (s.stability_ran) {
        off = appendReport(buf, buflen, off, "NFC stability score: %u/100\n", s.stability_score);
    }
    off = appendReport(buf, buflen, off, "----\n");

    for (uint8_t i = 0; i < s.result_count; i++) {
        const DiagnosticResult& r = s.results[i];
        off = appendReport(buf, buflen, off, "[%s] %s: %s\n",
                           statusName(r.status), testName(r.test), r.summary);
        if (r.recommendation[0]) {
            off = appendReport(buf, buflen, off, "       -> %s\n", r.recommendation);
        }
    }
    buf[off] = '\0';
    return off;
}
