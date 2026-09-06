# UI Style Guide — Screens

Distilled from the 26 existing `IScreen` implementations so new screens are cohesive
with the fleet instead of inventing a private dialect. This is a **reference**, not a
decision record — it documents the conventions the codebase already converged on and
declares a winner where it forked. Where an existing screen disagrees, the screen is
what changes (boyscout, opportunistically), not this doc.

Rule of thumb: **a new screen should be visually and behaviourally indistinguishable
from its neighbours except in the content area.** If you reach for a new constant,
colour, or key, check here first — the token almost always already exists.

---

## The `IScreen` contract

Every screen implements `adversary::IScreen` (`src/ui/screens/screen_interface.h`):
`show()` / `hide()` / `isVisible()`, `update()` (per-frame logic), `render(Canvas&)`,
`handleInput(char)`, `requestRedraw()`, `shouldExitToMenu()` / `resetExitFlag()`,
`getName()` / `getId()`, and `init()`. `SplashScreen` is the one deliberate non-member
(boot-only, no carousel).

---

## Layout

| Element | Token / value | Notes |
|---|---|---|
| Header band | `HEADER_HEIGHT = 20` | reserve the top 20 px for the status bar |
| Status bar | `ui::StatusBar::render(canvas, "TITLE", nullptr, theme::BG_SECONDARY(), theme::TEXT_PRIMARY())` | title is **UPPERCASE**; the bar auto-draws WiFi/battery/SD/GPS/heap badges |
| Footer band | `ui::FooterHints`, `FOOTER_HEIGHT = 16` | see [Footer](#footer) |
| Row height | **`theme::LIST_ITEM_HEIGHT` (20)** | the shared token (theme_manager.h). Do **not** invent `ROW_HEIGHT`/`LINE_HEIGHT` copies |
| Visible list rows | **5** | fits between header and footer on the 240×135 screen |
| Left margin | **`theme::PADDING_MD` (8)** | vertical gaps: the `PADDING_*` / `MARGIN_*` scale (XS=2 … XL=16) |

**Documented exception — dense consoles.** A screen that must show several stacked
lists on the 135 px-tall screen may use a tighter row than 20 px, *with a comment
stating why*. `RadioScreen` uses `LINE_HEIGHT = 14` for exactly this reason (a
title + multi-item menu + status must coexist). This is the CLAUDE.md "override with a
stated reason" escape hatch — not a licence to pick a random height. Everything else
(colours, keys, footer, redraw) still follows the standard.

---

## Colours — always `theme::` tokens, never raw `uint16_t`

Palette accessors live in `src/ui/theme_manager.h` (`namespace theme`): `BG_PRIMARY`,
`BG_SECONDARY`, `BG_TERTIARY`, `BG_SELECTED`, `ACCENT`, `TEXT_PRIMARY`,
`TEXT_SECONDARY`, `TEXT_DISABLED`, `SUCCESS`, `WARNING`, `ERROR`, `INFO`, plus the
`SIGNAL_*` set. They are theme-aware — a hardcoded `0xFFFF` breaks theming.

**Selected-row highlight (winner: `BG_SELECTED`).** Fill the selected row with
`theme::BG_SELECTED()` and draw its text in `theme::TEXT_PRIMARY()`; unselected rows
draw text in `theme::TEXT_SECONDARY()` (dimmer = clearer selection).

```cpp
if (i == selection_) {
    canvas.fillRect(0, y, canvas.width(), theme::LIST_ITEM_HEIGHT, theme::BG_SELECTED());
    canvas.setTextColor(theme::TEXT_PRIMARY());
} else {
    canvas.setTextColor(theme::TEXT_SECONDARY());
}
```

`ACCENT()` is for *active-state* emphasis (a running attack, a live value), not the
list cursor. Destructive items (Delete) and confirm prompts use `theme::WARNING()`.

---

## Input keys

The Cardputer has no arrow keys; these are the fleet-wide bindings.

| Key(s) | Action |
|---|---|
| `;` / `.` | up / down (list nav) |
| `\n` / `\r` | select / enter |
| `` ` `` **and** `0x1B` (ESC) | back one level / cancel; at the top level, exit to the carousel |
| `y` / `Y`, `n` / `N` | confirm yes / no |
| space | toggle focus to the footer (see below) |

Accept **both** `` ` `` and `0x1B` for back — normalise once at the top of
`handleInput` (`if (key == 0x1B) key = '`';`) so every back-handler catches ESC.

---

## Sub-level navigation — a state enum, never a view stack

There is no navigation/view stack in this framework. Multi-level screens hold an
`enum class …State` member and mutate it; `` ` `` pops one level by reassigning the
state, and only the top level sets the exit flag. This is the `CapturesScreen` idiom
followed by ~19 screens.

---

## Footer

Use the `ui::FooterHints` component (not an ad-hoc text string), and **re-set the hints
per state** so the operator always sees the live actions:

```cpp
footerHints_.setHints({{' ', "Focus"}, {';', "Nav"}, {'\n', "Select"}, {'`', "Back"}});
```

Route input through the component first; when the footer is unfocused it returns
`false` and the screen's own `;`/`.`/Enter run as list nav:

```cpp
char action = 0;
if (footerHints_.handleInputWithDispatch(key, action)) {
    if (action == 0) return true;   // focus toggle / footer navigation
    key = action;                   // fall through to the state switch with the chosen action
}
```

Disable an action that isn't currently valid (`{'\n', "Open", !list.empty()}`) rather
than hiding it. Don't duplicate a footer action as body text.

---

## Redraw model

Guard rendering on a dirty flag: `bool needsRedraw_` + `requestRedraw() { needsRedraw_
= true; }`, and early-out in `render()` (`if (!needsRedraw_) return; needsRedraw_ =
false;`). Gate both render and input on `bool visible_`, and wrap the drawing body in
`#ifdef ESP32`. A screen with live data (a running counter) may add a time throttle
(`REDRAW_INTERVAL_MS = 100`), as `CapturesScreen` does.

---

## Naming

- **Exit flag (winner): `shouldExit_`** + `shouldExitToMenu()` + `resetExitFlag()`.
  The legacy `m_shouldExit` Hungarian form is not used in new screens.
- **List state: `selection_` + `scrollOffset_`.** Only prefix (`fileSelection_`,
  `mainSelection_`) when a screen genuinely has more than one independent cursor.
- Members are `camelCase_` with a trailing underscore; no `m_` prefix in new screens.

---

## The capture → name → save → list → act pattern

`CapturesScreen` and `RadioScreen` already share this shape; a third consumer is what
prompted writing it down. Reuse it verbatim:

1. **List** — `SD.open(path)` + `openNextFile()`; skip directories; filter by extension
   with `strstr`; basename via `strrchr(name, '/')`; push a POD entry
   (`char name[N]; uint32_t size;`). Sort newest-first by write time when the file
   set is user-facing.
2. **Name** (capture flow only) — `TextInputPopup` (`show` / `setOnSubmit` /
   `setOnCancel`); sanitise the name (strip `/` `\`, bound the length, reject empty)
   before it becomes a path.
3. **Save** — `createDirectory` then `SDManager::writeFile`; fail loud on a write error
   (surface a status message, don't silently drop it).
4. **Act** — a two-item action sub-state (e.g. Replay / Delete). **Every destructive
   or emitting action goes behind a dedicated `…_CONFIRM` state** resolved by `Y`/`N`,
   with the prompt in `theme::WARNING()`. After a delete, reload the list and clamp
   `selection_` / `scrollOffset_` to the new size.

---

## Menus & submenus — wiring a screen in

Screens are reached through a two-level menu built from `MenuItem` factories
(`src/ui/components/menu.h`) in `main.cpp`. The carousel root holds one **category
submenu** per function area; each submenu holds the leaf actions:

```cpp
std::vector<MenuItem> irMenu = {
    MenuItem::action("TV-B-Gone", ACTION_IR_TVB_GONE, 't'), // leaf → navigates to a screen
    MenuItem::separator(),
    MenuItem::back()
};
rootMenu.push_back(MenuItem::submenu("Infrared", irMenu, 'i'));  // category → opens irMenu
```

Factories: `action(label, ACTION_ID, hotkey)`, `submenu(label, items, hotkey)`,
`disabled(label)`, `separator()`, `back()`.

To add a screen: (1) add a `ScreenId` (`screen_interface.h`); (2) `registerFactory` it in
`main.cpp`; (3) add a `MenuItem::action` **into the right existing category submenu**, with
an `ACTION_*` id; (4) map that `ACTION_*` to `navigateToScreen(ScreenId::…)` in the action
handler. A new feature in an existing area (a second IR tool, another BLE attack) is a
sibling entry in that submenu — **not** a new root category.

When hardware for a feature is absent, prefer `MenuItem::disabled("Feature (reason)")` over
hiding it — the greyed entry tells the operator the feature exists and why it's unavailable
(the RFID/HID submenus do this). This is the menu-level twin of a screen's "unavailable +
reason" state.

## New-screen checklist

- [ ] `StatusBar::render` with an UPPERCASE title; `HEADER_HEIGHT = 20`.
- [ ] `FooterHints` with per-state `setHints` + `handleInputWithDispatch`.
- [ ] `;`/`.`/`\n\r`/`` ` ``+ESC/`Y`/`N` bindings.
- [ ] `theme::` colours only; `BG_SELECTED` highlight; `WARNING` for destructive.
- [ ] `theme::LIST_ITEM_HEIGHT` rows, 5 visible (or a *commented* dense exception).
- [ ] `enum class …State` for sub-levels; `` ` `` pops one level.
- [ ] `needsRedraw_` / `requestRedraw()` / `visible_`; body under `#ifdef ESP32`.
- [ ] `shouldExit_` / `shouldExitToMenu()` / `resetExitFlag()`; `selection_` / `scrollOffset_`.
- [ ] Destructive/emitting actions gated behind a `Y`/`N` confirm state.
- [ ] Reached via a `MenuItem::action` in the right category submenu (ScreenId + factory + `ACTION_*` → `navigateToScreen`); `disabled(reason)` when its hardware is absent.

---

## Known divergences still to converge (boyscout, not a work item)

- `CapturesScreen` uses `ACCENT()` for its list highlight (should be `BG_SELECTED()`).
- ~11 older screens still use the `m_shouldExit` Hungarian exit member.
- Row heights vary (10/14/15/16/18) where no dense-console reason is documented.

Fix these when you're already in the file for another reason; don't open a dedicated
pass.
