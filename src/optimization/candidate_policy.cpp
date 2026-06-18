/**
 * @file candidate_policy.cpp
 * @brief Shared candidate action generation for optimizer policies.
 */

#include "optimization/candidate_policy.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cadd0040 {
namespace {

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
                                      const CandidatePolicyConfig& config,
                                      std::vector<CandidateMove>& candidates) {
    const auto violating_paths = sorted_violating_paths(timing);
    const std::size_t sample_count =
        std::min<std::size_t>(violating_paths.size(), config.violation_sample_limit);
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
                                       const CandidatePolicyConfig& config,
                                       std::vector<CandidateMove>& candidates) {
    const auto violating_paths = sorted_violating_paths(timing);
    const std::size_t sample_count =
        std::min<std::size_t>(violating_paths.size(), config.violation_sample_limit);
    for (std::size_t sample = 0; sample < sample_count; ++sample) {
        const std::size_t path_idx = violating_paths[sample];
        const auto& path = timing.paths()[path_idx];
        if (timing.ss_slack()[path_idx] < 0.0) {
            append_upstream_window_from_node(clock_tree, timing, path.capture_ff,
                                             config.upstream_window_depth, candidates);
        }
        if (timing.ff_slack()[path_idx] < 0.0) {
            append_upstream_window_from_node(clock_tree, timing, path.launch_ff,
                                             config.upstream_window_depth, candidates);
        }
    }
}

void append_critical_endpoint_candidates(const ClockTree& clock_tree, const TimingState& timing,
                                         const CandidatePolicyConfig& config,
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

    const std::size_t sample_count =
        std::min<std::size_t>(ranked.size(), config.critical_endpoint_limit);
    for (std::size_t idx = 0; idx < sample_count; ++idx) {
        append_insert_candidates_for_edge(timing, incoming_edge(clock_tree, ranked[idx].first),
                                          candidates);
    }
}

EdgeId random_edge(const ClockTree& clock_tree, std::mt19937& rng) {
    const auto edges = clock_tree.active_edge_ids();
    if (edges.empty()) {
        return kInvalidEdgeId;
    }
    std::uniform_int_distribution<std::size_t> dist(0, edges.size() - 1);
    return edges[dist(rng)];
}

NodeId random_buffer_node(const ClockTree& clock_tree, std::mt19937& rng) {
    const auto buffers = clock_tree.buffer_nodes();
    if (buffers.empty()) {
        return kInvalidNodeId;
    }
    std::uniform_int_distribution<std::size_t> dist(0, buffers.size() - 1);
    return buffers[dist(rng)];
}

NodeId random_inserted_buffer(const ClockTree& clock_tree, std::mt19937& rng) {
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
    return candidates[dist(rng)];
}

int random_insert_cell(const TimingState& timing, std::mt19937& rng) {
    const auto cells = timing.cells_for_fanout_by_area(1);
    if (cells.empty()) {
        return -1;
    }
    std::uniform_int_distribution<std::size_t> dist(0, cells.size() - 1);
    return cells[dist(rng)];
}

int random_valid_cell_for_buffer(const ClockTree& clock_tree, const TimingState& timing,
                                 NodeId node_id, std::mt19937& rng) {
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
    return valid_cells[dist(rng)];
}

CandidateMove random_action_space_move(const ClockTree& clock_tree, const TimingState& timing,
                                       std::mt19937& rng) {
    std::uniform_real_distribution<double> kind_dist(0.0, 1.0);
    const double roll = kind_dist(rng);
    if (roll < 0.40) {
        return CandidateMove{CandidateMove::Kind::Insert, random_edge(clock_tree, rng),
                             kInvalidNodeId, random_insert_cell(timing, rng)};
    }
    if (roll < 0.65) {
        return CandidateMove{CandidateMove::Kind::Remove, kInvalidEdgeId,
                             random_inserted_buffer(clock_tree, rng), -1};
    }
    const NodeId node_id = random_buffer_node(clock_tree, rng);
    return CandidateMove{CandidateMove::Kind::Resize, kInvalidEdgeId, node_id,
                         random_valid_cell_for_buffer(clock_tree, timing, node_id, rng)};
}

CandidateMove sampled_union_pool_move(const ClockTree& clock_tree, const TimingState& timing,
                                      const CandidatePolicyConfig& config, std::mt19937& rng) {
    std::uniform_real_distribution<double> family_dist(0.0, 1.0);
    const double roll = family_dist(rng);
    std::vector<CandidateMove> candidates;

    if (roll < 0.25) {
        append_violation_path_candidates(clock_tree, timing, config, candidates);
    } else if (roll < 0.45) {
        append_upstream_window_candidates(clock_tree, timing, config, candidates);
    } else if (roll < 0.60) {
        append_critical_endpoint_candidates(clock_tree, timing, config, candidates);
    } else if (roll < 0.80) {
        append_remove_candidates(clock_tree, config.removal_candidate_limit, candidates);
    } else {
        append_resize_candidates(clock_tree, timing, config.resize_node_limit, candidates);
    }

    dedupe_candidates(candidates, config.candidate_limit);
    if (candidates.empty()) {
        return random_action_space_move(clock_tree, timing, rng);
    }
    std::uniform_int_distribution<std::size_t> candidate_dist(0, candidates.size() - 1);
    return candidates[candidate_dist(rng)];
}

}  // namespace

const char* candidate_policy_name(CandidatePolicy policy) {
    switch (policy) {
        case CandidatePolicy::RandomActionSpace:
            return "random_action_space";
        case CandidatePolicy::ViolationPath:
            return "violation_path";
        case CandidatePolicy::UpstreamWindow:
            return "upstream_window";
        case CandidatePolicy::CriticalEndpoint:
            return "critical_endpoint";
        case CandidatePolicy::UnionPool:
            return "union_pool";
        case CandidatePolicy::SampledUnionPool:
            return "sampled_union_pool";
    }
    return "unknown";
}

const char* accept_policy_name(AcceptPolicy policy) {
    switch (policy) {
        case AcceptPolicy::BestScore:
            return "best_score";
        case AcceptPolicy::TwoStepSlackThenScore:
            return "two_step_slack_then_score";
        case AcceptPolicy::Metropolis:
            return "metropolis";
        case AcceptPolicy::IteratedMetropolis:
            return "iterated_metropolis";
        case AcceptPolicy::TabuBestNonTabu:
            return "tabu_best_non_tabu";
    }
    return "unknown";
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
    deduped.reserve(limit == 0 ? candidates.size() : std::min(candidates.size(), limit));
    for (const auto& candidate : candidates) {
        if (limit > 0 && deduped.size() >= limit) {
            break;
        }
        if (seen.insert(move_key(candidate)).second) {
            deduped.push_back(candidate);
        }
    }
    candidates = std::move(deduped);
}

void append_candidate_policy_moves(const ClockTree& clock_tree, const TimingState& timing,
                                   const CandidatePolicyConfig& config, CandidatePolicy policy,
                                   std::vector<CandidateMove>& candidates) {
    switch (policy) {
        case CandidatePolicy::RandomActionSpace: {
            std::mt19937 rng(2026u);
            for (std::size_t i = 0; i < config.random_candidate_limit; ++i) {
                candidates.push_back(random_action_space_move(clock_tree, timing, rng));
            }
            break;
        }
        case CandidatePolicy::ViolationPath:
            append_violation_path_candidates(clock_tree, timing, config, candidates);
            break;
        case CandidatePolicy::UpstreamWindow:
            append_upstream_window_candidates(clock_tree, timing, config, candidates);
            break;
        case CandidatePolicy::CriticalEndpoint:
            append_critical_endpoint_candidates(clock_tree, timing, config, candidates);
            break;
        case CandidatePolicy::UnionPool:
        case CandidatePolicy::SampledUnionPool:
            append_violation_path_candidates(clock_tree, timing, config, candidates);
            append_upstream_window_candidates(clock_tree, timing, config, candidates);
            append_critical_endpoint_candidates(clock_tree, timing, config, candidates);
            append_remove_candidates(clock_tree, config.removal_candidate_limit, candidates);
            append_resize_candidates(clock_tree, timing, config.resize_node_limit, candidates);
            break;
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

CandidateMove sample_candidate_policy_move(const ClockTree& clock_tree, const TimingState& timing,
                                           const CandidatePolicyConfig& config,
                                           CandidatePolicy policy, std::mt19937& rng) {
    if (policy == CandidatePolicy::SampledUnionPool || policy == CandidatePolicy::UnionPool) {
        return sampled_union_pool_move(clock_tree, timing, config, rng);
    }
    if (policy == CandidatePolicy::RandomActionSpace) {
        return random_action_space_move(clock_tree, timing, rng);
    }

    std::vector<CandidateMove> candidates;
    append_candidate_policy_moves(clock_tree, timing, config, policy, candidates);
    dedupe_candidates(candidates, config.candidate_limit);
    if (candidates.empty()) {
        return random_action_space_move(clock_tree, timing, rng);
    }
    std::uniform_int_distribution<std::size_t> candidate_dist(0, candidates.size() - 1);
    return candidates[candidate_dist(rng)];
}

}  // namespace cadd0040
