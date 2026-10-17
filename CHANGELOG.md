# Changelog

## Unreleased

- **First start:**
  - Breeze starts on stock fan control until you switch **Custom curve** on once; it remembers that on every launch after. The popup and a one-line hint in the panel say where.
  - One starting preset, **Breeze**: the play-tested quiet curve, which is also the default curve. Existing presets are kept.
- **Console minimum shown in the panel.** The PS5 won't run the fan below its own minimum: 13% in the menus, 23% while a game runs. Breeze measures that live, the curve chart shades it, and the Fan readout says "console min" when that's why the fan sits above your curve.
- **Fixed: fan stuck at the console's minimum** (e.g. 23%) after starting at idle, until the curve was touched. Breeze now pulls its target back next to the temperature as soon as the fan falls behind the curve.
- **Apply turns the custom curve on** if it was off, and a loaded preset can be applied straight away.
- **No preset is marked Active while the curve is off.**
- **A one-line hint** on the first visit says how to start.
- **The fan goes back to stock first** when Breeze is asked to stop, before the slower shutdown.
- **Hover tips** on the panel buttons.
- **Only one preset is marked Active,** even when two presets hold the same curve.
- **Saving under an existing name asks first** instead of silently replacing that preset.
- **Settings are only replaced once they're fully written,** and the panel tells you if saving on the console failed.
- **Turning the custom curve off always hands the fan back to stock.** Before, a target written at the same moment could stick.
- **Panel fixes:**
  - The switches wait for the first connection, and flip back if a request fails.
  - <kbd>Ctrl</kbd> + <kbd>S</kbd> includes the value you were typing.
  - Two quick swipes on a phone no longer add a point.
  - Edits made while Apply is in flight are kept.
  - Tab moves through the page again.
- **The panel can't be stalled** by a half-sent or idle connection.
- **A new launch never signals the wrong process** while taking over from the running copy.

## 1.3

- **Home screen tile.** Breeze adds a "Breeze" tile with the logo to the PS5 home screen; opening it brings up the panel in the browser. Breeze checks the tile on every start and reinstalls it only when it's missing or out of date (for example after changing the panel port).
- **Logo.** New wind-swirl logo, also used as the panel's browser tab icon.
- **Panel demo mode.** `web/panel.html` opened on its own (not served by Breeze) runs with built-in sample data, for previews and design work. It never activates on the console.

## 1.2

- **Rename presets** from the pencil button: Enter saves, Escape cancels, taken names are refused. Overwrite has its own save icon.
- **No sideways scrolling on phones:** the curve toolbar always fits on one row.

## 1.1

- **Explanations on the readouts:** hover or tap CPU, SoC, Fan, Curve wants or ICC target.
- **Cleaner point editor on phones.**

## 1.0

- First release: closed-loop fan curve, web panel with curve editor, presets and up to 3 hours of history, startup notification, and a new launch replacing the running copy.
