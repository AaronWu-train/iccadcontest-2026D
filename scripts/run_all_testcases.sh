#!/usr/bin/env bash
# Run cadd0040 on every testcase under testcases/ and summarize scores.
#
# Usage:
#   ./scripts/run_all_testcases.sh
#   CADD0040_SA_SECONDS=60 ./scripts/run_all_testcases.sh
#   BUILD_DIR=build ./scripts/run_all_testcases.sh
#
# Environment:
#   BUILD_DIR                        Path to CMake build directory (default: build-release)
#   CADD0040_SA_SECONDS              Optimizer time budget in seconds (default: 500)
#   CADD0040_CHECKPOINT_STEPS        Best-so-far output checkpoint interval (default: 1024)
#   OPTIMIZER                        --optimizer value (default: anneal)
#   CADD0040_REPORT_METRICS          Set to 0 to suppress per-run score lines (default: on)
#   CADD0040_DEBUG_PROGRESS            Set to 0 to disable periodic best-score progress (default: on)
#   CADD0040_DEBUG_PROGRESS_INTERVAL Progress interval in seconds (default: 15)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build-release}"
BINARY="${BUILD_DIR}/cadd0040"
TESTCASES_DIR="${ROOT}/testcases"
OPTIMIZER="${OPTIMIZER:-anneal}"
SA_SECONDS="${CADD0040_SA_SECONDS:-500}"
CHECKPOINT_STEPS="${CADD0040_CHECKPOINT_STEPS:-1024}"
REPORT_METRICS="${CADD0040_REPORT_METRICS:-1}"
DEBUG_PROGRESS="${CADD0040_DEBUG_PROGRESS:-1}"
DEBUG_PROGRESS_INTERVAL="${CADD0040_DEBUG_PROGRESS_INTERVAL:-15}"

if [[ ! -x "${BINARY}" ]]; then
    echo "Binary not found: ${BINARY}" >&2
    echo "Build first: make release" >&2
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
if [[ "${DEBUG_PROGRESS}" == "1" ]]; then
    echo "  progress : every ${DEBUG_PROGRESS_INTERVAL}s (best score)"
else
    echo "  progress : off"
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

    run_env=(
        CADD0040_SA_SECONDS="${SA_SECONDS}"
        CADD0040_CHECKPOINT_STEPS="${CHECKPOINT_STEPS}"
        CADD0040_REPORT_METRICS="${REPORT_METRICS}"
    )
    if [[ "${DEBUG_PROGRESS}" == "1" ]]; then
        run_env+=(CADD0040_DEBUG_PROGRESS=1 "CADD0040_DEBUG_PROGRESS_INTERVAL=${DEBUG_PROGRESS_INTERVAL}")
        echo ">>> ${testcase_name}" >&2
    fi

    set +e
    env "${run_env[@]}" "${BINARY}" \
        --optimizer "${OPTIMIZER}" \
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
