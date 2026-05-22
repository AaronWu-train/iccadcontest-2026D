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

    const auto& delays = corner == Corner::SS ? buffer_cell.ss_delays_by_fanout
                                              : buffer_cell.ff_delays_by_fanout;
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
    return true;
}

bool ClockTree::empty() const {
    return nodes_.empty();
}

std::size_t ClockTree::size() const {
    return nodes_.size();
}

const std::string& ClockTree::root_name() const {
    if (root_id_ == kInvalidNodeId) {
        throw std::logic_error("Clock tree root does not exist");
    }
    return node(root_id_).name;
}

bool ClockTree::contains_node(NodeId node_id) const {
    return node_id < nodes_.size();
}

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

const std::vector<ClockNode>& ClockTree::nodes() const {
    return nodes_;
}

std::size_t ClockTree::fanout(const std::string& node_name) const {
    return fanout(find_node(node_name));
}

std::size_t ClockTree::fanout(NodeId node_id) const {
    return node(node_id).child_ids.size();
}

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

double ClockTree::clock_time(const std::string& node_name, Corner corner) const {
    const NodeId node_id = find_node(node_name);
    const auto& clock_node = node(node_id);
    return corner == Corner::SS ? clock_node.ss_clock_time : clock_node.ff_clock_time;
}

void ClockTree::update_clock_times(const BufferLibrary& buffer_library) {
    if (root_id_ == kInvalidNodeId) {
        return;
    }

    update_clock_times_from(root_id_, buffer_library);
}

void ClockTree::update_clock_times_from(const std::string& node_name,
                                        const BufferLibrary& buffer_library) {
    update_clock_times_from(find_node(node_name), buffer_library);
}

void ClockTree::update_clock_times_from(NodeId node_id, const BufferLibrary& buffer_library) {
    const auto parent_id = node(node_id).parent_id;
    const double parent_ss_time =
        parent_id == kInvalidNodeId ? 0.0 : node(parent_id).ss_clock_time;
    const double parent_ff_time =
        parent_id == kInvalidNodeId ? 0.0 : node(parent_id).ff_clock_time;

    std::vector<NodeId> stack{node_id};
    while (!stack.empty()) {
        const NodeId current_id = stack.back();
        stack.pop_back();

        auto& current_node = mutable_node(current_id);
        const NodeId current_parent_id = current_node.parent_id;
        const double base_ss_time =
            current_parent_id == parent_id ? parent_ss_time : node(current_parent_id).ss_clock_time;
        const double base_ff_time =
            current_parent_id == parent_id ? parent_ff_time : node(current_parent_id).ff_clock_time;

        current_node.ss_clock_time = base_ss_time;
        current_node.ff_clock_time = base_ff_time;
        if (current_node.kind == NodeKind::Buffer) {
            const auto& buffer_cell = find_buffer_cell(buffer_library, current_node.cell_type);
            current_node.ss_clock_time += buffer_delay(buffer_cell, fanout(current_id), Corner::SS);
            current_node.ff_clock_time += buffer_delay(buffer_cell, fanout(current_id), Corner::FF);
        }

        for (auto it = current_node.child_ids.rbegin(); it != current_node.child_ids.rend(); ++it) {
            stack.push_back(*it);
        }
    }
}

double ClockTree::clock_delay(const std::string& node_name, const BufferLibrary& buffer_library,
                              Corner corner) const {
    double total_delay = 0.0;

    for (const NodeId path_node_id : path_from_root(find_node(node_name))) {
        const auto& path_node = node(path_node_id);
        if (path_node.kind != NodeKind::Buffer) {
            continue;
        }

        const auto& buffer_cell = find_buffer_cell(buffer_library, path_node.cell_type);
        total_delay += buffer_delay(buffer_cell, fanout(path_node_id), corner);
    }

    return total_delay;
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

}  // namespace cadd0040
