#include "core/crash_report.h"

#include <cstdio>

namespace adversary {

ResetClassification classifyReset(ResetReason reason) {
    switch (reason) {
        case ResetReason::Panic:        return {"PANIC (exception/crash)", true};
        case ResetReason::IntWdt:       return {"INT_WDT (interrupt watchdog)", true};
        case ResetReason::TaskWdt:      return {"TASK_WDT (task watchdog)", true};
        case ResetReason::OtherWdt:     return {"WDT (other watchdog)", true};
        case ResetReason::Brownout:     return {"BROWNOUT (power issue)", true};
        case ResetReason::ExternalSdio: return {"SDIO (external reset)", true};
        case ResetReason::PowerOn:      return {"POWERON (normal)", false};
        case ResetReason::Software:     return {"SW (software reset)", false};
        case ResetReason::DeepSleep:    return {"DEEPSLEEP (wakeup)", false};
        case ResetReason::Unknown:      break;
    }
    return {"UNKNOWN", false};
}

int formatCrashEntry(char* out, size_t outSize,
                     const ResetClassification& classification,
                     uint16_t crashCount,
                     bool snapshotValid,
                     const HeapSnapshot& snapshot,
                     const char* timestamp) {
    if (snapshotValid) {
        return snprintf(out, outSize,
            "========================================\n"
            "CRASH DETECTED (count: %u)\n"
            "Reason: %s\n"
            "Time: %s\n"
            "Pre-crash heap (sampled %lu ms into the session):\n"
            "  Free heap:          %lu bytes\n"
            "  Largest free block: %lu bytes\n"
            "  Min free heap:      %lu bytes\n"
            "========================================\n\n",
            static_cast<unsigned>(crashCount), classification.name, timestamp,
            static_cast<unsigned long>(snapshot.uptimeMs),
            static_cast<unsigned long>(snapshot.freeHeap),
            static_cast<unsigned long>(snapshot.largestFreeBlock),
            static_cast<unsigned long>(snapshot.minFreeHeap));
    }
    return snprintf(out, outSize,
        "========================================\n"
        "CRASH DETECTED (count: %u)\n"
        "Reason: %s\n"
        "Time: %s\n"
        "Pre-crash heap: unavailable (RTC flight recorder did not survive this reset:\n"
        "  cold power-on or brownout wipes it, and the session had not yet sampled)\n"
        "========================================\n\n",
        static_cast<unsigned>(crashCount), classification.name, timestamp);
}

} // namespace adversary
