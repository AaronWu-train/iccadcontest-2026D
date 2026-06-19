/**
 * @file app.hpp
 * @brief Command-line configuration and top-level application entry helpers.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "debug_progress.hpp"
#include "optimization/optimizer_config.hpp"

namespace cadd0040 {

struct AppConfig {
    std::filesystem::path testcase_dir;
    std::filesystem::path output_file;
    std::string optimizer_name;
    std::optional<OptimizerConfigFile> optimizer_config;
    DebugProgress debug_progress = DebugProgress::from_debug_flag(false);

    std::filesystem::path clk_tree_path;
    std::filesystem::path buflib_path;
    std::filesystem::path ss_delay_path;
    std::filesystem::path ff_delay_path;

    AppConfig() = default;

    AppConfig(std::filesystem::path testcase_dir, std::filesystem::path output_file,
              std::string optimizer_name,
              std::optional<OptimizerConfigFile> optimizer_config = std::nullopt,
              DebugProgress debug_progress = DebugProgress::from_debug_flag(false))
        : testcase_dir(std::move(testcase_dir)),
          output_file(std::move(output_file)),
          optimizer_name(std::move(optimizer_name)),
          optimizer_config(std::move(optimizer_config)),
          debug_progress(debug_progress),
          clk_tree_path(this->testcase_dir / "clk_tree.structure"),
          buflib_path(this->testcase_dir / "buf.lib"),
          ss_delay_path(this->testcase_dir / "SS_delay.rpt"),
          ff_delay_path(this->testcase_dir / "FF_delay.rpt") {}

    const OptimizerConfigFile* optimizer_config_ptr() const {
        return optimizer_config.has_value() ? &optimizer_config.value() : nullptr;
    }
};

AppConfig parse_arguments(int argc, char** argv);

int run(const AppConfig& config);

}  // namespace cadd0040
