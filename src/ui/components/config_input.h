/**
 * @file config_input.h
 * @brief Pure config-form input classifier: one keypress + row kind -> canonical action.
 *
 * The input twin of ConfigRow (config_row.h). ADR-0092 converged every uniform attack/config
 * form onto one keyboard model; this encodes that model in one place as a pure function so the
 * seven forms stop hand-rolling seven copies of it (ADR-0107). It maps a keypress and the kind
 * of the currently selected row to the canonical ConfigKeyAction; the screen computes its row's
 * kind, performs the returned action (its own value rings, activate, navigate, popup), and lets
 * any key the classifier does not own fall through to its letter hotkeys / back / other states.
 *
 * Pure and total: no state, no model, no canvas, no Arduino. Host-testable by include alone,
 * the way configRowValueX and the ListScrollModel predicate are. It never touches the selection
 * model, so it cannot drift the snap contract (ADR-0104) or the band-only render contract
 * (ADR-0096).
 */

#ifndef ADVERSARY_CONFIG_INPUT_H
#define ADVERSARY_CONFIG_INPUT_H

namespace adversary {
namespace ui {

/// Kind of the currently selected config-form row. ENTER dispatches on this (ADR-0092):
/// a Value row advances its field, a Terminal row activates (the green START button), a
/// Navigation row routes to another screen (a Target picked on the WiFi scanner), a Popup
/// row opens its popup (e.g. a name/SSID entry).
enum class ConfigRowKind { Value, Terminal, Navigation, Popup };

/// Canonical action a keypress maps to. None means the classifier does not own this key, so
/// the caller must handle it (letter hotkeys, space, back/ESC) or ignore it.
enum class ConfigKeyAction { None, NavUp, NavDown, EditPrev, EditNext, Activate, Navigate, Popup };

/// Maps one keypress plus the selected row's kind to the canonical action (ADR-0092 / 0107):
///   ';' -> NavUp, '.' -> NavDown; ',' / '<' -> EditPrev, '/' / '>' -> EditNext;
///   ENTER ('\n' / '\r') -> EditNext (Value), Activate (Terminal), Navigate (Navigation),
///     Popup (Popup); any other key -> None.
inline ConfigKeyAction classifyConfigKey(char key, ConfigRowKind selectedKind) {
    switch (key) {
        case ';':
            return ConfigKeyAction::NavUp;
        case '.':
            return ConfigKeyAction::NavDown;
        case ',':
        case '<':
            return ConfigKeyAction::EditPrev;
        case '/':
        case '>':
            return ConfigKeyAction::EditNext;
        case '\n':
        case '\r':
            // ENTER is the one key whose meaning depends on the row kind (ADR-0092). A value
            // row advances (never inert); the others fire their single primary action.
            switch (selectedKind) {
                case ConfigRowKind::Value:
                    return ConfigKeyAction::EditNext;
                case ConfigRowKind::Terminal:
                    return ConfigKeyAction::Activate;
                case ConfigRowKind::Navigation:
                    return ConfigKeyAction::Navigate;
                case ConfigRowKind::Popup:
                    return ConfigKeyAction::Popup;
            }
            return ConfigKeyAction::None;  // unreachable: all kinds handled above
        default:
            return ConfigKeyAction::None;
    }
}

}  // namespace ui
}  // namespace adversary

#endif  // ADVERSARY_CONFIG_INPUT_H
