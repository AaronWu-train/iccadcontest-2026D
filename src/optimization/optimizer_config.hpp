/**
 * @file optimizer_config.hpp
 * @brief Centralized optimizer tuning defaults.
 */

#pragma once

#include <chrono>
#include <cstddef>

namespace cadd0040 {

struct GreedyConfig {
    std::chrono::seconds time_budget{540};
    std::size_t max_steps = 4096;
    std::size_t max_resize_polish_steps = 96;
    std::size_t max_resize_nodes_per_step = 8192;
    std::size_t max_polish_phases = 5;
    std::size_t violation_sample_limit = 32;
    std::size_t removal_candidate_limit = 512;
};

struct MilpConfig {
    std::chrono::seconds time_budget{540};
    std::size_t max_rounds = 4096;
    std::size_t violation_window = 96;
    std::size_t candidate_limit = 4096;
    std::size_t resize_node_limit = 4096;
};

struct SaConfig {
    std::chrono::seconds time_budget{540};
    double initial_temperature = 0.08;
    double min_temperature = 1e-6;
    double cooling_factor = 0.01;
    std::size_t greedy_warmup_iterations = 256;
    std::size_t final_greedy_polish_iterations = 32;
    std::size_t restart_stale_iterations = 2500;
    double restart_score_gap = 0.05;
    std::size_t greedy_polish_interval = 0;
};

struct IsaConfig {
    std::chrono::seconds time_budget{540};
    double initial_temperature = 0.08;
    double min_temperature = 1e-6;
    double cooling_factor = 0.01;
    std::size_t greedy_warmup_iterations = 256;
    std::size_t rounds = 5;
    std::size_t greedy_round_iterations = 16;
    std::size_t final_greedy_polish_iterations = 32;
    std::size_t restart_stale_iterations = 2500;
    double restart_score_gap = 0.05;
};

GreedyConfig greedy_config_from_environment();
MilpConfig milp_config_from_environment();
SaConfig sa_config_from_environment();
IsaConfig isa_config_from_environment();

}  // namespace cadd0040
