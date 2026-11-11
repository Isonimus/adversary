#include "core/crash_report.h"

#include <cctype>
#include <cstdio>
#include <ctime>

namespace adversary {

ResetClassification classifyReset(ResetReason reason) {
    switch (reason) {
        case ResetReason::Panic:        return {"PANIC (exception/crash)", true};
        case ResetReason::IntWdt:       return {"INT_WDT (interrupt watchdog)", true};
        case ResetReason::TaskWdt:      return {"TASK_WDT (task watchdog)", true};
        case ResetReason::OtherWdt:     return {"WDT (other watchdog)", true};
        case ResetReason::Brownout:     return {"BROWNOUT (power issue)", true};
        case ResetReason::ExternalSdio: return {"SDIO (external reset)", true};
        case ResetReason::CpuLockup:    return {"CPU_LOCKUP (double exception/crash)", true};
        case ResetReason::PowerGlitch:  return {"PWR_GLITCH (power anomaly)", true};
        case ResetReason::Efuse:        return {"EFUSE (efuse read error)", true};
        case ResetReason::PowerOn:      return {"POWERON (normal)", false};
        case ResetReason::Software:     return {"SW (software reset)", false};
        case ResetReason::DeepSleep:    return {"DEEPSLEEP (wakeup)", false};
        case ResetReason::UsbReset:     return {"USB (USB-triggered reset)", false};
        case ResetReason::Jtag:         return {"JTAG (debugger reset)", false};
        case ResetReason::ExternalPin:  return {"EXT (external pin reset)", false};
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

int composeCoredumpPath(char* out, size_t outSize,
                        uint32_t epochSec,
                        uint32_t uptimeMs,
                        const char* reasonName) {
    // Extract the leading identifier of reasonName into a filename-safe tag:
    // chars until the first space, '(' or NUL, uppercased, bounded at 16 chars so an
    // unexpected long name still produces a bounded filename. A null or empty name
    // falls back to "UNK" so the path is always well-formed.
    char tag[17];
    size_t tagLen = 0;
    if (reasonName != nullptr) {
        for (const char* p = reasonName;
             *p != '\0' && *p != ' ' && *p != '(' && tagLen < sizeof(tag) - 1;
             ++p) {
            tag[tagLen++] = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
        }
    }
    if (tagLen == 0) {
        tag[tagLen++] = 'U';
        tag[tagLen++] = 'N';
        tag[tagLen++] = 'K';
    }
    tag[tagLen] = '\0';

    // Stamp: wall-clock when the RTC flight recorder had a synced time, else uptime so
    // a boot-time crash still names a unique file. Compact ISO-ish YYYYMMDDTHHMMSSZ so
    // the filename sorts chronologically on the SD browser.
    char stamp[24];
    if (epochSec != 0) {
        const time_t when = static_cast<time_t>(epochSec);
        struct tm utc;
        gmtime_r(&when, &utc);
        strftime(stamp, sizeof(stamp), "%Y%m%dT%H%M%SZ", &utc);
    } else {
        snprintf(stamp, sizeof(stamp), "boot-%lums", static_cast<unsigned long>(uptimeMs));
    }

    return snprintf(out, outSize, "/adversary/coredumps/%s-%s.elf", stamp, tag);
}

} // namespace adversary
