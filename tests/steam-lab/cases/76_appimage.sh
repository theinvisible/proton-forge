#!/usr/bin/env bash
# lab-requires: docker
#
# The AppImage: built the way a release builds it, and run where it was not built.
#
# Two claims are worth a case of their own, and neither can be made anywhere else.
#
# The first is the obvious one and still the easy half: an AppImage is supposed to
# be one file that runs on any distribution recent enough. Every other tier here
# tests a binary against the Qt of the machine it was built on. This one builds in
# debian:bookworm and then starts the artifact in docker/appimage/Dockerfile's
# image — the host's side of the bargain and nothing more: no qt6-base, no
# libsecret, no qtkeychain, but the graphics and text libraries every AppImage is
# entitled to expect. Running it in the build image would prove nothing, because
# everything is installed there.
#
# The second is specific to this application and is why the AppImage needed code
# rather than only packaging. An AppRun has to prepend the bundle to
# LD_LIBRARY_PATH, QT_PLUGIN_PATH and XDG_DATA_DIRS, and ProtonForge spawns
# things: Proton, games, kscreen-doctor, gsettings, tar. A game that inherits
# those loads the bundle's libstdc++ and libglib over its own — out of a mount
# that is unmounted the moment ProtonForge exits, while the game is still running.
# src/utils/HostEnvironment.cpp undoes it, packaging/appimage/AppRun saves what it
# needs to undo it with, and part e) here is the only place the two are checked
# against a real launched process rather than in isolation.
#
# Cost: the first run builds the build image and compiles Qt-linked sources from
# scratch, so several minutes. Later runs reuse the image.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"
# shellcheck source=../lib/docker.sh
source "$LAB_SRC_DIR/lib/docker.sh"
# shellcheck source=../lib/appimage.sh
source "$LAB_SRC_DIR/lib/appimage.sh"

case_setup
dock_require

APPID=1245620
VERSION="$(app_cmake_version)"
info "CMakeLists.txt says $VERSION"

# ---------------------------------------------------------------------------
part "a) it builds"

# build-appimage.sh is the file the release workflow calls, and it puts itself in
# a container — including here. Nothing about this case installs anything on the
# machine running it.
if ! APPIMAGE="$(ai_build)"; then
    fail "build-appimage.sh did not produce an AppImage" \
"$(tail -n 25 "$CASE_OUT_DIR/appimage-build.log" 2>/dev/null)"
    case_finish
fi
ok "build-appimage.sh builds one in a container ($(du -h "$APPIMAGE" | cut -f1))"

assert_eq "the file is named after the version in CMakeLists.txt" \
    "ProtonForge-${VERSION}-x86_64.AppImage" "$(basename "$APPIMAGE")"
assert_exec "and it is executable" "$APPIMAGE"

# ---------------------------------------------------------------------------
part "b) what is inside the bundle"

if ! APPDIR="$(ai_extract "$APPIMAGE")"; then
    fail "the AppImage could not be extracted" "its own runtime refused to unpack it"
    case_finish
fi

assert_file "Qt is bundled, not borrowed from the host" \
    "$APPDIR/usr/lib/libQt6Core.so.6"
assert_file "and so is the Widgets library the UI needs" \
    "$APPDIR/usr/lib/libQt6Widgets.so.6"

# The keyring backend. QtKeychain resolves libsecret through QLibrary and dlsym
# rather than linking it, so nothing in the dependency graph mentions it and
# linuxdeploy cannot know it is needed — build-appimage.sh names it explicitly.
# Without it every host that has no libsecret of its own falls through to
# SecretStore's 0600 file, silently (TESTS.md §7, finding 9).
assert_file "libsecret is bundled, which no dependency graph would have said" \
    "$APPDIR/usr/lib/libsecret-1.so.0"
assert_file "next to the qtkeychain that looks it up" \
    "$APPDIR/usr/lib/libqt6keychain.so.1"

# Every icon in resources.qrc is an SVG, including the store badges in the Game
# Stores dialog, and Qt renders exactly none of them without this plugin — QIcon
# comes back null and the dialogs have blank spaces. It is loaded by name at run
# time and nothing links Qt6Svg, so linuxdeploy bundles it only if the build image
# has it installed. The first AppImage did not, and the missing icons in a running
# app were the only symptom.
assert_file "the SVG image plugin is bundled, or no icon renders at all" \
    "$APPDIR/usr/plugins/imageformats/libqsvg.so"
assert_file "along with the Qt Svg library it needs" \
    "$APPDIR/usr/lib/libQt6Svg.so.6"

# The opposite requirement, and the one that would fail quietly on a user's
# machine: NVML belongs to the host's driver. NvmlSession dlopens it by name, so a
# bundled copy would be found first and then talk to a driver it does not match.
if find "$APPDIR" -name 'libnvidia-ml.so*' -print -quit | grep -q .; then
    fail "libnvidia-ml is bundled" \
"It has to come from the host driver. A copy in the bundle is loaded in preference
to the host's and reports whatever the build machine had."
else
    ok "libnvidia-ml is not bundled — it comes from the host driver"
fi

# Invisible until a user on a Wayland session wonders why the window is XWayland —
# or, without the second one, why the app does not start at all. linuxdeploy
# deploys neither on its own: the QPA plugin needs asking for, and the shell
# integration it loads at run time is in nobody's dependency graph.
assert_file "the Wayland QPA plugin made it in" \
    "$APPDIR/usr/plugins/platforms/libqwayland-generic.so"
assert_file "and the xdg-shell integration it cannot open a window without" \
    "$APPDIR/usr/plugins/wayland-shell-integration/libxdg-shell.so"
assert_file "along with the client-side decorations" \
    "$APPDIR/usr/plugins/wayland-decoration-client/libbradient.so"

# ---------------------------------------------------------------------------
part "c) it runs where Qt is not installed"

# The run image has no Qt, no libsecret and no qtkeychain — only what the
# excludelist says the host provides. This is the claim the format is sold on.
VERSION_OUT="$(ai_bare_run "$APPIMAGE" --version 2>"$(case_log bare-version)")"
assert_eq "a $LAB_APPIMAGE_HOST_IMAGE with no Qt installed runs it" \
    "ProtonForge $VERSION" "$VERSION_OUT"

if [[ "$VERSION_OUT" != "ProtonForge $VERSION" ]]; then
    info "stderr: $(tail -n 5 "$(case_log bare-version)")"
fi

# ---------------------------------------------------------------------------
part "d) it finds Steam from inside the bundle"

fx_reset
NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" >/dev/null

INFO="$(ai_bare_run "$APPIMAGE" --steam-info 2>"$(case_log bare-steaminfo)")"
if json_valid "$INFO"; then
    assert_json "the bundled app sees the native Steam install" "$INFO" 'd["variant"]' "native"
    GAMES="$(ai_bare_run "$APPIMAGE" --list-games 2>/dev/null)"
    assert_json "and enumerates the games in it" "$GAMES" 'len(d)' "1"
else
    fail "--steam-info returned nothing usable from the AppImage" \
"output: $INFO
stderr: $(tail -n 10 "$(case_log bare-steaminfo)")"
fi

# ---------------------------------------------------------------------------
part "e) a launched game does not inherit the bundle"

# The assertion this whole case exists for. ai_bare_run starts the AppImage with
# LD_LIBRARY_PATH set to a sentinel; AppRun prepends the bundle to it and saves the
# old value; HostEnvironment puts it back for the child. So what the game receives
# must be *exactly* the sentinel — an equality check, which also fails if the
# value is dropped rather than restored.
#
# A GOG game rather than a Steam one, and not for convenience: launching a Steam
# game goes through the client-readiness gate, which 45_launch satisfies with a
# process named steam and a pid file. A container has its own pid namespace, so
# that process is invisible from inside and the launch would be refused before any
# environment was composed. A GOG launch claims no Steam identity and has no such
# gate — and the environment under test is the same one either way, because both
# routes are built by EnvBuilder::buildEnvironment().
PRODUCT=1207658930
fx_reset
fx_steam_tree none >/dev/null
fx_gog_game "$PRODUCT" title="The Witcher 2" >/dev/null
stub_proton "$LAB_APP_HOME/.steam/root" "proton-cachyos-11.0-20260703-slr-x86_64" >/dev/null
stub_records_reset

ai_bare_run "$APPIMAGE" --launch "$PRODUCT" --timeout "$TIMEOUT_LAUNCH" \
    >"$(case_log bare-launch)" 2>&1
LAUNCH_RC=$?

if ! stub_proton_was_run; then
    fail "the game was never launched from the AppImage" \
"Nothing was recorded in $LAB_PROTON_RECORD, so parts of this cannot be judged.
exit code: $LAUNCH_RC
$(tail -n 15 "$(case_log bare-launch)")"
    case_finish
fi
ok "the AppImage launches a game through Proton"

assert_eq "the game gets the host's LD_LIBRARY_PATH, not the bundle's" \
    "$LAB_APPIMAGE_SENTINEL" "$(stub_proton_env LD_LIBRARY_PATH)"
assert_eq "and the host's QT_PLUGIN_PATH" \
    "$LAB_APPIMAGE_SENTINEL_QT" "$(stub_proton_env QT_PLUGIN_PATH)"
assert_eq "and the host's XDG_DATA_DIRS" \
    "$LAB_APPIMAGE_SENTINEL_XDG" "$(stub_proton_env XDG_DATA_DIRS)"

ai_cleanup
case_finish
