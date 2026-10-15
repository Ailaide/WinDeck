# WinDeck

**A lightweight C++ app for the Steam Deck running Windows** — it revives the built-in controller and maps its inputs to a virtual **Xbox 360 gamepad**, **keyboard**, and **mouse**.

On Windows, the Deck's built-in **Neptune** controller (VID `0x28DE`, PID `0x1205`) has no native gamepad support: Windows sees a raw HID device, so games ignore it and the touchpads, gyroscope, and back paddles (L4/L5/R4/R5) go dead.

WinDeck reads every Neptune input over HID and re-exposes it — as an Xbox 360 gamepad via ViGEmBus, or as keyboard and mouse actions — with flexible remapping and a Steam-button configuration overlay. Being native C++, it stays light where the C#-based alternatives bog down the Deck's hardware.

> **Status:** early development. Expect rough edges and breaking changes, including to the profile format.

---

## What you get

- **Play any game with a controller Windows actually recognizes.** Every button, stick, and trigger just works, and in-game rumble comes through the Deck's motors.
- **Use the whole Deck, not just the face buttons.** The touchpads, gyroscope, and back paddles (L4/L5/R4/R5) become real, usable inputs instead of dead hardware.
- **Make any control do what you want.** Bind buttons, sticks, and pads to gamepad buttons, keyboard keys, or mouse actions — whatever the game or app expects.
- **Run the desktop and keyboard-only games too.** Move the cursor with a touchpad or by tilting the Deck, walk with WASD or arrow keys from the sticks and D-pad, and scroll with a touchpad.
- **Reach your shortcuts in a thumb-flick.** An on-screen radial menu puts eight actions one gesture away.
- **One setup per game.** Save profiles and switch between them in a tap.
- **Tweak without leaving the game.** The Steam button opens an on-screen menu to remap controls and change profiles on the fly.
- **Stays out of your way.** Runs quietly in the background, needs no administrator rights, and stays light on the Deck's hardware.

## Requirements

- A Steam Deck with Windows 10/11 (x64) installed.
- The [ViGEmBus](https://github.com/nefarius/ViGEmBus) driver. WinDeck detects it and offers to install it on first run: it downloads the official 1.22.0 installer, verifies its SHA-256, and asks for administrator approval (UAC) once.
- Steam must not be running while you use WinDeck (see [Known issues](#known-issues--troubleshooting)).

`windeck.exe` is a single, self-contained executable (statically linked) — no .NET, no Visual C++ redistributable, no administrator rights for normal use.

## Installation

1. Download the latest `WinDeck-<version>-win64.zip` from [Releases](https://github.com/Ailaide/WinDeck/releases).
2. Extract it to a folder you can write to, for example `C:\Games\WinDeck`. WinDeck keeps its profiles and its log file next to `windeck.exe`.
3. Run `windeck.exe`.

## Usage

1. Run `windeck.exe`. It reads the Neptune controller, creates the virtual Xbox 360 pad, and logs to its console window and to `windeck.log` next to the executable. Only one instance can run at a time.
2. Play — games now see a standard Xbox 360 controller.
3. Press the **Steam button** to open the Quick Access Menu overlay and configure controls or switch profiles.
4. Close the console window (or press **Ctrl+C** in it) to stop. WinDeck restores the controller's default (lizard mode) settings on exit.

## Configuration & profiles

Profiles are JSON files describing the full mapping and mode configuration. They live in the `profiles` folder next to `windeck.exe` and can be created, switched, and deleted from the Quick Access Menu.

- **Default** is built in and always available; it cannot be edited or overwritten.
- Example profiles ship in [`resources/profiles/`](resources/profiles/); copy one into your `profiles` folder to try it. The release archive already contains them in its `profiles` folder.

> **Note:** the profile JSON format is not yet stable and may change between versions without migration — recreate old profiles if they stop loading.

## Known issues & troubleshooting

- **No input while Steam is running.** Steam takes over the Deck controller and WinDeck receives nothing. Exit Steam completely (tray icon → Exit) before starting WinDeck.
- **Some games see two controllers.** Hiding the physical controller from other apps (HidHide integration) is not enabled yet, so games that read the Deck controller directly may see it alongside the virtual Xbox pad.
- **The touch keyboard does not open.** The Quick Access Menu opens the Windows touch keyboard through its taskbar button. Enable it in *Settings → Personalization → Taskbar → Touch keyboard → Always*.
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

WinDeck runs a 3-thread model: a high-priority input loop, the main thread (message pump + overlays), and the ViGEm rumble callback.

## Acknowledgements

- Neptune protocol details were cross-checked against the Linux kernel `hid-steam` driver, SDL's Steam Deck support, and neptune-hidapi.net.
- [ViGEm](https://github.com/nefarius/ViGEmBus) and [HidHide](https://github.com/nefarius/HidHide) by Benjamin Höglinger-Stelzer (Nefarius).

Steam and Steam Deck are trademarks of Valve Corporation. WinDeck is an independent project and is not affiliated with or endorsed by Valve.

## Contributing

Bug reports, ideas, and pull requests are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for how to build, test, and submit changes. Please report security problems privately as described in [SECURITY.md](SECURITY.md).

## License

WinDeck is licensed under the **GNU General Public License v3.0** — see [`LICENSE`](LICENSE) for the full text.

In short: you're free to use, study, share, and modify WinDeck, but any distributed derivative must also be released under the GPLv3 with its source code available.
