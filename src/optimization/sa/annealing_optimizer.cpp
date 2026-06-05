/**
 * @file annealing_optimizer.cpp
 * @brief Simulated-annealing optimizer implementation.
 */

#include "optimization/sa/annealing_optimizer.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>

#include "optimization/sa/sa_common.hpp"
#include "optimization/sa/skew_model.hpp"

namespace cadd0040 {
namespace {

constexpr std::chrono::seconds kAnnealingTimeBudget{540};
constexpr double kInitialTemperature = 0.08;
constexpr double kMinTemperature = 1e-6;
constexpr double kCoolingFactor = 0.01;
constexpr std::size_t kGreedyWarmupIterations = 256;
constexpr std::size_t kFinalGreedyPolishIterations = 64;
constexpr std::size_t kRestartStaleIterations = 2500;
constexpr double kRestartScoreGap = 0.05;
constexpr std::size_t kGreedyPolishInterval = 250;

using sa::materialize;
using sa::maybe_update_best;
using sa::metrics_from_skew;
using sa::random_move;
using sa::restart_from_best;
using sa::rng;

}  // namespace

void AnnealingOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                             const BufferLibrary& buffer_library, const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;

    SkewModel model(clock_tree, data_path_graph, buffer_library);

    const double baseline_score = model.score(baseline_metrics);
    std::cerr << "AnnealingOptimizer: baseline score = " << baseline_score << '\n';

    model.apply_greedy_warmup(baseline_metrics, kGreedyWarmupIterations);

    double current_score = model.score(baseline_metrics);
    SkewModelState best_state = model.snapshot();
    double best_score = current_score;
    Metrics best_metrics = metrics_from_skew(best_state.metrics);

    std::cerr << "AnnealingOptimizer: after warmup score = " << current_score << '\n';

    std::chrono::seconds time_budget = kAnnealingTimeBudget;
    if (const char* env_seconds = std::getenv("CADD0040_SA_SECONDS")) {
        time_budget = std::chrono::seconds(std::stoll(env_seconds));
    }

    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + time_budget;

    std::size_t accepted_moves = 0;
    std::size_t rejected_moves = 0;
    std::size_t iteration = 0;
    std::size_t iterations_since_best = 0;
    std::size_t restarts = 0;
    std::size_t greedy_polish_steps = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - start_time).count();
        const double budget = static_cast<double>(time_budget.count());
        const double progress = std::min(1.0, elapsed / budget);
        const double temperature =
            std::max(kMinTemperature, kInitialTemperature * std::pow(kCoolingFactor, progress));

        if (iteration > 0 && iteration % kGreedyPolishInterval == 0) {
            if (model.apply_one_greedy_step(baseline_metrics)) {
                ++greedy_polish_steps;
                maybe_update_best(model, baseline_metrics, current_score, best_score, best_state,
                                  best_metrics);
                iterations_since_best = 0;
            }
        }

        SkewMove move = random_move(model);
        if (move.kind == SkewMoveKind::Resize && move.cell_idx < 0) {
            ++iteration;
            continue;
        }
        if (move.kind == SkewMoveKind::Insert && move.cell_idx < 0) {
            ++iteration;
            continue;
        }

        if (!model.try_move(move)) {
            ++iteration;
            continue;
        }

        const double new_score = model.score(baseline_metrics);
        const double delta = new_score - current_score;
        bool accept = delta > 0.0;

        if (!accept && temperature > kMinTemperature) {
            std::uniform_real_distribution<double> accept_dist(0.0, 1.0);
            const double probability = std::exp(delta / temperature);
            accept = accept_dist(rng()) < probability;
        }

        if (accept) {
            current_score = new_score;
            ++accepted_moves;
            if (new_score > best_score) {
                best_score = new_score;
                best_state = model.snapshot();
                best_metrics = metrics_from_skew(best_state.metrics);
                iterations_since_best = 0;
            } else {
                ++iterations_since_best;
            }
        } else {
            model.undo_move(move);
            ++rejected_moves;
            ++iterations_since_best;
        }

        if (iterations_since_best >= kRestartStaleIterations ||
            current_score < best_score - kRestartScoreGap) {
            restart_from_best(model, current_score, best_score, best_state);
            iterations_since_best = 0;
            ++restarts;
        }

        context.debug_progress.report_if_due(elapsed, best_metrics, baseline_metrics,
                                             current_score);

        ++iteration;
    }

    model.restore(best_state);
    current_score = best_score;
    for (std::size_t polish = 0; polish < kFinalGreedyPolishIterations; ++polish) {
        if (!model.apply_one_greedy_step(baseline_metrics)) {
            break;
        }
        ++greedy_polish_steps;
        maybe_update_best(model, baseline_metrics, current_score, best_score, best_state,
                          best_metrics);
    }

    materialize(clock_tree, best_state, model, buffer_library);

    model.restore(best_state);
    const double final_score = model.score(baseline_metrics);
    std::cerr << "AnnealingOptimizer: iterations = " << iteration
              << ", accepted = " << accepted_moves << ", rejected = " << rejected_moves
              << ", restarts = " << restarts << ", greedy_polish = " << greedy_polish_steps
              << ", best score = " << best_score << ", restored score = " << final_score << '\n';
}

}  // namespace cadd0040
