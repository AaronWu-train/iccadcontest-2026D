/**
 * @file optimizer.hpp
 * @brief Clock-tree optimization strategy interface.
 */

#pragma once

#include "../clock_tree.hpp"
#include "../datapath_graph.hpp"
#include "../evaluator.hpp"

namespace cadd0040 {

class Optimizer {
public:
    ClockTree optimize(const ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                       const Evaluator& evaluator) const;
};

}  // namespace cadd0040
