#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
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

std::filesystem::path fixture_path(std::string_view name) {
    return std::filesystem::path{CADD0040_SOURCE_DIR} / "tests" / "fixtures" / name;
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
    CHECK_FALSE(config.optimizer_config.has_value());
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

TEST_CASE("parse_arguments accepts numeric optimizer alias") {
    const auto config = parse_arguments({
        "cadd0040",
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
        "--optimizer",
        "A1",
    });

    CHECK(config.optimizer_name == "A1");
}

TEST_CASE("parse_arguments accepts a global optimizer seed") {
    const auto config = parse_arguments({
        "cadd0040",
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
        "--seed",
        "4321",
    });

    REQUIRE(config.optimizer_config.has_value());
    CHECK(config.optimizer_config->seed == 4321u);
    CHECK(cadd0040::isa_config_from_sources(config.optimizer_config_ptr()).seed == 4321u);
}

TEST_CASE("parse_arguments accepts a global optimizer time budget") {
    const auto config = parse_arguments({
        "cadd0040",
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
        "--seconds",
        "42",
    });

    REQUIRE(config.optimizer_config.has_value());
    REQUIRE(config.optimizer_config->time_budget.has_value());
    CHECK(config.optimizer_config->time_budget->count() == 42);
    CHECK(cadd0040::sa_config_from_sources(config.optimizer_config_ptr()).time_budget.count() ==
          42);
}

TEST_CASE("parse_arguments accepts debug output flag") {
    const auto config = parse_arguments({
        "cadd0040",
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
        "--debug",
    });

    CHECK(config.debug_progress.enabled());
}

TEST_CASE("parse_arguments accepts progress output options") {
    const auto config = parse_arguments({
        "cadd0040",
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
        "--progress-dir",
        "progress/testcase0",
        "--progress-steps",
        "64",
    });

    REQUIRE(config.progress_dir.has_value());
    CHECK(*config.progress_dir == std::filesystem::path{"progress/testcase0"});
    CHECK(config.progress_steps == 64);
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

TEST_CASE("parse_arguments loads optional config and overrides optimizer") {
    const auto config = parse_arguments({
        "cadd0040",
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
        "--optimizer",
        "isa",
        "--config",
        fixture_path("sa_experiment.conf").string(),
    });

    CHECK(config.optimizer_name == "sa");
    REQUIRE(config.optimizer_config.has_value());
    REQUIRE(config.optimizer_config->optimizer.has_value());
    CHECK(*config.optimizer_config->optimizer == "sa");
    CHECK(config.optimizer_config->seed == 1234u);
    CHECK(config.optimizer_config->time_budget->count() == 60);
}

TEST_CASE("parse_arguments lets CLI seed override config global seed") {
    const auto config = parse_arguments({
        "cadd0040",
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
        "--config",
        fixture_path("sa_experiment.conf").string(),
        "--seed",
        "9876",
    });

    REQUIRE(config.optimizer_config.has_value());
    CHECK(config.optimizer_config->seed == 9876u);
    CHECK(cadd0040::sa_config_from_sources(config.optimizer_config_ptr()).seed == 9876u);
}

TEST_CASE("parse_arguments keeps CLI optimizer when config omits optimizer") {
    const auto config = parse_arguments({
        "cadd0040",
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
        "--optimizer",
        "isa",
        "--config",
        fixture_path("timing_only.conf").string(),
    });

    CHECK(config.optimizer_name == "isa");
}

TEST_CASE("config optimizer overrides CLI optimizer") {
    const auto config = parse_arguments({
        "cadd0040",
        "testcases/testcase0",
        "testcases/testcase0/modified_clk_tree.structure",
        "--optimizer",
        "isa",
        "--config",
        fixture_path("greedy_experiment.conf").string(),
    });

    CHECK(config.optimizer_name == "greedy-violation-path");
}

TEST_CASE("parse_arguments rejects unknown optimizer in config") {
    const std::filesystem::path temp_path =
        std::filesystem::temp_directory_path() / "cadd0040_invalid_optimizer.conf";
    {
        std::ofstream output(temp_path);
        output << "optimizer = not-a-real-optimizer\n";
    }

    CHECK_THROWS_AS(parse_arguments({
                        "cadd0040",
                        "testcases/testcase0",
                        "testcases/testcase0/modified_clk_tree.structure",
                        "--config",
                        temp_path.string(),
                    }),
                    std::runtime_error);

    std::filesystem::remove(temp_path);
}
