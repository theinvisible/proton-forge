# shellcheck shell=bash
#
# Faking the things around Steam, so a test run needs no Steam account.
#
# ProtonForge reaches outside itself in exactly two ways: it runs programs found
# on $PATH, and it looks at files and D-Bus names. Both are cheap to stand in
# for, and standing in for them is what lets the container tier cover
# SteamClient's whole state machine and GameRunner's entire launch chain without
# a client, an account, a GPU or a game.
#
# Nothing here patches the app. Every stub satisfies the same check the real
# thing would, which is the only way a stub is worth having:
#
#   * the fake steam.pid points at a process whose /proc/<pid>/comm really is
#     "steam", because that is what SteamClient.cpp:57 compares against;
#   * the fake "ready" signal is the actual D-Bus name being actually
#     registered, because that is the whole of the Ready state;
#   * the fake proton is an executable file called `proton`, which is the only
#     thing GameRunner ever requires of one.

# stub_bin <name>  (script on stdin)
#
# Writes an executable into $LAB_STUB_BIN, which app_env() and gui_app_start()
# prepend to $PATH. This is the hook for nvidia-smi, lspci, lscpu,
# kscreen-doctor, gsettings, which, flatpak and steam — every QProcess::start in
# the codebase resolves through $PATH.
stub_bin() {
    local name="$1"
    mkdir -p "$LAB_STUB_BIN"
    cat >"$LAB_STUB_BIN/$name"
    chmod +x "$LAB_STUB_BIN/$name"
    printf '%s' "$LAB_STUB_BIN/$name"
}

# stub_bin_from_fixture <name> <fixture file> [args...]
#
# The common case: a program whose entire contribution is text on stdout.
# Captured real-world output lives in tests/steam-lab/fixtures/.
stub_bin_from_fixture() {
    local name="$1" fixture="$2"
    [[ -f "$LAB_STUB_DATA/$fixture" ]] \
        || die "stub_bin_from_fixture: no fixture $LAB_STUB_DATA/$fixture"
    stub_bin "$name" <<EOF
#!/bin/sh
cat "$LAB_STUB_DATA/$fixture"
EOF
}

# stub_bin_missing <name>
#
# Shadow a program with one that always fails, to exercise the "tool not
# installed" path on a machine that has it.
stub_bin_missing() {
    stub_bin "$1" <<'EOF'
#!/bin/sh
exit 127
EOF
}

stub_bin_clear() {
    rm -rf "$LAB_STUB_BIN"
    mkdir -p "$LAB_STUB_BIN"
}

# ------------------------------------------------------- a running "Steam"

LAB_FAKE_STEAM_PID=""
# Every fake process started here, so teardown cleans up all of them rather than
# just the most recent one.
LAB_STUB_PIDS=()

# _stub_long_running <name> -> the pid of a live process whose comm is <name>
#
# /proc/<pid>/comm comes from the name of the executable that was exec'd, so a
# process with a chosen comm needs a *copy* of a binary under that name. Two
# things rule out the obvious candidates:
#
#   * a symlink does not work — the kernel resolves it and reports the target's
#     name instead;
#   * `sleep` and friends do not work either, because current Ubuntu ships
#     coreutils as a single multi-call binary (uutils) that dispatches on
#     argv[0] and exits with "unknown program 'steam'".
#
# bash does not dispatch on argv[0] (beyond the sh/rbash compatibility names),
# so a copy of it under any name runs normally. The loop body is deliberate: with
# a single simple command, `bash -c` execs it and the process would lose the name
# again.
_stub_long_running() {
    local name="$1"
    mkdir -p "$LAB_RUN_DIR"

    local fake="$LAB_RUN_DIR/$name"
    cp -f "$(command -v bash)" "$fake" || return 1
    chmod +x "$fake"

    # stdout and stderr have to go nowhere. A background process that inherits
    # them keeps the case's own pipeline open, so `steamlab test | tail` would
    # never see EOF and the run would look like a hang.
    "$fake" -c 'while :; do sleep 1; done' >/dev/null 2>&1 &
    local pid=$!
    disown 2>/dev/null || true
    LAB_STUB_PIDS+=("$pid")

    # Wait for the kernel to have the name; the first read can otherwise race
    # with the exec.
    local waited=0
    while [[ "$(pid_comm "$pid")" != "$name" ]]; do
        if ! pid_alive "$pid"; then
            err "the fake '$name' process died immediately"
            return 1
        fi
        sleep 0.1
        waited=$(( waited + 1 ))
        if (( waited > 50 )); then
            err "the fake '$name' process never reported comm=$name (got '$(pid_comm "$pid")')"
            return 1
        fi
    done

    printf '%s' "$pid"
}

# stub_steam_pid <pid file path> -> the pid
#
# SteamClient::isRunningNative() reads the pid file and then insists that
# /proc/<pid>/comm is exactly "steam" — the guard that stops a stale or recycled
# pid reading as a live client. This satisfies it the same way the real client
# does, rather than working around it.
stub_steam_pid() {
    local pidfile="$1"
    mkdir -p "$(dirname "$pidfile")"

    LAB_FAKE_STEAM_PID="$(_stub_long_running steam)" || return 1
    printf '%s\n' "$LAB_FAKE_STEAM_PID" >"$pidfile"
    printf '%s' "$LAB_FAKE_STEAM_PID"
}

# stub_steam_pid_wrong_name <pid file path> -> the pid
#
# The counter-example: a live process that is not named steam. The app must
# still say not-running, or a recycled pid would read as a running client.
stub_steam_pid_wrong_name() {
    local pidfile="$1"
    mkdir -p "$(dirname "$pidfile")"

    LAB_FAKE_STEAM_PID="$(_stub_long_running notsteam)" || return 1
    printf '%s\n' "$LAB_FAKE_STEAM_PID" >"$pidfile"
    printf '%s' "$LAB_FAKE_STEAM_PID"
}

stub_steam_pid_stop() {
    local pid
    for pid in "${LAB_STUB_PIDS[@]+"${LAB_STUB_PIDS[@]}"}"; do
        kill "$pid" 2>/dev/null
    done
    LAB_STUB_PIDS=()
    LAB_FAKE_STEAM_PID=""

    # The startable stub spawns its own copy from inside the app's process tree,
    # which this shell never saw a pid for. The same sweep also catches anything
    # a killed-mid-run case left behind.
    pkill -f "$LAB_RUN_DIR/steam -c while" 2>/dev/null
    pkill -f "$LAB_RUN_DIR/notsteam -c while" 2>/dev/null
    return 0
}

# stub_steam_startable <pid file path>
#
# A `steam` on $PATH so SteamClient::start() succeeds. It records the arguments
# it was given and leaves behind a live process with comm=steam plus a pid file,
# so the deferred-launch path can actually progress instead of timing out.
stub_steam_startable() {
    local pidfile="$1"
    stub_bin steam <<EOF
#!/bin/bash
# Stands in for the Steam client: records that it was asked to start, then
# behaves like a client that is up but has not registered its launcher service
# yet. See _stub_long_running for why this is a copy of bash.
printf '%s\n' "\$@" >>"$LAB_RUN_DIR/steam-start-args.txt"
cp -f "\$(command -v bash)" "$LAB_RUN_DIR/steam-client" 2>/dev/null
mv -f "$LAB_RUN_DIR/steam-client" "$LAB_RUN_DIR/steam" 2>/dev/null
# Every inherited descriptor is closed off: this is started from inside the app,
# whose stdout the case is capturing, and anything holding that open would look
# like the app never exiting. No setsid, so \$! really is this process's pid and
# /proc/<pid>/comm is 'steam' as the pid file claims.
"$LAB_RUN_DIR/steam" -c 'while :; do sleep 1; done' </dev/null >/dev/null 2>&1 &
printf '%s\n' "\$!" >"$pidfile"
exit 0
EOF
}

# stub_steam_unstartable
#
# The opposite: no way to start Steam at all. QProcess::startDetached only fails
# when the program cannot be spawned — a `steam` that exists and exits 127 still
# counts as started — so this has to remove it from $PATH rather than shadow it.
# Cases pair it with LAB_APP_PATH.
stub_steam_unstartable() {
    LAB_APP_PATH="$LAB_STUB_BIN:$LAB_RUN_DIR/emptybin"
    mkdir -p "$LAB_RUN_DIR/emptybin"
    rm -f "$LAB_STUB_BIN/steam"
    export LAB_APP_PATH
}

# ------------------------------------------------- the "ready" D-Bus name

LAB_DBUS_STUB_PID=""

# stub_steam_dbus
#
# Registers com.steampowered.PressureVessel.LaunchAlongsideSteam on the session
# bus. That name being present *is* SteamClient's Ready state — nothing else is
# checked — so owning it is a complete and honest stand-in for a client that has
# finished starting up.
#
# Requires a session bus. Cases that need this run under dbus-run-session.
stub_steam_dbus() {
    have python3 || { err "python3 is required for the D-Bus stub"; return 1; }
    python3 -c 'import dbus' 2>/dev/null \
        || { skip_reason="python3-dbus is not installed"; return 1; }

    python3 - >"$LAB_RUN_DIR/dbus-stub.log" 2>&1 <<'PY' &
import dbus, dbus.service, dbus.mainloop.glib, signal, sys
from gi.repository import GLib

NAME = "com.steampowered.PressureVessel.LaunchAlongsideSteam"

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
# Claiming the name is the entire point; nothing needs to be served on it.
name = dbus.service.BusName(NAME, bus=bus, do_not_queue=True)
print("registered", NAME, flush=True)
loop = GLib.MainLoop()
signal.signal(signal.SIGTERM, lambda *_: loop.quit())
loop.run()
PY
    LAB_DBUS_STUB_PID=$!
    disown 2>/dev/null || true

    local waited=0
    while ! grep -q '^registered' "$LAB_RUN_DIR/dbus-stub.log" 2>/dev/null; do
        sleep 0.2
        waited=$(( waited + 1 ))
        if (( waited > 50 )); then
            err "the D-Bus stub never registered the name:
$(cat "$LAB_RUN_DIR/dbus-stub.log" 2>/dev/null)"
            return 1
        fi
    done
    return 0
}

stub_steam_dbus_stop() {
    [[ -n "$LAB_DBUS_STUB_PID" ]] && kill "$LAB_DBUS_STUB_PID" 2>/dev/null
    LAB_DBUS_STUB_PID=""
    return 0
}

# ------------------------------------------------------- flatpak Steam

# stub_flatpak_ps <yes|no>
#
# The Flatpak half of the liveness check runs `flatpak ps --columns=application`
# and greps for com.valvesoftware.Steam (SteamClient.cpp:65). A stub decides the
# answer, and also lets us prove the 3-second timeout is honoured.
stub_flatpak_ps() {
    local listed="$1"
    if [[ "$listed" == "yes" ]]; then
        stub_bin flatpak <<'EOF'
#!/bin/sh
case "$1" in
ps) printf 'com.valvesoftware.Steam\norg.something.Else\n' ;;
*)  exit 0 ;;
esac
EOF
    elif [[ "$listed" == "hang" ]]; then
        stub_bin flatpak <<'EOF'
#!/bin/sh
# Never answers: the app has to give up after its own timeout.
sleep 30
EOF
    else
        stub_bin flatpak <<'EOF'
#!/bin/sh
case "$1" in
ps) printf 'org.something.Else\n' ;;
*)  exit 0 ;;
esac
EOF
    fi
}

# ------------------------------------------------- a recording Proton

LAB_PROTON_RECORD="$LAB_RUN_DIR/proton-invocation.txt"
LAB_RUNTIME_RECORD="$LAB_RUN_DIR/runtime-invocation.txt"

# stub_proton <root> <name> [require_tool_appid] -> the tool directory
#
# Like fx_compat_tool, but the `proton` script records how it was called: its
# argv, one line per argument, and the STEAM_COMPAT_* environment it received.
# That recording is the assertion surface for the whole launch path — it is what
# lets a test state exactly what Steam's compat tool chain was composed into,
# without a game existing.
stub_proton() {
    local root="$1" name="$2" requireToolAppId="${3:-}"
    local dir="$root/compatibilitytools.d/$name"
    mkdir -p "$dir"

    cat >"$dir/proton" <<EOF
#!/bin/sh
{
    printf 'PROGRAM\t%s\n' "\$0"
    for arg in "\$@"; do printf 'ARG\t%s\n' "\$arg"; done
    printf 'CWD\t%s\n' "\$PWD"
    env | grep -E '^(STEAM_|Steam|LD_PRELOAD=|DISPLAY=|PROTON_|DXVK_|VKD3D_|NGX_|NVPRESENT_|MANGOHUD=|__NV_|__GLX_|__VK_)' \\
        | sed 's/^/ENV\t/'
} >>"$LAB_PROTON_RECORD"
exit 0
EOF
    chmod +x "$dir/proton"

    if [[ -n "$requireToolAppId" ]]; then
        cat >"$dir/toolmanifest.vdf" <<EOF
"manifest"
{
	"version"		"2"
	"commandline"		"/proton waitforexitandrun"
	"require_tool_appid"		"${requireToolAppId}"
}
EOF
    else
        cat >"$dir/toolmanifest.vdf" <<'EOF'
"manifest"
{
	"version"		"2"
	"commandline"		"/proton run"
}
EOF
    fi

    printf '%s' "$dir"
}

# stub_runtime <library root> <tool appid> <installdir> -> the runtime directory
#
# The Steam Linux Runtime half. GameRunner::findToolByAppId() wants an
# appmanifest_<appid>.acf naming an installdir, and an *executable*
# common/<installdir>/_v2-entry-point (GameRunner.cpp:239) — that is all. The
# entry point records its argv too, so a case can assert the wrapping.
stub_runtime() {
    local library="$1" appid="$2" installdir="$3"
    local steamapps="$library/steamapps"
    [[ -d "$steamapps" ]] || steamapps="$library"

    mkdir -p "$steamapps/common/$installdir"

    cat >"$steamapps/appmanifest_${appid}.acf" <<EOF
"AppState"
{
	"appid"		"${appid}"
	"name"		"Steam Linux Runtime"
	"StateFlags"		"4"
	"installdir"		"${installdir}"
	"SizeOnDisk"		"1000000"
	"buildid"		"99999"
}
EOF

    local entry="$steamapps/common/$installdir/_v2-entry-point"
    cat >"$entry" <<EOF
#!/bin/sh
{
    printf 'PROGRAM\t%s\n' "\$0"
    for arg in "\$@"; do printf 'ARG\t%s\n' "\$arg"; done
} >>"$LAB_RUNTIME_RECORD"

# Behave like the real entry point: everything after the "--" separator is the
# command to run inside the container, so run it.
seen_sep=0
cmd=""
for arg in "\$@"; do
    if [ "\$seen_sep" = 1 ]; then
        cmd="\$cmd \"\$arg\""
    elif [ "\$arg" = "--" ]; then
        seen_sep=1
    fi
done
[ -n "\$cmd" ] && eval "\$cmd"
exit 0
EOF
    chmod +x "$entry"

    printf '%s' "$steamapps/common/$installdir"
}

stub_records_reset() {
    mkdir -p "$LAB_RUN_DIR"
    : >"$LAB_PROTON_RECORD"
    : >"$LAB_RUNTIME_RECORD"
    rm -f "$LAB_RUN_DIR/steam-start-args.txt"
}

# stub_proton_args -> the recorded argv, one per line
stub_proton_args() {
    [[ -f "$LAB_PROTON_RECORD" ]] && sed -n 's/^ARG\t//p' "$LAB_PROTON_RECORD"
}

# stub_runtime_args -> the recorded argv of the runtime entry point
stub_runtime_args() {
    [[ -f "$LAB_RUNTIME_RECORD" ]] && sed -n 's/^ARG\t//p' "$LAB_RUNTIME_RECORD"
}

# stub_proton_env <VAR> -> the value the game process actually received
stub_proton_env() {
    [[ -f "$LAB_PROTON_RECORD" ]] || return 1
    sed -n "s/^ENV\t$1=//p" "$LAB_PROTON_RECORD" | tail -n1
}

stub_proton_was_run() {
    [[ -s "$LAB_PROTON_RECORD" ]]
}

stub_runtime_was_run() {
    [[ -s "$LAB_RUNTIME_RECORD" ]]
}
