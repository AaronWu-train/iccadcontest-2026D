/**
 * @file optimizer_config.cpp
 * @brief Optimizer tuning defaults and legacy environment overrides.
 */

#include "optimization/optimizer_config.hpp"

#include <cstdlib>
#include <string>

namespace cadd0040 {

GreedyConfig greedy_config_from_environment() {
    GreedyConfig config;
    if (const char* env_seconds = std::getenv("CADD0040_SA_SECONDS")) {
        config.time_budget = std::chrono::seconds(std::stoll(env_seconds));
    }
    return config;
}

MilpConfig milp_config_from_environment() {
    MilpConfig config;
    if (const char* env_seconds = std::getenv("CADD0040_SA_SECONDS")) {
        config.time_budget = std::chrono::seconds(std::stoll(env_seconds));
    }
    return config;
}

SaConfig sa_config_from_environment() {
    SaConfig config;
    if (const char* env_seconds = std::getenv("CADD0040_SA_SECONDS")) {
        config.time_budget = std::chrono::seconds(std::stoll(env_seconds));
    }
    return config;
}

IsaConfig isa_config_from_environment() {
    IsaConfig config;
    if (const char* env_seconds = std::getenv("CADD0040_SA_SECONDS")) {
        config.time_budget = std::chrono::seconds(std::stoll(env_seconds));
    }
    return config;
}

}  // namespace cadd0040
