/**
 * @file skew_model.cpp
 * @brief Incremental timing model implementation.
 */

#include "optimization/sa/skew_model.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <random>
#include <stdexcept>
#include <unordered_map>

namespace cadd0040 {
namespace {

constexpr double kScoreAlpha = 0.3;
constexpr double kScoreBeta = 0.3;
constexpr double kScoreGamma = 0.4;
constexpr double kZeroTolerance = 1e-12;

double safe_divide(double value, double baseline_value) {
    if (std::fabs(baseline_value) <= kZeroTolerance) {
        return 0.0;
    }
    return 1.0 - (value / baseline_value);
}

double cell_area(const FlatBufferCell& cell) { return cell.area; }

}  // namespace

SkewModel::SkewModel(const ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                     const BufferLibrary& buffer_library) {
    build_library(buffer_library);
    build_from_clock_tree(clock_tree);
    build_from_data_paths(data_path_graph);
    original_cell_indices_ = cell_indices_;
    compute_descendant_ff_counts();
    recompute_all_arrivals();
    recompute_all_slacks_and_metrics();
}

void SkewModel::build_library(const BufferLibrary& buffer_library) {
    cells_.clear();
    cells_.reserve(buffer_library.size());

    for (const auto& [name, cell] : buffer_library) {
        FlatBufferCell flat{name, cell.area != 0.0 ? cell.area : cell.width * cell.height,
                            cell.ss_delays_by_fanout, cell.ff_delays_by_fanout};
        cells_.push_back(std::move(flat));
    }
}

int SkewModel::lookup_cell_index(const std::string& cell_type) const {
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        if (cells_[i].name == cell_type) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void SkewModel::build_from_clock_tree(const ClockTree& clock_tree) {
    const auto& nodes = clock_tree.nodes();
    names_.resize(nodes.size());
    kinds_.resize(nodes.size());
    parents_.resize(nodes.size());
    children_.resize(nodes.size());
    cell_indices_.resize(nodes.size(), -1);
    arrival_ss_.resize(nodes.size(), 0.0);
    arrival_ff_.resize(nodes.size(), 0.0);

    std::unordered_map<std::string, std::size_t> name_to_idx;
    name_to_idx.reserve(nodes.size());

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        names_[i] = node.name;
        kinds_[i] = node.kind;
        parents_[i] = node.parent_id;
        children_[i] = node.child_ids;
        name_to_idx.emplace(node.name, i);

        if (node.kind == NodeKind::Buffer) {
            cell_indices_[i] = lookup_cell_index(node.cell_type);
            if (cell_indices_[i] < 0) {
                throw std::runtime_error("Unknown buffer cell in clock tree: " + node.cell_type);
            }
        }
    }

    edges_.clear();
    for (std::size_t parent_idx = 0; parent_idx < nodes.size(); ++parent_idx) {
        for (const std::size_t child_id : children_[parent_idx]) {
            edges_.push_back(TreeEdge{parent_idx, child_id, {}});
        }
    }
}

void SkewModel::build_from_data_paths(const DataPathGraph& data_path_graph) {
    clock_period_ = data_path_graph.clock_period();
    setup_time_ = data_path_graph.setup_time();
    hold_time_ = data_path_graph.hold_time();

    std::unordered_map<std::string, std::size_t> name_to_idx;
    for (std::size_t i = 0; i < names_.size(); ++i) {
        name_to_idx.emplace(names_[i], i);
    }

    const std::size_t path_count = data_path_graph.edge_count();
    launch_idx_.resize(path_count);
    capture_idx_.resize(path_count);
    data_delay_ss_.resize(path_count);
    data_delay_ff_.resize(path_count);
    ss_slack_.resize(path_count);
    ff_slack_.resize(path_count);

    paths_as_launch_.resize(names_.size());
    paths_as_capture_.resize(names_.size());

    for (const auto& edge : data_path_graph.all_edges()) {
        const std::size_t path_idx = edge.id;
        const auto launch_it = name_to_idx.find(edge.launch_flip_flop_name);
        const auto capture_it = name_to_idx.find(edge.capture_flip_flop_name);
        if (launch_it == name_to_idx.end() || capture_it == name_to_idx.end()) {
            throw std::runtime_error("Data path references unknown flip-flop");
        }

        launch_idx_[path_idx] = launch_it->second;
        capture_idx_[path_idx] = capture_it->second;
        data_delay_ss_[path_idx] = edge.data_delay.ss;
        data_delay_ff_[path_idx] = edge.data_delay.ff;

        paths_as_launch_[launch_it->second].push_back(path_idx);
        paths_as_capture_[capture_it->second].push_back(path_idx);
    }
}

void SkewModel::compute_descendant_ff_counts() {
    ff_descendant_counts_.assign(names_.size(), 0);

    std::function<std::size_t(std::size_t)> dfs = [&](std::size_t node_idx) -> std::size_t {
        std::size_t count = kinds_[node_idx] == NodeKind::FlipFlop ? 1 : 0;
        for (const std::size_t child_idx : children_[node_idx]) {
            count += dfs(child_idx);
        }
        ff_descendant_counts_[node_idx] = count;
        return count;
    };

    if (!names_.empty()) {
        dfs(0);
    }
}

bool SkewModel::cell_supports_fanout(int cell_idx, std::size_t fanout) const {
    if (cell_idx < 0 || static_cast<std::size_t>(cell_idx) >= cells_.size()) {
        return false;
    }
    const auto& cell = cells_[static_cast<std::size_t>(cell_idx)];
    return fanout <= cell.ss_delays_by_fanout.size() && fanout <= cell.ff_delays_by_fanout.size();
}

double SkewModel::node_buffer_delay_ss(std::size_t node_idx) const {
    if (kinds_[node_idx] != NodeKind::Buffer) {
        return 0.0;
    }
    const int cell_idx = cell_indices_[node_idx];
    const std::size_t fanout = children_[node_idx].size();
    if (fanout == 0 || !cell_supports_fanout(cell_idx, fanout)) {
        return 0.0;
    }
    return cells_[static_cast<std::size_t>(cell_idx)].ss_delays_by_fanout[fanout - 1];
}

double SkewModel::node_buffer_delay_ff(std::size_t node_idx) const {
    if (kinds_[node_idx] != NodeKind::Buffer) {
        return 0.0;
    }
    const int cell_idx = cell_indices_[node_idx];
    const std::size_t fanout = children_[node_idx].size();
    if (fanout == 0 || !cell_supports_fanout(cell_idx, fanout)) {
        return 0.0;
    }
    return cells_[static_cast<std::size_t>(cell_idx)].ff_delays_by_fanout[fanout - 1];
}

double SkewModel::edge_inserted_delay_ss(const TreeEdge& edge) const {
    double total = 0.0;
    for (const int cell_idx : edge.inserted_cell_indices) {
        if (cell_supports_fanout(cell_idx, 1)) {
            total += cells_[static_cast<std::size_t>(cell_idx)].ss_delays_by_fanout[0];
        }
    }
    return total;
}

double SkewModel::edge_inserted_delay_ff(const TreeEdge& edge) const {
    double total = 0.0;
    for (const int cell_idx : edge.inserted_cell_indices) {
        if (cell_supports_fanout(cell_idx, 1)) {
            total += cells_[static_cast<std::size_t>(cell_idx)].ff_delays_by_fanout[0];
        }
    }
    return total;
}

void SkewModel::recompute_all_arrivals() {
    std::fill(arrival_ss_.begin(), arrival_ss_.end(), 0.0);
    std::fill(arrival_ff_.begin(), arrival_ff_.end(), 0.0);

    std::vector<std::vector<const TreeEdge*>> incoming_edges(names_.size());
    for (const auto& edge : edges_) {
        incoming_edges[edge.child_idx].push_back(&edge);
    }

    std::vector<std::size_t> stack;
    stack.push_back(0);

    while (!stack.empty()) {
        const std::size_t node_idx = stack.back();
        stack.pop_back();

        double parent_ss = 0.0;
        double parent_ff = 0.0;
        if (parents_[node_idx] != std::numeric_limits<std::size_t>::max() &&
            parents_[node_idx] < names_.size()) {
            parent_ss = arrival_ss_[parents_[node_idx]];
            parent_ff = arrival_ff_[parents_[node_idx]];
        }

        double edge_ss = 0.0;
        double edge_ff = 0.0;
        for (const TreeEdge* edge : incoming_edges[node_idx]) {
            edge_ss += edge_inserted_delay_ss(*edge);
            edge_ff += edge_inserted_delay_ff(*edge);
        }

        arrival_ss_[node_idx] = parent_ss + edge_ss + node_buffer_delay_ss(node_idx);
        arrival_ff_[node_idx] = parent_ff + edge_ff + node_buffer_delay_ff(node_idx);

        for (auto it = children_[node_idx].rbegin(); it != children_[node_idx].rend(); ++it) {
            stack.push_back(*it);
        }
    }
}

void SkewModel::remove_negative_slack(double slack, Corner corner) {
    if (slack >= 0.0) {
        return;
    }
    if (corner == Corner::SS) {
        const auto it = negative_ss_slacks_.find(slack);
        if (it != negative_ss_slacks_.end()) {
            negative_ss_slacks_.erase(it);
        }
        metrics_.tns_ss -= slack;
    } else {
        const auto it = negative_ff_slacks_.find(slack);
        if (it != negative_ff_slacks_.end()) {
            negative_ff_slacks_.erase(it);
        }
        metrics_.tns_ff -= slack;
    }
}

void SkewModel::add_negative_slack(double slack, Corner corner) {
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

void SkewModel::recompute_wns(Corner corner) {
    if (corner == Corner::SS) {
        metrics_.wns_ss = negative_ss_slacks_.empty() ? 0.0 : *negative_ss_slacks_.begin();
    } else {
        metrics_.wns_ff = negative_ff_slacks_.empty() ? 0.0 : *negative_ff_slacks_.begin();
    }
}

void SkewModel::update_path_slack(std::size_t path_idx) {
    const double old_ss = ss_slack_[path_idx];
    const double old_ff = ff_slack_[path_idx];

    const std::size_t launch_idx = launch_idx_[path_idx];
    const std::size_t capture_idx = capture_idx_[path_idx];
    const double skew_ss = arrival_ss_[capture_idx] - arrival_ss_[launch_idx];
    const double skew_ff = arrival_ff_[capture_idx] - arrival_ff_[launch_idx];

    const double new_ss = clock_period_ - setup_time_ - data_delay_ss_[path_idx] + skew_ss;
    const double new_ff = data_delay_ff_[path_idx] - hold_time_ - skew_ff;

    remove_negative_slack(old_ss, Corner::SS);
    remove_negative_slack(old_ff, Corner::FF);

    ss_slack_[path_idx] = new_ss;
    ff_slack_[path_idx] = new_ff;

    add_negative_slack(new_ss, Corner::SS);
    add_negative_slack(new_ff, Corner::FF);
}

void SkewModel::recompute_all_slacks_and_metrics() {
    negative_ss_slacks_.clear();
    negative_ff_slacks_.clear();
    metrics_ = {};

    for (std::size_t node_idx = 0; node_idx < names_.size(); ++node_idx) {
        if (kinds_[node_idx] == NodeKind::Buffer && cell_indices_[node_idx] >= 0) {
            metrics_.area += cell_area(cells_[static_cast<std::size_t>(cell_indices_[node_idx])]);
        }
    }
    for (const auto& edge : edges_) {
        for (const int cell_idx : edge.inserted_cell_indices) {
            if (cell_idx >= 0) {
                metrics_.area += cell_area(cells_[static_cast<std::size_t>(cell_idx)]);
            }
        }
    }

    for (std::size_t path_idx = 0; path_idx < launch_idx_.size(); ++path_idx) {
        const std::size_t launch_idx = launch_idx_[path_idx];
        const std::size_t capture_idx = capture_idx_[path_idx];
        const double skew_ss = arrival_ss_[capture_idx] - arrival_ss_[launch_idx];
        const double skew_ff = arrival_ff_[capture_idx] - arrival_ff_[launch_idx];

        ss_slack_[path_idx] = clock_period_ - setup_time_ - data_delay_ss_[path_idx] + skew_ss;
        ff_slack_[path_idx] = data_delay_ff_[path_idx] - hold_time_ - skew_ff;

        add_negative_slack(ss_slack_[path_idx], Corner::SS);
        add_negative_slack(ff_slack_[path_idx], Corner::FF);
    }

    recompute_wns(Corner::SS);
    recompute_wns(Corner::FF);
}

void SkewModel::apply_arrival_delta(std::size_t root_idx, double delta_ss, double delta_ff) {
    if (delta_ss == 0.0 && delta_ff == 0.0) {
        return;
    }

    affected_ff_buffer_.clear();
    if (affected_path_epoch_.size() < launch_idx_.size()) {
        affected_path_epoch_.assign(launch_idx_.size(), 0);
    }
    ++path_epoch_;
    if (path_epoch_ == 0) {
        std::fill(affected_path_epoch_.begin(), affected_path_epoch_.end(), 0);
        path_epoch_ = 1;
    }

    std::vector<std::size_t> stack{root_idx};
    while (!stack.empty()) {
        const std::size_t node_idx = stack.back();
        stack.pop_back();

        arrival_ss_[node_idx] += delta_ss;
        arrival_ff_[node_idx] += delta_ff;

        if (kinds_[node_idx] == NodeKind::FlipFlop) {
            affected_ff_buffer_.push_back(node_idx);
        }

        for (auto it = children_[node_idx].rbegin(); it != children_[node_idx].rend(); ++it) {
            stack.push_back(*it);
        }
    }

    for (const std::size_t ff_idx : affected_ff_buffer_) {
        for (const std::size_t path_idx : paths_as_launch_[ff_idx]) {
            if (affected_path_epoch_[path_idx] != path_epoch_) {
                affected_path_epoch_[path_idx] = path_epoch_;
                update_path_slack(path_idx);
            }
        }
        for (const std::size_t path_idx : paths_as_capture_[ff_idx]) {
            if (affected_path_epoch_[path_idx] != path_epoch_) {
                affected_path_epoch_[path_idx] = path_epoch_;
                update_path_slack(path_idx);
            }
        }
    }

    recompute_wns(Corner::SS);
    recompute_wns(Corner::FF);
}

double SkewModel::score(const Metrics& baseline) const {
    const double tns_ss_score = safe_divide(metrics_.tns_ss, baseline.tns_ss);
    const double wns_ss_score = safe_divide(metrics_.wns_ss, baseline.wns_ss);
    const double tns_ff_score = safe_divide(metrics_.tns_ff, baseline.tns_ff);
    const double wns_ff_score = safe_divide(metrics_.wns_ff, baseline.wns_ff);
    const double area_score = safe_divide(metrics_.area, baseline.area);

    return kScoreAlpha * (tns_ss_score + wns_ss_score) +
           kScoreBeta * (tns_ff_score + wns_ff_score) + kScoreGamma * area_score;
}

bool SkewModel::try_move(const SkewMove& move) {
    switch (move.kind) {
        case SkewMoveKind::Insert: {
            if (move.edge_idx >= edges_.size() || !cell_supports_fanout(move.cell_idx, 1)) {
                return false;
            }
            auto& edge = edges_[move.edge_idx];
            const auto& cell = cells_[static_cast<std::size_t>(move.cell_idx)];
            edge.inserted_cell_indices.push_back(move.cell_idx);
            metrics_.area += cell.area;
            apply_arrival_delta(edge.child_idx, cell.ss_delays_by_fanout[0],
                                cell.ff_delays_by_fanout[0]);
            return true;
        }
        case SkewMoveKind::Remove: {
            if (move.edge_idx >= edges_.size()) {
                return false;
            }
            auto& edge = edges_[move.edge_idx];
            if (edge.inserted_cell_indices.empty()) {
                return false;
            }
            const std::size_t position =
                move.insert_position >= 0 && static_cast<std::size_t>(move.insert_position) <
                                                 edge.inserted_cell_indices.size()
                    ? static_cast<std::size_t>(move.insert_position)
                    : edge.inserted_cell_indices.size() - 1;
            const int cell_idx = edge.inserted_cell_indices[position];
            if (!cell_supports_fanout(cell_idx, 1)) {
                return false;
            }
            const auto& cell = cells_[static_cast<std::size_t>(cell_idx)];
            edge.inserted_cell_indices.erase(edge.inserted_cell_indices.begin() +
                                             static_cast<std::ptrdiff_t>(position));
            metrics_.area -= cell.area;
            apply_arrival_delta(edge.child_idx, -cell.ss_delays_by_fanout[0],
                                -cell.ff_delays_by_fanout[0]);
            return true;
        }
        case SkewMoveKind::Resize: {
            if (move.node_idx >= names_.size() || kinds_[move.node_idx] != NodeKind::Buffer) {
                return false;
            }
            const std::size_t fanout = children_[move.node_idx].size();
            if (!cell_supports_fanout(move.cell_idx, fanout)) {
                return false;
            }
            const int old_cell_idx = cell_indices_[move.node_idx];
            if (old_cell_idx == move.cell_idx) {
                return false;
            }
            const double old_ss = node_buffer_delay_ss(move.node_idx);
            const double old_ff = node_buffer_delay_ff(move.node_idx);
            cell_indices_[move.node_idx] = move.cell_idx;
            const double new_ss = node_buffer_delay_ss(move.node_idx);
            const double new_ff = node_buffer_delay_ff(move.node_idx);

            metrics_.area -= cell_area(cells_[static_cast<std::size_t>(old_cell_idx)]);
            metrics_.area += cell_area(cells_[static_cast<std::size_t>(move.cell_idx)]);

            apply_arrival_delta(move.node_idx, new_ss - old_ss, new_ff - old_ff);
            return true;
        }
    }
    return false;
}

void SkewModel::undo_move(const SkewMove& move) {
    SkewMove inverse = move;
    switch (move.kind) {
        case SkewMoveKind::Insert:
            inverse.kind = SkewMoveKind::Remove;
            inverse.insert_position = -1;
            try_move(inverse);
            return;
        case SkewMoveKind::Remove:
            inverse.kind = SkewMoveKind::Insert;
            try_move(inverse);
            return;
        case SkewMoveKind::Resize:
            inverse.cell_idx = move.old_cell_idx;
            inverse.old_cell_idx = move.cell_idx;
            try_move(inverse);
            return;
    }
}

SkewModelState SkewModel::snapshot() const {
    SkewModelState state;
    state.cell_indices = cell_indices_;
    state.edge_inserted_cells.resize(edges_.size());
    for (std::size_t i = 0; i < edges_.size(); ++i) {
        state.edge_inserted_cells[i] = edges_[i].inserted_cell_indices;
    }
    state.arrival_ss = arrival_ss_;
    state.arrival_ff = arrival_ff_;
    state.ss_slack = ss_slack_;
    state.ff_slack = ff_slack_;
    state.metrics = metrics_;
    return state;
}

void SkewModel::restore(const SkewModelState& state) {
    cell_indices_ = state.cell_indices;
    for (std::size_t i = 0; i < edges_.size(); ++i) {
        edges_[i].inserted_cell_indices = state.edge_inserted_cells[i];
    }
    arrival_ss_ = state.arrival_ss;
    arrival_ff_ = state.arrival_ff;
    ss_slack_ = state.ss_slack;
    ff_slack_ = state.ff_slack;
    metrics_ = SkewModelMetrics{0.0, 0.0, 0.0, 0.0, state.metrics.area};

    negative_ss_slacks_.clear();
    negative_ff_slacks_.clear();
    for (std::size_t path_idx = 0; path_idx < ss_slack_.size(); ++path_idx) {
        add_negative_slack(ss_slack_[path_idx], Corner::SS);
        add_negative_slack(ff_slack_[path_idx], Corner::FF);
    }
    recompute_wns(Corner::SS);
    recompute_wns(Corner::FF);
}

std::size_t SkewModel::random_edge_index() const {
    static thread_local std::mt19937 rng(42);
    if (edges_.empty()) {
        return 0;
    }
    std::uniform_int_distribution<std::size_t> dist(0, edges_.size() - 1);
    return dist(rng);
}

std::size_t SkewModel::random_buffer_node_index() const {
    static thread_local std::mt19937 rng(43);
    std::vector<std::size_t> buffer_nodes;
    buffer_nodes.reserve(names_.size());
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (kinds_[i] == NodeKind::Buffer) {
            buffer_nodes.push_back(i);
        }
    }
    if (buffer_nodes.empty()) {
        return 0;
    }
    std::uniform_int_distribution<std::size_t> dist(0, buffer_nodes.size() - 1);
    return buffer_nodes[dist(rng)];
}

int SkewModel::random_cell_index() const {
    static thread_local std::mt19937 rng(44);
    if (cells_.empty()) {
        return -1;
    }
    std::uniform_int_distribution<int> dist(0, static_cast<int>(cells_.size()) - 1);
    return dist(rng);
}

int SkewModel::random_valid_cell_for_buffer(std::size_t node_idx) const {
    static thread_local std::mt19937 rng(45);
    if (cells_.empty() || node_idx >= names_.size()) {
        return -1;
    }
    const std::size_t fanout = children_[node_idx].size();
    std::vector<int> valid_cells;
    valid_cells.reserve(cells_.size());
    for (int i = 0; i < static_cast<int>(cells_.size()); ++i) {
        if (cell_supports_fanout(i, fanout)) {
            valid_cells.push_back(i);
        }
    }
    if (valid_cells.empty()) {
        return -1;
    }
    std::uniform_int_distribution<std::size_t> dist(0, valid_cells.size() - 1);
    return valid_cells[dist(rng)];
}

std::size_t SkewModel::random_edge_with_inserts() const {
    static thread_local std::mt19937 rng(46);
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < edges_.size(); ++i) {
        if (!edges_[i].inserted_cell_indices.empty()) {
            candidates.push_back(i);
        }
    }
    if (candidates.empty()) {
        return random_edge_index();
    }
    std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
    return candidates[dist(rng)];
}

std::size_t SkewModel::random_guided_insert_edge() const {
    static thread_local std::mt19937 rng(47);

    std::vector<std::size_t> violating_paths;
    violating_paths.reserve(launch_idx_.size());
    for (std::size_t path_idx = 0; path_idx < launch_idx_.size(); ++path_idx) {
        if (ss_slack_[path_idx] < 0.0 || ff_slack_[path_idx] < 0.0) {
            violating_paths.push_back(path_idx);
        }
    }
    if (violating_paths.empty()) {
        return random_edge_index();
    }

    std::uniform_int_distribution<std::size_t> path_dist(0, violating_paths.size() - 1);
    const std::size_t path_idx = violating_paths[path_dist(rng)];
    const std::size_t launch = launch_idx_[path_idx];
    const std::size_t capture = capture_idx_[path_idx];

    std::vector<std::size_t> target_edges;
    if (ss_slack_[path_idx] < 0.0) {
        for (std::size_t edge_idx = 0; edge_idx < edges_.size(); ++edge_idx) {
            if (edges_[edge_idx].child_idx == capture) {
                target_edges.push_back(edge_idx);
            }
        }
    }
    if (ff_slack_[path_idx] < 0.0) {
        for (std::size_t edge_idx = 0; edge_idx < edges_.size(); ++edge_idx) {
            if (edges_[edge_idx].child_idx == launch) {
                target_edges.push_back(edge_idx);
            }
        }
    }
    if (target_edges.empty()) {
        return random_edge_index();
    }

    std::uniform_int_distribution<std::size_t> edge_dist(0, target_edges.size() - 1);
    return target_edges[edge_dist(rng)];
}

int SkewModel::smallest_fanout1_cell_index() const {
    int best_idx = -1;
    double best_area = std::numeric_limits<double>::infinity();
    for (int cell_idx = 0; cell_idx < static_cast<int>(cells_.size()); ++cell_idx) {
        if (!cell_supports_fanout(cell_idx, 1)) {
            continue;
        }
        const double area = cells_[static_cast<std::size_t>(cell_idx)].area;
        if (area < best_area) {
            best_area = area;
            best_idx = cell_idx;
        }
    }
    return best_idx;
}

int SkewModel::random_fanout1_cell_index() const {
    static thread_local std::mt19937 rng(48);
    std::vector<int> valid_cells;
    valid_cells.reserve(cells_.size());
    for (int cell_idx = 0; cell_idx < static_cast<int>(cells_.size()); ++cell_idx) {
        if (cell_supports_fanout(cell_idx, 1)) {
            valid_cells.push_back(cell_idx);
        }
    }
    if (valid_cells.empty()) {
        return -1;
    }
    std::uniform_int_distribution<std::size_t> dist(0, valid_cells.size() - 1);
    return valid_cells[dist(rng)];
}

std::size_t SkewModel::descendant_ff_count(std::size_t node_idx) const {
    if (node_idx >= ff_descendant_counts_.size()) {
        return 0;
    }
    return ff_descendant_counts_[node_idx];
}

bool SkewModel::apply_one_greedy_step(const Metrics& baseline_metrics) {
    struct Candidate {
        SkewMove move;
        double score_delta = 0.0;
    };

    Candidate best_candidate;
    best_candidate.score_delta = 0.0;

    const std::size_t path_count = launch_idx_.size();
    std::vector<std::size_t> violating_paths;
    violating_paths.reserve(path_count);
    for (std::size_t path_idx = 0; path_idx < path_count; ++path_idx) {
        if (ss_slack_[path_idx] < 0.0 || ff_slack_[path_idx] < 0.0) {
            violating_paths.push_back(path_idx);
        }
    }

    std::vector<int> fanout1_cells;
    fanout1_cells.reserve(cells_.size());
    for (int cell_idx = 0; cell_idx < static_cast<int>(cells_.size()); ++cell_idx) {
        if (cell_supports_fanout(cell_idx, 1)) {
            fanout1_cells.push_back(cell_idx);
        }
    }
    std::sort(fanout1_cells.begin(), fanout1_cells.end(), [&](int lhs, int rhs) {
        return cells_[static_cast<std::size_t>(lhs)].area <
               cells_[static_cast<std::size_t>(rhs)].area;
    });

    if (!violating_paths.empty()) {
        std::sort(violating_paths.begin(), violating_paths.end(),
                  [&](std::size_t lhs, std::size_t rhs) {
                      const double lhs_violation =
                          std::min(ss_slack_[lhs], 0.0) + std::min(ff_slack_[lhs], 0.0);
                      const double rhs_violation =
                          std::min(ss_slack_[rhs], 0.0) + std::min(ff_slack_[rhs], 0.0);
                      return lhs_violation < rhs_violation;
                  });

        const std::size_t sample_count = std::min<std::size_t>(violating_paths.size(), 32);
        for (std::size_t sample = 0; sample < sample_count; ++sample) {
            const std::size_t path_idx = violating_paths[sample];
            const std::size_t launch = launch_idx_[path_idx];
            const std::size_t capture = capture_idx_[path_idx];

            std::vector<std::size_t> target_edges;
            if (ss_slack_[path_idx] < 0.0) {
                for (std::size_t edge_idx = 0; edge_idx < edges_.size(); ++edge_idx) {
                    if (edges_[edge_idx].child_idx == capture) {
                        target_edges.push_back(edge_idx);
                    }
                }
            }
            if (ff_slack_[path_idx] < 0.0) {
                for (std::size_t edge_idx = 0; edge_idx < edges_.size(); ++edge_idx) {
                    if (edges_[edge_idx].child_idx == launch) {
                        target_edges.push_back(edge_idx);
                    }
                }
            }

            for (const std::size_t edge_idx : target_edges) {
                for (const int cell_idx : fanout1_cells) {
                    const double before = score(baseline_metrics);
                    SkewMove move{SkewMoveKind::Insert, edge_idx, 0, cell_idx};
                    if (!try_move(move)) {
                        continue;
                    }
                    const double after = score(baseline_metrics);
                    const double delta = after - before;
                    if (delta > best_candidate.score_delta) {
                        best_candidate.move = move;
                        best_candidate.score_delta = delta;
                    }
                    undo_move(move);
                }
            }
        }
    }

    std::size_t removal_candidates = 0;
    for (std::size_t edge_idx = 0; edge_idx < edges_.size(); ++edge_idx) {
        const auto& inserted_cells = edges_[edge_idx].inserted_cell_indices;
        if (inserted_cells.empty()) {
            continue;
        }
        ++removal_candidates;
        const int insert_position = static_cast<int>(inserted_cells.size() - 1);
        const int cell_idx = inserted_cells.back();
        const double before = score(baseline_metrics);
        SkewMove move{SkewMoveKind::Remove, edge_idx, 0, cell_idx, insert_position};
        if (!try_move(move)) {
            continue;
        }
        const double after = score(baseline_metrics);
        const double delta = after - before;
        if (delta > best_candidate.score_delta) {
            best_candidate.move = move;
            best_candidate.score_delta = delta;
        }
        undo_move(move);
        if (removal_candidates >= 512) {
            break;
        }
    }

    if (best_candidate.score_delta <= 0.0) {
        return false;
    }
    try_move(best_candidate.move);
    return true;
}

void SkewModel::apply_greedy_warmup(const Metrics& baseline_metrics, std::size_t max_iterations) {
    for (std::size_t iter = 0; iter < max_iterations; ++iter) {
        if (!apply_one_greedy_step(baseline_metrics)) {
            break;
        }
    }
}

}  // namespace cadd0040
