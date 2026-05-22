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
 *
 * Public APIs use node names as the primary key. NodeId is kept as an internal compact
 * index for storage and traversal, while callers should pass instance names such as
 * BUF_0 or FF_37. This keeps parser, optimizer, evaluator, and writer code aligned
 * with the contest file formats, which identify components by name.
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
    // Instance name in clk_tree.structure, e.g. BUF_0 or FF_37. Must be unique.
    std::string name;
    // Library/type name in parentheses, e.g. REALBUF_X8 or FIFO.
    // Buffer nodes use this to look up buf.lib delay/area; FF nodes keep it for output.
    std::string cell_type;
    NodeKind kind = NodeKind::Buffer;
    NodeId parent_id = kInvalidNodeId;
    std::vector<NodeId> child_ids;
    // Cached clock arrival times from root to this node. Dirty/lazy policy should live outside
    // ClockTree; call update_clock_times() or update_clock_times_from() to refresh cached values.
    double ss_clock_time = 0.0;
    double ff_clock_time = 0.0;
};

/**
 * @brief One node visited during a clock-tree traversal.
 *
 * node_name identifies the visited ClockNode. depth is the tree depth relative to
 * the clock source: root depth is 0, and the value for non-root nodes matches
 * the bracket level used by clk_tree.structure output lines.
 */
struct ClockTreeTraversalEntry {
    std::string node_name;
    std::size_t depth = 0;
};

class ClockTree {
public:
    // Empty construction is intentional; parser.cpp populates the tree from input files.
    ClockTree() = default;
    ~ClockTree() = default;

    void add_root(const std::string& root_name);
    void add_node(const std::string& node_name, const std::string& cell_type, NodeKind node_kind,
                  const std::string& parent_name);
    /**
     * @brief Inserts a new buffer between an existing parent-child edge.
     *
     * @return true if the buffer was inserted. Returns false and leaves the tree unchanged if
     * parent_name/child_name are invalid, buffer_name already exists, the parent-child relation is
     * invalid, parent_name is a flip-flop, cell_type is not in buffer_library, or the inserted
     * buffer's fanout is unsupported by the cell's SS/FF delay tables.
     */
    bool insert_buffer(const std::string& parent_name, const std::string& child_name,
                       const std::string& buffer_name, const std::string& cell_type,
                       const BufferLibrary& buffer_library);

    /**
     * @brief Changes an existing buffer node to another buffer cell type.
     *
     * @return true if the cell type was changed. Returns false and leaves the node unchanged if
     * node_name is invalid, node_name is not a buffer, cell_type is not in buffer_library, or the
     * replacement cell does not support the node's current fanout in both SS/FF delay tables.
     */
    bool resize_buffer(const std::string& node_name, const std::string& cell_type,
                       const BufferLibrary& buffer_library);

    bool empty() const;
    std::size_t size() const;
    const std::string& root_name() const;
    bool contains_name(const std::string& node_name) const;
    const ClockNode& node(const std::string& node_name) const;
    const std::vector<ClockNode>& nodes() const;

    std::size_t fanout(const std::string& node_name) const;
    // Iterative preorder traversal for clk_tree.structure writing. Prefer this in production
    // because it avoids call-stack overflow on deep testcase trees while preserving child order.
    std::vector<ClockTreeTraversalEntry> preorder_with_depth() const;
    // Recursive DFS preorder traversal with the same output order. Keep it for small-tree tests,
    // debugging, and comparison against the iterative implementation.
    std::vector<ClockTreeTraversalEntry> preorder_with_depth_recursive() const;
    std::vector<std::string> path_to_root(const std::string& node_name) const;
    std::vector<std::string> path_from_root(const std::string& node_name) const;

    // Returns the cached clock arrival time for the requested corner. Call an update_* function
    // after parsing, buffer insertion, or buffer resizing before relying on this cached value.
    double clock_time(const std::string& node_name, Corner corner) const;
    // Recomputes cached SS/FF clock arrival times for the whole tree from the root.
    // Use this after bulk edits or when no smaller dirty subtree is known.
    void update_clock_times(const BufferLibrary& buffer_library);
    // Recomputes cached SS/FF clock arrival times for node_name and its descendants.
    // Optimizer code can call this for a dirty subtree after local structural or sizing changes.
    void update_clock_times_from(const std::string& node_name, const BufferLibrary& buffer_library);

    double clock_delay(const std::string& node_name, const BufferLibrary& buffer_library,
                       Corner corner) const;
    double area(const BufferLibrary& buffer_library) const;

private:
    bool contains_node(NodeId node_id) const;
    NodeId find_node(const std::string& node_name) const;
    const ClockNode& node(NodeId node_id) const;
    ClockNode& mutable_node(NodeId node_id);
    std::size_t fanout(NodeId node_id) const;
    std::vector<NodeId> path_to_root(NodeId node_id) const;
    std::vector<NodeId> path_from_root(NodeId node_id) const;
    void update_clock_times_from(NodeId node_id, const BufferLibrary& buffer_library);

    NodeId root_id_ = kInvalidNodeId;
    std::vector<ClockNode> nodes_;
    std::unordered_map<std::string, NodeId> name_to_id_;
};

}  // namespace cadd0040
