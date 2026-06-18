/**
 * @file timing_state.cpp
 * @brief Incremental timing state implementation.
 */

#include "optimization/timing_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cadd0040 {
namespace {

double buffer_area(const BufferCell& cell) {
    return cell.area != 0.0 ? cell.area : cell.width * cell.height;
}

}  // namespace

TimingState::TimingState(const ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                         const BufferLibrary& buffer_library)
    : clock_tree_(&clock_tree),
      clock_period_(data_path_graph.clock_period()),
      setup_time_(data_path_graph.setup_time()),
      hold_time_(data_path_graph.hold_time()) {
    build_cells(buffer_library);
    bind_paths(clock_tree, data_path_graph);
    resize_node_state(clock_tree.nodes().size());
    recompute_all_arrivals(clock_tree);
    recompute_all_slacks_and_metrics(clock_tree);
}

double TimingState::score(const Metrics& baseline) const {
    return cadd0040::score(metrics_, baseline);
}

void TimingState::build_cells(const BufferLibrary& buffer_library) {
    cells_.clear();
    cells_.reserve(buffer_library.size());
    for (const auto& [name, cell] : buffer_library) {
        cells_.push_back(TimingCell{name, buffer_area(cell), cell.ss_delays_by_fanout,
                                    cell.ff_delays_by_fanout});
    }
}

void TimingState::bind_paths(const ClockTree& clock_tree, const DataPathGraph& data_path_graph) {
    paths_.clear();
    paths_.reserve(data_path_graph.edge_count());
    paths_as_launch_.assign(clock_tree.nodes().size(), {});
    paths_as_capture_.assign(clock_tree.nodes().size(), {});

    for (const auto& edge : data_path_graph.all_edges()) {
        const NodeId launch = clock_tree.node_id(edge.launch_flip_flop_name);
        const NodeId capture = clock_tree.node_id(edge.capture_flip_flop_name);
        if (!clock_tree.contains_node_id(launch) || !clock_tree.contains_node_id(capture)) {
            throw std::runtime_error("Data path references unknown flip-flop");
        }
        const std::size_t path_idx = paths_.size();
        paths_.push_back(
            TimingPath{edge.id, launch, capture, edge.data_delay.ss, edge.data_delay.ff});
        paths_as_launch_[launch].push_back(path_idx);
        paths_as_capture_[capture].push_back(path_idx);
    }
    ss_slack_.assign(paths_.size(), 0.0);
    ff_slack_.assign(paths_.size(), 0.0);
    affected_path_epoch_.assign(paths_.size(), 0);
}

void TimingState::resize_node_state(std::size_t node_count) {
    if (arrival_ss_.size() < node_count) {
        arrival_ss_.resize(node_count, 0.0);
        arrival_ff_.resize(node_count, 0.0);
    }
    if (paths_as_launch_.size() < node_count) {
        paths_as_launch_.resize(node_count);
        paths_as_capture_.resize(node_count);
    }
}

int TimingState::cell_index(const std::string& cell_name) const {
    for (std::size_t idx = 0; idx < cells_.size(); ++idx) {
        if (cells_[idx].name == cell_name) {
            return static_cast<int>(idx);
        }
    }
    return -1;
}

bool TimingState::cell_supports_fanout(int cell_idx, std::size_t fanout) const {
    if (cell_idx < 0 || static_cast<std::size_t>(cell_idx) >= cells_.size() || fanout == 0) {
        return false;
    }
    const auto& cell = cells_[static_cast<std::size_t>(cell_idx)];
    return fanout <= cell.ss_delays_by_fanout.size() && fanout <= cell.ff_delays_by_fanout.size();
}

double TimingState::cell_area(int cell_idx) const {
    return cells_[static_cast<std::size_t>(cell_idx)].area;
}

double TimingState::cell_delay_ss(int cell_idx, std::size_t fanout) const {
    return cells_[static_cast<std::size_t>(cell_idx)].ss_delays_by_fanout[fanout - 1];
}

double TimingState::cell_delay_ff(int cell_idx, std::size_t fanout) const {
    return cells_[static_cast<std::size_t>(cell_idx)].ff_delays_by_fanout[fanout - 1];
}

int TimingState::smallest_cell_for_fanout(std::size_t fanout) const {
    int best_idx = -1;
    double best_area = std::numeric_limits<double>::infinity();
    for (int cell_idx = 0; cell_idx < static_cast<int>(cells_.size()); ++cell_idx) {
        if (!cell_supports_fanout(cell_idx, fanout)) {
            continue;
        }
        const double area = cell_area(cell_idx);
        if (area < best_area) {
            best_area = area;
            best_idx = cell_idx;
        }
    }
    return best_idx;
}

std::vector<int> TimingState::cells_for_fanout_by_area(std::size_t fanout) const {
    std::vector<int> cell_indices;
    for (int cell_idx = 0; cell_idx < static_cast<int>(cells_.size()); ++cell_idx) {
        if (cell_supports_fanout(cell_idx, fanout)) {
            cell_indices.push_back(cell_idx);
        }
    }
    std::sort(cell_indices.begin(), cell_indices.end(), [&](int lhs, int rhs) {
        const auto& lhs_cell = cells_[static_cast<std::size_t>(lhs)];
        const auto& rhs_cell = cells_[static_cast<std::size_t>(rhs)];
        if (lhs_cell.area != rhs_cell.area) {
            return lhs_cell.area < rhs_cell.area;
        }
        return lhs_cell.name < rhs_cell.name;
    });
    return cell_indices;
}

double TimingState::node_buffer_delay_ss(const ClockTree& clock_tree, NodeId node_id) const {
    const auto& node = clock_tree.node(node_id);
    if (!node.alive || node.kind != NodeKind::Buffer) {
        return 0.0;
    }
    const int cell_idx = cell_index(node.cell_type);
    const std::size_t fanout = node.child_ids.size();
    return cell_supports_fanout(cell_idx, fanout) ? cell_delay_ss(cell_idx, fanout) : 0.0;
}

double TimingState::node_buffer_delay_ff(const ClockTree& clock_tree, NodeId node_id) const {
    const auto& node = clock_tree.node(node_id);
    if (!node.alive || node.kind != NodeKind::Buffer) {
        return 0.0;
    }
    const int cell_idx = cell_index(node.cell_type);
    const std::size_t fanout = node.child_ids.size();
    return cell_supports_fanout(cell_idx, fanout) ? cell_delay_ff(cell_idx, fanout) : 0.0;
}

void TimingState::recompute_all_arrivals(const ClockTree& clock_tree) {
    resize_node_state(clock_tree.nodes().size());
    std::fill(arrival_ss_.begin(), arrival_ss_.end(), 0.0);
    std::fill(arrival_ff_.begin(), arrival_ff_.end(), 0.0);

    if (clock_tree.empty()) {
        return;
    }

    const NodeId root = clock_tree.node_id(clock_tree.root_name());
    std::vector<NodeId> stack{root};
    while (!stack.empty()) {
        const NodeId node_id = stack.back();
        stack.pop_back();
        const auto& node = clock_tree.node(node_id);
        if (!node.alive) {
            continue;
        }

        double parent_ss = 0.0;
        double parent_ff = 0.0;
        if (node.parent_id != kInvalidNodeId) {
            parent_ss = arrival_ss_[node.parent_id];
            parent_ff = arrival_ff_[node.parent_id];
        }
        arrival_ss_[node_id] = parent_ss + node_buffer_delay_ss(clock_tree, node_id);
        arrival_ff_[node_id] = parent_ff + node_buffer_delay_ff(clock_tree, node_id);

        for (auto it = node.child_ids.rbegin(); it != node.child_ids.rend(); ++it) {
            if (clock_tree.is_alive(*it)) {
                stack.push_back(*it);
            }
        }
    }
}

void TimingState::remove_negative_slack(double slack, Corner corner) {
    if (slack >= 0.0) {
        return;
    }
    auto& slacks = corner == Corner::SS ? negative_ss_slacks_ : negative_ff_slacks_;
    auto it = slacks.find(slack);
    if (it != slacks.end()) {
        slacks.erase(it);
    }
    if (corner == Corner::SS) {
        metrics_.tns_ss -= slack;
    } else {
        metrics_.tns_ff -= slack;
    }
}

void TimingState::add_negative_slack(double slack, Corner corner) {
    if (slack >= 0.0) {
        return;
    }
    if (corner == Corner::SS) {
        negative_ss_slacks_.insert(slack);
        metrics_.tns_ss += slack;
    } else {
        negative_ff_slacks_.insert(slack);
        metrics_.tns_ff += slack;
    }
}

void TimingState::recompute_wns(Corner corner) {
    if (corner == Corner::SS) {
        metrics_.wns_ss = negative_ss_slacks_.empty() ? 0.0 : *negative_ss_slacks_.begin();
    } else {
        metrics_.wns_ff = negative_ff_slacks_.empty() ? 0.0 : *negative_ff_slacks_.begin();
    }
}

void TimingState::update_path_slack(std::size_t path_idx) {
    const double old_ss = ss_slack_[path_idx];
    const double old_ff = ff_slack_[path_idx];
    const auto& path = paths_[path_idx];
    const double skew_ss = arrival_ss_[path.capture_ff] - arrival_ss_[path.launch_ff];
    const double skew_ff = arrival_ff_[path.capture_ff] - arrival_ff_[path.launch_ff];

    const double new_ss = clock_period_ - setup_time_ - path.data_delay_ss + skew_ss;
    const double new_ff = path.data_delay_ff - hold_time_ - skew_ff;

    remove_negative_slack(old_ss, Corner::SS);
    remove_negative_slack(old_ff, Corner::FF);
    ss_slack_[path_idx] = new_ss;
    ff_slack_[path_idx] = new_ff;
    add_negative_slack(new_ss, Corner::SS);
    add_negative_slack(new_ff, Corner::FF);
}

void TimingState::recompute_all_slacks_and_metrics(const ClockTree& clock_tree) {
    negative_ss_slacks_.clear();
    negative_ff_slacks_.clear();
    metrics_ = {};

    for (const NodeId node_id : clock_tree.buffer_nodes()) {
        const int cell_idx = cell_index(clock_tree.node(node_id).cell_type);
        if (cell_idx >= 0) {
            metrics_.area += cell_area(cell_idx);
        }
    }

    for (std::size_t path_idx = 0; path_idx < paths_.size(); ++path_idx) {
        const auto& path = paths_[path_idx];
        const double skew_ss = arrival_ss_[path.capture_ff] - arrival_ss_[path.launch_ff];
        const double skew_ff = arrival_ff_[path.capture_ff] - arrival_ff_[path.launch_ff];
        ss_slack_[path_idx] = clock_period_ - setup_time_ - path.data_delay_ss + skew_ss;
        ff_slack_[path_idx] = path.data_delay_ff - hold_time_ - skew_ff;
        add_negative_slack(ss_slack_[path_idx], Corner::SS);
        add_negative_slack(ff_slack_[path_idx], Corner::FF);
    }

    recompute_wns(Corner::SS);
    recompute_wns(Corner::FF);
}

void TimingState::apply_arrival_delta(const ClockTree& clock_tree, NodeId root_id, double delta_ss,
                                      double delta_ff) {
    if (delta_ss == 0.0 && delta_ff == 0.0) {
        return;
    }
    resize_node_state(clock_tree.nodes().size());
    affected_ff_buffer_.clear();
    ++path_epoch_;
    if (path_epoch_ == 0) {
        std::fill(affected_path_epoch_.begin(), affected_path_epoch_.end(), 0);
        path_epoch_ = 1;
    }

    std::vector<NodeId> stack{root_id};
    while (!stack.empty()) {
        const NodeId node_id = stack.back();
        stack.pop_back();
        if (!clock_tree.is_alive(node_id)) {
            continue;
        }
        arrival_ss_[node_id] += delta_ss;
        arrival_ff_[node_id] += delta_ff;

        const auto& node = clock_tree.node(node_id);
        if (node.kind == NodeKind::FlipFlop) {
            affected_ff_buffer_.push_back(node_id);
        }
        for (auto it = node.child_ids.rbegin(); it != node.child_ids.rend(); ++it) {
            if (clock_tree.is_alive(*it)) {
                stack.push_back(*it);
            }
        }
    }

    for (const NodeId ff_id : affected_ff_buffer_) {
        for (const std::size_t path_idx : paths_as_launch_[ff_id]) {
            if (affected_path_epoch_[path_idx] != path_epoch_) {
                affected_path_epoch_[path_idx] = path_epoch_;
                update_path_slack(path_idx);
            }
        }
        for (const std::size_t path_idx : paths_as_capture_[ff_id]) {
            if (affected_path_epoch_[path_idx] != path_epoch_) {
                affected_path_epoch_[path_idx] = path_epoch_;
                update_path_slack(path_idx);
            }
        }
    }
    recompute_wns(Corner::SS);
    recompute_wns(Corner::FF);
}

void TimingState::apply(const ClockTreeEdit& edit) {
    if (!edit || clock_tree_ == nullptr) {
        return;
    }
    const ClockTree& clock_tree = *clock_tree_;
    resize_node_state(clock_tree.nodes().size());

    switch (edit.kind) {
        case ClockTreeEdit::Kind::InsertBuffer: {
            const int cell_idx = cell_index(edit.new_cell_type);
            arrival_ss_[edit.buffer_id] = arrival_ss_[edit.parent_id] + cell_delay_ss(cell_idx, 1);
            arrival_ff_[edit.buffer_id] = arrival_ff_[edit.parent_id] + cell_delay_ff(cell_idx, 1);
            metrics_.area += cell_area(cell_idx);
            apply_arrival_delta(clock_tree, edit.child_id, cell_delay_ss(cell_idx, 1),
                                cell_delay_ff(cell_idx, 1));
            return;
        }
        case ClockTreeEdit::Kind::ResizeBuffer: {
            const auto& node = clock_tree.node(edit.buffer_id);
            const std::size_t fanout = node.child_ids.size();
            const int old_cell_idx = cell_index(edit.old_cell_type);
            const int new_cell_idx = cell_index(edit.new_cell_type);
            metrics_.area -= cell_area(old_cell_idx);
            metrics_.area += cell_area(new_cell_idx);
            apply_arrival_delta(
                clock_tree, edit.buffer_id,
                cell_delay_ss(new_cell_idx, fanout) - cell_delay_ss(old_cell_idx, fanout),
                cell_delay_ff(new_cell_idx, fanout) - cell_delay_ff(old_cell_idx, fanout));
            return;
        }
        case ClockTreeEdit::Kind::RemoveInsertedBuffer: {
            const int cell_idx = cell_index(edit.old_cell_type);
            metrics_.area -= cell_area(cell_idx);
            apply_arrival_delta(clock_tree, edit.child_id, -cell_delay_ss(cell_idx, 1),
                                -cell_delay_ff(cell_idx, 1));
            return;
        }
        case ClockTreeEdit::Kind::None:
            return;
    }
}

void TimingState::undo(const ClockTreeEdit& edit) {
    if (!edit || clock_tree_ == nullptr) {
        return;
    }
    const ClockTree& clock_tree = *clock_tree_;
    resize_node_state(clock_tree.nodes().size());

    switch (edit.kind) {
        case ClockTreeEdit::Kind::InsertBuffer: {
            const int cell_idx = cell_index(edit.new_cell_type);
            metrics_.area -= cell_area(cell_idx);
            apply_arrival_delta(clock_tree, edit.child_id, -cell_delay_ss(cell_idx, 1),
                                -cell_delay_ff(cell_idx, 1));
            arrival_ss_[edit.buffer_id] = 0.0;
            arrival_ff_[edit.buffer_id] = 0.0;
            return;
        }
        case ClockTreeEdit::Kind::ResizeBuffer: {
            const auto& node = clock_tree.node(edit.buffer_id);
            const std::size_t fanout = node.child_ids.size();
            const int old_cell_idx = cell_index(edit.old_cell_type);
            const int new_cell_idx = cell_index(edit.new_cell_type);
            metrics_.area += cell_area(old_cell_idx);
            metrics_.area -= cell_area(new_cell_idx);
            apply_arrival_delta(
                clock_tree, edit.buffer_id,
                cell_delay_ss(old_cell_idx, fanout) - cell_delay_ss(new_cell_idx, fanout),
                cell_delay_ff(old_cell_idx, fanout) - cell_delay_ff(new_cell_idx, fanout));
            return;
        }
        case ClockTreeEdit::Kind::RemoveInsertedBuffer: {
            const int cell_idx = cell_index(edit.old_cell_type);
            metrics_.area += cell_area(cell_idx);
            arrival_ss_[edit.buffer_id] = arrival_ss_[edit.parent_id] + cell_delay_ss(cell_idx, 1);
            arrival_ff_[edit.buffer_id] = arrival_ff_[edit.parent_id] + cell_delay_ff(cell_idx, 1);
            apply_arrival_delta(clock_tree, edit.child_id, cell_delay_ss(cell_idx, 1),
                                cell_delay_ff(cell_idx, 1));
            return;
        }
        case ClockTreeEdit::Kind::None:
            return;
    }
}

TimingSnapshot TimingState::snapshot() const {
    return TimingSnapshot{arrival_ss_, arrival_ff_, ss_slack_, ff_slack_, metrics_};
}

void TimingState::restore(const TimingSnapshot& snapshot) {
    arrival_ss_ = snapshot.arrival_ss;
    arrival_ff_ = snapshot.arrival_ff;
    ss_slack_ = snapshot.ss_slack;
    ff_slack_ = snapshot.ff_slack;
    metrics_ = Metrics{0.0, 0.0, 0.0, 0.0, snapshot.metrics.area};

    negative_ss_slacks_.clear();
    negative_ff_slacks_.clear();
    for (std::size_t path_idx = 0; path_idx < ss_slack_.size(); ++path_idx) {
        add_negative_slack(ss_slack_[path_idx], Corner::SS);
        add_negative_slack(ff_slack_[path_idx], Corner::FF);
    }
    recompute_wns(Corner::SS);
    recompute_wns(Corner::FF);
}

}  // namespace cadd0040
