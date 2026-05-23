/**
 * @file evaluation.hpp
 * @brief Timing and score evaluation interface.
 */

#pragma once

#include <ostream>

#include "clock_tree.hpp"
#include "datapath_graph.hpp"
#include "types.hpp"

namespace cadd0040 {

struct Metrics {
    double tns_ss = 0.0;  // <= 0.0
    double wns_ss = 0.0;  // <= 0.0
    double tns_ff = 0.0;  // <= 0.0
    double wns_ff = 0.0;  // <= 0.0
    double area = 0.0;
};

std::ostream& operator<<(std::ostream& os, const Metrics& metrics);

Metrics evaluate(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                 const BufferLibrary& buffer_library);

double score(const Metrics& metrics, const Metrics& baseline);

}  // namespace cadd0040
