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
    nodes_.push_back(ClockNode{root_id,
                               root_name,
                               "",
                               NodeKind::ClockSource,
                               NodeOrigin::Original,
                               true,
                               kInvalidNodeId,
                               {}});
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
        node_id, node_name, cell_type, node_kind, NodeOrigin::Original, true, parent_id, {}});
    mutable_node(parent_id).child_ids.push_back(node_id);
    name_to_id_.emplace(node_name, node_id);
    add_edge(parent_id, node_id);
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

    const EdgeId edge_id = edge_between(parent_id, child_id);
    if (edge_id == kInvalidEdgeId) {
        std::cerr << "Failed to insert buffer " << buffer_name
                  << ": parent does not contain the specified child\n";
        return false;
    }

    return static_cast<bool>(
        insert_buffer_on_edge(edge_id, buffer_name, cell_type, buffer_library));
}

bool ClockTree::remove_buffer(const std::string& buffer_name) {
    const NodeId buffer_id = find_node(buffer_name);
    return static_cast<bool>(remove_inserted_buffer(buffer_id));
}

bool ClockTree::resize_buffer(const std::string& node_name, const std::string& cell_type,
                              const BufferLibrary& buffer_library) {
    const NodeId node_id = find_node(node_name);
    if (!contains_node(node_id)) {
        std::cerr << "Failed to resize buffer " << node_name << ": node does not exist\n";
        return false;
    }

    const auto& clock_node = node(node_id);
    if (clock_node.kind != NodeKind::Buffer || !clock_node.alive) {
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
    if (clock_node.cell_type == cell_type) {
        return true;
    }
    return static_cast<bool>(resize_buffer(node_id, cell_type, buffer_library));
}

ClockTreeEdit ClockTree::insert_buffer_on_edge(EdgeId edge_id, const std::string& buffer_name,
                                               const std::string& cell_type,
                                               const BufferLibrary& buffer_library) {
    if (edge_id >= edges_.size() || !edges_[edge_id].alive || contains_name(buffer_name)) {
        return {};
    }
    const auto& edge_ref = edges_[edge_id];
    const NodeId parent_id = edge_ref.parent_id;
    const NodeId child_id = edge_ref.child_id;
    if (!contains_node(parent_id) || !contains_node(child_id)) {
        return {};
    }
    if (!node(parent_id).alive || !node(child_id).alive ||
        node(parent_id).kind == NodeKind::FlipFlop) {
        return {};
    }
    const auto cell_it = buffer_library.find(cell_type);
    if (cell_it == buffer_library.end() || !cell_supports_fanout(cell_it->second, 1)) {
        return {};
    }

    const auto child_it = std::find(node(parent_id).child_ids.begin(), node(parent_id).child_ids.end(), child_id);
    if (child_it == node(parent_id).child_ids.end()) {
        return {};
    }
    const std::size_t child_index = static_cast<std::size_t>(child_it - node(parent_id).child_ids.begin());

    const NodeId buffer_id = nodes_.size();
    nodes_.push_back(ClockNode{buffer_id,
                               buffer_name,
                               cell_type,
                               NodeKind::Buffer,
                               NodeOrigin::Inserted,
                               true,
                               parent_id,
                               {child_id}});
    name_to_id_.emplace(buffer_name, buffer_id);

    mutable_node(parent_id).child_ids[child_index] = buffer_id;
    mutable_node(child_id).parent_id = buffer_id;
    set_edge_alive(edge_id, false);
    const EdgeId first_edge = add_edge(parent_id, buffer_id);
    const EdgeId second_edge = add_edge(buffer_id, child_id);

    mark_clock_arrivals_dirty_from(buffer_id);

    return ClockTreeEdit{ClockTreeEdit::Kind::InsertBuffer,
                         parent_id,
                         child_id,
                         buffer_id,
                         buffer_id,
                         edge_id,
                         first_edge,
                         second_edge,
                         kInvalidEdgeId,
                         child_index,
                         "",
                         cell_type};
}

ClockTreeEdit ClockTree::resize_buffer(NodeId node_id, const std::string& cell_type,
                                       const BufferLibrary& buffer_library) {
    if (!contains_node(node_id) || !node(node_id).alive || node(node_id).kind != NodeKind::Buffer) {
        return {};
    }
    const auto cell_it = buffer_library.find(cell_type);
    if (cell_it == buffer_library.end() ||
        !cell_supports_fanout(cell_it->second, fanout(node_id))) {
        return {};
    }
    auto& clock_node = mutable_node(node_id);
    if (clock_node.cell_type == cell_type) {
        return {};
    }

    const std::string old_cell_type = clock_node.cell_type;
    clock_node.cell_type = cell_type;
    mark_clock_arrivals_dirty_from(node_id);

    return ClockTreeEdit{ClockTreeEdit::Kind::ResizeBuffer,
                         kInvalidNodeId,
                         kInvalidNodeId,
                         node_id,
                         node_id,
                         kInvalidEdgeId,
                         kInvalidEdgeId,
                         kInvalidEdgeId,
                         kInvalidEdgeId,
                         0,
                         old_cell_type,
                         cell_type};
}

ClockTreeEdit ClockTree::remove_inserted_buffer(NodeId buffer_id) {
    if (!contains_node(buffer_id)) {
        return {};
    }
    auto& buffer_node = mutable_node(buffer_id);
    if (!buffer_node.alive || buffer_node.kind != NodeKind::Buffer ||
        buffer_node.origin != NodeOrigin::Inserted || buffer_node.parent_id == kInvalidNodeId ||
        buffer_node.child_ids.size() != 1) {
        return {};
    }

    const NodeId parent_id = buffer_node.parent_id;
    const NodeId child_id = buffer_node.child_ids.front();
    if (!contains_node(parent_id) || !contains_node(child_id)) {
        return {};
    }
    const auto child_it = std::find(node(parent_id).child_ids.begin(), node(parent_id).child_ids.end(), buffer_id);
    if (child_it == node(parent_id).child_ids.end()) {
        return {};
    }
    const std::size_t child_index = static_cast<std::size_t>(child_it - node(parent_id).child_ids.begin());
    const EdgeId first_edge = edge_between(parent_id, buffer_id);
    const EdgeId second_edge = edge_between(buffer_id, child_id);
    if (first_edge == kInvalidEdgeId || second_edge == kInvalidEdgeId) {
        return {};
    }

    mutable_node(parent_id).child_ids[child_index] = child_id;
    mutable_node(child_id).parent_id = parent_id;
    buffer_node.parent_id = kInvalidNodeId;
    buffer_node.child_ids.clear();
    buffer_node.alive = false;
    name_to_id_.erase(buffer_node.name);
    set_edge_alive(first_edge, false);
    set_edge_alive(second_edge, false);
    const EdgeId replacement_edge = add_edge(parent_id, child_id);

    mark_clock_arrivals_dirty_from(child_id);

    return ClockTreeEdit{ClockTreeEdit::Kind::RemoveInsertedBuffer,
                         parent_id,
                         child_id,
                         buffer_id,
                         child_id,
                         kInvalidEdgeId,
                         first_edge,
                         second_edge,
                         replacement_edge,
                         child_index,
                         buffer_node.cell_type,
                         ""};
}

void ClockTree::undo(const ClockTreeEdit& edit) {
    if (!edit) {
        return;
    }

    switch (edit.kind) {
        case ClockTreeEdit::Kind::InsertBuffer: {
            auto& parent = mutable_node(edit.parent_id);
            parent.child_ids[edit.parent_child_index] = edit.child_id;
            mutable_node(edit.child_id).parent_id = edit.parent_id;
            auto& buffer = mutable_node(edit.buffer_id);
            name_to_id_.erase(buffer.name);
            buffer.parent_id = kInvalidNodeId;
            buffer.child_ids.clear();
            buffer.alive = false;
            set_edge_alive(edit.original_edge_id, true);
            set_edge_alive(edit.first_edge_id, false);
            set_edge_alive(edit.second_edge_id, false);
            mark_clock_arrivals_dirty_from(edit.child_id);
            return;
        }
        case ClockTreeEdit::Kind::ResizeBuffer: {
            auto& buffer = mutable_node(edit.buffer_id);
            buffer.cell_type = edit.old_cell_type;
            mark_clock_arrivals_dirty_from(edit.buffer_id);
            return;
        }
        case ClockTreeEdit::Kind::RemoveInsertedBuffer: {
            auto& parent = mutable_node(edit.parent_id);
            parent.child_ids[edit.parent_child_index] = edit.buffer_id;
            auto& buffer = mutable_node(edit.buffer_id);
            buffer.parent_id = edit.parent_id;
            buffer.child_ids = {edit.child_id};
            buffer.alive = true;
            name_to_id_.emplace(buffer.name, edit.buffer_id);
            mutable_node(edit.child_id).parent_id = edit.buffer_id;
            set_edge_alive(edit.first_edge_id, true);
            set_edge_alive(edit.second_edge_id, true);
            set_edge_alive(edit.replacement_edge_id, false);
            mark_clock_arrivals_dirty_from(edit.buffer_id);
            return;
        }
        case ClockTreeEdit::Kind::None:
            return;
    }
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

bool ClockTree::contains_node_id(NodeId node_id) const { return contains_node(node_id); }

bool ClockTree::is_alive(NodeId node_id) const {
    return contains_node(node_id) && nodes_[node_id].alive;
}

NodeId ClockTree::node_id(const std::string& node_name) const { return find_node(node_name); }

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

const std::vector<ClockTreeEdge>& ClockTree::edges() const { return edges_; }

const ClockTreeEdge& ClockTree::edge(EdgeId edge_id) const {
    if (edge_id >= edges_.size()) {
        throw std::out_of_range("Clock tree edge does not exist");
    }
    return edges_[edge_id];
}

ClockTreeEdge& ClockTree::mutable_edge(EdgeId edge_id) {
    if (edge_id >= edges_.size()) {
        throw std::out_of_range("Clock tree edge does not exist");
    }
    return edges_[edge_id];
}

EdgeId ClockTree::edge_between(NodeId parent_id, NodeId child_id) const {
    for (const auto& edge : edges_) {
        if (edge.alive && edge.parent_id == parent_id && edge.child_id == child_id) {
            return edge.id;
        }
    }
    return kInvalidEdgeId;
}

std::vector<EdgeId> ClockTree::active_edge_ids() const {
    std::vector<EdgeId> edge_ids;
    edge_ids.reserve(edges_.size());
    for (const auto& edge : edges_) {
        if (edge.alive) {
            edge_ids.push_back(edge.id);
        }
    }
    return edge_ids;
}

std::vector<NodeId> ClockTree::buffer_nodes() const {
    std::vector<NodeId> node_ids;
    node_ids.reserve(nodes_.size());
    for (const auto& clock_node : nodes_) {
        if (clock_node.alive && clock_node.kind == NodeKind::Buffer) {
            node_ids.push_back(clock_node.id);
        }
    }
    return node_ids;
}

std::vector<NodeId> ClockTree::flip_flop_nodes() const {
    std::vector<NodeId> node_ids;
    node_ids.reserve(nodes_.size());
    for (const auto& clock_node : nodes_) {
        if (clock_node.alive && clock_node.kind == NodeKind::FlipFlop) {
            node_ids.push_back(clock_node.id);
        }
    }
    return node_ids;
}

std::size_t ClockTree::fanout(const std::string& node_name) const {
    return fanout(find_node(node_name));
}

std::size_t ClockTree::fanout(NodeId node_id) const { return node(node_id).child_ids.size(); }

std::vector<ClockTreeTraversalEntry> ClockTree::preorder_with_depth() const {
    std::vector<ClockTreeTraversalEntry> traversal;
    if (root_id_ == kInvalidNodeId || !node(root_id_).alive) {
        return traversal;
    }

    std::vector<TraversalStackEntry> stack;
    stack.push_back(TraversalStackEntry{root_id_, 0});

    // Preserve child_ids order so the writer can reproduce clk_tree.structure ordering.
    while (!stack.empty()) {
        const auto current = stack.back();
        stack.pop_back();

        const auto& current_node = node(current.node_id);
        if (!current_node.alive) {
            continue;
        }
        traversal.push_back(ClockTreeTraversalEntry{current_node.name, current.depth});

        for (auto it = current_node.child_ids.rbegin(); it != current_node.child_ids.rend(); ++it) {
            if (node(*it).alive) {
                stack.push_back(TraversalStackEntry{*it, current.depth + 1});
            }
        }
    }

    return traversal;
}

std::vector<ClockTreeTraversalEntry> ClockTree::preorder_with_depth_recursive() const {
    std::vector<ClockTreeTraversalEntry> traversal;
    if (root_id_ == kInvalidNodeId || !node(root_id_).alive) {
        return traversal;
    }

    // Recursive reference implementation; avoid this for very deep clock trees.
    const std::function<void(NodeId, std::size_t)> visit = [&](NodeId node_id, std::size_t depth) {
        const auto& current_node = node(node_id);
        if (!current_node.alive) {
            return;
        }
        traversal.push_back(ClockTreeTraversalEntry{current_node.name, depth});

        for (const NodeId child_id : current_node.child_ids) {
            if (node(child_id).alive) {
                visit(child_id, depth + 1);
            }
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
        if (!current_node.alive) {
            break;
        }
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
    if (root_id_ == kInvalidNodeId || !node(root_id_).alive) {
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
            if (node(*it).alive) {
                stack.push_back(*it);
            }
        }
    }
}

void ClockTree::mark_clock_arrivals_dirty_from(NodeId node_id) {
    if (!contains_node(node_id) || !node(node_id).alive) {
        return;
    }

    std::vector<NodeId> stack{node_id};
    while (!stack.empty()) {
        const NodeId current_id = stack.back();
        stack.pop_back();

        auto& current_node = mutable_node(current_id);
        if (!current_node.alive) {
            continue;
        }
        current_node.clock_arrival_dirty = true;

        for (auto it = current_node.child_ids.rbegin(); it != current_node.child_ids.rend(); ++it) {
            if (node(*it).alive) {
                stack.push_back(*it);
            }
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

EdgeId ClockTree::add_edge(NodeId parent_id, NodeId child_id) {
    const EdgeId edge_id = edges_.size();
    edges_.push_back(ClockTreeEdge{edge_id, parent_id, child_id, true});
    return edge_id;
}

void ClockTree::set_edge_alive(EdgeId edge_id, bool alive) {
    if (edge_id < edges_.size()) {
        edges_[edge_id].alive = alive;
    }
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

    if (root_id_ == kInvalidNodeId) {
        return total_area;
    }

    std::vector<NodeId> stack{root_id_};
    while (!stack.empty()) {
        const NodeId node_id = stack.back();
        stack.pop_back();

        const auto& clock_node = node(node_id);
        if (!clock_node.alive) {
            continue;
        }
        if (clock_node.kind != NodeKind::Buffer) {
            for (auto it = clock_node.child_ids.rbegin(); it != clock_node.child_ids.rend(); ++it) {
                if (node(*it).alive) {
                    stack.push_back(*it);
                }
            }
            continue;
        }

        total_area += buffer_area(find_buffer_cell(buffer_library, clock_node.cell_type));

        for (auto it = clock_node.child_ids.rbegin(); it != clock_node.child_ids.rend(); ++it) {
            if (node(*it).alive) {
                stack.push_back(*it);
            }
        }
    }

    return total_area;
}

std::ostream& operator<<(std::ostream& os, const ClockTree& clock_tree) {
    if (clock_tree.root_id_ == kInvalidNodeId || !clock_tree.nodes_[clock_tree.root_id_].alive) {
        return os;
    }

    const auto& root = clock_tree.nodes_[clock_tree.root_id_];
    os << "Root: " << root.name << '\n';

    std::vector<TraversalStackEntry> stack;
    for (auto it = root.child_ids.rbegin(); it != root.child_ids.rend(); ++it) {
        stack.push_back(TraversalStackEntry{*it, 1});
    }

    while (!stack.empty()) {
        const auto current = stack.back();
        stack.pop_back();

        const auto& current_node = clock_tree.nodes_[current.node_id];
        if (!current_node.alive) {
            continue;
        }
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
            if (clock_tree.nodes_[*it].alive) {
                stack.push_back(TraversalStackEntry{*it, current.depth + 1});
            }
        }
    }

    return os;
}

}  // namespace cadd0040
