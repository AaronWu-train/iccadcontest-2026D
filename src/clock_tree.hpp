/**
 * @file clock_tree.hpp
 * @brief Clock-tree data model and basic construction interface.
 */

#pragma once

#include <string>
#include <vector>

#include "types.hpp"

namespace cadd0040 {

struct ClockNode {
    NodeId id = 0;
    std::string name;
    std::string cell_type;
    NodeKind kind = NodeKind::Buffer;
    NodeId parent_id = 0;
    std::vector<NodeId> child_ids;
};

class ClockTree {
public:
    NodeId add_root(const std::string& root_name);
    NodeId add_node(const std::string& node_name, const std::string& cell_type, NodeKind node_kind,
                    NodeId parent_id);

    double area(const BufferLibrary& buffer_library) const;

private:
    std::vector<ClockNode> nodes_;
};

}  // namespace cadd0040
