/**
 * @file solver.hpp
 * @brief Overall solver workflow controller.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "app.hpp"
#include "clock_tree.hpp"
#include "datapath_graph.hpp"
#include "evaluator.hpp"
#include "optimization/optimizer.hpp"
#include "types.hpp"

namespace cadd0040 {

/**
 * @brief Parsed input objects used by the solver workflow.
 */
struct InputData {
    ClockTree clock_tree;
    DataPathGraph data_path_graph;
    BufferLibrary buffer_library;
};

/**
 * @brief Result of checking whether the generated clock tree is legal.
 */
struct ValidationResult {
    bool success = false;
    std::vector<std::string> errors;
};

/**
 * @brief Coordinates input loading, optimization, validation, and output writing.
 */
class Solver {
public:
    explicit Solver(AppConfig config);

    /**
     * @brief Runs the complete solver flow and returns a process exit code.
     */
    int run();

private:
    InputData load_input(const AppConfig& config) const;
    Metrics evaluate(const ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                     const BufferLibrary& buffer_library) const;
    ClockTree optimize(const ClockTree& clock_tree, const DataPathGraph& data_path_graph) const;
    ValidationResult validate(const ClockTree& clock_tree,
                              const BufferLibrary& buffer_library) const;
    void write_output(const ClockTree& clock_tree, const std::filesystem::path& output_path) const;

    AppConfig config_;
    Evaluator evaluator_;
    Optimizer optimizer_;
};

}  // namespace cadd0040
