#!/usr/bin/env bash
# lab-requires: build
#
# The unit tests, folded into this report.
#
# They run perfectly well on their own (`ctest --test-dir cmake-build-debug`),
# and CI runs them that way. Running them here too means `steamlab test` gives
# one junit.xml covering both tiers, so a single artifact answers "is the build
# healthy" rather than two that have to be read together.

set -uo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/case.sh"

BUILD_DIR="$REPO_ROOT/cmake-build-debug"

part "a) the test targets are configured"

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]] \
   || ! grep -q '^PROTONFORGE_BUILD_TESTS:BOOL=ON' "$BUILD_DIR/CMakeCache.txt"; then
    info "configuring with -DPROTONFORGE_BUILD_TESTS=ON"
    if cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
             -DPROTONFORGE_BUILD_TESTS=ON >"$(case_log cmake-configure)" 2>&1; then
        ok "cmake configures with the tests enabled"
    else
        fail "cmake configure failed" "$(tail -n 20 "$(case_log cmake-configure)")"
        case_finish
    fi
else
    ok "cmake configures with the tests enabled"
fi

part "b) everything builds"

if cmake --build "$BUILD_DIR" -j"$(nproc)" >"$(case_log cmake-build)" 2>&1; then
    ok "the library, the app and the tests build"
else
    fail "the build failed" "$(tail -n 30 "$(case_log cmake-build)")"
    case_finish
fi

# A warning-free build is not asserted — the project has pre-existing warnings
# and turning that into a red test here would only teach people to ignore it.
warnings="$(grep -c 'warning:' "$(case_log cmake-build)" || true)"
info "compiler warnings in this build: ${warnings:-0}"

part "c) ctest"

CTEST_LOG="$(case_log ctest)"
( cd "$BUILD_DIR" && ctest --output-on-failure ) >"$CTEST_LOG" 2>&1
CTEST_RC=$?

# One check per unit-test executable, so junit.xml names the one that broke
# rather than reporting "ctest failed".
seen=0
while IFS= read -r line; do
    name="$(printf '%s' "$line" | sed -n 's/^ *[0-9]*\/[0-9]* Test *#[0-9]*: *\([^ ]*\).*/\1/p')"
    [[ -z "$name" ]] && continue
    seen=$(( seen + 1 ))
    if printf '%s' "$line" | grep -q 'Passed'; then
        ok "$name"
    else
        # Pull that test's own output out of the log for the failure detail.
        detail="$(awk -v t="$name" '
            index($0, "Start testing of " t)    {capture=1}
            capture                             {print}
            index($0, "Finished testing of " t) {capture=0}
        ' "$CTEST_LOG" | grep -E '^(FAIL|QFATAL|QWARN)' | head -n 20)"
        fail "$name" "${detail:-see $CTEST_LOG}"
    fi
done < <(grep -E '^ *[0-9]+/[0-9]+ Test +#[0-9]+:' "$CTEST_LOG")

# No per-test lines at all means ctest did not get as far as running anything —
# report the raw result rather than silently claiming success.
if (( seen == 0 )); then
    if (( CTEST_RC == 0 )); then
        fail "ctest ran no tests" "$(tail -n 20 "$CTEST_LOG")"
    else
        fail "ctest failed before running any test" "$(tail -n 30 "$CTEST_LOG")"
    fi
else
    info "$seen unit test executables, ctest exit code $CTEST_RC"
fi
case_finish
