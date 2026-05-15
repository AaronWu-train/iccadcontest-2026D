/**
 * @file datapath_graph.hpp
 * @brief FF-to-FF data-path graph data model.
 */

#pragma once

#include <string>
#include <vector>

#include "types.hpp"

namespace cadd0040 {

struct DataPathEdge {
    EdgeId id = 0;
    std::string path_name;
    NodeId launch_flip_flop_id = 0;
    NodeId capture_flip_flop_id = 0;
    double ss_delay = 0.0;
    double ff_delay = 0.0;
};

class DataPathGraph {
public:
    EdgeId add_edge(const std::string& path_name, NodeId launch_flip_flop_id,
                    NodeId capture_flip_flop_id, double ss_delay, double ff_delay);
    void set_clock_period(double clock_period);

    const std::vector<DataPathEdge>& edges() const;

private:
    double clock_period_ = 0.0;
    std::vector<DataPathEdge> edges_;
};

}  // namespace cadd0040
