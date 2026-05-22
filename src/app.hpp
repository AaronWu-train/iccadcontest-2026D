/**
 * @file app.hpp
 * @brief Command-line configuration and top-level application entry helpers.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

using std::filesystem::path;

namespace cadd0040 {

struct AppConfig {
    std::filesystem::path testcase_dir;
    std::filesystem::path output_file;

    std::filesystem::path clk_tree_path;
    std::filesystem::path buflib_path;
    std::filesystem::path ss_delay_path;
    std::filesystem::path ff_delay_path;

    AppConfig() = default;

    AppConfig(std::filesystem::path testcase_dir, std::filesystem::path output_file)
        : testcase_dir(std::move(testcase_dir)),
          output_file(std::move(output_file)),
          clk_tree_path(this->testcase_dir / "clk_tree.structure"),
          buflib_path(this->testcase_dir / "buf.lib"),
          ss_delay_path(this->testcase_dir / "SS_delay.rpt"),
          ff_delay_path(this->testcase_dir / "FF_delay.rpt") {}
};

AppConfig parse_arguments(const std::vector<std::string>& args);

std::string help_message();

int run(const AppConfig& config);

}  // namespace cadd0040
