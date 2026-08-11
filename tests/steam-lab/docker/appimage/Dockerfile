# Where the finished AppImage is run: a distribution that has what an AppImage is
# entitled to expect from a host, and nothing else.
#
# The first version of this was a plain debian:bookworm, on the theory that an
# AppImage bundles everything. It does not, and deliberately so. linuxdeploy
# excludes a fixed list of libraries from every bundle — libEGL, libGLX, libOpenGL,
# libX11, libwayland-client, libfontconfig, libfreetype, libharfbuzz, libstdc++,
# libz — because they belong to the host's graphics stack and C++ runtime. A
# bundled libEGL would be loaded in preference to the one that matches the user's
# driver, which is the same mistake as bundling libnvidia-ml.
#
# So this image installs exactly that boundary: the excluded libraries, and no Qt,
# no zlib development files, no libsecret, no qtkeychain. What the bundle has to
# carry, it carries; what the host owes it, this image owes it. A missing library
# on either side shows up as
#
#     error while loading shared libraries: libX.so.N: cannot open shared object file
#
# which is exactly what a user on a fresh machine would see.
#
# Build arguments:
#   BASE_IMAGE     matches what the AppImage was built on, so glibc is not the
#                  variable under test here
#   INPUT_DIGEST   see docker/distro/Dockerfile

ARG BASE_IMAGE=debian:bookworm
FROM ${BASE_IMAGE}

ARG INPUT_DIGEST=unknown
LABEL org.protonforge.lab.inputs="${INPUT_DIGEST}"

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8

# The AppImage excludelist, as packages. Grouped by why the host owns them.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        # Graphics: driver-specific, which is the whole reason they are excluded.
        libegl1 \
        libgl1 \
        libglx0 \
        libopengl0 \
        # Display protocols. libwayland-client is excluded for the same reason the
        # GL libraries are: the compositor's ABI, not the app's.
        libx11-6 \
        libx11-xcb1 \
        libxcb1 \
        libwayland-client0 \
        # Fonts and text shaping — fontconfig in particular has to be the host's
        # or the app sees none of the user's fonts.
        libfontconfig1 \
        libfreetype6 \
        libharfbuzz0b \
        # The C++ runtime and the odds and ends the excludelist also names.
        libstdc++6 \
        zlib1g \
        libexpat1 \
        libuuid1 \
        libice6 \
        libsm6 \
        libcom-err2 \
        libgpg-error0 \
    && rm -rf /var/lib/apt/lists/*

# Deliberately absent: qt6-base, qt6-wayland, zlib1g-dev, libsecret-1-0,
# qtkeychain. If the AppImage runs here, it brought them.
ENTRYPOINT ["/bin/bash"]
