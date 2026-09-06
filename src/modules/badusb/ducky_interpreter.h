#pragma once
#include "i_hid_keyboard.h"

namespace adversary {

// Minimal DuckyScript 1.0 interpreter.
//
// Supported commands:
//   REM ...              comment (ignored)
//   DELAY <ms>           wait N ms before next command
//   DEFAULTDELAY <ms>    implicit delay after every command
//   STRING <text>        type text using the keyboard's active layout
//   STRINGLN <text>      STRING + ENTER
//   ENTER / TAB / ESC / BACKSPACE / SPACE
//   UP / DOWN / LEFT / RIGHT / DELETE / HOME / END / PAGEUP / PAGEDOWN
//   F1 … F12 / PRINTSCREEN / CAPSLOCK / INSERT
//   GUI [key]            Windows/Cmd key (+ optional second key)
//   CTRL [key]
//   ALT [key]
//   SHIFT [key]
//   CTRL ALT [key]       multi-modifier combos
//   CTRL SHIFT [key]
//   GUI SHIFT [key]
//   REPEAT <n>           repeat the previous command n times
//
// Call load() with a script text, then step() from the main loop each frame.
// The interpreter self-paces DELAY and DEFAULTDELAY without blocking.
class DuckyInterpreter {
public:
    enum class StepResult {
        CONTINUE,  // processed a command, call again next frame
        WAITING,   // inside a DELAY, call again later
        DONE,      // script finished
        ABORTED    // requestAbort() was called
    };

    void load(const char* scriptText);
    void reset();
    bool isRunning() const { return m_running; }
    void requestAbort() { m_abortRequested = true; }
    int currentLine() const { return m_lineNumber; }

    // Process one command per call. Returns WAITING during delays (non-blocking).
    StepResult step(IHidKeyboard& kbd);

private:
    const char* m_script       = nullptr;
    int         m_pos          = 0;
    int         m_len          = 0;
    int         m_lineNumber   = 0;
    bool        m_running      = false;
    bool        m_abortRequested = false;
    uint32_t    m_defaultDelay = 0;
    uint32_t    m_delayUntil   = 0;

    char     m_lastLine[256];
    bool     m_hasLastLine  = false;
    int      m_repeatCount  = 0;    // iterations still pending from a REPEAT command
    char     m_repeatLine[256];     // snapshot of the line being repeated

    // Read the next non-empty, non-comment line into buf. Returns false at EOF.
    bool nextLine(char* buf, int bufSize);

    // Execute one parsed line against the keyboard.
    void executeLine(const char* line, IHidKeyboard& kbd);
    void executeCombo(const char* line, IHidKeyboard& kbd);
};

} // namespace adversary
