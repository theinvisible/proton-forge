# shellcheck shell=bash
#
# Configuration, output and the assertion library. Sourced by everything.
#
# Note there is no `set -e` anywhere in this suite except in build-deb.sh. A
# failed assertion has to record itself and let the case carry on — one broken
# check should tell you about the other nine, not hide them.

set -o pipefail

LAB_SRC_DIR="${LAB_SRC_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
REPO_ROOT="${REPO_ROOT:-$(cd "$LAB_SRC_DIR/../.." && pwd)}"
export LAB_SRC_DIR REPO_ROOT

# Environment beats lab.env beats the defaults below.
if [[ -f "$LAB_SRC_DIR/lab.env" ]]; then
    # shellcheck disable=SC1091
    source "$LAB_SRC_DIR/lab.env"
fi

# --------------------------------------------------------------------- paths
#
# The work directory lives outside the repository. It fills up with container
# images' worth of packages and a Flatpak installation, and none of that belongs
# anywhere near a working copy.
: "${PF_LAB_DIR:=$HOME/.cache/protonforge-testlab}"
: "${PF_BIN:=}"

LAB_RUN_DIR="$PF_LAB_DIR/run"
LAB_OUT_DIR="$PF_LAB_DIR/out"
LAB_DEB_DIR="$PF_LAB_DIR/deb"

# The fake $HOME every case points the app at. $HOME is the only lever the app
# offers — SteamPaths derives every Steam path from QDir::homePath() — so
# redirecting it redirects the entire Steam world, and the developer's real
# ~/.steam and ~/.config/ProtonForge are never touched.
LAB_APP_HOME="$PF_LAB_DIR/apphome"
LAB_APP_TMP="$PF_LAB_DIR/apptmp"
LAB_STUB_BIN="$PF_LAB_DIR/stubbin"
LAB_STUB_DATA="$LAB_SRC_DIR/fixtures"
LAB_FLATPAK_DIR="$PF_LAB_DIR/flatpak"

export LAB_APP_HOME LAB_APP_TMP LAB_STUB_BIN LAB_STUB_DATA

# ------------------------------------------------------------------- tunables
: "${LAB_DOCKER_PREFIX:=protonforge-lab}"
: "${LAB_DOCKER_NET:=none}"        # deterministic by default: no Steam CDN, no ProtonDB, no GitHub
: "${LAB_GUI_WIDTH:=1280}"
: "${LAB_GUI_HEIGHT:=800}"
: "${LAB_GUI_WM:=openbox}"
: "${LAB_FLATPAK_DOCKER:=0}"       # 1 = build/run the Flatpak in a privileged container
: "${LAB_TEST_NETWORK:=0}"         # 1 = allow the cases that reach the internet
: "${LAB_GITHUB_TOKEN:=}"          # optional PAT, only for 80_proton_mgr

: "${TIMEOUT_BUILD:=1800}"
: "${TIMEOUT_GUI:=25}"
: "${TIMEOUT_CLI:=60}"
: "${TIMEOUT_FLATPAK:=3600}"
: "${TIMEOUT_LAUNCH:=30}"

# -------------------------------------------------------------------- output
if [[ -t 1 && "${NO_COLOR:-}" == "" ]]; then
    C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_YELLOW=$'\033[33m'
    C_BLUE=$'\033[34m'; C_BOLD=$'\033[1m'; C_DIM=$'\033[2m'; C_OFF=$'\033[0m'
else
    C_RED=''; C_GREEN=''; C_YELLOW=''
    C_BLUE=''; C_BOLD=''; C_DIM=''; C_OFF=''
fi

_ts() { date '+%H:%M:%S'; }

step() { printf '%s\n' "${C_BOLD}${C_BLUE}==>${C_OFF} ${C_BOLD}$*${C_OFF}"; }
info() { printf '%s\n' "${C_DIM}[$(_ts)]${C_OFF} $*"; }
warn() { printf '%s\n' "${C_YELLOW}[warn]${C_OFF} $*" >&2; }
err()  { printf '%s\n' "${C_RED}[error]${C_OFF} $*" >&2; }
die()  { err "$*"; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

lab_mkdirs() {
    mkdir -p "$LAB_RUN_DIR" "$LAB_OUT_DIR" "$LAB_DEB_DIR" \
             "$LAB_APP_HOME" "$LAB_APP_TMP" "$LAB_STUB_BIN"
}

# ----------------------------------------------------------------- utilities

# pid_alive <pid>
#
# A zombie does not count. It has already exited and is only waiting to be
# reaped, but /proc/<pid> and everything in it survive until someone does — and
# nobody may: a process started inside a command substitution is reparented when
# that subshell exits, and a container's PID 1 is frequently not a reaper. On a
# desktop the orphan is collected almost immediately and this never shows; in CI
# the entry lingers for the life of the job and "has it exited yet" answers no
# forever.
pid_alive() {
    [[ -n "${1:-}" ]] || return 1
    [[ -d "/proc/$1" ]] || return 1
    local stat
    stat="$(</proc/"$1"/stat)" 2>/dev/null || return 0
    # Everything up to the last ") " is pid and comm; comm can contain spaces and
    # parentheses, so it cannot be split on whitespace. The state follows.
    [[ "${stat##*') '}" == Z* ]] && return 1
    return 0
}

# pid_comm <pid> -> the kernel's idea of the process name. This is what
# SteamClient checks, so the stubs have to satisfy it rather than a pattern.
pid_comm() { [[ -r "/proc/$1/comm" ]] && tr -d '\n' <"/proc/$1/comm"; }

# wait_for <timeout-seconds> <description> <command...>  -- polls once per second
wait_for() {
    local timeout="$1" desc="$2"; shift 2
    local waited=0
    while ! "$@" >/dev/null 2>&1; do
        if (( waited >= timeout )); then err "timed out after ${timeout}s: $desc"; return 1; fi
        sleep 1
        waited=$(( waited + 1 ))
        if (( waited % 15 == 0 )); then info "waiting for $desc (${waited}/${timeout}s)"; fi
    done
    return 0
}

# json_get <json> <python-expression on `d`>
#
# Every CLI command that returns anything structured returns JSON, so most
# assertions boil down to pulling one value out of a document. Kept as a single
# helper rather than a jq dependency: python3 is already required for the JUnit
# report, jq is one more thing to install.
json_get() {
    printf '%s' "$1" | python3 -c '
import json, sys
try:
    d = json.load(sys.stdin)
except Exception as exc:
    sys.stderr.write("invalid JSON: %s\n" % exc)
    sys.exit(1)
v = eval(sys.argv[1], {"d": d, "len": len, "sorted": sorted, "any": any, "all": all})
if isinstance(v, bool):
    print("true" if v else "false")
elif v is None:
    print("")
elif isinstance(v, (list, dict)):
    print(json.dumps(v, sort_keys=True))
else:
    print(v)
' "$2"
}

# json_valid <json>
json_valid() { printf '%s' "$1" | python3 -c 'import json,sys; json.load(sys.stdin)' 2>/dev/null; }

# ------------------------------------------------------------- the assertions
#
# Every case runs as its own process. Results are appended to $CASE_RESULT_FILE
# as TSV (status<TAB>name<TAB>detail) and aggregated into junit.xml by the
# runner, so a check is recorded even when the case later dies.

CASE_CHECKS_OK=0
CASE_CHECKS_FAILED=0
CASE_CHECKS_SKIPPED=0

_case_record() {
    local status="$1" name="$2" detail="${3:-}"
    [[ -n "${CASE_RESULT_FILE:-}" ]] || return 0
    printf '%s\t%s\t%s\n' "$status" "$name" "${detail//$'\n'/ | }" >>"$CASE_RESULT_FILE"
}

ok() {
    CASE_CHECKS_OK=$(( CASE_CHECKS_OK + 1 ))
    printf '   %sok%s   %s\n' "$C_GREEN" "$C_OFF" "$1"
    _case_record ok "$1"
}

fail() {
    CASE_CHECKS_FAILED=$(( CASE_CHECKS_FAILED + 1 ))
    printf '   %sFAIL%s %s\n' "$C_RED" "$C_OFF" "$1"
    [[ -n "${2:-}" ]] && printf '        %s\n' "${2//$'\n'/$'\n'        }"
    _case_record fail "$1" "${2:-}"
}

skip() {
    CASE_CHECKS_SKIPPED=$(( CASE_CHECKS_SKIPPED + 1 ))
    printf '   %sskip%s %s%s\n' "$C_YELLOW" "$C_OFF" "$1" "${2:+ ($2)}"
    _case_record skip "$1" "${2:-}"
}

assert_true() {
    local name="$1"; shift
    local out
    if out="$("$@" 2>&1)"; then ok "$name"; else fail "$name" "$*
$out"; fi
}

assert_false() {
    local name="$1"; shift
    local out
    if out="$("$@" 2>&1)"; then
        fail "$name" "command unexpectedly succeeded: $*
$out"
    else
        ok "$name"
    fi
}

assert_eq() {
    if [[ "$2" == "$3" ]]; then
        ok "$1"
    else
        fail "$1" "expected: '$2'
actual:   '$3'"
    fi
}

assert_ne() {
    if [[ "$2" != "$3" ]]; then
        ok "$1"
    else
        fail "$1" "both values are '$2', expected them to differ"
    fi
}

# assert_contains_str <name> <haystack> <needle>
assert_contains_str() {
    if [[ "$2" == *"$3"* ]]; then
        ok "$1"
    else
        fail "$1" "'$3' not found in:
$2"
    fi
}

assert_not_contains_str() {
    if [[ "$2" != *"$3"* ]]; then
        ok "$1"
    else
        fail "$1" "'$3' unexpectedly present in:
$2"
    fi
}

# assert_contains <name> <file> <extended regex>
assert_contains() {
    local name="$1" file="$2" pattern="$3"
    if [[ -f "$file" ]] && grep -Eq -- "$pattern" "$file"; then
        ok "$name"
    else
        local tail=""
        [[ -f "$file" ]] && tail="$(tail -n 15 "$file")"
        fail "$name" "pattern '$pattern' not found in $file
--- last lines ---
$tail"
    fi
}

assert_not_contains() {
    local name="$1" file="$2" pattern="$3"
    if [[ -f "$file" ]] && grep -Eq -- "$pattern" "$file"; then
        fail "$name" "pattern '$pattern' unexpectedly found:
$(grep -En -- "$pattern" "$file" | head -n 5)"
    else
        ok "$name"
    fi
}

assert_file() {
    if [[ -f "$2" ]]; then ok "$1"; else fail "$1" "no such file: $2"; fi
}

assert_no_file() {
    if [[ ! -e "$2" ]]; then ok "$1"; else fail "$1" "should not exist: $2"; fi
}

assert_dir() {
    if [[ -d "$2" ]]; then ok "$1"; else fail "$1" "no such directory: $2"; fi
}

assert_exec() {
    if [[ -x "$2" ]]; then ok "$1"; else fail "$1" "not executable: $2"; fi
}

# assert_json <name> <json> <expression on `d`> <expected>
assert_json() {
    local name="$1" json="$2" expr="$3" expected="$4"
    local actual
    if ! actual="$(json_get "$json" "$expr" 2>&1)"; then
        fail "$name" "could not evaluate '$expr':
$actual
--- document ---
$json"
        return
    fi
    if [[ "$actual" == "$expected" ]]; then
        ok "$name"
    else
        fail "$name" "$expr
expected: '$expected'
actual:   '$actual'
--- document ---
$json"
    fi
}

# assert_json_contains <name> <json> <expression> <substring>
assert_json_contains() {
    local name="$1" json="$2" expr="$3" needle="$4"
    local actual
    if ! actual="$(json_get "$json" "$expr" 2>&1)"; then
        fail "$name" "could not evaluate '$expr':
$actual"
        return
    fi
    if [[ "$actual" == *"$needle"* ]]; then
        ok "$name"
    else
        fail "$name" "$expr
expected to contain: '$needle'
actual:              '$actual'"
    fi
}

# assert_mode <name> <file> <expected octal>
assert_mode() {
    local actual
    actual="$(stat -c '%a' "$2" 2>/dev/null)"
    assert_eq "$1" "$3" "${actual:-<missing>}"
}

case_summary() {
    local total=$(( CASE_CHECKS_OK + CASE_CHECKS_FAILED ))
    local skipped=""
    (( CASE_CHECKS_SKIPPED )) && skipped=", $CASE_CHECKS_SKIPPED skipped"

    if (( CASE_CHECKS_FAILED > 0 )); then
        printf '   %s%d/%d checks failed%s%s\n' \
            "$C_RED" "$CASE_CHECKS_FAILED" "$total" "$C_OFF" "$skipped"
        return 1
    fi
    # A case that only skipped should not read as "0/0 checks passed", which
    # looks like it ran and found nothing rather than deliberately not running.
    if (( total == 0 )); then
        printf '   %snothing to check here%s%s\n' "$C_YELLOW" "$C_OFF" "${skipped:-, nothing skipped either}"
        return 0
    fi
    printf '   %s%d/%d checks passed%s%s\n' \
        "$C_GREEN" "$CASE_CHECKS_OK" "$total" "$C_OFF" "$skipped"
    return 0
}
