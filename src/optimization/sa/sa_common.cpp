/**
 * @file sa_common.cpp
 * @brief Shared helpers for simulated-annealing optimizers.
 */

#include "optimization/sa/sa_common.hpp"

#include <iostream>
#include <string>

namespace cadd0040 {
namespace sa {

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

    if (roll < 0.45) {
        return SkewMove{
            .kind = SkewMoveKind::Insert,
            .edge_idx = model.random_guided_insert_edge(),
            .cell_idx = pick_insert_cell(model),
        };
    }
    if (roll < 0.55) {
        return SkewMove{
            .kind = SkewMoveKind::Insert,
            .edge_idx = model.random_edge_index(),
            .cell_idx = pick_insert_cell(model),
        };
    }
    if (roll < 0.80) {
        const std::size_t edge_idx = model.random_edge_with_inserts();
        const auto& inserted_cells = model.tree_edges()[edge_idx].inserted_cell_indices;
        const int insert_position =
            inserted_cells.empty() ? -1 : static_cast<int>(inserted_cells.size() - 1);
        const int cell_idx =
            insert_position < 0 ? -1 : inserted_cells[static_cast<std::size_t>(insert_position)];
        return SkewMove{
            .kind = SkewMoveKind::Remove,
            .edge_idx = edge_idx,
            .cell_idx = cell_idx,
            .insert_position = insert_position,
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
                std::cerr << "SA optimizer: failed to materialize " << buffer_name << '\n';
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

}  // namespace sa
}  // namespace cadd0040
