/**
 * @file types.hpp
 * @brief Shared lightweight data types used across the solver modules.
 */

#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

namespace cadd0040 {

using NodeId = std::size_t;
using EdgeId = std::size_t;

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
    double ss_delay = 0.0;
    double ff_delay = 0.0;
    double area = 0.0;
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
