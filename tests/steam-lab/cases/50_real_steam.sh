#!/usr/bin/env bash
# lab-requires: docker
#
# Real Steam, without a Steam account.
#
# Every other case here builds its Steam trees by hand, which is fast and lets
# each permutation be tested — but it can never catch the one thing that matters
# most: whether the list of paths SteamPaths probes for still matches what Steam
# actually creates. That list can rot silently. A Steam update that moves a
# directory would leave ProtonForge seeing no games, and the entire fixture-based
# suite would stay green.
#
# So this case uses the real thing, twice over, and neither needs an account:
#
#   * steamcmd, whose Debian wrapper deliberately creates ~/.steam/steam and
#     ~/.steam/root pointing at ~/.local/share/Steam — the same layout a desktop
#     install has. `+login anonymous +app_update 1007` then writes a genuine
#     steamapps/libraryfolders.vdf and appmanifest_1007.acf, with real Steam code
#     doing the writing. No display, no privileges.
#
#   * Valve's own bootstrap tarball, extracted exactly as bin_steam.sh does it.
#     That is what creates the ~/.steam/steam symlink on a real first run.
#
# Both are baked into the container image, so a run pays nothing for them.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"
# shellcheck source=../lib/docker.sh
source "$LAB_SRC_DIR/lib/docker.sh"

case_setup
dock_require

# Only the targets whose image carries the Steam tooling.
STEAM_TARGETS=()
while IFS='|' read -r IMAGE _ EXP_STEAM; do
    [[ "$EXP_STEAM" == "yes" ]] && STEAM_TARGETS+=("$IMAGE")
done < <(dock_distros)

if (( ${#STEAM_TARGETS[@]} == 0 )); then
    skip "real Steam" "no selected target carries the Steam tooling (see LAB_DISTROS_DEFAULT)"
    case_finish
fi

for IMAGE in "${STEAM_TARGETS[@]}"; do
    SLUG="$(dock_slug "$IMAGE")"
    OUT="$CASE_OUT_DIR/$SLUG.log"

    part "$IMAGE"

    dock_cleanup "$IMAGE"
    dock_build "$IMAGE"

    if DEB="$(dock_cached_deb "$IMAGE")"; then
        DEB_ARG="$DEB"
    else
        DEB_ARG="build"
    fi

    if dock_run_root "$IMAGE" "$DEB_ARG" package steam >"$OUT" 2>&1; then
        ok "$IMAGE: the container ran through"
    else
        fail "$IMAGE: the container exited with an error" "$(tail -n 25 "$OUT")"
    fi
    dock_cleanup "$IMAGE"

    r() { dock_result "$OUT" "$1"; }

    if [[ "$(r steamcmd_present)" != "yes" ]]; then
        skip "$IMAGE: steamcmd" "not installed in the image — check the build log"
        continue
    fi
    assert_eq "$IMAGE: a binary is available to read the real tree with" \
        "yes" "$(r binary_available_for_steam)"

    # -- what steamcmd created --------------------------------------------
    assert_eq "$IMAGE: i386 is enabled (steamcmd is a 32-bit package)" "yes" "$(r arch_i386)"
    assert_eq "$IMAGE: an anonymous login wrote libraryfolders.vdf" \
        "yes" "$(r steamcmd_libraryfolders)"
    assert_eq "$IMAGE: and an appmanifest for the app it fetched" \
        "yes" "$(r steamcmd_appmanifest)"

    # The symlinks are the part SteamPaths depends on and the part nobody would
    # notice changing.
    assert_eq "$IMAGE: ~/.steam/steam exists, as SteamPaths expects" \
        "yes" "$(r steamcmd_steam_symlink)"
    assert_eq "$IMAGE: ~/.steam/root exists too" "yes" "$(r steamcmd_root_symlink)"
    assert_contains_str "$IMAGE: and it resolves into ~/.local/share/Steam" \
        "$(r steamcmd_symlink_target)" "/.local/share/Steam"

    # -- and what ProtonForge makes of it ---------------------------------
    #
    # This is the assertion the whole case exists for: real Steam files, read by
    # the real detection code, with nothing hand-written in between.
    assert_eq "$IMAGE: ProtonForge detects it as a native install" \
        "native" "$(r pf_variant_after_steamcmd)"
    assert_contains_str "$IMAGE: at the path Steam actually used" \
        "$(r pf_root_after_steamcmd)" "/.local/share/Steam"
    assert_eq "$IMAGE: with one library folder" "1" "$(r pf_libraries_after_steamcmd)"

    # VDFParser reading a file it did not write is the other half.
    if [[ "$(r pf_games_after_steamcmd)" == "invalid" ]]; then
        fail "$IMAGE: --list-games could not read the real library" \
            "$(dock_output "$OUT" | tail -n 20)"
    else
        ok "$IMAGE: --list-games parses the real libraryfolders.vdf ($(r pf_games_after_steamcmd) entries)"
        info "$IMAGE: app ids found: $(r pf_appids_after_steamcmd)"
    fi

    # -- Valve's own bootstrap --------------------------------------------
    if [[ "$(r bootstrap_extracted)" != "yes" ]]; then
        skip "$IMAGE: Valve's bootstrap tarball" \
             "steam-launcher could not be fetched when the image was built"
    else
        assert_eq "$IMAGE: the bootstrap creates ~/.steam/steam" \
            "yes" "$(r bootstrap_symlink)"
        assert_eq "$IMAGE: and steam.sh" "yes" "$(r bootstrap_has_steam_sh)"
        assert_eq "$IMAGE: and ubuntu12_32, where the overlay lives" \
            "yes" "$(r bootstrap_has_ubuntu12_32)"

        # The interesting one. The bootstrap does not create steamapps or
        # libraryfolders.vdf — the client writes those later — and
        # SteamPaths::hasLibraryFolders() gates everything on that file. So a
        # Steam that has been installed but never signed into is invisible to
        # ProtonForge. That is worth knowing and is not obviously right.
        assert_eq "$IMAGE: the bootstrap alone writes no libraryfolders.vdf" \
            "no" "$(r bootstrap_libraryfolders)"
        assert_eq "$IMAGE: so a never-signed-into Steam is not detected" \
            "none" "$(r pf_variant_after_bootstrap)"
    fi
done

case_finish
