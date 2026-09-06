#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Find the test binary
TEST_BIN=""
POSSIBLE_PATHS=(
    "${QUIVER_TEST_BIN:-}"
    "${WORKSPACE_DIR}/build/tests/quiver_unit_tests"
    "${WORKSPACE_DIR}/build-local/tests/quiver_unit_tests"
    "${SCRIPT_DIR}/quiver_unit_tests"
)

for p in "${POSSIBLE_PATHS[@]}"; do
    if [[ -n "$p" && -x "$p" ]]; then
        TEST_BIN="$p"
        break
    fi
done

if [[ -z "$TEST_BIN" ]]; then
    echo "Error: quiver_unit_tests binary not found. Please build it first (e.g. cmake --build build)." >&2
    exit 1
fi

MODE="auto"
TOOL=""
PASSTHROUGH_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --wayland)
            MODE="wayland"
            shift
            ;;
        --x11)
            MODE="x11"
            shift
            ;;
        --both)
            MODE="both"
            shift
            ;;
        --native)
            MODE="native"
            shift
            ;;
        --gdb)
            TOOL="gdb"
            shift
            ;;
        --batch-gdb)
            TOOL="batch-gdb"
            shift
            ;;
        --valgrind)
            TOOL="valgrind"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS] [Catch2 args...]"
            echo ""
            echo "Display Options:"
            echo "  --wayland     Run in headless Wayland via wlheadless-run -c weston"
            echo "  --x11         Run in headless X11 via xvfb-run -a"
            echo "  --both        Run sequentially under Wayland and X11"
            echo "  --native      Run directly with current active display"
            echo ""
            echo "Tool Options:"
            echo "  --gdb         Run under interactive GDB with Catch2 --break"
            echo "  --batch-gdb   Dump GDB stack trace on test crash/failure"
            echo "  --valgrind    Run under Valgrind Memcheck with GLib suppressions"
            echo ""
            echo "All additional arguments are forwarded to Catch2."
            exit 0
            ;;
        *)
            PASSTHROUGH_ARGS+=("$1")
            shift
            ;;
    esac
done

run_single() {
    local disp_mode="$1"
    shift
    local cmd=()

    case "$disp_mode" in
        wayland)
            if command -v wlheadless-run >/dev/null 2>&1 && command -v weston >/dev/null 2>&1; then
                cmd=(wlheadless-run -c weston --)
            else
                echo "Warning: wlheadless-run or weston not found, falling back to direct run." >&2
            fi
            ;;
        x11)
            if command -v xvfb-run >/dev/null 2>&1; then
                cmd=(xvfb-run -a)
            else
                echo "Warning: xvfb-run not found, falling back to direct run." >&2
            fi
            ;;
        native)
            cmd=()
            ;;
        auto)
            if [[ -n "${WAYLAND_DISPLAY:-}" || -n "${DISPLAY:-}" ]]; then
                cmd=()
            elif command -v wlheadless-run >/dev/null 2>&1 && command -v weston >/dev/null 2>&1; then
                cmd=(wlheadless-run -c weston --)
            elif command -v xvfb-run >/dev/null 2>&1; then
                cmd=(xvfb-run -a)
            else
                cmd=()
            fi
            ;;
    esac

    case "$TOOL" in
        gdb)
            "${cmd[@]}" gdb --args "$TEST_BIN" --break "${PASSTHROUGH_ARGS[@]}"
            ;;
        batch-gdb)
            "${cmd[@]}" gdb -batch -ex "run" -ex "bt full" --args "$TEST_BIN" "${PASSTHROUGH_ARGS[@]}"
            ;;
        valgrind)
            local supp_file="${SCRIPT_DIR}/valgrind_suppressions.supp"
            G_SLICE=always-malloc G_DEBUG=gc-friendly "${cmd[@]}" valgrind \
                --leak-check=full \
                --suppressions="$supp_file" \
                --error-exitcode=1 \
                "$TEST_BIN" "${PASSTHROUGH_ARGS[@]}"
            ;;
        *)
            "${cmd[@]}" "$TEST_BIN" "${PASSTHROUGH_ARGS[@]}"
            ;;
    esac
}

if [[ "$MODE" == "both" ]]; then
    echo "============================================================"
    echo ">>> Running test suite under Headless Wayland (wlheadless-run) <<<"
    echo "============================================================"
    run_single wayland
    echo ""
    echo "============================================================"
    echo ">>> Running test suite under Headless X11 (xvfb-run) <<<"
    echo "============================================================"
    run_single x11
    echo ""
    echo ">>> Both Wayland and X11 test suites completed successfully! <<<"
else
    run_single "$MODE"
fi
