#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cadd0040 {

struct AppConfig {
    std::filesystem::path testcase_dir;
    std::filesystem::path output_file;
    std::filesystem::path clk_tree_path;
    std::filesystem::path buflib_path;
    std::filesystem::path ss_delay_path;
    std::filesystem::path ff_delay_path;
};

AppConfig make_config(std::filesystem::path testcase_dir,
                      std::filesystem::path output_file);

AppConfig parse_arguments(const std::vector<std::string>& args);

std::string help_message();

int run(const AppConfig& config);

}  // namespace cadd0040
