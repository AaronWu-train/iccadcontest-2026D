/**
 * @file clock_tree.hpp
 * @brief Clock-tree data model and basic construction interface.
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
    std::vector<NodeId> path_to_root(NodeId node_id) const;
    std::vector<NodeId> path_from_root(NodeId node_id) const;
    double clock_delay(NodeId node_id, const BufferLibrary& buffer_library, Corner corner) const;
    double area(const BufferLibrary& buffer_library) const;

private:
    ClockNode& node(NodeId node_id);

    NodeId root_id_ = kInvalidNodeId;
    std::vector<ClockNode> nodes_;
    std::unordered_map<std::string, NodeId> name_to_id_;
};

}  // namespace cadd0040
