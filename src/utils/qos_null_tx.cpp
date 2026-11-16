/**
 * @file qos_null_tx.cpp
 * @brief ESP32-side burst TX for the capture-time QoS-Null stimulation knob
 *        (slice-0160, ADR-0159).
 *
 * Intentionally thin: for each frame in the burst, build a 26-byte QoS Null
 * with the native-tested pure builder, hand it to RawWiFi::transmit on the
 * STA interface, then yield. STA is the right interface because the handshake
 * capture flow operates in sniffer/STA mode; the AP interface is only brought
 * up for a decoy AP, which this slice does not do.
 *
 * The PWR_MGT bit alternates 1,0,1,0 across the burst so each pair encodes
 * one buffer-then-flush cycle at the AP. Sequence numbers come from a static
 * per-TX counter so every frame in the burst (and across bursts) looks like
 * a plausible contiguous client, not an obvious replay.
 */

#include "utils/qos_null_tx.h"

#ifdef ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "utils/raw_wifi.h"
#include "utils/log.h"
#endif

namespace adversary::utils::qos_null_tx {

#ifdef ESP32
namespace {
    /// Rolling 12-bit sequence counter shared across bursts: monotonic across the whole session.
    uint16_t s_seqCounter = 0;
}  // namespace
#endif

bool send_burst(const uint8_t ta[6],
                const uint8_t bssid[6],
                size_t count,
                uint32_t inter_frame_us) {
    if (ta == nullptr || bssid == nullptr) return false;
    if (count == 0) return false;

#ifdef ESP32
    bool allOk = true;
    for (size_t i = 0; i < count; ++i) {
        uint8_t frame[kQosNullFrameSize];
        const bool pwrMgt = ((i & 0x1U) == 0);  // Starts PwrMgt=1 so the AP buffers immediately.
        const uint16_t seq = s_seqCounter;
        s_seqCounter = static_cast<uint16_t>((s_seqCounter + 1) & 0x0FFF);

        const size_t len = build_qos_null_data(frame, sizeof(frame), ta, bssid, pwrMgt, seq);
        if (len == 0) {
            LOG_W("QoSNull", "build_qos_null_data refused params at i=%u", static_cast<unsigned>(i));
            allOk = false;
            continue;
        }

        const adversary::TxResult tx =
            adversary::RawWiFi::transmit(adversary::WiFiInterface::STATION, frame, len, false);
        if (!tx.success) {
            LOG_W("QoSNull", "QoS-Null TX failed at i=%u err=%d", static_cast<unsigned>(i), tx.errorCode);
            allOk = false;
        }

        if (inter_frame_us > 0 && (i + 1) < count) {
            const uint32_t ms = inter_frame_us / 1000U;
            vTaskDelay(pdMS_TO_TICKS(ms == 0 ? 1U : ms));
        }
    }
    return allOk;
#else
    (void)count;
    (void)inter_frame_us;
    return true;
#endif
}

}  // namespace adversary::utils::qos_null_tx
