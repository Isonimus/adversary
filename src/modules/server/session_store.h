/**
 * @file session_store.h
 * @brief Pure, in-RAM dashboard session table (ADR-0170, slice-0171).
 *
 * Replaces the HTTP Basic-auth gate with a cookie-based session: the device
 * issues a 128-bit token on a successful login, keeps a small fixed-size table
 * of active sessions in RAM, and validates the cookie on every request. All
 * logic is pure over an injected @c Session table and @c nowMs so it is
 * native-testable; the device supplies @c esp_random() for token entropy and
 * @c millis() as the clock.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace adversary {

/// One active dashboard login. @c token is 32 hex chars + NUL (128 bits of
/// entropy from esp_random). An inactive slot has @c active == false.
struct Session {
    char token[33];
    uint32_t createdMs;   ///< When the session was issued (absolute TTL anchor).
    uint32_t lastSeenMs;  ///< Last validated-touch time (idle TTL + LRU anchor).
    bool active;
};

/**
 * @brief Extract the @c adv_session=<token> value from a Cookie header.
 *
 * @p cookieHeader is the raw value of the @c Cookie: request header
 * (e.g. @c "foo=bar; adv_session=abc123; x=y"). Writes the token into @p out
 * (NUL-terminated) when present and non-empty.
 *
 * @return true on success; false if @p cookieHeader is null, the cookie is
 *         absent, the value is empty, or @p out is null / @p outCap is 0.
 */
bool extractSessionToken(const char* cookieHeader, char* out, size_t outCap);

/**
 * @brief Install @p token into @p table.
 *
 * Picks a free (@c !active) slot if one exists, otherwise LRU-evicts the slot
 * with the smallest @c lastSeenMs. @c createdMs and @c lastSeenMs are set to
 * @p nowMs.
 *
 * @return true on success; false if inputs are null, @p n is 0, or @p token
 *         does not fit in @c Session::token.
 */
bool createSession(Session* table, size_t n, const char* token, uint32_t nowMs);

/**
 * @brief Validate @p token against @p table.
 *
 * Succeeds iff an active slot matches and both TTLs hold:
 *   - idle  : @c nowMs - lastSeenMs <= idleTtlMs
 *   - absolute: @c nowMs - createdMs <= absTtlMs
 * On success, @c lastSeenMs is touched to @p nowMs. On an expiry match, the
 * slot is deactivated (and the call returns false) so a replay cannot revive it.
 *
 * @return true on a live match; false otherwise.
 */
bool validateSession(Session* table,
                     size_t n,
                     const char* token,
                     uint32_t nowMs,
                     uint32_t idleTtlMs,
                     uint32_t absTtlMs);

/**
 * @brief Deactivate the slot matching @p token (logout).
 *
 * A no-op if no slot matches. Null inputs are safe.
 */
void clearSession(Session* table, size_t n, const char* token);

} // namespace adversary
