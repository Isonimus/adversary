/**
 * @file radio_screen.cpp
 * @brief RadioScreen — CC1101 OOK capture/replay console (slice-0003).
 */

#include "radio_screen.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "../../hal/expansion/expansion_cap.h"
#include "../../hal/expansion/cc1101.h"
#include "../../hal/expansion/nrf24.h"
#include "../../hal/storage/sd_manager.h"
#include "../../hal/input/input_manager.h"
#include "../../modules/rf/ook_rmt.h"
#include "../components/toast_manager.h"
#include "../components/pulse_strip.h"

#ifdef ESP32
#include <Arduino.h>
#include <SD.h>
#endif

namespace adversary {

namespace {

struct FreqPreset {
    double mhz;
    const char* label;
};

// The OOK bands the console offers, keyed to the named constants in cc1101.h.
const FreqPreset PRESETS[] = {
    {hal::CC1101_FREQ_315_MHZ, "315.00 MHz"},
    {hal::CC1101_FREQ_43392_MHZ, "433.92 MHz"},
    {hal::CC1101_FREQ_86835_MHZ, "868.35 MHz"},
    {hal::CC1101_FREQ_915_MHZ, "915.00 MHz"},
};
constexpr int PRESET_COUNT = 4;
constexpr int DEFAULT_PRESET = 1;  // 433.92 MHz — the common fixed-code band

// The band-sweep samples exactly the capture presets, so its model width and the
// preset table must agree or the sweep would index past the peak array.
static_assert(PRESET_COUNT == static_cast<int>(rf::BAND_SWEEP_COUNT),
              "band-sweep band count must match the frequency-preset table");

// CC1101 Sub-GHz console main menu. Named so the dispatch reads by intent, not by
// bare index — adding "Band sweep" here shifts the later items, so the indices
// must never be spelled as literals.
enum MainItem : int {
    MAIN_CAPTURE = 0,
    MAIN_BAND_SWEEP,
    MAIN_SAVED,
    MAIN_FREQUENCY,
    MAIN_JAM,
    MAIN_ITEM_COUNT
};
const char* const MAIN_ITEMS[MAIN_ITEM_COUNT] = {
    "Capture", "Band sweep", "Saved signals", "Frequency", "Jam"
};

// Bound for a formatted feedback line; toasts truncate at 63.
constexpr size_t MESSAGE_BUF = 64;

// Band-sweep display scaling: dBm mapped onto bar height. SWEEP_FLOOR is an empty
// bar (ambient noise), SWEEP_CEIL a full one (a remote right on top of the radio).
constexpr int16_t SWEEP_FLOOR_DBM = -110;
constexpr int16_t SWEEP_CEIL_DBM = -20;
// RSSI settle time after entering RX before the reading is valid (CC1101 needs a
// few RSSI samples at this RX bandwidth; measured headroom added on top).
constexpr uint32_t RSSI_SETTLE_US = 1500;

// Move a wrapping list cursor with the Cardputer's ;/. keys and follow it with
// the scroll window. Returns true iff @p key was an up/down key.
bool listNav(char key, int& selection, int count, int& scroll, int visible) {
    if (count <= 0) return false;
    if (key == ';') {
        selection = (selection - 1 + count) % count;
    } else if (key == '.') {
        selection = (selection + 1) % count;
    } else {
        return false;
    }
    if (selection < scroll) scroll = selection;
    else if (selection >= scroll + visible) scroll = selection - visible + 1;
    return true;
}

}  // namespace

RadioScreen::RadioScreen()
    : visible_(false)
    , shouldExit_(false)
    , needsRedraw_(true)
    , available_(false)
    , state_(MenuState::RADIO_SELECT)
    , lastHintState_(MenuState::RADIO_SELECT)
    , radioSelection_(0)
    , mainSelection_(0)
    , presetIndex_(DEFAULT_PRESET)
    , actionSelection_(0)
    , fileSelection_(0)
    , fileScroll_(0)
    , captureStage_(0)
    , jamModeSel_(0)
    , jamArmed_(false)
    , jamEmitting_(false)
    , actionSignalValid_(false)
    , scanChannel_(0)
    , scanActive_(false)
    , sweepBand_(0)
    , sweepActive_(false)
{
}

RadioScreen::~RadioScreen() {
    deinit();
}

void RadioScreen::init() {}

void RadioScreen::deinit() {
    hide();
}

void RadioScreen::show() {
    visible_ = true;
    shouldExit_ = false;
    needsRedraw_ = true;
    state_ = MenuState::RADIO_SELECT;
    radioSelection_ = 0;
    mainSelection_ = 0;
    captureStage_ = 0;

    available_ = (hal::resolvedExpansionCap() == hal::ExpansionCap::MultiRadio) &&
                 (SDManager::getInstance().spiBus() != nullptr);

    lastHintState_ = state_;
    updateFooterHints();
}

// Footer hints follow the state so the operator always sees the live actions
// (the house convention: FooterHints::handleInputWithDispatch + per-state setHints).
void RadioScreen::updateFooterHints() {
    if (!available_) {
        footerHints_.setHints({{'`', "Back"}});
        return;
    }
    switch (state_) {
        case MenuState::RADIO_SELECT:
        case MenuState::MAIN:
            footerHints_.setHints({{' ', "Focus"}, {';', "Nav"}, {'\n', "Select"}, {'`', "Back"}});
            break;
        case MenuState::NRF_SCAN:
        case MenuState::BAND_SWEEP:
            footerHints_.setHints({{'`', "Back"}});  // live sweep owns the view
            break;
        case MenuState::SIGNAL_LIST:
            footerHints_.setHints({{' ', "Focus"}, {';', "Nav"},
                                   {'\n', "Open", !signals_.empty()}, {'`', "Back"}});
            break;
        case MenuState::SIGNAL_ACTION:
            footerHints_.setHints({{' ', "Focus"}, {';', "Nav"}, {'\n', "Select"}, {'`', "Back"}});
            break;
        case MenuState::REPLAY_CONFIRM:
        case MenuState::DELETE_CONFIRM:
            footerHints_.setHints({{'Y', "Yes"}, {'N', "No"}, {'`', "Cancel"}});
            break;
        case MenuState::CAPTURE_REVIEW:
            footerHints_.setHints({{'S', "Save"}, {'`', "Discard"}});
            break;
        case MenuState::FREQ_SELECT:
            footerHints_.setHints({{' ', "Focus"}, {';', "Nav"}, {'\n', "OK"}, {'`', "Back"}});
            break;
        case MenuState::JAM_SELECT:
            footerHints_.setHints({{' ', "Focus"}, {';', "Nav"}, {'\n', "Select"}, {'`', "Back"}});
            break;
        case MenuState::JAM_CONFIRM:
            footerHints_.setHints({{'Y', "Arm"}, {'N', "No"}, {'`', "Cancel"}});
            break;
        case MenuState::JAM_ACTIVE:
            footerHints_.setHints({});  // hold-to-jam owns the input; SPACE = emit
            break;
        case MenuState::CAPTURING:
        case MenuState::NAMING:
            footerHints_.setHints({});  // blocking / popup owns the input
            break;
    }
}

void RadioScreen::hide() {
    // Leaving the screen mid-sweep or mid-jam must power the radio down and release
    // the bus, or a stuck carrier / unowned SD bus outlives the screen.
    if (scanActive_) stopNrfScan();
    if (sweepActive_) stopBandSweep();
    if (jamArmed_) jamDisarm();
    visible_ = false;
}

double RadioScreen::currentFreqMHz() const {
    return PRESETS[presetIndex_].mhz;
}

void RadioScreen::fullPath(const char* name, char* out, size_t outSize) const {
    snprintf(out, outSize, "%s/%s", config::SD_SUBGHZ_PATH, name);
}

// --- Capture / replay orchestration (borrowed bus; remount after) ------------

bool RadioScreen::captureSignal(rf::OokSignal& out) {
    const double mhz = currentFreqMHz();
    bool ok = false;
    if (hal::cc1101ConfigureOok(mhz) && hal::cc1101EnterRx()) {
        ok = rf::ookRmtCapture(CAPTURE_WINDOW_MS, out);
    }
    hal::cc1101Idle();
    SDManager::getInstance().remount();
    if (ok) out.frequencyHz = static_cast<uint32_t>(llround(mhz * 1000000.0));
    return ok;
}

bool RadioScreen::replaySignal(const rf::OokSignal& sig) {
    const double mhz = static_cast<double>(sig.frequencyHz) / 1000000.0;
    bool ok = false;
    if (hal::cc1101ConfigureOok(mhz) && hal::cc1101EnterTx()) {
        ok = rf::ookRmtReplay(sig);
    }
    hal::cc1101Idle();
    SDManager::getInstance().remount();
    return ok;
}

void RadioScreen::startCapture() {
    state_ = MenuState::CAPTURING;
    captureStage_ = 1;  // let "Listening" paint before the blocking RX window
    needsRedraw_ = true;
}

void RadioScreen::performCapture() {
    rf::OokSignal sig;
    if (captureSignal(sig) && !sig.durationsUs.empty()) {
#ifdef ESP32
        // Operator feedback: how many edges and a sample of their widths. With no
        // transmitter these are the ambient noise-floor edges; a real remote gives
        // a longer, regular ~100-1000 us train.
        const size_t sample = sig.durationsUs.size() < 6 ? sig.durationsUs.size() : 6;
        Serial.printf("[Radio] Captured %u edges on %s; first:",
                      static_cast<unsigned>(sig.durationsUs.size()),
                      PRESETS[presetIndex_].label);
        for (size_t i = 0; i < sample; ++i) Serial.printf(" %uus", sig.durationsUs[i]);
        Serial.println();
#endif
        pendingSignal_ = sig;
        // Show the pulse-strip preview first so the operator can judge the
        // capture (real train vs ambient noise floor) before naming it.
        state_ = MenuState::CAPTURE_REVIEW;
    } else {
#ifdef ESP32
        Serial.println("[Radio] No signal captured (RX window timed out)");
#endif
        showErrorToast("No signal captured");
        state_ = MenuState::MAIN;
    }
    captureStage_ = 0;
    needsRedraw_ = true;
}

void RadioScreen::promptForName() {
    namePopup_.setOnSubmit([this](const char* text) { saveCapturedSignal(text); });
    namePopup_.setOnCancel([this]() {
        showToast("Capture discarded");
        state_ = MenuState::MAIN;
    });
    namePopup_.show("Signal name", "capture", false, NAME_MAX_LEN);
    state_ = MenuState::NAMING;
    needsRedraw_ = true;
}

void RadioScreen::describeSignal(const rf::OokSignal& sig, char* out,
                                 size_t outSize) const {
    // OOK is always raw (no protocol decode, per slice-0003), so the encoding
    // line is edge count plus the carrier it was captured on.
    snprintf(out, outSize, "RAW %u edges  %.2f MHz",
             static_cast<unsigned>(sig.durationsUs.size()),
             static_cast<double>(sig.frequencyHz) / 1000000.0);
}

void RadioScreen::saveCapturedSignal(const char* name) {
    // Sanitise: strip path separators, bound to NAME_MAX_LEN, reject empty.
    char safe[NAME_MAX_LEN + 1];
    size_t j = 0;
    for (size_t i = 0; name[i] != '\0' && j < NAME_MAX_LEN; ++i) {
        char c = name[i];
        safe[j++] = (c == '/' || c == '\\') ? '_' : c;
    }
    safe[j] = '\0';
    if (j == 0) {
        showWarningToast("Name required");
        state_ = MenuState::MAIN;
        needsRedraw_ = true;
        return;
    }

    SDManager& sd = SDManager::getInstance();
    sd.createDirectory(config::SD_SUBGHZ_PATH);

    const size_t count = pendingSignal_.durationsUs.size();
    std::vector<uint8_t> buffer(rf::ookSignalSerializedSize(count));
    const size_t written = rf::serializeOokSignal(pendingSignal_, buffer.data(),
                                                  buffer.size());
    if (written == 0) {
        showErrorToast("Serialize failed");
        state_ = MenuState::MAIN;
        needsRedraw_ = true;
        return;
    }

    char path[128];
    snprintf(path, sizeof(path), "%s/%s.sub", config::SD_SUBGHZ_PATH, safe);
    const FileResult result = sd.writeFile(path, buffer.data(), written);
    if (result.success) {
        char message[MESSAGE_BUF];
        snprintf(message, sizeof(message), "Saved %s.sub", safe);
        showSuccessToast(message);
    } else {
        showErrorToast("Save failed");
    }
    state_ = MenuState::MAIN;
    needsRedraw_ = true;
}

void RadioScreen::loadSignalList() {
    signals_.clear();
#ifdef ESP32
    File dir = SD.open(config::SD_SUBGHZ_PATH);
    if (dir && dir.isDirectory()) {
        File entry;
        while ((entry = dir.openNextFile())) {
            if (!entry.isDirectory()) {
                const char* raw = entry.name();
                const char* slash = strrchr(raw, '/');
                const char* base = slash ? slash + 1 : raw;  // basename, not path
                if (strstr(base, ".sub") != nullptr) {
                    SignalEntry item;
                    strncpy(item.name, base, sizeof(item.name) - 1);
                    item.name[sizeof(item.name) - 1] = '\0';
                    item.size = entry.size();
                    signals_.push_back(item);
                }
            }
            entry.close();
        }
        dir.close();
    }
#endif
}

void RadioScreen::loadActionSignal() {
    // Read + decode the selected signal so the detail screen can preview it. A
    // failure is non-fatal (the actions still work; replay re-reads with its own
    // guards), so we only flag it and let the draw fall back to a placeholder.
    actionSignalValid_ = false;
    if (fileSelection_ < 0 || fileSelection_ >= static_cast<int>(signals_.size())) {
        return;
    }
    char path[128];
    fullPath(signals_[fileSelection_].name, path, sizeof(path));

    SDManager& sd = SDManager::getInstance();
    const size_t size = sd.getFileSize(path);
    if (size == 0 || size > rf::ookSignalSerializedSize(rf::OOK_MAX_PULSES)) return;

    std::vector<uint8_t> buffer(size);
    const FileResult result = sd.readFile(path, buffer.data(), buffer.size());
    if (result.success &&
        rf::deserializeOokSignal(buffer.data(), result.bytesRead, actionSignal_)) {
        actionSignalValid_ = true;
    }
}

void RadioScreen::replaySelected() {
#ifdef ESP32
    if (fileSelection_ < 0 || fileSelection_ >= static_cast<int>(signals_.size())) {
        state_ = MenuState::SIGNAL_LIST;
        return;
    }
    char path[128];
    fullPath(signals_[fileSelection_].name, path, sizeof(path));

    SDManager& sd = SDManager::getInstance();
    const size_t size = sd.getFileSize(path);
    if (size == 0 || size > rf::ookSignalSerializedSize(rf::OOK_MAX_PULSES)) {
        showErrorToast("Bad file size");
        state_ = MenuState::SIGNAL_ACTION;
        needsRedraw_ = true;
        return;
    }
    std::vector<uint8_t> buffer(size);
    const FileResult result = sd.readFile(path, buffer.data(), buffer.size());
    rf::OokSignal sig;
    if (!result.success ||
        !rf::deserializeOokSignal(buffer.data(), result.bytesRead, sig)) {
        showErrorToast("Corrupt signal");
        state_ = MenuState::SIGNAL_ACTION;
        needsRedraw_ = true;
        return;
    }
    const bool ok = replaySignal(sig);
    if (ok) showSuccessToast("Replayed");
    else showErrorToast("Replay failed");
    state_ = MenuState::SIGNAL_ACTION;
    needsRedraw_ = true;
#endif
}

void RadioScreen::deleteSelected() {
#ifdef ESP32
    if (fileSelection_ < 0 || fileSelection_ >= static_cast<int>(signals_.size())) {
        state_ = MenuState::SIGNAL_LIST;
        return;
    }
    char path[128];
    fullPath(signals_[fileSelection_].name, path, sizeof(path));
    if (SD.remove(path)) showSuccessToast("Deleted");
    else showErrorToast("Delete failed");

    loadSignalList();
    if (fileSelection_ >= static_cast<int>(signals_.size())) {
        fileSelection_ = signals_.empty() ? 0 : static_cast<int>(signals_.size()) - 1;
    }
    fileScroll_ = 0;
    state_ = MenuState::SIGNAL_LIST;
    needsRedraw_ = true;
#endif
}

// --- NRF24 2.4 GHz analyzer (borrowed bus; remount on exit) ------------------

// The analyzer's channel array and the NRF24 band must agree, or the sweep would
// index past the tally (or leave channels unswept).
static_assert(rf::SPECTRUM_CHANNELS == hal::NRF24_CHANNEL_COUNT,
              "spectrum channel count must match the NRF24 band");

void RadioScreen::startNrfScan() {
    spectrumScan_.reset();
    scanChannel_ = 0;
    scanActive_ = hal::nrf24BeginRxScan();
    if (scanActive_) {
        state_ = MenuState::NRF_SCAN;
    } else {
        showErrorToast("NRF24 unavailable");
        state_ = MenuState::RADIO_SELECT;
    }
    needsRedraw_ = true;
}

void RadioScreen::stopNrfScan() {
    hal::nrf24Idle();
    SDManager::getInstance().remount();
    scanActive_ = false;
}

void RadioScreen::stepNrfScan() {
    for (uint8_t n = 0; n < NRF_CHANNELS_PER_UPDATE; ++n) {
        hal::nrf24SetChannel(scanChannel_);
        if (hal::nrf24SampleRpd(NRF_DWELL_US)) {
            spectrumScan_.hits[scanChannel_]++;
        }
        if (scanChannel_ >= hal::NRF24_MAX_CHANNEL) {
            scanChannel_ = 0;
            if (spectrumScan_.sweeps >= NRF_MAX_SWEEPS) spectrumScan_.reset();
            spectrumScan_.sweeps++;  // one full 0..125 sweep completed
        } else {
            scanChannel_++;
        }
    }
    needsRedraw_ = true;
}

// --- CC1101 RSSI band-sweep (slice-0019): borrowed bus; remount on exit ------

void RadioScreen::startBandSweep() {
    bandSweep_.reset();
    sweepBand_ = 0;
    sweepActive_ = true;
    state_ = MenuState::BAND_SWEEP;
    needsRedraw_ = true;
}

void RadioScreen::stopBandSweep() {
#ifdef ESP32
    hal::cc1101Idle();
    SDManager::getInstance().remount();
#endif
    sweepActive_ = false;
}

void RadioScreen::stepBandSweep() {
#ifdef ESP32
    // Retune to the next band and read RSSI. cc1101ConfigureOok() starts with SRES,
    // so it re-tunes cleanly from the previous band with no explicit idle between;
    // the radio stays owned across bands (remount happens only on exit).
    const double mhz = PRESETS[sweepBand_].mhz;
    if (hal::cc1101ConfigureOok(mhz) && hal::cc1101EnterRx()) {
        delayMicroseconds(RSSI_SETTLE_US);
        const int16_t dbm = hal::cc1101ReadRssiDbm();
        if (dbm != INT16_MIN) bandSweep_.observe(sweepBand_, dbm);
    }
    sweepBand_ = static_cast<uint8_t>((sweepBand_ + 1) % PRESET_COUNT);
    needsRedraw_ = true;
#endif
}

// --- CC1101 jammer (slice-0017): borrowed bus; remount on exit ---------------

bool RadioScreen::jamArm() {
    jamEmitting_ = false;
#ifdef ESP32
    // Arm into TX on the selected preset; with GDO0 low the PA emits nothing until
    // a burst drives it, so the hold gates emission, not the radio state.
    if (hal::cc1101ConfigureOok(currentFreqMHz()) && hal::cc1101EnterTx()) {
        jamArmed_ = true;
        return true;
    }
    hal::cc1101Idle();
    SDManager::getInstance().remount();
#endif
    return false;
}

void RadioScreen::jamDisarm() {
    jamEmitting_ = false;
    jamArmed_ = false;
#ifdef ESP32
    hal::cc1101Idle();
    SDManager::getInstance().remount();
#endif
}

void RadioScreen::stepJam() {
#ifdef ESP32
    const bool held = InputManager::getInstance().isKeyDownNow(JAM_HOLD_KEY);
    if (held) {
        const rf::JamMode mode = (jamModeSel_ == 0) ? rf::JamMode::CarrierWave
                                                    : rf::JamMode::ModulatedNoise;
        rf::ookJamBurst(mode, JAM_CHUNK_MS);  // one burst, then GDO0 is released
    }
    if (held != jamEmitting_) {
        jamEmitting_ = held;
        needsRedraw_ = true;  // flip the live indicator only on a change
    }
#endif
}

// --- Frame update ------------------------------------------------------------

void RadioScreen::update() {
    if (!available_ || namePopup_.isVisible()) return;
    if (state_ == MenuState::CAPTURING) {
        if (captureStage_ < 2) {
            captureStage_++;  // give render a frame to show "Listening"
            needsRedraw_ = true;
        } else if (captureStage_ == 2) {
            captureStage_ = 3;
            performCapture();
        }
    } else if (state_ == MenuState::NRF_SCAN && scanActive_) {
        stepNrfScan();
    } else if (state_ == MenuState::BAND_SWEEP && sweepActive_) {
        stepBandSweep();
    } else if (state_ == MenuState::JAM_ACTIVE && jamArmed_) {
        stepJam();
    }
}

// --- Input -------------------------------------------------------------------

bool RadioScreen::handleInput(char key) {
    needsRedraw_ = true;

    if (namePopup_.isVisible()) {
        namePopup_.handleInput(key);  // its callbacks move state_
        return true;
    }

    if (key == 0x1B) key = '`';  // ESC is an alias for Back/Cancel

    if (!available_) {
        if (key == '`' || key == '\n' || key == '\r') shouldExit_ = true;
        return true;
    }

    // Footer interaction (space focus, ;/. cycle, Enter dispatch). When the footer
    // is unfocused this returns false and the state keys below run as list nav.
    char footerAction = 0;
    if (footerHints_.handleInputWithDispatch(key, footerAction)) {
        if (footerAction == 0) return true;  // focus toggle / footer navigation
        key = footerAction;                  // run the chosen action through the switch
    }

    int unusedScroll = 0;
    switch (state_) {
        case MenuState::RADIO_SELECT:
            if (listNav(key, radioSelection_, 2, unusedScroll, 2)) return true;
            if (key == '\n' || key == '\r') {
                if (radioSelection_ == 0) {
                    mainSelection_ = 0;
                    state_ = MenuState::MAIN;  // CC1101 Sub-GHz console
                } else {
                    startNrfScan();  // NRF24 2.4 GHz analyzer
                }
            } else if (key == '`') {
                shouldExit_ = true;
            }
            return true;

        case MenuState::MAIN:
            if (listNav(key, mainSelection_, MAIN_ITEM_COUNT, unusedScroll,
                        MAIN_ITEM_COUNT)) {
                return true;
            }
            if (key == '\n' || key == '\r') {
                switch (mainSelection_) {
                    case MAIN_CAPTURE:
                        startCapture();
                        break;
                    case MAIN_BAND_SWEEP:
                        startBandSweep();
                        break;
                    case MAIN_SAVED:
                        loadSignalList();
                        fileSelection_ = 0;
                        fileScroll_ = 0;
                        state_ = MenuState::SIGNAL_LIST;
                        break;
                    case MAIN_FREQUENCY:
                        state_ = MenuState::FREQ_SELECT;
                        break;
                    case MAIN_JAM:
                        jamModeSel_ = 0;
                        state_ = MenuState::JAM_SELECT;
                        break;
                }
            } else if (key == '`') {
                state_ = MenuState::RADIO_SELECT;  // back to the radio root
            }
            return true;

        case MenuState::NRF_SCAN:
            if (key == '`') {
                stopNrfScan();
                state_ = MenuState::RADIO_SELECT;
            }
            return true;

        case MenuState::BAND_SWEEP:
            if (key == '`') {
                stopBandSweep();
                state_ = MenuState::MAIN;  // back to the CC1101 console
            }
            return true;

        case MenuState::SIGNAL_LIST:
            if (listNav(key, fileSelection_, static_cast<int>(signals_.size()),
                        fileScroll_, VISIBLE_ROWS)) {
                return true;
            }
            if ((key == '\n' || key == '\r') && !signals_.empty()) {
                actionSelection_ = 0;
                loadActionSignal();
                state_ = MenuState::SIGNAL_ACTION;
            } else if (key == '`') {
                state_ = MenuState::MAIN;
            }
            return true;

        case MenuState::SIGNAL_ACTION:
            if (listNav(key, actionSelection_, 2, unusedScroll, 2)) return true;
            if (key == '\n' || key == '\r') {
                state_ = (actionSelection_ == 0) ? MenuState::REPLAY_CONFIRM
                                                 : MenuState::DELETE_CONFIRM;
            } else if (key == '`') {
                state_ = MenuState::SIGNAL_LIST;
            }
            return true;

        case MenuState::REPLAY_CONFIRM:
            if (key == 'y' || key == 'Y') replaySelected();
            else if (key == 'n' || key == 'N' || key == '`') state_ = MenuState::SIGNAL_ACTION;
            return true;

        case MenuState::DELETE_CONFIRM:
            if (key == 'y' || key == 'Y') deleteSelected();
            else if (key == 'n' || key == 'N' || key == '`') state_ = MenuState::SIGNAL_ACTION;
            return true;

        case MenuState::FREQ_SELECT:
            if (listNav(key, presetIndex_, PRESET_COUNT, unusedScroll, PRESET_COUNT)) {
                return true;
            }
            if (key == '\n' || key == '\r' || key == '`') state_ = MenuState::MAIN;
            return true;

        case MenuState::JAM_SELECT:
            if (listNav(key, jamModeSel_, 2, unusedScroll, 2)) return true;
            if (key == '\n' || key == '\r') state_ = MenuState::JAM_CONFIRM;
            else if (key == '`') state_ = MenuState::MAIN;
            return true;

        case MenuState::JAM_CONFIRM:
            if (key == 'y' || key == 'Y') {
                if (jamArm()) {
                    state_ = MenuState::JAM_ACTIVE;
                } else {
                    showErrorToast("Radio arm failed");
                    state_ = MenuState::MAIN;
                }
            } else if (key == 'n' || key == 'N' || key == '`') {
                state_ = MenuState::JAM_SELECT;
            }
            return true;

        case MenuState::JAM_ACTIVE:
            // Hold-to-jam is polled in stepJam(); here we only catch the exit. Any
            // other key (the SPACE hold included) is ignored.
            if (key == '`') {
                jamDisarm();
                state_ = MenuState::MAIN;
            }
            return true;

        case MenuState::CAPTURE_REVIEW:
            if (key == '\n' || key == '\r' || key == 's' || key == 'S') {
                promptForName();
            } else if (key == '`') {
                showToast("Capture discarded");
                state_ = MenuState::MAIN;
            }
            return true;

        case MenuState::CAPTURING:
        case MenuState::NAMING:
            return true;  // blocking / popup-owned
    }
    return true;
}

// --- Render ------------------------------------------------------------------

void RadioScreen::render(Canvas& canvas) {
    if (!visible_) return;

#ifdef ESP32
    if (!needsRedraw_) return;
    needsRedraw_ = false;

    if (state_ != lastHintState_) {
        updateFooterHints();
        lastHintState_ = state_;
    }

    canvas.fillScreen(theme::BG_PRIMARY());
    ui::StatusBar::render(canvas, "RADIO", nullptr, theme::BG_SECONDARY(),
                          theme::TEXT_PRIMARY());

    if (!available_) {
        drawUnavailable(canvas);
        footerHints_.render(canvas);
        return;
    }

    switch (state_) {
        case MenuState::RADIO_SELECT: drawRadioSelect(canvas); break;
        case MenuState::NRF_SCAN:     drawNrfScan(canvas); break;
        case MenuState::BAND_SWEEP:   drawBandSweep(canvas); break;
        case MenuState::MAIN:
        case MenuState::NAMING:      drawMain(canvas); break;
        case MenuState::CAPTURING:   drawCapturing(canvas); break;
        case MenuState::CAPTURE_REVIEW: drawCaptureReview(canvas); break;
        case MenuState::SIGNAL_LIST: drawSignalList(canvas); break;
        case MenuState::SIGNAL_ACTION: drawSignalAction(canvas); break;
        case MenuState::REPLAY_CONFIRM:
            drawConfirm(canvas, "Transmit this signal?", theme::WARNING());
            break;
        case MenuState::DELETE_CONFIRM:
            drawConfirm(canvas, "Delete this signal?", theme::WARNING());
            break;
        case MenuState::FREQ_SELECT:  drawFreqSelect(canvas); break;
        case MenuState::JAM_SELECT:
        case MenuState::JAM_CONFIRM:
        case MenuState::JAM_ACTIVE:   drawJam(canvas); break;
    }

    footerHints_.render(canvas);
    if (namePopup_.isVisible()) namePopup_.render(canvas);
#else
    (void)canvas;
#endif
}

void RadioScreen::drawUnavailable(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 6;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::WARNING());
    canvas.setCursor(x, y);
    canvas.print("Radio cap unavailable");
    y += LINE_HEIGHT + 2;

    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    if (hal::resolvedExpansionCap() != hal::ExpansionCap::MultiRadio) {
        canvas.print("No multi-radio cap resolved");
    } else {
        canvas.print("SD bus not owned (launcher)");
    }
    y += LINE_HEIGHT;
    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.setCursor(x, y);
    canvas.printf("CC1101 VER 0x%02X  NRF24 %s", hal::lastCapProbe().cc1101Version,
                  hal::lastCapProbe().nrf24Present ? "OK" : "--");
}

void RadioScreen::drawRadioSelect(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 6;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(x, y);
    canvas.print("Multi-Radio cap");
    y += LINE_HEIGHT + 2;

    const char* items[2] = {"CC1101 Sub-GHz", "NRF24 2.4 GHz"};
    for (int i = 0; i < 2; ++i) {
        if (i == radioSelection_) {
            canvas.fillRect(0, y - 2, canvas.width(), LINE_HEIGHT, theme::BG_SELECTED());
            canvas.setTextColor(theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(theme::TEXT_SECONDARY());
        }
        canvas.setCursor(x, y);
        canvas.print(items[i]);
        y += LINE_HEIGHT;
    }
}

void RadioScreen::drawNrfScan(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 4;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(x, y);
    canvas.printf("NRF24 2.4GHz  sweeps %u",
                  static_cast<unsigned>(spectrumScan_.sweeps));
    y += LINE_HEIGHT;

    // Occupancy bar band: one column per plot pixel, each bar the channel band's
    // max occupancy over the sweep window (spectrumColumns()).
    const int16_t plotX = x;
    const int16_t plotW = static_cast<int16_t>(canvas.width() - 2 * theme::PADDING_MD);
    const int16_t plotTop = y + 2;
    constexpr int16_t PLOT_H = 56;
    const int16_t baseline = plotTop + PLOT_H;
    canvas.drawRect(plotX, plotTop, plotW, PLOT_H, theme::TEXT_SECONDARY());

    uint8_t cols[240];
    int width = plotW - 2;
    if (width > static_cast<int>(sizeof(cols))) width = static_cast<int>(sizeof(cols));
    if (width > 0 &&
        rf::spectrumColumns(spectrumScan_, cols, static_cast<size_t>(width))) {
        const int16_t innerH = PLOT_H - 2;
        for (int c = 0; c < width; ++c) {
            const int barH = cols[c] * innerH / rf::SPECTRUM_FULL;
            if (barH > 0) {
                canvas.fillRect(plotX + 1 + c, baseline - 1 - barH, 1, barH,
                                theme::SUCCESS());
            }
        }
    } else {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(plotX + 4, plotTop + PLOT_H / 2 - 4);
        canvas.print("Sampling...");
    }

    // Band scale and the honest RPD limitation (occupancy, not signal strength).
    int16_t sy = baseline + 3;
    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.setCursor(plotX, sy);
    canvas.print("2400");
    canvas.setCursor(static_cast<int16_t>(plotX + plotW - 48), sy);
    canvas.print("2525 MHz");
    sy += LINE_HEIGHT - 2;
    canvas.setCursor(plotX, sy);
    canvas.print("occupancy > -64 dBm");
}

void RadioScreen::drawBandSweep(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 4;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(x, y);
    canvas.print("CC1101 RSSI sweep");
    y += LINE_HEIGHT;
    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.setCursor(x, y);
    canvas.print("Hold the remote's button");
    y += LINE_HEIGHT - 2;

    const int strongest = rf::bandSweepStrongest(bandSweep_);

    const int16_t plotX = x;
    const int16_t plotW = static_cast<int16_t>(canvas.width() - 2 * theme::PADDING_MD);
    const int16_t plotTop = y + 2;
    constexpr int16_t PLOT_H = 50;
    const int16_t baseline = plotTop + PLOT_H;
    const int16_t colW = plotW / PRESET_COUNT;
    constexpr int16_t BAR_INSET = 7;  // gap each side of a bar within its column

    canvas.fillRect(plotX, baseline, plotW, 1, theme::TEXT_DISABLED());  // baseline

    for (int b = 0; b < PRESET_COUNT; ++b) {
        const int16_t colX = static_cast<int16_t>(plotX + b * colW);
        const bool isStrongest = (b == strongest);
        const int16_t dbm = bandSweep_.peakDbm[b];

        // Bar height from the peak dBm, clamped to the display window. An unsampled
        // band (BAND_RSSI_NONE) draws no bar.
        int16_t barH = 0;
        if (dbm != rf::BAND_RSSI_NONE) {
            int16_t c = dbm;
            if (c < SWEEP_FLOOR_DBM) c = SWEEP_FLOOR_DBM;
            if (c > SWEEP_CEIL_DBM) c = SWEEP_CEIL_DBM;
            barH = static_cast<int16_t>((c - SWEEP_FLOOR_DBM) * PLOT_H /
                                        (SWEEP_CEIL_DBM - SWEEP_FLOOR_DBM));
        }

        const uint16_t accent = isStrongest ? theme::SUCCESS() : theme::TEXT_SECONDARY();
        if (barH > 0) {
            canvas.fillRect(static_cast<int16_t>(colX + BAR_INSET),
                            static_cast<int16_t>(baseline - barH),
                            static_cast<int16_t>(colW - 2 * BAR_INSET), barH, accent);
        }

        // Band (MHz, integer) and the peak dBm (or "--") beneath each bar.
        int16_t ly = baseline + 3;
        canvas.setTextColor(accent);
        canvas.setCursor(static_cast<int16_t>(colX + 2), ly);
        canvas.printf("%d", static_cast<int>(PRESETS[b].mhz));
        ly += LINE_HEIGHT - 2;
        canvas.setTextColor(theme::TEXT_DISABLED());
        canvas.setCursor(static_cast<int16_t>(colX + 2), ly);
        if (dbm == rf::BAND_RSSI_NONE) canvas.print("--");
        else canvas.printf("%ddBm", static_cast<int>(dbm));
    }
}

void RadioScreen::drawMain(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 6;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(x, y);
    canvas.printf("CC1101  %s", PRESETS[presetIndex_].label);
    y += LINE_HEIGHT + 2;

    for (int i = 0; i < MAIN_ITEM_COUNT; ++i) {
        if (i == mainSelection_) {
            canvas.fillRect(0, y - 2, canvas.width(), LINE_HEIGHT, theme::BG_SELECTED());
            canvas.setTextColor(theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(theme::TEXT_SECONDARY());
        }
        canvas.setCursor(x, y);
        canvas.print(MAIN_ITEMS[i]);
        y += LINE_HEIGHT;
    }
}

void RadioScreen::drawCapturing(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 10;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::SUCCESS());
    canvas.setCursor(x, y);
    canvas.printf("Listening  %s", PRESETS[presetIndex_].label);
    y += LINE_HEIGHT + 2;
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    canvas.print("Trigger the remote now...");
    y += LINE_HEIGHT;
    canvas.setCursor(x, y);
    canvas.printf("Window %lus",
                  static_cast<unsigned long>(CAPTURE_WINDOW_MS / 1000));
}

void RadioScreen::drawEncodingAndStrip(Canvas& canvas, const rf::OokSignal& sig,
                                       int16_t x, int16_t y, int16_t w) {
    constexpr int16_t PREVIEW_STRIP_H = 20;

    char summary[MESSAGE_BUF];
    describeSignal(sig, summary, sizeof(summary));
    canvas.setTextSize(1);
    // OOK stays raw (no protocol decode), so its summary reads as the dimmed
    // fallback encoding — same convention as a RAW IR code.
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    canvas.printf("%.30s", summary);

    if (!sig.durationsUs.empty()) {
        ui::renderPulseStrip(canvas, sig.durationsUs.data(), sig.durationsUs.size(),
                             sig.firstLevelHigh, x, y + 12, w, PREVIEW_STRIP_H);
    }
}

void RadioScreen::drawCaptureReview(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 6;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::SUCCESS());
    canvas.setCursor(x, y);
    canvas.print("Captured - review");
    y += LINE_HEIGHT + 2;
    drawEncodingAndStrip(canvas, pendingSignal_, x, y,
                         static_cast<int16_t>(canvas.width() - 2 * theme::PADDING_MD));
}

void RadioScreen::drawSignalList(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 4;
    canvas.setTextSize(1);
    if (signals_.empty()) {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(x, y);
        canvas.print("No saved signals");
        return;
    }
    int end = fileScroll_ + VISIBLE_ROWS;
    if (end > static_cast<int>(signals_.size())) end = static_cast<int>(signals_.size());
    for (int i = fileScroll_; i < end; ++i) {
        if (i == fileSelection_) {
            canvas.fillRect(0, y - 2, canvas.width(), LINE_HEIGHT, theme::BG_SELECTED());
            canvas.setTextColor(theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(theme::TEXT_SECONDARY());
        }
        canvas.setCursor(x, y);
        canvas.printf("%.26s", signals_[i].name);
        y += LINE_HEIGHT;
    }
}

void RadioScreen::drawSignalAction(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 6;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(x, y);
    canvas.printf("%.26s", signals_[fileSelection_].name);
    y += LINE_HEIGHT + 2;

    const char* items[2] = {"Replay", "Delete"};
    for (int i = 0; i < 2; ++i) {
        if (i == actionSelection_) {
            canvas.fillRect(0, y - 2, canvas.width(), LINE_HEIGHT, theme::BG_SELECTED());
            canvas.setTextColor(theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(i == 1 ? theme::WARNING() : theme::TEXT_SECONDARY());
        }
        canvas.setCursor(x, y);
        canvas.print(items[i]);
        y += LINE_HEIGHT;
    }

    // Encoding + preview of the selected signal, below the actions.
    if (actionSignalValid_) {
        drawEncodingAndStrip(canvas, actionSignal_, x, y + 2,
                             static_cast<int16_t>(canvas.width() - 2 * theme::PADDING_MD));
    } else {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(x, y + 2);
        canvas.print("Encoding unavailable");
    }
}

void RadioScreen::drawConfirm(Canvas& canvas, const char* question, uint16_t accent) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 10;
    canvas.setTextSize(1);
    canvas.setTextColor(accent);
    canvas.setCursor(x, y);
    canvas.print(question);
    y += LINE_HEIGHT + 2;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(x, y);
    canvas.printf("%.26s", signals_[fileSelection_].name);
    // The Yes/No/Cancel actions live in the footer hints (updateFooterHints).
}

void RadioScreen::drawJam(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 6;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::ACCENT());
    canvas.setCursor(x, y);
    canvas.printf("Jammer  %s", PRESETS[presetIndex_].label);
    y += LINE_HEIGHT + 2;

    if (state_ == MenuState::JAM_SELECT) {
        const char* modes[2] = {"Carrier (CW)", "Noise (modulated)"};
        for (int i = 0; i < 2; ++i) {
            if (i == jamModeSel_) {
                canvas.fillRect(0, y - 2, canvas.width(), LINE_HEIGHT, theme::BG_SELECTED());
                canvas.setTextColor(theme::TEXT_PRIMARY());
            } else {
                canvas.setTextColor(theme::TEXT_SECONDARY());
            }
            canvas.setCursor(x, y);
            canvas.print(modes[i]);
            y += LINE_HEIGHT;
        }
        return;
    }

    const char* modeName = (jamModeSel_ == 0) ? "Carrier (CW)" : "Noise";

    if (state_ == MenuState::JAM_CONFIRM) {
        canvas.setTextColor(theme::WARNING());
        canvas.setCursor(x, y);
        canvas.print("Transmit a JAM signal?");
        y += LINE_HEIGHT;
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(x, y);
        canvas.print("Illegal to operate on live");
        y += LINE_HEIGHT;
        canvas.setCursor(x, y);
        canvas.print("bands. Authorized tests only.");
        y += LINE_HEIGHT + 2;
        canvas.setTextColor(theme::TEXT_PRIMARY());
        canvas.setCursor(x, y);
        canvas.printf("%s @ %s", modeName, PRESETS[presetIndex_].label);
        return;
    }

    // JAM_ACTIVE
    if (jamEmitting_) {
        canvas.setTextColor(theme::ACCENT());
        canvas.setCursor(x, y);
        canvas.print(">> JAMMING <<");
    } else {
        canvas.setTextColor(theme::TEXT_SECONDARY());
        canvas.setCursor(x, y);
        canvas.print("Armed");
    }
    y += LINE_HEIGHT + 2;
    canvas.setTextColor(theme::TEXT_PRIMARY());
    canvas.setCursor(x, y);
    canvas.printf("%s @ %s", modeName, PRESETS[presetIndex_].label);
    y += LINE_HEIGHT;
    canvas.setTextColor(theme::TEXT_DISABLED());
    canvas.setCursor(x, y);
    canvas.print("Hold SPACE = jam");
    y += LINE_HEIGHT;
    canvas.setCursor(x, y);
    canvas.print("ESC = stop");
}

void RadioScreen::drawFreqSelect(Canvas& canvas) {
    int16_t x = theme::PADDING_MD;
    int16_t y = HEADER_HEIGHT + 6;
    canvas.setTextSize(1);
    canvas.setTextColor(theme::TEXT_SECONDARY());
    canvas.setCursor(x, y);
    canvas.print("Frequency preset");
    y += LINE_HEIGHT + 2;
    for (int i = 0; i < PRESET_COUNT; ++i) {
        if (i == presetIndex_) {
            canvas.fillRect(0, y - 2, canvas.width(), LINE_HEIGHT, theme::BG_SELECTED());
            canvas.setTextColor(theme::TEXT_PRIMARY());
        } else {
            canvas.setTextColor(theme::TEXT_SECONDARY());
        }
        canvas.setCursor(x, y);
        canvas.print(PRESETS[i].label);
        y += LINE_HEIGHT;
    }
}

} // namespace adversary
