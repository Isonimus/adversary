/**
 * @file attack_type_id.h
 * @brief Type-safe identifiers for attack modules
 * 
 * Used in EventBus payloads (AttackStateEventData.attackType) to identify
 * which module published the event. uint8_t-backed for struct compatibility.
 */

#pragma once

#include <cstdint>

namespace adversary {

enum class AttackTypeId : uint8_t {
    DEAUTH         = 0,
    BEACON_SPAM    = 1,
    PROBE_FLOOD    = 2,
    HANDSHAKE      = 3,
    DEAUTH_PACKET  = 6,   // Per-packet events (DEAUTH_PACKET_SENT)
};

} // namespace adversary
