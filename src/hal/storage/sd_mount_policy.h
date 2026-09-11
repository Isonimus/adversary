/**
 * @file sd_mount_policy.h
 * @brief Pure policy for whether SDManager::remount() should attempt a mount.
 *
 * Split out from the device-only SDManager so the one piece of decision logic in
 * an otherwise hardware-bound mount path can be unit-tested natively. The board
 * has no card-detect pin, so answering "is a card present?" costs the full FSPI
 * mount ladder (~5.6 s). Once a cardless boot is known, repeating that on every
 * shared-bus re-sync is pure waste — but an explicit operator retry must still be
 * able to pick up a card inserted after boot. Getting this truth table wrong
 * either reintroduces the triple-mount boot cost or blocks a legitimate retry
 * (slice-0020).
 */

#pragma once

namespace adversary {

/**
 * @brief Should remount() perform a real mount attempt?
 *
 * @param noCardVerdict A prior init() concluded no card is present this boot.
 * @param forceRetry    The caller is the operator's explicit "Retry SD mount".
 * @return true to attempt a real mount; false to short-circuit to the cached
 *         no-card verdict. Attempt unless a cached verdict says absent and this
 *         is not a forced retry.
 */
constexpr bool sdShouldAttemptRemount(bool noCardVerdict, bool forceRetry) {
    return !noCardVerdict || forceRetry;
}

}  // namespace adversary
