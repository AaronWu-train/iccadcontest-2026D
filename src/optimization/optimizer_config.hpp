/**
 * @file optimizer_config.hpp
 * @brief Centralized optimizer tuning defaults and experiment config loading.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace cadd0040 {

inline constexpr std::chrono::seconds kDefaultOptimizerTimeBudget{570};
inline constexpr unsigned int kDefaultRngSeed = 2026;

struct OptimizerConfigFile {
    std::optional<std::string> optimizer;
    std::optional<unsigned int> seed;
    std::optional<std::chrono::seconds> time_budget;
    std::map<std::string, std::map<std::string, std::string>> sections;
};

struct GreedyConfig {
    std::chrono::seconds time_budget{kDefaultOptimizerTimeBudget};
    std::size_t max_steps = 4096;
    std::size_t max_resize_polish_steps = 96;
    std::size_t max_resize_nodes_per_step = 8192;
    std::size_t max_polish_phases = 64;
    std::size_t violation_sample_limit = 32;
    std::size_t removal_candidate_limit = 512;
};

struct MilpConfig {
    std::chrono::seconds time_budget{kDefaultOptimizerTimeBudget};
    std::size_t max_rounds = 4096;
    std::size_t violation_window = 96;
    std::size_t candidate_limit = 4096;
    std::size_t resize_node_limit = 4096;
};

struct SaConfig {
    std::chrono::seconds time_budget{kDefaultOptimizerTimeBudget};
    unsigned int seed = kDefaultRngSeed;
    double initial_temperature = 0.08;
    double min_temperature = 1e-6;
    double cooling_factor = 0.01;
    std::size_t greedy_warmup_iterations = 256;
    std::size_t final_greedy_polish_iterations = 32;
    std::size_t restart_stale_iterations = 2500;
    double restart_score_gap = 0.05;
    std::size_t greedy_polish_interval = 0;
    std::size_t violation_sample_limit = 32;
    std::size_t removal_candidate_limit = 512;
};

struct IsaConfig {
    std::chrono::seconds time_budget{kDefaultOptimizerTimeBudget};
    unsigned int seed = kDefaultRngSeed;
    double initial_temperature = 0.08;
    double min_temperature = 1e-6;
    double cooling_factor = 0.01;
    std::size_t greedy_warmup_iterations = 256;
    std::size_t rounds = 16;
    std::size_t greedy_round_iterations = 16;
    std::size_t final_greedy_polish_iterations = 32;
    std::size_t restart_stale_iterations = 2500;
    double restart_score_gap = 0.05;
    std::size_t violation_sample_limit = 32;
    std::size_t removal_candidate_limit = 512;
};

struct CriticalEndpointConfig {
    std::chrono::seconds time_budget{kDefaultOptimizerTimeBudget};
    std::size_t max_steps = 4096;
    std::size_t max_resize_polish_steps = 96;
    std::size_t max_resize_nodes_per_step = 8192;
    std::size_t max_polish_phases = 64;
    std::size_t critical_endpoint_limit = 32;
    std::size_t removal_candidate_limit = 512;
};

struct UpstreamWindowConfig {
    std::chrono::seconds time_budget{kDefaultOptimizerTimeBudget};
    std::size_t max_steps = 4096;
    std::size_t max_resize_polish_steps = 96;
    std::size_t max_resize_nodes_per_step = 8192;
    std::size_t max_polish_phases = 64;
    std::size_t violation_sample_limit = 32;
    std::size_t upstream_window_depth = 4;
    std::size_t removal_candidate_limit = 512;
};

struct RepairRecoverConfig {
    std::chrono::seconds time_budget{kDefaultOptimizerTimeBudget};
    std::size_t timing_steps = 4096;
    std::size_t area_steps = 4096;
    std::size_t violation_sample_limit = 32;
    std::size_t upstream_window_depth = 4;
    std::size_t removal_candidate_limit = 1024;
    double max_timing_score_loss = 1e-9;
};

struct RandomizedRclConfig {
    std::chrono::seconds time_budget{kDefaultOptimizerTimeBudget};
    std::size_t restart_count = 16;
    std::size_t steps_per_restart = 512;
    std::size_t top_k = 8;
    std::size_t violation_sample_limit = 32;
    std::size_t removal_candidate_limit = 512;
    std::size_t final_resize_polish_steps = 96;
    unsigned int seed = kDefaultRngSeed;
};

struct TabuConfig {
    std::chrono::seconds time_budget{kDefaultOptimizerTimeBudget};
    std::size_t max_steps = 8192;
    std::size_t tenure = 128;
    std::size_t violation_sample_limit = 32;
    std::size_t critical_endpoint_limit = 16;
    std::size_t upstream_window_depth = 4;
    std::size_t removal_candidate_limit = 256;
    std::size_t resize_node_limit = 1024;
    std::size_t candidate_limit = 4096;
};

OptimizerConfigFile parse_optimizer_config_file(const std::filesystem::path& path);

GreedyConfig greedy_config_from_sources(const OptimizerConfigFile* config_file = nullptr);
MilpConfig milp_config_from_sources(const OptimizerConfigFile* config_file = nullptr);
SaConfig sa_config_from_sources(const OptimizerConfigFile* config_file = nullptr);
IsaConfig isa_config_from_sources(const OptimizerConfigFile* config_file = nullptr);
CriticalEndpointConfig critical_endpoint_config_from_sources(
    const OptimizerConfigFile* config_file = nullptr);
UpstreamWindowConfig upstream_window_config_from_sources(
    const OptimizerConfigFile* config_file = nullptr);
RepairRecoverConfig repair_recover_config_from_sources(
    const OptimizerConfigFile* config_file = nullptr);
RandomizedRclConfig randomized_rcl_config_from_sources(
    const OptimizerConfigFile* config_file = nullptr);
TabuConfig tabu_config_from_sources(const OptimizerConfigFile* config_file = nullptr);

GreedyConfig greedy_config_from_environment();
MilpConfig milp_config_from_environment();
SaConfig sa_config_from_environment();
IsaConfig isa_config_from_environment();
CriticalEndpointConfig critical_endpoint_config_from_environment();
UpstreamWindowConfig upstream_window_config_from_environment();
RepairRecoverConfig repair_recover_config_from_environment();
RandomizedRclConfig randomized_rcl_config_from_environment();
TabuConfig tabu_config_from_environment();

}  // namespace cadd0040
