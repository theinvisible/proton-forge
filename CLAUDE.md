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

Requires Qt6 (`Core Widgets Network Concurrent DBus`), CMake 3.16+, GCC 9+/Clang 10+. `.github/workflows/ci.yml` runs three jobs on every push/PR: **build** (debug + unit tests + release + `.deb` on `ubuntu:26.04`), **behaviour** (the fixture-driven cases plus the GUI under Xvfb) and **flatpak** (built from the working tree). The distribution matrix (`20_deb_install`, `50_real_steam`) is **local-only** — it compiles the app once per target, so it runs before a release and after packaging changes rather than on every push.

- **Two test tiers, both documented in `TESTS.md`.** `tests/unit/` is QtTest over the pure logic (`EnvBuilder`'s round-trip contract, `VDFParser`, `FeatureGate`, `DLSSSettings` JSON, `SteamPaths`), enabled with `-DPROTONFORGE_BUILD_TESTS=ON`. `tests/steam-lab/` is a bash harness that drives the real binary and the real `.deb` against fixture and real Steam installations, in containers and on Xvfb. **No Steam account and no GOG account is involved anywhere** — see `TESTS.md §3` for how. The GOG cases work from a fabricated `gog-installs.json` (`fx_gog_game`) and a deliberately invalid stored token, so a case that ever reached the real API would fail loudly rather than succeed against somebody's library. The suite should be fully green; the three bugs it found on its first run are fixed and written up in `TESTS.md §7`.
- **`build-deb.sh <source-dir> <output-dir>` is the one build path** — CI, the release workflow and the test lab all call it, and it prints the artifact path as its last stdout line. It builds from source by default; `PROTONFORGE_REUSE_BINARY=1` reuses `cmake-build-release/ProtonForge` and is only correct when that binary was built in the same environment the package is for (a Qt 6.10 binary installs on a Qt 6.8 system and then refuses to start). `PROTONFORGE_VERSION_SUFFIX=~noble` qualifies the package version with its distribution, which is how a release can ship one `.deb` per target without the files or the installed versions colliding.
- **`build-appimage.sh <source-dir> <output-dir>` is the AppImage's one build path**, same contract (artifact path as the last stdout line). It **always builds in a container**, including locally: started outside one it builds `packaging/appimage/Dockerfile` and re-executes itself inside it, with the source read-only and no network. Two reasons, both load-bearing — an AppImage only runs on glibc at least as new as the one it was built against, so the base (`debian:bookworm`) decides who can run the result; and the build wants `qt6-wayland`, `patchelf`, `libsecret` and a downloaded `linuxdeploy`, none of which belongs on a developer's machine. `PROTONFORGE_APPIMAGE_IN_CONTAINER=1` (set automatically inside a container, and by the release workflow) builds in place instead.
- **Three things have to be named explicitly in an AppImage or they are silently missing**, and each one is a comment in `build-appimage.sh`: `libsecret` (QtKeychain dlsyms it, so no dependency graph mentions it), the Wayland QPA plugins, and the `wayland-shell-integration` directory the QPA plugin loads at run time — without the last one Qt cannot create a window on Wayland at all. `libnvidia-ml` must *not* be bundled; the script fails if it appears.
- **An AppImage's AppRun points `LD_LIBRARY_PATH` and friends at the bundle, and every child process would inherit that** — a game loading the bundle's libstdc++ out of a mount that disappears when ProtonForge exits. `packaging/appimage/AppRun` saves the host's values as `PROTONFORGE_HOST_<VAR>` and `HostEnvironment::forChildProcess()` (`src/utils/`) puts them back, filtering any remaining `$APPDIR` entry; `EnvBuilder::buildEnvironment()` and `ProcessRunner::run()` both start from it. Outside an AppImage it is the identity function, so there is one code path rather than one per format. The two variable lists must stay in step.
- **Build dependencies live in `packaging/build-depends.txt`** (plus `packaging/appimage/build-depends.txt` for what only the AppImage needs, and `packaging/appimage/tools.env` for the pinned linuxdeploy and AppImage runtime, installed by `packaging/appimage/install-tools.sh` — one script, used by the build image and the release workflow), **target distributions in `packaging/distros.txt`** — one list each, read by the workflows and the lab's container images alike. `distros.txt`'s fourth column marks the targets a release publishes for.
- **Version is single-sourced** from `project(ProtonForge VERSION x.y.z)` in `CMakeLists.txt`. CMake generates `Version.h` from `src/core/Version.h.in` into `cmake-build-*/generated/`; `build-deb.sh` greps the same line. Bump it there only.
- **Adding a source file requires editing `CMakeLists.txt`** — the lists are explicit (no globbing), and they are split: `CORE_SOURCES`/`CORE_HEADERS` become the `protonforge_core` static library that both the executable and the tests link, `UI_SOURCES`/`UI_HEADERS` are `src/ui` and go only into the executable. A new non-UI file belongs in the core lists.
- Releases are tag-driven: push a `vX.Y.Z` tag and `release.yml` builds one `.deb` per target in `packaging/distros.txt` — each in that distribution's own container — plus one AppImage in `debian:bookworm`, and creates the GitHub release once, in a separate `publish` job so the legs cannot race. `publish` collects artifacts named `protonforge-*`, so a new kind of artifact only has to be named accordingly. It refuses to run on a non-tag ref; re-run one by hand with `gh workflow run release.yml --ref vX.Y.Z`. See `RELEASE.md`.

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

### Two sources, two interfaces

`ILauncher` is about **games already on disk** and is all the launch path needs;
`IStoreService` (`src/launchers/IStoreService.h`) is about the **account behind
them** and is only touched by `StoreLibraryDialog`. A launcher that merely reads
what is there returns `nullptr` from `storeService()` and none of the second
applies. `SteamStoreService` and `GogStoreService` are deliberately different —
Steam has no sign-in of its own and installs by handing off to its client, GOG
has both — and that asymmetry is what `canSignIn()`/`canInstall()` exist for.

**`LauncherTraits`** (`src/core/LauncherTraits.h`) is what replaced comparing
`game.launcher()` against `"Steam"` at two dozen call sites. Stamped onto every
`Game` at discovery by `LauncherManager`. All five flags false for GOG, which is
what keeps `SteamAppId`, the Steam overlay, launch-option writeback and ProtonDB
lookups away from a product id that is not an appid.

**GOG, end to end**: `GogAuth` (OAuth2, paste-the-redirect-URL flow, refresh
coalesced because GOG invalidates a refresh token on use) → `GogApiClient` (owned
library) → `GogContentClient` (builds, depot manifests, signed chunk URLs; every
body zlib-encoded with no `Content-Encoding`) → `GogInstallPlan` (pure: which
depots, which files, whose path wins, **and whose spelling of a directory
wins** — depots routinely disagree on case, which costs nothing on the
filesystem GOG builds for and splits a game's data in two on ours; see
`TESTS.md §7` finding 7) → `GogDownloader` (parallel chunks, off-GUI-thread
verify, journal-based resume, CDN-token re-signing) → `GogInstallRegistry` (the
*only* record of what we installed) → `GogLauncher`. The native route bypasses
the middle: `GogOfflineClient` fetches the `.sh` installer and `ZipReader`
unpacks it. See `src/gog/GogDownloader.h` for why the re-signing is the part
worth reading.

### Supporting subsystems

- **FeatureGate** (`src/core/`) — declarative capability gating. A static table maps each `Feature` (SmoothMotion, MultiFrameGen, Reflex, …) to a `Requirement` (min NVIDIA driver, min/max Proton version). `evaluate()` checks it against a `Context` built from the detected driver (`GpuInfoCache`) and the selected Proton version (`ProtonManager::resolveSelectedVersion`). Policy is intentionally lenient: unknown driver/Proton never warns. This is what drives the non-blocking compatibility warnings in the DLSS UI.
- **ProtonManager** (`src/utils/`) — manages Proton-CachyOS and Proton-GE installs by querying GitHub Releases (async `QNetworkAccessManager`), downloading + extracting into `compatibilitytools.d`, and checking for updates. Honors an optional GitHub PAT from settings to raise the API rate limit; surfaces 401 (expired token) and rate-limit errors back to the UI.
- **System probes** — all of them read `/proc`, `/sys` or an in-process library; none shells out. `NvmlSession` (`dlopen` of `libnvidia-ml.so.1`) is the only source of GPU data and `GPUDetector::displayDevices()` scans `/sys/bus/pci/devices` for the rest; `CPUDetector` reads `/proc/cpuinfo` + `/sys`. `GpuInfoCache` pays NVML's one-time ~2.6 s init in a background `QtConcurrent` task at startup and emits `updated()` so open widgets refresh their gates. The probe paths are function parameters (`pciRoot`, `sysRoot`, `procRoot`) defaulting to the real ones, which is what makes them unit-testable against fixture trees — follow that pattern when adding one.
- **Anything that must shell out** goes through `ProcessRunner::run()` (`src/utils/`), which returns a **null** `QString` on missing program / failed start / timeout / non-zero exit, distinct from an empty successful result. Never hand-roll `QProcess` + `waitForFinished` for this: dropping that return value is what made a menu entry vanish and HDR misreport (`TESTS.md §7`, findings 4 and 5). Only `kscreen-doctor` (via `KScreenDoctor::run()`, fetched once per `DisplayDetector::detect()` and shared with `HDRChecker`), `gsettings`, `flatpak ps` and `tar` remain — plus the game/Proton launches in `GameRunner`, which are asynchronous by design.
- **ProtonDB integration** — `ProtonDBClient` (`src/network/`) fetches a tier/score summary and per-game user reports. Note the non-obvious part: report files are served under an **obfuscated `gameId` hash** derived from the appId plus two salts in ProtonDB's `counts.json` that rotate every build, so the client fetches `counts.json` at runtime and recomputes the id (`computeGameId`). `LaunchOptionExtractor` mines those reports for launch-option recommendations shown in `RecommendationsDialog`. Responses are disk-cached under `~/.cache/ProtonForge/`.
- **ImageCache** (`src/network/`) downloads and caches Steam library artwork.

### Directory map

`src/core` (data + settings + feature gating + `SecretStore`) · `src/launchers` (`ILauncher` + Steam/GOG impls, `IStoreService` + Steam adapter) · `src/gog` (auth, account API, content system, downloader, install registry, ZIP reader) · `src/parsers` (VDF) · `src/runner` (process launch) · `src/network` (ProtonDB, image cache, `JsonDiskCache`) · `src/utils` (EnvBuilder, ProtonManager, detectors, SteamPaths) · `src/ui` (MainWindow + dialogs/widgets).

## Runtime state locations

- `~/.config/ProtonForge/settings.json` — per-game + default `DLSSSettings`. (It never held the GitHub token; that lived in `ProtonForge.conf` and now lives in `SecretStore`.)
- **Credentials** — the GOG refresh token, the Steam Web API key and the GitHub token all go through `SecretStore` (`src/core/SecretStore.h`): the system keyring via QtKeychain when one answers, otherwise `~/.config/ProtonForge/secrets.json` at 0600. `PROTONFORGE_SECRET_STORE=file` forces the file backend. Loaded once at startup and read synchronously afterwards — `ProtonManager::applyGitHubHeaders()` reads inline while building a request, which rules out an async read. The old plaintext `QSettings` key `github/apiToken` is migrated on first run and removed.
- `~/.config/ProtonForge/gog-manifests/<productId>.json` — the file fingerprints of a completed GOG install, so the next update is a delta rather than a full re-download. Deleted with the registry entry.
- `~/.config/ProtonForge/gog-installs.json` — the GOG install registry. The single source of truth for what ProtonForge installed; there is **no filesystem scan**, so a Heroic or Lutris library in the same directory is never adopted and uninstall always knows exactly what it may delete. It also carries what an installed game must be *drawn* with — the banner URL, alongside the cached newest build id — because `discoverGames()` runs on `GameListWidget`'s worker thread and may not fetch. Steam needs no equivalent: its artwork URL falls out of the appid, while GOG's is content-hashed and has to be looked up once (`GogStoreService::refreshInstalledArtwork()`) and remembered.
- `~/Games/ProtonForge/` — where GOG games go, overridable via `QSettings` key `gog/installRoot` and in Settings → GOG. Store-partitioned: `GOG/<game>` and `prefixes/GOG/<productId>`. A partial download carries a `.protonforge-gog/` journal inside its own directory, so deleting the folder is complete cleanup.
- `~/.cache/ProtonForge/` — image, ProtonDB and GOG caches.
- `~/.steam/.../compatibilitytools.d/` — where Proton versions are installed (via `SteamPaths`).
- A `QLockFile` at `$TMPDIR/protonforge.lock` enforces single-instance (`main.cpp`).
