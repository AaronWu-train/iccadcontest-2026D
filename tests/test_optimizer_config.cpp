#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef _WIN32
#include <cstdlib>
#endif

#include "optimization/optimizer_config.hpp"

namespace {

std::filesystem::path fixture_path(std::string_view name) {
    return std::filesystem::path{CADD0040_SOURCE_DIR} / "tests" / "fixtures" / name;
}

}  // namespace

TEST_CASE("optimizer config file parser loads global and section keys", "[optimization][config]") {
    const auto config_file =
        cadd0040::parse_optimizer_config_file(fixture_path("sa_experiment.conf"));

    REQUIRE(config_file.optimizer.has_value());
    CHECK(*config_file.optimizer == "sa");
    REQUIRE(config_file.seed.has_value());
    CHECK(*config_file.seed == 1234u);
    REQUIRE(config_file.time_budget.has_value());
    CHECK(config_file.time_budget->count() == 60);

    const auto sa_it = config_file.sections.find("sa");
    REQUIRE(sa_it != config_file.sections.end());
    CHECK(sa_it->second.at("greedy_warmup_iterations") == "8");
    CHECK(sa_it->second.at("initial_temperature") == "0.12");
}

TEST_CASE("optimizer config sources override environment variables", "[optimization][config]") {
#ifndef _WIN32
    setenv("CADD0040_SA_SECONDS", "7", 1);
    const auto config_file =
        cadd0040::parse_optimizer_config_file(fixture_path("sa_experiment.conf"));
    const cadd0040::SaConfig config = cadd0040::sa_config_from_sources(&config_file);

    CHECK(config.time_budget.count() == 60);
    CHECK(config.seed == 1234u);
    CHECK(config.greedy_warmup_iterations == 8);
    CHECK(config.initial_temperature == Catch::Approx(0.12));
    unsetenv("CADD0040_SA_SECONDS");
#endif
}

TEST_CASE("optimizer config sources apply only matching optimizer section",
          "[optimization][config]") {
    const auto config_file =
        cadd0040::parse_optimizer_config_file(fixture_path("greedy_experiment.conf"));

    const cadd0040::GreedyConfig greedy = cadd0040::greedy_config_from_sources(&config_file);
    const cadd0040::SaConfig sa = cadd0040::sa_config_from_sources(&config_file);

    CHECK(greedy.time_budget.count() == 45);
    CHECK(greedy.max_steps == 128);
    CHECK(sa.time_budget.count() == 45);
    CHECK(sa.greedy_warmup_iterations == 256);
}

TEST_CASE("policy averaged greedy seed tabu config section is loaded",
          "[optimization][config]") {
    const std::filesystem::path temp_path =
        std::filesystem::temp_directory_path() / "cadd0040_policy_seed_tabu.conf";
    {
        std::ofstream output(temp_path);
        output << "time_budget_seconds = 45\n";
        output << "[policy-averaged-greedy-seed-then-tabu]\n";
        output << "seed_steps = 11\n";
        output << "tabu_steps = 12\n";
        output << "tenure = 13\n";
        output << "violation_sample_limit = 14\n";
        output << "critical_endpoint_limit = 15\n";
        output << "upstream_window_depth = 3\n";
        output << "removal_candidate_limit = 16\n";
        output << "resize_node_limit = 17\n";
        output << "seed_candidate_limit = 18\n";
        output << "candidate_limit = 19\n";
        output << "consensus_bonus = 0.25\n";
        output << "min_seed_delta = 0.125\n";
    }

    const auto config_file = cadd0040::parse_optimizer_config_file(temp_path);
    const cadd0040::PolicyAveragedGreedySeedTabuConfig config =
        cadd0040::policy_averaged_greedy_seed_tabu_config_from_sources(&config_file);

    CHECK(config.time_budget.count() == 45);
    CHECK(config.seed_steps == 11);
    CHECK(config.tabu_steps == 12);
    CHECK(config.tenure == 13);
    CHECK(config.violation_sample_limit == 14);
    CHECK(config.critical_endpoint_limit == 15);
    CHECK(config.upstream_window_depth == 3);
    CHECK(config.removal_candidate_limit == 16);
    CHECK(config.resize_node_limit == 17);
    CHECK(config.seed_candidate_limit == 18);
    CHECK(config.candidate_limit == 19);
    CHECK(config.consensus_bonus == Catch::Approx(0.25));
    CHECK(config.min_seed_delta == Catch::Approx(0.125));

    std::filesystem::remove(temp_path);
}

TEST_CASE("optimizer config parser rejects unknown keys", "[optimization][config]") {
    const std::filesystem::path temp_path =
        std::filesystem::temp_directory_path() / "cadd0040_invalid_key.conf";
    {
        std::ofstream output(temp_path);
        output << "[sa]\n";
        output << "not_a_real_key = 1\n";
    }

    CHECK_THROWS_AS(cadd0040::parse_optimizer_config_file(temp_path), std::runtime_error);
    std::filesystem::remove(temp_path);
}
