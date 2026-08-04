# steam-lab

Integration tests for ProtonForge. The full documentation is
[`TESTS.md`](../../TESTS.md) in the repository root; this file is the quick
reference for working inside this directory.

```bash
./steamlab preflight            # what is installed, what is missing
./steamlab test                 # everything this machine can run
./steamlab test --list          # the cases and what each needs
./steamlab test 30_discovery    # one of them
```

## Layout

```
steamlab                 the CLI entry point — subcommands, requirement gating,
                         per-case execution, the JUnit report
lab.env.example          every knob, documented and commented out
lib/
  common.sh              config, output, the assertion library
  case.sh                per-case scaffolding, sourced first by every case
  app.sh                 the binary under test: discovery, its sandbox, app_cli
  fixtures.sh            synthetic Steam trees — fx_*
  stubs.sh               fake steam.pid, D-Bus name, proton, runtime — stub_*
  gui.sh                 Xvfb / openbox / xdotool — gui_*
  docker.sh              images, the distro matrix, RESULT parsing — dock_*
  flatpak.sh             isolated FLATPAK_USER_DIR, devel manifest — fp_*
cases/NN_name.sh         one file per case, executable on its own
docker/distro/           Dockerfile plus the in-container agent
fixtures/                captured nvidia-smi / lspci / lscpu / kscreen-doctor output
```

## The three ideas worth knowing before changing anything

**Nothing here needs a Steam account.** Fixture trees are hand-written in Steam's
own formats so the real parsers do the real work; `steamcmd`'s anonymous login
supplies genuinely Steam-written files where that matters; and the stubs satisfy
the actual checks the app makes rather than working around them — a real process
named `steam`, the real D-Bus name registered, a real executable called `proton`.

**The container never asserts.** `docker/distro/in-container.sh` prints
`RESULT:key=value` lines and the case on the host turns those into checks. That
keeps distribution knowledge in one place (`LAB_DISTROS_DEFAULT` in
`lib/docker.sh`), puts every result in the JUnit report, and lets the same agent
run as a bare step in a CI `container:` job with `LAB_IN_CONTAINER=1`.

**The app runs in a fake `$HOME`.** `HOME` is the only path lever ProtonForge has,
so pointing it at `$PF_LAB_DIR/apphome` moves its whole world. `TMPDIR` is
per-case (the single-instance lock lives there) and the session bus is pointed at
a path that cannot exist (otherwise your own running Steam makes the app report
`Ready`).

## Writing a case

```bash
#!/usr/bin/env bash
# lab-requires: build
set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"

case_setup

part "a) what this proves"
NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" 1245620 name="ELDEN RING" >/dev/null

INFO="$(app_cli --steam-info)"
assert_json "Steam is detected" "$INFO" 'd["variant"]' "native"
assert_eq "and the exit code agrees" "0" "$(app_rc)"

case_finish
```

Requirement tokens: `build`, `docker`, `gui`, `flatpak`. Only the tokens of the
cases you selected are enforced. No line at all counts as `docker`.

Two things that will bite you:

* **Read the exit code with `app_rc`, not `$?`.** The output comes back through a
  command substitution, which is a subshell.
* **Redirect anything you background.** A stub that inherits the case's stdout
  keeps the pipeline open, and `steamlab test | tail` then looks like a hang.

`fx_*` and `stub_*` reject unknown keys rather than ignoring them, so a typo in a
case fails immediately instead of quietly testing nothing.

## Cases

| Case | Requires | Covers |
|---|---|---|
| `05_unit` | `build` | builds everything, folds `ctest` into this report |
| `20_deb_install` | `docker` | the `.deb` on four LTS distributions |
| `30_discovery` | `build` | Steam detection and game discovery |
| `40_launchopts` | `build` | settings ↔ launch options ↔ `localconfig.vdf` |
| `45_launch` | `build` | the launch chain, incl. the Steam Linux Runtime |
| `50_real_steam` | `docker` | real Steam files vs what the app looks for |
| `55_steamclient` | `build` | the client state machine, deferred launches |
| `60_gui` | `gui build` | the real window on Xvfb |
| `70_flatpak` | `flatpak build` | the Flatpak and its sandbox |
| `80_proton_mgr` | `build` + `LAB_TEST_NETWORK=1` | installing Proton from GitHub |

Three cases currently fail on purpose — see [TESTS.md §7](../../TESTS.md#7-findings).

## Troubleshooting

See [TESTS.md §9](../../TESTS.md#9-troubleshooting) for the symptom table. The
short version: `./steamlab preflight` names anything missing along with the exact
`apt install` line, and `./steamlab status` says what the lab has built so far.
Everything a case captured is under `$PF_LAB_DIR/out/<case>/`.
