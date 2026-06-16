/**
 * @file app.cpp
 * @brief Command-line parsing and application bootstrap helpers.
 */

#include "app.hpp"

#include <CLI/CLI.hpp>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "optimization/factory.hpp"
#include "solver.hpp"

namespace cadd0040 {
namespace {

std::string program_name(char* argv0) { return std::filesystem::path(argv0).filename().string(); }

bool is_registered_optimizer(std::string_view optimizer_name) {
    const auto optimizers = available_optimizers();
    return std::find(optimizers.begin(), optimizers.end(), optimizer_name) != optimizers.end();
}

void configure_cli_app(CLI::App& app, std::filesystem::path& testcase_dir,
                       std::filesystem::path& output_file, std::string& optimizer_name,
                       std::filesystem::path& config_file) {
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

    app.add_option("--config", config_file,
                   "Optional optimizer experiment config file (INI key=value format)")
        ->check(CLI::ExistingFile);
}

}  // namespace

AppConfig parse_arguments(int argc, char** argv) {
    std::filesystem::path testcase_dir;
    std::filesystem::path output_file;
    std::string optimizer_name;
    std::filesystem::path config_file;

    CLI::App app{"ICCAD Contest 2026 Problem D solver", program_name(argv[0])};
    configure_cli_app(app, testcase_dir, output_file, optimizer_name, config_file);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        std::exit(app.exit(error));
    }

    std::optional<OptimizerConfigFile> optimizer_config;
    if (!config_file.empty()) {
        optimizer_config = parse_optimizer_config_file(config_file);
        if (optimizer_config->optimizer.has_value()) {
            optimizer_name = *optimizer_config->optimizer;
            if (!is_registered_optimizer(optimizer_name)) {
                throw std::runtime_error("Unknown optimizer in config file: " + optimizer_name);
            }
        }
    }

    return AppConfig(testcase_dir, output_file, optimizer_name, std::move(optimizer_config));
}

int run(const AppConfig& config) {
    Solver solver(config);
    int status = solver.run();
    return status;
}

}  // namespace cadd0040
