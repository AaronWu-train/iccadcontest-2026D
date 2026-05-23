/**
 * @file app.cpp
 * @brief Command-line parsing and application bootstrap helpers.
 */

#include "app.hpp"

#include <CLI/CLI.hpp>
#include <filesystem>
#include <string>

#include "optimization/factory.hpp"
#include "solver.hpp"

namespace cadd0040 {
namespace {

std::string program_name(char* argv0) { return std::filesystem::path(argv0).filename().string(); }

void configure_cli_app(CLI::App& app, std::filesystem::path& testcase_dir,
                       std::filesystem::path& output_file, std::string& optimizer_name) {
    app.add_option("testcase_dir", testcase_dir,
                   "Directory containing clk_tree.structure, buf.lib, "
                   "SS_delay.rpt, and FF_delay.rpt")
        ->required()
        ->check(CLI::ExistingDirectory);

    app.add_option("output_file", output_file, "Path to write modified_clk_tree.structure")
        ->required();

    app.add_option("--optimizer", optimizer_name, "Optimization strategy")
        ->default_val(cadd0040::kDefaultOptimizerName)
        ->check(CLI::IsMember(cadd0040::available_optimizers()));
}

}  // namespace

AppConfig parse_arguments(int argc, char** argv) {
    std::filesystem::path testcase_dir;
    std::filesystem::path output_file;
    std::string optimizer_name;

    CLI::App app{"ICCAD Contest 2026 Problem D solver", program_name(argv[0])};
    configure_cli_app(app, testcase_dir, output_file, optimizer_name);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        std::exit(app.exit(error));
    }

    return AppConfig(testcase_dir, output_file, optimizer_name);
}

int run(const AppConfig& config) {
    Solver solver(config);
    int status = solver.run();
    return status;
}

}  // namespace cadd0040
