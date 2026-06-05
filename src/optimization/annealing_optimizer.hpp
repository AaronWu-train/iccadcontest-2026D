/**
 * @file annealing_optimizer.hpp
 * @brief Simulated-annealing clock-tree optimizer.
 */

#pragma once

#include "optimization/optimizer.hpp"

namespace cadd0040 {

class AnnealingOptimizer : public Optimizer {
public:
    void run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
             const BufferLibrary& buffer_library, const OptimizerContext& context) override;
};

}  // namespace cadd0040
