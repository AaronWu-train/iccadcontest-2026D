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
    // Stable path key from SS_delay.rpt / FF_delay.rpt, e.g. "Path1".
    // Parser should use this to merge the SS and FF records for the same data path.
    std::string path_name;
    // Instance names from the delay report, e.g. "FF_5" and "FF_12".
    // Keep names here because the data-path parser does not need ClockTree NodeId knowledge.
    std::string launch_flip_flop_name;
    std::string capture_flip_flop_name;
    // SS delay is used by setup check; FF delay is used by hold check.
    CornerDelay data_delay;
};

class DataPathGraph {
public:
    // Empty construction is intentional; parser.cpp populates the graph from SS/FF delay reports.
    DataPathGraph() = default;
    ~DataPathGraph() = default;

    void clear();

    // Adds the FF-to-FF path identity only. Corner-specific delays can be filled later with
    // set_delay(), which matches the separate SS_delay.rpt and FF_delay.rpt input files.
    EdgeId add_edge(const std::string& path_name, const std::string& launch_flip_flop_name,
                    const std::string& capture_flip_flop_name);
    // edge_id is the id returned by add_edge() or one of the ids returned by incoming/outgoing
    // lookup APIs. corner selects whether delay updates data_delay.ss or data_delay.ff.
    void set_delay(EdgeId edge_id, Corner corner, double delay);
    // path_name must be the report path key, not a flip-flop name.
    void set_delay(const std::string& path_name, Corner corner, double delay);

    // Clock period comes from the delay report. setup_time() and hold_time() derive the fixed
    // contest FF library constraints from this value.
    void set_clock_period(double clock_period);
    double clock_period() const;
    double setup_time() const;
    double hold_time() const;

    // All path-name APIs use the report path key, e.g. "Path1".
    bool contains_edge(const std::string& path_name) const;
    const DataPathEdge& edge(EdgeId edge_id) const;
    const DataPathEdge& edge(const std::string& path_name) const;
    const std::vector<DataPathEdge>& edges() const;
    std::size_t edge_count() const;

    // FF-name lookup APIs use instance names from the report, e.g. "FF_5".
    std::vector<EdgeId> outgoing_edges(const std::string& launch_flip_flop_name) const;
    std::vector<EdgeId> incoming_edges(const std::string& capture_flip_flop_name) const;

private:
    TimingConstraint timing_constraint_;
    std::vector<DataPathEdge> edges_;
    std::unordered_map<std::string, EdgeId> path_name_to_id_;
    // TODO: If FF instance names are guaranteed to be "FF_<number>", parse the numeric suffix once
    // and replace these FF-name maps with numeric-keyed maps to reduce string hashing overhead.
    std::unordered_map<std::string, std::vector<EdgeId>> launch_to_edge_ids_;
    std::unordered_map<std::string, std::vector<EdgeId>> capture_to_edge_ids_;
};

}  // namespace cadd0040
