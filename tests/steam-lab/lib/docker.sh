# shellcheck shell=bash
#
# Container support for the distribution matrix.
#
# One image per target, built from docker/distro/Dockerfile with the base image
# as a build argument. The container supplies the user space under test — the
# distribution, its Qt, its dependency graph — and nothing else.
#
# Deliberately unprivileged: no --privileged, no --cap-add, no --pid host, no
# --network by default. The package tier does not need any of it, and keeping it
# that way means these cases run anywhere docker runs, including inside a
# GitHub Actions container job.
#
#   --network none   deterministic by default. With a network the app fetches
#                    game artwork from the Steam CDN, tier badges from ProtonDB
#                    and release lists from GitHub — none of which a packaging
#                    test should depend on, and the last of which is rate
#                    limited to 60 requests an hour per address.
#   --user 1000:1000 the lab directory is bind-mounted at its own absolute path
#                    so paths mean the same thing on both sides; matching uids
#                    keeps it writable and stops the container leaving
#                    root-owned files in it.

: "${LAB_DOCKER_PREFIX:=protonforge-lab}"

# The distributions the package is expected to work on. This is the only place
# that holds distribution knowledge; 20_deb_install reads the same list.
#
#   image | expected Qt6 core package | steam tooling in the image
#
# Long-term releases only. Ubuntu's interim releases live nine months, so a
# regression found on one is a regression on a target nobody will still be running
# by the time it is fixed — and both LTS releases plus both Debian stables already
# span Qt 6.4 to 6.10, which is the axis that actually matters here.
#
# Qt versions, for orientation: bookworm 6.4, trixie 6.8 (what the Flatpak runtime
# targets), noble 6.4, resolute 6.10.
LAB_DISTROS_DEFAULT=(
    "debian:bookworm|libqt6core6|no"
    "debian:trixie|libqt6core6t64|yes"
    "ubuntu:24.04|libqt6core6t64|no"
    "ubuntu:26.04|libqt6core6t64|yes"
)

# dock_distros -> the selected targets, one per line
#
# LAB_DISTROS narrows a run down, e.g. LAB_DISTROS="debian:trixie".
dock_distros() {
    if [[ -z "${LAB_DISTROS:-}" ]]; then
        printf '%s\n' "${LAB_DISTROS_DEFAULT[@]}"
        return 0
    fi
    local -a wanted
    read -r -a wanted <<<"$LAB_DISTROS"
    local want entry found=0
    for want in "${wanted[@]}"; do
        for entry in "${LAB_DISTROS_DEFAULT[@]}"; do
            if [[ "${entry%%|*}" == "$want" ]]; then
                printf '%s\n' "$entry"
                found=1
            fi
        done
    done
    (( found )) || die "no known target matches LAB_DISTROS='$LAB_DISTROS'.
Known: $(printf '%s ' "${LAB_DISTROS_DEFAULT[@]%%|*}")"
}

dock_require() {
    have docker || die "docker is missing: sudo apt install docker.io"
    docker info >/dev/null 2>&1 \
        || die "cannot talk to the docker daemon — is the user in the 'docker' group?
    sudo usermod -aG docker \$USER && newgrp docker"
}

# dock_slug <image> -> "ubuntu:24.04" becomes "ubuntu-24.04"
dock_slug() { printf '%s' "${1//[:\/]/-}"; }

dock_tag()     { printf '%s:%s' "$LAB_DOCKER_PREFIX" "$(dock_slug "$1")"; }
dock_deb_dir() { printf '%s/%s' "$LAB_DEB_DIR" "$(dock_slug "$1")"; }

# dock_cached_deb <image>
#
# Prints the path of a cached package if it is still newer than every source
# file, otherwise nothing. Building takes minutes; a run that changed no code
# should not pay for it again.
dock_cached_deb() {
    local dir; dir="$(dock_deb_dir "$1")"
    local deb; deb="$(find "$dir" -maxdepth 1 -name 'protonforge_*.deb' 2>/dev/null | head -n1)"
    [[ -n "$deb" ]] || return 1

    local newer
    newer="$(find "$REPO_ROOT/src" "$REPO_ROOT/CMakeLists.txt" "$REPO_ROOT/debian" \
        "$REPO_ROOT/build-deb.sh" "$REPO_ROOT/packaging" \
        "$REPO_ROOT/protonforge.desktop.in" \
        -newer "$deb" -print -quit 2>/dev/null)"
    [[ -n "$newer" ]] && return 1

    printf '%s' "$deb"
}

# dock_build <image> [--rebuild]
#
# The build context is a temporary directory holding only the Dockerfile and
# packaging/build-depends.txt, so a source change never invalidates the apt
# layers. The agent script is not in it — see the note at the end of the
# Dockerfile.
dock_build() {
    local image="$1" rebuild="${2:-}" ctx rc

    if [[ "$rebuild" != "--rebuild" ]] && dock_image_fresh "$image"; then
        return 0
    fi

    local steam="no" entry
    while IFS= read -r entry; do
        [[ "${entry%%|*}" == "$image" ]] && steam="${entry##*|}"
    done < <(printf '%s\n' "${LAB_DISTROS_DEFAULT[@]}")

    ctx="$(mktemp -d)"
    cp "$LAB_SRC_DIR/docker/distro/Dockerfile" "$ctx/"
    cp "$REPO_ROOT/packaging/build-depends.txt" "$ctx/build-depends.txt"

    local log="${CASE_OUT_DIR:-$LAB_OUT_DIR}/docker-build-$(dock_slug "$image").log"
    mkdir -p "$(dirname "$log")"
    info "building the image for $image (steam tooling: $steam) — first time takes a while"
    docker build \
        --build-arg "BASE_IMAGE=$image" \
        --build-arg "WITH_STEAM=$steam" \
        --build-arg "INPUT_DIGEST=$(dock_input_digest "$image")" \
        -t "$(dock_tag "$image")" "$ctx" >"$log" 2>&1
    rc=$?
    rm -rf "$ctx"
    (( rc == 0 )) || die "image build for $image failed:
$(tail -n 25 "$log")"
    return 0
}

# dock_input_digest <image> -> a digest of everything the image is built from
#
# The agent script is deliberately not part of it — that is mounted, not baked,
# so editing a test never costs an image rebuild.
dock_input_digest() {
    local steam="no" entry
    while IFS= read -r entry; do
        [[ "${entry%%|*}" == "$1" ]] && steam="${entry##*|}"
    done < <(printf '%s\n' "${LAB_DISTROS_DEFAULT[@]}")

    { printf '%s\n%s\n' "$1" "$steam"
      cat "$LAB_SRC_DIR/docker/distro/Dockerfile" "$REPO_ROOT/packaging/build-depends.txt"
    } | sha256sum | cut -c1-16
}

# dock_image_fresh <image>
#
# The image exists and was built from exactly these inputs. Compared by digest
# rather than by timestamp: BuildKit does not give a rebuilt image a fresh
# .Created, so mtimes would report a changed Dockerfile as up to date.
dock_image_fresh() {
    local label
    label="$(docker image inspect \
        --format '{{index .Config.Labels "org.protonforge.lab.inputs"}}' \
        "$(dock_tag "$1")" 2>/dev/null)" || return 1

    [[ "$label" == "$(dock_input_digest "$1")" ]] && return 0

    [[ -n "$label" ]] && info "the image for $1 was built from different inputs — rebuilding"
    return 1
}

dock_build_all() {
    local entry image
    while IFS= read -r entry; do
        image="${entry%%|*}"
        dock_build "$image" "${1:-}"
    done < <(dock_distros)
    info "images ready"
}

# dock_run <image> <deb|build> <args for in-container.sh...>
#
# With LAB_IN_CONTAINER=1 the agent is executed directly instead of through
# docker — that is how the very same case runs as a plain step inside a GitHub
# Actions `container:` job, with no nested docker involved.
dock_run() {
    local image="$1" deb="$2"; shift 2

    mkdir -p "$(dock_deb_dir "$image")"

    if [[ "${LAB_IN_CONTAINER:-0}" == "1" ]]; then
        env PF_LAB_DIR="$PF_LAB_DIR" PF_DEB_OUT="$(dock_deb_dir "$image")" \
            bash "$LAB_SRC_DIR/docker/distro/in-container.sh" "$deb" "$@"
        return $?
    fi

    local -a mounts=(-v "$REPO_ROOT:/src:ro" -v "$PF_LAB_DIR:$PF_LAB_DIR")
    if [[ "$deb" != "build" ]]; then
        mounts+=(-v "$deb:/tmp/protonforge.deb:ro")
        deb=/tmp/protonforge.deb
    fi

    docker run --rm \
        --name "protonforge-$(dock_slug "$image")" \
        --network "$LAB_DOCKER_NET" \
        --user "$(id -u):$(id -g)" \
        -e "PF_LAB_DIR=$PF_LAB_DIR" \
        -e "PF_DEB_OUT=$(dock_deb_dir "$image")" \
        "${mounts[@]}" \
        "$(dock_tag "$image")" \
        /src/tests/steam-lab/docker/distro/in-container.sh "$deb" "$@"
}

# dock_run_root <image> <deb|build> <args...>
#
# Installing a package needs root, and dpkg is the whole point of the packaging
# case. Root inside the container is still not root outside it, and the only
# thing mounted writable is the lab directory.
dock_run_root() {
    local image="$1" deb="$2"; shift 2

    mkdir -p "$(dock_deb_dir "$image")"

    if [[ "${LAB_IN_CONTAINER:-0}" == "1" ]]; then
        env PF_LAB_DIR="$PF_LAB_DIR" PF_DEB_OUT="$(dock_deb_dir "$image")" \
            bash "$LAB_SRC_DIR/docker/distro/in-container.sh" "$deb" "$@"
        return $?
    fi

    local -a mounts=(-v "$REPO_ROOT:/src:ro" -v "$PF_LAB_DIR:$PF_LAB_DIR")
    if [[ "$deb" != "build" ]]; then
        mounts+=(-v "$deb:/tmp/protonforge.deb:ro")
        deb=/tmp/protonforge.deb
    fi

    docker run --rm \
        --name "protonforge-$(dock_slug "$image")" \
        --network "$LAB_DOCKER_NET" \
        -e "PF_LAB_DIR=$PF_LAB_DIR" \
        -e "PF_DEB_OUT=$(dock_deb_dir "$image")" \
        -e "HOST_UID=$(id -u)" \
        -e "HOST_GID=$(id -g)" \
        "${mounts[@]}" \
        "$(dock_tag "$image")" \
        /src/tests/steam-lab/docker/distro/in-container.sh "$deb" "$@"
}

dock_cleanup() {
    docker rm -f "protonforge-$(dock_slug "$1")" >/dev/null 2>&1 || true
}

dock_cleanup_all() {
    local entry
    while IFS= read -r entry; do
        dock_cleanup "${entry%%|*}"
    done < <(printf '%s\n' "${LAB_DISTROS_DEFAULT[@]}")
    return 0
}

# dock_result <output file> <key> -> the value the container reported
dock_result() {
    sed -n "s/^RESULT:$2=//p" "$1" | tail -n1
}

# dock_output <output file> -> everything that was not a RESULT line
dock_output() {
    grep -vE '^RESULT:' "$1" 2>/dev/null
}
