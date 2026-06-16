/**
 * @file optimizer_config.cpp
 * @brief Optimizer tuning defaults and legacy environment overrides.
 */

#include "optimization/optimizer_config.hpp"

#include <cstdlib>
#include <string>

namespace cadd0040 {
namespace {

template <typename Config>
Config load_config() {
    Config config;
    if (const char* env_seconds = std::getenv("CADD0040_SA_SECONDS")) {
        config.time_budget = std::chrono::seconds(std::stoll(env_seconds));
    }
    return config;
}

}  // namespace

GreedyConfig greedy_config_from_environment() { return load_config<GreedyConfig>(); }

MilpConfig milp_config_from_environment() { return load_config<MilpConfig>(); }

SaConfig sa_config_from_environment() { return load_config<SaConfig>(); }

IsaConfig isa_config_from_environment() { return load_config<IsaConfig>(); }

CriticalEndpointConfig critical_endpoint_config_from_environment() {
    return load_config<CriticalEndpointConfig>();
}

UpstreamWindowConfig upstream_window_config_from_environment() {
    return load_config<UpstreamWindowConfig>();
}

RepairRecoverConfig repair_recover_config_from_environment() {
    return load_config<RepairRecoverConfig>();
}

RandomizedRclConfig randomized_rcl_config_from_environment() {
    return load_config<RandomizedRclConfig>();
}

TabuConfig tabu_config_from_environment() { return load_config<TabuConfig>(); }

}  // namespace cadd0040
