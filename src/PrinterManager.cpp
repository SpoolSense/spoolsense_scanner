#include "PrinterManager.h"
#include "ConfigurationManager.h"
#include "ApplicationManager.h"
#include "NFCManager.h"
#include "NFCTypes.h"
#ifndef NATIVE_TEST
#include "WebServerManager.h"
#endif
#include <Arduino.h>
#include <cstring>
#ifndef NATIVE_TEST
#include "MemoryDiagnostics.h"
#endif

extern "C" {
#include "openprinttag_lib.h"
}

// PrinterManager — tracks print job state (IDLE/TRACKING), validates filament metadata
// vs NFC tag, caches per-tool filament data, detects job transitions, and notifies
// ApplicationManager of print lifecycle events. Requires PrusaLink-compatible printer strategy.

PrinterManager& PrinterManager::getInstance() {
    static PrinterManager instance;
    return instance;
}

void PrinterManager::begin() {
    state_ = PrinterState::IDLE;
    currentJobId_ = -1;
    currentJobTotalFilamentG_ = 0.0f;
    lastProgressPercent_ = 0.0f;
    missingJobPollCount_ = 0;
    mismatchWarned_ = false;  // suppress duplicate filament-type warnings per job
    tempWarned_ = false;       // suppress duplicate temperature warnings per job
    validationPending_ = true;
    cachedToolCount_ = 0;
    memset(cachedFilamentPerTool_, 0, sizeof(cachedFilamentPerTool_));
    Serial.println("PrinterManager: Initialized");
}

// validate task creation — silent failure would leave pollingTaskHandle_ dangling
void PrinterManager::startPollingTask() {
    if (pollingTaskHandle_) {
        Serial.println("PrinterManager: Polling task already running");
        return;
    }

    BaseType_t result = xTaskCreate(
        pollingTaskFunc,
        "PrinterPoll",
        POLLING_TASK_STACK,
        this,
        POLLING_TASK_PRIORITY,
        &pollingTaskHandle_
    );

    if (result == pdPASS) {
        Serial.println("PrinterManager: Polling task started");
    } else {
        pollingTaskHandle_ = nullptr;
        Serial.println("PrinterManager: ERROR — failed to create polling task");
    }
}

void PrinterManager::pollingTaskFunc(void* param) {
    auto* self = static_cast<PrinterManager*>(param);
    auto& config = ConfigurationManager::getInstance();

    while (true) {
        self->poll();
#ifndef NATIVE_TEST
        MemoryDiagnostics::reportSelf(MemoryDiagnostics::Task::PrinterPoll);
#endif
        vTaskDelay(pdMS_TO_TICKS(config.getPollIntervalMs()));
    }
}

void PrinterManager::setStrategy(IPrinterStrategy* strat) {
    strategy_ = strat;
}

bool PrinterManager::isConnected() const {
    return strategy_ != nullptr && strategy_->isConnected();
}

// ---------------------------------------------------------------------------
// poll() — main state machine, called each cycle
// ---------------------------------------------------------------------------

void PrinterManager::poll() {
    if (!strategy_) return;
#ifndef NATIVE_TEST
    if (WebServerManager::getInstance().otaExclusive()) return;
#endif

    strategy_->update();

    if (!strategy_->isConnected()) {
        if (state_ == PrinterState::TRACKING) {
            missingJobPollCount_++;
            // tolerate transient disconnections (WiFi glitches) without losing job context
            if (missingJobPollCount_ >= JOB_MISSING_GRACE) {
                Serial.printf("PrinterManager: Job %d lost after disconnect\n", currentJobId_);
                handleJobDisappeared();
            }
        }
        return;
    }

    if (!strategy_->hasActiveJob()) {
        if (state_ == PrinterState::TRACKING) {
            missingJobPollCount_++;
            // tolerate transient API hiccups (job briefly missing before retry)
            if (missingJobPollCount_ >= JOB_MISSING_GRACE) {
                Serial.printf("PrinterManager: Job %d missing from API\n", currentJobId_);
                handleJobDisappeared();
            }
        }
        return;
    }

    missingJobPollCount_ = 0;

    int jobId = strategy_->getJobId();
    float progress = strategy_->getProgress();
    float totalFilamentG = strategy_->getTotalFilamentGrams();
    const char* jobState = strategy_->getJobState();

    if (state_ == PrinterState::IDLE) {
        if (strcmp(jobState, "PRINTING") == 0 || strcmp(jobState, "PAUSED") == 0) {
            // capture initial progress for mid-print detection (user starts scan after print begins)
            handleJobDetected(jobId, totalFilamentG, progress);
        }
    } else if (state_ == PrinterState::TRACKING) {
        // detect job replacement (queue changed, last job archived)
        if (jobId != currentJobId_) {
            resolveAndSendJobEnd(currentJobId_, lastProgressPercent_, false, "job_replaced");
            handleJobDetected(jobId, totalFilamentG, progress);
            return;
        }

        lastProgressPercent_ = progress;

        if (totalFilamentG > 0) {
            currentJobTotalFilamentG_ = totalFilamentG;
        }

        // refresh per-tool filament data in case gcode metadata updated
        cachePerToolData();

        // retry filament validation if spool wasn't present at job start
        if (validationPending_) {
            checkFilamentMismatch();
        }

        if (strcmp(jobState, "FINISHED") == 0) {
            resolveAndSendJobEnd(jobId, 100.0f, true, "finished");
        } else if (strcmp(jobState, "STOPPED") == 0 || strcmp(jobState, "ERROR") == 0) {
            resolveAndSendJobEnd(jobId, progress, true, "stopped_or_error");
        }
    }
}

// ── Job Lifecycle ─────────────────────────────────────────────────────────

// transition IDLE → TRACKING; captures initial progress, tries filament validation
void PrinterManager::handleJobDetected(int jobId, float totalFilamentG, float progressPercent) {
    state_ = PrinterState::TRACKING;
    currentJobId_ = jobId;
    currentJobTotalFilamentG_ = totalFilamentG;
    lastProgressPercent_ = progressPercent;
    missingJobPollCount_ = 0;
    mismatchWarned_ = false;
    tempWarned_ = false;
    validationPending_ = true;

    // snapshot per-tool filament from gcode before job state changes
    cachePerToolData();

    AppMessage msg;
    msg.type = AppMessageType::PRINT_STARTED;
    msg.payload.printStarted.job_id = jobId;
    ApplicationManager::getInstance().sendMessage(msg);

    Serial.printf("PrinterManager: Tracking job %d (filament: %.2fg, progress: %.1f%%)\n",
                  jobId, totalFilamentG, progressPercent);

    if (totalFilamentG <= 0) {
        Serial.printf("PrinterManager: WARNING — no filament metadata for job %d\n", jobId);
    }

    // attempt validation now; if spool isn't ready, retry during tracking
    checkFilamentMismatch();
}

// snapshot per-tool filament from gcode metadata (needed when job_replaced detected,
// since strategy object will have transitioned to replacement job's data)
void PrinterManager::cachePerToolData() {
    if (!strategy_) return;

    int toolCount = strategy_->getToolCount();
    if (toolCount > 0) {
        if (toolCount > IPrinterStrategy::MAX_TOOLS) {
            toolCount = IPrinterStrategy::MAX_TOOLS;
        }
        cachedToolCount_ = toolCount;
        for (int i = 0; i < toolCount; i++) {
            cachedFilamentPerTool_[i] = strategy_->getFilamentForTool(i);
        }
    }
}

// transition TRACKING → IDLE; resolves filament used and per-tool metrics
void PrinterManager::resolveAndSendJobEnd(int jobId, float progressPercent,
                                           bool allowDeferred, const char* reason) {
    bool canceled = (progressPercent < 100.0f);

    Serial.printf("PrinterManager: Resolving job %d reason=%s progress=%.1f%% filament=%.2fg\n",
                  jobId, reason, progressPercent, currentJobTotalFilamentG_);

    // deferred lookup allows gcode metadata to appear after job ends (some printers delay it)
    if (currentJobTotalFilamentG_ <= 0.0f && strategy_ != nullptr && allowDeferred) {
        float deferred = strategy_->fetchDeferredFilament(jobId);
        if (deferred > 0.0f) {
            currentJobTotalFilamentG_ = deferred;
            Serial.printf("PrinterManager: Deferred filament for job %d: %.2fg\n", jobId, deferred);
        }
    }

    float filamentUsed = (progressPercent / 100.0f) * currentJobTotalFilamentG_;

    AppMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AppMessageType::PRINT_ENDED;
    msg.payload.printEnded.job_id = jobId;
    msg.payload.printEnded.filament_used_grams = filamentUsed;
    msg.payload.printEnded.canceled = canceled;

    // use cached per-tool data (not live strategy object, which may have next job's data)
    if (cachedToolCount_ > 0) {
        msg.payload.printEnded.tool_count = cachedToolCount_;
        for (int i = 0; i < cachedToolCount_; i++) {
            msg.payload.printEnded.filament_per_tool[i] =
                (progressPercent / 100.0f) * cachedFilamentPerTool_[i];
        }
    }

    ApplicationManager::getInstance().sendMessage(msg);

    Serial.printf("PrinterManager: Job %d %s — %.2fg used\n",
                  jobId, canceled ? "canceled" : "finished", filamentUsed);

    state_ = PrinterState::IDLE;
    currentJobId_ = -1;
    currentJobTotalFilamentG_ = 0.0f;
    lastProgressPercent_ = 0.0f;
    missingJobPollCount_ = 0;
    mismatchWarned_ = false;
    tempWarned_ = false;
    validationPending_ = true;
    cachedToolCount_ = 0;
    memset(cachedFilamentPerTool_, 0, sizeof(cachedFilamentPerTool_));
}

void PrinterManager::handleJobDisappeared() {
    float progress = (lastProgressPercent_ >= 95.0f) ? 100.0f : lastProgressPercent_;
    Serial.printf("PrinterManager: Job %d disappeared at %.1f%% — treating as %s\n",
                  currentJobId_, lastProgressPercent_,
                  progress >= 100.0f ? "finished" : "canceled");
    resolveAndSendJobEnd(currentJobId_, progress, true, "job_disappeared");
}

// ── Filament Validation ───────────────────────────────────────────────────

// map OPT material enum to PrusaLink-compatible string for gcode comparison
static const char* optMaterialToString(uint8_t mat) {
    switch (mat) {
        case OPT_MATERIAL_TYPE_PLA:  return "PLA";
        case OPT_MATERIAL_TYPE_PETG: return "PETG";
        case OPT_MATERIAL_TYPE_TPU:  return "TPU";
        case OPT_MATERIAL_TYPE_ABS:  return "ABS";
        case OPT_MATERIAL_TYPE_ASA:  return "ASA";
        case OPT_MATERIAL_TYPE_PC:   return "PC";
        case OPT_MATERIAL_TYPE_PCTG: return "PCTG";
        case OPT_MATERIAL_TYPE_PP:   return "PP";
        case OPT_MATERIAL_TYPE_PA6:
        case OPT_MATERIAL_TYPE_PA11:
        case OPT_MATERIAL_TYPE_PA12:
        case OPT_MATERIAL_TYPE_PA66: return "PA";
        case OPT_MATERIAL_TYPE_HIPS: return "HIPS";
        case OPT_MATERIAL_TYPE_PVA:  return "PVA";
        case OPT_MATERIAL_TYPE_PET:  return "PET";
        default:                     return nullptr;
    }
}

// format filament/temperature mismatch for web UI display
static void sendPrinterWarning(const char* type, const char* expected, const char* actual,
                                float gcodeTemp = 0, int16_t tagMaxTemp = 0) {
    AppMessage warn;
    warn.type = AppMessageType::PRINTER_WARNING;
    memset(&warn.payload.printerWarning, 0, sizeof(warn.payload.printerWarning));
    strncpy(warn.payload.printerWarning.warning_type, type,
            sizeof(warn.payload.printerWarning.warning_type) - 1);
    if (expected) strncpy(warn.payload.printerWarning.expected, expected,
                          sizeof(warn.payload.printerWarning.expected) - 1);
    if (actual) strncpy(warn.payload.printerWarning.actual, actual,
                        sizeof(warn.payload.printerWarning.actual) - 1);
    warn.payload.printerWarning.gcode_temp = gcodeTemp;
    warn.payload.printerWarning.tag_max_temp = tagMaxTemp;
    ApplicationManager::getInstance().sendMessage(warn);
}

void PrinterManager::checkFilamentMismatch() {
    if (!strategy_) return;

    CurrentSpoolState spool;
    if (!NFCManager::getInstance().getCurrentSpoolState(spool)) return;
    if (!spool.present || !spool.tag_data_valid) return;

    validationPending_ = false;

    // warn once per job if gcode filament type doesn't match tag
    if (!mismatchWarned_) {
        const char* expected = strategy_->getExpectedFilamentType();
        if (expected[0] != '\0') {
            uint8_t tagMaterial = 0;
            opt_get_material_type(&spool.tag_data, &tagMaterial);
            const char* tagStr = optMaterialToString(tagMaterial);

            if (tagStr) {
                mismatchWarned_ = true;
                if (strncasecmp(expected, tagStr, strlen(tagStr)) != 0) {
                    Serial.printf("PrinterManager: FILAMENT MISMATCH — gcode expects '%s', tag has '%s'\n", expected, tagStr);
                    sendPrinterWarning("filament_mismatch", expected, tagStr);
                } else {
                    Serial.printf("PrinterManager: Filament OK — gcode '%s' matches tag '%s'\n", expected, tagStr);
                }
            }
        }
    }

    // warn once per job if gcode nozzle temp exceeds tag max + margin
    if (!tempWarned_) {
        float gcodeNozzle = strategy_->getExpectedNozzleTemp();
        if (gcodeNozzle <= 0.0f) return;

        int16_t tagMaxTemp = 0;
        opt_get_max_print_temp(&spool.tag_data, &tagMaxTemp);
        if (tagMaxTemp <= 0) return;

        tempWarned_ = true;
        // 10C margin absorbs transient gcode tolerance variations
        if (gcodeNozzle > static_cast<float>(tagMaxTemp) + 10.0f) {
            Serial.printf("PrinterManager: TEMP WARNING — gcode %.0fC exceeds tag max %dC\n", gcodeNozzle, tagMaxTemp);
            sendPrinterWarning("temp_exceeds_max", nullptr, nullptr, gcodeNozzle, tagMaxTemp);
        }
    }
}
