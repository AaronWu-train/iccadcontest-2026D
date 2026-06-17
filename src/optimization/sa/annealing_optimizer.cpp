/**
 * @file annealing_optimizer.cpp
 * @brief Simulated-annealing optimizer implementation.
 */

#include "optimization/sa/annealing_optimizer.hpp"

#include <chrono>
#include <limits>

#include "optimization/optimizer_config.hpp"
#include "optimization/sa/sa_search.hpp"
#include "optimization/timing_state.hpp"

namespace cadd0040 {

AnnealingOptimizer::AnnealingOptimizer(CandidatePolicy proposal_policy,
                                       std::string_view config_section,
                                       std::string_view legacy_config_section)
    : proposal_policy_(proposal_policy),
      config_section_(config_section),
      legacy_config_section_(legacy_config_section) {}

void AnnealingOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                             const BufferLibrary& buffer_library, const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;
    DebugProgress& debug = context.debug_progress;
    const SaConfig config =
        sa_config_from_sources(context.optimizer_config, config_section_, legacy_config_section_);
    sa::set_rng_seed(config.seed);

    TimingState timing(clock_tree, data_path_graph, buffer_library);

    debug.log([&](std::ostream& os) {
        os << "AnnealingOptimizer: baseline score = " << timing.score(baseline_metrics) << '\n';
    });

    sa::SearchState best_state{clock_tree, timing.snapshot(), timing.metrics(),
                               timing.score(baseline_metrics)};

    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + config.time_budget;

    std::size_t greedy_steps = 0;
    std::size_t checkpoint_steps = 0;
    std::size_t accepted_moves = 0;
    std::size_t rejected_moves = 0;
    greedy_steps +=
        sa::run_greedy_batch(clock_tree, timing, buffer_library, baseline_metrics, best_state,
                             config.greedy_warmup_iterations, config.violation_sample_limit,
                             config.removal_candidate_limit, deadline, start_time, "warmup", -1,
                             accepted_moves, rejected_moves, context, checkpoint_steps);
    double current_score = timing.score(baseline_metrics);

    debug.log([&](std::ostream& os) {
        os << "AnnealingOptimizer: after warmup score = " << current_score << '\n';
    });

    std::size_t restarts = 0;

    const std::size_t iterations = sa::run_sa_phase(
        clock_tree, timing, buffer_library, baseline_metrics, debug, current_score, best_state,
        start_time, deadline, config.time_budget, config.initial_temperature,
        config.min_temperature, config.cooling_factor, config.restart_stale_iterations,
        config.restart_score_gap, config.greedy_polish_interval, config.violation_sample_limit,
        config.removal_candidate_limit, greedy_steps, accepted_moves, rejected_moves, restarts,
        context, checkpoint_steps, "sa_phase", -1, AcceptPolicy::Metropolis, proposal_policy_);

    sa::restore_best(clock_tree, timing, current_score, best_state);
    greedy_steps +=
        sa::run_greedy_batch(clock_tree, timing, buffer_library, baseline_metrics, best_state,
                             config.final_greedy_polish_iterations, config.violation_sample_limit,
                             config.removal_candidate_limit, deadline, start_time, "final_polish",
                             -1, accepted_moves, rejected_moves, context, checkpoint_steps);
    sa::restore_best(clock_tree, timing, current_score, best_state);
    context.write_checkpoint(best_state.tree);
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
    const OptimizerProgressEvent final_event{checkpoint_steps,
                                             elapsed,
                                             "final",
                                             -1,
                                             "final",
                                             current_score,
                                             best_state.score,
                                             std::numeric_limits<double>::quiet_NaN(),
                                             timing.metrics(),
                                             accepted_moves,
                                             rejected_moves,
                                             candidate_policy_name(proposal_policy_),
                                             accept_policy_name(AcceptPolicy::Metropolis)};
    context.maybe_record_progress(final_event, true);
    context.maybe_record_visual(best_state.tree, final_event, true);

    debug.log([&](std::ostream& os) {
        os << "AnnealingOptimizer: iterations = " << iterations << ", accepted = " << accepted_moves
           << ", rejected = " << rejected_moves << ", restarts = " << restarts
           << ", greedy_polish = " << greedy_steps << ", best score = " << best_state.score << '\n';
    });
}

}  // namespace cadd0040
