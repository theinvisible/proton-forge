#!/bin/bash
#
# Build the ProtonForge AppImage.
#
# One build path for everybody, the same arrangement build-deb.sh has: the test
# case (tests/steam-lab/cases/76_appimage.sh) calls this, and so does
# .github/workflows/release.yml. Change how the AppImage is built here and all of
# them follow.
#
# Usage: build-appimage.sh [source-dir] [output-dir]
#
# Progress goes to stdout via log(). The LAST line of stdout is the path of the
# finished AppImage, so callers can do:
#
#     img="$(bash build-appimage.sh "$src" "$out" | tail -n1)"
#
# ---------------------------------------------------------------------------
# It always builds in a container, including on a developer's machine.
#
# An AppImage build wants qt6-wayland, patchelf, desktop-file-utils and a
# downloaded linuxdeploy, and it wants them from an old distribution — an AppImage
# only runs on glibc at least as new as the one it was built against, so the base
# decides who can run the result. Neither of those is a reason to change a
# developer's machine. So: started outside a container, this script builds
# packaging/appimage/Dockerfile and re-executes itself inside it; started inside
# one, it builds. The host ends up with a docker image and an artifact, nothing
# else.
#
#     PROTONFORGE_APPIMAGE_IN_CONTAINER=1   build here, do not use docker. Set
#                                           automatically when /.dockerenv exists,
#                                           and by the release workflow, which is
#                                           already running in a container.
#     PROTONFORGE_APPIMAGE_IMAGE=...        base image to build in
#                                           (default debian:bookworm — see the
#                                           Dockerfile for why the oldest wins).
#     PROTONFORGE_APPIMAGE_REBUILD=1        rebuild the build image even if it
#                                           looks current.

set -euo pipefail

SRC="${1:-$PWD}"
OUT="${2:-$PWD}"

log() { printf '==> %s\n' "$*"; }
die() { printf 'build-appimage: %s\n' "$*" >&2; exit 1; }

[[ -f "$SRC/CMakeLists.txt" ]] || die "$SRC/CMakeLists.txt not found — is that the source directory?"
[[ -f "$SRC/packaging/appimage/AppRun" ]] || die "$SRC/packaging/appimage/AppRun not found"

SRC="$(cd "$SRC" && pwd)"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

# Version from CMakeLists.txt — the single source of truth, read with the same
# expression build-deb.sh uses.
VERSION="$(grep -oP 'project\(ProtonForge VERSION \K[0-9]+\.[0-9]+\.[0-9]+' "$SRC/CMakeLists.txt")"
[[ -n "$VERSION" ]] || die "could not extract the version from $SRC/CMakeLists.txt"

APPIMAGE_NAME="ProtonForge-${VERSION}-x86_64.AppImage"
IMAGE_BASE="${PROTONFORGE_APPIMAGE_IMAGE:-debian:bookworm}"
IMAGE_TAG="protonforge-appimage:$(printf '%s' "${IMAGE_BASE//[:\/]/-}")"

# ---------------------------------------------------------------- in a container?
if [[ "${PROTONFORGE_APPIMAGE_IN_CONTAINER:-0}" != "1" ]] \
   && [[ ! -e /.dockerenv && ! -e /run/.containerenv ]]; then

    command -v docker >/dev/null 2>&1 || die "docker is missing: sudo apt install docker.io
The AppImage is built in a container on purpose — see the note at the top of this
script. Set PROTONFORGE_APPIMAGE_IN_CONTAINER=1 to build on this machine instead,
which needs the packages packaging/appimage/Dockerfile installs."
    docker info >/dev/null 2>&1 \
        || die "cannot talk to the docker daemon — is the user in the 'docker' group?
    sudo usermod -aG docker \$USER && newgrp docker"

    # A digest of everything the image is built from, so a changed Dockerfile or a
    # moved tool version is noticed. Timestamps do not work: BuildKit does not
    # give a rebuilt image a fresh .Created (same trick as the lab's images).
    DIGEST="$({ printf '%s\n' "$IMAGE_BASE"
                cat "$SRC/packaging/appimage/Dockerfile" \
                    "$SRC/packaging/appimage/tools.env" \
                    "$SRC/packaging/appimage/install-tools.sh" \
                    "$SRC/packaging/appimage/build-depends.txt" \
                    "$SRC/packaging/build-depends.txt"
              } | sha256sum | cut -c1-16)"
    CURRENT="$(docker image inspect \
        --format '{{index .Config.Labels "org.protonforge.lab.inputs"}}' \
        "$IMAGE_TAG" 2>/dev/null || true)"

    if [[ "${PROTONFORGE_APPIMAGE_REBUILD:-0}" == "1" || "$CURRENT" != "$DIGEST" ]]; then
        log "building the AppImage build image from $IMAGE_BASE (first time takes a while)"
        # The build context holds only the two files the Dockerfile copies, so a
        # source change never invalidates the apt layers.
        CTX="$(mktemp -d)"
        cp "$SRC/packaging/appimage/Dockerfile" \
           "$SRC/packaging/appimage/tools.env" \
           "$SRC/packaging/appimage/install-tools.sh" \
           "$CTX/"
        cp "$SRC/packaging/build-depends.txt" "$CTX/build-depends.txt"
        # Renamed in the context: two files called build-depends.txt cannot both
        # be copied in, and the Dockerfile says which is which.
        cp "$SRC/packaging/appimage/build-depends.txt" "$CTX/appimage-build-depends.txt"
        docker build \
            --build-arg "BASE_IMAGE=$IMAGE_BASE" \
            --build-arg "INPUT_DIGEST=$DIGEST" \
            -t "$IMAGE_TAG" "$CTX" >/dev/null \
            || { rm -rf "$CTX"; die "the build image could not be built"; }
        rm -rf "$CTX"
    fi

    log "building in $IMAGE_TAG"
    # Source read-only, output writable, no network — everything the build needs
    # is in the image. Matching uid keeps the artifact owned by the caller.
    docker run --rm \
        --network none \
        --user "$(id -u):$(id -g)" \
        -e PROTONFORGE_APPIMAGE_IN_CONTAINER=1 \
        -e HOME=/tmp/appimage-home \
        -v "$SRC:/src:ro" \
        -v "$OUT:/out" \
        "$IMAGE_TAG" \
        -c 'bash /src/build-appimage.sh /src /out' \
        || die "the containerised build failed"

    [[ -f "$OUT/$APPIMAGE_NAME" ]] || die "the build reported success but $OUT/$APPIMAGE_NAME is not there"

    log "$OUT/$APPIMAGE_NAME"
    printf '%s\n' "$OUT/$APPIMAGE_NAME"
    exit 0
fi

# ===========================================================================
# From here on: inside the container.
# ===========================================================================

log "ProtonForge $VERSION"

# shellcheck source=packaging/appimage/tools.env
. "$SRC/packaging/appimage/tools.env"

LINUXDEPLOY="$LINUXDEPLOY_PREFIX/linuxdeploy/AppRun"
[[ -x "$LINUXDEPLOY" ]] || die "linuxdeploy is not at $LINUXDEPLOY.
This script expects the environment packaging/appimage/Dockerfile builds. Run it
without PROTONFORGE_APPIMAGE_IN_CONTAINER to have it set that up itself."
command -v qmake6 >/dev/null 2>&1 || die "qmake6 is missing — linuxdeploy-plugin-qt needs it to find Qt 6"

# ---------------------------------------------------------------- work copy
# The source is a read-only mount here and a working copy on a developer's
# machine; neither should collect build artifacts.
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
BUILD_SRC="$WORK/src"

log "copying the source tree"
mkdir -p "$BUILD_SRC"
tar -C "$SRC" \
    --exclude='./.git' \
    --exclude='./cmake-build-*' \
    --exclude='./debian-build' \
    --exclude='./build' \
    --exclude='./.flatpak-builder' \
    --exclude='*.deb' \
    --exclude='*.AppImage' \
    --exclude='*.o' \
    -cf - . | tar -C "$BUILD_SRC" -xf -

# ---------------------------------------------------------------- compile
log "building the release binary (this takes a few minutes)"
cmake -S "$BUILD_SRC" -B "$BUILD_SRC/cmake-build-appimage" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr >/dev/null \
    || die "cmake configure failed"
cmake --build "$BUILD_SRC/cmake-build-appimage" -j"$(nproc)" >/dev/null \
    || die "cmake build failed"

# ---------------------------------------------------------------- AppDir
APPDIR="$WORK/AppDir"
log "installing into the AppDir"
DESTDIR="$APPDIR" cmake --install "$BUILD_SRC/cmake-build-appimage" >/dev/null \
    || die "cmake install failed"

# What the install rules in CMakeLists.txt put there. Asserted rather than
# assumed: linuxdeploy's error for a missing desktop file says nothing about why.
[[ -x "$APPDIR/usr/bin/ProtonForge" ]] \
    || die "no binary at usr/bin/ProtonForge — did the install rules change?"
[[ -f "$APPDIR/usr/share/applications/protonforge.desktop" ]] \
    || die "no desktop entry in the AppDir"
[[ -f "$APPDIR/usr/share/icons/hicolor/scalable/apps/protonforge.svg" ]] \
    || die "no icon in the AppDir"

# ---------------------------------------------------------------- deploy
# libsecret is bundled deliberately. QtKeychain 0.15 does not link it: it resolves
# "secret-1" through QLibrary and every entry point by dlsym, so nothing in the
# dependency graph mentions it and linuxdeploy has no way to know it is needed
# (TESTS.md §7, finding 9). Without it the keyring silently falls back to
# SecretStore's 0600 file on any host that has no libsecret of its own.
LIBSECRET="$(find /usr/lib -name 'libsecret-1.so.0' -print -quit 2>/dev/null || true)"
[[ -n "$LIBSECRET" ]] || die "libsecret-1.so.0 not found — is libsecret-1-0 installed?"

log "deploying Qt and the dependencies"

# linuxdeploy is chatty and its interesting lines are the ones nobody reads until
# something is missing from the bundle. Kept next to the artifact rather than in
# the (discarded) work directory, as a dotfile so it does not clutter an output
# directory that is otherwise the release itself.
LOG="$OUT/.appimage-build.log"
export QMAKE=/usr/bin/qmake6           # or the plugin looks for Qt 5 and finds nothing
export APPIMAGE_EXTRACT_AND_RUN=1      # no FUSE in a container
export NO_STRIP=""                     # linuxdeploy strips; binutils is installed for it
export OUTPUT="$APPIMAGE_NAME"

# Wayland has to be asked for by name. linuxdeploy-plugin-qt deploys the xcb
# platform plugin and nothing else on its own, so without this the AppImage runs
# every Wayland session through XWayland — on a tool whose own HDR advice is "use
# Wayland", that would be a poor joke.
#
# Two levers, and both are needed: EXTRA_PLATFORM_PLUGINS adds the QPA plugins,
# and the plugin's `waylandclient` module is what brings the shell integrations
# and the client-side decorations with it. Those are loaded at run time by
# libqwayland-generic, so they are in nobody's dependency graph; a window with no
# decorations and no way to resize is what their absence looks like.
# Both go through the environment, because linuxdeploy has no way to forward
# arguments to a plugin it invokes — the plugin's own --extra-module is
# unreachable from here.
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so"
export EXTRA_QT_MODULES="waylandclient"

# …and that still is not all of Wayland. The waylandclient module deploys the QPA
# plugin and libQt6WaylandClient, but not the plugin directories the QPA plugin
# loads at run time. Without wayland-shell-integration in particular there is no
# xdg-shell, and a Qt application on Wayland cannot create a window at all — it
# fails at startup rather than looking slightly wrong.
#
# So those directories are copied in by hand and linuxdeploy is asked to resolve
# their dependencies (--deploy-deps-only), which is the documented way to add
# plugins it does not know about.
QT_PLUGIN_DIR="$(qmake6 -query QT_INSTALL_PLUGINS)"
DEPLOY_DEPS_ONLY=()
for plugin_dir in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
    [[ -d "$QT_PLUGIN_DIR/$plugin_dir" ]] || continue
    mkdir -p "$APPDIR/usr/plugins/$plugin_dir"
    cp -n "$QT_PLUGIN_DIR/$plugin_dir"/*.so "$APPDIR/usr/plugins/$plugin_dir/" 2>/dev/null || true
    DEPLOY_DEPS_ONLY+=("--deploy-deps-only=$APPDIR/usr/plugins/$plugin_dir")
done
[[ ${#DEPLOY_DEPS_ONLY[@]} -gt 0 ]] \
    || die "no Wayland plugin directories under $QT_PLUGIN_DIR — is qt6-wayland installed?"

# appimagetool no longer embeds the runtime and downloads it mid-build, which a
# build with no network cannot do. The image has it; point the plugin at it.
RUNTIME="$LINUXDEPLOY_PREFIX/runtime-x86_64"
[[ -f "$RUNTIME" ]] || die "no AppImage runtime at $RUNTIME — rebuild the build image
(PROTONFORGE_APPIMAGE_REBUILD=1) so it picks up packaging/appimage/tools.env."
export LDAI_RUNTIME_FILE="$RUNTIME"

( cd "$WORK" && "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/ProtonForge" \
    --desktop-file "$APPDIR/usr/share/applications/protonforge.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/protonforge.svg" \
    --library "$LIBSECRET" \
    --custom-apprun "$BUILD_SRC/packaging/appimage/AppRun" \
    "${DEPLOY_DEPS_ONLY[@]}" \
    --plugin qt \
    --output appimage ) >"$LOG" 2>&1 \
    || die "linuxdeploy failed:
$(tail -n 25 "$LOG")"

# ---------------------------------------------------------------- verify
# The NVIDIA management library must come from the host's driver. NvmlSession
# dlopens it, so it is not in the dependency graph and linuxdeploy will not bundle
# it on its own — but a stray copy in the AppDir would be loaded in preference to
# the host's and then talk to a driver it does not match. Same spirit as the
# `ldd -r` check 20_deb_install runs on the .deb: assert the thing that would
# otherwise fail quietly on someone else's machine.
if find "$APPDIR" -name 'libnvidia-ml.so*' -print -quit | grep -q .; then
    die "libnvidia-ml was bundled into the AppDir. It belongs to the host driver;
NvmlSession dlopens it by name and a bundled copy would be found first."
fi

# Wayland is the deployment gap nothing else notices: linuxdeploy is perfectly
# happy without it, the AppImage starts on X11, and only a user on a Wayland
# session finds out. Both halves are checked — the QPA plugin, and the shell
# integration it cannot create a single window without.
[[ -e "$APPDIR/usr/plugins/platforms/libqwayland-generic.so" ]] \
    || die "no Wayland QPA plugin in the bundle — is qt6-wayland installed?"
[[ -e "$APPDIR/usr/plugins/wayland-shell-integration/libxdg-shell.so" ]] \
    || die "no xdg-shell integration in the bundle. The Wayland QPA plugin loads it
at run time and cannot create a window without it, so the app would fail to start
on every Wayland session."

RESULT="$OUT/$APPIMAGE_NAME"
mv -f "$WORK/$APPIMAGE_NAME" "$RESULT"
chmod 755 "$RESULT"

log "$RESULT ($(du -h "$RESULT" | cut -f1))"

# The path the caller is after — must stay the last line on stdout.
printf '%s\n' "$RESULT"
