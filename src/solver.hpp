/**
 * @file solver.hpp
 * @brief Overall solver workflow controller.
 */

#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "app.hpp"
#include "clock_tree.hpp"
#include "datapath_graph.hpp"
#include "evaluation.hpp"
#include "optimization/optimizer.hpp"
#include "types.hpp"

namespace cadd0040 {

/**
 * @brief Coordinates input loading, optimization, validation, and output writing.
 */
class Solver {
public:
    explicit Solver(AppConfig config) : config_(std::move(config)) {}

    /**
     * @brief Runs the complete solver flow and returns a process exit code.
     */
    int run();

private:
    void load_input();
    void write_output(const ClockTree& clock_tree, const std::filesystem::path& output_path);
    void write_output_atomically(const ClockTree& clock_tree,
                                 const std::filesystem::path& output_path);

    AppConfig config_;
    BufferLibrary buffer_library_;
    ClockTree clock_tree_;
    DataPathGraph data_path_graph_;
};

}  // namespace cadd0040
