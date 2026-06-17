/**
 * @file greedy_optimizer.cpp
 * @brief BestScore greedy optimizer with selectable CandidatePolicy.
 */

#include "optimization/greedy/greedy_optimizer.hpp"

#include <chrono>
#include <limits>
#include <random>
#include <string>
#include <vector>

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

struct GreedyRunConfig {
    std::chrono::seconds time_budget{0};
    std::size_t max_steps = 0;
    std::size_t max_resize_polish_steps = 0;
    std::size_t max_resize_nodes_per_step = 0;
    std::size_t max_polish_phases = 0;
    CandidatePolicyConfig candidate;
    unsigned int seed = kDefaultRngSeed;
};

OptimizerProgressEvent make_event(const std::chrono::steady_clock::time_point& start_time,
                                  std::size_t step, std::string phase, int round, std::string event,
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
                                  round,
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

CandidateChoice find_best_score_candidate(ClockTree& clock_tree, TimingState& timing,
                                          const BufferLibrary& buffer_library,
                                          const Metrics& baseline_metrics,
                                          const std::vector<CandidateMove>& candidates,
                                          const std::chrono::steady_clock::time_point& deadline) {
    const double before_score = timing.score(baseline_metrics);
    CandidateChoice best;
    for (const auto& candidate : candidates) {
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        const ClockTreeEdit edit = apply_candidate(clock_tree, timing, buffer_library, candidate);
        if (!edit) {
            continue;
        }
        const double delta = timing.score(baseline_metrics) - before_score;
        if (delta > 0.0 && (!best.found || delta > best.delta)) {
            best = CandidateChoice{true, candidate, delta};
        }
        undo_candidate(clock_tree, timing, edit);
    }
    return best;
}

bool apply_best_score_move(ClockTree& clock_tree, TimingState& timing,
                           const BufferLibrary& buffer_library, const Metrics& baseline_metrics,
                           std::vector<CandidateMove>& candidates,
                           const std::chrono::steady_clock::time_point& deadline,
                           double& delta_score) {
    dedupe_candidates(candidates);
    const CandidateChoice choice = find_best_score_candidate(
        clock_tree, timing, buffer_library, baseline_metrics, candidates, deadline);
    delta_score = choice.delta;
    return choice.found &&
           static_cast<bool>(apply_candidate(clock_tree, timing, buffer_library, choice.move));
}

bool run_resize_polish_step(ClockTree& clock_tree, TimingState& timing,
                            const BufferLibrary& buffer_library, const Metrics& baseline_metrics,
                            std::size_t node_limit,
                            const std::chrono::steady_clock::time_point& deadline,
                            double& delta_score) {
    std::vector<CandidateMove> candidates;
    append_resize_candidates(clock_tree, timing, node_limit, candidates);
    return apply_best_score_move(clock_tree, timing, buffer_library, baseline_metrics, candidates,
                                 deadline, delta_score);
}

GreedyRunConfig run_config_from_greedy_config(const GreedyConfig& config) {
    CandidatePolicyConfig candidate;
    candidate.random_candidate_limit = config.random_candidate_limit;
    candidate.violation_sample_limit = config.violation_sample_limit;
    candidate.critical_endpoint_limit = config.critical_endpoint_limit;
    candidate.upstream_window_depth = config.upstream_window_depth;
    candidate.removal_candidate_limit = config.removal_candidate_limit;
    candidate.resize_node_limit = config.max_resize_nodes_per_step;
    candidate.candidate_limit = config.candidate_limit;

    return GreedyRunConfig{config.time_budget,
                           config.max_steps,
                           config.max_resize_polish_steps,
                           config.max_resize_nodes_per_step,
                           config.max_polish_phases,
                           candidate,
                           config.seed};
}

std::string config_section_for_policy(CandidatePolicy policy) {
    switch (policy) {
        case CandidatePolicy::RandomActionSpace:
            return "greedy-random";
        case CandidatePolicy::ViolationPath:
            return "greedy-violation-path";
        case CandidatePolicy::UpstreamWindow:
            return "greedy-upstream-window";
        case CandidatePolicy::CriticalEndpoint:
            return "greedy-critical-endpoint";
        case CandidatePolicy::UnionPool:
            return "greedy-union-pool";
        case CandidatePolicy::SampledUnionPool:
            return "greedy-union-pool";
    }
    return "greedy-violation-path";
}

void build_greedy_candidates(const ClockTree& clock_tree, const TimingState& timing,
                             const GreedyRunConfig& config, CandidatePolicy policy,
                             std::mt19937& rng, std::vector<CandidateMove>& candidates) {
    if (policy == CandidatePolicy::RandomActionSpace) {
        for (std::size_t i = 0; i < config.candidate.random_candidate_limit; ++i) {
            candidates.push_back(sample_candidate_policy_move(
                clock_tree, timing, config.candidate, CandidatePolicy::RandomActionSpace, rng));
        }
        return;
    }

    append_candidate_policy_moves(clock_tree, timing, config.candidate, policy, candidates);
    if (policy != CandidatePolicy::UnionPool) {
        append_remove_candidates(clock_tree, config.candidate.removal_candidate_limit, candidates);
    }
}

}  // namespace

GreedyOptimizer::GreedyOptimizer(CandidatePolicy policy) : policy_(policy) {}

void GreedyOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                          const BufferLibrary& buffer_library, const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;
    DebugProgress& debug = context.debug_progress;
    const GreedyRunConfig config = run_config_from_greedy_config(
        greedy_config_from_sources(context.optimizer_config, config_section_for_policy(policy_)));
    const std::string candidate_policy = candidate_policy_name(policy_);
    const std::string accept_policy = accept_policy_name(AcceptPolicy::BestScore);
    std::mt19937 rng(config.seed);
    TimingState timing(clock_tree, data_path_graph, buffer_library);
    BestRunState best_state{clock_tree, timing.snapshot(), timing.metrics(),
                            timing.score(baseline_metrics)};
    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + config.time_budget;

    std::size_t accepted_moves = 0;
    std::size_t rejected_moves = 0;
    std::size_t phases = 0;
    record_trace(
        context, clock_tree,
        make_event(start_time, 0, "baseline", -1, "kept", timing, baseline_metrics, best_state,
                   accepted_moves, rejected_moves, candidate_policy, accept_policy),
        true);

    for (; phases < config.max_polish_phases && std::chrono::steady_clock::now() < deadline;
         ++phases) {
        bool phase_changed = false;
        record_trace(
            context, clock_tree,
            make_event(start_time, accepted_moves, "greedy_best_score", static_cast<int>(phases),
                       "phase_start", timing, baseline_metrics, best_state, accepted_moves,
                       rejected_moves, candidate_policy, accept_policy),
            true);

        while (accepted_moves < config.max_steps && std::chrono::steady_clock::now() < deadline) {
            std::vector<CandidateMove> candidates;
            build_greedy_candidates(clock_tree, timing, config, policy_, rng, candidates);
            double delta = 0.0;
            if (!apply_best_score_move(clock_tree, timing, buffer_library, baseline_metrics,
                                       candidates, deadline, delta)) {
                break;
            }
            phase_changed = true;
            ++accepted_moves;
            const bool best_updated =
                maybe_update_best(clock_tree, timing, baseline_metrics, best_state);
            context.maybe_checkpoint(best_state.tree, accepted_moves);
            record_trace(
                context, best_state.tree,
                make_event(start_time, accepted_moves, "greedy_best_score",
                           static_cast<int>(phases), best_updated ? "best_update" : "accepted",
                           timing, baseline_metrics, best_state, accepted_moves, rejected_moves,
                           candidate_policy, accept_policy, delta),
                best_updated);
        }

        for (std::size_t resize_steps = 0; resize_steps < config.max_resize_polish_steps &&
                                           std::chrono::steady_clock::now() < deadline;
             ++resize_steps) {
            double delta = 0.0;
            if (!run_resize_polish_step(clock_tree, timing, buffer_library, baseline_metrics,
                                        config.max_resize_nodes_per_step, deadline, delta)) {
                break;
            }
            phase_changed = true;
            ++accepted_moves;
            const bool best_updated =
                maybe_update_best(clock_tree, timing, baseline_metrics, best_state);
            context.maybe_checkpoint(best_state.tree, accepted_moves);
            record_trace(
                context, best_state.tree,
                make_event(start_time, accepted_moves, "resize_polish", static_cast<int>(phases),
                           best_updated ? "best_update" : "accepted", timing, baseline_metrics,
                           best_state, accepted_moves, rejected_moves, "resize", accept_policy,
                           delta),
                best_updated);
        }

        record_trace(
            context, best_state.tree,
            make_event(start_time, accepted_moves, "greedy_best_score", static_cast<int>(phases),
                       "phase_end", timing, baseline_metrics, best_state, accepted_moves,
                       rejected_moves, candidate_policy, accept_policy),
            true);
        if (!phase_changed) {
            break;
        }
    }

    clock_tree = best_state.tree;
    timing.restore(best_state.timing);
    context.write_checkpoint(best_state.tree);
    record_trace(
        context, best_state.tree,
        make_event(start_time, accepted_moves, "final", -1, "final", timing, baseline_metrics,
                   best_state, accepted_moves, rejected_moves, candidate_policy, accept_policy),
        true);
    debug.log([&](std::ostream& os) {
        os << "GreedyOptimizer(" << candidate_policy << ", " << accept_policy
           << "): phases = " << phases << ", accepted = " << accepted_moves
           << ", best score = " << best_state.score << '\n';
    });
}

}  // namespace cadd0040
