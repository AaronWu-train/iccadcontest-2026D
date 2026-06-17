/**
 * @file two_step_optimizer.hpp
 * @brief A6 TwoStepOptimize optimizer.
 */

#pragma once

#include <string>
#include <string_view>

#include "optimization/candidate_policy.hpp"
#include "optimization/optimizer.hpp"

namespace cadd0040 {

class TwoStepOptimizeOptimizer : public Optimizer {
public:
    explicit TwoStepOptimizeOptimizer(CandidatePolicy policy = CandidatePolicy::UnionPool,
                                      std::string_view config_section = "two-step-union-pool",
                                      std::string_view legacy_config_section = "two-step-optimize");

    void run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
             const BufferLibrary& buffer_library, const OptimizerContext& context) override;

private:
    CandidatePolicy policy_;
    std::string config_section_;
    std::string legacy_config_section_;
};

}  // namespace cadd0040
