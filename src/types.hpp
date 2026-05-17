/**
 * @file types.hpp
 * @brief Shared lightweight data types used across the solver modules.
 */

#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace cadd0040 {

// FIXME: @BenLai95 @AaronWu-train @liuchengLYC The types defined in this file are just
// placeholders. You should feel free to modify them as needed, as long as the overall solver
// workflow is not affected.

using NodeId = std::size_t;
using EdgeId = std::size_t;

// Variable to check whether the node is root or not existing node
inline constexpr NodeId kInvalidNodeId = std::numeric_limits<NodeId>::max();

enum class Corner {
    SS,
    FF,
};

enum class NodeKind {
    ClockSource,
    Buffer,
    FlipFlop,
};

struct BufferCell {
    std::string name;
    double width = 0.0;
    double height = 0.0;
    double area = 0.0;
    std::vector<double> ss_delays_by_fanout;
    std::vector<double> ff_delays_by_fanout;
};

using BufferLibrary = std::unordered_map<std::string, BufferCell>;

struct Metrics {
    double setup_tns = 0.0;
    double setup_wns = 0.0;
    double hold_tns = 0.0;
    double hold_wns = 0.0;
    double area = 0.0;
};

}  // namespace cadd0040
