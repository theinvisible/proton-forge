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
# See lib/flatpak.sh for why this runs on the host by default. LAB_FLATPAK_DOCKER=1
# runs the identical case in a privileged container instead — a machine that has
# never built this manifest, which is the state the tag workflow builds in.

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
assert [m['name'] for m in derived['modules']] == [m['name'] for m in committed['modules']], \
    'the module list changed — the dependencies are part of what is being tested'
app = next(m for m in derived['modules'] if m['name'] == 'protonforge')
src = app['sources'][0]
assert src['type'] == 'dir', 'the app module\'s first source is not a directory'
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

# The keyring backend. org.kde.Sdk ships libsecret-1.pc and the library but not
# the headers, so qtkeychain's pkg-config check passes and the compile then fails
# on <libsecret/secret.h> — which is how the first Flatpak build after
# SecretStore landed died. The manifest answers that by building libsecret
# itself, and these three assertions are what would notice it being dropped
# again: a bundle without them still builds, still starts and still passes every
# other check in this file, because SecretStore falls back to its 0600 file.
if fp_test_file /app/lib/libsecret-1.so.0; then
    ok "libsecret is built into /app, not taken from the SDK"
elif fp_test_file /app/lib64/libsecret-1.so.0; then
    # The interesting failure, and the one CI hit: meson defaults libdir to lib64
    # and only flatpak-builder >= 1.4.8 overrides it. /app/lib64 is in neither
    # PKG_CONFIG_PATH nor the loader path, so the module is built, installed, and
    # invisible to both qtkeychain's configure and QLibrary.
    fail "libsecret was installed into /app/lib64" \
"The manifest's libsecret module needs --libdir=lib. Without it, whether this
works depends on the flatpak-builder version doing the building, and the older
one is what the tag workflow has."
else
    fail "libsecret is not in the bundle" \
"The manifest is supposed to build it, because the SDK ships the library and its
.pc but not its headers."
fi
if fp_test_file /app/lib/libqt6keychain.so.1; then
    ok "qtkeychain is in the bundle"
elif fp_test_file /app/lib64/libqt6keychain.so.1; then
    # Worse than libsecret's version of this: the bundle builds, installs and
    # exports, and then the app dies on startup because /app/lib64 is not on the
    # loader path. GNUInstallDirs picks lib64 and flatpak-builder only pins it
    # from 1.4.8 on, so whether a release works depends on the runner.
    fail "qtkeychain was installed into /app/lib64" \
"The manifest's qtkeychain module needs -DCMAKE_INSTALL_LIBDIR=lib. Without it
the app cannot load libqt6keychain.so.1 at all."
else
    fail "qtkeychain is not in the bundle" "the manifest is supposed to build it"
fi

# Built *with* the libsecret backend, which the two files above cannot show:
# QtKeychain 0.15 does not link libsecret at all. It resolves the library by name
# through QLibrary and every entry point by dlsym, so DT_NEEDED never mentions it
# — a check on the link would pass on a build that has no libsecret support
# whatsoever. What does disappear when LIBSECRET_SUPPORT is off is the symbol
# names it looks up, so those are what is asserted. The headers matter only at
# compile time and the .so above is what QLibrary then finds at run time.
if fp_env flatpak run --command=grep "$LAB_FLATPAK_APP_ID" \
        -a -q -e secret_password_store -e LibSecretKeyring \
        /app/lib/libqt6keychain.so.1 2>/dev/null; then
    ok "qtkeychain carries its libsecret backend"
else
    fail "qtkeychain was built without libsecret support" \
"/app/lib/libqt6keychain.so.1 names none of libsecret's entry points, so it was
compiled with LIBSECRET_SUPPORT off — which leaves KWallet over D-Bus as the only
backend, and every non-KDE desktop then falls through to SecretStore's 0600 file
with nothing saying so. This is what a missing or failed libsecret module in the
manifest looks like from the outside."
fi

# ---------------------------------------------------------------------------
part "e) the sandbox can reach Steam"

# This is what the case is for. Fixture trees are written into the lab's fake
# HOME, and fp_run_bare passes no extra --filesystem — so only the manifest's own
# finish-args decide whether the app sees them.
#
# In container mode that only holds while the container's home is the host's home
# path, which needs the lab directory to be under it (see lib/flatpak.sh). When it
# is not, `--filesystem=home` grants a directory the fixtures are not in and every
# assertion here would fail for a reason that is not about the manifest.
if fp_docker && ! fp_docker_home_aligned; then
    skip "the sandbox can reach Steam" \
         "PF_LAB_DIR is outside \$HOME, so --filesystem=home cannot cover the fixtures in a container"
else
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
if fp_docker; then
    # The container mounts the lab directory and the checkout, deliberately not
    # your home — so the path exists on the host and not where the sandbox looks.
    # Reporting that as a missing grant would be a lie about the manifest.
    skip "the sandbox can reach the real ~/.var/app/com.valvesoftware.Steam" \
         "the container's home is a stand-in, so run this tier on the host to check it"
elif [[ -d "$REAL_FLATPAK_STEAM" ]]; then
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
fi   # end of the container-home guard around part e)

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
