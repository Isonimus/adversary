#pragma once
#include <stddef.h>

namespace adversary {

struct ScriptEntry {
    char        name[48];
    bool        isBuiltIn;
    const char* duckyPtr;   // non-null for built-ins
    char        sdPath[128];

    ScriptEntry() : isBuiltIn(false), duckyPtr(nullptr) {
        name[0]   = '\0';
        sdPath[0] = '\0';
    }
};

} // namespace adversary
