/**
 * @file evaluator.hpp
 * @brief Timing and score evaluation interface.
 */

#pragma once

#include "clock_tree.hpp"
#include "datapath_graph.hpp"
#include "types.hpp"

namespace cadd0040 {

class Evaluator {
public:
    Metrics evaluate(const ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                     const BufferLibrary& buffer_library) const;

    double score(const Metrics& metrics) const;
    bool is_better(const Metrics& before_metrics, const Metrics& after_metrics) const;
};

}  // namespace cadd0040
