# Testing ProtonForge

Two tiers, both runnable on a developer machine and both wired into CI:

* **`tests/unit/`** — QtTest, one executable per subject, linking the real code.
  Pure logic only: no network, no display, no Steam, no GPU. The system probes are
  covered here too, because they read `/proc` and `/sys` paths that are parameters
  and can therefore point at a fixture tree. Runs in under a second.
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
* `stub_bin_from_fixture` replaces `kscreen-doctor` with captured real output from
  `tests/steam-lab/fixtures/`. The `nvidia-smi`, `lspci` and `lscpu` fixtures are
  still there and still used — but by the **unit** tests now, not as stubs: the
  app stopped shelling out to those three (see [§7](#7-findings), finding 5), so
  stubbing them changes nothing about what it reads. `tst_kscreendoctor` reads
  `kscreen-doctor-o.txt` from the same directory, so a test and a stub cannot
  drift apart.

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
| `46_gog_launch` | `build` | the same chain for a GOG game: no Steam identity, no overlay, ProtonForge's own prefix, and the native `start.sh` route |
| `50_real_steam` | `docker` | real Steam-written files vs what `SteamPaths` looks for |
| `55_steamclient` | `build` | the client state machine, and deferred launches |
| `60_gui` | `gui build` | the real window on Xvfb |
| `70_flatpak` | `flatpak build` | the Flatpak built from the working tree, and its sandbox |
| `80_proton_mgr` | `build`, opt-in | installing Proton from GitHub for real |
| `90_gog` | `build` | GOG discovery from the install registry, update state, and the CLI's session commands |
| `91_stores` | `build` | the store layer: how a configured, unconfigured and unknown store each answer |

The distribution matrix lives in `packaging/distros.txt` — the only place holding
distribution knowledge, shared with `.github/workflows/release.yml` so a target
cannot be added to the lab and forgotten in the release. `lib/docker.sh` fills
`LAB_DISTROS_DEFAULT` from it. Adding a target is one line.

Long-term releases only: Ubuntu's interim releases live nine months, so a
regression found on one is a regression on a target nobody will still be running
by the time it is fixed. The four below already span Qt 6.4 to 6.10, which is the
axis that matters.

| Target | Qt | Qt6 core package | Released |
|---|---|---|---|
| `debian:bookworm` | 6.4 | `libqt6core6` | no |
| `debian:trixie` | 6.8 | `libqt6core6t64` | no |
| `ubuntu:24.04` (LTS) | 6.4 | `libqt6core6t64` | yes, `~noble` |
| `ubuntu:26.04` (LTS) | 6.10 | `libqt6core6t64` | yes, `~resolute` |

The last column is the file's fourth field. All four are tested; the two Ubuntu
LTS also get a published `.deb`, qualified with the distribution codename so the
packages are distinguishable as files *and* once installed — `build-deb.sh` takes
that suffix from `PROTONFORGE_VERSION_SUFFIX`.

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
| `--gog-login-url` | the GOG sign-in URL to open in a browser |
| `--gog-status` | JSON: whether a GOG session is restored, and whose |
| `--store-list <launcher>` | JSON array: what the account owns, installed or not |
| `--gog-plan <productid>` | JSON: what installing would fetch — and writes nothing |
| `--gog-install <productid>` | downloads and installs; progress on stderr, result on stdout |
| `--gog-uninstall <productid>` | deletes the install and its Proton prefix |

Exit codes: `0` ok, `1` error, `2` usage, `3` no Steam detected, `4` unknown game.

`--gog-plan` is the analogue of `--launch --dry-run`: it resolves builds, depots
and manifests against GOG's live content system and prints the file list and
total size without writing a byte of game data. It needs no sign-in, because the
content system is public and only the chunk URLs are signed — so it also answers
"can ProtonForge install this at all" before an account exists.

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

### 4. "System Information" disappeared from the Help menu — fixed

`60_gui g` — *"Help -> System Information is reachable without any external tool"*

Reported by a user, not by the suite: the menu entry was there on some starts and
gone on others, for the whole session. `MainWindow::setupMenuBar` added it only
`if (GPUDetector::hasNvidiaGPU())`, and that probe ran `lspci` with
`waitForFinished(1000)` **without checking the return value** — on timeout it read
an empty buffer and answered "no GPU". `lspci` parses ~1.5 MB of `pci.ids`; measured
on the reporter's machine it takes 2.54 s cold and 0.01 s warm, so the entry
survived a warm relaunch and vanished on the first start after a boot. The menu is
built once in the constructor and never rebuilt, so a lost race stayed lost.

The predicate was wrong quite apart from the timeout: it matched
`NVIDIA || "VGA compatible controller"`, which is true on any machine with a
display adapter. The timeout was the only branch that ever returned false — the
guard gated nothing and cost up to a second of GUI-thread stall before `show()`.

The entry is unconditional now, and `showSystemInfo()` opens the dialog even with
an empty GPU list: it reports CPU and monitor details too, and explains the missing
GPU in its own tab instead of refusing to open. `GPUDetector::hasNvidiaGPU()` is
gone.

`detectHybridGpu()` had the same defect with a 2000 ms budget — still under the
2.54 s — so the PRIME-offload warning was silently absent on a genuinely hybrid
laptop. It now reads `/sys/bus/pci/devices/*/{class,vendor}` directly: no
subprocess, no timeout, no `pci.ids`, and unit-testable against a fixture tree
(`tst_gpudetector`). Class `0x03xxxx` covers VGA, 3D controller — how Optimus dGPUs
announce themselves — and other display controllers.

The gap that let this through: `60_gui g` already started the app with `lspci`,
`nvidia-smi`, `lscpu` and `kscreen-doctor` stubbed missing, but only asserted the
app stayed up. It now drives Alt+H, S and waits for the dialog.

### 5. The same dropped-timeout mistake in five more places — fixed

`tst_processrunner`, `tst_cpudetector`, `tst_kscreendoctor`, `tst_gpudetector`

Finding 4 was not a one-off. `src/` was swept for the pattern, and every
hand-rolled `QProcess` call site made some version of the same mistake:

| Site | What it did |
|---|---|
| `MangoHudDialog::isMangoHudInstalled` | `which mangohud`, return value dropped, then `exitCode() == 0` — which is **0 on a process that never ran**, so an empty PATH reported MangoHud as *installed*. Also re-ran on every game click. |
| `HDRChecker` (×2), `KdeDisplayProbe` | Checked `error() == UnknownError`, which a timeout leaves untouched — so a slow `kscreen-doctor` read as a successful "HDR: disabled" on a machine with HDR on. |
| `CPUDetector`, `NvidiaGPUDetector` | Returned on timeout without `kill()`, leaving `~QProcess` to block on the live child. |
| `ProtonManager::extractArchive` | `tar` with no timeout *and* only `finished` connected — and `finished` is not emitted when a program fails to start, so a missing `tar` hung the install UI forever. |

All of it now goes through `ProcessRunner::run()`, which checks
`waitForStarted`, checks `waitForFinished`, kills the child on timeout, checks
the exit code, and expresses failure as a **null** `QString` — a value that
empty-but-successful output can never be confused with. `tst_processrunner` pins
that contract, including that a timeout returns null within a fraction of the
child's remaining lifetime.

Four of the subprocesses turned out not to be needed at all:

* **`lscpu`** — every field it supplied is in `/proc/cpuinfo` and `/sys`.
  `CPUDetector::readTopology()` reads them, which also removed the `LC_ALL=C`
  locale workaround. Two subtleties the fixture tests pin: the frequency envelope
  has to consider every CPU (cpu0 reports 4700 MHz on the machine this was
  written on while the favoured cores reach 4900), and cache totals must sum the
  individual instances, because a hybrid CPU's P and E cores differ — lscpu's
  "544 KiB (14 instances)" is 6×48K + 8×32K, not 14×48K.
* **`lspci -D -nnk`** — `GPUDetector::displayDevices()` reads
  `class`/`vendor`/`device` and the `driver` symlink straight from sysfs. The
  D3cold fallback then reads `/proc/driver/nvidia/gpus/<bdf>/information`, which
  is strictly better than what it replaced: the driver's own model name plus the
  GPU UUID and VBIOS version, none of which the lspci parse could fill in.
* **`modinfo -F license nvidia`** — `/sys/module/nvidia/taint` answers the same
  question (`P` = proprietary, otherwise the open module).
* **`nvidia-smi -q`** — replaced by NVML, which was already loaded via `dlopen`
  and already overwrote most of what the text parse produced. ~300 lines of
  key/value and structural regexes went with it, including six that hard-coded
  the field *order* of a driver-version-dependent format. Two bugs surfaced while
  porting: the Resizeable BAR test was `bar1 >= 16384`, which reported ReBAR off
  on every card with less than 16 GB of VRAM (it is now "BAR1 covers VRAM"), and
  the release date was parsed as "everything after the version", which on a
  driver that prints build metadata yielded
  `Release Build (dvs-builder@…) Tue May 19 …` instead of a date.

Measured on the reporting machine: `nvidia-smi -q` cost 2.65 s cold, and NVML's
one-time `nvmlInit_v2` costs the same 2.6 s — but it is paid once, off the GUI
thread, by `GpuInfoCache::refreshAsync()` at startup, after which detection is
6 ms. Opening System Information used to run `nvidia-smi` + `lscpu` + two
`kscreen-doctor` invocations synchronously on the GUI thread.

Two more redundancies went with it: `kscreen-doctor -o` ran **twice** per
`DisplayDetector::detect()` (`KdeDisplayProbe` and `HDRChecker` each spawned their
own), and `GameRunner::onSteamWaitTick()` evaluated `SteamClient::state()` twice
per 500 ms tick — which on a Flatpak Steam install meant two blocking
`flatpak ps` calls per tick.

Verified with `strace -f -e trace=execve` across a full pass of the UI: the only
`execve` left is ProtonForge itself, plus one `kscreen-doctor` on a KDE session
where there used to be two.

### 6. A finished GOG install kept its "installing" badge — fixed

`tst_gogqueue`

Reported from use, not by the suite. Installing a GOG game from the store dialog
left the row reading *"↓ installing"* after the download had completed, and the
button next to it still saying *"Cancel"*, with nothing that would later correct
either.

`GogDownloader` announced the outcome and *then* tore the job down:

```cpp
emit installFinished(productId, installPath);
endJob();                                  // clears m_job
```

Everything listening reacts to that signal by asking what is installing now —
`StoreLibraryDialog` rebuilds its rows from `IStoreService::isInstalling()`,
which reads the downloader's queue. During the emit the job was still on it, so
the answer was "this one", and the badge it painted was the one it already had.
The dialog's refresh was not missing; it ran against a queue that had not been
updated yet. Both orderings are now the other way round, in all four places that
end a job.

The same reversal decides when a *queued* job starts. `endJob()` called
`startNext()` inline, so with two games queued the second one's `queueChanged`,
its progress — and, when it could resolve its build from cache and fail there,
its own `installFailed` — all reached the listener **before** the news about the
job that had just ended. `startNext()` is now scheduled on the event loop, which
makes "a job's outcome is announced before the next job's first signal"
unconditional rather than true-if-it-happens-to-block.

`tst_gogqueue` pins both halves without a network or an account: two
content-system lookups pre-seeded into the disk cache with a generation-1-only
answer make an install fail for a real reason and entirely offline (the native
installer detour between them needs a token, and there is no session). It reads
`isInstalling()` from inside the failure handler — the moment that was wrong —
and, for the ordering, races a job that must go round the event loop against one
that reaches its answer without yielding.

Three more of the same shape, found while fixing it:

* **Cancelling a queued install announced nothing.** `cancel()` on a job that had
  not started yet dropped it from the queue and emitted only `queueChanged`,
  which says the queue moved but not which game left it — and the store dialog
  does not listen to it. So the row kept the badge that had just stopped being
  true, by the same mechanism and with no way back. Both cancellation paths now
  report the same way.
* **The progress bar could outlive its download.** `StoreLibraryDialog` hid the
  progress frame only when the finishing store was also the selected one, so
  switching to another store mid-install left the bar on screen afterwards,
  still showing an install that had ended. It belongs to whatever is
  downloading, not to whatever is on screen, and is now cleared before that
  check.
* **`saveStateJournal()` aimed at `/`** when a job failed before an install path
  had been resolved — `journalPath()` is `installPath + "/.protonforge-gog"`, and
  an empty install path names the filesystem root. Harmless as a normal user,
  which is why nothing had noticed.

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
  and the PCI scan against a fixture `sysfs` tree; no driver is involved. Note
  that a GPU-less app cannot be staged by emptying `$PATH` at all any more:
  NVML arrives via `dlopen` and the fallback reads `/proc/driver/nvidia`, neither
  of which `$PATH` touches, so `60_gui g` still sees the real card.
* **The D3cold fallback path.** `NvidiaGPUDetector::detectFromPci()` is what runs
  when NVML enumerates nothing — an Optimus dGPU in runtime suspend. It can be
  exercised by hand on this hardware, but nothing asserts it automatically, and
  one question about it is open: whether reading
  `/proc/driver/nvidia/gpus/<bdf>/information` *wakes* a suspended GPU.
  `/proc/driver/nvidia/version` does not; the per-GPU file is unverified. If it
  turns out to wake it, that read has to go and the name has to come from the PCI
  device id instead.
* **A proprietary NVIDIA kernel module.** The open/proprietary split now reads
  `/sys/module/nvidia/taint` and treats a `P` as proprietary. Only the open
  module (`O`) was available to check against.
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
