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
 * Optimizer APIs use stable NodeId and EdgeId handles so hot loops do not depend on
 * string lookup. Name-based APIs remain available for parser, writer, debug, and
 * compatibility paths because the contest file formats identify components by name.
 */

#pragma once

#include <cstddef>
#include <ostream>
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
    NodeOrigin origin = NodeOrigin::Original;
    bool alive = true;
    NodeId parent_id = kInvalidNodeId;
    std::vector<NodeId> child_ids;
    // Cached clock arrival times from root to this node. SS/FF here are timing corners, not
    // flip-flop or data-path reports. ClockTree owns the dirty/lazy policy for these values.
    double ss_clock_arrival = 0.0;
    double ff_clock_arrival = 0.0;
    bool clock_arrival_dirty = true;
};

struct ClockTreeEdge {
    EdgeId id = kInvalidEdgeId;
    NodeId parent_id = kInvalidNodeId;
    NodeId child_id = kInvalidNodeId;
    bool alive = true;
};

struct ClockTreeEdit {
    enum class Kind {
        None,
        InsertBuffer,
        ResizeBuffer,
        RemoveInsertedBuffer,
    };

    Kind kind = Kind::None;
    NodeId parent_id = kInvalidNodeId;
    NodeId child_id = kInvalidNodeId;
    NodeId buffer_id = kInvalidNodeId;
    NodeId affected_root_id = kInvalidNodeId;
    EdgeId original_edge_id = kInvalidEdgeId;
    EdgeId first_edge_id = kInvalidEdgeId;
    EdgeId second_edge_id = kInvalidEdgeId;
    EdgeId replacement_edge_id = kInvalidEdgeId;
    std::size_t parent_child_index = 0;
    std::string old_cell_type;
    std::string new_cell_type;

    bool valid() const { return kind != Kind::None; }
    explicit operator bool() const { return valid(); }
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
    // Complexity notes:
    // N = number of clock-tree nodes, H = height of a queried root-to-node path,
    // S = number of nodes in an updated subtree. unordered_map name lookups are average O(1),
    // but worst-case O(N) under heavy hash collisions or rehashing.

    // Empty construction is intentional; parser.cpp populates the tree from input files.
    // Time: O(1).
    ClockTree() = default;
    // Time: O(N + total child references) to release owned containers.
    ~ClockTree() = default;

    // Time: average O(1), excluding string copy/hash costs and occasional map/vector rehashing.
    void add_root(const std::string& root_name);
    // Time: average O(1), excluding string copy/hash costs and occasional map/vector rehashing.
    void add_node(const std::string& node_name, const std::string& cell_type, NodeKind node_kind,
                  const std::string& parent_name);
    /**
     * @brief Inserts a new buffer between an existing parent-child edge.
     * Time: average O(E + F + S), where E is edge count, F is parent fanout, and S is the
     * affected child subtree size.
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
     * @brief Removes a single-fanout buffer by reconnecting its parent directly to its child.
     * Time: average O(E + F + S), where E is edge count, F is parent fanout, and S is the
     * affected child subtree size.
     *
     * This is intended for optimizer rollback and visualization traces. It preserves existing
     * NodeId values by leaving the old node storage unreachable from the root.
     *
     * @return true if the buffer was removed. Returns false and leaves the tree unchanged if
     * buffer_name is invalid, is not a buffer, is root-like, or does not have exactly one child.
     */
    bool remove_buffer(const std::string& buffer_name);

    /**
     * @brief Changes an existing buffer node to another buffer cell type.
     * Time: average O(S), excluding string hash costs, where S is the resized buffer subtree size.
     *
     * @return true if the cell type was changed. Returns false and leaves the node unchanged if
     * node_name is invalid, node_name is not a buffer, cell_type is not in buffer_library, or the
     * replacement cell does not support the node's current fanout in both SS/FF delay tables.
     */
    bool resize_buffer(const std::string& node_name, const std::string& cell_type,
                       const BufferLibrary& buffer_library);

    ClockTreeEdit insert_buffer_on_edge(EdgeId edge_id, const std::string& buffer_name,
                                        const std::string& cell_type,
                                        const BufferLibrary& buffer_library);
    ClockTreeEdit resize_buffer(NodeId node_id, const std::string& cell_type,
                                const BufferLibrary& buffer_library);
    ClockTreeEdit remove_inserted_buffer(NodeId buffer_id);
    void undo(const ClockTreeEdit& edit);

    // Time: O(1).
    bool empty() const;
    // Time: O(1).
    std::size_t size() const;
    // Time: O(1).
    const std::string& root_name() const;
    // Time: average O(1), excluding string hash cost.
    bool contains_name(const std::string& node_name) const;
    bool contains_node_id(NodeId node_id) const;
    bool is_alive(NodeId node_id) const;
    NodeId node_id(const std::string& node_name) const;
    // Time: average O(1), excluding string hash cost.
    const ClockNode& node(const std::string& node_name) const;
    const ClockNode& node(NodeId node_id) const;
    // Returns the owned node array by reference; no node data is copied.
    // Time: O(1).
    const std::vector<ClockNode>& nodes() const;
    const std::vector<ClockTreeEdge>& edges() const;
    const ClockTreeEdge& edge(EdgeId edge_id) const;
    EdgeId edge_between(NodeId parent_id, NodeId child_id) const;
    std::vector<EdgeId> active_edge_ids() const;
    std::vector<NodeId> buffer_nodes() const;
    std::vector<NodeId> flip_flop_nodes() const;

    // Time: average O(1), excluding string hash cost.
    std::size_t fanout(const std::string& node_name) const;
    // Iterative preorder traversal for clk_tree.structure writing. Prefer this in production
    // because it avoids call-stack overflow on deep testcase trees while preserving child order.
    // Time: O(N).
    std::vector<ClockTreeTraversalEntry> preorder_with_depth() const;
    // Recursive DFS preorder traversal with the same output order. Keep it for small-tree tests,
    // debugging, and comparison against the iterative implementation.
    // Time: O(N).
    std::vector<ClockTreeTraversalEntry> preorder_with_depth_recursive() const;
    // Time: average O(H), excluding string hash cost.
    std::vector<std::string> path_to_root(const std::string& node_name) const;
    // Time: average O(H), excluding string hash cost.
    std::vector<std::string> path_from_root(const std::string& node_name) const;

    // Recomputes cached SS/FF clock arrival times for the whole tree from the root and clears all
    // per-node dirty flags.
    // Time: O(N), plus average O(1) buffer-library lookup per buffer.
    void update_clock_times(const BufferLibrary& buffer_library);
    // Recomputes cached SS/FF clock arrival times for node_name and its descendants, first ensuring
    // the parent path is current. Optimizer code can call this to eagerly clean a local subtree.
    // Time: average O(H + S), excluding initial string hash cost, plus average O(1)
    // buffer-library lookup per buffer in the updated subtree.
    void update_clock_times_from(const std::string& node_name, const BufferLibrary& buffer_library);

    // Lazily refreshes cached clock arrivals if needed, then returns the requested node's clock
    // arrival. After a local edit this recomputes only dirty nodes on the queried root-to-node
    // path. Time: average O(H), plus O(N) if switching to a different BufferLibrary object.
    double clock_delay(const std::string& node_name, const BufferLibrary& buffer_library,
                       Corner corner);

    // Lazily refreshes cached clock arrivals if needed, then returns capture minus launch clock
    // arrival for two flip-flop endpoints. After a local edit this recomputes only dirty nodes on
    // the two queried root-to-endpoint paths.
    // Time: average O(H_launch + H_capture), plus O(N) if switching BufferLibrary objects.
    double clock_skew(const std::string& launch_flip_flop_name,
                      const std::string& capture_flip_flop_name,
                      const BufferLibrary& buffer_library, Corner corner);

    // Time: O(N), plus average O(1) buffer-library lookup per buffer.
    double area(const BufferLibrary& buffer_library) const;

    friend std::ostream& operator<<(std::ostream& os, const ClockTree& clock_tree);

private:
    // Time: O(1).
    bool contains_node(NodeId node_id) const;
    // Time: average O(1), excluding string hash cost.
    NodeId find_node(const std::string& node_name) const;
    // Time: O(1).
    ClockNode& mutable_node(NodeId node_id);
    ClockTreeEdge& mutable_edge(EdgeId edge_id);
    // Time: O(1).
    std::size_t fanout(NodeId node_id) const;
    // Time: O(H).
    std::vector<NodeId> path_to_root(NodeId node_id) const;
    // Time: O(H).
    std::vector<NodeId> path_from_root(NodeId node_id) const;
    // Time: O(1).
    double cached_clock_arrival(NodeId node_id, Corner corner) const;
    // Time: O(1), plus average O(1) buffer-library lookup for buffer nodes.
    void update_clock_arrival(NodeId node_id, const BufferLibrary& buffer_library);
    // Time: O(S), plus average O(1) buffer-library lookup per buffer in the updated subtree.
    void update_clock_arrival_subtree(NodeId node_id, const BufferLibrary& buffer_library);
    // Time: O(S).
    void mark_clock_arrivals_dirty_from(NodeId node_id);
    // Time: O(H), plus O(N) if switching BufferLibrary objects.
    void ensure_clock_arrival(NodeId node_id, const BufferLibrary& buffer_library);
    // Time: O(N) if switching BufferLibrary objects; otherwise O(1).
    void use_clock_arrival_library(const BufferLibrary& buffer_library);
    EdgeId add_edge(NodeId parent_id, NodeId child_id);
    void set_edge_alive(EdgeId edge_id, bool alive);

    NodeId root_id_ = kInvalidNodeId;
    std::vector<ClockNode> nodes_;
    std::vector<ClockTreeEdge> edges_;
    std::unordered_map<std::string, NodeId> name_to_id_;
    const BufferLibrary* clock_arrival_buffer_library_ = nullptr;
};

}  // namespace cadd0040
