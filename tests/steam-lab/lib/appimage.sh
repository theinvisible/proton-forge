# shellcheck shell=bash
#
# Building the AppImage and running it where it was not built.
#
# The build is not this file's business: build-appimage.sh already puts itself in
# a container, so ai_build() just calls it — the same file the release workflow
# calls, no second path. What this file adds is the other half, which only a test
# wants: running the finished artifact somewhere it was not built.
#
# That distinction is the whole point of the tier. The build image has Qt, zlib
# and libsecret installed, so an AppImage that bundled nothing at all would still
# start there. docker/appimage/Dockerfile has none of them — only the libraries
# every AppImage is entitled to expect from its host — so the same file either
# runs or does not. See that Dockerfile for where the line is drawn and why.

: "${LAB_APPIMAGE_HOST_IMAGE:=debian:bookworm}"

ai_host_tag() { printf '%s:appimage-host' "$LAB_DOCKER_PREFIX"; }

ai_host_digest() {
    { printf '%s\n' "$LAB_APPIMAGE_HOST_IMAGE"
      cat "$LAB_SRC_DIR/docker/appimage/Dockerfile"
    } | sha256sum | cut -c1-16
}

# ai_host_image -- build the run environment if it is not current
ai_host_image() {
    local label
    label="$(docker image inspect \
        --format '{{index .Config.Labels "org.protonforge.lab.inputs"}}' \
        "$(ai_host_tag)" 2>/dev/null)" || label=""
    [[ "$label" == "$(ai_host_digest)" ]] && return 0

    local ctx log
    ctx="$(mktemp -d)"
    cp "$LAB_SRC_DIR/docker/appimage/Dockerfile" "$ctx/"
    log="${CASE_OUT_DIR:-$LAB_OUT_DIR}/docker-build-appimage-host.log"
    mkdir -p "$(dirname "$log")"

    step "Building the AppImage run environment from $LAB_APPIMAGE_HOST_IMAGE"
    docker build \
        --build-arg "BASE_IMAGE=$LAB_APPIMAGE_HOST_IMAGE" \
        --build-arg "INPUT_DIGEST=$(ai_host_digest)" \
        -t "$(ai_host_tag)" "$ctx" >"$log" 2>&1
    local rc=$?
    rm -rf "$ctx"
    (( rc == 0 )) || die "the AppImage run environment could not be built:
$(tail -n 25 "$log")"
    return 0
}

# Where the artifact and the extraction land. Inside $LAB_RUN_DIR, which is
# mounted into the containers at its own absolute path, so the paths mean the same
# thing on both sides.
LAB_APPIMAGE_DIR="$LAB_RUN_DIR/appimage"

# ai_build -> path of the built AppImage
#
# Prints nothing but the path; progress goes to the case log. build-appimage.sh
# builds its own image on first use, which is the slow part.
ai_build() {
    mkdir -p "$LAB_APPIMAGE_DIR"
    local log="${CASE_OUT_DIR:-$LAB_OUT_DIR}/appimage-build.log"
    local artifact
    artifact="$(bash "$REPO_ROOT/build-appimage.sh" "$REPO_ROOT" "$LAB_APPIMAGE_DIR" \
        2>"$log" | tail -n1)" || return 1
    [[ -n "$artifact" && -f "$artifact" ]] || return 1
    printf '%s' "$artifact"
}

# ai_bare_run <appimage> <args...> -- run it where it was not built
#
# The environment mirrors app_env() in lib/app.sh, for the same reasons: the fake
# HOME, the stub PATH, offscreen Qt, no startup checks. Two additions belong to
# this tier:
#
#   APPIMAGE_EXTRACT_AND_RUN  the type-2 runtime mounts itself with FUSE, which a
#                             container does not have without privileges it needs
#                             for nothing else. Extraction is the documented way
#                             out and costs a second per run.
#   the three sentinels       LD_LIBRARY_PATH, QT_PLUGIN_PATH and XDG_DATA_DIRS
#                             are set to recognisable values, on purpose. The
#                             AppRun is about to prepend the bundle to each and
#                             save the old value; what a launched game must end up
#                             with is exactly these strings. That turns "did the
#                             bundle leak" into an equality check — which also
#                             fails if the value is dropped instead of restored,
#                             and unlike a "does not contain /tmp/.mount" check it
#                             holds under --appimage-extract-and-run, where the
#                             bundle is not a mount at all.
LAB_APPIMAGE_SENTINEL="/lab-sentinel-ld-path"
LAB_APPIMAGE_SENTINEL_QT="/lab-sentinel-qt-plugins"
LAB_APPIMAGE_SENTINEL_XDG="/lab-sentinel-xdg-data"

ai_bare_run() {
    local appimage="$1"; shift
    mkdir -p "$LAB_APPIMAGE_DIR/extract" "$LAB_APP_HOME" "$LAB_RUN_DIR"
    # >&2: the caller captures our stdout, and the image build narrates on it.
    ai_host_image >&2

    docker run --rm \
        --name "${LAB_DOCKER_PREFIX}-appimage-bare" \
        --network none \
        --user "$(id -u):$(id -g)" \
        -w "$LAB_APPIMAGE_DIR/extract" \
        -v "$PF_LAB_DIR:$PF_LAB_DIR" \
        -e "HOME=$LAB_APP_HOME" \
        -e "XDG_CONFIG_HOME=$LAB_APP_HOME/.config" \
        -e "XDG_CACHE_HOME=$LAB_APP_HOME/.cache" \
        -e "XDG_DATA_HOME=$LAB_APP_HOME/.local/share" \
        -e "TMPDIR=$LAB_APP_TMP" \
        -e "PATH=$LAB_STUB_BIN:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
        -e "QT_QPA_PLATFORM=offscreen" \
        -e "PROTONFORGE_NO_STARTUP_CHECKS=1" \
        -e "APPIMAGE_EXTRACT_AND_RUN=1" \
        -e "LD_LIBRARY_PATH=$LAB_APPIMAGE_SENTINEL" \
        -e "QT_PLUGIN_PATH=$LAB_APPIMAGE_SENTINEL_QT" \
        -e "XDG_DATA_DIRS=$LAB_APPIMAGE_SENTINEL_XDG" \
        --entrypoint "$appimage" \
        "$(ai_host_tag)" "$@"
}

# ai_extract <appimage> -> the AppDir, for looking at what is inside the bundle
#
# Extracted in the run image rather than on the host: the runtime is the
# artifact's own, and unpacking it here means the host needs neither FUSE nor an
# appimagetool.
ai_extract() {
    local appimage="$1"
    local dir="$LAB_APPIMAGE_DIR/extract"
    rm -rf "$dir/squashfs-root"
    mkdir -p "$dir"
    # >&2, or the narration would end up in the AppDir path we print.
    ai_host_image >&2

    docker run --rm \
        --network none \
        --user "$(id -u):$(id -g)" \
        -w "$dir" \
        -v "$PF_LAB_DIR:$PF_LAB_DIR" \
        --entrypoint "$appimage" \
        "$(ai_host_tag)" --appimage-extract >/dev/null 2>&1 || return 1

    [[ -d "$dir/squashfs-root" ]] || return 1
    printf '%s' "$dir/squashfs-root"
}

ai_cleanup() {
    docker rm -f "${LAB_DOCKER_PREFIX}-appimage-bare" >/dev/null 2>&1 || true
    return 0
}
