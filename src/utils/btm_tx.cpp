/**
 * @file btm_tx.cpp
 * @brief ESP32-side one-shot TX for the autohunt BTM steering knob (slice-0155).
 *
 * Intentionally thin: build the 46-byte frame with the native-tested pure
 * builder, hand it to RawWiFi::transmit on the STA interface. STA is the
 * right interface because autohunt operates in sniffer/STA mode; the AP
 * interface is only brought up when we stand up a decoy AP, which this
 * slice does not do (the candidate BSSID is a fabricated LAA MAC, not a
 * real decoy; see slice-0155 Design).
 */

#include "utils/btm_tx.h"

#ifdef ESP32
#include "utils/raw_wifi.h"
#include "utils/log.h"
#endif

namespace adversary::utils {

bool sendBtmRequest(const BtmRequestParams& p) {
#ifdef ESP32
    uint8_t frame[kBtmRequestFrameSize];
    const size_t len = buildBtmRequestFrame(frame, sizeof(frame), p);
    if (len == 0) {
        LOG_W("BTM", "buildBtmRequestFrame refused params");
        return false;
    }

    const TxResult tx = RawWiFi::transmit(WiFiInterface::STATION, frame, len, false);
    if (!tx.success) {
        LOG_W("BTM", "BTM Request TX failed, err=%d", tx.errorCode);
    }
    return tx.success;
#else
    (void)p;
    return false;
#endif
}

}  // namespace adversary::utils
