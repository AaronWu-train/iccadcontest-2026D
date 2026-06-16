/**
 * @file greedy_optimizer.hpp
 * @brief Deterministic greedy clock-tree optimizer.
 */

#pragma once

#include "optimization/optimizer.hpp"

namespace cadd0040 {

enum class GreedyCandidatePolicy {
    ViolationPath,
    CriticalEndpoint,
    UpstreamWindow,
};

class GreedyOptimizer : public Optimizer {
public:
    explicit GreedyOptimizer(GreedyCandidatePolicy policy = GreedyCandidatePolicy::ViolationPath);

    void run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
             const BufferLibrary& buffer_library, const OptimizerContext& context) override;

private:
    GreedyCandidatePolicy policy_;
};

}  // namespace cadd0040
