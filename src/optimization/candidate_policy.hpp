/**
 * @file candidate_policy.hpp
 * @brief Shared candidate action generation for optimizer policies.
 */

#pragma once

#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include "clock_tree.hpp"
#include "optimization/timing_state.hpp"
#include "types.hpp"

namespace cadd0040 {

enum class CandidatePolicy {
    RandomActionSpace,
    ViolationPath,
    UpstreamWindow,
    CriticalEndpoint,
    UnionPool,
    SampledUnionPool,
};

enum class AcceptPolicy {
    BestScore,
    TwoStepSlackThenScore,
    Metropolis,
    IteratedMetropolis,
    TabuBestNonTabu,
};

struct CandidatePolicyConfig {
    std::size_t random_candidate_limit = 512;
    std::size_t violation_sample_limit = 32;
    std::size_t critical_endpoint_limit = 32;
    std::size_t upstream_window_depth = 4;
    std::size_t removal_candidate_limit = 512;
    std::size_t resize_node_limit = 1024;
    std::size_t candidate_limit = 0;
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

const char* candidate_policy_name(CandidatePolicy policy);
const char* accept_policy_name(AcceptPolicy policy);

ClockTreeEdit apply_candidate(ClockTree& clock_tree, TimingState& timing,
                              const BufferLibrary& buffer_library, const CandidateMove& move);
void undo_candidate(ClockTree& clock_tree, TimingState& timing, const ClockTreeEdit& edit);

std::string move_key(const CandidateMove& move);
void dedupe_candidates(std::vector<CandidateMove>& candidates, std::size_t limit = 0);

void append_candidate_policy_moves(const ClockTree& clock_tree, const TimingState& timing,
                                   const CandidatePolicyConfig& config, CandidatePolicy policy,
                                   std::vector<CandidateMove>& candidates);
void append_remove_candidates(const ClockTree& clock_tree, std::size_t limit,
                              std::vector<CandidateMove>& candidates);
void append_resize_candidates(const ClockTree& clock_tree, const TimingState& timing,
                              std::size_t node_limit, std::vector<CandidateMove>& candidates);

CandidateMove sample_candidate_policy_move(const ClockTree& clock_tree, const TimingState& timing,
                                           const CandidatePolicyConfig& config,
                                           CandidatePolicy policy, std::mt19937& rng);

}  // namespace cadd0040
