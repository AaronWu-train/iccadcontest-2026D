/**
 * @file timing_state.hpp
 * @brief Incremental timing state for optimization-time clock-tree edits.
 */

#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "clock_tree.hpp"
#include "datapath_graph.hpp"
#include "evaluation.hpp"
#include "types.hpp"

namespace cadd0040 {

struct TimingCell {
    std::string name;
    double area = 0.0;
    std::vector<double> ss_delays_by_fanout;
    std::vector<double> ff_delays_by_fanout;
};

struct TimingPath {
    EdgeId id = 0;
    NodeId launch_ff = kInvalidNodeId;
    NodeId capture_ff = kInvalidNodeId;
    double data_delay_ss = 0.0;
    double data_delay_ff = 0.0;
};

struct TimingSnapshot {
    std::vector<double> arrival_ss;
    std::vector<double> arrival_ff;
    std::vector<double> ss_slack;
    std::vector<double> ff_slack;
    Metrics metrics;
};

class TimingState {
public:
    TimingState(const ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                const BufferLibrary& buffer_library);

    Metrics metrics() const { return metrics_; }
    double score(const Metrics& baseline) const;

    void apply(const ClockTreeEdit& edit);
    void undo(const ClockTreeEdit& edit);

    TimingSnapshot snapshot() const;
    void restore(const TimingSnapshot& snapshot);

    std::size_t path_count() const { return paths_.size(); }
    std::size_t cell_count() const { return cells_.size(); }
    const std::vector<TimingCell>& cells() const { return cells_; }
    const std::vector<TimingPath>& paths() const { return paths_; }
    const std::vector<double>& ss_slack() const { return ss_slack_; }
    const std::vector<double>& ff_slack() const { return ff_slack_; }

    int cell_index(const std::string& cell_name) const;
    bool cell_supports_fanout(int cell_idx, std::size_t fanout) const;
    double cell_area(int cell_idx) const;
    double cell_delay_ss(int cell_idx, std::size_t fanout) const;
    double cell_delay_ff(int cell_idx, std::size_t fanout) const;
    int smallest_cell_for_fanout(std::size_t fanout) const;
    std::vector<int> cells_for_fanout_by_area(std::size_t fanout) const;

private:
    void build_cells(const BufferLibrary& buffer_library);
    void bind_paths(const ClockTree& clock_tree, const DataPathGraph& data_path_graph);
    void resize_node_state(std::size_t node_count);
    void recompute_all_arrivals(const ClockTree& clock_tree);
    void recompute_all_slacks_and_metrics(const ClockTree& clock_tree);
    void update_path_slack(std::size_t path_idx);
    void apply_arrival_delta(const ClockTree& clock_tree, NodeId root_id, double delta_ss,
                             double delta_ff);
    void remove_negative_slack(double slack, Corner corner);
    void add_negative_slack(double slack, Corner corner);
    void recompute_wns(Corner corner);
    double node_buffer_delay_ss(const ClockTree& clock_tree, NodeId node_id) const;
    double node_buffer_delay_ff(const ClockTree& clock_tree, NodeId node_id) const;

    const ClockTree* clock_tree_ = nullptr;
    double clock_period_ = 0.0;
    double setup_time_ = 0.0;
    double hold_time_ = 0.0;

    std::vector<TimingCell> cells_;
    std::vector<TimingPath> paths_;
    std::vector<std::vector<std::size_t>> paths_as_launch_;
    std::vector<std::vector<std::size_t>> paths_as_capture_;

    std::vector<double> arrival_ss_;
    std::vector<double> arrival_ff_;
    std::vector<double> ss_slack_;
    std::vector<double> ff_slack_;
    Metrics metrics_;
    std::multiset<double> negative_ss_slacks_;
    std::multiset<double> negative_ff_slacks_;

    std::vector<NodeId> affected_ff_buffer_;
    std::vector<std::size_t> affected_path_epoch_;
    std::size_t path_epoch_ = 1;
};

}  // namespace cadd0040
