# Changelog

## Unreleased

**Curve**
- **Optional smooth curve:** the **Smooth** switch on the Curve card bends the curve through your dots instead of joining them with straight lines. It never overshoots: between two dots the fan stays between their values. Off by default, applied with Apply like any edit, and each preset keeps its own setting.

**Fan control**
- **"Stock" is now really stock.** Breeze used to hand the fan back at 77 °C, which isn't the console's own setting. It now learns the console's own target (91 °C on a PS5 Slim) and puts that back, only if the fan still holds Breeze's last setting, and never anything warmer than 91 °C. A value another fan tool keeps writing is not mistaken for it.
- **The fan goes back to stock even when Breeze is killed** from Payload Manager or crashes. A small helper, breeze-guard.elf, waits next to Breeze and restores the console's setting within about a second. Breeze restarts the helper within 10 seconds if it's stopped, and a new launch puts back a setting a stopped copy left behind. Before, the fan kept Breeze's last setting until a game was closed or started.
- **Exact console minimum:** Breeze reads all four of the console's fan channels, so "min" is exact, and the Fan readout says "console" when another part of the console is asking for more fan than your curve.
- Breeze reads the target back every second and only rewrites it when it changed.

**Panel**
- **History survives a restart:** it's saved every 5 minutes and when Breeze stops.
- **The target shows one decimal.**
- **Sturdier in the PS5 browser:** no web fonts to wait for, switches and keys work on older browser engines, the layout holds without flex gap, and a stuck request can no longer stop the live updates.
- **Fixed: the hover readout was 1% low on falling parts of the curve.**
- A diagnostics overlay for browser problems: add `?diag` to the address or click the Breeze name five times.

**Reliability**
- **breeze.log is capped at 256 KB;** the one before is kept as breeze.log.1, and each day starts with a date line.
- A new launch no longer loses the previous log while replacing the running copy.

## 1.6

- **New default curve** (the Breeze preset on a fresh install): 23% up to 60 °C, then 26% at 70 °C, 32% at 75 °C, 37% at 78 °C and 40% at 80 °C. Existing setups keep their own curve.
- **Tidier panel:** four readouts (CPU, Fan, Curve wants, Target) in one row, or 2 x 2 on a phone; the duplicate Restore button is gone (the Custom curve switch does the same); the hint under the chart is one line.
- **On a phone, the drag bubble sits above the point,** so the finger holding it doesn't cover the value.
- **The console-minimum label moved to the left of the chart,** out of the way of the points.
- **The switches stay greyed out until the console answers,** instead of briefly showing "off".

## 1.5

- **Much more accurate fan control.** Breeze now steers the PS5 in fractions of a degree instead of whole degrees. In game the fan stayed within 2% of the curve 98% of the time, against 46% before, with no jumps.
- **Double-click or double-tap a point to remove it,** just like double-clicking empty space adds one. Undo brings it back.
- **Clicking a point no longer nudges it:** a drag starts only after a few pixels, and the point keeps its offset under the pointer instead of jumping to it.

## 1.4

**First start**
- Breeze starts on stock fan control until you switch **Custom curve** on once, and remembers it on every launch after. The popup and a one-line hint in the panel say where.
- One starting preset, **Breeze**: the play-tested curve (23% up to 60 °C, rising gently to 34% at 81 °C), also the default curve. Existing curves and presets are kept.
- **Apply turns the curve on** if it was off.

**Fan control**
- **Fixed: fan stuck at 23% in game** until the curve was touched. Breeze now takes over as soon as the fan falls behind the curve, and a rounding bug that made it think the console was still heating up is gone.
- **Turning the curve off always hands the fan back to stock,** and stopping Breeze does that first, before anything slower.

**Panel**
- **Console minimum shown:** the chart starts at 13% (the menu minimum) with a dashed band at 23% (the minimum while a game runs), and the Fan readout says "min" when that's why the fan sits above your curve.
- **Hover tips** on the buttons.
- **Presets:** only one is marked Active, none while the curve is off, and saving under an existing name asks first.
- **Fixes:** the switches flip back if a request fails, <kbd>Ctrl</kbd> + <kbd>S</kbd> includes the value you were typing, quick swipes on a phone no longer add a point, edits made during Apply are kept, and Tab works again.

**Reliability**
- Settings are only replaced once fully written, and the panel says if saving failed.
- A half-sent or idle connection can't stall the panel.
- A new launch never signals the wrong process when replacing the running copy.

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
