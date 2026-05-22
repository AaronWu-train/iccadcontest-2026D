/**
 * @file optimizer.cpp
 * @brief Optimizer implementation unit.
 */

#include "optimizer.hpp"

#include <iostream>
#include <memory>

namespace cadd0040 {

std::unique_ptr<Optimizer> make_optimizer(OptimizerType type) {
    switch (type) {
        case OptimizerType::Dummy:
            return std::make_unique<DummyOptimizer>();
    }

    throw std::runtime_error("Unknown optimizer type");
}

void DummyOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                         const BufferLibrary& buffer_library) {
    std::cerr << "DummyOptimizer: No optimization performed. This is a no-op optimizer for testing."
              << std::endl;
    // No-op optimizer for testing and benchmarking infrastructure.
}

}  // namespace cadd0040
