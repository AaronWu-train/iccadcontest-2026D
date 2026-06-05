/**
 * @file skew_model.hpp
 * @brief Incremental timing model for simulated-annealing clock-tree optimization.
 */

#pragma once

#include <cstddef>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "clock_tree.hpp"
#include "datapath_graph.hpp"
#include "evaluation.hpp"
#include "types.hpp"

namespace cadd0040 {

struct FlatBufferCell {
    std::string name;
    double area = 0.0;
    std::vector<double> ss_delays_by_fanout;
    std::vector<double> ff_delays_by_fanout;
};

struct TreeEdge {
    std::size_t parent_idx = 0;
    std::size_t child_idx = 0;
    std::vector<int> inserted_cell_indices;
};

struct SkewModelMetrics {
    double tns_ss = 0.0;
    double wns_ss = 0.0;
    double tns_ff = 0.0;
    double wns_ff = 0.0;
    double area = 0.0;
};

struct SkewModelState {
    std::vector<int> cell_indices;
    std::vector<std::vector<int>> edge_inserted_cells;
    std::vector<double> arrival_ss;
    std::vector<double> arrival_ff;
    std::vector<double> ss_slack;
    std::vector<double> ff_slack;
    SkewModelMetrics metrics;
};

enum class SkewMoveKind {
    Insert,
    Remove,
    Resize,
};

struct SkewMove {
    SkewMoveKind kind = SkewMoveKind::Insert;
    std::size_t edge_idx = 0;
    std::size_t node_idx = 0;
    int cell_idx = 0;
    int insert_position = 0;
    int old_cell_idx = -1;
};

class SkewModel {
public:
    SkewModel(const ClockTree& clock_tree, const DataPathGraph& data_path_graph,
              const BufferLibrary& buffer_library);

    double score(const Metrics& baseline) const;
    SkewModelMetrics metrics() const { return metrics_; }

    bool try_move(const SkewMove& move);
    void undo_move(const SkewMove& move);

    SkewModelState snapshot() const;
    void restore(const SkewModelState& state);

    std::size_t edge_count() const { return edges_.size(); }
    std::size_t node_count() const { return names_.size(); }
    std::size_t cell_count() const { return cells_.size(); }
    std::size_t path_count() const { return launch_idx_.size(); }

    const std::vector<std::string>& node_names() const { return names_; }
    const std::vector<TreeEdge>& tree_edges() const { return edges_; }
    const std::vector<int>& cell_indices() const { return cell_indices_; }
    const std::vector<int>& original_cell_indices() const { return original_cell_indices_; }
    const std::vector<FlatBufferCell>& cells() const { return cells_; }

    const std::vector<double>& ss_slack() const { return ss_slack_; }
    const std::vector<double>& ff_slack() const { return ff_slack_; }

    std::size_t random_edge_index() const;
    std::size_t random_buffer_node_index() const;
    int random_cell_index() const;
    int random_valid_cell_for_buffer(std::size_t node_idx) const;
    std::size_t random_edge_with_inserts() const;
    std::size_t random_guided_insert_edge() const;
    int smallest_fanout1_cell_index() const;
    int random_fanout1_cell_index() const;

    std::size_t descendant_ff_count(std::size_t node_idx) const;

    void apply_greedy_warmup(const Metrics& baseline_metrics, std::size_t max_iterations);
    bool apply_one_greedy_step(const Metrics& baseline_metrics);

private:
    void build_from_clock_tree(const ClockTree& clock_tree);
    void build_from_data_paths(const DataPathGraph& data_path_graph);
    void build_library(const BufferLibrary& buffer_library);
    void compute_descendant_ff_counts();
    void recompute_all_arrivals();
    void recompute_all_slacks_and_metrics();

    double node_buffer_delay_ss(std::size_t node_idx) const;
    double node_buffer_delay_ff(std::size_t node_idx) const;
    double edge_inserted_delay_ss(const TreeEdge& edge) const;
    double edge_inserted_delay_ff(const TreeEdge& edge) const;

    void apply_arrival_delta(std::size_t root_idx, double delta_ss, double delta_ff);
    void update_path_slack(std::size_t path_idx);
    void remove_negative_slack(double slack, Corner corner);
    void add_negative_slack(double slack, Corner corner);
    void recompute_wns(Corner corner);

    bool cell_supports_fanout(int cell_idx, std::size_t fanout) const;
    int lookup_cell_index(const std::string& cell_type) const;

    std::vector<std::string> names_;
    std::vector<NodeKind> kinds_;
    std::vector<std::size_t> parents_;
    std::vector<std::vector<std::size_t>> children_;
    std::vector<int> cell_indices_;
    std::vector<int> original_cell_indices_;
    std::vector<std::size_t> ff_descendant_counts_;

    std::vector<TreeEdge> edges_;
    std::vector<FlatBufferCell> cells_;

    std::vector<std::size_t> launch_idx_;
    std::vector<std::size_t> capture_idx_;
    std::vector<double> data_delay_ss_;
    std::vector<double> data_delay_ff_;
    std::vector<std::vector<std::size_t>> paths_as_launch_;
    std::vector<std::vector<std::size_t>> paths_as_capture_;

    double clock_period_ = 0.0;
    double setup_time_ = 0.0;
    double hold_time_ = 0.0;

    std::vector<double> arrival_ss_;
    std::vector<double> arrival_ff_;
    std::vector<double> ss_slack_;
    std::vector<double> ff_slack_;

    SkewModelMetrics metrics_;
    std::multiset<double> negative_ss_slacks_;
    std::multiset<double> negative_ff_slacks_;

    std::vector<std::size_t> affected_ff_buffer_;
    std::vector<std::size_t> affected_path_epoch_;
    std::size_t path_epoch_ = 1;
};

}  // namespace cadd0040
