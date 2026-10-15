# Contributing to WinDeck

Thanks for your interest in improving WinDeck! Bug reports, fixes, and new features are all welcome. WinDeck is in early development, so for anything bigger than a small fix, please open an issue first to agree on the approach before you write a lot of code.

- **Bugs and feature ideas:** use the [issue templates](https://github.com/Ailaide/WinDeck/issues/new/choose).
- **Security problems:** do not open a public issue — see [SECURITY.md](SECURITY.md).

## Building

Follow [Building from source](README.md#building-from-source) in the README: Visual Studio 2022 or 2026 with the C++ workload, vcpkg with `VCPKG_ROOT` set, and a clone with submodules.

If more than one Visual Studio version is installed, vcpkg builds the dependencies with the newest one. Configure WinDeck with that same version too — run CMake from its *Developer PowerShell* — or the link step can fail with unresolved `__std_*` symbols from the newer standard library.

### Unit tests

The tests use [Catch2](https://github.com/catchorg/Catch2) and are off by default, so a normal build doesn't download it. Turn them on with `WINDECK_BUILD_TESTS`:

```bash
cmake --preset release -DWINDECK_BUILD_TESTS=ON
cmake --build --preset release
ctest --preset release
```

The option is stored in the build cache, so later builds keep it. The `debug` presets work the same way.

Tests live in [`tests/`](tests/) and link against `windeck_core`, the static library that holds the logic that needs no controller, driver, or overlay: profiles and their JSON format, the per-control mappers, the output modes, and `SyntheticInput`. When you change that logic, add or update tests; when you write new logic of that kind, put it in `windeck_core` so it can be tested.

## Testing on a Steam Deck

Unit tests cover the math and the profile format, not the device. Anything that touches the controller, ViGEmBus, the overlays, or real input needs to be tried on a **Steam Deck running Windows** — there is no emulator for the Neptune controller.

- **Exit Steam completely** (tray icon → Exit) before testing. While Steam runs, WinDeck receives no input.
- WinDeck changes controller firmware settings that survive a reboot. It restores them on exit; if a crash leaves the controller without its default mouse/keyboard emulation, run `restore_neptune.exe`.
- To copy every build to the Deck automatically, set `WINDECK_DEPLOY_DIR` (see the README).
- `windeck.log` next to `windeck.exe` has the details of every run.

Say in your pull request what you tested on the Deck and what you could not.

## Coding conventions

- **C++20**, built with MSVC. Keep it compiling with the presets in `CMakePresets.json`.
- **English only** — code, identifiers, comments, log messages, UI strings, and commit messages.
- **Match the surrounding style:** 4-space indentation, `PascalCase` types, `snake_case` functions and variables, a trailing underscore for members (`mode_`), `kName` for constants, and code inside `windeck::<module>` namespaces. Comments explain *why*, not *what*.
- **Make motion time-based, not frame-based.** Input arrives at roughly 250 Hz, but not exactly and not always. Anything that moves or changes over time — cursor speed, inertia, smoothing, repeats — must scale with the elapsed time from `TickTimer` (and `smooth_alpha` for exponential smoothing), never assume a fixed tick rate. The same physical motion must give the same result at any update rate.
- **Send all keyboard and mouse output through `SyntheticInput`.** Never call `SendInput` directly: `SyntheticInput` tracks key state, batches one `SendInput` per frame, and releases everything on profile switch and shutdown. Directional sources (stick, D-pad, touchpad in WASD/Arrows modes) declare keys with `key_hold()` so they can share keys without fighting.
- **Leave the controller as you found it.** Every firmware setting WinDeck changes must be restored on exit, including abnormal exits. Never hide devices or change system state automatically without a visible way to undo it.
- **No administrator rights for normal use.** WinDeck runs as a regular user; only the one-time ViGEmBus install asks for elevation.
- **No compatibility shims before 1.0.** The profile format may still change; rename or restructure fields directly instead of keeping legacy aliases or migration code.
- **Fix the root cause.** Avoid workarounds such as timing heuristics or retries that hide the real problem.

## Commit messages

Use a short area prefix and an imperative summary, as in the existing history:

```
input: fix WASD/Arrows key spam from multiple directional sources
driver: verify installer SHA-256 before the elevated launch
docs: add troubleshooting for the touch keyboard
```

Common prefixes are `input`, `mapper`, `modes`, `config`, `profiles`, `QAM` (Quick Access Menu), `overlay`, `driver`, `tools`, `build`, `ci`, `tests`, `docs`, and `chore`. Keep the summary under about 72 characters with no trailing period, and use the body to explain why the change is needed when it isn't obvious.

## Pull requests

- Branch from `main` and keep each pull request to one topic; small pull requests get reviewed faster.
- Make sure the CI build and tests pass (`ctest` locally).
- Describe what changed, why, and how you tested it — unit tests, on a Deck, or both.
- Update the README when you change user-visible behavior, and the example profiles in `resources/profiles/` when you change the profile format.
- Don't commit build output, logs, or personal settings (`CMakeUserPresets.json` is ignored for that reason).

WinDeck is licensed under the [GNU GPL v3](LICENSE). By submitting a pull request, you agree that your contribution is licensed under the same terms.
