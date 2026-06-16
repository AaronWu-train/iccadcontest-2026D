/**
 * @file policy_averaged_greedy_seed_tabu_optimizer.hpp
 * @brief Policy-averaged greedy seed followed by tabu search.
 */

#pragma once

#include "optimization/optimizer.hpp"

namespace cadd0040 {

class PolicyAveragedGreedySeedTabuOptimizer : public Optimizer {
public:
    void run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
             const BufferLibrary& buffer_library, const OptimizerContext& context) override;
};

}  // namespace cadd0040
