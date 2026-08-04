#!/usr/bin/env bash
# lab-requires: flatpak build
#
# The Flatpak: built from the working tree, installed, and run.
#
# Until now the Flatpak has only ever been built from a release tarball by the
# tag workflow — and that workflow is broken on workflow_dispatch, where it
# resolves the version to "dev" and then fetches vdev.tar.gz. So the manifest is
# only exercised at tag time, when it is too late to find out it does not work.
#
# The interesting part is not that it compiles; it is the sandbox. finish-args
# decides what the app can see, and Steam's data is in three different places —
# ~/.steam, xdg-data/Steam and another app's ~/.var/app directory, which Flatpak
# protects by default. Whether those grants are sufficient cannot be reasoned
# about from the manifest; it has to be tried.
#
# See lib/flatpak.sh for why this runs on the host rather than in a container.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"
# shellcheck source=../lib/flatpak.sh
source "$LAB_SRC_DIR/lib/flatpak.sh"

case_setup
fp_require

APPID=1245620
RUNTIME_VERSION="$(fp_runtime_version)"
info "the manifest asks for org.kde.Platform//$RUNTIME_VERSION"

# ---------------------------------------------------------------------------
part "a) the pinned runtime still exists"

# Flathub retires old branches. "the runtime the manifest asks for is gone" is a
# real state and a finding in its own right, not a reason for the harness to fall
# over — but it does stop everything below it.
if ! fp_runtime_available; then
    fail "org.kde.Platform//$RUNTIME_VERSION is no longer on Flathub" \
"org.protonforge.ProtonForge.yml pins runtime-version: '$RUNTIME_VERSION', and
neither the platform nor the SDK can be found for that branch any more. The
Flatpak cannot be built until the manifest moves to a supported runtime.

Available KDE platform branches:
$(fp_env flatpak remote-info --user flathub org.kde.Platform 2>&1 | head -n 5)
$(fp_env flatpak remote-ls --user flathub --arch=x86_64 2>/dev/null | grep -E '^org\.kde\.Platform\b' | head -n 10)"
    case_finish
fi
ok "org.kde.Platform//$RUNTIME_VERSION and its SDK are available"

if ! fp_env flatpak info "org.kde.Platform//$RUNTIME_VERSION" >/dev/null 2>&1; then
    info "installing the runtime into $LAB_FLATPAK_DIR — first run only, about 1.5 GB"
    fp_install_runtime
fi
ok "the runtime is installed in the lab's own flatpak directory"

# ---------------------------------------------------------------------------
part "b) it builds from the working tree"

MANIFEST="$(fp_devel_manifest)"
assert_file "a buildable manifest was derived from the committed one" "$MANIFEST"

# The derived manifest must differ from the committed one only in its source.
assert_true "the derived manifest keeps the app id and the finish-args" \
    python3 -c "
import yaml
committed = yaml.safe_load(open('$REPO_ROOT/org.protonforge.ProtonForge.yml'))
derived   = yaml.safe_load(open('$MANIFEST'))
assert derived['app-id'] == committed['app-id'], 'app-id changed'
assert derived['finish-args'] == committed['finish-args'], 'finish-args changed'
assert derived['runtime-version'] == committed['runtime-version'], 'runtime-version changed'
assert derived['command'] == committed['command'], 'command changed'
src = derived['modules'][0]['sources'][0]
assert src['type'] == 'dir', 'the first source is not a directory'
assert src['path'] == '$REPO_ROOT', 'the source does not point at the checkout'
"

if fp_build; then
    ok "flatpak-builder builds the working tree"
else
    fail "flatpak-builder failed" "see $CASE_OUT_DIR/flatpak-build.log"
    case_finish
fi

BUNDLE="$(fp_bundle)"
if [[ -n "$BUNDLE" && -f "$BUNDLE" ]]; then
    ok "a bundle can be exported ($(du -h "$BUNDLE" | cut -f1))"
else
    fail "build-bundle failed" "see $CASE_OUT_DIR/flatpak-bundle.log"
    case_finish
fi

# ---------------------------------------------------------------------------
part "c) it installs and runs"

fp_cleanup
if fp_install_bundle "$BUNDLE"; then
    ok "the bundle installs"
else
    fail "the bundle does not install" "$(tail -n 20 "$CASE_OUT_DIR/flatpak-install.log")"
    case_finish
fi

CMAKE_VERSION="$(app_cmake_version)"
VERSION_OUT="$(fp_run --version 2>&1)"
assert_eq "the sandboxed app reports its version" "ProtonForge $CMAKE_VERSION" "$VERSION_OUT"

# ---------------------------------------------------------------------------
part "d) the layout inside /app"

# The manifest's command is lowercase but CMake installs the capitalised name, so
# post-install adds a symlink. If that ever breaks, `flatpak run` fails with a
# command-not-found that says nothing about the cause.
assert_true "/app/bin/ProtonForge is the CMake-installed binary" \
    fp_test_file /app/bin/ProtonForge
assert_true "/app/bin/protonforge exists, matching the manifest's command" \
    fp_test_file /app/bin/protonforge
assert_true "the metainfo file is installed" \
    fp_test_file /app/share/metainfo/org.protonforge.ProtonForge.metainfo.xml
assert_true "the desktop entry is installed" \
    fp_test_file /app/share/applications/org.protonforge.ProtonForge.desktop
assert_true "the icon is installed" \
    fp_test_file /app/share/icons/hicolor/scalable/apps/org.protonforge.ProtonForge.svg

# ---------------------------------------------------------------------------
part "e) the sandbox can reach Steam"

# This is what the case is for. Fixture trees are written into the lab's fake
# HOME, and fp_run_bare passes no extra --filesystem — so only the manifest's own
# finish-args decide whether the app sees them.
fx_reset
NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" >/dev/null

# The fake HOME is under ~/.cache, which --filesystem=home covers, so this checks
# the grant chain rather than a special case.
INFO="$(fp_env timeout "$TIMEOUT_CLI" flatpak run \
    --env=HOME="$LAB_APP_HOME" \
    --env=PROTONFORGE_NO_STARTUP_CHECKS=1 \
    "$LAB_FLATPAK_APP_ID" --steam-info 2>"$(case_log fp-steaminfo)")"

if json_valid "$INFO"; then
    assert_json "the sandbox sees a native Steam install" "$INFO" 'd["variant"]' "native"
    assert_json_contains "at the expected root" "$INFO" 'd["root"]' "/.local/share/Steam"

    GAMES="$(fp_env timeout "$TIMEOUT_CLI" flatpak run \
        --env=HOME="$LAB_APP_HOME" \
        --env=PROTONFORGE_NO_STARTUP_CHECKS=1 \
        "$LAB_FLATPAK_APP_ID" --list-games 2>/dev/null)"
    assert_json "and can enumerate the games in it" "$GAMES" 'len(d)' "1"
else
    fail "the sandboxed app could not read the Steam tree" \
"--steam-info returned nothing usable. The manifest grants
  --filesystem=home, --filesystem=~/.steam:rw, --filesystem=xdg-data/Steam:rw
so a tree under \$HOME should be visible.
output: $INFO
stderr: $(tail -n 10 "$(case_log fp-steaminfo)")"
fi

# The Flatpak-Steam layout, still inside the lab's fake HOME. Note what this does
# and does not prove: the fixture sits at
# $LAB_APP_HOME/.var/app/com.valvesoftware.Steam/..., which is a nested path under
# the lab directory, so --filesystem=home is what makes it visible. The
# classification is what is under test here.
fx_reset
FLATPAK_STEAM="$(fx_steam_tree flatpak)"
INFO="$(fp_env timeout "$TIMEOUT_CLI" flatpak run \
    --env=HOME="$LAB_APP_HOME" \
    --env=PROTONFORGE_NO_STARTUP_CHECKS=1 \
    "$LAB_FLATPAK_APP_ID" --steam-info 2>/dev/null)"
if json_valid "$INFO" && [[ "$(json_get "$INFO" 'd["variant"]')" == "flatpak" ]]; then
    ok "the sandbox classifies a Flatpak-Steam layout correctly"
else
    fail "the sandbox does not see the Flatpak-Steam layout" \
"variant reported: $(json_get "$INFO" 'd["variant"]' 2>/dev/null || echo '<invalid>')"
fi

# The grant that actually matters can only be checked against the real path.
# Flatpak hides other applications' ~/.var/app directories even under
# --filesystem=home, which is why the manifest names
# --filesystem=~/.var/app/com.valvesoftware.Steam:rw explicitly. Nothing in a fake
# HOME can stand in for that, and writing a fixture into the user's real ~/.var/app
# is not something a test should do — so this is probed where it exists and
# reported honestly where it does not.
REAL_FLATPAK_STEAM="$HOME/.var/app/com.valvesoftware.Steam"
if [[ -d "$REAL_FLATPAK_STEAM" ]]; then
    if fp_test_file "$REAL_FLATPAK_STEAM"; then
        ok "the sandbox can reach the real ~/.var/app/com.valvesoftware.Steam"
    else
        fail "the sandbox cannot reach the real ~/.var/app/com.valvesoftware.Steam" \
"--filesystem=~/.var/app/com.valvesoftware.Steam:rw is in the manifest for exactly
this. Without it a user running the Flatpak Steam sees no games at all, and
--filesystem=home does not cover it — Flatpak protects other apps' data."
    fi
else
    skip "the sandbox can reach the real ~/.var/app/com.valvesoftware.Steam" \
         "no Flatpak Steam on this machine, and a fake HOME cannot stand in for that path"
fi

# ---------------------------------------------------------------------------
part "f) differences the sandbox imposes"

# FLATPAK_ID is set inside the sandbox, and SteamPaths uses it to pick where a
# new Proton should be installed when no Steam is detected.
fx_reset
COMPAT="$(fp_env timeout "$TIMEOUT_CLI" flatpak run \
    --env=HOME="$LAB_APP_HOME" \
    --env=PROTONFORGE_NO_STARTUP_CHECKS=1 \
    "$LAB_FLATPAK_APP_ID" --steam-info 2>/dev/null)"
if json_valid "$COMPAT"; then
    assert_json_contains "with no Steam, Proton would be installed for Flatpak Steam" \
        "$COMPAT" 'd["defaultInstallCompatPath"]' \
        "/.var/app/com.valvesoftware.Steam/.local/share/Steam/compatibilitytools.d"
else
    fail "could not read --steam-info from the sandbox" "$COMPAT"
fi

# /proc/driver/nvidia/version is not readable without --filesystem=host, which the
# manifest deliberately does not grant. NvidiaGPUDetector says as much in a
# comment; the point is that the fallback path is taken and nothing crashes.
if fp_test_file /proc/driver/nvidia/version; then
    info "/proc/driver/nvidia/version is readable in this sandbox"
else
    ok "/proc/driver/nvidia/version is not readable, as the manifest implies"
fi
assert_eq "and the app still runs regardless" "ProtonForge $CMAKE_VERSION" \
    "$(fp_run --version 2>&1)"

# ---------------------------------------------------------------------------
part "g) flatpak-builder-lint, if it is available"

if fp_env flatpak run --command=flatpak-builder-lint \
        org.flatpak.Builder --help >/dev/null 2>&1; then
    if fp_env flatpak run --command=flatpak-builder-lint org.flatpak.Builder \
            manifest "$REPO_ROOT/org.protonforge.ProtonForge.yml" \
            >"$(case_log lint)" 2>&1; then
        ok "flatpak-builder-lint accepts the manifest"
    else
        # Lint findings are advisory — Flathub requires them for submission, this
        # project does not ship there.
        skip "flatpak-builder-lint accepts the manifest" \
             "it has findings, see $(case_log lint)"
    fi
else
    skip "flatpak-builder-lint" "org.flatpak.Builder is not installed"
fi

fp_cleanup
case_finish
