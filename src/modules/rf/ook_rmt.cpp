/**
 * @file ook_rmt.cpp
 * @brief RMT OOK capture/replay implementation (slice-0003). Firmware-only.
 */

#include "ook_rmt.h"

#include "../../config/pins.h"

#if defined(TARGET_CARDPUTER)
#include <Arduino.h>
#include <vector>

#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#endif

namespace adversary {
namespace rf {

#if defined(TARGET_CARDPUTER)

namespace {

// 1 tick = 1 us. Fine enough that the real pulse widths (deferred at freeze for
// lack of a 433 remote) never need to be known to size the resolution.
constexpr uint32_t RMT_RESOLUTION_HZ = 1000000;
// RMT's per-edge duration field is 15 bits, so one edge caps at 32767 ticks.
constexpr uint16_t RMT_MAX_DURATION_US = 32767;
// A hardware mem block; RX ping-pongs through it into our buffer, TX streams
// from it. 64 is the minimum the driver accepts.
constexpr size_t RMT_MEM_BLOCK_SYMBOLS = 64;

// Shortest edge RMT will register — rejects sub-microsecond line glitches while
// keeping the ~100 us+ edges of real OOK remotes.
constexpr uint32_t RX_MIN_PULSE_NS = 1000;
// Idle gap that ends a capture: once the line holds steady this long, the burst
// is complete. Below the 32.767 ms field ceiling; above a repeated remote's
// inter-frame gap so a full frame (with its natural repeats) is recorded.
constexpr uint32_t RX_IDLE_GAP_NS = 12000000;

// Fixed-code receivers debounce and need several frames to latch, so a single
// verbatim frame often does not actuate. Send the captured train a few times.
constexpr int REPLAY_REPEATS = 3;
constexpr uint32_t REPLAY_GAP_MS = 8;
constexpr uint32_t REPLAY_DONE_TIMEOUT_MS = 1000;

// One symbol carries two edges; bound the RX buffer to the model's edge cap.
constexpr size_t MAX_SYMBOLS = OOK_MAX_PULSES / 2;

// Level of edge @p index given the level the train started on. Edges strictly
// alternate, so even indices are the first level and odd ones its complement.
uint8_t edgeLevel(size_t index, bool firstLevelHigh) {
    bool high = (index % 2 == 0) ? firstLevelHigh : !firstLevelHigh;
    return high ? 1 : 0;
}

bool IRAM_ATTR onRxDone(rmt_channel_handle_t, const rmt_rx_done_event_data_t* edata,
                        void* userCtx) {
    QueueHandle_t queue = static_cast<QueueHandle_t>(userCtx);
    BaseType_t higherWoken = pdFALSE;
    xQueueSendFromISR(queue, edata, &higherWoken);
    return higherWoken == pdTRUE;
}

// Flatten RMT symbols into the alternating edge-duration list, dropping the
// terminating zero-duration field RMT appends.
void symbolsToSignal(const rmt_symbol_word_t* symbols, size_t count, OokSignal& out) {
    out.durationsUs.clear();
    out.firstLevelHigh = (symbols[0].level0 != 0);
    for (size_t i = 0; i < count; ++i) {
        if (symbols[i].duration0 == 0) break;
        out.durationsUs.push_back(symbols[i].duration0);
        if (symbols[i].duration1 == 0) break;
        out.durationsUs.push_back(symbols[i].duration1);
    }
}

}  // namespace

bool ookRmtCapture(uint32_t windowMs, OokSignal& out) {
    rmt_rx_channel_config_t rxConfig = {};
    rxConfig.gpio_num = static_cast<gpio_num_t>(pins::CC1101_GDO0);
    rxConfig.clk_src = RMT_CLK_SRC_DEFAULT;
    rxConfig.resolution_hz = RMT_RESOLUTION_HZ;
    rxConfig.mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS;

    rmt_channel_handle_t channel = nullptr;
    if (rmt_new_rx_channel(&rxConfig, &channel) != ESP_OK) {
        Serial.println("[OOK] rmt_new_rx_channel failed");
        return false;
    }

    QueueHandle_t doneQueue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    bool captured = false;
    if (doneQueue) {
        rmt_rx_event_callbacks_t callbacks = {};
        callbacks.on_recv_done = onRxDone;
        std::vector<rmt_symbol_word_t> buffer(MAX_SYMBOLS);
        rmt_receive_config_t receiveConfig = {};
        receiveConfig.signal_range_min_ns = RX_MIN_PULSE_NS;
        receiveConfig.signal_range_max_ns = RX_IDLE_GAP_NS;

        if (rmt_rx_register_event_callbacks(channel, &callbacks, doneQueue) == ESP_OK &&
            rmt_enable(channel) == ESP_OK &&
            rmt_receive(channel, buffer.data(),
                        buffer.size() * sizeof(rmt_symbol_word_t),
                        &receiveConfig) == ESP_OK) {
            rmt_rx_done_event_data_t event = {};
            if (xQueueReceive(doneQueue, &event, pdMS_TO_TICKS(windowMs)) == pdTRUE &&
                event.num_symbols > 0) {
                symbolsToSignal(event.received_symbols, event.num_symbols, out);
                captured = !out.durationsUs.empty();
            }
            rmt_disable(channel);
        } else {
            Serial.println("[OOK] RMT RX arm failed");
        }
        vQueueDelete(doneQueue);
    }

    rmt_del_channel(channel);
    return captured;
}

bool ookRmtReplay(const OokSignal& sig) {
    const size_t count = sig.durationsUs.size();
    if (count == 0 || count > OOK_MAX_PULSES) {
        Serial.println("[OOK] replay: empty or oversized signal");
        return false;
    }
    for (uint16_t duration : sig.durationsUs) {
        if (duration > RMT_MAX_DURATION_US) {
            Serial.printf("[OOK] replay: edge %u us exceeds RMT max %u us\n",
                          duration, RMT_MAX_DURATION_US);
            return false;
        }
    }

    // Pack the edge list into two-edge RMT symbols.
    std::vector<rmt_symbol_word_t> symbols((count + 1) / 2);
    for (size_t s = 0; s < symbols.size(); ++s) {
        const size_t i0 = 2 * s;
        const size_t i1 = i0 + 1;
        symbols[s].level0 = edgeLevel(i0, sig.firstLevelHigh);
        symbols[s].duration0 = sig.durationsUs[i0];
        if (i1 < count) {
            symbols[s].level1 = edgeLevel(i1, sig.firstLevelHigh);
            symbols[s].duration1 = sig.durationsUs[i1];
        } else {
            symbols[s].level1 = 0;  // carrier-off tail
            symbols[s].duration1 = 0;
        }
    }

    rmt_tx_channel_config_t txConfig = {};
    txConfig.gpio_num = static_cast<gpio_num_t>(pins::CC1101_GDO0);
    txConfig.clk_src = RMT_CLK_SRC_DEFAULT;
    txConfig.resolution_hz = RMT_RESOLUTION_HZ;
    txConfig.mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS;
    txConfig.trans_queue_depth = 4;

    rmt_channel_handle_t channel = nullptr;
    if (rmt_new_tx_channel(&txConfig, &channel) != ESP_OK) {
        Serial.println("[OOK] rmt_new_tx_channel failed");
        return false;
    }

    rmt_encoder_handle_t encoder = nullptr;
    rmt_copy_encoder_config_t encoderConfig = {};
    bool ok = false;
    if (rmt_new_copy_encoder(&encoderConfig, &encoder) == ESP_OK &&
        rmt_enable(channel) == ESP_OK) {
        rmt_transmit_config_t transmitConfig = {};
        transmitConfig.loop_count = 0;   // one frame per rmt_transmit; we loop in SW
        transmitConfig.flags.eot_level = 0;  // leave the data line low (carrier off)

        ok = true;
        for (int repeat = 0; repeat < REPLAY_REPEATS && ok; ++repeat) {
            if (rmt_transmit(channel, encoder, symbols.data(),
                             symbols.size() * sizeof(rmt_symbol_word_t),
                             &transmitConfig) != ESP_OK ||
                rmt_tx_wait_all_done(channel, REPLAY_DONE_TIMEOUT_MS) != ESP_OK) {
                Serial.println("[OOK] rmt_transmit failed");
                ok = false;
                break;
            }
            if (repeat + 1 < REPLAY_REPEATS) delay(REPLAY_GAP_MS);
        }
        rmt_disable(channel);
    } else {
        Serial.println("[OOK] RMT TX setup failed");
    }

    if (encoder) rmt_del_encoder(encoder);
    rmt_del_channel(channel);
    return ok;
}

#else  // !TARGET_CARDPUTER

bool ookRmtCapture(uint32_t, OokSignal&) { return false; }
bool ookRmtReplay(const OokSignal&) { return false; }

#endif  // TARGET_CARDPUTER

} // namespace rf
} // namespace adversary
