/**
 * @file datapath_graph.cpp
 * @brief DataPathGraph implementation unit.
 */

#include "datapath_graph.hpp"

#include <stdexcept>

namespace cadd0040 {

namespace {

void set_corner_delay(CornerDelay& corner_delay, Corner corner, double delay) {
    switch (corner) {
        case Corner::SS:
            corner_delay.ss = delay;
            return;
        case Corner::FF:
            corner_delay.ff = delay;
            return;
    }

    throw std::invalid_argument("Unknown timing corner");
}

}  // namespace

void DataPathGraph::clear() {
    timing_constraint_ = TimingConstraint{};
    edges_.clear();
    path_name_to_id_.clear();
    launch_to_edge_ids_.clear();
    capture_to_edge_ids_.clear();
}

EdgeId DataPathGraph::add_edge(const std::string& path_name,
                               const std::string& launch_flip_flop_name,
                               const std::string& capture_flip_flop_name) {
    if (path_name.empty()) {
        throw std::invalid_argument("Data path name cannot be empty");
    }
    if (launch_flip_flop_name.empty()) {
        throw std::invalid_argument("Launch flip-flop name cannot be empty");
    }
    if (capture_flip_flop_name.empty()) {
        throw std::invalid_argument("Capture flip-flop name cannot be empty");
    }
    if (contains_edge(path_name)) {
        throw std::invalid_argument("Duplicate data path name: " + path_name);
    }

    const EdgeId edge_id = edges_.size();
    edges_.push_back(DataPathEdge{
        .id = edge_id,
        .path_name = path_name,
        .launch_flip_flop_name = launch_flip_flop_name,
        .capture_flip_flop_name = capture_flip_flop_name,
        .data_delay = {},
    });

    path_name_to_id_.emplace(path_name, edge_id);
    launch_to_edge_ids_[launch_flip_flop_name].push_back(edge_id);
    capture_to_edge_ids_[capture_flip_flop_name].push_back(edge_id);

    return edge_id;
}

void DataPathGraph::set_delay(EdgeId edge_id, Corner corner, double delay) {
    if (edge_id >= edges_.size()) {
        throw std::out_of_range("Data path edge id does not exist");
    }

    set_corner_delay(edges_[edge_id].data_delay, corner, delay);
}

void DataPathGraph::set_delay(const std::string& path_name, Corner corner, double delay) {
    set_delay(edge(path_name).id, corner, delay);
}

void DataPathGraph::set_clock_period(double clock_period) {
    timing_constraint_.clock_period = clock_period;
}

double DataPathGraph::clock_period() const {
    return timing_constraint_.clock_period;
}

double DataPathGraph::setup_time() const {
    return timing_constraint_.setup_time();
}

double DataPathGraph::hold_time() const {
    return timing_constraint_.hold_time();
}

bool DataPathGraph::contains_edge(const std::string& path_name) const {
    return path_name_to_id_.find(path_name) != path_name_to_id_.end();
}

const DataPathEdge& DataPathGraph::edge(EdgeId edge_id) const {
    if (edge_id >= edges_.size()) {
        throw std::out_of_range("Data path edge id does not exist");
    }

    return edges_[edge_id];
}

const DataPathEdge& DataPathGraph::edge(const std::string& path_name) const {
    const auto it = path_name_to_id_.find(path_name);
    if (it == path_name_to_id_.end()) {
        throw std::out_of_range("Data path name does not exist: " + path_name);
    }

    return edge(it->second);
}

const std::vector<DataPathEdge>& DataPathGraph::edges() const {
    return edges_;
}

std::size_t DataPathGraph::edge_count() const {
    return edges_.size();
}

std::vector<EdgeId> DataPathGraph::outgoing_edges(
    const std::string& launch_flip_flop_name) const {
    const auto it = launch_to_edge_ids_.find(launch_flip_flop_name);
    if (it == launch_to_edge_ids_.end()) {
        return {};
    }

    return it->second;
}

std::vector<EdgeId> DataPathGraph::incoming_edges(
    const std::string& capture_flip_flop_name) const {
    const auto it = capture_to_edge_ids_.find(capture_flip_flop_name);
    if (it == capture_to_edge_ids_.end()) {
        return {};
    }

    return it->second;
}

}  // namespace cadd0040
