/**
 * @file iterated_sa_optimizer.cpp
 * @brief Multi-round SA + Greedy optimizer implementation.
 */

#include "optimization/sa/iterated_sa_optimizer.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <random>

#include "optimization/sa/sa_common.hpp"
#include "optimization/sa/skew_model.hpp"

namespace cadd0040 {
namespace {

constexpr std::chrono::seconds kAnnealingTimeBudget{540};
constexpr double kInitialTemperature = 0.08;
constexpr double kMinTemperature = 1e-6;
constexpr double kCoolingFactor = 0.01;
constexpr std::size_t kGreedyWarmupIterations = 512;
constexpr std::size_t kNumSaRounds = 5;
constexpr std::size_t kGreedyRoundIterations = 48;
constexpr std::size_t kFinalGreedyPolishIterations = 64;
constexpr std::size_t kFinalResizePolishIterations = 32;
constexpr std::size_t kResizeNodeScanLimit = 4096;
constexpr std::size_t kRestartStaleIterations = 2500;
constexpr double kRestartScoreGap = 0.05;

using sa::materialize;
using sa::maybe_update_best;
using sa::metrics_from_skew;
using sa::random_move;
using sa::restart_from_best;
using sa::rng;

std::size_t run_greedy_batch(DebugProgress& debug, SkewModel& model,
                             const Metrics& baseline_metrics, double& current_score,
                             double& best_score, SkewModelState& best_state, Metrics& best_metrics,
                             std::size_t max_steps, std::size_t& greedy_steps) {
    const double score_before = best_score;
    model.restore(best_state);
    current_score = best_score;

    std::size_t batch_steps = 0;
    for (std::size_t step = 0; step < max_steps; ++step) {
        if (!model.apply_one_greedy_step(baseline_metrics)) {
            break;
        }
        ++batch_steps;
        ++greedy_steps;
        maybe_update_best(model, baseline_metrics, current_score, best_score, best_state,
                          best_metrics);
    }

    debug.log([&](std::ostream& os) {
        os << "IteratedSaOptimizer: greedy batch done, steps=" << batch_steps << ", score "
           << score_before << " -> " << best_score << '\n';
    });
    return batch_steps;
}

std::size_t run_sa_phase(SkewModel& model, const Metrics& baseline_metrics,
                         const OptimizerContext& context, double& current_score, double& best_score,
                         SkewModelState& best_state, Metrics& best_metrics,
                         const std::chrono::steady_clock::time_point& start_time,
                         const std::chrono::steady_clock::time_point& round_deadline,
                         const std::chrono::seconds& time_budget, std::size_t& accepted_moves,
                         std::size_t& rejected_moves, std::size_t& restarts) {
    std::size_t iteration = 0;
    std::size_t iterations_since_best = 0;

    while (std::chrono::steady_clock::now() < round_deadline) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - start_time).count();
        const double budget = static_cast<double>(time_budget.count());
        const double progress = std::min(1.0, elapsed / budget);
        const double temperature =
            std::max(kMinTemperature, kInitialTemperature * std::pow(kCoolingFactor, progress));

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

    return iteration;
}

bool apply_one_resize_polish_step(SkewModel& model, const Metrics& baseline_metrics) {
    SkewMove best_move{SkewMoveKind::Resize};
    double best_delta = 0.0;
    const double before_score = model.score(baseline_metrics);

    std::size_t scanned_buffer_nodes = 0;
    for (std::size_t node_idx = 0; node_idx < model.node_count(); ++node_idx) {
        const int old_cell_idx = model.cell_indices()[node_idx];
        if (old_cell_idx < 0) {
            continue;
        }

        ++scanned_buffer_nodes;
        for (int cell_idx = 0; cell_idx < static_cast<int>(model.cell_count()); ++cell_idx) {
            SkewMove move{SkewMoveKind::Resize, 0, node_idx, cell_idx, 0, old_cell_idx};
            if (!model.try_move(move)) {
                continue;
            }

            const double delta = model.score(baseline_metrics) - before_score;
            if (delta > best_delta) {
                best_delta = delta;
                best_move = move;
            }
            model.undo_move(move);
        }

        if (scanned_buffer_nodes >= kResizeNodeScanLimit) {
            break;
        }
    }

    if (best_delta <= 0.0) {
        return false;
    }
    return model.try_move(best_move);
}

}  // namespace

void IteratedSaOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                              const BufferLibrary& buffer_library,
                              const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;
    DebugProgress& debug = context.debug_progress;

    SkewModel model(clock_tree, data_path_graph, buffer_library);

    debug.log([&](std::ostream& os) {
        os << "IteratedSaOptimizer: baseline score = " << model.score(baseline_metrics) << '\n';
    });

    model.apply_greedy_warmup(baseline_metrics, kGreedyWarmupIterations);

    double current_score = model.score(baseline_metrics);
    SkewModelState best_state = model.snapshot();
    double best_score = current_score;
    Metrics best_metrics = metrics_from_skew(best_state.metrics);

    debug.log([&](std::ostream& os) {
        os << "IteratedSaOptimizer: after warmup score = " << current_score << '\n';
        os << "IteratedSaOptimizer: mode -> multi_round (" << kNumSaRounds << " rounds)\n";
    });

    std::chrono::seconds time_budget = kAnnealingTimeBudget;
    if (const char* env_seconds = std::getenv("CADD0040_SA_SECONDS")) {
        time_budget = std::chrono::seconds(std::stoll(env_seconds));
    }

    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + time_budget;

    std::size_t accepted_moves = 0;
    std::size_t rejected_moves = 0;
    std::size_t total_iterations = 0;
    std::size_t restarts = 0;
    std::size_t greedy_steps = 0;

    for (std::size_t round = 0; round < kNumSaRounds; ++round) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            break;
        }

        const auto remaining = deadline - now;
        const auto round_budget =
            remaining / static_cast<std::chrono::seconds::rep>(kNumSaRounds - round);
        const auto round_deadline = now + round_budget;
        const double round_budget_seconds = std::chrono::duration<double>(round_budget).count();
        const double elapsed = std::chrono::duration<double>(now - start_time).count();
        const double progress = std::min(1.0, elapsed / static_cast<double>(time_budget.count()));
        const double temperature =
            std::max(kMinTemperature, kInitialTemperature * std::pow(kCoolingFactor, progress));

        debug.log([&](std::ostream& os) {
            os << "IteratedSaOptimizer: round " << (round + 1) << "/" << kNumSaRounds
               << " mode -> SA"
               << " (budget=" << round_budget_seconds << "s"
               << ", temperature=" << temperature << ", best_score=" << best_score << ")\n";
        });

        const std::size_t round_iterations = run_sa_phase(
            model, baseline_metrics, context, current_score, best_score, best_state, best_metrics,
            start_time, round_deadline, time_budget, accepted_moves, rejected_moves, restarts);
        total_iterations += round_iterations;

        debug.log([&](std::ostream& os) {
            os << "IteratedSaOptimizer: round " << (round + 1) << " SA done"
               << ", iterations=" << round_iterations << ", best_score=" << best_score
               << ", current_score=" << current_score << '\n';
        });

        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }

        debug.log([&](std::ostream& os) {
            os << "IteratedSaOptimizer: round " << (round + 1) << " mode -> greedy_batch"
               << " (max_steps=" << kGreedyRoundIterations << ", best_score=" << best_score
               << ")\n";
        });
        run_greedy_batch(debug, model, baseline_metrics, current_score, best_score, best_state,
                         best_metrics, kGreedyRoundIterations, greedy_steps);
    }

    debug.log([&](std::ostream& os) {
        os << "IteratedSaOptimizer: mode -> final_greedy_polish"
           << " (max_steps=" << kFinalGreedyPolishIterations << ", best_score=" << best_score
           << ")\n";
    });

    model.restore(best_state);
    current_score = best_score;
    const double polish_score_before = best_score;
    std::size_t polish_steps = 0;
    for (std::size_t polish = 0; polish < kFinalGreedyPolishIterations; ++polish) {
        if (!model.apply_one_greedy_step(baseline_metrics)) {
            break;
        }
        ++polish_steps;
        ++greedy_steps;
        maybe_update_best(model, baseline_metrics, current_score, best_score, best_state,
                          best_metrics);
    }

    debug.log([&](std::ostream& os) {
        os << "IteratedSaOptimizer: final polish done, steps=" << polish_steps << ", score "
           << polish_score_before << " -> " << best_score << '\n';
    });

    const double resize_score_before = best_score;
    std::size_t resize_polish_steps = 0;
    for (std::size_t polish = 0; polish < kFinalResizePolishIterations; ++polish) {
        if (!apply_one_resize_polish_step(model, baseline_metrics)) {
            break;
        }
        ++resize_polish_steps;
        maybe_update_best(model, baseline_metrics, current_score, best_score, best_state,
                          best_metrics);
    }

    debug.log([&](std::ostream& os) {
        os << "IteratedSaOptimizer: final resize polish done, steps=" << resize_polish_steps
           << ", score " << resize_score_before << " -> " << best_score << '\n';
    });

    materialize(clock_tree, best_state, model, buffer_library);

    model.restore(best_state);

    debug.log([&](std::ostream& os) {
        const double final_score = model.score(baseline_metrics);
        os << "IteratedSaOptimizer: iterations = " << total_iterations
           << ", accepted = " << accepted_moves << ", rejected = " << rejected_moves
           << ", restarts = " << restarts << ", greedy_steps = " << greedy_steps
           << ", resize_polish_steps = " << resize_polish_steps
           << ", best score = " << best_score << ", restored score = " << final_score << '\n';
    });
}

}  // namespace cadd0040
