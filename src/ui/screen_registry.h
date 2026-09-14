/**
 * @file screen_registry.h
 * @brief Central registration of every application screen's lazy factory.
 *
 * The screen→factory table used to live inline in main.cpp, which forced the
 * entry point to #include ~25 concrete screen headers it otherwise never names.
 * Moving the table here breaks that coupling: main.cpp includes only this header
 * (plus the handful of screens it genuinely downcasts) and calls
 * registerAllScreens() once at setup (slice-0023).
 */

#pragma once

namespace adversary {

class ScreenManager;

/**
 * @brief Register the lazy factory for every application screen.
 *
 * One explicit call site for the whole screen→factory table. Explicit (not
 * static-init self-registration) so a screen can never be silently dropped by
 * the linker's --gc-sections pass — a missing registration is a compile error
 * here, not an invisible dead menu entry on device (slice-0023).
 *
 * @param manager The ScreenManager to populate with factories.
 */
void registerAllScreens(ScreenManager& manager);

} // namespace adversary
