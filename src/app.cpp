/**
 * @file app.cpp
 * @brief Command-line parsing and application bootstrap helpers.
 */

#include "app.hpp"

#include <CLI/CLI.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <limits>
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

unsigned int parse_seed_option(const std::string& seed_text) {
    if (seed_text.empty() || !std::all_of(seed_text.begin(), seed_text.end(),
                                          [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        throw std::runtime_error("--seed must be a non-negative integer");
    }

    const unsigned long parsed = std::stoul(seed_text);
    if (parsed > std::numeric_limits<unsigned int>::max()) {
        throw std::runtime_error("--seed is too large for an unsigned int");
    }
    return static_cast<unsigned int>(parsed);
}

std::chrono::seconds parse_seconds_option(const std::string& seconds_text) {
    if (seconds_text.empty() ||
        !std::all_of(seconds_text.begin(), seconds_text.end(),
                     [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        throw std::runtime_error("--seconds must be a non-negative integer");
    }

    return std::chrono::seconds{std::stoll(seconds_text)};
}

void configure_cli_app(CLI::App& app, std::filesystem::path& testcase_dir,
                       std::filesystem::path& output_file, std::string& optimizer_name,
                       std::filesystem::path& config_file, std::string& seed_text,
                       std::string& seconds_text, bool& debug_output) {
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

    app.add_option("--seed", seed_text, "Global optimizer RNG seed for seed-aware optimizers")
        ->check(CLI::NonNegativeNumber);

    app.add_option("--seconds", seconds_text, "Global optimizer time budget in seconds")
        ->check(CLI::NonNegativeNumber);

    app.add_flag("--debug", debug_output, "Enable debug optimizer progress output");
}

}  // namespace

AppConfig parse_arguments(int argc, char** argv) {
    std::filesystem::path testcase_dir;
    std::filesystem::path output_file;
    std::string optimizer_name;
    std::filesystem::path config_file;
    std::string seed_text;
    std::string seconds_text;
    bool debug_output = false;

    CLI::App app{"ICCAD Contest 2026 Problem D solver", program_name(argv[0])};
    configure_cli_app(app, testcase_dir, output_file, optimizer_name, config_file, seed_text,
                      seconds_text, debug_output);

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
    if (!seed_text.empty()) {
        if (!optimizer_config.has_value()) {
            optimizer_config.emplace();
        }
        optimizer_config->seed = parse_seed_option(seed_text);
    }
    if (!seconds_text.empty()) {
        if (!optimizer_config.has_value()) {
            optimizer_config.emplace();
        }
        optimizer_config->time_budget = parse_seconds_option(seconds_text);
    }

    return AppConfig(testcase_dir, output_file, optimizer_name, std::move(optimizer_config),
                     DebugProgress::from_debug_flag(debug_output));
}

int run(const AppConfig& config) {
    Solver solver(config);
    int status = solver.run();
    return status;
}

}  // namespace cadd0040
