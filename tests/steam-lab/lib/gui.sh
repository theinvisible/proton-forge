# shellcheck shell=bash
#
# Driving the real ProtonForge on a virtual X server.
#
# Everything here exists because some of what the app does is only observable on
# a screen: whether the window fits, whether the game list actually filled,
# whether a modal dialog appears where one is expected. Xvfb plus a window
# manager gives us that with no desk involved.
#
# Four things were learned the hard way and are encoded below — change them and
# the GUI cases stop working:
#
#   * Qt6 prefers Wayland whenever WAYLAND_DISPLAY is set, and then ignores
#     DISPLAY entirely. Without unsetting it the windows open on the developer's
#     real desktop and nothing is found on Xvfb.
#   * DBUS_SESSION_BUS_ADDRESS has to go too, or the menu bar can be exported to
#     the desktop's global menu and the window has none. gui_app_start therefore
#     builds the environment with `env -i`.
#   * A window manager is not optional: without one nothing has input focus and
#     WM_NORMAL_HINTS is not enforced, so both the keyboard and the minimum-size
#     assertions would be meaningless.
#   * "xdotool getwindowgeometry" reports the wrong absolute position under a
#     reparenting window manager (off by the title bar height). Positions come
#     from "xwininfo -id" instead; sizes agree between the two.

LAB_GUI_DISPLAY=""
LAB_GUI_XVFB_PID=""
LAB_GUI_WM_PID=""
LAB_GUI_APP_PID=""

# LAB_GUI_PATH is the PATH the app sees, and therefore which nvidia-smi, lspci
# or flatpak it finds. Cases prepend their stub directory to it.
: "${LAB_GUI_PATH:=$LAB_STUB_BIN:/usr/bin:/bin}"

gui_require_tools() {
    local missing=() tool
    for tool in Xvfb "$LAB_GUI_WM" xdotool xwininfo xprop xdpyinfo; do
        have "$tool" || missing+=("$tool")
    done
    (( ${#missing[@]} )) && die "the GUI cases need: ${missing[*]}
    sudo apt install xvfb xdotool openbox x11-utils x11-apps"
    return 0
}

# gui_start_display [width] [height]
gui_start_display() {
    local w="${1:-$LAB_GUI_WIDTH}" h="${2:-$LAB_GUI_HEIGHT}" n
    gui_require_tools

    for n in $(seq 90 119); do
        [[ -e "/tmp/.X${n}-lock" ]] && continue
        LAB_GUI_DISPLAY=":$n"; break
    done
    [[ -n "$LAB_GUI_DISPLAY" ]] || die "no free X display between :90 and :119"

    Xvfb "$LAB_GUI_DISPLAY" -screen 0 "${w}x${h}x24" -nolisten tcp -ac \
        >"$CASE_OUT_DIR/xvfb.log" 2>&1 &
    LAB_GUI_XVFB_PID=$!

    local waited=0
    until gui_env xdpyinfo >/dev/null 2>&1; do
        (( waited >= 20 )) && die "Xvfb did not come up on $LAB_GUI_DISPLAY"
        sleep 0.5
        waited=$(( waited + 1 ))
    done

    gui_env "$LAB_GUI_WM" --sm-disable >"$CASE_OUT_DIR/wm.log" 2>&1 &
    LAB_GUI_WM_PID=$!
    sleep 1
    info "virtual screen ${w}x${h} on $LAB_GUI_DISPLAY, window manager $LAB_GUI_WM"
}

gui_stop_display() {
    gui_app_stop
    [[ -n "$LAB_GUI_WM_PID" ]] && kill "$LAB_GUI_WM_PID" 2>/dev/null
    [[ -n "$LAB_GUI_XVFB_PID" ]] && kill "$LAB_GUI_XVFB_PID" 2>/dev/null
    LAB_GUI_WM_PID=""; LAB_GUI_XVFB_PID=""
    return 0
}

# gui_env <command...> -- run an X client against the test display
gui_env() { env -u WAYLAND_DISPLAY "DISPLAY=$LAB_GUI_DISPLAY" "$@"; }
xd()  { gui_env xdotool "$@"; }
xwi() { gui_env xwininfo "$@"; }

# gui_app_start [extra args...]
#
# The environment is stripped down on purpose: no session bus, no Wayland, no
# desktop theme. PROTONFORGE_NO_STARTUP_CHECKS is set here because the startup
# check otherwise pops a modal dialog a second in, or reaches out to the GitHub
# API. The one case that wants that dialog calls gui_app_start_with_checks.
gui_app_start() {
    _gui_app_start_impl 1 "$@"
}

# gui_app_start_with_checks [extra args...] -- leaves the startup check enabled
gui_app_start_with_checks() {
    _gui_app_start_impl 0 "$@"
}

_gui_app_start_impl() {
    local suppress="$1"; shift
    local bin; bin="$(app_require_bin)"
    [[ -n "$LAB_GUI_DISPLAY" ]] || die "no virtual screen — call gui_start_display first"

    local -a envargs=(
        "PATH=$LAB_GUI_PATH"
        "HOME=$LAB_APP_HOME"
        "XDG_CONFIG_HOME=$LAB_APP_HOME/.config"
        "XDG_CACHE_HOME=$LAB_APP_HOME/.cache"
        "XDG_DATA_HOME=$LAB_APP_HOME/.local/share"
        "TMPDIR=$LAB_APP_TMP"
        "DISPLAY=$LAB_GUI_DISPLAY"
        QT_QPA_PLATFORM=xcb
    )
    (( suppress )) && envargs+=(PROTONFORGE_NO_STARTUP_CHECKS=1)

    env -i "${envargs[@]}" "$bin" "$@" \
        >"$CASE_OUT_DIR/gui-stdout.log" 2>&1 &
    LAB_GUI_APP_PID=$!

    if ! gui_wait_window '^ProtonForge' "$TIMEOUT_GUI" >/dev/null; then
        local windows; windows="$(gui_list_windows)"
        gui_app_stop
        err "the main window did not appear within ${TIMEOUT_GUI}s.
windows on screen:
${windows:-  (none)}
last lines of the app's output:
$(tail -n 15 "$CASE_OUT_DIR/gui-stdout.log" 2>/dev/null)"
        return 1
    fi
    info "GUI running (pid $LAB_GUI_APP_PID) on $LAB_GUI_DISPLAY"
    return 0
}

# gui_app_start_second -- a second instance sharing the same TMPDIR
#
# Exercises the QLockFile single-instance guard in main.cpp. Returns the pid; the
# caller decides what "correct" looks like, because what actually happens (a
# modal QMessageBox before the event loop, headless) is worth asserting rather
# than assuming.
gui_app_start_second() {
    local bin; bin="$(app_require_bin)"
    env -i "PATH=$LAB_GUI_PATH" "HOME=$LAB_APP_HOME" \
        "XDG_CONFIG_HOME=$LAB_APP_HOME/.config" \
        "XDG_CACHE_HOME=$LAB_APP_HOME/.cache" \
        "TMPDIR=$LAB_APP_TMP" \
        "DISPLAY=$LAB_GUI_DISPLAY" \
        QT_QPA_PLATFORM=xcb PROTONFORGE_NO_STARTUP_CHECKS=1 \
        "$bin" >"$CASE_OUT_DIR/gui-second.log" 2>&1 &
    printf '%s' "$!"
}

gui_app_stop() {
    [[ -z "$LAB_GUI_APP_PID" ]] && return 0
    kill "$LAB_GUI_APP_PID" 2>/dev/null
    local waited=0
    while pid_alive "$LAB_GUI_APP_PID"; do
        sleep 0.2
        waited=$(( waited + 1 ))
        if (( waited > 25 )); then
            warn "the GUI did not exit on SIGTERM, sending SIGKILL"
            kill -9 "$LAB_GUI_APP_PID" 2>/dev/null
            break
        fi
    done
    LAB_GUI_APP_PID=""
    return 0
}

gui_app_running() { pid_alive "$LAB_GUI_APP_PID"; }

# ------------------------------------------------------------ window handling

# gui_win <name regex> [min width] -> window id
#
# Name *and* a minimum width: Qt applications keep small helper windows around,
# and matching on the name alone finds those instead of the real one.
gui_win() {
    local re="$1" minw="${2:-300}" id w
    for id in $(xd search --onlyvisible --name "$re" 2>/dev/null); do
        w="$(xwi -id "$id" 2>/dev/null | sed -n 's/^ *Width: *//p')"
        [[ -n "$w" ]] && (( w >= minw )) && { printf '%s' "$id"; return 0; }
    done
    return 1
}

# gui_wait_window <name regex> [timeout] [min width] -> window id
gui_wait_window() {
    local re="$1" timeout="${2:-$TIMEOUT_GUI}" minw="${3:-300}" waited=0 id
    while ! id="$(gui_win "$re" "$minw")"; do
        if (( waited >= timeout )); then return 1; fi
        sleep 1
        waited=$(( waited + 1 ))
    done
    printf '%s' "$id"
    return 0
}

gui_wait_no_window() {
    local re="$1" timeout="${2:-$TIMEOUT_GUI}" minw="${3:-300}" waited=0
    while gui_win "$re" "$minw" >/dev/null; do
        if (( waited >= timeout )); then return 1; fi
        sleep 1
        waited=$(( waited + 1 ))
    done
    return 0
}

gui_win_size() {
    xwi -id "$1" 2>/dev/null \
        | sed -n 's/^ *Width: *\(.*\)/\1/p;s/^ *Height: *\(.*\)/\1/p' | paste -sd'x'
}

gui_win_pos() {
    xwi -id "$1" 2>/dev/null \
        | sed -n 's/.*Absolute upper-left X: *\(.*\)/\1/p;s/.*Absolute upper-left Y: *\(.*\)/\1/p' \
        | paste -sd' '
}

# gui_min_size <id> -> "WIDTHxHEIGHT" from WM_NORMAL_HINTS, empty if unset
gui_min_size() {
    gui_env xprop -id "$1" WM_NORMAL_HINTS 2>/dev/null \
        | sed -n 's/.*program specified minimum size: *\([0-9]*\) by \([0-9]*\).*/\1x\2/p'
}

gui_win_title() { xd getwindowname "$1" 2>/dev/null; }

gui_resize()   { xd windowsize "$1" "$2" "$3"; sleep 1.2; }
gui_activate() { xd windowactivate --sync "$1" 2>/dev/null; sleep 0.4; }

# Deliberately without --window: Qt ignores some synthetic events, so the
# focused window is driven instead, after making sure the right one has focus.
gui_key()  { xd key --clearmodifiers "$@"; sleep 0.4; }
gui_type() { xd type --delay 60 "$1"; sleep 0.4; }

# gui_click <id> <dx> <dy> -- click at an offset inside a window
gui_click() {
    local id="$1" dx="$2" dy="$3" x y
    read -r x y <<<"$(gui_win_pos "$id")"
    [[ -n "$x" && -n "$y" ]] || return 1
    xd mousemove $(( x + dx )) $(( y + dy )) click 1
    sleep 1
}

# gui_screenshot <name> -- raw X dump into the case output, for failures
gui_screenshot() {
    have xwd || return 0
    gui_env xwd -root -silent >"$CASE_OUT_DIR/$1.xwd" 2>/dev/null || true
    printf '%s/%s.xwd' "$CASE_OUT_DIR" "$1"
}

# gui_list_windows -- every named window, for diagnostics
gui_list_windows() {
    local id n
    for id in $(xd search --onlyvisible --name '.' 2>/dev/null); do
        n="$(xd getwindowname "$id" 2>/dev/null)"
        [[ -n "$n" ]] && printf "  '%s' %s\n" "$n" "$(gui_win_size "$id")"
    done
}

# gui_fits_screen <id> -- the window is not larger than the virtual screen
gui_fits_screen() {
    local size w h
    size="$(gui_win_size "$1")"
    w="${size%x*}"; h="${size#*x}"
    [[ -n "$w" && -n "$h" ]] || return 1
    (( w <= LAB_GUI_WIDTH && h <= LAB_GUI_HEIGHT ))
}
