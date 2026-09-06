/**
 * @file theme_manager.h
 * @brief Runtime theme management with preset and custom themes
 * 
 * Provides centralized color management with 6 preset themes:
 * - Red Team (default)
 * - Matrix (green monochrome)
 * - T-800 (Terminator red HUD)
 * - Fallout (Pip-Boy green)
 * - Cyberpunk (neon pink/blue)
 * - M5Stick (orange)
 */

#pragma once

#include <cstdint>
#include <cstring>

namespace adversary {

// =============================================================================
// Theme Presets
// =============================================================================

/**
 * @brief Available theme presets
 */
enum class ThemePreset : uint8_t {
    RED_TEAM = 0,    ///< Default red hacker theme
    MATRIX = 1,      ///< Green monochrome Matrix style
    T800 = 2,        ///< Terminator red HUD
    FALLOUT = 3,     ///< Pip-Boy green terminal
    CYBERPUNK = 4,   ///< Neon pink/blue synthwave
    M5STICK = 5,     ///< Orange matching M5Stick hardware
    CUSTOM = 6,      ///< User-defined (future)
    PRESET_COUNT = 7
};

/**
 * @brief Get human-readable theme name
 */
inline const char* getThemeName(ThemePreset preset) {
    switch (preset) {
        case ThemePreset::RED_TEAM:  return "Red Team";
        case ThemePreset::MATRIX:    return "Matrix";
        case ThemePreset::T800:      return "T-800";
        case ThemePreset::FALLOUT:   return "Fallout";
        case ThemePreset::CYBERPUNK: return "Cyberpunk";
        case ThemePreset::M5STICK:   return "M5Stick";
        case ThemePreset::CUSTOM:    return "Custom";
        default:                     return "Unknown";
    }
}

// =============================================================================
// Theme Colors Structure
// =============================================================================

/**
 * @brief Complete color palette for a theme
 */
struct ThemeColors {
    // Backgrounds
    uint16_t bgPrimary;      ///< Main background (usually black or dark)
    uint16_t bgSecondary;    ///< Secondary background (panels, headers)
    uint16_t bgTertiary;     ///< Tertiary background (hover states)
    uint16_t bgSelected;     ///< Selected item highlight
    
    // Accent colors (theme-defining)
    uint16_t accent;         ///< Primary accent color
    uint16_t accentLight;    ///< Lighter accent variant
    uint16_t accentDark;     ///< Darker accent variant
    
    // Text colors
    uint16_t textPrimary;    ///< Main text color
    uint16_t textSecondary;  ///< Secondary/muted text
    uint16_t textDisabled;   ///< Disabled/inactive text
    uint16_t textInverse;    ///< Text on light backgrounds
};

// =============================================================================
// RGB565 Helper
// =============================================================================

/**
 * @brief Convert RGB888 to RGB565
 */
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// =============================================================================
// Preset Theme Definitions
// =============================================================================

namespace presets {

// Red Team (Default) - Aggressive red hacker aesthetic
inline const ThemeColors RED_TEAM = {
    .bgPrimary    = 0x0000,              // Black
    .bgSecondary  = rgb565(0x18, 0x18, 0x18),  // Dark gray
    .bgTertiary   = rgb565(0x20, 0x20, 0x20),  // Lighter gray
    .bgSelected   = rgb565(0x30, 0x00, 0x00),  // Dark red selection
    .accent       = rgb565(0xFF, 0x00, 0x00),  // Pure red
    .accentLight  = rgb565(0xFF, 0x40, 0x40),  // Light red
    .accentDark   = rgb565(0x80, 0x00, 0x00),  // Dark red
    .textPrimary  = rgb565(0xFF, 0xFF, 0xFF),  // White
    .textSecondary = rgb565(0x7B, 0x7B, 0x7B), // Gray
    .textDisabled = rgb565(0x42, 0x42, 0x42),  // Dark gray
    .textInverse  = 0x0000                     // Black
};

// Matrix - Green monochrome digital rain
inline const ThemeColors MATRIX = {
    .bgPrimary    = 0x0000,              // Black
    .bgSecondary  = rgb565(0x00, 0x10, 0x00),  // Very dark green
    .bgTertiary   = rgb565(0x00, 0x1A, 0x00),  // Dark green
    .bgSelected   = rgb565(0x00, 0x30, 0x00),  // Selection green
    .accent       = rgb565(0x00, 0xFF, 0x41),  // Matrix green
    .accentLight  = rgb565(0x50, 0xFF, 0x70),  // Light green
    .accentDark   = rgb565(0x00, 0x80, 0x20),  // Dark green
    .textPrimary  = rgb565(0x00, 0xFF, 0x41),  // Matrix green
    .textSecondary = rgb565(0x00, 0xA0, 0x28), // Muted green
    .textDisabled = rgb565(0x00, 0x50, 0x14),  // Dark green
    .textInverse  = 0x0000                     // Black
};

// T-800 - Terminator red HUD targeting system
inline const ThemeColors T800 = {
    .bgPrimary    = rgb565(0x0A, 0x00, 0x00),  // Very dark red
    .bgSecondary  = rgb565(0x1A, 0x00, 0x00),  // Dark red
    .bgTertiary   = rgb565(0x28, 0x00, 0x00),  // Medium dark red
    .bgSelected   = rgb565(0x40, 0x00, 0x00),  // Selection red
    .accent       = rgb565(0xFF, 0x33, 0x33),  // Terminator red
    .accentLight  = rgb565(0xFF, 0x66, 0x66),  // Light red
    .accentDark   = rgb565(0x8B, 0x00, 0x00),  // Blood red
    .textPrimary  = rgb565(0xFF, 0xCC, 0xCC),  // Red-tinted white
    .textSecondary = rgb565(0xCC, 0x80, 0x80), // Muted red text
    .textDisabled = rgb565(0x66, 0x33, 0x33),  // Dark red text
    .textInverse  = 0x0000                     // Black
};

// Fallout - Pip-Boy green terminal
inline const ThemeColors FALLOUT = {
    .bgPrimary    = rgb565(0x00, 0x0A, 0x05),  // Very dark green (matches T-800 approach)
    .bgSecondary  = rgb565(0x00, 0x0A, 0x05),  // Very dark green
    .bgTertiary   = rgb565(0x10, 0x20, 0x10),  // Dark green
    .bgSelected   = rgb565(0x1B, 0x4D, 0x3E),  // Selection green
    .accent       = rgb565(0x50, 0xC8, 0x78),  // Pip-Boy green
    .accentLight  = rgb565(0x80, 0xE0, 0xA0),  // Light pip green
    .accentDark   = rgb565(0x30, 0x80, 0x50),  // Dark pip green
    .textPrimary  = rgb565(0x50, 0xC8, 0x78),  // Pip-Boy green
    .textSecondary = rgb565(0x40, 0x90, 0x60), // Muted green
    .textDisabled = rgb565(0x28, 0x50, 0x38),  // Dark green
    .textInverse  = rgb565(0x00, 0x0A, 0x05)                     // Black
};

// Cyberpunk - Neon synthwave aesthetic
// NOTE: the bg ramp is tuned to survive the 8-bit RGB332 canvas. RGB332 has
// only 4 blue levels (0/85/170/255) and 8 red levels, so the original very-dark
// purples (R<32, B<64) all quantized to pure black. These values land on the
// darkest *visible* RGB332 cells — (0,0,85) → (36,0,85) → (73,0,85) →
// (109,0,170) — a dark-indigo→purple ramp instead of black. Slightly bluer/
// brighter than the old near-black on the 16bpp path (splash), but coherent.
inline const ThemeColors CYBERPUNK = {
    .bgPrimary    = rgb565(0x08, 0x00, 0x50),  // Dark indigo   (RGB332 0,0,85)
    .bgSecondary  = rgb565(0x28, 0x00, 0x50),  // Dark purple   (RGB332 36,0,85)
    .bgTertiary   = rgb565(0x48, 0x00, 0x60),  // Purple        (RGB332 73,0,85)
    .bgSelected   = rgb565(0x6C, 0x08, 0xA0),  // Selection     (RGB332 109,0,170)
    .accent       = rgb565(0xFF, 0x14, 0x93),  // Neon pink
    .accentLight  = rgb565(0x00, 0xD9, 0xFF),  // Electric blue
    .accentDark   = rgb565(0xFF, 0x6B, 0x35),  // Neon orange
    .textPrimary  = rgb565(0xFF, 0xFF, 0xFF),  // White
    .textSecondary = rgb565(0xB0, 0xB0, 0xD0), // Light purple-gray
    .textDisabled = rgb565(0x60, 0x50, 0x80),  // Muted purple
    .textInverse  = 0x0000                     // Black
};

// M5Stick - Orange matching hardware
inline const ThemeColors M5STICK = {
    .bgPrimary    = 0x0000,              // Black
    .bgSecondary  = rgb565(0x18, 0x10, 0x00),  // Very dark orange
    .bgTertiary   = rgb565(0x28, 0x18, 0x00),  // Dark orange
    .bgSelected   = rgb565(0x40, 0x28, 0x00),  // Selection orange
    .accent       = rgb565(0xFF, 0x66, 0x00),  // M5 Orange
    .accentLight  = rgb565(0xFF, 0x99, 0x40),  // Light orange
    .accentDark   = rgb565(0xCC, 0x52, 0x00),  // Dark orange
    .textPrimary  = rgb565(0xFF, 0xE0, 0xCC),  // Orange-tinted white
    .textSecondary = rgb565(0xCC, 0xA0, 0x80), // Muted orange text
    .textDisabled = rgb565(0x66, 0x44, 0x30),  // Dark orange text
    .textInverse  = 0x0000                     // Black
};

// Custom (placeholder - will be user-defined)
inline const ThemeColors CUSTOM_DEFAULT = {
    .bgPrimary    = 0x0000,
    .bgSecondary  = rgb565(0x18, 0x18, 0x18),
    .bgTertiary   = rgb565(0x20, 0x20, 0x20),
    .bgSelected   = rgb565(0x30, 0x30, 0x50),
    .accent       = rgb565(0x00, 0x80, 0xFF),  // Blue default for custom
    .accentLight  = rgb565(0x40, 0xA0, 0xFF),
    .accentDark   = rgb565(0x00, 0x40, 0x80),
    .textPrimary  = rgb565(0xFF, 0xFF, 0xFF),
    .textSecondary = rgb565(0x7B, 0x7B, 0x7B),
    .textDisabled = rgb565(0x42, 0x42, 0x42),
    .textInverse  = 0x0000
};

} // namespace presets

// =============================================================================
// Status Colors (Constant across all themes)
// =============================================================================

namespace status {
    constexpr uint16_t SUCCESS = rgb565(0x00, 0xFF, 0x00);   // Green
    constexpr uint16_t WARNING = rgb565(0xFF, 0xA0, 0x00);   // Orange
    constexpr uint16_t ERROR   = rgb565(0xFF, 0x00, 0x00);   // Red
    constexpr uint16_t INFO    = rgb565(0x00, 0xFF, 0xFF);   // Cyan
    
    // Signal strength (also constant)
    constexpr uint16_t SIGNAL_EXCELLENT = rgb565(0x00, 0xFF, 0x00);  // Green
    constexpr uint16_t SIGNAL_GOOD      = rgb565(0x80, 0xFF, 0x00);  // Yellow-green
    constexpr uint16_t SIGNAL_FAIR      = rgb565(0xFF, 0xA0, 0x00);  // Orange
    constexpr uint16_t SIGNAL_WEAK      = rgb565(0xFF, 0x00, 0x00);  // Red
}

// =============================================================================
// ThemeManager Singleton
// =============================================================================

/**
 * @brief Centralized theme management
 * 
 * Singleton providing runtime theme switching with 6 presets
 * and future custom theme support.
 */
class ThemeManager {
public:
    static ThemeManager& getInstance() {
        static ThemeManager instance;
        return instance;
    }
    
    // Prevent copying
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;
    
    // -------------------------------------------------------------------------
    // Theme Selection
    // -------------------------------------------------------------------------
    
    /**
     * @brief Set active theme
     */
    void setTheme(ThemePreset preset) {
        if (preset == currentPreset_) return;
        currentPreset_ = preset;
        loadTheme(preset);
    }
    
    /**
     * @brief Get current theme preset
     */
    ThemePreset getTheme() const { return currentPreset_; }
    
    /**
     * @brief Get current theme name
     */
    const char* getThemeName() const { return adversary::getThemeName(currentPreset_); }
    
    /**
     * @brief Get total number of available presets (excluding custom if disabled)
     */
    uint8_t getPresetCount() const { return customEnabled_ ? 7 : 6; }
    
    /**
     * @brief Cycle to next theme (for live preview)
     */
    void nextTheme() {
        uint8_t next = (static_cast<uint8_t>(currentPreset_) + 1) % getPresetCount();
        setTheme(static_cast<ThemePreset>(next));
    }
    
    /**
     * @brief Cycle to previous theme
     */
    void prevTheme() {
        uint8_t count = getPresetCount();
        uint8_t prev = (static_cast<uint8_t>(currentPreset_) + count - 1) % count;
        setTheme(static_cast<ThemePreset>(prev));
    }
    
    // -------------------------------------------------------------------------
    // Color Accessors
    // -------------------------------------------------------------------------
    
    // Backgrounds
    uint16_t bgPrimary() const { return colors_.bgPrimary; }
    uint16_t bgSecondary() const { return colors_.bgSecondary; }
    uint16_t bgTertiary() const { return colors_.bgTertiary; }
    uint16_t bgSelected() const { return colors_.bgSelected; }
    
    // Accents
    uint16_t accent() const { return colors_.accent; }
    uint16_t accentLight() const { return colors_.accentLight; }
    uint16_t accentDark() const { return colors_.accentDark; }
    
    // Text
    uint16_t textPrimary() const { return colors_.textPrimary; }
    uint16_t textSecondary() const { return colors_.textSecondary; }
    uint16_t textDisabled() const { return colors_.textDisabled; }
    uint16_t textInverse() const { return colors_.textInverse; }
    
    // Status colors (constant across themes)
    uint16_t success() const { return status::SUCCESS; }
    uint16_t warning() const { return status::WARNING; }
    uint16_t error() const { return status::ERROR; }
    uint16_t info() const { return status::INFO; }
    
    // -------------------------------------------------------------------------
    // Custom Theme (Future)
    // -------------------------------------------------------------------------
    
    void setCustomEnabled(bool enabled) { customEnabled_ = enabled; }
    bool isCustomEnabled() const { return customEnabled_; }
    
    /**
     * @brief Set custom theme colors (for future use)
     */
    void setCustomColors(const ThemeColors& colors) {
        customColors_ = colors;
        if (currentPreset_ == ThemePreset::CUSTOM) {
            colors_ = customColors_;
        }
    }

private:
    ThemeManager() : currentPreset_(ThemePreset::RED_TEAM), customEnabled_(false) {
        colors_ = presets::RED_TEAM;
        customColors_ = presets::CUSTOM_DEFAULT;
    }
    
    void loadTheme(ThemePreset preset) {
        switch (preset) {
            case ThemePreset::RED_TEAM:  colors_ = presets::RED_TEAM; break;
            case ThemePreset::MATRIX:    colors_ = presets::MATRIX; break;
            case ThemePreset::T800:      colors_ = presets::T800; break;
            case ThemePreset::FALLOUT:   colors_ = presets::FALLOUT; break;
            case ThemePreset::CYBERPUNK: colors_ = presets::CYBERPUNK; break;
            case ThemePreset::M5STICK:   colors_ = presets::M5STICK; break;
            case ThemePreset::CUSTOM:    colors_ = customColors_; break;
            default:                     colors_ = presets::RED_TEAM; break;
        }
    }
    
    ThemeColors colors_;
    ThemeColors customColors_;
    ThemePreset currentPreset_;
    bool customEnabled_;
};

// =============================================================================
// Convenience Namespace (replaces old theme:: constants)
// =============================================================================

namespace theme {

// Background colors
inline uint16_t BG_PRIMARY() { return ThemeManager::getInstance().bgPrimary(); }
inline uint16_t BG_SECONDARY() { return ThemeManager::getInstance().bgSecondary(); }
inline uint16_t BG_TERTIARY() { return ThemeManager::getInstance().bgTertiary(); }
inline uint16_t BG_SELECTED() { return ThemeManager::getInstance().bgSelected(); }

// Accent colors
inline uint16_t ACCENT() { return ThemeManager::getInstance().accent(); }
inline uint16_t ACCENT_LIGHT() { return ThemeManager::getInstance().accentLight(); }
inline uint16_t ACCENT_DARK() { return ThemeManager::getInstance().accentDark(); }

// Text colors
inline uint16_t TEXT_PRIMARY() { return ThemeManager::getInstance().textPrimary(); }
inline uint16_t TEXT_SECONDARY() { return ThemeManager::getInstance().textSecondary(); }
inline uint16_t TEXT_DISABLED() { return ThemeManager::getInstance().textDisabled(); }
inline uint16_t TEXT_INVERSE() { return ThemeManager::getInstance().textInverse(); }

// Status colors (constant)
inline uint16_t SUCCESS() { return status::SUCCESS; }
inline uint16_t WARNING() { return status::WARNING; }
inline uint16_t ERROR() { return status::ERROR; }
inline uint16_t INFO() { return status::INFO; }

// Signal colors (constant)
inline uint16_t SIGNAL_EXCELLENT() { return status::SIGNAL_EXCELLENT; }
inline uint16_t SIGNAL_GOOD() { return status::SIGNAL_GOOD; }
inline uint16_t SIGNAL_FAIR() { return status::SIGNAL_FAIR; }
inline uint16_t SIGNAL_WEAK() { return status::SIGNAL_WEAK; }

// Typography (unchanged)
constexpr uint8_t FONT_SIZE_SMALL = 1;
constexpr uint8_t FONT_SIZE_NORMAL = 2;
constexpr uint8_t FONT_SIZE_LARGE = 3;
constexpr uint8_t FONT_SIZE_XLARGE = 4;

// Spacing (unchanged)
constexpr uint8_t PADDING_XS = 2;
constexpr uint8_t PADDING_SM = 4;
constexpr uint8_t PADDING_MD = 8;
constexpr uint8_t PADDING_LG = 12;
constexpr uint8_t PADDING_XL = 16;

constexpr uint8_t MARGIN_XS = 2;
constexpr uint8_t MARGIN_SM = 4;
constexpr uint8_t MARGIN_MD = 8;
constexpr uint8_t MARGIN_LG = 12;
constexpr uint8_t MARGIN_XL = 16;

// Component Dimensions (unchanged)
constexpr uint8_t LIST_ITEM_HEIGHT = 20;
constexpr uint8_t BUTTON_HEIGHT = 24;
constexpr uint8_t PROGRESS_BAR_HEIGHT = 8;
constexpr uint8_t STATUS_ICON_SIZE = 16;
constexpr uint8_t MENU_ITEM_HEIGHT = 22;

// Animation (unchanged)
constexpr uint32_t ANIMATION_FAST_MS = 100;
constexpr uint32_t ANIMATION_NORMAL_MS = 200;
constexpr uint32_t ANIMATION_SLOW_MS = 400;

// Helper functions
inline uint16_t getSignalColor(int8_t rssi) {
    if (rssi > -50) return SIGNAL_EXCELLENT();
    if (rssi > -60) return SIGNAL_GOOD();
    if (rssi > -70) return SIGNAL_FAIR();
    return SIGNAL_WEAK();
}

inline uint8_t getSignalBars(int8_t rssi) {
    if (rssi > -50) return 4;
    if (rssi > -60) return 3;
    if (rssi > -70) return 2;
    return 1;
}

} // namespace theme
} // namespace adversary
