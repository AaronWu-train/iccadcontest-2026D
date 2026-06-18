/**
 * @file tabu_optimizer.cpp
 * @brief A9 Tabu optimizer.
 */

#include "optimization/tabu/tabu_optimizer.hpp"

#include <chrono>
#include <deque>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "optimization/candidate_policy.hpp"
#include "optimization/optimizer_config.hpp"
#include "optimization/timing_state.hpp"

namespace cadd0040 {
namespace {

struct BestRunState {
    ClockTree tree;
    TimingSnapshot timing;
    Metrics metrics;
    double score = -std::numeric_limits<double>::infinity();
};

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
                                  std::string accept_policy,
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
                                  std::move(candidate_policy),
                                  std::move(accept_policy)};
}

void record_trace(const OptimizerContext& context, const ClockTree& clock_tree,
                  const OptimizerProgressEvent& event, bool force) {
    context.maybe_record_progress(event, force);
    context.maybe_record_visual(clock_tree, event, force);
}

bool tabu_active(const std::unordered_map<std::string, std::size_t>& tabu_until,
                 const std::string& key, std::size_t step) {
    const auto it = tabu_until.find(key);
    return it != tabu_until.end() && it->second > step;
}

CandidatePolicyConfig candidate_config_from_tabu(const TabuConfig& config) {
    CandidatePolicyConfig candidate;
    candidate.random_candidate_limit = config.random_candidate_limit;
    candidate.violation_sample_limit = config.violation_sample_limit;
    candidate.critical_endpoint_limit = config.critical_endpoint_limit;
    candidate.upstream_window_depth = config.upstream_window_depth;
    candidate.removal_candidate_limit = config.removal_candidate_limit;
    candidate.resize_node_limit = config.resize_node_limit;
    candidate.candidate_limit = config.candidate_limit;
    return candidate;
}

void build_candidates(const ClockTree& clock_tree, const TimingState& timing,
                      const CandidatePolicyConfig& config, CandidatePolicy policy,
                      std::mt19937& rng, std::vector<CandidateMove>& candidates) {
    if (policy == CandidatePolicy::RandomActionSpace) {
        for (std::size_t i = 0; i < config.random_candidate_limit; ++i) {
            candidates.push_back(
                sample_candidate_policy_move(clock_tree, timing, config, policy, rng));
        }
        return;
    }
    append_candidate_policy_moves(clock_tree, timing, config, policy, candidates);
}

}  // namespace

TabuOptimizer::TabuOptimizer(CandidatePolicy policy, std::string_view config_section,
                             std::string_view legacy_config_section)
    : policy_(policy),
      config_section_(config_section),
      legacy_config_section_(legacy_config_section) {}

void TabuOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                        const BufferLibrary& buffer_library, const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;
    DebugProgress& debug = context.debug_progress;
    const TabuConfig config =
        tabu_config_from_sources(context.optimizer_config, config_section_, legacy_config_section_);
    const CandidatePolicyConfig candidate_config = candidate_config_from_tabu(config);
    const std::string candidate_policy = candidate_policy_name(policy_);
    const std::string accept_policy = accept_policy_name(AcceptPolicy::TabuBestNonTabu);
    std::mt19937 rng(config.seed);
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

    record_trace(
        context, clock_tree,
        make_event(start_time, 0, "baseline", -1, "kept", timing, baseline_metrics, best_state,
                   accepted_moves, rejected_moves, candidate_policy, accept_policy),
        true);

    for (std::size_t step = 0;
         step < config.max_steps && std::chrono::steady_clock::now() < deadline; ++step) {
        std::vector<CandidateMove> candidates;
        build_candidates(clock_tree, timing, candidate_config, policy_, rng, candidates);
        dedupe_candidates(candidates, candidate_config.candidate_limit);

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
                                best_state, accepted_moves, rejected_moves, candidate_policy,
                                accept_policy, current_score - before_score),
                     best_updated);
    }

    clock_tree = best_state.tree;
    timing.restore(best_state.timing);
    context.write_checkpoint(best_state.tree);
    record_trace(
        context, best_state.tree,
        make_event(start_time, accepted_moves, "final", -1, "final", timing, baseline_metrics,
                   best_state, accepted_moves, rejected_moves, candidate_policy, accept_policy),
        true);
    debug.log([&](std::ostream& os) {
        os << "TabuOptimizer(" << candidate_policy << ", " << accept_policy
           << "): accepted = " << accepted_moves << ", rejected = " << rejected_moves
           << ", best score = " << best_state.score << '\n';
    });
}

}  // namespace cadd0040
