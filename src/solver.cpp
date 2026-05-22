/**
 * @file solver.cpp
 * @brief Solver implementation unit.
 */

#include "solver.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "parser.hpp"

namespace cadd0040 {

void Solver::load_input() {
    parse_clock_tree(config_.clk_tree_path, clock_tree_);
    parse_data_path_graph(config_.ff_delay_path, config_.ss_delay_path, data_path_graph_);
    parse_buffer_library(config_.buflib_path, buffer_library_);
}

void Solver::write_output(const ClockTree& clock_tree, const std::filesystem::path& output_path) {
    std::ofstream output(output_path);

    if (!output) {
        throw std::runtime_error("Failed to open output file: " + output_path.string());
    }

    output << clock_tree;

    if (!output) {
        throw std::runtime_error("Failed to write output file: " + output_path.string());
    }
}

int Solver::run() {
    load_input();

    // TODO: Call optimizer here.

    write_output(clock_tree_, config_.output_file);
}

}  // namespace cadd0040
