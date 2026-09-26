#!/usr/bin/env bash
# lab-requires: flatpak
#
# Issue #1: "Flatpak Steam library not detected until native Steam paths are
# symlinked" — reproduced as reported, in the sandbox, against the real
# ~/.var/app path.
#
# 70_flatpak already checks that the sandbox can classify a Flatpak-Steam layout,
# but it does so inside the lab's fake $HOME — a path `--filesystem=home` covers
# on its own. That is precisely the case the report is *not* about. What the
# reporters hit needs three things at once, and none of them is exercised by a
# fixture under $LAB_APP_HOME:
#
#   1. the Steam root at the *real* $HOME/.var/app/com.valvesoftware.Steam/...,
#      which Flatpak hides from other apps even under --filesystem=home, so only
#      the manifest's explicit grant can make it visible;
#   2. the games themselves on another mount (/mnt/games/SteamLibrary in the
#      report), reached through --filesystem=/mnt, plus a ~/games -> /mnt/games
#      symlink that the vdf may name instead;
#   3. native leftovers — the reporter had ~/.steam/root/compatibilitytools.d
#      written by ProtonForge itself and no native library at all — which must
#      not win the variant tie-break.
#
# Writing a fixture into a developer's real ~/.var/app is not something a test
# gets to do, so this case runs only in the privileged container
# (LAB_FLATPAK_DOCKER=1), whose $HOME is a throwaway directory mounted at the
# host's home *path*. There the real path and a safe path are the same string,
# which is the whole reason this is checkable at all.
#
#   LAB_FLATPAK_DOCKER=1 tests/steam-lab/steamlab test 71_flatpak_steam

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"
# shellcheck source=../lib/flatpak.sh
source "$LAB_SRC_DIR/lib/flatpak.sh"

case_setup

if ! fp_docker; then
    skip "issue #1 reproduction" \
"this case writes a Steam tree into \$HOME/.var/app and /mnt, so it runs only in
the container: LAB_FLATPAK_DOCKER=1 steamlab test 71_flatpak_steam"
    case_finish
fi

fp_require

APPID=1245620
APPID2=570
RUNTIME_VERSION="$(fp_runtime_version)"

# Inside the container these are the same strings as on the host — the lab
# directory and the home stand-in are mounted at their host paths.
C_HOME="$FP_DOCKER_HOME"
FP_STEAM="$C_HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"
MNT_LIB="/mnt/games/SteamLibrary"
SYMLINK_LIB="$C_HOME/games/SteamLibrary2"   # via ~/games -> /mnt/games

# fp_root <command...> -- root inside the container, for the /mnt mountpoint
fp_root() { docker exec -u 0 "$FP_CONTAINER" "$@"; }

# fp_sh <script> -- run a script inside the container as the lab user
fp_sh() { docker exec -e "HOME=$C_HOME" "$FP_CONTAINER" bash -c "$1"; }

# ---------------------------------------------------------------------------
part "a) the app the report is about"

if ! fp_runtime_available; then
    fail "org.kde.Platform//$RUNTIME_VERSION is no longer on Flathub" \
         "the manifest cannot be built, so nothing below can be checked"
    case_finish
fi
if ! fp_env flatpak info "org.kde.Platform//$RUNTIME_VERSION" >/dev/null 2>&1; then
    info "installing the runtime into $LAB_FLATPAK_DIR — first run only, about 1.5 GB"
    fp_install_runtime
fi

# Built from the working tree every time unless told otherwise: a bundle left
# installed by an earlier run reports the same version and may be a different
# tree, which would make every result below about code nobody can point at.
# LAB_FP_REUSE=1 skips the rebuild when iterating on the fixture itself.
if [[ "${LAB_FP_REUSE:-0}" == "1" ]] && fp_installed; then
    warn "LAB_FP_REUSE=1 — testing the bundle already installed, not the working tree"
    ok "a Flatpak is installed in the lab installation"
else
    fp_cleanup
    if ! fp_build; then
        fail "flatpak-builder failed" "see $CASE_OUT_DIR/flatpak-build.log"
        case_finish
    fi
    BUNDLE="$(fp_bundle)"
    if [[ -z "$BUNDLE" || ! -f "$BUNDLE" ]]; then
        fail "build-bundle failed" "see $CASE_OUT_DIR/flatpak-bundle.log"
        case_finish
    fi
    if fp_install_bundle "$BUNDLE"; then
        ok "the Flatpak built from the working tree installs"
    else
        fail "the bundle does not install" "$(tail -n 20 "$CASE_OUT_DIR/flatpak-install.log")"
        case_finish
    fi
fi

info "version under test: $(fp_run --version 2>&1)"

# ---------------------------------------------------------------------------
part "b) the reporter's machine, rebuilt"

# /mnt/games is the reporter's second drive. It is a directory in the container's
# own filesystem rather than a mount, which is what the grant sees either way.
# ---------------------------------------------------------------------------
fp_root rm -rf /mnt/games >/dev/null 2>&1
fp_root mkdir -p "$MNT_LIB/steamapps/common" >/dev/null 2>&1 \
    || { fail "could not create /mnt/games in the container" "docker exec -u 0 failed"
         case_finish; }
fp_root chown -R "$(id -u):$(id -g)" /mnt/games >/dev/null 2>&1

cat >"$LAB_RUN_DIR/fixture-issue1.sh" <<EOF
set -eu
HOME="$C_HOME"
FP_STEAM="$FP_STEAM"
MNT_LIB="$MNT_LIB"

# A clean slate for the parts of the throwaway home this case owns.
rm -rf "\$HOME/.var/app/com.valvesoftware.Steam" "\$HOME/.steam" "\$HOME/games" \\
       "\$HOME/.local/share/Steam" "\$HOME/.config/ProtonForge" \\
       "\$HOME/.var/app/org.protonforge.ProtonForge"

# Flatpak Steam, as Flathub's com.valvesoftware.Steam lays it out.
mkdir -p "\$FP_STEAM/steamapps/common" "\$FP_STEAM/steamapps/compatdata" \\
         "\$FP_STEAM/steamapps/shadercache" "\$FP_STEAM/compatibilitytools.d" \\
         "\$FP_STEAM/config" "\$FP_STEAM/userdata" \\
         "\$FP_STEAM/ubuntu12_32/steam-runtime" "\$FP_STEAM/ubuntu12_64" \\
         "\$HOME/.var/app/com.valvesoftware.Steam/.steam"
: >"\$FP_STEAM/ubuntu12_32/gameoverlayrenderer.so"
: >"\$FP_STEAM/ubuntu12_64/gameoverlayrenderer.so"

# ~/games -> /mnt/games, as the report has it, and a library named through it.
ln -sfn /mnt/games "\$HOME/games"
mkdir -p "$SYMLINK_LIB/steamapps/common"

# What ProtonForge left behind on the reporter's machine: a native-looking
# compatibilitytools.d and no native library anywhere. This is the tie-break.
mkdir -p "\$HOME/.steam/root/compatibilitytools.d"
ln -sfn "\$HOME/.steam/root" "\$HOME/.steam/steam"

cat >"\$FP_STEAM/steamapps/libraryfolders.vdf" <<VDF
"libraryfolders"
{
	"0"
	{
		"path"		"\$FP_STEAM"
		"label"		""
		"apps"
		{
		}
	}
	"1"
	{
		"path"		"\$MNT_LIB"
		"label"		"games"
		"apps"
		{
			"$APPID"		"1234567"
		}
	}
	"2"
	{
		"path"		"$SYMLINK_LIB"
		"label"		"via-home-symlink"
		"apps"
		{
			"$APPID2"		"7654321"
		}
	}
}
VDF

write_manifest() {   # <library> <appid> <name> <installdir>
	mkdir -p "\$1/steamapps/common/\$4"
	cat >"\$1/steamapps/appmanifest_\$2.acf" <<ACF
"AppState"
{
	"appid"		"\$2"
	"name"		"\$3"
	"StateFlags"		"4"
	"installdir"		"\$4"
	"buildid"		"1234567"
	"SizeOnDisk"		"1073741824"
}
ACF
}
write_manifest "\$MNT_LIB" "$APPID" "ELDEN RING" "ELDEN RING"
mkdir -p "\$MNT_LIB/steamapps/common/ELDEN RING/Game"
: >"\$MNT_LIB/steamapps/common/ELDEN RING/Game/eldenring.exe"
write_manifest "$SYMLINK_LIB" "$APPID2" "Dota 2" "dota 2 beta"
EOF

if fp_sh "bash '$LAB_RUN_DIR/fixture-issue1.sh'"; then
    ok "Flatpak Steam at ~/.var/app, a library on /mnt and one through ~/games -> /mnt/games"
else
    fail "could not write the fixture in the container" "see the output above"
    case_finish
fi

assert_true "and no native Steam library exists, exactly as reported" \
    fp_sh "test ! -e '$C_HOME/.local/share/Steam/steamapps/libraryfolders.vdf' \
        && test ! -e '$C_HOME/.steam/root/steamapps/libraryfolders.vdf' \
        && test -d '$C_HOME/.steam/root/compatibilitytools.d'"

# ---------------------------------------------------------------------------
part "c) what the sandbox can see"

# fp_run_bare: no --env=HOME, no extra --filesystem. Only the manifest's own
# finish-args decide, which is what the report is really about.
assert_true "the sandbox can read ~/.var/app/com.valvesoftware.Steam at all" \
    fp_test_file "$FP_STEAM/steamapps/libraryfolders.vdf"
assert_true "and /mnt/games/SteamLibrary" \
    fp_test_file "$MNT_LIB/steamapps/appmanifest_$APPID.acf"
assert_true "and the library behind the ~/games symlink" \
    fp_test_file "$SYMLINK_LIB/steamapps/appmanifest_$APPID2.acf"

# Which grant is actually carrying this? The manifest claims --filesystem=home
# does not cover another app's ~/.var/app, and that claim is checkable: drop home
# and see what is left. Informational rather than pass/fail — the app works
# either way, but a wrong belief here is how the explicit line gets "cleaned up".
without_home=$(fp_env flatpak run --nofilesystem=home --command=test \
    "$LAB_FLATPAK_APP_ID" -e "$FP_STEAM/steamapps/libraryfolders.vdf" 2>/dev/null \
    && echo yes || echo no)
without_either=$(fp_env flatpak run --nofilesystem=home \
    --nofilesystem="$C_HOME/.var/app/com.valvesoftware.Steam" --command=test \
    "$LAB_FLATPAK_APP_ID" -e "$FP_STEAM/steamapps/libraryfolders.vdf" 2>/dev/null \
    && echo yes || echo no)
info "note: reachable with home revoked: $without_home; with both revoked: $without_either"
if [[ "$without_home" == "yes" && "$without_either" == "no" ]]; then
    ok "the explicit --filesystem=~/.var/app/com.valvesoftware.Steam grant is what carries it"
else
    info "note: --filesystem=home appears to cover ~/.var/app here, so the explicit grant is belt-and-braces"
fi

# Same question for the games. This one is not academic: --filesystem=/mnt only
# arrived in 51d36e4 (after v1.0.11), and both follow-up reporters kept their
# games in /mnt/games.
if fp_env flatpak run --nofilesystem=/mnt --command=test "$LAB_FLATPAK_APP_ID" \
        -e "$MNT_LIB/steamapps/appmanifest_$APPID.acf" 2>/dev/null; then
    info "note: /mnt is reachable even with --nofilesystem=/mnt — the grant cannot be attributed here"
else
    ok "and --filesystem=/mnt is what makes the games on the second drive readable"
fi

# ---------------------------------------------------------------------------
part "d) detection, which is what the report says is broken"

INFO="$(fp_run_bare --steam-info 2>"$(case_log steaminfo)")"
if ! json_valid "$INFO"; then
    fail "the sandboxed app could not report a Steam install" \
"This is issue #1 as filed: --steam-info returned nothing usable with Flatpak
Steam in place.
output: $INFO
stderr: $(tail -n 15 "$(case_log steaminfo)")"
    case_finish
fi

assert_json "the variant is flatpak, not none" "$INFO" 'd["variant"]' "flatpak"
assert_json_contains "and the root is the Flatpak Steam data directory" "$INFO" \
    'd["root"]' "/.var/app/com.valvesoftware.Steam/.local/share/Steam"
assert_json_contains "the library on /mnt is among the library folders" "$INFO" \
    'd["libraries"]' "$MNT_LIB/steamapps"
assert_json_contains "so is the one named through the ~/games symlink" "$INFO" \
    'd["libraries"]' "SteamLibrary2/steamapps"

# The reporter's other symptom: Proton went to ~/.steam/root/compatibilitytools.d
# while Steam's own tools directory is under ~/.var/app.
assert_json_contains "Proton would be installed into Flatpak Steam's compatibilitytools.d" \
    "$INFO" 'd["compatibilityTools"]' \
    "/.var/app/com.valvesoftware.Steam/.local/share/Steam/compatibilitytools.d"
assert_json_contains "and that is also where a new install would be written" \
    "$INFO" 'd["defaultInstallCompatPath"]' \
    "/.var/app/com.valvesoftware.Steam/.local/share/Steam/compatibilitytools.d"

# ...and the grant is :rw, so an install there has to actually be possible.
if fp_env flatpak run --command=touch "$LAB_FLATPAK_APP_ID" \
        "$FP_STEAM/compatibilitytools.d/.pf-write-probe" 2>/dev/null; then
    ok "the sandbox can write there, so Install Proton has somewhere to go"
    fp_sh "rm -f '$FP_STEAM/compatibilitytools.d/.pf-write-probe'" >/dev/null 2>&1
else
    fail "the sandbox cannot write to Flatpak Steam's compatibilitytools.d" \
"--filesystem=~/.var/app/com.valvesoftware.Steam:rw is supposed to make this
writable. Read-only means Install Proton fails or lands somewhere Steam does not
look — the second half of issue #1."
fi

# ---------------------------------------------------------------------------
part "e) the 72 games the reporter had to symlink for"

GAMES="$(fp_run_bare --list-games 2>"$(case_log listgames)")"
if ! json_valid "$GAMES"; then
    fail "the sandboxed app could not enumerate games" \
         "stderr: $(tail -n 15 "$(case_log listgames)")"
    case_finish
fi

assert_json "both games are found, with no symlink bridging anything" "$GAMES" 'len(d)' "2"
assert_json_contains "the one on /mnt/games is there" "$GAMES" \
    '[g["name"] for g in d]' "ELDEN RING"
assert_json_contains "and it is read from the library on the other mount" "$GAMES" \
    '[g["installPath"] for g in d]' "$MNT_LIB/steamapps/common/ELDEN RING"
assert_json_contains "the one behind the ~/games symlink is there too" "$GAMES" \
    '[g["name"] for g in d]' "Dota 2"

# ---------------------------------------------------------------------------
part "f) and it is the sandbox that was under test"

# A guard against the whole case passing for the wrong reason: if some native
# tree had appeared, everything above would be green while proving nothing about
# Flatpak Steam.
assert_json "no native install was involved" "$INFO" 'd["variant"]' "flatpak"
assert_true "the app under test really was the sandboxed one" \
    fp_env flatpak info "$LAB_FLATPAK_APP_ID"

# ---------------------------------------------------------------------------
part "g) the negative control: the same app, one grant short"

# A green case is only worth something if it could have been red. v1.0.11 — the
# version both follow-up reporters ran — had the ~/.var/app grant but no
# --filesystem=/mnt (that came with 51d36e4, months later), and both of them kept
# their games in /mnt/games. Revoking that one grant at run time reproduces the
# report against today's binary, which is what makes the pass above meaningful.
#
# It runs last, and after the fixture has been proven to work: with no /mnt/games
# at all every assertion here would pass for the wrong reason.
assert_true "the fixture is still in place, so a miss below means the grant" \
    fp_sh "test -f '$MNT_LIB/steamapps/appmanifest_$APPID.acf'"

GAMES_NO_MNT="$(fp_env timeout "$TIMEOUT_CLI" flatpak run --nofilesystem=/mnt \
    --env=PROTONFORGE_NO_STARTUP_CHECKS=1 "$LAB_FLATPAK_APP_ID" --list-games \
    2>"$(case_log listgames-nomnt)")"
if json_valid "$GAMES_NO_MNT"; then
    COUNT_NO_MNT="$(json_get "$GAMES_NO_MNT" 'len(d)')"
    assert_ne "without --filesystem=/mnt the games on the second drive are gone" \
        "2" "$COUNT_NO_MNT"
    info "note: $COUNT_NO_MNT of 2 games left when /mnt is revoked"
else
    fail "the app did not survive having /mnt revoked" \
         "stderr: $(tail -n 15 "$(case_log listgames-nomnt)")"
fi

# And now it says so. SteamLauncher::scanLibraries() keeps the library folder
# whose steamapps directory it cannot see, and libraryWarnings() turns it into a
# sentence naming the path — plus, inside a Flatpak, the exact `flatpak override`
# that grants it. Before that, an unreadable drive was indistinguishable from one
# that was never configured, which is how this arrived as "detection is broken".
INFO_NO_MNT="$(fp_env timeout "$TIMEOUT_CLI" flatpak run --nofilesystem=/mnt \
    --env=PROTONFORGE_NO_STARTUP_CHECKS=1 "$LAB_FLATPAK_APP_ID" --steam-info \
    2>/dev/null)"
if json_valid "$INFO_NO_MNT"; then
    assert_json_contains "the app still reports a healthy Steam install" \
        "$INFO_NO_MNT" 'd["variant"]' "flatpak"
    assert_not_contains_str "the unreachable library is not among the ones it walks" \
        "$(json_get "$INFO_NO_MNT" 'd["libraries"]')" "$MNT_LIB"
    assert_json_contains "but it is reported as unreadable rather than dropped" \
        "$INFO_NO_MNT" 'd["unreadableLibraries"]' "$MNT_LIB"
    assert_json_contains "and the warning names the path" \
        "$INFO_NO_MNT" 'd["libraryWarnings"][0]' "$MNT_LIB"
    # The whole point of doing this inside the sandbox: FLATPAK_ID is set here, so
    # the message is the Flatpak one and the command it prints is the real remedy
    # for the real app id.
    assert_json_contains "with the flatpak override that fixes it" \
        "$INFO_NO_MNT" 'd["libraryWarnings"][0]' \
        "flatpak override --user --filesystem=\"$MNT_LIB\" $LAB_FLATPAK_APP_ID"
else
    fail "--steam-info failed with /mnt revoked" "$INFO_NO_MNT"
fi

# With the grant back in place there must be nothing to report — otherwise the
# bar would sit on screen forever on a perfectly healthy install.
assert_json "and with the grant restored the warning is gone" "$INFO" \
    'len(d["libraryWarnings"])' "0"

fp_root rm -rf /mnt/games >/dev/null 2>&1
fp_sh "rm -rf '$C_HOME/.var/app/com.valvesoftware.Steam' '$C_HOME/games' '$C_HOME/.steam'" >/dev/null 2>&1
case_finish
