/**
 * @file a6_tabu_repair_recover_optimizer.hpp
 * @brief Hybrid A6 repair/recover optimizer with tabu move memory.
 */

#pragma once

#include "optimization/optimizer.hpp"

namespace cadd0040 {

class A6TabuRepairRecoverOptimizer : public Optimizer {
public:
    void run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
             const BufferLibrary& buffer_library, const OptimizerContext& context) override;
};

}  // namespace cadd0040
