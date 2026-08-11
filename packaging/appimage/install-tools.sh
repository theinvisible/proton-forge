#!/bin/bash
#
# Install the pinned AppImage build tools into $LINUXDEPLOY_PREFIX.
#
# Two callers, one script: packaging/appimage/Dockerfile bakes the tools into the
# build image, and .github/workflows/release.yml installs them into its own
# container. Keeping the download-and-verify in one place is the point — the
# alternative is the same wget/sha256sum dance in a Dockerfile and in a workflow,
# drifting apart until a release is built with something other than what was
# tested.
#
# Reads packaging/appimage/tools.env for the versions. Everything is verified
# against its sha256: the tags are dated releases rather than "continuous", but a
# checksum is what actually pins them.
#
# Both AppImages are extracted rather than kept as files, because executing one
# needs FUSE and a container has no reason to be given that privilege. A build
# afterwards needs no network at all.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=tools.env
. "$HERE/tools.env"

fetch() {
    local url="$1" sha="$2" dest="$3"
    wget -q -O "$dest" "$url" || { printf 'could not download %s\n' "$url" >&2; exit 1; }
    printf '%s  %s\n' "$sha" "$dest" | sha256sum -c - >/dev/null \
        || { printf '%s does not match its pinned sha256 — refusing it\n' "$url" >&2; exit 1; }
}

mkdir -p "$LINUXDEPLOY_PREFIX"
cd "$LINUXDEPLOY_PREFIX"

fetch "$LINUXDEPLOY_URL"           "$LINUXDEPLOY_SHA256"           linuxdeploy.AppImage
fetch "$LINUXDEPLOY_PLUGIN_QT_URL" "$LINUXDEPLOY_PLUGIN_QT_SHA256" linuxdeploy-plugin-qt.AppImage
fetch "$APPIMAGE_RUNTIME_URL"      "$APPIMAGE_RUNTIME_SHA256"      runtime-x86_64

for tool in linuxdeploy linuxdeploy-plugin-qt; do
    chmod +x "$tool.AppImage"
    rm -rf squashfs-root "$tool"
    "./$tool.AppImage" --appimage-extract >/dev/null
    mv squashfs-root "$tool"
    rm -f "$tool.AppImage"
done

# linuxdeploy finds its plugins on PATH, by name.
ln -sf "$LINUXDEPLOY_PREFIX/linuxdeploy-plugin-qt/AppRun" /usr/local/bin/linuxdeploy-plugin-qt

printf 'installed linuxdeploy, its Qt plugin and the AppImage runtime into %s\n' \
    "$LINUXDEPLOY_PREFIX"
