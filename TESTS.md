# Testing ProtonForge

Two tiers, both runnable on a developer machine and both wired into CI:

* **`tests/unit/`** — QtTest, one executable per subject, linking the real code.
  Pure logic only: no network, no display, no Steam, no GPU. Runs in under a second.
* **`tests/steam-lab/`** — a bash harness that drives the real binary and the real
  `.deb` against Steam installations, in containers and on a virtual screen.

**No Steam account is involved anywhere.** Nothing logs in, nothing needs
credentials, and no case will ask for any — see [§3](#3-testing-steam-without-steam)
for how that is possible.

```bash
# unit tests
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug -DPROTONFORGE_BUILD_TESTS=ON
cmake --build cmake-build-debug -j$(nproc)
ctest --test-dir cmake-build-debug --output-on-failure

# the lab
tests/steam-lab/steamlab preflight     # what is installed and what is missing
tests/steam-lab/steamlab test          # everything this machine can run
tests/steam-lab/steamlab test 30_discovery 45_launch
```

---

## 1. Requirements

Nothing below is needed for all of it — the harness only demands what the cases
you selected actually declare, so `steamlab test 60_gui` runs on a machine
without docker.

| Tier | Needs | Install |
|---|---|---|
| unit, and most cases | cmake, a C++17 compiler, Qt 6 | `sudo apt install cmake build-essential qt6-base-dev qt6-base-dev-tools` |
| `docker` cases | a usable docker daemon | `sudo apt install docker.io` and be in the `docker` group |
| `gui` cases | a virtual screen | `sudo apt install xvfb openbox xdotool x11-utils x11-apps` |
| `flatpak` cases | flatpak and a manifest reader | `sudo apt install flatpak flatpak-builder python3-yaml` |
| `55_steamclient` part f | a D-Bus name to register | `sudo apt install python3-dbus python3-gi dbus` |

`steamlab preflight` prints all of it in green and red and names the exact
`apt install` line for anything missing.

Everything the lab writes goes to `$PF_LAB_DIR`, `~/.cache/protonforge-testlab`
by default — outside the repository, because it fills up with a package per
distribution and, if the Flatpak tier runs, a 1.5 GB runtime.

---

## 2. Running

```
steamlab preflight              check the tools, report what is missing
steamlab build                  build the binary and the unit tests
steamlab image [--rebuild]      build the distribution container images
steamlab test [case...]         run cases (all the runnable ones by default)
steamlab test --list            list the cases and what each needs
steamlab fixtures <variant>     write a Steam fixture tree and print its root
steamlab shell <image>          interactive shell in a distribution container
steamlab status                 what is present and what is not
steamlab clean [--all]          remove the work directory (--all: images too)
```

Cases are named with or without the `.sh`. `--keep-going` carries on past a
failing case instead of stopping at the first.

```bash
LAB_DISTROS=debian:trixie steamlab test 20_deb_install
LAB_TEST_NETWORK=1 steamlab test 80_proton_mgr
```

Every knob lives in `tests/steam-lab/lab.env` — see `lab.env.example`, which
documents all of them. Environment beats `lab.env` beats the defaults.

### How long it takes

The fixture-driven cases are seconds each. The container cases are not, and it is
worth knowing where the time goes before assuming something has hung:

| | Cost |
|---|---|
| everything except `20_deb_install` / `50_real_steam` / `70_flatpak` | ~30 s total |
| a container image, first build | 1–4 min per distribution; the two Steam targets also fetch steamcmd and run an anonymous login |
| `20_deb_install`, per target | ~50 s: about 12 s compiling ProtonForge, the rest `apt` installing the Qt 6 runtime |
| `20_deb_install`, per target, cached | ~40 s — the package is reused, the `apt` install is not |
| `70_flatpak`, first run | plus a ~1.5 GB runtime download |

Each target compiles its own package on purpose. A binary carries the Qt version
it was linked against as a symbol requirement, so one built elsewhere installs and
then refuses to start — which is exactly the bug this matrix found (see §7).
Packages are cached per distribution under `$PF_LAB_DIR/deb/<distro>/` and rebuilt
as soon as anything under `src/`, `CMakeLists.txt`, `debian/`, `packaging/` or
`build-deb.sh` is newer.

Narrowing the matrix while working on something is usually what you want:

```bash
LAB_DISTROS=ubuntu:24.04 steamlab test 20_deb_install
```

### Where the output goes

```
$PF_LAB_DIR/
├── out/junit.xml                   the aggregated report
├── out/results/<case>.tsv          status<TAB>check<TAB>detail
├── out/results/<case>.time         seconds
├── out/<case>/*.log                everything the case captured
├── out/<case>/*.xwd                screenshots, on failure only
├── out/<case>/proton-invocation.txt  what the fake Proton was asked to do
├── deb/<distro>/*.deb              cached packages
├── apphome/                        the fake $HOME the app runs in
└── flatpak/                        the lab's own flatpak installation
```

---

## 3. Testing Steam without Steam

The obvious way to test this application is to install Steam, log in, and drive
it. That needs an account, a display, several gigabytes and a human to type a
Steam Guard code — none of which belongs in a test run. Four sources between them
cover almost everything instead.

**Fixture trees** (`lib/fixtures.sh`) write exactly the files ProtonForge reads,
in Steam's own formats, so the real parsers do the real work. There are no mocks
anywhere in this suite. `fx_steam_tree` produces the native layout, the Flatpak
layout, both at once, neither, or the bootstrap-only state; `fx_add_game`,
`fx_localconfig`, `fx_config_vdf` and `fx_compat_tool` fill it in.

The one thing that gates everything: `SteamPaths::hasLibraryFolders()`
(`SteamPaths.cpp:21`) requires `<root>/steamapps/libraryfolders.vdf` to exist as a
regular file. Without it the variant is `None`, `SteamLauncher::isAvailable()` is
false, and the launcher is never even registered.

**Real Steam, anonymously** (`50_real_steam`). `steamcmd` on Debian and Ubuntu
deliberately creates `~/.steam/steam` and `~/.steam/root` pointing at
`~/.local/share/Steam` — the same layout a desktop install has — and
`+login anonymous +app_update 1007` then writes a genuine `libraryfolders.vdf`
and `appmanifest_1007.acf`. No account, no display, no privileges, and it runs in
a `docker build` layer so a test run pays nothing for it. Valve's own bootstrap
tarball is extracted the same way `bin_steam.sh` does it.

This is the only thing that can catch the failure a fixture never will: the list
of paths `SteamPaths` probes for drifting away from what Steam actually creates.

**Stubs** (`lib/stubs.sh`) stand in for the rest. Every external program the app
runs is resolved through `$PATH`, and every liveness check it makes is a file or a
D-Bus name — so all of it can be satisfied honestly rather than worked around:

* `stub_steam_pid` leaves a `steam.pid` pointing at a process whose
  `/proc/<pid>/comm` really is `steam`, which is what `SteamClient.cpp:57`
  compares against. (A *copy* of `bash` under that name — a symlink reports the
  target's name, and `sleep` no longer works at all now that Ubuntu ships
  coreutils as one multi-call binary that dispatches on `argv[0]`.)
* `stub_steam_dbus` registers `com.steampowered.PressureVessel.LaunchAlongsideSteam`
  on a private session bus. That name being present *is* the `Ready` state —
  nothing else is checked — so owning it is a complete stand-in for a client that
  has finished starting.
* `stub_proton` and `stub_runtime` are an executable `proton` and an executable
  `_v2-entry-point` that record their `argv` and environment. That is all
  `GameRunner` ever requires of either, and it turns the whole compat-tool chain
  into something a test can read back.
* `stub_bin_from_fixture` replaces `nvidia-smi`, `lspci`, `lscpu` and
  `kscreen-doctor` with captured real output from `tests/steam-lab/fixtures/`.

**The CLI** (`src/core/Cli.cpp`) is what lets any of this be asserted cheaply. See
[§5](#5-the-cli).

### Isolation from your own setup

Every case runs the app with `HOME` pointed at `$PF_LAB_DIR/apphome`, and
`XDG_CONFIG_HOME`, `XDG_CACHE_HOME` and `TMPDIR` under it. `HOME` is the only path
lever the app has — `SteamPaths` derives everything from `QDir::homePath()` — so
redirecting it redirects the entire Steam world. Your real `~/.steam` and
`~/.config/ProtonForge` are never touched.

Three less obvious pieces of that:

* **`TMPDIR` is per-case.** `main.cpp:27` takes a `QLockFile` in `QDir::temp()`
  with `setStaleLockTime(0)`, so two runs sharing a `TMPDIR` collide.
* **The session bus is pointed at a path that cannot exist.** Unsetting
  `DBUS_SESSION_BUS_ADDRESS` is not enough: libdbus then falls back to
  `$XDG_RUNTIME_DIR/bus` and finds your real session — where your own Steam may
  own the launcher-service name, which would make `SteamClient` report `Ready`
  regardless of the fixture.
* **The Flatpak tier uses `FLATPAK_USER_DIR`**, so the lab's installation is its
  own and your `--user` installs are untouched.

---

## 4. The cases

Each declares what it needs in a `# lab-requires:` line, and `steamlab test`
enforces only the requirements of the cases you selected. A case with no line
counts as `docker`, so nothing runs unguarded by accident.

| Case | Requires | What it covers |
|---|---|---|
| `05_unit` | `build` | builds everything and folds `ctest` into this report |
| `20_deb_install` | `docker` | the `.deb` on four LTS distributions: build, install, layout, linkage, run, purge |
| `30_discovery` | `build` | Steam detection and game discovery across every layout permutation |
| `40_launchopts` | `build` | settings → launch options → `localconfig.vdf`, and back |
| `45_launch` | `build` | the whole launch chain, including the Steam Linux Runtime wrapping |
| `50_real_steam` | `docker` | real Steam-written files vs what `SteamPaths` looks for |
| `55_steamclient` | `build` | the client state machine, and deferred launches |
| `60_gui` | `gui build` | the real window on Xvfb |
| `70_flatpak` | `flatpak build` | the Flatpak built from the working tree, and its sandbox |
| `80_proton_mgr` | `build`, opt-in | installing Proton from GitHub for real |

The distribution matrix lives in `LAB_DISTROS_DEFAULT` in `lib/docker.sh` and is
the only place holding distribution knowledge. Adding a target is one line.

Long-term releases only: Ubuntu's interim releases live nine months, so a
regression found on one is a regression on a target nobody will still be running
by the time it is fixed. The four below already span Qt 6.4 to 6.10, which is the
axis that matters.

| Target | Qt | Qt6 core package |
|---|---|---|
| `debian:bookworm` | 6.4 | `libqt6core6` |
| `debian:trixie` | 6.8 | `libqt6core6t64` |
| `ubuntu:24.04` (LTS) | 6.4 | `libqt6core6t64` |
| `ubuntu:26.04` (LTS) | 6.10 | `libqt6core6t64` |

---

## 5. The CLI

`src/core/Cli.{h,cpp}`, dispatched from `main()` **before** `QApplication` is
constructed and only when one of its own options is present — so a bare
invocation and Qt's own flags reach the GUI exactly as they did before it existed.

| Option | Output |
|---|---|
| `--version`, `--help` | — |
| `--steam-info` | JSON: variant, root, every derived path, libraries, compat tools |
| `--list-games` | JSON array: app id, name, paths, native/Windows, update state, stored launch options |
| `--steam-client` | JSON: state, and which check decided it |
| `--print-launch-options <appid>` | the launch-options string |
| `--parse-launch-options <string>` | JSON: the settings it maps to, plus a `roundTrip` rebuild |
| `--apply <appid>` | writes `localconfig.vdf`, reports what it wrote and what it reads back |
| `--launch <appid> [--dry-run]` | launches, or prints the resolved plan without starting anything |
| `--set key=value` | overrides one setting first; repeatable |

Exit codes: `0` ok, `1` error, `2` usage, `3` no Steam detected, `4` unknown game.

`--set` takes the field names from the settings file and validates them against
`DLSSSettings` itself rather than a list, so a new setting becomes settable with no
change here. `--parse-launch-options` exists so `EnvBuilder`'s documented
round-trip contract can be checked from a shell as well as from a unit test.

One deliberate rough edge: an option the CLI does not recognise falls through to
`QApplication`, because Qt accepts its own flags in double-dash form
(`--platform`, `--style`) and swallowing them would break those. The consequence
is that a mistyped option opens the GUI instead of being rejected.

---

## 6. Extending the suite

A case is a standalone bash script under `cases/`. Discovery is by glob, so
dropping a file in is enough, and each one is executable on its own:

```bash
#!/usr/bin/env bash
# lab-requires: build
set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"

case_setup                       # checks the binary, resets the fake HOME

part "a) the thing"
NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" 1245620 name="ELDEN RING" >/dev/null

INFO="$(app_cli --steam-info)"
assert_json "Steam is detected" "$INFO" 'd["variant"]' "native"
assert_eq "the exit code is right" "0" "$(app_rc)"

case_finish                      # writes the result rows, sets the exit code
```

| Helper | Purpose |
|---|---|
| `ok` / `fail` / `skip` | record one check |
| `assert_eq`, `assert_ne`, `assert_true`, `assert_false` | the basics |
| `assert_json`, `assert_json_contains` | pull a value out of CLI output with a Python expression on `d` |
| `assert_contains`, `assert_not_contains` | file plus regex; dumps the tail of the file on failure |
| `assert_file`, `assert_no_file`, `assert_dir`, `assert_exec`, `assert_mode` | filesystem |
| `part "…"` | section heading |
| `app_cli` / `app_rc` / `app_env` | run the binary in its sandbox; read the exit code |
| `fx_*` | build Steam trees |
| `stub_*` | fake the things around Steam |
| `gui_*` | the virtual screen |
| `dock_*` / `fp_*` | containers and Flatpak |

Read the exit code with `app_rc`, never `$?`: the output comes back through a
command substitution, which is a subshell, so a variable set inside `app_cli`
would not survive. `app_rc` reads it from a file instead.

Two conventions worth keeping. Failure details are written to be actionable —
they name the function that has to change, quote the last lines of the relevant
log, and say what a user would see. And non-obvious lines cite the exact source
file and line they depend on, because that is what makes a test comprehensible
three years later.

---

## 7. Findings

What the suite found on its first run. All of it is fixed; the checks that
caught each one are named so a regression has an obvious home.

### 1. Applying settings to a game Steam had no entry for did nothing — fixed

`40_launchopts e3` — *"a game with no section gets one"*

`SteamLauncher::writeToLocalConfig` found the app's section with
`"\"<appid>\"\s*\{[^}]*\}"`. Any game whose launch options had never been set has
no such section — Steam only writes one once there is something to write — so the
regex found nothing and the `if (match.hasMatch())` block was skipped. But the
write was unconditional and `success = true` was set regardless, so
`applySettings()` returned true having changed nothing.

This was the most likely path through the function, not an edge case: press Apply
on a game you had not configured before, be told it worked, and Steam still
launched without the options.

### 2. A nested block before `LaunchOptions` corrupted the file — fixed

`40_launchopts e2` — *"the options are not buried inside the nested block"*

`[^}]*` cannot span a nested `{}` block, and real app sections contain several in
no guaranteed order. With one ahead of `LaunchOptions` the match ended early, the
"add it before the closing brace" branch ran, and the insert landed on the *nested*
block's closing brace. The original key was left behind, so the file ended up with
two — one of them somewhere Steam would never read it:

```
"1245620"
{
    "LastPlayed"  "1700000000"
    "BadgeData"
    {
        "level"  "5"

    "LaunchOptions"  "PROTON_ENABLE_NVAPI=1 ... %command%"
}
        "LaunchOptions"  ""
    }
```

### 3. A second instance hung instead of exiting — fixed

`60_gui f1` — *"a headless second instance exits rather than hanging"*

`main.cpp` took the `QLockFile` and, on failure, called `QMessageBox::warning`
before `app.exec()`. `QMessageBox` runs its own event loop, so on a scripted or
headless start nothing dismissed it and the process waited forever — a launch that
appears to hang with no window to be found.

### What changed

`SteamLauncher::writeToLocalConfig` now finds the section by **counting braces**,
skipping over quoted strings, and navigates down to the `apps` block one
case-insensitive level at a time so a same-named key elsewhere cannot be mistaken
for it. It creates the section when it is absent, escapes the value it writes
(user launch parameters can contain quotes and backslashes, which written raw
would end the VDF string early), writes through `QSaveFile` so the original is
replaced by a rename rather than truncated, and **returns false when it could not
make the change** — the caller is telling a user their settings reached Steam, and
that was the missing signal.

`main.cpp` keeps the dialog where a dialog belongs and prints to stderr instead
under the `offscreen` and `minimal` platform plugins, where nobody could dismiss
one.

The `40_launchopts e` and `60_gui f` sections cover all four shapes a real
`localconfig.vdf` comes in, both platforms, and the failure path.

### Also found, and fixed

* **`build-deb.sh` packaged a binary from the wrong environment.** It reused
  `cmake-build-release/ProtonForge` whenever the file existed, so building inside
  a trixie container packaged the host's Qt 6.10 binary. The result installed
  cleanly and then died with ``libQt6Core.so.6: version `Qt_6.10' not found``.
  Reuse is now opt-in via `PROTONFORGE_REUSE_BINARY=1`, which only the CI jobs
  that just built in the same container set. `ldd -r` in `20_deb_install` catches
  it independently.
* **The `.deb`'s desktop entry had drifted from `protonforge.desktop.in`** —
  different `Categories`, no `StartupNotify`. `build-deb.sh` used a heredoc while
  CMake installed the template. Both come from the template now, and
  `20_deb_install` asserts they match.
* **`Installed-Size` was appended after `Description:`** in the control file,
  which worked only because `Homepage:` happened to follow. It is inserted before
  `Homepage:` now.

### Noted, not defects

* `libQt6DBus` is linked but `libqt6dbus6` is not in `Depends`; it arrives
  transitively through `libqt6gui6`. It works by grace of the Qt packaging graph
  rather than by declaration, so `20_deb_install` records both facts.
* `debian/control` asks for `libqt6core6 (>= 6.0.0)`, which on everything except
  bookworm is a virtual name satisfied by `libqt6core6t64`'s versioned `Provides`.
  Also fine today, also worth watching.
* `desktop-file-validate` draws one hint: `Game` and `Utility` are both main
  categories, so the app may appear twice in a menu.
* An option the CLI does not recognise falls through to `QApplication`, because Qt
  accepts its own flags in double-dash form. So a mistyped option opens the GUI
  rather than being rejected.
* `ProtonDBClient::Report::tier` is declared, never written and never read.
* A Steam that has been installed but never signed into is invisible to
  ProtonForge, because the bootstrap creates no `libraryfolders.vdf`. Asserted in
  both `30_discovery` and `50_real_steam` so a change to it is a decision.
* ProtonDB's `gameId` derivation takes a timestamp that, for every realistic
  input, has no effect at all — it only enters as a modulus. Matching the site is
  what matters, so this is pinned rather than corrected.

---

## 8. Known limitations

Honest list of what these tests do **not** cover.

* **A logged-in Steam client**, and therefore `localconfig.vdf` as Steam itself
  writes it. Findings 1 and 2 were caught against a file the lab wrote, and the
  fixes are verified the same way; proving they survive a real Steam restart needs
  a real client. That is the main thing [§11](#11-phase-2-the-vm-tier) is for.
* **A game actually starting.** `45_launch` verifies the exact command and
  environment down to the last variable, against a Proton that records instead of
  running. Whether that command then renders a frame is untested.
* **A real NVIDIA GPU.** `FeatureGate` is unit-tested against synthetic contexts
  and the `nvidia-smi` parsers against captured output; no driver is involved.
* **Widget-level GUI assertions.** `60_gui` checks window geometry, X properties
  and side effects on disk. It cannot read a `QListWidget`, so "the list shows the
  right rows" is verified through the CLI reporting the same data, not by reading
  the widget.
* **`com.valvesoftware.Steam` (Flatpak).** About 1 GB installed, and running it
  inside another sandbox means bubblewrap inside bubblewrap, which upstream does
  not support.
* **`80_proton_mgr` in CI.** It downloads a Proton build, and GitHub allows 60
  unauthenticated API requests an hour per address — which runners share.

---

## 9. Troubleshooting

| Symptom | Cause |
|---|---|
| `no ProtonForge binary found` | `steamlab build`, or set `PF_BIN` |
| `cannot talk to the docker daemon` | not in the `docker` group; `sudo usermod -aG docker $USER && newgrp docker` |
| `python3-yaml is missing` | needed to derive a buildable Flatpak manifest |
| a case reports `Ready` when nothing is running | your own Steam owns the D-Bus name; the lab points the app at a dead bus, so this means that isolation broke |
| `steamlab test` seems to hang | a background stub inherited the pipeline's stdout. Every one of them must redirect to `/dev/null` — see `_stub_long_running` |
| the GUI cases find no window | `WAYLAND_DISPLAY` leaked in. Qt6 prefers Wayland whenever it is set and then ignores `DISPLAY` |
| a container runs an old version of a test | it should not: the agent is mounted from `/src`, not baked into the image |
| `libQt6Core.so.6: version 'Qt_6.10' not found` | a binary from a newer Qt was packaged. Do not set `PROTONFORGE_REUSE_BINARY` across environments |

---

## 10. CI

`.github/workflows/ci.yml` has three jobs:

* **build** — debug with tests, `ctest`, release, and the `.deb`.
* **behaviour** — the fixture-driven cases plus `60_gui` under Xvfb. This is the
  bulk of the coverage and it runs in well under two minutes.
* **flatpak** — the Flatpak built from the working tree. No `container:`, because
  bubblewrap needs privileges a normal container does not have.

Both test jobs upload `junit.xml` and their logs.

**The distribution matrix is deliberately not in CI.** `20_deb_install` builds
ProtonForge from source once per target — four full release builds, plus an `apt`
install of the Qt runtime each — and answers a question that only changes when
packaging or a distribution does, not on every push. It is a local tool:

```bash
tests/steam-lab/steamlab test 20_deb_install 50_real_steam
LAB_DISTROS=debian:trixie tests/steam-lab/steamlab test 20_deb_install
```

Run it before a release (`RELEASE.md` lists it) and after touching
`build-deb.sh`, `debian/control` or `packaging/`. Because the list lives only in
`lib/docker.sh` and no workflow matrix mirrors it, adding a target really is one
line.

For anyone who does want to run it inside a container they already have —
`LAB_IN_CONTAINER=1` makes the harness execute the agent directly instead of
through docker, so no nested containers are involved:

```bash
docker run --rm -v "$PWD:/src:ro" -e LAB_IN_CONTAINER=1 -e LAB_DISTROS=debian:trixie \
    debian:trixie bash -c 'cp -r /src /b && cd /b && tests/steam-lab/steamlab test 20_deb_install'
```

---

## 11. Phase 2: the VM tier

Not built. What is left after [§8](#8-known-limitations) needs a real logged-in
Steam client, and the way to get one without a login in every run is a golden
image:

```
img/base-<distro>.qcow2       a cloud image, chmod a-w
  └─ golden-<distro>.qcow2    Steam installed, signed into once, one small game
       └─ run/disk.qcow2      throwaway overlay, recreated on every boot
```

`steamlab vm-prepare` provisions with cloud-init, `vm-login` exposes a VNC display
so the sign-in (including Steam Guard) happens once by hand, `vm-snapshot` freezes
the result. Every run afterwards boots a fresh overlay of an already-authenticated
system. Credentials live only in the golden image and in `lab.env`, both outside
the repository.

What only that unlocks: `localconfig.vdf` as Steam writes it, a game launching
end to end, and native and Flatpak Steam live side by side for the `SteamPaths`
tie-break.

Nothing in the current suite is scaffolding for this. If it is never built, what
exists stands on its own.
