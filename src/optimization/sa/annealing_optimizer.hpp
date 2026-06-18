/**
 * @file annealing_optimizer.hpp
 * @brief Simulated-annealing clock-tree optimizer.
 */

#pragma once

#include <string>
#include <string_view>

#include "optimization/candidate_policy.hpp"
#include "optimization/optimizer.hpp"

namespace cadd0040 {

class AnnealingOptimizer : public Optimizer {
public:
    explicit AnnealingOptimizer(CandidatePolicy proposal_policy = CandidatePolicy::SampledUnionPool,
                                std::string_view config_section = "sa-sampled-union-pool",
                                std::string_view legacy_config_section = "sa");

    void run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
             const BufferLibrary& buffer_library, const OptimizerContext& context) override;

private:
    CandidatePolicy proposal_policy_;
    std::string config_section_;
    std::string legacy_config_section_;
};

}  // namespace cadd0040
