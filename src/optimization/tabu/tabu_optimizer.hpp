/**
 * @file tabu_optimizer.hpp
 * @brief A9 Tabu optimizer.
 */

#pragma once

#include <string>
#include <string_view>

#include "optimization/candidate_policy.hpp"
#include "optimization/optimizer.hpp"

namespace cadd0040 {

class TabuOptimizer : public Optimizer {
public:
    explicit TabuOptimizer(CandidatePolicy policy = CandidatePolicy::UnionPool,
                           std::string_view config_section = "tabu-union-pool",
                           std::string_view legacy_config_section = "tabu");

    void run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
             const BufferLibrary& buffer_library, const OptimizerContext& context) override;

private:
    CandidatePolicy policy_;
    std::string config_section_;
    std::string legacy_config_section_;
};

}  // namespace cadd0040
