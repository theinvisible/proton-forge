#!/usr/bin/env bash
# lab-requires: build
#
# The launch-options translation, and writing it back into Steam.
#
# EnvBuilder is unit tested at the string level already; what this case adds is
# the other half — the path from a stored setting through to what actually lands
# in Steam's localconfig.vdf, with real files on both ends.
#
# Part e) is expected to fail. writeToLocalConfig matches an app section with
# "\"<appid>\"\s*\{[^}]*\}" (SteamLauncher.cpp:228), and a [^}]* body cannot
# span a nested {} block — which real localconfig.vdf app sections routinely
# contain. When the regex misses, the function still rewrites the file with
# unchanged content and returns true (the write at :254 is unconditional), so
# the caller is told the settings were applied when nothing happened. See
# TESTS.md for the write-up.

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
part "e) --apply where the regex-based rewrite breaks down"

# writeToLocalConfig finds the app section with "\"<appid>\"\s*\{[^}]*\}"
# (SteamLauncher.cpp:229). Three things follow from a [^}]* body, and only the
# first of them happens to work.

# e1) A nested block *after* LaunchOptions. The match is cut short at the nested
#     block's closing brace, but LaunchOptions is still inside what was matched,
#     so the replacement lands correctly. This one is fine — asserted so a
#     future fix does not regress it.
LOCALCONFIG="$(fx_localconfig "$NATIVE" nested=after "$APPID=")"
app_cli --apply "$APPID" --set enableProtonHDR=true >/dev/null
assert_contains "a nested block after LaunchOptions still works" "$LOCALCONFIG" 'PROTON_ENABLE_HDR=1'
assert_contains "and the nested block survives" "$LOCALCONFIG" 'BadgeData'

# e2) The game has no entry in localconfig.vdf at all. This is the ordinary case
#     for any game whose launch options have never been touched — Steam only
#     writes a section once there is something to write. The regex finds nothing,
#     so nothing is changed; but the write at :254 happens regardless of the
#     match and true is returned either way.
LOCALCONFIG="$(fx_localconfig "$NATIVE" "570=")"     # a different game only
BEFORE="$(cat "$LOCALCONFIG")"
RESULT="$(app_cli --apply "$APPID" --set enableProtonHDR=true)"
AFTER="$(cat "$LOCALCONFIG")"

if [[ "$BEFORE" == "$AFTER" && "$(json_get "$RESULT" 'd["applied"]')" == "true" ]]; then
    fail "a game with no localconfig entry is silently not written" \
"--apply reported success and the file is byte-identical afterwards.

Any game whose launch options have never been set has no \"$APPID\" section in
localconfig.vdf, so the regex at SteamLauncher.cpp:229 finds nothing. The
if(match.hasMatch()) block is skipped, but the write at :254 is unconditional and
:257 sets success = true — so applySettings() returns true having done nothing.

A user meets this as: press Apply on a fresh game, ProtonForge says it worked,
Steam still launches without the options. It is the most likely path through
this function, not an edge case.

Fix: create the section when it is absent, and return false when the write
cannot be made — the caller has no other way to find out.
--- what --apply reported ---
$RESULT"
else
    assert_contains "a game with no localconfig entry gets one" "$LOCALCONFIG" 'PROTON_ENABLE_HDR=1'
    assert_json "and the read-back agrees" "$RESULT" 'd["matches"]' "true"
fi

# e3) A nested block *before* LaunchOptions. Now LaunchOptions falls outside the
#     shortened match, so the "add it before the closing brace" branch runs and
#     inserts it inside the nested block instead.
LOCALCONFIG="$(fx_localconfig "$NATIVE" nested=before "$APPID=")"
app_cli --apply "$APPID" --set enableProtonHDR=true >/dev/null

# Whatever else happens, the file has to stay parseable and the options must end
# up somewhere Steam will read them — i.e. not buried in BadgeData.
misplaced=0
python3 - "$LOCALCONFIG" <<'PY' || misplaced=1
import re, sys
text = open(sys.argv[1]).read()
badge = re.search(r'"BadgeData"\s*\{(.*?)\}', text, re.S)
if badge and "LaunchOptions" in badge.group(1):
    sys.exit(1)
if text.count("LaunchOptions") > 1:
    sys.exit(1)
sys.exit(0)
PY

if (( misplaced )); then
    fail "a nested block before LaunchOptions misplaces the write" \
"The options were written into the nested BadgeData block, or written twice.

With the nested block ahead of LaunchOptions, the [^}]* match ends before
LaunchOptions is reached. launchRegex then finds nothing in the captured text, so
the else branch at SteamLauncher.cpp:245 inserts at appSection.lastIndexOf('}') —
which is the nested block's closing brace, not the app section's.

Steam will not read a LaunchOptions key from inside BadgeData, and the original
key is left behind, so the file now has two.
--- the app section as written ---
$(sed -n '/"'"$APPID"'"/,/^\t\{5\}}/p' "$LOCALCONFIG")"
else
    assert_contains "a nested block before LaunchOptions is handled" \
        "$LOCALCONFIG" 'PROTON_ENABLE_HDR=1'
fi

# ---------------------------------------------------------------------------
part "f) the file is never left truncated"

# There is no atomic temp+rename and no backup, so the worst outcome would be a
# destroyed localconfig.vdf. Whatever else happens, it has to stay parseable.
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
