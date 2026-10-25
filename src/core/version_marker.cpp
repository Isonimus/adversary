/**
 * @file version_marker.cpp
 * @brief NVS-backed implementation of the last-seen-version marker (ADR-0121).
 */

#include "version_marker.h"
#include "config/config.h"

#ifdef ESP32
#include <Preferences.h>

namespace adversary {

// One NVS namespace/key for the marker. The namespace is the device-level bucket ADR-0121
// reserves for must-survive-without-SD markers; the key fits NVS's 15-char limit.
static constexpr const char* NVS_NAMESPACE = "adversary";
static constexpr const char* NVS_KEY_LAST_SEEN = "lastSeenVer";

bool VersionMarker::currentVersionIsNew() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
    String seen = prefs.getString(NVS_KEY_LAST_SEEN, "");
    prefs.end();
    return versionDiffers(seen.c_str(), config::VERSION);
}

void VersionMarker::markCurrentVersionSeen() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
    prefs.putString(NVS_KEY_LAST_SEEN, config::VERSION);
    prefs.end();
}

} // namespace adversary

#else  // !ESP32

// Native build: NVS is device-only hardware, so these are compile-only stubs (the native
// +<core/> glob still compiles this TU). Native never calls them; the only logic under test
// is the pure versionDiffers() in the header. This mirrors the sd_manager.cpp native-stub
// convention for a device-only capability.
namespace adversary {

bool VersionMarker::currentVersionIsNew() { return false; }
void VersionMarker::markCurrentVersionSeen() {}

} // namespace adversary

#endif
