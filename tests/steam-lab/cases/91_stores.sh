#!/usr/bin/env bash
# lab-requires: build
#
# The store layer: what --store-list reports for each launcher that has an
# account behind it.
#
# The abstraction exists to hold two deliberately different stores. GOG has its
# own sign-in and its own downloader; Steam has neither — it authenticates with
# a Web API key typed into Settings and installs by asking the Steam client. The
# assertions here are mostly about those two answering the *same* questions
# differently and both being handled without a special case.
#
# No network: the owned-games call itself is a unit test against a fixture. What
# is checked here is everything around it — configuration state, exit codes, and
# the messages a script would have to act on.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"

case_setup

APP_STDERR="${CASE_OUT_DIR:-$LAB_OUT_DIR}/app-stderr.log"

# ---------------------------------------------------------------------------
part "a) an unconfigured Steam store"

fx_reset
fx_steam_tree native compat_tool= >/dev/null

app_cli --store-list Steam >/dev/null 2>&1
# Non-zero rather than an empty array: "no API key configured" and "you own
# nothing" are different answers and a script has to be able to tell them apart.
assert_eq "it reports the unconfigured state, not an empty library" "1" "$(app_rc)"

# The harness collects the app's stderr into a log rather than the caller's
# pipe, so that is where a diagnostic has to be looked for.
assert_contains "and says what to do about it" "$APP_STDERR" "Settings"

# ---------------------------------------------------------------------------
part "b) a store that does not exist"

app_cli --store-list Epic >/dev/null 2>&1
assert_eq "an unknown launcher is a usage error" "2" "$(app_rc)"

# ---------------------------------------------------------------------------
part "c) GOG while signed out"

fx_reset
fx_steam_tree none >/dev/null
fx_gog_signout

app_cli --store-list GOG >/dev/null 2>&1
assert_eq "it reports not being signed in" "1" "$(app_rc)"

# GOG can sign in on its own, so the advice differs from Steam's — that
# asymmetry is exactly what IStoreService::canSignIn() exists for.
assert_contains "and offers signing in rather than a settings page" \
    "$APP_STDERR" "not signed in to GOG"

# ---------------------------------------------------------------------------
part "d) the launcher a store belongs to stays resolvable"

# LauncherManager registers every launcher unconditionally and filters only on
# availability at call time. Before that change, a launcher whose isAvailable()
# was false at construction vanished for the life of the process — which is
# fatal for GOG, whose availability flips when a session is restored.
fx_reset
fx_steam_tree none >/dev/null

# Not "launcher not found": Steam is absent, but its store still exists and has
# a real answer to give. The exit code is what separates the two — 2 would mean
# the launcher could not be resolved at all, 1 means it answered.
#
# (The stderr log accumulates across the whole case, so grepping it for the
# absence of an earlier part's message would prove nothing.)
app_cli --store-list Steam >/dev/null 2>&1
assert_eq "Steam's store is reachable with no Steam installed" "1" "$(app_rc)"

case_finish
