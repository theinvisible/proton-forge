#!/usr/bin/env bash
# lab-requires: build
#
# Steam detection and game discovery, against fixture trees.
#
# This is the widest-reaching case in the suite: SteamPaths decides whether
# there is a Steam installation and which kind, SteamLauncher walks
# libraryfolders.vdf and every appmanifest_*.acf it finds, and VDFParser does
# the reading. Everything the app shows the user comes through here, and all of
# it is derived from files — so a fake $HOME is a complete test bed.
#
# No Steam account, no Steam client, no network.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"

case_setup

# ---------------------------------------------------------------------------
part "a) no Steam at all"

fx_steam_tree none >/dev/null
INFO="$(app_cli --steam-info)"

assert_json "variant is none" "$INFO" 'd["variant"]' "none"
assert_json "root is empty" "$INFO" 'd["root"]' ""
assert_eq "exit code says no Steam" "3" "$(app_rc)"

# Derived paths must be empty rather than half-built: callers test for an empty
# string, and "/steamapps" would send a write somewhere unexpected.
for field in steamApps compatibilityTools configVdf userData steamRuntime overlay64 pidFile; do
    assert_json "$field is empty without Steam" "$INFO" "d[\"$field\"]" ""
done

# "Install Proton" still has to have somewhere to go.
assert_json_contains "there is still a default install target" "$INFO" \
    'd["defaultInstallCompatPath"]' "compatibilitytools.d"

GAMES="$(app_cli --list-games)"
assert_json "no games are reported" "$GAMES" 'len(d)' "0"

# ---------------------------------------------------------------------------
part "b) a Steam that was installed but never signed into"

# What Valve's bootstrap actually leaves behind: the directory tree and the
# ~/.steam/steam symlink, but no steamapps and no libraryfolders.vdf — the
# client writes that later. hasLibraryFolders() gates everything, so ProtonForge
# cannot see this installation at all. That is current behaviour and arguably a
# rough edge; it is asserted so a change to it is a decision, not an accident.
fx_steam_tree bootstrap >/dev/null
INFO="$(app_cli --steam-info)"

assert_json "a bootstrapped-only Steam is not detected" "$INFO" 'd["variant"]' "none"
assert_file "the ~/.steam/steam symlink does exist" "$LAB_APP_HOME/.steam/steam/steam.sh"

# ---------------------------------------------------------------------------
part "c) a native install"

NATIVE="$(fx_steam_tree native)"
INFO="$(app_cli --steam-info)"

assert_json "variant is native" "$INFO" 'd["variant"]' "native"
assert_json "root is the native path" "$INFO" 'd["root"]' "$NATIVE"
assert_json "steamapps is derived from it" "$INFO" 'd["steamApps"]' "$NATIVE/steamapps"
assert_json "compatibilitytools.d is derived from it" "$INFO" \
    'd["compatibilityTools"]' "$NATIVE/compatibilitytools.d"
assert_json "config.vdf is derived from it" "$INFO" 'd["configVdf"]' "$NATIVE/config/config.vdf"
assert_json "userdata is derived from it" "$INFO" 'd["userData"]' "$NATIVE/userdata"
# The pid file is the one path that is NOT under the root.
assert_json "the pid file is under \$HOME, not the root" "$INFO" \
    'd["pidFile"]' "$LAB_APP_HOME/.steam/steam.pid"
assert_json "one library" "$INFO" 'len(d["libraries"])' "1"
assert_json "the fake Proton is found" "$INFO" 'len(d["compatTools"])' "1"
assert_json "and recognised as Proton-CachyOS" "$INFO" 'd["protonCachyOSInstalled"]' "true"

# ---------------------------------------------------------------------------
part "d) a Flatpak install"

fx_reset
FLATPAK="$(fx_steam_tree flatpak)"
INFO="$(app_cli --steam-info)"

assert_json "variant is flatpak" "$INFO" 'd["variant"]' "flatpak"
assert_json "root is the sandboxed path" "$INFO" 'd["root"]' "$FLATPAK"
assert_json_contains "the path is classified by its ~/.var/app segment" "$INFO" \
    'd["root"]' "/.var/app/com.valvesoftware.Steam/"
assert_json "the pid file follows the variant" "$INFO" \
    'd["pidFile"]' "$LAB_APP_HOME/.var/app/com.valvesoftware.Steam/.steam/steam.pid"

# ---------------------------------------------------------------------------
part "e) both at once"

fx_reset
NATIVE="$(fx_steam_tree both)"
INFO="$(app_cli --steam-info)"

# Deliberate tie-break: existing users keep the path they already had.
assert_json "native wins" "$INFO" 'd["variant"]' "native"
assert_json "and the native root is the one used" "$INFO" 'd["root"]' "$(fx_native_root)"

# ---------------------------------------------------------------------------
part "f) game discovery"

fx_reset
NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" 1245620 name="ELDEN RING" installdir="ELDEN RING" \
    sizeondisk=52000000000 >/dev/null
fx_add_game "$NATIVE" 570 name="Dota 2" installdir="dota 2 beta" exe=native >/dev/null
fx_add_game "$NATIVE" 292030 name="The Witcher 3" stateflags=6 >/dev/null

# These are filtered out by name (SteamLauncher.cpp:77) — they are tools, not
# games, and showing them would be noise in the list.
fx_add_game "$NATIVE" 1493710 name="Proton Experimental" >/dev/null
fx_add_game "$NATIVE" 1628350 name="Steam Linux Runtime 3.0 (sniper)" >/dev/null
fx_add_game "$NATIVE" 228980 name="Steamworks Common Redistributables" >/dev/null

GAMES="$(app_cli --list-games)"

assert_json "three games, three tools filtered out" "$GAMES" 'len(d)' "3"
assert_json "sorted case-insensitively by name" "$GAMES" \
    '[g["name"] for g in d]' '["Dota 2", "ELDEN RING", "The Witcher 3"]'
assert_json "the app id is carried through" "$GAMES" \
    'sorted(g["appId"] for g in d)' '["1245620", "292030", "570"]'
assert_json "SizeOnDisk is parsed as a 64-bit value" "$GAMES" \
    '[g["sizeOnDisk"] for g in d if g["appId"]=="1245620"][0]' "52000000000"
assert_json "the install path points into common/" "$GAMES" \
    '[g["installPath"] for g in d if g["appId"]=="1245620"][0]' \
    "$NATIVE/steamapps/common/ELDEN RING"
assert_json "the library path is recorded" "$GAMES" \
    '[g["libraryPath"] for g in d if g["appId"]=="1245620"][0]' "$NATIVE/steamapps"
assert_json "the settings key is launcher:appid" "$GAMES" \
    '[g["settingsKey"] for g in d if g["appId"]=="1245620"][0]' "Steam:1245620"

# StateFlags bit 1 is "update pending".
assert_json "StateFlags 6 means an update is pending" "$GAMES" \
    '[g["needsUpdate"] for g in d if g["appId"]=="292030"][0]' "true"
assert_json "StateFlags 4 does not" "$GAMES" \
    '[g["needsUpdate"] for g in d if g["appId"]=="1245620"][0]' "false"

# A recursive scan for *.exe decides this, so it is driven by one dropped file.
assert_json "a game with an .exe is not native Linux" "$GAMES" \
    '[g["nativeLinux"] for g in d if g["appId"]=="1245620"][0]' "false"
assert_json "a game without one is" "$GAMES" \
    '[g["nativeLinux"] for g in d if g["appId"]=="570"][0]' "true"

assert_json_contains "the artwork URL is built from the app id" "$GAMES" \
    '[g["imageUrl"] for g in d if g["appId"]=="1245620"][0]' "1245620"

# ---------------------------------------------------------------------------
part "g) several library folders"

fx_reset
EXTRA="$PF_LAB_DIR/extra-library"
rm -rf "$EXTRA"
NATIVE="$(fx_steam_tree native libraries="$EXTRA")"
fx_add_game "$NATIVE" 1245620 name="ELDEN RING" >/dev/null
fx_add_game "$NATIVE" 400 name="Portal" library="$EXTRA" >/dev/null

INFO="$(app_cli --steam-info)"
GAMES="$(app_cli --list-games)"

assert_json "both libraries are found" "$INFO" 'len(d["libraries"])' "2"
assert_json "the default library comes first" "$INFO" \
    'd["libraries"][0]' "$NATIVE/steamapps"
assert_json "games from both libraries are listed" "$GAMES" 'len(d)' "2"
assert_json "the second library's game keeps its own library path" "$GAMES" \
    '[g["libraryPath"] for g in d if g["appId"]=="400"][0]' "$EXTRA/steamapps"

# ---------------------------------------------------------------------------
part "h) existing launch options are read back from Steam"

fx_reset
NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" 1245620 name="ELDEN RING" >/dev/null

# Steam's key casing has varied between client versions, which is why
# readLaunchOptions walks the tree case-insensitively. Both spellings have to
# work, and only a fixture can prove it.
for casing in canonical lower; do
    fx_localconfig "$NATIVE" casing="$casing" \
        "1245620=PROTON_ENABLE_NVAPI=1 DXVK_ASYNC=1 %command% -skipintro" >/dev/null
    GAMES="$(app_cli --list-games)"
    assert_json "launch options are read with $casing key casing" "$GAMES" \
        '[g["launchOptions"] for g in d if g["appId"]=="1245620"][0]' \
        "PROTON_ENABLE_NVAPI=1 DXVK_ASYNC=1 %command% -skipintro"
done

# An app with no LaunchOptions entry must come back empty, not fall over.
fx_localconfig "$NATIVE" "570=" >/dev/null
GAMES="$(app_cli --list-games)"
assert_json "a game with no stored options reports none" "$GAMES" \
    '[g["launchOptions"] for g in d if g["appId"]=="1245620"][0]' ""

# ---------------------------------------------------------------------------
part "i) unknown app ids"

fx_reset
NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" 1245620 >/dev/null

app_cli --print-launch-options 999999 >/dev/null 2>&1
assert_eq "an unknown app id is its own exit code" "4" "$(app_rc)"

case_finish
