/**
 * @file two_step_optimizer.cpp
 * @brief A6 TwoStepOptimize optimizer.
 */

#include "optimization/two_step/two_step_optimizer.hpp"

#include <chrono>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "optimization/candidate_policy.hpp"
#include "optimization/optimizer_config.hpp"
#include "optimization/timing_state.hpp"

namespace cadd0040 {
namespace {

struct BestRunState {
    ClockTree tree;
    TimingSnapshot timing;
    Metrics metrics;
    double score = -std::numeric_limits<double>::infinity();
};

struct CandidateChoice {
    bool found = false;
    CandidateMove move;
    double delta = 0.0;
};

bool maybe_update_best(const ClockTree& clock_tree, const TimingState& timing,
                       const Metrics& baseline_metrics, BestRunState& best_state) {
    const double current_score = timing.score(baseline_metrics);
    if (current_score > best_state.score) {
        best_state.tree = clock_tree;
        best_state.timing = timing.snapshot();
        best_state.metrics = timing.metrics();
        best_state.score = current_score;
        return true;
    }
    return false;
}

OptimizerProgressEvent make_event(const std::chrono::steady_clock::time_point& start_time,
                                  std::size_t step, std::string phase, std::string event,
                                  const TimingState& timing, const Metrics& baseline_metrics,
                                  const BestRunState& best_state, std::size_t accepted_moves,
                                  std::size_t rejected_moves, std::string candidate_policy,
                                  std::string accept_policy,
                                  double delta_score = std::numeric_limits<double>::quiet_NaN()) {
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
    return OptimizerProgressEvent{step,
                                  elapsed,
                                  std::move(phase),
                                  -1,
                                  std::move(event),
                                  timing.score(baseline_metrics),
                                  best_state.score,
                                  delta_score,
                                  timing.metrics(),
                                  accepted_moves,
                                  rejected_moves,
                                  std::move(candidate_policy),
                                  std::move(accept_policy)};
}

void record_trace(const OptimizerContext& context, const ClockTree& clock_tree,
                  const OptimizerProgressEvent& event, bool force) {
    context.maybe_record_progress(event, force);
    context.maybe_record_visual(clock_tree, event, force);
}

double slack_objective(const Metrics& metrics) {
    return 0.55 * metrics.tns_ss + 0.20 * metrics.wns_ss + 0.20 * metrics.tns_ff +
           0.05 * metrics.wns_ff;
}

CandidatePolicyConfig candidate_config_from_two_step(const TwoStepConfig& config) {
    CandidatePolicyConfig candidate;
    candidate.violation_sample_limit = config.violation_sample_limit;
    candidate.critical_endpoint_limit = config.critical_endpoint_limit;
    candidate.upstream_window_depth = config.upstream_window_depth;
    candidate.removal_candidate_limit = config.removal_candidate_limit;
    candidate.resize_node_limit = config.resize_node_limit;
    candidate.candidate_limit = config.candidate_limit;
    return candidate;
}

CandidateChoice find_best_candidate_by_objective(
    ClockTree& clock_tree, TimingState& timing, const BufferLibrary& buffer_library,
    const std::vector<CandidateMove>& candidates,
    const std::chrono::steady_clock::time_point& deadline,
    const std::function<double(const TimingState&)>& objective) {
    const double before_value = objective(timing);
    CandidateChoice best;
    for (const auto& candidate : candidates) {
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        const ClockTreeEdit edit = apply_candidate(clock_tree, timing, buffer_library, candidate);
        if (!edit) {
            continue;
        }
        const double delta = objective(timing) - before_value;
        if (delta > 0.0 && (!best.found || delta > best.delta)) {
            best = CandidateChoice{true, candidate, delta};
        }
        undo_candidate(clock_tree, timing, edit);
    }
    return best;
}

bool apply_choice(ClockTree& clock_tree, TimingState& timing, const BufferLibrary& buffer_library,
                  const CandidateChoice& choice) {
    return choice.found &&
           static_cast<bool>(apply_candidate(clock_tree, timing, buffer_library, choice.move));
}

}  // namespace

void TwoStepOptimizeOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                                   const BufferLibrary& buffer_library,
                                   const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;
    DebugProgress& debug = context.debug_progress;
    const TwoStepConfig config = two_step_config_from_sources(context.optimizer_config);
    const CandidatePolicyConfig candidate_config = candidate_config_from_two_step(config);
    const std::string candidate_policy = candidate_policy_name(CandidatePolicy::UnionPool);
    const std::string accept_policy = accept_policy_name(AcceptPolicy::TwoStepSlackThenScore);
    TimingState timing(clock_tree, data_path_graph, buffer_library);
    BestRunState best_state{clock_tree, timing.snapshot(), timing.metrics(),
                            timing.score(baseline_metrics)};
    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + config.time_budget;

    std::size_t accepted_moves = 0;
    std::size_t rejected_moves = 0;
    record_trace(context, clock_tree,
                 make_event(start_time, 0, "baseline", "kept", timing, baseline_metrics, best_state,
                            accepted_moves, rejected_moves, candidate_policy, accept_policy),
                 true);

    for (; accepted_moves < config.timing_steps && std::chrono::steady_clock::now() < deadline;) {
        std::vector<CandidateMove> candidates;
        append_candidate_policy_moves(clock_tree, timing, candidate_config,
                                      CandidatePolicy::UnionPool, candidates);
        dedupe_candidates(candidates, candidate_config.candidate_limit);

        const CandidateChoice choice = find_best_candidate_by_objective(
            clock_tree, timing, buffer_library, candidates, deadline,
            [](const TimingState& state) { return slack_objective(state.metrics()); });
        if (!apply_choice(clock_tree, timing, buffer_library, choice)) {
            break;
        }
        ++accepted_moves;
        const bool best_updated =
            maybe_update_best(clock_tree, timing, baseline_metrics, best_state);
        context.maybe_checkpoint(best_state.tree, accepted_moves);
        record_trace(context, best_state.tree,
                     make_event(start_time, accepted_moves, "slack_step",
                                best_updated ? "best_update" : "accepted", timing, baseline_metrics,
                                best_state, accepted_moves, rejected_moves, candidate_policy,
                                accept_policy, choice.delta),
                     best_updated);
    }

    for (std::size_t score_steps = 0;
         score_steps < config.score_steps && std::chrono::steady_clock::now() < deadline;
         ++score_steps) {
        std::vector<CandidateMove> candidates;
        append_candidate_policy_moves(clock_tree, timing, candidate_config,
                                      CandidatePolicy::UnionPool, candidates);
        dedupe_candidates(candidates, candidate_config.candidate_limit);

        const CandidateChoice choice = find_best_candidate_by_objective(
            clock_tree, timing, buffer_library, candidates, deadline,
            [&](const TimingState& state) { return state.score(baseline_metrics); });
        if (!apply_choice(clock_tree, timing, buffer_library, choice)) {
            break;
        }
        ++accepted_moves;
        const bool best_updated =
            maybe_update_best(clock_tree, timing, baseline_metrics, best_state);
        context.maybe_checkpoint(best_state.tree, accepted_moves);
        record_trace(context, best_state.tree,
                     make_event(start_time, accepted_moves, "score_step",
                                best_updated ? "best_update" : "accepted", timing, baseline_metrics,
                                best_state, accepted_moves, rejected_moves, candidate_policy,
                                accept_policy, choice.delta),
                     best_updated);
    }

    clock_tree = best_state.tree;
    timing.restore(best_state.timing);
    context.write_checkpoint(best_state.tree);
    record_trace(
        context, best_state.tree,
        make_event(start_time, accepted_moves, "final", "final", timing, baseline_metrics,
                   best_state, accepted_moves, rejected_moves, candidate_policy, accept_policy),
        true);
    debug.log([&](std::ostream& os) {
        os << "TwoStepOptimizeOptimizer(" << candidate_policy << ", " << accept_policy
           << "): accepted = " << accepted_moves << ", best score = " << best_state.score << '\n';
    });
}

}  // namespace cadd0040
