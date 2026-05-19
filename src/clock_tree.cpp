/**
 * @file clock_tree.cpp
 * @brief ClockTree implementation unit.
 */

#include "clock_tree.hpp"

#include <algorithm>
#include <functional>
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

}  // namespace

NodeId ClockTree::add_root(const std::string& root_name) {
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
    return root_id;
}

NodeId ClockTree::add_node(const std::string& node_name, const std::string& cell_type,
                           NodeKind node_kind, NodeId parent_id) {
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
    node(parent_id).child_ids.push_back(node_id);
    name_to_id_.emplace(node_name, node_id);
    return node_id;
}

NodeId ClockTree::insert_buffer(NodeId parent_id, NodeId child_id, const std::string& buffer_name,
                                const std::string& cell_type) {
    if (!contains_node(parent_id)) {
        throw std::out_of_range("Parent node does not exist");
    }
    if (!contains_node(child_id)) {
        throw std::out_of_range("Child node does not exist");
    }
    if (contains_name(buffer_name)) {
        throw std::invalid_argument("Duplicate clock tree node name: " + buffer_name);
    }
    if (node(child_id).parent_id != parent_id) {
        throw std::invalid_argument("Child is not driven by the specified parent");
    }
    if (node(parent_id).kind == NodeKind::FlipFlop) {
        throw std::invalid_argument("Flip-flop nodes cannot drive inserted buffers");
    }

    const auto& parent_children = node(parent_id).child_ids;
    const auto child_it = std::find(parent_children.begin(), parent_children.end(), child_id);
    if (child_it == parent_children.end()) {
        throw std::invalid_argument("Parent does not contain the specified child");
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

    node(parent_id).child_ids[child_index] = buffer_id;
    node(child_id).parent_id = buffer_id;
    return buffer_id;
}

void ClockTree::resize_buffer(NodeId node_id, const std::string& cell_type) {
    auto& clock_node = node(node_id);
    if (clock_node.kind != NodeKind::Buffer) {
        throw std::invalid_argument("Only buffer nodes can be resized");
    }
    clock_node.cell_type = cell_type;
}

bool ClockTree::empty() const {
    return nodes_.empty();
}

std::size_t ClockTree::size() const {
    return nodes_.size();
}

NodeId ClockTree::root_id() const {
    return root_id_;
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

const ClockNode& ClockTree::node(NodeId node_id) const {
    if (!contains_node(node_id)) {
        throw std::out_of_range("Clock tree node does not exist");
    }
    return nodes_[node_id];
}

ClockNode& ClockTree::node(NodeId node_id) {
    if (!contains_node(node_id)) {
        throw std::out_of_range("Clock tree node does not exist");
    }
    return nodes_[node_id];
}

const std::vector<ClockNode>& ClockTree::nodes() const {
    return nodes_;
}

std::size_t ClockTree::fanout(NodeId node_id) const {
    return node(node_id).child_ids.size();
}

std::vector<ClockTreeTraversalEntry> ClockTree::preorder_with_depth() const {
    std::vector<ClockTreeTraversalEntry> traversal;
    if (root_id_ == kInvalidNodeId) {
        return traversal;
    }

    std::vector<ClockTreeTraversalEntry> stack;
    stack.push_back(ClockTreeTraversalEntry{
        .node_id = root_id_,
        .depth = 0,
    });

    // Preserve child_ids order so the writer can reproduce clk_tree.structure ordering.
    while (!stack.empty()) {
        const auto current = stack.back();
        stack.pop_back();

        const auto& current_node = node(current.node_id);
        traversal.push_back(current);

        for (auto it = current_node.child_ids.rbegin(); it != current_node.child_ids.rend(); ++it) {
            stack.push_back(ClockTreeTraversalEntry{
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
            .node_id = node_id,
            .depth = depth,
        });

        for (const NodeId child_id : current_node.child_ids) {
            visit(child_id, depth + 1);
        }
    };

    visit(root_id_, 0);
    return traversal;
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

std::vector<NodeId> ClockTree::path_from_root(NodeId node_id) const {
    auto path = path_to_root(node_id);
    std::reverse(path.begin(), path.end());
    return path;
}

double ClockTree::clock_delay(NodeId node_id, const BufferLibrary& buffer_library,
                              Corner corner) const {
    double total_delay = 0.0;

    for (const NodeId path_node_id : path_from_root(node_id)) {
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
