#!/usr/bin/env bash
#
# Runs inside a distribution container and reports its findings as
# "RESULT:<key>=<value>" lines. The case on the host turns those into
# assertions — keeping the bookkeeping outside means the results land in the
# JUnit report like every other check, and it means this script never has to
# know what "correct" is on a given distribution.
#
# It is also runnable as a plain step inside a GitHub Actions `container:` job:
# it takes no docker-specific state, only positional arguments and $HOME. The
# host side switches to that mode with LAB_IN_CONTAINER=1.
#
# Usage: in-container.sh <deb-path|build> <section...>
#
# Sections, so a case only pays for what it asserts:
#   package   build/install/layout/linkage/smoke/purge
#   steam     what real Steam creates vs what SteamPaths looks for

set -uo pipefail

DEB="${1:?path to a .deb, or the word 'build'}"
shift
SECTIONS=("${@:-package}")

result() { printf 'RESULT:%s=%s\n' "$1" "$2"; }
note()   { printf '        %s\n' "$*"; }
want()   { local s; for s in "${SECTIONS[@]}"; do [[ "$s" == "$1" ]] && return 0; done; return 1; }

BIN=/usr/bin/protonforge

# ------------------------------------------------------------ 0) identify
#
# Everything is detected, not assumed: the point of the matrix is that these
# differ, so nothing here may hardcode a distribution's answers.
. /etc/os-release 2>/dev/null || true
result distro "${PRETTY_NAME:-unknown}"
result distro_id "${ID:-unknown}"

QT_PKG=""
QT_VERSION=""
for candidate in libqt6core6t64 libqt6core6; do
    if version="$(dpkg-query -W -f='${Version}' "$candidate" 2>/dev/null)" && [[ -n "$version" ]]; then
        QT_PKG="$candidate"; QT_VERSION="$version"; break
    fi
done
# Before the package is installed the Qt runtime is not there either; ask apt
# which name even exists on this distribution.
if [[ -z "$QT_PKG" ]]; then
    for candidate in libqt6core6t64 libqt6core6; do
        if apt-cache show "$candidate" >/dev/null 2>&1 \
           && [[ -n "$(apt-cache policy "$candidate" 2>/dev/null | sed -n 's/.*Candidate: *//p' | grep -v '(none)')" ]]; then
            QT_PKG="$candidate"; break
        fi
    done
fi
result qt_core_pkg "${QT_PKG:-none}"
result qt_version "${QT_VERSION:-unknown}"
result arch_i386 "$(dpkg --print-foreign-architectures | grep -qx i386 && echo yes || echo no)"
result steamcmd_present "$([[ -x /usr/games/steamcmd ]] && echo yes || echo no)"

# ============================================================ package section

if want package; then

# ------------------------------------------------------------ 1) build
if [[ "$DEB" == "build" ]]; then
    # Per-distribution, and handed in rather than guessed: every target produces a
    # protonforge_<version>_amd64.deb with the same name, so a shared directory
    # would have them overwrite each other — and the host's cache would then serve
    # one distribution's package to another, which is the exact mistake that
    # produced a package linked against the wrong Qt.
    DEB_OUT="${PF_DEB_OUT:-${PF_LAB_DIR:-/tmp}/deb-out}"
    mkdir -p "$DEB_OUT"
    if built="$(bash /src/build-deb.sh /src "$DEB_OUT" 2>/tmp/build.log | tail -n1)" \
       && [[ -f "$built" ]]; then
        result deb_build ok
        result deb_file "$(basename "$built")"
        result deb_size "$(stat -c%s "$built")"
        DEB="$built"
    else
        result deb_build fail
        note "$(tail -n 25 /tmp/build.log)"
        # Graceful degradation: say what could not be checked rather than dying
        # and leaving the host guessing.
        for key in deb_install apt_fixup_needed binary_path cli_version purge_ok; do
            result "$key" skipped
        done
        printf 'RESULT:done=yes\n'
        exit 0
    fi
else
    result deb_build reused
    result deb_file "$(basename "$DEB")"
    result deb_size "$(stat -c%s "$DEB")"
fi

# ------------------------------------------------------------ 2) install
#
# `apt-get install ./pkg.deb` resolves dependencies itself, which is what a user
# does and what proves the Depends line is satisfiable here. The `dpkg -i` then
# `apt-get -f install` dance is recorded separately because needing the second
# step is itself a finding.
result depends_declared "$(dpkg-deb -f "$DEB" Depends | tr -d '\n')"

apt-get update -qq >/dev/null 2>&1
if apt-get install -y --no-install-recommends "$DEB" >/tmp/apt.log 2>&1; then
    result deb_install ok
else
    result deb_install fail
    note "$(tail -n 20 /tmp/apt.log)"
fi

# Would a bare dpkg -i have been enough? Reinstall the hard way to find out.
dpkg --purge protonforge >/dev/null 2>&1
if dpkg -i "$DEB" >/tmp/dpkg.log 2>&1; then
    result apt_fixup_needed no
else
    result apt_fixup_needed yes
    note "dpkg -i alone failed: $(tail -n 5 /tmp/dpkg.log)"
    apt-get install -y -f >/dev/null 2>&1
fi
result installed_version "$(dpkg-query -W -f='${Version}' protonforge 2>/dev/null)"

# ------------------------------------------------------------ 3) layout
result binary_path "$([[ -f "$BIN" ]] && echo "$BIN" || echo missing)"
result binary_mode "$(stat -c '%a' "$BIN" 2>/dev/null || echo none)"

ICON=/usr/share/icons/hicolor/scalable/apps/protonforge.svg
DESKTOP=/usr/share/applications/protonforge.desktop
result icon_installed "$([[ -f "$ICON" ]] && echo yes || echo no)"
result desktop_installed "$([[ -f "$DESKTOP" ]] && echo yes || echo no)"
result copyright_installed "$([[ -f /usr/share/doc/protonforge/copyright ]] && echo yes || echo no)"
result changelog_gzipped "$([[ -f /usr/share/doc/protonforge/changelog.gz ]] && echo yes || echo no)"

if [[ -f "$DESKTOP" ]]; then
    if out="$(desktop-file-validate "$DESKTOP" 2>&1)"; then
        # Hints are informational; only errors matter. Report them either way so
        # the host can show them without failing on them.
        if [[ -z "$out" ]]; then
            result desktop_valid clean
        else
            result desktop_valid hints
            note "$out"
        fi
    else
        result desktop_valid errors
        note "$out"
    fi

    # The package's desktop entry and the one CMake installs used to be two
    # separate texts that had drifted apart. They come from one template now.
    if diff -q <(sed 's/@PROJECT_VERSION@//g' /src/protonforge.desktop.in) \
               <(sed 's/@PROJECT_VERSION@//g' "$DESKTOP") >/dev/null 2>&1; then
        result desktop_matches_template yes
    else
        result desktop_matches_template no
        note "$(diff /src/protonforge.desktop.in "$DESKTOP" || true)"
    fi
else
    result desktop_valid missing
    result desktop_matches_template missing
fi

# ------------------------------------------------------------ 4) linkage
if [[ -x "$BIN" ]]; then
    unresolved="$(ldd -r "$BIN" 2>&1 | grep -cE 'not found|undefined symbol' || true)"
    result ldd_unresolved "${unresolved:-0}"
    [[ "${unresolved:-0}" != "0" ]] && note "$(ldd -r "$BIN" 2>&1 | grep -E 'not found|undefined symbol' | head -n 10)"

    # libQt6DBus is linked but is not in the Depends line; it arrives
    # transitively through libqt6gui6. Recording both makes that explicit
    # instead of leaving it to luck and nobody noticing.
    result links_dbus "$(ldd "$BIN" 2>/dev/null | grep -qi libQt6DBus && echo yes || echo no)"
    result dbus_declared "$(dpkg-deb -f "$DEB" Depends | grep -qi 'libqt6dbus' && echo yes || echo no)"
    result links_concurrent "$(ldd "$BIN" 2>/dev/null | grep -qi libQt6Concurrent && echo yes || echo no)"
else
    result ldd_unresolved skipped
fi

# ------------------------------------------------------------ 5) smoke
#
# The CLI is what makes this more than a file-listing exercise: it runs the real
# binary, on this distribution's Qt, with no display.
if [[ -x "$BIN" ]]; then
    # Every invocation is time-boxed. An unrecognised option is passed through to
    # the GUI on purpose (see cli_unknown_flag below), and a GUI that comes up
    # never returns — so a missing timeout here would hang the whole run instead
    # of failing one check.
    pf() {
        timeout 30 runuser -u labuser -- \
            env QT_QPA_PLATFORM=offscreen HOME=/home/labuser "$@"
    }

    if out="$(pf "$BIN" --version 2>&1)"; then
        result cli_version "$out"
    else
        result cli_version "failed: $out"
    fi

    cmake_version="$(grep -oP 'project\(ProtonForge VERSION \K[0-9]+\.[0-9]+\.[0-9]+' /src/CMakeLists.txt)"
    if [[ "$(pf "$BIN" --version 2>/dev/null)" == "ProtonForge $cmake_version" ]]; then
        result cli_version_matches_cmake yes
    else
        result cli_version_matches_cmake no
    fi

    pf "$BIN" --help >/tmp/help.txt 2>&1
    result cli_help "$([[ -s /tmp/help.txt ]] && echo ok || echo empty)"

    # A malformed --set is a usage error, and so is using it on a command that
    # would ignore it — a typo that looks applied is worse than one that is
    # rejected.
    pf "$BIN" --set nonsense-without-an-equals-sign --print-launch-options 1 >/dev/null 2>&1
    result cli_exit_bad_value "$?"
    pf "$BIN" --set enableProtonHDR=true --steam-info >/dev/null 2>&1
    result cli_exit_set_ignored "$?"

    # An *unknown* option is a different matter: Qt accepts its own flags in
    # double-dash form (--platform, --style), so anything the CLI does not
    # recognise has to reach QApplication or those would stop working. The
    # consequence is that a mistyped option opens the GUI. Recorded rather than
    # asserted, because the alternative is worse — and because on a machine with
    # no display it exits non-zero anyway, which is not the same thing.
    pf "$BIN" --definitely-not-an-option >/dev/null 2>&1
    result cli_unknown_flag "$?"

    # With no Steam anywhere, --steam-info still answers and says so.
    if out="$(timeout 30 runuser -u labuser -- \
                env QT_QPA_PLATFORM=offscreen HOME=/tmp/emptyhome "$BIN" --steam-info 2>&1)"; then
        result cli_steaminfo_rc 0
    else
        result cli_steaminfo_rc "$?"
    fi
    result cli_steaminfo_json "$(printf '%s' "$out" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["variant"])' 2>/dev/null || echo invalid)"
fi

# ------------------------------------------------------------ 6) purge
if dpkg --purge protonforge >/tmp/purge.log 2>&1; then
    result purge_ok yes
else
    result purge_ok no
    note "$(tail -n 10 /tmp/purge.log)"
fi
leftovers="$(ls -1 "$BIN" "$ICON" "$DESKTOP" /usr/share/doc/protonforge 2>/dev/null | wc -l)"
result purge_leftovers "$leftovers"
[[ "$leftovers" != "0" ]] && note "$(ls -la "$BIN" "$ICON" "$DESKTOP" /usr/share/doc/protonforge 2>/dev/null)"

fi  # package section

# ============================================================== steam section

if want steam; then

STEAM_HOME=/home/labuser

# The package section ends by purging, so when both sections run the binary is
# gone by the time we get here. Put it back rather than reordering: purging last
# is what proves it uninstalls cleanly, and this section needs something to run.
if [[ ! -x "$BIN" && "$DEB" != "build" && -f "$DEB" ]]; then
    apt-get install -y --no-install-recommends "$DEB" >/tmp/apt-reinstall.log 2>&1 \
        || note "could not reinstall for the steam section: $(tail -n 5 /tmp/apt-reinstall.log)"
elif [[ ! -x "$BIN" ]]; then
    built="$(find "${PF_DEB_OUT:-${PF_LAB_DIR:-/tmp}/deb-out}" -name 'protonforge_*.deb' 2>/dev/null | head -n1)"
    if [[ -n "$built" ]]; then
        apt-get install -y --no-install-recommends "$built" >/tmp/apt-reinstall.log 2>&1 \
            || note "could not reinstall for the steam section: $(tail -n 5 /tmp/apt-reinstall.log)"
    fi
fi
result binary_available_for_steam "$([[ -x "$BIN" ]] && echo yes || echo no)"

# ---- what steamcmd wrote, with a real anonymous Steam login and no account ----
if [[ -x /usr/games/steamcmd ]]; then
    LIB="$STEAM_HOME/.local/share/Steam/steamapps/libraryfolders.vdf"
    MANIFEST="$STEAM_HOME/.local/share/Steam/steamapps/appmanifest_1007.acf"
    result steamcmd_libraryfolders "$([[ -f "$LIB" ]] && echo yes || echo no)"
    result steamcmd_appmanifest "$([[ -f "$MANIFEST" ]] && echo yes || echo no)"
    result steamcmd_steam_symlink "$([[ -L "$STEAM_HOME/.steam/steam" ]] && echo yes || echo no)"
    result steamcmd_root_symlink "$([[ -L "$STEAM_HOME/.steam/root" ]] && echo yes || echo no)"
    if [[ -L "$STEAM_HOME/.steam/steam" ]]; then
        result steamcmd_symlink_target "$(readlink -f "$STEAM_HOME/.steam/steam")"
    fi
    [[ -f "$LIB" ]] && result steamcmd_libraryfolders_bytes "$(stat -c%s "$LIB")"
else
    for key in steamcmd_libraryfolders steamcmd_appmanifest steamcmd_steam_symlink \
               steamcmd_root_symlink; do
        result "$key" skipped
    done
fi

# ---- what Valve's own bootstrap creates ----
BOOTSTRAP=/usr/lib/steam/bootstraplinux_ubuntu12_32.tar.xz
if [[ -f "$BOOTSTRAP" ]]; then
    BS_HOME=/tmp/bootstraphome
    rm -rf "$BS_HOME"
    mkdir -p "$BS_HOME/.local/share/Steam" "$BS_HOME/.steam"
    # Exactly what bin_steam.sh's install_bootstrap() does.
    tar -xJf "$BOOTSTRAP" -C "$BS_HOME/.local/share/Steam" 2>/dev/null
    ln -fns "$BS_HOME/.local/share/Steam" "$BS_HOME/.steam/steam"

    result bootstrap_extracted yes
    result bootstrap_symlink "$([[ -L "$BS_HOME/.steam/steam" ]] && echo yes || echo no)"
    result bootstrap_has_steam_sh "$([[ -f "$BS_HOME/.local/share/Steam/steam.sh" ]] && echo yes || echo no)"
    result bootstrap_has_ubuntu12_32 "$([[ -d "$BS_HOME/.local/share/Steam/ubuntu12_32" ]] && echo yes || echo no)"
    # The interesting one: the bootstrap does NOT create steamapps or
    # libraryfolders.vdf, so a Steam that has been installed but never signed
    # into is invisible to ProtonForge.
    result bootstrap_libraryfolders \
        "$([[ -f "$BS_HOME/.local/share/Steam/steamapps/libraryfolders.vdf" ]] && echo yes || echo no)"

    if [[ -x "$BIN" ]]; then
        chown -R labuser:labuser "$BS_HOME" 2>/dev/null || true
        out="$(runuser -u labuser -- env QT_QPA_PLATFORM=offscreen HOME="$BS_HOME" \
                 "$BIN" --steam-info 2>/dev/null)"
        result pf_variant_after_bootstrap \
            "$(printf '%s' "$out" | python3 -c 'import json,sys; print(json.load(sys.stdin)["variant"])' 2>/dev/null || echo invalid)"
    fi
else
    result bootstrap_extracted no
fi

# ---- and what ProtonForge makes of the steamcmd tree ----
if [[ -x "$BIN" && -f "$STEAM_HOME/.local/share/Steam/steamapps/libraryfolders.vdf" ]]; then
    out="$(runuser -u labuser -- env QT_QPA_PLATFORM=offscreen HOME="$STEAM_HOME" \
             "$BIN" --steam-info 2>/dev/null)"
    result pf_variant_after_steamcmd \
        "$(printf '%s' "$out" | python3 -c 'import json,sys; print(json.load(sys.stdin)["variant"])' 2>/dev/null || echo invalid)"
    result pf_root_after_steamcmd \
        "$(printf '%s' "$out" | python3 -c 'import json,sys; print(json.load(sys.stdin)["root"])' 2>/dev/null || echo invalid)"
    result pf_libraries_after_steamcmd \
        "$(printf '%s' "$out" | python3 -c 'import json,sys; print(len(json.load(sys.stdin)["libraries"]))' 2>/dev/null || echo invalid)"

    games="$(runuser -u labuser -- env QT_QPA_PLATFORM=offscreen HOME="$STEAM_HOME" \
               "$BIN" --list-games 2>/dev/null)"
    result pf_games_after_steamcmd \
        "$(printf '%s' "$games" | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))' 2>/dev/null || echo invalid)"
    result pf_appids_after_steamcmd \
        "$(printf '%s' "$games" | python3 -c 'import json,sys; print(",".join(sorted(g["appId"] for g in json.load(sys.stdin))))' 2>/dev/null || echo invalid)"
else
    result pf_variant_after_steamcmd skipped
fi

fi  # steam section

printf 'RESULT:done=yes\n'
