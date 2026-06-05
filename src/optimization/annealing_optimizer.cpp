/**
 * @file annealing_optimizer.cpp
 * @brief Simulated-annealing optimizer implementation.
 */

#include "optimization/annealing_optimizer.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>

#include "optimization/skew_model.hpp"

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

std::mt19937& rng() {
    static thread_local std::mt19937 engine(2026);
    return engine;
}

int pick_insert_cell(SkewModel& model) {
    static std::uniform_real_distribution<double> cell_dist(0.0, 1.0);
    if (cell_dist(rng()) < 0.75) {
        const int smallest = model.smallest_fanout1_cell_index();
        if (smallest >= 0) {
            return smallest;
        }
    }
    return model.random_fanout1_cell_index();
}

SkewMove random_move(SkewModel& model) {
    static std::uniform_real_distribution<double> kind_dist(0.0, 1.0);
    const double roll = kind_dist(rng());

    if (roll < 0.50) {
        return SkewMove{
            .kind = SkewMoveKind::Insert,
            .edge_idx = model.random_guided_insert_edge(),
            .cell_idx = pick_insert_cell(model),
        };
    }
    if (roll < 0.60) {
        return SkewMove{
            .kind = SkewMoveKind::Insert,
            .edge_idx = model.random_edge_index(),
            .cell_idx = pick_insert_cell(model),
        };
    }
    if (roll < 0.70) {
        return SkewMove{
            .kind = SkewMoveKind::Remove,
            .edge_idx = model.random_edge_with_inserts(),
            .insert_position = -1,
        };
    }

    const std::size_t node_idx = model.random_buffer_node_index();
    const int new_cell_idx = model.random_valid_cell_for_buffer(node_idx);
    return SkewMove{
        .kind = SkewMoveKind::Resize,
        .node_idx = node_idx,
        .cell_idx = new_cell_idx,
        .old_cell_idx = model.cell_indices()[node_idx],
    };
}

void materialize(ClockTree& clock_tree, const SkewModelState& state, const SkewModel& model,
                 const BufferLibrary& buffer_library) {
    const auto& names = model.node_names();
    const auto& cells = model.cells();
    const auto& tree_edges = model.tree_edges();
    const auto& original_cells = model.original_cell_indices();

    for (std::size_t node_idx = 0; node_idx < names.size(); ++node_idx) {
        if (state.cell_indices[node_idx] < 0) {
            continue;
        }
        const int original_cell_idx = original_cells[node_idx];
        const int target_cell_idx = state.cell_indices[node_idx];
        if (original_cell_idx == target_cell_idx) {
            continue;
        }
        clock_tree.resize_buffer(
            names[node_idx], cells[static_cast<std::size_t>(target_cell_idx)].name, buffer_library);
    }

    std::size_t new_buf_counter = 0;
    while (clock_tree.contains_name("NEW_BUF_" + std::to_string(new_buf_counter))) {
        ++new_buf_counter;
    }

    for (std::size_t edge_idx = 0; edge_idx < tree_edges.size(); ++edge_idx) {
        const auto& original_edge = tree_edges[edge_idx];
        const auto& inserted_cells = state.edge_inserted_cells[edge_idx];
        if (inserted_cells.empty()) {
            continue;
        }

        std::string parent_name = names[original_edge.parent_idx];
        std::string downstream_name = names[original_edge.child_idx];

        for (const int cell_idx : inserted_cells) {
            if (cell_idx < 0) {
                continue;
            }
            std::string buffer_name;
            do {
                buffer_name = "NEW_BUF_" + std::to_string(new_buf_counter++);
            } while (clock_tree.contains_name(buffer_name));

            const std::string& cell_name = cells[static_cast<std::size_t>(cell_idx)].name;
            if (!clock_tree.insert_buffer(parent_name, downstream_name, buffer_name, cell_name,
                                          buffer_library)) {
                std::cerr << "AnnealingOptimizer: failed to materialize " << buffer_name << '\n';
                continue;
            }
            downstream_name = buffer_name;
        }
    }
}

Metrics metrics_from_skew(const SkewModelMetrics& model_metrics) {
    return Metrics{
        .tns_ss = model_metrics.tns_ss,
        .wns_ss = model_metrics.wns_ss,
        .tns_ff = model_metrics.tns_ff,
        .wns_ff = model_metrics.wns_ff,
        .area = model_metrics.area,
    };
}

void maybe_update_best(SkewModel& model, const Metrics& baseline_metrics, double& current_score,
                       double& best_score, SkewModelState& best_state, Metrics& best_metrics) {
    current_score = model.score(baseline_metrics);
    if (current_score > best_score) {
        best_score = current_score;
        best_state = model.snapshot();
        best_metrics = metrics_from_skew(best_state.metrics);
    }
}

void restart_from_best(SkewModel& model, double& current_score, double best_score,
                       const SkewModelState& best_state) {
    model.restore(best_state);
    current_score = best_score;
}

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

    const double final_score = model.score(baseline_metrics);
    std::cerr << "AnnealingOptimizer: iterations = " << iteration
              << ", accepted = " << accepted_moves << ", rejected = " << rejected_moves
              << ", restarts = " << restarts << ", greedy_polish = " << greedy_polish_steps
              << ", best score = " << best_score << ", restored score = " << final_score << '\n';
}

}  // namespace cadd0040
