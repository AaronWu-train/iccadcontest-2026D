#include "app.hpp"

#include <CLI/CLI.hpp>
#include <sstream>
#include <utility>

namespace cadd0040 {
namespace {

void configure_cli_app(CLI::App& app, std::filesystem::path& testcase_dir,
                       std::filesystem::path& output_file) {
    app.add_option("testcase_dir", testcase_dir,
                   "Directory containing clk_tree.structure, buf.lib, "
                   "SS_delay.rpt, and FF_delay.rpt")
        ->required();
    app.add_option("output_file", output_file,
                   "Path to write modified_clk_tree.structure")
        ->required();
}

}  // namespace

AppConfig make_config(std::filesystem::path testcase_dir,
                      std::filesystem::path output_file) {
    AppConfig config;
    config.testcase_dir = std::move(testcase_dir);
    config.output_file = std::move(output_file);
    config.clk_tree_path = config.testcase_dir / "clk_tree.structure";
    config.buflib_path = config.testcase_dir / "buf.lib";
    config.ss_delay_path = config.testcase_dir / "SS_delay.rpt";
    config.ff_delay_path = config.testcase_dir / "FF_delay.rpt";
    return config;
}

AppConfig parse_arguments(const std::vector<std::string>& args) {
    std::filesystem::path testcase_dir;
    std::filesystem::path output_file;

    CLI::App app{"ICCAD Contest 2026 Problem D solver"};
    configure_cli_app(app, testcase_dir, output_file);

    std::vector<std::string> cli_args(args.rbegin(), args.rend());
    app.parse(cli_args);

    return make_config(testcase_dir, output_file);
}

std::string help_message() {
    std::filesystem::path testcase_dir;
    std::filesystem::path output_file;
    CLI::App app{"ICCAD Contest 2026 Problem D solver"};
    configure_cli_app(app, testcase_dir, output_file);
    return app.help();
}

int run(const AppConfig& config) {
    (void)config;
    return 0;
}

}  // namespace cadd0040
