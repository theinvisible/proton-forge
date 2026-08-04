#!/bin/bash
#
# Build the ProtonForge Debian package.
#
# One build path for everybody: the distribution test case
# (tests/steam-lab/cases/20_deb_install.sh) runs this inside a container, and so
# do .github/workflows/{ci,release}.yml. Change how the package is built here,
# and all of them follow.
#
# Usage: build-deb.sh [source-dir] [output-dir]
#
# Both default to the current directory, so the historical zero-argument call
# from the repository root behaves exactly as it always has.
#
# The source tree is copied before anything is built. In the container it is a
# read-only mount, and on a developer's machine it is their working copy —
# neither should collect build artifacts or root-owned files.
#
# Progress goes to stdout via log(). The LAST line of stdout is the path of the
# finished package, so callers can do:
#
#     deb="$(bash build-deb.sh "$src" "$out" | tail -n1)"

set -euo pipefail

SRC="${1:-$PWD}"
OUT="${2:-$PWD}"

log() { printf '==> %s\n' "$*"; }
die() { printf 'build-deb: %s\n' "$*" >&2; exit 1; }

[[ -f "$SRC/CMakeLists.txt" ]] || die "$SRC/CMakeLists.txt not found — is that the source directory?"
[[ -f "$SRC/debian/control" ]] || die "$SRC/debian/control not found"

SRC="$(cd "$SRC" && pwd)"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

# Version from CMakeLists.txt — the single source of truth.
VERSION="$(grep -oP 'project\(ProtonForge VERSION \K[0-9]+\.[0-9]+\.[0-9]+' "$SRC/CMakeLists.txt")"
[[ -n "$VERSION" ]] || die "could not extract the version from $SRC/CMakeLists.txt"
log "ProtonForge $VERSION"

PACKAGE_NAME="protonforge"
ARCH="amd64"

# ---------------------------------------------------------------- build deps
if ! command -v cmake >/dev/null 2>&1 || ! command -v dpkg-deb >/dev/null 2>&1; then
    missing="$(grep -vE '^\s*(#|$)' "$SRC/packaging/build-depends.txt" | tr '\n' ' ')"
    if [[ "$(id -u)" -eq 0 ]] && command -v apt-get >/dev/null 2>&1; then
        log "installing build dependencies"
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        # shellcheck disable=SC2086
        apt-get install -y --no-install-recommends $missing \
            || die "could not install the build dependencies: $missing"
    else
        die "build dependencies are missing and we are not root:
    $missing
    sudo apt-get install $missing"
    fi
fi

# ---------------------------------------------------------------- work copy
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
    --exclude='*.o' \
    -cf - . | tar -C "$BUILD_SRC" -xf -

# ---------------------------------------------------------------- compile
#
# The package is built from source by default, and that is not just tidiness: a
# binary carries the Qt version it was linked against as a symbol-version
# requirement. One built against Qt 6.10 installs happily on a distribution with
# Qt 6.8 — the dependency is satisfied — and then refuses to start with
#
#     libQt6Core.so.6: version `Qt_6.10' not found
#
# So a prebuilt binary may only be reused when it was built in the same
# environment the package is for. Only the caller can know that, which is why it
# has to say so explicitly rather than the script guessing from a file's presence.
if [[ "${PROTONFORGE_REUSE_BINARY:-0}" == "1" && -x "$SRC/cmake-build-release/ProtonForge" ]]; then
    log "reusing cmake-build-release/ProtonForge (PROTONFORGE_REUSE_BINARY=1)"
    BINARY="$SRC/cmake-build-release/ProtonForge"
else
    log "building the release binary (this takes a few minutes)"
    cmake -S "$BUILD_SRC" -B "$BUILD_SRC/cmake-build-release" \
        -DCMAKE_BUILD_TYPE=Release >/dev/null \
        || die "cmake configure failed"
    cmake --build "$BUILD_SRC/cmake-build-release" -j"$(nproc)" >/dev/null \
        || die "cmake build failed"
    BINARY="$BUILD_SRC/cmake-build-release/ProtonForge"
fi
[[ -x "$BINARY" ]] || die "no ProtonForge binary at $BINARY"

# ---------------------------------------------------------------- staging
PACKAGE_DIR="$WORK/${PACKAGE_NAME}_${VERSION}_${ARCH}"
mkdir -p "$PACKAGE_DIR/DEBIAN" \
         "$PACKAGE_DIR/usr/bin" \
         "$PACKAGE_DIR/usr/share/applications" \
         "$PACKAGE_DIR/usr/share/icons/hicolor/scalable/apps" \
         "$PACKAGE_DIR/usr/share/doc/${PACKAGE_NAME}"

log "staging files"
install -m 755 "$BINARY" "$PACKAGE_DIR/usr/bin/${PACKAGE_NAME}"
install -m 644 "$BUILD_SRC/org.protonforge.ProtonForge.svg" \
    "$PACKAGE_DIR/usr/share/icons/hicolor/scalable/apps/${PACKAGE_NAME}.svg"

# The desktop entry comes from the same template CMake installs. It used to be
# a heredoc here, and the two drifted apart — different Categories, no
# StartupNotify — so the package and the Flatpak advertised different things.
sed -e "s/@PROJECT_VERSION@/${VERSION}/g" \
    "$BUILD_SRC/protonforge.desktop.in" \
    >"$PACKAGE_DIR/usr/share/applications/${PACKAGE_NAME}.desktop"
chmod 644 "$PACKAGE_DIR/usr/share/applications/${PACKAGE_NAME}.desktop"

cat >"$PACKAGE_DIR/usr/share/doc/${PACKAGE_NAME}/copyright" <<'EOF'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: protonforge
Source: https://github.com/theinvisible/proton-forge

Files: *
Copyright: 2025 ProtonForge
License: MIT
 Permission is hereby granted, free of charge, to any person obtaining a
 copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:
 .
 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.
 .
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 DEALINGS IN THE SOFTWARE.
EOF
chmod 644 "$PACKAGE_DIR/usr/share/doc/${PACKAGE_NAME}/copyright"

cat >"$PACKAGE_DIR/usr/share/doc/${PACKAGE_NAME}/changelog" <<EOF
protonforge (${VERSION}) stable; urgency=medium

  * DLSS Super Resolution, Ray Reconstruction, and Frame Generation support
  * HDR support configuration (Wayland)
  * Direct game launch with custom settings
  * Proton-CachyOS and Proton-GE management
  * Per-game settings persistence
  * Frame rate limiting for smooth motion
  * Steam CDN game artwork integration

 -- ProtonForge <noreply@protonforge>  $(date -R)
EOF
gzip -9n "$PACKAGE_DIR/usr/share/doc/${PACKAGE_NAME}/changelog"
chmod 644 "$PACKAGE_DIR/usr/share/doc/${PACKAGE_NAME}/changelog.gz"

# ---------------------------------------------------------------- control
# Installed-Size has to land inside the field block, not after Description —
# a folded Description swallows anything appended below it.
INSTALLED_SIZE="$(du -sk "$PACKAGE_DIR" | cut -f1)"
sed -e "s/@VERSION@/${VERSION}/g" \
    -e "s|^Homepage:|Installed-Size: ${INSTALLED_SIZE}\nHomepage:|" \
    "$BUILD_SRC/debian/control" >"$PACKAGE_DIR/DEBIAN/control"
chmod 644 "$PACKAGE_DIR/DEBIAN/control"

grep -q '^Installed-Size:' "$PACKAGE_DIR/DEBIAN/control" \
    || die "Installed-Size was not inserted — did debian/control lose its Homepage field?"

# ---------------------------------------------------------------- package
log "building the package"
dpkg-deb --build --root-owner-group "$PACKAGE_DIR" >/dev/null

DEB="$OUT/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
mv -f "${PACKAGE_DIR}.deb" "$DEB"

log "$DEB"
log ""
log "To install:"
log "  sudo apt-get install ./$(basename "$DEB")"

# The path the caller is after — must stay the last line on stdout.
printf '%s\n' "$DEB"
