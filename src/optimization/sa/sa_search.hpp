/**
 * @file sa_search.hpp
 * @brief Shared search primitives for SA-family optimizers.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <random>
#include <string_view>

#include "clock_tree.hpp"
#include "debug_progress.hpp"
#include "evaluation.hpp"
#include "optimization/optimizer.hpp"
#include "optimization/timing_state.hpp"
#include "types.hpp"

namespace cadd0040 {
namespace sa {

struct SearchState {
    ClockTree tree;
    TimingSnapshot timing;
    Metrics metrics;
    double score = 0.0;
};

std::mt19937& rng();

void set_rng_seed(unsigned int seed);

void maybe_update_best(const ClockTree& clock_tree, const TimingState& timing,
                       const Metrics& baseline_metrics, SearchState& best_state);

void restore_best(ClockTree& clock_tree, TimingState& timing, double& current_score,
                  const SearchState& best_state);

std::size_t run_greedy_batch(ClockTree& clock_tree, TimingState& timing,
                             const BufferLibrary& buffer_library, const Metrics& baseline_metrics,
                             SearchState& best_state, std::size_t max_steps,
                             std::size_t violation_sample_limit,
                             std::size_t removal_candidate_limit,
                             const std::chrono::steady_clock::time_point& deadline,
                             const std::chrono::steady_clock::time_point& start_time,
                             std::string_view phase_name, int round_index,
                             std::size_t& accepted_moves, std::size_t& rejected_moves,
                             const OptimizerContext& context, std::size_t& checkpoint_steps);

std::size_t run_sa_phase(
    ClockTree& clock_tree, TimingState& timing, const BufferLibrary& buffer_library,
    const Metrics& baseline_metrics, DebugProgress& debug, double& current_score,
    SearchState& best_state, const std::chrono::steady_clock::time_point& start_time,
    const std::chrono::steady_clock::time_point& phase_deadline, std::chrono::seconds total_budget,
    double initial_temperature, double min_temperature, double cooling_factor,
    std::size_t restart_stale_iterations, double restart_score_gap,
    std::size_t greedy_polish_interval, std::size_t violation_sample_limit,
    std::size_t removal_candidate_limit, std::size_t& greedy_steps, std::size_t& accepted_moves,
    std::size_t& rejected_moves, std::size_t& restarts, const OptimizerContext& context,
    std::size_t& checkpoint_steps, std::string_view phase_name, int round_index);

}  // namespace sa
}  // namespace cadd0040
