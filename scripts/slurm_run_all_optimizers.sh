#!/usr/bin/env bash
# Submit Slurm array jobs: every canonical optimizer × every testcase.
# Submits in the background like normal sbatch usage; aggregate when jobs finish.
#
# Usage:
#   ./scripts/slurm_run_all_optimizers.sh
#   SLURM_PARTITION=short ./scripts/slurm_run_all_optimizers.sh
#   ./scripts/slurm_run_all_optimizers.sh --wait   # block until done, then aggregate
#   ./scripts/slurm_run_all_optimizers.sh --local  # no Slurm, run sequentially
#   OUTPUT_DIR=slurm_runs/20260606_120000 ./scripts/slurm_run_all_optimizers.sh --aggregate-only
#
# Typical Slurm workflow:
#   1. ./scripts/slurm_run_all_optimizers.sh
#   2. squeue -j <job_id>          # check progress (job_id printed on submit)
#   3. OUTPUT_DIR=... ./scripts/slurm_run_all_optimizers.sh --aggregate-only
#
# Output layout (after aggregation):
#   logs/<optimizer>/<testcase>.log
#   outputs/<optimizer>/<testcase>/modified_clk_tree.structure
#   summary.txt
#   slurm-<jobid>_<taskid>.{out,err}   (Slurm only)
#
# Environment:
#   BUILD_DIR                        CMake build directory (default: build-release)
#   TESTCASES_DIR                    Testcase root (default: testcases/)
#   OUTPUT_DIR                       Run directory (default: slurm_runs/<timestamp>)
#   OPTIMIZERS                       Space-separated list (default: greedy milp anneal isa)
#   CADD0040_SA_SECONDS              Optimizer time budget (default: 540)
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
OPTIMIZERS="${OPTIMIZERS:-greedy milp anneal isa}"

RUN_MODE="slurm"
SLURM_WAIT=0
AGGREGATE_FORCE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --local)
            RUN_MODE="local"
            shift
            ;;
        --aggregate-only)
            RUN_MODE="aggregate-only"
            shift
            ;;
        --worker)
            RUN_MODE="worker"
            shift
            ;;
        --wait)
            SLURM_WAIT=1
            shift
            ;;
        --force)
            AGGREGATE_FORCE=1
            shift
            ;;
        -h | --help)
            sed -n '2,35p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *)
            echo "Unknown option: $1 (try --help)" >&2
            exit 1
            ;;
    esac
done

if [[ "${RUN_MODE}" == "aggregate-only" && -z "${OUTPUT_DIR:-}" ]]; then
    echo "OUTPUT_DIR is required for --aggregate-only" >&2
    echo "Example: OUTPUT_DIR=slurm_runs/20260606_120000 $0 --aggregate-only" >&2
    exit 1
fi

timestamp="$(date +%Y%m%d_%H%M%S)"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT}/slurm_runs/${timestamp}}"
OUTPUT_DIR="${OUTPUT_DIR%/}"
LOG_DIR="${OUTPUT_DIR}/logs"
OUTPUTS_DIR="${OUTPUT_DIR}/outputs"
META_DIR="${OUTPUT_DIR}/meta"
MANIFEST="${OUTPUT_DIR}/.manifest"
RUN_ENV="${OUTPUT_DIR}/.run.env"
JOB_ID_FILE="${OUTPUT_DIR}/.job_id"
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
ROOT='${ROOT}'
BUILD_DIR='${BUILD_DIR}'
BINARY='${BINARY}'
TESTCASES_DIR='${TESTCASES_DIR}'
OUTPUT_DIR='${OUTPUT_DIR}'
SA_SECONDS=${SA_SECONDS}
DEBUG_PROGRESS=${DEBUG_PROGRESS}
DEBUG_PROGRESS_INTERVAL=${DEBUG_PROGRESS_INTERVAL}
OPTIMIZERS='${OPTIMIZERS}'
EOF
}

load_run_env() {
    local line key value
    while IFS= read -r line || [[ -n "${line}" ]]; do
        [[ -z "${line}" || "${line}" == \#* ]] && continue
        key="${line%%=*}"
        value="${line#*=}"
        if [[ "${value}" == \'*\' ]]; then
            value="${value:1:-1}"
        elif [[ "${value}" == \"*\" ]]; then
            value="${value:1:-1}"
        fi
        printf -v "${key}" '%s' "${value}"
    done < "${RUN_ENV}"
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

    mkdir -p "${LOG_DIR}/${optimizer}" "${META_DIR}" "${output_dir}"

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
        "CADD0040_REPORT_METRICS=1"
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
    rm -f "${MANIFEST}" "${RUN_ENV}" "${JOB_ID_FILE}"
    rm -rf "${META_DIR}"
}

slurm_job_running() {
    local job_id="$1"
    squeue -h -j "${job_id}" 2>/dev/null | grep -q .
}

wait_for_slurm_job() {
    local job_id="$1"
    echo "Waiting for job array ${job_id}..."
    while slurm_job_running "${job_id}"; do
        sleep 10
    done
}

print_submit_instructions() {
    local job_id="$1"
    local num_tasks="$2"

    echo
    echo "Submitted ${num_tasks} array tasks as job ${job_id}."
    echo "Launcher exiting; jobs run on compute nodes."
    echo
    echo "  output dir : ${OUTPUT_DIR}"
    echo "  job id     : ${job_id}"
    echo
    echo "Check status:"
    echo "  squeue -j ${job_id}"
    echo "  sacct -j ${job_id} --format=JobID,State,Elapsed,ExitCode"
    echo
    echo "When all tasks finish, aggregate:"
    echo "  OUTPUT_DIR=${OUTPUT_DIR} $0 --aggregate-only"
    echo
}

ensure_jobs_finished() {
    if [[ ! -f "${JOB_ID_FILE}" ]]; then
        return 0
    fi

    local job_id
    job_id="$(cat "${JOB_ID_FILE}")"
    if slurm_job_running "${job_id}"; then
        if [[ "${AGGREGATE_FORCE}" == "1" ]]; then
            echo "Warning: job ${job_id} still running; aggregating partial results (--force)." >&2
            return 0
        fi
        echo "Job ${job_id} is still running." >&2
        echo "Wait for completion, or pass --force for a partial summary." >&2
        echo "  squeue -j ${job_id}" >&2
        exit 1
    fi
}

worker_main() {
    load_run_env

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

count_meta_files() {
    find "${META_DIR}" -maxdepth 1 -type f -name '*.tsv' 2>/dev/null | wc -l | tr -d ' '
}

count_log_files() {
    find "${LOG_DIR}" -mindepth 2 -maxdepth 2 -type f -name '*.log' 2>/dev/null | wc -l | tr -d ' '
}

collect_results_from_meta() {
    find "${META_DIR}" -maxdepth 1 -type f -name '*.tsv' -print0 2>/dev/null |
        sort -z |
        xargs -0 cat 2>/dev/null |
        sort -t $'\t' -k1,1 -k2,2 || true
}

collect_results_from_logs() {
    local log_file optimizer testcase_name output_file
    local initial_score final_score status exit_code elapsed

    while IFS= read -r log_file; do
        [[ -z "${log_file}" ]] && continue
        optimizer="$(basename "$(dirname "${log_file}")")"
        testcase_name="$(basename "${log_file}" .log)"
        output_file="${OUTPUTS_DIR}/${optimizer}/${testcase_name}/modified_clk_tree.structure"

        initial_score="$(grep -E '^Initial Score = ' "${log_file}" | tail -1 | awk '{print $NF}' || true)"
        final_score="$(grep -E '^Final Score = ' "${log_file}" | tail -1 | awk '{print $NF}' || true)"
        initial_score="${initial_score:-"-"}"
        final_score="${final_score:-"-"}"
        elapsed="-"
        exit_code="-"

        if [[ -f "${output_file}" ]]; then
            status="OK"
            exit_code="0"
        elif grep -qE '^SKIP ' "${log_file}"; then
            status="SKIP"
        else
            status="FAIL"
            exit_code="1"
        fi

        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "${optimizer}" "${testcase_name}" "${initial_score}" "${final_score}" \
            "${elapsed}" "${exit_code}" "${status}"
    done < <(find "${LOG_DIR}" -mindepth 2 -maxdepth 2 -type f -name '*.log' 2>/dev/null | sort)
}

print_empty_run_diagnostics() {
    echo "No results found under ${OUTPUT_DIR}." >&2
    echo "Checked meta/*.tsv and logs/*/*.log." >&2
    if [[ -f "${JOB_ID_FILE}" ]]; then
        echo "  job id     : $(cat "${JOB_ID_FILE}")" >&2
        echo "  job status : squeue -j $(cat "${JOB_ID_FILE}")" >&2
    fi
    local err_count
    err_count="$(find "${OUTPUT_DIR}" -maxdepth 1 -type f -name 'slurm-*.err' 2>/dev/null | wc -l | tr -d ' ')"
    if [[ "${err_count}" -gt 0 ]]; then
        echo "  slurm errs : ${OUTPUT_DIR}/slurm-*.err (${err_count} files)" >&2
        echo "  hint       : grep -h . ${OUTPUT_DIR}/slurm-*.err | head" >&2
    fi
}

aggregate_results() {
    local meta_count=0
    local log_count=0
    local data_source="meta"

    if [[ -d "${META_DIR}" ]]; then
        meta_count="$(count_meta_files)"
    fi
    if [[ -d "${LOG_DIR}" ]]; then
        log_count="$(count_log_files)"
    fi

    if [[ "${meta_count}" -eq 0 && "${log_count}" -eq 0 ]]; then
        print_empty_run_diagnostics
        exit 1
    fi

    if [[ "${meta_count}" -eq 0 ]]; then
        data_source="logs"
        echo "Note: meta/ is empty; aggregating from logs/ instead." >&2
    fi

    local results_tsv
    results_tsv="$(mktemp)"

    {
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "OPTIMIZER" "TESTCASE" "INITIAL" "FINAL" "TIME(s)" "EXIT" "STATUS"
        if [[ "${data_source}" == "meta" ]]; then
            collect_results_from_meta
        else
            collect_results_from_logs
        fi
    } > "${results_tsv}"

    local row_count
    row_count="$(awk 'END { print NR }' "${results_tsv}")"
    if [[ "${row_count}" -le 1 ]]; then
        print_empty_run_diagnostics
        rm -f "${results_tsv}"
        exit 1
    fi

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
        echo "=== Best final score per testcase (higher is better) ==="
        printf '%-12s %10s %s\n' "TESTCASE" "BEST" "OPTIMIZER"
        printf '%-12s %10s %s\n' "--------" "----" "---------"
        awk -F '\t' '
            NR == 1 { next }
            $7 == "OK" && $4 != "-" && $4 ~ /^-?[0-9]+(\.[0-9]+)?$/ {
                tc = $2
                if (!(tc in best_score) || $4 > best_score[tc]) {
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
    echo "${job_id}" > "${JOB_ID_FILE}"
    echo "Submitted job array: ${job_id}"

    if [[ "${SLURM_WAIT}" == "1" ]]; then
        wait_for_slurm_job "${job_id}"
        echo
        echo "All array tasks finished. Aggregating..."
        echo
        aggregate_results
        cleanup_run_state
        return
    fi

    print_submit_instructions "${job_id}" "${num_tasks}"
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
        if [[ ! -d "${OUTPUT_DIR}" ]]; then
            echo "Output directory not found: ${OUTPUT_DIR}" >&2
            exit 1
        fi
        if [[ -f "${RUN_ENV}" ]]; then
            load_run_env
        fi
        ensure_jobs_finished
        aggregate_results
        cleanup_run_state
        ;;
    slurm)
        submit_slurm_jobs
        ;;
esac
