#!/usr/bin/env bash
# Run cadd0040 on every testcase under testcases/ and summarize scores.
#
# Usage:
#   ./scripts/run_all_testcases.sh [options]
#
# Options:
#   --build-dir <dir>      Path to CMake build directory (default: build)
#   --optimizer <name>     --optimizer value (default: tabu-random)
#   --seconds <n>          Optimizer time budget in seconds (default: 570)
#   --debug                Enable optimizer debug progress output

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
OPTIMIZER="tabu-random"
SA_SECONDS="570"
DEBUG_ARGS=()

print_help() {
    awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$0"
}

require_value() {
    local option="$1"
    local value="${2:-}"
    if [[ -z "${value}" ]]; then
        echo "${option} requires a value" >&2
        exit 1
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            require_value "$1" "${2:-}"
            BUILD_DIR="$2"
            shift 2
            ;;
        --build-dir=*)
            BUILD_DIR="${1#*=}"
            shift
            ;;
        --optimizer)
            require_value "$1" "${2:-}"
            OPTIMIZER="$2"
            shift 2
            ;;
        --optimizer=*)
            OPTIMIZER="${1#*=}"
            shift
            ;;
        --seconds)
            require_value "$1" "${2:-}"
            SA_SECONDS="$2"
            shift 2
            ;;
        --seconds=*)
            SA_SECONDS="${1#*=}"
            shift
            ;;
        --debug)
            DEBUG_ARGS=(--debug)
            shift
            ;;
        -h | --help)
            print_help
            exit 0
            ;;
        *)
            echo "Unknown option: $1 (try --help)" >&2
            exit 1
            ;;
    esac
done

BINARY="${BUILD_DIR}/cadd0040"
TESTCASES_DIR="${ROOT}/testcases"

if [[ ! -x "${BINARY}" ]]; then
    echo "Binary not found: ${BINARY}" >&2
    echo "Build first: make build" >&2
    exit 1
fi

if [[ ! -d "${TESTCASES_DIR}" ]]; then
    echo "Testcases directory not found: ${TESTCASES_DIR}" >&2
    exit 1
fi

TESTCASES=()
while IFS= read -r testcase; do
    TESTCASES+=("${testcase}")
done < <(find "${TESTCASES_DIR}" -mindepth 1 -maxdepth 1 -type d -name 'testcase*' | sort)
if [[ ${#TESTCASES[@]} -eq 0 ]]; then
    echo "No testcase* directories under ${TESTCASES_DIR}" >&2
    exit 1
fi

echo "========================================"
echo "cadd0040 batch run"
echo "  binary   : ${BINARY}"
echo "  optimizer: ${OPTIMIZER}"
echo "  SA budget: ${SA_SECONDS}s"
if [[ ${#DEBUG_ARGS[@]} -gt 0 ]]; then
    echo "  debug log: on"
else
    echo "  debug log: off"
fi
echo "  cases    : ${#TESTCASES[@]}"
echo "========================================"
echo

PASS=0
FAIL=0
TOTAL_ELAPSED=0

printf "%-12s %10s %10s %12s %s\n" "TESTCASE" "INITIAL" "FINAL" "TIME(s)" "STATUS"
printf "%-12s %10s %10s %12s %s\n" "--------" "-------" "-----" "-------" "------"

for testcase_path in "${TESTCASES[@]}"; do
    testcase_name="$(basename "${testcase_path}")"
    output_file="${testcase_path}/modified_clk_tree.structure"

    for required in clk_tree.structure buf.lib SS_delay.rpt FF_delay.rpt; do
        if [[ ! -f "${testcase_path}/${required}" ]]; then
            echo "SKIP ${testcase_name}: missing ${required}" >&2
            ((FAIL++)) || true
            printf "%-12s %10s %10s %12s %s\n" "${testcase_name}" "-" "-" "-" "SKIP(missing input)"
            continue 2
        fi
    done

    log_file="$(mktemp)"
    start_ns="$(date +%s)"

    if [[ ${#DEBUG_ARGS[@]} -gt 0 ]]; then
        echo ">>> ${testcase_name}" >&2
    fi

    set +e
    "${BINARY}" \
        --optimizer "${OPTIMIZER}" \
        --seconds "${SA_SECONDS}" \
        "${DEBUG_ARGS[@]}" \
        "${testcase_path}" \
        "${output_file}" \
        2>&1 | tee "${log_file}"
    exit_code="${PIPESTATUS[0]}"
    set -e

    end_ns="$(date +%s)"
    elapsed="$((end_ns - start_ns))"
    TOTAL_ELAPSED="$((TOTAL_ELAPSED + elapsed))"

    initial_score="$(grep -E '^Initial Score = ' "${log_file}" | tail -1 | awk '{print $NF}' || true)"
    final_score="$(grep -E '^Final Score = ' "${log_file}" | tail -1 | awk '{print $NF}' || true)"
    initial_score="${initial_score:-"-"}"
    final_score="${final_score:-"-"}"

    if [[ ${exit_code} -eq 0 && -f "${output_file}" ]]; then
        status="OK"
        ((PASS++)) || true
    else
        status="FAIL(exit ${exit_code})"
        ((FAIL++)) || true
        echo "--- ${testcase_name} log ---" >&2
        cat "${log_file}" >&2
        echo "------------------------" >&2
    fi

    printf "%-12s %10s %10s %12s %s\n" \
        "${testcase_name}" "${initial_score}" "${final_score}" "${elapsed}" "${status}"

    rm -f "${log_file}"
done

echo
echo "========================================"
echo "Summary: ${PASS} passed, ${FAIL} failed"
echo "Total wall time: ${TOTAL_ELAPSED}s"
echo "========================================"

if [[ ${FAIL} -gt 0 ]]; then
    exit 1
fi
