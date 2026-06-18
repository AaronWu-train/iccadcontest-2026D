/**
 * @file iterated_sa_optimizer.hpp
 * @brief Multi-round SA + Greedy clock-tree optimizer.
 */

#pragma once

#include <string>
#include <string_view>

#include "optimization/candidate_policy.hpp"
#include "optimization/optimizer.hpp"

namespace cadd0040 {

class IteratedSaOptimizer : public Optimizer {
public:
    explicit IteratedSaOptimizer(
        CandidatePolicy proposal_policy = CandidatePolicy::SampledUnionPool,
        std::string_view config_section = "isa-sampled-union-pool",
        std::string_view legacy_config_section = "isa");

    void run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
             const BufferLibrary& buffer_library, const OptimizerContext& context) override;

private:
    CandidatePolicy proposal_policy_;
    std::string config_section_;
    std::string legacy_config_section_;
};

}  // namespace cadd0040
