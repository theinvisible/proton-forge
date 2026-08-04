#!/usr/bin/env bash
# lab-requires: build
#
# The launch-options translation, and writing it back into Steam.
#
# EnvBuilder is unit tested at the string level already; what this case adds is
# the other half — the path from a stored setting through to what actually lands
# in Steam's localconfig.vdf, with real files on both ends.
#
# Part e) is where this suite earned its keep. writeToLocalConfig used to find the
# app's section with a regex whose body was [^}]*, which stops at the first closing
# brace — and real app sections contain nested blocks. Two of the four shapes in e)
# failed silently: one wrote the options into the wrong block and left a duplicate
# behind, the other changed nothing at all while reporting success. Both are fixed
# (SteamLauncher.cpp counts braces now); e) is the regression test.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"

case_setup

APPID=1245620

# ---------------------------------------------------------------------------
part "a) settings become env vars"

NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" >/dev/null

# --set drives the settings through their JSON names, so this table needs no
# knowledge of the C++ side.
assert_contains_str "NVAPI is on by default" \
    "$(app_cli --print-launch-options "$APPID")" "PROTON_ENABLE_NVAPI=1"

OPTS="$(app_cli --print-launch-options "$APPID" \
    --set srOverride=true --set srMode=Quality --set srPreset="Preset E")"
assert_contains_str "super resolution reaches DXVK_NVAPI_DRS_SETTINGS" "$OPTS" \
    "DXVK_NVAPI_DRS_SETTINGS=NGX_DLSS_SR_OVERRIDE=on"
assert_contains_str "and the mode is lowercased for the driver" "$OPTS" "NGX_DLSS_SR_MODE=quality"

OPTS="$(app_cli --print-launch-options "$APPID" \
    --set enableFrameRateLimit=true --set targetFrameRate=144)"
assert_contains_str "the frame cap goes to DXVK" "$OPTS" "DXVK_FRAME_RATE=144"
assert_contains_str "and to VKD3D" "$OPTS" "VKD3D_FRAME_RATE=144"

OPTS="$(app_cli --print-launch-options "$APPID" \
    --set enableProtonWayland=true --set enableProtonHDR=true --set enableHDRWSI=true)"
assert_contains_str "wayland" "$OPTS" "PROTON_ENABLE_WAYLAND=1"
assert_contains_str "hdr" "$OPTS" "PROTON_ENABLE_HDR=1"
assert_contains_str "hdr wsi" "$OPTS" "ENABLE_HDR_WSI=1"

OPTS="$(app_cli --print-launch-options "$APPID" --set enablePrimeRenderOffload=true)"
assert_contains_str "the PRIME trio is emitted together (1/3)" "$OPTS" "__NV_PRIME_RENDER_OFFLOAD=1"
assert_contains_str "the PRIME trio is emitted together (2/3)" "$OPTS" "__GLX_VENDOR_LIBRARY_NAME=nvidia"
assert_contains_str "the PRIME trio is emitted together (3/3)" "$OPTS" "__VK_LAYER_NV_optimus=NVIDIA_only"

assert_contains_str "%command% terminates the string" \
    "$(app_cli --print-launch-options "$APPID")" "%command%"

# ---------------------------------------------------------------------------
part "b) the round trip, through the shell"

# EnvBuilder's header states parseLaunchOptions is the inverse of
# buildLaunchOptions. The unit test checks that in C++; this checks it end to end
# through the two CLI commands, which is what a user's import/export actually
# exercises.
for combo in \
    "--set srOverride=true --set srMode=Performance" \
    "--set fgOverride=true --set fgMultiFrameCount=3 --set fgMode=Auto" \
    "--set enableReflex=true --set protonUseNTSync=true --set protonLog=true" \
    "--set enableSmoothMotion=true --set disableAutoHDR=true" \
    "--set dlssUpgrade=true --set showIndicator=true --set enableMangoHud=true"
do
    # shellcheck disable=SC2086
    BUILT="$(app_cli --print-launch-options "$APPID" $combo)"
    PARSED="$(app_cli --parse-launch-options "$BUILT")"
    assert_json "round trip:$(printf '%s' "$combo" | sed 's/--set //g')" \
        "$PARSED" 'd["roundTrip"]' "$BUILT"
done

# ---------------------------------------------------------------------------
part "c) options ProtonForge does not model survive"

RAW="SOME_FUTURE_FLAG=1 gamemoderun PROTON_ENABLE_NVAPI=1 %command% -skipintro -width 2560"
PARSED="$(app_cli --parse-launch-options "$RAW")"

assert_json "NVAPI is recognised" "$PARSED" 'd["settings"]["enableNVAPI"]' "true"
assert_json_contains "an unknown env var is kept" "$PARSED" \
    'd["customParams"]' "SOME_FUTURE_FLAG=1"
assert_json_contains "a wrapper command is kept" "$PARSED" 'd["customParams"]' "gamemoderun"
assert_json_contains "the game arguments are kept verbatim" "$PARSED" \
    'd["customParams"]' "-skipintro -width 2560"

# ---------------------------------------------------------------------------
part "d) --apply writes into localconfig.vdf"

LOCALCONFIG="$(fx_localconfig "$NATIVE" "$APPID=")"
BEFORE="$(cat "$LOCALCONFIG")"

RESULT="$(app_cli --apply "$APPID" --set enableProtonHDR=true --set protonUseNTSync=true)"
AFTER="$(cat "$LOCALCONFIG")"

assert_json "the write is reported as done" "$RESULT" 'd["applied"]' "true"
assert_ne "the file changed" "$BEFORE" "$AFTER"
assert_contains "the options are in the file" "$LOCALCONFIG" 'PROTON_ENABLE_HDR=1'
assert_contains "and so is the second one" "$LOCALCONFIG" 'PROTON_USE_NTSYNC=1'

# --apply reads the file back and reports what it found there, so agreement
# between the two is itself an assertion the app makes about its own work.
assert_json "what was written matches what was asked for" "$RESULT" 'd["matches"]' "true"

# Reading it back through the app closes the loop.
GAMES="$(app_cli --list-games)"
assert_json_contains "the app now sees its own launch options" "$GAMES" \
    '[g["launchOptions"] for g in d if g["appId"]=="'"$APPID"'"][0]' "PROTON_ENABLE_HDR=1"

# Applying twice must not append or duplicate.
app_cli --apply "$APPID" --set enableProtonHDR=true >/dev/null
count="$(grep -c 'LaunchOptions' "$LOCALCONFIG")"
assert_eq "applying twice leaves one LaunchOptions entry" "1" "$count"

# ---------------------------------------------------------------------------
part "e) the shapes a real localconfig.vdf comes in"

# writeToLocalConfig edits the file in place, finding the app's section by
# counting braces. It used to use "\"<appid>\"\\s*\\{[^}]*\\}", which cannot work: a
# [^}]* body stops at the first closing brace, and real app sections contain
# nested blocks in no guaranteed order. All four shapes below came out wrong in
# some way, and two of them silently — see TESTS.md §7.

# e1) A nested block after LaunchOptions. This one happened to work even before.
LOCALCONFIG="$(fx_localconfig "$NATIVE" nested=after "$APPID=")"
app_cli --apply "$APPID" --set enableProtonHDR=true >/dev/null
assert_contains "a nested block after LaunchOptions: written" "$LOCALCONFIG" 'PROTON_ENABLE_HDR=1'
assert_contains "and the nested block survives" "$LOCALCONFIG" 'BadgeData'

# e2) A nested block *before* LaunchOptions. The old code inserted a second
#     LaunchOptions inside the nested block and left the original behind.
LOCALCONFIG="$(fx_localconfig "$NATIVE" nested=before "$APPID=")"
RESULT="$(app_cli --apply "$APPID" --set enableProtonHDR=true)"

assert_contains "a nested block before LaunchOptions: written" "$LOCALCONFIG" 'PROTON_ENABLE_HDR=1'
assert_json "and the read-back agrees" "$RESULT" 'd["matches"]' "true"
assert_true "the options are not buried inside the nested block" \
    python3 -c "
import re, sys
text = open('$LOCALCONFIG').read()
badge = re.search(r'\"BadgeData\"\s*\{(.*?)\}', text, re.S)
assert badge, 'the nested block disappeared'
assert 'LaunchOptions' not in badge.group(1), 'LaunchOptions ended up inside BadgeData'
assert text.count('LaunchOptions') == 1, 'there is more than one LaunchOptions key'
"

# e3) The game has no section at all — the ordinary case for anything whose
#     launch options have never been set, because Steam only writes a section
#     once there is something to write. The old code reported success and changed
#     nothing.
LOCALCONFIG="$(fx_localconfig "$NATIVE" "570=")"
BEFORE="$(cat "$LOCALCONFIG")"
RESULT="$(app_cli --apply "$APPID" --set enableProtonHDR=true)"

assert_ne "the file changed" "$BEFORE" "$(cat "$LOCALCONFIG")"
assert_contains "a game with no section gets one" "$LOCALCONFIG" "\"$APPID\""
assert_contains "with the options in it" "$LOCALCONFIG" 'PROTON_ENABLE_HDR=1'
assert_json "and the read-back agrees" "$RESULT" 'd["matches"]' "true"
assert_contains "the other game's section is untouched" "$LOCALCONFIG" '"570"'
assert_true "the file is still balanced VDF" \
    python3 -c "
text = open('$LOCALCONFIG').read()
assert text.count('{') == text.count('}'), 'unbalanced braces'
"

# e4) Lower-case keys. Steam's casing has varied between client versions, so the
#     walk down to the apps block has to be case-insensitive on the way in, not
#     just on the way out.
LOCALCONFIG="$(fx_localconfig "$NATIVE" casing=lower "$APPID=")"
app_cli --apply "$APPID" --set protonUseNTSync=true >/dev/null
assert_contains "lower-case keys are navigated too" "$LOCALCONFIG" 'PROTON_USE_NTSYNC=1'

part "e5) failure is reported as failure"

# The caller tells a user their settings reached Steam. When they did not, it has
# to be able to find out — there is no other signal.
fx_reset
NOUSERDATA="$(fx_steam_tree native)"
fx_add_game "$NOUSERDATA" "$APPID" name="ELDEN RING" >/dev/null
rm -rf "$NOUSERDATA/userdata"

RESULT="$(app_cli --apply "$APPID" --set enableProtonHDR=true)"
assert_json "with no userdata at all, applying reports failure" "$RESULT" 'd["applied"]' "false"
assert_eq "and the exit code says so" "1" "$(app_rc)"

# A userdata directory with no localconfig.vdf in it is the same story.
mkdir -p "$NOUSERDATA/userdata/21017850/config"
RESULT="$(app_cli --apply "$APPID" --set enableProtonHDR=true)"
assert_json "and with no localconfig.vdf either" "$RESULT" 'd["applied"]' "false"

part "e6) values that need escaping"

# Custom launch parameters are free-form user text and can contain quotes and
# backslashes. Written raw they would end the VDF string early and corrupt the
# file; the round trip through VDFParser is what proves the escaping is right.
fx_reset
NATIVE="$(fx_steam_tree native)"
fx_add_game "$NATIVE" "$APPID" name="ELDEN RING" >/dev/null
LOCALCONFIG="$(fx_localconfig "$NATIVE" "$APPID=")"

app_cli --apply "$APPID" --set customLaunchParams='%command% -name "My Game" -path C:\dir' >/dev/null
assert_true "a value with quotes and backslashes stays balanced VDF" \
    python3 -c "
text = open('$LOCALCONFIG').read()
assert text.count('{') == text.count('}'), 'unbalanced braces'
"
GAMES="$(app_cli --list-games)"
READBACK='[g["launchOptions"] for g in d if g["appId"]=="'"$APPID"'"][0]'
assert_json_contains "and reads back with the quotes intact" "$GAMES" \
    "$READBACK" '-name "My Game"'
assert_json_contains "and the backslash intact" "$GAMES" \
    "$READBACK" 'C:\dir'

# ---------------------------------------------------------------------------
part "f) the file is never left truncated"

# The write goes through QSaveFile, so the original is replaced by a rename rather
# than being opened for truncation. This is Steam's file and there is no backup: a
# half-written localconfig.vdf loses every game's settings, not one game's.
LOCALCONFIG="$(fx_localconfig "$NATIVE" "$APPID=PROTON_LOG=1 %command%" "570=")"
app_cli --apply "$APPID" --set enableProtonHDR=true >/dev/null

assert_true "the file is still valid VDF afterwards" \
    python3 -c "
import sys
text = open('$LOCALCONFIG').read()
assert text.count('{') == text.count('}'), 'unbalanced braces'
assert text.strip().endswith('}'), 'truncated'
assert 'UserLocalConfigStore' in text, 'lost the root key'
"
assert_contains "the other game's entry is untouched" "$LOCALCONFIG" '"570"'

case_finish
