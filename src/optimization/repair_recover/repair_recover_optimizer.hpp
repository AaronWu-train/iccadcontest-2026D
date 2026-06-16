/**
 * @file repair_recover_optimizer.hpp
 * @brief A6 Greedy-RepairRecover optimizer.
 */

#pragma once

#include "optimization/optimizer.hpp"

namespace cadd0040 {

class GreedyRepairRecoverOptimizer : public Optimizer {
public:
    void run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
             const BufferLibrary& buffer_library, const OptimizerContext& context) override;
};

}  // namespace cadd0040
