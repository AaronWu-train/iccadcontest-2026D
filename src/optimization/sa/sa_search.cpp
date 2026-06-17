/**
 * @file sa_search.cpp
 * @brief Shared search primitives for SA-family optimizers.
 */

#include "optimization/sa/sa_search.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "optimization/candidate_policy.hpp"
#include "optimization/optimizer_config.hpp"

namespace cadd0040 {
namespace sa {
namespace {

CandidatePolicyConfig candidate_config(std::size_t violation_sample_limit,
                                       std::size_t removal_candidate_limit) {
    CandidatePolicyConfig config;
    config.violation_sample_limit = violation_sample_limit;
    config.removal_candidate_limit = removal_candidate_limit;
    return config;
}

bool try_best_candidate(ClockTree& clock_tree, TimingState& timing,
                        const BufferLibrary& buffer_library, const Metrics& baseline_metrics,
                        std::vector<CandidateMove>& candidates,
                        const std::chrono::steady_clock::time_point& deadline) {
    const double before_score = timing.score(baseline_metrics);
    double best_delta = 0.0;
    CandidateMove best_move;

    dedupe_candidates(candidates);
    for (const auto& candidate : candidates) {
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        const ClockTreeEdit edit = apply_candidate(clock_tree, timing, buffer_library, candidate);
        if (!edit) {
            continue;
        }
        const double delta = timing.score(baseline_metrics) - before_score;
        if (delta > best_delta) {
            best_delta = delta;
            best_move = candidate;
        }
        undo_candidate(clock_tree, timing, edit);
    }

    if (best_delta <= 0.0 || std::chrono::steady_clock::now() >= deadline) {
        return false;
    }
    return static_cast<bool>(apply_candidate(clock_tree, timing, buffer_library, best_move));
}

bool apply_one_greedy_step(ClockTree& clock_tree, TimingState& timing,
                           const BufferLibrary& buffer_library, const Metrics& baseline_metrics,
                           std::size_t violation_sample_limit, std::size_t removal_candidate_limit,
                           const std::chrono::steady_clock::time_point& deadline) {
    std::vector<CandidateMove> candidates;
    const CandidatePolicyConfig config =
        candidate_config(violation_sample_limit, removal_candidate_limit);
    append_candidate_policy_moves(clock_tree, timing, config, CandidatePolicy::ViolationPath,
                                  candidates);
    append_remove_candidates(clock_tree, removal_candidate_limit, candidates);
    return try_best_candidate(clock_tree, timing, buffer_library, baseline_metrics, candidates,
                              deadline);
}

OptimizerProgressEvent make_event(const std::chrono::steady_clock::time_point& start_time,
                                  std::size_t step, std::string_view phase, int round,
                                  std::string event, const TimingState& timing,
                                  const Metrics& baseline_metrics, const SearchState& best_state,
                                  std::size_t accepted_moves, std::size_t rejected_moves,
                                  std::string candidate_policy, std::string accept_policy,
                                  double delta_score = std::numeric_limits<double>::quiet_NaN()) {
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
    return OptimizerProgressEvent{step,
                                  elapsed,
                                  std::string(phase),
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

}  // namespace

std::mt19937& rng() {
    static thread_local std::mt19937 engine(kDefaultRngSeed);
    return engine;
}

void set_rng_seed(unsigned int seed) { rng().seed(seed); }

void maybe_update_best(const ClockTree& clock_tree, const TimingState& timing,
                       const Metrics& baseline_metrics, SearchState& best_state) {
    const double current_score = timing.score(baseline_metrics);
    if (current_score > best_state.score) {
        best_state.tree = clock_tree;
        best_state.timing = timing.snapshot();
        best_state.metrics = timing.metrics();
        best_state.score = current_score;
    }
}

void restore_best(ClockTree& clock_tree, TimingState& timing, double& current_score,
                  const SearchState& best_state) {
    clock_tree = best_state.tree;
    timing.restore(best_state.timing);
    current_score = best_state.score;
}

std::size_t run_greedy_batch(ClockTree& clock_tree, TimingState& timing,
                             const BufferLibrary& buffer_library, const Metrics& baseline_metrics,
                             SearchState& best_state, std::size_t max_steps,
                             std::size_t violation_sample_limit,
                             std::size_t removal_candidate_limit,
                             const std::chrono::steady_clock::time_point& deadline,
                             const std::chrono::steady_clock::time_point& start_time,
                             std::string_view phase_name, int round_index,
                             std::size_t& accepted_moves, std::size_t& rejected_moves,
                             const OptimizerContext& context, std::size_t& checkpoint_steps) {
    std::size_t steps = 0;
    const std::string candidate_policy = candidate_policy_name(CandidatePolicy::ViolationPath);
    const std::string accept_policy = accept_policy_name(AcceptPolicy::BestScore);
    record_trace(context, clock_tree,
                 make_event(start_time, checkpoint_steps, phase_name, round_index, "phase_start",
                            timing, baseline_metrics, best_state, accepted_moves, rejected_moves,
                            candidate_policy, accept_policy),
                 true);
    for (; steps < max_steps && std::chrono::steady_clock::now() < deadline; ++steps) {
        const double before_score = timing.score(baseline_metrics);
        if (!apply_one_greedy_step(clock_tree, timing, buffer_library, baseline_metrics,
                                   violation_sample_limit, removal_candidate_limit, deadline)) {
            break;
        }
        const double delta = timing.score(baseline_metrics) - before_score;
        const double before_best = best_state.score;
        maybe_update_best(clock_tree, timing, baseline_metrics, best_state);
        const bool best_updated = best_state.score > before_best;
        ++accepted_moves;
        ++checkpoint_steps;
        context.maybe_checkpoint(best_state.tree, checkpoint_steps);
        record_trace(context, best_state.tree,
                     make_event(start_time, checkpoint_steps, phase_name, round_index,
                                best_updated ? "best_update" : "accepted", timing, baseline_metrics,
                                best_state, accepted_moves, rejected_moves, candidate_policy,
                                accept_policy, delta),
                     best_updated);
    }
    record_trace(context, best_state.tree,
                 make_event(start_time, checkpoint_steps, phase_name, round_index, "phase_end",
                            timing, baseline_metrics, best_state, accepted_moves, rejected_moves,
                            candidate_policy, accept_policy),
                 true);
    return steps;
}

std::size_t run_sa_phase(ClockTree& clock_tree, TimingState& timing,
                         const BufferLibrary& buffer_library, const Metrics& baseline_metrics,
                         DebugProgress& debug, double& current_score, SearchState& best_state,
                         const std::chrono::steady_clock::time_point& start_time,
                         const std::chrono::steady_clock::time_point& phase_deadline,
                         std::chrono::seconds total_budget, double initial_temperature,
                         double min_temperature, double cooling_factor,
                         std::size_t restart_stale_iterations, double restart_score_gap,
                         std::size_t greedy_polish_interval, std::size_t violation_sample_limit,
                         std::size_t removal_candidate_limit, std::size_t& greedy_steps,
                         std::size_t& accepted_moves, std::size_t& rejected_moves,
                         std::size_t& restarts, const OptimizerContext& context,
                         std::size_t& checkpoint_steps, std::string_view phase_name,
                         int round_index, AcceptPolicy accept_policy_kind) {
    std::size_t iteration = 0;
    std::size_t iterations_since_best = 0;
    const CandidatePolicyConfig proposals =
        candidate_config(violation_sample_limit, removal_candidate_limit);
    const std::string candidate_policy = candidate_policy_name(CandidatePolicy::SampledUnionPool);
    const std::string accept_policy = accept_policy_name(accept_policy_kind);

    record_trace(context, clock_tree,
                 make_event(start_time, checkpoint_steps, phase_name, round_index, "phase_start",
                            timing, baseline_metrics, best_state, accepted_moves, rejected_moves,
                            candidate_policy, accept_policy),
                 true);

    while (std::chrono::steady_clock::now() < phase_deadline) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - start_time).count();
        const double budget = static_cast<double>(total_budget.count());
        const double progress = budget <= 0.0 ? 1.0 : std::min(1.0, elapsed / budget);
        const double temperature =
            std::max(min_temperature, initial_temperature * std::pow(cooling_factor, progress));

        if (greedy_polish_interval > 0 && iteration > 0 &&
            iteration % greedy_polish_interval == 0) {
            if (apply_one_greedy_step(clock_tree, timing, buffer_library, baseline_metrics,
                                      violation_sample_limit, removal_candidate_limit,
                                      phase_deadline)) {
                ++greedy_steps;
                current_score = timing.score(baseline_metrics);
                const double before_best = best_state.score;
                maybe_update_best(clock_tree, timing, baseline_metrics, best_state);
                const bool best_updated = best_state.score > before_best;
                iterations_since_best = 0;
                ++accepted_moves;
                ++checkpoint_steps;
                context.maybe_checkpoint(best_state.tree, checkpoint_steps);
                record_trace(
                    context, best_state.tree,
                    make_event(start_time, checkpoint_steps, phase_name, round_index,
                               best_updated ? "best_update" : "greedy_polish", timing,
                               baseline_metrics, best_state, accepted_moves, rejected_moves,
                               candidate_policy_name(CandidatePolicy::ViolationPath),
                               accept_policy_name(AcceptPolicy::BestScore)),
                    best_updated);
            }
        }

        const CandidateMove move = sample_candidate_policy_move(
            clock_tree, timing, proposals, CandidatePolicy::SampledUnionPool, rng());
        const ClockTreeEdit edit = apply_candidate(clock_tree, timing, buffer_library, move);
        if (!edit) {
            ++iteration;
            continue;
        }

        const double new_score = timing.score(baseline_metrics);
        const double delta = new_score - current_score;
        bool accept = delta > 0.0;
        if (!accept && temperature > min_temperature) {
            std::uniform_real_distribution<double> accept_dist(0.0, 1.0);
            accept = accept_dist(rng()) < std::exp(delta / temperature);
        }

        if (accept) {
            current_score = new_score;
            ++accepted_moves;
            if (new_score > best_state.score) {
                maybe_update_best(clock_tree, timing, baseline_metrics, best_state);
                iterations_since_best = 0;
            } else {
                ++iterations_since_best;
            }
        } else {
            undo_candidate(clock_tree, timing, edit);
            ++rejected_moves;
            ++iterations_since_best;
        }

        const bool best_updated = current_score >= best_state.score && accept && delta > 0.0;

        if (iterations_since_best >= restart_stale_iterations ||
            current_score < best_state.score - restart_score_gap) {
            restore_best(clock_tree, timing, current_score, best_state);
            iterations_since_best = 0;
            ++restarts;
            record_trace(context, best_state.tree,
                         make_event(start_time, checkpoint_steps, phase_name, round_index,
                                    "restart", timing, baseline_metrics, best_state, accepted_moves,
                                    rejected_moves, candidate_policy, accept_policy),
                         true);
        }

        debug.report_if_due(elapsed, best_state.metrics, baseline_metrics, current_score);
        ++iteration;
        ++checkpoint_steps;
        context.maybe_checkpoint(best_state.tree, checkpoint_steps);
        record_trace(context, best_state.tree,
                     make_event(start_time, checkpoint_steps, phase_name, round_index,
                                best_updated ? "best_update" : (accept ? "accepted" : "rejected"),
                                timing, baseline_metrics, best_state, accepted_moves,
                                rejected_moves, candidate_policy, accept_policy, delta),
                     best_updated);
    }

    record_trace(context, best_state.tree,
                 make_event(start_time, checkpoint_steps, phase_name, round_index, "phase_end",
                            timing, baseline_metrics, best_state, accepted_moves, rejected_moves,
                            candidate_policy, accept_policy),
                 true);
    return iteration;
}

}  // namespace sa
}  // namespace cadd0040
