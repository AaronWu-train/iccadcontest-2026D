#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

#include "app.hpp"
#include "optimization/factory.hpp"

TEST_CASE("make_config derives testcase input paths") {
    const auto config = cadd0040::AppConfig(
        std::filesystem::path{"testcases/testcase0"},
        std::filesystem::path{"testcases/testcase0/modified_clk_tree.structure"},
        std::string{cadd0040::kDefaultOptimizerName});

    CHECK(config.testcase_dir == std::filesystem::path{"testcases/testcase0"});
    CHECK(config.output_file ==
          std::filesystem::path{"testcases/testcase0/modified_clk_tree.structure"});
    CHECK(config.clk_tree_path == std::filesystem::path{"testcases/testcase0/clk_tree.structure"});
    CHECK(config.buflib_path == std::filesystem::path{"testcases/testcase0/buf.lib"});
    CHECK(config.ss_delay_path == std::filesystem::path{"testcases/testcase0/SS_delay.rpt"});
    CHECK(config.ff_delay_path == std::filesystem::path{"testcases/testcase0/FF_delay.rpt"});
}

TEST_CASE("parse_arguments accepts the required positional arguments") {
    const std::vector<std::string> args{
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
    };

    const auto config = cadd0040::parse_arguments(args);

    CHECK(config.testcase_dir == std::filesystem::path{"testcases/testcase0"});
    CHECK(config.output_file ==
          std::filesystem::path{"testcases/testcase0/modified_clk_tree.structure"});
    CHECK(config.optimizer_name == cadd0040::kDefaultOptimizerName);
}

TEST_CASE("parse_arguments accepts an explicit optimizer") {
    const std::vector<std::string> args{
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
        "--optimizer",
        std::string{cadd0040::kDefaultOptimizerName},
    };

    const auto config = cadd0040::parse_arguments(args);

    CHECK(config.optimizer_name == cadd0040::kDefaultOptimizerName);
}

TEST_CASE("parse_arguments rejects missing positional arguments") {
    const std::vector<std::string> args{"testcases/testcase0"};

    CHECK_THROWS_AS(cadd0040::parse_arguments(args), CLI::ParseError);
}

TEST_CASE("parse_arguments handles help without running the application") {
    const std::vector<std::string> args{"--help"};

    CHECK_THROWS_AS(cadd0040::parse_arguments(args), CLI::CallForHelp);
    CHECK(cadd0040::help_message().find("testcase_dir") != std::string::npos);
    CHECK(cadd0040::help_message().find("output_file") != std::string::npos);
}
