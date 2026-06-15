/**
 * @file annealing_optimizer.cpp
 * @brief Simulated-annealing optimizer implementation.
 */

#include "optimization/sa/annealing_optimizer.hpp"

#include <chrono>

#include "optimization/optimizer_config.hpp"
#include "optimization/sa/sa_search.hpp"
#include "optimization/timing_state.hpp"

namespace cadd0040 {

void AnnealingOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                             const BufferLibrary& buffer_library, const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;
    DebugProgress& debug = context.debug_progress;
    const SaConfig config = sa_config_from_environment();

    TimingState timing(clock_tree, data_path_graph, buffer_library);

    debug.log([&](std::ostream& os) {
        os << "AnnealingOptimizer: baseline score = " << timing.score(baseline_metrics) << '\n';
    });

    sa::SearchState best_state{clock_tree, timing.snapshot(), timing.metrics(),
                               timing.score(baseline_metrics)};

    std::size_t greedy_steps = 0;
    greedy_steps += sa::run_greedy_batch(clock_tree, timing, buffer_library, baseline_metrics,
                                         best_state, config.greedy_warmup_iterations);
    double current_score = timing.score(baseline_metrics);

    debug.log([&](std::ostream& os) {
        os << "AnnealingOptimizer: after warmup score = " << current_score << '\n';
    });

    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + config.time_budget;

    std::size_t accepted_moves = 0;
    std::size_t rejected_moves = 0;
    std::size_t restarts = 0;

    const std::size_t iterations = sa::run_sa_phase(
        clock_tree, timing, buffer_library, baseline_metrics, debug, current_score, best_state,
        start_time, deadline, config.time_budget, config.initial_temperature,
        config.min_temperature, config.cooling_factor, config.restart_stale_iterations,
        config.restart_score_gap, config.greedy_polish_interval, greedy_steps, accepted_moves,
        rejected_moves, restarts);

    sa::restore_best(clock_tree, timing, current_score, best_state);
    greedy_steps += sa::run_greedy_batch(clock_tree, timing, buffer_library, baseline_metrics,
                                         best_state, config.final_greedy_polish_iterations);
    sa::restore_best(clock_tree, timing, current_score, best_state);

    debug.log([&](std::ostream& os) {
        os << "AnnealingOptimizer: iterations = " << iterations << ", accepted = " << accepted_moves
           << ", rejected = " << rejected_moves << ", restarts = " << restarts
           << ", greedy_polish = " << greedy_steps << ", best score = " << best_state.score << '\n';
    });
}

}  // namespace cadd0040
