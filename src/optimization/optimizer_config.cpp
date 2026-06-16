/**
 * @file optimizer_config.cpp
 * @brief Optimizer tuning defaults, environment overrides, and config file loading.
 */

#include "optimization/optimizer_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace cadd0040 {
namespace {

constexpr std::string_view kGlobalSection = "";

std::string trim(std::string_view text) {
    const auto begin = std::find_if_not(text.begin(), text.end(),
                                        [](unsigned char ch) { return std::isspace(ch); });
    const auto end = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char ch) {
                         return std::isspace(ch);
                     }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::string strip_inline_comment(std::string_view line) {
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char ch = line[index];
        if (ch == '#' || ch == ';') {
            return trim(line.substr(0, index));
        }
    }
    return std::string(line);
}

bool is_section_header(std::string_view line, std::string* section_name) {
    const std::string trimmed = trim(line);
    if (trimmed.size() < 3 || trimmed.front() != '[' || trimmed.back() != ']') {
        return false;
    }
    *section_name = trim(trimmed.substr(1, trimmed.size() - 2));
    return true;
}

std::size_t parse_size_t(const std::string& value, const std::string& context) {
    try {
        const auto parsed = std::stoull(value);
        return static_cast<std::size_t>(parsed);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid integer value for " + context + ": " + value);
    }
}

double parse_double(const std::string& value, const std::string& context) {
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid floating-point value for " + context + ": " + value);
    }
}

unsigned int parse_uint(const std::string& value, const std::string& context) {
    try {
        const auto parsed = std::stoull(value);
        return static_cast<unsigned int>(parsed);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid unsigned integer value for " + context + ": " + value);
    }
}

std::chrono::seconds parse_seconds(const std::string& value, const std::string& context) {
    return std::chrono::seconds(parse_size_t(value, context));
}

void apply_time_budget(std::chrono::seconds& time_budget, const std::string& value,
                       const std::string& context) {
    time_budget = parse_seconds(value, context);
}

const std::map<std::string, std::string>* section_entries(const OptimizerConfigFile* config_file,
                                                          std::string_view section) {
    if (config_file == nullptr) {
        return nullptr;
    }
    const auto section_it = config_file->sections.find(std::string(section));
    if (section_it == config_file->sections.end()) {
        return nullptr;
    }
    return &section_it->second;
}

void apply_global_overrides(std::chrono::seconds& time_budget, std::optional<unsigned int>& seed,
                            const OptimizerConfigFile* config_file) {
    if (config_file == nullptr) {
        return;
    }
    if (config_file->time_budget.has_value()) {
        time_budget = *config_file->time_budget;
    }
    if (config_file->seed.has_value()) {
        seed = *config_file->seed;
    }
}

void apply_env_time_budget(std::chrono::seconds& time_budget) {
    if (const char* env_seconds = std::getenv("CADD0040_SA_SECONDS")) {
        time_budget = std::chrono::seconds(std::stoll(env_seconds));
    }
}

[[noreturn]] void throw_unknown_key(std::string_view section, const std::string& key) {
    throw std::runtime_error("Unknown optimizer config key '" + key + "' in section [" +
                             std::string(section) + "]");
}

void apply_greedy_section(GreedyConfig& config, const std::map<std::string, std::string>& entries,
                          std::string_view section) {
    for (const auto& [key, value] : entries) {
        const std::string context = std::string(section) + "." + key;
        if (key == "time_budget_seconds") {
            apply_time_budget(config.time_budget, value, context);
        } else if (key == "max_steps") {
            config.max_steps = parse_size_t(value, context);
        } else if (key == "max_resize_polish_steps") {
            config.max_resize_polish_steps = parse_size_t(value, context);
        } else if (key == "max_resize_nodes_per_step") {
            config.max_resize_nodes_per_step = parse_size_t(value, context);
        } else if (key == "max_polish_phases") {
            config.max_polish_phases = parse_size_t(value, context);
        } else if (key == "violation_sample_limit") {
            config.violation_sample_limit = parse_size_t(value, context);
        } else if (key == "removal_candidate_limit") {
            config.removal_candidate_limit = parse_size_t(value, context);
        } else {
            throw_unknown_key(section, key);
        }
    }
}

void apply_milp_section(MilpConfig& config, const std::map<std::string, std::string>& entries,
                        std::string_view section) {
    for (const auto& [key, value] : entries) {
        const std::string context = std::string(section) + "." + key;
        if (key == "time_budget_seconds") {
            apply_time_budget(config.time_budget, value, context);
        } else if (key == "max_rounds") {
            config.max_rounds = parse_size_t(value, context);
        } else if (key == "violation_window") {
            config.violation_window = parse_size_t(value, context);
        } else if (key == "candidate_limit") {
            config.candidate_limit = parse_size_t(value, context);
        } else if (key == "resize_node_limit") {
            config.resize_node_limit = parse_size_t(value, context);
        } else {
            throw_unknown_key(section, key);
        }
    }
}

void apply_sa_section(SaConfig& config, const std::map<std::string, std::string>& entries,
                      std::string_view section) {
    for (const auto& [key, value] : entries) {
        const std::string context = std::string(section) + "." + key;
        if (key == "time_budget_seconds") {
            apply_time_budget(config.time_budget, value, context);
        } else if (key == "seed") {
            config.seed = parse_uint(value, context);
        } else if (key == "initial_temperature") {
            config.initial_temperature = parse_double(value, context);
        } else if (key == "min_temperature") {
            config.min_temperature = parse_double(value, context);
        } else if (key == "cooling_factor") {
            config.cooling_factor = parse_double(value, context);
        } else if (key == "greedy_warmup_iterations") {
            config.greedy_warmup_iterations = parse_size_t(value, context);
        } else if (key == "final_greedy_polish_iterations") {
            config.final_greedy_polish_iterations = parse_size_t(value, context);
        } else if (key == "restart_stale_iterations") {
            config.restart_stale_iterations = parse_size_t(value, context);
        } else if (key == "restart_score_gap") {
            config.restart_score_gap = parse_double(value, context);
        } else if (key == "greedy_polish_interval") {
            config.greedy_polish_interval = parse_size_t(value, context);
        } else if (key == "violation_sample_limit") {
            config.violation_sample_limit = parse_size_t(value, context);
        } else if (key == "removal_candidate_limit") {
            config.removal_candidate_limit = parse_size_t(value, context);
        } else {
            throw_unknown_key(section, key);
        }
    }
}

void apply_isa_section(IsaConfig& config, const std::map<std::string, std::string>& entries,
                       std::string_view section) {
    for (const auto& [key, value] : entries) {
        const std::string context = std::string(section) + "." + key;
        if (key == "time_budget_seconds") {
            apply_time_budget(config.time_budget, value, context);
        } else if (key == "seed") {
            config.seed = parse_uint(value, context);
        } else if (key == "initial_temperature") {
            config.initial_temperature = parse_double(value, context);
        } else if (key == "min_temperature") {
            config.min_temperature = parse_double(value, context);
        } else if (key == "cooling_factor") {
            config.cooling_factor = parse_double(value, context);
        } else if (key == "greedy_warmup_iterations") {
            config.greedy_warmup_iterations = parse_size_t(value, context);
        } else if (key == "rounds") {
            config.rounds = parse_size_t(value, context);
        } else if (key == "greedy_round_iterations") {
            config.greedy_round_iterations = parse_size_t(value, context);
        } else if (key == "final_greedy_polish_iterations") {
            config.final_greedy_polish_iterations = parse_size_t(value, context);
        } else if (key == "restart_stale_iterations") {
            config.restart_stale_iterations = parse_size_t(value, context);
        } else if (key == "restart_score_gap") {
            config.restart_score_gap = parse_double(value, context);
        } else if (key == "violation_sample_limit") {
            config.violation_sample_limit = parse_size_t(value, context);
        } else if (key == "removal_candidate_limit") {
            config.removal_candidate_limit = parse_size_t(value, context);
        } else {
            throw_unknown_key(section, key);
        }
    }
}

void apply_critical_endpoint_section(CriticalEndpointConfig& config,
                                     const std::map<std::string, std::string>& entries,
                                     std::string_view section) {
    for (const auto& [key, value] : entries) {
        const std::string context = std::string(section) + "." + key;
        if (key == "time_budget_seconds") {
            apply_time_budget(config.time_budget, value, context);
        } else if (key == "max_steps") {
            config.max_steps = parse_size_t(value, context);
        } else if (key == "max_resize_polish_steps") {
            config.max_resize_polish_steps = parse_size_t(value, context);
        } else if (key == "max_resize_nodes_per_step") {
            config.max_resize_nodes_per_step = parse_size_t(value, context);
        } else if (key == "max_polish_phases") {
            config.max_polish_phases = parse_size_t(value, context);
        } else if (key == "critical_endpoint_limit") {
            config.critical_endpoint_limit = parse_size_t(value, context);
        } else if (key == "removal_candidate_limit") {
            config.removal_candidate_limit = parse_size_t(value, context);
        } else {
            throw_unknown_key(section, key);
        }
    }
}

void apply_upstream_window_section(UpstreamWindowConfig& config,
                                   const std::map<std::string, std::string>& entries,
                                   std::string_view section) {
    for (const auto& [key, value] : entries) {
        const std::string context = std::string(section) + "." + key;
        if (key == "time_budget_seconds") {
            apply_time_budget(config.time_budget, value, context);
        } else if (key == "max_steps") {
            config.max_steps = parse_size_t(value, context);
        } else if (key == "max_resize_polish_steps") {
            config.max_resize_polish_steps = parse_size_t(value, context);
        } else if (key == "max_resize_nodes_per_step") {
            config.max_resize_nodes_per_step = parse_size_t(value, context);
        } else if (key == "max_polish_phases") {
            config.max_polish_phases = parse_size_t(value, context);
        } else if (key == "violation_sample_limit") {
            config.violation_sample_limit = parse_size_t(value, context);
        } else if (key == "upstream_window_depth") {
            config.upstream_window_depth = parse_size_t(value, context);
        } else if (key == "removal_candidate_limit") {
            config.removal_candidate_limit = parse_size_t(value, context);
        } else {
            throw_unknown_key(section, key);
        }
    }
}

void apply_repair_recover_section(RepairRecoverConfig& config,
                                  const std::map<std::string, std::string>& entries,
                                  std::string_view section) {
    for (const auto& [key, value] : entries) {
        const std::string context = std::string(section) + "." + key;
        if (key == "time_budget_seconds") {
            apply_time_budget(config.time_budget, value, context);
        } else if (key == "timing_steps") {
            config.timing_steps = parse_size_t(value, context);
        } else if (key == "area_steps") {
            config.area_steps = parse_size_t(value, context);
        } else if (key == "violation_sample_limit") {
            config.violation_sample_limit = parse_size_t(value, context);
        } else if (key == "upstream_window_depth") {
            config.upstream_window_depth = parse_size_t(value, context);
        } else if (key == "removal_candidate_limit") {
            config.removal_candidate_limit = parse_size_t(value, context);
        } else if (key == "max_timing_score_loss") {
            config.max_timing_score_loss = parse_double(value, context);
        } else {
            throw_unknown_key(section, key);
        }
    }
}

void apply_randomized_rcl_section(RandomizedRclConfig& config,
                                  const std::map<std::string, std::string>& entries,
                                  std::string_view section) {
    for (const auto& [key, value] : entries) {
        const std::string context = std::string(section) + "." + key;
        if (key == "time_budget_seconds") {
            apply_time_budget(config.time_budget, value, context);
        } else if (key == "seed") {
            config.seed = parse_uint(value, context);
        } else if (key == "restart_count") {
            config.restart_count = parse_size_t(value, context);
        } else if (key == "steps_per_restart") {
            config.steps_per_restart = parse_size_t(value, context);
        } else if (key == "top_k") {
            config.top_k = parse_size_t(value, context);
        } else if (key == "violation_sample_limit") {
            config.violation_sample_limit = parse_size_t(value, context);
        } else if (key == "removal_candidate_limit") {
            config.removal_candidate_limit = parse_size_t(value, context);
        } else if (key == "final_resize_polish_steps") {
            config.final_resize_polish_steps = parse_size_t(value, context);
        } else {
            throw_unknown_key(section, key);
        }
    }
}

void apply_tabu_section(TabuConfig& config, const std::map<std::string, std::string>& entries,
                        std::string_view section) {
    for (const auto& [key, value] : entries) {
        const std::string context = std::string(section) + "." + key;
        if (key == "time_budget_seconds") {
            apply_time_budget(config.time_budget, value, context);
        } else if (key == "max_steps") {
            config.max_steps = parse_size_t(value, context);
        } else if (key == "tenure") {
            config.tenure = parse_size_t(value, context);
        } else if (key == "violation_sample_limit") {
            config.violation_sample_limit = parse_size_t(value, context);
        } else if (key == "critical_endpoint_limit") {
            config.critical_endpoint_limit = parse_size_t(value, context);
        } else if (key == "upstream_window_depth") {
            config.upstream_window_depth = parse_size_t(value, context);
        } else if (key == "removal_candidate_limit") {
            config.removal_candidate_limit = parse_size_t(value, context);
        } else if (key == "resize_node_limit") {
            config.resize_node_limit = parse_size_t(value, context);
        } else if (key == "candidate_limit") {
            config.candidate_limit = parse_size_t(value, context);
        } else {
            throw_unknown_key(section, key);
        }
    }
}

void apply_global_section_keys(OptimizerConfigFile& config_file,
                               const std::map<std::string, std::string>& entries) {
    for (const auto& [key, value] : entries) {
        const std::string context = "global." + key;
        if (key == "optimizer") {
            config_file.optimizer = value;
        } else if (key == "seed") {
            config_file.seed = parse_uint(value, context);
        } else if (key == "time_budget_seconds") {
            config_file.time_budget = parse_seconds(value, context);
        } else {
            throw_unknown_key(kGlobalSection, key);
        }
    }
}

}  // namespace

OptimizerConfigFile parse_optimizer_config_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open optimizer config file: " + path.string());
    }

    OptimizerConfigFile config_file;
    std::string current_section;
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;
        const std::string stripped = strip_inline_comment(line);
        const std::string trimmed = trim(stripped);
        if (trimmed.empty()) {
            continue;
        }

        std::string section_name;
        if (is_section_header(trimmed, &section_name)) {
            current_section = section_name;
            continue;
        }

        const std::size_t delimiter = trimmed.find('=');
        if (delimiter == std::string::npos) {
            throw std::runtime_error("Invalid config line " + std::to_string(line_number) + " in " +
                                     path.string() + ": " + trimmed);
        }

        const std::string key = trim(trimmed.substr(0, delimiter));
        const std::string value = trim(trimmed.substr(delimiter + 1));
        if (key.empty() || value.empty()) {
            throw std::runtime_error("Invalid key/value on line " + std::to_string(line_number) +
                                     " in " + path.string());
        }

        if (current_section.empty()) {
            apply_global_section_keys(config_file, {{key, value}});
            continue;
        }

        config_file.sections[current_section][key] = value;
    }

    if (const auto global_it = config_file.sections.find("global");
        global_it != config_file.sections.end()) {
        apply_global_section_keys(config_file, global_it->second);
        config_file.sections.erase(global_it);
    }

    for (const auto& [section, entries] : config_file.sections) {
        if (section == "greedy-violation-path") {
            GreedyConfig probe;
            apply_greedy_section(probe, entries, section);
        } else if (section == "milp") {
            MilpConfig probe;
            apply_milp_section(probe, entries, section);
        } else if (section == "sa") {
            SaConfig probe;
            apply_sa_section(probe, entries, section);
        } else if (section == "isa") {
            IsaConfig probe;
            apply_isa_section(probe, entries, section);
        } else if (section == "greedy-critical-endpoint") {
            CriticalEndpointConfig probe;
            apply_critical_endpoint_section(probe, entries, section);
        } else if (section == "greedy-upstream-window") {
            UpstreamWindowConfig probe;
            apply_upstream_window_section(probe, entries, section);
        } else if (section == "greedy-repair-recover") {
            RepairRecoverConfig probe;
            apply_repair_recover_section(probe, entries, section);
        } else if (section == "greedy-randomized-rcl") {
            RandomizedRclConfig probe;
            apply_randomized_rcl_section(probe, entries, section);
        } else if (section == "tabu") {
            TabuConfig probe;
            apply_tabu_section(probe, entries, section);
        } else {
            throw std::runtime_error("Unknown optimizer config section [" + section + "] in " +
                                     path.string());
        }
    }

    return config_file;
}

GreedyConfig greedy_config_from_sources(const OptimizerConfigFile* config_file) {
    GreedyConfig config;
    apply_env_time_budget(config.time_budget);
    std::optional<unsigned int> unused_seed;
    apply_global_overrides(config.time_budget, unused_seed, config_file);
    if (const auto* entries = section_entries(config_file, "greedy-violation-path");
        entries != nullptr) {
        apply_greedy_section(config, *entries, "greedy-violation-path");
    }
    return config;
}

MilpConfig milp_config_from_sources(const OptimizerConfigFile* config_file) {
    MilpConfig config;
    apply_env_time_budget(config.time_budget);
    std::optional<unsigned int> unused_seed;
    apply_global_overrides(config.time_budget, unused_seed, config_file);
    if (const auto* entries = section_entries(config_file, "milp"); entries != nullptr) {
        apply_milp_section(config, *entries, "milp");
    }
    return config;
}

SaConfig sa_config_from_sources(const OptimizerConfigFile* config_file) {
    SaConfig config;
    apply_env_time_budget(config.time_budget);
    std::optional<unsigned int> seed = config.seed;
    apply_global_overrides(config.time_budget, seed, config_file);
    if (seed.has_value()) {
        config.seed = *seed;
    }
    if (const auto* entries = section_entries(config_file, "sa"); entries != nullptr) {
        apply_sa_section(config, *entries, "sa");
    }
    return config;
}

IsaConfig isa_config_from_sources(const OptimizerConfigFile* config_file) {
    IsaConfig config;
    apply_env_time_budget(config.time_budget);
    std::optional<unsigned int> seed = config.seed;
    apply_global_overrides(config.time_budget, seed, config_file);
    if (seed.has_value()) {
        config.seed = *seed;
    }
    if (const auto* entries = section_entries(config_file, "isa"); entries != nullptr) {
        apply_isa_section(config, *entries, "isa");
    }
    return config;
}

CriticalEndpointConfig critical_endpoint_config_from_sources(
    const OptimizerConfigFile* config_file) {
    CriticalEndpointConfig config;
    apply_env_time_budget(config.time_budget);
    std::optional<unsigned int> unused_seed;
    apply_global_overrides(config.time_budget, unused_seed, config_file);
    if (const auto* entries = section_entries(config_file, "greedy-critical-endpoint");
        entries != nullptr) {
        apply_critical_endpoint_section(config, *entries, "greedy-critical-endpoint");
    }
    return config;
}

UpstreamWindowConfig upstream_window_config_from_sources(const OptimizerConfigFile* config_file) {
    UpstreamWindowConfig config;
    apply_env_time_budget(config.time_budget);
    std::optional<unsigned int> unused_seed;
    apply_global_overrides(config.time_budget, unused_seed, config_file);
    if (const auto* entries = section_entries(config_file, "greedy-upstream-window");
        entries != nullptr) {
        apply_upstream_window_section(config, *entries, "greedy-upstream-window");
    }
    return config;
}

RepairRecoverConfig repair_recover_config_from_sources(const OptimizerConfigFile* config_file) {
    RepairRecoverConfig config;
    apply_env_time_budget(config.time_budget);
    std::optional<unsigned int> unused_seed;
    apply_global_overrides(config.time_budget, unused_seed, config_file);
    if (const auto* entries = section_entries(config_file, "greedy-repair-recover");
        entries != nullptr) {
        apply_repair_recover_section(config, *entries, "greedy-repair-recover");
    }
    return config;
}

RandomizedRclConfig randomized_rcl_config_from_sources(const OptimizerConfigFile* config_file) {
    RandomizedRclConfig config;
    apply_env_time_budget(config.time_budget);
    std::optional<unsigned int> seed = config.seed;
    apply_global_overrides(config.time_budget, seed, config_file);
    if (seed.has_value()) {
        config.seed = *seed;
    }
    if (const auto* entries = section_entries(config_file, "greedy-randomized-rcl");
        entries != nullptr) {
        apply_randomized_rcl_section(config, *entries, "greedy-randomized-rcl");
    }
    return config;
}

TabuConfig tabu_config_from_sources(const OptimizerConfigFile* config_file) {
    TabuConfig config;
    apply_env_time_budget(config.time_budget);
    std::optional<unsigned int> unused_seed;
    apply_global_overrides(config.time_budget, unused_seed, config_file);
    if (const auto* entries = section_entries(config_file, "tabu"); entries != nullptr) {
        apply_tabu_section(config, *entries, "tabu");
    }
    return config;
}

GreedyConfig greedy_config_from_environment() { return greedy_config_from_sources(nullptr); }

MilpConfig milp_config_from_environment() { return milp_config_from_sources(nullptr); }

SaConfig sa_config_from_environment() { return sa_config_from_sources(nullptr); }

IsaConfig isa_config_from_environment() { return isa_config_from_sources(nullptr); }

CriticalEndpointConfig critical_endpoint_config_from_environment() {
    return critical_endpoint_config_from_sources(nullptr);
}

UpstreamWindowConfig upstream_window_config_from_environment() {
    return upstream_window_config_from_sources(nullptr);
}

RepairRecoverConfig repair_recover_config_from_environment() {
    return repair_recover_config_from_sources(nullptr);
}

RandomizedRclConfig randomized_rcl_config_from_environment() {
    return randomized_rcl_config_from_sources(nullptr);
}

TabuConfig tabu_config_from_environment() { return tabu_config_from_sources(nullptr); }

}  // namespace cadd0040
