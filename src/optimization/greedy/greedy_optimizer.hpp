/**
 * @file greedy_optimizer.hpp
 * @brief Deterministic greedy clock-tree optimizer.
 */

#pragma once

#include "optimization/candidate_policy.hpp"
#include "optimization/optimizer.hpp"

namespace cadd0040 {

class GreedyOptimizer : public Optimizer {
public:
    explicit GreedyOptimizer(CandidatePolicy policy = CandidatePolicy::ViolationPath);

    void run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
             const BufferLibrary& buffer_library, const OptimizerContext& context) override;

private:
    CandidatePolicy policy_;
};

}  // namespace cadd0040
