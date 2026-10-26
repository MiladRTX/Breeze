<p align="center">
  <img src="docs/logo/logo-icon.svg" alt="Breeze logo" width="120">
</p>

<h1 align="center">Breeze</h1>

<p align="center">
  <b>A fan curve for your PS5.</b><br>
  Decide how loud the fan gets at every temperature, right from your PS5, phone or PC.
</p>

<p align="center">
  <a href="https://github.com/MiladRTX/Breeze/releases/latest"><img src="https://img.shields.io/badge/Release-v1.9.1-8ea2ff?style=flat" alt="Release v1.9.1"/></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License GPLv3"/></a>
  <img src="https://img.shields.io/badge/Platform-PlayStation%205-003791?style=flat&logo=playstation" alt="PlayStation 5"/>
  <img src="https://img.shields.io/badge/C-00599C?style=flat&logo=c&logoColor=white" alt="C"/>
  <img src="https://img.shields.io/badge/Build-CMake-064F8C?style=flat&logo=cmake" alt="CMake"/>
  <a href="https://github.com/ps5-payload-dev/sdk"><img src="https://img.shields.io/badge/SDK-ps5--payload--sdk-555?style=flat" alt="ps5-payload-sdk"/></a>
</p>

<p align="center">
  <a href="https://recompstore.com/game/PPSA92878"><img src="https://recompstore.com/assets/press/badge-dark.svg" alt="Get it on RecompStore" height="56"></a>
</p>

<p align="center">
  <img src="docs/demo.gif" alt="Breeze web panel: live readouts, dragging the fan curve, presets and history" width="880">
</p>

<p align="center">
  <a href="https://github.com/MiladRTX/Breeze/releases/latest"><b>Download</b></a> ·
  <a href="#quick-start">Quick start</a> ·
  <a href="#the-app">The app</a> ·
  <a href="#good-to-know">Good to know</a> ·
  <a href="#the-panel">The panel</a> ·
  <a href="#how-it-works">How it works</a> ·
  <a href="#faq">FAQ</a>
</p>

---

## Why Breeze

<table>
  <tr>
    <td width="33%" valign="top"><b>Your curve, your noise</b><br>Up to 30 points of temperature → fan %. Quiet when you're browsing, cool when you're gaming.</td>
    <td width="33%" valign="top"><b>Edit from anywhere</b><br>The Breeze app on the PS5 itself, or a web panel on your LAN: drag the curve on your phone or PC and hit Apply.</td>
    <td width="33%" valign="top"><b>Actually follows it</b><br>Reads the real fan speed every second and steers it to within 1–2% of your curve.</td>
  </tr>
  <tr>
    <td valign="top"><b>No sudden roars</b><br>Loading screens and sensor spikes are filtered, so the fan doesn't surge every scene change.</td>
    <td valign="top"><b>Presets</b><br>Up to 10 curves. Apply saves your edits into the one in use, and the built-in one is always there to go back to.</td>
    <td valign="top"><b>Safe by default</b><br>Stock fan until you turn the curve on, full cooling past 88 °C, and back to stock when you turn it off or Breeze stops, even if it's killed.</td>
  </tr>
</table>

## Quick start

There are three ways to run Breeze. The app is the easiest: it shows your curve on the screen and starts Breeze for you.

### The app from a store (easiest)

1. **Install it:** open **[ProsperoStore](https://github.com/darkxex/ProsperoStore)** or **[RecompStore](https://recompstore.com/game/PPSA92878)** on your PS5, search for **Breeze** and install it. The store keeps it up to date too.
2. **Open Breeze** from the home screen. It starts Breeze for you.
3. **Switch on Custom curve** (top right). Until you do, the fan stays fully stock. Breeze remembers the switch, so after that it runs your curve on every launch.

The app needs [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) and an ELF loader on port 9021 (elfldr).

### The app by hand, with the zip

1. **Download** `PPSA92878.zip` from the [latest release](https://github.com/MiladRTX/Breeze/releases/latest).
2. **Unzip it.** You get a folder called `PPSA92878`.
3. **Copy the whole folder** to `/data/homebrew/` on the console, over FTP for example (ftpsrv listens on port 2121). You should end up with `/data/homebrew/PPSA92878/eboot.bin`.
4. **Wait a few seconds:** ShadowMountPlus finds the folder and adds **Breeze** to the home screen, under **Media**.
5. **Open Breeze** and switch on **Custom curve** (top right).

To update by hand, close the app and copy the new `PPSA92878` folder over the old one. Coming from a Breeze that sits under Games (1.9 and older)? Delete it first, then copy the new folder: the PS5 only moves an app to Media on a fresh install.

### Payload only

No app, just the payload and the web panel on your phone or PC.

1. **Download** `breeze.elf` from the [latest release](https://github.com/MiladRTX/Breeze/releases/latest).
2. **Load it** with [Payload Manager](https://github.com/itsPLK/ps5-payload-manager): choose **Upload ELF Payload**, then launch it.
3. **Open the panel.** The console pops up the address:

   > Breeze running (stock fan)<br>
   > Turn the curve on at `http://<PS5-IP>:2878`

   On the first start Breeze also adds a **Breeze tile to your home screen** (unless the [Breeze app](#the-app) is installed): open it and the panel comes up in the PS5 browser.
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

## The app

Breeze also comes as an app for the PS5 home screen: the same curve, history, presets and settings as the web panel, on the screen with the DualSense.

**Install it** from [ProsperoStore](https://github.com/darkxex/ProsperoStore), [RecompStore](https://recompstore.com/game/PPSA92878) or by hand with `PPSA92878.zip`: see [Quick start](#quick-start).

**It needs** [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) and an ELF loader on port 9021 (elfldr): the app carries its own `breeze.elf` and starts it when Breeze isn't running. Breeze keeps running after you close the app, and the app takes the place of the browser tile.

- **After a reboot,** open the app once to start Breeze again, or add `breeze.elf` to Payload Manager's **Autoload** list so it starts by itself.
- **After an update,** open the app once: if an older Breeze is running, the app replaces it with its own copy. The version at the top of the app is the running Breeze's.
- **It's a Media app,** so it stays open next to a game: check the fan mid-game and jump straight back. To start a *different* game while Breeze is open, close the current game first; picking the new one directly gives error CE-105773-3. Every Media homebrew app does this (Stremio too).

| Tab | Controls |
| --- | --- |
| **Curve** | ←→ choose a point, ↑↓ fan, L2/R2 temperature, slide on the touchpad to move the point, △ add, □ remove, L3/R3 undo and redo, ✕ apply, ○ revert |
| **History** | ←→ range (5 minutes to 24 hours), slide on the touchpad to look back |
| **Presets** | ↑↓ choose, ✕ use, △ new preset from the curve, □ delete (asks for a second press) |
| **Settings** | Custom curve on or off, reset to the built-in Breeze preset |

L1 and R1 switch tabs. While the app is open the PS5 treats it like a game, so the fan minimum is the in-game 23%.

## Good to know

- **The PS5 has a minimum fan speed:** 13% in the menus, 23% while a game or app runs. Breeze can add fan on top of that but never go below it, so in a game the fan only rises once your curve asks for more than 23% (from about 60 °C with the default curve).
- **The fan settles within about 1–2% of your curve,** 10–20 s after a big change.
- **Dots are joined by straight lines:** halfway between 60 °C at 30% and 70 °C at 40% is 35% at 65 °C.
- **Past 88 °C Breeze always asks for full cooling,** whatever your curve says.
- **Stopping Breeze hands the fan back to stock,** whether you turn Custom curve off, reboot, or kill it.

## The panel

| Part | What it does |
| --- | --- |
| **Readouts** | CPU and GPU temperature, fan speed, what your curve asks for, and the target Breeze gives the PS5. The curve follows the CPU; the GPU is shown only. Hover (or tap) any of them for a short explanation. |
| **Curve** | Drag points, double-click empty space to add one or a point to remove it, arrow keys to nudge. The dashed lines show where the temperature and fan are right now; the shaded band is the 23% in-game minimum. |
| **History** | 5 minutes to 24 hours of temperature and fan speed. Hover to see any moment. |
| **Presets** | Use, rename or delete saved curves, or start a new one from the curve in the editor. The one in use is marked **Active**: Apply saves your edits into it. The built-in **Breeze** preset never changes: edits of it are saved as a new preset, "My curve". |
| **Points** | The same curve as a table, for exact values. |

Nothing changes until you press **Apply** (or <kbd>Ctrl</kbd> + <kbd>S</kbd>).

## How it works

The PS5 has no "set the fan to 40 %" switch. Its fan controller works like a **thermostat**: you give it a target temperature and it spins the fan to cool towards it.

Breeze closes the loop around that thermostat. Every second it:

1. reads the CPU temperature and the **real** fan speed,
2. looks up what your curve wants at that temperature,
3. nudges the target up or down, in fractions of a degree, until the fan matches.

Breeze makes small corrections and lets each one settle, ignores one-second spikes and dips, and backs off fast if the fan overshoots, so the fan stays within about 1–2% of your curve without jumping.

When Breeze hands the fan back, it puts back the console's own target (91 °C on a PS5 Slim), which it learns from the console itself, and never anything warmer than 91 °C.

## FAQ

<details>
<summary><b>Is it safe for my console?</b></summary>

<br>

Breeze only changes the fan controller's target. Past 88 °C it always asks for full cooling, the console keeps its own minimum fan speed, and turning the curve off or stopping Breeze, even by killing it, hands the fan straight back to the console's own setting. Still, keep an eye on your temperatures, especially with very quiet curves.

</details>

<details>
<summary><b>Why isn't the fan exactly on my curve?</b></summary>

<br>

Breeze steers the PS5's thermostat, not the fan itself, so it gets the fan to within about 1–2% of the curve rather than exactly on it, and takes a few seconds after each change. Gaps of 2% or less are left alone on purpose, so the fan doesn't keep shifting. It also can't go under the console's minimum (13% in the menus, 23% in a game).

Steep sections of the curve make the fan jumpy during loading screens, so add a dot where you want a gentler bend.

</details>

<details>
<summary><b>Why does the Fan readout say "console"?</b></summary>

<br>

Other parts of the console can ask for more fan. The PS5 cools some parts on its own, and when one of them needs more than your curve, the Fan readout says **console**. That's the PS5 protecting itself.

</details>

<details>
<summary><b>What is breeze-guard.elf?</b></summary>

<br>

The small helper next to breeze.elf in the process list. If breeze.elf is killed (from Payload Manager's process list, for example), the guard puts the console's own fan setting back within about a second. Leave it running: Breeze restarts it within 10 seconds if it's stopped, and it exits by itself once the fan is back.

</details>

<details>
<summary><b>The fan seems stuck after Breeze stopped</b></summary>

<br>

Launch Breeze again (it puts back what a stopped copy left behind), start or close a game, or reboot.

</details>

<details>
<summary><b>Known limitations</b></summary>

<br>

Breeze is new and has been tested on one console (a PS5 Slim), so expect some rough edges:

- **Short spikes can still happen** for a few seconds right as a game launches.
- **History is saved every 5 minutes,** so if Breeze is killed the last few minutes of the graph are lost.

Found something off? [Open an issue](https://github.com/MiladRTX/Breeze/issues) and attach `/data/breeze/breeze.log` if you can.

</details>

<details>
<summary><b>Where are my settings stored?</b></summary>

<br>

| Path | What |
| --- | --- |
| `/data/breeze/breeze.conf` | Curve, on/off, panel port, panel token |
| `/data/breeze/presets.conf` | Saved presets |
| `/data/breeze/history.bin` | The last 24 hours of history, so the graph survives a restart |
| `/data/breeze/stock.conf` | The console's own fan target, learned by Breeze (delete it to relearn) |
| `/data/breeze/breeze.log` | Controller log, up to 256 KB; the one before is kept as `breeze.log.1` |

</details>

<details>
<summary><b>How do I build it myself?</b></summary>

<br>

Needs [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk), CMake and Ninja:

```sh
PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk ./build.sh   # -> build/breeze.elf
```

The panel is plain HTML in [`web/panel.html`](web/panel.html) and gets embedded into the ELF. Opened on its own, it runs with sample data, which is handy for working on the design.

The app builds in Linux or WSL with clang and lld 18, make and Python 3, after `build.sh`:

```sh
app/build.sh   # -> build/app/PPSA92878 and PPSA92878.zip
```

It fetches [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) at a pinned commit. `app/preview/run.sh` draws every screen as a PNG on a PC, without a console.

</details>

## Credits

Made by **[Milad](https://github.com/MiladRTX)**.

Thanks to [John Törnblom](https://github.com/john-tornblom) for the [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk), to [aydencharles](https://github.com/aydencharles) for [onionHEN](https://github.com/aydencharles/onionHEN), where the fan controller interface was first worked out, and to [BlackBearReloaded](https://github.com/blackbearreloaded) for [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate), which the app is built on. The app uses the [Inter](https://rsms.me/inter/) and [JetBrains Mono](https://www.jetbrains.com/lp/mono/) fonts (SIL Open Font License).

## License

[GPL-3.0](LICENSE)

> [!WARNING]
> Breeze is an unofficial homebrew project and is not affiliated with or endorsed by Sony Interactive Entertainment. It changes how your console's fan behaves: use it only on hardware you own, keep an eye on temperatures, and use it at your own risk. No warranty is provided.
