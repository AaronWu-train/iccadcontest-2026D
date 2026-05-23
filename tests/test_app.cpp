#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

#include "app.hpp"
#include "optimization/factory.hpp"

namespace {

cadd0040::AppConfig parse_arguments(std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.reserve(args.size());

    for (auto& arg : args) {
        argv.push_back(arg.data());
    }

    return cadd0040::parse_arguments(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

TEST_CASE("AppConfig derives testcase input paths") {
    const auto config = cadd0040::AppConfig(
        std::filesystem::path{"testcases/testcase0"},
        std::filesystem::path{"testcases/testcase0/modified_clk_tree.structure"},
        std::string{cadd0040::kDefaultOptimizerName});

    CHECK(config.testcase_dir == std::filesystem::path{"testcases/testcase0"});
    CHECK(config.output_file ==
          std::filesystem::path{"testcases/testcase0/modified_clk_tree.structure"});
    CHECK(config.optimizer_name == cadd0040::kDefaultOptimizerName);
    CHECK(config.clk_tree_path == std::filesystem::path{"testcases/testcase0/clk_tree.structure"});
    CHECK(config.buflib_path == std::filesystem::path{"testcases/testcase0/buf.lib"});
    CHECK(config.ss_delay_path == std::filesystem::path{"testcases/testcase0/SS_delay.rpt"});
    CHECK(config.ff_delay_path == std::filesystem::path{"testcases/testcase0/FF_delay.rpt"});
}

TEST_CASE("parse_arguments accepts the required positional arguments") {
    const auto config = parse_arguments({
        "cadd0040",
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
    });

    CHECK(config.testcase_dir == std::filesystem::path{"testcases/testcase0"});
    CHECK(config.output_file ==
          std::filesystem::path{"testcases/testcase0/modified_clk_tree.structure"});
    CHECK(config.optimizer_name == cadd0040::kDefaultOptimizerName);
}

TEST_CASE("parse_arguments accepts an explicit optimizer") {
    const auto config = parse_arguments({
        "cadd0040",
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
        "--optimizer",
        std::string{cadd0040::kDefaultOptimizerName},
    });

    CHECK(config.testcase_dir == std::filesystem::path{"testcases/testcase0"});
    CHECK(config.output_file ==
          std::filesystem::path{"testcases/testcase0/modified_clk_tree.structure"});
    CHECK(config.optimizer_name == cadd0040::kDefaultOptimizerName);
}

TEST_CASE("parse_arguments uses argv[0] only as the program name") {
    const auto config = parse_arguments({
        "cadd0040-alpha",
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
    });

    CHECK(config.testcase_dir == std::filesystem::path{"testcases/testcase0"});
    CHECK(config.output_file ==
          std::filesystem::path{"testcases/testcase0/modified_clk_tree.structure"});
}
