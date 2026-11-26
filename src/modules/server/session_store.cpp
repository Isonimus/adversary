/**
 * @file session_store.cpp
 * @brief Pure, in-RAM dashboard session table (ADR-0170, slice-0171).
 */

#include "session_store.h"

#include <string.h>

namespace adversary {

namespace {

constexpr const char* kCookieName = "adv_session";
constexpr size_t kCookieNameLen = 11;  // strlen("adv_session")

// Skip leading SP/HTAB per RFC 6265 cookie-string parsing.
const char* skipWs(const char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

} // namespace

bool extractSessionToken(const char* cookieHeader, char* out, size_t outCap) {
    if (cookieHeader == nullptr || out == nullptr || outCap == 0) return false;

    const char* p = cookieHeader;
    while (*p != '\0') {
        p = skipWs(p);

        // Candidate name runs until '=' or ';' or end.
        const char* nameStart = p;
        while (*p != '\0' && *p != '=' && *p != ';') ++p;
        size_t nameLen = static_cast<size_t>(p - nameStart);

        // Skip past '=' to the value (if present).
        const char* valueStart = nullptr;
        if (*p == '=') {
            ++p;
            valueStart = p;
            while (*p != '\0' && *p != ';') ++p;
        }

        bool match = (nameLen == kCookieNameLen) &&
                     (memcmp(nameStart, kCookieName, kCookieNameLen) == 0);
        if (match && valueStart != nullptr) {
            size_t valueLen = static_cast<size_t>(p - valueStart);
            // Trim trailing WS from the value.
            while (valueLen > 0 &&
                   (valueStart[valueLen - 1] == ' ' || valueStart[valueLen - 1] == '\t')) {
                --valueLen;
            }
            if (valueLen == 0) return false;          // empty value
            if (valueLen + 1 > outCap) return false;  // doesn't fit
            memcpy(out, valueStart, valueLen);
            out[valueLen] = '\0';
            return true;
        }

        // Advance past the ';' separator (if any) and continue.
        if (*p == ';') ++p;
    }
    return false;
}

bool createSession(Session* table, size_t n, const char* token, uint32_t nowMs) {
    if (table == nullptr || token == nullptr || n == 0) return false;
    size_t tokenLen = strlen(token);
    if (tokenLen == 0 || tokenLen + 1 > sizeof(table[0].token)) return false;

    // Prefer an inactive slot; else LRU-evict the oldest lastSeenMs.
    size_t pick = 0;
    bool foundFree = false;
    uint32_t oldestSeen = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!table[i].active) {
            pick = i;
            foundFree = true;
            break;
        }
        if (i == 0 || table[i].lastSeenMs < oldestSeen) {
            oldestSeen = table[i].lastSeenMs;
            pick = i;
        }
    }
    (void)foundFree;

    memcpy(table[pick].token, token, tokenLen);
    table[pick].token[tokenLen] = '\0';
    table[pick].createdMs = nowMs;
    table[pick].lastSeenMs = nowMs;
    table[pick].active = true;
    return true;
}

bool validateSession(Session* table,
                     size_t n,
                     const char* token,
                     uint32_t nowMs,
                     uint32_t idleTtlMs,
                     uint32_t absTtlMs) {
    if (table == nullptr || token == nullptr || n == 0) return false;
    if (*token == '\0') return false;

    for (size_t i = 0; i < n; ++i) {
        if (!table[i].active) continue;
        if (strcmp(table[i].token, token) != 0) continue;

        // Match. Use subtraction on uint32_t so a millis() wrap stays correct
        // over the TTL window (it is well under 2^31 ms).
        uint32_t idleAge = nowMs - table[i].lastSeenMs;
        uint32_t absAge  = nowMs - table[i].createdMs;
        if (idleAge > idleTtlMs || absAge > absTtlMs) {
            table[i].active = false;  // deactivate so a later replay can't revive it
            return false;
        }
        table[i].lastSeenMs = nowMs;
        return true;
    }
    return false;
}

void clearSession(Session* table, size_t n, const char* token) {
    if (table == nullptr || token == nullptr || n == 0) return;
    for (size_t i = 0; i < n; ++i) {
        if (table[i].active && strcmp(table[i].token, token) == 0) {
            table[i].active = false;
            table[i].token[0] = '\0';
            return;
        }
    }
}

} // namespace adversary
