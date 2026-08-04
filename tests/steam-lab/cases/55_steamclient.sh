#!/usr/bin/env bash
# lab-requires: build
#
# Is the Steam client running, and is it ready?
#
# ProtonForge waits for this before launching a Steam game, because Steamworks,
# the overlay, cloud saves and playtime all go through the client. Getting it
# wrong is quiet: too eager and the game starts without any of that, too
# cautious and the user sits through a 60-second wait for nothing.
#
# All three states are reachable without a Steam account, because none of the
# checks involve one:
#
#   Ready       a name on the session bus. We register it ourselves.
#   Starting    a pid file whose process is really named "steam". A copy of
#               `sleep` named `steam` satisfies that exactly as the client does.
#   NotRunning  neither.
#
# The counter-cases matter as much as the positive ones: a stale pid file and a
# recycled pid both have to read as not running, and that guard has never been
# exercised.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"

case_setup

DBUS_NAME="com.steampowered.PressureVessel.LaunchAlongsideSteam"

# ---------------------------------------------------------------------------
part "a) no Steam installation at all"

fx_steam_tree none >/dev/null
STATE="$(app_cli --steam-client)"

assert_json "state is not-running" "$STATE" 'd["state"]' "not-running"
assert_json "variant is none" "$STATE" 'd["variant"]' "none"
assert_json_contains "and it says why" "$STATE" 'd["detail"]' "no Steam installation"
assert_eq "the command itself succeeds" "0" "$(app_rc)"

# ---------------------------------------------------------------------------
part "b) a native install with no client running"

NATIVE="$(fx_steam_tree native)"
STATE="$(app_cli --steam-client)"

assert_json "state is not-running" "$STATE" 'd["state"]' "not-running"
assert_json "variant is native" "$STATE" 'd["variant"]' "native"
assert_json "the pid file is where it should be looked for" "$STATE" \
    'd["pidFile"]' "$LAB_APP_HOME/.steam/steam.pid"
assert_json "and it is not there" "$STATE" 'd["pidFileExists"]' "false"
assert_json_contains "the reason names the missing file" "$STATE" 'd["detail"]' "no pid file"

# No session bus at all — the container default. This must be a quiet
# not-running, not a crash and not a hang.
assert_json "with no session bus the app does not fall over" "$STATE" 'd["dbusConnected"]' "false"

# ---------------------------------------------------------------------------
part "c) a live client that has not finished starting"

stub_steam_pid "$LAB_APP_HOME/.steam/steam.pid"
PID="$LAB_FAKE_STEAM_PID"
info "fake steam pid $PID, comm=$(pid_comm "$PID")"
STATE="$(app_cli --steam-client)"

assert_json "state is starting" "$STATE" 'd["state"]' "starting"
assert_json "the pid file was found" "$STATE" 'd["pidFileExists"]' "true"
assert_json "and the pid read back" "$STATE" 'd["pid"]' "$PID"
assert_json "the process name is checked, not just its existence" "$STATE" 'd["comm"]' "steam"
assert_json_contains "the reason explains what is still missing" "$STATE" \
    'd["detail"]' "launcher service"

# ---------------------------------------------------------------------------
part "d) a stale pid file"

# The pid file survives a shutdown, so a dead pid must not read as a live client.
stub_steam_pid_stop
sleep 0.5
STATE="$(app_cli --steam-client)"

assert_json "a dead pid reads as not running" "$STATE" 'd["state"]' "not-running"
assert_json_contains "and is called stale" "$STATE" 'd["detail"]' "stale"

# ---------------------------------------------------------------------------
part "e) a recycled pid"

# The guard that makes the above safe: a *live* process that is not Steam. In a
# container pids are recycled aggressively, so this is not a theoretical case.
stub_steam_pid_wrong_name "$LAB_APP_HOME/.steam/steam.pid"
PID="$LAB_FAKE_STEAM_PID"
info "fake non-steam pid $PID, comm=$(pid_comm "$PID")"
STATE="$(app_cli --steam-client)"

assert_json "a live process that is not steam reads as not running" "$STATE" \
    'd["state"]' "not-running"
assert_json "the pid is alive" "$STATE" 'd["pid"]' "$PID"
assert_ne "but its name is not steam" "steam" "$(json_get "$STATE" 'd["comm"]')"
assert_json_contains "and the reason says so" "$STATE" 'd["detail"]' "recycled"
stub_steam_pid_stop

# Garbage in the pid file is a third way for it to be unusable.
printf 'not a pid\n' >"$LAB_APP_HOME/.steam/steam.pid"
STATE="$(app_cli --steam-client)"
assert_json "an unparseable pid file reads as not running" "$STATE" 'd["state"]' "not-running"
assert_json_contains "and says the pid is unusable" "$STATE" 'd["detail"]' "usable pid"
rm -f "$LAB_APP_HOME/.steam/steam.pid"

# ---------------------------------------------------------------------------
part "f) ready"

# The Ready state is exactly "this D-Bus name is registered", so registering it
# is a complete stand-in — there is nothing else to fake.
if ! python3 -c 'import dbus, gi' 2>/dev/null; then
    skip "the client reports ready once the launcher service appears" \
         "python3-dbus and python3-gi are needed: sudo apt install python3-dbus python3-gi"
    skip "a registered name outranks everything else" "same"
else
    # A private bus of our own, so the developer's real session is untouched.
    READY="$(dbus-run-session -- bash -c "
        set -uo pipefail
        source '$LAB_SRC_DIR/lib/common.sh'
        source '$LAB_SRC_DIR/lib/app.sh'
        source '$LAB_SRC_DIR/lib/stubs.sh'
        CASE_OUT_DIR='$CASE_OUT_DIR'
        stub_steam_dbus || exit 90
        app_cli_with_dbus --steam-client
    " 2>"$(case_log dbus)")"
    RC=$?

    if (( RC == 90 )); then
        skip "the client reports ready once the launcher service appears" \
             "the D-Bus stub could not register the name — see $(case_log dbus)"
        skip "a registered name outranks everything else" "same"
    else
        assert_json "the client reports ready once the launcher service appears" \
            "$READY" 'd["state"]' "ready"
        assert_json "the name is seen on the bus" "$READY" 'd["dbusNameRegistered"]' "true"
        assert_json_contains "and the reason names it" "$READY" 'd["detail"]' "$DBUS_NAME"
    fi

    # With no name registered on that same private bus, it must fall back.
    NOTREADY="$(dbus-run-session -- bash -c "
        set -uo pipefail
        source '$LAB_SRC_DIR/lib/common.sh'
        source '$LAB_SRC_DIR/lib/app.sh'
        app_cli_with_dbus --steam-client
    " 2>/dev/null)"
    assert_json "a connected bus without the name is not ready" "$NOTREADY" \
        'd["dbusConnected"]' "true"
    assert_json "and reads as not running" "$NOTREADY" 'd["state"]' "not-running"
fi

# ---------------------------------------------------------------------------
part "g) the Flatpak variant asks flatpak instead"

# A pid written inside the sandbox means nothing on the host, so the Flatpak
# check shells out to `flatpak ps`. A stub decides the answer.
fx_reset
fx_steam_tree flatpak >/dev/null

stub_flatpak_ps no
STATE="$(app_cli --steam-client)"
assert_json "not listed by flatpak ps means not running" "$STATE" 'd["state"]' "not-running"
assert_json "the probe ran" "$STATE" 'd["flatpakProbeRan"]' "true"
assert_json "and did not find it" "$STATE" 'd["flatpakAppListed"]' "false"

stub_flatpak_ps yes
STATE="$(app_cli --steam-client)"
assert_json "listed by flatpak ps means starting" "$STATE" 'd["state"]' "starting"
assert_json "the app was found" "$STATE" 'd["flatpakAppListed"]' "true"

# A flatpak that never answers must not hang the app. There is a 3-second
# timeout for this; the wall clock is what proves it is honoured.
stub_flatpak_ps hang
START=$SECONDS
STATE="$(app_cli --steam-client)"
ELAPSED=$(( SECONDS - START ))
assert_json "an unresponsive flatpak reads as not running" "$STATE" 'd["state"]' "not-running"
if (( ELAPSED <= 10 )); then
    ok "and the app gives up rather than hanging (${ELAPSED}s)"
else
    fail "the app waited too long for flatpak" \
        "took ${ELAPSED}s; SteamClient.cpp:66 sets a 3 second timeout"
fi
stub_bin_clear

# ---------------------------------------------------------------------------
part "h) a launch is deferred rather than blocking"

# When Steam is not ready, GameRunner accepts the launch and polls on a timer
# instead of blocking. The timeouts are 60 s to give up and 10 s of grace before
# a merely-running client counts — so a regression that turns this back into a
# blocking wait shows up as a slow test rather than a wrong answer, and that is
# what the wall clock below is for.
fx_reset
NATIVE="$(fx_steam_tree native compat_tool=)"
fx_add_game "$NATIVE" 1245620 name="ELDEN RING" installdir="ELDEN RING" >/dev/null
stub_proton "$NATIVE" "proton-cachyos-11.0-20260703-slr-x86_64" >/dev/null

# No `steam` anywhere on PATH and no steam.sh in the Steam root: there is no way
# to start the client, so the launch is refused outright rather than deferred
# forever. Note this has to remove the program rather than shadow it with a
# failing one — startDetached reports success for anything it can spawn.
stub_steam_unstartable
RESULT="$(app_cli --launch 1245620 --timeout 5)"
assert_json "a launch with no way to start Steam is refused" "$RESULT" 'd["accepted"]' "false"
assert_json_contains "and says so" "$RESULT" 'd["error"]' "Steam"
unset LAB_APP_PATH

# Now make it startable. The launch is accepted immediately and reported as
# pending; the game follows once the client is deemed up.
stub_steam_startable "$LAB_APP_HOME/.steam/steam.pid"
stub_records_reset
START=$SECONDS
RESULT="$(app_cli --launch 1245620 --timeout 4)"
ELAPSED=$(( SECONDS - START ))

assert_json "the launch is accepted" "$RESULT" 'd["accepted"]' "true"
assert_json "and reported as pending" "$RESULT" 'd["pending"]' "true"
assert_file "Steam was asked to start" "$LAB_RUN_DIR/steam-start-args.txt"
assert_contains "silently, so it does not steal focus" \
    "$LAB_RUN_DIR/steam-start-args.txt" '\-silent'

if (( ELAPSED <= 8 )); then
    ok "acceptance does not block on the client (${ELAPSED}s)"
else
    fail "the launch blocked while waiting for Steam" \
        "took ${ELAPSED}s for a --timeout of 4; the wait is supposed to run on a
timer (GameRunner.cpp:406) so the caller returns straight away."
fi

# The game has not started yet — that is the whole point of "pending".
assert_false "the game has not been launched yet" stub_proton_was_run
stub_bin_clear
stub_steam_pid_stop

case_finish
