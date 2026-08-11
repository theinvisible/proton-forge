#!/usr/bin/env bash
# lab-requires: build
#
# Launching a GOG game, which is where "not Steam" has to actually hold.
#
# 45_launch pins the Steam path; this is its counterpart. The things that must
# be true here are the ones that produce a game which starts and then behaves
# oddly if they are wrong: a Steamworks identity a DRM-free game never had, an
# overlay injected into a process that cannot talk to Steam, a prefix in a
# directory that does not exist, or a pressure-vessel mount set that hides the
# game's own data from it.
#
# No Steam account, no Steam client. Part (d) is the exception that proves the
# rule: it puts a *real* Steam tree next to the GOG game, because testing "no
# overlay" on a machine with no overlay to inject proves nothing.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"

case_setup

PRODUCT=1207658930

# ---------------------------------------------------------------------------
part "a) a GOG game launches with no Steam on the machine"

fx_reset
fx_steam_tree none >/dev/null
DIR="$(fx_gog_game "$PRODUCT" title="The Witcher 2" args="-launcher --skipintro")"

# Proton installed by ProtonForge itself, in the location SteamPaths falls back
# to when there is no Steam. Before Phase 0 this was not searched at all and a
# Steam-less box could not launch anything.
stub_proton "$LAB_APP_HOME/.steam/root" "proton-cachyos-11.0-20260703-slr-x86_64" >/dev/null

PLAN="$(app_cli --launch "$PRODUCT" --dry-run)"

assert_json "the plan is valid without Steam or an explicit Proton" "$PLAN" 'd["valid"]' "true"
assert_json "the game is not native Linux" "$PLAN" 'd["nativeLinux"]' "false"
assert_json "the executable comes from the registry" "$PLAN" 'd["gameExe"]' "$DIR/bin/game.exe"

# ---------------------------------------------------------------------------
part "b) the prefix is ProtonForge's own"

assert_json "the prefix is under the GOG install root" "$PLAN" \
    'd["compatDataPath"]' "$(fx_gog_root)/prefixes/GOG/$PRODUCT"
# The old derivation would have produced "/compatdata/<id>" from two empty
# strings — a path mkpath cannot create, handed to Proton anyway.
assert_json "and never a root-relative one" "$PLAN" \
    'd["compatDataPath"].startswith("/compatdata")' "false"
assert_json "STEAM_COMPAT_DATA_PATH agrees with it" "$PLAN" \
    'd["env"]["STEAM_COMPAT_DATA_PATH"]' "$(fx_gog_root)/prefixes/GOG/$PRODUCT"

# The shader cache is only ever put in the environment on the container route —
# it is a pressure-vessel concern. This stub Proton needs no runtime, so the
# plan carries none. Game::shaderCachePath() itself is asserted in 90_gog.
assert_json "no shader path outside the container route" "$PLAN" 'd["shaderPath"]' ""

# ---------------------------------------------------------------------------
part "c) no Steam identity is claimed"

for var in SteamAppId SteamGameId STEAM_RUNTIME STEAM_COMPAT_CLIENT_INSTALL_PATH; do
    assert_json "$var is absent" "$PLAN" "d[\"env\"].get(\"$var\", \"<absent>\")" "<absent>"
done

# ---------------------------------------------------------------------------
part "d) the Steam overlay stays out, even with Steam installed"

# The point of this part: on a machine with no Steam there is no overlay to
# inject, so asserting its absence proves nothing. Here the overlay exists and
# a Steam game would get it.
fx_reset
NATIVE="$(fx_steam_tree native compat_tool= overlay=yes)"
fx_gog_game "$PRODUCT" title="The Witcher 2" >/dev/null
stub_proton "$NATIVE" "proton-cachyos-11.0-20260703-slr-x86_64" >/dev/null

PLAN="$(app_cli --launch "$PRODUCT" --dry-run --set enableSteamOverlay=true)"

assert_json "the plan is still valid" "$PLAN" 'd["valid"]' "true"
# enableSteamOverlay keeps its default of true and its stored value untouched —
# it simply stops being consulted for a launcher that has no Steam env.
assert_json "nothing is preloaded even with the overlay switched on" "$PLAN" \
    'd["env"].get("LD_PRELOAD", "")' ""
assert_json "and still no SteamAppId" "$PLAN" \
    'd["env"].get("SteamAppId", "<absent>")' "<absent>"

# The Steam game next to it must be unaffected — this is the control.
fx_add_game "$NATIVE" 1245620 name="ELDEN RING" installdir="ELDEN RING" >/dev/null
STEAM_PLAN="$(app_cli --launch 1245620 --dry-run --set enableSteamOverlay=true)"
assert_json_contains "a Steam game in the same run still gets the overlay" "$STEAM_PLAN" \
    'd["env"]["LD_PRELOAD"]' "gameoverlayrenderer.so"

# ---------------------------------------------------------------------------
part "e) launch arguments and the working directory"

fx_reset
fx_steam_tree none >/dev/null
DIR="$(fx_gog_game "$PRODUCT" title="The Witcher 2" args="-launcher --skipintro")"
stub_proton "$LAB_APP_HOME/.steam/root" "proton-cachyos-11.0-20260703-slr-x86_64" >/dev/null

PLAN="$(app_cli --launch "$PRODUCT" --dry-run --set customLaunchParams="%command% -dx11")"

# The registry's arguments come from the game's own playTask; the user's come
# last so they still win.
assert_json "the play task's arguments are passed" "$PLAN" \
    '" ".join(d["args"])' "run $DIR/bin/game.exe -launcher --skipintro -dx11"
# GOG playTasks routinely name a working directory that is not the exe's own.
assert_json "the working directory is the one the registry recorded" "$PLAN" \
    'd["workingDirectory"]' "$DIR/bin"

# ---------------------------------------------------------------------------
part "f) a native Linux game skips Proton entirely"

fx_reset
fx_steam_tree none >/dev/null
DIR="$(fx_gog_game 1207666073 title="FTL" platform=linux exe=native)"

PLAN="$(app_cli --launch 1207666073 --dry-run)"

assert_json "the plan is valid with no Proton at all" "$PLAN" 'd["valid"]' "true"
assert_json "it is a native launch" "$PLAN" 'd["nativeLinux"]' "true"
assert_json "start.sh is run directly" "$PLAN" 'd["program"]' "$DIR/start.sh"
assert_json "no Proton is involved" "$PLAN" 'd["protonPath"]' ""
assert_json "and no prefix is set up" "$PLAN" \
    'd["env"].get("STEAM_COMPAT_DATA_PATH", "<absent>")' "<absent>"

# ---------------------------------------------------------------------------
part "g) a dry run writes nothing"

fx_reset
fx_steam_tree none >/dev/null
fx_gog_game "$PRODUCT" title="The Witcher 2" >/dev/null
stub_proton "$LAB_APP_HOME/.steam/root" "proton-cachyos-11.0-20260703-slr-x86_64" >/dev/null

PREFIX="$(fx_gog_root)/prefixes/GOG/$PRODUCT"
rm -rf "$PREFIX"
app_cli --launch "$PRODUCT" --dry-run >/dev/null
assert_false "the prefix directory was not created" test -d "$PREFIX"

# ---------------------------------------------------------------------------
part "h) a real launch, with no steam binary anywhere"

fx_reset
fx_steam_tree none >/dev/null
DIR="$(fx_gog_game "$PRODUCT" title="The Witcher 2")"
stub_proton "$LAB_APP_HOME/.steam/root" "proton-cachyos-11.0-20260703-slr-x86_64" >/dev/null
stub_steam_unstartable
stub_records_reset

app_cli --launch "$PRODUCT" --timeout 10 >/dev/null 2>&1
# requiresClientRunning is false for GOG, so nothing waits for a client that
# does not exist — the launch goes straight through.
assert_eq "the launch succeeds without Steam" "0" "$(app_rc)"
assert_true "the stub Proton was actually run" stub_proton_was_run
assert_contains "and it was given the game" "$LAB_PROTON_RECORD" "game.exe"
assert_not_contains "with no SteamAppId in its environment" "$LAB_PROTON_RECORD" \
    "^ENV	SteamAppId"

stub_bin_clear

case_finish
