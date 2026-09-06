#include "ducky_interpreter.h"
#include "hid_keycodes.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <Arduino.h>

namespace adversary {

void DuckyInterpreter::load(const char* scriptText) {
    m_script         = scriptText;
    m_pos            = 0;
    m_len            = scriptText ? (int)strlen(scriptText) : 0;
    m_lineNumber     = 0;
    m_running        = (m_len > 0);
    m_abortRequested = false;
    m_defaultDelay   = 0;
    m_delayUntil     = 0;
    m_hasLastLine    = false;
    m_lastLine[0]    = '\0';
    m_repeatCount    = 0;
    m_repeatLine[0]  = '\0';
}

void DuckyInterpreter::reset() {
    m_pos            = 0;
    m_lineNumber     = 0;
    m_running        = (m_script && m_len > 0);
    m_abortRequested = false;
    m_defaultDelay   = 0;
    m_delayUntil     = 0;
    m_hasLastLine    = false;
    m_repeatCount    = 0;
}

bool DuckyInterpreter::nextLine(char* buf, int bufSize) {
    while (m_pos < m_len) {
        // Skip leading whitespace / blank lines
        while (m_pos < m_len && (m_script[m_pos] == '\r' || m_script[m_pos] == '\n'))
            m_pos++;
        if (m_pos >= m_len) return false;

        // Copy until newline
        int start = m_pos;
        int end   = start;
        while (end < m_len && m_script[end] != '\n' && m_script[end] != '\r')
            end++;
        m_pos = end;

        int len = end - start;
        if (len <= 0) continue;
        if (len >= bufSize) len = bufSize - 1;
        memcpy(buf, m_script + start, len);
        // Trim trailing spaces/CR
        while (len > 0 && (buf[len-1] == ' ' || buf[len-1] == '\r' || buf[len-1] == '\t'))
            len--;
        buf[len] = '\0';
        if (len == 0) continue;
        m_lineNumber++;
        return true;
    }
    return false;
}

DuckyInterpreter::StepResult DuckyInterpreter::step(IHidKeyboard& kbd) {
    if (!m_running) return StepResult::DONE;
    if (m_abortRequested) { m_running = false; return StepResult::ABORTED; }
    if (millis() < m_delayUntil) return StepResult::WAITING;

    // Execute one pending REPEAT iteration per call so the main loop can check
    // abort and service the watchdog between iterations.
    if (m_repeatCount > 0) {
        executeLine(m_repeatLine, kbd);
        --m_repeatCount;
        // Explicit DELAY inside a repeat sets m_delayUntil; only apply
        // DEFAULTDELAY when no delay was already scheduled (same rule as below).
        if (m_defaultDelay > 0 && m_delayUntil == 0)
            m_delayUntil = millis() + m_defaultDelay;
        return m_running ? StepResult::CONTINUE : StepResult::DONE;
    }

    char line[256];
    if (!nextLine(line, sizeof(line))) {
        m_running = false;
        return StepResult::DONE;
    }

    // Skip comments
    if (strncasecmp(line, "REM", 3) == 0 && (line[3] == ' ' || line[3] == '\t' || line[3] == '\0')) {
        return StepResult::CONTINUE;
    }

    executeLine(line, kbd);

    // Skip DEFAULTDELAY if this line already scheduled its own delay (DELAY wins).
    if (m_defaultDelay > 0 && m_delayUntil == 0) {
        m_delayUntil = millis() + m_defaultDelay;
    }

    return m_running ? StepResult::CONTINUE : StepResult::DONE;
}

// ---------------------------------------------------------------------------
// Parse and execute one DuckyScript line
// ---------------------------------------------------------------------------
void DuckyInterpreter::executeLine(const char* line, IHidKeyboard& kbd) {
    // DELAY <ms>
    if (strncasecmp(line, "DELAY ", 6) == 0) {
        uint32_t ms = (uint32_t)atoi(line + 6);
        m_delayUntil = millis() + ms;
        return;
    }

    // DEFAULTDELAY <ms>
    if (strncasecmp(line, "DEFAULTDELAY ", 13) == 0 ||
        strncasecmp(line, "DEFAULT_DELAY ", 14) == 0) {
        const char* p = strchr(line, ' '); if (!p) return;
        m_defaultDelay = (uint32_t)atoi(p + 1);
        return;
    }

    // STRING <text>
    if (strncasecmp(line, "STRING ", 7) == 0) {
        kbd.typeString(line + 7);
        strncpy(m_lastLine, line, sizeof(m_lastLine)-1);
        m_hasLastLine = true;
        return;
    }

    // STRINGLN <text>  (STRING + ENTER)
    if (strncasecmp(line, "STRINGLN ", 9) == 0) {
        kbd.typeString(line + 9);
        kbd.pressKey(hid_keycodes::keyNameToUsage("ENTER"), 0);
        strncpy(m_lastLine, line, sizeof(m_lastLine)-1);
        m_hasLastLine = true;
        return;
    }

    // REPEAT <n> — store state; step() executes one iteration per call so the
    // main loop can check abort and service the watchdog between iterations.
    if (strncasecmp(line, "REPEAT ", 7) == 0) {
        if (!m_hasLastLine) return;
        m_repeatCount = atoi(line + 7);
        strncpy(m_repeatLine, m_lastLine, sizeof(m_repeatLine) - 1);
        m_repeatLine[sizeof(m_repeatLine) - 1] = '\0';
        return;
    }

    // Standalone keys and modifier combos — all resolved via the KEY_NAMES table.
    executeCombo(line, kbd);
}

void DuckyInterpreter::executeCombo(const char* line, IHidKeyboard& kbd) {
    // Collect modifier bits from space-delimited tokens until we hit a non-modifier.
    // The last token is the key (special name or single char).
    char buf[256];
    strncpy(buf, line, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    uint8_t modifiers = 0;
    uint8_t usage     = 0;

    char* saveptr = nullptr;
    char* tok = strtok_r(buf, " \t", &saveptr);
    while (tok) {
        uint8_t modBit = hid_keycodes::modNameToBit(tok);
        if (modBit) {
            modifiers |= modBit;
        } else {
            // This token is the key
            usage = hid_keycodes::keyNameToUsage(tok);
            break;
        }
        tok = strtok_r(nullptr, " \t", &saveptr);
    }

    if (modifiers == 0 && usage == 0) return;  // unrecognised line
    if (usage == 0 && modifiers != 0) {
        // Modifier-only (e.g. bare "CTRL") — press just the modifier key? Skip for now.
        return;
    }

    kbd.pressKey(usage, modifiers);
    strncpy(m_lastLine, line, sizeof(m_lastLine)-1);
    m_hasLastLine = true;
}

} // namespace adversary
