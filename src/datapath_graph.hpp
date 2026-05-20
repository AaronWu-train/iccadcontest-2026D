/**
 * @file datapath_graph.hpp
 * @brief FF-to-FF data-path graph data model.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "types.hpp"

namespace cadd0040 {

// TODO: @BenLai95 - you can modify all the code in this file as needed, as long as the overall
// solver workflow is not affected.

struct DataPathEdge {
    EdgeId id = 0;
    std::string path_name;
    std::string launch_flip_flop_name;
    std::string capture_flip_flop_name;
    CornerDelay data_delay;
};

class DataPathGraph {
public:
    void clear();

    EdgeId add_edge(const std::string& path_name, const std::string& launch_flip_flop_name,
                    const std::string& capture_flip_flop_name);
    void set_delay(EdgeId edge_id, Corner corner, double delay);
    void set_delay(const std::string& path_name, Corner corner, double delay);

    void set_clock_period(double clock_period);
    double clock_period() const;
    double setup_time() const;
    double hold_time() const;

    bool contains_edge(const std::string& path_name) const;
    const DataPathEdge& edge(EdgeId edge_id) const;
    const DataPathEdge& edge(const std::string& path_name) const;
    const std::vector<DataPathEdge>& edges() const;
    std::size_t edge_count() const;

    std::vector<EdgeId> outgoing_edges(const std::string& launch_flip_flop_name) const;
    std::vector<EdgeId> incoming_edges(const std::string& capture_flip_flop_name) const;

private:
    TimingConstraint timing_constraint_;
    std::vector<DataPathEdge> edges_;
    std::unordered_map<std::string, EdgeId> path_name_to_id_;
    std::unordered_map<std::string, std::vector<EdgeId>> launch_to_edge_ids_;
    std::unordered_map<std::string, std::vector<EdgeId>> capture_to_edge_ids_;
};

}  // namespace cadd0040
