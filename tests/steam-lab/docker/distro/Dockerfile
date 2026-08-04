# One image per distribution the package is supposed to work on.
#
# What varies between them is what the test is about: which Qt the package is
# built against, and whether its declared dependencies can actually be resolved
# there. Debian bookworm still has libqt6core6; trixie, noble, questing and
# resolute renamed it to libqt6core6t64 in the 64-bit time_t transition and left
# a versioned Provides behind. debian/control names the old one, so it resolves —
# but by grace of the Qt packaging graph rather than by declaration, and this
# matrix is what notices the day that stops being true.
#
# The image contains no ProtonForge. The package is built and installed at run
# time so every run tests the current source.
#
# Build arguments:
#   BASE_IMAGE   debian:bookworm, debian:trixie, ubuntu:24.04, ubuntu:26.04, ...
#   WITH_STEAM   yes -> steamcmd plus Valve's bootstrap tarball, so one case can
#                       check ProtonForge's path assumptions against what Steam
#                       itself creates. No account is involved: steamcmd's
#                       anonymous login is enough, and it needs no display.

ARG BASE_IMAGE=ubuntu:26.04
FROM ${BASE_IMAGE}

ARG WITH_STEAM=no

# A digest of everything this image is built from, so the harness can tell a
# stale image from a current one. Comparing timestamps does not work: BuildKit
# does not give a rebuilt image a fresh .Created, so a changed Dockerfile can
# leave an image that looks newer than its own inputs.
ARG INPUT_DIGEST=unknown
LABEL org.protonforge.lab.inputs="${INPUT_DIGEST}"

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8

# Build dependencies come from packaging/build-depends.txt, not from a second
# list kept in here. Only that file is copied in, so a source change does not
# invalidate this layer.
COPY build-depends.txt /tmp/build-depends.txt
RUN apt-get update \
    && grep -vE '^\s*(#|$)' /tmp/build-depends.txt | xargs apt-get install -y --no-install-recommends \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        desktop-file-utils \
        python3 \
        xvfb \
        x11-utils \
        binutils \
    && rm -rf /var/lib/apt/lists/*

# uid 1000 matches the host user, so the bind-mounted lab directory stays
# writable from both sides and the container leaves nothing root-owned behind.
# Steam additionally refuses to run as root outright (bin_steam.sh: "Cannot run
# as root user"), which makes an unprivileged user mandatory rather than tidy.
RUN set -eu; \
    if getent passwd 1000 >/dev/null; then \
        old="$(getent passwd 1000 | cut -d: -f1)"; \
        usermod --login labuser --home /home/labuser --move-home \
                --shell /bin/bash "$old"; \
    else \
        useradd --uid 1000 --create-home --shell /bin/bash labuser; \
    fi

# ---------------------------------------------------------------- real Steam
#
# Two independent sources of genuinely Steam-written files, neither of which
# needs an account, a display or any privilege:
#
#   * steamcmd. On Debian and Ubuntu its wrapper deliberately creates
#     ~/.steam/steam and ~/.steam/root pointing at ~/.local/share/Steam, i.e.
#     the same layout a desktop install has. `+login anonymous +app_update 1007`
#     then writes a real steamapps/libraryfolders.vdf and appmanifest_1007.acf.
#     This is baked in at build time so a run pays nothing for it.
#
#   * Valve's own steam-launcher package, kept only for its bootstrap tarball.
#     Extracting that is precisely what the client's first run does, and it is
#     how the ~/.steam/steam symlink comes into existence — one of the paths
#     SteamPaths probes for.
#
# steamcmd is the one package here that still asks a debconf question, and its
# preinst aborts under `set -e` if it goes unanswered. The owner field is
# "steam", not "steamcmd".
# Note there is no `set -e` in this layer, and every step ends in `|| true`.
# steamcmd is an optional extra: it is in multiverse on Ubuntu and non-free on
# Debian, both of which can be unavailable or renamed. If it cannot be had, the
# image is still perfectly good for the packaging tier and 50_real_steam skips
# itself — so nothing here may fail the build.
#
# Ubuntu's own images already enable multiverse, so only Debian needs a change.
RUN set -u; \
    if [ "$WITH_STEAM" = yes ]; then \
        dpkg --add-architecture i386 || true; \
        sed -i 's/^Components: main$/Components: main contrib non-free/' \
            /etc/apt/sources.list.d/debian.sources 2>/dev/null || true; \
        echo steam steam/question select "I AGREE" | debconf-set-selections || true; \
        echo steam steam/license note '' | debconf-set-selections || true; \
        apt-get update || true; \
        apt-get install -y --no-install-recommends steamcmd wget xz-utils || true; \
        rm -rf /var/lib/apt/lists/*; \
        if [ -x /usr/games/steamcmd ]; then \
            echo "steamcmd installed"; \
        else \
            echo "steamcmd is not available here — 50_real_steam will skip"; \
        fi; \
    fi

RUN set -u; \
    if [ "$WITH_STEAM" = yes ] && [ -x /usr/games/steamcmd ]; then \
        runuser -u labuser -- /usr/games/steamcmd \
            +force_install_dir /home/labuser/.local/share/Steam \
            +login anonymous +app_update 1007 validate +quit \
            >/tmp/steamcmd.log 2>&1 || cat /tmp/steamcmd.log; \
        ls -la /home/labuser/.local/share/Steam/steamapps/ 2>/dev/null || true; \
    fi

RUN set -u; \
    if [ "$WITH_STEAM" = yes ] && command -v wget >/dev/null 2>&1; then \
        wget -q -O /tmp/steam-launcher.deb \
            https://repo.steampowered.com/steam/pool/steam/s/steam/steam-launcher_1.0.0.87_amd64.deb \
            && dpkg-deb --fsys-tarfile /tmp/steam-launcher.deb \
               | tar -C / -x ./usr/lib/steam/bootstraplinux_ubuntu12_32.tar.xz \
                            ./usr/bin/steam 2>/dev/null \
            && rm -f /tmp/steam-launcher.deb \
            || echo "steam-launcher could not be fetched — 50_real_steam will skip that part"; \
    fi

# The agent script is deliberately NOT copied in. It is test logic that changes
# far more often than this image does, and baking it in means every edit needs a
# rebuild — or worse, silently runs the stale copy. The repository is already
# mounted read-only at /src, so dock_run invokes it from there.
ENTRYPOINT ["/bin/bash"]
