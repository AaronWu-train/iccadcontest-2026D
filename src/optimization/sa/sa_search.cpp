/**
 * @file sa_search.cpp
 * @brief Shared search primitives for SA-family optimizers.
 */

#include "optimization/sa/sa_search.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace cadd0040 {
namespace sa {
namespace {

struct CandidateMove {
    enum class Kind {
        Insert,
        Remove,
        Resize,
    };

    Kind kind = Kind::Insert;
    EdgeId edge_id = kInvalidEdgeId;
    NodeId node_id = kInvalidNodeId;
    int cell_idx = -1;
};

std::string next_buffer_name(const ClockTree& clock_tree) {
    std::size_t index = 0;
    while (clock_tree.contains_name("NEW_BUF_" + std::to_string(index))) {
        ++index;
    }
    return "NEW_BUF_" + std::to_string(index);
}

EdgeId incoming_edge(const ClockTree& clock_tree, NodeId node_id) {
    if (!clock_tree.contains_node_id(node_id)) {
        return kInvalidEdgeId;
    }
    const NodeId parent = clock_tree.node(node_id).parent_id;
    return parent == kInvalidNodeId ? kInvalidEdgeId : clock_tree.edge_between(parent, node_id);
}

ClockTreeEdit apply_candidate(ClockTree& clock_tree, TimingState& timing,
                              const BufferLibrary& buffer_library, const CandidateMove& move) {
    ClockTreeEdit edit;
    switch (move.kind) {
        case CandidateMove::Kind::Insert:
            if (move.cell_idx < 0) {
                return {};
            }
            edit = clock_tree.insert_buffer_on_edge(
                move.edge_id, next_buffer_name(clock_tree),
                timing.cells()[static_cast<std::size_t>(move.cell_idx)].name, buffer_library);
            break;
        case CandidateMove::Kind::Remove:
            edit = clock_tree.remove_inserted_buffer(move.node_id);
            break;
        case CandidateMove::Kind::Resize:
            if (move.cell_idx < 0) {
                return {};
            }
            edit = clock_tree.resize_buffer(
                move.node_id, timing.cells()[static_cast<std::size_t>(move.cell_idx)].name,
                buffer_library);
            break;
    }
    if (edit) {
        timing.apply(edit);
    }
    return edit;
}

void undo_candidate(ClockTree& clock_tree, TimingState& timing, const ClockTreeEdit& edit) {
    timing.undo(edit);
    clock_tree.undo(edit);
}

EdgeId random_edge(const ClockTree& clock_tree) {
    const auto edges = clock_tree.active_edge_ids();
    if (edges.empty()) {
        return kInvalidEdgeId;
    }
    std::uniform_int_distribution<std::size_t> dist(0, edges.size() - 1);
    return edges[dist(rng())];
}

NodeId random_buffer_node(const ClockTree& clock_tree) {
    const auto buffers = clock_tree.buffer_nodes();
    if (buffers.empty()) {
        return kInvalidNodeId;
    }
    std::uniform_int_distribution<std::size_t> dist(0, buffers.size() - 1);
    return buffers[dist(rng())];
}

NodeId random_inserted_buffer(const ClockTree& clock_tree) {
    std::vector<NodeId> candidates;
    for (const NodeId node_id : clock_tree.buffer_nodes()) {
        if (clock_tree.node(node_id).origin == NodeOrigin::Inserted) {
            candidates.push_back(node_id);
        }
    }
    if (candidates.empty()) {
        return kInvalidNodeId;
    }
    std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
    return candidates[dist(rng())];
}

int random_valid_cell_for_buffer(const ClockTree& clock_tree, const TimingState& timing,
                                 NodeId node_id) {
    if (!clock_tree.contains_node_id(node_id)) {
        return -1;
    }
    const auto& node = clock_tree.node(node_id);
    std::vector<int> valid_cells;
    for (int cell_idx = 0; cell_idx < static_cast<int>(timing.cell_count()); ++cell_idx) {
        if (timing.cell_supports_fanout(cell_idx, node.child_ids.size()) &&
            timing.cells()[static_cast<std::size_t>(cell_idx)].name != node.cell_type) {
            valid_cells.push_back(cell_idx);
        }
    }
    if (valid_cells.empty()) {
        return -1;
    }
    std::uniform_int_distribution<std::size_t> dist(0, valid_cells.size() - 1);
    return valid_cells[dist(rng())];
}

int pick_insert_cell(const TimingState& timing) {
    std::uniform_real_distribution<double> cell_dist(0.0, 1.0);
    if (cell_dist(rng()) < 0.75) {
        const int smallest = timing.smallest_cell_for_fanout(1);
        if (smallest >= 0) {
            return smallest;
        }
    }
    const auto cells = timing.cells_for_fanout_by_area(1);
    if (cells.empty()) {
        return -1;
    }
    std::uniform_int_distribution<std::size_t> dist(0, cells.size() - 1);
    return cells[dist(rng())];
}

EdgeId random_guided_insert_edge(const ClockTree& clock_tree, const TimingState& timing) {
    std::vector<std::size_t> violating_paths;
    for (std::size_t path_idx = 0; path_idx < timing.path_count(); ++path_idx) {
        if (timing.ss_slack()[path_idx] < 0.0 || timing.ff_slack()[path_idx] < 0.0) {
            violating_paths.push_back(path_idx);
        }
    }
    if (violating_paths.empty()) {
        return random_edge(clock_tree);
    }

    std::uniform_int_distribution<std::size_t> path_dist(0, violating_paths.size() - 1);
    const std::size_t path_idx = violating_paths[path_dist(rng())];
    const auto& path = timing.paths()[path_idx];
    std::vector<EdgeId> target_edges;
    if (timing.ss_slack()[path_idx] < 0.0) {
        const EdgeId edge = incoming_edge(clock_tree, path.capture_ff);
        if (edge != kInvalidEdgeId) {
            target_edges.push_back(edge);
        }
    }
    if (timing.ff_slack()[path_idx] < 0.0) {
        const EdgeId edge = incoming_edge(clock_tree, path.launch_ff);
        if (edge != kInvalidEdgeId) {
            target_edges.push_back(edge);
        }
    }
    if (target_edges.empty()) {
        return random_edge(clock_tree);
    }
    std::uniform_int_distribution<std::size_t> edge_dist(0, target_edges.size() - 1);
    return target_edges[edge_dist(rng())];
}

CandidateMove random_move(const ClockTree& clock_tree, const TimingState& timing) {
    std::uniform_real_distribution<double> kind_dist(0.0, 1.0);
    const double roll = kind_dist(rng());

    if (roll < 0.45) {
        return CandidateMove{CandidateMove::Kind::Insert,
                             random_guided_insert_edge(clock_tree, timing), kInvalidNodeId,
                             pick_insert_cell(timing)};
    }
    if (roll < 0.55) {
        return CandidateMove{CandidateMove::Kind::Insert, random_edge(clock_tree), kInvalidNodeId,
                             pick_insert_cell(timing)};
    }
    if (roll < 0.80) {
        return CandidateMove{CandidateMove::Kind::Remove, kInvalidEdgeId,
                             random_inserted_buffer(clock_tree), -1};
    }

    const NodeId node_id = random_buffer_node(clock_tree);
    return CandidateMove{CandidateMove::Kind::Resize, kInvalidEdgeId, node_id,
                         random_valid_cell_for_buffer(clock_tree, timing, node_id)};
}

bool try_best_candidate(ClockTree& clock_tree, TimingState& timing,
                        const BufferLibrary& buffer_library, const Metrics& baseline_metrics,
                        const std::vector<CandidateMove>& candidates) {
    const double before_score = timing.score(baseline_metrics);
    double best_delta = 0.0;
    CandidateMove best_move;

    for (const auto& candidate : candidates) {
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

    if (best_delta <= 0.0) {
        return false;
    }
    return static_cast<bool>(apply_candidate(clock_tree, timing, buffer_library, best_move));
}

bool apply_one_greedy_step(ClockTree& clock_tree, TimingState& timing,
                           const BufferLibrary& buffer_library, const Metrics& baseline_metrics) {
    std::vector<std::size_t> violating_paths;
    for (std::size_t path_idx = 0; path_idx < timing.path_count(); ++path_idx) {
        if (timing.ss_slack()[path_idx] < 0.0 || timing.ff_slack()[path_idx] < 0.0) {
            violating_paths.push_back(path_idx);
        }
    }
    std::sort(violating_paths.begin(), violating_paths.end(),
              [&](std::size_t lhs, std::size_t rhs) {
                  const double lhs_violation =
                      std::min(timing.ss_slack()[lhs], 0.0) + std::min(timing.ff_slack()[lhs], 0.0);
                  const double rhs_violation =
                      std::min(timing.ss_slack()[rhs], 0.0) + std::min(timing.ff_slack()[rhs], 0.0);
                  return lhs_violation < rhs_violation;
              });

    const auto fanout1_cells = timing.cells_for_fanout_by_area(1);
    std::vector<CandidateMove> candidates;
    const std::size_t sample_count = std::min<std::size_t>(violating_paths.size(), 32);
    for (std::size_t sample = 0; sample < sample_count; ++sample) {
        const std::size_t path_idx = violating_paths[sample];
        const auto& path = timing.paths()[path_idx];
        std::vector<EdgeId> target_edges;
        if (timing.ss_slack()[path_idx] < 0.0) {
            const EdgeId edge = incoming_edge(clock_tree, path.capture_ff);
            if (edge != kInvalidEdgeId) {
                target_edges.push_back(edge);
            }
        }
        if (timing.ff_slack()[path_idx] < 0.0) {
            const EdgeId edge = incoming_edge(clock_tree, path.launch_ff);
            if (edge != kInvalidEdgeId) {
                target_edges.push_back(edge);
            }
        }
        for (const EdgeId edge : target_edges) {
            for (const int cell_idx : fanout1_cells) {
                candidates.push_back(
                    CandidateMove{CandidateMove::Kind::Insert, edge, kInvalidNodeId, cell_idx});
            }
        }
    }

    std::size_t removal_candidates = 0;
    for (const NodeId node_id : clock_tree.buffer_nodes()) {
        if (clock_tree.node(node_id).origin != NodeOrigin::Inserted) {
            continue;
        }
        candidates.push_back(
            CandidateMove{CandidateMove::Kind::Remove, kInvalidEdgeId, node_id, -1});
        if (++removal_candidates >= 512) {
            break;
        }
    }

    return try_best_candidate(clock_tree, timing, buffer_library, baseline_metrics, candidates);
}

}  // namespace

std::mt19937& rng() {
    static thread_local std::mt19937 engine(2026);
    return engine;
}

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
                             const std::chrono::steady_clock::time_point& deadline) {
    std::size_t steps = 0;
    for (; steps < max_steps && std::chrono::steady_clock::now() < deadline; ++steps) {
        if (!apply_one_greedy_step(clock_tree, timing, buffer_library, baseline_metrics)) {
            break;
        }
        maybe_update_best(clock_tree, timing, baseline_metrics, best_state);
    }
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
                         std::size_t greedy_polish_interval, std::size_t& greedy_steps,
                         std::size_t& accepted_moves, std::size_t& rejected_moves,
                         std::size_t& restarts) {
    std::size_t iteration = 0;
    std::size_t iterations_since_best = 0;

    while (std::chrono::steady_clock::now() < phase_deadline) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - start_time).count();
        const double budget = static_cast<double>(total_budget.count());
        const double progress = budget <= 0.0 ? 1.0 : std::min(1.0, elapsed / budget);
        const double temperature =
            std::max(min_temperature, initial_temperature * std::pow(cooling_factor, progress));

        if (greedy_polish_interval > 0 && iteration > 0 &&
            iteration % greedy_polish_interval == 0) {
            if (apply_one_greedy_step(clock_tree, timing, buffer_library, baseline_metrics)) {
                ++greedy_steps;
                current_score = timing.score(baseline_metrics);
                maybe_update_best(clock_tree, timing, baseline_metrics, best_state);
                iterations_since_best = 0;
            }
        }

        const CandidateMove move = random_move(clock_tree, timing);
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

        if (iterations_since_best >= restart_stale_iterations ||
            current_score < best_state.score - restart_score_gap) {
            restore_best(clock_tree, timing, current_score, best_state);
            iterations_since_best = 0;
            ++restarts;
        }

        debug.report_if_due(elapsed, best_state.metrics, baseline_metrics, current_score);
        ++iteration;
    }

    return iteration;
}

}  // namespace sa
}  // namespace cadd0040
