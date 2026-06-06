#!/usr/bin/env bash
# Submit Slurm array jobs: every canonical optimizer × every testcase.
# Each job writes its own log; the launcher waits and prints an aggregate summary.
#
# Usage:
#   ./scripts/slurm_run_all_optimizers.sh
#   OUTPUT_DIR=/path/to/run ./scripts/slurm_run_all_optimizers.sh
#   CADD0040_SA_SECONDS=60 ./scripts/slurm_run_all_optimizers.sh
#   ./scripts/slurm_run_all_optimizers.sh --local          # no Slurm, run sequentially
#   ./scripts/slurm_run_all_optimizers.sh --aggregate-only # re-summarize existing OUTPUT_DIR
#
# Output layout (after completion):
#   logs/<optimizer>/<testcase>.log
#   outputs/<optimizer>/<testcase>/modified_clk_tree.structure
#   summary.txt
#   slurm-<jobid>_<taskid>.{out,err}   (Slurm only)
#
# Environment:
#   BUILD_DIR                        CMake build directory (default: build-release)
#   TESTCASES_DIR                    Testcase root (default: testcases/)
#   OUTPUT_DIR                       Run directory (default: slurm_runs/<timestamp>)
#   OPTIMIZERS                       Space-separated list (default: dummy greedy milp anneal isa)
#   CADD0040_SA_SECONDS              SA time budget (default: 540)
#   CADD0040_DEBUG_PROGRESS          1 to enable debug progress (default: 0)
#   CADD0040_DEBUG_PROGRESS_INTERVAL Progress interval seconds (default: 30)
#   SLURM_PARTITION / SLURM_ACCOUNT  Optional Slurm account settings
#   SLURM_TIME                       Job time limit (default: 02:00:00)
#   SLURM_MEM                        Memory per task (default: 4G)
#   SLURM_CPUS                       CPUs per task (default: 1)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build-release}"
BINARY="${BUILD_DIR}/cadd0040"
TESTCASES_DIR="${TESTCASES_DIR:-${ROOT}/testcases}"
SA_SECONDS="${CADD0040_SA_SECONDS:-540}"
DEBUG_PROGRESS="${CADD0040_DEBUG_PROGRESS:-0}"
DEBUG_PROGRESS_INTERVAL="${CADD0040_DEBUG_PROGRESS_INTERVAL:-30}"
SLURM_TIME="${SLURM_TIME:-02:00:00}"
SLURM_MEM="${SLURM_MEM:-4G}"
SLURM_CPUS="${SLURM_CPUS:-1}"

# Canonical optimizer names (skip registry aliases to avoid duplicate runs).
OPTIMIZERS="${OPTIMIZERS:-dummy greedy milp anneal isa}"

RUN_MODE="slurm"
if [[ "${1:-}" == "--local" ]]; then
    RUN_MODE="local"
    shift
elif [[ "${1:-}" == "--aggregate-only" ]]; then
    RUN_MODE="aggregate-only"
    shift
elif [[ "${1:-}" == "--worker" ]]; then
    RUN_MODE="worker"
    shift
fi

timestamp="$(date +%Y%m%d_%H%M%S)"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT}/slurm_runs/${timestamp}}"
LOG_DIR="${OUTPUT_DIR}/logs"
OUTPUTS_DIR="${OUTPUT_DIR}/outputs"
META_DIR="${OUTPUT_DIR}/meta"
MANIFEST="${OUTPUT_DIR}/.manifest"
RUN_ENV="${OUTPUT_DIR}/.run.env"
SUMMARY_TXT="${OUTPUT_DIR}/summary.txt"

read -r -a OPTIMIZER_LIST <<< "${OPTIMIZERS}"

discover_testcases() {
    local -n _out=$1
    _out=()
    while IFS= read -r testcase; do
        _out+=("${testcase}")
    done < <(find "${TESTCASES_DIR}" -mindepth 1 -maxdepth 1 -type d -name 'testcase*' | sort)
}

write_manifest() {
    discover_testcases TESTCASES
    if [[ ${#TESTCASES[@]} -eq 0 ]]; then
        echo "No testcase* directories under ${TESTCASES_DIR}" >&2
        exit 1
    fi

    mkdir -p "${OUTPUT_DIR}" "${LOG_DIR}" "${OUTPUTS_DIR}" "${META_DIR}"
    : > "${MANIFEST}"

    local optimizer testcase_path testcase_name
    for optimizer in "${OPTIMIZER_LIST[@]}"; do
        for testcase_path in "${TESTCASES[@]}"; do
            testcase_name="$(basename "${testcase_path}")"
            printf '%s\t%s\n' "${optimizer}" "${testcase_path}" >> "${MANIFEST}"
        done
    done

    cat > "${RUN_ENV}" <<EOF
ROOT=${ROOT}
BUILD_DIR=${BUILD_DIR}
BINARY=${BINARY}
TESTCASES_DIR=${TESTCASES_DIR}
OUTPUT_DIR=${OUTPUT_DIR}
SA_SECONDS=${SA_SECONDS}
DEBUG_PROGRESS=${DEBUG_PROGRESS}
DEBUG_PROGRESS_INTERVAL=${DEBUG_PROGRESS_INTERVAL}
OPTIMIZERS=${OPTIMIZERS}
EOF
}

run_one_job() {
    local optimizer="$1"
    local testcase_path="$2"
    local testcase_name
    testcase_name="$(basename "${testcase_path}")"

    local log_file="${LOG_DIR}/${optimizer}/${testcase_name}.log"
    local meta_file="${META_DIR}/${optimizer}__${testcase_name}.tsv"
    local output_dir="${OUTPUTS_DIR}/${optimizer}/${testcase_name}"
    local output_file="${output_dir}/modified_clk_tree.structure"

    mkdir -p "${LOG_DIR}/${optimizer}" "${output_dir}"

    for required in clk_tree.structure buf.lib SS_delay.rpt FF_delay.rpt; do
        if [[ ! -f "${testcase_path}/${required}" ]]; then
            {
                echo "SKIP ${testcase_name}: missing ${required}"
            } > "${log_file}"
            printf '%s\t%s\t-\t-\t0\t-\tSKIP(missing %s)\n' \
                "${optimizer}" "${testcase_name}" "${required}" > "${meta_file}"
            return 0
        fi
    done

    local start_ns end_ns elapsed exit_code
    start_ns="$(date +%s)"

    local -a run_env=(
        "CADD0040_SA_SECONDS=${SA_SECONDS}"
    )
    if [[ "${DEBUG_PROGRESS}" == "1" ]]; then
        run_env+=(
            "CADD0040_DEBUG_PROGRESS=1"
            "CADD0040_DEBUG_PROGRESS_INTERVAL=${DEBUG_PROGRESS_INTERVAL}"
        )
    fi

    set +e
    env "${run_env[@]}" "${BINARY}" \
        --optimizer "${optimizer}" \
        "${testcase_path}" \
        "${output_file}" \
        > "${log_file}" 2>&1
    exit_code=$?
    set -e

    end_ns="$(date +%s)"
    elapsed="$((end_ns - start_ns))"

    local initial_score final_score status
    initial_score="$(grep -E '^Initial Score = ' "${log_file}" | tail -1 | awk '{print $NF}' || true)"
    final_score="$(grep -E '^Final Score = ' "${log_file}" | tail -1 | awk '{print $NF}' || true)"
    initial_score="${initial_score:-"-"}"
    final_score="${final_score:-"-"}"

    if [[ ${exit_code} -eq 0 && -f "${output_file}" ]]; then
        status="OK"
    else
        status="FAIL(exit ${exit_code})"
    fi

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${optimizer}" "${testcase_name}" "${initial_score}" "${final_score}" \
        "${elapsed}" "${exit_code}" "${status}" > "${meta_file}"
}

cleanup_run_state() {
    rm -f "${MANIFEST}" "${RUN_ENV}"
    rm -rf "${META_DIR}"
}

worker_main() {
    # shellcheck disable=SC1091
    source "${RUN_ENV}"

    if [[ ! -x "${BINARY}" ]]; then
        echo "Binary not found: ${BINARY}" >&2
        exit 1
    fi

    local task_id="${SLURM_ARRAY_TASK_ID:-}"
    if [[ -z "${task_id}" ]]; then
        echo "SLURM_ARRAY_TASK_ID is not set" >&2
        exit 1
    fi

    local line="$((task_id + 1))"
    local optimizer testcase_path
    optimizer="$(awk -F '\t' -v line="${line}" 'NR == line { print $1; exit }' "${MANIFEST}")"
    testcase_path="$(awk -F '\t' -v line="${line}" 'NR == line { print $2; exit }' "${MANIFEST}")"

    if [[ -z "${optimizer}" || -z "${testcase_path}" ]]; then
        echo "No manifest entry for array task ${task_id} (line ${line})" >&2
        exit 1
    fi

    run_one_job "${optimizer}" "${testcase_path}"
}

aggregate_results() {
    if [[ ! -d "${META_DIR}" ]]; then
        echo "Meta directory not found: ${META_DIR}" >&2
        exit 1
    fi

    local results_tsv
    results_tsv="$(mktemp)"

    {
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "OPTIMIZER" "TESTCASE" "INITIAL" "FINAL" "TIME(s)" "EXIT" "STATUS"
        cat "${META_DIR}"/*.tsv 2>/dev/null | sort -t $'\t' -k1,1 -k2,2 || true
    } > "${results_tsv}"

    local total_ok total_fail
    total_ok="$(awk -F '\t' 'NR > 1 && $7 == "OK" { c++ } END { print c+0 }' "${results_tsv}")"
    total_fail="$(awk -F '\t' 'NR > 1 && $7 != "OK" { c++ } END { print c+0 }' "${results_tsv}")"

    {
        echo "cadd0040 optimizer × testcase matrix"
        echo "  output dir : ${OUTPUT_DIR}"
        echo "  binary     : ${BINARY}"
        echo "  SA budget  : ${SA_SECONDS}s"
        echo "  optimizers : ${OPTIMIZERS}"
        echo
        echo "=== Full results ==="
        column -t -s $'\t' "${results_tsv}" 2>/dev/null || cat "${results_tsv}"
        echo
        echo "=== Per optimizer ==="
        awk -F '\t' '
            NR == 1 { next }
            {
                opt = $1
                total[opt]++
                time[opt] += ($5 ~ /^[0-9]+$/) ? $5 : 0
                if ($7 == "OK") ok[opt]++
                else fail[opt]++
                if ($4 != "-" && $4 ~ /^-?[0-9]+(\.[0-9]+)?$/) {
                    sum_final[opt] += $4
                    count_final[opt]++
                }
            }
            END {
                printf "%-10s %8s %8s %12s %12s\n", "OPTIMIZER", "OK", "FAIL", "AVG_FINAL", "TOTAL_TIME"
                printf "%-10s %8s %8s %12s %12s\n", "---------", "--", "----", "---------", "----------"
                for (opt in total) {
                    avg = (count_final[opt] > 0) ? sum_final[opt] / count_final[opt] : 0
                    printf "%-10s %8d %8d %12.6f %12d\n", opt, ok[opt]+0, fail[opt]+0, avg, time[opt]+0
                }
            }
        ' "${results_tsv}"
        echo
        echo "=== Best final score per testcase ==="
        printf '%-12s %10s %s\n' "TESTCASE" "BEST" "OPTIMIZER"
        printf '%-12s %10s %s\n' "--------" "----" "---------"
        awk -F '\t' '
            NR == 1 { next }
            $7 == "OK" && $4 != "-" && $4 ~ /^-?[0-9]+(\.[0-9]+)?$/ {
                tc = $2
                if (!(tc in best_score) || $4 < best_score[tc]) {
                    best_score[tc] = $4
                    best_opt[tc] = $1
                }
            }
            END {
                for (tc in best_score) {
                    printf "%-12s\t%10s\t%s\n", tc, best_score[tc], best_opt[tc]
                }
            }
        ' "${results_tsv}" | sort -t $'\t' -k1,1 | awk -F '\t' '{ printf "%-12s %10s %s\n", $1, $2, $3 }'
        echo
        echo "Overall: ${total_ok} passed, ${total_fail} failed"
        echo "Logs   : ${LOG_DIR}/<optimizer>/<testcase>.log"
        echo "Outputs: ${OUTPUTS_DIR}/<optimizer>/<testcase>/modified_clk_tree.structure"
    } | tee "${SUMMARY_TXT}"

    rm -f "${results_tsv}"

    if [[ "${total_fail}" -gt 0 ]]; then
        return 1
    fi
    return 0
}

submit_slurm_jobs() {
    if ! command -v sbatch >/dev/null 2>&1; then
        echo "sbatch not found; use --local or install Slurm client tools." >&2
        exit 1
    fi

    if [[ ! -x "${BINARY}" ]]; then
        echo "Binary not found: ${BINARY}" >&2
        echo "Build first: make release" >&2
        exit 1
    fi

    write_manifest
    local num_tasks
    num_tasks="$(wc -l < "${MANIFEST}" | tr -d ' ')"
    local last_index="$((num_tasks - 1))"

    echo "========================================"
    echo "Submitting Slurm array: ${num_tasks} jobs"
    echo "  output dir : ${OUTPUT_DIR}"
    echo "  binary     : ${BINARY}"
    echo "  optimizers : ${OPTIMIZERS}"
    echo "  SA budget  : ${SA_SECONDS}s"
    echo "========================================"

    local -a sbatch_args=(
        "--job-name=cadd0040-matrix"
        "--array=0-${last_index}"
        "--time=${SLURM_TIME}"
        "--cpus-per-task=${SLURM_CPUS}"
        "--mem=${SLURM_MEM}"
        "--output=${OUTPUT_DIR}/slurm-%A_%a.out"
        "--error=${OUTPUT_DIR}/slurm-%A_%a.err"
        "--export=ALL,OUTPUT_DIR=${OUTPUT_DIR}"
    )

    if [[ -n "${SLURM_PARTITION:-}" ]]; then
        sbatch_args+=("--partition=${SLURM_PARTITION}")
    fi
    if [[ -n "${SLURM_ACCOUNT:-}" ]]; then
        sbatch_args+=("--account=${SLURM_ACCOUNT}")
    fi

    local job_id
    job_id="$(sbatch --parsable "${sbatch_args[@]}" "${BASH_SOURCE[0]}" --worker)"
    echo "Submitted job array: ${job_id}"
    echo "Waiting for completion..."

    while squeue -h -j "${job_id}" 2>/dev/null | grep -q .; do
        sleep 10
    done

    echo
    echo "All array tasks finished. Aggregating..."
    echo
    aggregate_results
    cleanup_run_state
}

run_local() {
    if [[ ! -x "${BINARY}" ]]; then
        echo "Binary not found: ${BINARY}" >&2
        echo "Build first: make release" >&2
        exit 1
    fi

    write_manifest
    local optimizer testcase_path
    while IFS=$'\t' read -r optimizer testcase_path; do
        echo ">>> ${optimizer} / $(basename "${testcase_path}")"
        run_one_job "${optimizer}" "${testcase_path}"
    done < "${MANIFEST}"

    echo
    aggregate_results
    cleanup_run_state
}

case "${RUN_MODE}" in
    worker)
        worker_main
        ;;
    local)
        run_local
        ;;
    aggregate-only)
        if [[ -z "${OUTPUT_DIR}" || ! -d "${META_DIR}" ]]; then
            echo "Set OUTPUT_DIR to an existing run directory with meta/ for --aggregate-only" >&2
            exit 1
        fi
        if [[ -f "${RUN_ENV}" ]]; then
            # shellcheck disable=SC1091
            source "${RUN_ENV}"
        fi
        aggregate_results
        cleanup_run_state
        ;;
    slurm)
        submit_slurm_jobs
        ;;
esac
