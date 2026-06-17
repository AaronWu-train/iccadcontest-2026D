/**
 * @file iterated_sa_optimizer.cpp
 * @brief Multi-round SA + Greedy optimizer implementation.
 */

#include "optimization/sa/iterated_sa_optimizer.hpp"

#include <chrono>
#include <cmath>
#include <limits>

#include "optimization/optimizer_config.hpp"
#include "optimization/sa/sa_search.hpp"
#include "optimization/timing_state.hpp"

namespace cadd0040 {

IteratedSaOptimizer::IteratedSaOptimizer(CandidatePolicy proposal_policy,
                                         std::string_view config_section,
                                         std::string_view legacy_config_section)
    : proposal_policy_(proposal_policy),
      config_section_(config_section),
      legacy_config_section_(legacy_config_section) {}

void IteratedSaOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                              const BufferLibrary& buffer_library,
                              const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;
    DebugProgress& debug = context.debug_progress;
    const IsaConfig config =
        isa_config_from_sources(context.optimizer_config, config_section_, legacy_config_section_);
    sa::set_rng_seed(config.seed);

    TimingState timing(clock_tree, data_path_graph, buffer_library);

    debug.log([&](std::ostream& os) {
        os << "IteratedSaOptimizer: baseline score = " << timing.score(baseline_metrics) << '\n';
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
        os << "IteratedSaOptimizer: after warmup score = " << current_score << '\n';
        os << "IteratedSaOptimizer: mode -> multi_round (" << config.rounds << " rounds)\n";
    });

    std::size_t total_iterations = 0;
    std::size_t restarts = 0;

    for (std::size_t round = 0; round < config.rounds; ++round) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            break;
        }

        const auto remaining = deadline - now;
        const auto round_budget =
            remaining / static_cast<std::chrono::seconds::rep>(config.rounds - round);
        const auto round_deadline = now + round_budget;
        const double elapsed = std::chrono::duration<double>(now - start_time).count();
        const double progress =
            std::min(1.0, elapsed / static_cast<double>(config.time_budget.count()));
        const double temperature =
            std::max(config.min_temperature,
                     config.initial_temperature * std::pow(config.cooling_factor, progress));

        debug.log([&](std::ostream& os) {
            os << "IteratedSaOptimizer: round " << (round + 1) << "/" << config.rounds
               << " mode -> SA"
               << " (budget=" << std::chrono::duration<double>(round_budget).count() << "s"
               << ", temperature=" << temperature << ", best_score=" << best_state.score << ")\n";
        });

        std::size_t phase_greedy_steps = 0;
        const std::size_t round_iterations = sa::run_sa_phase(
            clock_tree, timing, buffer_library, baseline_metrics, debug, current_score, best_state,
            start_time, round_deadline, config.time_budget, config.initial_temperature,
            config.min_temperature, config.cooling_factor, config.restart_stale_iterations,
            config.restart_score_gap, 0, config.violation_sample_limit,
            config.removal_candidate_limit, phase_greedy_steps, accepted_moves, rejected_moves,
            restarts, context, checkpoint_steps, "round_sa", static_cast<int>(round + 1),
            AcceptPolicy::IteratedMetropolis, proposal_policy_);
        total_iterations += round_iterations;

        debug.log([&](std::ostream& os) {
            os << "IteratedSaOptimizer: round " << (round + 1) << " SA done"
               << ", iterations=" << round_iterations << ", best_score=" << best_state.score
               << ", current_score=" << current_score << '\n';
        });

        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }

        debug.log([&](std::ostream& os) {
            os << "IteratedSaOptimizer: round " << (round + 1) << " mode -> greedy_batch"
               << " (max_steps=" << config.greedy_round_iterations
               << ", best_score=" << best_state.score << ")\n";
        });
        sa::restore_best(clock_tree, timing, current_score, best_state);
        greedy_steps += sa::run_greedy_batch(
            clock_tree, timing, buffer_library, baseline_metrics, best_state,
            config.greedy_round_iterations, config.violation_sample_limit,
            config.removal_candidate_limit, deadline, start_time, "round_greedy",
            static_cast<int>(round + 1), accepted_moves, rejected_moves, context, checkpoint_steps);
        current_score = timing.score(baseline_metrics);
    }

    debug.log([&](std::ostream& os) {
        os << "IteratedSaOptimizer: mode -> final_greedy_polish"
           << " (max_steps=" << config.final_greedy_polish_iterations
           << ", best_score=" << best_state.score << ")\n";
    });

    sa::restore_best(clock_tree, timing, current_score, best_state);
    const double polish_score_before = best_state.score;
    const std::size_t polish_steps =
        sa::run_greedy_batch(clock_tree, timing, buffer_library, baseline_metrics, best_state,
                             config.final_greedy_polish_iterations, config.violation_sample_limit,
                             config.removal_candidate_limit, deadline, start_time, "final_polish",
                             -1, accepted_moves, rejected_moves, context, checkpoint_steps);
    greedy_steps += polish_steps;
    sa::restore_best(clock_tree, timing, current_score, best_state);
    context.write_checkpoint(best_state.tree);
    const double final_elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
    const OptimizerProgressEvent final_event{checkpoint_steps,
                                             final_elapsed,
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
                                             accept_policy_name(AcceptPolicy::IteratedMetropolis)};
    context.maybe_record_progress(final_event, true);
    context.maybe_record_visual(best_state.tree, final_event, true);

    debug.log([&](std::ostream& os) {
        os << "IteratedSaOptimizer: final polish done, steps=" << polish_steps << ", score "
           << polish_score_before << " -> " << best_state.score << '\n';
    });

    debug.log([&](std::ostream& os) {
        os << "IteratedSaOptimizer: iterations = " << total_iterations
           << ", accepted = " << accepted_moves << ", rejected = " << rejected_moves
           << ", restarts = " << restarts << ", greedy_steps = " << greedy_steps
           << ", best score = " << best_state.score << '\n';
    });
}

}  // namespace cadd0040
