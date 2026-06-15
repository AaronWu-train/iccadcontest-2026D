/**
 * @file milp_optimizer.cpp
 * @brief MILP-inspired deterministic optimizer implementation.
 */

#include "optimization/milp/milp_optimizer.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <string>
#include <vector>

#include "optimization/optimizer_config.hpp"
#include "optimization/timing_state.hpp"

namespace cadd0040 {
namespace {

struct Violation {
    std::size_t path_idx = 0;
    double severity = 0.0;
};

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

struct BestRunState {
    ClockTree tree;
    TimingSnapshot timing;
    Metrics metrics;
    double score = -std::numeric_limits<double>::infinity();
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

void maybe_update_best(const ClockTree& clock_tree, const TimingState& timing,
                       const Metrics& baseline_metrics, BestRunState& best_state) {
    const double current_score = timing.score(baseline_metrics);
    if (current_score > best_state.score) {
        best_state.tree = clock_tree;
        best_state.timing = timing.snapshot();
        best_state.metrics = timing.metrics();
        best_state.score = current_score;
    }
}

std::vector<Violation> worst_violations(const TimingState& timing, const MilpConfig& config) {
    std::vector<Violation> violations;
    violations.reserve(timing.path_count());
    for (std::size_t path_idx = 0; path_idx < timing.path_count(); ++path_idx) {
        const double severity = std::max(0.0, -timing.ss_slack()[path_idx]) +
                                std::max(0.0, -timing.ff_slack()[path_idx]);
        if (severity > 0.0) {
            violations.push_back(Violation{path_idx, severity});
        }
    }
    std::sort(violations.begin(), violations.end(), [](const Violation& lhs, const Violation& rhs) {
        return lhs.severity > rhs.severity;
    });
    if (violations.size() > config.violation_window) {
        violations.resize(config.violation_window);
    }
    return violations;
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

bool apply_one_milp_round(ClockTree& clock_tree, TimingState& timing,
                          const BufferLibrary& buffer_library, const Metrics& baseline_metrics,
                          const MilpConfig& config) {
    const auto violations = worst_violations(timing, config);
    const auto fanout1_cells = timing.cells_for_fanout_by_area(1);
    std::vector<CandidateMove> candidates;
    candidates.reserve(config.candidate_limit);

    for (const auto& violation : violations) {
        const std::size_t path_idx = violation.path_idx;
        const auto& path = timing.paths()[path_idx];

        if (timing.ss_slack()[path_idx] < 0.0) {
            const EdgeId edge = incoming_edge(clock_tree, path.capture_ff);
            if (edge != kInvalidEdgeId) {
                for (const int cell_idx : fanout1_cells) {
                    candidates.push_back(
                        CandidateMove{CandidateMove::Kind::Insert, edge, kInvalidNodeId, cell_idx});
                    if (candidates.size() >= config.candidate_limit) {
                        break;
                    }
                }
            }
        }
        if (candidates.size() >= config.candidate_limit) {
            break;
        }

        if (timing.ff_slack()[path_idx] < 0.0) {
            const EdgeId edge = incoming_edge(clock_tree, path.launch_ff);
            if (edge != kInvalidEdgeId) {
                for (const int cell_idx : fanout1_cells) {
                    candidates.push_back(
                        CandidateMove{CandidateMove::Kind::Insert, edge, kInvalidNodeId, cell_idx});
                    if (candidates.size() >= config.candidate_limit) {
                        break;
                    }
                }
            }
        }
        if (candidates.size() >= config.candidate_limit) {
            break;
        }
    }

    if (violations.empty()) {
        for (const NodeId node_id : clock_tree.buffer_nodes()) {
            if (clock_tree.node(node_id).origin != NodeOrigin::Inserted) {
                continue;
            }
            candidates.push_back(
                CandidateMove{CandidateMove::Kind::Remove, kInvalidEdgeId, node_id, -1});
            if (candidates.size() >= config.candidate_limit) {
                break;
            }
        }

        std::size_t resize_nodes = 0;
        for (const NodeId node_id : clock_tree.buffer_nodes()) {
            const auto& node = clock_tree.node(node_id);
            const int old_cell_idx = timing.cell_index(node.cell_type);
            if (old_cell_idx < 0) {
                continue;
            }
            ++resize_nodes;
            for (int cell_idx = 0; cell_idx < static_cast<int>(timing.cell_count()); ++cell_idx) {
                if (cell_idx == old_cell_idx ||
                    !timing.cell_supports_fanout(cell_idx, node.child_ids.size())) {
                    continue;
                }
                candidates.push_back(
                    CandidateMove{CandidateMove::Kind::Resize, kInvalidEdgeId, node_id, cell_idx});
                if (candidates.size() >= config.candidate_limit) {
                    break;
                }
            }
            if (candidates.size() >= config.candidate_limit ||
                resize_nodes >= config.resize_node_limit) {
                break;
            }
        }
    }

    return try_best_candidate(clock_tree, timing, buffer_library, baseline_metrics, candidates);
}

}  // namespace

void MilpOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                        const BufferLibrary& buffer_library, const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;
    DebugProgress& debug = context.debug_progress;
    const MilpConfig config = milp_config_from_environment();

    TimingState timing(clock_tree, data_path_graph, buffer_library);
    BestRunState best_state{clock_tree, timing.snapshot(), timing.metrics(),
                            timing.score(baseline_metrics)};

    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + config.time_budget;

    std::size_t rounds = 0;
    while (rounds < config.max_rounds && std::chrono::steady_clock::now() < deadline) {
        if (!apply_one_milp_round(clock_tree, timing, buffer_library, baseline_metrics, config)) {
            break;
        }
        ++rounds;
        maybe_update_best(clock_tree, timing, baseline_metrics, best_state);

        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
        debug.report_if_due(elapsed, best_state.metrics, baseline_metrics,
                            timing.score(baseline_metrics));
    }

    clock_tree = best_state.tree;

    debug.log([&](std::ostream& os) {
        os << "MilpOptimizer: rounds = " << rounds << ", best score = " << best_state.score << '\n';
    });
}

}  // namespace cadd0040
