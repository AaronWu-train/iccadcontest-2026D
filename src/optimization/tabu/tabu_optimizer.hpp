/**
 * @file tabu_optimizer.hpp
 * @brief A9 Tabu optimizer.
 */

#pragma once

#include "optimization/optimizer.hpp"

namespace cadd0040 {

class TabuOptimizer : public Optimizer {
public:
    void run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
             const BufferLibrary& buffer_library, const OptimizerContext& context) override;
};

}  // namespace cadd0040
