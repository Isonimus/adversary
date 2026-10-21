/**
 * @file sniffer_actions.cpp
 * @brief Pure quick-action policy for the packet sniffer screen (see header).
 */

#include "sniffer_actions.h"
#include "screen_interface.h"  // ScreenId

#include <cstring>

namespace adversary {

bool makeSnifferAttackTarget(const PacketSummary& packet, PacketAction action,
                             AttackTargetEventData& out)
{
    ScreenId target;
    switch (action) {
        case PacketAction::HANDSHAKE_CAPTURE: target = ScreenId::HANDSHAKE_MANUAL; break;
        case PacketAction::DEAUTH_ATTACK:     target = ScreenId::DEAUTH;    break;
        case PacketAction::EVIL_TWIN:         target = ScreenId::EVIL_TWIN; break;
        case PacketAction::KARMA_ATTACK:      target = ScreenId::KARMA;     break;
        default:
            // CANCEL, or any action with no attack mapping: launch nothing.
            return false;
    }

    out.targetScreen = static_cast<int16_t>(target);
    memcpy(out.bssid, packet.srcMac, sizeof(out.bssid));
    strncpy(out.ssid, packet.ssid, sizeof(out.ssid) - 1);
    out.ssid[sizeof(out.ssid) - 1] = '\0';
    out.channel = packet.channel;
    return true;
}

size_t snifferLogPhysicalIndex(size_t head, size_t size, int displayIndex)
{
    // head is 0 until the ring wraps, so before the wrap this is the identity the screen used
    // historically; after the wrap it rotates the oldest slot (head) to display row 0.
    return (head + static_cast<size_t>(displayIndex)) % size;
}

} // namespace adversary
