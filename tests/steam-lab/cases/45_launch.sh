#!/usr/bin/env bash
# lab-requires: build
#
# The launch chain: what ProtonForge actually asks the system to run.
#
# GameRunner composes Steam's compat tool chain, and getting it subtly wrong
# produces a game that starts but behaves oddly — no overlay, no Steamworks, the
# wrong userland — which is close to undebuggable from a bug report. It had no
# test coverage at all because testing it looked like it needed Proton and a game.
#
# It does not. A Proton is any directory with an executable file called `proton`,
# and a Steam Linux Runtime is an appmanifest plus an executable
# _v2-entry-point. Both are a few lines of shell that record how they were
# called, which turns the entire chain into something a test can read back.
#
# No Steam account, no Proton download, no game.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"

case_setup

APPID=1245620
RUNTIME_APPID=1628350

# Actually launching a Steam game goes through the client-readiness gate first
# (GameRunner.cpp:392), and what happens there depends on the machine: with a real
# `steam` on $PATH the launch is deferred and waits, without one it is refused
# outright. Neither is what these parts are testing, and either would make them
# pass or fail for the wrong reason.
#
# So readiness is arranged deliberately. A pid file pointing at a process that
# really is called steam makes SteamClient report Starting, and the deferred
# launch then proceeds once the liveness grace has elapsed
# (kSteamLivenessGraceMs = 10 s) — no client, no account, and the same answer on
# every machine. It costs those ten seconds, which is why only the two parts that
# spawn for real do it.
launch_with_steam_ready() {
    stub_steam_pid "$LAB_APP_HOME/.steam/steam.pid" >/dev/null \
        || { fail "could not fake a running Steam client"; return 1; }
    app_cli --launch "$1" --timeout "$TIMEOUT_LAUNCH" >"$(case_log "launch-$1")" 2>&1
    local rc=$?
    stub_steam_pid_stop
    return $rc
}

# ---------------------------------------------------------------------------
part "a) Proton resolution follows the documented order"

NATIVE="$(fx_steam_tree native compat_tool=)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" >/dev/null

# Nothing installed at all.
PLAN="$(app_cli --launch "$APPID" --dry-run)"
assert_json "with no Proton the launch is refused" "$PLAN" 'd["valid"]' "false"
assert_json_contains "and says why" "$PLAN" 'd["error"]' "Proton"
assert_eq "the exit code reports the failure" "1" "$(app_rc)"

# The preference list is walked in order, so a less preferred build must lose to
# a more preferred one present at the same time.
fx_compat_tool "$NATIVE" "GE-Proton9-20" >/dev/null
PLAN="$(app_cli --launch "$APPID" --dry-run)"
assert_json_contains "GE-Proton is used when it is all there is" "$PLAN" \
    'd["protonPath"]' "GE-Proton9-20"

fx_compat_tool "$NATIVE" "proton-cachyos-11.0-20260703-slr-x86_64" >/dev/null
PLAN="$(app_cli --launch "$APPID" --dry-run)"
assert_json_contains "proton-cachyos outranks GE-Proton" "$PLAN" \
    'd["protonPath"]' "proton-cachyos-11.0-20260703-slr-x86_64"

# A per-game mapping in config.vdf is Steam's own choice and outranks the scan.
fx_compat_tool "$NATIVE" "Proton-Special" >/dev/null
fx_config_vdf "$NATIVE" "$APPID=Proton-Special" >/dev/null
PLAN="$(app_cli --launch "$APPID" --dry-run)"
assert_json_contains "config.vdf's per-game mapping wins over the scan" "$PLAN" \
    'd["protonPath"]' "Proton-Special"

# And an explicit user choice outranks even that.
PLAN="$(app_cli --launch "$APPID" --dry-run --set protonVersion=GE-Proton9-20)"
assert_json_contains "an explicitly selected version wins over config.vdf" "$PLAN" \
    'd["protonPath"]' "GE-Proton9-20"

# ---------------------------------------------------------------------------
part "b) paths and the environment Proton is given"

fx_reset
NATIVE="$(fx_steam_tree native compat_tool=)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" installdir="ELDEN RING" >/dev/null
stub_proton "$NATIVE" "proton-cachyos-11.0-20260703-slr-x86_64" >/dev/null

PLAN="$(app_cli --launch "$APPID" --dry-run)"

assert_json "the plan is valid" "$PLAN" 'd["valid"]' "true"
assert_json "the game is not native Linux" "$PLAN" 'd["nativeLinux"]' "false"
assert_json "the executable was found" "$PLAN" \
    'd["gameExe"]' "$NATIVE/steamapps/common/ELDEN RING/ELDEN RING.exe"
assert_json "compatdata sits beside the game's library" "$PLAN" \
    'd["compatDataPath"]' "$NATIVE/steamapps/compatdata/$APPID"
assert_json "the working directory is the game's own" "$PLAN" \
    'd["workingDirectory"]' "$NATIVE/steamapps/common/ELDEN RING"

# The variables Proton needs to find its prefix and the client.
assert_json "STEAM_COMPAT_DATA_PATH" "$PLAN" \
    'd["env"]["STEAM_COMPAT_DATA_PATH"]' "$NATIVE/steamapps/compatdata/$APPID"
assert_json "STEAM_COMPAT_CLIENT_INSTALL_PATH" "$PLAN" \
    'd["env"]["STEAM_COMPAT_CLIENT_INSTALL_PATH"]' "$NATIVE"
assert_json "STEAM_RUNTIME" "$PLAN" \
    'd["env"]["STEAM_RUNTIME"]' "$NATIVE/ubuntu12_32/steam-runtime"
assert_json "SteamAppId" "$PLAN" 'd["env"]["SteamAppId"]' "$APPID"
assert_json "SteamGameId" "$PLAN" 'd["env"]["SteamGameId"]' "$APPID"

# DLSS settings have to reach the game process too, not just Steam's own string.
PLAN="$(app_cli --launch "$APPID" --dry-run --set enableProtonHDR=true)"
assert_json_contains "a DLSS setting reaches the child environment" "$PLAN" \
    'sorted(d["env"].keys())' "STEAM_COMPAT_DATA_PATH"

# ---------------------------------------------------------------------------
part "c) without a Steam Linux Runtime it is a plain proton run"

fx_reset
NATIVE="$(fx_steam_tree native compat_tool=)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" installdir="ELDEN RING" >/dev/null
PROTON="$(stub_proton "$NATIVE" "proton-cachyos-11.0-20260703-slr-x86_64")"

PLAN="$(app_cli --launch "$APPID" --dry-run)"

assert_json "no container is used" "$PLAN" 'd["usesContainer"]' "false"
assert_json "no runtime was required" "$PLAN" 'd["runtimeRequired"]' "false"
assert_json "proton itself is the program" "$PLAN" 'd["program"]' "$PROTON/proton"
assert_json "and the verb is run" "$PLAN" 'd["args"][0]' "run"
assert_json "followed by the executable" "$PLAN" \
    'd["args"][1]' "$NATIVE/steamapps/common/ELDEN RING/ELDEN RING.exe"

# And it really is what gets executed. This one waits out the readiness grace;
# see launch_with_steam_ready above.
stub_records_reset
launch_with_steam_ready "$APPID"
assert_true "proton was actually executed" stub_proton_was_run
assert_eq "with the run verb" "run" "$(stub_proton_args | head -n1)"
assert_eq "STEAM_COMPAT_DATA_PATH arrived in the child" \
    "$NATIVE/steamapps/compatdata/$APPID" "$(stub_proton_env STEAM_COMPAT_DATA_PATH)"

# ---------------------------------------------------------------------------
part "d) with a Steam Linux Runtime the call is wrapped"

# This is the chain Steam itself composes, and the reason the verb matters:
#   <runtime>/_v2-entry-point --verb=waitforexitandrun -- <proton> waitforexitandrun <exe>
# --verb=run would put the entry point in batch mode, which is for setup steps
# rather than the main process.
fx_reset
NATIVE="$(fx_steam_tree native compat_tool=)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" installdir="ELDEN RING" >/dev/null
PROTON="$(stub_proton "$NATIVE" "proton-cachyos-11.0-20260703-slr-x86_64" "$RUNTIME_APPID")"
RUNTIME="$(stub_runtime "$NATIVE" "$RUNTIME_APPID" "SteamLinuxRuntime_sniper")"

PLAN="$(app_cli --launch "$APPID" --dry-run)"

assert_json "the runtime is required" "$PLAN" 'd["runtimeRequired"]' "true"
assert_json "and it was found" "$PLAN" 'd["runtimePath"]' "$RUNTIME"
assert_json "the container is used" "$PLAN" 'd["usesContainer"]' "true"
assert_json "the entry point is the program" "$PLAN" 'd["program"]' "$RUNTIME/_v2-entry-point"
assert_json "the argv is Steam's own chain" "$PLAN" 'd["args"]' \
    "$(python3 -c '
import json,sys
print(json.dumps(["--verb=waitforexitandrun", "--",
                  sys.argv[1] + "/proton", "waitforexitandrun",
                  sys.argv[2] + "/steamapps/common/ELDEN RING/ELDEN RING.exe"], sort_keys=True))
' "$PROTON" "$NATIVE")"

# Container mode adds the mount and shader variables — pressure-vessel only sees
# what it is told to mount, so anything the game touches has to be named.
assert_json "STEAM_COMPAT_APP_ID" "$PLAN" 'd["env"]["STEAM_COMPAT_APP_ID"]' "$APPID"
assert_json "STEAM_COMPAT_INSTALL_PATH" "$PLAN" \
    'd["env"]["STEAM_COMPAT_INSTALL_PATH"]' "$NATIVE/steamapps/common/ELDEN RING"
assert_json "STEAM_COMPAT_LIBRARY_PATHS" "$PLAN" \
    'd["env"]["STEAM_COMPAT_LIBRARY_PATHS"]' "$NATIVE/steamapps"
assert_json "STEAM_COMPAT_TOOL_PATHS names both tools" "$PLAN" \
    'd["env"]["STEAM_COMPAT_TOOL_PATHS"]' "$PROTON:$RUNTIME"
assert_json_contains "STEAM_COMPAT_MOUNTS covers the library" "$PLAN" \
    'd["env"]["STEAM_COMPAT_MOUNTS"]' "$NATIVE/steamapps"
assert_json "STEAM_COMPAT_SHADER_PATH" "$PLAN" \
    'd["env"]["STEAM_COMPAT_SHADER_PATH"]' \
    "$NATIVE/steamapps/shadercache/$APPID/fozpipelinesv6"

# Now run it for real and read back what each side was handed.
stub_records_reset
launch_with_steam_ready "$APPID"

assert_true "the entry point was executed" stub_runtime_was_run
assert_eq "with the waitforexitandrun verb" "--verb=waitforexitandrun" "$(stub_runtime_args | head -n1)"
assert_true "and it passed the command through to proton" stub_proton_was_run
assert_eq "which got the waitforexitandrun verb too" \
    "waitforexitandrun" "$(stub_proton_args | head -n1)"

# ---------------------------------------------------------------------------
part "e) a required runtime that is not installed"

# Steam would install it; ProtonForge cannot, so it warns and carries on rather
# than refusing to launch.
fx_reset
NATIVE="$(fx_steam_tree native compat_tool=)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" installdir="ELDEN RING" >/dev/null
PROTON="$(stub_proton "$NATIVE" "proton-cachyos-11.0-20260703-slr-x86_64" "$RUNTIME_APPID")"
# ...and deliberately no stub_runtime.

PLAN="$(app_cli --launch "$APPID" --dry-run)"

assert_json "the plan is still valid" "$PLAN" 'd["valid"]' "true"
assert_json "the runtime was required" "$PLAN" 'd["runtimeRequired"]' "true"
assert_json "but not found" "$PLAN" 'd["runtimePath"]' ""
assert_json "so no container is used" "$PLAN" 'd["usesContainer"]' "false"
assert_json_contains "and the user is warned" "$PLAN" 'd["warning"]' "Steam Linux Runtime"
assert_json "it falls back to a plain proton run" "$PLAN" 'd["args"][0]' "run"

# An appmanifest whose _v2-entry-point is not executable must count as missing —
# a non-executable entry point cannot be run, so treating it as present would
# turn a warning into a failed launch.
RUNTIME="$(stub_runtime "$NATIVE" "$RUNTIME_APPID" "SteamLinuxRuntime_sniper")"
chmod -x "$RUNTIME/_v2-entry-point"
PLAN="$(app_cli --launch "$APPID" --dry-run)"
assert_json "a non-executable entry point counts as not installed" "$PLAN" \
    'd["runtimePath"]' ""
chmod +x "$RUNTIME/_v2-entry-point"

# ---------------------------------------------------------------------------
part "f) the Steam overlay"

fx_reset
NATIVE="$(fx_steam_tree native compat_tool= overlay=yes)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" installdir="ELDEN RING" >/dev/null
stub_proton "$NATIVE" "proton-cachyos-11.0-20260703-slr-x86_64" >/dev/null

PLAN="$(app_cli --launch "$APPID" --dry-run --set enableSteamOverlay=true)"
assert_json_contains "the 64-bit overlay is preloaded" "$PLAN" \
    'd["env"]["LD_PRELOAD"]' "ubuntu12_64/gameoverlayrenderer.so"
assert_json_contains "and the 32-bit one" "$PLAN" \
    'd["env"]["LD_PRELOAD"]' "ubuntu12_32/gameoverlayrenderer.so"

PLAN="$(app_cli --launch "$APPID" --dry-run --set enableSteamOverlay=false)"
assert_json "with the overlay off nothing is preloaded" "$PLAN" \
    'd["env"].get("LD_PRELOAD", "")' ""

# Without the libraries on disk there is nothing to preload, and the launch must
# still go ahead.
fx_reset
NATIVE="$(fx_steam_tree native compat_tool= overlay=no)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" installdir="ELDEN RING" >/dev/null
stub_proton "$NATIVE" "proton-cachyos-11.0-20260703-slr-x86_64" >/dev/null
PLAN="$(app_cli --launch "$APPID" --dry-run --set enableSteamOverlay=true)"
assert_json "a missing overlay library is not preloaded" "$PLAN" \
    'd["env"].get("LD_PRELOAD", "")' ""
assert_json "and the launch still goes ahead" "$PLAN" 'd["valid"]' "true"

# ---------------------------------------------------------------------------
part "g) native Linux games take the other branch"

fx_reset
NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" 570 name="Dota 2" installdir="dota 2 beta" exe=native >/dev/null

PLAN="$(app_cli --launch 570 --dry-run)"

assert_json "the game is native" "$PLAN" 'd["nativeLinux"]' "true"
assert_json "no Proton is involved" "$PLAN" 'd["protonPath"]' ""
assert_json "no compatdata is involved" "$PLAN" 'd["compatDataPath"]' ""
assert_json "the executable is the program" "$PLAN" \
    'd["program"]' "$NATIVE/steamapps/common/dota 2 beta/dota 2 beta"
assert_json "with no verb in front of it" "$PLAN" 'len(d["args"])' "0"
assert_json "SteamAppId is still set" "$PLAN" 'd["env"]["SteamAppId"]' "570"

# Custom game arguments become argv on the native path, in the order given —
# argv order is meaningful, so this is a sequence and not a set.
PLAN="$(app_cli --launch 570 --dry-run --set customLaunchParams="%command% -novid -console")"
assert_json "custom arguments are passed to the game, in order" "$PLAN" \
    'd["args"]' '["-novid", "-console"]'

# ---------------------------------------------------------------------------
part "h) --dry-run really does nothing"

fx_reset
NATIVE="$(fx_steam_tree native compat_tool=)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" installdir="ELDEN RING" >/dev/null
stub_proton "$NATIVE" "proton-cachyos-11.0-20260703-slr-x86_64" >/dev/null
stub_records_reset

# With a running client faked, so "nothing was started" cannot be explained away
# by the readiness gate having refused the launch.
stub_steam_pid "$LAB_APP_HOME/.steam/steam.pid" >/dev/null
app_cli --launch "$APPID" --dry-run >/dev/null
stub_steam_pid_stop

assert_false "no process was started" stub_proton_was_run
# GameRunner creates the prefix directory as a side effect of launching; a plan
# must not.
assert_no_file "the compatdata directory was not created" \
    "$NATIVE/steamapps/compatdata/$APPID"

case_finish
