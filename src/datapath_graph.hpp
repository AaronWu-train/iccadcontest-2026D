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
    // Complexity notes:
    // E = number of data-path edges, F = number of distinct FF names in launch/capture maps.
    // unordered_map operations are average O(1), but worst-case O(E) or O(F) under heavy hash
    // collisions or rehashing.

    // Empty construction is intentional; parser.cpp populates the graph from SS/FF delay reports.
    // TODO: Consider adding an explicit constructor that accepts expected path/FF counts and
    // reserves the edge vector and lookup maps before parser insertion to reduce rehash overhead.
    // Note that in parser.cpp, building data path graph will clear the class first
    // Time: O(1).
    DataPathGraph() = default;
    // Time: O(E + F + total stored incident edge ids) to release owned containers.
    ~DataPathGraph() = default;

    // Clears all edges, lookup maps, and timing constraints.
    // Time: O(E + F + total stored incident edge ids).
    void clear();

    // Adds the FF-to-FF path identity only. Corner-specific delays can be filled later with
    // set_delay(), which matches the separate SS_delay.rpt and FF_delay.rpt input files.
    // Time: average O(1), excluding string copy/hash costs and occasional vector/map rehashing.
    EdgeId add_edge(const std::string& path_name, const std::string& launch_flip_flop_name,
                    const std::string& capture_flip_flop_name);
    // path_name must be the report path key, not a flip-flop name.
    // Time: average O(1), excluding string hash cost.
    void set_delay(const std::string& path_name, Corner corner, double delay);

    // Clock period comes from the delay report. setup_time() and hold_time() derive the fixed
    // contest FF library constraints from this value.
    // Time: O(1).
    void set_clock_period(double clock_period);
    // Time: O(1).
    double clock_period() const;
    // Time: O(1).
    double setup_time() const;
    // Time: O(1).
    double hold_time() const;

    // All path-name APIs use the report path key, e.g. "Path1".
    // Time: average O(1), excluding string hash cost.
    bool contains_edge(const std::string& path_name) const;
    // Time: average O(1), excluding string hash cost.
    const DataPathEdge& edge(const std::string& path_name) const;
    // Returns the owned edge array by reference; no edge data is copied.
    // Time: O(1).
    const std::vector<DataPathEdge>& all_edges() const;
    // Time: O(1).
    std::size_t edge_count() const;

    // FF-name lookup APIs use instance names from the report, e.g. "FF_5".
    // Returns a reference to the incident edge-id list; no vector is copied.
    // Time: average O(1), excluding string hash cost.
    const std::vector<EdgeId>& outgoing_edges(const std::string& launch_flip_flop_name) const;
    // Returns a reference to the incident edge-id list; no vector is copied.
    // Time: average O(1), excluding string hash cost.
    const std::vector<EdgeId>& incoming_edges(const std::string& capture_flip_flop_name) const;

private:
    // Time: O(1).
    void set_delay(EdgeId edge_id, Corner corner, double delay);
    // Time: O(1).
    const DataPathEdge& edge(EdgeId edge_id) const;

    TimingConstraint timing_constraint_;
    std::vector<DataPathEdge> edges_;
    std::vector<EdgeId> empty_edge_ids_;
    std::unordered_map<std::string, EdgeId> path_name_to_id_;
    // TODO: If FF instance names are guaranteed to be "FF_<number>", parse the numeric suffix once
    // and replace these FF-name maps with numeric-keyed maps to reduce string hashing overhead.
    std::unordered_map<std::string, std::vector<EdgeId>> launch_to_edge_ids_;
    std::unordered_map<std::string, std::vector<EdgeId>> capture_to_edge_ids_;
};

}  // namespace cadd0040
