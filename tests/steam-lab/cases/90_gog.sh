#!/usr/bin/env bash
# lab-requires: build
#
# GOG as a game source: discovery from the install registry, and what the CLI
# reports about a session.
#
# GogLauncher lists exactly what GogInstallRegistry records — there is no
# filesystem scan, deliberately, so that no other tool's install can be mistaken
# for one of ours and uninstall always knows what it may delete. That makes a
# fabricated registry plus a fake $HOME a complete test bed.
#
# No GOG account, no network, no Steam. The stored token is a fixture string
# that GOG would reject, so a case that accidentally reached the real API would
# fail loudly rather than succeed against somebody's library.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"

case_setup

# ---------------------------------------------------------------------------
part "a) signed out, nothing installed"

fx_reset
fx_steam_tree none >/dev/null
fx_gog_signout

STATUS="$(app_cli --gog-status)"
assert_json "not signed in" "$STATUS" 'd["loggedIn"]' "false"

GAMES="$(app_cli --list-games)"
assert_json "and no games at all" "$GAMES" 'len(d)' "0"

# The sign-in URL is a pure computation and must work with no session, since it
# is what a user needs *before* they have one.
URL="$(app_cli --gog-login-url)"
assert_contains_str "the login URL is GOG's auth endpoint" "$URL" "auth.gog.com/auth"
assert_contains_str "and carries the redirect GOG has registered" "$URL" "embed.gog.com"

# ---------------------------------------------------------------------------
part "b) an installed GOG game, with no Steam anywhere"

fx_reset
fx_steam_tree none >/dev/null
fx_gog_game 1207658930 title="The Witcher 2" build=100 latest=100 >/dev/null

GAMES="$(app_cli --list-games)"
assert_json "the GOG game is discovered" "$GAMES" 'len(d)' "1"
assert_json "and is attributed to GOG" "$GAMES" 'd[0]["launcher"]' "GOG"
assert_json "under its product id" "$GAMES" 'd[0]["appId"]' "1207658930"
assert_json "with a launcher-namespaced settings key" "$GAMES" \
    'd[0]["settingsKey"]' "GOG:1207658930"

# Being signed out must not hide an installed game: GOG games are DRM-free, so
# the account is only needed to browse and install.
assert_eq "discovery works while signed out" "0" "$(app_rc)"

# Every trait false is what keeps Steam's machinery away from this game.
for trait in usesSteamEnv requiresClientRunning supportsLaunchOptionsIO providesUpdateState idIsSteamAppId; do
    assert_json "trait $trait is false" "$GAMES" "d[0][\"traits\"][\"$trait\"]" "false"
done

# The prefix is ProtonForge's own, not a Steam compatdata directory.
assert_json_contains "the prefix lives under the GOG install root" "$GAMES" \
    'd[0]["compatDataPath"]' "/prefixes/GOG/1207658930"
# Derived rather than empty, and under the GOG root rather than a Steam
# library: Proton skips fossilize caching entirely without this variable, so a
# GOG game gets the same shader pre-caching a Steam one does.
assert_json_contains "the shader cache is under the GOG install root" "$GAMES" \
    'd[0]["shaderCachePath"]' "/Games/ProtonForge/shadercache/1207658930"

# ---------------------------------------------------------------------------
part "c) the banner comes from the registry, never from the id"

fx_reset
fx_steam_tree none >/dev/null
BANNER="https://images.gog-statics.com/abc123_product_tile_256.jpg"
fx_gog_game 1207658930 title="With Art" image="$BANNER" >/dev/null

GAMES="$(app_cli --list-games)"
assert_json "the recorded banner is handed to the list" "$GAMES" 'd[0]["imageUrl"]' "$BANNER"

fx_reset
fx_steam_tree none >/dev/null
fx_gog_game 1207658930 title="No Art Yet" >/dev/null

GAMES="$(app_cli --list-games)"
# A GOG product id is not a Steam appid, and GOG's artwork is content-hashed —
# there is nothing to derive from the id. Empty is the honest answer, and it is
# what tells the store service to go and look one up; a guessed URL would leave
# the tile shimmering at a 404 forever.
assert_json "and nothing is invented when none was recorded" "$GAMES" 'd[0]["imageUrl"]' ""

# Discovery must not have gone looking for it either — it runs on the game
# list's worker thread, where a network call would block the UI.
assert_eq "listing a GOG game with no artwork still succeeds" "0" "$(app_rc)"

# ---------------------------------------------------------------------------
part "d) an incomplete download is not a game"

fx_reset
fx_steam_tree none >/dev/null
fx_gog_game 1207658930 title="Half Done" complete=no >/dev/null

GAMES="$(app_cli --list-games)"
# It stays in the registry — that is what makes it resumable — but offering a
# half-written executable to launch would be worse than not listing it.
assert_json "an unfinished install is not listed" "$GAMES" 'len(d)' "0"

# ---------------------------------------------------------------------------
part "e) an update waiting"

fx_reset
fx_steam_tree none >/dev/null
fx_gog_game 1207658930 title="Outdated" build=100 latest=200 >/dev/null

GAMES="$(app_cli --list-games)"
assert_json "the game reports an update" "$GAMES" 'd[0]["needsUpdate"]' "true"

fx_reset
fx_steam_tree none >/dev/null
fx_gog_game 1207658930 title="Unchecked" build=100 latest= >/dev/null
GAMES="$(app_cli --list-games)"
# latest= means "never checked". Claiming an update on no evidence would send
# the user into a pointless multi-gigabyte re-download.
assert_json "but never on an unchecked one" "$GAMES" 'd[0]["needsUpdate"]' "false"

# ---------------------------------------------------------------------------
part "f) a native Linux install"

fx_reset
fx_steam_tree none >/dev/null
fx_gog_game 1207666073 title="FTL" platform=linux exe=native >/dev/null

GAMES="$(app_cli --list-games)"
assert_json "it is marked native" "$GAMES" 'd[0]["nativeLinux"]' "true"
assert_json_contains "and points at start.sh" "$GAMES" 'd[0]["executablePath"]' "start.sh"

# ---------------------------------------------------------------------------
part "g) uninstalling something we never installed"

fx_reset
fx_steam_tree none >/dev/null
fx_gog_tree >/dev/null

app_cli --gog-uninstall 1207658930 >/dev/null 2>&1
# The registry is the only record of what ProtonForge owns, so a product it has
# no entry for must be refused rather than guessed at — this command deletes a
# directory tree.
assert_eq "it refuses rather than guessing a path" "1" "$(app_rc)"

# ---------------------------------------------------------------------------
part "h) --gog-install needs a session"

fx_reset
fx_steam_tree none >/dev/null
fx_gog_signout

app_cli --gog-install 1207658930 >/dev/null 2>&1
# Unlike the read-only content system, chunk URLs are signed per user. Saying so
# up front beats resolving a whole build and failing at the first chunk.
assert_eq "installing while signed out fails" "1" "$(app_rc)"

case_finish
