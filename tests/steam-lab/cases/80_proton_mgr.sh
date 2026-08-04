#!/usr/bin/env bash
# lab-requires: build
#
# Installing Proton, for real, from GitHub.
#
# ProtonManager is the one part of the app that has to reach the internet, and
# the only part where a third party can break it without anyone touching this
# code: GitHub's release API, the archive layout, the rate limit. It needs no
# Steam and no account, so it belongs in this tier rather than waiting for a VM.
#
# Opt-in, because it downloads a Proton build (several hundred MB) and because
# GitHub allows 60 unauthenticated requests an hour per address — which CI
# runners share, so running this there would be flaky by construction:
#
#     LAB_TEST_NETWORK=1 steamlab test 80_proton_mgr
#
# Set LAB_GITHUB_TOKEN in lab.env to raise the limit.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"

case_setup

if [[ "$LAB_TEST_NETWORK" != "1" ]]; then
    skip "Proton installation from GitHub" "set LAB_TEST_NETWORK=1 to run this"
    case_finish
fi

# ---------------------------------------------------------------------------
part "a) with no network the failure is graceful"

# Checked first, while the fixture is still clean. The app must not hang or crash
# when it cannot reach GitHub — every user behind a captive portal hits this.
NATIVE="$(fx_steam_tree native compat_tool=)"

# There is no --check-updates command, so this is what the app does at startup
# with the checks enabled. Reaching a blackhole address is the closest thing to
# "no network" that does not need privileges.
START=$SECONDS
LAB_APP_PATH="$LAB_STUB_BIN:$PATH" \
    app_env env https_proxy="http://127.0.0.1:1" http_proxy="http://127.0.0.1:1" \
        "$(app_bin)" --steam-info >/dev/null 2>&1
ELAPSED=$(( SECONDS - START ))
if (( ELAPSED < 30 )); then
    ok "the app answers promptly with no route to GitHub (${ELAPSED}s)"
else
    fail "the app hung when GitHub was unreachable" "took ${ELAPSED}s"
fi

# ---------------------------------------------------------------------------
part "b) the release list"

# There is no CLI command for this either, so the app's own settings file is used
# to carry the token and the GUI path is what would fetch. What can be asserted
# from here is that the endpoint the app uses still answers in the shape it
# expects — if that changes, the app silently offers no versions.
API="https://api.github.com/repos/CachyOS/proton-cachyos/releases"
HEADERS=(-H "Accept: application/vnd.github+json")
[[ -n "$LAB_GITHUB_TOKEN" ]] && HEADERS+=(-H "Authorization: Bearer $LAB_GITHUB_TOKEN")

RELEASES="$(curl -sS --max-time 30 "${HEADERS[@]}" "$API?per_page=5" 2>"$(case_log curl)")"

if ! json_valid "$RELEASES"; then
    fail "the GitHub releases endpoint did not answer with JSON" \
        "$(head -c 500 <<<"$RELEASES")
$(tail -n 5 "$(case_log curl)")"
    case_finish
fi

if [[ "$(json_get "$RELEASES" 'type(d).__name__' 2>/dev/null)" != "list" ]]; then
    MESSAGE="$(json_get "$RELEASES" 'd.get("message", "")' 2>/dev/null)"
    if [[ "$MESSAGE" == *"rate limit"* ]]; then
        skip "the release list can be fetched" \
             "GitHub rate limit reached — set LAB_GITHUB_TOKEN in lab.env"
        case_finish
    fi
    fail "the releases endpoint answered with something unexpected" "$MESSAGE"
    case_finish
fi

assert_json "the endpoint returns releases" "$RELEASES" 'len(d) > 0' "true"
assert_json "each release has a tag" "$RELEASES" 'all("tag_name" in r for r in d)' "true"
# The asset naming is what ProtonManager matches on to find the archive.
assert_json "and at least one has a downloadable archive" "$RELEASES" \
    'any(any(a["name"].endswith((".tar.xz", ".tar.zst", ".tar.gz")) for a in r.get("assets", [])) for r in d)' \
    "true"

TAG="$(json_get "$RELEASES" 'd[0]["tag_name"]')"
info "newest release: $TAG"

# ---------------------------------------------------------------------------
part "c) a downloaded archive extracts into compatibilitytools.d"

# The app extracts with `tar xf` (ProtonManager.cpp) and expects to end up with a
# directory holding an executable `proton`. Doing the same here proves the archive
# layout still matches that expectation — which is a CachyOS packaging decision,
# not something this project controls.
URL="$(json_get "$RELEASES" '
[a["browser_download_url"] for r in d for a in r.get("assets", [])
 if a["name"].endswith((".tar.xz", ".tar.zst", ".tar.gz"))][0]')"
SIZE="$(json_get "$RELEASES" '
[a["size"] for r in d for a in r.get("assets", [])
 if a["name"].endswith((".tar.xz", ".tar.zst", ".tar.gz"))][0]')"
info "downloading $(basename "$URL") ($(( SIZE / 1024 / 1024 )) MB)"

ARCHIVE="$LAB_RUN_DIR/$(basename "$URL")"
if [[ -f "$ARCHIVE" ]] && [[ "$(stat -c%s "$ARCHIVE")" == "$SIZE" ]]; then
    info "reusing the cached download"
    ok "the archive can be downloaded"
elif curl -sS -L --max-time 900 -o "$ARCHIVE" "$URL" 2>>"$(case_log curl)"; then
    ok "the archive can be downloaded"
else
    fail "the download failed" "$(tail -n 5 "$(case_log curl)")"
    case_finish
fi

COMPAT="$NATIVE/compatibilitytools.d"
rm -rf "$COMPAT"; mkdir -p "$COMPAT"
if tar -C "$COMPAT" -xf "$ARCHIVE" 2>"$(case_log tar)"; then
    ok "tar xf extracts it, as ProtonManager does"
else
    fail "extraction failed" "$(tail -n 10 "$(case_log tar)")"
    case_finish
fi

TOOLDIR="$(find "$COMPAT" -mindepth 1 -maxdepth 1 -type d | head -n1)"
assert_dir "it unpacks into a single directory" "$TOOLDIR"
assert_exec "which contains an executable 'proton', as the app requires" "$TOOLDIR/proton"

# ---------------------------------------------------------------------------
part "d) and the app then recognises it"

INFO="$(app_cli --steam-info)"
assert_json "the extracted build is listed as a compat tool" "$INFO" 'len(d["compatTools"])' "1"
assert_json "and recognised as Proton-CachyOS" "$INFO" 'd["protonCachyOSInstalled"]' "true"
assert_json_contains "with its version read from the directory name" "$INFO" \
    'd["installedVersion"]' "."

# A real Proton declares which Steam Linux Runtime it needs, and that is what
# turns the launch into the container chain — so a real archive is the strongest
# check that the toolmanifest reading works against the real thing.
if [[ -f "$TOOLDIR/toolmanifest.vdf" ]]; then
    ok "the extracted build ships a toolmanifest.vdf"
    REQUIRED="$(sed -n 's/.*"require_tool_appid"[[:space:]]*"\([0-9]*\)".*/\1/p' \
        "$TOOLDIR/toolmanifest.vdf" | head -n1)"
    if [[ -n "$REQUIRED" ]]; then
        info "it asks for Steam Linux Runtime appid $REQUIRED"
        fx_add_game "$NATIVE" 1245620 name="ELDEN RING" installdir="ELDEN RING" >/dev/null
        PLAN="$(app_cli --launch 1245620 --dry-run)"
        assert_json "and the app plans the container chain accordingly" "$PLAN" \
            'd["runtimeRequired"]' "true"
    else
        info "it runs directly on the host, with no runtime requirement"
    fi
else
    skip "the extracted build ships a toolmanifest.vdf" "none present in this archive"
fi

case_finish
