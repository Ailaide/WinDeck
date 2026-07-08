# WinDeck

**A lightweight C++ app for the Steam Deck running Windows** — it revives the built-in controller and maps its inputs to a virtual **Xbox 360 gamepad**, **keyboard**, and **mouse**.

On Windows, the Deck's built-in **Neptune** controller (VID `0x28DE`, PID `0x1205`) has no native gamepad support: Windows sees a raw HID device, so games ignore it and the touchpads, gyroscope, and back paddles (L4/L5/R4/R5) go dead.

WinDeck reads every Neptune input over HID and re-exposes it — as an Xbox 360 gamepad via ViGEmBus, or as keyboard and mouse actions — with flexible remapping and a Steam-button configuration overlay. Being native C++, it stays light where the C#-based alternatives bog down the Deck's hardware.

> **Status:** in active development.

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

To **run** WinDeck you only need:

- A Steam Deck with Windows 10/11 (x64) installed.
- The [ViGEmBus](https://github.com/nefarius/ViGEmBus) driver — WinDeck detects it and offers to install it on first run, so there's nothing to set up by hand.

That's it. `windeck.exe` is a single, self-contained executable (statically linked) — no .NET, no Visual C++ redistributable, no administrator rights.

## Usage

1. Run `windeck.exe`. It reads the Neptune controller, creates the virtual Xbox 360 pad, and logs to the console and a log file. Only one instance can run at a time.
2. Play — games now see a standard Xbox 360 controller.
3. Press the **Steam button** to open the Quick Access Menu overlay and configure controls or switch profiles.
4. Press **Ctrl+C** (or close the console) to stop. WinDeck restores the controller's lizard-mode settings on exit.

## Configuration & profiles

Profiles are JSON files describing the full mapping and mode configuration. Example profiles ship in [`resources/profiles/`](resources/profiles/). Profiles can be created, switched, and deleted from the Quick Access Menu.

> **Note:** the profile JSON format is not yet stable and may change between versions without migration — recreate old profiles if they stop loading.

## Tech stack

- **C++20**, **CMake** 3.21+ with **vcpkg**
- [hidapi](https://github.com/libusb/hidapi) — Neptune HID input
- [ViGEmClient / ViGEmBus](https://github.com/nefarius/ViGEmClient) — virtual Xbox 360 device (vendored as a submodule)
- [HidHide](https://github.com/nefarius/HidHide) — hide the physical controller
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
| `src/driver/` | ViGEmBus auto-detect + install |
| `src/main.cpp` | Entry point, thread orchestration, input loop, shutdown/restore |

WinDeck runs a 3-thread model: a high-priority input loop, the main thread (message pump + overlays), and the ViGEm rumble callback.

## License

WinDeck is licensed under the **GNU General Public License v3.0** — see [`LICENSE`](LICENSE) for the full text.

In short: you're free to use, study, share, and modify WinDeck, but any distributed derivative must also be released under the GPLv3 with its source code available.
