/**
 * @file clock_tree.cpp
 * @brief ClockTree implementation unit.
 */

#include "clock_tree.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace cadd0040 {
namespace {

const BufferCell& find_buffer_cell(const BufferLibrary& buffer_library,
                                   const std::string& cell_type) {
    const auto it = buffer_library.find(cell_type);
    if (it == buffer_library.end()) {
        throw std::invalid_argument("Unknown buffer cell type: " + cell_type);
    }
    return it->second;
}

double buffer_delay(const BufferCell& buffer_cell, std::size_t fanout, Corner corner) {
    if (fanout == 0) {
        throw std::invalid_argument("Buffer fanout must be greater than zero");
    }

    const auto& delays =
        corner == Corner::SS ? buffer_cell.ss_delays_by_fanout : buffer_cell.ff_delays_by_fanout;
    if (fanout > delays.size()) {
        throw std::out_of_range("Buffer fanout exceeds delay table");
    }

    return delays[fanout - 1];
}

double buffer_area(const BufferCell& buffer_cell) {
    if (buffer_cell.area != 0.0) {
        return buffer_cell.area;
    }
    return buffer_cell.width * buffer_cell.height;
}

bool cell_supports_fanout(const BufferCell& buffer_cell, std::size_t fanout) {
    return fanout <= buffer_cell.ss_delays_by_fanout.size() &&
           fanout <= buffer_cell.ff_delays_by_fanout.size();
}

void require_flip_flop_endpoint(const ClockNode& clock_node, const char* role) {
    if (clock_node.kind != NodeKind::FlipFlop) {
        throw std::invalid_argument(std::string("Clock skew ") + role +
                                    " endpoint is not a flip-flop: " + clock_node.name);
    }
}

struct TraversalStackEntry {
    NodeId node_id = kInvalidNodeId;
    std::size_t depth = 0;
};

}  // namespace

void ClockTree::add_root(const std::string& root_name) {
    if (root_id_ != kInvalidNodeId) {
        throw std::logic_error("Clock tree root already exists");
    }
    if (contains_name(root_name)) {
        throw std::invalid_argument("Duplicate clock tree node name: " + root_name);
    }

    const NodeId root_id = nodes_.size();
    nodes_.push_back(ClockNode{
        .id = root_id,
        .name = root_name,
        .cell_type = "",
        .kind = NodeKind::ClockSource,
        .parent_id = kInvalidNodeId,
        .child_ids = {},
    });
    root_id_ = root_id;
    name_to_id_.emplace(root_name, root_id);
    mark_clock_arrivals_dirty_from(root_id);
}

void ClockTree::add_node(const std::string& node_name, const std::string& cell_type,
                         NodeKind node_kind, const std::string& parent_name) {
    const NodeId parent_id = find_node(parent_name);
    if (!contains_node(parent_id)) {
        throw std::out_of_range("Parent node does not exist");
    }
    if (contains_name(node_name)) {
        throw std::invalid_argument("Duplicate clock tree node name: " + node_name);
    }
    if (node_kind == NodeKind::ClockSource) {
        throw std::invalid_argument("Use add_root to create clock source nodes");
    }
    if (node(parent_id).kind == NodeKind::FlipFlop) {
        throw std::invalid_argument("Flip-flop nodes cannot drive child nodes");
    }

    const NodeId node_id = nodes_.size();
    nodes_.push_back(ClockNode{
        .id = node_id,
        .name = node_name,
        .cell_type = cell_type,
        .kind = node_kind,
        .parent_id = parent_id,
        .child_ids = {},
    });
    mutable_node(parent_id).child_ids.push_back(node_id);
    name_to_id_.emplace(node_name, node_id);
    mark_clock_arrivals_dirty_from(parent_id);
}

bool ClockTree::insert_buffer(const std::string& parent_name, const std::string& child_name,
                              const std::string& buffer_name, const std::string& cell_type,
                              const BufferLibrary& buffer_library) {
    const NodeId parent_id = find_node(parent_name);
    const NodeId child_id = find_node(child_name);
    if (!contains_node(parent_id)) {
        std::cerr << "Failed to insert buffer " << buffer_name
                  << ": parent node does not exist: " << parent_name << '\n';
        return false;
    }
    if (!contains_node(child_id)) {
        std::cerr << "Failed to insert buffer " << buffer_name
                  << ": child node does not exist: " << child_name << '\n';
        return false;
    }
    if (contains_name(buffer_name)) {
        std::cerr << "Failed to insert buffer " << buffer_name
                  << ": duplicate clock tree node name\n";
        return false;
    }
    if (node(child_id).parent_id != parent_id) {
        std::cerr << "Failed to insert buffer " << buffer_name << ": child " << child_name
                  << " is not driven by parent " << parent_name << '\n';
        return false;
    }
    if (node(parent_id).kind == NodeKind::FlipFlop) {
        std::cerr << "Failed to insert buffer " << buffer_name
                  << ": flip-flop nodes cannot drive inserted buffers\n";
        return false;
    }
    const auto cell_it = buffer_library.find(cell_type);
    if (cell_it == buffer_library.end()) {
        std::cerr << "Failed to insert buffer " << buffer_name
                  << ": unknown buffer cell type: " << cell_type << '\n';
        return false;
    }
    if (!cell_supports_fanout(cell_it->second, 1)) {
        std::cerr << "Failed to insert buffer " << buffer_name << ": buffer cell " << cell_type
                  << " does not support fanout 1 in both timing corners\n";
        return false;
    }

    const auto& parent_children = node(parent_id).child_ids;
    const auto child_it = std::find(parent_children.begin(), parent_children.end(), child_id);
    if (child_it == parent_children.end()) {
        std::cerr << "Failed to insert buffer " << buffer_name
                  << ": parent does not contain the specified child\n";
        return false;
    }
    const auto child_index = static_cast<std::size_t>(child_it - parent_children.begin());

    const NodeId buffer_id = nodes_.size();
    nodes_.push_back(ClockNode{
        .id = buffer_id,
        .name = buffer_name,
        .cell_type = cell_type,
        .kind = NodeKind::Buffer,
        .parent_id = parent_id,
        .child_ids = {child_id},
    });
    name_to_id_.emplace(buffer_name, buffer_id);

    mutable_node(parent_id).child_ids[child_index] = buffer_id;
    mutable_node(child_id).parent_id = buffer_id;
    mark_clock_arrivals_dirty_from(buffer_id);
    return true;
}

bool ClockTree::resize_buffer(const std::string& node_name, const std::string& cell_type,
                              const BufferLibrary& buffer_library) {
    const NodeId node_id = find_node(node_name);
    if (!contains_node(node_id)) {
        std::cerr << "Failed to resize buffer " << node_name << ": node does not exist\n";
        return false;
    }

    auto& clock_node = mutable_node(node_id);
    if (clock_node.kind != NodeKind::Buffer) {
        std::cerr << "Failed to resize buffer " << node_name << ": node is not a buffer\n";
        return false;
    }
    const auto cell_it = buffer_library.find(cell_type);
    if (cell_it == buffer_library.end()) {
        std::cerr << "Failed to resize buffer " << node_name
                  << ": unknown buffer cell type: " << cell_type << '\n';
        return false;
    }
    const std::size_t current_fanout = fanout(node_id);
    if (!cell_supports_fanout(cell_it->second, current_fanout)) {
        std::cerr << "Failed to resize buffer " << node_name << ": buffer cell " << cell_type
                  << " does not support current fanout " << current_fanout
                  << " in both timing corners\n";
        return false;
    }
    clock_node.cell_type = cell_type;
    mark_clock_arrivals_dirty_from(node_id);
    return true;
}

bool ClockTree::empty() const { return nodes_.empty(); }

std::size_t ClockTree::size() const { return nodes_.size(); }

const std::string& ClockTree::root_name() const {
    if (root_id_ == kInvalidNodeId) {
        throw std::logic_error("Clock tree root does not exist");
    }
    return node(root_id_).name;
}

bool ClockTree::contains_node(NodeId node_id) const { return node_id < nodes_.size(); }

bool ClockTree::contains_name(const std::string& node_name) const {
    return name_to_id_.find(node_name) != name_to_id_.end();
}

NodeId ClockTree::find_node(const std::string& node_name) const {
    const auto it = name_to_id_.find(node_name);
    if (it == name_to_id_.end()) {
        return kInvalidNodeId;
    }
    return it->second;
}

const ClockNode& ClockTree::node(const std::string& node_name) const {
    return node(find_node(node_name));
}

const ClockNode& ClockTree::node(NodeId node_id) const {
    if (!contains_node(node_id)) {
        throw std::out_of_range("Clock tree node does not exist");
    }
    return nodes_[node_id];
}

ClockNode& ClockTree::mutable_node(NodeId node_id) {
    if (!contains_node(node_id)) {
        throw std::out_of_range("Clock tree node does not exist");
    }
    return nodes_[node_id];
}

const std::vector<ClockNode>& ClockTree::nodes() const { return nodes_; }

std::size_t ClockTree::fanout(const std::string& node_name) const {
    return fanout(find_node(node_name));
}

std::size_t ClockTree::fanout(NodeId node_id) const { return node(node_id).child_ids.size(); }

std::vector<ClockTreeTraversalEntry> ClockTree::preorder_with_depth() const {
    std::vector<ClockTreeTraversalEntry> traversal;
    if (root_id_ == kInvalidNodeId) {
        return traversal;
    }

    std::vector<TraversalStackEntry> stack;
    stack.push_back(TraversalStackEntry{
        .node_id = root_id_,
        .depth = 0,
    });

    // Preserve child_ids order so the writer can reproduce clk_tree.structure ordering.
    while (!stack.empty()) {
        const auto current = stack.back();
        stack.pop_back();

        const auto& current_node = node(current.node_id);
        traversal.push_back(ClockTreeTraversalEntry{
            .node_name = current_node.name,
            .depth = current.depth,
        });

        for (auto it = current_node.child_ids.rbegin(); it != current_node.child_ids.rend(); ++it) {
            stack.push_back(TraversalStackEntry{
                .node_id = *it,
                .depth = current.depth + 1,
            });
        }
    }

    return traversal;
}

std::vector<ClockTreeTraversalEntry> ClockTree::preorder_with_depth_recursive() const {
    std::vector<ClockTreeTraversalEntry> traversal;
    if (root_id_ == kInvalidNodeId) {
        return traversal;
    }

    // Recursive reference implementation; avoid this for very deep clock trees.
    const std::function<void(NodeId, std::size_t)> visit = [&](NodeId node_id, std::size_t depth) {
        const auto& current_node = node(node_id);
        traversal.push_back(ClockTreeTraversalEntry{
            .node_name = current_node.name,
            .depth = depth,
        });

        for (const NodeId child_id : current_node.child_ids) {
            visit(child_id, depth + 1);
        }
    };

    visit(root_id_, 0);
    return traversal;
}

std::vector<std::string> ClockTree::path_to_root(const std::string& node_name) const {
    std::vector<std::string> path_names;
    for (const NodeId node_id : path_to_root(find_node(node_name))) {
        path_names.push_back(node(node_id).name);
    }
    return path_names;
}

std::vector<NodeId> ClockTree::path_to_root(NodeId node_id) const {
    std::vector<NodeId> path;
    NodeId current_id = node_id;

    while (current_id != kInvalidNodeId) {
        const auto& current_node = node(current_id);
        path.push_back(current_id);
        current_id = current_node.parent_id;
    }

    return path;
}

std::vector<std::string> ClockTree::path_from_root(const std::string& node_name) const {
    std::vector<std::string> path_names;
    for (const NodeId node_id : path_from_root(find_node(node_name))) {
        path_names.push_back(node(node_id).name);
    }
    return path_names;
}

std::vector<NodeId> ClockTree::path_from_root(NodeId node_id) const {
    auto path = path_to_root(node_id);
    std::reverse(path.begin(), path.end());
    return path;
}

void ClockTree::update_clock_times(const BufferLibrary& buffer_library) {
    use_clock_arrival_library(buffer_library);
    if (root_id_ == kInvalidNodeId) {
        return;
    }

    update_clock_arrival_subtree(root_id_, buffer_library);
}

void ClockTree::update_clock_times_from(const std::string& node_name,
                                        const BufferLibrary& buffer_library) {
    const NodeId node_id = find_node(node_name);
    if (!contains_node(node_id)) {
        throw std::out_of_range("Clock tree node does not exist: " + node_name);
    }

    use_clock_arrival_library(buffer_library);
    const NodeId parent_id = node(node_id).parent_id;
    if (parent_id != kInvalidNodeId) {
        ensure_clock_arrival(parent_id, buffer_library);
    }
    update_clock_arrival_subtree(node_id, buffer_library);
}

double ClockTree::cached_clock_arrival(NodeId node_id, Corner corner) const {
    const auto& clock_node = node(node_id);
    return corner == Corner::SS ? clock_node.ss_clock_arrival : clock_node.ff_clock_arrival;
}

void ClockTree::update_clock_arrival(NodeId node_id, const BufferLibrary& buffer_library) {
    auto& clock_node = mutable_node(node_id);

    const NodeId parent_id = clock_node.parent_id;
    const double parent_ss_arrival =
        parent_id == kInvalidNodeId ? 0.0 : node(parent_id).ss_clock_arrival;
    const double parent_ff_arrival =
        parent_id == kInvalidNodeId ? 0.0 : node(parent_id).ff_clock_arrival;

    clock_node.ss_clock_arrival = parent_ss_arrival;
    clock_node.ff_clock_arrival = parent_ff_arrival;
    if (clock_node.kind == NodeKind::Buffer) {
        const auto& buffer_cell = find_buffer_cell(buffer_library, clock_node.cell_type);
        clock_node.ss_clock_arrival += buffer_delay(buffer_cell, fanout(node_id), Corner::SS);
        clock_node.ff_clock_arrival += buffer_delay(buffer_cell, fanout(node_id), Corner::FF);
    }

    clock_node.clock_arrival_dirty = false;
}

void ClockTree::update_clock_arrival_subtree(NodeId node_id, const BufferLibrary& buffer_library) {
    std::vector<NodeId> stack{node_id};
    while (!stack.empty()) {
        const NodeId current_id = stack.back();
        stack.pop_back();

        update_clock_arrival(current_id, buffer_library);

        const auto& current_node = node(current_id);
        for (auto it = current_node.child_ids.rbegin(); it != current_node.child_ids.rend(); ++it) {
            stack.push_back(*it);
        }
    }
}

void ClockTree::mark_clock_arrivals_dirty_from(NodeId node_id) {
    if (!contains_node(node_id)) {
        return;
    }

    std::vector<NodeId> stack{node_id};
    while (!stack.empty()) {
        const NodeId current_id = stack.back();
        stack.pop_back();

        auto& current_node = mutable_node(current_id);
        current_node.clock_arrival_dirty = true;

        for (auto it = current_node.child_ids.rbegin(); it != current_node.child_ids.rend(); ++it) {
            stack.push_back(*it);
        }
    }
}

void ClockTree::ensure_clock_arrival(NodeId node_id, const BufferLibrary& buffer_library) {
    if (!contains_node(node_id)) {
        throw std::out_of_range("Clock tree node does not exist");
    }

    use_clock_arrival_library(buffer_library);
    for (const NodeId current_id : path_from_root(node_id)) {
        if (node(current_id).clock_arrival_dirty) {
            update_clock_arrival(current_id, buffer_library);
        }
    }
}

void ClockTree::use_clock_arrival_library(const BufferLibrary& buffer_library) {
    if (clock_arrival_buffer_library_ == &buffer_library) {
        return;
    }

    if (root_id_ != kInvalidNodeId) {
        mark_clock_arrivals_dirty_from(root_id_);
    }
    clock_arrival_buffer_library_ = &buffer_library;
}

double ClockTree::clock_delay(const std::string& node_name, const BufferLibrary& buffer_library,
                              Corner corner) {
    const NodeId node_id = find_node(node_name);
    ensure_clock_arrival(node_id, buffer_library);
    return cached_clock_arrival(node_id, corner);
}

double ClockTree::clock_skew(const std::string& launch_flip_flop_name,
                             const std::string& capture_flip_flop_name,
                             const BufferLibrary& buffer_library, Corner corner) {
    const NodeId launch_id = find_node(launch_flip_flop_name);
    if (!contains_node(launch_id)) {
        throw std::invalid_argument("Clock skew launch endpoint does not exist: " +
                                    launch_flip_flop_name);
    }

    const NodeId capture_id = find_node(capture_flip_flop_name);
    if (!contains_node(capture_id)) {
        throw std::invalid_argument("Clock skew capture endpoint does not exist: " +
                                    capture_flip_flop_name);
    }

    const auto& launch_node = node(launch_id);
    const auto& capture_node = node(capture_id);
    require_flip_flop_endpoint(launch_node, "launch");
    require_flip_flop_endpoint(capture_node, "capture");

    ensure_clock_arrival(launch_id, buffer_library);
    ensure_clock_arrival(capture_id, buffer_library);

    if (corner == Corner::SS) {
        return cached_clock_arrival(capture_id, Corner::SS) -
               cached_clock_arrival(launch_id, Corner::SS);
    }
    return cached_clock_arrival(capture_id, Corner::FF) -
           cached_clock_arrival(launch_id, Corner::FF);
}

double ClockTree::area(const BufferLibrary& buffer_library) const {
    double total_area = 0.0;

    for (const auto& clock_node : nodes_) {
        if (clock_node.kind != NodeKind::Buffer) {
            continue;
        }

        total_area += buffer_area(find_buffer_cell(buffer_library, clock_node.cell_type));
    }

    return total_area;
}

std::ostream& operator<<(std::ostream& os, const ClockTree& clock_tree) {
    if (clock_tree.root_id_ == kInvalidNodeId) {
        return os;
    }

    const auto& root = clock_tree.nodes_[clock_tree.root_id_];
    os << "Root: " << root.name << '\n';

    std::vector<TraversalStackEntry> stack;
    for (auto it = root.child_ids.rbegin(); it != root.child_ids.rend(); ++it) {
        stack.push_back(TraversalStackEntry{
            .node_id = *it,
            .depth = 1,
        });
    }

    while (!stack.empty()) {
        const auto current = stack.back();
        stack.pop_back();

        const auto& current_node = clock_tree.nodes_[current.node_id];
        for (std::size_t depth = 0; depth < current.depth; ++depth) {
            os << '\t';
        }
        os << '[' << current.depth << "] " << current_node.name << " (" << current_node.cell_type
           << ')';
        if (current_node.kind == NodeKind::FlipFlop) {
            os << " (SINK)";
        }
        os << '\n';

        for (auto it = current_node.child_ids.rbegin(); it != current_node.child_ids.rend(); ++it) {
            stack.push_back(TraversalStackEntry{
                .node_id = *it,
                .depth = current.depth + 1,
            });
        }
    }

    return os;
}

}  // namespace cadd0040
