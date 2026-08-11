# The Flatpak tier in a container, for LAB_FLATPAK_DOCKER=1.
#
# The host is the default for this tier and lib/flatpak.sh says why. This image
# is the clean-room alternative: nothing of the developer's flatpak setup, no
# assumption about which runtimes happen to be installed, and a machine that has
# never built this manifest before — which is the state the tag workflow builds
# in, and the state in which the missing libsecret headers went unnoticed.
#
# It must be run --privileged (see lib/flatpak.sh: bubblewrap calls pivot_root,
# which Docker's default seccomp profile answers with EPERM). The runtime is not
# baked in: installing it during `docker build` would need BuildKit's
# `RUN --security=insecure` and a custom builder. It is installed at run time
# into the bind-mounted FLATPAK_USER_DIR instead, which the host tier uses as
# well — so the ~1.5 GB download is paid once and shared between both modes.
#
# Build arguments:
#   BASE_IMAGE     the distribution whose flatpak-builder is used
#   INPUT_DIGEST   see the note in docker/distro/Dockerfile

ARG BASE_IMAGE=ubuntu:26.04
FROM ${BASE_IMAGE}

ARG INPUT_DIGEST=unknown
LABEL org.protonforge.lab.inputs="${INPUT_DIGEST}"

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8

# flatpak-builder pulls in flatpak and bubblewrap. python3-yaml is what derives
# the buildable manifest from the committed one, binutils is for reading the
# ELF of what came out, and fuse3 is here so that a run which does *not* pass
# --disable-rofiles-fuse still has a chance rather than a confusing failure.
#
# elfutils is not optional and is only a Recommends, which --no-install-recommends
# drops: flatpak-builder splits debug info out of every module it builds and
# shells out to eu-strip to do it. Without it the build dies *after* compiling
# ("Failed to execute child process eu-strip"), which reads like a manifest fault
# and is not one.
#
# dbus is here for the same class of reason: installing a bundle activates
# flatpak's authenticator over the session bus, so with no bus `flatpak install`
# fails with "Could not connect: No such file or directory" — and the app under
# test links QtDBus and expects a session bus of its own anyway. lib/flatpak.sh
# starts one daemon per container and hands its address to every exec.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        flatpak \
        flatpak-builder \
        elfutils \
        dbus \
        python3 \
        python3-yaml \
        binutils \
        fuse3 \
        xz-utils \
        zstd \
    && rm -rf /var/lib/apt/lists/*

# uid 1000 matches the host user, so the bind-mounted lab directory stays
# writable from both sides and the container leaves nothing root-owned behind.
# flatpak also wants a passwd entry for the uid it runs as: without one,
# `flatpak --user` cannot resolve a home directory and gives up.
RUN set -eu; \
    if getent passwd 1000 >/dev/null; then \
        old="$(getent passwd 1000 | cut -d: -f1)"; \
        usermod --login labuser --home /home/labuser --move-home \
                --shell /bin/bash "$old"; \
    else \
        useradd --uid 1000 --create-home --shell /bin/bash labuser; \
    fi

# As in docker/distro, no test logic is baked in: the repository is mounted and
# the case drives this container from the host through `docker exec`.
ENTRYPOINT ["/bin/bash"]
