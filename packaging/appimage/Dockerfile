# The AppImage build environment.
#
# The build happens in here and nowhere else — locally too. build-appimage.sh
# re-executes itself in this image when it is started outside a container, so a
# developer's machine never grows an apt package or a downloaded linuxdeploy for
# the sake of a release artifact. Same reasoning as the Flatpak tier's container
# (tests/steam-lab/docker/flatpak/Dockerfile), one layer up: there it is about a
# clean room, here it is about not touching the host at all.
#
# Why bookworm: an AppImage runs on every glibc at least as new as the one it was
# built against, so the oldest supported target is the right base. bookworm has
# glibc 2.36 and Qt 6.4.2, which covers all four entries in packaging/distros.txt
# and a good deal of what is older. Building on something newer would silently
# narrow that, and nothing in the artifact would say so.
#
# Nothing here is described twice: the package lists are the same two files every
# other build path reads, and the tools come from the same script the release
# workflow runs.
#
# Build arguments:
#   BASE_IMAGE     the distribution the artifact's glibc floor comes from
#   INPUT_DIGEST   a digest of everything this image is built from, so a caller
#                  can tell a stale image from a current one — the same trick
#                  the lab's images use, and for the same reason: BuildKit does
#                  not give a rebuilt image a fresh .Created.

ARG BASE_IMAGE=debian:bookworm
FROM ${BASE_IMAGE}

ARG INPUT_DIGEST=unknown
LABEL org.protonforge.lab.inputs="${INPUT_DIGEST}"

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8

# packaging/build-depends.txt is the application's build dependencies, shared with
# the .deb and CI. packaging/appimage/build-depends.txt is what only an AppImage
# needs, with a note per package on why. Only those two are copied in, so a source
# change does not invalidate this layer.
COPY build-depends.txt /tmp/build-depends.txt
COPY appimage-build-depends.txt /tmp/appimage-build-depends.txt
RUN apt-get update \
    && grep -hvE '^\s*(#|$)' /tmp/build-depends.txt /tmp/appimage-build-depends.txt \
       | xargs apt-get install -y --no-install-recommends \
    && rm -rf /var/lib/apt/lists/* /tmp/*-depends.txt

# linuxdeploy, its Qt plugin and the AppImage runtime, pinned and verified by the
# same script the release workflow uses.
COPY tools.env install-tools.sh /tmp/appimage-tools/
RUN bash /tmp/appimage-tools/install-tools.sh && rm -rf /tmp/appimage-tools

# The build script is not copied in. It is packaging logic that changes far more
# often than this image does, and baking it in means every edit needs a rebuild —
# or worse, silently runs the stale copy. The source tree is mounted instead.
ENTRYPOINT ["/bin/bash"]
