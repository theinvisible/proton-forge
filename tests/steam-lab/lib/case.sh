# shellcheck shell=bash
#
# Scaffolding for the cases under cases/. Sourced there first; it pulls in the
# libraries, sets up the output directory and the result file, and arranges
# cleanup. It also makes every case runnable on its own:
#
#   tests/steam-lab/cases/30_discovery.sh

LAB_SRC_DIR="${LAB_SRC_DIR:-$(cd "$(dirname "${BASH_SOURCE[1]}")/.." && pwd)}"

# shellcheck source=common.sh
source "$LAB_SRC_DIR/lib/common.sh"
# shellcheck source=app.sh
source "$LAB_SRC_DIR/lib/app.sh"
# shellcheck source=fixtures.sh
source "$LAB_SRC_DIR/lib/fixtures.sh"
# shellcheck source=stubs.sh
source "$LAB_SRC_DIR/lib/stubs.sh"
# shellcheck source=gui.sh
source "$LAB_SRC_DIR/lib/gui.sh"

CASE_NAME="${CASE_NAME:-$(basename "${BASH_SOURCE[1]}" .sh)}"
CASE_OUT_DIR="${CASE_OUT_DIR:-$LAB_OUT_DIR/$CASE_NAME}"
CASE_RESULT_FILE="${CASE_RESULT_FILE:-$LAB_OUT_DIR/results/$CASE_NAME.tsv}"

lab_mkdirs
mkdir -p "$CASE_OUT_DIR" "$(dirname "$CASE_RESULT_FILE")"
: >"$CASE_RESULT_FILE"

case_setup() {
    app_require_bin >/dev/null
    fx_reset
    stub_records_reset
    info "binary: $(app_bin)"
    info "fake HOME: $LAB_APP_HOME"
}

case_teardown() {
    local rc=$?
    gui_stop_display 2>/dev/null || true
    stub_steam_pid_stop 2>/dev/null || true
    stub_steam_dbus_stop 2>/dev/null || true

    # Keep whatever the case produced next to its results.
    cp -f "$LAB_RUN_DIR/proton-invocation.txt" "$CASE_OUT_DIR/" 2>/dev/null || true
    cp -f "$LAB_RUN_DIR/runtime-invocation.txt" "$CASE_OUT_DIR/" 2>/dev/null || true
    cp -f "$LAB_APP_HOME/.config/ProtonForge/settings.json" "$CASE_OUT_DIR/" 2>/dev/null || true

    return $rc
}

# case_log <suffix> -> path for a log file belonging to this case
case_log() { printf '%s/%s.log' "$CASE_OUT_DIR" "$1"; }

# Sub-section heading in the output
part() { printf '  %s%s%s\n' "$C_BOLD" "$1" "$C_OFF"; }

case_finish() {
    case_summary
    exit $?
}

trap case_teardown EXIT
