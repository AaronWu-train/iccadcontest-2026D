/**
 * @file clock_tree.hpp
 * @brief Clock-tree data model and basic construction interface.
 *
 * ClockTree stores the clock network as a rooted tree. Each ClockNode has one parent
 * except the clock source, and may drive zero or more children. Buffer and flip-flop
 * nodes keep their testcase names and cell types so the parser, optimizer, evaluator,
 * and output writer can share the same structure without translating between models.
 *
 * This class owns the basic structural invariants that every later stage depends on:
 * node names are unique, root creation is explicit, flip-flops are sinks, and inserted
 * buffers are placed between an existing parent-child pair without changing the
 * relative order of existing components. Higher-level legality checks, such as NEW_BUF
 * numbering or full output-format validation, should still live in the solver/parser
 * validation layer.
 *
 * Timing helpers such as clock_delay() use the buffer library's fanout-indexed delay
 * tables. For a buffer with fanout N, the Nth entry in the SS/FF delay table is used.
 * Area helpers sum buffer cell areas only; clock source and flip-flop nodes are not
 * counted as buffer-library area.
 */

#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "types.hpp"

namespace cadd0040 {

// TODO: @liuchengLYC - you can modify all the code in this file as needed, as long as the overall
// solver workflow is not affected.

struct ClockNode {
    NodeId id = 0;
    std::string name;
    std::string cell_type;
    NodeKind kind = NodeKind::Buffer;
    NodeId parent_id = kInvalidNodeId;
    std::vector<NodeId> child_ids;
};

struct ClockTreeTraversalEntry {
    NodeId node_id = kInvalidNodeId;
    std::size_t depth = 0;
};

class ClockTree {
public:
    NodeId add_root(const std::string& root_name);
    NodeId add_node(const std::string& node_name, const std::string& cell_type, NodeKind node_kind,
                    NodeId parent_id);
    NodeId insert_buffer(NodeId parent_id, NodeId child_id, const std::string& buffer_name,
                         const std::string& cell_type);

    void resize_buffer(NodeId node_id, const std::string& cell_type);

    bool empty() const;
    std::size_t size() const;
    NodeId root_id() const;
    bool contains_node(NodeId node_id) const;
    bool contains_name(const std::string& node_name) const;
    NodeId find_node(const std::string& node_name) const;
    const ClockNode& node(NodeId node_id) const;
    const std::vector<ClockNode>& nodes() const;

    std::size_t fanout(NodeId node_id) const;
    // Iterative preorder for clk_tree.structure output. Root depth is 0; child lines use depth.
    std::vector<ClockTreeTraversalEntry> preorder_with_depth() const;
    // Recursive DFS variant for small trees or tests; use preorder_with_depth() for large cases.
    std::vector<ClockTreeTraversalEntry> preorder_with_depth_recursive() const;
    std::vector<NodeId> path_to_root(NodeId node_id) const;
    std::vector<NodeId> path_from_root(NodeId node_id) const;
    double clock_delay(NodeId node_id, const BufferLibrary& buffer_library, Corner corner) const;
    double area(const BufferLibrary& buffer_library) const;

private:
    ClockNode& mutable_node(NodeId node_id);

    NodeId root_id_ = kInvalidNodeId;
    std::vector<ClockNode> nodes_;
    std::unordered_map<std::string, NodeId> name_to_id_;
};

}  // namespace cadd0040
