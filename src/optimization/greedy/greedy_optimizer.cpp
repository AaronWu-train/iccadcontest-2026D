/**
 * @file greedy_optimizer.cpp
 * @brief Deterministic greedy optimizer implementation.
 */

#include "optimization/greedy/greedy_optimizer.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>

#include "optimization/sa/sa_common.hpp"
#include "optimization/sa/skew_model.hpp"

namespace cadd0040 {
namespace {

constexpr std::chrono::seconds kGreedyTimeBudget{60};
constexpr std::size_t kMaxGreedySteps = 4096;
constexpr std::size_t kMaxResizePolishSteps = 96;
constexpr std::size_t kMaxResizeNodesPerStep = 8192;
constexpr std::size_t kMaxPolishPhases = 5;

std::chrono::seconds greedy_time_budget() {
    if (const char* env_seconds = std::getenv("CADD0040_SA_SECONDS")) {
        return std::chrono::seconds(std::stoll(env_seconds));
    }
    return kGreedyTimeBudget;
}

bool apply_one_resize_polish_step(SkewModel& model, const Metrics& baseline_metrics) {
    SkewMove best_move{
        .kind = SkewMoveKind::Resize,
    };
    double best_delta = 0.0;
    const double before = model.score(baseline_metrics);

    std::size_t tested_nodes = 0;
    for (std::size_t node_idx = 0; node_idx < model.node_count(); ++node_idx) {
        if (model.cell_indices()[node_idx] < 0) {
            continue;
        }
        ++tested_nodes;

        for (int cell_idx = 0; cell_idx < static_cast<int>(model.cell_count()); ++cell_idx) {
            SkewMove move{
                .kind = SkewMoveKind::Resize,
                .node_idx = node_idx,
                .cell_idx = cell_idx,
                .old_cell_idx = model.cell_indices()[node_idx],
            };
            if (!model.try_move(move)) {
                continue;
            }

            const double delta = model.score(baseline_metrics) - before;
            if (delta > best_delta) {
                best_delta = delta;
                best_move = move;
            }
            model.undo_move(move);
        }

        if (tested_nodes >= kMaxResizeNodesPerStep) {
            break;
        }
    }

    if (best_delta <= 0.0) {
        return false;
    }
    return model.try_move(best_move);
}

}  // namespace

void GreedyOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                          const BufferLibrary& buffer_library,
                          const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;
    SkewModel model(clock_tree, data_path_graph, buffer_library);

    double current_score = model.score(baseline_metrics);
    double best_score = current_score;
    SkewModelState best_state = model.snapshot();
    Metrics best_metrics = sa::metrics_from_skew(best_state.metrics);

    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + greedy_time_budget();

    std::size_t greedy_steps = 0;
    std::size_t resize_steps = 0;
    std::size_t phases = 0;
    for (; phases < kMaxPolishPhases && std::chrono::steady_clock::now() < deadline; ++phases) {
        bool phase_changed = false;

        while (greedy_steps < kMaxGreedySteps && std::chrono::steady_clock::now() < deadline) {
            if (!model.apply_one_greedy_step(baseline_metrics)) {
                break;
            }
            phase_changed = true;
            ++greedy_steps;
            sa::maybe_update_best(model, baseline_metrics, current_score, best_score, best_state,
                                  best_metrics);

            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time)
                    .count();
            context.debug_progress.report_if_due(elapsed, best_metrics, baseline_metrics,
                                                 current_score);
        }

        std::size_t phase_resize_steps = 0;
        while (phase_resize_steps < kMaxResizePolishSteps &&
               std::chrono::steady_clock::now() < deadline) {
            if (!apply_one_resize_polish_step(model, baseline_metrics)) {
                break;
            }
            phase_changed = true;
            ++phase_resize_steps;
            ++resize_steps;
            sa::maybe_update_best(model, baseline_metrics, current_score, best_score, best_state,
                                  best_metrics);
        }

        if (!phase_changed) {
            break;
        }
    }

    sa::materialize(clock_tree, best_state, model, buffer_library);
    model.restore(best_state);

    std::cerr << "GreedyOptimizer: phases = " << phases << ", steps = " << greedy_steps
              << ", resize_steps = " << resize_steps << ", best score = " << best_score
              << ", restored score = " << model.score(baseline_metrics) << '\n';
}

}  // namespace cadd0040
