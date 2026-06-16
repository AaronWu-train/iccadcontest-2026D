/**
 * @file randomized_rcl_optimizer.cpp
 * @brief A7 Greedy-RandomizedRCL optimizer.
 */

#include "optimization/randomized_rcl/randomized_rcl_optimizer.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <random>
#include <string>
#include <unordered_set>
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

struct ScoredCandidate {
    CandidateMove move;
    double delta = 0.0;
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

void dedupe_candidates(std::vector<CandidateMove>& candidates) {
    std::unordered_set<std::string> seen;
    std::vector<CandidateMove> deduped;
    deduped.reserve(candidates.size());
    for (const auto& candidate : candidates) {
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
                              std::vector<CandidateMove>& candidates) {
    for (const NodeId node_id : clock_tree.buffer_nodes()) {
        const auto& node = clock_tree.node(node_id);
        const int old_cell_idx = timing.cell_index(node.cell_type);
        if (old_cell_idx < 0) {
            continue;
        }
        for (int cell_idx = 0; cell_idx < static_cast<int>(timing.cell_count()); ++cell_idx) {
            if (cell_idx == old_cell_idx ||
                !timing.cell_supports_fanout(cell_idx, node.child_ids.size())) {
                continue;
            }
            candidates.push_back(
                CandidateMove{CandidateMove::Kind::Resize, kInvalidEdgeId, node_id, cell_idx});
        }
    }
}

std::vector<ScoredCandidate> positive_candidates_by_score(
    ClockTree& clock_tree, TimingState& timing, const BufferLibrary& buffer_library,
    const Metrics& baseline_metrics, const std::vector<CandidateMove>& candidates,
    const std::chrono::steady_clock::time_point& deadline) {
    const double before_score = timing.score(baseline_metrics);
    std::vector<ScoredCandidate> scored;
    for (const auto& candidate : candidates) {
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        const ClockTreeEdit edit = apply_candidate(clock_tree, timing, buffer_library, candidate);
        if (!edit) {
            continue;
        }
        const double delta = timing.score(baseline_metrics) - before_score;
        if (delta > 0.0) {
            scored.push_back(ScoredCandidate{candidate, delta});
        }
        undo_candidate(clock_tree, timing, edit);
    }
    std::sort(scored.begin(), scored.end(),
              [](const ScoredCandidate& lhs, const ScoredCandidate& rhs) {
                  return lhs.delta > rhs.delta;
              });
    return scored;
}

bool run_resize_polish_step(ClockTree& clock_tree, TimingState& timing,
                            const BufferLibrary& buffer_library, const Metrics& baseline_metrics,
                            const std::chrono::steady_clock::time_point& deadline,
                            double& delta_score) {
    std::vector<CandidateMove> candidates;
    append_resize_candidates(clock_tree, timing, candidates);
    dedupe_candidates(candidates);
    auto scored = positive_candidates_by_score(clock_tree, timing, buffer_library, baseline_metrics,
                                               candidates, deadline);
    if (scored.empty()) {
        return false;
    }
    delta_score = scored.front().delta;
    return static_cast<bool>(
        apply_candidate(clock_tree, timing, buffer_library, scored.front().move));
}

}  // namespace

void GreedyRandomizedRclOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                                       const BufferLibrary& buffer_library,
                                       const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;
    DebugProgress& debug = context.debug_progress;
    const RandomizedRclConfig config = randomized_rcl_config_from_sources(context.optimizer_config);
    const ClockTree base_tree = clock_tree;
    TimingState initial_timing(clock_tree, data_path_graph, buffer_library);
    BestRunState best_state{clock_tree, initial_timing.snapshot(), initial_timing.metrics(),
                            initial_timing.score(baseline_metrics)};
    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + config.time_budget;
    std::mt19937 rng(config.seed);

    std::size_t accepted_moves = 0;
    std::size_t rejected_moves = 0;
    record_trace(context, clock_tree,
                 make_event(start_time, 0, "baseline", -1, "kept", initial_timing, baseline_metrics,
                            best_state, accepted_moves, rejected_moves, "randomized_rcl"),
                 true);

    for (std::size_t restart = 0;
         restart < config.restart_count && std::chrono::steady_clock::now() < deadline; ++restart) {
        clock_tree = base_tree;
        TimingState timing(clock_tree, data_path_graph, buffer_library);
        record_trace(context, clock_tree,
                     make_event(start_time, accepted_moves, "rcl_restart",
                                static_cast<int>(restart), "phase_start", timing, baseline_metrics,
                                best_state, accepted_moves, rejected_moves, "randomized_rcl"),
                     true);
        for (std::size_t step = 0;
             step < config.steps_per_restart && std::chrono::steady_clock::now() < deadline;
             ++step) {
            std::vector<CandidateMove> candidates;
            append_violation_path_candidates(clock_tree, timing, config.violation_sample_limit,
                                             candidates);
            append_remove_candidates(clock_tree, config.removal_candidate_limit, candidates);
            dedupe_candidates(candidates);
            auto scored = positive_candidates_by_score(clock_tree, timing, buffer_library,
                                                       baseline_metrics, candidates, deadline);
            if (scored.empty()) {
                break;
            }
            const std::size_t pick_limit = std::min<std::size_t>(scored.size(), config.top_k);
            std::uniform_int_distribution<std::size_t> dist(0, pick_limit - 1);
            const ScoredCandidate chosen = scored[dist(rng)];
            if (!apply_candidate(clock_tree, timing, buffer_library, chosen.move)) {
                ++rejected_moves;
                continue;
            }
            ++accepted_moves;
            const bool best_updated =
                maybe_update_best(clock_tree, timing, baseline_metrics, best_state);
            context.maybe_checkpoint(best_state.tree, accepted_moves);
            record_trace(
                context, best_state.tree,
                make_event(start_time, accepted_moves, "rcl_construct", static_cast<int>(restart),
                           best_updated ? "best_update" : "accepted", timing, baseline_metrics,
                           best_state, accepted_moves, rejected_moves, "randomized_rcl",
                           chosen.delta),
                best_updated);
        }
    }

    clock_tree = best_state.tree;
    TimingState timing(clock_tree, data_path_graph, buffer_library);
    for (std::size_t polish = 0;
         polish < config.final_resize_polish_steps && std::chrono::steady_clock::now() < deadline;
         ++polish) {
        double delta = 0.0;
        if (!run_resize_polish_step(clock_tree, timing, buffer_library, baseline_metrics, deadline,
                                    delta)) {
            break;
        }
        ++accepted_moves;
        const bool best_updated =
            maybe_update_best(clock_tree, timing, baseline_metrics, best_state);
        context.maybe_checkpoint(best_state.tree, accepted_moves);
        record_trace(context, best_state.tree,
                     make_event(start_time, accepted_moves, "resize_polish", -1,
                                best_updated ? "best_update" : "accepted", timing, baseline_metrics,
                                best_state, accepted_moves, rejected_moves, "resize", delta),
                     best_updated);
    }

    clock_tree = best_state.tree;
    timing.restore(best_state.timing);
    context.write_checkpoint(best_state.tree);
    record_trace(
        context, best_state.tree,
        make_event(start_time, accepted_moves, "final", -1, "final", timing, baseline_metrics,
                   best_state, accepted_moves, rejected_moves, "randomized_rcl"),
        true);
    debug.log([&](std::ostream& os) {
        os << "GreedyRandomizedRclOptimizer: accepted = " << accepted_moves
           << ", rejected = " << rejected_moves << ", best score = " << best_state.score << '\n';
    });
}

}  // namespace cadd0040
