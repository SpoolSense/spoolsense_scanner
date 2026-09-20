#pragma once

#include <Arduino.h>

// IDF 5 rejects an out-of-range core ID. Preserve intentional affinity on
// dual-core ESP32/S3 targets and let the scheduler place tasks on unicore
// C3/C5/C6 targets.
inline BaseType_t createTaskWithAffinity(TaskFunction_t task,
                                         const char* name,
                                         uint32_t stackDepth,
                                         void* parameter,
                                         UBaseType_t priority,
                                         TaskHandle_t* handle,
                                         BaseType_t core) {
#if CONFIG_FREERTOS_UNICORE
    (void)core;
    return xTaskCreate(task, name, stackDepth, parameter, priority, handle);
#else
    return xTaskCreatePinnedToCore(task, name, stackDepth, parameter, priority, handle, core);
#endif
}

// Single definition of "a live task exists": the scheduler returned pdPASS
// AND installed a non-null handle. All manager startup paths use this so a
// failed creation is never reported as success (issue #264).
inline bool taskCreationSucceeded(BaseType_t result, const TaskHandle_t* handle) {
    return result == pdPASS && handle != nullptr && *handle != nullptr;
}
