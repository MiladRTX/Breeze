<p align="center">
  <img src="docs/logo/logo-icon.svg" alt="Breeze logo" width="120">
</p>

<h1 align="center">Breeze</h1>

<p align="center">
  <b>A fan curve for your PS5.</b><br>
  Decide how loud the fan gets at every temperature, right from your phone or PC.
</p>

<p align="center">
  <a href="https://github.com/MiladRTX/Breeze/releases/latest"><img src="https://img.shields.io/badge/Release-v1.6-8ea2ff?style=flat" alt="Release v1.6"/></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License GPLv3"/></a>
  <img src="https://img.shields.io/badge/Platform-PlayStation%205-003791?style=flat&logo=playstation" alt="PlayStation 5"/>
  <img src="https://img.shields.io/badge/C-00599C?style=flat&logo=c&logoColor=white" alt="C"/>
  <img src="https://img.shields.io/badge/Build-CMake-064F8C?style=flat&logo=cmake" alt="CMake"/>
  <a href="https://github.com/ps5-payload-dev/sdk"><img src="https://img.shields.io/badge/SDK-ps5--payload--sdk-555?style=flat" alt="ps5-payload-sdk"/></a>
</p>

<p align="center">
  <img src="docs/demo.gif" alt="Breeze web panel: live readouts, dragging the fan curve, presets and history" width="880">
</p>

<p align="center">
  <a href="https://github.com/MiladRTX/Breeze/releases/latest"><b>Download</b></a> ·
  <a href="#good-to-know">Good to know</a> ·
  <a href="#quick-start">Quick start</a> ·
  <a href="#the-panel">The panel</a> ·
  <a href="#how-it-works">How it works</a> ·
  <a href="#faq">FAQ</a>
</p>

---

## Why Breeze

<table>
  <tr>
    <td width="33%" valign="top"><b>Your curve, your noise</b><br>Up to 30 points of temperature → fan %. Quiet when you're browsing, cool when you're gaming.</td>
    <td width="33%" valign="top"><b>Edit from anywhere</b><br>A web panel on your LAN. Drag the curve on your phone or PC and hit Apply.</td>
    <td width="33%" valign="top"><b>Actually follows it</b><br>Reads the real fan speed every second and steers it to within 1–2% of your curve.</td>
  </tr>
  <tr>
    <td valign="top"><b>No sudden roars</b><br>Loading screens and sensor spikes are filtered, so the fan doesn't surge every scene change.</td>
    <td valign="top"><b>Presets</b><br>Save up to 10 curves, then switch, rename or overwrite them in a tap.</td>
    <td valign="top"><b>Safe by default</b><br>Stock fan until you turn the curve on, full cooling past 88 °C, and back to stock when you turn it off.</td>
  </tr>
</table>

## Good to know

- **The PS5 sets a minimum fan speed:** 13% in the menus, 23% while a game or app runs. Breeze can add fan on top of that but never go below it. The chart starts at 13% and shows the 23% line as a dashed band.
- **So in a game, the fan only rises once your curve asks for more than 23%.** With the default curve that's from about 60 °C. Until then it sits at 23%, which is not a bug.
- **Expect the fan within about 1–2% of your curve** once it settles. After a big change it takes 10–20 s to get there, because the console's temperature moves along with the fan. Gaps of 2% or less are left alone on purpose, so the fan doesn't keep shifting.
- **Dots are joined by straight lines,** and the fan follows the line between them. Steep sections make the fan jumpy during loading screens; add a dot where you want a gentler bend.
- **Past 88 °C Breeze always asks for full cooling,** whatever your curve says.
- **Stop Breeze by turning Custom curve off, or by rebooting.** Both put the fan back on stock right away. Killing it from Payload Manager's process list stops it too hard for that, and the fan keeps Breeze's last setting until you relaunch Breeze or reboot.

## Quick start

1. **Download** `breeze.elf` from the [latest release](https://github.com/MiladRTX/Breeze/releases/latest).
2. **Load it** with [Payload Manager](https://github.com/itsPLK/ps5-payload-manager): choose **Upload ELF Payload**, then launch it.
3. **Open the panel.** The console pops up the address:

   > Breeze running (stock fan)<br>
   > Turn the curve on at `http://<PS5-IP>:2878`

   On the first start Breeze also adds a **Breeze tile to your home screen**: open it and the panel comes up in the PS5 browser.
4. **Switch on Custom curve** (top right). Until you do, the fan stays fully stock. Breeze remembers the switch, so after that it runs your curve on every launch.

> [!TIP]
> Add Breeze to Payload Manager's **Autoload** list so your curve is back after every reboot. To update, just launch the new version: it replaces the running one by itself.

<details>
<summary><b>Using elfldr directly instead</b></summary>

<br>

Send the ELF to the loader on port 9021:

```sh
nc -q0 <PS5-IP> 9021 < breeze.elf
```

</details>

## The panel

| Part | What it does |
| --- | --- |
| **Readouts** | CPU temperature, fan speed, what your curve wants, and the target Breeze gives the PS5. Hover (or tap) any of them for a short explanation. |
| **Curve** | Drag points, double-click empty space to add one or a point to remove it, arrow keys to nudge. The dashed lines show where the temperature and fan are right now; the shaded band is the 23% in-game minimum. |
| **History** | 5 minutes to 3 hours of temperature and fan speed. Hover to see any moment. |
| **Presets** | Load, rename, overwrite or delete saved curves. The one running is marked **Active**. |
| **Points** | The same curve as a table, for exact values. |

Nothing changes until you press **Apply** (or <kbd>Ctrl</kbd> + <kbd>S</kbd>).

## How it works

The PS5 has no "set the fan to 40 %" switch. Its fan controller works like a **thermostat**: you give it a target temperature and it spins the fan to cool towards it.

Breeze closes the loop around that thermostat. Every second it:

1. reads the CPU temperature and the **real** fan speed,
2. looks up what your curve wants at that temperature,
3. nudges the target up or down, in fractions of a degree, until the fan matches.

Breeze makes small corrections and lets each one settle, ignores one-second spikes and dips, and backs off fast if the fan overshoots, so the fan stays within about 1–2% of your curve without jumping.

## FAQ

<details>
<summary><b>Is it safe for my console?</b></summary>

<br>

Breeze only changes the fan controller's target. Past 88 °C it always asks for full cooling, the console keeps its own minimum fan speed, and turning the curve off or closing Breeze normally hands the fan straight back to stock (see [Good to know](#good-to-know) for the one exception). Still, keep an eye on your temperatures, especially with very quiet curves.

</details>

<details>
<summary><b>Why isn't the fan exactly on my curve?</b></summary>

<br>

Breeze steers the PS5's thermostat, not the fan itself, so it gets the fan to within about 1–2% of the curve rather than exactly on it, and takes a few seconds after each change. It also can't go under the console's minimum (13% in the menus, 23% in a game). See [Good to know](#good-to-know).

</details>

<details>
<summary><b>Known limitations</b></summary>

<br>

Breeze is new and has been tested on one console, so expect some rough edges:

- **The fan readout may be a little off.** It comes from an undocumented system call and its scale is a best guess. Treat the % as a close estimate.
- **Short spikes can still happen** for a few seconds right as a game launches.
- **The history graph resets** when Breeze restarts.

Found something off? [Open an issue](https://github.com/MiladRTX/Breeze/issues) and attach `/data/breeze/breeze.log` if you can.

</details>

<details>
<summary><b>Where are my settings stored?</b></summary>

<br>

| Path | What |
| --- | --- |
| `/data/breeze/breeze.conf` | Curve, on/off, panel port, panel token |
| `/data/breeze/presets.conf` | Saved presets |
| `/data/breeze/breeze.log` | Controller log |

</details>

<details>
<summary><b>How do I build it myself?</b></summary>

<br>

Needs [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk), CMake and Ninja:

```sh
PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk ./build.sh   # -> build/breeze.elf
```

The panel is plain HTML in [`web/panel.html`](web/panel.html) and gets embedded into the ELF. Opened on its own, it runs with sample data, which is handy for working on the design.

</details>

## Credits

Made by **[Milad](https://github.com/MiladRTX)**.

Thanks to [John Törnblom](https://github.com/john-tornblom) for the [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk), and to [aydencharles](https://github.com/aydencharles) for [onionHEN](https://github.com/aydencharles/onionHEN), where the fan controller interface was first worked out.

## License

[GPL-3.0](LICENSE)

> [!WARNING]
> Breeze is an unofficial homebrew project and is not affiliated with or endorsed by Sony Interactive Entertainment. It changes how your console's fan behaves: use it only on hardware you own, keep an eye on temperatures, and use it at your own risk. No warranty is provided.
