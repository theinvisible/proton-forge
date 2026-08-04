# shellcheck shell=bash
#
# Driving the binary under test.
#
# Two things make this workable, and both are worth knowing before changing
# anything here:
#
#   * $HOME is the only path lever the app has. SteamPaths derives every Steam
#     path from QDir::homePath(), and the config and cache directories come from
#     QStandardPaths, which follows XDG_*. Point all of those at $LAB_APP_HOME
#     and the app lives in a sandbox of our making, with the real ~/.steam and
#     ~/.config/ProtonForge untouched.
#
#   * $TMPDIR has to be per-case. main.cpp takes a QLockFile in QDir::temp()
#     with setStaleLockTime(0), so two runs sharing a $TMPDIR collide — and the
#     losing one pops a modal QMessageBox *before* the event loop starts, which
#     headless means it hangs rather than exits. Every case gets its own.

# app_bin -> path of the binary to test, or nothing
app_bin() {
    if [[ -n "$PF_BIN" ]]; then printf '%s' "$PF_BIN"; return 0; fi
    local candidates=(
        "$REPO_ROOT/cmake-build-debug/ProtonForge"
        "$REPO_ROOT/cmake-build-release/ProtonForge"
        "/usr/bin/protonforge"
    )
    local c
    for c in "${candidates[@]}"; do
        [[ -x "$c" ]] && { printf '%s' "$c"; return 0; }
    done
    return 1
}

app_require_bin() {
    local bin
    bin="$(app_bin)" || die "no ProtonForge binary found. Build one with:
    cmake -S '$REPO_ROOT' -B '$REPO_ROOT/cmake-build-debug' -DCMAKE_BUILD_TYPE=Debug
    cmake --build '$REPO_ROOT/cmake-build-debug' -j\$(nproc)
or point PF_BIN at an existing one (e.g. /usr/bin/protonforge after installing the .deb)."
    printf '%s' "$bin"
}

# app_build [--tests] -- configure and build if there is nothing to test yet
app_build() {
    local with_tests=0
    [[ "${1:-}" == "--tests" ]] && with_tests=1

    local args=(-DCMAKE_BUILD_TYPE=Debug)
    (( with_tests )) && args+=(-DPROTONFORGE_BUILD_TESTS=ON)

    step "Building ProtonForge"
    cmake -S "$REPO_ROOT" -B "$REPO_ROOT/cmake-build-debug" "${args[@]}" \
        >"$LAB_OUT_DIR/cmake-configure.log" 2>&1 \
        || die "cmake configure failed:
$(tail -n 20 "$LAB_OUT_DIR/cmake-configure.log")"
    cmake --build "$REPO_ROOT/cmake-build-debug" -j"$(nproc)" \
        >"$LAB_OUT_DIR/cmake-build.log" 2>&1 \
        || die "build failed:
$(tail -n 30 "$LAB_OUT_DIR/cmake-build.log")"
    info "built $(app_bin)"
}

# app_home_init -- create the fake $HOME skeleton
app_home_init() {
    mkdir -p "$LAB_APP_HOME" \
             "$LAB_APP_HOME/.config" \
             "$LAB_APP_HOME/.cache" \
             "$LAB_APP_HOME/.local/share" \
             "$LAB_APP_TMP" \
             "$LAB_STUB_BIN"
}

# app_home_reset -- wipe it and start over
app_home_reset() {
    rm -rf "$LAB_APP_HOME" "$LAB_APP_TMP" "$LAB_STUB_BIN"
    app_home_init
}

# app_env <command...> -- run something in the app's sandbox
#
# WAYLAND_DISPLAY has to go: Qt6 prefers the Wayland plugin whenever it is set
# and then ignores QT_QPA_PLATFORM's offscreen request, which on a developer's
# desktop means real windows appear.
app_env() {
    env -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS \
        HOME="$LAB_APP_HOME" \
        XDG_CONFIG_HOME="$LAB_APP_HOME/.config" \
        XDG_CACHE_HOME="$LAB_APP_HOME/.cache" \
        XDG_DATA_HOME="$LAB_APP_HOME/.local/share" \
        TMPDIR="$LAB_APP_TMP" \
        PATH="$LAB_STUB_BIN:$PATH" \
        QT_QPA_PLATFORM=offscreen \
        PROTONFORGE_NO_STARTUP_CHECKS=1 \
        "$@"
}

# app_cli <args...> -- run a CLI command, stdout captured, stderr to the case log
#
# Read the exit code with app_rc, not $?.
#
# Cases almost always want both the output and the exit code, and the output has
# to come back through a command substitution — which is a subshell, so a
# variable set inside app_cli would never reach the caller. The code is written
# to a file instead, which does survive:
#
#     INFO="$(app_cli --steam-info)"
#     assert_eq "exit code" 3 "$(app_rc)"
app_cli() {
    _app_run "with-dbus=no" "$@"
}

# app_cli_with_dbus <args...> -- same, but keeping the session bus
#
# Only 55_steamclient needs this: SteamClient's Ready state *is* a name on the
# session bus, so a case that stubs that name has to let the app see the bus.
app_cli_with_dbus() {
    _app_run "with-dbus=yes" "$@"
}

_app_run() {
    local dbus="$1"; shift
    local bin errfile
    bin="$(app_require_bin)"
    errfile="${CASE_OUT_DIR:-$LAB_OUT_DIR}/app-stderr.log"
    mkdir -p "$(dirname "$errfile")" "$LAB_RUN_DIR"

    local -a envcmd=(env -u WAYLAND_DISPLAY)
    if [[ "$dbus" == "with-dbus=no" ]]; then
        # Unsetting DBUS_SESSION_BUS_ADDRESS is not enough: libdbus then falls
        # back to $XDG_RUNTIME_DIR/bus and finds the developer's real session —
        # where their own Steam may well own the launcher-service name, which
        # would make SteamClient report Ready no matter what the fixture says.
        # Point it somewhere that cannot exist instead, and take the runtime dir
        # away as well so there is nothing to fall back to.
        envcmd+=(
            -u XDG_RUNTIME_DIR
            "DBUS_SESSION_BUS_ADDRESS=unix:path=$LAB_RUN_DIR/no-such-bus"
        )
    fi
    envcmd+=(
        "HOME=$LAB_APP_HOME"
        "XDG_CONFIG_HOME=$LAB_APP_HOME/.config"
        "XDG_CACHE_HOME=$LAB_APP_HOME/.cache"
        "XDG_DATA_HOME=$LAB_APP_HOME/.local/share"
        "TMPDIR=$LAB_APP_TMP"
        # LAB_APP_PATH lets a case control what the app can find. It decides
        # which nvidia-smi, lspci, flatpak or steam is reached — every external
        # program the app runs is resolved through $PATH — and a case that needs
        # one of them to be genuinely absent has to be able to say so.
        "PATH=${LAB_APP_PATH:-$LAB_STUB_BIN:$PATH}"
        QT_QPA_PLATFORM=offscreen
        PROTONFORGE_NO_STARTUP_CHECKS=1
    )

    timeout "$TIMEOUT_CLI" "${envcmd[@]}" "$bin" "$@" 2>>"$errfile"
    local rc=$?
    printf '%s' "$rc" >"$LAB_RUN_DIR/app-rc"
    return $rc
}

# app_rc -> the exit code of the last app_cli call
app_rc() {
    cat "$LAB_RUN_DIR/app-rc" 2>/dev/null || printf 'no-run'
}

# app_version -> the version the binary reports
app_version() {
    app_cli --version | awk '{print $2}'
}

# app_cmake_version -> the version CMakeLists.txt declares
app_cmake_version() {
    grep -oP 'project\(ProtonForge VERSION \K[0-9]+\.[0-9]+\.[0-9]+' "$REPO_ROOT/CMakeLists.txt"
}
