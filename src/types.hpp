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

// Sentinel value used for missing nodes or nodes without a parent.
inline constexpr NodeId kInvalidNodeId = std::numeric_limits<NodeId>::max();
inline constexpr EdgeId kInvalidEdgeId = std::numeric_limits<EdgeId>::max();

enum class Corner {
    SS,
    FF,
};

enum class NodeKind {
    ClockSource,
    Buffer,
    FlipFlop,
};

struct CornerDelay {
    double ss = 0.0;
    double ff = 0.0;
};

struct TimingConstraint {
    double clock_period = 0.0;
    double setup_ratio = 0.08;
    double hold_ratio = 0.05;

    double setup_time() const { return setup_ratio * clock_period; }
    double hold_time() const { return hold_ratio * clock_period; }
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
