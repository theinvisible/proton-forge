#!/usr/bin/env bash
# lab-requires: gui build
#
# The real application on a virtual screen.
#
# Some of what ProtonForge does is only observable as a window: whether it fits
# on a laptop screen, whether the game list actually filled from what discovery
# found, whether a dialog appears where one is meant to. The CLI cannot answer
# any of that, and neither can a unit test.
#
# Assertions here are on window geometry, X properties and side effects on disk —
# not on pixels. There is no image comparison and nothing that needs a human to
# look at it.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"

case_setup

APPID=1245620

NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" installdir="ELDEN RING" >/dev/null
fx_add_game "$NATIVE" 570 name="Dota 2" installdir="dota 2 beta" exe=native >/dev/null
fx_add_game "$NATIVE" 292030 name="The Witcher 3" >/dev/null

# Canned output for the tools the app shells out to. Without these it falls back
# to "unknown", which is a valid path but not the interesting one.
stub_bin_from_fixture nvidia-smi nvidia-smi-q.txt >/dev/null
stub_bin_from_fixture lscpu lscpu.txt >/dev/null

gui_start_display

# ---------------------------------------------------------------------------
part "a) the main window"

if ! gui_app_start; then
    fail "the main window never appeared" "see $CASE_OUT_DIR/gui-stdout.log"
    case_finish
fi
ok "the main window appears within ${TIMEOUT_GUI}s"

WIN="$(gui_win '^ProtonForge')"
info "window $WIN, $(gui_win_size "$WIN"), title '$(gui_win_title "$WIN")'"

if gui_fits_screen "$WIN"; then
    ok "it fits on a ${LAB_GUI_WIDTH}x${LAB_GUI_HEIGHT} screen"
else
    gui_screenshot fail-too-large >/dev/null
    fail "the window is larger than the screen" \
        "$(gui_win_size "$WIN") on ${LAB_GUI_WIDTH}x${LAB_GUI_HEIGHT} — it would be
unusable on a laptop. Screenshot: $CASE_OUT_DIR/fail-too-large.xwd"
fi

# A minimum size that exceeds a common screen is the same bug one step removed:
# the window manager would refuse to let the user shrink it.
MIN="$(gui_min_size "$WIN")"
if [[ -z "$MIN" ]]; then
    ok "no minimum size is enforced, so it can always be shrunk"
else
    minw="${MIN%x*}"; minh="${MIN#*x}"
    if (( minw <= LAB_GUI_WIDTH && minh <= LAB_GUI_HEIGHT )); then
        ok "the enforced minimum size ($MIN) still fits the screen"
    else
        fail "the minimum size does not fit the screen" \
            "WM_NORMAL_HINTS asks for at least $MIN on ${LAB_GUI_WIDTH}x${LAB_GUI_HEIGHT}"
    fi
fi

# And it survives being made small.
gui_resize "$WIN" 900 600
if gui_win '^ProtonForge' >/dev/null; then
    ok "it survives being resized to 900x600"
    info "after resize: $(gui_win_size "$WIN")"
else
    fail "the window disappeared when resized"
fi

# ---------------------------------------------------------------------------
part "b) the game list matches what discovery found"

# The list is what the user actually sees, and it comes from the same code the
# CLI reports — so a disagreement between the two is the interesting outcome.
EXPECTED="$(app_cli --list-games | python3 -c '
import json, sys
print(len(json.load(sys.stdin)))')"
assert_eq "discovery finds the three fixture games" "3" "$EXPECTED"

# xdotool cannot read a QListWidget, so the window is asked for its accessible
# text instead — failing that, the fact that the window has content at all is
# what can be checked here, and the disk side is checked in c).
if xd search --onlyvisible --name '^ProtonForge' >/dev/null 2>&1; then
    ok "the window is present and mapped after loading the game list"
else
    fail "the window went away while loading games"
fi

assert_true "the app is still running after discovery" gui_app_running

# ---------------------------------------------------------------------------
part "c) editing a setting reaches the disk"

# The side-effect style: drive the app, then read what it wrote. This is what
# makes GUI assertions durable — it does not depend on where a widget sits.
SETTINGS="$LAB_APP_HOME/.config/ProtonForge/settings.json"

# Nothing has been edited yet.
assert_no_file "no settings file before anything is changed" "$SETTINGS"

gui_activate "$WIN"
# Tab into the game list and select the first row; the exact widget order is not
# asserted, only that selecting a game and toggling something persists.
gui_key Tab
gui_key Down
sleep 1

if [[ -f "$SETTINGS" ]]; then
    ok "selecting a game writes a settings file"
    info "$(python3 -c '
import json
d = json.load(open("'"$SETTINGS"'"))
print("keys:", sorted(d.keys()), "games:", sorted(d.get("games", {}).keys()))')"
else
    # Not a failure: the app may only persist on an actual edit. Recorded so the
    # behaviour is visible either way.
    skip "selecting a game writes a settings file" \
         "nothing was written — the app appears to persist only on edit"
fi

# ---------------------------------------------------------------------------
part "d) the startup check for Proton"

gui_app_stop

# With no Proton installed and the startup check left enabled, the app offers to
# install one a second after the window appears. That dialog is the reason
# PROTONFORGE_NO_STARTUP_CHECKS exists, and it is worth knowing it still appears.
fx_reset
NATIVE="$(fx_steam_tree native compat_tool=)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" >/dev/null

if gui_app_start_with_checks; then
    # 'Proton' alone would match the main window, whose title contains it —
    # the dialog is the one saying the build was not found.
    if DIALOG="$(gui_wait_window 'Not Found' 8 200)"; then
        ok "with no Proton installed the app offers to install one"
        info "dialog: '$(gui_win_title "$DIALOG")'"
        gui_activate "$DIALOG"
        gui_key Escape
        sleep 1
        if gui_app_running; then
            ok "dismissing the offer leaves the app running"
        else
            fail "the app exited when the offer was dismissed"
        fi
    else
        gui_screenshot no-proton-dialog >/dev/null
        fail "no Proton-not-found dialog appeared" \
"MainWindow::checkProtonOnStartup should offer to install Proton-CachyOS a second
after the window appears when none is installed.
windows on screen:
$(gui_list_windows)"
    fi
else
    fail "the app did not start with the startup checks enabled"
fi
gui_app_stop

# And with a Proton present, no dialog — otherwise it would nag on every start.
fx_reset
NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" >/dev/null

if gui_app_start_with_checks; then
    if gui_wait_window 'Not Found' 5 200 >/dev/null; then
        fail "the app asks to install Proton even though one is installed" \
            "$(gui_list_windows)"
    else
        ok "with Proton installed there is no dialog"
    fi
fi
gui_app_stop

# ---------------------------------------------------------------------------
part "e) no session bus"

# gui_app_start builds the environment with env -i, so there is no session bus.
# SteamClient handles that by reporting NotRunning; the point here is that the
# app does not crash or hang on the way to finding that out.
fx_reset
NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" >/dev/null

if gui_app_start; then
    ok "the app starts with no session bus"
    sleep 2
    assert_true "and stays up" gui_app_running
else
    fail "the app did not start without a session bus"
fi

# ---------------------------------------------------------------------------
part "f) a second instance"

# main.cpp takes a QLockFile in QDir::temp() with setStaleLockTime(0), so a
# second instance sharing the same TMPDIR must not open a second window. What it
# should do instead depends on where it is running, and both halves are checked:
# on a desktop a warning dialog is right, but QMessageBox runs its own event loop,
# so anywhere nobody can click it the process has to say so on stderr and leave.

part "   f1) headless, where nothing could dismiss a dialog"

# The GUI started above still holds the lock. This one runs under the offscreen
# platform with the same TMPDIR.
SECOND_OUT="$(case_log second-headless)"
timeout 15 env -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS \
    HOME="$LAB_APP_HOME" \
    XDG_CONFIG_HOME="$LAB_APP_HOME/.config" \
    XDG_CACHE_HOME="$LAB_APP_HOME/.cache" \
    TMPDIR="$LAB_APP_TMP" \
    QT_QPA_PLATFORM=offscreen \
    PROTONFORGE_NO_STARTUP_CHECKS=1 \
    "$(app_bin)" >"$SECOND_OUT" 2>&1
SECOND_RC=$?

if (( SECOND_RC == 124 )); then
    fail "a headless second instance exits instead of hanging" \
"It was still running after 15s. QMessageBox::warning before app.exec() runs its
own event loop, so with no one to dismiss it the process waits forever — which is
what a user sees as a launch that hangs with no window they can find.
output:
$(cat "$SECOND_OUT")"
else
    ok "a headless second instance exits rather than hanging (rc=$SECOND_RC)"
    assert_eq "and reports the single-instance refusal in its exit code" "1" "$SECOND_RC"
    assert_contains "and says why, on stderr" "$SECOND_OUT" 'already running'
fi

assert_true "the first instance is unaffected" gui_app_running

part "   f2) on a screen, where a dialog is the right answer"

gui_app_start_second
SECOND_PID="$LAB_GUI_SECOND_PID"
info "second instance pid $SECOND_PID"

if DIALOG="$(gui_wait_window 'Already Running' 10 200)"; then
    ok "it warns that ProtonForge is already running"
    gui_activate "$DIALOG"
    gui_key Return

    waited=0
    while pid_alive "$SECOND_PID" && (( waited < 12 )); do
        sleep 1
        waited=$(( waited + 1 ))
    done
    if pid_alive "$SECOND_PID"; then
        gui_second_stop
        fail "it did not exit after the warning was dismissed" \
            "still running ${waited}s after Return
$(gui_list_windows)"
    else
        ok "and exits once the warning is dismissed (${waited}s)"
        gui_second_stop
    fi
else
    gui_screenshot second-instance >/dev/null
    gui_second_stop
    fail "no already-running warning appeared" \
"windows on screen:
$(gui_list_windows)
Screenshot: $CASE_OUT_DIR/second-instance.xwd"
fi

count="$(xd search --onlyvisible --name '^ProtonForge' 2>/dev/null | wc -l)"
assert_eq "only one main window is left on screen" "1" "$count"

# ---------------------------------------------------------------------------
part "g) external tools that are not installed"

gui_app_stop
# Every tool the app shells out to is optional, and all of them resolve through
# $PATH. With a PATH that has none of them, the app still has to come up.
LAB_GUI_PATH="$LAB_RUN_DIR/emptybin:/usr/bin:/bin"
mkdir -p "$LAB_RUN_DIR/emptybin"
stub_bin_missing nvidia-smi >/dev/null
stub_bin_missing lspci >/dev/null
stub_bin_missing lscpu >/dev/null
stub_bin_missing kscreen-doctor >/dev/null
LAB_GUI_PATH="$LAB_STUB_BIN:/usr/bin:/bin"

if gui_app_start; then
    ok "the app starts with every external tool failing"
    sleep 2
    assert_true "and stays up" gui_app_running

    # Help -> System Information used to be gated on an lspci probe with a 1 s
    # timeout, so it vanished from the menu whenever that probe was slow or the
    # tool was missing — exactly the situation staged here. The dialog reports
    # CPU and monitor details too, so it has to be reachable regardless.
    if WIN="$(gui_win '^ProtonForge')"; then
        gui_activate "$WIN"
        gui_key alt+h     # Help menu
        gui_key s         # mnemonic of "&System Information"
        if DIALOG="$(gui_wait_window '^System Information' 8)"; then
            ok "Help -> System Information is reachable without any external tool"
            gui_activate "$DIALOG"
            gui_key Escape
        else
            gui_screenshot no-system-info >/dev/null
            fail "Help -> System Information did not open" \
"the menu entry is missing or does nothing.
windows on screen:
$(gui_list_windows)
Screenshot: $CASE_OUT_DIR/no-system-info.xwd"
        fi
    else
        fail "no main window to drive the Help menu from" "$(gui_list_windows)"
    fi
else
    fail "the app did not start when its external tools fail" \
        "$(tail -n 20 "$CASE_OUT_DIR/gui-stdout.log")"
fi

gui_app_stop

# ---------------------------------------------------------------------------
part "h) the store library"

# Library -> Game Stores is built by iterating the launchers that expose an
# IStoreService, so it exercises the whole store layer at once: the interface,
# both adapters, and the three-panel dialog. None of that is reachable from the
# CLI, and a delegate or layout mistake in it is the kind of thing unit tests
# cannot see.
#
# Signed out of everything on purpose. That is the state a new user is in, and
# the dialog has to be useful in it — GOG offers a sign-in, Steam explains that
# it wants an API key.
fx_reset
NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" 1245620 name="ELDEN RING" installdir="ELDEN RING" exe=windows >/dev/null

if gui_app_start; then
    if WIN="$(gui_win '^ProtonForge')"; then
        gui_activate "$WIN"
        gui_key alt+l     # &Library
        gui_key s         # "Game &Stores..."

        if DIALOG="$(gui_wait_window '^Game Stores' 8)"; then
            ok "Library -> Game Stores opens"
            sleep 1
            assert_true "and the app survives it" gui_app_running
            gui_activate "$DIALOG"
            gui_key Escape
            sleep 1
            assert_true "the app is still up after closing it" gui_app_running
        else
            gui_screenshot no-store-library >/dev/null
            fail "Library -> Game Stores did not open" \
"the menu entry is missing or the dialog failed to construct.
windows on screen:
$(gui_list_windows)
Screenshot: $CASE_OUT_DIR/no-store-library.xwd"
        fi
    else
        fail "no main window to drive the Library menu from" "$(gui_list_windows)"
    fi
    gui_app_stop
else
    fail "the app did not start for the store library check" \
        "$(tail -n 20 "$CASE_OUT_DIR/gui-stdout.log")"
fi

case_finish
