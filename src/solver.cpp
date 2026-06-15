/**
 * @file solver.cpp
 * @brief Solver implementation unit.
 */

#include "solver.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "debug_progress.hpp"
#include "optimization/factory.hpp"
#include "optimization/optimizer.hpp"
#include "parser.hpp"

namespace cadd0040 {
namespace {

bool metrics_report_enabled() {
    const char* value = std::getenv("CADD0040_REPORT_METRICS");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

}  // namespace

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
    try {
        load_input();

        DebugProgress debug_progress = DebugProgress::from_environment();
        const Metrics baseline_metrics = evaluate(clock_tree_, data_path_graph_, buffer_library_);
        const bool report_metrics = metrics_report_enabled();

        if (report_metrics) {
            std::cout << "Initial baseline metrics: " << baseline_metrics << '\n';
            std::cout << "Initial Score = " << score(baseline_metrics, baseline_metrics) << '\n';
        }

        OptimizerContext optimizer_context{baseline_metrics, debug_progress};

        auto optimizer = make_optimizer(config_.optimizer_name);
        optimizer->run(clock_tree_, data_path_graph_, buffer_library_, optimizer_context);

        if (report_metrics) {
            const Metrics final_metrics = evaluate(clock_tree_, data_path_graph_, buffer_library_);
            std::cout << "======================================\n";
            std::cout << "Final metrics: " << final_metrics << '\n';
            std::cout << "Final Score = " << score(final_metrics, baseline_metrics) << '\n';
        }

        write_output(clock_tree_, config_.output_file);

        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "Solver failed: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}

}  // namespace cadd0040
