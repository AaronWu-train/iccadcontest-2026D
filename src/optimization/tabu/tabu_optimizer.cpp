/**
 * @file tabu_optimizer.cpp
 * @brief A8 Tabu optimizer.
 */

#include "optimization/tabu/tabu_optimizer.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "optimization/optimizer_config.hpp"
#include "optimization/timing_state.hpp"

namespace cadd0040 {
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

std::string move_key(const CandidateMove& move) {
    switch (move.kind) {
        case CandidateMove::Kind::Insert:
            return "I:" + std::to_string(move.edge_id) + ":" + std::to_string(move.cell_idx);
        case CandidateMove::Kind::Remove:
            return "R:" + std::to_string(move.node_id);
        case CandidateMove::Kind::Resize:
            return "Z:" + std::to_string(move.node_id) + ":" + std::to_string(move.cell_idx);
    }
    return "unknown";
}

void dedupe_candidates(std::vector<CandidateMove>& candidates, std::size_t limit) {
    std::unordered_set<std::string> seen;
    std::vector<CandidateMove> deduped;
    deduped.reserve(std::min(candidates.size(), limit));
    for (const auto& candidate : candidates) {
        if (deduped.size() >= limit) {
            break;
        }
        if (seen.insert(move_key(candidate)).second) {
            deduped.push_back(candidate);
        }
    }
    candidates = std::move(deduped);
}

bool maybe_update_best(const ClockTree& clock_tree, const TimingState& timing,
                       const Metrics& baseline_metrics, BestRunState& best_state) {
    const double current_score = timing.score(baseline_metrics);
    if (current_score > best_state.score) {
        best_state.tree = clock_tree;
        best_state.timing = timing.snapshot();
        best_state.metrics = timing.metrics();
        best_state.score = current_score;
        return true;
    }
    return false;
}

OptimizerProgressEvent make_event(const std::chrono::steady_clock::time_point& start_time,
                                  std::size_t step, std::string phase, int round, std::string event,
                                  const TimingState& timing, const Metrics& baseline_metrics,
                                  const BestRunState& best_state, std::size_t accepted_moves,
                                  std::size_t rejected_moves, std::string candidate_policy,
                                  double delta_score = std::numeric_limits<double>::quiet_NaN()) {
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
    return OptimizerProgressEvent{step,
                                  elapsed,
                                  std::move(phase),
                                  round,
                                  std::move(event),
                                  timing.score(baseline_metrics),
                                  best_state.score,
                                  delta_score,
                                  timing.metrics(),
                                  accepted_moves,
                                  rejected_moves,
                                  std::move(candidate_policy)};
}

void record_trace(const OptimizerContext& context, const ClockTree& clock_tree,
                  const OptimizerProgressEvent& event, bool force) {
    context.maybe_record_progress(event, force);
    context.maybe_record_visual(clock_tree, event, force);
}

std::vector<std::size_t> sorted_violating_paths(const TimingState& timing) {
    std::vector<std::size_t> paths;
    paths.reserve(timing.path_count());
    for (std::size_t path_idx = 0; path_idx < timing.path_count(); ++path_idx) {
        if (timing.ss_slack()[path_idx] < 0.0 || timing.ff_slack()[path_idx] < 0.0) {
            paths.push_back(path_idx);
        }
    }
    std::sort(paths.begin(), paths.end(), [&](std::size_t lhs, std::size_t rhs) {
        const double lhs_violation =
            std::min(timing.ss_slack()[lhs], 0.0) + std::min(timing.ff_slack()[lhs], 0.0);
        const double rhs_violation =
            std::min(timing.ss_slack()[rhs], 0.0) + std::min(timing.ff_slack()[rhs], 0.0);
        return lhs_violation < rhs_violation;
    });
    return paths;
}

void append_insert_candidates_for_edge(const TimingState& timing, EdgeId edge,
                                       std::vector<CandidateMove>& candidates) {
    if (edge == kInvalidEdgeId) {
        return;
    }
    for (const int cell_idx : timing.cells_for_fanout_by_area(1)) {
        candidates.push_back(
            CandidateMove{CandidateMove::Kind::Insert, edge, kInvalidNodeId, cell_idx});
    }
}

void append_violation_path_candidates(const ClockTree& clock_tree, const TimingState& timing,
                                      std::size_t violation_sample_limit,
                                      std::vector<CandidateMove>& candidates) {
    const auto violating_paths = sorted_violating_paths(timing);
    const std::size_t sample_count =
        std::min<std::size_t>(violating_paths.size(), violation_sample_limit);
    for (std::size_t sample = 0; sample < sample_count; ++sample) {
        const std::size_t path_idx = violating_paths[sample];
        const auto& path = timing.paths()[path_idx];
        if (timing.ss_slack()[path_idx] < 0.0) {
            append_insert_candidates_for_edge(timing, incoming_edge(clock_tree, path.capture_ff),
                                              candidates);
        }
        if (timing.ff_slack()[path_idx] < 0.0) {
            append_insert_candidates_for_edge(timing, incoming_edge(clock_tree, path.launch_ff),
                                              candidates);
        }
    }
}

void append_critical_endpoint_candidates(const ClockTree& clock_tree, const TimingState& timing,
                                         std::size_t critical_endpoint_limit,
                                         std::vector<CandidateMove>& candidates) {
    std::unordered_map<NodeId, double> criticality;
    for (std::size_t path_idx = 0; path_idx < timing.path_count(); ++path_idx) {
        const auto& path = timing.paths()[path_idx];
        if (timing.ss_slack()[path_idx] < 0.0) {
            criticality[path.capture_ff] += timing.ss_slack()[path_idx];
        }
        if (timing.ff_slack()[path_idx] < 0.0) {
            criticality[path.launch_ff] += timing.ff_slack()[path_idx];
        }
    }

    std::vector<std::pair<NodeId, double>> ranked;
    ranked.reserve(criticality.size());
    for (const auto& [ff_id, score] : criticality) {
        ranked.emplace_back(ff_id, score);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second != rhs.second) {
            return lhs.second < rhs.second;
        }
        return lhs.first < rhs.first;
    });

    const std::size_t sample_count = std::min<std::size_t>(ranked.size(), critical_endpoint_limit);
    for (std::size_t idx = 0; idx < sample_count; ++idx) {
        append_insert_candidates_for_edge(timing, incoming_edge(clock_tree, ranked[idx].first),
                                          candidates);
    }
}

void append_upstream_window_from_node(const ClockTree& clock_tree, const TimingState& timing,
                                      NodeId node_id, std::size_t depth,
                                      std::vector<CandidateMove>& candidates) {
    NodeId current = node_id;
    for (std::size_t level = 0; level < depth && current != kInvalidNodeId; ++level) {
        const EdgeId edge = incoming_edge(clock_tree, current);
        if (edge == kInvalidEdgeId) {
            break;
        }
        append_insert_candidates_for_edge(timing, edge, candidates);
        current = clock_tree.node(current).parent_id;
    }
}

void append_upstream_window_candidates(const ClockTree& clock_tree, const TimingState& timing,
                                       std::size_t violation_sample_limit,
                                       std::size_t upstream_window_depth,
                                       std::vector<CandidateMove>& candidates) {
    const auto violating_paths = sorted_violating_paths(timing);
    const std::size_t sample_count =
        std::min<std::size_t>(violating_paths.size(), violation_sample_limit);
    for (std::size_t sample = 0; sample < sample_count; ++sample) {
        const std::size_t path_idx = violating_paths[sample];
        const auto& path = timing.paths()[path_idx];
        if (timing.ss_slack()[path_idx] < 0.0) {
            append_upstream_window_from_node(clock_tree, timing, path.capture_ff,
                                             upstream_window_depth, candidates);
        }
        if (timing.ff_slack()[path_idx] < 0.0) {
            append_upstream_window_from_node(clock_tree, timing, path.launch_ff,
                                             upstream_window_depth, candidates);
        }
    }
}

void append_remove_candidates(const ClockTree& clock_tree, std::size_t limit,
                              std::vector<CandidateMove>& candidates) {
    std::size_t count = 0;
    for (const NodeId node_id : clock_tree.buffer_nodes()) {
        if (clock_tree.node(node_id).origin != NodeOrigin::Inserted) {
            continue;
        }
        candidates.push_back(
            CandidateMove{CandidateMove::Kind::Remove, kInvalidEdgeId, node_id, -1});
        if (++count >= limit) {
            break;
        }
    }
}

void append_resize_candidates(const ClockTree& clock_tree, const TimingState& timing,
                              std::size_t node_limit, std::vector<CandidateMove>& candidates) {
    std::size_t tested_nodes = 0;
    for (const NodeId node_id : clock_tree.buffer_nodes()) {
        const auto& node = clock_tree.node(node_id);
        const int old_cell_idx = timing.cell_index(node.cell_type);
        if (old_cell_idx < 0) {
            continue;
        }
        ++tested_nodes;
        for (int cell_idx = 0; cell_idx < static_cast<int>(timing.cell_count()); ++cell_idx) {
            if (cell_idx == old_cell_idx ||
                !timing.cell_supports_fanout(cell_idx, node.child_ids.size())) {
                continue;
            }
            candidates.push_back(
                CandidateMove{CandidateMove::Kind::Resize, kInvalidEdgeId, node_id, cell_idx});
        }
        if (tested_nodes >= node_limit) {
            break;
        }
    }
}

bool tabu_active(const std::unordered_map<std::string, std::size_t>& tabu_until,
                 const std::string& key, std::size_t step) {
    const auto it = tabu_until.find(key);
    return it != tabu_until.end() && it->second > step;
}

}  // namespace

void TabuOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                        const BufferLibrary& buffer_library, const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;
    DebugProgress& debug = context.debug_progress;
    const TabuConfig config = tabu_config_from_sources(context.optimizer_config);
    TimingState timing(clock_tree, data_path_graph, buffer_library);
    BestRunState best_state{clock_tree, timing.snapshot(), timing.metrics(),
                            timing.score(baseline_metrics)};
    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + config.time_budget;

    std::unordered_map<std::string, std::size_t> tabu_until;
    std::deque<std::string> tabu_queue;
    std::size_t accepted_moves = 0;
    std::size_t rejected_moves = 0;
    double current_score = timing.score(baseline_metrics);

    record_trace(context, clock_tree,
                 make_event(start_time, 0, "baseline", -1, "kept", timing, baseline_metrics,
                            best_state, accepted_moves, rejected_moves, "tabu"),
                 true);

    for (std::size_t step = 0;
         step < config.max_steps && std::chrono::steady_clock::now() < deadline; ++step) {
        std::vector<CandidateMove> candidates;
        append_violation_path_candidates(clock_tree, timing, config.violation_sample_limit,
                                         candidates);
        append_critical_endpoint_candidates(clock_tree, timing, config.critical_endpoint_limit,
                                            candidates);
        append_upstream_window_candidates(clock_tree, timing, config.violation_sample_limit,
                                          config.upstream_window_depth, candidates);
        append_remove_candidates(clock_tree, config.removal_candidate_limit, candidates);
        append_resize_candidates(clock_tree, timing, config.resize_node_limit, candidates);
        dedupe_candidates(candidates, config.candidate_limit);

        bool found = false;
        CandidateMove best_move;
        double best_candidate_score = -std::numeric_limits<double>::infinity();
        for (const auto& candidate : candidates) {
            if (std::chrono::steady_clock::now() >= deadline) {
                break;
            }
            const ClockTreeEdit edit =
                apply_candidate(clock_tree, timing, buffer_library, candidate);
            if (!edit) {
                continue;
            }
            const double candidate_score = timing.score(baseline_metrics);
            const std::string key = move_key(candidate);
            const bool is_tabu = tabu_active(tabu_until, key, step);
            const bool aspiration = candidate_score > best_state.score;
            if ((!is_tabu || aspiration) && (!found || candidate_score > best_candidate_score)) {
                found = true;
                best_move = candidate;
                best_candidate_score = candidate_score;
            }
            undo_candidate(clock_tree, timing, edit);
        }

        if (!found) {
            break;
        }

        const double before_score = current_score;
        if (!apply_candidate(clock_tree, timing, buffer_library, best_move)) {
            ++rejected_moves;
            continue;
        }
        current_score = timing.score(baseline_metrics);
        ++accepted_moves;

        const std::string key = move_key(best_move);
        tabu_until[key] = step + config.tenure;
        tabu_queue.push_back(key);
        while (tabu_queue.size() > config.tenure * 2) {
            tabu_until.erase(tabu_queue.front());
            tabu_queue.pop_front();
        }

        const bool best_updated =
            maybe_update_best(clock_tree, timing, baseline_metrics, best_state);
        context.maybe_checkpoint(best_state.tree, accepted_moves);
        record_trace(context, best_state.tree,
                     make_event(start_time, accepted_moves, "tabu_search", -1,
                                best_updated ? "best_update" : "accepted", timing, baseline_metrics,
                                best_state, accepted_moves, rejected_moves, "tabu",
                                current_score - before_score),
                     best_updated);
    }

    clock_tree = best_state.tree;
    timing.restore(best_state.timing);
    context.write_checkpoint(best_state.tree);
    record_trace(context, best_state.tree,
                 make_event(start_time, accepted_moves, "final", -1, "final", timing,
                            baseline_metrics, best_state, accepted_moves, rejected_moves, "tabu"),
                 true);
    debug.log([&](std::ostream& os) {
        os << "TabuOptimizer: accepted = " << accepted_moves << ", rejected = " << rejected_moves
           << ", best score = " << best_state.score << '\n';
    });
}

}  // namespace cadd0040
