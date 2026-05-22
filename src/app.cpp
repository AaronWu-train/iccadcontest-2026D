/**
 * @file app.cpp
 * @brief Command-line parsing and application bootstrap helpers.
 */

#include "app.hpp"

#include <CLI/CLI.hpp>

#include "optimization/factory.hpp"
#include "solver.hpp"

namespace cadd0040 {
namespace {

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

AppConfig parse_arguments(const std::vector<std::string>& args) {
    std::filesystem::path testcase_dir;
    std::filesystem::path output_file;
    std::string optimizer_name;

    CLI::App app{"ICCAD Contest 2026 Problem D solver"};
    configure_cli_app(app, testcase_dir, output_file, optimizer_name);

    std::vector<std::string> cli_args(args.rbegin(), args.rend());
    app.parse(cli_args);

    return AppConfig(testcase_dir, output_file, optimizer_name);
}

std::string help_message() {
    std::filesystem::path testcase_dir;
    std::filesystem::path output_file;
    std::string optimizer_name;
    CLI::App app{"ICCAD Contest 2026 Problem D solver"};
    configure_cli_app(app, testcase_dir, output_file, optimizer_name);
    return app.help();
}

int run(const AppConfig& config) {
    Solver solver(config);
    int status = solver.run();
    return status;
}

}  // namespace cadd0040
