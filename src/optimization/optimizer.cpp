/**
 * @file optimizer.cpp
 * @brief Optimizer implementation unit.
 */

#include "optimizer.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>

namespace cadd0040 {

void DummyOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                         const BufferLibrary& buffer_library, const OptimizerContext& context) {
    (void)clock_tree;
    (void)data_path_graph;
    (void)buffer_library;
    (void)context;

    context.debug_progress.log([](std::ostream& os) {
        os << "DummyOptimizer: No optimization performed. This is a no-op optimizer for testing.\n";
    });
    // No-op optimizer for testing and benchmarking infrastructure.
}

}  // namespace cadd0040
