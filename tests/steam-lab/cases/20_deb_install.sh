#!/usr/bin/env bash
# lab-requires: docker
#
# The package, on every long-term release it is supposed to work on.
#
# One thing differs between the targets and it has bitten plenty of projects:
# which Qt the distribution ships, and under what package name. Debian bookworm
# still has libqt6core6; trixie, noble, questing and resolute renamed it to
# libqt6core6t64 in the 64-bit time_t transition and left a versioned Provides
# behind. debian/control names the old one, so it resolves everywhere today —
# but by grace of the Qt packaging graph rather than by declaration, and this
# matrix is what notices the day that stops being true.
#
# Every target builds its own package with build-deb.sh, the same script the
# GitHub workflow calls, then installs it the way a user would.
#
# Adding a target is one entry in LAB_DISTROS_DEFAULT in lib/docker.sh.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"
# shellcheck source=../lib/docker.sh
source "$LAB_SRC_DIR/lib/docker.sh"

case_setup
dock_require

CMAKE_VERSION="$(app_cmake_version)"
info "expecting version $CMAKE_VERSION from CMakeLists.txt"

while IFS='|' read -r IMAGE EXP_QT_PKG EXP_STEAM; do
    [[ -z "$IMAGE" ]] && continue
    SLUG="$(dock_slug "$IMAGE")"
    OUT="$CASE_OUT_DIR/$SLUG.log"

    part "$IMAGE"

    dock_cleanup "$IMAGE"
    dock_build "$IMAGE"

    # A package from an earlier run is reused unless something in the sources is
    # newer — building takes minutes, the rest of the case takes seconds.
    if DEB="$(dock_cached_deb "$IMAGE")"; then
        info "reusing $(basename "$DEB")"
        DEB_ARG="$DEB"
    else
        info "building the package inside the container (a few minutes)"
        DEB_ARG="build"
    fi

    if dock_run_root "$IMAGE" "$DEB_ARG" package >"$OUT" 2>&1; then
        ok "$IMAGE: the container ran through"
    else
        fail "$IMAGE: the container exited with an error" "$(tail -n 25 "$OUT")"
    fi
    dock_cleanup "$IMAGE"

    r() { dock_result "$OUT" "$1"; }

    assert_eq "$IMAGE: the agent finished" "yes" "$(r done)"
    info "$IMAGE: $(r distro), Qt $(r qt_version) from $(r qt_core_pkg)"

    # -- the package ------------------------------------------------------
    if [[ "$DEB_ARG" == "build" ]]; then
        assert_eq "$IMAGE: build-deb.sh produces a package" "ok" "$(r deb_build)"
    fi
    assert_eq "$IMAGE: it installs with its dependencies resolved" "ok" "$(r deb_install)"
    assert_eq "$IMAGE: dpkg -i alone is enough, no apt-get -f needed" "no" "$(r apt_fixup_needed)"
    assert_eq "$IMAGE: the installed version is the one in CMakeLists.txt" \
        "$CMAKE_VERSION" "$(r installed_version)"

    # This is the Qt-naming question the matrix exists for.
    assert_eq "$IMAGE: the expected Qt6 core package is the one present" \
        "$EXP_QT_PKG" "$(r qt_core_pkg)"

    # -- layout -----------------------------------------------------------
    assert_eq "$IMAGE: the binary is at /usr/bin/protonforge" \
        "/usr/bin/protonforge" "$(r binary_path)"
    assert_eq "$IMAGE: and is executable" "755" "$(r binary_mode)"
    assert_eq "$IMAGE: the icon is installed" "yes" "$(r icon_installed)"
    assert_eq "$IMAGE: the desktop entry is installed" "yes" "$(r desktop_installed)"
    assert_eq "$IMAGE: the copyright file is installed" "yes" "$(r copyright_installed)"
    assert_eq "$IMAGE: the changelog is gzipped" "yes" "$(r changelog_gzipped)"

    # desktop-file-validate distinguishes errors from hints. Only errors are a
    # failure; the current entry does draw one hint (Game and Utility are both
    # main categories, so it may appear twice in a menu), and turning that into
    # a red test would only teach people to ignore the check.
    case "$(r desktop_valid)" in
    clean) ok "$IMAGE: desktop-file-validate is clean" ;;
    hints) ok "$IMAGE: desktop-file-validate reports no errors (hints only)"
           info "$IMAGE: $(grep -A3 'desktop_valid=hints' "$OUT" | tail -n2)" ;;
    *)     fail "$IMAGE: desktop-file-validate reports errors" "$(dock_output "$OUT" | tail -n 15)" ;;
    esac

    # The package used to build its .desktop from a heredoc while CMake installed
    # a separate template, and the two had drifted. They come from one file now.
    assert_eq "$IMAGE: the desktop entry matches protonforge.desktop.in" \
        "yes" "$(r desktop_matches_template)"

    # -- linkage ----------------------------------------------------------
    assert_eq "$IMAGE: no unresolved symbols" "0" "$(r ldd_unresolved)"

    # libQt6DBus is linked but is not in the Depends line — it arrives
    # transitively through libqt6gui6. That works, and it works by luck rather
    # than by declaration, so both facts are recorded.
    if [[ "$(r links_dbus)" == "yes" && "$(r dbus_declared)" == "no" ]]; then
        skip "$IMAGE: libqt6dbus6 is declared" \
             "linked but undeclared — satisfied transitively via libqt6gui6"
    else
        assert_eq "$IMAGE: the DBus dependency is declared if it is linked" \
            "$(r links_dbus)" "$(r dbus_declared)"
    fi

    # -- it actually runs -------------------------------------------------
    assert_eq "$IMAGE: the installed binary reports its version" \
        "ProtonForge $CMAKE_VERSION" "$(r cli_version)"
    assert_eq "$IMAGE: and that matches CMakeLists.txt" "yes" "$(r cli_version_matches_cmake)"
    assert_eq "$IMAGE: --help produces something" "ok" "$(r cli_help)"
    assert_eq "$IMAGE: a malformed option value is a usage error" \
        "2" "$(r cli_exit_bad_value)"
    assert_eq "$IMAGE: --set on a command that ignores it is a usage error" \
        "2" "$(r cli_exit_set_ignored)"
    # An unrecognised option deliberately falls through to QApplication, because
    # Qt accepts its own flags in double-dash form and swallowing them would break
    # --platform. So a mistyped option opens the GUI rather than being rejected.
    # Recorded so the trade-off is visible, and so a change to it is noticed.
    info "$IMAGE: an unrecognised option exits $(r cli_unknown_flag) (falls through to the GUI by design)"
    # With no Steam anywhere it still answers, and says so in its exit code.
    assert_eq "$IMAGE: --steam-info answers on a system with no Steam" \
        "none" "$(r cli_steaminfo_json)"
    assert_eq "$IMAGE: and reports it in the exit code" "3" "$(r cli_steaminfo_rc)"

    # -- uninstall --------------------------------------------------------
    assert_eq "$IMAGE: it uninstalls cleanly" "yes" "$(r purge_ok)"
    assert_eq "$IMAGE: and leaves nothing behind" "0" "$(r purge_leftovers)"

done < <(dock_distros)

case_finish
