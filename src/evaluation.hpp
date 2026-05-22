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

static Metrics baseline_metrics = {
    .tns_ss = -100.0,
    .wns_ss = -0.1,
    .tns_ff = -100.0,
    .wns_ff = -0.1,
    .area = 10000.0,
};

std::ostream& operator<<(std::ostream& os, const Metrics& metrics);

Metrics evaluate(const ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                 const BufferLibrary& buffer_library);

double score(const Metrics& metrics, const Metrics& baseline = baseline_metrics);

}  // namespace cadd0040
