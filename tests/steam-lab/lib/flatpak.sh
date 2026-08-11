# shellcheck shell=bash
#
# Building, installing and running the Flatpak.
#
# This tier runs on the host by default rather than in a container, and the
# reason is worth writing down because it looks like the lazy choice and is not:
#
#   Flatpak runs everything through bubblewrap, and bubblewrap calls pivot_root
#   unconditionally. Docker's default seccomp profile has no rule for
#   pivot_root at all, so it returns EPERM — which means --cap-add SYS_ADMIN is
#   *not* enough on its own; you also need --security-opt seccomp=unconfined and
#   (on any AppArmor host, so Debian and Ubuntu) --security-opt
#   apparmor=unconfined. That is --privileged in all but name. Installing the
#   runtime during `docker build` needs more still: BuildKit's
#   `RUN --security=insecure`, which requires a custom buildx builder started
#   with an extra entitlement. Upstream's own flatpak-github-actions simply
#   writes `options: --privileged` and moves on.
#
# So: the host, with FLATPAK_USER_DIR pointed at the lab directory. That is the
# Flatpak equivalent of the fake $HOME — the developer's real --user
# installations are never touched, and nothing needs elevated privileges.
# LAB_FLATPAK_DOCKER=1 switches to the privileged container instead, which is
# what a clean-room check wants and what CI can afford.

: "${LAB_FLATPAK_APP_ID:=org.protonforge.ProtonForge}"
# ubuntu:24.04 deliberately, not the newest: it is what GitHub's ubuntu-latest
# runner is, and therefore which flatpak-builder the tag workflow uses. The
# version matters more than it looks — 1.4.8 passes meson a libdir and 1.4.2 does
# not, which is the difference between a module landing in /app/lib and in
# /app/lib64, and a clean-room check on the newer one passed a manifest that CI
# could not build. Override with LAB_FLATPAK_IMAGE to test another distribution.
: "${LAB_FLATPAK_IMAGE:=ubuntu:24.04}"

# shellcheck source=docker.sh
source "$LAB_SRC_DIR/lib/docker.sh"

FP_CONTAINER="${LAB_DOCKER_PREFIX}-flatpak"
# Container-local, not on the bind mount: the socket belongs to the container's
# lifetime, and a stale one in the lab directory would outlive it.
FP_DBUS_SOCKET="/tmp/lab-session-bus"

# The container's home is the *host* user's home path, with a writable directory
# mounted there. That is not cosmetic: `--filesystem=home` is one of the grants
# under test, and it means "whatever this user's home is". With the container's
# own /home/labuser it would mean a directory the fixtures are not in, and every
# sandbox-visibility assertion would fail for a reason that has nothing to do
# with the manifest. Aligning the path makes the grant mean the same thing on
# both sides — and the lab directory then sits under it exactly as it does on the
# host, because $PF_LAB_DIR defaults to ~/.cache/protonforge-testlab.
FP_DOCKER_HOME="$HOME"
FP_DOCKER_HOME_DATA="$LAB_FLATPAK_DIR/container-home"
# …unless the lab directory lives somewhere else entirely, in which case nothing
# can be nested under home and the case is told to skip those assertions rather
# than report a grant that is fine as broken.
fp_docker_home_aligned() { [[ "$PF_LAB_DIR" == "$HOME"/* ]]; }

fp_user_dir() { printf '%s' "$LAB_FLATPAK_DIR"; }

# fp_docker -> 0 when this tier runs in the privileged container
fp_docker() { [[ "${LAB_FLATPAK_DOCKER:-0}" == "1" ]]; }

# fp_env <command...> -- flatpak against the isolated installation
#
# The one seam between the two modes. Every flatpak invocation in this file goes
# through here, so the container mode is a different exec rather than a second
# copy of the tier: -w matches the caller's working directory (fp_build cds into
# the run directory before calling), and the lab paths are identical inside the
# container because they are bind-mounted at the same place.
fp_env() {
    if fp_docker; then
        docker exec \
            -w "$LAB_RUN_DIR" \
            -e "FLATPAK_USER_DIR=$LAB_FLATPAK_DIR" \
            -e "HOME=$FP_DOCKER_HOME" \
            -e "DBUS_SESSION_BUS_ADDRESS=unix:path=$FP_DBUS_SOCKET" \
            "$FP_CONTAINER" "$@"
    else
        env FLATPAK_USER_DIR="$LAB_FLATPAK_DIR" "$@"
    fi
}

fp_require() {
    python3 -c 'import yaml' 2>/dev/null \
        || die "python3-yaml is missing (needed to derive a buildable manifest): sudo apt install python3-yaml"
    mkdir -p "$LAB_FLATPAK_DIR"

    if fp_docker; then
        dock_require
        fp_docker_build
        fp_docker_start
    else
        have flatpak || die "flatpak is missing: sudo apt install flatpak"
        have flatpak-builder || die "flatpak-builder is missing: sudo apt install flatpak-builder"
    fi

    fp_env flatpak remote-add --user --if-not-exists flathub \
        https://dl.flathub.org/repo/flathub.flatpakrepo >/dev/null 2>&1 \
        || warn "could not add the flathub remote to $LAB_FLATPAK_DIR"
    return 0
}

# ── the privileged container ────────────────────────────────────────────────

fp_docker_tag()    { printf '%s:flatpak-%s' "$LAB_DOCKER_PREFIX" "$(dock_slug "$LAB_FLATPAK_IMAGE")"; }
fp_docker_digest() {
    { printf '%s\n' "$LAB_FLATPAK_IMAGE"
      cat "$LAB_SRC_DIR/docker/flatpak/Dockerfile"
    } | sha256sum | cut -c1-16
}

fp_docker_build() {
    local label
    label="$(docker image inspect \
        --format '{{index .Config.Labels "org.protonforge.lab.inputs"}}' \
        "$(fp_docker_tag)" 2>/dev/null)" || label=""
    [[ "$label" == "$(fp_docker_digest)" ]] && return 0

    local ctx log
    ctx="$(mktemp -d)"
    cp "$LAB_SRC_DIR/docker/flatpak/Dockerfile" "$ctx/"
    log="${CASE_OUT_DIR:-$LAB_OUT_DIR}/docker-build-flatpak.log"
    mkdir -p "$(dirname "$log")"

    step "Building the flatpak image from $LAB_FLATPAK_IMAGE"
    docker build \
        --build-arg "BASE_IMAGE=$LAB_FLATPAK_IMAGE" \
        --build-arg "INPUT_DIGEST=$(fp_docker_digest)" \
        -t "$(fp_docker_tag)" "$ctx" >"$log" 2>&1
    local rc=$?
    rm -rf "$ctx"
    (( rc == 0 )) || die "the flatpak image build failed:
$(tail -n 25 "$log")"
    return 0
}

# fp_docker_start -- a long-lived container the case then execs into
#
# --privileged, and lib/flatpak.sh's header note explains why nothing smaller
# does. --network is *not* $LAB_DOCKER_NET: that defaults to none to keep the
# other tiers deterministic, and this one cannot work without reaching Flathub.
# The repository is read-only — flatpak-builder copies a `type: dir` source
# rather than building in place — while the lab directory is writable and
# mounted at its host path, which is what lets the case keep talking in host
# paths on both sides of the seam.
fp_docker_start() {
    mkdir -p "$FP_DOCKER_HOME_DATA"
    fp_docker_home_aligned || warn \
"PF_LAB_DIR is not under \$HOME, so nothing can be nested under the container's
home — 70_flatpak will skip the sandbox-visibility assertions."

    # Reuse a container that is already up and built from the current image. The
    # runner satisfies `lab-requires: flatpak` before the case starts and the case
    # asks again for itself, so without this the second call would tear down the
    # container the first one just made.
    if [[ "$(docker inspect -f '{{.State.Running}} {{.Config.Image}}' \
                "$FP_CONTAINER" 2>/dev/null)" == "true $(fp_docker_tag)" ]]; then
        fp_docker_bus
        fp_docker_trap
        return 0
    fi
    docker rm -f "$FP_CONTAINER" >/dev/null 2>&1 || true

    local args=(
        --detach --rm
        --name "$FP_CONTAINER"
        --privileged
        --network bridge
        --user "$(id -u):$(id -g)"
        -v "$REPO_ROOT:$REPO_ROOT:ro"
        # A writable stand-in for the developer's home, at the host's home path,
        # with the lab directory mounted under it — see FP_DOCKER_HOME above.
        -v "$FP_DOCKER_HOME_DATA:$FP_DOCKER_HOME"
        -v "$PF_LAB_DIR:$PF_LAB_DIR"
        -e "HOME=$FP_DOCKER_HOME"
        -e "FLATPAK_USER_DIR=$LAB_FLATPAK_DIR"
    )
    [[ -e /dev/fuse ]] && args+=(--device /dev/fuse)

    docker run "${args[@]}" "$(fp_docker_tag)" -c 'sleep infinity' >/dev/null \
        || die "could not start $FP_CONTAINER"

    # GLib reads HOME first but falls back to the passwd entry, and bwrap builds
    # the sandbox's own passwd from it. Both should agree, or the two disagree
    # about what home means the moment something is run without an explicit HOME.
    # Edited in place rather than with usermod, which refuses outright while a
    # process is running as that user — and the container's own PID 1 is.
    docker exec -u 0 "$FP_CONTAINER" sed -i \
        "s|^\(labuser:[^:]*:[0-9]*:[0-9]*:[^:]*:\)[^:]*:|\1$FP_DOCKER_HOME:|" /etc/passwd \
        >/dev/null 2>&1 || warn "could not align the container user's home directory"

    # Prove the seam works before the case starts trusting it: a container that
    # cannot run bubblewrap fails every assertion below for the same reason, and
    # saying so once here is the difference between a diagnosis and a mystery.
    fp_docker_bus
    fp_env flatpak --version >/dev/null 2>&1 \
        || die "flatpak does not run inside $FP_CONTAINER"
    fp_docker_trap

    info "flatpak tier running in $FP_CONTAINER ($LAB_FLATPAK_IMAGE, privileged)"
    return 0
}

# fp_docker_bus -- the two buses a bundle install turns out to need
#
# The system bus is the one that is easy to miss. `flatpak install` asks
# malcontent for "parental controls details" of the ref it is about to install,
# and that is a system-bus call: with no bus at all the install fails with
# "Could not connect: No such file or directory" *after* the build, which reads
# as a broken bundle and is nothing of the sort. Nothing has to answer the call —
# the bus merely has to exist — so an empty dbus-daemon is enough, and its
# warnings about unknown malcontent usernames are expected.
#
# The session bus is for the app itself, which links QtDBus. Its address is fixed
# rather than printed because every fp_env is its own exec and cannot inherit it.
# Started as root (the container runs as the host user) since /run/dbus is not
# writable otherwise; the socket is world-writable, as a system bus's is.
fp_docker_bus() {
    docker exec "$FP_CONTAINER" test -S /run/dbus/system_bus_socket 2>/dev/null \
        || docker exec -u 0 "$FP_CONTAINER" sh -c \
            'mkdir -p /run/dbus && dbus-uuidgen --ensure && dbus-daemon --system --fork' \
            >/dev/null 2>&1

    docker exec "$FP_CONTAINER" test -S "$FP_DBUS_SOCKET" 2>/dev/null \
        || docker exec -d "$FP_CONTAINER" \
            dbus-daemon --session --nofork --address="unix:path=$FP_DBUS_SOCKET" \
            >/dev/null 2>&1

    local i
    for (( i = 0; i < 50; i++ )); do
        if docker exec "$FP_CONTAINER" test -S "$FP_DBUS_SOCKET" 2>/dev/null \
           && docker exec "$FP_CONTAINER" test -S /run/dbus/system_bus_socket 2>/dev/null; then
            return 0
        fi
        sleep 0.1
    done
    warn "a D-Bus socket is missing in $FP_CONTAINER — installing the bundle will fail"
    return 0
}

# fp_docker_trap -- take the container down however this process ends
#
# The container outlives its caller otherwise, including when a case stops early
# at a fail. Both the runner and a case get here; only the case has
# case_teardown, and it must still run first so it sees the real exit status.
fp_docker_trap() {
    if declare -F case_teardown >/dev/null; then
        trap 'case_teardown; fp_docker_stop' EXIT
    else
        trap fp_docker_stop EXIT
    fi
}

fp_docker_stop() {
    fp_docker || return 0
    docker rm -f "$FP_CONTAINER" >/dev/null 2>&1 || true
    return 0
}

# fp_runtime_version -> the runtime-version the committed manifest asks for
fp_runtime_version() {
    python3 - "$REPO_ROOT/$LAB_FLATPAK_APP_ID.yml" <<'PY'
import sys, yaml
m = yaml.safe_load(open(sys.argv[1]))
print(m.get("runtime-version", ""))
PY
}

# fp_runtime_available -> 0 when the pinned runtime and SDK can still be had
#
# The manifest pins a version. Flathub retires old branches, so "the runtime the
# manifest asks for no longer exists" is a real state — and a finding in its own
# right rather than a reason for the harness to fall over.
fp_runtime_available() {
    local version; version="$(fp_runtime_version)"
    [[ -n "$version" ]] || return 1
    fp_env flatpak remote-info --user flathub "org.kde.Platform//$version" >/dev/null 2>&1 \
        && fp_env flatpak remote-info --user flathub "org.kde.Sdk//$version" >/dev/null 2>&1
}

fp_install_runtime() {
    local version; version="$(fp_runtime_version)"
    step "Installing org.kde.Platform//$version and org.kde.Sdk//$version (once, ~1.5 GB)"
    fp_env flatpak install --user -y --noninteractive flathub \
        "org.kde.Platform//$version" "org.kde.Sdk//$version" \
        >"${CASE_OUT_DIR:-$LAB_OUT_DIR}/flatpak-runtime.log" 2>&1 \
        || die "could not install the runtime:
$(tail -n 20 "${CASE_OUT_DIR:-$LAB_OUT_DIR}/flatpak-runtime.log")"
}

# fp_devel_manifest -> path of a manifest that builds the working tree
#
# The committed manifest takes its source from a GitHub release tarball with a
# placeholder sha256, so it cannot build a checkout — the release workflow
# rewrites it at tag time. Rather than keep a second copy of the manifest (which
# would drift, and the finish-args are the interesting part), the archive source
# is swapped for a `type: dir` pointing at the repository. Everything else —
# runtime-version, finish-args, post-install — comes from the file that ships.
fp_devel_manifest() {
    local out="$LAB_RUN_DIR/manifest-devel.yml"
    mkdir -p "$LAB_RUN_DIR"

    python3 - "$REPO_ROOT/$LAB_FLATPAK_APP_ID.yml" "$out" "$REPO_ROOT" <<'PY'
import sys, yaml

src, dst, repo = sys.argv[1], sys.argv[2], sys.argv[3]
manifest = yaml.safe_load(open(src))

# By name, not by position: the manifest builds its dependencies (libsecret,
# qtkeychain) as modules of their own, and those come first because the app
# links against them. Taking modules[0] pointed the checkout at libsecret and
# built the app from a release tarball that does not exist yet.
module = next(m for m in manifest["modules"] if m["name"] == "protonforge")
kept = [s for s in module.get("sources", []) if s.get("type") != "archive"]

# flatpak-builder resolves a source path relative to the manifest, and this
# manifest is written outside the repository — so the plain filenames the
# committed one uses ("org.protonforge.ProtonForge.desktop", ...) would not be
# found. Absolutise them against the checkout.
import os
for source in kept:
    path = source.get("path")
    if path and not os.path.isabs(path):
        source["path"] = os.path.join(repo, path)

module["sources"] = [{"type": "dir", "path": repo}] + kept

with open(dst, "w") as handle:
    yaml.safe_dump(manifest, handle, sort_keys=False, default_flow_style=False)
PY

    [[ -f "$out" ]] || die "could not derive a devel manifest"
    printf '%s' "$out"
}

# fp_build -- build from the working tree into a local repo
#
# --disable-rofiles-fuse is unconditional, exactly as upstream's own action has
# it. Without it the build fails with a misleading "Build directory not
# initialised, use flatpak build-init" because the rofiles-fuse mount silently
# never comes up. All it costs is the hardlink-checkout cache.
fp_build() {
    local manifest; manifest="$(fp_devel_manifest)"
    local log="${CASE_OUT_DIR:-$LAB_OUT_DIR}/flatpak-build.log"

    step "Building the Flatpak from the working tree"
    ( cd "$LAB_RUN_DIR" && fp_env timeout "$TIMEOUT_FLATPAK" flatpak-builder \
        --user \
        --disable-rofiles-fuse \
        --force-clean \
        --repo="$LAB_RUN_DIR/fp-repo" \
        "$LAB_RUN_DIR/fp-build" \
        "$manifest" ) >"$log" 2>&1
    local rc=$?
    (( rc == 0 )) || {
        err "flatpak-builder failed:
$(tail -n 30 "$log")"
        return 1
    }
    return 0
}

# fp_bundle -> path of the .flatpak bundle
fp_bundle() {
    local out="$LAB_RUN_DIR/protonforge.flatpak"
    fp_env flatpak build-bundle "$LAB_RUN_DIR/fp-repo" "$out" "$LAB_FLATPAK_APP_ID" \
        >"${CASE_OUT_DIR:-$LAB_OUT_DIR}/flatpak-bundle.log" 2>&1 || return 1
    printf '%s' "$out"
}

fp_install_bundle() {
    local bundle="$1"
    fp_env flatpak install --user -y --noninteractive "$bundle" \
        >"${CASE_OUT_DIR:-$LAB_OUT_DIR}/flatpak-install.log" 2>&1
}

fp_installed() {
    fp_env flatpak info "$LAB_FLATPAK_APP_ID" >/dev/null 2>&1
}

# fp_run <args...> -- run the installed app
#
# The Steam fixture trees live under $LAB_APP_HOME, and the sandbox only reaches
# what finish-args let it — which is exactly what 70_flatpak is checking, so
# HOME is handed over as-is rather than worked around.
fp_run() {
    fp_env timeout "$TIMEOUT_CLI" flatpak run \
        --env=HOME="$LAB_APP_HOME" \
        --env=XDG_CONFIG_HOME="$LAB_APP_HOME/.config" \
        --env=XDG_CACHE_HOME="$LAB_APP_HOME/.cache" \
        --env=PROTONFORGE_NO_STARTUP_CHECKS=1 \
        --filesystem="$LAB_APP_HOME" \
        "$LAB_FLATPAK_APP_ID" "$@"
}

# fp_run_bare <args...> -- no extra --filesystem, so only the manifest's own
# finish-args decide what is visible. This is what proves they are sufficient.
fp_run_bare() {
    fp_env timeout "$TIMEOUT_CLI" flatpak run \
        --env=PROTONFORGE_NO_STARTUP_CHECKS=1 \
        "$LAB_FLATPAK_APP_ID" "$@"
}

# fp_ls <path inside /app> -> a listing, for the layout assertions
fp_ls() {
    fp_env flatpak run --command=ls "$LAB_FLATPAK_APP_ID" -la "$1" 2>/dev/null
}

# fp_test_file <path inside the sandbox> -> 0 when it is readable there
#
# -e rather than -r so it works for directories as well as files; readability of a
# directory means something different and is not what any caller is asking.
fp_test_file() {
    fp_env flatpak run --command=test "$LAB_FLATPAK_APP_ID" -e "$1" 2>/dev/null
}

# fp_cleanup -- remove the installed app, nothing else
#
# Deliberately not the container: the case calls this in the middle of itself to
# get a clean install, and taking the container down there left every assertion
# after it reporting "No such container" instead of what it was checking.
fp_cleanup() {
    fp_installed && fp_env flatpak uninstall --user -y "$LAB_FLATPAK_APP_ID" >/dev/null 2>&1
    return 0
}
