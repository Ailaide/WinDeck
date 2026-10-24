# WinDeck — Steam Deck controller remapper for Windows

[![Build](https://github.com/Ailaide/WinDeck/actions/workflows/build.yml/badge.svg)](https://github.com/Ailaide/WinDeck/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/Ailaide/WinDeck)](https://github.com/Ailaide/WinDeck/releases/latest)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

**A native C++ controller remapper for the Steam Deck running Windows 10/11** — it maps the built-in controller to a virtual **Xbox 360 gamepad (XInput)**, **keyboard**, and **mouse**.

On Windows, the Deck's built-in **Neptune** controller (VID `0x28DE`, PID `0x1205`) has no native gamepad support: Windows sees a raw HID device, so games ignore it and the touchpads, gyroscope, and back paddles (L4/L5/R4/R5) go dead.

WinDeck reads Neptune inputs over HID and sends them through ViGEmBus or Windows keyboard and mouse input, with remapping, per-game profiles, and a Steam-button configuration overlay. It uses C++20 and does not require .NET.

> **Status:** early development. Expect rough edges and breaking changes, including to the profile format.

---

## What you get

- **Use a standard Xbox 360 controller in games that support XInput.** Buttons, sticks, and triggers map to the virtual controller, and in-game rumble comes through the Deck's motors.
- **Use the whole Deck, not just the face buttons.** The touchpads, gyroscope, and back paddles (L4/L5/R4/R5) become real, usable inputs instead of dead hardware.
- **Make any control do what you want.** Bind buttons, sticks, and pads to gamepad buttons, keyboard keys, or mouse actions — whatever the game or app expects.
- **Turn the triggers into buttons.** Each trigger can stay an analog Xbox trigger or fire up to three bindings at their own pull depths — for example aim at a light pull and fire at a full pull — with a click from the motor on that side.
- **Run the desktop and keyboard-only games too.** Move the cursor with a touchpad or by tilting the Deck, walk with WASD or arrow keys from the sticks and D-pad, and scroll with a touchpad.
- **Reach your shortcuts in a thumb-flick.** An on-screen radial menu puts eight actions one gesture away.
- **One setup per game.** Save profiles and switch between them in a tap.
- **Tweak without leaving the game.** The Steam button opens an on-screen menu to remap controls and change profiles on the fly.
- **Type without a physical keyboard.** The Quick Access button shows or hides the Windows touch keyboard.
- **No administrator rights for normal use.** Keep WinDeck running while you play; its console window can be minimized.

## Requirements

- A Steam Deck with Windows 10/11 (x64) installed.
- The [ViGEmBus](https://github.com/nefarius/ViGEmBus) driver. WinDeck detects it and offers to install it on first run: it downloads the official 1.22.0 installer, verifies its SHA-256, and asks for administrator approval (UAC) once.
- Steam must not be running while you use WinDeck (see [Known issues](#known-issues--troubleshooting)).

`windeck.exe` is a single, self-contained executable (statically linked) — no .NET, no Visual C++ redistributable, no administrator rights for normal use.

## Installation

1. Download the latest `WinDeck-<version>-win64.zip` from [Releases](https://github.com/Ailaide/WinDeck/releases).
2. Extract it to a folder you can write to, for example `C:\Games\WinDeck`. WinDeck keeps its profiles and its log file next to `windeck.exe`.
3. Exit Steam completely, including its tray icon.
4. Run `windeck.exe` and accept the one-time ViGEmBus installation prompt if needed.

Each release includes `SHA256SUMS.txt` with hashes for the archive and both executables. To check a download, run `Get-FileHash .\WinDeck-<version>-win64.zip -Algorithm SHA256` in PowerShell (replace `<version>` with the downloaded version) and compare its hash with the matching line in `SHA256SUMS.txt`. You can check the extracted executables the same way.

## Usage

1. Run `windeck.exe`. It reads the Neptune controller, creates the virtual Xbox 360 pad, and logs to its console window and to `windeck.log` next to the executable. Only one instance can run at a time.
2. Play — games now see a standard Xbox 360 controller.
3. Press the **Steam button** to open the Quick Access Menu overlay and configure controls or switch profiles.
4. Press the **Quick Access button** (**…**) to show or hide the Windows touch keyboard.
5. Close the console window (or press **Ctrl+C** in it) to stop. WinDeck restores the controller's default (lizard mode) settings on exit.

## Configuration & profiles

Profiles are JSON files describing the full mapping and mode configuration. They live in the `profiles` folder next to `windeck.exe` and can be created, switched, and deleted from the Quick Access Menu.

- **Default** is built in and always available; it cannot be edited or overwritten.
- Select **New** in the profile bar to create an editable profile from Default. Changes are saved when you close the menu or switch profiles, and the last active profile is restored on the next run.
- **Triggers:** select **LT** or **RT** in the menu's top row. **A** switches between the analog Xbox trigger and Buttons; **X** opens its settings. In Buttons mode each of bindings A, B and C has an action (key, mouse button, wheel tick or Xbox button), a pull depth from 10% to 100%, a mode — **Hold** presses at that depth and releases 5 percentage points below it, **Immediate** taps for 50 ms — and a haptic click (None, Low or High). Wheel bindings send one tick on activation. After a profile change or closing the menu, a trigger in Buttons mode that is still pulled does nothing until you let it go.
- Example profiles ship in [`resources/profiles/`](resources/profiles/); copy one into your `profiles` folder to try it. The release archive already contains them in its `profiles` folder.

> **Note:** the profile JSON format is not yet stable and may change between versions without migration — recreate old profiles if they stop loading.

Version 0.3.0 replaced the old trigger settings with `mode`, `deadzone`, and bindings `a`, `b`, and `c`. Triggers in older profiles fall back to analog Xbox mode with a 2% dead zone; configure them again in the menu. Other settings in those profiles still load.

### Quick Access Menu controls

| Control | Action |
|---|---|
| Steam button | Open or close the menu |
| D-pad | Navigate tiles, settings, and binding choices |
| A | Activate the selected item, change a mode, or choose a binding |
| X | Open the selected control's settings, when available |
| B | Go back or close the menu |
| LB / RB in the binding picker | Switch between keyboard, mouse, and Xbox choices |
| Y in the binding picker | Clear the selected binding |

Game input is blocked while the menu is open. Default is read-only: create or select an editable profile before changing controls.

## Known issues & troubleshooting

- **No input while Steam is running.** Steam takes over the Deck controller and WinDeck receives nothing. Exit Steam completely (tray icon → Exit) before starting WinDeck.
- **Some games see two controllers.** Hiding the physical controller from other apps (HidHide integration) is not enabled yet, so games that read the Deck controller directly may see it alongside the virtual Xbox pad.
- **A console window remains open.** WinDeck currently uses the console for status and shutdown; there is no system tray icon or automatic startup option yet.
- **The touch keyboard does not appear.** WinDeck shows and hides it through the Windows touch keyboard host (`TabTip.exe`), which Windows starts with the *Text Input Management Service*. If it does not appear, look for a touch keyboard warning in `windeck.log`.
- **The controller has no mouse/keyboard emulation after WinDeck crashed.** Firmware settings persist until they are restored. Run `restore_neptune.exe` (included in the release and built with the project) to reset the controller to its defaults.
- **Recovery scripts** in [`tools/`](tools/):
  - `unhide_neptune.bat` / `unhide_neptune.ps1`, `hidhide_reset.bat` — make the controller visible again if HidHide (installed by WinDeck or another tool) is hiding it.
  - `uninstall_vigembus.bat` — remove the ViGEmBus driver.

## Building from source

**Prerequisites**

- Windows 10/11 x64.
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload (includes CMake).
- [vcpkg](https://github.com/microsoft/vcpkg) with the `VCPKG_ROOT` environment variable pointing to it. Visual Studio 2022 ships vcpkg, and the *Developer PowerShell for VS 2022* sets `VCPKG_ROOT` for you; a standalone vcpkg clone works too.

**Build**

```bash
git clone --recurse-submodules https://github.com/Ailaide/WinDeck.git
cd WinDeck
cmake --preset release
cmake --build --preset release
```

The first configure installs the dependencies (hidapi, spdlog, nlohmann-json) through vcpkg manifest mode and may take a few minutes. The results are `build/release/Release/windeck.exe` and `build/release/Release/restore_neptune.exe`. Use the `debug` presets for a debug build.

If you cloned without `--recurse-submodules`, run `git submodule update --init` before configuring.

**Testing on a Deck.** WinDeck needs the real Neptune controller, so test builds have to run on a Steam Deck. To copy every build to the Deck automatically (for example through a shared folder), set the `WINDECK_DEPLOY_DIR` cache variable — for instance in a personal, untracked `CMakeUserPresets.json`:

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "release-deploy",
      "inherits": "release",
      "cacheVariables": { "WINDECK_DEPLOY_DIR": "//STEAMDECK/Shared" }
    }
  ]
}
```

To build and run the unit tests, see [CONTRIBUTING.md](CONTRIBUTING.md#unit-tests).

## Tech stack

- **C++20**, **CMake** 3.21+ with **vcpkg**
- [hidapi](https://github.com/libusb/hidapi) — Neptune HID input
- [ViGEmClient / ViGEmBus](https://github.com/nefarius/ViGEmClient) — virtual Xbox 360 device (client vendored as a submodule). The upstream repositories were archived in 2023; ViGEmBus 1.22.0 is the final release and works on Windows 10/11.
- [HidHide](https://github.com/nefarius/HidHide) — hiding the physical controller (integration present, currently disabled)
- [nlohmann::json](https://github.com/nlohmann/json) — config & profiles
- [spdlog](https://github.com/gabime/spdlog) — logging
- **Direct2D + DirectWrite** — overlays (radial menu, Quick Access Menu)

## Architecture

| Module | Responsibility |
|---|---|
| `src/neptune/` | HID read at ~250 Hz, report parsing, lizard-mode disable/restore, haptics/rumble |
| `src/output/` | `VirtualXbox` (ViGEm + rumble callback) and `SyntheticInput` (unified `SendInput`) |
| `src/mapper/` | `Mapper` + sub-mappers: Button, Stick, Trigger, Dpad, Touchpad, Gyro |
| `src/config/` | `Profile` struct, JSON (de)serialization, `Config` manager (`shared_mutex`) |
| `src/modes/` | Reusable output modes: DirectionalMode, MouseMode, Inertia, RadialMenuMode, TickTimer |
| `src/overlay/` | `OverlayBase` (shared D2D), `RadialOverlay`, `QuickAccessOverlay` |
| `src/hidhide/` | HidHide client (hide all Neptune interfaces) |
| `src/driver/` | ViGEmBus auto-detect + verified installer download |
| `src/main.cpp` | Entry point, thread orchestration, input loop, shutdown/restore |
| `tools/` | `restore_neptune` recovery tool and recovery scripts |

The high-priority input loop reads the controller and updates outputs. Overlays run their own window threads, touch keyboard requests use a dedicated worker, and ViGEm handles rumble callbacks. Profile saves can run in a background thread; the main thread waits for shutdown and joins the input loop.

## Acknowledgements

- Neptune protocol details were cross-checked against the Linux kernel `hid-steam` driver, SDL's Steam Deck support, and neptune-hidapi.net.
- [ViGEm](https://github.com/nefarius/ViGEmBus) and [HidHide](https://github.com/nefarius/HidHide) by Benjamin Höglinger-Stelzer (Nefarius).

Steam and Steam Deck are trademarks of Valve Corporation. WinDeck is an independent project and is not affiliated with or endorsed by Valve.

## Contributing

Bug reports, ideas, and pull requests are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for how to build, test, and submit changes. Please report security problems privately as described in [SECURITY.md](SECURITY.md).

## License

WinDeck is licensed under the **GNU General Public License v3.0** — see [`LICENSE`](LICENSE) for the full text.

In short: you're free to use, study, share, and modify WinDeck, but any distributed derivative must also be released under the GPLv3 with its source code available.
