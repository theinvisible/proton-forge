# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

ProtonForge is a Qt6/C++17 desktop GUI for Linux that lets Steam gamers configure NVIDIA DLSS, HDR, Proton tweaks, and per-game launch options, then either launch games directly or write the options back into Steam. It manipulates Steam config files and `compatibilitytools.d`; it is not a library.

## Build, run, package

```bash
# Debug build (used for development)
mkdir -p cmake-build-debug && cd cmake-build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . -j$(nproc)
./ProtonForge            # run from the build dir

# Release build
mkdir -p cmake-build-release && cd cmake-build-release
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)

# .deb package — builds from source itself; see the note below
bash build-deb.sh
```

```bash
# Tests. See TESTS.md for the whole picture.
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug -DPROTONFORGE_BUILD_TESTS=ON
cmake --build cmake-build-debug -j$(nproc)
ctest --test-dir cmake-build-debug --output-on-failure   # unit tests, ~1s

tests/steam-lab/steamlab preflight    # what is installed, what is missing
tests/steam-lab/steamlab test         # integration tests
```

Requires Qt6 (`Core Widgets Network Concurrent DBus`), CMake 3.16+, GCC 9+/Clang 10+. `.github/workflows/ci.yml` runs four jobs on every push/PR: **build** (debug + unit tests + release + `.deb` on `ubuntu:26.04`), **integration** (the `.deb` on each of four LTS distributions), **behaviour** (the fixture-driven cases plus the GUI under Xvfb) and **flatpak** (built from the working tree).

- **Two test tiers, both documented in `TESTS.md`.** `tests/unit/` is QtTest over the pure logic (`EnvBuilder`'s round-trip contract, `VDFParser`, `FeatureGate`, `DLSSSettings` JSON, `SteamPaths`), enabled with `-DPROTONFORGE_BUILD_TESTS=ON`. `tests/steam-lab/` is a bash harness that drives the real binary and the real `.deb` against fixture and real Steam installations, in containers and on Xvfb. **No Steam account is involved anywhere** — see `TESTS.md §3` for how. The suite should be fully green; the three bugs it found on its first run are fixed and written up in `TESTS.md §7`.
- **`build-deb.sh <source-dir> <output-dir>` is the one build path** — CI, the release workflow and the test lab all call it, and it prints the artifact path as its last stdout line. It builds from source by default; `PROTONFORGE_REUSE_BINARY=1` reuses `cmake-build-release/ProtonForge` and is only correct when that binary was built in the same environment the package is for (a Qt 6.10 binary installs on a Qt 6.8 system and then refuses to start).
- **Build dependencies live in `packaging/build-depends.txt`** — one list, read by both workflows and the lab's container images.
- **Version is single-sourced** from `project(ProtonForge VERSION x.y.z)` in `CMakeLists.txt`. CMake generates `Version.h` from `src/core/Version.h.in` into `cmake-build-*/generated/`; `build-deb.sh` greps the same line. Bump it there only.
- **Adding a source file requires editing `CMakeLists.txt`** — the lists are explicit (no globbing), and they are split: `CORE_SOURCES`/`CORE_HEADERS` become the `protonforge_core` static library that both the executable and the tests link, `UI_SOURCES`/`UI_HEADERS` are `src/ui` and go only into the executable. A new non-UI file belongs in the core lists.
- Releases are tag-driven: push a `vX.Y.Z` tag and `release.yml` builds the `.deb` and creates the GitHub release. See `RELEASE.md`.

## Conventions

- Includes are rooted at `src/` (e.g. `#include "core/Game.h"`), wired via `target_include_directories`. Generated headers (`Version.h`) are included unqualified.
- `CMAKE_AUTOMOC/AUTORCC/AUTOUIC` are on. Any `QObject` subclass needs `Q_OBJECT`; no manual moc wiring. Icons, `style.qss`, and the app icon are bundled through `resources.qrc` and loaded via `:/` resource paths.
- Header guards (`#ifndef FOO_H`), not `#pragma once`.
- App-wide services are singletons accessed via `Type::instance()`: `SettingsManager`, `LauncherManager`, `ProtonManager`, `ProtonDBClient`, `GpuInfoCache`. Use them; don't construct second copies.
- The app is dark-themed via a `QPalette` + `style.qss` set in `main.cpp`. `OpaqueTooltip` is a custom event filter installed to defeat compositor tooltip transparency — prefer it over per-widget tooltip hacks.

## Architecture — the core data flow

`DLSSSettings` (`src/core/DLSSSettings.h`) is the central value object: a flat struct of every configurable option (DLSS SR/RR/FG, HDR, Proton tweaks, overlay, Proton version, executable, free-form `customLaunchParams`). It serializes to/from JSON and is the unit of persistence, editing, and launch. Most features are "add a field here + handle it in the translation and UI layers."

The pipeline that ties the codebase together:

1. **Discovery** — `LauncherManager` (singleton, plugin-style registry of `ILauncher`) calls `SteamLauncher::discoverGames()`, which parses Steam's `appmanifest_*.acf` / `libraryfolders.vdf` via the hand-written `VDFParser` (`src/parsers/`) into `Game` objects. `SteamPaths` centralizes locating the Steam root — it transparently handles both native and **Flatpak** (`com.valvesoftware.Steam`) installs, with cached detection.
2. **Edit** — `MainWindow` hosts `GameListWidget` (left) and `DLSSSettingsWidget` (right). Selecting a game loads its `DLSSSettings`; editing emits the new settings back up.
3. **Persist** — `SettingsManager` stores `DLSSSettings` per game keyed by `Game::settingsKey()`, plus a default profile, in `~/.config/ProtonForge/settings.json`.
4. **Translate** — `EnvBuilder` (`src/utils/`) is the bidirectional bridge between `DLSSSettings` and Steam's launch-options string. `buildLaunchOptions()` / `buildEnvironment()` emit env vars (`PROTON_*`, `DXVK_*`, `NGX_*`, etc.); `parseLaunchOptions()` is the inverse, mapping a raw string back onto fields and round-tripping anything unrecognized through `customLaunchParams`. When changing how an option maps to an env var, update **both directions** here.
5. **Apply** — either `GameRunner` (`src/runner/`) launches the game directly (resolving the Proton path + game executable + `compatdata` prefix and spawning a `QProcess`, native-Linux vs Proton paths differ), or `SteamLauncher::applySettings()` writes the options into `localconfig.vdf`.

### Supporting subsystems

- **FeatureGate** (`src/core/`) — declarative capability gating. A static table maps each `Feature` (SmoothMotion, MultiFrameGen, Reflex, …) to a `Requirement` (min NVIDIA driver, min/max Proton version). `evaluate()` checks it against a `Context` built from the detected driver (`GpuInfoCache`) and the selected Proton version (`ProtonManager::resolveSelectedVersion`). Policy is intentionally lenient: unknown driver/Proton never warns. This is what drives the non-blocking compatibility warnings in the DLSS UI.
- **ProtonManager** (`src/utils/`) — manages Proton-CachyOS and Proton-GE installs by querying GitHub Releases (async `QNetworkAccessManager`), downloading + extracting into `compatibilitytools.d`, and checking for updates. Honors an optional GitHub PAT from settings to raise the API rate limit; surfaces 401 (expired token) and rate-limit errors back to the UI.
- **GPU detection** — `GpuInfoCache` runs `nvidia-smi` once in a background `QtConcurrent` task at startup and emits `updated()` so open widgets refresh their gates. `GPUDetector`/`NvidiaGPUDetector`/`CPUDetector`/`HDRChecker` are the underlying probes (HDR detection is DE-specific: KDE Plasma vs GNOME).
- **ProtonDB integration** — `ProtonDBClient` (`src/network/`) fetches a tier/score summary and per-game user reports. Note the non-obvious part: report files are served under an **obfuscated `gameId` hash** derived from the appId plus two salts in ProtonDB's `counts.json` that rotate every build, so the client fetches `counts.json` at runtime and recomputes the id (`computeGameId`). `LaunchOptionExtractor` mines those reports for launch-option recommendations shown in `RecommendationsDialog`. Responses are disk-cached under `~/.cache/ProtonForge/`.
- **ImageCache** (`src/network/`) downloads and caches Steam library artwork.

### Directory map

`src/core` (data + settings + feature gating) · `src/launchers` (`ILauncher` interface + Steam impl) · `src/parsers` (VDF) · `src/runner` (process launch) · `src/network` (ProtonDB, image cache) · `src/utils` (EnvBuilder, ProtonManager, detectors, SteamPaths) · `src/ui` (MainWindow + dialogs/widgets).

## Runtime state locations

- `~/.config/ProtonForge/settings.json` — per-game + default `DLSSSettings`, GitHub token.
- `~/.cache/ProtonForge/` — image and ProtonDB caches.
- `~/.steam/.../compatibilitytools.d/` — where Proton versions are installed (via `SteamPaths`).
- A `QLockFile` at `$TMPDIR/protonforge.lock` enforces single-instance (`main.cpp`).
